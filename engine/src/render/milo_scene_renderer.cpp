// engine/src/render/milo_scene_renderer.cpp — see milo_scene_renderer.h.

#include "render/milo_scene_renderer.h"
#include "render/window_d3d9.h"
#include "render/scene_d3d9.h"   // Mat4
#include "asset/milo_image.h"
#include "ps2_texture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ghogx::render {

namespace {

// Vertex matching the GH2 mesh data: position + normal + diffuse + uv.
struct SVtx {
  float x, y, z;
  float nx, ny, nz;
  D3DCOLOR color;
  float u, v;
};

constexpr DWORD kFVF =
    D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
struct ParticleBillboardVertex {
  float x, y, z;
  D3DCOLOR color;
  float u, v;
};
constexpr DWORD kParticleBillboardFVF =
    D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr DWORD kDefaultSceneAmbient = D3DCOLOR_XRGB(170, 170, 178);
constexpr DWORD kSceneFillLightFirstSlot = 0;
constexpr DWORD kDefaultSceneFillLightSlotCount = 2;
constexpr DWORD kSceneFillLightSlotCount = 4;
constexpr DWORD kAuthoredLightFirstSlot =
    kSceneFillLightFirstSlot + kSceneFillLightSlotCount;
constexpr DWORD kAuthoredLightSlotCount = 4;
constexpr float kMaxAuthoredLightColor = 64.0f;
constexpr float kApproxDirectionalScale = 1.0f;

struct D3DStateCache {
  explicit D3DStateCache(IDirect3DDevice9* device) : dev(device) {}

  void render(D3DRENDERSTATETYPE state, DWORD value) {
    const size_t idx = static_cast<size_t>(state);
    if (idx < render_valid.size() && render_valid[idx] &&
        render_values[idx] == value) {
      return;
    }
    dev->SetRenderState(state, value);
    if (idx < render_valid.size()) {
      render_valid[idx] = true;
      render_values[idx] = value;
    }
  }

  void sampler(DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value) {
    const size_t sampler_idx = static_cast<size_t>(sampler);
    const size_t state_idx = static_cast<size_t>(state);
    if (sampler_idx < sampler_valid.size() &&
        state_idx < sampler_valid[sampler_idx].size() &&
        sampler_valid[sampler_idx][state_idx] &&
        sampler_values[sampler_idx][state_idx] == value) {
      return;
    }
    dev->SetSamplerState(sampler, state, value);
    if (sampler_idx < sampler_valid.size() &&
        state_idx < sampler_valid[sampler_idx].size()) {
      sampler_valid[sampler_idx][state_idx] = true;
      sampler_values[sampler_idx][state_idx] = value;
    }
  }

  void texture_stage(DWORD stage, D3DTEXTURESTAGESTATETYPE state,
                     DWORD value) {
    const size_t stage_idx = static_cast<size_t>(stage);
    const size_t state_idx = static_cast<size_t>(state);
    if (stage_idx < texture_stage_valid.size() &&
        state_idx < texture_stage_valid[stage_idx].size() &&
        texture_stage_valid[stage_idx][state_idx] &&
        texture_stage_values[stage_idx][state_idx] == value) {
      return;
    }
    dev->SetTextureStageState(stage, state, value);
    if (stage_idx < texture_stage_valid.size() &&
        state_idx < texture_stage_valid[stage_idx].size()) {
      texture_stage_valid[stage_idx][state_idx] = true;
      texture_stage_values[stage_idx][state_idx] = value;
    }
  }

  void texture(DWORD stage, IDirect3DBaseTexture9* texture_value) {
    const size_t stage_idx = static_cast<size_t>(stage);
    if (stage_idx < textures_valid.size() && textures_valid[stage_idx] &&
        textures[stage_idx] == texture_value) {
      return;
    }
    dev->SetTexture(stage, texture_value);
    if (stage_idx < textures_valid.size()) {
      textures_valid[stage_idx] = true;
      textures[stage_idx] = texture_value;
    }
  }

  IDirect3DDevice9* dev = nullptr;
  std::array<DWORD, 256> render_values{};
  std::array<bool, 256> render_valid{};
  std::array<std::array<DWORD, 32>, 8> sampler_values{};
  std::array<std::array<bool, 32>, 8> sampler_valid{};
  std::array<std::array<DWORD, 32>, 8> texture_stage_values{};
  std::array<std::array<bool, 32>, 8> texture_stage_valid{};
  std::array<IDirect3DBaseTexture9*, 8> textures{};
  std::array<bool, 8> textures_valid{};
};

std::array<float, 4> average_particle_color(
    const std::array<float, 4>& low,
    const std::array<float, 4>& high) {
  std::array<float, 4> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = (low[i] + high[i]) * 0.5f;
  }
  return out;
}

std::array<float, 4> average_particle_color_from_key(
    const std::array<float, 4>& sampled_low,
    const std::array<float, 4>& base_low,
    const std::array<float, 4>& base_high) {
  std::array<float, 4> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = sampled_low[i] + (base_high[i] - base_low[i]) * 0.5f;
  }
  return out;
}

std::array<float, 4> sample_particle_color_with_mid(
    const std::array<float, 4>& start,
    const std::array<float, 4>& mid,
    const std::array<float, 4>& end,
    float mid_ratio,
    float phase) {
  std::array<float, 4> out{};
  const float clamped_phase = std::clamp(phase, 0.0f, 1.0f);
  const float clamped_mid = std::clamp(mid_ratio, 0.0f, 1.0f);
  if (clamped_mid > 0.0f && clamped_mid < 1.0f) {
    if (clamped_phase <= clamped_mid) {
      const float t = clamped_mid > 0.0f ? clamped_phase / clamped_mid : 1.0f;
      for (size_t i = 0; i < out.size(); ++i) {
        out[i] = start[i] + (mid[i] - start[i]) * t;
      }
    } else {
      const float span = std::max(0.001f, 1.0f - clamped_mid);
      const float t = (clamped_phase - clamped_mid) / span;
      for (size_t i = 0; i < out.size(); ++i) {
        out[i] = mid[i] + (end[i] - mid[i]) * t;
      }
    }
  } else {
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = start[i] + (end[i] - start[i]) * clamped_phase;
    }
  }
  return out;
}

float sample_particle_grow_shrink(float grow_ratio, float shrink_ratio,
                                  float phase) {
  const float grow = std::clamp(grow_ratio, 0.0f, 1.0f);
  const float shrink = std::clamp(shrink_ratio, 0.0f, 1.0f);
  const float clamped_phase = std::clamp(phase, 0.0f, 1.0f);
  if (shrink <= grow) return 1.0f;
  if (grow > 0.0f && clamped_phase < grow) {
    return std::clamp(clamped_phase / grow, 0.0f, 1.0f);
  }
  if (shrink < 1.0f && clamped_phase > shrink) {
    return std::clamp((1.0f - clamped_phase) / (1.0f - shrink), 0.0f, 1.0f);
  }
  return 1.0f;
}

enum MiloBlend : uint8_t {
  kBlendDest = 0,
  kBlendSrc = 1,
  kBlendAdd = 2,
  kBlendSrcAlpha = 3,
  kBlendSrcAlphaAdd = 4,
  kBlendSubtract = 5,
  kBlendMultiply = 6,
};

enum MiloZMode : uint8_t {
  kZModeDisable = 0,
  kZModeNormal = 1,
  kZModeTransparent = 2,
  kZModeForce = 3,
  kZModeDecal = 4,
};

struct BlendState {
  DWORD src = D3DBLEND_SRCALPHA;
  DWORD dest = D3DBLEND_INVSRCALPHA;
  DWORD op = D3DBLENDOP_ADD;
  bool additive = false;
};

struct ApproxLightCandidate {
  std::string ref;
  std::array<float, 3> direction = {0.0f, 0.0f, -1.0f};
  std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 1.0f};
  float score = 0.0f;
};

struct EnvironLightCounts {
  size_t real = 0;
  size_t approx = 0;
};

BlendState blend_state_for(uint8_t blend) {
  switch (blend) {
    case kBlendDest:
      return {D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLENDOP_ADD, false};
    case kBlendSrc:
      return {D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    case kBlendAdd:
      // Milo's PS2 indexed textures decode to straight-alpha RGBA, not
      // premultiplied RGB. Weight the source by its authored texture alpha
      // before adding it to the framebuffer. ONE/ONE exposes RGB stored in
      // fully transparent palette entries (for example GH1 band_shadow.tex's
      // transparent white border) and is only valid for premultiplied input.
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kBlendSrcAlpha:
      return {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
              false};
    case kBlendSrcAlphaAdd:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kBlendSubtract:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT, true};
    case kBlendMultiply:
      return {D3DBLEND_DESTCOLOR, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    default:
      return {};
  }
}

bool image_alpha_is_fully_opaque(const ghogx::asset::Image& image) {
  if (!image.valid()) return false;
  for (size_t i = 3; i < image.rgba.size(); i += 4) {
    if (image.rgba[i] != 0xff) return false;
  }
  return true;
}

std::unordered_set<std::string> source_multimesh_alpha_textures(
    const milo_scene::Scene& scene) {
  std::unordered_map<std::string, const milo_scene::MeshObj*> meshes;
  meshes.reserve(scene.meshes.size());
  for (const auto& mesh : scene.meshes) meshes.emplace(mesh.name, &mesh);

  std::unordered_map<std::string, const milo_scene::MatObj*> materials;
  materials.reserve(scene.mats.size());
  for (const auto& material : scene.mats)
    materials.emplace(material.name, &material);

  std::unordered_set<std::string> textures;
  for (const auto& multi_mesh : scene.multi_meshes) {
    if (!multi_mesh.decoded) continue;
    const auto mesh_it = meshes.find(multi_mesh.mesh);
    if (mesh_it == meshes.end() || !mesh_it->second) continue;
    const auto material_it = materials.find(mesh_it->second->material);
    if (material_it == materials.end() || !material_it->second) continue;
    const auto& material = *material_it->second;
    if (material.diffuse_tex.empty() ||
        (material.blend != kBlendSrcAlpha &&
         material.blend != kBlendSrcAlphaAdd)) {
      continue;
    }
    textures.insert(material.diffuse_tex);
  }
  return textures;
}

std::string read_env_string_raw(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
  std::string result(value);
  std::free(value);
  return result;
}

struct MiloEnvFrameCache {
  MiloEnvFrameCache() {
    values.reserve(48);
  }

  std::string string_value(const char* name) const {
    const auto [it, inserted] = values.emplace(name, std::string{});
    if (inserted) it->second = read_env_string_raw(name);
    return it->second;
  }

  mutable std::unordered_map<std::string, std::string> values;
};

thread_local const MiloEnvFrameCache* active_milo_env_frame_cache = nullptr;

struct ScopedMiloEnvFrameCache {
  explicit ScopedMiloEnvFrameCache(const MiloEnvFrameCache& cache)
      : previous(active_milo_env_frame_cache) {
    active_milo_env_frame_cache = &cache;
  }

  ~ScopedMiloEnvFrameCache() {
    active_milo_env_frame_cache = previous;
  }

  const MiloEnvFrameCache* previous = nullptr;
};

std::string env_cached_string(const char* name) {
  if (active_milo_env_frame_cache) {
    return active_milo_env_frame_cache->string_value(name);
  }
  return read_env_string_raw(name);
}

bool mesh_matches_env_spec(const char* name, const std::string& mesh) {
  const std::string spec = env_cached_string(name);
  if (spec.empty()) return false;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string needle = spec.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!needle.empty() && mesh.find(needle) != std::string::npos) return true;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

DWORD float_to_dword(float value) {
  DWORD out = 0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

int color_channel(float value) {
  const int out =
      static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  return std::clamp(out, 0, 255);
}

D3DCOLOR d3d_color_from_rgba(const std::array<float, 4>& color) {
  return D3DCOLOR_XRGB(color_channel(color[0]), color_channel(color[1]),
                       color_channel(color[2]));
}

void append_debug_float(std::string& out, float value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "|%.3f",
                std::isfinite(value) ? value : 0.0f);
  out += buf;
}

std::string environ_lighting_debug_signature(
    const std::string& environment_name, size_t real_lights,
    size_t approx_lights, const std::array<float, 4>& ambient,
    const std::vector<ApproxLightCandidate>& approx_directional_lights) {
  std::string key = environment_name + "|real=" + std::to_string(real_lights) +
                    "|approx=" + std::to_string(approx_lights) +
                    "|dir=" +
                    std::to_string(approx_directional_lights.size()) +
                    "|ambient";
  for (float value : ambient) append_debug_float(key, value);
  for (const auto& light : approx_directional_lights) {
    key += "|ref=" + light.ref + "|color";
    for (float value : light.color) append_debug_float(key, value);
    key += "|dir";
    for (float value : light.direction) append_debug_float(key, value);
  }
  return key;
}

std::array<float, 3> authored_fake_spot_direction_from_world(
    const std::array<float, 16>& light_world) {
  // RndLight aim follows the Trans -Z axis. The RedOctane band/drummer lights
  // are directional and their -Z rows consistently point down toward the band.
  return {-light_world[8], -light_world[9], -light_world[10]};
}

std::array<float, 3> authored_directional_emission_from_world(
    const std::array<float, 16>& light_world) {
  // GH2 X360 DxEnviron::Select's type-1 helper (0x8230B150) writes
  // -WorldXfm().m.y as its surface-to-light shader vector. D3DLIGHT9::Direction
  // instead stores the direction emitted light travels, so the fixed-function
  // equivalent is the opposite vector: +WorldXfm().m.y.
  return {light_world[4], light_world[5], light_world[6]};
}

bool is_authored_real_environment_light_type(int light_type) {
  // ihatecompvir rb3 RndEnviron::IsValidRealLight classifies only point and
  // fake-spot lights as real environment lights. Directional lights live in
  // mLightsApprox and are handled by the separate approximate-light path.
  return light_type == 0 || light_type == 2;
}

bool is_authored_approx_environment_light_type(int light_type) {
  return light_type == 1;
}

void install_default_scene_fill_lights(IDirect3DDevice9* dev) {
  if (!dev) return;
  auto set_dir_light = [&](DWORD idx, float x, float y, float z, float bright) {
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = bright;
    float ll = std::sqrt(x * x + y * y + z * z);
    light.Direction = {x / ll, y / ll, z / ll};
    dev->SetLight(idx, &light);
    dev->LightEnable(idx, TRUE);
  };
  set_dir_light(kSceneFillLightFirstSlot + 0, 0.3f, 0.5f, -0.8f, 0.55f);
  set_dir_light(kSceneFillLightFirstSlot + 1, -0.4f, -0.6f, -0.5f, 0.30f);
  for (DWORD i = kDefaultSceneFillLightSlotCount;
       i < kSceneFillLightSlotCount; ++i) {
    dev->LightEnable(kSceneFillLightFirstSlot + i, FALSE);
  }
}

void install_approx_scene_lights(
    IDirect3DDevice9* dev,
    std::vector<ApproxLightCandidate>& candidates,
    bool install_defaults_when_empty) {
  if (!dev) return;
  if (candidates.empty()) {
    if (install_defaults_when_empty) {
      install_default_scene_fill_lights(dev);
    } else {
      // A decoded RndEnviron with no approximate lights is an authored
      // ambient-only state.  The renderer's fallback fill lights are only for
      // meshes with no Environ ownership; adding them here double-lights
      // use_environ materials (especially additive venue light pools).
      for (DWORD i = 0; i < kSceneFillLightSlotCount; ++i) {
        dev->LightEnable(kSceneFillLightFirstSlot + i, FALSE);
      }
    }
    return;
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const ApproxLightCandidate& a,
               const ApproxLightCandidate& b) { return a.score > b.score; });
  for (DWORD i = 0; i < kSceneFillLightSlotCount; ++i) {
    dev->LightEnable(kSceneFillLightFirstSlot + i, FALSE);
  }
  const size_t count =
      std::min<size_t>(candidates.size(), kSceneFillLightSlotCount);
  for (size_t i = 0; i < count; ++i) {
    const auto& candidate = candidates[i];
    const float len =
        std::sqrt(candidate.direction[0] * candidate.direction[0] +
                  candidate.direction[1] * candidate.direction[1] +
                  candidate.direction[2] * candidate.direction[2]);
    if (len <= 0.0001f) continue;
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Direction = {candidate.direction[0] / len,
                       candidate.direction[1] / len,
                       candidate.direction[2] / len};
    light.Diffuse.r = std::clamp(candidate.color[0] * kApproxDirectionalScale,
                                 0.0f, kMaxAuthoredLightColor);
    light.Diffuse.g = std::clamp(candidate.color[1] * kApproxDirectionalScale,
                                 0.0f, kMaxAuthoredLightColor);
    light.Diffuse.b = std::clamp(candidate.color[2] * kApproxDirectionalScale,
                                 0.0f, kMaxAuthoredLightColor);
    light.Diffuse.a = std::clamp(candidate.color[3], 0.0f, 1.0f);
    dev->SetLight(kSceneFillLightFirstSlot + static_cast<DWORD>(i), &light);
    dev->LightEnable(kSceneFillLightFirstSlot + static_cast<DWORD>(i), TRUE);
  }
}

void install_scene_fill_lighting(IDirect3DDevice9* dev) {
  if (!dev) return;
  dev->SetRenderState(D3DRS_LIGHTING, TRUE);
  dev->SetRenderState(D3DRS_AMBIENT, kDefaultSceneAmbient);
  dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
  dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
  dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  install_default_scene_fill_lights(dev);
  for (DWORD i = 0; i < kAuthoredLightSlotCount; ++i) {
    dev->LightEnable(kAuthoredLightFirstSlot + i, FALSE);
  }
  D3DMATERIAL9 mtrl{};
  mtrl.Diffuse.r = mtrl.Diffuse.g = mtrl.Diffuse.b = mtrl.Diffuse.a = 1.0f;
  mtrl.Ambient.r = mtrl.Ambient.g = mtrl.Ambient.b = mtrl.Ambient.a = 1.0f;
  dev->SetMaterial(&mtrl);
  dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
  dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
  dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
}

bool fog_values_sane(bool enabled, float start, float end,
                     const std::array<float, 4>& color) {
  if (!enabled) return false;
  if (!std::isfinite(start) || !std::isfinite(end)) return false;
  if (end <= start + 1.0f) return false;
  for (float value : color) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

float hash01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

std::array<float, 16> mul16(const std::array<float, 16>& a,
                            const std::array<float, 16>& b) {
  std::array<float, 16> r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
      r[i * 4 + j] = s;
    }
  return r;
}

std::array<float, 16> identity16() {
  return {1.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f};
}

constexpr uint32_t kConstraintLocalRotate = 1;
constexpr uint32_t kConstraintParentWorld = 2;

std::array<float, 16> apply_transform_constraint(
    const std::array<float, 16>& local,
    const std::array<float, 16>& parent_world,
    uint32_t constraint) {
  if (constraint == kConstraintParentWorld) return parent_world;
  if (constraint == kConstraintLocalRotate) {
    std::array<float, 16> out = local;
    const float x = local[12];
    const float y = local[13];
    const float z = local[14];
    out[12] = x * parent_world[0] + y * parent_world[4] +
              z * parent_world[8] + parent_world[12];
    out[13] = x * parent_world[1] + y * parent_world[5] +
              z * parent_world[9] + parent_world[13];
    out[14] = x * parent_world[2] + y * parent_world[6] +
              z * parent_world[10] + parent_world[14];
    return out;
  }
  return mul16(local, parent_world);
}

std::array<float, 16> affine_inverse16(const std::array<float, 16>& m) {
  const float a = m[0], b = m[1], c = m[2];
  const float d = m[4], e = m[5], f = m[6];
  const float g = m[8], h = m[9], i = m[10];
  const float det =
      a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  const float id = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
  std::array<float, 16> r{};
  r[0] = (e * i - f * h) * id;
  r[1] = (c * h - b * i) * id;
  r[2] = (b * f - c * e) * id;
  r[4] = (f * g - d * i) * id;
  r[5] = (a * i - c * g) * id;
  r[6] = (c * d - a * f) * id;
  r[8] = (d * h - e * g) * id;
  r[9] = (b * g - a * h) * id;
  r[10] = (a * e - b * d) * id;
  const float tx = m[12], ty = m[13], tz = m[14];
  r[12] = -(tx * r[0] + ty * r[4] + tz * r[8]);
  r[13] = -(tx * r[1] + ty * r[5] + tz * r[9]);
  r[14] = -(tx * r[2] + ty * r[6] + tz * r[10]);
  r[15] = 1.0f;
  return r;
}

std::array<float, 3> transform_point16(const std::array<float, 16>& m,
                                       float x, float y, float z) {
  return {x * m[0] + y * m[4] + z * m[8] + m[12],
          x * m[1] + y * m[5] + z * m[9] + m[13],
          x * m[2] + y * m[6] + z * m[10] + m[14]};
}

std::array<float, 3> transform_vector16(const std::array<float, 16>& m,
                                        float x, float y, float z) {
  return {x * m[0] + y * m[4] + z * m[8],
          x * m[1] + y * m[5] + z * m[9],
          x * m[2] + y * m[6] + z * m[10]};
}

void normalize_vector3(std::array<float, 3>& v) {
  const float len =
      std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len <= 1.0e-8f || !std::isfinite(len)) return;
  v[0] /= len;
  v[1] /= len;
  v[2] /= len;
}

std::array<float, 16> xfm_to_mat4(const milo_scene::Xfm& x) {
  return {x.rot[0][0], x.rot[0][1], x.rot[0][2], 0.0f,
          x.rot[1][0], x.rot[1][1], x.rot[1][2], 0.0f,
          x.rot[2][0], x.rot[2][1], x.rot[2][2], 0.0f,
          x.pos[0],    x.pos[1],    x.pos[2],    1.0f};
}

struct SceneNodeXfm {
  std::array<float, 16> local = identity16();
  std::array<float, 16> world_stored = identity16();
  bool world_xfm_override = false;
  std::string parent;
  uint32_t constraint = 0;
};

bool scene_node_xfm_for(const milo_scene::Scene& scene,
                        const std::string& name, SceneNodeXfm& out) {
  for (const auto& mesh : scene.meshes) {
    if (mesh.name != name) continue;
    out.local = xfm_to_mat4(mesh.local);
    out.world_stored = xfm_to_mat4(mesh.world_stored);
    out.world_xfm_override = mesh.world_xfm_override;
    out.parent = mesh.parent;
    out.constraint = mesh.constraint;
    return true;
  }
  for (const auto& trans : scene.transes) {
    if (trans.name != name) continue;
    out.local = xfm_to_mat4(trans.local);
    out.world_stored = xfm_to_mat4(trans.world_stored);
    out.world_xfm_override = trans.world_xfm_override;
    out.parent = trans.parent;
    out.constraint = trans.constraint;
    return true;
  }
  for (const auto& group : scene.groups) {
    if (group.name != name) continue;
    out.local = group.has_transform ? xfm_to_mat4(group.local) : identity16();
    out.world_stored =
        group.has_transform ? xfm_to_mat4(group.world_stored) : identity16();
    out.world_xfm_override = group.world_xfm_override;
    out.parent = group.parent;
    out.constraint = group.has_transform ? group.constraint : 0;
    return true;
  }
  for (const auto& placer : scene.band_placers) {
    if (placer.name != name || !placer.decoded) continue;
    out.local = xfm_to_mat4(placer.local);
    out.world_stored = xfm_to_mat4(placer.world_stored);
    out.parent = placer.parent;
    out.constraint = 0;
    return true;
  }
  for (const auto& camera : scene.cams) {
    if (camera.name != name || !camera.decoded) continue;
    out.local = xfm_to_mat4(camera.local);
    out.world_stored = xfm_to_mat4(camera.world_stored);
    out.parent = camera.parent;
    out.constraint = camera.constraint;
    return true;
  }
  return false;
}

bool scene_node_world_for(const milo_scene::Scene& scene,
                          const std::string& name,
                          std::array<float, 16>& out,
                          int guard = 0) {
  if (guard >= 64) return false;
  SceneNodeXfm node;
  if (!scene_node_xfm_for(scene, name, node)) return false;
  if (node.world_xfm_override) {
    out = node.world_stored;
    return true;
  }
  if (node.parent.empty() || node.parent == name) {
    out = node.local;
    return true;
  }
  std::array<float, 16> parent_world{};
  if (!scene_node_world_for(scene, node.parent, parent_world, guard + 1))
    return false;
  out = apply_transform_constraint(node.local, parent_world, node.constraint);
  return true;
}

void apply_local_translation_delta(std::array<float, 16>& world,
                                   const float delta[3]) {
  const float dx = delta[0] * world[0] + delta[1] * world[4] +
                   delta[2] * world[8];
  const float dy = delta[0] * world[1] + delta[1] * world[5] +
                   delta[2] * world[9];
  const float dz = delta[0] * world[2] + delta[1] * world[6] +
                   delta[2] * world[10];
  world[12] += dx;
  world[13] += dy;
  world[14] += dz;
}

std::array<float, 4> normalize_quat_xyzw(std::array<float, 4> q) {
  const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] +
                              q[2] * q[2] + q[3] * q[3]);
  if (!std::isfinite(len) || len <= 0.000001f)
    return {0.0f, 0.0f, 0.0f, 1.0f};
  const float inv = 1.0f / len;
  for (float& v : q) v *= inv;
  return q;
}

void quat_xyzw_to_row_rot(const std::array<float, 4>& q_in,
                          float rot[3][3]) {
  const auto q = normalize_quat_xyzw(q_in);
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  rot[0][0] = 1.0f - 2.0f * (y * y + z * z);
  rot[0][1] = 2.0f * (x * y + z * w);
  rot[0][2] = 2.0f * (x * z - y * w);
  rot[1][0] = 2.0f * (x * y - z * w);
  rot[1][1] = 1.0f - 2.0f * (x * x + z * z);
  rot[1][2] = 2.0f * (y * z + x * w);
  rot[2][0] = 2.0f * (x * z + y * w);
  rot[2][1] = 2.0f * (y * z - x * w);
  rot[2][2] = 1.0f - 2.0f * (x * x + y * y);
}

void apply_local_rotation_delta(std::array<float, 16>& world,
                                const std::array<float, 4>& quat_xyzw) {
  float rot[3][3];
  quat_xyzw_to_row_rot(quat_xyzw, rot);
  std::array<float, 9> basis = {world[0], world[1], world[2],
                                world[4], world[5], world[6],
                                world[8], world[9], world[10]};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      world[r * 4 + c] =
          basis[r * 3 + 0] * rot[0][c] +
          basis[r * 3 + 1] * rot[1][c] +
          basis[r * 3 + 2] * rot[2][c];
    }
  }
}

void apply_local_scale_delta(std::array<float, 16>& world,
                             const std::array<float, 3>& scale) {
  for (int c = 0; c < 3; ++c) world[c] *= scale[0];
  for (int c = 0; c < 3; ++c) world[4 + c] *= scale[1];
  for (int c = 0; c < 3; ++c) world[8 + c] *= scale[2];
}

std::array<float, 3> local_row_scales(const std::array<float, 16>& local) {
  std::array<float, 3> scale = {
      std::sqrt(local[0] * local[0] + local[1] * local[1] +
                local[2] * local[2]),
      std::sqrt(local[4] * local[4] + local[5] * local[5] +
                local[6] * local[6]),
      std::sqrt(local[8] * local[8] + local[9] * local[9] +
                local[10] * local[10]),
  };
  const float cross01[3] = {
      local[1] * local[6] - local[2] * local[5],
      local[2] * local[4] - local[0] * local[6],
      local[0] * local[5] - local[1] * local[4],
  };
  const float det_sign =
      cross01[0] * local[8] + cross01[1] * local[9] +
      cross01[2] * local[10];
  if (det_sign < 0.0f) scale[2] = -scale[2];
  return scale;
}

float local_basis_determinant(const std::array<float, 16>& local) {
  const float cross01[3] = {
      local[1] * local[6] - local[2] * local[5],
      local[2] * local[4] - local[0] * local[6],
      local[0] * local[5] - local[1] * local[4],
  };
  return cross01[0] * local[8] + cross01[1] * local[9] +
         cross01[2] * local[10];
}

void normalize_row3(float row[3], const float fallback[3]) {
  const float len =
      std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
  if (std::isfinite(len) && len > 0.000001f) {
    const float inv = 1.0f / len;
    row[0] *= inv;
    row[1] *= inv;
    row[2] *= inv;
    return;
  }
  row[0] = fallback[0];
  row[1] = fallback[1];
  row[2] = fallback[2];
}

void source_normalized_rows(const std::array<float, 16>& local,
                            float rot[3][3]) {
  rot[1][0] = local[4];
  rot[1][1] = local[5];
  rot[1][2] = local[6];
  const float y_fallback[3] = {0.0f, 1.0f, 0.0f};
  normalize_row3(rot[1], y_fallback);

  const float src_z[3] = {local[8], local[9], local[10]};
  rot[0][0] = rot[1][1] * src_z[2] - rot[1][2] * src_z[1];
  rot[0][1] = rot[1][2] * src_z[0] - rot[1][0] * src_z[2];
  rot[0][2] = rot[1][0] * src_z[1] - rot[1][1] * src_z[0];
  const float x_fallback[3] = {1.0f, 0.0f, 0.0f};
  normalize_row3(rot[0], x_fallback);

  rot[2][0] = rot[0][1] * rot[1][2] - rot[0][2] * rot[1][1];
  rot[2][1] = rot[0][2] * rot[1][0] - rot[0][0] * rot[1][2];
  rot[2][2] = rot[0][0] * rot[1][1] - rot[0][1] * rot[1][0];
}

std::array<float, 4> quat_xyzw_from_row_rot(const float rot[3][3]) {
  std::array<float, 4> q{};
  const float row0x = rot[0][0];
  const float row1y = rot[1][1];
  const float row2z = rot[2][2];
  const float diag = row0x + row1y + row2z;
  if (diag > 0.0f) {
    q[3] = diag + 1.0f;
    q[0] = rot[1][2] - rot[2][1];
    q[1] = rot[2][0] - rot[0][2];
    q[2] = rot[0][1] - rot[1][0];
  } else if (row2z > row0x && row2z > row1y) {
    q[2] = row2z - row0x - row1y + 1.0f;
    q[3] = rot[0][1] - rot[1][0];
    q[0] = rot[2][0] + rot[0][2];
    q[1] = rot[2][1] + rot[1][2];
  } else if (row1y > row0x) {
    q[1] = row1y - row2z - row0x + 1.0f;
    q[3] = rot[2][0] - rot[0][2];
    q[2] = rot[1][2] + rot[2][1];
    q[0] = rot[1][0] + rot[0][1];
  } else {
    q[0] = row0x - row1y - row2z + 1.0f;
    q[3] = rot[1][2] - rot[2][1];
    q[1] = rot[0][1] + rot[1][0];
    q[2] = rot[0][2] + rot[2][0];
  }
  return normalize_quat_xyzw(q);
}

void mul_row_rot3(const float a[3][3], const float b[3][3], float out[3][3]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r][c] =
          a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
    }
  }
}

void transpose_row_rot3(const float in[3][3], float out[3][3]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) out[r][c] = in[c][r];
  }
}

std::array<float, 4> fast_interp_quat_xyzw(std::array<float, 4> a,
                                           std::array<float, 4> b,
                                           float t);

float finite_or(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

void apply_absolute_local_rot_scale(
    std::array<float, 16>& local,
    const MiloSceneRenderer::MeshTransformSample& sample) {
  float rot[3][3];
  if (sample.has_rotation && sample.rotation_is_absolute) {
    quat_xyzw_to_row_rot(sample.rotation_xyzw, rot);
  } else {
    source_normalized_rows(local, rot);
  }

  std::array<float, 3> scale = local_row_scales(local);
  if (sample.has_scale && sample.scale_is_absolute) {
    for (int i = 0; i < 3; ++i) {
      scale[i] = finite_or(sample.scale[i], scale[i]);
    }
  }

  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      local[r * 4 + c] = rot[r][c] * scale[r];
    }
  }
}

std::array<float, 4> slerp_quat_xyzw(std::array<float, 4> a,
                                    std::array<float, 4> b, float t);

void apply_mesh_transform_sample_full(
    std::array<float, 16>& world,
    const MiloSceneRenderer::MeshTransformSample& sample) {
  if (sample.has_translation) {
    if (sample.translation_is_absolute) {
      world[12] = sample.translation[0];
      world[13] = sample.translation[1];
      world[14] = sample.translation[2];
    } else {
      apply_local_translation_delta(world, sample.translation.data());
    }
  }
  const bool has_absolute_rot_scale =
      (sample.has_rotation && sample.rotation_is_absolute) ||
      (sample.has_scale && sample.scale_is_absolute);
  if (has_absolute_rot_scale) apply_absolute_local_rot_scale(world, sample);
  if (sample.has_rotation && !sample.rotation_is_absolute)
    apply_local_rotation_delta(world, sample.rotation_xyzw);
  if (sample.has_scale && !sample.scale_is_absolute)
    apply_local_scale_delta(world, sample.scale);
}

void apply_mesh_transform_sample(
    std::array<float, 16>& world,
    const MiloSceneRenderer::MeshTransformSample& sample) {
  if (sample.has_local_transform) {
    world = sample.local_transform;
    return;
  }
  const float blend = std::isfinite(sample.blend)
                          ? std::clamp(sample.blend, 0.0f, 1.0f)
                          : 1.0f;
  if (blend <= 0.0f) return;
  if (blend >= 0.9999f) {
    apply_mesh_transform_sample_full(world, sample);
    return;
  }

  auto blended = sample;
  blended.blend = 1.0f;
  if (sample.has_translation) {
    if (sample.translation_is_absolute) {
      for (int axis = 0; axis < 3; ++axis) {
        const float base = world[12 + axis];
        blended.translation[axis] =
            base + (sample.translation[axis] - base) * blend;
      }
    } else {
      for (int axis = 0; axis < 3; ++axis)
        blended.translation[axis] = sample.translation[axis] * blend;
    }
  }
  if (sample.has_rotation) {
    if (sample.rotation_is_absolute) {
      float base_rot[3][3];
      source_normalized_rows(world, base_rot);
      const auto base_quat = quat_xyzw_from_row_rot(base_rot);
      blended.rotation_xyzw =
          sample.rotation_slerp
              ? slerp_quat_xyzw(base_quat, sample.rotation_xyzw, blend)
              : fast_interp_quat_xyzw(base_quat, sample.rotation_xyzw, blend);
    } else {
      blended.rotation_xyzw =
          fast_interp_quat_xyzw({0.0f, 0.0f, 0.0f, 1.0f},
                                sample.rotation_xyzw, blend);
    }
  }
  if (sample.has_scale) {
    if (sample.scale_is_absolute) {
      const auto base_scale = local_row_scales(world);
      for (int axis = 0; axis < 3; ++axis) {
        blended.scale[axis] =
            base_scale[axis] + (sample.scale[axis] - base_scale[axis]) * blend;
      }
    } else {
      for (int axis = 0; axis < 3; ++axis)
        blended.scale[axis] = 1.0f + (sample.scale[axis] - 1.0f) * blend;
    }
  }
  apply_mesh_transform_sample_full(world, blended);
}

void apply_face_camera_yaw(std::array<float, 16>& world,
                           const milo_scene::MeshObj& mesh,
                           const float eye[3]) {
  float normal[3] = {0.0f, 0.0f, 0.0f};
  for (const auto& v : mesh.verts) {
    normal[0] += v.nx;
    normal[1] += v.ny;
    normal[2] += v.nz;
  }
  const float normal_len =
      std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                normal[2] * normal[2]);
  if (normal_len > 0.000001f) {
    const float inv_len = 1.0f / normal_len;
    normal[0] *= inv_len;
    normal[1] *= inv_len;
    normal[2] *= inv_len;
  } else {
    normal[0] = 0.0f;
    normal[1] = 1.0f;
    normal[2] = 0.0f;
  }
  float current[3] = {
      normal[0] * world[0] + normal[1] * world[4] + normal[2] * world[8],
      normal[0] * world[1] + normal[1] * world[5] + normal[2] * world[9],
      normal[0] * world[2] + normal[1] * world[6] + normal[2] * world[10],
  };
  current[2] = 0.0f;
  const float current_len =
      std::sqrt(current[0] * current[0] + current[1] * current[1]);
  if (current_len <= 0.000001f) return;
  current[0] /= current_len;
  current[1] /= current_len;

  float desired[3] = {eye[0] - world[12], eye[1] - world[13], 0.0f};
  const float desired_len =
      std::sqrt(desired[0] * desired[0] + desired[1] * desired[1]);
  if (desired_len <= 0.000001f) return;
  desired[0] /= desired_len;
  desired[1] /= desired_len;

  const float cross = current[0] * desired[1] - current[1] * desired[0];
  const float dot = std::clamp(current[0] * desired[0] +
                                   current[1] * desired[1],
                               -1.0f, 1.0f);
  const float angle = std::atan2(cross, dot);
  if (!std::isfinite(angle) || std::fabs(angle) <= 0.000001f) return;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  for (int r = 0; r < 3; ++r) {
    const int base = r * 4;
    const float x = world[base + 0];
    const float y = world[base + 1];
    world[base + 0] = x * c - y * s;
    world[base + 1] = x * s + y * c;
  }
}

struct VecKeySample {
  size_t a = 0;
  size_t b = 0;
  float t = 0.0f;
};

VecKeySample sample_vec_key(
    const std::vector<MiloSceneRenderer::MeshAnimKey>& keys, float frame) {
  VecKeySample sample;
  if (keys.empty()) return sample;
  sample.a = 0;
  sample.b = keys.size() - 1;
  if (keys.size() == 1 || frame < keys.front().frame) {
    sample.b = sample.a;
    return sample;
  }
  if (frame >= keys.back().frame) {
    sample.a = sample.b;
    return sample;
  }
  for (size_t i = 1; i < keys.size(); ++i) {
    if (frame <= keys[i].frame) {
      sample.a = i - 1;
      sample.b = i;
      break;
    }
  }
  const auto& a = keys[sample.a];
  const auto& b = keys[sample.b];
  const float span = std::max(b.frame - a.frame, 0.001f);
  sample.t = std::clamp((frame - a.frame) / span, 0.0f, 1.0f);
  return sample;
}

std::array<float, 3> spline_tangent(
    const std::vector<MiloSceneRenderer::MeshAnimKey>& keys, size_t index) {
  std::array<float, 3> out = {0.0f, 0.0f, 0.0f};
  if (keys.size() < 2) return out;
  auto diff = [&](size_t lhs, size_t rhs) {
    return std::array<float, 3>{
        keys[lhs].pos[0] - keys[rhs].pos[0],
        keys[lhs].pos[1] - keys[rhs].pos[1],
        keys[lhs].pos[2] - keys[rhs].pos[2]};
  };
  if (keys.size() == 2) {
    return diff(1, 0);
  }
  if (index == 0) {
    const auto a = diff(1, 0);
    const auto b = diff(2, 0);
    for (int axis = 0; axis < 3; ++axis) out[axis] = a[axis] * 1.5f - b[axis] * 0.25f;
    return out;
  }
  if (index >= keys.size() - 1) {
    const auto a = diff(keys.size() - 1, keys.size() - 2);
    const auto b = diff(keys.size() - 1, keys.size() - 3);
    for (int axis = 0; axis < 3; ++axis) out[axis] = a[axis] * 1.5f - b[axis] * 0.25f;
    return out;
  }
  const auto a = diff(index + 1, index - 1);
  for (int axis = 0; axis < 3; ++axis) out[axis] = a[axis] * 0.5f;
  return out;
}

std::array<float, 3> sample_vec_value(
    const std::vector<MiloSceneRenderer::MeshAnimKey>& keys, float frame,
    bool spline) {
  std::array<float, 3> out = {0.0f, 0.0f, 0.0f};
  if (keys.empty()) return out;
  const VecKeySample sample = sample_vec_key(keys, frame);
  const auto& a = keys[sample.a];
  const auto& b = keys[sample.b];
  if (!spline || keys.size() < 3 || sample.a == sample.b) {
    for (int axis = 0; axis < 3; ++axis)
      out[axis] = a.pos[axis] + (b.pos[axis] - a.pos[axis]) * sample.t;
    return out;
  }
  const auto ta = spline_tangent(keys, sample.a);
  const auto tb = spline_tangent(keys, sample.b);
  const float t = sample.t;
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;
  for (int axis = 0; axis < 3; ++axis) {
    out[axis] = a.pos[axis] * h00 + ta[axis] * h10 +
                b.pos[axis] * h01 + tb[axis] * h11;
  }
  return out;
}

float repeat_translation_sample_frame(
    const std::vector<MiloSceneRenderer::MeshAnimKey>& keys, float frame,
    bool repeat, std::array<float, 3>& repeat_offset) {
  repeat_offset = {0.0f, 0.0f, 0.0f};
  if (!repeat || keys.size() < 2 || !std::isfinite(frame)) return frame;
  const float first_frame = keys.front().frame;
  const float last_frame = keys.back().frame;
  const float span = last_frame - first_frame;
  if (!std::isfinite(span) || span <= 0.001f || frame < last_frame) {
    return frame;
  }
  const float cycles = std::floor((frame - first_frame) / span);
  if (!std::isfinite(cycles) || cycles <= 0.0f) return frame;
  for (int axis = 0; axis < 3; ++axis) {
    repeat_offset[axis] =
        (keys.back().pos[axis] - keys.front().pos[axis]) * cycles;
  }
  return frame - cycles * span;
}

std::array<float, 4> slerp_quat_xyzw(std::array<float, 4> a,
                                     std::array<float, 4> b, float t) {
  a = normalize_quat_xyzw(a);
  b = normalize_quat_xyzw(b);
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0f) {
    for (float& v : b) v = -v;
    dot = -dot;
  }
  if (dot > 0.9995f) {
    std::array<float, 4> out = {
        a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t};
    return normalize_quat_xyzw(out);
  }
  dot = std::clamp(dot, -1.0f, 1.0f);
  const float theta0 = std::acos(dot);
  const float theta = theta0 * t;
  const float sin_theta = std::sin(theta);
  const float sin_theta0 = std::sin(theta0);
  const float s0 = std::cos(theta) - dot * sin_theta / sin_theta0;
  const float s1 = sin_theta / sin_theta0;
  return normalize_quat_xyzw({a[0] * s0 + b[0] * s1,
                              a[1] * s0 + b[1] * s1,
                              a[2] * s0 + b[2] * s1,
                              a[3] * s0 + b[3] * s1});
}

std::array<float, 4> fast_interp_quat_xyzw(std::array<float, 4> a,
                                           std::array<float, 4> b,
                                           float t) {
  a = normalize_quat_xyzw(a);
  b = normalize_quat_xyzw(b);
  const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0f) {
    for (float& v : b) v = -v;
  }
  return normalize_quat_xyzw({a[0] + (b[0] - a[0]) * t,
                              a[1] + (b[1] - a[1]) * t,
                              a[2] + (b[2] - a[2]) * t,
                              a[3] + (b[3] - a[3]) * t});
}

std::array<float, 4> sample_rotation_value(
    const std::vector<MiloSceneRenderer::MeshQuatAnimKey>& keys, float frame,
    bool slerp) {
  if (keys.empty()) return {0.0f, 0.0f, 0.0f, 1.0f};
  const auto* a = &keys.front();
  const auto* b = &keys.back();
  for (size_t i = 1; i < keys.size(); ++i) {
    if (frame <= keys[i].frame) {
      a = &keys[i - 1];
      b = &keys[i];
      break;
    }
  }
  const float span = std::max(b->frame - a->frame, 0.001f);
  const float t = std::clamp((frame - a->frame) / span, 0.0f, 1.0f);
  const std::array<float, 4> qa = {a->quat_xyzw[0], a->quat_xyzw[1],
                                   a->quat_xyzw[2], a->quat_xyzw[3]};
  const std::array<float, 4> qb = {b->quat_xyzw[0], b->quat_xyzw[1],
                                   b->quat_xyzw[2], b->quat_xyzw[3]};
  return slerp ? slerp_quat_xyzw(qa, qb, t)
               : fast_interp_quat_xyzw(qa, qb, t);
}

MiloSceneRenderer::MeshTransformSample sample_transform_anim(
    const MiloSceneRenderer::MeshTransformAnim& anim, float frame) {
  MiloSceneRenderer::MeshTransformSample sample;
  sample.has_source_frame = true;
  sample.source_frame = frame;
  sample.rotation_slerp = anim.rotation_slerp;
  if (!anim.translation_keys.empty()) {
    sample.has_translation = true;
    sample.translation_is_absolute = true;
    std::array<float, 3> repeat_offset;
    const float sample_frame = repeat_translation_sample_frame(
        anim.translation_keys, frame, anim.translation_repeat, repeat_offset);
    sample.translation = sample_vec_value(
        anim.translation_keys, sample_frame, anim.translation_spline);
    for (int axis = 0; axis < 3; ++axis) {
      sample.translation[axis] += repeat_offset[axis];
    }
  }
  if (!anim.rotation_keys.empty()) {
    sample.has_rotation = true;
    // RndTransAnim/PropKeys rebuild local rotation from the sampled quat and
    // reapply MakeScale-signed rows; venue rotations are not first-key deltas.
    sample.rotation_is_absolute = true;
    sample.rotation_xyzw =
        sample_rotation_value(anim.rotation_keys, frame, anim.rotation_slerp);
  }
  if (!anim.scale_keys.empty()) {
    sample.has_scale = true;
    sample.scale_is_absolute = true;
    sample.scale =
        sample_vec_value(anim.scale_keys, frame, anim.scale_spline);
  }
  return sample;
}

bool env_enabled(const char* name) {
  const std::string value = env_cached_string(name);
  return !value.empty() && value[0] != '0';
}

bool env_mesh_filter_matches(const char* name, const std::string& mesh_name) {
  const std::string filter = env_cached_string(name);
  if (filter.empty() || filter == "0") return false;
  if (filter == "1" || filter == "true" || filter == "TRUE") return true;
  return mesh_name.find(filter) != std::string::npos;
}

float env_float_or(const char* name, float fallback, float min_value,
                   float max_value) {
  const std::string value = env_cached_string(name);
  if (value.empty()) return fallback;
  char* end = nullptr;
  const char* text = value.c_str();
  const float parsed = std::strtof(text, &end);
  if (end == text || !std::isfinite(parsed)) return fallback;
  if (parsed < min_value || parsed > max_value) return fallback;
  return parsed;
}

float env_camera_aspect_preset_or(const char* name, float fallback) {
  const std::string value = env_cached_string(name);
  if (value.empty()) return fallback;

  constexpr float kAspect4x3 = 4.0f / 3.0f;
  constexpr float kAspect16x9 = 16.0f / 9.0f;
  constexpr float kAspectEpsilon = 0.002f;
  float aspect = fallback;
  if (value == "4:3") {
    aspect = kAspect4x3;
  } else if (value == "16:9") {
    aspect = kAspect16x9;
  } else {
    char* end = nullptr;
    const char* text = value.c_str();
    const float parsed = std::strtof(text, &end);
    if (end != text && std::isfinite(parsed)) {
      if (std::fabs(parsed - kAspect4x3) <= kAspectEpsilon) {
        aspect = kAspect4x3;
      } else if (std::fabs(parsed - kAspect16x9) <= kAspectEpsilon) {
        aspect = kAspect16x9;
      }
    }
  }
  return aspect;
}

size_t mesh_anim_log_stride() {
  return static_cast<size_t>(
      std::max(1.0f, env_float_or("GHOGX_LOG_MESH_ANIM_STRIDE", 30.0f, 1.0f,
                                  100000.0f)));
}

bool should_log_mesh_anim_sample(size_t sample) {
  const size_t stride = mesh_anim_log_stride();
  return sample == 1 || stride <= 1 || sample % stride == 0;
}

void normalize3(float v[3]) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len <= 1e-6f) return;
  v[0] /= len;
  v[1] /= len;
  v[2] /= len;
}

float row_len16(const std::array<float, 16>& m, size_t row) {
  const size_t i = row * 4;
  return std::sqrt(m[i + 0] * m[i + 0] + m[i + 1] * m[i + 1] +
                   m[i + 2] * m[i + 2]);
}

int dominant_abs_axis(const std::array<float, 3>& v) {
  int axis = 0;
  float best = std::fabs(v[0]);
  for (int i = 1; i < 3; ++i) {
    const float value = std::fabs(v[i]);
    if (value > best) {
      best = value;
      axis = i;
    }
  }
  return axis;
}

int smallest_axis(const std::array<float, 3>& v) {
  int axis = 0;
  float best = std::fabs(v[0]);
  for (int i = 1; i < 3; ++i) {
    const float value = std::fabs(v[i]);
    if (value < best) {
      best = value;
      axis = i;
    }
  }
  return axis;
}

const char* axis_label(int axis) {
  switch (axis) {
    case 0:
      return "x";
    case 1:
      return "y";
    case 2:
      return "z";
    default:
      return "-";
  }
}

std::array<float, 3> normalized3(std::array<float, 3> v) {
  const float len =
      std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!std::isfinite(len) || len <= 0.000001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  const float inv = 1.0f / len;
  for (float& value : v) value *= inv;
  return v;
}

std::array<float, 3> transform_local_dir(
    const std::array<float, 16>& m, const std::array<float, 3>& dir) {
  return normalized3({
      dir[0] * m[0] + dir[1] * m[4] + dir[2] * m[8],
      dir[0] * m[1] + dir[1] * m[5] + dir[2] * m[9],
      dir[0] * m[2] + dir[1] * m[6] + dir[2] * m[10],
  });
}

struct MeshLocalAxisDiagnostics {
  bool valid = false;
  std::array<float, 3> extent = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> triangle_normal_abs = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> face_dir = {0.0f, 0.0f, 1.0f};
  int thin_axis = 2;
  int face_axis = 2;
};

MeshLocalAxisDiagnostics mesh_local_axis_diagnostics(
    const milo_scene::MeshObj& mesh) {
  MeshLocalAxisDiagnostics out;
  if (!mesh.decoded || mesh.verts.empty()) return out;
  out.valid = true;
  float mn[3] = {mesh.verts.front().px, mesh.verts.front().py,
                 mesh.verts.front().pz};
  float mx[3] = {mn[0], mn[1], mn[2]};
  std::array<float, 3> vertex_normal_sum = {0.0f, 0.0f, 0.0f};
  for (const auto& v : mesh.verts) {
    mn[0] = std::min(mn[0], v.px);
    mn[1] = std::min(mn[1], v.py);
    mn[2] = std::min(mn[2], v.pz);
    mx[0] = std::max(mx[0], v.px);
    mx[1] = std::max(mx[1], v.py);
    mx[2] = std::max(mx[2], v.pz);
    vertex_normal_sum[0] += v.nx;
    vertex_normal_sum[1] += v.ny;
    vertex_normal_sum[2] += v.nz;
  }
  out.extent = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
  std::array<float, 3> triangle_normal_sum = {0.0f, 0.0f, 0.0f};
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const uint16_t ia = mesh.indices[i + 0];
    const uint16_t ib = mesh.indices[i + 1];
    const uint16_t ic = mesh.indices[i + 2];
    if (ia >= mesh.verts.size() || ib >= mesh.verts.size() ||
        ic >= mesh.verts.size()) {
      continue;
    }
    const auto& a = mesh.verts[ia];
    const auto& b = mesh.verts[ib];
    const auto& c = mesh.verts[ic];
    const float e1[3] = {b.px - a.px, b.py - a.py, b.pz - a.pz};
    const float e2[3] = {c.px - a.px, c.py - a.py, c.pz - a.pz};
    const std::array<float, 3> n = {
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
    };
    triangle_normal_sum[0] += n[0];
    triangle_normal_sum[1] += n[1];
    triangle_normal_sum[2] += n[2];
    out.triangle_normal_abs[0] += std::fabs(n[0]);
    out.triangle_normal_abs[1] += std::fabs(n[1]);
    out.triangle_normal_abs[2] += std::fabs(n[2]);
  }
  out.thin_axis = smallest_axis(out.extent);
  out.face_axis = dominant_abs_axis(out.triangle_normal_abs);
  float sign_source = triangle_normal_sum[out.face_axis];
  if (std::fabs(sign_source) <= 0.000001f) {
    sign_source = vertex_normal_sum[out.face_axis];
  }
  out.face_dir = {0.0f, 0.0f, 0.0f};
  out.face_dir[out.face_axis] = sign_source < 0.0f ? -1.0f : 1.0f;
  return out;
}

void log_mesh_anim_local_rows(
    const std::string& mesh_name, const std::string& target_name,
    const std::string& target_kind, const std::string& target_parent,
    const std::array<float, 16>& base_local,
    const std::array<float, 16>& sampled_local,
    const MiloSceneRenderer::MeshTransformSample* applied_sample,
    float active_frame, bool has_offset, bool has_active_anim) {
  static std::unordered_map<std::string, size_t> logged_local_rows;
  const std::string key = mesh_name + "|" + target_name;
  const size_t sample = ++logged_local_rows[key];
  if (!should_log_mesh_anim_sample(sample)) return;
  const auto base_scale = local_row_scales(base_local);
  const auto sampled_scale = local_row_scales(sampled_local);
  const float base_det = local_basis_determinant(base_local);
  const float sampled_det = local_basis_determinant(sampled_local);
  float source_base_rot[3][3];
  float source_sampled_rot[3][3];
  source_normalized_rows(base_local, source_base_rot);
  source_normalized_rows(sampled_local, source_sampled_rot);
  float source_base_inv_rot[3][3];
  float recompose_delta_rot[3][3];
  float local_delta_rot[3][3];
  transpose_row_rot3(source_base_rot, source_base_inv_rot);
  mul_row_rot3(source_sampled_rot, source_base_inv_rot, recompose_delta_rot);
  mul_row_rot3(source_base_inv_rot, source_sampled_rot, local_delta_rot);
  const int has_pos =
      applied_sample && applied_sample->has_translation ? 1 : 0;
  const int abs_pos =
      applied_sample && applied_sample->translation_is_absolute ? 1 : 0;
  const int has_rot = applied_sample && applied_sample->has_rotation ? 1 : 0;
  const int abs_rot =
      applied_sample && applied_sample->rotation_is_absolute ? 1 : 0;
  const int has_scale = applied_sample && applied_sample->has_scale ? 1 : 0;
  const int abs_scale =
      applied_sample && applied_sample->scale_is_absolute ? 1 : 0;
  const int has_source_frame =
      applied_sample && applied_sample->has_source_frame ? 1 : 0;
  const float source_frame =
      has_source_frame ? applied_sample->source_frame : active_frame;
  const std::array<float, 4> quat =
      applied_sample ? applied_sample->rotation_xyzw
                    : std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 3> sample_axis =
      has_rot ? normalized3({quat[0], quat[1], quat[2]})
              : std::array<float, 3>{0.0f, 0.0f, 0.0f};
  const bool sample_axis_valid =
      std::fabs(sample_axis[0]) + std::fabs(sample_axis[1]) +
          std::fabs(sample_axis[2]) >
      0.000001f;
  const std::array<float, 4> recompose_delta_quat =
      quat_xyzw_from_row_rot(recompose_delta_rot);
  const std::array<float, 3> recompose_delta_axis =
      has_rot ? normalized3({recompose_delta_quat[0],
                             recompose_delta_quat[1],
                             recompose_delta_quat[2]})
              : std::array<float, 3>{0.0f, 0.0f, 0.0f};
  const bool recompose_delta_axis_valid =
      std::fabs(recompose_delta_axis[0]) +
          std::fabs(recompose_delta_axis[1]) +
          std::fabs(recompose_delta_axis[2]) >
      0.000001f;
  const std::array<float, 4> local_delta_quat =
      quat_xyzw_from_row_rot(local_delta_rot);
  const std::array<float, 3> local_delta_axis =
      has_rot ? normalized3({local_delta_quat[0], local_delta_quat[1],
                             local_delta_quat[2]})
              : std::array<float, 3>{0.0f, 0.0f, 0.0f};
  const bool local_delta_axis_valid =
      std::fabs(local_delta_axis[0]) + std::fabs(local_delta_axis[1]) +
          std::fabs(local_delta_axis[2]) >
      0.000001f;
  const std::array<float, 3> scale =
      applied_sample ? applied_sample->scale
                    : std::array<float, 3>{1.0f, 1.0f, 1.0f};
  std::fprintf(
      stderr,
      "[milo_scene] mesh_anim_local mesh=%s target=%s kind=%s parent=%s "
      "sample=%zu "
      "offset=%d active=%d frame=%.3f source_frame=%d:%.3f "
      "sample_pos=%d:%d sample_rot=%d:%d "
      "quat=(%.6f %.6f %.6f %.6f) "
      "sample_axis=(%.6f %.6f %.6f) sample_axis_dom=%s "
      "recompose_delta_axis=(%.6f %.6f %.6f) "
      "recompose_delta_axis_dom=%s "
      "local_delta_axis=(%.6f %.6f %.6f) local_delta_axis_dom=%s "
      "sample_scale=%d:%d "
      "scale_vec=(%.6f %.6f %.6f) base_scale=(%.6f %.6f %.6f) "
      "sampled_scale=(%.6f %.6f %.6f) "
      "base_det=%.6f sampled_det=%.6f "
      "source_norm_base_row2=(%.6f %.6f %.6f) "
      "source_norm_sampled_row0=(%.6f %.6f %.6f) "
      "source_norm_sampled_row2=(%.6f %.6f %.6f) "
      "base_row0=(%.6f %.6f %.6f) base_row1=(%.6f %.6f %.6f) "
      "base_row2=(%.6f %.6f %.6f) sampled_row0=(%.6f %.6f %.6f) "
      "sampled_row1=(%.6f %.6f %.6f) sampled_row2=(%.6f %.6f %.6f)\n",
      mesh_name.c_str(), target_name.c_str(), target_kind.c_str(),
      target_parent.c_str(), sample, has_offset ? 1 : 0,
      has_active_anim ? 1 : 0, active_frame, has_source_frame, source_frame,
      has_pos, abs_pos, has_rot, abs_rot, quat[0], quat[1], quat[2],
      quat[3], sample_axis[0], sample_axis[1], sample_axis[2],
      sample_axis_valid ? axis_label(dominant_abs_axis(sample_axis)) : "-",
      recompose_delta_axis[0], recompose_delta_axis[1],
      recompose_delta_axis[2],
      recompose_delta_axis_valid
          ? axis_label(dominant_abs_axis(recompose_delta_axis))
          : "-",
      local_delta_axis[0], local_delta_axis[1], local_delta_axis[2],
      local_delta_axis_valid ? axis_label(dominant_abs_axis(local_delta_axis))
                             : "-",
      has_scale, abs_scale, scale[0], scale[1], scale[2],
      base_scale[0], base_scale[1], base_scale[2], sampled_scale[0],
      sampled_scale[1], sampled_scale[2], base_det, sampled_det,
      source_base_rot[2][0], source_base_rot[2][1],
      source_base_rot[2][2], source_sampled_rot[0][0],
      source_sampled_rot[0][1], source_sampled_rot[0][2],
      source_sampled_rot[2][0], source_sampled_rot[2][1],
      source_sampled_rot[2][2],
      base_local[0], base_local[1], base_local[2], base_local[4],
      base_local[5], base_local[6], base_local[8], base_local[9],
      base_local[10], sampled_local[0], sampled_local[1],
      sampled_local[2], sampled_local[4], sampled_local[5],
      sampled_local[6], sampled_local[8], sampled_local[9],
      sampled_local[10]);
}

void log_camera_matrix_rows(const OrbitCamera& cam, const float eye[3],
                            const float at[3], const float up[3],
                            float aspect, const Mat4& view,
                            const Mat4& proj) {
  float forward[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  normalize3(forward);
  const float right[3] = {view.m[0][0], view.m[1][0], view.m[2][0]};
  const float adjusted_up[3] = {view.m[0][1], view.m[1][1], view.m[2][1]};
  std::fprintf(
      stderr,
      "[camera-matrix] authored=%d fov=%.6f aspect=%.6f near=%.6f far=%.6f "
      "screen_offset=(%.6f %.6f) eye=(%.6f %.6f %.6f) "
      "at=(%.6f %.6f %.6f) up_in=(%.6f %.6f %.6f)\n",
      cam.authored ? 1 : 0, cam.fov, aspect, cam.near_z, cam.far_z,
      cam.screen_offset[0], cam.screen_offset[1], eye[0], eye[1], eye[2],
      at[0], at[1], at[2], up[0], up[1], up[2]);
  std::fprintf(
      stderr,
      "[camera-matrix] output forward=(%.6f %.6f %.6f 0.000000) "
      "position=(%.6f %.6f %.6f 1.000000) "
      "right=(%.6f %.6f %.6f 0.000000) up=(%.6f %.6f %.6f 0.000000)\n",
      forward[0], forward[1], forward[2], eye[0], eye[1], eye[2], right[0],
      right[1], right[2], adjusted_up[0], adjusted_up[1], adjusted_up[2]);
  if (cam.result_frame.valid) {
    std::fprintf(
        stderr,
        "[camera-matrix] result_frame source=%s "
        "custom_view=%d custom_projection=%d "
        "position=(%.6f %.6f %.6f 1.000000) "
        "forward=(%.6f %.6f %.6f 0.000000) "
        "right=(%.6f %.6f %.6f 0.000000) "
        "up=(%.6f %.6f %.6f 0.000000)\n",
        cam.result_frame.source.c_str(), cam.result_frame.has_custom_view ? 1 : 0,
        cam.result_frame.has_custom_projection ? 1 : 0, cam.result_frame.position[0],
        cam.result_frame.position[1], cam.result_frame.position[2],
        cam.result_frame.forward[0], cam.result_frame.forward[1],
        cam.result_frame.forward[2], cam.result_frame.right[0],
        cam.result_frame.right[1], cam.result_frame.right[2],
        cam.result_frame.up[0], cam.result_frame.up[1],
        cam.result_frame.up[2]);
    if (cam.result_frame.has_custom_view) {
      std::fprintf(
          stderr,
          "[camera-matrix] custom_view row0=(%.6f %.6f %.6f %.6f) "
          "row1=(%.6f %.6f %.6f %.6f) "
          "row2=(%.6f %.6f %.6f %.6f) "
          "row3=(%.6f %.6f %.6f %.6f)\n",
          cam.result_frame.custom_view[0], cam.result_frame.custom_view[1],
          cam.result_frame.custom_view[2], cam.result_frame.custom_view[3],
          cam.result_frame.custom_view[4], cam.result_frame.custom_view[5],
          cam.result_frame.custom_view[6], cam.result_frame.custom_view[7],
          cam.result_frame.custom_view[8], cam.result_frame.custom_view[9],
          cam.result_frame.custom_view[10], cam.result_frame.custom_view[11],
          cam.result_frame.custom_view[12], cam.result_frame.custom_view[13],
          cam.result_frame.custom_view[14], cam.result_frame.custom_view[15]);
    }
    if (cam.result_frame.has_custom_projection) {
      std::fprintf(
          stderr,
          "[camera-matrix] custom_projection row0=(%.6f %.6f %.6f %.6f) "
          "row1=(%.6f %.6f %.6f %.6f) "
          "row2=(%.6f %.6f %.6f %.6f) "
          "row3=(%.6f %.6f %.6f %.6f)\n",
          cam.result_frame.custom_projection[0],
          cam.result_frame.custom_projection[1],
          cam.result_frame.custom_projection[2],
          cam.result_frame.custom_projection[3],
          cam.result_frame.custom_projection[4],
          cam.result_frame.custom_projection[5],
          cam.result_frame.custom_projection[6],
          cam.result_frame.custom_projection[7],
          cam.result_frame.custom_projection[8],
          cam.result_frame.custom_projection[9],
          cam.result_frame.custom_projection[10],
          cam.result_frame.custom_projection[11],
          cam.result_frame.custom_projection[12],
          cam.result_frame.custom_projection[13],
          cam.result_frame.custom_projection[14],
          cam.result_frame.custom_projection[15]);
    }
  }
  for (int r = 0; r < 4; ++r) {
    std::fprintf(stderr,
                 "[camera-matrix] view row%d=(%.6f %.6f %.6f %.6f)\n", r,
                 view.m[r][0], view.m[r][1], view.m[r][2], view.m[r][3]);
  }
  for (int r = 0; r < 4; ++r) {
    std::fprintf(stderr,
                 "[camera-matrix] proj row%d=(%.6f %.6f %.6f %.6f)\n", r,
                 proj.m[r][0], proj.m[r][1], proj.m[r][2], proj.m[r][3]);
  }
}

bool has_suffix(const std::string& s, const char* suffix) {
  const size_t suffix_len = std::strlen(suffix);
  return s.size() >= suffix_len &&
         s.compare(s.size() - suffix_len, suffix_len, suffix) == 0;
}

bool environ_color_sane(const milo_scene::EnvironObj& env) {
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(env.color_a[i]) || env.color_a[i] < 0.0f ||
        env.color_a[i] > 4.0f) {
      return false;
    }
  }
  return true;
}

bool light_color_sane(const milo_scene::LightObj& light) {
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(light.color[i]) || light.color[i] < 0.0f ||
        light.color[i] > kMaxAuthoredLightColor) {
      return false;
    }
  }
  return std::isfinite(light.range) && light.range >= 0.0f &&
         light.range <= 100000.0f;
}

float vec_len3(float x, float y, float z) {
  return std::sqrt(x * x + y * y + z * z);
}

bool mesh_bbox_outside_clip_frustum(const milo_scene::MeshObj& mesh,
                                    const std::array<float, 16>& wvp) {
  const float xs[2] = {mesh.bb_min[0], mesh.bb_max[0]};
  const float ys[2] = {mesh.bb_min[1], mesh.bb_max[1]};
  const float zs[2] = {mesh.bb_min[2], mesh.bb_max[2]};
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(mesh.bb_min[axis]) ||
        !std::isfinite(mesh.bb_max[axis]) ||
        mesh.bb_max[axis] < mesh.bb_min[axis]) {
      return false;
    }
  }

  bool outside_left = true;
  bool outside_right = true;
  bool outside_bottom = true;
  bool outside_top = true;
  bool outside_near = true;
  bool outside_far = true;
  for (float px : xs) {
    for (float py : ys) {
      for (float pz : zs) {
        const float x =
            px * wvp[0] + py * wvp[4] + pz * wvp[8] + wvp[12];
        const float y =
            px * wvp[1] + py * wvp[5] + pz * wvp[9] + wvp[13];
        const float z =
            px * wvp[2] + py * wvp[6] + pz * wvp[10] + wvp[14];
        const float w =
            px * wvp[3] + py * wvp[7] + pz * wvp[11] + wvp[15];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            !std::isfinite(w)) {
          return false;
        }
        const float margin = std::max(0.01f, std::fabs(w) * 0.03f);
        outside_left = outside_left && (x + w < -margin);
        outside_right = outside_right && (w - x < -margin);
        outside_bottom = outside_bottom && (y + w < -margin);
        outside_top = outside_top && (w - y < -margin);
        outside_near = outside_near && (z < -margin);
        outside_far = outside_far && (w - z < -margin);
      }
    }
  }
  return outside_left || outside_right || outside_bottom || outside_top ||
         outside_near || outside_far;
}

struct DebugVenuePick {
  bool hit = false;
  std::string mesh;
  std::string multi_mesh;
  float draw_origin[3] = {};
  std::string material;
  float distance = 0.0f;
  float point[3] = {0.0f, 0.0f, 0.0f};
  bool backfacing = false;
  bool would_draw = true;
  bool source_pick = false;
  bool hidden_by_filter = false;
  bool source_showing = true;
  bool material_invisible = false;
  bool spotlight_template = false;
  bool cull_enabled = false;
  bool culled_by_backface = false;
  bool has_axis_diagnostics = false;
  float shape_extent[3] = {0.0f, 0.0f, 0.0f};
  int thin_axis = 2;
  int face_axis = 2;
  float face_dir[3] = {0.0f, 0.0f, 1.0f};
  float draw_face[3] = {0.0f, 0.0f, 0.0f};
};

struct DebugVenuePickAccumulator {
  bool enabled = false;
  bool source_mode = false;
  std::string label = "center";
  float ndc_x = 0.0f;
  float ndc_y = 0.0f;
  float eye[3] = {0.0f, 0.0f, 0.0f};
  float dir[3] = {0.0f, 1.0f, 0.0f};
  DebugVenuePick best;
};

struct DebugVenuePickMeshState {
  std::string multi_mesh;
  bool would_draw = true;
  bool source_pick = false;
  bool hidden_by_filter = false;
  bool source_showing = true;
  bool material_invisible = false;
  bool spotlight_template = false;
  bool cull_enabled = false;
  DWORD cull_mode = D3DCULL_NONE;
};

struct DebugVenueInspectorState {
  bool initialized = false;
  bool announced = false;
  float dt = 1.0f / 60.0f;
  OrbitCamera camera;
  std::string highlight_mesh;
  std::string last_title;
  std::string last_grid_log;
  float crosshair_ndc_x = 0.0f;
  float crosshair_ndc_y = 0.0f;
  DebugVenuePick pick;
  bool highlight_source_only = false;
  bool highlight_enabled = true;
  bool highlight_toggle_down = false;
  bool axes_enabled = false;
  bool axes_toggle_down = false;
};

std::unordered_map<const MiloSceneRenderer*, DebugVenueInspectorState>&
debug_venue_inspector_states() {
  static std::unordered_map<const MiloSceneRenderer*, DebugVenueInspectorState>
      states;
  return states;
}

bool debug_venue_inspector_enabled() {
  return env_enabled("GHOGX_VENUE_FREECAM") ||
         env_enabled("GHOGX_DEBUG_VENUE_FREECAM") ||
         env_enabled("GHOGX_DEBUG_VENUE_PICK_AUTHORED");
}

DWORD venue_mesh_cull_mode(const milo_scene::MatObj* mat,
                           const std::array<float, 16>& draw_world,
                           bool source_winding_reversed,
                           DWORD authored_cull_mode,
                           bool debug_highlighted_mesh) {
  return static_cast<DWORD>(source_mesh_cull_mode(
      !mat || mat->cull, debug_highlighted_mesh,
      world_transform_reverses_winding(draw_world), source_winding_reversed,
      static_cast<uint32_t>(authored_cull_mode)));
}

bool debug_pick_culled_by_cull_mode(DWORD cull_mode, bool backfacing) {
  if (cull_mode == D3DCULL_NONE) return false;
  if (cull_mode == D3DCULL_CW) return backfacing;
  if (cull_mode == D3DCULL_CCW) return !backfacing;
  return backfacing;
}

bool env_string(const char* name, std::string& out) {
  out = env_cached_string(name);
  return !out.empty() && out != "0";
}

bool parse_float_pair(const std::string& text, float out[2]) {
  const char* cur = text.c_str();
  char* end = nullptr;
  out[0] = std::strtof(cur, &end);
  if (end == cur) return false;
  cur = end;
  while (*cur == ',' || *cur == ';' || std::isspace(static_cast<unsigned char>(*cur))) {
    ++cur;
  }
  out[1] = std::strtof(cur, &end);
  return end != cur && std::isfinite(out[0]) && std::isfinite(out[1]);
}

bool parse_float_triplet(const std::string& text, float out[3]) {
  const char* cur = text.c_str();
  char* end = nullptr;
  for (int i = 0; i < 3; ++i) {
    out[i] = std::strtof(cur, &end);
    if (end == cur || !std::isfinite(out[i])) return false;
    cur = end;
    while (*cur == ',' || *cur == ';' ||
           std::isspace(static_cast<unsigned char>(*cur))) {
      ++cur;
    }
  }
  return true;
}

bool env_float_pair(const char* name, float out[2]) {
  std::string text;
  return env_string(name, text) && parse_float_pair(text, out);
}

bool env_float_triplet(const char* name, float out[3]) {
  std::string text;
  return env_string(name, text) && parse_float_triplet(text, out);
}

void direction_from_orbit_angles(const OrbitCamera& cam, float out[3]) {
  const float cp = std::cos(cam.pitch);
  out[0] = -std::sin(cam.yaw) * cp;
  out[1] = std::cos(cam.yaw) * cp;
  out[2] = -std::sin(cam.pitch);
  normalize3(out);
}

void freecam_seed_from_current_camera(OrbitCamera& cam) {
  float eye[3] = {0.0f, 0.0f, 0.0f};
  cam.eye(eye);
  float at[3] = {cam.target[0], cam.target[1], cam.target[2]};
  if (cam.result_frame.valid) {
    for (int k = 0; k < 3; ++k) {
      at[k] = cam.result_frame.position[k] + cam.result_frame.forward[k] * 100.0f;
    }
  } else if (cam.authored) {
    for (int k = 0; k < 3; ++k) at[k] = cam.authored_at[k];
  }
  float forward[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  normalize3(forward);
  const float xy = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
  if (xy > 0.000001f) {
    cam.yaw = std::atan2(-forward[0], forward[1]);
    cam.pitch = std::asin(std::clamp(-forward[2], -0.99f, 0.99f));
  }
  cam.authored = false;
  cam.result_frame = {};
  cam.distance = env_float_or("GHOGX_VENUE_FREECAM_LOOK_DIST", 45.0f, 1.0f,
                              500.0f);
  cam.near_z = std::min(cam.near_z, 1.0f);
  cam.far_z = std::max(cam.far_z, 15000.0f);
  direction_from_orbit_angles(cam, forward);
  for (int k = 0; k < 3; ++k) cam.target[k] = eye[k] + forward[k] * cam.distance;
}

bool apply_debug_venue_env_camera(OrbitCamera& cam) {
  float eye[3] = {0.0f, 0.0f, 0.0f};
  float at[3] = {0.0f, 0.0f, 0.0f};
  const bool has_eye = env_float_triplet("GHOGX_VENUE_FREECAM_EYE", eye);
  const bool has_at = env_float_triplet("GHOGX_VENUE_FREECAM_AT", at);
  if (!has_eye && !has_at) return false;
  if (!has_eye) cam.eye(eye);
  if (!has_at) {
    for (int k = 0; k < 3; ++k) at[k] = cam.target[k];
  }

  float forward[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  normalize3(forward);
  const float xy = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
  if (xy > 0.000001f) {
    cam.yaw = std::atan2(-forward[0], forward[1]);
    cam.pitch = std::asin(std::clamp(-forward[2], -0.99f, 0.99f));
  }
  cam.authored = false;
  cam.result_frame = {};
  cam.distance = env_float_or("GHOGX_VENUE_FREECAM_LOOK_DIST", 45.0f, 1.0f,
                              500.0f);
  cam.fov = env_float_or("GHOGX_VENUE_FREECAM_FOV", cam.fov, 0.15f, 2.4f);
  cam.near_z = env_float_or("GHOGX_VENUE_FREECAM_NEAR", 1.0f, 0.01f, 100.0f);
  cam.far_z =
      env_float_or("GHOGX_VENUE_FREECAM_FAR", std::max(cam.far_z, 15000.0f),
                   100.0f, 100000.0f);
  for (int k = 0; k < 3; ++k) cam.target[k] = eye[k] + forward[k] * cam.distance;
  return true;
}

void apply_debug_venue_freecam(Window* win, OrbitCamera& cam,
                               DebugVenueInspectorState& state) {
  if (!win) return;
  win->set_relative_mouse(true);
  if (!state.initialized || win->key_down('C')) {
    const bool first_init = !state.initialized;
    state.camera = cam;
    freecam_seed_from_current_camera(state.camera);
    state.initialized = true;
    if (first_init) {
      state.axes_enabled = env_enabled("GHOGX_VENUE_PICK_AXES");
      state.highlight_enabled =
          !env_enabled("GHOGX_VENUE_FREECAM_NO_HIGHLIGHT");
    }
    if (!state.announced) {
      std::fprintf(stderr,
                   "[venue-freecam] enabled: mouse look, WASD move, E/R up, "
                   "Q/F down, arrows look, Shift fast, Ctrl slow, H highlight, "
                   "X axes, C reseed, Esc quit\n");
      state.announced = true;
    }
  }

  OrbitCamera& freecam = state.camera;
  if (apply_debug_venue_env_camera(freecam)) {
    cam = freecam;
    return;
  }

  float eye[3] = {0.0f, 0.0f, 0.0f};
  freecam.eye(eye);
  const float dt = std::clamp(state.dt, 0.001f, 0.1f);
  const float turn_speed =
      env_float_or("GHOGX_VENUE_FREECAM_TURN_SPEED", 1.35f, 0.05f, 8.0f);
  const float mouse_sens =
      env_float_or("GHOGX_VENUE_FREECAM_MOUSE_SENS", 0.0035f, 0.0001f, 0.05f);
  const float move_speed =
      env_float_or("GHOGX_VENUE_FREECAM_SPEED", 210.0f, 1.0f, 3000.0f);
  const float turn = turn_speed * dt;
  const bool highlight_toggle = win->key_down('H');
  if (highlight_toggle && !state.highlight_toggle_down) {
    state.highlight_enabled = !state.highlight_enabled;
    std::fprintf(stderr, "[venue-freecam] pick highlight %s\n",
                 state.highlight_enabled ? "on" : "off");
  }
  state.highlight_toggle_down = highlight_toggle;
  const bool axes_toggle = win->key_down('X');
  if (axes_toggle && !state.axes_toggle_down) {
    state.axes_enabled = !state.axes_enabled;
    state.last_title.clear();
    std::fprintf(stderr, "[venue-freecam] pick axes %s\n",
                 state.axes_enabled ? "on" : "off");
  }
  state.axes_toggle_down = axes_toggle;
  int mouse_dx = 0;
  int mouse_dy = 0;
  win->mouse_delta(mouse_dx, mouse_dy);
  freecam.yaw -= static_cast<float>(mouse_dx) * mouse_sens;
  freecam.pitch -= static_cast<float>(mouse_dy) * mouse_sens;
  if (win->key_down(VK_LEFT)) freecam.yaw += turn;
  if (win->key_down(VK_RIGHT)) freecam.yaw -= turn;
  if (win->key_down(VK_UP)) freecam.pitch += turn;
  if (win->key_down(VK_DOWN)) freecam.pitch -= turn;
  freecam.pitch = std::clamp(freecam.pitch, -1.45f, 1.45f);

  float forward[3] = {0.0f, 1.0f, 0.0f};
  direction_from_orbit_angles(freecam, forward);
  for (int k = 0; k < 3; ++k) {
    freecam.target[k] = eye[k] + forward[k] * freecam.distance;
  }

  float right[3] = {forward[1], -forward[0], 0.0f};
  normalize3(right);
  const float fast = win->key_down(VK_SHIFT) ? 4.0f : 1.0f;
  const float slow = win->key_down(VK_CONTROL) ? 0.25f : 1.0f;
  const float step = move_speed * fast * slow * dt;
  float delta[3] = {0.0f, 0.0f, 0.0f};
  auto add_scaled = [&](const float axis[3], float scale) {
    for (int k = 0; k < 3; ++k) delta[k] += axis[k] * scale;
  };
  if (win->key_down('W')) add_scaled(forward, step);
  if (win->key_down('S')) add_scaled(forward, -step);
  if (win->key_down('D')) add_scaled(right, step);
  if (win->key_down('A')) add_scaled(right, -step);
  if (win->key_down('E') || win->key_down('R')) delta[2] += step;
  if (win->key_down('Q') || win->key_down('F')) delta[2] -= step;
  for (int k = 0; k < 3; ++k) freecam.target[k] += delta[k];
  cam = freecam;
}

void transform_debug_point(const std::array<float, 16>& world,
                           const milo_scene::Vertex& v, float out[3]) {
  out[0] = v.px * world[0] + v.py * world[4] + v.pz * world[8] + world[12];
  out[1] = v.px * world[1] + v.py * world[5] + v.pz * world[9] + world[13];
  out[2] = v.px * world[2] + v.py * world[6] + v.pz * world[10] + world[14];
}

bool intersect_debug_triangle(const float eye[3], const float dir[3],
                              const float a[3], const float b[3],
                              const float c[3], float& out_t,
                              bool& out_backfacing) {
  const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  const float h[3] = {
      dir[1] * e2[2] - dir[2] * e2[1],
      dir[2] * e2[0] - dir[0] * e2[2],
      dir[0] * e2[1] - dir[1] * e2[0],
  };
  const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
  if (std::fabs(det) <= 0.000001f) return false;
  const float inv_det = 1.0f / det;
  const float s[3] = {eye[0] - a[0], eye[1] - a[1], eye[2] - a[2]};
  const float u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) * inv_det;
  if (u < 0.0f || u > 1.0f) return false;
  const float q[3] = {
      s[1] * e1[2] - s[2] * e1[1],
      s[2] * e1[0] - s[0] * e1[2],
      s[0] * e1[1] - s[1] * e1[0],
  };
  const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv_det;
  if (v < 0.0f || u + v > 1.0f) return false;
  const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv_det;
  if (t <= 0.01f) return false;
  const float normal[3] = {
      e1[1] * e2[2] - e1[2] * e2[1],
      e1[2] * e2[0] - e1[0] * e2[2],
      e1[0] * e2[1] - e1[1] * e2[0],
  };
  out_t = t;
  out_backfacing =
      (normal[0] * dir[0] + normal[1] * dir[1] + normal[2] * dir[2]) > 0.0f;
  return true;
}

void accumulate_debug_venue_pick(DebugVenuePickAccumulator& pick,
                                 const milo_scene::MeshObj& mesh,
                                 const std::array<float, 16>& world,
                                 const DebugVenuePickMeshState& mesh_state) {
  if (!pick.enabled || !mesh.decoded || mesh.verts.empty() ||
      mesh.indices.size() < 3) {
    return;
  }
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const uint16_t ia = mesh.indices[i + 0];
    const uint16_t ib = mesh.indices[i + 1];
    const uint16_t ic = mesh.indices[i + 2];
    if (ia >= mesh.verts.size() || ib >= mesh.verts.size() ||
        ic >= mesh.verts.size()) {
      continue;
    }
    float a[3], b[3], c[3];
    transform_debug_point(world, mesh.verts[ia], a);
    transform_debug_point(world, mesh.verts[ib], b);
    transform_debug_point(world, mesh.verts[ic], c);
    float t = 0.0f;
    bool backfacing = false;
    if (!intersect_debug_triangle(pick.eye, pick.dir, a, b, c, t,
                                  backfacing)) {
      continue;
    }
    if (pick.best.hit && t >= pick.best.distance) continue;
    pick.best.hit = true;
    pick.best.mesh = mesh.name;
    pick.best.material = mesh.material;
    pick.best.distance = t;
    pick.best.backfacing = backfacing;
    const bool culled_by_backface =
        debug_pick_culled_by_cull_mode(mesh_state.cull_mode, backfacing);
    pick.best.would_draw = mesh_state.would_draw && !culled_by_backface;
    pick.best.source_pick = mesh_state.source_pick;
    pick.best.multi_mesh = mesh_state.multi_mesh;
    for (int k = 0; k < 3; ++k) pick.best.draw_origin[k] = world[12 + k];
    pick.best.hidden_by_filter = mesh_state.hidden_by_filter;
    pick.best.source_showing = mesh_state.source_showing;
    pick.best.material_invisible = mesh_state.material_invisible;
    pick.best.spotlight_template = mesh_state.spotlight_template;
    pick.best.cull_enabled = mesh_state.cull_enabled;
    pick.best.culled_by_backface = culled_by_backface;
    for (int k = 0; k < 3; ++k) pick.best.point[k] = pick.eye[k] + pick.dir[k] * t;
    const MeshLocalAxisDiagnostics axes = mesh_local_axis_diagnostics(mesh);
    if (axes.valid) {
      const std::array<float, 3> draw_face =
          transform_local_dir(world, axes.face_dir);
      pick.best.has_axis_diagnostics = true;
      pick.best.thin_axis = axes.thin_axis;
      pick.best.face_axis = axes.face_axis;
      for (int k = 0; k < 3; ++k) {
        pick.best.shape_extent[k] = axes.extent[k];
        pick.best.face_dir[k] = axes.face_dir[k];
        pick.best.draw_face[k] = draw_face[k];
      }
    }
  }
}

DebugVenuePickAccumulator make_debug_venue_pick_accumulator(
    const char* label, float ndc_x, float ndc_y, const float eye[3],
    const float at[3], const float* up_in, float fov, float aspect) {
  DebugVenuePickAccumulator pick;
  pick.enabled = true;
  pick.label = label ? label : "pick";
  pick.ndc_x = ndc_x;
  pick.ndc_y = ndc_y;
  for (int k = 0; k < 3; ++k) pick.eye[k] = eye[k];

  float forward[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  normalize3(forward);
  const float* up_src = up_in;
  const float default_up[3] = {0.0f, 0.0f, 1.0f};
  if (!up_src) up_src = default_up;
  float up[3] = {up_src[0], up_src[1], up_src[2]};
  normalize3(up);
  float right[3] = {
      forward[1] * up[2] - forward[2] * up[1],
      forward[2] * up[0] - forward[0] * up[2],
      forward[0] * up[1] - forward[1] * up[0],
  };
  normalize3(right);
  up[0] = right[1] * forward[2] - right[2] * forward[1];
  up[1] = right[2] * forward[0] - right[0] * forward[2];
  up[2] = right[0] * forward[1] - right[1] * forward[0];
  normalize3(up);
  const float tan_y = std::tan(std::clamp(fov, 0.05f, 3.0f) * 0.5f);
  const float tan_x = tan_y * std::max(aspect, 0.01f);
  for (int k = 0; k < 3; ++k) {
    pick.dir[k] = forward[k] + right[k] * ndc_x * tan_x +
                  up[k] * ndc_y * tan_y;
  }
  normalize3(pick.dir);
  return pick;
}

std::string debug_pick_summary(const DebugVenuePickAccumulator& pick) {
  char buf[512];
  if (pick.best.hit) {
    const char* status =
        pick.best.would_draw
            ? "draw"
            : (pick.best.culled_by_backface ? "culled" : "source");
    std::snprintf(buf, sizeof(buf),
                  "%s=%s|%s|%s|bf%d|cull%d|d%.1f|p(%.1f,%.1f,%.1f)",
                  pick.label.c_str(), pick.best.mesh.c_str(),
                  pick.best.material.c_str(), status,
                  pick.best.backfacing ? 1 : 0,
                  pick.best.culled_by_backface ? 1 : 0,
                  pick.best.distance,
                  pick.best.point[0], pick.best.point[1],
                  pick.best.point[2]);
  } else {
    std::snprintf(buf, sizeof(buf), "%s=none", pick.label.c_str());
  }
  return buf;
}

void log_debug_pick_grid(DebugVenueInspectorState& state,
                         const std::vector<DebugVenuePickAccumulator>& picks) {
  if (!env_enabled("GHOGX_VENUE_PICK_GRID") || picks.empty()) return;
  std::string key;
  std::string line = "[venue-freecam] pick_grid";
  for (const auto& pick : picks) {
    const std::string summary = debug_pick_summary(pick);
    line += " ";
    line += summary;
    key += summary;
    key += ";";
  }
  if (state.last_grid_log == key) return;
  state.last_grid_log = key;
  std::fprintf(stderr, "%s\n", line.c_str());
}

void update_debug_venue_title(Window* win, DebugVenueInspectorState& state) {
  if (!win) return;
  char title[384];
  char key[384];
  if (state.pick.hit) {
    std::fprintf(stderr,
                 "[venue-freecam] pick provenance mesh=%s multimesh=%s origin=(%.3f %.3f %.3f)\n",
                 state.pick.mesh.c_str(),
                 state.pick.multi_mesh.empty() ? "<standalone>" : state.pick.multi_mesh.c_str(),
                 state.pick.draw_origin[0], state.pick.draw_origin[1], state.pick.draw_origin[2]);
    const char* status =
        state.pick.would_draw
            ? "renders"
            : (state.pick.culled_by_backface ? "backface-culled"
                                             : "source-only");
    if (state.axes_enabled && state.pick.has_axis_diagnostics) {
      std::snprintf(
          title, sizeof(title),
          "GuitarHeroOGX venue freecam - %s | %s | %.1f | %s | face %s dir %.2f %.2f %.2f",
          state.pick.mesh.c_str(), state.pick.material.c_str(),
          state.pick.distance, status, axis_label(state.pick.face_axis),
          state.pick.draw_face[0], state.pick.draw_face[1],
          state.pick.draw_face[2]);
      std::snprintf(key, sizeof(key), "%s|%s|%d|%d|%d|axis%d|%.2f|%.2f|%.2f",
                    state.pick.mesh.c_str(), state.pick.material.c_str(),
                    state.pick.backfacing ? 1 : 0,
                    state.pick.would_draw ? 1 : 0,
                    state.pick.culled_by_backface ? 1 : 0,
                    state.pick.face_axis, state.pick.draw_face[0],
                    state.pick.draw_face[1], state.pick.draw_face[2]);
    } else {
      std::snprintf(title, sizeof(title),
                    "GuitarHeroOGX venue freecam - %s | %s | %.1f | %s | backface %s | culled %s",
                    state.pick.mesh.c_str(), state.pick.material.c_str(),
                    state.pick.distance, status,
                    state.pick.backfacing ? "yes" : "no",
                    state.pick.culled_by_backface ? "yes" : "no");
      std::snprintf(key, sizeof(key), "%s|%s|%d|%d|%d|axes%d",
                    state.pick.mesh.c_str(), state.pick.material.c_str(),
                    state.pick.backfacing ? 1 : 0,
                    state.pick.would_draw ? 1 : 0,
                    state.pick.culled_by_backface ? 1 : 0,
                    state.axes_enabled ? 1 : 0);
    }
  } else {
    std::snprintf(title, sizeof(title),
                  "GuitarHeroOGX venue freecam - no mesh under crosshair");
    std::snprintf(key, sizeof(key), "none");
  }
  if (state.last_title == key) return;
  state.last_title = key;
  win->set_title(title);
  if (state.pick.hit) {
    if (state.axes_enabled && state.pick.has_axis_diagnostics) {
      std::fprintf(
          stderr,
          "[venue-freecam] pick mesh=%s material=%s dist=%.2f "
          "point=(%.2f %.2f %.2f) backface=%d would_draw=%d "
          "source_pick=%d hidden=%d showing=%d invisible_mat=%d "
          "spotlight_template=%d cull_enabled=%d culled_by_backface=%d "
          "axes=1 extent=(%.3f %.3f %.3f) thin_axis=%s face_axis=%s "
          "face_dir=(%.1f %.1f %.1f) draw_face=(%.3f %.3f %.3f)\n",
          state.pick.mesh.c_str(), state.pick.material.c_str(),
          state.pick.distance, state.pick.point[0], state.pick.point[1],
          state.pick.point[2], state.pick.backfacing ? 1 : 0,
          state.pick.would_draw ? 1 : 0, state.pick.source_pick ? 1 : 0,
          state.pick.hidden_by_filter ? 1 : 0,
          state.pick.source_showing ? 1 : 0,
          state.pick.material_invisible ? 1 : 0,
          state.pick.spotlight_template ? 1 : 0,
          state.pick.cull_enabled ? 1 : 0,
          state.pick.culled_by_backface ? 1 : 0,
          state.pick.shape_extent[0], state.pick.shape_extent[1],
          state.pick.shape_extent[2], axis_label(state.pick.thin_axis),
          axis_label(state.pick.face_axis), state.pick.face_dir[0],
          state.pick.face_dir[1], state.pick.face_dir[2],
          state.pick.draw_face[0], state.pick.draw_face[1],
          state.pick.draw_face[2]);
    } else {
      std::fprintf(stderr,
                   "[venue-freecam] pick mesh=%s material=%s dist=%.2f "
                   "point=(%.2f %.2f %.2f) backface=%d would_draw=%d "
                   "source_pick=%d hidden=%d showing=%d invisible_mat=%d "
                   "spotlight_template=%d cull_enabled=%d culled_by_backface=%d\n",
                   state.pick.mesh.c_str(), state.pick.material.c_str(),
                   state.pick.distance, state.pick.point[0],
                   state.pick.point[1], state.pick.point[2],
                   state.pick.backfacing ? 1 : 0,
                   state.pick.would_draw ? 1 : 0,
                   state.pick.source_pick ? 1 : 0,
                   state.pick.hidden_by_filter ? 1 : 0,
                   state.pick.source_showing ? 1 : 0,
                   state.pick.material_invisible ? 1 : 0,
                   state.pick.spotlight_template ? 1 : 0,
                   state.pick.cull_enabled ? 1 : 0,
                   state.pick.culled_by_backface ? 1 : 0);
    }
  } else {
    std::fprintf(stderr, "[venue-freecam] pick none\n");
  }
}

struct DebugLineVertex {
  float x, y, z, rhw;
  D3DCOLOR color;
};

void draw_debug_crosshair(IDirect3DDevice9* dev, int width, int height,
                          float ndc_x, float ndc_y, bool hit,
                          bool would_draw) {
  if (!dev || width <= 0 || height <= 0) return;
  const float cx =
      (std::clamp(ndc_x, -1.0f, 1.0f) * 0.5f + 0.5f) *
      static_cast<float>(width);
  const float cy =
      (0.5f - std::clamp(ndc_y, -1.0f, 1.0f) * 0.5f) *
      static_cast<float>(height);
  const float gap = 5.0f;
  const float arm = 18.0f;
  const D3DCOLOR color =
      hit ? (would_draw ? D3DCOLOR_ARGB(235, 255, 232, 64)
                        : D3DCOLOR_ARGB(235, 64, 232, 255))
          : D3DCOLOR_ARGB(235, 255, 80, 80);
  DebugLineVertex verts[] = {
      {cx - arm, cy, 0.0f, 1.0f, color},
      {cx - gap, cy, 0.0f, 1.0f, color},
      {cx + gap, cy, 0.0f, 1.0f, color},
      {cx + arm, cy, 0.0f, 1.0f, color},
      {cx, cy - arm, 0.0f, 1.0f, color},
      {cx, cy - gap, 0.0f, 1.0f, color},
      {cx, cy + gap, 0.0f, 1.0f, color},
      {cx, cy + arm, 0.0f, 1.0f, color},
  };
  dev->SetTexture(0, nullptr);
  dev->SetRenderState(D3DRS_ZENABLE, FALSE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  dev->DrawPrimitiveUP(D3DPT_LINELIST, 4, verts, sizeof(DebugLineVertex));
}

bool project_debug_point_to_screen(const Mat4& view, const Mat4& proj,
                                   const float point[3], int width,
                                   int height, float out[2]) {
  const float vx = point[0] * view.m[0][0] + point[1] * view.m[1][0] +
                   point[2] * view.m[2][0] + view.m[3][0];
  const float vy = point[0] * view.m[0][1] + point[1] * view.m[1][1] +
                   point[2] * view.m[2][1] + view.m[3][1];
  const float vz = point[0] * view.m[0][2] + point[1] * view.m[1][2] +
                   point[2] * view.m[2][2] + view.m[3][2];
  const float x = vx * proj.m[0][0] + vy * proj.m[1][0] +
                  vz * proj.m[2][0] + proj.m[3][0];
  const float y = vx * proj.m[0][1] + vy * proj.m[1][1] +
                  vz * proj.m[2][1] + proj.m[3][1];
  const float w = vx * proj.m[0][3] + vy * proj.m[1][3] +
                  vz * proj.m[2][3] + proj.m[3][3];
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) ||
      w <= 0.001f) {
    return false;
  }
  const float ndc_x = x / w;
  const float ndc_y = y / w;
  if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) return false;
  out[0] = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
  out[1] = (0.5f - ndc_y * 0.5f) * static_cast<float>(height);
  return true;
}

void draw_debug_pick_face_axis(IDirect3DDevice9* dev, int width, int height,
                               const Mat4& view, const Mat4& proj,
                               const DebugVenuePick& pick) {
  if (!dev || width <= 0 || height <= 0 || !pick.hit ||
      !pick.has_axis_diagnostics) {
    return;
  }
  const float face_len =
      std::sqrt(pick.draw_face[0] * pick.draw_face[0] +
                pick.draw_face[1] * pick.draw_face[1] +
                pick.draw_face[2] * pick.draw_face[2]);
  if (face_len <= 0.001f) return;
  const float face[3] = {pick.draw_face[0] / face_len,
                         pick.draw_face[1] / face_len,
                         pick.draw_face[2] / face_len};
  const float extent =
      std::max({pick.shape_extent[0], pick.shape_extent[1],
                pick.shape_extent[2], 20.0f});
  const float axis_len = std::clamp(extent * 0.55f, 18.0f, 90.0f);
  float origin[2], forward[2], back[2];
  float p0[3] = {pick.point[0], pick.point[1], pick.point[2]};
  float p1[3] = {pick.point[0] + face[0] * axis_len,
                 pick.point[1] + face[1] * axis_len,
                 pick.point[2] + face[2] * axis_len};
  float p2[3] = {pick.point[0] - face[0] * axis_len * 0.35f,
                 pick.point[1] - face[1] * axis_len * 0.35f,
                 pick.point[2] - face[2] * axis_len * 0.35f};
  if (!project_debug_point_to_screen(view, proj, p0, width, height, origin) ||
      !project_debug_point_to_screen(view, proj, p1, width, height, forward) ||
      !project_debug_point_to_screen(view, proj, p2, width, height, back)) {
    return;
  }

  constexpr D3DCOLOR kForward = D3DCOLOR_ARGB(245, 80, 255, 255);
  constexpr D3DCOLOR kBack = D3DCOLOR_ARGB(190, 255, 80, 255);
  constexpr D3DCOLOR kTip = D3DCOLOR_ARGB(245, 255, 236, 64);
  const float tick = 7.0f;
  DebugLineVertex verts[] = {
      {origin[0], origin[1], 0.0f, 1.0f, kForward},
      {forward[0], forward[1], 0.0f, 1.0f, kForward},
      {origin[0], origin[1], 0.0f, 1.0f, kBack},
      {back[0], back[1], 0.0f, 1.0f, kBack},
      {forward[0] - tick, forward[1], 0.0f, 1.0f, kTip},
      {forward[0] + tick, forward[1], 0.0f, 1.0f, kTip},
      {forward[0], forward[1] - tick, 0.0f, 1.0f, kTip},
      {forward[0], forward[1] + tick, 0.0f, 1.0f, kTip},
      {origin[0] - tick * 0.55f, origin[1], 0.0f, 1.0f, kForward},
      {origin[0] + tick * 0.55f, origin[1], 0.0f, 1.0f, kForward},
      {origin[0], origin[1] - tick * 0.55f, 0.0f, 1.0f, kForward},
      {origin[0], origin[1] + tick * 0.55f, 0.0f, 1.0f, kForward},
  };
  dev->SetTexture(0, nullptr);
  dev->SetRenderState(D3DRS_ZENABLE, FALSE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  dev->DrawPrimitiveUP(D3DPT_LINELIST, 6, verts, sizeof(DebugLineVertex));
}

milo_scene::Vertex generated_spotlight_vertex(float x, float y, float z,
                                              float nx, float ny, float nz,
                                              float u, float v,
                                              float alpha = 1.0f,
                                              float color = 1.0f) {
  milo_scene::Vertex out{};
  out.px = x;
  out.py = y;
  out.pz = z;
  out.nx = nx;
  out.ny = ny;
  out.nz = nz;
  out.r = color;
  out.g = color;
  out.b = color;
  out.a = alpha;
  out.u = u;
  out.v = v;
  return out;
}

void finish_generated_spotlight_mesh(milo_scene::MeshObj& mesh) {
  mesh.vertex_count = static_cast<uint32_t>(mesh.verts.size());
  mesh.face_count = static_cast<uint32_t>(mesh.indices.size() / 3u);
  mesh.decoded = !mesh.verts.empty() && !mesh.indices.empty();
  if (!mesh.decoded) return;
  for (int c = 0; c < 3; ++c) {
    mesh.bb_min[c] = std::numeric_limits<float>::infinity();
    mesh.bb_max[c] = -std::numeric_limits<float>::infinity();
  }
  for (const auto& vertex : mesh.verts) {
    const float p[3] = {vertex.px, vertex.py, vertex.pz};
    for (int c = 0; c < 3; ++c) {
      mesh.bb_min[c] = std::min(mesh.bb_min[c], p[c]);
      mesh.bb_max[c] = std::max(mesh.bb_max[c], p[c]);
    }
  }
}

milo_scene::MeshObj make_generated_spotlight_beam(
    const milo_scene::SpotlightObj& spot) {
  milo_scene::MeshObj mesh;
  mesh.name = "__generated_beam__" + spot.name;
  mesh.material = spot.material;
  mesh.showing = true;
  const float length = std::max(0.0f, spot.beam_length);
  const float top_radius = std::max(0.0f, spot.beam_top_radius);
  const float bottom_radius = std::max(0.0f, spot.beam_bottom_radius);
  if (length <= 0.0001f || mesh.material.empty() ||
      (top_radius <= 0.0001f && bottom_radius <= 0.0001f)) {
    return mesh;
  }

  constexpr float kTwoPi = 6.28318530717958647692f;
  if (spot.beam_is_cone) {
    // GH2 retail BuildCone (SLUS 0x002776e0) creates three rings of 16
    // vertices. The last vertex closes a 15-section circumference. The middle
    // ring marks the start of the authored bottom fade; only the final ring's
    // alpha is zero.
    constexpr int kConeSections = 15;
    constexpr int kRingVerts = kConeSections + 1;
    const float bottom_fade =
        std::min(length, std::max(0.0f, length * spot.beam_bottom_border));
    const float fade_start = length - bottom_fade;
    const float fade_t = fade_start / length;
    const float fade_radius =
        top_radius + (bottom_radius - top_radius) * fade_t;
    mesh.verts.reserve(kRingVerts * 3u);
    mesh.indices.reserve(kConeSections * 12u);
    for (int ring = 0; ring < 3; ++ring) {
      const float axial = ring == 0 ? 0.0f : (ring == 1 ? fade_start : length);
      const float radius =
          ring == 0 ? top_radius : (ring == 1 ? fade_radius : bottom_radius);
      const float alpha = ring == 2 ? 0.0f : 1.0f;
      for (int i = 0; i < kRingVerts; ++i) {
        const float u = static_cast<float>(i) / kConeSections;
        const float theta = u * kTwoPi;
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        mesh.verts.push_back(generated_spotlight_vertex(
            radius * c, axial, radius * s, c, 0.0f, s, u,
            axial / length, alpha));
      }
    }
    for (int ring = 0; ring < 2; ++ring) {
      const uint16_t a = static_cast<uint16_t>(ring * kRingVerts);
      const uint16_t b = static_cast<uint16_t>((ring + 1) * kRingVerts);
      for (int i = 0; i < kConeSections; ++i) {
        const uint16_t a0 = static_cast<uint16_t>(a + i);
        const uint16_t a1 = static_cast<uint16_t>(a + i + 1);
        const uint16_t b0 = static_cast<uint16_t>(b + i);
        const uint16_t b1 = static_cast<uint16_t>(b + i + 1);
        mesh.indices.insert(mesh.indices.end(),
                            {a0, b0, a1, a1, b0, b1});
      }
    }
  } else {
    // GH2 retail BuildBeam (SLUS 0x00277c18) is a four-vertex ribbon at each
    // longitudinal row, not a solid frustum. It interpolates the side-border
    // widths along the shaft and subdivides the opaque and bottom-fade spans
    // at approximately 15 world units per row. The original mesh uses -Z as
    // its axial coordinate; this renderer maps that axis to its spotlight
    // target axis (+Y) while preserving the recovered row data exactly.
    const float bottom_fade =
        std::max(0.0f, length * spot.beam_bottom_border);
    const float top_length = length - bottom_fade;
    const int bottom_rows =
        std::max(1, static_cast<int>(std::lround(bottom_fade / 15.0f)));
    const int top_rows =
        std::max(4, static_cast<int>(std::lround(top_length / 15.0f)));
    const int rows = top_rows + bottom_rows;
    const float top_inner = spot.beam_top_side_border * top_radius;
    const float bottom_inner =
        spot.beam_bottom_side_border * bottom_radius;
    const float fade_start_radius =
        top_radius + (bottom_radius - top_radius) * (top_length / length);
    const float top_radius_step =
        (fade_start_radius - top_radius) / static_cast<float>(top_rows);
    const float bottom_radius_step =
        (bottom_radius - fade_start_radius) /
        static_cast<float>(bottom_rows);
    float radius = top_radius;
    mesh.verts.reserve(static_cast<size_t>(rows) * 4u);
    mesh.indices.reserve(static_cast<size_t>(std::max(0, rows - 1)) * 18u);
    for (int row = 0; row < rows; ++row) {
      float axial = 0.0f;
      float intensity = 1.0f;
      if (row == rows - 1) {
        axial = length;
        intensity = 0.0f;
      } else if (row >= top_rows) {
        const int fade_row = row - top_rows;
        axial = top_length +
                static_cast<float>(fade_row) * bottom_fade /
                    static_cast<float>(bottom_rows);
        intensity =
            1.0f - static_cast<float>(fade_row) /
                       static_cast<float>(bottom_rows);
      } else {
        axial = static_cast<float>(row) * top_length /
                static_cast<float>(top_rows);
      }

      const float along = axial / length;
      const float inner = top_inner + (bottom_inner - top_inner) * along;
      const float inner_u =
          radius > 0.0001f ? inner / (radius + radius) : 0.5f;
      const float left_inner = std::max(inner - radius, 0.0f);
      const float right_inner = std::max(radius - inner, 0.0f);
      mesh.verts.push_back(generated_spotlight_vertex(
          -radius, axial, 0.0f, 0, 0, 0, 0.0f, along, 0.0f, 0.0f));
      mesh.verts.push_back(generated_spotlight_vertex(
          left_inner, axial, 0.0f, 0, 0, 0, inner_u, along, intensity,
          intensity));
      mesh.verts.push_back(generated_spotlight_vertex(
          right_inner, axial, 0.0f, 0, 0, 0, 1.0f - inner_u, along,
          intensity, intensity));
      mesh.verts.push_back(generated_spotlight_vertex(
          radius, axial, 0.0f, 0, 0, 0, 1.0f, along, 0.0f, 0.0f));
      radius += row < top_rows ? top_radius_step : bottom_radius_step;
    }
    for (int row = 0; row + 1 < rows; ++row) {
      const uint16_t a = static_cast<uint16_t>(row * 4);
      const uint16_t b = static_cast<uint16_t>((row + 1) * 4);
      for (int band = 0; band < 3; ++band) {
        const uint16_t a0 = static_cast<uint16_t>(a + band);
        const uint16_t a1 = static_cast<uint16_t>(a + band + 1);
        const uint16_t b0 = static_cast<uint16_t>(b + band);
        const uint16_t b1 = static_cast<uint16_t>(b + band + 1);
        mesh.indices.insert(mesh.indices.end(),
                            {a0, b0, a1, a1, b0, b1});
      }
    }
  }
  finish_generated_spotlight_mesh(mesh);
  return mesh;
}

milo_scene::MeshObj make_generated_spotlight_disc(const char* name,
                                                   bool square) {
  milo_scene::MeshObj mesh;
  mesh.name = name;
  mesh.showing = true;
  if (square) {
    // Local X/Z plane; callers provide either the camera-facing flare basis or
    // the source lens basis.
    mesh.verts = {
        generated_spotlight_vertex(-1, 0, 1, 0, -1, 0, 0, 0),
        generated_spotlight_vertex(1, 0, 1, 0, -1, 0, 1, 0),
        generated_spotlight_vertex(1, 0, -1, 0, -1, 0, 1, 1),
        generated_spotlight_vertex(-1, 0, -1, 0, -1, 0, 0, 1),
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
  } else {
    constexpr int kSegments = 24;
    constexpr float kTwoPi = 6.28318530717958647692f;
    mesh.verts.reserve(kSegments + 1u);
    mesh.indices.reserve(kSegments * 3u);
    mesh.verts.push_back(
        generated_spotlight_vertex(0, 0, 0, 0, -1, 0, 0.5f, 0.5f));
    for (int i = 0; i < kSegments; ++i) {
      const float theta = static_cast<float>(i) / kSegments * kTwoPi;
      const float c = std::cos(theta);
      const float s = std::sin(theta);
      mesh.verts.push_back(generated_spotlight_vertex(
          c, 0, s, 0, -1, 0, c * 0.5f + 0.5f, 0.5f - s * 0.5f));
    }
    for (int i = 0; i < kSegments; ++i) {
      mesh.indices.insert(mesh.indices.end(),
                          {0, static_cast<uint16_t>(i + 1),
                           static_cast<uint16_t>((i + 1) % kSegments + 1)});
    }
  }
  finish_generated_spotlight_mesh(mesh);
  return mesh;
}

}  // namespace

bool world_transform_reverses_winding(
    const std::array<float, 16>& world_transform) {
  return local_basis_determinant(world_transform) < -1.0e-8f;
}

bool mesh_source_winding_reversed(const milo_scene::MeshObj& mesh) {
  size_t forward = 0;
  size_t reversed = 0;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const uint16_t ia = mesh.indices[i + 0];
    const uint16_t ib = mesh.indices[i + 1];
    const uint16_t ic = mesh.indices[i + 2];
    if (ia >= mesh.verts.size() || ib >= mesh.verts.size() ||
        ic >= mesh.verts.size()) {
      continue;
    }
    const auto& a = mesh.verts[ia];
    const auto& b = mesh.verts[ib];
    const auto& c = mesh.verts[ic];
    const float abx = b.px - a.px;
    const float aby = b.py - a.py;
    const float abz = b.pz - a.pz;
    const float acx = c.px - a.px;
    const float acy = c.py - a.py;
    const float acz = c.pz - a.pz;
    const float cx = aby * acz - abz * acy;
    const float cy = abz * acx - abx * acz;
    const float cz = abx * acy - aby * acx;
    const float nx = a.nx + b.nx + c.nx;
    const float ny = a.ny + b.ny + c.ny;
    const float nz = a.nz + b.nz + c.nz;
    const float cross_len2 = cx * cx + cy * cy + cz * cz;
    const float normal_len2 = nx * nx + ny * ny + nz * nz;
    if (cross_len2 <= 1.0e-12f || normal_len2 <= 1.0e-12f) continue;
    const float alignment = cx * nx + cy * ny + cz * nz;
    const float epsilon =
        1.0e-6f * std::sqrt(cross_len2 * normal_len2);
    if (alignment > epsilon) {
      ++forward;
    } else if (alignment < -epsilon) {
      ++reversed;
    }
  }
  return reversed > forward;
}

uint32_t source_mesh_cull_mode(bool material_cull,
                               bool debug_highlighted_mesh,
                               bool world_winding_reversed,
                               bool source_winding_reversed,
                               uint32_t authored_cull_mode) {
  constexpr uint32_t kCullNone = 1;
  constexpr uint32_t kCullCw = 2;
  constexpr uint32_t kCullCcw = 3;
  if (debug_highlighted_mesh || !material_cull) return kCullNone;
  if (world_winding_reversed == source_winding_reversed)
    return authored_cull_mode;
  if (authored_cull_mode == kCullCw) return kCullCcw;
  if (authored_cull_mode == kCullCcw) return kCullCw;
  return authored_cull_mode;
}

SourceTexGenXfm2D source_texgen_xfm_2d(
    uint8_t tex_gen, float m00, float m01, float m10, float m11,
    float tu, float tv) {
  if (tex_gen != 1) return {m00, m01, m10, m11, tu, tv};

  // Exact kXfm conversion observed in live GH2 PS2 RndMat instances.
  // Harmonix authors TexXfm around a centered Y-up coordinate system; the
  // runtime generator stores the equivalent centered V-down transform.
  const float out_m00 = m00;
  const float out_m01 = -m01;
  const float out_m10 = -m10;
  const float out_m11 = m11;
  const float centered_u = 0.5f + tu;
  const float centered_v = 0.5f - tv;
  const float out_tu =
      0.5f - (centered_u * out_m00 + centered_v * out_m10);
  const float out_tv =
      0.5f - (centered_u * out_m01 + centered_v * out_m11);
  return {out_m00, out_m01, out_m10, out_m11, out_tu, out_tv};
}

bool source_material_bypasses_fixed_lighting(
    bool prelit, bool use_environment) {
  // GH2 PS2 Ps2Mat::Select at 0x0019D12C..0x0019D154 stores one light
  // channel when mUseEnviron || !mPreLit. The independent WiiMat::Select
  // signature identifies that computed value as numLightChannels.
  return prelit && !use_environment;
}

MiloSceneRenderer::MaterialUvSamplerDecision choose_material_uv_sampler(
    const MiloSceneRenderer::MaterialUvBounds& final_uv_bounds,
    float scale_u, float scale_v, bool material_tex_anim) {
  const bool bounds_sane =
      final_uv_bounds.valid && std::isfinite(final_uv_bounds.min_u) &&
      std::isfinite(final_uv_bounds.min_v) &&
      std::isfinite(final_uv_bounds.max_u) &&
      std::isfinite(final_uv_bounds.max_v);
  const bool uv_repeats =
      bounds_sane &&
      (final_uv_bounds.min_u < -0.05f ||
       final_uv_bounds.min_v < -0.05f ||
       final_uv_bounds.max_u > 1.05f ||
       final_uv_bounds.max_v > 1.05f);
  return {uv_repeats,
          scale_u > 1.01f || scale_v > 1.01f || material_tex_anim ||
              uv_repeats};
}

void OrbitCamera::eye(float out[3]) const {
  if (result_frame.valid) {
    out[0] = result_frame.position[0];
    out[1] = result_frame.position[1];
    out[2] = result_frame.position[2];
    return;
  }
  if (authored) {
    out[0] = authored_eye[0];
    out[1] = authored_eye[1];
    out[2] = authored_eye[2];
    return;
  }
  // Spherical about target. Up axis is world +Z (GH2 convention). At yaw=0 the
  // camera sits along -Y (in front of the stage looking toward +Y/back).
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  out[0] = target[0] + distance * cp * sy;
  out[1] = target[1] - distance * cp * cy;
  out[2] = target[2] + distance * sp;
}

MiloSceneRenderer::MiloSceneRenderer(Window& win) : win_(&win) {
  dev_ = static_cast<IDirect3DDevice9*>(win.device_ptr());
}

MiloSceneRenderer::~MiloSceneRenderer() {
  debug_venue_inspector_states().erase(this);
  for (auto& [name, state] : flare_visibility_) {
    (void)name;
    if (state.query) state.query->Release();
  }
  for (auto& [mesh, buffers] : static_mesh_buffers_) {
    (void)mesh;
    if (buffers.vertices) buffers.vertices->Release();
    if (buffers.indices) buffers.indices->Release();
  }
  for (auto& kv : tex_)
    if (kv.second) kv.second->Release();
  for (auto* t : text_tex_)
    if (t) t->Release();
}

void MiloSceneRenderer::set_text(std::vector<TextVertex> verts,
                                 const ghogx::asset::Image& atlas) {
  std::vector<TextBatch> batches;
  TextBatch b;
  b.verts = std::move(verts);
  b.atlas = &atlas;
  batches.push_back(std::move(b));
  set_text_batches(std::move(batches));
}

void MiloSceneRenderer::set_text_batches(std::vector<TextBatch> batches) {
  for (auto* t : text_tex_)
    if (t) t->Release();
  text_tex_.clear();
  text_.clear();
  text_transform_spans_.clear();
  for (auto& b : batches) {
    if (b.verts.empty() || !b.atlas || !b.atlas->valid()) continue;
    IDirect3DTexture9* tex = upload(*b.atlas);
    if (!tex) continue;
    text_tex_.push_back(tex);
    text_.push_back(std::move(b.verts));
    text_transform_spans_.push_back(std::move(b.transform_spans));
  }
}

void MiloSceneRenderer::set_viewport(int x, int y, int width, int height) {
  if (width <= 0 || height <= 0) {
    custom_viewport_ = false;
    return;
  }
  custom_viewport_ = true;
  viewport_x_ = x;
  viewport_y_ = y;
  viewport_w_ = width;
  viewport_h_ = height;
}

void MiloSceneRenderer::set_default_camera_aspect(float aspect) {
  default_camera_aspect_ =
      std::isfinite(aspect) && aspect > 0.0f ? aspect : 0.0f;
}

void MiloSceneRenderer::set_clear_color(uint8_t r, uint8_t g, uint8_t b) {
  clear_r_ = r;
  clear_g_ = g;
  clear_b_ = b;
}

void MiloSceneRenderer::set_clear_depth_on_overlay(bool enabled) {
  clear_depth_on_overlay_ = enabled;
}

void MiloSceneRenderer::set_environment_dynamic_lights(bool enabled) {
  force_environment_dynamic_lights_ = enabled;
}

void MiloSceneRenderer::set_world_transform(const std::array<float, 16>& m) {
  world_transform_ = m;
}

void MiloSceneRenderer::set_global_tint(float brightness, float alpha) {
  global_brightness_ = std::clamp(brightness, 0.0f, 1.0f);
  global_alpha_ = std::clamp(alpha, 0.0f, 1.0f);
}

void MiloSceneRenderer::set_additive_blend(bool additive) {
  additive_blend_ = additive;
}

void MiloSceneRenderer::set_active_spotlights(std::vector<SpotlightState> spots) {
  active_spotlight_filter_ = true;
  active_spotlights_.clear();
  for (auto& spot : spots) active_spotlights_[spot.name] = std::move(spot);
}

void MiloSceneRenderer::set_active_particle_systems(
    std::unordered_set<std::string> particle_names) {
  active_particle_filter_ = true;
  active_particle_systems_ = std::move(particle_names);
}

void MiloSceneRenderer::set_particle_intensities(
    std::map<std::string, float> intensities) {
  particle_intensities_ = std::move(intensities);
}

void MiloSceneRenderer::set_particle_sizes(std::map<std::string, float> sizes) {
  particle_sizes_ = std::move(sizes);
}

void MiloSceneRenderer::set_particle_speeds(std::map<std::string, float> speeds) {
  particle_speeds_ = std::move(speeds);
}

void MiloSceneRenderer::set_particle_lifetimes(
    std::map<std::string, float> lifetimes) {
  particle_lifetimes_ = std::move(lifetimes);
}

void MiloSceneRenderer::set_particle_start_colors(
    std::map<std::string, std::array<float, 4>> colors) {
  particle_start_colors_ = std::move(colors);
}

void MiloSceneRenderer::set_particle_end_colors(
    std::map<std::string, std::array<float, 4>> colors) {
  particle_end_colors_ = std::move(colors);
}

void MiloSceneRenderer::set_hidden_meshes(std::unordered_set<std::string> mesh_names) {
  hidden_meshes_ = std::move(mesh_names);
}

void MiloSceneRenderer::set_post_text_meshes(
    std::unordered_set<std::string> mesh_names) {
  post_text_meshes_ = std::move(mesh_names);
}

void MiloSceneRenderer::set_post_text_mesh_world_offsets(
    std::map<std::string, std::array<float, 3>> offsets) {
  post_text_mesh_world_offsets_ = std::move(offsets);
}

void MiloSceneRenderer::set_post_text_mesh_text_split(size_t batch_index) {
  post_text_mesh_text_split_ = batch_index;
}

void MiloSceneRenderer::set_material_alpha_multipliers(
    std::map<std::string, float> material_alpha) {
  material_alpha_ = std::move(material_alpha);
}

void MiloSceneRenderer::set_material_alpha_overrides(
    std::map<std::string, float> material_alpha) {
  material_alpha_overrides_ = std::move(material_alpha);
}

void MiloSceneRenderer::set_material_color_overrides(
    std::map<std::string, std::array<float, 4>> material_colors) {
  material_colors_ = std::move(material_colors);
}

void MiloSceneRenderer::set_material_texture_overrides(
    std::map<std::string, std::string> material_textures) {
  material_textures_ = std::move(material_textures);
}

void MiloSceneRenderer::set_material_tex_transform_overrides(
    std::map<std::string, MaterialTexTransformSample> material_tex_transforms) {
  material_tex_transforms_ = std::move(material_tex_transforms);
}

void MiloSceneRenderer::set_environment_lighting_enabled(bool enabled) {
  environment_lighting_enabled_ = enabled;
}

void MiloSceneRenderer::set_environment_color_overrides(
    std::map<std::string, std::array<float, 4>> environment_colors) {
  environment_color_overrides_ = std::move(environment_colors);
}

void MiloSceneRenderer::set_environment_fog_overrides(
    std::map<std::string, EnvironmentFogOverride> environment_fog) {
  environment_fog_overrides_ = std::move(environment_fog);
}

void MiloSceneRenderer::set_light_color_overrides(
    std::map<std::string, std::array<float, 4>> light_colors) {
  light_color_overrides_ = std::move(light_colors);
}

void MiloSceneRenderer::set_light_state_overrides(
    std::map<std::string, LightStateOverride> light_states) {
  light_state_overrides_ = std::move(light_states);
}

void MiloSceneRenderer::set_default_environment(std::string environment_name) {
  default_environment_ = std::move(environment_name);
}

bool MiloSceneRenderer::apply_environment_lighting_state(
    const std::string& environment_name) {
  if (!dev_) return false;
  install_scene_fill_lighting(dev_);
  if (!environment_lighting_enabled_ ||
      env_enabled("GHOGX_DISABLE_ENVIRON_LIGHTING")) {
    return false;
  }

  const auto* env = scene_.find_environ(environment_name);
  if (!env || !env->decoded) return false;

  std::array<float, 4> env_color = {1.0f, 1.0f, 1.0f, 1.0f};
  bool has_env_color = false;
  if (environ_color_sane(*env)) {
    env_color = {env->color_a[0], env->color_a[1], env->color_a[2],
                 env->color_a[3]};
    if (const auto color_it = environment_color_overrides_.find(env->name);
        color_it != environment_color_overrides_.end()) {
      env_color = color_it->second;
    }
    has_env_color = true;
  }

  std::vector<ApproxLightCandidate> approx_directional_lights;
  size_t approx_lights = 0;
  // RndEnviron keeps ambient_color and lights_approx as separate source
  // channels. Submit approximate lights as directional lights here; folding
  // them into ambient double-counts them and erases their directionality.
  if (!env_enabled("GHOGX_DISABLE_ENVIRON_APPROX_LIGHTS")) {
    for (const auto& ref : env->lights) {
      const auto* light = scene_.find_light(ref);
      if (!light || !light_color_sane(*light)) continue;
      std::array<float, 4> light_color = {light->color[0], light->color[1],
                                          light->color[2], light->color[3]};
      if (const auto color_it = light_color_overrides_.find(ref);
          color_it != light_color_overrides_.end()) {
        light_color = color_it->second;
      }
      int light_type = light->type;
      if (const auto state_it = light_state_overrides_.find(ref);
          state_it != light_state_overrides_.end()) {
        if (state_it->second.has_color) light_color = state_it->second.color;
        if (state_it->second.has_type) light_type = state_it->second.type;
      }
      if (!is_authored_approx_environment_light_type(light_type)) continue;
      auto light_world = xfm_to_mat4(light->world_stored);
      if (const auto xfm_it = mesh_transform_offsets_.find(ref);
          xfm_it != mesh_transform_offsets_.end()) {
        apply_mesh_transform_sample(light_world, xfm_it->second);
      }
      ApproxLightCandidate candidate;
      candidate.ref = ref;
      candidate.direction =
          authored_directional_emission_from_world(light_world);
      candidate.color = light_color;
      candidate.score = std::max({std::fabs(light_color[0]),
                                  std::fabs(light_color[1]),
                                  std::fabs(light_color[2])});
      approx_directional_lights.push_back(candidate);
      ++approx_lights;
    }
  }
  if (has_env_color) dev_->SetRenderState(D3DRS_AMBIENT,
                                          d3d_color_from_rgba(env_color));
  install_approx_scene_lights(dev_, approx_directional_lights, false);

  if (env_enabled("GHOGX_DISABLE_ENVIRON_DYNAMIC_LIGHTS")) return true;

  DWORD slot = kAuthoredLightFirstSlot;
  size_t enabled_lights = 0;
  for (const auto& ref : env->lights) {
    if (slot >= kAuthoredLightFirstSlot + kAuthoredLightSlotCount) break;
    const auto* light = scene_.find_light(ref);
    if (!light || !light_color_sane(*light)) continue;
    std::array<float, 4> light_color = {light->color[0], light->color[1],
                                        light->color[2], light->color[3]};
    if (const auto color_it = light_color_overrides_.find(ref);
        color_it != light_color_overrides_.end()) {
      light_color = color_it->second;
    }
    float light_range = light->range;
    int light_type = light->type;
    if (const auto state_it = light_state_overrides_.find(ref);
        state_it != light_state_overrides_.end()) {
      if (state_it->second.has_color) light_color = state_it->second.color;
      if (state_it->second.has_range) light_range = state_it->second.range;
      if (state_it->second.has_type) light_type = state_it->second.type;
    }
    auto light_world = xfm_to_mat4(light->world_stored);
    if (const auto xfm_it = mesh_transform_offsets_.find(ref);
        xfm_it != mesh_transform_offsets_.end()) {
      apply_mesh_transform_sample(light_world, xfm_it->second);
    }
    D3DLIGHT9 dl{};
    dl.Diffuse.r =
        std::clamp(light_color[0], 0.0f, kMaxAuthoredLightColor);
    dl.Diffuse.g =
        std::clamp(light_color[1], 0.0f, kMaxAuthoredLightColor);
    dl.Diffuse.b =
        std::clamp(light_color[2], 0.0f, kMaxAuthoredLightColor);
    dl.Diffuse.a = std::clamp(light_color[3], 0.0f, 1.0f);
    if (!is_authored_real_environment_light_type(light_type)) continue;
    if (light_type == 2) {
      const auto direction =
          authored_fake_spot_direction_from_world(light_world);
      const float dx = direction[0];
      const float dy = direction[1];
      const float dz = direction[2];
      const float len = vec_len3(dx, dy, dz);
      if (len <= 0.0001f) continue;
      dl.Type = D3DLIGHT_SPOT;
      dl.Direction = {dx / len, dy / len, dz / len};
      dl.Position = {light_world[12], light_world[13], light_world[14]};
      dl.Range = std::max(light_range, 1.0f);
      dl.Attenuation0 = 1.0f;
      constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
      dl.Theta = 20.0f * kDegToRad;
      dl.Phi = 45.0f * kDegToRad;
    } else if (light_type == 0) {
      dl.Type = D3DLIGHT_POINT;
      dl.Position = {light_world[12], light_world[13], light_world[14]};
      dl.Range = std::max(light_range, 1.0f);
      dl.Attenuation0 = 1.0f;
    } else {
      continue;
    }
    dev_->SetLight(slot, &dl);
    dev_->LightEnable(slot, TRUE);
    ++slot;
    ++enabled_lights;
  }
  if (env_enabled("GHOGX_LOG_ENVIRON_LIGHTING")) {
    static std::unordered_set<std::string> logged_overlay_envs;
    const std::string key = environ_lighting_debug_signature(
        environment_name, enabled_lights, approx_lights, env_color,
        approx_directional_lights);
    if (logged_overlay_envs.insert(key).second) {
      std::fprintf(stderr,
                   "[milo_scene] Environ lighting state applied: %s "
                   "real_lights=%zu approx_lights=%zu "
                   "approx_directional=%zu refs=%zu ambient=(%.3f %.3f %.3f %.3f)",
                   environment_name.c_str(), enabled_lights, approx_lights,
                   approx_directional_lights.size(), env->lights.size(),
                   env_color[0], env_color[1], env_color[2], env_color[3]);
      if (approx_directional_lights.empty()) {
        std::fprintf(stderr, " approx=-");
      } else {
        std::fprintf(stderr, " approx=");
        for (size_t i = 0; i < approx_directional_lights.size(); ++i) {
          const auto& a = approx_directional_lights[i];
          std::fprintf(stderr,
                       "%s%s:type1 color=(%.3f %.3f %.3f %.3f) dir=(%.3f %.3f %.3f)",
                       i == 0 ? "" : ";", a.ref.c_str(), a.color[0],
                       a.color[1], a.color[2], a.color[3], a.direction[0],
                       a.direction[1], a.direction[2]);
        }
      }
      std::fprintf(stderr, "\n");
    }
  }
  return true;
}

void MiloSceneRenderer::set_mesh_translation_offsets(
    std::map<std::string, std::array<float, 3>> offsets) {
  mesh_translation_offsets_ = std::move(offsets);
  mesh_transform_offsets_.clear();
  for (const auto& [mesh, offset] : mesh_translation_offsets_) {
    MeshTransformSample sample;
    sample.has_translation = true;
    sample.translation = offset;
    mesh_transform_offsets_[mesh] = sample;
  }
}

MiloSceneRenderer::MeshTransformSample
MiloSceneRenderer::compose_transform_animation_sample(
    const std::array<float, 16>& base_local,
    const MeshTransformSample* current,
    const MeshTransformSample& incoming) {
  // GH2 RndTransAnim::SetFrame 0x1E0FA0 copies the current LocalXfm before
  // MakeTransform(0x1E0790), then publishes it. Preserve unkeyed channels and
  // the exact zero-blend pose, including scale/handedness. Do not advance this
  // state per draw: one object may be drawn by several cameras/passes.
  auto local = base_local;
  if (current) apply_mesh_transform_sample(local, *current);
  apply_mesh_transform_sample(local, incoming);
  auto result = incoming;
  result.has_local_transform = true;
  result.local_transform = local;
  return result;
}

bool MiloSceneRenderer::compose_transform_animation_sample(
    const std::string& target, const MeshTransformSample* current,
    MeshTransformSample& incoming) const {
  SceneNodeXfm node;
  if (!scene_node_xfm_for(scene_, target, node)) return false;
  incoming = compose_transform_animation_sample(node.local, current, incoming);
  return true;
}

void MiloSceneRenderer::set_mesh_transform_offsets(
    std::map<std::string, MeshTransformSample> offsets) {
  mesh_transform_offsets_ = std::move(offsets);
  mesh_translation_offsets_.clear();
  for (const auto& [mesh, sample] : mesh_transform_offsets_) {
    if (sample.has_translation) mesh_translation_offsets_[mesh] = sample.translation;
  }
  cam_.result_frame = {};
  if (!selected_camera_name_.empty()) {
    const auto sample_it = mesh_transform_offsets_.find(selected_camera_name_);
    if (sample_it != mesh_transform_offsets_.end()) {
      for (const auto& camera : scene_.cams) {
        if (!camera.decoded || camera.name != selected_camera_name_) continue;
        std::array<float, 16> world = {
            camera.local.rot[0][0], camera.local.rot[0][1], camera.local.rot[0][2], 0.0f,
            camera.local.rot[1][0], camera.local.rot[1][1], camera.local.rot[1][2], 0.0f,
            camera.local.rot[2][0], camera.local.rot[2][1], camera.local.rot[2][2], 0.0f,
            camera.local.pos[0], camera.local.pos[1], camera.local.pos[2], 1.0f};
        apply_mesh_transform_sample(world, sample_it->second);
        world = scene_.world_matrix(camera, world);
        cam_.result_frame.valid = true;
        cam_.result_frame.source = selected_camera_name_;
        const auto right = normalized3({world[0], world[1], world[2]});
        const auto forward = normalized3({world[4], world[5], world[6]});
        const auto up = normalized3({world[8], world[9], world[10]});
        for (int axis = 0; axis < 3; ++axis) {
          cam_.result_frame.position[axis] = world[12 + axis];
          cam_.result_frame.right[axis] = right[axis];
          cam_.result_frame.forward[axis] = forward[axis];
          cam_.result_frame.up[axis] = up[axis];
        }
        break;
      }
    }
  }
}

void MiloSceneRenderer::set_mesh_position_overrides(
    std::map<std::string, std::vector<std::array<float, 3>>> positions) {
  mesh_position_overrides_ = std::move(positions);
}

void MiloSceneRenderer::set_mesh_normal_overrides(
    std::map<std::string, std::vector<std::array<float, 3>>> normals) {
  mesh_normal_overrides_ = std::move(normals);
}

void MiloSceneRenderer::set_mesh_texcoord_overrides(
    std::map<std::string, std::vector<std::array<float, 2>>> texcoords) {
  mesh_texcoord_overrides_ = std::move(texcoords);
}

void MiloSceneRenderer::set_mesh_color_overrides(
    std::map<std::string, std::vector<std::array<float, 4>>> colors) {
  mesh_color_overrides_ = std::move(colors);
}

void MiloSceneRenderer::set_mesh_anim_blends(
    std::map<std::string, float> blends) {
  mesh_anim_blends_ = std::move(blends);
}

void MiloSceneRenderer::set_face_camera_meshes(
    std::unordered_set<std::string> mesh_names) {
  face_camera_meshes_ = std::move(mesh_names);
}

void MiloSceneRenderer::trigger_mesh_pulse(const std::string& mesh_name,
                                            float amplitude) {
  mesh_pulses_[mesh_name] = std::max(mesh_pulses_[mesh_name], amplitude);
}

void MiloSceneRenderer::trigger_mesh_translation_anim(
    const std::string& mesh_name, std::vector<MeshAnimKey> keys,
    float frames_per_second) {
  MeshTransformAnim anim;
  anim.translation_keys = std::move(keys);
  trigger_mesh_transform_anim(mesh_name, std::move(anim), frames_per_second);
}

void MiloSceneRenderer::trigger_mesh_transform_anim(
    const std::string& mesh_name, MeshTransformAnim transform_anim,
    float frames_per_second, bool loop) {
  if (frames_per_second <= 0.0f) return;
  auto empty = [](const MeshTransformAnim& a) {
    return a.translation_keys.empty() && a.rotation_keys.empty() &&
           a.scale_keys.empty();
  };
  if (empty(transform_anim)) return;
  std::sort(transform_anim.translation_keys.begin(),
            transform_anim.translation_keys.end(),
            [](const MeshAnimKey& a, const MeshAnimKey& b) {
              return a.frame < b.frame;
            });
  std::sort(transform_anim.rotation_keys.begin(),
            transform_anim.rotation_keys.end(),
            [](const MeshQuatAnimKey& a, const MeshQuatAnimKey& b) {
              return a.frame < b.frame;
            });
  std::sort(transform_anim.scale_keys.begin(), transform_anim.scale_keys.end(),
            [](const MeshAnimKey& a, const MeshAnimKey& b) {
              return a.frame < b.frame;
            });
  ActiveMeshAnim active;
  active.anim = std::move(transform_anim);
  active.frames_per_second = frames_per_second;
  active.elapsed = 0.0f;
  active.loop = loop;
  active_mesh_anims_[mesh_name] = std::move(active);
  if (env_enabled("GHOGX_LOG_MESH_ANIM")) {
    std::fprintf(stderr, "[milo_scene] anim %s target=%s fps=%.1f loop=%d\n",
                 loop ? "loop" : "one-shot", mesh_name.c_str(),
                 frames_per_second, loop ? 1 : 0);
  }
}

void MiloSceneRenderer::update(float dt_seconds) {
  if (dt_seconds <= 0.0f) return;
  if (debug_venue_inspector_enabled() && !scene_.meshes.empty()) {
    debug_venue_inspector_states()[this].dt = dt_seconds;
  }
  particle_time_ += dt_seconds;
  if (!mesh_pulses_.empty()) {
    for (auto it = mesh_pulses_.begin(); it != mesh_pulses_.end();) {
      it->second = std::max(0.0f, it->second - dt_seconds * 10.0f);
      if (it->second <= 0.001f) {
        it = mesh_pulses_.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (!active_mesh_anims_.empty()) {
    for (auto it = active_mesh_anims_.begin(); it != active_mesh_anims_.end();) {
      it->second.elapsed += dt_seconds;
      const float frame = it->second.elapsed * it->second.frames_per_second;
      float last_frame = 0.0f;
      if (!it->second.anim.translation_keys.empty()) {
        last_frame =
            std::max(last_frame, it->second.anim.translation_keys.back().frame);
      }
      if (!it->second.anim.rotation_keys.empty()) {
        last_frame =
            std::max(last_frame, it->second.anim.rotation_keys.back().frame);
      }
      if (!it->second.anim.scale_keys.empty()) {
        last_frame = std::max(last_frame, it->second.anim.scale_keys.back().frame);
      }
      if (last_frame > 0.0f && frame > last_frame) {
        if (it->second.loop) {
          const float wrapped = std::fmod(frame, last_frame);
          it->second.elapsed = wrapped / it->second.frames_per_second;
          ++it;
        } else {
          it = active_mesh_anims_.erase(it);
        }
      } else {
        ++it;
      }
    }
  }
}

IDirect3DTexture9* MiloSceneRenderer::upload(const ghogx::asset::Image& img) {
  if (!dev_ || !img.valid()) return nullptr;
  IDirect3DTexture9* t = nullptr;
  if (FAILED(dev_->CreateTexture(static_cast<UINT>(img.width),
                                 static_cast<UINT>(img.height), 1, 0,
                                 D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)))
    return nullptr;
  D3DLOCKED_RECT lr;
  if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
    for (int y = 0; y < img.height; ++y) {
      auto* dst = static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
      const uint8_t* src = img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
      for (int x = 0; x < img.width; ++x) {
        dst[x * 4 + 0] = src[x * 4 + 2];  // R->B
        dst[x * 4 + 1] = src[x * 4 + 1];  // G
        dst[x * 4 + 2] = src[x * 4 + 0];  // B->R
        dst[x * 4 + 3] = src[x * 4 + 3];  // A
      }
    }
    t->UnlockRect(0);
  }
  return t;
}

const milo_scene::MatObj* MiloSceneRenderer::find_material(
    const std::string& name) const {
  const auto it = materials_by_name_.find(name);
  return it != materials_by_name_.end() ? it->second : nullptr;
}

void MiloSceneRenderer::select_gh1_crowd_region(int index) {
  if (gh1_crowd_regions_.regions().empty()) return;
  pending_gh1_crowd_auto_ = index < 0 && !last_crowd_camera_valid_;
  const bool selected = index >= 0 ? gh1_crowd_regions_.select(index) :
      (!pending_gh1_crowd_auto_ && gh1_crowd_regions_.select_auto(
          last_crowd_camera_view_, last_crowd_camera_projection_));
  const int active = gh1_crowd_regions_.selected();
  std::fprintf(stderr,
      "[crowd_region] request=%d selected=%d regions=%zu flat_excluded=%zu total=%zu "
      "selection_ok=%d camera=%s promoted=%zu replacement_3d_pool=wired "
      "source=SLUS_212.24:1727B0\n",
      index, active, gh1_crowd_regions_.regions().size(),
      active >= 0 ? gh1_crowd_regions_.regions()[active].members.size() : 0,
      gh1_crowd_regions_.instance_count(), selected ? 1 : 0,
      pending_gh1_crowd_auto_ ? "pending_first_submission" : "previous_submission",
      gh1_crowd_regions_.promoted_count());
}

void MiloSceneRenderer::set_gh1_crowd_sizes(float promoted_fraction,
                                            float flat_fraction) {
  gh1_crowd_regions_.set_sizes(promoted_fraction, flat_fraction);
  if (!gh1_crowd_regions_.regions().empty()) {
    std::fprintf(
        stderr,
        "[crowd_region] sizes promoted_fraction=%.3f flat_fraction=%.3f "
        "promoted=%zu active_flat=%zu total=%zu "
        "source=SLUS_212.24:172EC8\n",
        gh1_crowd_regions_.promoted_fraction(),
        gh1_crowd_regions_.flat_fraction(),
        gh1_crowd_regions_.promoted_count(),
        gh1_crowd_regions_.active_flat_count(),
        gh1_crowd_regions_.instance_count());
  }
}

std::vector<std::array<float, 16>>
MiloSceneRenderer::gh1_crowd_replacement_worlds(bool include_far) const {
  return include_far ? gh1_crowd_regions_.replacement_worlds()
                     : gh1_crowd_regions_.promoted_worlds();
}

void MiloSceneRenderer::exclude_gh1_crowd_already_owned_by(
    const MiloSceneRenderer& owner) {
  nonowning_gh1_crowd_drawables_ =
      Gh1CrowdRegions::duplicate_drawables(owner.scene_, scene_);
  if (!nonowning_gh1_crowd_drawables_.empty()) {
    std::fprintf(stderr,
        "[crowd_region] single_owner=%s secondary=%s duplicate_draws=%zu "
        "match=ownership_template_geometry_instances source=Arena::Crowd\n",
        owner.scene_.dir_name.c_str(), scene_.dir_name.c_str(),
        nonowning_gh1_crowd_drawables_.size());
  }
}

void MiloSceneRenderer::set_scene(
    milo_scene::Scene scene,
    const std::map<std::string, ghogx::asset::Image>& textures) {
  for (auto& [mesh, buffers] : static_mesh_buffers_) {
    (void)mesh;
    if (buffers.vertices) buffers.vertices->Release();
    if (buffers.indices) buffers.indices->Release();
  }
  static_mesh_buffers_.clear();
  source_mesh_winding_reversed_.clear();
  for (auto& [name, state] : flare_visibility_) {
    (void)name;
    if (state.query) state.query->Release();
  }
  flare_visibility_.clear();
  scene_ = std::move(scene);
  nonowning_gh1_crowd_drawables_.clear();
  gh1_crowd_regions_.rebuild(scene_);
  last_crowd_camera_valid_ = false;
  pending_gh1_crowd_auto_ = false;
  selected_camera_name_.clear();
  materials_by_name_.clear();
  for (const auto& mat : scene_.mats) {
    materials_by_name_[mat.name] = &mat;
  }
  meshes_by_name_.clear();
  for (const auto& mesh : scene_.meshes) {
    meshes_by_name_.emplace(mesh.name, &mesh);
  }
  base_mesh_worlds_.clear();
  base_mesh_worlds_.reserve(scene_.meshes.size());
  for (const auto& mesh : scene_.meshes) {
    base_mesh_worlds_.emplace(&mesh, scene_.world_matrix(mesh));
    source_mesh_winding_reversed_.emplace(
        &mesh, mesh_source_winding_reversed(mesh));
  }
  groups_by_name_.clear();
  for (const auto& group : scene_.groups) {
    groups_by_name_.emplace(group.name, &group);
  }
  authored_parents_.clear();
  for (const auto& group : scene_.groups) {
    if (!group.parent.empty()) {
      authored_parents_.emplace(group.name, group.parent);
    }
  }
  for (const auto& mesh : scene_.meshes) {
    if (!mesh.parent.empty()) {
      authored_parents_.emplace(mesh.name, mesh.parent);
    }
  }
  for (const auto& trans : scene_.transes) {
    if (!trans.parent.empty()) {
      authored_parents_.emplace(trans.name, trans.parent);
    }
  }
  for (const auto& placer : scene_.band_placers) {
    if (!placer.parent.empty()) {
      authored_parents_.emplace(placer.name, placer.parent);
    }
  }
  for (const auto& group : scene_.groups) {
    for (const auto& child : group.children) {
      authored_parents_.emplace(child, group.name);
    }
  }
  ordered_mesh_draws_.clear();
  size_t multi_mesh_instances = 0;
  for (const auto& multi_mesh : scene_.multi_meshes)
    multi_mesh_instances += multi_mesh.instances.size();
  ordered_mesh_draws_.reserve(scene_.meshes.size() + multi_mesh_instances);
  std::unordered_set<std::string> queued_drawables;
  queued_drawables.reserve(scene_.meshes.size() + scene_.multi_meshes.size());
  std::unordered_set<std::string> grouped_meshes(scene_.grouped_meshes.begin(),
                                                 scene_.grouped_meshes.end());
  std::unordered_set<std::string> grouped_multi_meshes(
      scene_.grouped_multi_meshes.begin(),
      scene_.grouped_multi_meshes.end());
  std::unordered_map<std::string, const milo_scene::MultiMeshObj*>
      multi_meshes_by_name;
  for (const auto& multi_mesh : scene_.multi_meshes)
    multi_meshes_by_name.emplace(multi_mesh.name, &multi_mesh);
  size_t missing_multi_mesh_templates = 0;
  std::unordered_set<std::string> visited_draw_groups;
  std::function<void(const std::string&)> queue_authored_draw =
      [&](const std::string& name) {
    for (const auto& mesh : scene_.meshes) {
      if (mesh.name == name && queued_drawables.insert(name).second) {
        ordered_mesh_draws_.push_back(OrderedMeshDraw{&mesh, nullptr, nullptr});
        return;
      }
    }
    const auto multi_mesh_it = multi_meshes_by_name.find(name);
    if (multi_mesh_it != multi_meshes_by_name.end() &&
        multi_mesh_it->second &&
        queued_drawables.insert(name).second) {
      const auto* multi_mesh = multi_mesh_it->second;
      const auto mesh_it = meshes_by_name_.find(multi_mesh->mesh);
      if (mesh_it == meshes_by_name_.end() || !mesh_it->second) {
        ++missing_multi_mesh_templates;
        return;
      }
      for (const auto& instance : multi_mesh->instances) {
        ordered_mesh_draws_.push_back(
            OrderedMeshDraw{mesh_it->second, multi_mesh, &instance});
      }
      return;
    }
    const auto group_it = groups_by_name_.find(name);
    if (group_it == groups_by_name_.end() || !group_it->second ||
        !visited_draw_groups.insert(name).second) {
      return;
    }
    for (const auto& child : group_it->second->children) {
      queue_authored_draw(child);
    }
  };
  for (const auto& name : scene_.draw_order) {
    queue_authored_draw(name);
  }
  // GH1 Environ revision 1 is itself a drawable in the authored root graph.
  // Its legacy drawable list is therefore an additional draw traversal, not
  // part of ObjectDir's ungrouped-object fallback.
  for (const auto& env : scene_.environs) {
    if (!env.decoded || !env.legacy_drawable_showing) continue;
    for (const auto& mesh : env.legacy_drawable_refs) {
      queue_authored_draw(mesh);
    }
  }
  const bool legacy_authored_root_only =
      scene_.dir_revision == 10 && !scene_.draw_order.empty();
  for (const auto& mesh : scene_.meshes) {
    // GH1 revision-10 RndDirs draw through their selected authored root View.
    // Ungrouped objects include placement, crowd-limit, and editor helpers;
    // ObjectDir does not implicitly append them after drawing that root.
    if (legacy_authored_root_only) continue;
    if (grouped_meshes.find(mesh.name) != grouped_meshes.end()) {
      continue;
    }
    queue_authored_draw(mesh.name);
  }
  for (const auto& multi_mesh : scene_.multi_meshes) {
    if (legacy_authored_root_only) continue;
    if (grouped_multi_meshes.find(multi_mesh.name) !=
        grouped_multi_meshes.end()) {
      continue;
    }
    queue_authored_draw(multi_mesh.name);
  }
  if (!scene_.multi_meshes.empty()) {
    std::fprintf(
        stderr,
        "[milo_scene] RndMultiMesh draw plan: objects=%zu instances=%zu "
        "submitted=%zu missing_templates=%zu source=RndMultiMesh::DrawShowing\n",
        scene_.multi_meshes.size(), multi_mesh_instances,
        std::count_if(ordered_mesh_draws_.begin(), ordered_mesh_draws_.end(),
                      [](const OrderedMeshDraw& draw) {
                        return draw.multi_mesh != nullptr;
                      }),
        missing_multi_mesh_templates);
  }
  if (gh1_crowd_regions_.instance_count() &&
      std::getenv("GHOGX_DEBUG_VENUE_FILTERS")) {
    size_t standalone_cards = 0;
    for (const auto& draw : ordered_mesh_draws_) {
      if (!draw.mesh || draw.multi_mesh ||
          draw.mesh->name.rfind("Crowd_", 0) != 0) continue;
      if (standalone_cards++ < 16) {
        std::fprintf(stderr,
            "[crowd_region] scene=%s standalone=%s showing=%d parent=%s\n",
            scene_.dir_name.c_str(), draw.mesh->name.c_str(),
            draw.mesh->showing, draw.mesh->parent.c_str());
      }
    }
    std::fprintf(stderr,
        "[crowd_region] scene=%s standalone_cards=%zu draws=%zu\n",
        scene_.dir_name.c_str(), standalone_cards, ordered_mesh_draws_.size());
  }
  spotlight_template_meshes_.clear();
  auto add_spotlight_group_meshes = [&](const std::string& group_name) {
    std::vector<std::string> pending{group_name};
    std::unordered_set<std::string> seen_groups;
    while (!pending.empty()) {
      const std::string current = pending.back();
      pending.pop_back();
      if (!seen_groups.insert(current).second) continue;
      const auto group_it = groups_by_name_.find(current);
      if (group_it != groups_by_name_.end() && group_it->second) {
        for (const auto& child : group_it->second->children) {
          if (child.rfind(".mesh") != std::string::npos) {
            spotlight_template_meshes_.insert(child);
          } else if (child.rfind(".grp") != std::string::npos) {
            pending.push_back(child);
          }
        }
      }
    }
  };
  for (const auto& spot : scene_.spotlights) {
    add_spotlight_group_meshes(spot.group);
    if (!spot.target.empty()) spotlight_template_meshes_.insert(spot.target);
    if (!spot.circle_mesh.empty()) {
      spotlight_template_meshes_.insert(spot.circle_mesh);
    }
    for (const auto& mesh : spot.instance_meshes) {
      if (!mesh.empty()) spotlight_template_meshes_.insert(mesh);
    }
  }
  generated_spotlight_beams_.clear();
  for (const auto& spot : scene_.spotlights) {
    auto beam = make_generated_spotlight_beam(spot);
    if (beam.decoded) {
      generated_spotlight_beams_.emplace(spot.name, std::move(beam));
    }
  }
  generated_spotlight_disc_ =
      make_generated_spotlight_disc("__generated_spotlight_disc__", false);
  generated_spotlight_flare_ =
      make_generated_spotlight_disc("__generated_spotlight_flare__", true);
  active_spotlight_filter_ = false;
  active_spotlights_.clear();
  mesh_environments_.clear();
  legacy_environment_drawables_.clear();

  size_t mesh_environment_conflicts = 0;
  auto assign_group_environments =
      [&](auto&& self, const std::string& group_name, std::string current_env,
          std::unordered_set<std::string>& visiting) -> void {
    if (!visiting.insert(group_name).second) return;
    const auto group_it = groups_by_name_.find(group_name);
    if (group_it == groups_by_name_.end() || !group_it->second) {
      visiting.erase(group_name);
      return;
    }
    const auto& group = *group_it->second;
    if (!group.environment_ref.empty()) current_env = group.environment_ref;
    const auto assign_mesh_environment =
        [&](const std::string& mesh_name) {
      if (current_env.empty()) return;
      const auto [it, inserted] =
          mesh_environments_.emplace(mesh_name, current_env);
      if (!inserted && it->second != current_env) {
        ++mesh_environment_conflicts;
      }
    };
    const auto assign_child = [&](const std::string& child) {
      if (has_suffix(child, ".mesh")) {
        assign_mesh_environment(child);
      } else if (has_suffix(child, ".mm")) {
        const auto multi_mesh_it = multi_meshes_by_name.find(child);
        if (multi_mesh_it != multi_meshes_by_name.end() &&
            multi_mesh_it->second) {
          // RndMultiMesh::DrawShowing submits its referenced RndMesh while the
          // owning Group's environment is current. The target renderer keys
          // environment state by the submitted template mesh, so preserve
          // that exact indirection here.
          assign_mesh_environment(multi_mesh_it->second->mesh);
        }
      } else if (groups_by_name_.find(child) != groups_by_name_.end()) {
        self(self, child, current_env, visiting);
      }
    };
    if (!group.draw_only.empty()) {
      assign_child(group.draw_only);
    } else {
      for (const auto& child : group.children) assign_child(child);
    }
    visiting.erase(group_name);
  };
  for (const auto& group : scene_.groups) {
    std::unordered_set<std::string> visiting;
    assign_group_environments(assign_group_environments, group.name, {},
                               visiting);
  }
  for (const auto& env : scene_.environs) {
    if (!env.decoded || !env.legacy_drawable_showing) continue;
    for (const auto& mesh : env.legacy_drawable_refs) {
      const auto it = mesh_environments_.find(mesh);
      if (it != mesh_environments_.end() && it->second != env.name) {
        ++mesh_environment_conflicts;
      }
      mesh_environments_[mesh] = env.name;
      legacy_environment_drawables_.insert(mesh);
    }
  }
  if (env_enabled("GHOGX_LOG_ENVIRON_MESHES")) {
    size_t decoded_refs = 0;
    size_t missing_refs = 0;
    for (const auto& [mesh, env_name] : mesh_environments_) {
      (void)mesh;
      const auto* env = scene_.find_environ(env_name);
      if (env && environ_color_sane(*env)) {
        ++decoded_refs;
      } else {
        ++missing_refs;
      }
    }
    std::fprintf(stderr,
                 "[milo_scene] group environment map: groups=%zu meshes=%zu "
                 "decoded_refs=%zu missing_refs=%zu conflicts=%zu\n",
                 scene_.groups.size(), mesh_environments_.size(),
                 decoded_refs, missing_refs, mesh_environment_conflicts);
  }

  // Upload every texture once, keyed by its .tex entry name.
  for (auto& kv : tex_)
    if (kv.second) kv.second->Release();
  tex_.clear();
  const auto multimesh_alpha_textures =
      source_multimesh_alpha_textures(scene_);
  for (const auto& kv : textures) {
    if (env_enabled("GHOGX_LOG_TEXTURE_ALPHA") &&
        mesh_matches_env_spec("GHOGX_LOG_TEXTURE_ALPHA_MATCH", kv.first)) {
      uint8_t min_alpha = 255;
      uint8_t max_alpha = 0;
      std::array<uint8_t, 3> min_rgb = {255, 255, 255};
      std::array<uint8_t, 3> max_rgb = {0, 0, 0};
      size_t transparent = 0;
      size_t partial = 0;
      size_t black = 0;
      for (size_t i = 0; i + 3 < kv.second.rgba.size(); i += 4) {
        for (size_t channel = 0; channel < 3; ++channel) {
          min_rgb[channel] = std::min(min_rgb[channel], kv.second.rgba[i + channel]);
          max_rgb[channel] = std::max(max_rgb[channel], kv.second.rgba[i + channel]);
        }
        const uint8_t alpha = kv.second.rgba[i + 3];
        min_alpha = std::min(min_alpha, alpha);
        max_alpha = std::max(max_alpha, alpha);
        transparent += alpha == 0 ? 1u : 0u;
        partial += alpha > 0 && alpha < 255 ? 1u : 0u;
        black += kv.second.rgba[i] == 0 && kv.second.rgba[i + 1] == 0 &&
                         kv.second.rgba[i + 2] == 0
                     ? 1u
                     : 0u;
      }
      std::fprintf(stderr,
                   "[milo_scene] texture alpha name=%s size=%dx%d "
                   "range=%u..%u transparent=%zu partial=%zu "
                   "rgb=%u..%u,%u..%u,%u..%u black=%zu pixels=%zu\n",
                   kv.first.c_str(), kv.second.width, kv.second.height,
                   static_cast<unsigned>(min_alpha),
                   static_cast<unsigned>(max_alpha), transparent, partial,
                   static_cast<unsigned>(min_rgb[0]),
                   static_cast<unsigned>(max_rgb[0]),
                   static_cast<unsigned>(min_rgb[1]),
                   static_cast<unsigned>(max_rgb[1]),
                   static_cast<unsigned>(min_rgb[2]),
                   static_cast<unsigned>(max_rgb[2]), black,
                   kv.second.rgba.size() / 4);
    }
    const ghogx::asset::Image* upload_image = &kv.second;
    ghogx::asset::Image reconstructed;
    if (multimesh_alpha_textures.find(kv.first) !=
            multimesh_alpha_textures.end() &&
        image_alpha_is_fully_opaque(kv.second)) {
      // All audited GH1 RndMultiMesh0 objects are crowd cards. Seven source
      // packages carry the same binary silhouette alpha; Theatre decodes to
      // the same RGB image with an all-opaque cached CLUT. Apply
      // the established source-family silhouette only to an alpha-blended
      // RndMultiMesh template. Indexed textures outside this exact serialized
      // topology retain their authored CLUT alpha.
      reconstructed = kv.second;
      const size_t changed =
          gh::tex::apply_transparent_black_alpha(reconstructed.rgba);
      if (changed != 0) {
        upload_image = &reconstructed;
        if (env_enabled("GHOGX_LOG_PS2_ALPHA")) {
          std::fprintf(
              stderr,
              "[milo_scene] ps2-alpha texture=%s "
              "mode=source_multimesh_silhouette changed=%zu pixels=%zu "
              "source=RndMultiMesh0+alpha_blend+matched_GH1_RGB\n",
              kv.first.c_str(), changed, reconstructed.rgba.size() / 4);
        }
      }
    }
    IDirect3DTexture9* t = upload(*upload_image);
    if (t) tex_[kv.first] = t;
  }
  std::fprintf(stderr, "[scene3d] uploaded %zu/%zu textures\n", tex_.size(),
               textures.size());

  cam_.authored = false;
  cam_.result_frame = {};
  frame_camera_on_bounds();
  const milo_scene::CamObj* authored = nullptr;
  for (const auto& c : scene_.cams) {
    if (c.decoded && c.name == "default.cam") {
      authored = &c;
      break;
    }
  }
  if (!authored) {
    for (const auto& c : scene_.cams) {
      if (c.decoded && c.name == "guitar_setup.cam") {
        authored = &c;
        break;
      }
    }
  }
  if (!authored) {
    for (const auto& c : scene_.cams) {
      if (c.decoded) {
        authored = &c;
        break;
      }
    }
  }
  if (authored) {
    const auto authored_world = scene_.world_matrix(*authored);
    cam_.authored = true;
    for (int k = 0; k < 3; ++k) {
      cam_.authored_eye[k] = authored_world[12 + k];
      cam_.authored_at[k] =
          authored_world[12 + k] + authored_world[4 + k] * 100.0f;
      cam_.authored_up[k] = authored_world[8 + k];
    }
    cam_.fov = authored->fov;
    cam_.near_z = authored->near_plane;
    cam_.far_z = authored->far_plane;
    std::fprintf(stderr,
                 "[scene3d] authored camera %s eye=(%.1f %.1f %.1f) "
                 "forward=(%.3f %.3f %.3f) up=(%.3f %.3f %.3f) "
                 "fov=%.3f near=%.1f far=%.1f rect=(%.3f %.3f %.3f %.3f) "
                 "zrange=(%.3f %.3f) parent=%s target=%s\n",
                 authored->name.c_str(), cam_.authored_eye[0],
                 cam_.authored_eye[1], cam_.authored_eye[2],
                 authored->local.rot[1][0], authored->local.rot[1][1],
                 authored->local.rot[1][2], cam_.authored_up[0],
                 cam_.authored_up[1], cam_.authored_up[2], cam_.fov,
                 cam_.near_z, cam_.far_z, authored->screen_rect[0],
                 authored->screen_rect[1], authored->screen_rect[2],
                 authored->screen_rect[3], authored->z_range[0],
                 authored->z_range[1],
                 authored->parent.empty() ? "<none>" : authored->parent.c_str(),
                 authored->target.empty() ? "<none>" : authored->target.c_str());
  }
}

void MiloSceneRenderer::set_mesh_transform_offset(
    const std::string& mesh_name, MeshTransformSample sample) {
  mesh_transform_offsets_[mesh_name] = sample;
  if (sample.has_translation)
    mesh_translation_offsets_[mesh_name] = sample.translation;
  else
    mesh_translation_offsets_.erase(mesh_name);
}

void MiloSceneRenderer::set_transform_parent_overrides(
    std::map<std::string, std::string> parents) {
  transform_parent_overrides_ = std::move(parents);
}

bool MiloSceneRenderer::current_scene_node_world(
    const std::string& name, std::array<float, 16>& world) const {
  SceneNodeXfm node;
  if (!scene_node_xfm_for(scene_, name, node)) return false;

  auto parent_for = [&](const std::string& target) {
    SceneNodeXfm value;
    if (!scene_node_xfm_for(scene_, target, value)) return std::string{};
    const auto override_it = transform_parent_overrides_.find(target);
    return override_it == transform_parent_overrides_.end()
               ? value.parent
               : override_it->second;
  };
  auto target_has_sample = [&](const std::string& target) {
    return mesh_transform_offsets_.find(target) !=
               mesh_transform_offsets_.end() ||
           active_mesh_anims_.find(target) != active_mesh_anims_.end();
  };
  auto apply_samples = [&](std::array<float, 16>& local,
                           const std::string& target) {
    if (const auto offset = mesh_transform_offsets_.find(target);
        offset != mesh_transform_offsets_.end()) {
      apply_mesh_transform_sample(local, offset->second);
    }
    if (const auto active = active_mesh_anims_.find(target);
        active != active_mesh_anims_.end()) {
      const float frame =
          active->second.elapsed * active->second.frames_per_second;
      apply_mesh_transform_sample(
          local, sample_transform_anim(active->second.anim, frame));
    }
  };

  std::vector<std::string> ancestors;
  for (std::string parent = parent_for(name); !parent.empty();) {
    ancestors.push_back(parent);
    const std::string next = parent_for(parent);
    if (next.empty() || next == parent) break;
    parent = next;
    if (ancestors.size() >= 64) return false;
  }
  std::reverse(ancestors.begin(), ancestors.end());

  const bool animated =
      target_has_sample(name) ||
      std::any_of(ancestors.begin(), ancestors.end(), target_has_sample);
  if (!animated && transform_parent_overrides_.empty())
    return scene_node_world_for(scene_, name, world);

  std::vector<std::string> chain = ancestors;
  chain.push_back(name);
  std::array<float, 16> anchor_base{};
  std::array<float, 16> anchor_current{};
  bool have_anchor = false;
  for (const std::string& target : chain) {
    const bool is_requested = target == name;
    const bool sampled = target_has_sample(target);
    if (!sampled && !is_requested) continue;

    std::array<float, 16> base_world{};
    if (!scene_node_world_for(scene_, target, base_world)) return false;
    std::array<float, 16> current_world = base_world;
    if (have_anchor) {
      current_world =
          mul16(mul16(base_world, affine_inverse16(anchor_base)),
                anchor_current);
    }
    if (sampled) {
      SceneNodeXfm sampled_node;
      if (!scene_node_xfm_for(scene_, target, sampled_node)) return false;
      std::array<float, 16> sampled_local = sampled_node.local;
      apply_samples(sampled_local, target);
      current_world =
          mul16(mul16(sampled_local, affine_inverse16(sampled_node.local)),
                current_world);
      anchor_base = base_world;
      anchor_current = current_world;
      have_anchor = true;
    }
    if (is_requested) world = current_world;
  }
  return true;
}

void MiloSceneRenderer::set_flare_steps(std::map<std::string, int> steps) {
  flare_steps_ = std::move(steps);
}

bool MiloSceneRenderer::resolve_transform_parent_world(
    const std::string& name, std::array<float, 16>& world) const {
  auto resolve = [&](auto&& self, const std::string& node,
                     std::array<float, 16>& out, int guard) -> bool {
    if (guard >= 64) return false;

    if (node == selected_camera_name_ && cam_.result_frame.valid) {
      const auto& frame = cam_.result_frame;
      auto normalize3 = [](std::array<float, 3> value) {
        const float length = std::sqrt(value[0] * value[0] +
                                       value[1] * value[1] +
                                       value[2] * value[2]);
        if (length > 0.000001f) {
          for (float& component : value) component /= length;
        }
        return value;
      };
      const auto forward = normalize3(
          {frame.forward[0], frame.forward[1], frame.forward[2]});
      const auto up =
          normalize3({frame.up[0], frame.up[1], frame.up[2]});
      const auto right = normalize3(
          {forward[1] * up[2] - forward[2] * up[1],
           forward[2] * up[0] - forward[0] * up[2],
           forward[0] * up[1] - forward[1] * up[0]});
      out = {right[0], right[1], right[2], 0.0f,
             forward[0], forward[1], forward[2], 0.0f,
             up[0], up[1], up[2], 0.0f,
             frame.position[0], frame.position[1], frame.position[2], 1.0f};
      return true;
    }

    std::array<float, 16> local{};
    std::string parent;
    uint32_t constraint = 0;
    bool found = false;
    for (const auto& mesh : scene_.meshes) {
      if (mesh.name != node) continue;
      local = xfm_to_mat4(mesh.local);
      parent = mesh.parent;
      constraint = mesh.constraint;
      found = true;
      break;
    }
    if (!found) {
      for (const auto& group : scene_.groups) {
        if (group.name != node) continue;
        local =
            group.has_transform ? xfm_to_mat4(group.local) : identity16();
        parent = group.parent;
        constraint = group.constraint;
        found = true;
        break;
      }
    }
    if (!found) {
      for (const auto& trans : scene_.transes) {
        if (trans.name != node) continue;
        local = xfm_to_mat4(trans.local);
        parent = trans.parent;
        constraint = trans.constraint;
        found = true;
        break;
      }
    }
    if (!found) {
      for (const auto& flare : scene_.flares) {
        if (flare.name != node) continue;
        local = xfm_to_mat4(flare.local);
        parent = flare.parent;
        constraint = flare.constraint;
        found = true;
        break;
      }
    }
    if (!found) {
      for (const auto& light : scene_.lights) {
        if (light.name != node) continue;
        local = xfm_to_mat4(light.local);
        parent = light.parent;
        constraint = light.constraint;
        found = true;
        break;
      }
    }
    if (!found) {
      for (const auto& camera : scene_.cams) {
        if (camera.name != node) continue;
        local = xfm_to_mat4(camera.local);
        parent = camera.parent;
        constraint = camera.constraint;
        found = true;
        break;
      }
    }
    if (!found) return false;

    if (const auto override_it = transform_parent_overrides_.find(node);
        override_it != transform_parent_overrides_.end()) {
      parent = override_it->second;
    }
    if (parent.empty() || parent == node) {
      out = local;
      return true;
    }
    std::array<float, 16> parent_world{};
    if (!self(self, parent, parent_world, guard + 1)) return false;
    out = apply_transform_constraint(local, parent_world, constraint);
    return true;
  };
  return resolve(resolve, name, world, 0);
}

bool MiloSceneRenderer::select_authored_camera(const std::string& name) {
  if (name.empty()) return false;
  const milo_scene::CamObj* authored = nullptr;
  for (const auto& camera : scene_.cams) {
    if (camera.decoded && camera.name == name) {
      authored = &camera;
      break;
    }
  }
  if (!authored) return false;

  selected_camera_name_ = name;
  const auto authored_world = scene_.world_matrix(*authored);
  cam_.authored = true;
  cam_.result_frame = {};
  for (int k = 0; k < 3; ++k) {
    cam_.authored_eye[k] = authored_world[12 + k];
    cam_.authored_at[k] =
        authored_world[12 + k] + authored_world[4 + k] * 100.0f;
    cam_.authored_up[k] = authored_world[8 + k];
  }
  cam_.fov = authored->fov;
  cam_.near_z = authored->near_plane;
  cam_.far_z = authored->far_plane;
  std::fprintf(stderr, "[scene3d] PanelDir camera %s selected\n", name.c_str());
  return true;
}

bool MiloSceneRenderer::select_scene_panel_camera() {
  return scene_.panel_dir_config_valid &&
         select_authored_camera(scene_.panel_camera);
}

void MiloSceneRenderer::frame_camera_on_bounds() {
  have_bounds_ = false;

  // Frame on where geometry DETAIL concentrates, weighted by vertex count: a
  // venue's stage + crowd carry the vast majority of vertices, while the sky /
  // ground shell is a few huge low-poly meshes. So we use the per-axis MEDIAN
  // of all world-space vertex positions as the look-at target, and a percentile
  // of vertex distance-from-target as the framing radius. This naturally lands
  // on the stage and ignores the sparse far shell — no per-mesh outlier guess.
  // The same pass tracks the FULL extent (every transformed vertex) for the far
  // plane (skybox can be far away).
  std::vector<float> vx, vy, vz;
  for (const auto& m : scene_.meshes) {
    if (!m.decoded || !m.showing || m.vertex_count == 0) continue;
    auto w = scene_.world_matrix(m);
    for (const auto& v : m.verts) {
      const float wx = v.px*w[0] + v.py*w[4] + v.pz*w[8]  + w[12];
      const float wy = v.px*w[1] + v.py*w[5] + v.pz*w[9]  + w[13];
      const float wz = v.px*w[2] + v.py*w[6] + v.pz*w[10] + w[14];
      vx.push_back(wx); vy.push_back(wy); vz.push_back(wz);
      const float p[3] = {wx, wy, wz};
      if (!have_bounds_) { for (int k=0;k<3;++k) bb_min_[k]=bb_max_[k]=p[k]; have_bounds_=true; }
      else { for (int k=0;k<3;++k){ bb_min_[k]=std::min(bb_min_[k],p[k]); bb_max_[k]=std::max(bb_max_[k],p[k]); } }
    }
  }
  if (!have_bounds_) return;

  auto pct = [](std::vector<float>& v, float q) -> float {
    if (v.empty()) return 0.0f;
    size_t i = static_cast<size_t>(v.size() * q);
    if (i >= v.size()) i = v.size() - 1;
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
  };
  std::array<float, 3> ctr{pct(vx, 0.5f), pct(vy, 0.5f), pct(vz, 0.5f)};
  // Distance of each vertex from the median center.
  std::vector<float> vd;
  vd.reserve(vx.size());
  for (size_t i = 0; i < vx.size(); ++i) {
    float dx = vx[i]-ctr[0], dy = vy[i]-ctr[1], dz = vz[i]-ctr[2];
    vd.push_back(std::sqrt(dx*dx + dy*dy + dz*dz));
  }
  const float radius = std::max(pct(vd, 0.80f), 5.0f);  // covers ~80% of detail

  for (int k = 0; k < 3; ++k) cam_.target[k] = ctr[k];
  cam_.distance = std::max(radius * 1.9f, 5.0f);
  cam_.near_z = std::max(cam_.distance * 0.01f, 0.5f);
  // Far plane spans the whole scene from the camera (skybox can be far away).
  const float fdx = bb_max_[0]-bb_min_[0], fdy = bb_max_[1]-bb_min_[1], fdz = bb_max_[2]-bb_min_[2];
  const float full_r = 0.5f * std::sqrt(fdx*fdx + fdy*fdy + fdz*fdz);
  cam_.far_z = cam_.distance * 4.0f + full_r * 4.0f;
  // An elevated 3/4 view reads best for a stage.
  cam_.pitch = 0.45f;
  cam_.yaw = 0.0f;
  std::fprintf(stderr,
               "[scene3d] full extent [%.1f %.1f %.1f]..[%.1f %.1f %.1f]  "
               "target=(%.1f %.1f %.1f) radius=%.1f dist=%.1f far=%.0f\n",
               bb_min_[0], bb_min_[1], bb_min_[2], bb_max_[0], bb_max_[1],
               bb_max_[2], cam_.target[0], cam_.target[1], cam_.target[2],
               radius, cam_.distance, cam_.far_z);
}

void MiloSceneRenderer::draw() {
  draw_impl(true, true, true);
}

void MiloSceneRenderer::draw_over_scene(const OrbitCamera& cam) {
  cam_ = cam;
  draw_impl(false, true, true);
}

void MiloSceneRenderer::draw_scene_only() {
  draw_impl(true, true, false);
}

void MiloSceneRenderer::draw_scene_only_over_scene() {
  draw_impl(false, true, false);
}

void MiloSceneRenderer::draw_scene_only_over_scene_preserving_state() {
  if (!dev_) return;
  IDirect3DStateBlock9* state = nullptr;
  if (FAILED(dev_->CreateStateBlock(D3DSBT_ALL, &state))) {
    draw_scene_only_over_scene();
    return;
  }
  if (state) state->Capture();
  draw_scene_only_over_scene();
  if (state) {
    state->Apply();
    state->Release();
  }
}

void MiloSceneRenderer::draw_scene_only_over_scene_preserving_state(
    const OrbitCamera& cam) {
  cam_ = cam;
  draw_scene_only_over_scene_preserving_state();
}

void MiloSceneRenderer::draw_text_over_scene() {
  draw_impl(false, false, true);
}

void MiloSceneRenderer::draw_impl(bool clear_target, bool draw_scene,
                                  bool draw_text) {
  if (!dev_) return;
  MiloEnvFrameCache env_cache;
  ScopedMiloEnvFrameCache scoped_env_cache(env_cache);

  const float physical_backbuffer_aspect =
      win_->bb_height() > 0 ? static_cast<float>(win_->bb_width()) /
                                  static_cast<float>(win_->bb_height())
                            : 16.0f / 9.0f;
  const float backbuffer_aspect = default_camera_aspect_ > 0.0f
                                      ? default_camera_aspect_
                                      : physical_backbuffer_aspect;
  const float aspect =
      env_camera_aspect_preset_or("GHOGX_CAMERA_ASPECT", backbuffer_aspect);

  DebugVenueInspectorState* debug_venue = nullptr;
  if (draw_scene && debug_venue_inspector_enabled() &&
      (!scene_.meshes.empty() ||
       env_enabled("GHOGX_DEBUG_VENUE_FREECAM") ||
       env_enabled("GHOGX_DEBUG_VENUE_PICK_AUTHORED"))) {
    debug_venue = &debug_venue_inspector_states()[this];
    if (!env_enabled("GHOGX_DEBUG_VENUE_PICK_AUTHORED")) {
      apply_debug_venue_freecam(win_, cam_, *debug_venue);
    }
  }

  float eye[3];
  cam_.eye(eye);
  float result_at[3] = {};
  const float* at = cam_.authored ? cam_.authored_at : cam_.target;
  const float* up = cam_.authored ? cam_.authored_up : nullptr;
  if (cam_.result_frame.valid) {
    for (int k = 0; k < 3; ++k) {
      result_at[k] = cam_.result_frame.position[k] +
                     cam_.result_frame.forward[k] * 100.0f;
    }
    at = result_at;
    up = cam_.result_frame.up;
  }
  Mat4 view = Mat4::look_at_lh(eye[0], eye[1], eye[2],
                               at[0], at[1], at[2],
                               up ? up[0] : 0.0f,
                               up ? up[1] : 0.0f,
                               up ? up[2] : 1.0f);
  Mat4 proj = Mat4::perspective_lh(cam_.fov, aspect, cam_.near_z, cam_.far_z);
  if (cam_.result_frame.valid && cam_.result_frame.has_custom_view) {
    std::memcpy(&view, cam_.result_frame.custom_view, 64);
  }
  if (cam_.result_frame.valid && cam_.result_frame.has_custom_projection) {
    std::memcpy(&proj, cam_.result_frame.custom_projection, 64);
  }
  // GH2 world is right-handed; mirror clip-X for the LH D3D pipeline so the
  // scene isn't left/right flipped (same convention as the highway renderer).
  if (!(cam_.result_frame.valid && cam_.result_frame.has_custom_projection)) {
    proj.m[0][0] = -proj.m[0][0];
  }
  if ((cam_.authored || cam_.result_frame.valid) &&
      !(cam_.result_frame.valid && cam_.result_frame.has_custom_projection) &&
      !(cam_.result_frame.valid && cam_.result_frame.screen_offset_consumed) &&
      !env_enabled("GHOGX_DISABLE_CAMERA_SCREEN_OFFSET")) {
    // CamShot stores screen_offset in the same render-camera family that
    // traces showed carrying stable 768.0 screen/projection values, not in
    // already-normalized D3D clip units.
    constexpr float kScreenOffsetToClip = 1.0f / 768.0f;
    proj.m[2][0] += cam_.screen_offset[0] * kScreenOffsetToClip;
    proj.m[2][1] += cam_.screen_offset[1] * kScreenOffsetToClip;
  }
  if (draw_scene) {
    std::memcpy(last_crowd_camera_view_.data(), &view, 64);
    std::memcpy(last_crowd_camera_projection_.data(), &proj, 64);
    last_crowd_camera_valid_ = true;
    if (pending_gh1_crowd_auto_) select_gh1_crowd_region(-1);
  }
  if (clear_target && env_enabled("GHOGX_LOG_CAMERA_MATRIX")) {
    log_camera_matrix_rows(cam_, eye, at, up ? up : cam_.authored_up, aspect,
                           view, proj);
  }

  D3DVIEWPORT9 full_viewport = {};
  full_viewport.X = 0;
  full_viewport.Y = 0;
  full_viewport.Width = static_cast<DWORD>(std::max(1, win_->bb_width()));
  full_viewport.Height = static_cast<DWORD>(std::max(1, win_->bb_height()));
  full_viewport.MinZ = 0.0f;
  full_viewport.MaxZ = 1.0f;
  dev_->SetViewport(&full_viewport);

  if (clear_target) {
    const D3DCOLOR clear_color =
        env_enabled("GHOGX_CHARACTER_SOFT_GREEN_BG")
            ? D3DCOLOR_XRGB(116, 151, 124)
            : D3DCOLOR_XRGB(clear_r_, clear_g_, clear_b_);
    dev_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                clear_color, 1.0f, 0);
  } else if (clear_depth_on_overlay_) {
    dev_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
  }

  if (custom_viewport_) {
    D3DVIEWPORT9 active = full_viewport;
    const int max_w = std::max(1, win_->bb_width());
    const int max_h = std::max(1, win_->bb_height());
    const int x = std::clamp(viewport_x_, 0, max_w - 1);
    const int y = std::clamp(viewport_y_, 0, max_h - 1);
    active.X = static_cast<DWORD>(x);
    active.Y = static_cast<DWORD>(y);
    active.Width = static_cast<DWORD>(
        std::clamp(viewport_w_, 1, std::max(1, max_w - x)));
    active.Height = static_cast<DWORD>(
        std::clamp(viewport_h_, 1, std::max(1, max_h - y)));
    dev_->SetViewport(&active);
  }

  dev_->BeginScene();

  D3DMATRIX dv, dp;
  std::memcpy(&dv, &view, 64);
  std::memcpy(&dp, &proj, 64);
  dev_->SetTransform(D3DTS_VIEW, &dv);
  dev_->SetTransform(D3DTS_PROJECTION, &dp);
  const auto view_arr = [&]() {
    std::array<float, 16> out{};
    std::memcpy(out.data(), &view, 64);
    return out;
  }();
  const auto proj_arr = [&]() {
    std::array<float, 16> out{};
    std::memcpy(out.data(), &proj, 64);
    return out;
  }();
  const auto view_proj_arr = mul16(view_arr, proj_arr);

  dev_->SetFVF(kFVF);
  dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
  dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  // Mirroring clip-X flips winding for the D3D bridge. Keep the broadly
  // validated CW default; debug overrides let individual venue winding issues
  // be inspected without changing asset data.
  DWORD authored_cull_mode = D3DCULL_CW;
  if (env_enabled("GHOGX_CULL_CCW")) authored_cull_mode = D3DCULL_CCW;
  if (env_enabled("GHOGX_CULL_NONE")) authored_cull_mode = D3DCULL_NONE;
  dev_->SetRenderState(D3DRS_CULLMODE, authored_cull_mode);
  dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

  // Fixed-function lighting using the decoded normals. Bright ambient keeps all
  // textured surfaces readable (venues bake their own light into the textures;
  // we just need enough fill that nothing is pure black), plus two opposed
  // directional lights so geometry still has shape regardless of facing.
  install_scene_fill_lighting(dev_);

  dev_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  dev_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
  dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
  // Modulate texture by the lit vertex colour.
  dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

  D3DStateCache d3d_state(dev_);
  D3DMATRIX wm;
  std::vector<SVtx> vb;
  const std::string debug_highlight_mesh =
      debug_venue ? debug_venue->highlight_mesh : std::string{};
  const bool apply_environment_lighting =
      environment_lighting_enabled_ &&
      !env_enabled("GHOGX_DISABLE_ENVIRON_LIGHTING");
  const bool apply_environment_dynamic_lights =
      apply_environment_lighting &&
      !env_enabled("GHOGX_DISABLE_ENVIRON_DYNAMIC_LIGHTS");
  const bool apply_environment_fog =
      apply_environment_lighting && !env_enabled("GHOGX_DISABLE_ENVIRON_FOG");
  std::string active_authored_light_key;
  const milo_scene::EnvironObj* active_authored_light_environment = nullptr;
  bool authored_lights_configured = false;
  auto disable_authored_lights = [&]() {
    if (authored_lights_configured &&
        active_authored_light_environment == nullptr) {
      return;
    }
    for (DWORD i = 0; i < kAuthoredLightSlotCount; ++i) {
      dev_->LightEnable(kAuthoredLightFirstSlot + i, FALSE);
    }
    active_authored_light_key.clear();
    active_authored_light_environment = nullptr;
    authored_lights_configured = true;
  };
  auto transform_target_has_sample = [&](const std::string& target) -> bool {
    return mesh_transform_offsets_.find(target) !=
               mesh_transform_offsets_.end() ||
           active_mesh_anims_.find(target) != active_mesh_anims_.end();
  };
  auto apply_transform_samples_to_target =
      [&](std::array<float, 16>& local, const std::string& target) {
        if (const auto offset_it = mesh_transform_offsets_.find(target);
            offset_it != mesh_transform_offsets_.end()) {
          apply_mesh_transform_sample(local, offset_it->second);
        }
        const auto anim_it = active_mesh_anims_.find(target);
        if (anim_it == active_mesh_anims_.end()) return;
        const auto& active = anim_it->second;
        const float anim_frame = active.elapsed * active.frames_per_second;
        apply_mesh_transform_sample(
            local, sample_transform_anim(active.anim, anim_frame));
      };
  auto sampled_light_world = [&](const milo_scene::LightObj& light,
                                 const std::string& ref) {
    auto world = xfm_to_mat4(light.world_stored);
    if (transform_parent_overrides_.find(ref) !=
        transform_parent_overrides_.end()) {
      (void)resolve_transform_parent_world(ref, world);
    }
    if (transform_target_has_sample(ref)) {
      apply_transform_samples_to_target(world, ref);
    }
    return world;
  };
  auto count_environ_light_paths =
      [&](const milo_scene::EnvironObj* env) -> EnvironLightCounts {
    EnvironLightCounts counts;
    if (!env) return counts;
    for (const auto& ref : env->lights) {
      const auto* light = scene_.find_light(ref);
      if (!light || !light_color_sane(*light)) continue;
      int light_type = light->type;
      if (const auto state_it = light_state_overrides_.find(ref);
          state_it != light_state_overrides_.end() &&
          state_it->second.has_type) {
        light_type = state_it->second.type;
      }
      if (is_authored_real_environment_light_type(light_type)) {
        ++counts.real;
      } else if (is_authored_approx_environment_light_type(light_type)) {
        ++counts.approx;
      }
    }
    return counts;
  };
  auto configure_authored_lights =
      [&](const milo_scene::EnvironObj* env) {
    if (!apply_environment_dynamic_lights || !env || env->lights.empty()) {
      disable_authored_lights();
      return;
    }
    if (authored_lights_configured &&
        active_authored_light_environment == env) {
      return;
    }
    for (DWORD i = 0; i < kAuthoredLightSlotCount; ++i) {
      dev_->LightEnable(kAuthoredLightFirstSlot + i, FALSE);
    }

    DWORD slot = kAuthoredLightFirstSlot;
    size_t enabled = 0;
    for (const auto& ref : env->lights) {
      if (slot >= kAuthoredLightFirstSlot + kAuthoredLightSlotCount) break;
      const auto* light = scene_.find_light(ref);
      if (!light || !light_color_sane(*light)) continue;
      std::array<float, 4> light_color = {light->color[0], light->color[1],
                                          light->color[2], light->color[3]};
      if (const auto color_it = light_color_overrides_.find(ref);
          color_it != light_color_overrides_.end()) {
        light_color = color_it->second;
      }
      float light_range = light->range;
      int light_type = light->type;
      if (const auto state_it = light_state_overrides_.find(ref);
          state_it != light_state_overrides_.end()) {
        if (state_it->second.has_color) light_color = state_it->second.color;
        if (state_it->second.has_range) light_range = state_it->second.range;
        if (state_it->second.has_type) light_type = state_it->second.type;
      }
      const auto light_world = sampled_light_world(*light, ref);
      D3DLIGHT9 dl{};
      dl.Diffuse.r =
          std::clamp(light_color[0], 0.0f, kMaxAuthoredLightColor);
      dl.Diffuse.g =
          std::clamp(light_color[1], 0.0f, kMaxAuthoredLightColor);
      dl.Diffuse.b =
          std::clamp(light_color[2], 0.0f, kMaxAuthoredLightColor);
      dl.Diffuse.a = std::clamp(light_color[3], 0.0f, 1.0f);
      if (!is_authored_real_environment_light_type(light_type)) continue;
      if (light_type == 2) {
        const auto direction =
            authored_fake_spot_direction_from_world(light_world);
        float dx = direction[0];
        float dy = direction[1];
        float dz = direction[2];
        const float len = vec_len3(dx, dy, dz);
        if (len <= 0.0001f) continue;
        dl.Type = D3DLIGHT_SPOT;
        dl.Direction = {dx / len, dy / len, dz / len};
        dl.Position = {light_world[12], light_world[13], light_world[14]};
        dl.Range = std::max(light_range, 1.0f);
        dl.Attenuation0 = 1.0f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        dl.Theta = 20.0f * kDegToRad;
        dl.Phi = 45.0f * kDegToRad;
      } else if (light_type == 0) {
        dl.Type = D3DLIGHT_POINT;
        dl.Position = {light_world[12], light_world[13], light_world[14]};
        dl.Range = std::max(light_range, 1.0f);
        dl.Attenuation0 = 1.0f;
      } else {
        continue;
      }
      dev_->SetLight(slot, &dl);
      dev_->LightEnable(slot, TRUE);
      ++slot;
      ++enabled;
    }
    active_authored_light_key = enabled == 0 ? std::string{} : env->name;
    active_authored_light_environment = env;
    authored_lights_configured = true;
  };
  std::string active_authored_fog_key;
  const milo_scene::EnvironObj* active_fog_environment = nullptr;
  bool authored_fog_configured = false;
  auto disable_authored_fog = [&]() {
    if (authored_fog_configured && active_fog_environment == nullptr) return;
    dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    active_authored_fog_key.clear();
    active_fog_environment = nullptr;
    authored_fog_configured = true;
  };
  auto configure_authored_fog = [&](const milo_scene::EnvironObj* env) {
    if (!apply_environment_fog || !env) {
      disable_authored_fog();
      return;
    }
    if (authored_fog_configured && active_fog_environment == env) return;
    std::array<float, 4> fog_color = {
        env->fog_color[0], env->fog_color[1], env->fog_color[2],
        env->fog_color[3]};
    bool fog_enabled = env->fog_enabled;
    float fog_start = env->fog_start;
    float fog_end = env->fog_end;
    if (const auto fog_it = environment_fog_overrides_.find(env->name);
        fog_it != environment_fog_overrides_.end()) {
      if (fog_it->second.has_enabled) fog_enabled = fog_it->second.enabled;
      if (fog_it->second.has_color) fog_color = fog_it->second.color;
      if (fog_it->second.has_range) {
        fog_start = fog_it->second.range[0];
        fog_end = fog_it->second.range[1];
      }
    }
    if (!fog_values_sane(fog_enabled, fog_start, fog_end, fog_color)) {
      disable_authored_fog();
      return;
    }
    dev_->SetRenderState(D3DRS_FOGENABLE, TRUE);
    dev_->SetRenderState(D3DRS_FOGCOLOR, d3d_color_from_rgba(fog_color));
    dev_->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    dev_->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    dev_->SetRenderState(D3DRS_FOGSTART, float_to_dword(fog_start));
    dev_->SetRenderState(D3DRS_FOGEND, float_to_dword(fog_end));
    if (env_enabled("GHOGX_LOG_ENVIRON_FOG")) {
      static std::unordered_set<std::string> logged_fog_envs;
      if (logged_fog_envs.insert(env->name).second) {
        std::fprintf(stderr,
                     "[milo_scene] Environ fog active: %s start=%.3f end=%.3f color=(%.3f %.3f %.3f %.3f)\n",
                     env->name.c_str(), fog_start, fog_end, fog_color[0],
                     fog_color[1], fog_color[2], fog_color[3]);
      }
    }
    active_authored_fog_key = env->name;
    active_fog_environment = env;
    authored_fog_configured = true;
  };
  const milo_scene::EnvironObj* active_approx_light_environment = nullptr;
  bool approx_lights_configured = false;
  auto draw_mesh_with_world = [&](const milo_scene::MeshObj& m,
                                   const std::array<float, 16>& w,
                                   const std::string* material_override = nullptr,
                                   const SpotlightState* spotlight_state = nullptr,
                                   bool force_debug_draw = false) {
    const bool legacy_environment_drawable =
        legacy_environment_drawables_.find(m.name) !=
        legacy_environment_drawables_.end();
    if (!m.decoded ||
        (!m.showing && !legacy_environment_drawable && !force_debug_draw) ||
        m.vertex_count == 0 || m.face_count == 0) {
      return;
    }
    if (!force_debug_draw &&
        mesh_matches_env_spec("GHOGX_SKIP_VENUE_MESH", m.name)) {
      return;
    }
    std::memcpy(&wm, w.data(), 64);
    dev_->SetTransform(D3DTS_WORLD, &wm);

    IDirect3DTexture9* texture = nullptr;
    float su = 1.0f, sv = 1.0f, tu = 0.0f, tv = 0.0f;
    float uv_m00 = 1.0f, uv_m01 = 0.0f;
    float uv_m10 = 0.0f, uv_m11 = 1.0f;
    float uv_m20 = 0.0f, uv_m21 = 0.0f;
    float mr = 1.0f, mg = 1.0f, mb = 1.0f, ma = 1.0f;
    uint8_t material_blend = kBlendSrcAlpha;
    uint8_t material_z_mode = kZModeNormal;
    const std::string& material =
        (material_override && !material_override->empty()) ? *material_override
                                                           : m.material;
    // RndMesh submission requires an assigned RndMat. Null-material meshes
    // remain valid transform/editor helpers, but the platform renderer has no
    // draw state to submit for them.
    if (material.empty() && !force_debug_draw) return;
    const bool debug_spotlight_solid =
        spotlight_state && env_enabled("GHOGX_DEBUG_SPOTLIGHT_SOLID");
    const bool debug_highlighted_mesh =
        !debug_highlight_mesh.empty() && m.name == debug_highlight_mesh;
    const bool debug_highlighted_source_only =
        debug_highlighted_mesh && debug_venue &&
        debug_venue->highlight_source_only;
    const milo_scene::MatObj* mat_obj = find_material(material);
    std::string diffuse_tex;
    if (mat_obj) {
      const auto* mat = mat_obj;
      diffuse_tex = mat->diffuse_tex;
      su = mat->tex_scale[0]; sv = mat->tex_scale[1];
      tu = mat->tex_offset[0]; tv = mat->tex_offset[1];
      uv_m00 = mat->tex_xfm[0][0];
      uv_m01 = mat->tex_xfm[0][1];
      uv_m10 = mat->tex_xfm[1][0];
      uv_m11 = mat->tex_xfm[1][1];
      uv_m20 = mat->tex_xfm[2][0];
      uv_m21 = mat->tex_xfm[2][1];
      // GH2's Career poster is the one PS2 menu sheet whose authored Mat
      // carries a V=-1 transform while the bitmap payload is already decoded
      // into top-down rows for D3D. Applying both inversions mirrors the whole
      // page vertically (header art at the bottom, store art at the top).
      // Retail's live page keeps the decoded sheet upright, so cancel only
      // this redundant image-orientation transform. The pause-card tile Mats
      // retain their negative U/V scales because those are real corner flips.
      if (m.name == "poster_bottom.mesh" &&
          material == "poster_bottom.mat" &&
          std::fabs(uv_m01) < 1.0e-5f &&
          std::fabs(uv_m10) < 1.0e-5f &&
          uv_m11 < -0.999f) {
        uv_m11 = -uv_m11;
        uv_m21 = 0.0f;
      }
      mr = mat->color[0]; mg = mat->color[1]; mb = mat->color[2]; ma = mat->color[3];
      material_blend = mat->blend;
      material_z_mode = mat->z_mode;
    }
    if (env_enabled("GHOGX_LOG_VENUE_MATERIAL") &&
        (mesh_matches_env_spec("GHOGX_LOG_VENUE_MATERIAL_MATCH", m.name) ||
         m.name.find("stadium_spotlight") != std::string::npos ||
         material.find("stadium_rays") != std::string::npos ||
         material.find("searchlight_beam") != std::string::npos)) {
      static std::unordered_set<std::string> logged_materials;
      const std::string key = m.name + "|" + material;
      if (logged_materials.insert(key).second) {
        std::array<float, 4> vertex_min = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> vertex_max = {0.0f, 0.0f, 0.0f, 0.0f};
        for (const auto& vertex : m.verts) {
          const std::array<float, 4> color = {
              vertex.r, vertex.g, vertex.b, vertex.a};
          for (size_t channel = 0; channel < color.size(); ++channel) {
            vertex_min[channel] =
                std::min(vertex_min[channel], color[channel]);
            vertex_max[channel] =
                std::max(vertex_max[channel], color[channel]);
          }
        }
        std::fprintf(stderr,
                     "[milo_scene] venue material mesh=%s material=%s "
                     "tex=%s blend=%u color=(%.3f %.3f %.3f %.3f) "
                     "legacy_stages=%zu multipass=%d normalize=%d "
                     "vertex_ambient=%d vertex_dynamic=%d "
                     "prelit=%d use_environ=%d zmode=%u "
                     "vertex0=(%.3f %.3f %.3f %.3f) "
                     "vertex_range=[%.3f..%.3f %.3f..%.3f "
                     "%.3f..%.3f %.3f..%.3f]\n",
                     m.name.c_str(), material.c_str(), diffuse_tex.c_str(),
                     static_cast<unsigned>(material_blend), mr, mg, mb, ma,
                     mat_obj ? mat_obj->legacy_texture_stages.size() : 0u,
                     mat_obj ? mat_obj->legacy_multipass : 0,
                     mat_obj && mat_obj->legacy_normalize ? 1 : 0,
                     mat_obj && mat_obj->legacy_vertex_ambient ? 1 : 0,
                     mat_obj && mat_obj->legacy_vertex_dynamic ? 1 : 0,
                     mat_obj && mat_obj->prelit ? 1 : 0,
                     mat_obj && mat_obj->use_environ ? 1 : 0,
                     static_cast<unsigned>(material_z_mode),
                     m.verts.empty() ? 1.0f : m.verts.front().r,
                     m.verts.empty() ? 1.0f : m.verts.front().g,
                     m.verts.empty() ? 1.0f : m.verts.front().b,
                     m.verts.empty() ? 1.0f : m.verts.front().a,
                     vertex_min[0], vertex_max[0], vertex_min[1],
                     vertex_max[1], vertex_min[2], vertex_max[2],
                     vertex_min[3], vertex_max[3]);
      }
    }
    if (const auto tex_name_it = material_textures_.find(material);
        tex_name_it != material_textures_.end() && !tex_name_it->second.empty()) {
      diffuse_tex = tex_name_it->second;
    }
    if (!diffuse_tex.empty()) {
      auto it = tex_.find(diffuse_tex);
      if (it != tex_.end()) texture = it->second;
    }
    if (const auto color_it = material_colors_.find(material);
        color_it != material_colors_.end()) {
      const auto& c = color_it->second;
      mr = c[0];
      mg = c[1];
      mb = c[2];
      ma = c[3];
    }
    const milo_scene::EnvironObj* mesh_env = nullptr;
    std::string mesh_env_name;
    if (apply_environment_lighting && mat_obj && mat_obj->use_environ) {
      std::string env_name = default_environment_;
      const auto env_it = mesh_environments_.find(m.name);
      if (env_it != mesh_environments_.end()) env_name = env_it->second;
      mesh_env_name = env_name;
      mesh_env = env_name.empty() ? nullptr : scene_.find_environ(env_name);
    }
    if (mat_obj && mat_obj->use_environ &&
        env_enabled("GHOGX_LOG_MESH_ENV_BINDING")) {
      static std::unordered_set<std::string> logged_mesh_environment_bindings;
      const std::string key =
          m.name + "|" + material + "|" + mesh_env_name + "|" +
          (mesh_env ? "found" : "missing");
      if (logged_mesh_environment_bindings.insert(key).second) {
        std::fprintf(
            stderr,
            "[milo_scene] Mesh environment binding: mesh=%s material=%s "
            "default=%s selected=%s decoded=%d lighting_enabled=%d\n",
            m.name.c_str(), material.c_str(),
            default_environment_.empty() ? "(none)"
                                         : default_environment_.c_str(),
            mesh_env_name.empty() ? "(none)" : mesh_env_name.c_str(),
            mesh_env ? 1 : 0, apply_environment_lighting ? 1 : 0);
      }
    }
    std::array<float, 4> mesh_env_color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool has_mesh_env_color = false;
    DWORD mesh_ambient = kDefaultSceneAmbient;
    std::vector<ApproxLightCandidate> approx_directional_lights;
    if (mesh_env && environ_color_sane(*mesh_env)) {
        mesh_env_color = {mesh_env->color_a[0], mesh_env->color_a[1],
                          mesh_env->color_a[2], mesh_env->color_a[3]};
        if (const auto color_it =
                environment_color_overrides_.find(mesh_env->name);
            color_it != environment_color_overrides_.end()) {
          mesh_env_color = color_it->second;
        }
        has_mesh_env_color = true;
        const auto cc_env = [](float f) -> int {
          int i = static_cast<int>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
          return i < 0 ? 0 : (i > 255 ? 255 : i);
        };
        mesh_ambient = D3DCOLOR_XRGB(cc_env(mesh_env_color[0]),
                                     cc_env(mesh_env_color[1]),
                                     cc_env(mesh_env_color[2]));
    }
    if (mesh_env && !env_enabled("GHOGX_DISABLE_ENVIRON_APPROX_LIGHTS")) {
      size_t approx_lights = 0;
      for (const auto& ref : mesh_env->lights) {
        const auto* light = scene_.find_light(ref);
        if (!light || !light_color_sane(*light)) continue;
        std::array<float, 4> light_color = {light->color[0], light->color[1],
                                            light->color[2], light->color[3]};
        if (const auto color_it = light_color_overrides_.find(ref);
            color_it != light_color_overrides_.end()) {
          light_color = color_it->second;
        }
        int light_type = light->type;
        if (const auto state_it = light_state_overrides_.find(ref);
            state_it != light_state_overrides_.end()) {
          if (state_it->second.has_color) light_color = state_it->second.color;
          if (state_it->second.has_type) light_type = state_it->second.type;
        }
        if (!is_authored_approx_environment_light_type(light_type)) continue;
        const auto light_world = sampled_light_world(*light, ref);
        ApproxLightCandidate candidate;
        candidate.ref = ref;
        candidate.direction =
            authored_directional_emission_from_world(light_world);
        candidate.color = light_color;
        candidate.score = std::max({std::fabs(light_color[0]),
                                    std::fabs(light_color[1]),
                                    std::fabs(light_color[2])});
        approx_directional_lights.push_back(candidate);
        ++approx_lights;
      }
    }
    d3d_state.render(D3DRS_AMBIENT, mesh_ambient);
    if (!approx_lights_configured ||
        active_approx_light_environment != mesh_env) {
      install_approx_scene_lights(dev_, approx_directional_lights,
                                  mesh_env == nullptr);
      active_approx_light_environment = mesh_env;
      approx_lights_configured = true;
    }
    configure_authored_fog(mesh_env);
    configure_authored_lights(mesh_env);
    if (mesh_env && env_enabled("GHOGX_LOG_ENVIRON_LIGHTING")) {
      static std::unordered_set<std::string> logged_mesh_envs;
      const auto counts = count_environ_light_paths(mesh_env);
      const std::string key = m.name + "|" +
                              environ_lighting_debug_signature(
                                  mesh_env->name, counts.real, counts.approx,
                                  mesh_env_color,
                                  approx_directional_lights);
      if (logged_mesh_envs.insert(key).second) {
        std::fprintf(stderr,
                     "[milo_scene] Mesh Environ lighting state: mesh=%s "
                     "env=%s real_lights=%zu approx_lights=%zu "
                     "approx_directional=%zu refs=%zu "
                     "ambient=(%.3f %.3f %.3f %.3f)\n",
                     m.name.c_str(), mesh_env->name.c_str(), counts.real,
                     counts.approx, approx_directional_lights.size(),
                     mesh_env->lights.size(), mesh_env_color[0],
                     mesh_env_color[1], mesh_env_color[2],
                     mesh_env_color[3]);
      }
    }
    bool material_tex_anim = false;
    float rot = 0.0f;
    if (const auto tex_it = material_tex_transforms_.find(material);
        tex_it != material_tex_transforms_.end()) {
      const auto& transform = tex_it->second;
      // RndMatAnim::SetFrame starts from Mat.TexXfm, replaces keyed
      // rotation, then scales the current rows.
      if (transform.has_rotation) {
        rot = transform.rotation_radians;
        const float c = std::cos(rot);
        const float sn = std::sin(rot);
        uv_m00 = c;
        uv_m01 = sn;
        uv_m10 = -sn;
        uv_m11 = c;
        material_tex_anim = true;
      }
      if (transform.has_scale) {
        const float sx = transform.scale[0];
        const float sy = transform.scale[1];
        uv_m00 *= sx;
        uv_m01 *= sx;
        uv_m10 *= sy;
        uv_m11 *= sy;
        material_tex_anim = true;
      }
      if (transform.has_translation) {
        tu = uv_m20 = transform.translation[0];
        tv = uv_m21 = transform.translation[1];
        material_tex_anim = true;
      }
      if (material_tex_anim) {
        su = std::hypot(uv_m00, uv_m01);
        sv = std::hypot(uv_m10, uv_m11);
        tu = uv_m20;
        tv = uv_m21;
      }
    }
    if (mat_obj) {
      const SourceTexGenXfm2D tex_gen_xfm = source_texgen_xfm_2d(
          mat_obj->tex_gen, uv_m00, uv_m01, uv_m10, uv_m11, uv_m20, uv_m21);
      uv_m00 = tex_gen_xfm.m00;
      uv_m01 = tex_gen_xfm.m01;
      uv_m10 = tex_gen_xfm.m10;
      uv_m11 = tex_gen_xfm.m11;
      uv_m20 = tex_gen_xfm.tu;
      uv_m21 = tex_gen_xfm.tv;
      su = std::hypot(uv_m00, uv_m01);
      sv = std::hypot(uv_m10, uv_m11);
      tu = uv_m20;
      tv = uv_m21;
    }
    if (spotlight_state) {
      mr *= spotlight_state->r;
      mg *= spotlight_state->g;
      mb *= spotlight_state->b;
      ma *= spotlight_state->intensity;
    }
    mr *= global_brightness_;
    mg *= global_brightness_;
    mb *= global_brightness_;
    ma *= global_alpha_;
    if (const auto alpha_it = material_alpha_.find(material);
        alpha_it != material_alpha_.end()) {
      ma *= alpha_it->second;
    }
    if (const auto alpha_it = material_alpha_overrides_.find(material);
        alpha_it != material_alpha_overrides_.end()) {
      ma = alpha_it->second;
    }
    const bool prelit_material =
        mat_obj && mat_obj->prelit && !env_enabled("GHOGX_DISABLE_PRELIT_MATERIALS");
    // Native keeps an environment light channel active for prelit materials
    // which opt into RndEnviron. Their baked vertex colors are the light
    // channel's material input, not a reason to suppress authored ambient and
    // approximate/dynamic lights. Only prelit + !use_environ bypasses.
    const bool prelit_lighting_bypass =
        source_material_bypasses_fixed_lighting(
            prelit_material, mat_obj && mat_obj->use_environ);
    (void)has_mesh_env_color;
    if (debug_spotlight_solid) {
      texture = nullptr;
      mr = 1.0f;
      mg = 0.0f;
      mb = 1.0f;
      ma = 1.0f;
      material_blend = kBlendSrc;
    }
    if (debug_highlighted_mesh) {
      texture = nullptr;
      if (debug_highlighted_source_only) {
        mr = 0.05f;
        mg = 0.82f;
        mb = 1.0f;
        ma = 0.28f;
      } else {
        mr = 1.0f;
        mg = 0.08f;
        mb = 0.95f;
        ma = std::max(ma, 0.82f);
      }
      material_blend = kBlendSrcAlpha;
      material_z_mode = kZModeForce;
    }
    if (mesh_matches_env_spec("GHOGX_DEBUG_ALPHA_VENUE_MESH", m.name)) {
      ma *= env_float_or("GHOGX_DEBUG_ALPHA_VENUE_VALUE", 0.2f, 0.0f, 1.0f);
      material_blend = kBlendSrcAlpha;
      material_z_mode = kZModeTransparent;
    }
    const BlendState blend_state = blend_state_for(material_blend);
    const auto source_winding_it = source_mesh_winding_reversed_.find(&m);
    const bool source_winding_reversed =
        source_winding_it != source_mesh_winding_reversed_.end() &&
        source_winding_it->second;
    const DWORD mesh_cull_mode =
        venue_mesh_cull_mode(mat_obj, w, source_winding_reversed,
                             authored_cull_mode, debug_highlighted_mesh);
    d3d_state.render(D3DRS_CULLMODE, mesh_cull_mode);
    const bool alpha_test =
        mat_obj && mat_obj->has_render_state && mat_obj->alpha_cut &&
        !debug_highlighted_mesh;
    const DWORD alpha_ref = static_cast<DWORD>(
        std::clamp(mat_obj ? mat_obj->alpha_threshold : 0, 0, 255));
    d3d_state.render(D3DRS_ALPHATESTENABLE, alpha_test ? TRUE : FALSE);
    d3d_state.render(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    d3d_state.render(D3DRS_ALPHAREF, alpha_ref);
    if (material_blend == kBlendAdd && ma < 0.999f) {
      // ONE/ONE additive blending ignores vertex alpha, so treat Mat alpha as
      // authored emissive intensity for fading glows/beams.
      mr *= ma;
      mg *= ma;
      mb *= ma;
    }
    if (ma <= 0.001f) return;
    const bool z_mode_writes =
        material_z_mode == kZModeNormal || material_z_mode == kZModeForce ||
        material_z_mode == kZModeDecal;
    const bool z_mode_tests = material_z_mode != kZModeDisable;
    const bool disable_zwrite =
        !z_mode_tests || !z_mode_writes || blend_state.additive ||
        material_blend == kBlendSubtract || material_blend == kBlendMultiply ||
        ma < 0.999f;
    d3d_state.render(D3DRS_ZENABLE, z_mode_tests ? TRUE : FALSE);
    d3d_state.render(D3DRS_ZWRITEENABLE, disable_zwrite ? FALSE : TRUE);
    d3d_state.render(D3DRS_BLENDOP, blend_state.op);
    d3d_state.render(D3DRS_SRCBLEND, blend_state.src);
    d3d_state.render(D3DRS_DESTBLEND, blend_state.dest);
    if (env_enabled("GHOGX_LOG_ALPHA_MESHES") &&
        (blend_state.additive || ma < 0.999f ||
         material.find("glow") != std::string::npos ||
         material.find("beam") != std::string::npos)) {
      static std::unordered_set<std::string> logged;
      const std::string key = m.name + "|" + material;
      if (logged.insert(key).second) {
        std::fprintf(stderr,
                     "[milo_scene] alpha mesh name=%s material=%s blend=%u "
                     "mat_alpha=%.3f additive=%d verts=%zu faces=%zu\n",
                     m.name.c_str(), material.c_str(),
                     static_cast<unsigned>(material_blend), ma,
                     blend_state.additive ? 1 : 0, m.verts.size(),
                     m.indices.size() / 3);
      }
    }
    const std::vector<std::array<float, 2>>* texcoord_override = nullptr;
    if (const auto uv_it = mesh_texcoord_overrides_.find(m.name);
        uv_it != mesh_texcoord_overrides_.end() && !uv_it->second.empty()) {
      texcoord_override = &uv_it->second;
    }
    const std::vector<std::array<float, 4>>* color_override = nullptr;
    if (const auto color_it = mesh_color_overrides_.find(m.name);
        color_it != mesh_color_overrides_.end() && !color_it->second.empty()) {
      color_override = &color_it->second;
    }
    float mesh_anim_blend = 1.0f;
    if (const auto blend_it = mesh_anim_blends_.find(m.name);
        blend_it != mesh_anim_blends_.end() && std::isfinite(blend_it->second)) {
      mesh_anim_blend = std::clamp(blend_it->second, 0.0f, 1.0f);
    }
    const std::vector<std::array<float, 3>>* position_override = nullptr;
    if (const auto pos_it = mesh_position_overrides_.find(m.name);
        pos_it != mesh_position_overrides_.end() && !pos_it->second.empty()) {
      position_override = &pos_it->second;
    }
    const std::vector<std::array<float, 3>>* normal_override = nullptr;
    if (const auto normal_it = mesh_normal_overrides_.find(m.name);
        normal_it != mesh_normal_overrides_.end() &&
        !normal_it->second.empty()) {
      normal_override = &normal_it->second;
    }
    const bool static_buffer_eligible =
        !material_override && !spotlight_state && !force_debug_draw &&
        !debug_highlighted_mesh && m.bones.empty() && !material_tex_anim &&
        !position_override && !normal_override && !texcoord_override &&
        !color_override &&
        mesh_anim_blends_.find(m.name) == mesh_anim_blends_.end() &&
        material_colors_.find(material) == material_colors_.end() &&
        material_alpha_.find(material) == material_alpha_.end() &&
        material_alpha_overrides_.find(material) ==
            material_alpha_overrides_.end() &&
        material_textures_.find(material) == material_textures_.end() &&
        !(prelit_lighting_bypass && has_mesh_env_color) &&
        global_brightness_ == 1.0f && global_alpha_ == 1.0f;
    if (static_buffer_eligible) {
      const auto cached = static_mesh_buffers_.find(&m);
      if (cached != static_mesh_buffers_.end()) {
        if (texture) {
          d3d_state.texture(0, texture);
          d3d_state.sampler(
              0, D3DSAMP_ADDRESSU,
              cached->second.wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
          d3d_state.sampler(
              0, D3DSAMP_ADDRESSV,
              cached->second.wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
          d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
          d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        } else {
          d3d_state.texture(0, nullptr);
          d3d_state.sampler(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
          d3d_state.sampler(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
          d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
          d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        }
        const bool disable_mesh_lighting =
            debug_spotlight_solid || prelit_lighting_bypass;
        DWORD prev_lighting = TRUE;
        if (disable_mesh_lighting) {
          dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
          d3d_state.render(D3DRS_LIGHTING, FALSE);
        }
        dev_->SetStreamSource(0, cached->second.vertices, 0, sizeof(SVtx));
        dev_->SetIndices(cached->second.indices);
        dev_->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST, 0, 0,
            static_cast<UINT>(m.vertex_count), 0,
            static_cast<UINT>(m.face_count));
        if (disable_mesh_lighting) {
          d3d_state.render(D3DRS_LIGHTING, prev_lighting);
        }
        return;
      }
    }
    float base_min_u = std::numeric_limits<float>::infinity();
    float base_min_v = std::numeric_limits<float>::infinity();
    float base_max_u = -std::numeric_limits<float>::infinity();
    float base_max_v = -std::numeric_limits<float>::infinity();
    float final_min_u = std::numeric_limits<float>::infinity();
    float final_min_v = std::numeric_limits<float>::infinity();
    float final_max_u = -std::numeric_limits<float>::infinity();
    float final_max_v = -std::numeric_limits<float>::infinity();
    bool has_uv_range = false;
    if (texture) {
      has_uv_range = true;
      for (size_t vi = 0; vi < m.verts.size(); ++vi) {
        const auto& v = m.verts[vi];
        float base_u = v.u;
        float base_v = v.v;
        if (texcoord_override && vi < texcoord_override->size()) {
          const auto& uv = (*texcoord_override)[vi];
          base_u = v.u + (uv[0] - v.u) * mesh_anim_blend;
          base_v = v.v + (uv[1] - v.v) * mesh_anim_blend;
        }
        base_min_u = std::min(base_min_u, base_u);
        base_min_v = std::min(base_min_v, base_v);
        base_max_u = std::max(base_max_u, base_u);
        base_max_v = std::max(base_max_v, base_v);
        float u = base_u * uv_m00 + base_v * uv_m10;
        float vv = base_u * uv_m01 + base_v * uv_m11;
        u += uv_m20;
        vv += uv_m21;
        final_min_u = std::min(final_min_u, u);
        final_min_v = std::min(final_min_v, vv);
        final_max_u = std::max(final_max_u, u);
        final_max_v = std::max(final_max_v, vv);
      }
    }
    const auto uv_sampler = choose_material_uv_sampler(
        MiloSceneRenderer::MaterialUvBounds{
            has_uv_range, final_min_u, final_min_v, final_max_u, final_max_v},
        su, sv, material_tex_anim);
    const bool uv_repeats = uv_sampler.uv_repeats;
    const bool tiled = uv_sampler.wrap;
    if (texture && env_enabled("GHOGX_LOG_MATERIAL_UV")) {
      static std::unordered_set<std::string> logged_material_uv;
      const std::string key = m.name + "|" + material + "|" + diffuse_tex +
                              "|" + (tiled ? "wrap" : "clamp") +
                              "|" + (texcoord_override ? "uvanim" : "static");
      if (logged_material_uv.insert(key).second) {
        std::fprintf(
            stderr,
            "[milo_scene] material_uv mesh=%s material=%s tex=%s verts=%zu "
            "base=[%.3f..%.3f %.3f..%.3f] final=[%.3f..%.3f %.3f..%.3f] "
            "scale=(%.3f %.3f) offset=(%.3f %.3f) "
            "uvm=[%.3f %.3f %.3f %.3f %.3f %.3f] rot=%.3f anim=%d "
            "uv_override=%d uv_repeat=%d sampler=%s blend=%u prelit=%d\n",
            m.name.c_str(), material.c_str(), diffuse_tex.c_str(),
            m.verts.size(), base_min_u, base_max_u, base_min_v, base_max_v,
            final_min_u, final_max_u, final_min_v, final_max_v, su, sv, tu,
            tv, uv_m00, uv_m01, uv_m10, uv_m11, uv_m20, uv_m21, rot,
            material_tex_anim ? 1 : 0, texcoord_override ? 1 : 0,
            uv_repeats ? 1 : 0, tiled ? "wrap" : "clamp",
            static_cast<unsigned>(material_blend),
            prelit_material ? 1 : 0);
      }
    }
    if (texture) {
      d3d_state.texture(0, texture);
      d3d_state.sampler(
          0, D3DSAMP_ADDRESSU,
          tiled ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
      d3d_state.sampler(
          0, D3DSAMP_ADDRESSV,
          tiled ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
      d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
      d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    } else {
      d3d_state.texture(0, nullptr);
      d3d_state.sampler(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
      d3d_state.sampler(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
      d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
      d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    }

    const bool disable_mesh_lighting =
        debug_spotlight_solid || prelit_lighting_bypass;
    DWORD prev_lighting = TRUE;
    if (disable_mesh_lighting) {
      dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
      d3d_state.render(D3DRS_LIGHTING, FALSE);
      if (prelit_lighting_bypass && env_enabled("GHOGX_LOG_PRELIT_MESHES")) {
        static std::unordered_set<std::string> logged_prelit;
        const std::string key = m.name + "|" + material;
        if (logged_prelit.insert(key).second) {
          std::fprintf(stderr,
                       "[milo_scene] prelit material disables fixed lighting: "
                       "mesh=%s material=%s\n",
                       m.name.c_str(), material.c_str());
        }
      }
    }

    std::vector<std::array<float, 16>> skin_mats;
    if (!m.bones.empty()) {
      const std::array<float, 16> inv_mesh_world = affine_inverse16(w);
      skin_mats.reserve(m.bones.size());
      bool resolved_all_bones = true;
      for (const auto& bone : m.bones) {
        std::array<float, 16> bone_world{};
        if (!scene_node_world_for(scene_, bone.name, bone_world)) {
          resolved_all_bones = false;
          break;
        }
        skin_mats.push_back(
            mul16(mul16(xfm_to_mat4(bone.offset), bone_world),
                  inv_mesh_world));
      }
      if (!resolved_all_bones) skin_mats.clear();
      if (env_enabled("GHOGX_LOG_SKINNED_MILO_MESHES")) {
        static std::unordered_set<std::string> logged_skinned_meshes;
        if (logged_skinned_meshes.insert(m.name).second) {
          std::fprintf(stderr,
                       "[milo_scene] skinned mesh=%s bones=%zu resolved=%zu\n",
                       m.name.c_str(), m.bones.size(), skin_mats.size());
          for (size_t bi = 0; bi < m.bones.size(); ++bi) {
            std::fprintf(stderr,
                         "[milo_scene]   bone[%zu]=%s%s\n", bi,
                         m.bones[bi].name.c_str(),
                         bi < skin_mats.size() ? "" : " unresolved");
          }
        }
      }
    }
    vb.resize(m.verts.size());
    for (size_t vi = 0; vi < m.verts.size(); ++vi) {
      const auto& v = m.verts[vi];
      SVtx& s = vb[vi];
      std::array<float, 3> base_pos{};
      if (position_override && vi < position_override->size()) {
        const auto& p = (*position_override)[vi];
        base_pos = {v.px + (p[0] - v.px) * mesh_anim_blend,
                    v.py + (p[1] - v.py) * mesh_anim_blend,
                    v.pz + (p[2] - v.pz) * mesh_anim_blend};
      } else {
        base_pos = {v.px, v.py, v.pz};
      }
      std::array<float, 3> base_nrm{};
      if (normal_override && vi < normal_override->size()) {
        const auto& n = (*normal_override)[vi];
        base_nrm = {v.nx + (n[0] - v.nx) * mesh_anim_blend,
                    v.ny + (n[1] - v.ny) * mesh_anim_blend,
                    v.nz + (n[2] - v.nz) * mesh_anim_blend};
      } else {
        base_nrm = {v.nx, v.ny, v.nz};
      }
      if (!skin_mats.empty()) {
        std::array<float, 3> skinned_pos{0.0f, 0.0f, 0.0f};
        std::array<float, 3> skinned_nrm{0.0f, 0.0f, 0.0f};
        float weight_sum = 0.0f;
        const size_t slots = std::min<size_t>(skin_mats.size(), 4);
        for (size_t bi = 0; bi < slots; ++bi) {
          const float weight = v.w[bi];
          if (std::fabs(weight) <= 1.0e-6f) continue;
          const auto p = transform_point16(skin_mats[bi], base_pos[0],
                                           base_pos[1], base_pos[2]);
          const auto n = transform_vector16(skin_mats[bi], base_nrm[0],
                                            base_nrm[1], base_nrm[2]);
          for (int k = 0; k < 3; ++k) {
            skinned_pos[k] += p[k] * weight;
            skinned_nrm[k] += n[k] * weight;
          }
          weight_sum += weight;
        }
        if (std::fabs(weight_sum) > 1.0e-6f) {
          base_pos = skinned_pos;
          base_nrm = skinned_nrm;
          normalize_vector3(base_nrm);
        }
      }
      s.x = base_pos[0];
      s.y = base_pos[1];
      s.z = base_pos[2];
      s.nx = base_nrm[0];
      s.ny = base_nrm[1];
      s.nz = base_nrm[2];
      const auto cc = [](float f) -> int {
        int i = static_cast<int>(f * 255.0f + 0.5f);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
      };
      const bool use_color_override =
          color_override && vi < color_override->size();
      const float vr =
          use_color_override
              ? v.r + ((*color_override)[vi][0] - v.r) * mesh_anim_blend
              : v.r;
      const float vg =
          use_color_override
              ? v.g + ((*color_override)[vi][1] - v.g) * mesh_anim_blend
              : v.g;
      const float vc_b =
          use_color_override
              ? v.b + ((*color_override)[vi][2] - v.b) * mesh_anim_blend
              : v.b;
      const float va =
          use_color_override
              ? v.a + ((*color_override)[vi][3] - v.a) * mesh_anim_blend
              : v.a;
      s.color = D3DCOLOR_ARGB(cc(va * ma), cc(vr * mr), cc(vg * mg),
                              cc(vc_b * mb));
      const bool use_texcoord_override =
          texcoord_override && vi < texcoord_override->size();
      float base_u = v.u;
      float base_v = v.v;
      if (use_texcoord_override) {
        const auto& uv = (*texcoord_override)[vi];
        base_u = v.u + (uv[0] - v.u) * mesh_anim_blend;
        base_v = v.v + (uv[1] - v.v) * mesh_anim_blend;
      }
      float u = base_u * uv_m00 + base_v * uv_m10;
      float vv = base_u * uv_m01 + base_v * uv_m11;
      s.u = u + uv_m20;
      s.v = vv + uv_m21;
    }

    bool submitted_static_buffers = false;
    if (static_buffer_eligible) {
      StaticMeshBuffers buffers;
      const UINT vertex_bytes =
          static_cast<UINT>(vb.size() * sizeof(SVtx));
      const UINT index_bytes =
          static_cast<UINT>(m.indices.size() * sizeof(uint16_t));
      if (SUCCEEDED(dev_->CreateVertexBuffer(
              vertex_bytes, D3DUSAGE_WRITEONLY, kFVF, D3DPOOL_MANAGED,
              &buffers.vertices, nullptr)) &&
          SUCCEEDED(dev_->CreateIndexBuffer(
              index_bytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
              D3DPOOL_MANAGED, &buffers.indices, nullptr))) {
        void* vertex_data = nullptr;
        void* index_data = nullptr;
        if (SUCCEEDED(buffers.vertices->Lock(
                0, vertex_bytes, &vertex_data, 0)) &&
            SUCCEEDED(buffers.indices->Lock(
                0, index_bytes, &index_data, 0))) {
          std::memcpy(vertex_data, vb.data(), vertex_bytes);
          std::memcpy(index_data, m.indices.data(), index_bytes);
          buffers.vertices->Unlock();
          buffers.indices->Unlock();
          buffers.wrap = tiled;
          auto [cached, inserted] =
              static_mesh_buffers_.emplace(&m, buffers);
          if (inserted) {
            dev_->SetStreamSource(0, cached->second.vertices, 0,
                                  sizeof(SVtx));
            dev_->SetIndices(cached->second.indices);
            dev_->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST, 0, 0,
                static_cast<UINT>(m.vertex_count), 0,
                static_cast<UINT>(m.face_count));
            submitted_static_buffers = true;
          }
        } else {
          if (vertex_data) buffers.vertices->Unlock();
          if (index_data) buffers.indices->Unlock();
        }
      }
      if (!submitted_static_buffers) {
        if (buffers.vertices) buffers.vertices->Release();
        if (buffers.indices) buffers.indices->Release();
      }
    }
    if (!submitted_static_buffers) {
      dev_->DrawIndexedPrimitiveUP(
          D3DPT_TRIANGLELIST, 0, static_cast<UINT>(m.vertex_count),
          static_cast<UINT>(m.face_count), m.indices.data(), D3DFMT_INDEX16,
          vb.data(), sizeof(SVtx));
    }
    if (disable_mesh_lighting) {
      d3d_state.render(D3DRS_LIGHTING, prev_lighting);
    }
  };

  auto draw_particle_system = [&](const milo_scene::ParticleSysObj& p) {
    if (!p.decoded || !p.showing || p.material.empty()) return;
    if (active_particle_filter_ &&
        active_particle_systems_.find(p.name) == active_particle_systems_.end()) {
      return;
    }
    float intensity = 1.0f;
    if (const auto intensity_it = particle_intensities_.find(p.name);
        intensity_it != particle_intensities_.end()) {
      intensity = intensity_it->second;
    }
    intensity = std::clamp(intensity, 0.0f, 8.0f);
    if (intensity <= 0.001f) return;
    const milo_scene::MatObj* mat = find_material(p.material);
    if (!mat) return;
    IDirect3DTexture9* texture = nullptr;
    if (const auto tex_it = tex_.find(mat->diffuse_tex); tex_it != tex_.end())
      texture = tex_it->second;
    if (!texture) return;
    std::array<float, 4> start_color =
        average_particle_color(p.start_color_low, p.start_color_high);
    std::array<float, 4> mid_color =
        average_particle_color(p.mid_color_low, p.mid_color_high);
    std::array<float, 4> end_color =
        average_particle_color(p.end_color_low, p.end_color_high);
    if (const auto color_it = particle_start_colors_.find(p.name);
        color_it != particle_start_colors_.end()) {
      start_color = average_particle_color_from_key(
          color_it->second, p.start_color_low, p.start_color_high);
    }
    if (const auto color_it = particle_end_colors_.find(p.name);
        color_it != particle_end_colors_.end()) {
      end_color = average_particle_color_from_key(
          color_it->second, p.end_color_low, p.end_color_high);
    }

    const int count = static_cast<int>(std::clamp(
        static_cast<float>(p.max_particles > 0 ? p.max_particles : 16u) *
            std::max(intensity, 0.0f),
        1.0f, 512.0f));
    float lifetime =
        std::clamp((p.lifetime_min + p.lifetime_max) * 0.5f, 0.05f, 20.0f);
    if (const auto life_it = particle_lifetimes_.find(p.name);
        life_it != particle_lifetimes_.end()) {
      lifetime = std::clamp(life_it->second / 30.0f, 0.05f, 20.0f);
    }
    const float base_speed =
        std::clamp((p.speed_min + p.speed_max) * 0.5f, 0.0f, 10000.0f);
    float authored_speed = base_speed;
    bool has_speed_override = false;
    if (const auto speed_it = particle_speeds_.find(p.name);
        speed_it != particle_speeds_.end()) {
      authored_speed = std::clamp(speed_it->second, 0.0f, 10000.0f);
      has_speed_override = true;
    }
    float start_size =
        std::max(0.0f, (p.start_size_min + p.start_size_max) * 0.5f);
    bool has_size_override = false;
    if (const auto size_it = particle_sizes_.find(p.name);
        size_it != particle_sizes_.end()) {
      start_size = std::max(0.0f, size_it->second);
      has_size_override = true;
    }
    auto world = mul16(scene_.world_matrix(p), world_transform_);
    // ihatecompvir's recovered WiiParticleSys::DrawShowing has four explicit
    // Vector2 corners (v1..v4), camera-derived right/up vectors, and a scalar
    // particle size. Mirror that source contract with world-space billboard
    // quads. D3D point sprites are screen-sized and had been turning GH2's
    // authored 5..80 world-unit dry-ice particles into fixed 80-pixel cards.
    const float camera_right[3] = {view.m[0][0], view.m[1][0], view.m[2][0]};
    const float camera_up[3] = {view.m[0][1], view.m[1][1], view.m[2][1]};
    std::vector<ParticleBillboardVertex> billboards;
    billboards.reserve(static_cast<size_t>(count) * 6u);
    const uint32_t seed_base = static_cast<uint32_t>(
        std::hash<std::string>{}(p.name) & 0xffffffffu);
    for (int i = 0; i < count; ++i) {
      const uint32_t seed = seed_base + static_cast<uint32_t>(i) * 977u;
      const float h0 = hash01(seed + 1u);
      const float h1 = hash01(seed + 2u);
      const float h2 = hash01(seed + 3u);
      const float h3 = hash01(seed + 4u);
      const float h4 = hash01(seed + 5u);
      const float h5 = hash01(seed + 6u);
      const float h6 = hash01(seed + 7u);
      const float h7 = hash01(seed + 8u);
      const float h8 = hash01(seed + 9u);
      const float phase = std::fmod(particle_time_ / lifetime + h0, 1.0f);
      const float hashes[3] = {h1, h2, h3};
      float local[3] = {};
      for (int c = 0; c < 3; ++c) {
        const float lo = std::min(p.box_extent_min[c], p.box_extent_max[c]);
        const float hi = std::max(p.box_extent_min[c], p.box_extent_max[c]);
        local[c] = lo + (hi - lo) * hashes[c];
      }
      const float speed = has_speed_override
                              ? authored_speed
                              : p.speed_min + (p.speed_max - p.speed_min) * h4;
      const float pitch = p.pitch_min + (p.pitch_max - p.pitch_min) * h5;
      const float yaw = p.yaw_min + (p.yaw_max - p.yaw_min) * h6;
      const float cp = std::cos(pitch);
      const float dir[3] = {std::sin(yaw) * cp, std::cos(yaw) * cp,
                            std::sin(pitch)};
      const float travel_frames = phase * lifetime * 30.0f;
      for (int c = 0; c < 3; ++c) {
        local[c] += dir[c] * speed * travel_frames;
        local[c] += 0.5f * p.force_dir[c] * travel_frames * travel_frames;
      }
      if (p.bubble) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float bubble_period =
            std::max(0.001f, p.bubble_period_min +
                                 (p.bubble_period_max -
                                  p.bubble_period_min) *
                                     h7);
        const float bubble_size =
            p.bubble_size_min + (p.bubble_size_max - p.bubble_size_min) * h8;
        const float bubble_frame = particle_time_ * 30.0f + h0 * bubble_period;
        const float bubble_angle = bubble_frame / bubble_period * kTwoPi;
        local[0] += std::cos(bubble_angle) * bubble_size;
        local[2] += std::sin(bubble_angle + h1 * kTwoPi) * bubble_size;
      }
      const float center[3] = {
          world[12] + local[0] * world[0] + local[1] * world[4] +
              local[2] * world[8],
          world[13] + local[0] * world[1] + local[1] * world[5] +
              local[2] * world[9],
          world[14] + local[0] * world[2] + local[1] * world[6] +
              local[2] * world[10]};
      const float particle_start_size =
          has_size_override
              ? start_size
              : p.start_size_min + (p.start_size_max - p.start_size_min) * h7;
      const float particle_delta_size =
          p.delta_size_min + (p.delta_size_max - p.delta_size_min) * h8;
      const float grow_shrink =
          sample_particle_grow_shrink(p.grow_ratio, p.shrink_ratio, phase);
      const float particle_size = std::clamp(
          std::max(0.0f, particle_start_size + particle_delta_size * phase) *
              grow_shrink,
          0.0f, 10000.0f);
      if (particle_size <= 0.0001f) continue;
      const auto cc = [](float f) -> int {
        int i = static_cast<int>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
        return std::clamp(i, 0, 255);
      };
      const std::array<float, 4> sampled_color =
          sample_particle_color_with_mid(start_color, mid_color, end_color,
                                         p.mid_color_ratio, phase);
      const float alpha =
          std::clamp(mat->color[3], 0.0f, 1.0f) *
          std::clamp(intensity, 0.0f, 1.0f) *
          std::clamp(sampled_color[3], 0.0f, 1.0f);
      const float red = mat->color[0] * sampled_color[0];
      const float green = mat->color[1] * sampled_color[1];
      const float blue = mat->color[2] * sampled_color[2];
      const D3DCOLOR color =
          D3DCOLOR_ARGB(cc(alpha), cc(red), cc(green), cc(blue));
      const float half_size = particle_size * 0.5f;
      auto corner = [&](float right_scale, float up_scale, float u, float v) {
        ParticleBillboardVertex out{};
        out.x = center[0] + camera_right[0] * right_scale * half_size +
                camera_up[0] * up_scale * half_size;
        out.y = center[1] + camera_right[1] * right_scale * half_size +
                camera_up[1] * up_scale * half_size;
        out.z = center[2] + camera_right[2] * right_scale * half_size +
                camera_up[2] * up_scale * half_size;
        out.color = color;
        out.u = u;
        out.v = v;
        return out;
      };
      const ParticleBillboardVertex top_left = corner(-1.0f, 1.0f, 0.0f, 0.0f);
      const ParticleBillboardVertex top_right = corner(1.0f, 1.0f, 1.0f, 0.0f);
      const ParticleBillboardVertex bottom_right =
          corner(1.0f, -1.0f, 1.0f, 1.0f);
      const ParticleBillboardVertex bottom_left =
          corner(-1.0f, -1.0f, 0.0f, 1.0f);
      billboards.push_back(top_left);
      billboards.push_back(top_right);
      billboards.push_back(bottom_right);
      billboards.push_back(top_left);
      billboards.push_back(bottom_right);
      billboards.push_back(bottom_left);
    }
    if (billboards.empty()) return;

    DWORD old_lighting = TRUE;
    DWORD old_zenable = TRUE;
    DWORD old_zwrite = TRUE;
    DWORD old_cull = D3DCULL_CW;
    DWORD old_alpha_blend = TRUE;
    DWORD old_src = D3DBLEND_SRCALPHA;
    DWORD old_dest = D3DBLEND_INVSRCALPHA;
    DWORD old_op = D3DBLENDOP_ADD;
    DWORD old_point_sprite = FALSE;
    DWORD old_point_scale = FALSE;
    DWORD old_fvf = kFVF;
    DWORD old_color_op = D3DTOP_MODULATE;
    DWORD old_color_arg1 = D3DTA_TEXTURE;
    DWORD old_color_arg2 = D3DTA_DIFFUSE;
    DWORD old_alpha_op = D3DTOP_MODULATE;
    DWORD old_alpha_arg1 = D3DTA_TEXTURE;
    DWORD old_alpha_arg2 = D3DTA_DIFFUSE;
    DWORD old_address_u = D3DTADDRESS_WRAP;
    DWORD old_address_v = D3DTADDRESS_WRAP;
    IDirect3DBaseTexture9* old_texture = nullptr;
    D3DMATRIX old_world{};
    const BlendState blend_state = blend_state_for(mat->blend);
    dev_->GetRenderState(D3DRS_LIGHTING, &old_lighting);
    dev_->GetRenderState(D3DRS_ZENABLE, &old_zenable);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &old_zwrite);
    dev_->GetRenderState(D3DRS_CULLMODE, &old_cull);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_alpha_blend);
    dev_->GetRenderState(D3DRS_SRCBLEND, &old_src);
    dev_->GetRenderState(D3DRS_DESTBLEND, &old_dest);
    dev_->GetRenderState(D3DRS_BLENDOP, &old_op);
    dev_->GetRenderState(D3DRS_POINTSPRITEENABLE, &old_point_sprite);
    dev_->GetRenderState(D3DRS_POINTSCALEENABLE, &old_point_scale);
    dev_->GetFVF(&old_fvf);
    dev_->GetTextureStageState(0, D3DTSS_COLOROP, &old_color_op);
    dev_->GetTextureStageState(0, D3DTSS_COLORARG1, &old_color_arg1);
    dev_->GetTextureStageState(0, D3DTSS_COLORARG2, &old_color_arg2);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAOP, &old_alpha_op);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG1, &old_alpha_arg1);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG2, &old_alpha_arg2);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSU, &old_address_u);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSV, &old_address_v);
    dev_->GetTexture(0, &old_texture);
    dev_->GetTransform(D3DTS_WORLD, &old_world);
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    dev_->SetTransform(D3DTS_WORLD, &identity);
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev_->SetRenderState(D3DRS_BLENDOP, blend_state.op);
    dev_->SetRenderState(D3DRS_SRCBLEND, blend_state.src);
    dev_->SetRenderState(D3DRS_DESTBLEND, blend_state.dest);
    dev_->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev_->SetTexture(0, texture);
    dev_->SetFVF(kParticleBillboardFVF);
    dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                          static_cast<UINT>(billboards.size() / 3u),
                          billboards.data(), sizeof(ParticleBillboardVertex));
    dev_->SetTransform(D3DTS_WORLD, &old_world);
    dev_->SetRenderState(D3DRS_LIGHTING, old_lighting);
    dev_->SetRenderState(D3DRS_ZENABLE, old_zenable);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, old_zwrite);
    dev_->SetRenderState(D3DRS_CULLMODE, old_cull);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, old_alpha_blend);
    dev_->SetRenderState(D3DRS_SRCBLEND, old_src);
    dev_->SetRenderState(D3DRS_DESTBLEND, old_dest);
    dev_->SetRenderState(D3DRS_BLENDOP, old_op);
    dev_->SetRenderState(D3DRS_POINTSPRITEENABLE, old_point_sprite);
    dev_->SetRenderState(D3DRS_POINTSCALEENABLE, old_point_scale);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, old_color_op);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, old_color_arg1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, old_color_arg2);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, old_alpha_op);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, old_alpha_arg1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, old_alpha_arg2);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, old_address_u);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, old_address_v);
    dev_->SetTexture(0, old_texture);
    if (old_texture) old_texture->Release();
    dev_->SetFVF(old_fvf);
  };

  struct PostTextMesh {
    const milo_scene::MeshObj* mesh = nullptr;
    std::array<float, 16> world{};
  };
  std::vector<PostTextMesh> post_text_draw_meshes;

  // A split live-menu draw renders the base scene, then character/guitar
  // renderers, then calls draw_text_over_scene(). Traverse the scene again for
  // explicitly deferred foreground meshes during that text-only pass so those
  // meshes can remain coupled to their overlay labels.
  if (draw_scene || (draw_text && !post_text_meshes_.empty())) {
    const auto& draw_meshes = ordered_mesh_draws_;
    if (env_enabled("GHOGX_LOG_SCENE_DRAW_ORDER")) {
      static std::unordered_set<const MiloSceneRenderer*> logged_draw_order;
      if (logged_draw_order.insert(this).second) {
        std::fprintf(stderr,
                     "[milo_scene] renderer draw order meshes=%zu "
                     "groups=%zu grouped=%zu source_order=%zu\n",
                     draw_meshes.size(), scene_.groups.size(),
                     scene_.grouped_meshes.size(), scene_.draw_order.size());
        for (const auto& group : scene_.groups) {
          std::fprintf(stderr,
                       "[milo_scene] draw_group=%s showing=%d legacy_view=%d "
                       "children=%zu anim_children=%zu draw_only=%s parent=%s\n",
                       group.name.c_str(), group.showing ? 1 : 0,
                       group.legacy_view ? 1 : 0, group.children.size(),
                       group.anim_children.size(), group.draw_only.c_str(),
                       group.parent.c_str());
        }
        for (size_t i = 0; i < draw_meshes.size(); ++i) {
          const auto& draw = draw_meshes[i];
          const auto* mesh = draw.mesh;
          const auto* mat = mesh ? scene_.find_mat(mesh->material) : nullptr;
          std::fprintf(stderr,
                       "[milo_scene] draw_order[%zu]=%s material=%s zmode=%u "
                       "parent=%s instance_of=%s\n",
                       i,
                       draw.multi_mesh ? draw.multi_mesh->name.c_str()
                                       : (mesh ? mesh->name.c_str() : "<null>"),
                       mesh ? mesh->material.c_str() : "<none>",
                       mat ? static_cast<unsigned>(mat->z_mode) : 255u,
                       mesh ? mesh->parent.c_str() : "<none>",
                       draw.multi_mesh
                           ? draw.multi_mesh->mesh.c_str()
                           : "<none>");
        }
      }
    }

    const bool draw_spotlight_instances =
        !env_enabled("GHOGX_DISABLE_SPOTLIGHT_INSTANCES");
    const bool debug_camera_meshes =
        env_enabled("GHOGX_DEBUG_CAMERA_MESHES");
    std::vector<DebugVenuePickAccumulator> venue_picks;
    const bool debug_source_pick =
        debug_venue && (env_enabled("GHOGX_VENUE_PICK_SOURCE") ||
                        !env_enabled("GHOGX_VENUE_PICK_RENDER_ONLY"));
    const bool venue_frustum_cull_enabled =
        !env_enabled("GHOGX_DISABLE_VENUE_FRUSTUM_CULL") && !debug_venue &&
        !debug_camera_meshes &&
        !(cam_.result_frame.valid && cam_.result_frame.has_custom_projection);
    if (debug_venue) {
      float pick_ndc[2] = {0.0f, 0.0f};
      if (env_float_pair("GHOGX_VENUE_PICK_NDC", pick_ndc)) {
        pick_ndc[0] = std::clamp(pick_ndc[0], -1.0f, 1.0f);
        pick_ndc[1] = std::clamp(pick_ndc[1], -1.0f, 1.0f);
      }
      debug_venue->crosshair_ndc_x = pick_ndc[0];
      debug_venue->crosshair_ndc_y = pick_ndc[1];
      venue_picks.push_back(make_debug_venue_pick_accumulator(
          "primary", pick_ndc[0], pick_ndc[1], eye, at, up, cam_.fov,
          aspect));
      if (debug_source_pick) {
        auto source_pick = make_debug_venue_pick_accumulator(
            "source", pick_ndc[0], pick_ndc[1], eye, at, up, cam_.fov,
            aspect);
        source_pick.source_mode = true;
        venue_picks.push_back(std::move(source_pick));
      }
      if (env_enabled("GHOGX_VENUE_PICK_GRID")) {
        venue_picks.push_back(make_debug_venue_pick_accumulator(
            "lower_left", -0.72f, -0.72f, eye, at, up, cam_.fov, aspect));
        venue_picks.push_back(make_debug_venue_pick_accumulator(
            "lower_center", 0.0f, -0.72f, eye, at, up, cam_.fov, aspect));
        venue_picks.push_back(make_debug_venue_pick_accumulator(
            "lower_right", 0.72f, -0.72f, eye, at, up, cam_.fov, aspect));
        venue_picks.push_back(make_debug_venue_pick_accumulator(
            "mid_left", -0.72f, 0.0f, eye, at, up, cam_.fov, aspect));
        venue_picks.push_back(make_debug_venue_pick_accumulator(
            "mid_right", 0.72f, 0.0f, eye, at, up, cam_.fov, aspect));
        if (debug_source_pick) {
          auto lower_left_source = make_debug_venue_pick_accumulator(
              "source_lower_left", -0.72f, -0.72f, eye, at, up, cam_.fov,
              aspect);
          auto lower_center_source = make_debug_venue_pick_accumulator(
              "source_lower_center", 0.0f, -0.72f, eye, at, up, cam_.fov,
              aspect);
          auto lower_right_source = make_debug_venue_pick_accumulator(
              "source_lower_right", 0.72f, -0.72f, eye, at, up, cam_.fov,
              aspect);
          lower_left_source.source_mode = true;
          lower_center_source.source_mode = true;
          lower_right_source.source_mode = true;
          venue_picks.push_back(std::move(lower_left_source));
          venue_picks.push_back(std::move(lower_center_source));
          venue_picks.push_back(std::move(lower_right_source));
        }
      }
    }
  struct CameraMeshHit {
    std::string name;
    std::string material;
    float center[3] = {0, 0, 0};
    float radius = 0.0f;
    float distance = 0.0f;
  };
  std::vector<CameraMeshHit> camera_mesh_hits;
  struct ProjectedCameraMeshHit {
    std::string name;
    std::string material;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float clipped_area = 0.0f;
    size_t in_front = 0;
    size_t on_screen = 0;
    size_t verts = 0;
    float distance = 0.0f;
  };
    std::vector<ProjectedCameraMeshHit> projected_camera_mesh_hits;
    const auto& spotlight_template_meshes = spotlight_template_meshes_;

  for (const auto& ordered_draw : draw_meshes) {
    if (!ordered_draw.mesh) continue;
    if (ordered_draw.multi_mesh && nonowning_gh1_crowd_drawables_.count(
            ordered_draw.multi_mesh->name)) continue;
    // Standing presentation requirement: every owned crowd card is replaced
    // by a 3D actor. Retail region membership remains available for auditing.
    if (gh1_crowd_regions_.owns(ordered_draw.instance_world)) continue;
    const auto& m = *ordered_draw.mesh;
    const bool multi_mesh_instance =
        ordered_draw.multi_mesh && ordered_draw.instance_world;
    const std::string& drawable_name =
        multi_mesh_instance ? ordered_draw.multi_mesh->name : m.name;
    const bool hidden_by_runtime =
        hidden_meshes_.find(drawable_name) != hidden_meshes_.end() ||
        hidden_meshes_.find(m.name) != hidden_meshes_.end();
    const bool hidden_by_debug_skip =
        mesh_matches_env_spec("GHOGX_SKIP_VENUE_MESH", m.name);
    const bool spotlight_template_mesh =
        spotlight_template_meshes.find(m.name) != spotlight_template_meshes.end();
    const auto* pick_mat = find_material(m.material);
    const bool material_zero_alpha =
        pick_mat && pick_mat->color[3] <= 0.001f;
    const bool source_showing =
        (multi_mesh_instance ? ordered_draw.multi_mesh->showing : m.showing) ||
        legacy_environment_drawables_.find(m.name) !=
            legacy_environment_drawables_.end();
    const bool would_reach_draw =
        m.decoded &&
        (!multi_mesh_instance || ordered_draw.multi_mesh->decoded) &&
        source_showing && !hidden_by_runtime && !hidden_by_debug_skip &&
        !spotlight_template_mesh && !material_zero_alpha;
    const bool debug_source_highlighted =
        debug_venue && !debug_venue->highlight_mesh.empty() &&
        m.name == debug_venue->highlight_mesh;
    if (!would_reach_draw && !debug_source_pick && !debug_source_highlighted)
      continue;
    auto target_kind_for = [&](const std::string& name) -> const char* {
      if (m.name == name) return "Mesh";
      for (const auto& mesh : scene_.meshes) {
        if (mesh.name == name) return "Mesh";
      }
      for (const auto& trans : scene_.transes) {
        if (trans.name == name) return "Trans";
      }
      for (const auto& group : scene_.groups) {
        if (group.name == name) return "Group";
      }
      for (const auto& placer : scene_.band_placers) {
        if (placer.name == name) return "BandPlacer";
      }
      for (const auto& light : scene_.lights) {
        if (light.name == name) return "Light";
      }
      return "Unknown";
    };
    auto parent_for = [&](const std::string& name) -> std::string {
      const auto it = authored_parents_.find(name);
      return it == authored_parents_.end() ? std::string{} : it->second;
    };
    auto constraint_for = [&](const std::string& name) -> uint32_t {
      if (m.name == name) return m.constraint;
      for (const auto& mesh : scene_.meshes) {
        if (mesh.name == name) return mesh.constraint;
      }
      for (const auto& group : scene_.groups) {
        if (group.name == name && group.has_transform) return group.constraint;
      }
      for (const auto& trans : scene_.transes) {
        if (trans.name == name) return trans.constraint;
      }
      for (const auto& placer : scene_.band_placers) {
        if (placer.name == name) return 0;
      }
      return 0;
    };
    auto world_override_for = [&](const std::string& name,
                                  std::array<float, 16>& world) -> bool {
      if (m.name == name && m.world_xfm_override) {
        world = xfm_to_mat4(m.world_stored);
        return true;
      }
      for (const auto& mesh : scene_.meshes) {
        if (mesh.name == name && mesh.world_xfm_override) {
          world = xfm_to_mat4(mesh.world_stored);
          return true;
        }
      }
      for (const auto& group : scene_.groups) {
        if (group.name == name && group.has_transform &&
            group.world_xfm_override) {
          world = xfm_to_mat4(group.world_stored);
          return true;
        }
      }
      for (const auto& trans : scene_.transes) {
        if (trans.name == name && trans.world_xfm_override) {
          world = xfm_to_mat4(trans.world_stored);
          return true;
        }
      }
      return false;
    };
    auto local_for = [&](const std::string& name, std::array<float, 16>& local)
        -> bool {
      if (m.name == name) {
        local = xfm_to_mat4(m.local);
        return true;
      }
      for (const auto& group : scene_.groups) {
        if (group.name == name) {
          local = group.has_transform ? xfm_to_mat4(group.local) : identity16();
          return true;
        }
      }
      for (const auto& mesh : scene_.meshes) {
        if (mesh.name == name) {
          local = xfm_to_mat4(mesh.local);
          return true;
        }
      }
      for (const auto& trans : scene_.transes) {
        if (trans.name == name) {
          local = xfm_to_mat4(trans.local);
          return true;
        }
      }
      for (const auto& placer : scene_.band_placers) {
        if (placer.name == name && placer.decoded) {
          local = xfm_to_mat4(placer.local);
          return true;
        }
      }
      return false;
    };
    auto composed_world_for = [&](const std::string& name,
                                  std::array<float, 16>& world) -> bool {
      auto impl = [&](auto&& self, const std::string& node,
                      std::array<float, 16>& out, int guard) -> bool {
        if (guard >= 64) return false;
        if (transform_parent_overrides_.find(node) !=
                transform_parent_overrides_.end() &&
            resolve_transform_parent_world(node, out)) {
          return true;
        }
        if (world_override_for(node, out)) return true;
        std::array<float, 16> local{};
        if (!local_for(node, local)) return false;
        const std::string parent = parent_for(node);
        if (parent.empty() || parent == node) {
          out = local;
          return true;
        }
        std::array<float, 16> parent_world{};
        if (!self(self, parent, parent_world, guard + 1)) return false;
        out = apply_transform_constraint(local, parent_world,
                                         constraint_for(node));
        return true;
      };
      return impl(impl, name, world, 0);
    };
    auto base_world_for = [&](const std::string& name,
                              std::array<float, 16>& world) -> bool {
      if (composed_world_for(name, world)) return true;
      if (m.name == name) {
        const auto cached = base_mesh_worlds_.find(&m);
        world = cached != base_mesh_worlds_.end()
                    ? cached->second
                    : scene_.world_matrix(m);
        return true;
      }
      const auto mesh_it = meshes_by_name_.find(name);
      if (mesh_it != meshes_by_name_.end() && mesh_it->second) {
        const auto cached = base_mesh_worlds_.find(mesh_it->second);
        world = cached != base_mesh_worlds_.end()
                    ? cached->second
                    : scene_.world_matrix(*mesh_it->second);
        return true;
      }
      for (const auto& group : scene_.groups) {
        if (group.name == name) {
          world =
              group.has_transform ? xfm_to_mat4(group.world_stored) : identity16();
          return true;
        }
      }
      for (const auto& trans : scene_.transes) {
        if (trans.name == name) {
          world = xfm_to_mat4(trans.world_stored);
          return true;
        }
      }
      for (const auto& placer : scene_.band_placers) {
        if (placer.name == name && placer.decoded) {
          world = xfm_to_mat4(placer.world_stored);
          return true;
        }
      }
      return false;
    };
    auto target_has_transform_sample = [&](const std::string& target) -> bool {
      return mesh_transform_offsets_.find(target) !=
                 mesh_transform_offsets_.end() ||
             active_mesh_anims_.find(target) != active_mesh_anims_.end();
    };
    auto apply_transform_samples = [&](std::array<float, 16>& local,
                                       const std::string& target) {
      if (const auto offset_it = mesh_transform_offsets_.find(target);
          offset_it != mesh_transform_offsets_.end()) {
        apply_mesh_transform_sample(local, offset_it->second);
      }
      const auto anim_it = active_mesh_anims_.find(target);
      if (anim_it == active_mesh_anims_.end()) return;
      const auto& active = anim_it->second;
      const float anim_frame = active.elapsed * active.frames_per_second;
      apply_mesh_transform_sample(
          local, sample_transform_anim(active.anim, anim_frame));
    };
    std::vector<std::string> animated_ancestors;
    if (!multi_mesh_instance) {
      for (std::string parent = m.parent; !parent.empty();) {
        animated_ancestors.push_back(parent);
        const std::string next = parent_for(parent);
        if (next.empty() || next == parent) break;
        parent = next;
        if (animated_ancestors.size() >= 64) break;
      }
    }
    std::reverse(animated_ancestors.begin(), animated_ancestors.end());
    const bool chain_has_transform_sample =
        !multi_mesh_instance &&
        (target_has_transform_sample(m.name) ||
         std::any_of(animated_ancestors.begin(), animated_ancestors.end(),
                     target_has_transform_sample));
    const bool chain_has_world_override =
        !multi_mesh_instance &&
        (m.world_xfm_override ||
         std::any_of(animated_ancestors.begin(), animated_ancestors.end(),
                     [&](const std::string& target) {
                       std::array<float, 16> ignored{};
                       return world_override_for(target, ignored);
                     }));
    const auto cached_base_world = base_mesh_worlds_.find(&m);
    auto world =
        multi_mesh_instance
            ? xfm_to_mat4(*ordered_draw.instance_world)
            : (cached_base_world != base_mesh_worlds_.end()
                   ? cached_base_world->second
                   : scene_.world_matrix(m));
    bool recomposed_animated_chain = false;
    if (chain_has_transform_sample || chain_has_world_override) {
      std::vector<std::string> chain;
      chain.reserve(animated_ancestors.size() + 1);
      for (const auto& ancestor : animated_ancestors) chain.push_back(ancestor);
      chain.push_back(m.name);
      std::array<float, 16> anchor_base{};
      std::array<float, 16> anchor_current{};
      bool have_anchor = false;
      bool resolved_all_nodes = true;
      std::array<float, 16> composed = world;
      for (const std::string& target : chain) {
        const bool target_is_mesh = target == m.name;
        const bool target_sampled = target_has_transform_sample(target);
        if (!target_sampled && !target_is_mesh) continue;
        std::array<float, 16> base_world{};
        if (!base_world_for(target, base_world)) {
          resolved_all_nodes = false;
          break;
        }
        std::array<float, 16> current_world = base_world;
        if (have_anchor) {
          current_world =
              mul16(mul16(base_world, affine_inverse16(anchor_base)),
                    anchor_current);
        }
        if (target_sampled) {
          std::array<float, 16> base_local{};
          if (!local_for(target, base_local)) {
            resolved_all_nodes = false;
            break;
          }
          std::array<float, 16> sampled_local = base_local;
          const bool log_local =
              env_mesh_filter_matches("GHOGX_LOG_MESH_ANIM_LOCAL", m.name) ||
              env_mesh_filter_matches("GHOGX_LOG_MESH_ANIM_LOCAL", target);
          const auto offset_it = mesh_transform_offsets_.find(target);
          const bool has_offset = offset_it != mesh_transform_offsets_.end();
          const auto active_it = active_mesh_anims_.find(target);
          const bool has_active_anim = active_it != active_mesh_anims_.end();
          MiloSceneRenderer::MeshTransformSample active_sample;
          const MiloSceneRenderer::MeshTransformSample* applied_sample =
              nullptr;
          float active_frame = 0.0f;
          if (has_offset) {
            applied_sample = &offset_it->second;
            apply_mesh_transform_sample(sampled_local, offset_it->second);
          }
          if (has_active_anim) {
            const auto& active = active_it->second;
            active_frame = active.elapsed * active.frames_per_second;
            active_sample = sample_transform_anim(active.anim, active_frame);
            applied_sample = &active_sample;
            apply_mesh_transform_sample(sampled_local, active_sample);
          }
          if (log_local) {
            log_mesh_anim_local_rows(
                m.name, target, target_kind_for(target), parent_for(target),
                base_local, sampled_local, applied_sample, active_frame,
                has_offset, has_active_anim);
          }
          current_world =
              mul16(mul16(sampled_local, affine_inverse16(base_local)),
                    current_world);
          anchor_base = base_world;
          anchor_current = current_world;
          have_anchor = true;
        }
        if (target_is_mesh) composed = current_world;
      }
      if (resolved_all_nodes) {
        world = composed;
        recomposed_animated_chain = true;
      }
    }
    if (!multi_mesh_instance && !recomposed_animated_chain) {
      for (const std::string& target : animated_ancestors) {
        apply_transform_samples(world, target);
      }
      apply_transform_samples(world, m.name);
    }
    if (!multi_mesh_instance) {
      const auto pulse_it = mesh_pulses_.find(m.name);
      if (pulse_it != mesh_pulses_.end()) {
        world[14] += pulse_it->second;
      }
      if (face_camera_meshes_.find(m.name) != face_camera_meshes_.end()) {
        apply_face_camera_yaw(world, m, eye);
      }
    }
    auto draw_world = mul16(world, world_transform_);
    const auto source_winding_it = source_mesh_winding_reversed_.find(&m);
    const bool source_winding_reversed =
        source_winding_it != source_mesh_winding_reversed_.end() &&
        source_winding_it->second;
    const DWORD pick_cull_mode =
        venue_mesh_cull_mode(pick_mat, draw_world, source_winding_reversed,
                             authored_cull_mode, false);
    const bool cull_enabled = pick_cull_mode != D3DCULL_NONE;
    if (chain_has_transform_sample &&
        env_mesh_filter_matches("GHOGX_LOG_MESH_ANIM_WORLD", m.name)) {
      static std::unordered_map<std::string, size_t> logged_mesh_world_rows;
      const size_t sample = ++logged_mesh_world_rows[m.name];
      if (should_log_mesh_anim_sample(sample)) {
        const MeshLocalAxisDiagnostics axes = mesh_local_axis_diagnostics(m);
        const std::array<float, 3> world_face =
            axes.valid ? transform_local_dir(world, axes.face_dir)
                       : std::array<float, 3>{0.0f, 0.0f, 0.0f};
        const std::array<float, 3> draw_face =
            axes.valid ? transform_local_dir(draw_world, axes.face_dir)
                       : std::array<float, 3>{0.0f, 0.0f, 0.0f};
        std::fprintf(
            stderr,
            "[milo_scene] mesh_anim_world mesh=%s parent=%s sample=%zu "
            "recomposed=%d world_pos=(%.6f %.6f %.6f) "
            "row0=(%.6f %.6f %.6f) row1=(%.6f %.6f %.6f) "
            "row2=(%.6f %.6f %.6f) row_len=(%.6f %.6f %.6f) "
            "draw_pos=(%.6f %.6f %.6f) draw_row0=(%.6f %.6f %.6f) "
            "draw_row1=(%.6f %.6f %.6f) draw_row2=(%.6f %.6f %.6f) "
            "shape_extent=(%.6f %.6f %.6f) thin_axis=%s "
            "tri_norm_abs=(%.6f %.6f %.6f) face_axis=%s "
            "face_dir=(%.1f %.1f %.1f) world_face=(%.6f %.6f %.6f) "
            "draw_face=(%.6f %.6f %.6f)\n",
            m.name.c_str(), m.parent.c_str(), sample,
            recomposed_animated_chain ? 1 : 0, world[12], world[13],
            world[14], world[0], world[1], world[2], world[4], world[5],
            world[6], world[8], world[9], world[10], row_len16(world, 0),
            row_len16(world, 1), row_len16(world, 2), draw_world[12],
            draw_world[13], draw_world[14], draw_world[0], draw_world[1],
            draw_world[2], draw_world[4], draw_world[5], draw_world[6],
            draw_world[8], draw_world[9], draw_world[10], axes.extent[0],
            axes.extent[1], axes.extent[2], axis_label(axes.thin_axis),
            axes.triangle_normal_abs[0], axes.triangle_normal_abs[1],
            axes.triangle_normal_abs[2], axis_label(axes.face_axis),
            axes.face_dir[0], axes.face_dir[1], axes.face_dir[2],
            world_face[0], world_face[1], world_face[2], draw_face[0],
            draw_face[1], draw_face[2]);
      }
    }
    for (auto& venue_pick : venue_picks) {
      if (!venue_pick.source_mode && !would_reach_draw) continue;
      DebugVenuePickMeshState pick_mesh_state;
      if (ordered_draw.multi_mesh) pick_mesh_state.multi_mesh = ordered_draw.multi_mesh->name;
      pick_mesh_state.would_draw = would_reach_draw;
      pick_mesh_state.source_pick = venue_pick.source_mode;
      pick_mesh_state.hidden_by_filter =
          hidden_by_runtime || hidden_by_debug_skip;
      pick_mesh_state.source_showing = m.showing;
      pick_mesh_state.material_invisible = material_zero_alpha;
      pick_mesh_state.spotlight_template = spotlight_template_mesh;
      pick_mesh_state.cull_enabled = cull_enabled;
      pick_mesh_state.cull_mode = pick_cull_mode;
      accumulate_debug_venue_pick(venue_pick, m, draw_world,
                                  pick_mesh_state);
    }
    if (post_text_meshes_.find(m.name) != post_text_meshes_.end()) {
      if (const auto overlay_it = post_text_mesh_world_offsets_.find(m.name);
          overlay_it != post_text_mesh_world_offsets_.end()) {
        draw_world[12] += overlay_it->second[0];
        draw_world[13] += overlay_it->second[1];
        draw_world[14] += overlay_it->second[2];
      }
      post_text_draw_meshes.push_back({&m, draw_world});
      continue;
    }
    if (!draw_scene) continue;
    if (venue_frustum_cull_enabled && would_reach_draw &&
        !debug_source_highlighted) {
      const auto pos_it = mesh_position_overrides_.find(m.name);
      const bool has_position_override =
          pos_it != mesh_position_overrides_.end() && !pos_it->second.empty();
      if (!has_position_override) {
        const auto wvp = mul16(draw_world, view_proj_arr);
        if (mesh_bbox_outside_clip_frustum(m, wvp)) continue;
      }
    }
    if (debug_camera_meshes && m.decoded && !m.verts.empty()) {
      float mn[3] = {0, 0, 0};
      float mx[3] = {0, 0, 0};
      bool have = false;
      for (const auto& v : m.verts) {
        const float p[3] = {
            v.px * world[0] + v.py * world[4] + v.pz * world[8] + world[12],
            v.px * world[1] + v.py * world[5] + v.pz * world[9] + world[13],
            v.px * world[2] + v.py * world[6] + v.pz * world[10] + world[14]};
        if (!have) {
          for (int k = 0; k < 3; ++k) mn[k] = mx[k] = p[k];
          have = true;
        } else {
          for (int k = 0; k < 3; ++k) {
            mn[k] = std::min(mn[k], p[k]);
            mx[k] = std::max(mx[k], p[k]);
          }
        }
      }
      if (have) {
        CameraMeshHit hit;
        hit.name = m.name;
        hit.material = m.material;
        for (int k = 0; k < 3; ++k) hit.center[k] = (mn[k] + mx[k]) * 0.5f;
        const float rx = (mx[0] - mn[0]) * 0.5f;
        const float ry = (mx[1] - mn[1]) * 0.5f;
        const float rz = (mx[2] - mn[2]) * 0.5f;
        hit.radius = std::sqrt(rx * rx + ry * ry + rz * rz);
        const float dx = hit.center[0] - eye[0];
        const float dy = hit.center[1] - eye[1];
        const float dz = hit.center[2] - eye[2];
        hit.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (hit.distance <= hit.radius + 30.0f) {
          camera_mesh_hits.push_back(std::move(hit));
        }
      }
      const auto wv = mul16(draw_world, view_arr);
      const auto wvp = mul16(wv, proj_arr);
      float min_x = std::numeric_limits<float>::infinity();
      float min_y = std::numeric_limits<float>::infinity();
      float max_x = -std::numeric_limits<float>::infinity();
      float max_y = -std::numeric_limits<float>::infinity();
      size_t in_front = 0;
      size_t on_screen = 0;
      const bool log_projected_vertices =
          env_mesh_filter_matches("GHOGX_LOG_PROJECTED_MESH_VERTICES", m.name);
      static std::unordered_map<std::string, size_t>
          logged_projected_vertex_mesh_samples;
      const size_t projected_vertex_sample =
          log_projected_vertices
              ? ++logged_projected_vertex_mesh_samples[m.name]
              : 0;
      const bool emit_projected_vertices =
          log_projected_vertices &&
          should_log_mesh_anim_sample(projected_vertex_sample);
      size_t projected_vertex_index = 0;
      for (const auto& v : m.verts) {
        const float x =
            v.px * wvp[0] + v.py * wvp[4] + v.pz * wvp[8] + wvp[12];
        const float y =
            v.px * wvp[1] + v.py * wvp[5] + v.pz * wvp[9] + wvp[13];
        const float w =
            v.px * wvp[3] + v.py * wvp[7] + v.pz * wvp[11] + wvp[15];
        if (w <= 0.001f) {
          ++projected_vertex_index;
          continue;
        }
        ++in_front;
        const float nx = x / w;
        const float ny = y / w;
        if (emit_projected_vertices) {
          std::fprintf(
              stderr,
              "[milo_scene] projected_vertex mesh=%s sample=%zu index=%zu "
              "local=(%.6f %.6f %.6f) uv=(%.6f %.6f) "
              "ndc=(%.6f %.6f) w=%.6f\n",
              m.name.c_str(), projected_vertex_sample,
              projected_vertex_index, v.px, v.py, v.pz, v.u, v.v, nx, ny,
              w);
        }
        ++projected_vertex_index;
        min_x = std::min(min_x, nx);
        max_x = std::max(max_x, nx);
        min_y = std::min(min_y, ny);
        max_y = std::max(max_y, ny);
        if (nx >= -1.0f && nx <= 1.0f && ny >= -1.0f && ny <= 1.0f) {
          ++on_screen;
        }
      }
      if (in_front > 0 && std::isfinite(min_x) && std::isfinite(min_y) &&
          std::isfinite(max_x) && std::isfinite(max_y)) {
        const float clipped_min_x = std::max(min_x, -1.0f);
        const float clipped_max_x = std::min(max_x, 1.0f);
        const float clipped_min_y = std::max(min_y, -1.0f);
        const float clipped_max_y = std::min(max_y, 1.0f);
        const float clipped_w =
            std::max(0.0f, clipped_max_x - clipped_min_x);
        const float clipped_h =
            std::max(0.0f, clipped_max_y - clipped_min_y);
        const float clipped_area = clipped_w * clipped_h;
        if (debug_camera_meshes || clipped_area > 0.0001f || on_screen > 0) {
          ProjectedCameraMeshHit hit;
          hit.name = m.name;
          hit.material = m.material;
          hit.min_x = min_x;
          hit.min_y = min_y;
          hit.max_x = max_x;
          hit.max_y = max_y;
          hit.clipped_area = clipped_area;
          hit.in_front = in_front;
          hit.on_screen = on_screen;
          hit.verts = m.verts.size();
          const float dx = draw_world[12] - eye[0];
          const float dy = draw_world[13] - eye[1];
          const float dz = draw_world[14] - eye[2];
          hit.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
          if (env_mesh_filter_matches("GHOGX_LOG_PROJECTED_MESH", m.name)) {
            static std::unordered_map<std::string, size_t>
                logged_projected_mesh_samples;
            const size_t sample = ++logged_projected_mesh_samples[m.name];
            if (should_log_mesh_anim_sample(sample)) {
              std::fprintf(
                  stderr,
                  "[milo_scene] projected_mesh mesh=%s material=%s sample=%zu "
                  "area=%.6f verts=%zu/%zu on_screen=%zu "
                  "ndc=(%.6f %.6f)..(%.6f %.6f) distance=%.6f\n",
                  hit.name.c_str(), hit.material.c_str(), sample,
                  hit.clipped_area, hit.in_front, hit.verts, hit.on_screen,
                  hit.min_x, hit.min_y, hit.max_x, hit.max_y, hit.distance);
            }
          }
          projected_camera_mesh_hits.push_back(std::move(hit));
        }
      }
    }
    if (would_reach_draw || debug_source_highlighted) {
      draw_mesh_with_world(m, draw_world, nullptr, nullptr,
                           debug_source_highlighted && !would_reach_draw);
    }
  }
  if (debug_venue) {
    debug_venue->pick =
        venue_picks.empty() ? DebugVenuePick{} : venue_picks.front().best;
    if (!debug_venue->pick.hit) {
      for (const auto& venue_pick : venue_picks) {
        if (venue_pick.source_mode && venue_pick.best.hit) {
          debug_venue->pick = venue_pick.best;
          break;
        }
      }
    }
    const bool debug_pick_highlight =
        debug_venue->highlight_enabled &&
        !env_enabled("GHOGX_DISABLE_VENUE_PICK_HIGHLIGHT");
    debug_venue->highlight_mesh =
        debug_pick_highlight && debug_venue->pick.hit ? debug_venue->pick.mesh
                                                      : std::string{};
    debug_venue->highlight_source_only =
        debug_pick_highlight && debug_venue->pick.hit &&
        !debug_venue->pick.would_draw;
    update_debug_venue_title(win_, *debug_venue);
    log_debug_pick_grid(*debug_venue, venue_picks);
  }
  disable_authored_fog();
  disable_authored_lights();

  if (!scene_.particles.empty()) {
    for (const auto& particle : scene_.particles) {
      draw_particle_system(particle);
    }
  }

  if (debug_camera_meshes && !camera_mesh_hits.empty()) {
    static std::unordered_set<std::string> logged_eyes;
    char key[128];
    std::snprintf(key, sizeof(key), "%.0f:%.0f:%.0f", eye[0], eye[1], eye[2]);
    if (logged_eyes.insert(key).second) {
      std::sort(camera_mesh_hits.begin(), camera_mesh_hits.end(),
                [](const auto& a, const auto& b) {
                  return (a.distance - a.radius) < (b.distance - b.radius);
                });
      const size_t limit = std::min<size_t>(camera_mesh_hits.size(), 24);
      std::fprintf(stderr,
                   "[milo_scene] camera mesh proximity eye=(%.2f %.2f %.2f) hits=%zu\n",
                   eye[0], eye[1], eye[2], camera_mesh_hits.size());
      for (size_t i = 0; i < limit; ++i) {
        const auto& h = camera_mesh_hits[i];
        std::fprintf(
            stderr,
            "[milo_scene]   near[%zu] mesh=%s material=%s center=(%.2f %.2f %.2f) radius=%.2f dist=%.2f margin=%.2f\n",
            i, h.name.c_str(), h.material.c_str(), h.center[0], h.center[1],
            h.center[2], h.radius, h.distance, h.distance - h.radius);
      }
    }
  }
  if (debug_camera_meshes && !projected_camera_mesh_hits.empty()) {
    static std::unordered_set<std::string> logged_projected_eyes;
    char key[128];
    std::snprintf(key, sizeof(key), "%.0f:%.0f:%.0f", eye[0], eye[1], eye[2]);
    if (logged_projected_eyes.insert(key).second) {
      std::sort(projected_camera_mesh_hits.begin(),
                projected_camera_mesh_hits.end(),
                [](const auto& a, const auto& b) {
                  if (a.clipped_area != b.clipped_area) {
                    return a.clipped_area > b.clipped_area;
                  }
                  return a.distance < b.distance;
                });
      const size_t limit =
          std::min<size_t>(projected_camera_mesh_hits.size(), 32);
      std::fprintf(stderr,
                   "[milo_scene] camera mesh projection eye=(%.2f %.2f %.2f) hits=%zu\n",
                   eye[0], eye[1], eye[2],
                   projected_camera_mesh_hits.size());
      for (size_t i = 0; i < limit; ++i) {
        const auto& h = projected_camera_mesh_hits[i];
        std::fprintf(
            stderr,
            "[milo_scene]   projected[%zu] mesh=%s material=%s area=%.4f verts=%zu/%zu on_screen=%zu ndc=(%.2f %.2f)..(%.2f %.2f) dist=%.2f\n",
            i, h.name.c_str(), h.material.c_str(), h.clipped_area,
            h.in_front, h.verts, h.on_screen, h.min_x, h.min_y, h.max_x,
            h.max_y, h.distance);
      }
    }
  }

  if (draw_spotlight_instances) {
    for (const auto& spot : scene_.spotlights) {
      const auto active_it = active_spotlights_.find(spot.name);
      if (active_spotlight_filter_ && active_it == active_spotlights_.end()) {
        continue;
      }
      if (env_enabled("GHOGX_LOG_SPOTLIGHT_AUTHORING")) {
        static std::unordered_set<std::string> logged_spotlight_authoring;
        const std::string key = spot.name + "|" + spot.group + "|" +
                                spot.target + "|" + spot.material;
        if (logged_spotlight_authoring.insert(key).second) {
          std::fprintf(
              stderr,
              "[milo_scene] Spotlight authoring spot=%s group=%s target=%s "
              "beam={cone=%d length=%.3f bottom=%.3f top=%.3f "
              "top_side=%.3f bottom_side=%.3f bottom_border=%.3f "
              "offset=%.3f angle=(%.3f %.3f) mat=%s} "
              "floor={scale=%.3f height=%.3f mat=%s} "
              "flare={mat=%s size=(%.3f %.3f) range=(%.3f %.3f) "
              "steps=%d offset=%.3f enabled=%d visibility=%d} "
              "lens={mat=%s size=%.3f offset=%.3f}\n",
              spot.name.c_str(), spot.group.c_str(), spot.target.c_str(),
              spot.beam_is_cone ? 1 : 0, spot.beam_length,
              spot.beam_bottom_radius, spot.beam_top_radius,
              spot.beam_top_side_border, spot.beam_bottom_side_border,
              spot.beam_bottom_border, spot.beam_offset,
              spot.beam_target_offset[0], spot.beam_target_offset[1],
              spot.material.c_str(), spot.spot_scale, spot.spot_height,
              spot.disc_material.c_str(), spot.flare_material.c_str(),
              spot.flare_size[0], spot.flare_size[1], spot.flare_range[0],
              spot.flare_range[1], spot.flare_steps, spot.flare_offset,
              spot.flare_enabled ? 1 : 0,
              spot.flare_visibility_test ? 1 : 0,
              spot.lens_material.c_str(), spot.lens_size,
              spot.lens_offset);
        }
      }
      if (!spot.has_transform) continue;
      const auto group_it = groups_by_name_.find(spot.group);
      const milo_scene::GroupObj* group =
          group_it == groups_by_name_.end() ? nullptr : group_it->second;
      auto spot_world = xfm_to_mat4(spot.world_stored);
      const bool has_preset_target =
          active_it != active_spotlights_.end() &&
          active_it->second.has_target_override;
      const std::string target_ref = has_preset_target
                                         ? active_it->second.target_mesh
                                         : spot.target;
      const milo_scene::MeshObj* target_mesh = nullptr;
      if (!target_ref.empty()) {
        if (const auto target_it = meshes_by_name_.find(target_ref);
            target_it != meshes_by_name_.end() && target_it->second) {
          target_mesh = target_it->second;
        }
      }
      if (!target_mesh && active_it != active_spotlights_.end() &&
          active_it->second.has_rotation &&
          spot.animate_orientation_from_preset) {
        float preset_rotation[3][3];
        quat_xyzw_to_row_rot(active_it->second.rotation_xyzw,
                             preset_rotation);
        for (int r = 0; r < 3; ++r) {
          for (int c = 0; c < 3; ++c) {
            spot_world[r * 4 + c] = preset_rotation[r][c];
          }
        }
      }
      if (target_mesh &&
          !env_enabled("GHOGX_DISABLE_SPOTLIGHT_TARGET_AIM")) {
          const auto target_world = scene_.world_matrix(*target_mesh);
          const float origin[3] = {spot_world[12], spot_world[13],
                                   spot_world[14]};
          const float target_pos[3] = {target_world[12], target_world[13],
                                       target_world[14]};
          float forward[3] = {target_pos[0] - origin[0],
                              target_pos[1] - origin[1],
                              target_pos[2] - origin[2]};
          float len = std::sqrt(forward[0] * forward[0] +
                                forward[1] * forward[1] +
                                forward[2] * forward[2]);
          if (len > 0.001f) {
            forward[0] /= len;
            forward[1] /= len;
            forward[2] /= len;
            float aim_up[3] = {0.0f, 0.0f, 1.0f};
            if (std::fabs(forward[2]) > 0.96f) {
              aim_up[0] = 1.0f;
              aim_up[1] = 0.0f;
              aim_up[2] = 0.0f;
            }
            float right[3] = {
                aim_up[1] * forward[2] - aim_up[2] * forward[1],
                aim_up[2] * forward[0] - aim_up[0] * forward[2],
                aim_up[0] * forward[1] - aim_up[1] * forward[0]};
            float rlen = std::sqrt(right[0] * right[0] +
                                   right[1] * right[1] +
                                   right[2] * right[2]);
            if (rlen > 0.001f) {
              right[0] /= rlen;
              right[1] /= rlen;
              right[2] /= rlen;
              aim_up[0] = forward[1] * right[2] - forward[2] * right[1];
              aim_up[1] = forward[2] * right[0] - forward[0] * right[2];
              aim_up[2] = forward[0] * right[1] - forward[1] * right[0];
              spot_world[0] = right[0];
              spot_world[1] = right[1];
              spot_world[2] = right[2];
              spot_world[4] = forward[0];
              spot_world[5] = forward[1];
              spot_world[6] = forward[2];
              spot_world[8] = aim_up[0];
              spot_world[9] = aim_up[1];
              spot_world[10] = aim_up[2];
            }
          }
      }
      auto light_can_world = spot_world;
      light_can_world[12] += spot_world[4] * spot.light_can_offset;
      light_can_world[13] += spot_world[5] * spot.light_can_offset;
      light_can_world[14] += spot_world[6] * spot.light_can_offset;
      auto beam_world = spot_world;
      // Spotlight::UpdateTransforms sets BeamDef::mBeam local position to
      // (0, offset, 0). Keep that authored translation on the unrotated
      // spotlight trajectory; it is independent of the shaft mesh's shape.
      beam_world[12] += spot_world[4] * spot.beam_offset;
      beam_world[13] += spot_world[5] * spot.beam_offset;
      beam_world[14] += spot_world[6] * spot.beam_offset;
      const auto spot_draw_world = mul16(spot_world, world_transform_);
      const auto beam_draw_world = mul16(beam_world, world_transform_);
      const auto light_can_draw_world =
          mul16(light_can_world, world_transform_);
      if (additive_blend_) {
        if (!active_spotlight_filter_ || active_it == active_spotlights_.end())
          continue;
        DWORD prev_z_enable = TRUE;
        if (env_enabled("GHOGX_DISABLE_SPOTLIGHT_DEPTH")) {
          dev_->GetRenderState(D3DRS_ZENABLE, &prev_z_enable);
          dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
        }
        bool drew_circle = false;
        if (!spot.circle_mesh.empty()) {
          const auto circle_it = meshes_by_name_.find(spot.circle_mesh);
          if (circle_it != meshes_by_name_.end() && circle_it->second) {
            const auto& m = *circle_it->second;
            const std::string* mat =
                spot.circle_material.empty() ? nullptr : &spot.circle_material;
            draw_mesh_with_world(m, spot_draw_world, mat, &active_it->second);
            drew_circle = true;
          }
        }
        if (group) {
          for (const auto& child : group->children) {
            if (child == spot.circle_mesh && drew_circle) continue;
            if (hidden_meshes_.find(child) != hidden_meshes_.end()) continue;
            const auto child_it = meshes_by_name_.find(child);
            if (child_it == meshes_by_name_.end() || !child_it->second)
              continue;
            const auto& m = *child_it->second;
            draw_mesh_with_world(m, light_can_draw_world, nullptr,
                                 &active_it->second);
            if (env_enabled("GHOGX_LOG_SPOTLIGHT_MESHES")) {
              std::fprintf(
                  stderr,
                  "[milo_scene] spotlight light-can spot=%s mesh=%s "
                  "material=%s world_pos=(%.2f %.2f %.2f)\n",
                  spot.name.c_str(), m.name.c_str(), m.material.c_str(),
                  light_can_draw_world[12], light_can_draw_world[13],
                  light_can_draw_world[14]);
            }
          }
        }
        for (const auto& child : spot.instance_meshes) {
          if (child == spot.target) continue;
          if (child == spot.circle_mesh && drew_circle) continue;
          if (group && std::find(group->children.begin(), group->children.end(),
                                 child) != group->children.end()) {
            continue;
          }
          if (hidden_meshes_.find(child) != hidden_meshes_.end()) continue;
          const auto child_it = meshes_by_name_.find(child);
          if (child_it == meshes_by_name_.end() || !child_it->second) continue;
          const auto& m = *child_it->second;
          draw_mesh_with_world(m, spot_draw_world, nullptr,
                               &active_it->second);
          if (env_enabled("GHOGX_LOG_SPOTLIGHT_MESHES")) {
            std::fprintf(stderr,
                         "[milo_scene] spotlight direct mesh spot=%s mesh=%s material=%s world_pos=(%.2f %.2f %.2f)\n",
                         spot.name.c_str(), m.name.c_str(),
                         m.material.c_str(), spot_draw_world[12],
                         spot_draw_world[13], spot_draw_world[14]);
          }
        }

        if (!env_enabled("GHOGX_DISABLE_GENERATED_SPOTLIGHT_ACCESSORIES")) {
          // Spotlight::Generate builds the shaft at load time, while
          // SpotlightDrawer::DrawWorld submits beam, floor spot, lens, and
          // flare accessories only for the active preset entries.
          if (const auto beam_it = generated_spotlight_beams_.find(spot.name);
              beam_it != generated_spotlight_beams_.end()) {
            draw_mesh_with_world(beam_it->second, beam_draw_world, nullptr,
                                 &active_it->second);
          }

          if (target_mesh && !spot.disc_material.empty() &&
              spot.spot_scale > 0.0001f) {
            const auto target_world = scene_.world_matrix(*target_mesh);
            const float forward[3] = {spot_world[4], spot_world[5],
                                      spot_world[6]};
            if (std::fabs(forward[2]) > 0.001f) {
              const float plane_z = target_world[14] + spot.spot_height;
              const float t = (plane_z - spot_world[14]) / forward[2];
              if (std::isfinite(t) && t >= 0.0f) {
                float along_x = forward[0];
                float along_y = forward[1];
                const float along_len =
                    std::sqrt(along_x * along_x + along_y * along_y);
                if (along_len > 0.001f) {
                  along_x /= along_len;
                  along_y /= along_len;
                } else {
                  along_x = 0.0f;
                  along_y = 1.0f;
                }
                const float major =
                    spot.spot_scale /
                    std::max(0.08f, std::fabs(forward[2]));
                auto floor_world = identity16();
                floor_world[0] = -along_y * spot.spot_scale;
                floor_world[1] = along_x * spot.spot_scale;
                floor_world[2] = 0.0f;
                floor_world[4] = 0.0f;
                floor_world[5] = 0.0f;
                floor_world[6] = 1.0f;
                floor_world[8] = along_x * major;
                floor_world[9] = along_y * major;
                floor_world[10] = 0.0f;
                floor_world[12] = spot_world[12] + forward[0] * t;
                floor_world[13] = spot_world[13] + forward[1] * t;
                floor_world[14] = plane_z;
                floor_world = mul16(floor_world, world_transform_);
                draw_mesh_with_world(generated_spotlight_disc_, floor_world,
                                     &spot.disc_material,
                                     &active_it->second);
              }
            }
          }

          if (!spot.lens_material.empty() && spot.lens_size > 0.0001f) {
            auto lens_world = identity16();
            // Spotlight::UpdateTransforms constructs the lens basis as
            // (-size,0,0), (0,0,size), (0,size,0), then multiplies it by the
            // spotlight's world rotation.
            for (int c = 0; c < 3; ++c) {
              lens_world[c] = -spot_world[c] * spot.lens_size;
              lens_world[4 + c] = spot_world[8 + c] * spot.lens_size;
              lens_world[8 + c] = spot_world[4 + c] * spot.lens_size;
            }
            lens_world[12] += spot_world[4] * spot.lens_offset;
            lens_world[13] += spot_world[5] * spot.lens_offset;
            lens_world[14] += spot_world[6] * spot.lens_offset;
            lens_world[12] += spot_world[12];
            lens_world[13] += spot_world[13];
            lens_world[14] += spot_world[14];
            lens_world = mul16(lens_world, world_transform_);
            draw_mesh_with_world(generated_spotlight_disc_, lens_world,
                                 &spot.lens_material, &active_it->second);
          }

          const bool flare_enabled =
              active_it->second.has_flare_enabled
                  ? active_it->second.flare_enabled
                  : spot.flare_enabled;
          if (flare_enabled && !spot.flare_material.empty() &&
              std::max(spot.flare_size[0], spot.flare_size[1]) > 0.0001f) {
            float forward[3] = {spot_draw_world[4], spot_draw_world[5],
                                spot_draw_world[6]};
            const float forward_len =
                std::sqrt(forward[0] * forward[0] +
                          forward[1] * forward[1] +
                          forward[2] * forward[2]);
            if (forward_len > 0.001f) {
              for (float& component : forward) component /= forward_len;
            }
            const float center[3] = {
                spot_draw_world[12] + forward[0] * spot.flare_offset,
                spot_draw_world[13] + forward[1] * spot.flare_offset,
                spot_draw_world[14] + forward[2] * spot.flare_offset};
            const float dx = center[0] - eye[0];
            const float dy = center[1] - eye[1];
            const float dz = center[2] - eye[2];
            const float distance = std::max(
                0.001f, std::sqrt(dx * dx + dy * dy + dz * dz));
            float size = spot.flare_size[0];
            if (spot.flare_range[1] > spot.flare_range[0] + 0.001f) {
              const float range_t = std::clamp(
                  (distance - spot.flare_range[0]) /
                      (spot.flare_range[1] - spot.flare_range[0]),
                  0.0f, 1.0f);
              size += (spot.flare_size[1] - spot.flare_size[0]) * range_t;
            }
            const float half_size =
                std::max(0.0001f, size * distance *
                                      std::tan(cam_.fov * 0.5f));
            auto flare_world = identity16();
            flare_world[0] = view.m[0][0] * half_size;
            flare_world[1] = view.m[1][0] * half_size;
            flare_world[2] = view.m[2][0] * half_size;
            flare_world[8] = view.m[0][1] * half_size;
            flare_world[9] = view.m[1][1] * half_size;
            flare_world[10] = view.m[2][1] * half_size;
            flare_world[12] = center[0];
            flare_world[13] = center[1];
            flare_world[14] = center[2];
            draw_mesh_with_world(generated_spotlight_flare_, flare_world,
                                 &spot.flare_material, &active_it->second);
          }
        }
        if (env_enabled("GHOGX_LOG_ALPHA_MESHES")) {
          static std::unordered_set<std::string> logged_spots;
          if (logged_spots.insert(spot.name).second) {
            std::fprintf(stderr,
                         "[milo_scene] active spotlight name=%s group=%s children=%zu direct=%zu circle=%s target=%s intensity=%.3f color=(%.3f %.3f %.3f)\n",
                         spot.name.c_str(), spot.group.c_str(),
                         group ? group->children.size() : 0u,
                         spot.instance_meshes.size(),
                         spot.circle_mesh.c_str(),
                         active_it->second.target_mesh.c_str(),
                         active_it->second.intensity, active_it->second.r,
                         active_it->second.g, active_it->second.b);
          }
        }
        if (env_enabled("GHOGX_DISABLE_SPOTLIGHT_DEPTH")) {
          dev_->SetRenderState(D3DRS_ZENABLE, prev_z_enable);
        }
        continue;
      }
      if (group) {
        for (const auto& child : group->children) {
          if (hidden_meshes_.find(child) != hidden_meshes_.end()) continue;
          const auto child_it = meshes_by_name_.find(child);
          if (child_it == meshes_by_name_.end() || !child_it->second) continue;
          draw_mesh_with_world(*child_it->second, light_can_draw_world);
        }
      }
    }
  }

    for (const auto& flare : scene_.flares) {
      if (!flare.decoded || !flare.showing || flare.material.empty() ||
          hidden_meshes_.find(flare.name) != hidden_meshes_.end()) {
        continue;
      }
      std::array<float, 16> flare_source_world{};
      const auto parent_override =
          transform_parent_overrides_.find(flare.name);
      const bool has_runtime_parent =
          parent_override != transform_parent_overrides_.end() ||
          (!flare.parent.empty() && flare.parent != flare.name);
      if (has_runtime_parent) {
        if (!resolve_transform_parent_world(flare.name, flare_source_world)) {
          continue;
        }
      } else {
        // RndDrawable submits its Transformable's cached WorldXfm. GH1
        // revision-8 root transforms commonly serialize an identity local
        // matrix, a self/null parent reference, and the authored placement in
        // the stored world matrix. With no reconstructed/runtime parent, that
        // stored matrix is the native drawable transform.
        flare_source_world = xfm_to_mat4(flare.world_stored);
      }
      const auto flare_draw_world =
          mul16(flare_source_world, world_transform_);
      float forward[3] = {flare_draw_world[4], flare_draw_world[5],
                          flare_draw_world[6]};
      const float forward_len =
          std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] +
                    forward[2] * forward[2]);
      if (forward_len > 0.001f) {
        for (float& component : forward) component /= forward_len;
      }
      const float center[3] = {
          flare_draw_world[12] + forward[0] * flare.offset,
          flare_draw_world[13] + forward[1] * flare.offset,
          flare_draw_world[14] + forward[2] * flare.offset};
      const float dx = center[0] - eye[0];
      const float dy = center[1] - eye[1];
      const float dz = center[2] - eye[2];
      const float distance =
          std::max(0.001f, std::sqrt(dx * dx + dy * dy + dz * dz));
      auto& visibility = flare_visibility_[flare.name];
      if (flare.point_test) {
        if (!visibility.query) {
          dev_->CreateQuery(D3DQUERYTYPE_OCCLUSION, &visibility.query);
        }
        if (visibility.query && visibility.query_issued) {
          DWORD visible_pixels = 0;
          if (visibility.query->GetData(&visible_pixels,
                                        sizeof(visible_pixels), 0) == S_OK) {
            visibility.target_visible = visible_pixels != 0;
            visibility.query_issued = false;
            if (env_enabled("GHOGX_DEBUG_VENUE_FILTERS")) {
              static std::unordered_set<std::string> logged_flare_queries;
              if (logged_flare_queries.insert(flare.name).second) {
                std::fprintf(
                    stderr,
                    "[milo_scene] Flare point_test name=%s visible_pixels=%lu visible=%d center=(%.3f %.3f %.3f) distance=%.3f local=(%.3f %.3f %.3f) stored=(%.3f %.3f %.3f) parent=%s\n",
                    flare.name.c_str(),
                    static_cast<unsigned long>(visible_pixels),
                    visibility.target_visible ? 1 : 0, center[0], center[1],
                    center[2], distance, flare.local.pos[0],
                    flare.local.pos[1], flare.local.pos[2],
                    flare.world_stored.pos[0], flare.world_stored.pos[1],
                    flare.world_stored.pos[2],
                    flare.parent.c_str());
              }
            }
          }
        }
        if (visibility.query && !visibility.query_issued) {
          DWORD color_write = 0;
          DWORD z_enable = 0;
          DWORD z_write = 0;
          DWORD alpha_blend = 0;
          DWORD alpha_test = 0;
          DWORD lighting = 0;
          DWORD cull_mode = 0;
          DWORD old_fvf = kFVF;
          dev_->GetRenderState(D3DRS_COLORWRITEENABLE, &color_write);
          dev_->GetRenderState(D3DRS_ZENABLE, &z_enable);
          dev_->GetRenderState(D3DRS_ZWRITEENABLE, &z_write);
          dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &alpha_blend);
          dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &alpha_test);
          dev_->GetRenderState(D3DRS_LIGHTING, &lighting);
          dev_->GetRenderState(D3DRS_CULLMODE, &cull_mode);
          dev_->GetFVF(&old_fvf);
          D3DMATRIX point_world;
          const auto point_identity = identity16();
          std::memcpy(&point_world, point_identity.data(), 64);
          dev_->SetTransform(D3DTS_WORLD, &point_world);
          dev_->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
          dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
          dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
          dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
          dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
          dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
          dev_->SetTexture(0, nullptr);
          dev_->SetFVF(kFVF);
          // Native RndFlare point-test visibility is a raster visibility test
          // at the transformed flare origin. D3D9 point primitives are
          // implementation-dependent and can produce no samples; submit a
          // sub-pixel camera-facing quad so the occlusion query measures that
          // same screen point without turning the test into flare geometry.
          const float query_half_size =
              std::max(0.02f, distance * std::tan(cam_.fov * 0.5f) * 0.002f);
          const float right[3] = {view.m[0][0] * query_half_size,
                                  view.m[1][0] * query_half_size,
                                  view.m[2][0] * query_half_size};
          const float camera_up[3] = {view.m[0][1] * query_half_size,
                                      view.m[1][1] * query_half_size,
                                      view.m[2][1] * query_half_size};
          const auto query_vertex = [&](float right_sign, float up_sign) {
            return SVtx{center[0] + right[0] * right_sign +
                            camera_up[0] * up_sign,
                        center[1] + right[1] * right_sign +
                            camera_up[1] * up_sign,
                        center[2] + right[2] * right_sign +
                            camera_up[2] * up_sign,
                        0.0f,
                        0.0f,
                        1.0f,
                        0xffffffffu,
                        0.0f,
                        0.0f};
          };
          const SVtx query_quad[6] = {
              query_vertex(-1.0f, -1.0f), query_vertex(-1.0f, 1.0f),
              query_vertex(1.0f, 1.0f),   query_vertex(-1.0f, -1.0f),
              query_vertex(1.0f, 1.0f),   query_vertex(1.0f, -1.0f)};
          visibility.query->Issue(D3DISSUE_BEGIN);
          dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, query_quad,
                                sizeof(SVtx));
          visibility.query->Issue(D3DISSUE_END);
          visibility.query_issued = true;
          dev_->SetRenderState(D3DRS_COLORWRITEENABLE, color_write);
          dev_->SetRenderState(D3DRS_ZENABLE, z_enable);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, z_write);
          dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, alpha_blend);
          dev_->SetRenderState(D3DRS_ALPHATESTENABLE, alpha_test);
          dev_->SetRenderState(D3DRS_LIGHTING, lighting);
          dev_->SetRenderState(D3DRS_CULLMODE, cull_mode);
          dev_->SetFVF(old_fvf);
        }
      } else {
        visibility.target_visible = true;
      }
      const auto step_it = flare_steps_.find(flare.name);
      const int steps =
          step_it != flare_steps_.end() ? step_it->second : flare.steps;
      if (steps <= 0) {
        visibility.fade = visibility.target_visible ? 1.0f : 0.0f;
      } else {
        const float delta = 1.0f / static_cast<float>(steps);
        visibility.fade = std::clamp(
            visibility.fade +
                (visibility.target_visible ? delta : -delta),
            0.0f, 1.0f);
      }
      if (visibility.fade <= 0.0001f) continue;
      float size = flare.sizes[0];
      if (flare.range[1] > flare.range[0] + 0.001f) {
        const float range_t = std::clamp(
            (distance - flare.range[0]) /
                (flare.range[1] - flare.range[0]),
            0.0f, 1.0f);
        size += (flare.sizes[1] - flare.sizes[0]) * range_t;
      }
      const float half_size =
          std::max(0.0001f,
                   size * distance * std::tan(cam_.fov * 0.5f));
      auto billboard_world = identity16();
      billboard_world[0] = view.m[0][0] * half_size;
      billboard_world[1] = view.m[1][0] * half_size;
      billboard_world[2] = view.m[2][0] * half_size;
      billboard_world[8] = view.m[0][1] * half_size;
      billboard_world[9] = view.m[1][1] * half_size;
      billboard_world[10] = view.m[2][1] * half_size;
      billboard_world[12] = center[0];
      billboard_world[13] = center[1];
      billboard_world[14] = center[2];
      SpotlightState flare_state;
      flare_state.intensity = visibility.fade;
      draw_mesh_with_world(generated_spotlight_flare_, billboard_world,
                           &flare.material, &flare_state);
      if (env_enabled("GHOGX_DEBUG_VENUE_FILTERS")) {
        static std::unordered_set<std::string> logged_flares;
        if (logged_flares.insert(flare.name).second) {
          std::fprintf(
              stderr,
              "[milo_scene] Flare draw name=%s source_order=1 material=%s sizes=(%.3f %.3f) range=(%.3f %.3f) steps=%d point_test=%d visible=%d fade=%.3f\n",
              flare.name.c_str(), flare.material.c_str(), flare.sizes[0],
              flare.sizes[1], flare.range[0], flare.range[1], steps,
              flare.point_test ? 1 : 0,
              visibility.target_visible ? 1 : 0, visibility.fade);
        }
      }
    }

    disable_authored_fog();
    disable_authored_lights();
  }

  dev_->SetTexture(0, nullptr);

  const auto target_has_text_transform_sample = [&](const std::string& target) {
    return mesh_transform_offsets_.find(target) !=
               mesh_transform_offsets_.end() ||
           active_mesh_anims_.find(target) != active_mesh_anims_.end();
  };
  const auto apply_text_transform_sample = [&](std::array<float, 16>& local,
                                                const std::string& target) {
    if (const auto offset = mesh_transform_offsets_.find(target);
        offset != mesh_transform_offsets_.end()) {
      apply_mesh_transform_sample(local, offset->second);
    }
    if (const auto active = active_mesh_anims_.find(target);
        active != active_mesh_anims_.end()) {
      const float frame = active->second.elapsed * active->second.frames_per_second;
      apply_mesh_transform_sample(
          local, sample_transform_anim(active->second.anim, frame));
    }
  };
  const auto animated_text_span_world = [&](const TextTransformSpan& span) {
    const auto& span_bind_world = span.bind_world;
    auto parent_for = [&](const std::string& name) {
      if (name == span.name) return span.parent;
      SceneNodeXfm node;
      return scene_node_xfm_for(scene_, name, node) ? node.parent : std::string{};
    };
    auto local_for = [&](const std::string& name,
                         std::array<float, 16>& local) {
      if (name == span.name) {
        local = span.local;
        return true;
      }
      SceneNodeXfm node;
      if (!scene_node_xfm_for(scene_, name, node)) return false;
      local = node.local;
      return true;
    };
    auto base_world_for = [&](const std::string& name,
                              std::array<float, 16>& world) {
      if (name == span.name) {
        world = span_bind_world;
        return true;
      }
      return scene_node_world_for(scene_, name, world);
    };

    std::vector<std::string> ancestors;
    for (std::string parent = span.parent; !parent.empty();) {
      ancestors.push_back(parent);
      const std::string next = parent_for(parent);
      if (next.empty() || next == parent) break;
      parent = next;
      if (ancestors.size() >= 64) break;
    }
    std::reverse(ancestors.begin(), ancestors.end());
    const bool animated = target_has_text_transform_sample(span.name) ||
                          std::any_of(ancestors.begin(), ancestors.end(),
                                      target_has_text_transform_sample);
    if (!animated) return span_bind_world;

    std::vector<std::string> chain = ancestors;
    chain.push_back(span.name);
    std::array<float, 16> anchor_base{};
    std::array<float, 16> anchor_current{};
    bool have_anchor = false;
    std::array<float, 16> composed = span_bind_world;
    for (const std::string& target : chain) {
      const bool is_label = target == span.name;
      const bool sampled = target_has_text_transform_sample(target);
      if (!sampled && !is_label) continue;
      std::array<float, 16> base_world{};
      if (!base_world_for(target, base_world)) return span_bind_world;
      std::array<float, 16> current_world = base_world;
      if (have_anchor) {
        current_world =
            mul16(mul16(base_world, affine_inverse16(anchor_base)),
                  anchor_current);
      }
      if (sampled) {
        std::array<float, 16> base_local{};
        if (!local_for(target, base_local)) return span_bind_world;
        std::array<float, 16> sampled_local = base_local;
        apply_text_transform_sample(sampled_local, target);
        current_world =
            mul16(mul16(sampled_local, affine_inverse16(base_local)),
                  current_world);
        anchor_base = base_world;
        anchor_current = current_world;
        have_anchor = true;
      }
      if (is_label) composed = current_world;
    }
    return composed;
  };

  const auto draw_text_batches = [&](size_t begin, size_t end) {
    if (begin >= end || begin >= text_.size()) return;
    end = std::min(end, text_.size());
    D3DMATRIX wi;
    std::memcpy(&wi, world_transform_.data(), 64);
    dev_->SetTransform(D3DTS_WORLD, &wi);

    dev_->SetFVF(kFVF);
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev_->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    const DWORD text_filter =
        env_enabled("GHOGX_MENU_TEXT_POINT") ? D3DTEXF_POINT : D3DTEXF_LINEAR;
    dev_->SetSamplerState(0, D3DSAMP_MINFILTER, text_filter);
    dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, text_filter);
    dev_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    for (size_t bi = begin; bi < end; ++bi) {
      if (bi >= text_tex_.size() || !text_tex_[bi] || text_[bi].size() < 3) continue;
      dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
      dev_->SetTexture(0, text_tex_[bi]);
      std::vector<TextVertex> draw_text = text_[bi];
      if (bi < text_transform_spans_.size()) {
        for (const TextTransformSpan& span : text_transform_spans_[bi]) {
          const size_t span_end = std::min(
              draw_text.size(), span.first_vertex + span.vertex_count);
          if (span.first_vertex >= span_end) continue;
          const auto current_world = animated_text_span_world(span);
          const auto& bind_world = span.bind_world;
          const auto delta =
              mul16(affine_inverse16(bind_world), current_world);
          for (size_t vi = span.first_vertex; vi < span_end; ++vi) {
            const auto point = transform_point16(
                delta, draw_text[vi].x, draw_text[vi].y, draw_text[vi].z);
            draw_text[vi].x = point[0];
            draw_text[vi].y = point[1];
            draw_text[vi].z = point[2];
          }
        }
      }
      std::vector<SVtx> tv;
      tv.reserve(draw_text.size());
      for (const auto& t : draw_text) {
        SVtx s;
        s.x = t.x; s.y = t.y; s.z = t.z;
        s.nx = 0; s.ny = 1; s.nz = 0;
        const uint32_t a = (t.argb >> 24) & 0xffu;
        const uint32_t r = (t.argb >> 16) & 0xffu;
        const uint32_t g = (t.argb >> 8) & 0xffu;
        const uint32_t b = t.argb & 0xffu;
        const auto scale_channel = [](uint32_t v, float scale) {
          return static_cast<uint32_t>(
              std::clamp(static_cast<float>(v) * scale, 0.0f, 255.0f) + 0.5f);
        };
        s.color =
            (scale_channel(a, global_alpha_) << 24) |
            (scale_channel(r, global_brightness_) << 16) |
            (scale_channel(g, global_brightness_) << 8) |
            scale_channel(b, global_brightness_);
        s.u = t.u; s.v = t.v;
        tv.push_back(s);
      }
      dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                            static_cast<UINT>(tv.size() / 3), tv.data(),
                            sizeof(SVtx));
    }
    dev_->SetTexture(0, nullptr);
  };

  const size_t text_split =
      std::min(post_text_mesh_text_split_, text_.size());
  if (draw_text) draw_text_batches(0, text_split);

  if (draw_text && !post_text_draw_meshes.empty()) {
    dev_->SetFVF(kFVF);
    dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    for (const auto& item : post_text_draw_meshes) {
      if (!item.mesh) continue;
      draw_mesh_with_world(*item.mesh, item.world);
    }
    disable_authored_fog();
    disable_authored_lights();
    dev_->SetTexture(0, nullptr);
  }

  // ---- Menu text overlay: world-space glyph quads from the font atlas. -------
  // Drawn after the 3-D scene, alpha-blended, no depth write, unlit (the glyph
  // colour comes straight from the per-vertex tint modulated by the atlas alpha).
  if (draw_text && !text_.empty()) {
    draw_text_batches(text_split, text_.size());
  }

  if (debug_venue) {
    if (debug_venue->axes_enabled) {
      draw_debug_pick_face_axis(dev_, win_->bb_width(), win_->bb_height(),
                                view, proj, debug_venue->pick);
    }
    draw_debug_crosshair(dev_, win_->bb_width(), win_->bb_height(),
                         debug_venue->crosshair_ndc_x,
                         debug_venue->crosshair_ndc_y,
                         debug_venue->pick.hit,
                         debug_venue->pick.would_draw);
  }

  dev_->EndScene();
}

}  // namespace ghogx::render
