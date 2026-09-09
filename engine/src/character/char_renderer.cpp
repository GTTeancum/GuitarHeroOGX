// engine/src/character/char_renderer.cpp — see char_renderer.h.

#include "character/char_renderer.h"
#include "render/milo_scene_renderer.h"  // OrbitCamera
#include "render/window_d3d9.h"
#include "render/scene_d3d9.h"           // Mat4
#include "asset/milo_image.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>
#include <vector>

namespace ghogx::character {

using ghogx::render::Mat4;
using ghogx::render::OrbitCamera;
using ghogx::render::Window;

namespace {

struct SVtx {
  float x, y, z;
  float nx, ny, nz;
  D3DCOLOR color;
  float u, v;
};
constexpr DWORD kFVF =
    D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;

enum MiloBlend : uint8_t {
  kBlendDest = 0,
  kBlendSrc = 1,
  kBlendAdd = 2,
  kBlendSrcAlpha = 3,
  kBlendSrcAlphaAdd = 4,
  kBlendSubtract = 5,
  kBlendMultiply = 6,
};

struct BlendState {
  DWORD src = D3DBLEND_SRCALPHA;
  DWORD dest = D3DBLEND_INVSRCALPHA;
  DWORD op = D3DBLENDOP_ADD;
  bool additive = false;
};

BlendState character_blend_state_for(uint8_t blend) {
  switch (blend) {
    case kBlendDest:
      return {D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLENDOP_ADD, false};
    case kBlendSrc:
      return {D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    case kBlendAdd:
      return {D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kBlendSrcAlpha:
      return {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
              false};
    case kBlendSrcAlphaAdd:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kBlendSubtract:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT,
              true};
    case kBlendMultiply:
      return {D3DBLEND_DESTCOLOR, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    default:
      return {};
  }
}

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
  std::array<std::array<DWORD, 32>, 8> texture_stage_values{};
  std::array<std::array<bool, 32>, 8> texture_stage_valid{};
  std::array<IDirect3DBaseTexture9*, 8> textures{};
  std::array<bool, 8> textures_valid{};
};

// The venue renderer owns fixed-function light slots 0..3 for approximate
// directional lights. Character draws temporarily fit shader-domain colors to
// fixed-function D3DCOLORVALUE range, then restore the exact venue state before
// the next scene pass or performer selects its own Environ.
struct ScopedCharacterSceneLightRange {
  explicit ScopedCharacterSceneLightRange(IDirect3DDevice9* device,
                                           bool enabled)
      : dev(device) {
    if (!dev || !enabled) return;
    for (DWORD slot = 0; slot < original.size(); ++slot) {
      D3DLIGHT9 light{};
      if (FAILED(dev->GetLight(slot, &light))) continue;
      original[slot] = light;
      const auto rgb = source_character_fixed_function_light_rgb(
          light.Diffuse.r, light.Diffuse.g, light.Diffuse.b);
      if (rgb[0] == light.Diffuse.r && rgb[1] == light.Diffuse.g &&
          rgb[2] == light.Diffuse.b) {
        continue;
      }
      light.Diffuse.r = rgb[0];
      light.Diffuse.g = rgb[1];
      light.Diffuse.b = rgb[2];
      changed[slot] = SUCCEEDED(dev->SetLight(slot, &light));
    }
  }

  ~ScopedCharacterSceneLightRange() {
    if (!dev) return;
    for (DWORD slot = 0; slot < original.size(); ++slot) {
      if (changed[slot]) dev->SetLight(slot, &original[slot]);
    }
  }

  IDirect3DDevice9* dev = nullptr;
  std::array<D3DLIGHT9, 4> original{};
  std::array<bool, 4> changed{};
};

bool read_char_env_enabled(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return false;
  const bool enabled = value[0] && value[0] != '0';
  std::free(value);
  return enabled;
}

std::string read_char_env_string(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
  std::string result = value;
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value ? value : "";
#endif
}

struct CharEnvFrameCache {
  CharEnvFrameCache() {
    bool_values.reserve(32);
    string_values.reserve(12);
  }

  mutable std::unordered_map<std::string, bool> bool_values;
  mutable std::unordered_map<std::string, std::string> string_values;

  bool enabled(const char* name) const {
    const auto [it, inserted] =
        bool_values.emplace(name, false);
    if (inserted) it->second = read_char_env_enabled(name);
    return it->second;
  }

  const std::string& string_value(const char* name) const {
    const auto [it, inserted] =
        string_values.emplace(name, std::string{});
    if (inserted) it->second = read_char_env_string(name);
    return it->second;
  }
};

thread_local const CharEnvFrameCache* active_char_env_frame_cache = nullptr;

struct ScopedCharEnvFrameCache {
  explicit ScopedCharEnvFrameCache(const CharEnvFrameCache& cache)
      : previous(active_char_env_frame_cache) {
    active_char_env_frame_cache = &cache;
  }
  ~ScopedCharEnvFrameCache() {
    active_char_env_frame_cache = previous;
  }

  const CharEnvFrameCache* previous = nullptr;
};

bool char_env_enabled(const char* name) {
  if (active_char_env_frame_cache) {
    return active_char_env_frame_cache->enabled(name);
  }
  return read_char_env_enabled(name);
}

const std::string& empty_char_env_string() {
  static const std::string empty;
  return empty;
}

const std::string& char_env_string_ref(const char* name) {
  if (active_char_env_frame_cache) {
    return active_char_env_frame_cache->string_value(name);
  }
  thread_local std::string uncached_value;
  uncached_value = read_char_env_string(name);
  return uncached_value;
}

std::string char_env_string(const char* name) {
  return char_env_string_ref(name);
}

bool comma_spec_matches(const std::string& spec, const std::string& value) {
  if (spec.empty()) return false;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string needle = spec.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!needle.empty() && value.find(needle) != std::string::npos) return true;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

float char_env_float_or(const char* name, float fallback, float min_value,
                        float max_value) {
  const std::string& value = char_env_string_ref(name);
  if (value.empty()) return fallback;
  char* end = nullptr;
  const float parsed = std::strtof(value.c_str(), &end);
  if (end == value.c_str() || !std::isfinite(parsed)) return fallback;
  if (parsed < min_value || parsed > max_value) return fallback;
  return parsed;
}

bool source_material_alpha_state_enabled() {
  return !char_env_enabled("GHOGX_DISABLE_SOURCE_MAT_ALPHA_STATE");
}

bool source_material_zmode_depth_enabled() {
  return !char_env_enabled("GHOGX_DISABLE_SOURCE_MAT_ZMODE_DEPTH");
}

bool source_group_draw_order_enabled() {
  return !char_env_enabled("GHOGX_DISABLE_SOURCE_GROUP_DRAW_ORDER");
}

bool material_depth_write_enabled(const milo_scene::MatObj* material) {
  if (material && material->has_render_state &&
      source_material_zmode_depth_enabled()) {
    switch (material->z_mode) {
      case 1:  // kZModeNormal
      case 3:  // kZModeForce
        return true;
      case 0:  // kZModeDisable
      case 2:  // kZModeTransparent
      case 4:  // kZModeDecal
      default:
        return false;
    }
  }
  // RB3 RndMat defaults to kNormal z mode. If an older/partial material row
  // does not expose zMode, keep that source default instead of deriving render
  // state from mesh or material names.
  return true;
}

DWORD texture_address_for_wrap(uint8_t tex_wrap) {
  switch (tex_wrap) {
    case 0:  // kTexWrapClamp
      return D3DTADDRESS_CLAMP;
    case 4:  // kTexWrapMirror
      return D3DTADDRESS_MIRROR;
    case 1:  // kTexWrapRepeat
    default:
      return D3DTADDRESS_WRAP;
  }
}

D3DCOLOR character_clear_color() {
  if (char_env_enabled("GHOGX_CHARACTER_SOFT_GREEN_BG")) {
    return D3DCOLOR_XRGB(116, 151, 124);
  }
  return D3DCOLOR_XRGB(24, 26, 38);
}

float char_camera_aspect_preset_or(const char* name, float fallback) {
  const std::string& value = char_env_string_ref(name);
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
    const float parsed = std::strtof(value.c_str(), &end);
    if (end != value.c_str() && std::isfinite(parsed)) {
      if (std::fabs(parsed - kAspect4x3) <= kAspectEpsilon) {
        aspect = kAspect4x3;
      } else if (std::fabs(parsed - kAspect16x9) <= kAspectEpsilon) {
        aspect = kAspect16x9;
      }
    }
  }
  return aspect;
}

struct Bounds3 {
  float mn[3] = {0, 0, 0};
  float mx[3] = {0, 0, 0};
  bool valid = false;
};

void add_bounds(Bounds3& b, const std::array<float, 3>& p) {
  if (!b.valid) {
    for (int k = 0; k < 3; ++k) b.mn[k] = b.mx[k] = p[k];
    b.valid = true;
    return;
  }
  for (int k = 0; k < 3; ++k) {
    b.mn[k] = std::min(b.mn[k], p[k]);
    b.mx[k] = std::max(b.mx[k], p[k]);
  }
}

std::array<float, 3> xform_point(const std::array<float, 16>& m,
                                 const std::array<float, 3>& p) {
  return {
      p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
      p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
      p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14],
  };
}

std::array<float, 3> basis_delta(const std::array<float, 16>& basis,
                                 const std::array<float, 3>& p) {
  const float dx = p[0] - basis[12];
  const float dy = p[1] - basis[13];
  const float dz = p[2] - basis[14];
  return {
      basis[0] * dx + basis[1] * dy + basis[2] * dz,
      basis[4] * dx + basis[5] * dy + basis[6] * dz,
      basis[8] * dx + basis[9] * dy + basis[10] * dz,
  };
}

float aabb_distance_sq(const milo_scene::MeshObj& mesh,
                       const std::array<float, 3>& local) {
  float d2 = 0.0f;
  for (int k = 0; k < 3; ++k) {
    float d = 0.0f;
    if (local[k] < mesh.bb_min[k]) {
      d = mesh.bb_min[k] - local[k];
    } else if (local[k] > mesh.bb_max[k]) {
      d = local[k] - mesh.bb_max[k];
    }
    d2 += d * d;
  }
  return d2;
}

std::array<float, 3> sub3(const std::array<float, 3>& a,
                          const std::array<float, 3>& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

std::array<float, 3> add3(const std::array<float, 3>& a,
                          const std::array<float, 3>& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

std::array<float, 3> scale3(const std::array<float, 3>& a, float s) {
  return {a[0] * s, a[1] * s, a[2] * s};
}

float dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> cross3(const std::array<float, 3>& a,
                            const std::array<float, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

std::array<float, 3> normalize3(const std::array<float, 3>& a) {
  const float len2 = dot3(a, a);
  if (len2 <= 1.0e-12f) return {0, 0, 0};
  return scale3(a, 1.0f / std::sqrt(len2));
}

std::array<float, 3> closest_point_on_triangle(
    const std::array<float, 3>& p, const std::array<float, 3>& a,
    const std::array<float, 3>& b, const std::array<float, 3>& c) {
  const auto ab = sub3(b, a);
  const auto ac = sub3(c, a);
  const auto ap = sub3(p, a);
  const float d1 = dot3(ab, ap);
  const float d2 = dot3(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return a;

  const auto bp = sub3(p, b);
  const float d3 = dot3(ab, bp);
  const float d4 = dot3(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return b;

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return add3(a, scale3(ab, v));
  }

  const auto cp = sub3(p, c);
  const float d5 = dot3(ab, cp);
  const float d6 = dot3(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return c;

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return add3(a, scale3(ac, w));
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const auto bc = sub3(c, b);
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return add3(b, scale3(bc, w));
  }

  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return add3(add3(a, scale3(ab, v)), scale3(ac, w));
}

struct MeshSurfaceClosest {
  bool valid = false;
  float dist_sq = std::numeric_limits<float>::infinity();
  float signed_dist = 0.0f;
  size_t tri = 0;
  std::array<float, 3> closest{0, 0, 0};
  std::array<float, 3> normal{0, 0, 0};
};

MeshSurfaceClosest closest_mesh_surface(const milo_scene::MeshObj& mesh,
                                        const std::array<float, 3>& local) {
  MeshSurfaceClosest best;
  for (size_t tri = 0; tri + 2 < mesh.indices.size(); tri += 3) {
    const uint16_t ia = mesh.indices[tri + 0];
    const uint16_t ib = mesh.indices[tri + 1];
    const uint16_t ic = mesh.indices[tri + 2];
    if (ia >= mesh.verts.size() || ib >= mesh.verts.size() ||
        ic >= mesh.verts.size()) {
      continue;
    }
    const auto a = std::array<float, 3>{mesh.verts[ia].px, mesh.verts[ia].py,
                                        mesh.verts[ia].pz};
    const auto b = std::array<float, 3>{mesh.verts[ib].px, mesh.verts[ib].py,
                                        mesh.verts[ib].pz};
    const auto c = std::array<float, 3>{mesh.verts[ic].px, mesh.verts[ic].py,
                                        mesh.verts[ic].pz};
    const auto normal = normalize3(cross3(sub3(b, a), sub3(c, a)));
    if (dot3(normal, normal) <= 0.0f) continue;
    const auto closest = closest_point_on_triangle(local, a, b, c);
    const auto delta = sub3(local, closest);
    const float d2 = dot3(delta, delta);
    if (!best.valid || d2 < best.dist_sq) {
      best.valid = true;
      best.dist_sq = d2;
      best.signed_dist = dot3(delta, normal);
      best.tri = tri / 3;
      best.closest = closest;
      best.normal = normal;
    }
  }
  return best;
}

const milo_scene::GroupObj* find_character_group(const Character& character,
                                                 const std::string& name) {
  for (const auto& group : character.groups) {
    if (group.name == name) return &group;
  }
  return nullptr;
}

int character_direct_group_rank(const Character& character,
                                const std::string& group_name,
                                const std::string& mesh_name) {
  const milo_scene::GroupObj* group = find_character_group(character, group_name);
  if (!group) return -1;
  for (size_t i = 0; i < group->children.size(); ++i) {
    if (group->children[i] == mesh_name) return static_cast<int>(i);
  }
  return -1;
}

int character_active_lod_group_rank(const Character& character,
                                    const std::string& mesh_name,
                                    int min_lod) {
  const auto active = source_character_active_lod_view(character, min_lod);
  return active ? character_direct_group_rank(character, *active, mesh_name)
                : -1;
}

bool character_group_contains_mesh(const Character& character,
                                   const std::string& group_name,
                                   const std::string& mesh_name,
                                   int depth = 0) {
  if (depth > 8) return false;
  const milo_scene::GroupObj* group = find_character_group(character, group_name);
  if (!group) return false;
  for (const auto& child : group->children) {
    if (child == mesh_name) return true;
    if (find_character_group(character, child) &&
        character_group_contains_mesh(character, child, mesh_name, depth + 1)) {
      return true;
    }
  }
  return false;
}

bool is_hidden_by_character_lod_selection(const Character& character,
                                          const SkinnedMesh& mesh,
                                          int min_lod) {
  const auto active = source_character_active_lod_view(character, min_lod);
  if (!active) return false;
  if (character_group_contains_mesh(character, *active, mesh.name)) {
    return false;
  }
  for (const auto& lod : character.root_lods) {
    if (lod.group != *active &&
        character_group_contains_mesh(character, lod.group, mesh.name)) {
      return true;
    }
  }
  return false;
}

void collect_character_group_meshes(
    const Character& character, const std::string& group_name,
    std::unordered_set<std::string>& visited_groups,
    std::unordered_set<std::string>& meshes) {
  if (!visited_groups.insert(group_name).second) return;
  const milo_scene::GroupObj* group =
      find_character_group(character, group_name);
  if (!group) return;
  for (const auto& child : group->children) {
    if (find_character_group(character, child)) {
      collect_character_group_meshes(
          character, child, visited_groups, meshes);
      continue;
    }
    const bool is_resident_mesh =
        std::any_of(character.meshes.begin(), character.meshes.end(),
                    [&](const SkinnedMesh& mesh) {
                      return mesh.name == child;
                    });
    if (is_resident_mesh) meshes.insert(child);
  }
}

std::unordered_set<std::string> source_character_shadow_meshes(
    const Character& character) {
  std::unordered_set<std::string> meshes;
  if (!character.root_decoded || character.root_shadow.empty()) {
    return meshes;
  }
  std::unordered_set<std::string> visited_groups;
  collect_character_group_meshes(
      character, character.root_shadow, visited_groups, meshes);
  return meshes;
}

std::unordered_set<std::string> source_character_eye_meshes(
    const Character& character) {
  std::unordered_map<std::string, const CharLookAt*> lookats;
  lookats.reserve(character.lookats.size());
  for (const auto& lookat : character.lookats) {
    lookats.emplace(lookat.name, &lookat);
  }
  std::unordered_set<std::string> meshes;
  for (const auto& eyes : character.eyes) {
    for (const auto& lookat_ref : eyes.lookats) {
      const auto found = lookats.find(lookat_ref);
      if (found == lookats.end() || found->second->source.empty()) continue;
      meshes.insert(found->second->source);
    }
  }
  return meshes;
}

bool debug_meshes_enabled() {
  return char_env_enabled("GHOGX_DEBUG_MESHES");
}

bool debug_lighting_enabled() {
  return char_env_enabled("GHOGX_LOG_PERFORMER_LIGHTING");
}

bool debug_texture_alpha_enabled() {
  return char_env_enabled("GHOGX_DEBUG_TEXTURE_ALPHA");
}

DWORD character_cull_mode(
    const ghogx::milo_scene::MatObj* material = nullptr) {
  std::string mode = char_env_string_ref("GHOGX_CHARACTER_CULL");
  std::transform(mode.begin(), mode.end(), mode.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  if (mode == "cw") return D3DCULL_CW;
  if (mode == "ccw") return D3DCULL_CCW;
  if (mode == "none") return D3DCULL_NONE;
  if (material && material->has_cull && !material->cull) {
    return D3DCULL_NONE;
  }
  return D3DCULL_CW;
}

bool debug_bones_enabled() {
  return char_env_enabled("GHOGX_DEBUG_BONES");
}

bool debug_prop_enabled() {
  return char_env_enabled("GHOGX_DEBUG_PROP");
}

bool hide_attached_props_enabled() {
  return char_env_enabled("GHOGX_HIDE_ATTACHED_PROPS");
}

bool hide_eyes_enabled() {
  return char_env_enabled("GHOGX_HIDE_EYES");
}

bool skip_material_enabled(const std::string& material) {
  const std::string& needle = char_env_string_ref("GHOGX_SKIP_MATERIAL");
  if (needle.empty()) return false;
  return material.find(needle) != std::string::npos;
}

bool skip_mesh_enabled(const std::string& mesh) {
  return comma_spec_matches(char_env_string_ref("GHOGX_SKIP_MESH"), mesh);
}

bool only_mesh_enabled(const std::string& mesh) {
  const std::string& spec = char_env_string_ref("GHOGX_ONLY_MESH");
  if (spec.empty()) return true;
  return comma_spec_matches(spec, mesh);
}

bool highlight_mesh_enabled(const std::string& mesh) {
  return comma_spec_matches(char_env_string_ref("GHOGX_HIGHLIGHT_MESH"), mesh);
}

bool debug_mesh_mode_enabled(const std::string& mesh) {
  const std::string& spec = char_env_string_ref("GHOGX_DEBUG_MESH_MODE");
  if (spec.empty()) return false;
  if (spec == "1" || spec == "all") return true;
  return comma_spec_matches(spec, mesh);
}

bool raw_mesh_enabled(const std::string& mesh) {
  return comma_spec_matches(char_env_string_ref("GHOGX_RAW_MESH"), mesh);
}

bool contains_case_insensitive(const std::string& n, const char* needle) {
  std::string lower = n;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  std::string lower_needle = needle ? needle : "";
  std::transform(lower_needle.begin(), lower_needle.end(),
                 lower_needle.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return !lower_needle.empty() &&
         lower.find(lower_needle) != std::string::npos;
}

bool mesh_uses_raw_pose_output(const SkinnedMesh& m,
                               bool raw_mesh_requested) {
  // RndMesh has exactly one source split here: records with an active bone
  // palette use decoded skinning; records without one submit their authored
  // vertices through the mesh transform.  Names, materials, bounds, parents,
  // and anatomical roles do not override a serialized palette.
  return raw_mesh_requested || m.bone_palette.empty();
}

bool debug_skin_bounds_enabled() {
  return char_env_enabled("GHOGX_DEBUG_SKIN_BOUNDS");
}

bool debug_skin_seams_enabled() {
  return char_env_enabled("GHOGX_DEBUG_SKIN_SEAMS");
}

bool debug_face_rows_enabled() {
  return char_env_enabled("GHOGX_DEBUG_FACE_ROWS");
}

bool is_mouth_probe_mesh(const SkinnedMesh& m) {
  return contains_case_insensitive(m.name, "teeth") ||
         contains_case_insensitive(m.name, "tounge") ||
         contains_case_insensitive(m.name, "tongue");
}

bool debug_weight_stats_enabled() {
  return char_env_enabled("GHOGX_DEBUG_WEIGHT_STATS");
}

bool debug_skin_matrix_enabled() {
  return char_env_enabled("GHOGX_DEBUG_SKIN_MATRIX");
}

bool debug_skin_matrix_all_enabled() {
  return char_env_enabled("GHOGX_DEBUG_SKIN_MATRIX_ALL");
}

bool debug_surface_contact_enabled() {
  return char_env_enabled("GHOGX_DEBUG_SURFACE_CONTACT");
}

bool use_mesh_bind_material_enabled(const std::string& material) {
  const std::string& needle =
      char_env_string_ref("GHOGX_USE_MESH_BIND_MATERIAL");
  if (needle.empty()) return false;
  return material.find(needle) != std::string::npos;
}

bool use_mesh_bind_inverse_material_enabled(const std::string& material) {
  const std::string& needle =
      char_env_string_ref("GHOGX_USE_MESH_BIND_INVERSE_MATERIAL");
  if (needle.empty()) return false;
  return material.find(needle) != std::string::npos;
}

bool reverse_skin_weight_slots_enabled() {
  return char_env_enabled("GHOGX_REVERSE_SKIN_WEIGHT_SLOTS");
}

const std::string& skin_matrix_mode() {
  return char_env_string_ref("GHOGX_SKIN_MATRIX_MODE");
}

int character_bone_index(const Character& character, const std::string& name) {
  for (size_t i = 0; i < character.bones.size(); ++i) {
    if (character.bones[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::optional<float> current_toe_reference_z(const Character& character) {
  static constexpr const char* kToeBones[] = {
      "bone_L-toe.mesh", "bone_R-toe.mesh",
      "bone_L-toe0.mesh", "bone_R-toe0.mesh",
      "bone_L-foot.mesh", "bone_R-foot.mesh",
  };
  std::optional<float> min_z;
  for (const char* name : kToeBones) {
    if (character_bone_index(character, name) < 0) continue;
    const auto world = character.bone_world_local_chain(name);
    if (!std::isfinite(world[14])) continue;
    min_z = min_z ? std::min(*min_z, world[14]) : world[14];
  }
  return min_z;
}

const milo_scene::TransObj* scene_trans(
    const milo_scene::Scene& scene,
    const std::string& name) {
  for (const auto& t : scene.transes) {
    if (t.name == name) return &t;
  }
  return nullptr;
}

void reconcile_instrument_anchor(Character& character,
                                 std::vector<milo_scene::Xfm>& original_locals,
                                 const milo_scene::Scene& prop_scene,
                                 const std::string& attach_bone,
                                 const char* anchor_name) {
  const auto* prop_anchor = scene_trans(prop_scene, anchor_name);
  if (!prop_anchor) return;

  const int bone_i = character_bone_index(character, anchor_name);
  if (bone_i < 0) return;
  auto& bone = character.bones[static_cast<size_t>(bone_i)];
  if (bone.parent != prop_anchor->parent) return;
  if (prop_anchor->parent != attach_bone &&
      character_bone_index(character, prop_anchor->parent) < 0) {
    return;
  }

  const auto old = bone.local;
  bone.local = prop_anchor->local;
  if (character.bind_bone_local.size() > static_cast<size_t>(bone_i))
    character.bind_bone_local[static_cast<size_t>(bone_i)] =
        prop_anchor->local;
  if (original_locals.size() > static_cast<size_t>(bone_i))
    original_locals[static_cast<size_t>(bone_i)] = prop_anchor->local;

  std::fprintf(stderr,
               "[char3d] instrument anchor %s from prop '%s': "
               "parent=%s local=(%.4f %.4f %.4f) "
               "was=(%.4f %.4f %.4f) source=prop-asset\n",
               anchor_name, prop_scene.dir_name.c_str(),
               prop_anchor->parent.c_str(), prop_anchor->local.pos[0],
               prop_anchor->local.pos[1], prop_anchor->local.pos[2],
               old.pos[0], old.pos[1], old.pos[2]);
}

void reconcile_instrument_fret_targets(
    Character& character, std::vector<milo_scene::Xfm>& original_locals,
    const milo_scene::Scene& prop_scene, const std::string& attach_bone) {
  char anchor[32];
  for (int fret = 1; fret <= 20; ++fret) {
    std::snprintf(anchor, sizeof(anchor), "spot_neck_fret%02d.mesh", fret);
    reconcile_instrument_anchor(character, original_locals, prop_scene,
                                attach_bone, anchor);
  }
}

size_t import_attached_prop_transform_proxies(
    Character& character, const milo_scene::Scene& prop_scene) {
  auto transform_exists = [&](const std::string& name) {
    return std::any_of(character.bones.begin(), character.bones.end(),
                       [&](const auto& bone) { return bone.name == name; }) ||
           std::any_of(character.meshes.begin(), character.meshes.end(),
                       [&](const auto& mesh) { return mesh.name == name; }) ||
           character.attached_prop_transform_proxies.find(name) !=
               character.attached_prop_transform_proxies.end();
  };

  size_t imported = 0;
  for (const auto& source : prop_scene.transes) {
    // Plain RndTransformable rows share the retail ObjectDir with meshes.
    // GH2 guitars author fret spots and hand-target hierarchy as Trans rows,
    // while GH1 guitars commonly expose equivalent rows as mesh transforms.
    // Import both representations as transform-only proxies so character
    // revision and instrument revision remain independently selectable.
    if (transform_exists(source.name)) continue;
    AttachedPropTransformProxy proxy;
    proxy.name = source.name;
    proxy.parent = source.parent;
    proxy.local = source.local;
    proxy.bind_local = source.local;
    character.attached_prop_transform_proxies.emplace(source.name,
                                                      std::move(proxy));
    ++imported;
  }
  for (const auto& source : prop_scene.meshes) {
    // Every RndMesh is also a transform. Retail drivers resolve prop transforms
    // in the same world ObjectDir as the character, including visible fret
    // markers used as positioning targets. Import only their transform rows
    // (never duplicate prop geometry), preserving names and hierarchy.
    if (transform_exists(source.name)) {
      continue;
    }
    AttachedPropTransformProxy proxy;
    proxy.name = source.name;
    proxy.parent = source.parent;
    proxy.local = source.local;
    proxy.bind_local = source.local;
    character.attached_prop_transform_proxies.emplace(source.name,
                                                      std::move(proxy));
    ++imported;
  }
  return imported;
}

std::array<float, 16> mul16(const std::array<float, 16>& a,
                            const std::array<float, 16>& b);
std::array<float, 16> affine_inverse(const std::array<float, 16>& m);
std::array<float, 16> xfm16(const milo_scene::Xfm& x);
std::array<float, 16> scene_object_world(const milo_scene::Scene& scene,
                                         const std::string& name);
std::optional<std::array<float, 16>> scene_object_stored_world(
    const milo_scene::Scene& scene,
    const std::string& name);

std::array<float, 16> prop_attach_world(const Character& character,
                                        const std::string& attach_bone) {
  // Accepted prop traces route guitars/mics through the same moving Trans rows
  // as skinned character output. Keep instruments in the character local-chain
  // basis instead of the stored-world correction used for a few rigid meshes.
  return character.bone_world_local_chain(attach_bone);
}

void log_matrix_row(const char* tag, const std::string& mesh,
                    const std::string& bone,
                    const std::array<float, 16>& m) {
  std::fprintf(stderr,
               "[hair-space] %-12s mesh=%-16s bone=%-20s "
               "r0=(%.4f %.4f %.4f %.4f) "
               "r1=(%.4f %.4f %.4f %.4f) "
               "r2=(%.4f %.4f %.4f %.4f) "
               "pos=(%.4f %.4f %.4f %.4f)\n",
               tag, mesh.c_str(), bone.c_str(), m[0], m[1], m[2], m[3],
               m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12],
               m[13], m[14], m[15]);
}

void log_compact_matrix_rows(const char* tag,
                             const std::string& mesh,
                             const std::string& bone,
                             const std::array<float, 16>& m) {
  for (int row = 0; row < 4; ++row) {
    std::fprintf(stderr,
                 "[skin-row-%s] mesh=%s bone=%s row=%d "
                 "%.5f %.5f %.5f %.5f\n",
                 tag, mesh.c_str(), bone.c_str(), row, m[row * 4 + 0],
                 m[row * 4 + 1], m[row * 4 + 2], m[row * 4 + 3]);
  }
}

std::array<float, 3> transform_point(const std::array<float, 3>& p,
                                     const std::array<float, 16>& m) {
  return {p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
          p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
          p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]};
}

int wrapped_texel_coord(float uv, int size) {
  if (size <= 0 || !std::isfinite(uv)) return 0;
  const float wrapped = uv - std::floor(uv);
  int coord = static_cast<int>(wrapped * static_cast<float>(size));
  if (coord < 0) coord = 0;
  if (coord >= size) coord = size - 1;
  return coord;
}

int clamped_texel_coord(float uv, int size) {
  if (size <= 0 || !std::isfinite(uv)) return 0;
  const float clamped = std::clamp(uv, 0.0f, 1.0f);
  int coord = static_cast<int>(clamped * static_cast<float>(size - 1));
  if (coord < 0) coord = 0;
  if (coord >= size) coord = size - 1;
  return coord;
}

int mirrored_texel_coord(float uv, int size) {
  if (size <= 0 || !std::isfinite(uv)) return 0;
  float t = std::fmod(std::abs(uv), 2.0f);
  if (t > 1.0f) t = 2.0f - t;
  int coord = static_cast<int>(t * static_cast<float>(size - 1));
  if (coord < 0) coord = 0;
  if (coord >= size) coord = size - 1;
  return coord;
}

uint8_t sample_alpha_mode(const ghogx::asset::Image& img, float u, float v,
                          const char* mode) {
  if (!img.valid()) return 255;
  int x = 0;
  int y = 0;
  if (std::strcmp(mode, "clamp") == 0) {
    x = clamped_texel_coord(u, img.width);
    y = clamped_texel_coord(v, img.height);
  } else if (std::strcmp(mode, "mirror") == 0) {
    x = mirrored_texel_coord(u, img.width);
    y = mirrored_texel_coord(v, img.height);
  } else {
    x = wrapped_texel_coord(u, img.width);
    y = wrapped_texel_coord(v, img.height);
  }
  const size_t idx = (static_cast<size_t>(y) * img.width + x) * 4 + 3;
  return idx < img.rgba.size() ? img.rgba[idx] : 255;
}

uint8_t sample_alpha(const ghogx::asset::Image& img, float u, float v) {
  return sample_alpha_mode(img, u, v, "wrap");
}

void log_texture_alpha_stats(const SkinnedMesh& mesh,
                             const milo_scene::MatObj* material,
                             const ghogx::asset::Image* image) {
  if (!image || !image->valid() || mesh.verts.empty()) return;
  float min_u = mesh.verts[0].u;
  float max_u = mesh.verts[0].u;
  float min_v = mesh.verts[0].v;
  float max_v = mesh.verts[0].v;
  int vert_zero = 0;
  int vert_lt32 = 0;
  int vert_lt96 = 0;
  int vert_opaque = 0;
  for (const auto& vert : mesh.verts) {
    min_u = std::min(min_u, vert.u);
    max_u = std::max(max_u, vert.u);
    min_v = std::min(min_v, vert.v);
    max_v = std::max(max_v, vert.v);
    const uint8_t a = sample_alpha(*image, vert.u, vert.v);
    if (a == 0) ++vert_zero;
    if (a < 32) ++vert_lt32;
    if (a < 96) ++vert_lt96;
    if (a >= 250) ++vert_opaque;
  }

  int tri_zero = 0;
  int tri_lt32 = 0;
  int tri_lt96 = 0;
  int tri_opaque = 0;
  int tri_samples = 0;
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
    const float u = (a.u + b.u + c.u) / 3.0f;
    const float v = (a.v + b.v + c.v) / 3.0f;
    const uint8_t alpha = sample_alpha(*image, u, v);
    ++tri_samples;
    if (alpha == 0) ++tri_zero;
    if (alpha < 32) ++tri_lt32;
    if (alpha < 96) ++tri_lt96;
    if (alpha >= 250) ++tri_opaque;
  }

  std::fprintf(stderr,
               "[tex-alpha] %-24s mat=%-18s tex=%-24s size=%dx%d",
               mesh.name.c_str(), mesh.material.c_str(),
               material ? material->diffuse_tex.c_str() : "", image->width,
               image->height);
  std::fprintf(stderr, " uv=(%.3f..%.3f, %.3f..%.3f)", min_u, max_u,
               min_v, max_v);
  std::fprintf(stderr, " verts=%llu a0=%d a<32=%d a<96=%d opaque=%d",
               static_cast<unsigned long long>(mesh.verts.size()), vert_zero,
               vert_lt32, vert_lt96, vert_opaque);
  std::fprintf(stderr, " tris=%d a0=%d a<32=%d a<96=%d opaque=%d\n",
               tri_samples, tri_zero, tri_lt32, tri_lt96, tri_opaque);

  auto count_mode = [&](const char* mode, int& zero, int& lt32, int& lt96,
                        int& opaque) {
    zero = lt32 = lt96 = opaque = 0;
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
      const float u = (a.u + b.u + c.u) / 3.0f;
      const float v = (a.v + b.v + c.v) / 3.0f;
      const uint8_t alpha = sample_alpha_mode(*image, u, v, mode);
      if (alpha == 0) ++zero;
      if (alpha < 32) ++lt32;
      if (alpha < 96) ++lt96;
      if (alpha >= 250) ++opaque;
    }
  };
  int cz = 0, c32 = 0, c96 = 0, co = 0;
  int mz = 0, m32 = 0, m96 = 0, mo = 0;
  count_mode("clamp", cz, c32, c96, co);
  count_mode("mirror", mz, m32, m96, mo);
  std::fprintf(stderr,
               "[tex-alpha-address] %-24s wrap(a<96=%d opaque=%d) "
               "clamp(a<96=%d opaque=%d) mirror(a<96=%d opaque=%d)\n",
               mesh.name.c_str(), tri_lt96, tri_opaque, c96, co, m96, mo);
}

void log_prop_texture_alpha_stats(const milo_scene::MeshObj& mesh,
                                  const milo_scene::MatObj* material,
                                  const ghogx::asset::Image* image,
                                  bool texture_alpha_enabled) {
  if (!image || !image->valid() || mesh.verts.empty()) return;
  int vert_zero = 0;
  int vert_lt32 = 0;
  int vert_lt96 = 0;
  int vert_opaque = 0;
  for (const auto& vert : mesh.verts) {
    const uint8_t alpha = sample_alpha(*image, vert.u, vert.v);
    if (alpha == 0) ++vert_zero;
    if (alpha < 32) ++vert_lt32;
    if (alpha < 96) ++vert_lt96;
    if (alpha >= 250) ++vert_opaque;
  }

  int tri_zero = 0;
  int tri_lt32 = 0;
  int tri_lt96 = 0;
  int tri_opaque = 0;
  int tri_samples = 0;
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
    const float u = (a.u + b.u + c.u) / 3.0f;
    const float v = (a.v + b.v + c.v) / 3.0f;
    const uint8_t alpha = sample_alpha(*image, u, v);
    ++tri_samples;
    if (alpha == 0) ++tri_zero;
    if (alpha < 32) ++tri_lt32;
    if (alpha < 96) ++tri_lt96;
    if (alpha >= 250) ++tri_opaque;
  }

  std::fprintf(stderr,
               "[prop-alpha] mesh=%s mat=%s tex=%s size=%dx%d "
               "textureAlpha=%d blend=%u renderState=%d alphaCut=%d "
               "alphaRef=%d cull=%d zMode=%u "
               "verts=%u a0=%d a<32=%d a<96=%d opaque=%d "
               "tris=%d a0=%d a<32=%d a<96=%d opaque=%d\n",
               mesh.name.c_str(), mesh.material.c_str(),
               material ? material->diffuse_tex.c_str() : "", image->width,
               image->height, texture_alpha_enabled ? 1 : 0,
               material ? static_cast<unsigned>(material->blend) : 0u,
               material && material->has_render_state ? 1 : 0,
               material && material->alpha_cut ? 1 : 0,
               material ? material->alpha_threshold : 0,
               material && material->has_cull
                   ? (material->cull ? 1 : 0)
                   : -1,
               material ? static_cast<unsigned>(material->z_mode) : 0u,
               static_cast<unsigned>(mesh.verts.size()), vert_zero, vert_lt32,
               vert_lt96, vert_opaque, tri_samples, tri_zero, tri_lt32,
               tri_lt96, tri_opaque);
  std::fprintf(stderr,
               "[prop-alpha-counts] mesh=%s vertZero=%d vertLt32=%d "
               "vertLt96=%d vertOpaque=%d triSamples=%d triZero=%d "
               "triLt32=%d triLt96=%d triOpaque=%d\n",
               mesh.name.c_str(), vert_zero, vert_lt32, vert_lt96,
               vert_opaque, tri_samples, tri_zero, tri_lt32, tri_lt96,
               tri_opaque);
}

void log_vertex_float_stats(const SkinnedMesh& mesh) {
  if (mesh.verts.empty()) return;
  float mn[4] = {mesh.verts[0].w[0], mesh.verts[0].w[1], mesh.verts[0].w[2],
                 mesh.verts[0].w[3]};
  float mx[4] = {mn[0], mn[1], mn[2], mn[3]};
  int zeros[4] = {};
  int ones[4] = {};
  for (const auto& v : mesh.verts) {
    for (int i = 0; i < 4; ++i) {
      mn[i] = std::min(mn[i], v.w[i]);
      mx[i] = std::max(mx[i], v.w[i]);
      if (std::abs(v.w[i]) < 1e-5f) ++zeros[i];
      if (std::abs(v.w[i] - 1.0f) < 1e-5f) ++ones[i];
    }
  }
  std::fprintf(stderr,
               "[vertex-floats] %-24s palette=%zu mat=%-18s "
               "f0=(%.3f..%.3f z=%d one=%d) "
               "f1=(%.3f..%.3f z=%d one=%d) "
               "f2=(%.3f..%.3f z=%d one=%d) "
               "f3=(%.3f..%.3f z=%d one=%d)\n",
               mesh.name.c_str(), mesh.bone_palette.size(),
               mesh.material.c_str(), mn[0], mx[0], zeros[0], ones[0], mn[1],
               mx[1], zeros[1], ones[1], mn[2], mx[2], zeros[2], ones[2],
               mn[3], mx[3], zeros[3], ones[3]);
}

void build_character_draw_order(const Character& character, int min_lod,
                                 std::vector<const SkinnedMesh*>& out) {
  out.clear();
  out.reserve(character.meshes.size());
  const SourceCharacterDrawClosure closure =
      source_character_draw_closure(character, min_lod);
  for (const auto& mesh : character.meshes) {
    if (!closure.authoritative ||
        closure.meshes.find(mesh.name) != closure.meshes.end()) {
      out.push_back(&mesh);
    }
  }
  const bool use_source_group_order = source_group_draw_order_enabled();
  std::stable_sort(out.begin(), out.end(),
                   [&](const SkinnedMesh* a, const SkinnedMesh* b) {
                      if (use_source_group_order) {
                        const int ar = character_active_lod_group_rank(
                            character, a->name, min_lod);
                        const int br = character_active_lod_group_rank(
                            character, b->name, min_lod);
                        if (ar >= 0 && br >= 0 && ar != br) return ar < br;
                        if ((ar >= 0) != (br >= 0)) return ar >= 0;
                      }
                      if (std::fabs(a->draw_order - b->draw_order) > 1.0e-5f) {
                        return a->draw_order < b->draw_order;
                      }
                     // stable_sort retains packed source order when both
                     // authored ordering fields are equal. Anatomical or
                     // material-name guesses must not reorder the graph.
                     return false;
                    });
}

void build_prop_draw_order(
    const milo_scene::Scene& scene,
    std::vector<const milo_scene::MeshObj*>& out) {
  out.clear();
  out.reserve(scene.meshes.size());
  std::unordered_set<std::string> emitted;
  emitted.reserve(scene.meshes.size());

  // RndGroup child order is authoritative for a packed instrument. In
  // particular, GH2/RB2 guitar groups draw the opaque body before the
  // alpha-blended strings. Iterating raw directory order can reverse those
  // records, allowing transparent string texels to populate depth before the
  // body and punch holes through it.
  for (const std::string& name : scene.draw_order) {
    const auto it = std::find_if(
        scene.meshes.begin(), scene.meshes.end(),
        [&](const milo_scene::MeshObj& mesh) { return mesh.name == name; });
    if (it != scene.meshes.end() && emitted.insert(name).second) {
      out.push_back(&*it);
    }
  }
  for (const milo_scene::MeshObj& mesh : scene.meshes) {
    if (emitted.insert(mesh.name).second) out.push_back(&mesh);
  }
}

DWORD modulate_source_vertex_color(D3DCOLOR base,
                                   const SkinVertex& vertex,
                                   bool use_vertex_color) {
  if (!use_vertex_color) return base;
  const auto channel = [](DWORD packed, unsigned shift, float value) {
    const float source = static_cast<float>((packed >> shift) & 0xFFu);
    const int result = static_cast<int>(source * value + 0.5f);
    return static_cast<DWORD>(std::clamp(result, 0, 255));
  };
  return D3DCOLOR_ARGB(
      channel(base, 24, vertex.a), channel(base, 16, vertex.r),
      channel(base, 8, vertex.g), channel(base, 0, vertex.b));
}

void append_raw_pose_vertices(const SkinnedMesh& mesh,
                              D3DCOLOR mesh_color,
                              bool use_vertex_color,
                              std::vector<SVtx>& out) {
  out.resize(mesh.verts.size());
  for (size_t vi = 0; vi < mesh.verts.size(); ++vi) {
    const auto& v = mesh.verts[vi];
    SVtx& s = out[vi];
    s.x = v.px; s.y = v.py; s.z = v.pz;
    s.nx = v.nx; s.ny = v.ny; s.nz = v.nz;
    s.color =
        modulate_source_vertex_color(mesh_color, v, use_vertex_color);
    s.u = v.u; s.v = v.v;
  }
}

void append_skinned_pose_vertices(
    const SkinnedMesh& mesh,
    const std::vector<std::array<float, 3>>& pos,
    const std::vector<std::array<float, 3>>& nrm,
    D3DCOLOR mesh_color,
    bool use_vertex_color,
    std::vector<SVtx>& out) {
  out.resize(mesh.verts.size());
  for (size_t vi = 0; vi < mesh.verts.size(); ++vi) {
    SVtx& s = out[vi];
    s.x = pos[vi][0]; s.y = pos[vi][1]; s.z = pos[vi][2];
    s.nx = nrm[vi][0]; s.ny = nrm[vi][1]; s.nz = nrm[vi][2];
    s.color = modulate_source_vertex_color(
        mesh_color, mesh.verts[vi], use_vertex_color);
    s.u = mesh.verts[vi].u;
    s.v = mesh.verts[vi].v;
  }
}

struct CharacterWorldFrameCache {
  explicit CharacterWorldFrameCache(const Character& c) : character(c) {
    const size_t capacity = c.bones.size() + c.meshes.size() + 8;
    current_local_chain.reserve(capacity);
    bind_local_chain.reserve(capacity);
    current_world.reserve(capacity);
    bind_world.reserve(capacity);
  }

  std::array<float, 16> bone_world_local_chain(
      const std::string& name) const {
    return cached(current_local_chain, name, [&]() {
      return character.bone_world_local_chain(name);
    });
  }

  std::array<float, 16> bone_world_bind_local_chain(
      const std::string& name) const {
    return cached(bind_local_chain, name, [&]() {
      return character.bone_world_bind_local_chain(name);
    });
  }

  std::array<float, 16> bone_world(const std::string& name) const {
    return cached(current_world, name, [&]() {
      return character.bone_world(name);
    });
  }

  std::array<float, 16> bone_world_bind(const std::string& name) const {
    return cached(bind_world, name, [&]() {
      return character.bone_world_bind(name);
    });
  }

  const Character& character;
  mutable std::unordered_map<std::string, std::array<float, 16>>
      current_local_chain;
  mutable std::unordered_map<std::string, std::array<float, 16>>
      bind_local_chain;
  mutable std::unordered_map<std::string, std::array<float, 16>>
      current_world;
  mutable std::unordered_map<std::string, std::array<float, 16>> bind_world;

 private:
  template <typename Compute>
  std::array<float, 16> cached(
      std::unordered_map<std::string, std::array<float, 16>>& map,
      const std::string& name, Compute compute) const {
    const auto [it, inserted] = map.emplace(name, std::array<float, 16>{});
    if (inserted) it->second = compute();
    return it->second;
  }
};

void skin_to_pose_impl(
    const SkinnedMesh& mesh,
    const Character& character,
    const CharacterWorldFrameCache& world_cache,
    std::vector<std::array<float, 3>>& out_pos,
    std::vector<std::array<float, 3>>& out_nrm,
    std::vector<std::array<float, 16>>& skin);

}  // namespace

struct CharRenderer::Impl {
  Window* win = nullptr;
  IDirect3DDevice9* dev = nullptr;
  Character character;
  OrbitCamera cam;
  OrbitCamera impostor_cam;
  std::map<std::string, IDirect3DTexture9*> tex;  // keyed by .tex entry name
  std::map<std::string, ghogx::asset::Image> tex_images;
  std::map<std::string, const milo_scene::MatObj*> character_mats_by_name;
  std::unordered_set<std::string> eye_meshes;
  std::unordered_set<std::string> shadow_meshes;
  std::set<std::string> logged_texture_alpha_meshes;
  milo_scene::Scene prop_scene;
  std::map<std::string, IDirect3DTexture9*> prop_tex;
  std::map<std::string, ghogx::asset::Image> prop_tex_images;
  std::set<std::string> logged_prop_texture_alpha_meshes;
  std::map<std::string, const milo_scene::MatObj*> prop_mats_by_name;
  std::string prop_attach_bone;
  bool has_prop = false;
  bool logged_prop_debug = false;
  float bb_min[3] = {0, 0, 0};
  float bb_max[3] = {0, 0, 0};
  bool have_bounds = false;
  float world_offset[3] = {0.0f, 0.0f, 0.0f};
  std::array<float, 16> world_transform = {1, 0, 0, 0, 0, 1, 0, 0,
                                           0, 0, 1, 0, 0, 0, 0, 1};
  int min_lod = 0;
  bool use_scene_lighting = false;
  bool proof_lighting = false;
  bool reference_base = false;
  float color_mod[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  IDirect3DTexture9* worldcrowd_impostor_tex = nullptr;
  IDirect3DSurface9* worldcrowd_impostor_surface = nullptr;
  IDirect3DSurface9* worldcrowd_impostor_depth = nullptr;
  float camera_aspect_override = 0.0f;

  // Procedural idle animation time (seconds).
  float anim_t = 0.0f;
  // Original bone local Xfm values saved at set_character() for restore-before-update.
  std::vector<milo_scene::Xfm> original_bone_local;
  std::vector<milo_scene::Xfm> original_mesh_local;
  std::vector<const SkinnedMesh*> ordered_draw_meshes;
  std::unordered_set<std::string> hidden_meshes;
  std::vector<SVtx> scratch_vertices;
  std::vector<std::array<float, 3>> scratch_pos;
  std::vector<std::array<float, 3>> scratch_nrm;
  std::vector<std::array<float, 16>> scratch_skin;
};

CharRenderer::CharRenderer(Window& win) : impl_(new Impl) {
  impl_->win = &win;
  impl_->dev = static_cast<IDirect3DDevice9*>(win.device_ptr());
}

CharRenderer::~CharRenderer() {
  for (auto& kv : impl_->tex)
    if (kv.second) kv.second->Release();
  for (auto& kv : impl_->prop_tex)
    if (kv.second) kv.second->Release();
  if (impl_->worldcrowd_impostor_surface)
    impl_->worldcrowd_impostor_surface->Release();
  if (impl_->worldcrowd_impostor_depth)
    impl_->worldcrowd_impostor_depth->Release();
  if (impl_->worldcrowd_impostor_tex)
    impl_->worldcrowd_impostor_tex->Release();
  delete impl_;
}

OrbitCamera& CharRenderer::camera() { return impl_->cam; }
Character& CharRenderer::character() { return impl_->character; }
bool CharRenderer::has_drawable_geometry() const {
  return impl_ && impl_->have_bounds;
}

bool CharRenderer::set_object_showing(std::string_view object_name,
                                      bool showing) {
  std::vector<std::string> pending{std::string(object_name)};
  std::unordered_set<std::string> visited;
  std::unordered_set<std::string> meshes;
  while (!pending.empty()) {
    std::string current = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(current).second) continue;
    bool group_found = false;
    for (const auto& group : impl_->character.groups) {
      if (group.name != current) continue;
      group_found = true;
      for (const auto& child : group.children) pending.push_back(child);
    }
    if (group_found) continue;
    for (const auto& mesh : impl_->character.meshes) {
      if (mesh.name == current) meshes.insert(mesh.name);
    }
  }
  for (const auto& mesh : meshes) {
    if (showing)
      impl_->hidden_meshes.erase(mesh);
    else
      impl_->hidden_meshes.insert(mesh);
  }
  return !meshes.empty();
}

void CharRenderer::set_world_offset(float x, float y, float z) {
  impl_->world_offset[0] = x;
  impl_->world_offset[1] = y;
  impl_->world_offset[2] = z;
  impl_->world_transform = {1, 0, 0, 0, 0, 1, 0, 0,
                            0, 0, 1, 0, x, y, z, 1};
}

void CharRenderer::set_world_transform(const std::array<float, 16>& m) {
  impl_->world_transform = m;
  impl_->world_offset[0] = impl_->world_offset[1] = impl_->world_offset[2] = 0.0f;
}

void CharRenderer::set_min_lod(int min_lod) {
  const int clamped = std::max(0, min_lod);
  if (impl_->min_lod == clamped) return;
  impl_->min_lod = clamped;
  build_character_draw_order(impl_->character, impl_->min_lod,
                             impl_->ordered_draw_meshes);
  if (debug_meshes_enabled()) {
    std::fprintf(stderr, "[char3d] min_lod active: %d\n", impl_->min_lod);
  }
}

void CharRenderer::set_use_scene_lighting(bool enabled) {
  impl_->use_scene_lighting = enabled;
}

void CharRenderer::set_proof_lighting(bool enabled) {
  impl_->proof_lighting = enabled;
}

void CharRenderer::set_reference_base(bool enabled) {
  impl_->reference_base = enabled;
}

void CharRenderer::set_color_modulation(float r, float g, float b, float a) {
  impl_->color_mod[0] = std::clamp(r, 0.0f, 4.0f);
  impl_->color_mod[1] = std::clamp(g, 0.0f, 4.0f);
  impl_->color_mod[2] = std::clamp(b, 0.0f, 4.0f);
  impl_->color_mod[3] = std::clamp(a, 0.0f, 1.0f);
}

std::optional<std::array<float, 16>> CharRenderer::attached_prop_world(
    std::string_view object_name) const {
  const auto& impl = *impl_;
  if (!impl.has_prop || impl.prop_attach_bone.empty()) return std::nullopt;

  auto matches = [&](std::string_view candidate) {
    if (candidate == object_name) return true;
    constexpr std::string_view suffix = ".mesh";
    if (candidate.size() > suffix.size() &&
        candidate.substr(candidate.size() - suffix.size()) == suffix &&
        candidate.substr(0, candidate.size() - suffix.size()) == object_name) {
      return true;
    }
    if (object_name.size() > suffix.size() &&
        object_name.substr(object_name.size() - suffix.size()) == suffix &&
        object_name.substr(0, object_name.size() - suffix.size()) ==
            candidate) {
      return true;
    }
    return false;
  };

  std::optional<std::string> matched_object;
  const char* matched_kind = nullptr;
  for (const auto& mesh : impl.prop_scene.meshes) {
    if (!mesh.decoded || !matches(mesh.name)) continue;
    matched_object = mesh.name;
    matched_kind = "mesh";
    break;
  }
  if (!matched_object) {
    for (const auto& trans : impl.prop_scene.transes) {
      if (!matches(trans.name)) continue;
      matched_object = trans.name;
      matched_kind = "trans";
      break;
    }
  }
  if (!matched_object) return std::nullopt;

  if (debug_prop_enabled()) {
    static std::set<std::string> logged_prop_targets;
    const std::string requested(object_name);
    const std::string log_key = impl.prop_scene.dir_name + "|" + requested +
                                "|" + *matched_object;
    if (logged_prop_targets.size() < 128 &&
        logged_prop_targets.insert(log_key).second) {
      std::fprintf(stderr,
                   "[char3d] prop camera target '%s' resolved as %s '%s' "
                   "attach=%s prop=%s\n",
                   requested.c_str(), matched_kind ? matched_kind : "object",
                   matched_object->c_str(), impl.prop_attach_bone.c_str(),
                   impl.prop_scene.dir_name.c_str());
    }
  }

  const auto attach_world =
      prop_attach_world(impl.character, impl.prop_attach_bone);
  const auto prop_anchor_world =
      scene_object_world(impl.prop_scene, impl.prop_attach_bone);
  const auto prop_to_attach =
      mul16(affine_inverse(prop_anchor_world), attach_world);
  auto world = mul16(scene_object_world(impl.prop_scene, *matched_object),
                     prop_to_attach);
  world = mul16(world, impl.world_transform);
  return world;
}

IDirect3DTexture9* CharRenderer::upload(const ghogx::asset::Image& img) {
  if (!impl_->dev || !img.valid()) return nullptr;
  IDirect3DTexture9* t = nullptr;
  if (FAILED(impl_->dev->CreateTexture(static_cast<UINT>(img.width),
                                       static_cast<UINT>(img.height), 1, 0,
                                       D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t,
                                       nullptr)))
    return nullptr;
  D3DLOCKED_RECT lr;
  if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
    for (int y = 0; y < img.height; ++y) {
      auto* dst = static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
      const uint8_t* src =
          img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
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

void CharRenderer::set_character(
    Character character,
    const std::map<std::string, ghogx::asset::Image>& textures) {
  impl_->character = std::move(character);
  impl_->tex_images = textures;
  impl_->character_mats_by_name.clear();
  for (const auto& mat : impl_->character.mats) {
    impl_->character_mats_by_name[mat.name] = &mat;
  }
  impl_->eye_meshes = source_character_eye_meshes(impl_->character);
  impl_->shadow_meshes = source_character_shadow_meshes(impl_->character);
  impl_->logged_texture_alpha_meshes.clear();

  // Save original bind-pose bone local transforms for restore-before-update.
  impl_->original_bone_local.clear();
  impl_->original_bone_local.reserve(impl_->character.bones.size());
  for (const auto& b : impl_->character.bones)
    impl_->original_bone_local.push_back(b.local);
  impl_->original_mesh_local.clear();
  impl_->original_mesh_local.reserve(impl_->character.meshes.size());
  for (const auto& m : impl_->character.meshes)
    impl_->original_mesh_local.push_back(m.local);
  build_character_draw_order(impl_->character, impl_->min_lod,
                             impl_->ordered_draw_meshes);
  if (debug_bones_enabled()) {
    for (const auto& b : impl_->character.bones) {
      std::fprintf(stderr,
                   "[bone] %-28s parent=%-28s local=(%.3f %.3f %.3f) "
                   "world=(%.3f %.3f %.3f)\n",
                   b.name.c_str(), b.parent.c_str(), b.local.pos[0],
                   b.local.pos[1], b.local.pos[2], b.world_stored.pos[0],
                   b.world_stored.pos[1], b.world_stored.pos[2]);
    }
  }
  if (debug_meshes_enabled()) {
    for (const auto& mat : impl_->character.mats) {
      std::fprintf(stderr,
                   "[mat] %-24s tex=%-24s color=(%.3f %.3f %.3f %.3f) "
                   "blend=0x%02x use_env=%d prelit=%d zmode=%u\n",
                   mat.name.c_str(), mat.diffuse_tex.c_str(), mat.color[0],
                   mat.color[1], mat.color[2], mat.color[3], mat.blend,
                   mat.use_environ ? 1 : 0, mat.prelit ? 1 : 0,
                   static_cast<unsigned>(mat.z_mode));
    }
    for (const auto& group : impl_->character.groups) {
      std::fprintf(stderr, "[char-group] %s children=%zu", group.name.c_str(),
                   group.children.size());
      for (const auto& child : group.children)
        std::fprintf(stderr, " %s", child.c_str());
      std::fprintf(stderr, "\n");
    }
    for (const auto& m : impl_->character.meshes) {
      float mn[3] = {0, 0, 0};
      float mx[3] = {0, 0, 0};
      if (!m.verts.empty()) {
        mn[0] = mx[0] = m.verts[0].px;
        mn[1] = mx[1] = m.verts[0].py;
        mn[2] = mx[2] = m.verts[0].pz;
        for (const auto& v : m.verts) {
          mn[0] = std::min(mn[0], v.px); mx[0] = std::max(mx[0], v.px);
          mn[1] = std::min(mn[1], v.py); mx[1] = std::max(mx[1], v.py);
          mn[2] = std::min(mn[2], v.pz); mx[2] = std::max(mx[2], v.pz);
        }
      }
      std::fprintf(stderr,
                   "[mesh] %-32s parent=%-24s mat=%-20s show=%d verts=%zu faces=%zu palette=%zu bbox=(%.3f %.3f %.3f)..(%.3f %.3f %.3f)\n",
                   m.name.c_str(), m.parent.c_str(), m.material.c_str(),
                   m.showing ? 1 : 0, m.verts.size(), m.indices.size() / 3,
                   m.bone_palette.size(), mn[0], mn[1], mn[2], mx[0], mx[1],
                   mx[2]);
      if (debug_mesh_mode_enabled(m.name)) {
        std::fprintf(stderr,
                     "[mesh-xfm] %-32s local=(%.3f %.3f %.3f) "
                     "worldStored=(%.3f %.3f %.3f) "
                     "localRows=[%.3f %.3f %.3f|%.3f %.3f %.3f|"
                     "%.3f %.3f %.3f] "
                     "storedRows=[%.3f %.3f %.3f|%.3f %.3f %.3f|"
                     "%.3f %.3f %.3f]\n",
                     m.name.c_str(), m.local.pos[0], m.local.pos[1],
                     m.local.pos[2], m.world_stored.pos[0],
                     m.world_stored.pos[1], m.world_stored.pos[2],
                     m.local.rot[0][0], m.local.rot[0][1], m.local.rot[0][2],
                     m.local.rot[1][0], m.local.rot[1][1], m.local.rot[1][2],
                     m.local.rot[2][0], m.local.rot[2][1], m.local.rot[2][2],
                     m.world_stored.rot[0][0], m.world_stored.rot[0][1],
                     m.world_stored.rot[0][2], m.world_stored.rot[1][0],
                     m.world_stored.rot[1][1], m.world_stored.rot[1][2],
                     m.world_stored.rot[2][0], m.world_stored.rot[2][1],
                     m.world_stored.rot[2][2]);
      }
      if (!m.bone_palette.empty()) {
        float wsum[4] = {};
        for (const auto& v : m.verts) {
          for (int wi = 0; wi < 4; ++wi) wsum[wi] += v.w[wi];
        }
        std::fprintf(stderr, "[mesh-palette] %s weights=(%.3f %.3f %.3f %.3f)",
                     m.name.c_str(), wsum[0], wsum[1], wsum[2], wsum[3]);
        for (const auto& p : m.bone_palette) {
          std::fprintf(stderr, " %s", p.c_str());
        }
        std::fprintf(stderr, "\n");
      }
    }
  }

  for (auto& kv : impl_->tex)
    if (kv.second) kv.second->Release();
  impl_->tex.clear();
  for (const auto& kv : textures) {
    IDirect3DTexture9* t = upload(kv.second);
    if (t) impl_->tex[kv.first] = t;
  }
  std::fprintf(stderr, "[char3d] uploaded %zu/%zu textures\n", impl_->tex.size(),
               textures.size());
  frame_camera();
}

void CharRenderer::set_attached_prop(
    milo_scene::Scene scene,
    const std::map<std::string, ghogx::asset::Image>& textures,
    const std::string& attach_bone) {
  for (auto& kv : impl_->prop_tex)
    if (kv.second) kv.second->Release();
  impl_->prop_tex.clear();
  impl_->prop_tex_images = textures;
  impl_->logged_prop_texture_alpha_meshes.clear();
  impl_->prop_scene = std::move(scene);
  impl_->prop_mats_by_name.clear();
  for (const auto& mat : impl_->prop_scene.mats) {
    impl_->prop_mats_by_name[mat.name] = &mat;
  }
  impl_->prop_attach_bone = attach_bone;
  impl_->has_prop = true;
  const size_t imported_transform_proxies =
      import_attached_prop_transform_proxies(impl_->character,
                                             impl_->prop_scene);
  reconcile_instrument_anchor(impl_->character, impl_->original_bone_local,
                              impl_->prop_scene, impl_->prop_attach_bone,
                              "bone_fret.mesh");
  reconcile_instrument_anchor(impl_->character, impl_->original_bone_local,
                              impl_->prop_scene, impl_->prop_attach_bone,
                              "bone_fret_hand.mesh");
  reconcile_instrument_fret_targets(impl_->character,
                                    impl_->original_bone_local,
                                    impl_->prop_scene,
                                    impl_->prop_attach_bone);
  for (const auto& kv : textures) {
    IDirect3DTexture9* t = upload(kv.second);
    if (t) impl_->prop_tex[kv.first] = t;
  }
  std::fprintf(stderr, "[char3d] prop '%s' attached to %s, textures %zu/%zu\n",
               impl_->prop_scene.dir_name.c_str(), attach_bone.c_str(),
               impl_->prop_tex.size(), textures.size());
  if (imported_transform_proxies != 0) {
    std::fprintf(stderr,
                 "[char3d] prop transform proxies imported=%zu "
                 "source=shared-objectdir-transform-only\n",
                 imported_transform_proxies);
  }
}

void CharRenderer::frame_camera() {
  impl_->have_bounds = false;
  // Frame the same bind-pose positions the renderer submits. Weighted RndMesh
  // vertices become world-space through source offsets and bind-pose bones;
  // unweighted rows retain their mesh transform. Bounding raw stored vertices
  // instead works accidentally for many GH2 meshes but crops revision-10 GH1
  // bodies whose component geometry is authored around local origins.
  std::vector<std::array<float, 3>> posed;
  std::vector<std::array<float, 3>> normals;
  const SourceCharacterDrawClosure closure =
      source_character_draw_closure(impl_->character, impl_->min_lod);
  for (const auto& m : impl_->character.meshes) {
    if (!m.decoded || !m.showing || m.verts.empty() ||
        (closure.authoritative &&
         closure.meshes.find(m.name) == closure.meshes.end()) ||
        impl_->shadow_meshes.find(m.name) != impl_->shadow_meshes.end() ||
        is_hidden_by_character_lod_selection(impl_->character, m,
                                             impl_->min_lod)) continue;
    skin_to_pose(m, impl_->character, posed, normals);
    const std::array<float, 16> submission =
        source_character_mesh_submission_world(m, impl_->character);
    for (const auto& local : posed) {
      const std::array<float, 3> world = xform_point(submission, local);
      const float p[3] = {world[0], world[1], world[2]};
      if (!impl_->have_bounds) {
        for (int k = 0; k < 3; ++k) impl_->bb_min[k] = impl_->bb_max[k] = p[k];
        impl_->have_bounds = true;
      } else {
        for (int k = 0; k < 3; ++k) {
          impl_->bb_min[k] = std::min(impl_->bb_min[k], p[k]);
          impl_->bb_max[k] = std::max(impl_->bb_max[k], p[k]);
        }
      }
    }
  }
  OrbitCamera& c = impl_->cam;
  if (!impl_->have_bounds) return;
  float ctr[3], ext = 0.0f;
  for (int k = 0; k < 3; ++k) {
    ctr[k] = 0.5f * (impl_->bb_min[k] + impl_->bb_max[k]);
    ext = std::max(ext, impl_->bb_max[k] - impl_->bb_min[k]);
  }
  for (int k = 0; k < 3; ++k) c.target[k] = ctr[k];
  // A character is tall in Z; frame the full height with headroom.
  c.distance = std::max(ext * 1.85f, 5.0f);
  c.near_z = std::max(c.distance * 0.01f, 0.2f);
  c.far_z = c.distance * 6.0f + ext * 2.0f;
  // Face the front, slightly elevated. In this camera convention yaw=pi looks
  // from +Y toward the model, which matches the character's authored facing.
  c.pitch = 0.18f;
  c.yaw = 3.14159265f;
  c.fov = 0.6f;
  impl_->impostor_cam = c;
  std::fprintf(stderr,
               "[char3d] bind-pose extent [%.1f %.1f %.1f]..[%.1f %.1f %.1f] "
               "target=(%.1f %.1f %.1f) dist=%.1f\n",
               impl_->bb_min[0], impl_->bb_min[1], impl_->bb_min[2],
               impl_->bb_max[0], impl_->bb_max[1], impl_->bb_max[2],
               c.target[0], c.target[1], c.target[2], c.distance);
}

void CharRenderer::frame_menu_preview(float aspect) {
  frame_camera();
  if (!impl_->have_bounds) return;
  auto& c = impl_->cam;
  const float width = impl_->bb_max[0] - impl_->bb_min[0];
  const float height = impl_->bb_max[2] - impl_->bb_min[2];
  const float depth = impl_->bb_max[1] - impl_->bb_min[1];
  const float tangent = std::tan(c.fov * 0.5f);
  c.pitch = 0.0f;
  c.distance = std::max(height / (2.0f * tangent * 0.68f),
                        width / (2.0f * tangent * std::max(aspect, 0.1f) * 0.32f))
               + depth * 0.5f;
  c.near_z = 0.2f;
  c.far_z = std::max(5000.0f, c.distance * 6.0f);
  c.eye(c.authored_eye);
  for (int i = 0; i < 3; ++i) c.authored_at[i] = c.target[i];
  c.authored = true;
  c.screen_offset[0] = -0.6f * 768.0f;
  c.screen_offset[1] = 0.0f;
}

void CharRenderer::update(float dt) {
  impl_->anim_t += dt;

  // Restore ALL bones to their bind-pose local transforms. The clip (applied by
  // the caller AFTER update()) then modifies only the bones it animates, starting
  // from a clean bind each frame. No procedural animation here — earlier code
  // applied a bone_spine2 sine-wave sway every frame, which contaminated clip
  // playback (the torso wobbled independently of the clip). Removed.
  for (size_t i = 0; i < impl_->character.bones.size() &&
                     i < impl_->original_bone_local.size(); ++i)
    impl_->character.bones[i].local = impl_->original_bone_local[i];
  for (size_t i = 0; i < impl_->character.meshes.size() &&
                     i < impl_->original_mesh_local.size(); ++i)
    impl_->character.meshes[i].local = impl_->original_mesh_local[i];
  for (auto& [name, proxy] :
       impl_->character.attached_prop_transform_proxies) {
    (void)name;
    proxy.local = proxy.bind_local;
  }
}

void CharRenderer::draw_impl(bool clear_target, uint32_t clear_color) {
  auto& impl = *impl_;
  IDirect3DDevice9* dev = impl.dev;
  if (!dev) return;
  CharEnvFrameCache env_cache;
  ScopedCharEnvFrameCache scoped_env_cache(env_cache);
  CharacterWorldFrameCache world_cache(impl.character);
  Window* win = impl.win;
  OrbitCamera& cam = impl.cam;

  const float backbuffer_aspect =
      win->bb_height() > 0
          ? static_cast<float>(win->bb_width()) /
                static_cast<float>(win->bb_height())
          : 16.0f / 9.0f;
  const float aspect = impl.camera_aspect_override > 0.0f
                           ? impl.camera_aspect_override
                           : char_camera_aspect_preset_or(
                                 "GHOGX_CAMERA_ASPECT", backbuffer_aspect);

  float eye[3];
  cam.eye(eye);
  float result_at[3] = {};
  const float* at = cam.authored ? cam.authored_at : cam.target;
  const float* up = cam.authored ? cam.authored_up : nullptr;
  if (cam.result_frame.valid) {
    for (int k = 0; k < 3; ++k) {
      result_at[k] = cam.result_frame.position[k] +
                     cam.result_frame.forward[k] * 100.0f;
    }
    at = result_at;
    up = cam.result_frame.up;
  }
  Mat4 view = Mat4::look_at_lh(eye[0], eye[1], eye[2], at[0], at[1], at[2],
                               up ? up[0] : 0.0f, up ? up[1] : 0.0f,
                               up ? up[2] : 1.0f);
  Mat4 proj = Mat4::perspective_lh(cam.fov, aspect, cam.near_z, cam.far_z);
  proj.m[0][0] = -proj.m[0][0];  // RH world -> LH clip (no left/right flip)
  if ((cam.authored || cam.result_frame.valid) &&
      !(cam.result_frame.valid && cam.result_frame.screen_offset_consumed) &&
      !char_env_enabled("GHOGX_DISABLE_CAMERA_SCREEN_OFFSET")) {
    constexpr float kScreenOffsetToClip = 1.0f / 768.0f;
    proj.m[2][0] += cam.screen_offset[0] * kScreenOffsetToClip;
    proj.m[2][1] += cam.screen_offset[1] * kScreenOffsetToClip;
  }

  if (clear_target) {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
               static_cast<D3DCOLOR>(clear_color), 1.0f, 0);
  }
  dev->BeginScene();

  D3DMATRIX dv, dp;
  std::memcpy(&dv, &view, 64);
  std::memcpy(&dp, &proj, 64);
  dev->SetTransform(D3DTS_VIEW, &dv);
  dev->SetTransform(D3DTS_PROJECTION, &dp);

  dev->SetFVF(kFVF);
  dev->SetRenderState(D3DRS_ZENABLE, TRUE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  dev->SetRenderState(D3DRS_CULLMODE, character_cull_mode());
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
  dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
  dev->SetRenderState(D3DRS_ALPHAREF, 96);

  // Standalone viewer lighting uses bright ambient plus two opposed directional
  // lights. Venue composites reuse the existing scene light state instead of
  // installing unrelated viewer lights.
  dev->SetRenderState(D3DRS_LIGHTING, TRUE);
  // Venue-authored vertex colors can be nearly black at a particular cue.
  // The proof-light switch must illuminate the existing texture/material,
  // independent of that cue, rather than merely installing brighter lights
  // which are then multiplied by the dark vertex color.
  dev->SetRenderState(D3DRS_COLORVERTEX,
                      impl.proof_lighting ? FALSE : TRUE);
  dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,
                      impl.proof_lighting ? D3DMCS_MATERIAL : D3DMCS_COLOR1);
  dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE,
                      impl.proof_lighting ? D3DMCS_MATERIAL : D3DMCS_COLOR1);
  dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
  dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
  if (!impl.use_scene_lighting) {
    const unsigned char ambient = impl.proof_lighting ? 60 : 150;
    dev->SetRenderState(
        D3DRS_AMBIENT,
        D3DCOLOR_XRGB(ambient, ambient, ambient));
    auto set_light = [&](DWORD i, float x, float y, float z, float b) {
      D3DLIGHT9 l{};
      l.Type = D3DLIGHT_DIRECTIONAL;
      l.Diffuse.r = l.Diffuse.g = l.Diffuse.b = b;
      float ll = std::sqrt(x * x + y * y + z * z);
      l.Direction = {x / ll, y / ll, z / ll};
      dev->SetLight(i, &l);
      dev->LightEnable(i, TRUE);
    };
    set_light(0, 0.3f, -0.6f, -0.5f,
              impl.proof_lighting ? 0.35f : 0.6f);
    set_light(1, -0.4f, 0.6f, -0.3f,
              impl.proof_lighting ? 0.15f : 0.3f);
  }
  ScopedCharacterSceneLightRange scene_light_range(
      dev, impl.use_scene_lighting);
  D3DMATERIAL9 mtrl{};
  mtrl.Diffuse.r = mtrl.Diffuse.g = mtrl.Diffuse.b = mtrl.Diffuse.a = 1.0f;
  mtrl.Ambient.r = mtrl.Ambient.g = mtrl.Ambient.b = mtrl.Ambient.a = 1.0f;
  if (impl.proof_lighting) {
    // A neutral fill card keeps black venue cues from swallowing the subject;
    // the two directional lights above still provide shape and normal detail.
    mtrl.Emissive.r = mtrl.Emissive.g = mtrl.Emissive.b = 0.10f;
  }
  dev->SetMaterial(&mtrl);
  auto set_proof_material = [&](const milo_scene::MatObj* material) {
    if (!impl.proof_lighting) return;
    const float r = material ? material->color[0] : 1.0f;
    const float g = material ? material->color[1] : 1.0f;
    const float b = material ? material->color[2] : 1.0f;
    const float a = material ? material->color[3] : 1.0f;
    D3DMATERIAL9 proof{};
    proof.Diffuse = {r, g, b, a};
    proof.Ambient = {r, g, b, a};
    proof.Emissive = {r * 0.03f, g * 0.03f, b * 0.03f, a};
    dev->SetMaterial(&proof);
  };

  dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

  if (impl.reference_base && impl.have_bounds) {
    const float cx = 0.5f * (impl.bb_min[0] + impl.bb_max[0]);
    const float cy = 0.5f * (impl.bb_min[1] + impl.bb_max[1]);
    const float radius =
        std::max({40.0f, impl.bb_max[0] - impl.bb_min[0],
                  impl.bb_max[1] - impl.bb_min[1]}) *
            0.75f +
        18.0f;
    const float z =
        current_toe_reference_z(impl.character).value_or(impl.bb_min[2]) -
        0.25f;
    const float axis_w = 0.75f;
    const float axis_z = z + 0.05f;
    auto v = [](float x, float y, float z, D3DCOLOR color) {
      return SVtx{x, y, z, 0.0f, 0.0f, 1.0f, color, 0.0f, 0.0f};
    };
    std::vector<SVtx> base;
    base.reserve(18);
    const D3DCOLOR floor_color = D3DCOLOR_ARGB(92, 224, 226, 224);
    const float x0 = cx - radius, x1 = cx + radius;
    const float y0 = cy - radius, y1 = cy + radius;
    base.push_back(v(x0, y0, z, floor_color));
    base.push_back(v(x1, y0, z, floor_color));
    base.push_back(v(x1, y1, z, floor_color));
    base.push_back(v(x0, y0, z, floor_color));
    base.push_back(v(x1, y1, z, floor_color));
    base.push_back(v(x0, y1, z, floor_color));
    const D3DCOLOR x_color = D3DCOLOR_ARGB(220, 236, 70, 70);
    base.push_back(v(x0, cy - axis_w, axis_z, x_color));
    base.push_back(v(x1, cy - axis_w, axis_z, x_color));
    base.push_back(v(x1, cy + axis_w, axis_z, x_color));
    base.push_back(v(x0, cy - axis_w, axis_z, x_color));
    base.push_back(v(x1, cy + axis_w, axis_z, x_color));
    base.push_back(v(x0, cy + axis_w, axis_z, x_color));
    const D3DCOLOR y_color = D3DCOLOR_ARGB(220, 64, 180, 86);
    base.push_back(v(cx - axis_w, y0, axis_z, y_color));
    base.push_back(v(cx + axis_w, y0, axis_z, y_color));
    base.push_back(v(cx + axis_w, y1, axis_z, y_color));
    base.push_back(v(cx - axis_w, y0, axis_z, y_color));
    base.push_back(v(cx + axis_w, y1, axis_z, y_color));
    base.push_back(v(cx - axis_w, y1, axis_z, y_color));

    D3DMATRIX base_world;
    std::memcpy(&base_world, impl.world_transform.data(), 64);
    dev->SetTransform(D3DTS_WORLD, &base_world);
    dev->SetTexture(0, nullptr);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                         static_cast<UINT>(base.size() / 3), base.data(),
                         sizeof(SVtx));
  }

  D3DStateCache d3d_state(dev);
  auto& vb = impl.scratch_vertices;
  auto& spos = impl.scratch_pos;
  auto& snrm = impl.scratch_nrm;
  auto& skin = impl.scratch_skin;
  if (impl.ordered_draw_meshes.empty() && !impl.character.meshes.empty()) {
    build_character_draw_order(impl.character, impl.min_lod,
                               impl.ordered_draw_meshes);
  }
  const auto& draw_meshes = impl.ordered_draw_meshes;
  const std::string& only_mesh_spec = char_env_string_ref("GHOGX_ONLY_MESH");
  const std::string& skip_mesh_spec = char_env_string_ref("GHOGX_SKIP_MESH");
  const std::string& skip_material_spec =
      char_env_string_ref("GHOGX_SKIP_MATERIAL");
  const std::string& highlight_mesh_spec =
      char_env_string_ref("GHOGX_HIGHLIGHT_MESH");
  const std::string& raw_mesh_spec = char_env_string_ref("GHOGX_RAW_MESH");
  const bool hide_eyes = hide_eyes_enabled();
  const bool debug_meshes = debug_meshes_enabled();
  const bool debug_lighting = debug_lighting_enabled();
  const bool debug_skin_bounds = debug_skin_bounds_enabled();
  const bool debug_skin_seams = debug_skin_seams_enabled();
  const bool debug_surface_contact = debug_surface_contact_enabled();
  const bool debug_texture_alpha = debug_texture_alpha_enabled();

  struct DebugSkinSeamSample {
    std::string mesh;
    size_t vertex = 0;
    std::array<float, 3> source = {0, 0, 0};
    std::array<float, 3> posed = {0, 0, 0};
  };
  std::unordered_map<std::string, std::vector<DebugSkinSeamSample>>
      debug_skin_seam_groups;

  for (const SkinnedMesh* mp : draw_meshes) {
    const auto& m = *mp;
    if (!m.decoded || m.verts.empty() || m.indices.empty()) continue;
    if (!m.showing) continue;
    if (impl.hidden_meshes.find(m.name) != impl.hidden_meshes.end()) continue;
    if (!only_mesh_spec.empty() && !comma_spec_matches(only_mesh_spec, m.name))
      continue;
    if (comma_spec_matches(skip_mesh_spec, m.name)) continue;
    if (!skip_material_spec.empty() &&
        m.material.find(skip_material_spec) != std::string::npos)
      continue;
    const bool eye_mesh = impl.eye_meshes.find(m.name) != impl.eye_meshes.end();
    const bool debug_mesh_mode = debug_mesh_mode_enabled(m.name);
    const bool raw_mesh_requested = comma_spec_matches(raw_mesh_spec, m.name);
    if (hide_eyes && eye_mesh) {
      if (debug_meshes)
        std::fprintf(stderr, "[skip-eye] %s\n", m.name.c_str());
      continue;
    }
    if (impl.shadow_meshes.find(m.name) != impl.shadow_meshes.end() ||
        is_hidden_by_character_lod_selection(impl.character, m,
                                             impl.min_lod)) continue;
    const auto material_it = impl.character_mats_by_name.find(m.material);
    const milo_scene::MatObj* material =
        material_it != impl.character_mats_by_name.end()
            ? material_it->second
            : nullptr;
    set_proof_material(material);
    const SourceCharacterMaterialLightingPlan material_lighting =
        source_character_material_lighting_plan(
            impl.use_scene_lighting, material && material->use_environ,
            material && material->prelit);
    const bool hair_point_bone =
        character_mesh_uses_char_hair_point_bone(impl.character, m);
    const DWORD mesh_cull_mode = character_cull_mode(material);
    d3d_state.render(D3DRS_CULLMODE, mesh_cull_mode);
    d3d_state.render(D3DRS_ZWRITEENABLE,
                     material_depth_write_enabled(material) ? TRUE : FALSE);
    d3d_state.render(D3DRS_LIGHTING,
                     material_lighting.fixed_function_lighting ? TRUE : FALSE);
    if (debug_lighting) {
      static std::set<std::string> logged_character_lighting;
      const std::string key = m.name + "|" + m.material + "|" +
                              (impl.use_scene_lighting ? "scene" : "viewer") +
                              "|" +
                              (material_lighting.fixed_function_lighting
                                   ? "fixed_lit"
                                   : "unlit");
      if (logged_character_lighting.insert(key).second) {
        std::fprintf(stderr,
                     "[char3d] performer lighting mesh=%s material=%s "
                     "scene=%d use_env=%d prelit=%d fixed_lit=%d "
                     "color_mod=(%.3f %.3f %.3f %.3f)\n",
                     m.name.c_str(), m.material.c_str(),
                     impl.use_scene_lighting ? 1 : 0,
                     material && material->use_environ ? 1 : 0,
                     material && material->prelit ? 1 : 0,
                     material_lighting.fixed_function_lighting ? 1 : 0,
                     impl.color_mod[0],
                     impl.color_mod[1], impl.color_mod[2],
                     impl.color_mod[3]);
      }
    }

    const char* world_mode = m.bone_palette.empty()
                                 ? "source-trans-world"
                                 : "identity-source-skinned";
    std::array<float, 16> mw =
        source_character_mesh_submission_world(m, impl.character);
    mw = mul16(mw, impl.world_transform);
    const bool needs_pose_vectors =
        debug_mesh_mode || debug_skin_bounds ||
        (debug_surface_contact && debug_mesh_mode);
    const bool raw_pose =
        (raw_mesh_requested ||
         !source_character_mesh_renders_decoded_skinning(m)) &&
        !needs_pose_vectors;
    if (raw_pose) {
      spos.clear();
      snrm.clear();
    } else {
      skin_to_pose_impl(m, impl.character, world_cache, spos, snrm, skin);
    }
    if (debug_face_rows_enabled() && (is_mouth_probe_mesh(m) || eye_mesh) &&
        !spos.empty()) {
      auto log_candidate = [&](const char* candidate,
                               std::array<float, 16> row) {
        row = mul16(row, impl.world_transform);
        Bounds3 b;
        for (const auto& p : spos) add_bounds(b, transform_point(p, row));
        std::fprintf(
            stderr,
            "[face-row-candidate] mesh=%s parent=%s mat=%s candidate=%s "
            "pos=(%.3f %.3f %.3f) bbox=(%.3f %.3f %.3f)..(%.3f %.3f %.3f) "
            "rows=[%.3f %.3f %.3f|%.3f %.3f %.3f|%.3f %.3f %.3f]\n",
            m.name.c_str(), m.parent.c_str(), m.material.c_str(), candidate,
            row[12], row[13], row[14], b.mn[0], b.mn[1], b.mn[2], b.mx[0],
            b.mx[1], b.mx[2], row[0], row[1], row[2], row[4], row[5],
            row[6], row[8], row[9], row[10]);
      };
      log_candidate("source-world", impl.character.mesh_world(m));
      log_candidate("source-bind-world",
                    impl.character.bone_world_bind_local_chain(m.name));
      log_candidate("stored-world", xfm16(m.world_stored));
      if (!m.parent.empty()) {
        log_candidate("parent-source-world",
                      impl.character.bone_world_local_chain(m.parent));
        log_candidate("local-parent-source-world",
                      mul16(xfm16(m.local),
                            impl.character.bone_world_local_chain(m.parent)));
        log_candidate("local-parent-bind-world",
                      mul16(xfm16(m.local),
                            impl.character.bone_world_bind_local_chain(
                                m.parent)));
      }
    }
    if (debug_mesh_mode) {
      std::fprintf(stderr,
                   "[mesh-mode] char=%s mesh=%-24s parent=%-18s "
                   "mat=%-18s palette=%zu "
                   "world=%s constraint=%u target=%s raw=%d "
                   "pos=(%.3f %.3f %.3f) "
                   "rows=[%.3f %.3f %.3f|%.3f %.3f %.3f|"
                   "%.3f %.3f %.3f]\n",
                   impl.character.dir_name.c_str(), m.name.c_str(),
                   m.parent.c_str(), m.material.c_str(),
                   m.bone_palette.size(), world_mode,
                   m.constraint, m.target.empty() ? "<none>" : m.target.c_str(),
                   raw_mesh_requested ? 1 : 0, mw[12], mw[13], mw[14],
                   mw[0], mw[1], mw[2], mw[4], mw[5], mw[6],
                   mw[8], mw[9], mw[10]);
      if (!spos.empty()) {
        size_t min_z_i = 0;
        size_t max_z_i = 0;
        std::array<float, 3> mn = transform_point(spos[0], mw);
        std::array<float, 3> mx = mn;
        for (size_t vi = 1; vi < spos.size(); ++vi) {
          const auto wp = transform_point(spos[vi], mw);
          if (wp[2] < transform_point(spos[min_z_i], mw)[2]) min_z_i = vi;
          if (wp[2] > transform_point(spos[max_z_i], mw)[2]) max_z_i = vi;
          mn[0] = std::min(mn[0], wp[0]);
          mn[1] = std::min(mn[1], wp[1]);
          mn[2] = std::min(mn[2], wp[2]);
          mx[0] = std::max(mx[0], wp[0]);
          mx[1] = std::max(mx[1], wp[1]);
          mx[2] = std::max(mx[2], wp[2]);
        }
        const auto v0 = transform_point(spos[0], mw);
        const auto min_z = transform_point(spos[min_z_i], mw);
        const auto max_z = transform_point(spos[max_z_i], mw);
        std::fprintf(stderr,
                     "[mesh-world-verts] mesh=%s v0=(%.4f %.4f %.4f) "
                     "bbox=(%.4f %.4f %.4f)..(%.4f %.4f %.4f) "
                     "minZ_i=%zu minZ=(%.4f %.4f %.4f) "
                     "maxZ_i=%zu maxZ=(%.4f %.4f %.4f)\n",
                     m.name.c_str(), v0[0], v0[1], v0[2], mn[0], mn[1],
                     mn[2], mx[0], mx[1], mx[2], min_z_i, min_z[0],
                     min_z[1], min_z[2], max_z_i, max_z[0], max_z[1],
                     max_z[2]);
      }
    }
    if (eye_mesh && debug_skin_bounds) {
      std::fprintf(stderr,
                   "[eye-world] %-12s parent=%-16s pos=(%.3f %.3f %.3f) "
                   "rows=[%.3f %.3f %.3f|%.3f %.3f %.3f|%.3f %.3f %.3f]\n",
                   m.name.c_str(), m.parent.c_str(), mw[12], mw[13], mw[14],
                   mw[0], mw[1], mw[2], mw[4], mw[5], mw[6], mw[8], mw[9],
                   mw[10]);
    }
    if (debug_skin_bounds && !spos.empty()) {
      float mn[3] = {0, 0, 0};
      float mx[3] = {0, 0, 0};
      for (size_t vi = 0; vi < spos.size(); ++vi) {
        const auto& p = spos[vi];
        const float x = p[0] * mw[0] + p[1] * mw[4] + p[2] * mw[8] + mw[12];
        const float y = p[0] * mw[1] + p[1] * mw[5] + p[2] * mw[9] + mw[13];
        const float z = p[0] * mw[2] + p[1] * mw[6] + p[2] * mw[10] + mw[14];
        if (vi == 0) {
          mn[0] = mx[0] = x;
          mn[1] = mx[1] = y;
          mn[2] = mx[2] = z;
        } else {
          mn[0] = std::min(mn[0], x); mx[0] = std::max(mx[0], x);
          mn[1] = std::min(mn[1], y); mx[1] = std::max(mx[1], y);
          mn[2] = std::min(mn[2], z); mx[2] = std::max(mx[2], z);
        }
      }
      float maximum_face_edge = 0.0f;
      size_t maximum_face_index = 0;
      std::array<uint16_t, 3> maximum_face_vertices = {0, 0, 0};
      size_t faces_with_edge_over_5 = 0;
      size_t faces_with_edge_over_10 = 0;
      for (size_t index = 0; index + 2 < m.indices.size(); index += 3) {
        float face_maximum_edge = 0.0f;
        for (size_t corner = 0; corner < 3; ++corner) {
          const uint16_t a_index = m.indices[index + corner];
          const uint16_t b_index = m.indices[index + (corner + 1) % 3];
          if (a_index >= spos.size() || b_index >= spos.size()) continue;
          float length_squared = 0.0f;
          for (size_t axis = 0; axis < 3; ++axis) {
            const float delta = spos[a_index][axis] - spos[b_index][axis];
            length_squared += delta * delta;
          }
          face_maximum_edge =
              std::max(face_maximum_edge, std::sqrt(length_squared));
        }
        if (face_maximum_edge > maximum_face_edge) {
          maximum_face_edge = face_maximum_edge;
          maximum_face_index = index / 3;
          maximum_face_vertices = {m.indices[index], m.indices[index + 1],
                                   m.indices[index + 2]};
        }
        if (face_maximum_edge > 5.0f) ++faces_with_edge_over_5;
        if (face_maximum_edge > 10.0f) ++faces_with_edge_over_10;
      }
      std::fprintf(stderr,
                   "[skin-bounds] %-24s mat=%-18s verts=%zu "
                   "bbox=(%.2f %.2f %.2f)..(%.2f %.2f %.2f) "
                   "maxFaceEdge=%.3f faceEdgeGt5=%zu faceEdgeGt10=%zu\n",
                   m.name.c_str(), m.material.c_str(), spos.size(), mn[0],
                   mn[1], mn[2], mx[0], mx[1], mx[2], maximum_face_edge,
                   faces_with_edge_over_5, faces_with_edge_over_10);
      if (maximum_face_edge > 10.0f) {
        auto palette_name = [&](size_t slot) -> const char* {
          return slot < m.bone_palette.size()
                     ? m.bone_palette[slot].c_str()
                     : "<none>";
        };
        std::fprintf(stderr,
                     "[skin-max-face] mesh=%s face=%zu edge=%.3f "
                     "indices=(%u %u %u) palette=(%s|%s|%s|%s)\n",
                     m.name.c_str(), maximum_face_index, maximum_face_edge,
                     maximum_face_vertices[0], maximum_face_vertices[1],
                     maximum_face_vertices[2], palette_name(0),
                     palette_name(1), palette_name(2), palette_name(3));
        for (size_t corner = 0; corner < maximum_face_vertices.size();
             ++corner) {
          const size_t vertex_index = maximum_face_vertices[corner];
          if (vertex_index >= m.verts.size() || vertex_index >= spos.size())
            continue;
          const auto& source = m.verts[vertex_index];
          const auto& posed = spos[vertex_index];
          std::fprintf(stderr,
                       "[skin-max-vertex] mesh=%s face=%zu corner=%zu "
                       "vertex=%zu source=(%.4f %.4f %.4f) "
                       "posed=(%.4f %.4f %.4f) "
                       "weights=(%.6f %.6f %.6f %.6f)\n",
                       m.name.c_str(), maximum_face_index, corner,
                       vertex_index, source.px, source.py, source.pz,
                       posed[0], posed[1], posed[2], source.w[0], source.w[1],
                       source.w[2], source.w[3]);
        }
      }
    }
    if (debug_skin_seams && !spos.empty()) {
      for (size_t vertex_index = 0; vertex_index < m.verts.size() &&
                                    vertex_index < spos.size();
           ++vertex_index) {
        const auto& source = m.verts[vertex_index];
        uint32_t source_bits[3] = {0, 0, 0};
        std::memcpy(&source_bits[0], &source.px, sizeof(uint32_t));
        std::memcpy(&source_bits[1], &source.py, sizeof(uint32_t));
        std::memcpy(&source_bits[2], &source.pz, sizeof(uint32_t));
        char key_suffix[32] = {};
        std::snprintf(key_suffix, sizeof(key_suffix), "%08x%08x%08x",
                      source_bits[0], source_bits[1], source_bits[2]);
        auto posed_world = transform_point(spos[vertex_index], mw);
        debug_skin_seam_groups[m.material + "|" + key_suffix].push_back(
            {m.name, vertex_index, {source.px, source.py, source.pz},
             posed_world});
      }
    }
    if (debug_surface_contact && debug_mesh_mode &&
        !spos.empty()) {
      auto log_ref_space = [&](const char* ref_name) {
        const int ref_i = character_bone_index(impl.character, ref_name);
        if (ref_i < 0) return;
        auto ref_world = impl.character.bone_world_local_chain(ref_name);
        ref_world = mul16(ref_world, impl.world_transform);
        Bounds3 b;
        size_t max_front_i = 0;
        std::array<float, 3> max_front{0, 0, 0};
        for (size_t vi = 0; vi < spos.size(); ++vi) {
          const auto world = xform_point(mw, spos[vi]);
          const auto d = basis_delta(ref_world, world);
          add_bounds(b, d);
          if (vi == 0 || d[2] > max_front[2]) {
            max_front_i = vi;
            max_front = d;
          }
        }
        const SkinVertex& v = m.verts[max_front_i];
        std::fprintf(stderr,
                     "[surface-contact] mesh=%s ref=%s verts=%zu "
                     "localBounds=(%.4f %.4f %.4f)..(%.4f %.4f %.4f) "
                     "maxFront_i=%zu local=(%.4f %.4f %.4f) "
                     "raw=(%.4f %.4f %.4f) weights=(%.4f %.4f %.4f %.4f)\n",
                     m.name.c_str(), ref_name, spos.size(), b.mn[0], b.mn[1],
                     b.mn[2], b.mx[0], b.mx[1], b.mx[2], max_front_i,
                     max_front[0], max_front[1], max_front[2], v.px, v.py,
                     v.pz, v.w[0], v.w[1], v.w[2], v.w[3]);
      };
      log_ref_space("bone_fret_hand.mesh");
      log_ref_space("bone_fret.mesh");

      if (impl.has_prop && !impl.prop_attach_bone.empty()) {
        const auto attach_world =
            prop_attach_world(impl.character, impl.prop_attach_bone);
        const auto prop_anchor_world =
            scene_object_world(impl.prop_scene, impl.prop_attach_bone);
        const auto prop_to_attach =
            mul16(affine_inverse(prop_anchor_world), attach_world);
        for (const auto& pm : impl.prop_scene.meshes) {
          if (!pm.decoded || pm.verts.empty()) continue;
          auto prop_world =
              mul16(scene_object_world(impl.prop_scene, pm.name),
                    prop_to_attach);
          prop_world = mul16(prop_world, impl.world_transform);
          const auto inv_prop_world = affine_inverse(prop_world);
          float best_d2 = 0.0f;
          size_t best_i = 0;
          int inside_count = 0;
          std::array<float, 3> best_local{0, 0, 0};
          const bool tri_contact = !pm.indices.empty();
          size_t tri_samples = 0;
          float min_dist = std::numeric_limits<float>::infinity();
          float max_dist = 0.0f;
          float sum_dist = 0.0f;
          float min_signed = std::numeric_limits<float>::infinity();
          float max_signed = -std::numeric_limits<float>::infinity();
          int negative_signed = 0;
          size_t nearest_tri_i = 0;
          size_t nearest_tri = 0;
          size_t min_signed_i = 0;
          size_t min_signed_tri = 0;
          std::array<float, 3> nearest_tri_local{0, 0, 0};
          std::array<float, 3> nearest_tri_closest{0, 0, 0};
          std::array<float, 3> nearest_tri_normal{0, 0, 0};
          std::array<float, 3> min_signed_local{0, 0, 0};
          std::array<float, 3> min_signed_closest{0, 0, 0};
          std::array<float, 3> min_signed_normal{0, 0, 0};
          for (size_t vi = 0; vi < spos.size(); ++vi) {
            const auto world = xform_point(mw, spos[vi]);
            const auto local = xform_point(inv_prop_world, world);
            const float d2 = aabb_distance_sq(pm, local);
            if (vi == 0 || d2 < best_d2) {
              best_d2 = d2;
              best_i = vi;
              best_local = local;
            }
            if (d2 == 0.0f) ++inside_count;
            if (tri_contact) {
              const auto hit = closest_mesh_surface(pm, local);
              if (hit.valid) {
                const float dist = std::sqrt(hit.dist_sq);
                if (tri_samples == 0 || dist < min_dist) {
                  min_dist = dist;
                  nearest_tri_i = vi;
                  nearest_tri = hit.tri;
                  nearest_tri_local = local;
                  nearest_tri_closest = hit.closest;
                  nearest_tri_normal = hit.normal;
                }
                max_dist = std::max(max_dist, dist);
                sum_dist += dist;
                if (tri_samples == 0 || hit.signed_dist < min_signed) {
                  min_signed = hit.signed_dist;
                  min_signed_i = vi;
                  min_signed_tri = hit.tri;
                  min_signed_local = local;
                  min_signed_closest = hit.closest;
                  min_signed_normal = hit.normal;
                }
                max_signed = std::max(max_signed, hit.signed_dist);
                if (hit.signed_dist < -1.0e-4f) ++negative_signed;
                ++tri_samples;
              }
            }
          }
          const SkinVertex& v = m.verts[best_i];
          std::fprintf(stderr,
                       "[surface-prop] mesh=%s prop=%s mat=%s verts=%zu "
                       "inside=%d nearest_i=%zu dist=%.4f "
                       "propLocal=(%.4f %.4f %.4f) "
                       "propBBox=(%.4f %.4f %.4f)..(%.4f %.4f %.4f) "
                       "raw=(%.4f %.4f %.4f) weights=(%.4f %.4f %.4f %.4f)\n",
                       m.name.c_str(), pm.name.c_str(), pm.material.c_str(),
                       spos.size(), inside_count, best_i, std::sqrt(best_d2),
                       best_local[0], best_local[1], best_local[2],
                       pm.bb_min[0], pm.bb_min[1], pm.bb_min[2],
                       pm.bb_max[0], pm.bb_max[1], pm.bb_max[2], v.px, v.py,
                       v.pz, v.w[0], v.w[1], v.w[2], v.w[3]);
          if (tri_contact && tri_samples > 0) {
            const SkinVertex& nearest_v = m.verts[nearest_tri_i];
            const SkinVertex& signed_v = m.verts[min_signed_i];
            std::fprintf(
                stderr,
                "[surface-prop-tri] mesh=%s prop=%s verts=%zu tris=%zu "
                "minDist=%.4f meanDist=%.4f maxDist=%.4f "
                "minSigned=%.4f maxSigned=%.4f negSigned=%d "
                "nearest_i=%zu tri=%zu local=(%.4f %.4f %.4f) "
                "closest=(%.4f %.4f %.4f) normal=(%.4f %.4f %.4f) "
                "raw=(%.4f %.4f %.4f) weights=(%.4f %.4f %.4f %.4f) "
                "minSigned_i=%zu minSignedTri=%zu "
                "minSignedLocal=(%.4f %.4f %.4f) "
                "minSignedClosest=(%.4f %.4f %.4f) "
                "minSignedNormal=(%.4f %.4f %.4f) "
                "minSignedRaw=(%.4f %.4f %.4f) "
                "minSignedWeights=(%.4f %.4f %.4f %.4f)\n",
                m.name.c_str(), pm.name.c_str(), spos.size(),
                pm.indices.size() / 3, min_dist,
                sum_dist / static_cast<float>(tri_samples), max_dist,
                min_signed, max_signed, negative_signed, nearest_tri_i,
                nearest_tri, nearest_tri_local[0], nearest_tri_local[1],
                nearest_tri_local[2], nearest_tri_closest[0],
                nearest_tri_closest[1], nearest_tri_closest[2],
                nearest_tri_normal[0], nearest_tri_normal[1],
                nearest_tri_normal[2], nearest_v.px, nearest_v.py,
                nearest_v.pz, nearest_v.w[0], nearest_v.w[1], nearest_v.w[2],
                nearest_v.w[3], min_signed_i, min_signed_tri,
                min_signed_local[0], min_signed_local[1],
                min_signed_local[2], min_signed_closest[0],
                min_signed_closest[1], min_signed_closest[2],
                min_signed_normal[0], min_signed_normal[1],
                min_signed_normal[2], signed_v.px, signed_v.py, signed_v.pz,
                signed_v.w[0], signed_v.w[1], signed_v.w[2], signed_v.w[3]);
          }
        }
      }
    }
    D3DMATRIX dm{}; std::memcpy(&dm, mw.data(), 64);
    dev->SetTransform(D3DTS_WORLD, &dm);

    auto color_byte = [](float f) -> int {
      int i = static_cast<int>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
      return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    const bool highlight_mesh = comma_spec_matches(highlight_mesh_spec, m.name);
    if (highlight_mesh) {
      // Diagnostic highlight must remain visible even when the source normal
      // transform or venue light state is the subject under test.
      d3d_state.render(D3DRS_LIGHTING, FALSE);
    }
    uint8_t material_blend =
        material ? material->blend : static_cast<uint8_t>(kBlendSrcAlpha);
    if (highlight_mesh) material_blend = kBlendSrc;
    const BlendState blend_state = character_blend_state_for(material_blend);
    const bool use_source_alpha =
        material && material->has_render_state &&
        source_material_alpha_state_enabled();
    const bool alpha_test = use_source_alpha ? material->alpha_cut : true;
    const DWORD alpha_ref =
        use_source_alpha
            ? static_cast<DWORD>(
                  std::clamp(material->alpha_threshold, 0, 255))
            : 96u;
    const bool depth_write = material_depth_write_enabled(material);
    d3d_state.render(D3DRS_BLENDOP, blend_state.op);
    d3d_state.render(D3DRS_SRCBLEND, blend_state.src);
    d3d_state.render(D3DRS_DESTBLEND, blend_state.dest);
    d3d_state.render(D3DRS_ZWRITEENABLE, depth_write ? TRUE : FALSE);
    d3d_state.render(D3DRS_ALPHATESTENABLE, alpha_test ? TRUE : FALSE);
    d3d_state.render(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    d3d_state.render(D3DRS_ALPHAREF, alpha_ref);
    const DWORD tex_address =
        material && material->has_render_state
            ? texture_address_for_wrap(material->tex_wrap)
            : D3DTADDRESS_WRAP;
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, tex_address);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, tex_address);
    if (debug_mesh_mode) {
      const int group_rank = character_active_lod_group_rank(
          impl.character, m.name, impl.min_lod);
      std::fprintf(stderr,
                   "[mesh-render] %-24s mat=%-18s blend=%d "
                   "zwrite=%d ngCull=%d cullMode=%lu src=%lu dst=%lu "
                   "op=%lu drawOrder=%.3f groupRank=%d alphaTest=%d alphaCut=%d "
                   "alphaRef=%lu zMode=%u texWrap=%u hairPointBone=%d\n",
                   m.name.c_str(), m.material.c_str(),
                    static_cast<int>(material_blend),
                    depth_write ? 1 : 0,
                   material && material->has_cull ? (material->cull ? 1 : 0)
                                                   : -1,
                   static_cast<unsigned long>(mesh_cull_mode),
                   static_cast<unsigned long>(blend_state.src),
                   static_cast<unsigned long>(blend_state.dest),
                   static_cast<unsigned long>(blend_state.op),
                   m.draw_order, group_rank, alpha_test ? 1 : 0,
                   material && material->has_render_state &&
                           material->alpha_cut
                       ? 1
                       : 0,
                   static_cast<unsigned long>(alpha_ref),
                   material && material->has_render_state
                       ? static_cast<unsigned>(material->z_mode)
                       : 0,
                    material && material->has_render_state
                        ? static_cast<unsigned>(material->tex_wrap)
                        : 1,
                    hair_point_bone ? 1 : 0);
    }
    float mesh_alpha =
        material ? material->color[3] * impl.color_mod[3] : impl.color_mod[3];
    float mesh_r =
        material ? material->color[0] * impl.color_mod[0] : impl.color_mod[0];
    float mesh_g =
        material ? material->color[1] * impl.color_mod[1] : impl.color_mod[1];
    float mesh_b =
        material ? material->color[2] * impl.color_mod[2] : impl.color_mod[2];
    if (material_blend == kBlendAdd && mesh_alpha < 0.999f) {
      // ONE/ONE additive blending ignores vertex alpha; GH2 Mat alpha acts as
      // intensity for faded additive surfaces in the general MILO path.
      mesh_r *= mesh_alpha;
      mesh_g *= mesh_alpha;
      mesh_b *= mesh_alpha;
      mesh_alpha = 1.0f;
    }
    const D3DCOLOR mesh_color =
        highlight_mesh
            ? D3DCOLOR_ARGB(255, 255, 0, 255)
            : D3DCOLOR_ARGB(color_byte(mesh_alpha), color_byte(mesh_r),
                             color_byte(mesh_g), color_byte(mesh_b));

    IDirect3DTexture9* texture = nullptr;
    const ghogx::asset::Image* texture_image = nullptr;
    if (material && !highlight_mesh) {
      const auto* mat = material;
      auto it = impl.tex.find(mat->diffuse_tex);
      if (it != impl.tex.end()) texture = it->second;
      auto image_it = impl.tex_images.find(mat->diffuse_tex);
      if (image_it != impl.tex_images.end()) texture_image = &image_it->second;
    }
    if (debug_texture_alpha &&
        impl.logged_texture_alpha_meshes.insert(m.name).second) {
      log_vertex_float_stats(m);
      log_texture_alpha_stats(m, material, texture_image);
    }
    if (texture) {
      d3d_state.texture(0, texture);
      d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
      d3d_state.texture_stage(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      d3d_state.texture_stage(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
      d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      d3d_state.texture_stage(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    } else {
      d3d_state.texture(0, nullptr);
      d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
      d3d_state.texture_stage(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
      d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
      d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    }

    const bool use_vertex_color =
        m.bone_palette.empty() && !highlight_mesh;
    if (raw_pose) {
      append_raw_pose_vertices(m, mesh_color, use_vertex_color, vb);
    } else {
      append_skinned_pose_vertices(
          m, spos, snrm, mesh_color, use_vertex_color, vb);
    }
    const HRESULT draw_result = dev->DrawIndexedPrimitiveUP(
        D3DPT_TRIANGLELIST, 0, static_cast<UINT>(m.verts.size()),
        static_cast<UINT>(m.indices.size() / 3), m.indices.data(),
        D3DFMT_INDEX16, vb.data(), sizeof(SVtx));
    if (debug_mesh_mode && FAILED(draw_result)) {
      std::fprintf(stderr,
                   "[mesh-draw-failed] mesh=%s hr=0x%08lx verts=%zu "
                   "indices=%zu faces=%zu\n",
                   m.name.c_str(), static_cast<unsigned long>(draw_result),
                   m.verts.size(), m.indices.size(), m.indices.size() / 3);
    }
  }
  if (debug_skin_seams) {
    size_t cross_mesh_groups = 0;
    size_t cross_mesh_pairs = 0;
    size_t pairs_over_005 = 0;
    size_t pairs_over_025 = 0;
    size_t pairs_over_1 = 0;
    float maximum_separation = 0.0f;
    DebugSkinSeamSample worst_a;
    DebugSkinSeamSample worst_b;
    for (const auto& [key, samples] : debug_skin_seam_groups) {
      (void)key;
      bool group_crosses_meshes = false;
      for (size_t a_index = 0; a_index < samples.size(); ++a_index) {
        for (size_t b_index = a_index + 1; b_index < samples.size();
             ++b_index) {
          const auto& a = samples[a_index];
          const auto& b = samples[b_index];
          if (a.mesh == b.mesh) continue;
          group_crosses_meshes = true;
          float separation_squared = 0.0f;
          for (size_t axis = 0; axis < 3; ++axis) {
            const float delta = a.posed[axis] - b.posed[axis];
            separation_squared += delta * delta;
          }
          const float separation = std::sqrt(separation_squared);
          ++cross_mesh_pairs;
          if (separation > 0.05f) ++pairs_over_005;
          if (separation > 0.25f) ++pairs_over_025;
          if (separation > 1.0f) ++pairs_over_1;
          if (separation > maximum_separation) {
            maximum_separation = separation;
            worst_a = a;
            worst_b = b;
          }
        }
      }
      if (group_crosses_meshes) ++cross_mesh_groups;
    }
    std::fprintf(
        stderr,
        "[skin-seams] groups=%zu pairs=%zu gt0.05=%zu gt0.25=%zu "
        "gt1=%zu max=%.6f source=(%.4f %.4f %.4f) "
        "a=%s:%zu posed=(%.4f %.4f %.4f) "
        "b=%s:%zu posed=(%.4f %.4f %.4f)\n",
        cross_mesh_groups, cross_mesh_pairs, pairs_over_005,
        pairs_over_025, pairs_over_1, maximum_separation,
        worst_a.source[0], worst_a.source[1], worst_a.source[2],
        worst_a.mesh.empty() ? "<none>" : worst_a.mesh.c_str(),
        worst_a.vertex, worst_a.posed[0], worst_a.posed[1],
        worst_a.posed[2],
        worst_b.mesh.empty() ? "<none>" : worst_b.mesh.c_str(),
        worst_b.vertex, worst_b.posed[0], worst_b.posed[1],
        worst_b.posed[2]);
  }
  d3d_state.render(D3DRS_ZWRITEENABLE, TRUE);
  d3d_state.render(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  d3d_state.render(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  d3d_state.render(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

  if (impl.has_prop && !impl.prop_attach_bone.empty() &&
      !hide_attached_props_enabled()) {
    // Most instrument prop surfaces are opaque, but the authored string card is
    // a texture-alpha cutout and must keep its diffuse texture alpha.
    d3d_state.render(D3DRS_CULLMODE, D3DCULL_CW);
    d3d_state.render(D3DRS_LIGHTING, TRUE);
    d3d_state.render(D3DRS_ALPHATESTENABLE, FALSE);
    d3d_state.render(D3DRS_ALPHABLENDENABLE, FALSE);
    d3d_state.render(D3DRS_ZWRITEENABLE, TRUE);
    d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    const auto attach_world =
        prop_attach_world(impl.character, impl.prop_attach_bone);
    const auto prop_anchor_world =
        scene_object_world(impl.prop_scene, impl.prop_attach_bone);
    const auto prop_to_attach =
        mul16(affine_inverse(prop_anchor_world), attach_world);
    if (debug_prop_enabled() && !impl.logged_prop_debug) {
      impl.logged_prop_debug = true;
      auto log_prop_matrix =
          [&](const char* kind, const std::array<float, 16>& matrix) {
            std::fprintf(
                stderr,
                "[prop-matrix] kind=%s attach=%s "
                "r0=(%.6f %.6f %.6f) r1=(%.6f %.6f %.6f) "
                "r2=(%.6f %.6f %.6f) pos=(%.6f %.6f %.6f)\n",
                kind, impl.prop_attach_bone.c_str(),
                matrix[0], matrix[1], matrix[2],
                matrix[4], matrix[5], matrix[6],
                matrix[8], matrix[9], matrix[10],
                matrix[12], matrix[13], matrix[14]);
          };
      std::fprintf(stderr,
                   "[prop] attach %s pos=(%.3f %.3f %.3f) "
                   "anchor=(%.3f %.3f %.3f) delta=(%.3f %.3f %.3f)\n",
                   impl.prop_attach_bone.c_str(), attach_world[12],
                   attach_world[13], attach_world[14],
                   prop_anchor_world[12], prop_anchor_world[13],
                   prop_anchor_world[14], prop_to_attach[12],
                   prop_to_attach[13], prop_to_attach[14]);
      log_prop_matrix("attach-world", attach_world);
      log_prop_matrix("prop-anchor-world", prop_anchor_world);
      log_prop_matrix("prop-to-attach", prop_to_attach);
      for (const auto& target : impl.character.meshes) {
        if (target.name != impl.prop_attach_bone) continue;
        log_prop_matrix("target-local", xfm16(target.local));
        if (!target.parent.empty()) {
          log_prop_matrix(
              "target-parent-world",
              impl.character.bone_world_local_chain(target.parent));
        }
        break;
      }
      for (const auto& transform : impl.character.meshes) {
        if (!debug_mesh_mode_enabled(transform.name)) continue;
        const std::string local_kind =
            "debug-local:" + transform.name;
        const std::string world_kind =
            "debug-world:" + transform.name;
        log_prop_matrix(local_kind.c_str(), xfm16(transform.local));
        log_prop_matrix(
            world_kind.c_str(),
            impl.character.bone_world_local_chain(transform.name));
      }
      static constexpr const char* kPropDebugObjects[] = {
           "bone_pos_guitar.mesh",
           "bone_fret.mesh",
           "bone_fret_hand.mesh",
           "bone_strum.mesh",
           "bone_strum_hand.mesh",
           "guitar.mesh",
          "guitar_detail01.mesh",
          "guitar_detail02.mesh",
          "guitar_detail03.mesh",
          "guitar_detail04.mesh",
          "guitar_strings.mesh",
          "shadow_guitar.mesh",
      };
      for (const char* object_name : kPropDebugObjects) {
        const auto stored_world =
            scene_object_stored_world(impl.prop_scene, object_name);
        if (!stored_world) continue;
         const auto composed_world =
             scene_object_world(impl.prop_scene, object_name);
         const auto anchor_local =
             mul16(composed_world, affine_inverse(prop_anchor_world));
         const auto composed_char = mul16(composed_world, prop_to_attach);
        const auto stored_char = mul16(*stored_world, prop_to_attach);
        std::fprintf(stderr,
                     "[prop-rel] obj=%s comp=(%.3f %.3f %.3f)\n",
                     object_name, composed_world[12], composed_world[13],
                     composed_world[14]);
        std::fprintf(stderr,
                     "[prop-rel] obj=%s stored=(%.3f %.3f %.3f)\n",
                     object_name, (*stored_world)[12], (*stored_world)[13],
                     (*stored_world)[14]);
         std::fprintf(stderr,
                      "[prop-rel] obj=%s char_comp=(%.3f %.3f %.3f)\n",
                      object_name, composed_char[12], composed_char[13],
                      composed_char[14]);
         std::fprintf(stderr,
                      "[prop-anchor-local] obj=%s pos=(%.6f %.6f %.6f)\n",
                      object_name, anchor_local[12], anchor_local[13],
                      anchor_local[14]);
        std::fprintf(stderr,
                     "[prop-rel] obj=%s char_stored=(%.3f %.3f %.3f)\n",
                     object_name, stored_char[12], stored_char[13],
                     stored_char[14]);
      }
    }
    D3DMATRIX wm;
    auto color_byte = [](float f) -> int {
      int i = static_cast<int>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
      return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    std::vector<const milo_scene::MeshObj*> prop_draw_meshes;
    build_prop_draw_order(impl.prop_scene, prop_draw_meshes);
    for (const milo_scene::MeshObj* prop_mesh : prop_draw_meshes) {
      const auto& m = *prop_mesh;
      if (!m.decoded || m.vertex_count == 0 || m.face_count == 0) continue;
      // These authored helper/effect meshes rely on the venue's special
      // shader path. They obscure the instrument when submitted through the
      // neutral fixed-function proof light, so omit them only for this
      // diagnostic view.
      if (impl.proof_lighting &&
          (m.name == "shadow_guitar.mesh" ||
           m.name == "guitar_fire.mesh")) {
        continue;
      }
      // Prop ObjectDirs commonly terminate their transform chain at a
      // Character root object (for example "guitar_sg") that is not itself a
      // serialized RndTransformable.  Scene::world_matrix therefore falls
      // back to a child mesh's stale stored-world cache when it cannot resolve
      // that final parent.  Compose the known local chain here, as the
      // attached-prop camera/anchor path already does, so multipart children
      // remain rigidly aligned with guitar.mesh.
      auto local_world = scene_object_world(impl.prop_scene, m.name);
      if (debug_prop_enabled()) {
        static std::set<std::string> logged_prop_mesh_worlds;
        const std::string key = impl.prop_scene.dir_name + "|" +
                                impl.prop_attach_bone + "|" + m.name;
        if (logged_prop_mesh_worlds.insert(key).second) {
          std::fprintf(
              stderr,
              "[prop-mesh-world] mesh=%s parent=%s "
              "local=(%.3f %.3f %.3f) stored=(%.3f %.3f %.3f) "
              "resolved=(%.3f %.3f %.3f)\n",
              m.name.c_str(), m.parent.c_str(),
              m.local.pos[0], m.local.pos[1], m.local.pos[2],
              m.world_stored.pos[0], m.world_stored.pos[1],
              m.world_stored.pos[2], local_world[12], local_world[13],
              local_world[14]);
        }
      }
      auto character_world = mul16(local_world, prop_to_attach);
      auto world = mul16(character_world, impl.world_transform);
      if (debug_prop_enabled() && impl.logged_prop_debug) {
        std::fprintf(stderr,
                     "[prop] mesh %s local=(%.3f %.3f %.3f) "
                     "char=(%.3f %.3f %.3f) world=(%.3f %.3f %.3f)\n",
                     m.name.c_str(), local_world[12], local_world[13],
                     local_world[14], character_world[12], character_world[13],
                     character_world[14], world[12], world[13], world[14]);
        impl.logged_prop_debug = false;
      }
      std::memcpy(&wm, world.data(), 64);
      dev->SetTransform(D3DTS_WORLD, &wm);

      const auto prop_material_it = impl.prop_mats_by_name.find(m.material);
      const milo_scene::MatObj* prop_material =
          prop_material_it != impl.prop_mats_by_name.end()
              ? prop_material_it->second
              : nullptr;
      set_proof_material(prop_material);
      const bool prop_scene_lit =
          !impl.use_scene_lighting ||
          (prop_material && prop_material->use_environ);
      d3d_state.render(D3DRS_LIGHTING, prop_scene_lit ? TRUE : FALSE);
      if (debug_lighting_enabled()) {
        static std::set<std::string> logged_prop_lighting;
        const std::string key =
            m.name + "|" + m.material + "|" +
            (impl.use_scene_lighting ? "scene" : "viewer") + "|" +
            (prop_scene_lit ? "lit" : "unlit");
        if (logged_prop_lighting.insert(key).second) {
          std::fprintf(stderr,
                       "[char3d] prop lighting mesh=%s material=%s scene=%d "
                       "use_env=%d prelit=%d lit=%d "
                       "color_mod=(%.3f %.3f %.3f %.3f)\n",
                       m.name.c_str(), m.material.c_str(),
                       impl.use_scene_lighting ? 1 : 0,
                       prop_material && prop_material->use_environ ? 1 : 0,
                       prop_material && prop_material->prelit ? 1 : 0,
                       prop_scene_lit ? 1 : 0, impl.color_mod[0],
                       impl.color_mod[1], impl.color_mod[2],
                       impl.color_mod[3]);
        }
      }
      IDirect3DTexture9* texture = nullptr;
      const ghogx::asset::Image* texture_image = nullptr;
      if (prop_material) {
        auto it = impl.prop_tex.find(prop_material->diffuse_tex);
        if (it != impl.prop_tex.end()) texture = it->second;
        auto image_it =
            impl.prop_tex_images.find(prop_material->diffuse_tex);
        if (image_it != impl.prop_tex_images.end())
          texture_image = &image_it->second;
      }

      const uint8_t prop_material_blend =
          prop_material ? prop_material->blend
                        : static_cast<uint8_t>(kBlendSrc);
      const BlendState prop_blend_state =
          character_blend_state_for(prop_material_blend);
      const bool prop_uses_source_alpha =
          prop_material && prop_material->has_render_state &&
          source_material_alpha_state_enabled();
      const bool prop_alpha_test =
          prop_uses_source_alpha && prop_material->alpha_cut;
      const bool prop_uses_texture_alpha =
          texture &&
          (prop_alpha_test || prop_material_blend != kBlendSrc);
      const DWORD prop_alpha_ref =
          prop_alpha_test
              ? static_cast<DWORD>(
                    std::clamp(prop_material->alpha_threshold, 0, 255))
              : 0u;
      const bool prop_depth_write =
          material_depth_write_enabled(prop_material);
      d3d_state.render(
          D3DRS_CULLMODE,
          character_cull_mode(prop_material));
      d3d_state.render(D3DRS_ALPHABLENDENABLE, TRUE);
      d3d_state.render(D3DRS_BLENDOP, prop_blend_state.op);
      d3d_state.render(D3DRS_SRCBLEND, prop_blend_state.src);
      d3d_state.render(D3DRS_DESTBLEND, prop_blend_state.dest);
      d3d_state.render(D3DRS_ZWRITEENABLE,
                       prop_depth_write ? TRUE : FALSE);
      d3d_state.render(D3DRS_ALPHATESTENABLE,
                       prop_alpha_test ? TRUE : FALSE);
      d3d_state.render(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
      d3d_state.render(D3DRS_ALPHAREF, prop_alpha_ref);
      const DWORD prop_tex_address =
          prop_material && prop_material->has_render_state
              ? texture_address_for_wrap(prop_material->tex_wrap)
              : D3DTADDRESS_WRAP;
      dev->SetSamplerState(0, D3DSAMP_ADDRESSU, prop_tex_address);
      dev->SetSamplerState(0, D3DSAMP_ADDRESSV, prop_tex_address);
      if (debug_texture_alpha_enabled() &&
          impl.logged_prop_texture_alpha_meshes.insert(m.name).second) {
        log_prop_texture_alpha_stats(m, prop_material, texture_image,
                                     prop_uses_texture_alpha);
      }
      if (texture) {
        d3d_state.texture(0, texture);
        d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        if (prop_uses_texture_alpha) {
          d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
          d3d_state.texture_stage(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
          d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        } else {
          d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
          d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        }
      } else {
        d3d_state.texture(0, nullptr);
        d3d_state.texture_stage(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d3d_state.texture_stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        d3d_state.texture_stage(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
      }

      vb.resize(m.verts.size());
      float mat_alpha =
          prop_material ? prop_material->color[3] : 1.0f;
      float mat_r = prop_material ? prop_material->color[0] : 1.0f;
      float mat_g = prop_material ? prop_material->color[1] : 1.0f;
      float mat_b = prop_material ? prop_material->color[2] : 1.0f;
      if (prop_material_blend == kBlendAdd && mat_alpha < 0.999f) {
        mat_r *= mat_alpha;
        mat_g *= mat_alpha;
        mat_b *= mat_alpha;
        mat_alpha = 1.0f;
      }
      const bool prop_prelit = prop_material && prop_material->prelit;
      for (size_t vi = 0; vi < m.verts.size(); ++vi) {
        const auto& v = m.verts[vi];
        const float src_r = prop_prelit ? v.r : 1.0f;
        const float src_g = prop_prelit ? v.g : 1.0f;
        const float src_b = prop_prelit ? v.b : 1.0f;
        const float src_a = prop_prelit ? v.a : 1.0f;
        SVtx& s = vb[vi];
        s.x = v.px; s.y = v.py; s.z = v.pz;
        s.nx = v.nx; s.ny = v.ny; s.nz = v.nz;
        s.color = D3DCOLOR_ARGB(
            color_byte(mat_alpha * src_a * impl.color_mod[3]),
            color_byte(mat_r * src_r * impl.color_mod[0]),
            color_byte(mat_g * src_g * impl.color_mod[1]),
            color_byte(mat_b * src_b * impl.color_mod[2]));
        s.u = v.u; s.v = v.v;
      }
      dev->DrawIndexedPrimitiveUP(
          D3DPT_TRIANGLELIST, 0, static_cast<UINT>(m.vertex_count),
          static_cast<UINT>(m.face_count), m.indices.data(), D3DFMT_INDEX16,
          vb.data(), sizeof(SVtx));
    }
  }

  d3d_state.texture(0, nullptr);
  d3d_state.render(D3DRS_ZWRITEENABLE, TRUE);
  d3d_state.render(D3DRS_ALPHATESTENABLE, FALSE);
  d3d_state.render(D3DRS_ALPHABLENDENABLE, FALSE);
  dev->EndScene();
}

void CharRenderer::draw() {
  draw_impl(true, static_cast<uint32_t>(character_clear_color()));
}

void CharRenderer::draw_over_scene(const OrbitCamera& cam) {
  impl_->cam = cam;
  draw_impl(false, 0u);
}

bool CharRenderer::refresh_worldcrowd_impostor(
    const OrbitCamera& scene_cam,
    const std::array<float, 16>& source_character_world,
    float source_character_height,
    const std::function<void(const OrbitCamera&)>& draw_attached) {
  auto& impl = *impl_;
  IDirect3DDevice9* dev = impl.dev;
  if (!dev || !impl.have_bounds ||
      !std::isfinite(source_character_height) ||
      source_character_height <= 0.0f) {
    return false;
  }

  // The source owns two tall gImpostorTextures and renders characters through
  // gImpostorCamera before WorldCrowd::DrawShowing submits billboard meshes.
  // Keep that tall silhouette shape here instead of flattening the source
  // character into a square card.
  constexpr UINT kImpostorWidth = 128;
  constexpr UINT kImpostorHeight = 256;
  if (!impl.worldcrowd_impostor_tex) {
    if (FAILED(dev->CreateTexture(
            kImpostorWidth, kImpostorHeight, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            &impl.worldcrowd_impostor_tex, nullptr)) ||
        !impl.worldcrowd_impostor_tex ||
        FAILED(impl.worldcrowd_impostor_tex->GetSurfaceLevel(
            0, &impl.worldcrowd_impostor_surface)) ||
        !impl.worldcrowd_impostor_surface) {
      return false;
    }

    IDirect3DSurface9* current_depth = nullptr;
    D3DFORMAT depth_format = D3DFMT_D24S8;
    if (SUCCEEDED(dev->GetDepthStencilSurface(&current_depth)) &&
        current_depth) {
      D3DSURFACE_DESC desc{};
      if (SUCCEEDED(current_depth->GetDesc(&desc))) depth_format = desc.Format;
      current_depth->Release();
    }
    if (FAILED(dev->CreateDepthStencilSurface(
            kImpostorWidth, kImpostorHeight, depth_format,
            D3DMULTISAMPLE_NONE, 0, TRUE,
            &impl.worldcrowd_impostor_depth, nullptr)) ||
        !impl.worldcrowd_impostor_depth) {
      return false;
    }
  }

  IDirect3DSurface9* old_target = nullptr;
  IDirect3DSurface9* old_depth = nullptr;
  D3DVIEWPORT9 old_viewport{};
  IDirect3DStateBlock9* old_state = nullptr;
  if (FAILED(dev->GetRenderTarget(0, &old_target)) || !old_target) return false;
  dev->GetDepthStencilSurface(&old_depth);
  dev->GetViewport(&old_viewport);
  if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &old_state)) && old_state)
    old_state->Capture();

  const OrbitCamera saved_cam = impl.cam;
  const auto saved_world = impl.world_transform;
  const float saved_aspect = impl.camera_aspect_override;
  const bool target_ok =
      SUCCEEDED(dev->SetRenderTarget(0, impl.worldcrowd_impostor_surface)) &&
      SUCCEEDED(dev->SetDepthStencilSurface(impl.worldcrowd_impostor_depth));
  if (target_ok) {
    const D3DVIEWPORT9 viewport = {0, 0, kImpostorWidth, kImpostorHeight,
                                  0.0f, 1.0f};
    dev->SetViewport(&viewport);
    // GH2 retail WorldCrowd::DrawShowing (SLUS_214.47, 0x0026aeb8-
    // 0x0026b02c) aims gImpostorCamera from the active scene-camera position
    // at placementMesh + CharDef::mHeight/2.  Its distance is clamped to
    // near+halfHeight and its vertical FOV is 2*atan(halfHeight/distance).
    // This is why one fixed front-facing portrait is insufficient: a crowd
    // whose authored rotate flag is false must expose the same character side
    // the live scene camera sees.
    const float half_height = source_character_height * 0.5f;
    const auto source_center =
        transform_point({0.0f, 0.0f, half_height}, source_character_world);
    float scene_eye[3] = {};
    scene_cam.eye(scene_eye);
    float view_from_center[3] = {
        scene_eye[0] - source_center[0],
        scene_eye[1] - source_center[1],
        scene_eye[2] - source_center[2]};
    float distance = std::sqrt(view_from_center[0] * view_from_center[0] +
                               view_from_center[1] * view_from_center[1] +
                               view_from_center[2] * view_from_center[2]);
    if (!std::isfinite(distance) || distance <= 1.0e-5f) {
      view_from_center[0] = 0.0f;
      view_from_center[1] = -1.0f;
      view_from_center[2] = 0.0f;
      distance = 1.0f;
    } else {
      for (float& component : view_from_center) component /= distance;
    }
    distance = std::max(
        distance, std::max(0.001f, scene_cam.near_z) + half_height);

    OrbitCamera impostor = scene_cam;
    impostor.result_frame = {};
    impostor.authored = true;
    for (int axis = 0; axis < 3; ++axis) {
      impostor.authored_eye[axis] =
          source_center[axis] + view_from_center[axis] * distance;
      impostor.authored_at[axis] = source_center[axis];
    }
    if (scene_cam.result_frame.valid) {
      for (int axis = 0; axis < 3; ++axis) {
        impostor.authored_up[axis] = scene_cam.result_frame.up[axis];
      }
    } else if (scene_cam.authored) {
      for (int axis = 0; axis < 3; ++axis) {
        impostor.authored_up[axis] = scene_cam.authored_up[axis];
      }
    } else {
      impostor.authored_up[0] = 0.0f;
      impostor.authored_up[1] = 0.0f;
      impostor.authored_up[2] = 1.0f;
    }
    impostor.fov = 2.0f * std::atan(half_height / distance);
    impostor.near_z = std::max(0.001f, scene_cam.near_z);
    impostor.far_z = std::max(impostor.near_z + source_character_height,
                              scene_cam.far_z);
    impostor.screen_offset[0] = 0.0f;
    impostor.screen_offset[1] = 0.0f;
    impl.impostor_cam = impostor;
    impl.cam = impostor;
    impl.world_transform = source_character_world;
    impl.camera_aspect_override =
        static_cast<float>(kImpostorWidth) /
        static_cast<float>(kImpostorHeight);
    draw_impl(true, 0u);
    if (draw_attached) draw_attached(impostor);
  }

  impl.cam = saved_cam;
  impl.world_transform = saved_world;
  impl.camera_aspect_override = saved_aspect;
  dev->SetDepthStencilSurface(nullptr);
  dev->SetRenderTarget(0, old_target);
  if (old_depth) dev->SetDepthStencilSurface(old_depth);
  dev->SetViewport(&old_viewport);
  if (old_state) {
    old_state->Apply();
    old_state->Release();
  }
  old_target->Release();
  if (old_depth) old_depth->Release();
  return target_ok;
}

void CharRenderer::draw_worldcrowd_impostors_over_scene(
    const OrbitCamera& cam,
    const std::vector<std::array<float, 16>>& placement_worlds,
    float source_character_height) {
  auto& impl = *impl_;
  IDirect3DDevice9* dev = impl.dev;
  if (!dev || !impl.worldcrowd_impostor_tex || placement_worlds.empty() ||
      !std::isfinite(source_character_height) || source_character_height <= 0.0f)
    return;

  IDirect3DStateBlock9* old_state = nullptr;
  if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &old_state)) && old_state)
    old_state->Capture();

  float eye[3] = {};
  cam.eye(eye);
  float result_at[3] = {};
  const float* at = cam.authored ? cam.authored_at : cam.target;
  const float* up = cam.authored ? cam.authored_up : nullptr;
  if (cam.result_frame.valid) {
    for (int axis = 0; axis < 3; ++axis) {
      result_at[axis] = cam.result_frame.position[axis] +
                        cam.result_frame.forward[axis] * 100.0f;
    }
    at = result_at;
    up = cam.result_frame.up;
  }
  Mat4 view = Mat4::look_at_lh(eye[0], eye[1], eye[2], at[0], at[1], at[2],
                               up ? up[0] : 0.0f,
                               up ? up[1] : 0.0f,
                               up ? up[2] : 1.0f);
  // BuildBillboard marks its RndMesh kFastBillboardXYZ, whose source
  // implementation replaces the mesh rotation with
  // RndCam::sCurrent->WorldXfm().m every frame. Harmonix Transform is
  // right-handed (m.x=Cross(m.y,m.z)); D3D's LH view column 0 is the opposite
  // axis and cannot be copied back as source m.x without mirroring the card.
  const std::array<float, 3> camera_forward = {
      view.m[0][2], view.m[1][2], view.m[2][2]};
  const std::array<float, 3> camera_up = {
      view.m[0][1], view.m[1][1], view.m[2][1]};
  const std::array<float, 3> camera_right = {
      camera_forward[1] * camera_up[2] -
          camera_forward[2] * camera_up[1],
      camera_forward[2] * camera_up[0] -
          camera_forward[0] * camera_up[2],
      camera_forward[0] * camera_up[1] -
          camera_forward[1] * camera_up[0]};
  const float backbuffer_aspect =
      impl.win->bb_height() > 0
          ? static_cast<float>(impl.win->bb_width()) /
                static_cast<float>(impl.win->bb_height())
          : 16.0f / 9.0f;
  const float aspect = char_camera_aspect_preset_or(
      "GHOGX_CAMERA_ASPECT", backbuffer_aspect);
  Mat4 proj = Mat4::perspective_lh(cam.fov, aspect, cam.near_z, cam.far_z);
  proj.m[0][0] = -proj.m[0][0];
  if ((cam.authored || cam.result_frame.valid) &&
      !(cam.result_frame.valid && cam.result_frame.screen_offset_consumed) &&
      !char_env_enabled("GHOGX_DISABLE_CAMERA_SCREEN_OFFSET")) {
    constexpr float kScreenOffsetToClip = 1.0f / 768.0f;
    proj.m[2][0] += cam.screen_offset[0] * kScreenOffsetToClip;
    proj.m[2][1] += cam.screen_offset[1] * kScreenOffsetToClip;
  }

  struct BillboardVtx {
    float x, y, z;
    D3DCOLOR color;
    float u, v;
  };
  constexpr DWORD kBillboardFvf =
      D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
  std::vector<BillboardVtx> verts;
  verts.reserve(placement_worlds.size() * 6);
  for (const auto& placement : placement_worlds) {
    // GH2 retail WorldCrowd::BuildBillboard (SLUS_214.47, 0x0026bba0)
    // creates one local X/Z card, with vertices 0..3 at
    // (-hx,+hz), (-hx,-hz), (+hx,+hz), (+hx,-hz), and faces
    // (0,1,2), (1,3,2). Its final call is
    // SetTransConstraint(kFastBillboardXYZ, ..., false), so the active camera
    // supplies the card rotation while each serialized instance supplies its
    // authored position (and any non-unit placement scale).
    const float half_height = source_character_height * 0.5f;
    const float half_width = source_character_height * 0.25f;
    auto row_length = [&](int row) {
      const int base = row * 4;
      const float length =
          std::sqrt(placement[base] * placement[base] +
                    placement[base + 1] * placement[base + 1] +
                    placement[base + 2] * placement[base + 2]);
      return std::isfinite(length) && length > 1.0e-5f ? length : 1.0f;
    };
    const float scale_x = row_length(0);
    const float scale_z = row_length(2);
    auto billboard_point = [&](float local_x, float local_z) {
      return std::array<float, 3>{
          placement[12] + camera_right[0] * local_x * scale_x +
              camera_up[0] * local_z * scale_z,
          placement[13] + camera_right[1] * local_x * scale_x +
              camera_up[1] * local_z * scale_z,
          placement[14] + camera_right[2] * local_x * scale_x +
              camera_up[2] * local_z * scale_z};
    };
    const auto v0 = billboard_point(-half_width, half_height);
    const auto v1 = billboard_point(-half_width, -half_height);
    const auto v2 = billboard_point(half_width, half_height);
    const auto v3 = billboard_point(half_width, -half_height);
    constexpr D3DCOLOR white = 0xffffffffu;
    verts.push_back({v0[0], v0[1], v0[2], white, 0.0f, 0.0f});
    verts.push_back({v1[0], v1[1], v1[2], white, 0.0f, 1.0f});
    verts.push_back({v2[0], v2[1], v2[2], white, 1.0f, 0.0f});
    verts.push_back({v1[0], v1[1], v1[2], white, 0.0f, 1.0f});
    verts.push_back({v3[0], v3[1], v3[2], white, 1.0f, 1.0f});
    verts.push_back({v2[0], v2[1], v2[2], white, 1.0f, 0.0f});
  }

  dev->BeginScene();
  D3DMATRIX identity{};
  identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
  D3DMATRIX dview{}, dproj{};
  std::memcpy(&dview, &view, sizeof(dview));
  std::memcpy(&dproj, &proj, sizeof(dproj));
  dev->SetTransform(D3DTS_WORLD, &identity);
  dev->SetTransform(D3DTS_VIEW, &dview);
  dev->SetTransform(D3DTS_PROJECTION, &dproj);
  dev->SetFVF(kBillboardFvf);
  dev->SetTexture(0, impl.worldcrowd_impostor_tex);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  // WorldCrowd's gImpostorMat never disables RndMat's default cull=true.
  // Preserve BuildBillboard's source face order above. Its (0,1,2) face has
  // a -Y source normal and is front-facing from the venue audience camera.
  // The D3D bridge's mirrored projection leaves that source front face as the
  // same CW render face used by the normal character path.
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
  dev->SetRenderState(D3DRS_ZENABLE, TRUE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
  dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
  // WorldCrowd's gImpostorMat uses alpha cut with threshold 0x80.
  dev->SetRenderState(D3DRS_ALPHAREF, 0x80);
  // WorldCrowd's gImpostorMat uses kSrc, not kSrcAlpha.  Alpha cut supplies
  // the silhouette edge; accepted pixels replace the destination exactly.
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
  dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                       static_cast<UINT>(verts.size() / 3), verts.data(),
                       sizeof(BillboardVtx));
  dev->SetTexture(0, nullptr);
  dev->EndScene();

  if (old_state) {
    old_state->Apply();
    old_state->Release();
  }
}

// ---------------------------------------------------------------------------
// Linear-blend skinning — called by draw() each frame.
// ---------------------------------------------------------------------------
namespace {

// Invert a 3x4 affine (rigid-ish bind matrix) given as row-major 4x4. Uses the
// 3x3 cofactor inverse + translated origin; robust for rotation+uniform-scale.
std::array<float, 16> affine_inverse(const std::array<float, 16>& m) {
  // 3x3 block.
  const float a = m[0], b = m[1], c = m[2];
  const float d = m[4], e = m[5], f = m[6];
  const float g = m[8], h = m[9], i = m[10];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  const float id = (std::fabs(det) > 1e-12f) ? 1.0f / det : 0.0f;
  std::array<float, 16> r{};
  r[0] = (e * i - f * h) * id; r[1] = (c * h - b * i) * id; r[2] = (b * f - c * e) * id;
  r[4] = (f * g - d * i) * id; r[5] = (a * i - c * g) * id; r[6] = (c * d - a * f) * id;
  r[8] = (d * h - e * g) * id; r[9] = (b * g - a * h) * id; r[10] = (a * e - b * d) * id;
  // translation: -T * inv3x3
  const float tx = m[12], ty = m[13], tz = m[14];
  r[12] = -(tx * r[0] + ty * r[4] + tz * r[8]);
  r[13] = -(tx * r[1] + ty * r[5] + tz * r[9]);
  r[14] = -(tx * r[2] + ty * r[6] + tz * r[10]);
  r[15] = 1.0f;
  return r;
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

std::array<float, 16> xfm16(const milo_scene::Xfm& x) {
  std::array<float, 16> m{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) m[r * 4 + c] = x.rot[r][c];
  m[12] = x.pos[0]; m[13] = x.pos[1]; m[14] = x.pos[2]; m[15] = 1.0f;
  return m;
}

std::array<float, 16> transpose_xfm_rotation(const milo_scene::Xfm& x) {
  auto m = xfm16(x);
  for (int row = 0; row < 3; ++row) {
    for (int col = row + 1; col < 3; ++col) {
      std::swap(m[row * 4 + col], m[col * 4 + row]);
    }
  }
  return m;
}

std::array<float, 16> scene_object_world(const milo_scene::Scene& scene,
                                         const std::string& name) {
  auto find_xfm = [&](const std::string& object_name,
                      const milo_scene::Xfm*& xfm,
                      std::string& parent) -> bool {
    for (const auto& t : scene.transes) {
      if (t.name == object_name) {
        xfm = &t.local;
        parent = t.parent;
        return true;
      }
    }
    for (const auto& m : scene.meshes) {
      if (m.name == object_name) {
        xfm = &m.local;
        parent = m.parent;
        return true;
      }
    }
    return false;
  };

  const milo_scene::Xfm* xfm = nullptr;
  std::string parent;
  if (!find_xfm(name, xfm, parent)) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  }

  std::array<float, 16> world = xfm16(*xfm);
  int guard = 0;
  while (!parent.empty() && guard++ < 128) {
    const milo_scene::Xfm* parent_xfm = nullptr;
    std::string next_parent;
    if (!find_xfm(parent, parent_xfm, next_parent)) break;
    world = mul16(world, xfm16(*parent_xfm));
    parent = next_parent;
  }
  return world;
}

std::optional<std::array<float, 16>> scene_object_stored_world(
    const milo_scene::Scene& scene,
    const std::string& name) {
  for (const auto& m : scene.meshes) {
    if (m.name == name) return xfm16(m.world_stored);
  }
  for (const auto& t : scene.transes) {
    if (t.name == name) return xfm16(t.world_stored);
  }
  return std::nullopt;
}

}  // namespace

namespace {

void skin_to_pose_impl(
    const SkinnedMesh& mesh,
    const Character& character,
    const CharacterWorldFrameCache& world_cache,
    std::vector<std::array<float, 3>>& out_pos,
    std::vector<std::array<float, 3>>& out_nrm,
    std::vector<std::array<float, 16>>& skin) {
  out_pos.resize(mesh.verts.size());
  out_nrm.resize(mesh.verts.size());
  const size_t nb = mesh.bone_palette.size();
  const bool debug_mesh_mode = debug_mesh_mode_enabled(mesh.name);
  const bool debug_skin_matrix = debug_skin_matrix_enabled();
  const bool debug_skin_matrix_all = debug_skin_matrix_all_enabled();
  // The GH2 RndMesh consumer has one weighted path.  Only meshes with no
  // serialized palette are unskinned; eye, hair, arm, leg, and attachment
  // names do not select alternate skin spaces.
  const char* raw_mode = nb == 0 ? "unskinned-source-trans" : nullptr;
  if (raw_mode) {
    if (debug_mesh_mode) {
      std::fprintf(stderr,
                   "[skin-mode] %-24s mat=%-18s palette=%zu bind=%zu mode=%s\n",
                   mesh.name.c_str(), mesh.material.c_str(), nb,
                   mesh.bind.size(), raw_mode);
    }
    for (size_t vi = 0; vi < mesh.verts.size(); ++vi) {
      const SkinVertex& v = mesh.verts[vi];
      out_pos[vi] = {v.px, v.py, v.pz};
      out_nrm[vi] = {v.nx, v.ny, v.nz};
    }
    return;
  }

  // RndMesh::SetBone stores mesh WorldXfm * inverse(bone WorldXfm). Skinning
  // consumes that decoded offset directly: vertex * offset * currentBoneWorld.
  if (debug_weight_stats_enabled()) {
    float min_sum = 999999.0f;
    float max_sum = -999999.0f;
    float max_weight = 0.0f;
    int nonzero_counts[5] = {};
    for (const auto& v : mesh.verts) {
      float sum = 0.0f;
      int nonzero = 0;
      for (int wi = 0; wi < 4; ++wi) {
        sum += v.w[wi];
        max_weight = std::max(max_weight, v.w[wi]);
        if (v.w[wi] != 0.0f) ++nonzero;
      }
      min_sum = std::min(min_sum, sum);
      max_sum = std::max(max_sum, sum);
      if (nonzero >= 0 && nonzero <= 4) ++nonzero_counts[nonzero];
    }
    std::fprintf(stderr,
                 "[weight-stats] %-24s mat=%-18s verts=%zu palette=%zu "
                 "sum=(%.3f..%.3f) maxw=%.3f nonzero=(%d,%d,%d,%d,%d)\n",
                 mesh.name.c_str(), mesh.material.c_str(), mesh.verts.size(),
                 mesh.bone_palette.size(), min_sum, max_sum, max_weight,
                 nonzero_counts[0], nonzero_counts[1], nonzero_counts[2],
                 nonzero_counts[3], nonzero_counts[4]);
  }
  const bool has_source_bone_transforms = mesh.bind.size() >= nb;
  if (!has_source_bone_transforms) {
    if (debug_mesh_mode) {
      std::fprintf(stderr,
                   "[skin-mode] %-24s mat=%-18s palette=%zu bind=%zu "
                   "mode=missing-source-offset-unsupported\n",
                   mesh.name.c_str(), mesh.material.c_str(), nb,
                   mesh.bind.size());
    }
    for (size_t vi = 0; vi < mesh.verts.size(); ++vi) {
      const SkinVertex& v = mesh.verts[vi];
      out_pos[vi] = {v.px, v.py, v.pz};
      out_nrm[vi] = {v.nx, v.ny, v.nz};
    }
    return;
  }
  const std::array<float, 16> identity = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  skin.assign(nb, identity);
  constexpr const char* skin_mode = "source-four-slot-offset-current-world";
  if (debug_mesh_mode) {
    std::fprintf(stderr,
                 "[skin-mode] %-24s mat=%-18s palette=%zu bind=%zu mode=%s\n",
                 mesh.name.c_str(), mesh.material.c_str(), nb,
                 mesh.bind.size(), skin_mode);
  }
  for (size_t i = 0; i < nb; ++i) {
    const std::string& bone_name = mesh.bone_palette[i];
    if (bone_name.empty() || !character.has_transform(bone_name)) {
      if (debug_skin_matrix_enabled() && debug_mesh_mode_enabled(mesh.name)) {
        std::fprintf(stderr,
                     "[skin-slot-skip] %-24s slot=%zu bone=%s reason=unresolved\n",
                     mesh.name.c_str(), i, bone_name.c_str());
      }
      // A null serialized slot contributes through identity exactly as the
      // retail consumer does.  Keep its weight in the matching slot.
      continue;
    }
    const std::array<float, 16> curr_world =
        world_cache.bone_world_local_chain(bone_name);
    skin[i] = mul16(xfm16(mesh.bind[i]), curr_world);
    const bool debug_this_skin =
        debug_skin_matrix && (debug_skin_matrix_all || debug_mesh_mode);
    if (debug_this_skin && (i == 0 || debug_mesh_mode)) {
      const auto& s = skin[i];
      std::fprintf(stderr,
                   "[skin-matrix] %-24s bone=%-24s mode=%s "
                   "diag=(%.3f %.3f %.3f) "
                   "pos=(%.3f %.3f %.3f)\n",
                   mesh.name.c_str(), bone_name.c_str(),
                   skin_mode, s[0], s[5], s[10], s[12], s[13], s[14]);
      if (debug_mesh_mode) {
        const auto bind_lc =
            world_cache.bone_world_bind_local_chain(bone_name);
        const auto curr_lc =
            world_cache.bone_world_local_chain(bone_name);
        const auto bind_stored = world_cache.bone_world_bind(bone_name);
        const auto curr_stored = world_cache.bone_world(bone_name);
        const auto mesh_lc = world_cache.bone_world_local_chain(mesh.name);
        log_compact_matrix_rows("bind-lc", mesh.name, bone_name, bind_lc);
        log_compact_matrix_rows("curr-lc", mesh.name, bone_name, curr_lc);
        log_compact_matrix_rows("bind-stored", mesh.name, bone_name,
                                bind_stored);
        log_compact_matrix_rows("curr-stored", mesh.name, bone_name,
                                curr_stored);
        log_compact_matrix_rows("mesh-lc", mesh.name, bone_name, mesh_lc);
        std::fprintf(stderr,
                     "[skin-matrix-row] %-24s bone=%-24s "
                     "r0=(%.4f %.4f %.4f %.4f) "
                     "r1=(%.4f %.4f %.4f %.4f) "
                     "r2=(%.4f %.4f %.4f %.4f) "
                     "pos=(%.4f %.4f %.4f %.4f)\n",
                     mesh.name.c_str(), bone_name.c_str(),
                     s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7],
                     s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
        log_compact_matrix_rows("skin", mesh.name, bone_name, s);
      }
    }
  }

  for (size_t vi = 0; vi < mesh.verts.size(); ++vi) {
    const SkinVertex& v = mesh.verts[vi];
    std::array<float, 3> p{0, 0, 0}, n{0, 0, 0};
    bool any = false;
    for (size_t i = 0; i < nb && i < 4; ++i) {
      const float wgt = v.w[i];
      if (wgt == 0.0f) continue;
      if (i >= skin.size()) continue;
      const auto& s = skin[i];
      p[0] += wgt * (v.px * s[0] + v.py * s[4] + v.pz * s[8] + s[12]);
      p[1] += wgt * (v.px * s[1] + v.py * s[5] + v.pz * s[9] + s[13]);
      p[2] += wgt * (v.px * s[2] + v.py * s[6] + v.pz * s[10] + s[14]);
      n[0] += wgt * (v.nx * s[0] + v.ny * s[4] + v.nz * s[8]);
      n[1] += wgt * (v.nx * s[1] + v.ny * s[5] + v.nz * s[9]);
      n[2] += wgt * (v.nx * s[2] + v.ny * s[6] + v.nz * s[10]);
      any = true;
    }
    if (!any) { p = {v.px, v.py, v.pz}; n = {v.nx, v.ny, v.nz}; }
    out_pos[vi] = p;
    out_nrm[vi] = n;
    if (vi == 0 && debug_skin_matrix && debug_mesh_mode) {
      std::fprintf(stderr,
                   "[skin-vertex0] %-24s raw=(%.4f %.4f %.4f) "
                   "weights=(%.4f %.4f %.4f %.4f) "
                   "out=(%.4f %.4f %.4f) any=%d\n",
                   mesh.name.c_str(), v.px, v.py, v.pz, v.w[0], v.w[1],
                   v.w[2], v.w[3], p[0], p[1], p[2], any ? 1 : 0);
      std::fprintf(stderr,
                   "[skin-vtx] mesh=%s vi=0 raw=%.5f %.5f %.5f "
                   "weights=%.5f %.5f %.5f %.5f\n",
                   mesh.name.c_str(), v.px, v.py, v.pz, v.w[0], v.w[1],
                   v.w[2], v.w[3]);
      std::fprintf(stderr,
                   "[skin-vtx-out] mesh=%s vi=0 out=%.5f %.5f %.5f any=%d\n",
                   mesh.name.c_str(), p[0], p[1], p[2], any ? 1 : 0);
    }
  }
  if (debug_skin_matrix && debug_mesh_mode && !out_pos.empty()) {
    size_t min_z_i = 0;
    size_t max_z_i = 0;
    for (size_t vi = 1; vi < out_pos.size(); ++vi) {
      if (out_pos[vi][2] < out_pos[min_z_i][2]) min_z_i = vi;
      if (out_pos[vi][2] > out_pos[max_z_i][2]) max_z_i = vi;
    }
    const SkinVertex& min_v = mesh.verts[min_z_i];
    const SkinVertex& max_v = mesh.verts[max_z_i];
    std::fprintf(stderr,
                 "[skin-z-extreme] %-24s min_i=%zu raw=(%.4f %.4f %.4f) "
                 "weights=(%.4f %.4f %.4f %.4f) out=(%.4f %.4f %.4f) "
                 "max_i=%zu raw=(%.4f %.4f %.4f) "
                 "weights=(%.4f %.4f %.4f %.4f) out=(%.4f %.4f %.4f)\n",
                 mesh.name.c_str(), min_z_i, min_v.px, min_v.py, min_v.pz,
                 min_v.w[0], min_v.w[1], min_v.w[2], min_v.w[3],
                 out_pos[min_z_i][0], out_pos[min_z_i][1],
                 out_pos[min_z_i][2], max_z_i, max_v.px, max_v.py, max_v.pz,
                 max_v.w[0], max_v.w[1], max_v.w[2], max_v.w[3],
                 out_pos[max_z_i][0], out_pos[max_z_i][1],
                 out_pos[max_z_i][2]);
  }
}

}  // namespace

void skin_to_pose(const SkinnedMesh& mesh, const Character& character,
                  std::vector<std::array<float, 3>>& out_pos,
                  std::vector<std::array<float, 3>>& out_nrm) {
  std::vector<std::array<float, 16>> skin;
  CharacterWorldFrameCache world_cache(character);
  skin_to_pose_impl(mesh, character, world_cache, out_pos, out_nrm, skin);
}

std::array<float, 16> source_character_mesh_submission_world(
    const SkinnedMesh& mesh, const Character& character) {
  if (!mesh.bone_palette.empty()) {
    return {1, 0, 0, 0, 0, 1, 0, 0,
            0, 0, 1, 0, 0, 0, 0, 1};
  }
  return character.mesh_world(mesh);
}

bool source_character_mesh_renders_decoded_skinning(const SkinnedMesh& mesh) {
  return !mesh_uses_raw_pose_output(mesh, false);
}

}  // namespace ghogx::character
