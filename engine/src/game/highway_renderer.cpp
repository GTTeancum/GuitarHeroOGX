// engine/src/game/highway_renderer.cpp
//
// Geometry starts from GH2 source data and is then constrained by PCSX2 traces:
//   * config/gen/track_graphics.dtb : track_width 20, horizon_y 110,
//     remove_y -15, alpha_dist 40, track_speed per difficulty, slot_colors.
//   * track/gen/track.milo_ps2 -> Cam 'track.cam' : position (0.197,-63.22,
//     17.94), 6.53deg downward pitch, near 50, far 250, fov 0.4102 rad.
//   * proofs/pcsx2_highway_trace_20260715/active_gsdumps/frame_16_*
//     constrains the PC dev projection, far fade, and base sustain-tail width.
// World axes (Harmonix track space): X = lanes (across), Y = depth/scroll
// (notes travel from +Y toward 0 = strikeline), Z = up.

#include "game/highway_renderer.h"
#include "game/gameplay_session.h"
#include "ark_v3.h"
#include "dtb.h"
#include "milo_scene/milo_scene.h"
#include "milo.h"
#include "render/window_d3d9.h"
#include "render/scene_d3d9.h"   // Mat4
#include "asset/milo_image.h"

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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ghogx::game {

namespace {

struct V3 {
  float x, y, z;
  float nx, ny, nz;
  D3DCOLOR c;
  float u, v;
};
constexpr DWORD kFVF =
    D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
struct V2 { float x, y, z, rhw; D3DCOLOR c; };
constexpr DWORD kDebugFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
struct ParticleVtx {
  float x, y, z;
  float size;
  D3DCOLOR c;
};
constexpr DWORD kParticleFvf = D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE;

// --- Geometry constants, all from track_graphics.dtb / track.cam ----------
constexpr float kStrikeY     = 0.0f;     // strikeline (hit line) depth
constexpr float kBoardZ      = 0.0f;     // board surface height
constexpr float kGemZ        = 0.12f;    // gems just above the board
constexpr float kGemHalf     = 1.7f;     // fallback only when native gem meshes are absent
constexpr float kDefaultTrackWidth = 20.0f;
constexpr float kDefaultLaneSpacing = kDefaultTrackWidth / 5.0f;
constexpr float kDefaultBoardHalfX = kDefaultTrackWidth * 0.5f;
constexpr float kDefaultTopY = 110.0f;
constexpr float kDefaultRemoveY = -15.0f;
constexpr float kDefaultAlphaDist = 40.0f;
constexpr float kDefaultTailGlowWidth = 1.5f;
constexpr float kDefaultTailGlowTightWidth = 0.7f;
constexpr float kDefaultHorizonTailClip = 7.0f;
constexpr float kDefaultNowbarTailClip = 1.5f;
constexpr float kDefaultCamNear = 50.0f;
constexpr float kDefaultCamFar = 200.0f;
constexpr float kDefaultCamZStart = 0.0f;
constexpr float kDefaultCamZEnd = 0.1f;
constexpr float kDefaultBurnNormalFrame = 0.0f;
constexpr float kDefaultBurnWhammyFrame = 10.0f;
constexpr float kDefaultBurnBonusFrame = 20.0f;
// Stock ui/gen/track_panel.dtb drives the PanelDir from frame 0 to 1920 in
// one second.  The same script reveals lanes 0..4 at 1.3..1.7 seconds.
constexpr float kTrackIntroEndFrame = 1920.0f;
constexpr double kTrackIntroExtendSeconds = 1.0;
constexpr double kTrackIntroFirstSmasherSeconds = 1.3;
constexpr double kTrackIntroSmasherStepSeconds = 0.1;
constexpr double kTrackIntroRefreshButtonsSeconds = 2.5;
// PCSX2 rail/tail measurements keep the source track art as the base, then
// apply one measured root-space correction so track meshes, lane positions,
// tails, and smashers stay locked together. The 4:3 value comes from the live
// 20260715 PCSX2 gameplay rail fit in
// proofs/highway_4x3_projection_measure_20260715_04/projection_trace.json
// and the follow-up 4:3 aspect profile fit.
constexpr float kPcsx2MeasuredTrackXScale4x3 = 1.02769107f;
constexpr float kPcsx2MeasuredTrackXScale16x9 = 1.35005774f;
constexpr float kPcsx2MeasuredCamX4x3 = -0.04188884f;
constexpr float kPcsx2MeasuredCamYawDeg4x3 = 0.21988230f;
constexpr float kPcsx2MeasuredCamX16x9 = 0.197f;
constexpr float kPcsx2MeasuredCamYawDeg16x9 = 0.0f;
constexpr float kPcsx2MeasuredFadeTopY = 164.47972f;
constexpr float kPcsx2MeasuredFadeAlphaDist = 71.67506f;
struct TailWidthSample {
  float rel_y = 0.0f;
  float width_px_720 = 0.0f;
};
// Body rows only from proofs/tail_profile_compare_20260715_07_window_body_cap_split.
// Rows past rel_y 0.80 are strike/cap effects and are not treated as tail body.
constexpr std::array<TailWidthSample, 19> kPcsx2WhammyBodyWidthProfile4x3 = {{
    {0.00000f, 10.929f},
    {0.01366f, 10.929f},
    {0.05738f, 12.857f},
    {0.10109f, 14.143f},
    {0.14481f, 14.786f},
    {0.18852f, 15.429f},
    {0.23224f, 16.714f},
    {0.27596f, 18.000f},
    {0.31967f, 18.000f},
    {0.36339f, 19.286f},
    {0.40710f, 20.571f},
    {0.45082f, 21.214f},
    {0.49454f, 21.857f},
    {0.53825f, 23.143f},
    {0.58197f, 24.429f},
    {0.62568f, 25.714f},
    {0.66940f, 25.714f},
    {0.71311f, 27.000f},
    {0.80055f, 28.286f},
}};
// Normal non-whammy held body rows from the corrected 4:3 PCSX2 red sustain
// trace, lane_body layer, scaled from 480px to 720px height:
// proofs/pcsx2_4x3_red_normal_tail_corrected_20260716_01/
// pcsx2_4x3_frame17_red_normal_tubed_normal_tail_trace.json
// Rows after rel_y ~= 0.80 intersect the near note cap/effect and are not used
// as the steady tail body. The separate held_tight core carries the cyan/white
// center.
constexpr std::array<TailWidthSample, 13> kPcsx2NormalBodyWidthProfile4x3 = {{
    {0.00000f, 7.500f},
    {0.10088f, 9.000f},
    {0.17105f, 10.500f},
    {0.24123f, 10.500f},
    {0.31140f, 11.250f},
    {0.38158f, 12.000f},
    {0.45175f, 12.000f},
    {0.52193f, 13.500f},
    {0.59211f, 13.500f},
    {0.66228f, 15.000f},
    {0.73246f, 15.000f},
    {0.80263f, 16.500f},
    {1.00000f, 16.500f},
}};
// The same PCSX2 trace also separates the full visible silhouette. GH2's normal
// held tail reads as a single ribbon because the textured/detail layer occupies
// this broader silhouette while the lane-colored body stays visible through the
// center; using the lane_body width for both layers collapses the tail into thin
// rails in motion.
constexpr std::array<TailWidthSample, 12> kPcsx2NormalSilhouetteWidthProfile4x3 = {{
    {0.00000f, 7.500f},
    {0.10088f, 12.750f},
    {0.17105f, 15.000f},
    {0.24123f, 15.000f},
    {0.31140f, 16.500f},
    {0.38158f, 18.000f},
    {0.45175f, 18.000f},
    {0.52193f, 21.750f},
    {0.59211f, 33.000f},
    {0.66228f, 28.500f},
    {0.73246f, 34.500f},
    {1.00000f, 34.500f},
}};
// Near-cap cyan/white join rows from the stock PCSX2 GS dump:
// proofs/pcsx2_normal_cap_gs_trace_20260717_01/frame17_tail_extract/
// tail_trace.json. The source trace is 480p, so widths are scaled 1.5x to the
// renderer's 720p measurement space. This layer is short and normal-only; it
// deliberately avoids changing the red lane body or global tail width.
constexpr std::array<TailWidthSample, 5> kPcsx2NormalTightJoinWidthProfile4x3 = {{
    {0.000f, 26.250f},
    {0.330f, 58.500f},
    {0.660f, 93.750f},
    {0.860f, 45.000f},
    {1.000f, 18.000f},
}};
// The `tail_tight.tex` source mesh visibly narrows the middle of this short
// join layer. The first compensated pass fixed the missing bulge but coarse
// sectioning over-widened the shoulder. The refined proof
// proofs/native_4x3_tight_join_refined_20260717_01 measures this curve against
// the PCSX2 GS visible target after subdividing the profile sections.
constexpr std::array<TailWidthSample, 5>
    kNormalTightJoinGeometryCompensationProfile4x3 = {{
        {0.000f, 1.000f},
        {0.330f, 1.000f},
        {0.660f, 1.630f},
        {0.860f, 1.170f},
        {1.000f, 1.250f},
}};
// Corrected 4:3 PCSX2 red normal sustain trace:
// proofs/pcsx2_4x3_red_normal_tail_corrected_20260716_01/
// The cyan/white tight core is only a thin highlight over the lane-colored
// body, not the broad active held glow used by star/whammy tails.
constexpr float kNormalHeldTightCoreWidthScale4x3 = 0.18f;
// The corrected 4:3 tubed trace is the visible filled-body target. The active
// held detail mesh contributes some lane color at the edges, so the solid
// center fill now carries the traced lane-body width directly. The authored
// tight glow is drawn below this body, so the body should cover its center and
// leave only the PCSX2-like cyan/white edge visible.
constexpr float kNormalHeldBodyFillWidthScale4x3 = 1.0f;
// The authored active tail_glow_%s line01 texture renders visibly narrower than
// its mesh geometry. The isolated layer comparison in
// proofs/native_4x3_active_tail_glow_material_20260716_01/ measured the detail
// layer at a 1.667x median width deficit against the corrected PCSX2
// silhouette trace, so the geometry is expanded to land the visible glow on the
// traced width instead of shrinking the source texture.
constexpr float kNormalHeldBodyDetailWidthScale4x3 = 1.67f;
constexpr uint8_t kNormalHeldBodyFillAlpha4x3 = 245u;
// Corrected 4:3 PCSX2 red normal-tail rows put the cyan/white tight edge about
// 7-10 px left of the lane-colored body through the stable body region.
constexpr float kNormalHeldTightEdgeOffsetPx720 = -8.5f;
// The first shifted-edge proof traced the native core at ~5 px median against
// the corrected PCSX2 core's ~3 px median, so the overlay uses a 0.6x width fit.
constexpr float kNormalHeldTightEdgeWidthScale4x3 = 0.6f;
// Full-composite color stats on the corrected red sustain reference measured
// native tight-core median luma at ~227.5 versus PCSX2 at ~182.3, so normal
// active tight layers use an 80% alpha scale while keeping the traced widths.
constexpr uint8_t kNormalHeldTightUnderlayAlpha4x3 = 196u;
constexpr uint8_t kNormalHeldTightEdgeAlpha4x3 = 204u;
constexpr uint8_t kNormalHeldTightJoinAlpha4x3 = 176u;
constexpr float kNormalHeldTightJoinWorldLength4x3 = 4.0f;
// The incoming ordinary tail keeps the authored broad mesh width. Its flat
// center fill is narrowed to the same-ROI yellow incoming proof width so it
// closes the middle without widening the source lane-tail silhouette.
constexpr float kIncomingNormalBodyFillWidthScale4x3 = 0.74f;
constexpr uint8_t kIncomingNormalBodyFillAlpha4x3 = 185u;
// The profile above is the PCSX2 visible silhouette target. This fit only
// inverts the native D3D line-texture response measured in
// proofs/whammy_tail_body_width_compensation_20260715_01.json so the rendered
// visible width lands on that target instead of on a narrower texture core.
constexpr float kNativeWhammyLineResponseA4x3 = -0.59653887f;
constexpr float kNativeWhammyLineResponseB4x3 = 0.79456448f;
// Active held-lane body uses the same soft line texture as the source lane
// glow. At mid-tail, proofs/native_4x3_long_single_star_whammy_sweetchild_
// 20260715_11_solver_trace/run.log measured 1.035 local half for a 28.461 px
// geometry target, while the 1.5 authored half produced a ~17 px visible body
// in proofs/whammy_tail_blob_analysis_20260715_15_native_measured_held_body_stack.
constexpr float kNativeHeldLineVisibleFraction4x3 = 0.414f;
constexpr uint8_t kActiveBonusTailAlpha = 245u;
constexpr uint8_t kActiveStarTailAlpha = 245u;
constexpr float kSmasherClipZ = kBoardZ + 0.02f;
constexpr float kSmasherBodyTopZ = kBoardZ + 0.20f;
constexpr float kSmasherFixedRingTopZ = kBoardZ + 0.22f;
constexpr DWORD kNoteCardAlphaRef = 8;
// Preserve the authored standard-note silhouette while making its black edge
// readable at PC highway speed. A faint wider copy feathers the boundary, a
// solid copy establishes the outline, and the untouched source cap sits above
// both. All three copies share one draw call.
constexpr float kPcStandardRingSolidScale = 1.23f;
constexpr float kPcStandardRingFeatherScale = 1.30f;
constexpr float kPcStandardRingFeatherAlpha = 0.35f;
constexpr float kHighwayMaxAuthoredLightColor = 64.0f;
constexpr float kHighwayApproxFillScale = 0.45f;
constexpr const char* kStarBlackTopTextureAlias = "gem.tex#star_top_black_raw";
constexpr const char* kSmasherRingAddAlphaKeySuffix =
    "#smasher_ring_add_black_key";
constexpr const char* kSmasherAddAlphaKeySuffix =
    "#smasher_add_rgb_mask";
constexpr std::array<float, 4> kDefaultTrackSpeed = {1.0f, 1.0f, 1.4f, 1.4f};
const std::array<std::string, 5> kDefaultSlotColorNames = {
    "green", "red", "yellow", "blue", "orange"};
const std::array<uint32_t, 5> kDefaultSlotLaneColors = {
    D3DCOLOR_ARGB(225, 60, 230, 70), D3DCOLOR_ARGB(225, 235, 60, 50),
    D3DCOLOR_ARGB(225, 240, 210, 40), D3DCOLOR_ARGB(225, 60, 150, 235),
    D3DCOLOR_ARGB(225, 245, 140, 30)};

enum HighwayMiloBlend : uint8_t {
  kHighwayBlendDest = 0,
  kHighwayBlendSrc = 1,
  kHighwayBlendAdd = 2,
  kHighwayBlendSrcAlpha = 3,
  kHighwayBlendSrcAlphaAdd = 4,
  kHighwayBlendSubtract = 5,
  kHighwayBlendMultiply = 6,
};

enum HighwayMiloZMode : uint8_t {
  kHighwayZModeDisable = 0,
  kHighwayZModeNormal = 1,
  kHighwayZModeTransparent = 2,
  kHighwayZModeForce = 3,
  kHighwayZModeDecal = 4,
};

struct HighwayBlendState {
  DWORD src = D3DBLEND_SRCALPHA;
  DWORD dest = D3DBLEND_INVSRCALPHA;
  DWORD op = D3DBLENDOP_ADD;
  bool additive = false;
};

enum class HighwayTextureAlphaMode {
  Raw,
  BlackKey,
  RgbMask,
};

HighwayBlendState highway_blend_state_for(uint8_t blend) {
  switch (blend) {
    case kHighwayBlendDest:
      return {D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLENDOP_ADD, false};
    case kHighwayBlendSrc:
      return {D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    case kHighwayBlendAdd:
      return {D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kHighwayBlendSrcAlpha:
      return {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA,
              D3DBLENDOP_ADD, false};
    case kHighwayBlendSrcAlphaAdd:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD, true};
    case kHighwayBlendSubtract:
      return {D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT,
              true};
    case kHighwayBlendMultiply:
      return {D3DBLEND_DESTCOLOR, D3DBLEND_ZERO, D3DBLENDOP_ADD, false};
    default:
      return {};
  }
}

bool highway_z_mode_writes(uint8_t z_mode) {
  return z_mode == kHighwayZModeNormal || z_mode == kHighwayZModeForce ||
         z_mode == kHighwayZModeDecal;
}

DWORD dword_from_float(float value) {
  DWORD out = 0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

float highway_particle_hash01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x & 0x00ffffffu) /
         static_cast<float>(0x01000000u);
}

int particle_color_channel(float value) {
  return std::clamp(static_cast<int>(std::clamp(value, 0.0f, 1.0f) *
                                     255.0f + 0.5f),
                    0, 255);
}

DWORD highway_color_from_rgba(const std::array<float, 4>& color) {
  return D3DCOLOR_XRGB(particle_color_channel(color[0]),
                       particle_color_channel(color[1]),
                       particle_color_channel(color[2]));
}

std::array<float, 3> source_light_direction(
    const ghogx::milo_scene::LightObj& light) {
  return {-light.world_stored.rot[2][0],
          -light.world_stored.rot[2][1],
          -light.world_stored.rot[2][2]};
}

bool source_light_color_sane(const ghogx::milo_scene::LightObj& light) {
  for (float value : light.color) {
    if (!std::isfinite(value) || value < 0.0f ||
        value > kHighwayMaxAuthoredLightColor) {
      return false;
    }
  }
  return true;
}

bool source_environment_color_sane(const ghogx::milo_scene::EnvironObj& env) {
  for (float value : env.color_a) {
    if (!std::isfinite(value) || value < 0.0f || value > 4.0f) {
      return false;
    }
  }
  return true;
}

bool source_light_is_real(int type) {
  return type == 0 || type == 2;
}

bool source_light_is_approx(int type) {
  return type == 1;
}

std::array<float, 4> average_particle_color(
    const std::array<float, 4>& low,
    const std::array<float, 4>& high) {
  std::array<float, 4> out{};
  for (size_t i = 0; i < out.size(); ++i) out[i] = (low[i] + high[i]) * 0.5f;
  return out;
}

std::array<float, 4> sample_particle_color(
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
      const float t = clamped_phase / clamped_mid;
      for (size_t i = 0; i < out.size(); ++i) {
        out[i] = start[i] + (mid[i] - start[i]) * t;
      }
    } else {
      const float t = (clamped_phase - clamped_mid) /
                      std::max(0.001f, 1.0f - clamped_mid);
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
    return std::clamp((1.0f - clamped_phase) / (1.0f - shrink), 0.0f,
                      1.0f);
  }
  return 1.0f;
}

std::array<float, 4> sample_particle_anim_color(
    const std::vector<HighwayRenderer::ParticleColorAnimKey>& keys,
    float frame, const std::array<float, 4>& fallback) {
  if (keys.empty() || !std::isfinite(frame)) return fallback;
  if (keys.size() == 1 || frame <= keys.front().frame) {
    return keys.front().color;
  }
  if (frame >= keys.back().frame) return keys.back().color;
  for (size_t i = 1; i < keys.size(); ++i) {
    if (frame > keys[i].frame) continue;
    const auto& a = keys[i - 1];
    const auto& b = keys[i];
    const float t = std::clamp(
        (frame - a.frame) / std::max(0.001f, b.frame - a.frame),
        0.0f, 1.0f);
    std::array<float, 4> out{};
    for (size_t channel = 0; channel < out.size(); ++channel) {
      out[channel] =
          a.color[channel] + (b.color[channel] - a.color[channel]) * t;
    }
    return out;
  }
  return keys.back().color;
}

std::array<float, 2> sample_particle_anim_vector2(
    const std::vector<HighwayRenderer::ParticleVector2AnimKey>& keys,
    float frame, const std::array<float, 2>& fallback) {
  if (keys.empty() || !std::isfinite(frame)) return fallback;
  if (keys.size() == 1 || frame <= keys.front().frame) {
    return keys.front().value;
  }
  if (frame >= keys.back().frame) return keys.back().value;
  for (size_t i = 1; i < keys.size(); ++i) {
    if (frame > keys[i].frame) continue;
    const auto& a = keys[i - 1];
    const auto& b = keys[i];
    const float t = std::clamp(
        (frame - a.frame) / std::max(0.001f, b.frame - a.frame),
        0.0f, 1.0f);
    return {a.value[0] + (b.value[0] - a.value[0]) * t,
            a.value[1] + (b.value[1] - a.value[1]) * t};
  }
  return keys.back().value;
}

// Camera (track.cam, decoded) with PCSX2 rail/strikeline corrections applied.
// Harmonix cams look down local +Y, up = local +Z.
constexpr float kCamY = -63.22f;
constexpr float kCamZ = 17.33562394f;
constexpr float kCamFwd[3] = { 0.0f, 0.9954675f, -0.0951024f };
constexpr float kCamUp [3] = { 0.0f, 0.0951024f,  0.9954675f };
constexpr float kCamFov    = 0.4102f;    // vertical fov, radians
constexpr float kCamPitchDeg = 5.45721249f;

// Base scroll-rate fallback (world-units/sec). The authored y_per_second scalar
// is loaded from track/gen/track.milo_ps2's root PanelDir body when available.
constexpr float kDefaultYPerSecond = 80.0f;

std::string lane_texture_name(const char* prefix, const std::string& slot_color,
                              const char* suffix) {
  return std::string(prefix) + slot_color + suffix;
}

bool valid_slot_color_name(const std::string& name) {
  if (name.empty() || name.size() > 32) return false;
  for (unsigned char ch : name) {
    if (std::isalnum(ch) || ch == '_') continue;
    return false;
  }
  return true;
}

std::array<std::string, 5> keyed_slot_color_names(
    const gh::dtb::Tree& tree,
    const std::array<std::string, 5>& fallback) {
  auto node = gh::dtb::find_keyed(tree, "slot_colors");
  if (!node || !gh::dtb::is_array(*node)) return fallback;
  const auto& kids = gh::dtb::children(*node);
  if (kids.size() < 6) return fallback;
  std::array<std::string, 5> out{};
  for (size_t lane = 0; lane < out.size(); ++lane) {
    if (!kids[lane + 1]) return fallback;
    auto value = gh::dtb::as_string(*kids[lane + 1]);
    if (!value || !valid_slot_color_name(*value)) return fallback;
    out[lane] = std::move(*value);
  }
  return out;
}

bool is_slot_gem_tex_name(const std::string& name,
                          const std::array<std::string, 5>& slot_colors) {
  for (const auto& slot_color : slot_colors) {
    if (name == lane_texture_name("gem_", slot_color, ".tex")) return true;
  }
  return false;
}

bool is_note_black_card_tex_name(const std::string& name,
                                 const std::array<std::string, 5>& slot_colors) {
  if (is_slot_gem_tex_name(name, slot_colors)) return true;
  if (name == "gem.tex" || name == "gem_bonus.tex" ||
      name == "gem_star.tex" || name == "gem_specular.tex" ||
      name == "gem_specular_star.tex" || name == "spade.tex") {
    return true;
  }
  if (name.rfind("spade_", 0) == 0 &&
      name.size() > 4 &&
      name.compare(name.size() - 4, 4, ".tex") == 0) {
    return true;
  }
  return false;
}

bool is_smasher_ring_add_mask_tex_name(const std::string& name) {
  return name.rfind("now_", 0) == 0 && name.size() > 8 &&
         name.compare(name.size() - 8, 8, "_add.tex") == 0;
}

std::string smasher_ring_add_alpha_key_alias(const std::string& name) {
  return name + kSmasherRingAddAlphaKeySuffix;
}

bool is_smasher_ring_add_alpha_key_alias(const std::string& name) {
  const std::string suffix = kSmasherRingAddAlphaKeySuffix;
  return name.size() > suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_smasher_add_mask_tex_name(const std::string& name) {
  return name.rfind("smasher_add", 0) == 0 && name.size() > 4 &&
         name.compare(name.size() - 4, 4, ".tex") == 0;
}

std::string smasher_add_alpha_key_alias(const std::string& name) {
  return name + kSmasherAddAlphaKeySuffix;
}

bool is_smasher_add_alpha_key_alias(const std::string& name) {
  const std::string suffix = kSmasherAddAlphaKeySuffix;
  return name.size() > suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint8_t lane_gem_alpha(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (r <= 8 && g <= 8 && b <= 8) return 0;
  return a;
}

uint8_t rgb_mask_alpha(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  const uint8_t mask = std::max({r, g, b});
  if (mask <= 8) return 0;
  return std::min(a, mask);
}

uint8_t texture_alpha_for_mode(HighwayTextureAlphaMode mode,
                               uint8_t r,
                               uint8_t g,
                               uint8_t b,
                               uint8_t a) {
  switch (mode) {
    case HighwayTextureAlphaMode::BlackKey:
      return lane_gem_alpha(r, g, b, a);
    case HighwayTextureAlphaMode::RgbMask:
      return rgb_mask_alpha(r, g, b, a);
    case HighwayTextureAlphaMode::Raw:
    default:
      return a;
  }
}

uint32_t sample_lane_color_from_gem(const ghogx::asset::Image& img,
                                    uint32_t fallback) {
  if (!img.valid()) return fallback;
  double sum_r = 0.0;
  double sum_g = 0.0;
  double sum_b = 0.0;
  double sum_w = 0.0;
  for (int y = 0; y < img.height; ++y) {
    const uint8_t* src =
        img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
    for (int x = 0; x < img.width; ++x) {
      const uint8_t r = src[x * 4 + 0];
      const uint8_t g = src[x * 4 + 1];
      const uint8_t b = src[x * 4 + 2];
      const uint8_t a = lane_gem_alpha(r, g, b, src[x * 4 + 3]);
      if (a < 32) continue;
      const uint8_t hi = std::max({r, g, b});
      const uint8_t lo = std::min({r, g, b});
      const uint8_t saturation = static_cast<uint8_t>(hi - lo);
      if (saturation < 24) continue;
      const double weight = static_cast<double>(a) * saturation;
      sum_r += static_cast<double>(r) * weight;
      sum_g += static_cast<double>(g) * weight;
      sum_b += static_cast<double>(b) * weight;
      sum_w += weight;
    }
  }
  if (sum_w <= 0.0) return fallback;
  const int alpha = static_cast<int>((fallback >> 24) & 0xff);
  const int r = std::clamp(static_cast<int>(sum_r / sum_w + 0.5), 0, 255);
  const int g = std::clamp(static_cast<int>(sum_g / sum_w + 0.5), 0, 255);
  const int b = std::clamp(static_cast<int>(sum_b / sum_w + 0.5), 0, 255);
  return D3DCOLOR_ARGB(alpha, r, g, b);
}

bool read_env_enabled(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  const bool enabled =
      _dupenv_s(&value, &len, name) == 0 && value && value[0] != '\0';
  std::free(value);
  return enabled;
}

std::optional<std::string> read_env_string_nonempty(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return std::nullopt;
  std::string text(value);
  std::free(value);
  if (text.empty() || text == "0") return std::nullopt;
  return text;
}

bool env_enabled(const char* name) {
  const std::string key(name ? name : "");
  static std::vector<std::pair<std::string, bool>> cached_values;
  for (const auto& entry : cached_values) {
    if (entry.first == key) return entry.second;
  }
  const bool value = read_env_enabled(name);
  cached_values.emplace_back(key, value);
  return value;
}

bool highway_mesh_uses_source_lighting(bool use_environ,
                                       bool prelit,
                                       bool point_lights) {
  return use_environ && point_lights && !prelit &&
         !env_enabled("GHOGX_DISABLE_HIGHWAY_SOURCE_LIGHTING");
}

std::optional<std::string> env_string_nonempty(const char* name) {
  const std::string key(name ? name : "");
  static std::vector<std::pair<std::string, std::optional<std::string>>>
      cached_values;
  for (const auto& entry : cached_values) {
    if (entry.first == key) return entry.second;
  }
  std::optional<std::string> value = read_env_string_nonempty(name);
  cached_values.emplace_back(key, value);
  return value;
}

void append_debug_rect(std::vector<V2>& out, float x, float y, float w, float h,
                       D3DCOLOR color) {
  const float x0 = x;
  const float y0 = y;
  const float x1 = x + w;
  const float y1 = y + h;
  out.push_back({x0, y0, 0.0f, 1.0f, color});
  out.push_back({x1, y0, 0.0f, 1.0f, color});
  out.push_back({x1, y1, 0.0f, 1.0f, color});
  out.push_back({x0, y0, 0.0f, 1.0f, color});
  out.push_back({x1, y1, 0.0f, 1.0f, color});
  out.push_back({x0, y1, 0.0f, 1.0f, color});
}

std::array<uint8_t, 7> debug_glyph(char ch) {
  switch (std::toupper(static_cast<unsigned char>(ch))) {
    case '0': return {0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu};
    case '1': return {0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu};
    case '2': return {0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu};
    case '3': return {0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu};
    case '4': return {0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u};
    case '5': return {0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu};
    case '6': return {0x06u, 0x08u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu};
    case '7': return {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u};
    case '8': return {0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu};
    case '9': return {0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x02u, 0x0cu};
    case 'A': return {0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u};
    case 'C': return {0x0fu, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x0fu};
    case 'D': return {0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu};
    case 'E': return {0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu};
    case 'G': return {0x0fu, 0x10u, 0x10u, 0x13u, 0x11u, 0x11u, 0x0fu};
    case 'H': return {0x11u, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u};
    case 'I': return {0x1fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x1fu};
    case 'K': return {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u};
    case 'L': return {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1fu};
    case 'M': return {0x11u, 0x1bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u};
    case 'N': return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    case 'O': return {0x0eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu};
    case 'P': return {0x1eu, 0x11u, 0x11u, 0x1eu, 0x10u, 0x10u, 0x10u};
    case 'R': return {0x1eu, 0x11u, 0x11u, 0x1eu, 0x14u, 0x12u, 0x11u};
    case 'S': return {0x0fu, 0x10u, 0x10u, 0x0eu, 0x01u, 0x01u, 0x1eu};
    case 'T': return {0x1fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
    case 'U': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu};
    case 'X': return {0x11u, 0x11u, 0x0au, 0x04u, 0x0au, 0x11u, 0x11u};
    case '-': return {0x00u, 0x00u, 0x00u, 0x1fu, 0x00u, 0x00u, 0x00u};
    case '.': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0cu, 0x0cu};
    case ' ': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    default:  return {0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u};
  }
}

float debug_text_width(const char* text, float scale) {
  const float step = 6.0f * scale;
  float width = 0.0f;
  for (const char* p = text; p && *p; ++p) width += step;
  return std::max(0.0f, width - scale);
}

void append_debug_text(std::vector<V2>& out, const char* text, float x, float y,
                       float scale, D3DCOLOR color) {
  float pen = x;
  for (const char* p = text; p && *p; ++p) {
    const std::array<uint8_t, 7> glyph = debug_glyph(*p);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((glyph[row] & (1u << (4 - col))) == 0) continue;
        append_debug_rect(out, pen + static_cast<float>(col) * scale,
                          y + static_cast<float>(row) * scale,
                          scale, scale, color);
      }
    }
    pen += 6.0f * scale;
  }
}

DWORD highway_note_cull_mode() {
  if (env_enabled("GHOGX_HIGHWAY_NOTE_CULL_NONE")) return D3DCULL_NONE;
  if (env_enabled("GHOGX_HIGHWAY_NOTE_CULL_CW")) return D3DCULL_CW;
  if (env_enabled("GHOGX_HIGHWAY_NOTE_CULL_CCW")) return D3DCULL_CCW;
  return D3DCULL_NONE;
}

DWORD highway_source_cull_mode(bool source_cull_enabled) {
  return source_cull_enabled ? D3DCULL_CW : D3DCULL_NONE;
}

float node_scalar_float(const gh::dtb::Node& node, float fallback) {
  if (!gh::dtb::is_array(node)) return fallback;
  const auto& kids = gh::dtb::children(node);
  if (kids.size() < 2 || !kids[1]) return fallback;
  if (auto f = gh::dtb::as_float(*kids[1])) return *f;
  if (auto i = gh::dtb::as_int(*kids[1])) return static_cast<float>(*i);
  return fallback;
}

float keyed_float(const gh::dtb::Tree& tree, const char* key, float fallback) {
  auto node = gh::dtb::find_keyed(tree, key);
  return node ? node_scalar_float(*node, fallback) : fallback;
}

float keyed_child_float(const gh::dtb::Node& parent, const char* key,
                        float fallback) {
  auto node = gh::dtb::find_keyed(parent, key);
  return node ? node_scalar_float(*node, fallback) : fallback;
}

uint32_t read_u32_unaligned_at(const uint8_t* body, size_t size,
                               size_t offset) {
  if (offset + 4 > size) return 0;
  uint32_t value = 0;
  std::memcpy(&value, body + offset, sizeof(value));
  return value;
}

float read_f32_unaligned_at(const uint8_t* body, size_t size, size_t offset) {
  if (offset + 4 > size) return 0.0f;
  float value = 0.0f;
  std::memcpy(&value, body + offset, sizeof(value));
  return value;
}

bool body_contains_milo_string(const uint8_t* body, size_t size,
                               const char* wanted) {
  const std::string_view target(wanted);
  for (size_t offset = 0; offset + 4 <= size; ++offset) {
    const uint32_t len = read_u32_unaligned_at(body, size, offset);
    if (len != target.size() || offset + 4 + len > size) continue;
    const char* src = reinterpret_cast<const char*>(body + offset + 4);
    if (std::string_view(src, len) == target) return true;
  }
  return false;
}

std::optional<float> track_panel_y_per_second_from_body(
    const uint8_t* body, size_t size) {
  if (!body || size < 96) return std::nullopt;
  if (!body_contains_milo_string(body, size, "track.cam")) return std::nullopt;

  // GH2 PS2 PanelDir bodies begin with a small unaligned header followed by a
  // count of 3x4 matrices. In the stock track PanelDir, the y_per_second scalar
  // sits in the scalar block immediately after that matrix table.
  const size_t scan_limit = std::min<size_t>(size, 32);
  for (size_t matrix_count_offset = 0; matrix_count_offset + 4 <= scan_limit;
       ++matrix_count_offset) {
    const uint32_t matrix_count =
        read_u32_unaligned_at(body, size, matrix_count_offset);
    if (matrix_count < 4 || matrix_count > 16) continue;
    const size_t matrices_offset = matrix_count_offset + 4;
    const size_t scalar_block = matrices_offset + matrix_count * 12u * 4u;
    const size_t yps_offset = scalar_block + 0x10;
    if (yps_offset + 4 > size) continue;
    const float candidate = read_f32_unaligned_at(body, size, yps_offset);
    if (!std::isfinite(candidate) || candidate < 20.0f ||
        candidate > 300.0f) {
      continue;
    }
    return candidate;
  }
  return std::nullopt;
}

std::optional<float> load_track_panel_y_per_second(
    const gh::ark::ArkV3Reader& ark, const std::string& ark_path) {
  auto entry = ark.find("track/gen/track.milo_ps2");
  if (!entry) entry = ark.find("../../system/run/track/gen/track.milo_ps2");
  if (!entry) return std::nullopt;
  const auto bytes = ark.read_entry(*entry, {ark_path});
  const auto hdr = gh::milo::parse_header(bytes);
  const auto payload = gh::milo::inflate_payload(bytes, hdr);
  const auto dir = gh::milo::parse_directory(payload);
  if (dir.dir_type != "PanelDir" || dir.dir_name != "track" ||
      dir.dir_entry_offset + dir.dir_entry_size > payload.size()) {
    return std::nullopt;
  }
  return track_panel_y_per_second_from_body(
      payload.data() + dir.dir_entry_offset,
      static_cast<size_t>(dir.dir_entry_size));
}

std::vector<HighwayRenderer::QuatAnimKey> decode_transanim_rotation_keys(
    const uint8_t* body, size_t size) {
  std::vector<HighwayRenderer::QuatAnimKey> best;
  float best_delta = 0.0f;
  for (size_t off = 0; off + 4 <= size; ++off) {
    const uint32_t count = read_u32_unaligned_at(body, size, off);
    if (count < 2 || count > 128) continue;
    const size_t start = off + 4;
    if (start + static_cast<size_t>(count) * 20 > size) continue;
    std::vector<HighwayRenderer::QuatAnimKey> keys;
    keys.reserve(count);
    float prev_frame = -1.0f;
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t p = start + static_cast<size_t>(i) * 20;
      HighwayRenderer::QuatAnimKey key;
      key.x = read_f32_unaligned_at(body, size, p + 0);
      key.y = read_f32_unaligned_at(body, size, p + 4);
      key.z = read_f32_unaligned_at(body, size, p + 8);
      key.w = read_f32_unaligned_at(body, size, p + 12);
      key.frame = read_f32_unaligned_at(body, size, p + 16);
      const float norm = std::sqrt(key.x * key.x + key.y * key.y +
                                   key.z * key.z + key.w * key.w);
      if (!std::isfinite(norm) || norm < 0.5f || norm > 1.5f ||
          !std::isfinite(key.frame) || key.frame < prev_frame ||
          key.frame > 1000.0f) {
        ok = false;
        break;
      }
      prev_frame = key.frame;
      keys.push_back(key);
    }
    if (!ok) continue;
    float delta = 0.0f;
    const auto& first = keys.front();
    for (const auto& key : keys) {
      const float dot = std::abs(first.x * key.x + first.y * key.y +
                                 first.z * key.z + first.w * key.w);
      delta = std::max(delta, 1.0f - std::min(dot, 1.0f));
    }
    if (delta > best_delta && delta > 0.000001f) {
      best_delta = delta;
      best = std::move(keys);
    }
  }
  return best;
}

struct TransAnimVec3Block {
  std::vector<HighwayRenderer::Vec3AnimKey> keys;
  float delta = 0.0f;
  bool scale_like = false;
};

std::vector<TransAnimVec3Block> decode_transanim_vec3_blocks(
    const uint8_t* body, size_t size) {
  std::vector<TransAnimVec3Block> blocks;
  auto plausible_vec = [](float value) {
    return std::isfinite(value) && std::abs(value) < 2000.0f;
  };
  for (size_t off = 0; off + 4 <= size; ++off) {
    const uint32_t count = read_u32_unaligned_at(body, size, off);
    if (count < 2 || count > 128) continue;
    const size_t start = off + 4;
    if (start + static_cast<size_t>(count) * 16 > size) continue;

    TransAnimVec3Block block;
    block.keys.reserve(count);
    float prev_frame = -1.0f;
    bool ok = true;
    bool scale_like = true;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t p = start + static_cast<size_t>(i) * 16;
      HighwayRenderer::Vec3AnimKey key;
      key.x = read_f32_unaligned_at(body, size, p + 0);
      key.y = read_f32_unaligned_at(body, size, p + 4);
      key.z = read_f32_unaligned_at(body, size, p + 8);
      key.frame = read_f32_unaligned_at(body, size, p + 12);
      if (!plausible_vec(key.x) || !plausible_vec(key.y) ||
          !plausible_vec(key.z) || !std::isfinite(key.frame) ||
          key.frame < 0.0f || key.frame < prev_frame ||
          key.frame > 1000.0f) {
        ok = false;
        break;
      }
      for (float value : {key.x, key.y, key.z}) {
        if (value <= 0.001f || value > 20.0f) scale_like = false;
      }
      prev_frame = key.frame;
      block.keys.push_back(key);
    }
    if (!ok) continue;
    for (float value : {block.keys.front().x, block.keys.front().y,
                        block.keys.front().z}) {
      if (value < 0.05f || value > 5.0f) scale_like = false;
    }
    float delta = 0.0f;
    const auto& first = block.keys.front();
    for (const auto& key : block.keys) {
      const float dx = key.x - first.x;
      const float dy = key.y - first.y;
      const float dz = key.z - first.z;
      delta = std::max(delta, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (delta <= 0.001f) continue;
    block.delta = delta;
    block.scale_like = scale_like;
    blocks.push_back(std::move(block));
  }
  return blocks;
}

HighwayRenderer::MeshTransformAnim decode_transanim_transform_anim(
    const uint8_t* body, size_t size) {
  HighwayRenderer::MeshTransformAnim anim;
  const auto blocks = decode_transanim_vec3_blocks(body, size);
  const TransAnimVec3Block* translation = nullptr;
  const TransAnimVec3Block* fallback_translation = nullptr;
  const TransAnimVec3Block* scale = nullptr;
  for (const auto& block : blocks) {
    if (!fallback_translation || block.delta > fallback_translation->delta) {
      fallback_translation = &block;
    }
    if (!block.scale_like &&
        (!translation || block.delta > translation->delta)) {
      translation = &block;
    }
  }
  if (!translation && fallback_translation && !fallback_translation->scale_like) {
    translation = fallback_translation;
  }
  for (const auto& block : blocks) {
    if (&block == translation || !block.scale_like) continue;
    if (!scale || block.delta > scale->delta) scale = &block;
  }
  if (translation) anim.translation_keys = translation->keys;
  if (scale) anim.scale_keys = scale->keys;
  anim.rotation_keys = decode_transanim_rotation_keys(body, size);
  return anim;
}

bool mesh_transform_anim_empty(const HighwayRenderer::MeshTransformAnim& anim) {
  return anim.translation_keys.empty() && anim.rotation_keys.empty() &&
         anim.scale_keys.empty();
}

float quat_anim_duration_frames(
    const std::vector<HighwayRenderer::QuatAnimKey>& keys) {
  float duration = 0.0f;
  for (const auto& key : keys) {
    if (std::isfinite(key.frame)) duration = std::max(duration, key.frame);
  }
  return duration;
}

float vec3_anim_duration_frames(
    const std::vector<HighwayRenderer::Vec3AnimKey>& keys) {
  float duration = 0.0f;
  for (const auto& key : keys) {
    if (std::isfinite(key.frame)) duration = std::max(duration, key.frame);
  }
  return duration;
}

float mesh_transform_anim_duration_frames(
    const HighwayRenderer::MeshTransformAnim& anim) {
  return std::max({vec3_anim_duration_frames(anim.translation_keys),
                   quat_anim_duration_frames(anim.rotation_keys),
                   vec3_anim_duration_frames(anim.scale_keys)});
}

std::vector<HighwayRenderer::QuatAnimKey> load_track_transanim_rotation_keys(
    const std::string& hdr_path, const std::string& ark_path,
    const char* anim_name) {
  std::vector<HighwayRenderer::QuatAnimKey> out;
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("track/gen/track.milo_ps2");
    if (!entry) entry = ark.find("../../system/run/track/gen/track.milo_ps2");
    if (!entry) return out;
    const auto bytes = ark.read_entry(*entry, {ark_path});
    const auto hdr = gh::milo::parse_header(bytes);
    const auto payload = gh::milo::inflate_payload(bytes, hdr);
    const auto dir = gh::milo::parse_directory(payload);
    for (const auto& de : dir.entries) {
      if (de.type != "TransAnim" || de.name != anim_name ||
          de.offset + de.size > payload.size()) {
        continue;
      }
      out = decode_transanim_rotation_keys(
          payload.data() + de.offset, static_cast<size_t>(de.size));
      break;
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[highway] track TransAnim %s load failed: %s\n",
                 anim_name, ex.what());
  }
  return out;
}

HighwayRenderer::MeshTransformAnim load_track_transanim_transform_anim(
    const std::string& hdr_path, const std::string& ark_path,
    const char* anim_name) {
  HighwayRenderer::MeshTransformAnim out;
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("track/gen/track.milo_ps2");
    if (!entry) entry = ark.find("../../system/run/track/gen/track.milo_ps2");
    if (!entry) return out;
    const auto bytes = ark.read_entry(*entry, {ark_path});
    const auto hdr = gh::milo::parse_header(bytes);
    const auto payload = gh::milo::inflate_payload(bytes, hdr);
    const auto dir = gh::milo::parse_directory(payload);
    for (const auto& de : dir.entries) {
      if (de.type != "TransAnim" || de.name != anim_name ||
          de.offset + de.size > payload.size()) {
        continue;
      }
      out = decode_transanim_transform_anim(
          payload.data() + de.offset, static_cast<size_t>(de.size));
      break;
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[highway] track TransAnim %s load failed: %s\n",
                 anim_name, ex.what());
  }
  return out;
}

std::array<float, 4> normalize_quat(std::array<float, 4> q) {
  const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] +
                              q[3] * q[3]);
  if (!std::isfinite(len) || len <= 0.000001f) {
    return {0.0f, 0.0f, 0.0f, 1.0f};
  }
  for (float& value : q) value /= len;
  return q;
}

std::array<float, 3> normalize_vec3(std::array<float, 3> v) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!std::isfinite(len) || len <= 0.00001f) {
    return {0.0f, 0.0f, 1.0f};
  }
  const float inv = 1.0f / len;
  return {v[0] * inv, v[1] * inv, v[2] * inv};
}

std::array<float, 4> quat_conjugate(std::array<float, 4> q) {
  q[0] = -q[0];
  q[1] = -q[1];
  q[2] = -q[2];
  return q;
}

std::array<float, 4> quat_mul(const std::array<float, 4>& a,
                              const std::array<float, 4>& b) {
  const float ax = a[0], ay = a[1], az = a[2], aw = a[3];
  const float bx = b[0], by = b[1], bz = b[2], bw = b[3];
  return normalize_quat({
      aw * bx + ax * bw + ay * bz - az * by,
      aw * by - ax * bz + ay * bw + az * bx,
      aw * bz + ax * by - ay * bx + az * bw,
      aw * bw - ax * bx - ay * by - az * bz,
  });
}

std::array<float, 4> sample_quat_anim(
    const std::vector<HighwayRenderer::QuatAnimKey>& keys,
    float duration_frames, float frame) {
  if (keys.empty()) return {0.0f, 0.0f, 0.0f, 1.0f};
  if (keys.size() == 1 || duration_frames <= 0.001f) {
    return normalize_quat({keys.front().x, keys.front().y, keys.front().z,
                           keys.front().w});
  }
  if (std::isfinite(frame)) {
    frame = std::fmod(std::max(0.0f, frame), duration_frames);
  } else {
    frame = 0.0f;
  }
  const HighwayRenderer::QuatAnimKey* a = &keys.front();
  const HighwayRenderer::QuatAnimKey* b = &keys.back();
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (frame >= keys[i].frame && frame <= keys[i + 1].frame) {
      a = &keys[i];
      b = &keys[i + 1];
      break;
    }
  }
  const float span = std::max(0.001f, b->frame - a->frame);
  float t = std::clamp((frame - a->frame) / span, 0.0f, 1.0f);
  std::array<float, 4> qa = normalize_quat({a->x, a->y, a->z, a->w});
  std::array<float, 4> qb = normalize_quat({b->x, b->y, b->z, b->w});
  float dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
  if (dot < 0.0f) {
    dot = -dot;
    for (float& value : qb) value = -value;
  }
  dot = std::clamp(dot, -1.0f, 1.0f);
  if (dot > 0.9995f) {
    return normalize_quat({qa[0] + (qb[0] - qa[0]) * t,
                           qa[1] + (qb[1] - qa[1]) * t,
                           qa[2] + (qb[2] - qa[2]) * t,
                           qa[3] + (qb[3] - qa[3]) * t});
  }
  const float theta = std::acos(dot);
  const float sin_theta = std::sin(theta);
  const float wa = std::sin((1.0f - t) * theta) / sin_theta;
  const float wb = std::sin(t * theta) / sin_theta;
  return normalize_quat({qa[0] * wa + qb[0] * wb,
                         qa[1] * wa + qb[1] * wb,
                         qa[2] * wa + qb[2] * wb,
                         qa[3] * wa + qb[3] * wb});
}

std::array<float, 3> sample_vec3_anim(
    const std::vector<HighwayRenderer::Vec3AnimKey>& keys,
    float duration_frames, float frame,
    std::array<float, 3> fallback) {
  if (keys.empty()) return fallback;
  if (keys.size() == 1 || duration_frames <= 0.001f) {
    return {keys.front().x, keys.front().y, keys.front().z};
  }
  if (std::isfinite(frame)) {
    frame = std::fmod(std::max(0.0f, frame), duration_frames);
  } else {
    frame = 0.0f;
  }
  const HighwayRenderer::Vec3AnimKey* a = &keys.front();
  const HighwayRenderer::Vec3AnimKey* b = &keys.back();
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (frame >= keys[i].frame && frame <= keys[i + 1].frame) {
      a = &keys[i];
      b = &keys[i + 1];
      break;
    }
  }
  const float span = std::max(0.001f, b->frame - a->frame);
  const float t = std::clamp((frame - a->frame) / span, 0.0f, 1.0f);
  return {a->x + (b->x - a->x) * t,
          a->y + (b->y - a->y) * t,
          a->z + (b->z - a->z) * t};
}

HighwayRenderer::MeshTransformSample sample_transform_anim(
    const HighwayRenderer::MeshTransformAnim& anim,
    float duration_frames, float frame) {
  HighwayRenderer::MeshTransformSample out;
  if (!anim.translation_keys.empty()) {
    out.has_translation = true;
    out.translation = sample_vec3_anim(anim.translation_keys, duration_frames,
                                       frame, out.translation);
  }
  if (!anim.rotation_keys.empty()) {
    out.has_rotation = true;
    out.rotation_xyzw = sample_quat_anim(anim.rotation_keys, duration_frames,
                                         frame);
  }
  if (!anim.scale_keys.empty()) {
    out.has_scale = true;
    out.scale = sample_vec3_anim(anim.scale_keys, duration_frames, frame,
                                 out.scale);
  }
  return out;
}

HighwayRenderer::MeshTransformSample sample_transform_anim_delta(
    const HighwayRenderer::MeshTransformAnim& anim,
    float duration_frames, float frame) {
  HighwayRenderer::MeshTransformSample out;
  if (anim.translation_keys.size() >= 2) {
    const auto sampled = sample_vec3_anim(anim.translation_keys,
                                          duration_frames, frame,
                                          {0.0f, 0.0f, 0.0f});
    const auto& base = anim.translation_keys.front();
    out.has_translation = true;
    out.translation = {
        sampled[0] - base.x,
        sampled[1] - base.y,
        sampled[2] - base.z,
    };
  }
  if (anim.rotation_keys.size() >= 2) {
    const auto sampled =
        sample_quat_anim(anim.rotation_keys, duration_frames, frame);
    const auto& base_key = anim.rotation_keys.front();
    const auto base =
        normalize_quat({base_key.x, base_key.y, base_key.z, base_key.w});
    out.has_rotation = true;
    out.rotation_xyzw = quat_mul(quat_conjugate(base), sampled);
  }
  if (anim.scale_keys.size() >= 2) {
    const auto sampled = sample_vec3_anim(anim.scale_keys, duration_frames,
                                          frame, {1.0f, 1.0f, 1.0f});
    const auto& base = anim.scale_keys.front();
    auto ratio = [](float value, float base_value) {
      if (!std::isfinite(base_value) || std::abs(base_value) <= 0.0001f) {
        return value;
      }
      return value / base_value;
    };
    out.has_scale = true;
    out.scale = {
        ratio(sampled[0], base.x),
        ratio(sampled[1], base.y),
        ratio(sampled[2], base.z),
    };
  }
  return out;
}

std::array<float, 3> rotate_vec_by_quat(const std::array<float, 3>& v,
                                        const std::array<float, 4>& q) {
  const std::array<float, 3> u = {q[0], q[1], q[2]};
  const float s = q[3];
  const std::array<float, 3> uv = {
      u[1] * v[2] - u[2] * v[1],
      u[2] * v[0] - u[0] * v[2],
      u[0] * v[1] - u[1] * v[0],
  };
  const std::array<float, 3> uuv = {
      u[1] * uv[2] - u[2] * uv[1],
      u[2] * uv[0] - u[0] * uv[2],
      u[0] * uv[1] - u[1] * uv[0],
  };
  return {
      v[0] + 2.0f * (s * uv[0] + uuv[0]),
      v[1] + 2.0f * (s * uv[1] + uuv[1]),
      v[2] + 2.0f * (s * uv[2] + uuv[2]),
  };
}

inline float lane_x_for(float lane_spacing, int lane) {
  return (static_cast<float>(lane) - 2.0f) * lane_spacing;
}

struct HighwayRootSpace {
  float x_scale = 1.0f;
  float lane_spacing = kDefaultLaneSpacing;
  float board_half_x = kDefaultBoardHalfX;

  float lane_x(int lane) const {
    return lane_x_for(lane_spacing, lane);
  }

  float lane_boundary_x(int boundary) const {
    return (static_cast<float>(boundary) - 2.5f) * lane_spacing;
  }

  float scale_x(float local_x) const {
    return local_x * x_scale;
  }
};

enum class TailWidthProfileKind {
  kNone,
  kWhammyBody,
  kNormalBodyFill,
  kNormalBodyDetail,
  kNormalTightJoin,
};

struct HighwayAspectProfile {
  float track_x_scale = kPcsx2MeasuredTrackXScale4x3;
  float cam_x = kPcsx2MeasuredCamX4x3;
  float cam_y = kCamY;
  float cam_z = kCamZ;
  float cam_yaw_deg = kPcsx2MeasuredCamYawDeg4x3;
};

bool highway_aspect_is_4x3(float aspect) {
  if (!std::isfinite(aspect)) return true;
  constexpr float kAspect4x3 = 4.0f / 3.0f;
  constexpr float kAspect16x9 = 16.0f / 9.0f;
  return std::fabs(aspect - kAspect4x3) <=
         std::fabs(aspect - kAspect16x9);
}

HighwayAspectProfile pcsx2_measured_highway_profile_for_aspect(float aspect) {
  if (highway_aspect_is_4x3(aspect)) {
    return {kPcsx2MeasuredTrackXScale4x3,
            kPcsx2MeasuredCamX4x3,
            kCamY,
            kCamZ,
            kPcsx2MeasuredCamYawDeg4x3};
  }
  return {kPcsx2MeasuredTrackXScale16x9,
          kPcsx2MeasuredCamX16x9,
          kCamY,
          kCamZ,
          kPcsx2MeasuredCamYawDeg16x9};
}

float native_track_y_scale(float source_min_y, float source_max_y,
                           float native_top_y) {
  const float source_span = source_max_y - source_min_y;
  const float native_span = native_top_y - source_min_y;
  if (!std::isfinite(source_span) || !std::isfinite(native_span) ||
      source_span <= 0.001f || native_span <= source_span + 0.001f) {
    return 1.0f;
  }
  return native_span / source_span;
}

std::array<float, 3> highway_camera_forward(
    const HighwayAspectProfile& profile) {
  const float pitch = kCamPitchDeg * 3.14159265358979323846f / 180.0f;
  const float yaw = profile.cam_yaw_deg * 3.14159265358979323846f / 180.0f;
  const float cp = std::cos(pitch);
  return {std::sin(yaw) * cp, std::cos(yaw) * cp, -std::sin(pitch)};
}

std::array<float, 3> highway_camera_up(
    const HighwayAspectProfile&) {
  const float pitch = kCamPitchDeg * 3.14159265358979323846f / 180.0f;
  return {0.0f, std::sin(pitch), std::cos(pitch)};
}

ghogx::render::Mat4 highway_camera_view(const HighwayAspectProfile& profile) {
  const auto fwd = highway_camera_forward(profile);
  const auto up = highway_camera_up(profile);
  return ghogx::render::Mat4::look_at_lh(profile.cam_x, profile.cam_y,
                                         profile.cam_z,
                                         profile.cam_x + fwd[0],
                                         profile.cam_y + fwd[1],
                                         profile.cam_z + fwd[2],
                                         up[0], up[1], up[2]);
}

struct MatAnimColorKey {
  float rgb[3] = {1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;
  float frame = 0.0f;
};

struct MatAnimAlphaKey {
  float alpha = 1.0f;
  float frame = 0.0f;
};

struct MatAnimColorKeys {
  std::string material;
  std::vector<MatAnimColorKey> keys;
  std::vector<MatAnimAlphaKey> alpha_keys;
};

struct TrackParticleAnim {
  std::string particle;
  std::string keys_owner;
  std::vector<HighwayRenderer::ParticleColorAnimKey> start_color_keys;
  std::vector<HighwayRenderer::ParticleColorAnimKey> end_color_keys;
  std::vector<HighwayRenderer::ParticleVector2AnimKey> emit_rate_keys;
  std::vector<HighwayRenderer::ParticleVector2AnimKey> speed_keys;
  std::vector<HighwayRenderer::ParticleVector2AnimKey> life_keys;
  std::vector<HighwayRenderer::ParticleVector2AnimKey> start_size_keys;
};

float clamp_color(float value) {
  if (!std::isfinite(value)) return 1.0f;
  return std::clamp(value, 0.0f, 1.0f);
}

bool read_u32(const uint8_t* body, size_t size, size_t& pos, uint32_t& out) {
  if (pos + 4 > size) return false;
  std::memcpy(&out, body + pos, sizeof(out));
  pos += 4;
  return true;
}

bool read_f32(const uint8_t* body, size_t size, size_t& pos, float& out) {
  if (pos + 4 > size) return false;
  std::memcpy(&out, body + pos, sizeof(out));
  pos += 4;
  return std::isfinite(out);
}

std::optional<std::string> read_milo_string(const uint8_t* body, size_t size,
                                            size_t& pos) {
  uint32_t len = 0;
  if (!read_u32(body, size, pos, len) || len == 0 || len > 128 ||
      pos + len > size) {
    return std::nullopt;
  }
  std::string s(reinterpret_cast<const char*>(body + pos), len);
  pos += len;
  return s;
}

std::map<std::string, MatAnimColorKeys> load_track_mat_anim_colors(
    const std::string& hdr_path, const std::string& ark_path) {
  std::map<std::string, MatAnimColorKeys> out;
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("track/gen/track.milo_ps2");
    if (!entry) return out;
    auto bytes = ark.read_entry(*entry, {ark_path});
    auto hdr = gh::milo::parse_header(bytes);
    auto payload = gh::milo::inflate_payload(bytes, hdr);
    auto dir = gh::milo::parse_directory(payload);
    for (const auto& de : dir.entries) {
      if (de.type != "MatAnim" || de.offset + de.size > payload.size())
        continue;
      const auto* body = payload.data() + de.offset;
      const size_t size = static_cast<size_t>(de.size);
      if (size < 48) continue;
      uint32_t version = 0;
      std::memcpy(&version, body, sizeof(version));
      if (version != 7) continue;
      size_t pos = 25;
      auto material = read_milo_string(body, size, pos);
      auto anim_name = read_milo_string(body, size, pos);
      if (!material || !anim_name)
        continue;
      uint32_t color_count = 0;
      if (!read_u32(body, size, pos, color_count) || color_count > 16)
        continue;
      MatAnimColorKeys anim;
      anim.material = *material;
      for (uint32_t i = 0; i < color_count; ++i) {
        MatAnimColorKey key;
        if (!read_f32(body, size, pos, key.rgb[0]) ||
            !read_f32(body, size, pos, key.rgb[1]) ||
            !read_f32(body, size, pos, key.rgb[2]) ||
            !read_f32(body, size, pos, key.alpha) ||
            !read_f32(body, size, pos, key.frame)) {
          anim.keys.clear();
          break;
        }
        key.rgb[0] = clamp_color(key.rgb[0]);
        key.rgb[1] = clamp_color(key.rgb[1]);
        key.rgb[2] = clamp_color(key.rgb[2]);
        key.alpha = clamp_color(key.alpha);
        anim.keys.push_back(key);
      }
      if (color_count != anim.keys.size()) continue;
      if (pos + 4 <= size) {
        uint32_t alpha_count = 0;
        if (!read_u32(body, size, pos, alpha_count) || alpha_count > 16)
          continue;
        for (uint32_t i = 0; i < alpha_count; ++i) {
          MatAnimAlphaKey key;
          if (!read_f32(body, size, pos, key.alpha) ||
              !read_f32(body, size, pos, key.frame)) {
            anim.alpha_keys.clear();
            break;
          }
          key.alpha = clamp_color(key.alpha);
          anim.alpha_keys.push_back(key);
        }
        if (alpha_count != anim.alpha_keys.size()) continue;
      }
      if (!anim.keys.empty() || !anim.alpha_keys.empty()) {
        out[*anim_name] = std::move(anim);
      }
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[highway] track side-rail MatAnim load: %s\n",
                 ex.what());
  }
  return out;
}

HighwayRenderer::SideRailColorState side_rail_color_from_anim(
    const std::map<std::string, MatAnimColorKeys>& anims,
    const std::string& name,
    bool use_last_key) {
  HighwayRenderer::SideRailColorState out;
  const auto it = anims.find(name);
  if (it == anims.end() || it->second.keys.empty()) return out;
  const auto& key = use_last_key ? it->second.keys.back()
                                : it->second.keys.front();
  out.r = key.rgb[0];
  out.g = key.rgb[1];
  out.b = key.rgb[2];
  // RndMatAnim::SetFrame sends ColorKeys through RndMat::SetColor, and
  // SetColor copies RGB only.  Material alpha is a separate AlphaKeys channel.
  // The GH2 neutral rail color key even contains non-semantic low bits in its
  // fourth component, so treating that component as alpha hides the white
  // source mesh.
  out.a = 1.0f;
  if (!it->second.alpha_keys.empty()) {
    out.a = use_last_key ? it->second.alpha_keys.back().alpha
                         : it->second.alpha_keys.front().alpha;
  }
  out.ok = true;
  return out;
}

HighwayRenderer::MeshTransformAnim load_track_intro_transanim_source_order(
    const std::string& hdr_path, const std::string& ark_path,
    const char* anim_name) {
  HighwayRenderer::MeshTransformAnim out;
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("track/gen/track.milo_ps2");
    if (!entry) entry = ark.find("../../system/run/track/gen/track.milo_ps2");
    if (!entry) return out;
    const auto bytes = ark.read_entry(*entry, {ark_path});
    const auto hdr = gh::milo::parse_header(bytes);
    const auto payload = gh::milo::inflate_payload(bytes, hdr);
    const auto dir = gh::milo::parse_directory(payload);
    for (const auto& de : dir.entries) {
      if (de.type != "TransAnim" || de.name != anim_name ||
          de.offset + de.size > payload.size()) {
        continue;
      }

      const auto decoded =
          ghogx::milo_scene::decode_rndtrans_anim_body_source_order(
              payload.data() + de.offset, static_cast<size_t>(de.size));
      if (!decoded.decoded) {
        std::fprintf(stderr,
                     "[highway] track intro TransAnim %s source-order decode failed at %zu/%llu: %s\n",
                     anim_name, decoded.bytes_consumed,
                     static_cast<unsigned long long>(de.size),
                     decoded.error.c_str());
        return out;
      }
      if (decoded.target != "track.cam") {
        std::fprintf(stderr,
                     "[highway] track intro TransAnim %s rejected target=%s (expected track.cam)\n",
                     anim_name, decoded.target.c_str());
        return out;
      }
      if (!decoded.keys_owner.empty() && decoded.keys_owner != anim_name) {
        std::fprintf(stderr,
                     "[highway] track intro TransAnim %s external keys owner=%s is unresolved\n",
                     anim_name, decoded.keys_owner.c_str());
        return out;
      }

      out.translation_keys.reserve(decoded.translation_keys.size());
      for (const auto& source_key : decoded.translation_keys) {
        HighwayRenderer::Vec3AnimKey key;
        key.x = source_key.value[0];
        key.y = source_key.value[1];
        key.z = source_key.value[2];
        key.frame = source_key.frame;
        out.translation_keys.push_back(key);
      }
      out.rotation_keys.reserve(decoded.rotation_keys.size());
      for (const auto& source_key : decoded.rotation_keys) {
        HighwayRenderer::QuatAnimKey key;
        key.x = source_key.value[0];
        key.y = source_key.value[1];
        key.z = source_key.value[2];
        key.w = source_key.value[3];
        key.frame = source_key.frame;
        out.rotation_keys.push_back(key);
      }
      out.scale_keys.reserve(decoded.scale_keys.size());
      for (const auto& source_key : decoded.scale_keys) {
        HighwayRenderer::Vec3AnimKey key;
        key.x = source_key.value[0];
        key.y = source_key.value[1];
        key.z = source_key.value[2];
        key.frame = source_key.frame;
        out.scale_keys.push_back(key);
      }

      const auto frame_0 =
          ghogx::milo_scene::sample_rndtrans_anim_translation(decoded, 0.0f);
      const auto frame_720 =
          ghogx::milo_scene::sample_rndtrans_anim_translation(decoded, 720.0f);
      const auto frame_1440 =
          ghogx::milo_scene::sample_rndtrans_anim_translation(decoded, 1440.0f);
      const auto frame_1920 =
          ghogx::milo_scene::sample_rndtrans_anim_translation(decoded, 1920.0f);
      std::fprintf(
          stderr,
          "[highway] track intro TransAnim %s source_reader=MiloEditor::RndTransAnim.Read rev=%u anim_rev=%u rate=%d target=%s owner=%s pos=%zu rot=%zu scale=%zu flags=trans_spline:%d repeat:%d scale_spline:%d follow_path:%d rot_slerp:%d rot_spline:%d bytes=%zu/%llu eof_exact=%d panel_range=0..1920 samples_y=(f0:%.3f f720:%.3f f1440:%.3f f1920:%.3f)\n",
          anim_name, decoded.revision, decoded.anim_revision,
          decoded.anim_rate, decoded.target.c_str(),
          decoded.keys_owner.empty() ? "<self>" : decoded.keys_owner.c_str(),
          decoded.translation_keys.size(), decoded.rotation_keys.size(),
          decoded.scale_keys.size(), decoded.trans_spline ? 1 : 0,
          decoded.repeat_trans ? 1 : 0, decoded.scale_spline ? 1 : 0,
          decoded.follow_path ? 1 : 0, decoded.rot_slerp ? 1 : 0,
          decoded.rot_spline ? 1 : 0, decoded.bytes_consumed,
          static_cast<unsigned long long>(de.size),
          decoded.exact_eof ? 1 : 0, frame_0[1], frame_720[1],
          frame_1440[1], frame_1920[1]);
      break;
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr,
                 "[highway] track intro TransAnim %s load failed: %s\n",
                 anim_name, ex.what());
  }
  return out;
}

std::array<float, 3> sample_vec3_anim_clamped(
    const std::vector<HighwayRenderer::Vec3AnimKey>& keys, float frame,
    const std::array<float, 3>& fallback) {
  if (keys.empty()) return fallback;
  if (keys.size() == 1 || frame <= keys.front().frame) {
    return {keys.front().x, keys.front().y, keys.front().z};
  }
  if (frame >= keys.back().frame) {
    return {keys.back().x, keys.back().y, keys.back().z};
  }
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (frame < keys[i].frame || frame > keys[i + 1].frame) continue;
    const auto& a = keys[i];
    const auto& b = keys[i + 1];
    const float t = std::clamp(
        (frame - a.frame) / std::max(0.001f, b.frame - a.frame), 0.0f,
        1.0f);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
  }
  return {keys.back().x, keys.back().y, keys.back().z};
}

std::map<std::string, TrackParticleAnim> load_track_particle_anims(
    const std::string& hdr_path, const std::string& ark_path) {
  std::map<std::string, TrackParticleAnim> out;
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("track/gen/track.milo_ps2");
    if (!entry) entry = ark.find("../../system/run/track/gen/track.milo_ps2");
    if (!entry) return out;
    const auto bytes = ark.read_entry(*entry, {ark_path});
    const auto hdr = gh::milo::parse_header(bytes);
    const auto payload = gh::milo::inflate_payload(bytes, hdr);
    const auto dir = gh::milo::parse_directory(payload);

    auto read_count = [](const uint8_t* body, size_t size, size_t& pos,
                         uint32_t& count) {
      return read_u32(body, size, pos, count) && count <= 4096;
    };
    auto read_color_keys = [&](const uint8_t* body, size_t size, size_t& pos,
                               std::vector<HighwayRenderer::ParticleColorAnimKey>& keys) {
      uint32_t count = 0;
      if (!read_count(body, size, pos, count)) return false;
      keys.clear();
      keys.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        HighwayRenderer::ParticleColorAnimKey key;
        for (float& channel : key.color) {
          if (!read_f32(body, size, pos, channel)) return false;
        }
        if (!read_f32(body, size, pos, key.frame) || key.frame < 0.0f)
          return false;
        keys.push_back(key);
      }
      std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
      });
      return true;
    };
    auto read_vector2_keys = [&](const uint8_t* body, size_t size, size_t& pos,
                                 std::vector<HighwayRenderer::ParticleVector2AnimKey>& keys) {
      uint32_t count = 0;
      if (!read_count(body, size, pos, count)) return false;
      keys.clear();
      keys.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        HighwayRenderer::ParticleVector2AnimKey key;
        if (!read_f32(body, size, pos, key.value[0]) ||
            !read_f32(body, size, pos, key.value[1]) ||
            !read_f32(body, size, pos, key.frame) || key.frame < 0.0f) {
          return false;
        }
        keys.push_back(key);
      }
      std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
      });
      return true;
    };

    for (const auto& de : dir.entries) {
      if (de.type != "ParticleSysAnim" ||
          de.offset + de.size > payload.size()) {
        continue;
      }
      const auto* body = payload.data() + de.offset;
      const size_t size = static_cast<size_t>(de.size);
      if (size < 29) continue;
      uint32_t revision = 0;
      std::memcpy(&revision, body, sizeof(revision));
      if ((revision & 0xffffu) != 3u) continue;

      // GH2 rev-3 source order is Object, RndAnimatable, particle target,
      // start/end colors, emit rate, keys owner, then speed/life/start size.
      // Object plus RndAnimatable occupies 25 bytes in the retail entries.
      size_t pos = 25;
      auto particle = read_milo_string(body, size, pos);
      if (!particle) continue;
      TrackParticleAnim anim;
      anim.particle = *particle;
      if (!read_color_keys(body, size, pos, anim.start_color_keys) ||
          !read_color_keys(body, size, pos, anim.end_color_keys) ||
          !read_vector2_keys(body, size, pos, anim.emit_rate_keys)) {
        continue;
      }
      auto owner = read_milo_string(body, size, pos);
      if (!owner) continue;
      anim.keys_owner = *owner;
      if (!read_vector2_keys(body, size, pos, anim.speed_keys) ||
          !read_vector2_keys(body, size, pos, anim.life_keys) ||
          !read_vector2_keys(body, size, pos, anim.start_size_keys) ||
          pos != size) {
        continue;
      }
      out[de.name] = std::move(anim);
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[highway] track ParticleSysAnim load: %s\n",
                 ex.what());
  }
  return out;
}

HighwayRenderer::ColorAnimState mat_anim_color_curve(
    const std::map<std::string, MatAnimColorKeys>& anims,
    const std::string& name) {
  HighwayRenderer::ColorAnimState out;
  const auto it = anims.find(name);
  if (it == anims.end()) return out;
  if (!it->second.keys.empty()) {
    out.keys.reserve(it->second.keys.size());
    out.has_rgb = true;
    for (const auto& key : it->second.keys) {
      out.keys.push_back(HighwayRenderer::ColorAnimKey{
          key.rgb[0], key.rgb[1], key.rgb[2], key.frame});
    }
  }
  if (!it->second.alpha_keys.empty()) {
    out.alpha_keys.reserve(it->second.alpha_keys.size());
    out.has_alpha = true;
    for (const auto& key : it->second.alpha_keys) {
      out.alpha_keys.push_back(
          HighwayRenderer::ColorAnimAlphaKey{key.alpha, key.frame});
    }
  }
  out.ok = !out.keys.empty() || !out.alpha_keys.empty();
  return out;
}

HighwayRenderer::SideRailColorState sample_color_anim(
    const HighwayRenderer::ColorAnimState& anim, float frame) {
  HighwayRenderer::SideRailColorState out;
  if (!anim.ok || !std::isfinite(frame)) return out;
  out.ok = true;
  if (!anim.keys.empty()) {
    const auto sample_rgb = [&]() {
      if (frame <= anim.keys.front().frame || anim.keys.size() == 1) {
        return anim.keys.front();
      }
      for (size_t i = 1; i < anim.keys.size(); ++i) {
        const auto& prev = anim.keys[i - 1];
        const auto& next = anim.keys[i];
        if (frame > next.frame) continue;
        const float span = std::max(0.001f, next.frame - prev.frame);
        const float t =
            std::clamp((frame - prev.frame) / span, 0.0f, 1.0f);
        return HighwayRenderer::ColorAnimKey{
            prev.r + (next.r - prev.r) * t,
            prev.g + (next.g - prev.g) * t,
            prev.b + (next.b - prev.b) * t, frame};
      }
      return anim.keys.back();
    }();
    out.r = sample_rgb.r;
    out.g = sample_rgb.g;
    out.b = sample_rgb.b;
  }
  if (!anim.alpha_keys.empty()) {
    if (frame <= anim.alpha_keys.front().frame ||
        anim.alpha_keys.size() == 1) {
      out.a = anim.alpha_keys.front().a;
    } else {
      out.a = anim.alpha_keys.back().a;
      for (size_t i = 1; i < anim.alpha_keys.size(); ++i) {
        const auto& prev = anim.alpha_keys[i - 1];
        const auto& next = anim.alpha_keys[i];
        if (frame > next.frame) continue;
        const float span = std::max(0.001f, next.frame - prev.frame);
        const float t =
            std::clamp((frame - prev.frame) / span, 0.0f, 1.0f);
        out.a = prev.a + (next.a - prev.a) * t;
        break;
      }
    }
  }
  out.ok = true;
  return out;
}

float color_anim_last_frame(const HighwayRenderer::ColorAnimState& anim) {
  if (!anim.ok) return 0.0f;
  float frame = anim.keys.empty() ? 0.0f : anim.keys.back().frame;
  if (!anim.alpha_keys.empty()) {
    frame = std::max(frame, anim.alpha_keys.back().frame);
  }
  return frame;
}

float color_anim_peak_dark_frame(const HighwayRenderer::ColorAnimState& anim) {
  if (!anim.ok || anim.keys.empty()) return 0.0f;
  const HighwayRenderer::ColorAnimKey* best = &anim.keys.front();
  float best_luma = best->r + best->g + best->b;
  for (const auto& key : anim.keys) {
    const float luma = key.r + key.g + key.b;
    if (luma >= best_luma) continue;
    best = &key;
    best_luma = luma;
  }
  return best->frame;
}

HighwayRenderer::SideRailColorState lerp_side_rail_color(
    HighwayRenderer::SideRailColorState a,
    HighwayRenderer::SideRailColorState b,
    float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  if (!a.ok) a = {};
  if (!b.ok) b = a;
  HighwayRenderer::SideRailColorState out;
  out.r = a.r + (b.r - a.r) * t;
  out.g = a.g + (b.g - a.g) * t;
  out.b = a.b + (b.b - a.b) * t;
  out.a = a.a + (b.a - a.a) * t;
  out.ok = a.ok || b.ok;
  return out;
}

D3DCOLOR side_rail_d3d_color(HighwayRenderer::SideRailColorState color) {
  const int a = std::clamp(static_cast<int>(color.a * 255.0f), 0, 255);
  const int r = std::clamp(static_cast<int>(color.r * 255.0f), 0, 255);
  const int g = std::clamp(static_cast<int>(color.g * 255.0f), 0, 255);
  const int b = std::clamp(static_cast<int>(color.b * 255.0f), 0, 255);
  return D3DCOLOR_ARGB(a, r, g, b);
}

D3DCOLOR multiply_rgb(D3DCOLOR base, HighwayRenderer::SideRailColorState color,
                      float strength) {
  strength = std::clamp(strength, 0.0f, 1.0f);
  if (!color.ok || strength <= 0.0f) return base;
  const float cr = 1.0f + (color.r - 1.0f) * strength;
  const float cg = 1.0f + (color.g - 1.0f) * strength;
  const float cb = 1.0f + (color.b - 1.0f) * strength;
  const int a = static_cast<int>((base >> 24) & 0xff);
  const int r = std::clamp(
      static_cast<int>(static_cast<float>((base >> 16) & 0xff) * cr), 0, 255);
  const int g = std::clamp(
      static_cast<int>(static_cast<float>((base >> 8) & 0xff) * cg), 0, 255);
  const int b = std::clamp(
      static_cast<int>(static_cast<float>(base & 0xff) * cb), 0, 255);
  return D3DCOLOR_ARGB(a, r, g, b);
}

// Build a quad on the board (XY plane at height z), centered at (cx, cy),
// half-width hx (X across) and half-depth hy (Y along the track). Far = +Y.
void flat_quad(V3 out[4], float cx, float cy, float z, float hx, float hy,
               D3DCOLOR col) {
  out[0] = {cx - hx, cy + hy, z, 0.0f, 0.0f, 1.0f, col, 0.0f, 0.0f};  // far-left
  out[1] = {cx + hx, cy + hy, z, 0.0f, 0.0f, 1.0f, col, 1.0f, 0.0f};  // far-right
  out[2] = {cx - hx, cy - hy, z, 0.0f, 0.0f, 1.0f, col, 0.0f, 1.0f};  // near-left
  out[3] = {cx + hx, cy - hy, z, 0.0f, 0.0f, 1.0f, col, 1.0f, 1.0f};  // near-right
}

void bind_texture_diffuse_stage(IDirect3DDevice9* dev,
                                IDirect3DTexture9* texture,
                                bool use_texture_alpha,
                                bool intensify = false,
                                bool texture_wrap = true) {
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  if (texture) {
    const DWORD address =
        texture_wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, address);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, address);
    dev->SetTexture(0, texture);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,
                              intensify ? D3DTOP_MODULATE2X
                                        : D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,
                              use_texture_alpha ? D3DTOP_MODULATE
                                                : D3DTOP_SELECTARG2);
  } else {
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
  }
}

void draw_quad(IDirect3DDevice9* dev,
               IDirect3DTexture9* texture,
               const V3 c[4],
               bool use_texture_alpha = true,
               bool intensify = false,
               bool texture_wrap = true) {
  const V3 tris[6] = { c[0], c[1], c[2], c[1], c[3], c[2] };
  bind_texture_diffuse_stage(dev, texture, use_texture_alpha, intensify,
                             texture_wrap);
  dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, tris, sizeof(V3));
}

V3 lerp_vertex(const V3& a, const V3& b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const auto lerp = [t](float va, float vb) { return va + (vb - va) * t; };
  const float aa = static_cast<float>((a.c >> 24) & 0xff);
  const float ar = static_cast<float>((a.c >> 16) & 0xff);
  const float ag = static_cast<float>((a.c >> 8) & 0xff);
  const float ab = static_cast<float>(a.c & 0xff);
  const float ba = static_cast<float>((b.c >> 24) & 0xff);
  const float br = static_cast<float>((b.c >> 16) & 0xff);
  const float bg = static_cast<float>((b.c >> 8) & 0xff);
  const float bb = static_cast<float>(b.c & 0xff);
  const int ca = std::clamp(static_cast<int>(lerp(aa, ba)), 0, 255);
  const int cr = std::clamp(static_cast<int>(lerp(ar, br)), 0, 255);
  const int cg = std::clamp(static_cast<int>(lerp(ag, bg)), 0, 255);
  const int cb = std::clamp(static_cast<int>(lerp(ab, bb)), 0, 255);
  const auto n = normalize_vec3(
      {lerp(a.nx, b.nx), lerp(a.ny, b.ny), lerp(a.nz, b.nz)});
  return V3{lerp(a.x, b.x), lerp(a.y, b.y), lerp(a.z, b.z),
            n[0], n[1], n[2], D3DCOLOR_ARGB(ca, cr, cg, cb),
            lerp(a.u, b.u), lerp(a.v, b.v)};
}

void append_triangle_z_clipped(std::vector<V3>& tris,
                               const std::array<V3, 3>& tri,
                               bool clip_to_z_min,
                               float z_min) {
  if (!clip_to_z_min) {
    tris.push_back(tri[0]);
    tris.push_back(tri[1]);
    tris.push_back(tri[2]);
    return;
  }
  std::vector<V3> poly = {tri[0], tri[1], tri[2]};
  std::vector<V3> clipped;
  clipped.reserve(4);
  for (size_t i = 0; i < poly.size(); ++i) {
    const V3& cur = poly[i];
    const V3& next = poly[(i + 1) % poly.size()];
    const bool cur_in = cur.z >= z_min;
    const bool next_in = next.z >= z_min;
    const float dz = next.z - cur.z;
    const float t = std::fabs(dz) > 0.00001f ? (z_min - cur.z) / dz : 0.0f;
    if (cur_in && next_in) {
      clipped.push_back(next);
    } else if (cur_in && !next_in) {
      clipped.push_back(lerp_vertex(cur, next, t));
    } else if (!cur_in && next_in) {
      clipped.push_back(lerp_vertex(cur, next, t));
      clipped.push_back(next);
    }
  }
  if (clipped.size() < 3) return;
  for (size_t i = 1; i + 1 < clipped.size(); ++i) {
    tris.push_back(clipped[0]);
    tris.push_back(clipped[i]);
    tris.push_back(clipped[i + 1]);
  }
}

// Fade factor 0..1 for a note/board point at depth y (1 near the strike, fading
// out over the alpha_dist band just before the horizon).
inline float depth_fade_for(float y, float top_y, float alpha_dist) {
  const float dist = std::max(0.001f, alpha_dist);
  const float start = top_y - dist;  // begin fading here
  if (y <= start) return 1.0f;
  if (y >= top_y) return 0.0f;
  return 1.0f - (y - start) / dist;
}

template <size_t N>
float sample_width_profile_px_720(
    const std::array<TailWidthSample, N>& profile, float rel_y) {
  const float rel = std::clamp(rel_y, 0.0f, 1.0f);
  if (rel <= profile.front().rel_y) return profile.front().width_px_720;
  for (size_t i = 1; i < profile.size(); ++i) {
    if (rel <= profile[i].rel_y) {
      const float span =
          std::max(0.00001f, profile[i].rel_y - profile[i - 1].rel_y);
      const float t = (rel - profile[i - 1].rel_y) / span;
      return profile[i - 1].width_px_720 +
             (profile[i].width_px_720 - profile[i - 1].width_px_720) * t;
    }
  }
  return profile.back().width_px_720;
}

float sample_tail_width_px_720(float rel_y) {
  return sample_width_profile_px_720(kPcsx2WhammyBodyWidthProfile4x3, rel_y);
}

float sample_normal_body_width_px_720(float rel_y) {
  return sample_width_profile_px_720(kPcsx2NormalBodyWidthProfile4x3, rel_y);
}

float sample_normal_silhouette_width_px_720(float rel_y) {
  return sample_width_profile_px_720(kPcsx2NormalSilhouetteWidthProfile4x3,
                                     rel_y);
}

float sample_normal_tight_join_width_px_720(float rel_y) {
  return sample_width_profile_px_720(kPcsx2NormalTightJoinWidthProfile4x3,
                                     rel_y);
}

float sample_normal_tight_join_geometry_width_px_720(float rel_y) {
  return sample_normal_tight_join_width_px_720(rel_y) *
         sample_width_profile_px_720(
             kNormalTightJoinGeometryCompensationProfile4x3, rel_y);
}

float sample_normal_body_fill_width_px_720(float rel_y) {
  return sample_normal_body_width_px_720(rel_y) *
         kNormalHeldBodyFillWidthScale4x3;
}

float sample_normal_body_detail_width_px_720(float rel_y) {
  return sample_normal_silhouette_width_px_720(rel_y) *
         kNormalHeldBodyDetailWidthScale4x3;
}

float compensated_tail_width_px_720(float visible_target_px_720) {
  const float response_b = std::max(0.0001f, kNativeWhammyLineResponseB4x3);
  const float authored =
      (visible_target_px_720 - kNativeWhammyLineResponseA4x3) / response_b;
  return std::max(visible_target_px_720, authored);
}

float sample_compensated_tail_width_px_720(float rel_y) {
  return compensated_tail_width_px_720(sample_tail_width_px_720(rel_y));
}

float sample_held_body_geometry_width_px_720(float rel_y) {
  const float visible_fraction =
      std::clamp(kNativeHeldLineVisibleFraction4x3, 0.05f, 1.0f);
  return sample_tail_width_px_720(rel_y) / visible_fraction;
}

float sample_mesh_body_geometry_width_px_720(TailWidthProfileKind profile,
                                             float rel_y) {
  if (profile == TailWidthProfileKind::kNormalBodyFill) {
    return sample_normal_body_fill_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kNormalBodyDetail) {
    return sample_normal_body_detail_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kNormalTightJoin) {
    return sample_normal_tight_join_geometry_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kWhammyBody) {
    return sample_held_body_geometry_width_px_720(rel_y);
  }
  return 0.0f;
}

float sample_line_body_geometry_width_px_720(TailWidthProfileKind profile,
                                             float rel_y) {
  if (profile == TailWidthProfileKind::kWhammyBody) {
    return sample_compensated_tail_width_px_720(rel_y);
  }
  return sample_mesh_body_geometry_width_px_720(profile, rel_y);
}

size_t tail_width_profile_sample_count(TailWidthProfileKind profile) {
  if (profile == TailWidthProfileKind::kNormalBodyFill) {
    return kPcsx2NormalBodyWidthProfile4x3.size();
  }
  if (profile == TailWidthProfileKind::kNormalBodyDetail) {
    return kPcsx2NormalSilhouetteWidthProfile4x3.size();
  }
  if (profile == TailWidthProfileKind::kNormalTightJoin) {
    return kPcsx2NormalTightJoinWidthProfile4x3.size();
  }
  if (profile == TailWidthProfileKind::kWhammyBody) {
    return kPcsx2WhammyBodyWidthProfile4x3.size();
  }
  return 0;
}

float sample_visible_tail_width_px_720(TailWidthProfileKind profile,
                                       float rel_y) {
  if (profile == TailWidthProfileKind::kNormalBodyFill) {
    return sample_normal_body_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kNormalBodyDetail) {
    return sample_normal_silhouette_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kNormalTightJoin) {
    return sample_normal_tight_join_width_px_720(rel_y);
  }
  if (profile == TailWidthProfileKind::kWhammyBody) {
    return sample_tail_width_px_720(rel_y);
  }
  return 0.0f;
}

float normal_held_tight_core_width(float authored_half_width) {
  return std::max(0.001f,
                  authored_half_width * kNormalHeldTightCoreWidthScale4x3);
}

}  // namespace

using ghogx::render::Mat4;

HighwayRenderer::HighwayRenderer(ghogx::render::Window& win) : win_(&win) {
  dev_ = static_cast<IDirect3DDevice9*>(win.device_ptr());
}

HighwayRenderer::~HighwayRenderer() {
  release_textures();
}

void HighwayRenderer::release_textures() {
  for (auto& kv : textures_) {
    if (kv.second) kv.second->Release();
  }
  textures_.clear();
  loaded_surface_ref_.clear();
  selected_surface_loaded_ = false;
  loaded_ = false;
}

IDirect3DTexture9* HighwayRenderer::tex(const std::string& name) const {
  auto it = textures_.find(name);
  return it == textures_.end() ? nullptr : it->second;
}

void HighwayRenderer::configure_source_lighting() const {
  if (!dev_) return;
  constexpr DWORD kLightSlots = 8;
  constexpr DWORD kApproxLightSlots = 4;
  constexpr DWORD kRealLightFirstSlot = kApproxLightSlots;
  dev_->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
  dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
  dev_->SetRenderState(D3DRS_COLORVERTEX, TRUE);
  dev_->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
  dev_->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
  dev_->SetRenderState(
      D3DRS_AMBIENT,
      track_environment_color_ok_
          ? highway_color_from_rgba(track_environment_color_)
          : D3DCOLOR_XRGB(0, 0, 0));
  D3DMATERIAL9 material{};
  material.Diffuse.r = material.Diffuse.g = material.Diffuse.b =
      material.Diffuse.a = 1.0f;
  material.Ambient.r = material.Ambient.g = material.Ambient.b =
      material.Ambient.a = 1.0f;
  dev_->SetMaterial(&material);
  for (DWORD i = 0; i < kLightSlots; ++i) {
    dev_->LightEnable(i, FALSE);
  }
  DWORD approx_slot = 0;
  DWORD real_slot = kRealLightFirstSlot;
  for (const RuntimeLight& source : track_lights_) {
    D3DLIGHT9 light{};
    light.Diffuse.r =
        std::clamp(source.color[0], 0.0f, kHighwayMaxAuthoredLightColor);
    light.Diffuse.g =
        std::clamp(source.color[1], 0.0f, kHighwayMaxAuthoredLightColor);
    light.Diffuse.b =
        std::clamp(source.color[2], 0.0f, kHighwayMaxAuthoredLightColor);
    light.Diffuse.a = std::clamp(source.color[3], 0.0f, 1.0f);
    if (source_light_is_approx(source.type)) {
      if (approx_slot >= kApproxLightSlots) continue;
      const auto dir = normalize_vec3(source.direction);
      light.Type = D3DLIGHT_DIRECTIONAL;
      light.Direction = {dir[0], dir[1], dir[2]};
      dev_->SetLight(approx_slot, &light);
      dev_->LightEnable(approx_slot, TRUE);
      ++approx_slot;
      continue;
    }
    if (!source_light_is_real(source.type) || real_slot >= kLightSlots) {
      continue;
    }
    if (source.type == 2) {
      const auto dir = normalize_vec3(source.direction);
      light.Type = D3DLIGHT_SPOT;
      light.Direction = {dir[0], dir[1], dir[2]};
      constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
      light.Theta = 20.0f * kDegToRad;
      light.Phi = 45.0f * kDegToRad;
    } else {
      light.Type = D3DLIGHT_POINT;
    }
    light.Position = {source.position[0], source.position[1],
                      source.position[2]};
    light.Range = std::max(source.range, 1.0f);
    light.Attenuation0 = 1.0f;
    dev_->SetLight(real_slot, &light);
    dev_->LightEnable(real_slot, TRUE);
    ++real_slot;
  }
}

void HighwayRenderer::draw_runtime_mesh(const RuntimeMesh& mesh,
                                        float cx,
                                        float cy,
                                        uint32_t tint,
                                        float scale,
                                        bool use_texture_alpha,
                                        float z_offset,
                                        bool clip_to_z_min,
                                        float z_min) const {
  draw_runtime_mesh_with_texture(mesh, mesh.texture_name, cx, cy, tint, scale,
                                 use_texture_alpha, z_offset, clip_to_z_min,
                                 z_min);
}

void HighwayRenderer::draw_runtime_mesh_with_texture(
    const RuntimeMesh& mesh,
    const std::string& texture_name,
    float cx,
    float cy,
    uint32_t tint,
    float scale,
    bool use_texture_alpha,
    float z_offset,
    bool clip_to_z_min,
    float z_min) const {
  draw_runtime_mesh_scaled_with_texture(mesh, texture_name, cx, cy, tint, scale,
                                        scale, scale, use_texture_alpha,
                                        0.0f, 0.0f, true, z_offset,
                                        clip_to_z_min, z_min);
}

void HighwayRenderer::draw_runtime_mesh_scaled_with_texture(
    const RuntimeMesh& mesh,
    const std::string& texture_name,
    float cx,
    float cy,
    uint32_t tint,
    float scale_x,
    float scale_y,
    float scale_z,
    bool use_texture_alpha,
    float uv_u_offset,
    float uv_v_offset,
    bool use_vertex_color,
    float z_offset,
    bool clip_to_z_min,
    float z_min,
    bool apply_depth_fade,
    float depth_fade_top_y,
    float depth_fade_alpha_dist) const {
  if (!dev_ || !mesh.ok || mesh.indices.empty() || mesh.verts.empty()) return;
  std::vector<V3> tris;
  tris.reserve(mesh.indices.size());
  const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
  const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
  const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
  const float tb = static_cast<float>(tint & 0xff) / 255.0f;
  auto transformed_vertex = [&](uint16_t idx, V3& out) {
    if (idx >= mesh.verts.size()) return false;
    const auto& v = mesh.verts[idx];
    const float va = use_vertex_color ? v.a : 1.0f;
    const float vr = use_vertex_color ? v.r : 1.0f;
    const float vg = use_vertex_color ? v.g : 1.0f;
    const float vb = use_vertex_color ? v.b : 1.0f;
    const float world_y = cy + v.y * scale_y;
    const float depth_fade =
        apply_depth_fade
            ? depth_fade_for(world_y, depth_fade_top_y,
                             depth_fade_alpha_dist)
            : 1.0f;
    const int a = std::clamp(
        static_cast<int>(va * ta * depth_fade * 255.0f), 0, 255);
    const int r = std::clamp(static_cast<int>(vr * tr * 255.0f), 0, 255);
    const int g = std::clamp(static_cast<int>(vg * tg * 255.0f), 0, 255);
    const int b = std::clamp(static_cast<int>(vb * tb * 255.0f), 0, 255);
    const auto n = normalize_vec3({v.nx, v.ny, v.nz});
    out = V3{cx + v.x * scale_x, world_y,
             z_offset + v.z * scale_z,
             n[0], n[1], n[2], D3DCOLOR_ARGB(a, r, g, b),
             v.u + uv_u_offset, v.v + uv_v_offset};
    return true;
  };
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    std::array<V3, 3> tri;
    if (!transformed_vertex(mesh.indices[i], tri[0]) ||
        !transformed_vertex(mesh.indices[i + 1], tri[1]) ||
        !transformed_vertex(mesh.indices[i + 2], tri[2])) {
      continue;
    }
    append_triangle_z_clipped(tris, tri, clip_to_z_min, z_min);
  }
  if (tris.empty()) return;
  IDirect3DTexture9* texture = tex(texture_name);
  bind_texture_diffuse_stage(dev_, texture, use_texture_alpha,
                             mesh.intensify, mesh.texture_wrap);
  DWORD prev_lighting = FALSE;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->SetRenderState(D3DRS_LIGHTING,
                       highway_mesh_uses_source_lighting(mesh.use_environ,
                                                         mesh.prelit,
                                                         mesh.point_lights)
                           ? TRUE
                           : FALSE);
  dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(tris.size() / 3),
                        tris.data(), sizeof(V3));
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
}

void HighwayRenderer::draw_centered_runtime_mesh_scaled(
    const RuntimeMesh& mesh,
    float cx,
    float cy,
    uint32_t tint,
    float scale_x,
    float scale_y,
    float scale_z,
    bool use_texture_alpha,
    float z_offset,
    bool clip_to_z_min,
    float z_min) const {
  draw_runtime_mesh_scaled_with_texture(
      mesh, mesh.texture_name, cx - mesh.center_x * scale_x,
      cy - mesh.center_y * scale_y, tint, scale_x, scale_y, scale_z,
      use_texture_alpha, 0.0f, 0.0f, true, z_offset, clip_to_z_min, z_min);
}

void HighwayRenderer::draw_centered_runtime_mesh(const RuntimeMesh& mesh,
                                                 float cx,
                                                 float cy,
                                                 uint32_t tint,
                                                 float scale,
                                                 bool use_texture_alpha,
                                                 float z_offset,
                                                 bool clip_to_z_min,
                                                 float z_min) const {
  draw_runtime_mesh(mesh, cx - mesh.center_x * scale,
                    cy - mesh.center_y * scale, tint, scale,
                    use_texture_alpha, z_offset, clip_to_z_min, z_min);
}

void HighwayRenderer::draw_authored_runtime_mesh(const RuntimeMesh& mesh,
                                                 float origin_x,
                                                 float origin_y,
                                                 uint32_t tint,
                                                 float scale,
                                                 bool use_texture_alpha,
                                                 float z_offset,
                                                 bool clip_to_z_min,
                                                 float z_min,
                                                 bool use_vertex_color) const {
  draw_runtime_mesh_scaled_with_texture(
      mesh, mesh.texture_name, origin_x, origin_y, tint, scale, scale, scale,
      use_texture_alpha, 0.0f, 0.0f, use_vertex_color, z_offset,
      clip_to_z_min, z_min);
}

void HighwayRenderer::draw_authored_runtime_mesh_scaled(
    const RuntimeMesh& mesh,
    float origin_x,
    float origin_y,
    uint32_t tint,
    float scale_x,
    float scale_y,
    float scale_z,
    bool use_texture_alpha,
    float z_offset,
    bool clip_to_z_min,
    float z_min,
    bool use_vertex_color) const {
  draw_runtime_mesh_scaled_with_texture(
      mesh, mesh.texture_name, origin_x, origin_y, tint, scale_x, scale_y,
      scale_z, use_texture_alpha, 0.0f, 0.0f, use_vertex_color, z_offset,
      clip_to_z_min, z_min);
}

void HighwayRenderer::draw_centered_runtime_mesh_rotated(
    const RuntimeMesh& mesh,
    float cx,
    float cy,
    uint32_t tint,
    const std::array<float, 4>& quat_xyzw,
    float scale,
    bool use_texture_alpha,
    float z_offset) const {
  if (!dev_ || !mesh.ok || mesh.indices.empty() || mesh.verts.empty()) return;

  const std::array<float, 4> q = normalize_quat(quat_xyzw);
  const float center_z = (mesh.min_z + mesh.max_z) * 0.5f;
  const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
  const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
  const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
  const float tb = static_cast<float>(tint & 0xff) / 255.0f;

  std::vector<V3> tris;
  tris.reserve(mesh.indices.size());
  auto transformed_vertex = [&](uint16_t idx, V3& out) {
    if (idx >= mesh.verts.size()) return false;
    const auto& v = mesh.verts[idx];
    const int a = std::clamp(static_cast<int>(v.a * ta * 255.0f), 0, 255);
    const int r = std::clamp(static_cast<int>(v.r * tr * 255.0f), 0, 255);
    const int g = std::clamp(static_cast<int>(v.g * tg * 255.0f), 0, 255);
    const int b = std::clamp(static_cast<int>(v.b * tb * 255.0f), 0, 255);
    const std::array<float, 3> local = {
        (v.x - mesh.center_x) * scale,
        (v.y - mesh.center_y) * scale,
        (v.z - center_z) * scale,
    };
    const auto rotated = rotate_vec_by_quat(local, q);
    const auto n = normalize_vec3(
        rotate_vec_by_quat({v.nx, v.ny, v.nz}, q));
    out = V3{cx + rotated[0], cy + rotated[1],
             z_offset + center_z * scale + rotated[2],
             n[0], n[1], n[2], D3DCOLOR_ARGB(a, r, g, b), v.u, v.v};
    return true;
  };

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    V3 tri[3];
    if (!transformed_vertex(mesh.indices[i], tri[0]) ||
        !transformed_vertex(mesh.indices[i + 1], tri[1]) ||
        !transformed_vertex(mesh.indices[i + 2], tri[2])) {
      continue;
    }
    tris.push_back(tri[0]);
    tris.push_back(tri[1]);
    tris.push_back(tri[2]);
  }
  if (tris.empty()) return;

  IDirect3DTexture9* texture = tex(mesh.texture_name);
  bind_texture_diffuse_stage(dev_, texture, use_texture_alpha,
                             mesh.intensify, mesh.texture_wrap);
  DWORD prev_lighting = FALSE;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->SetRenderState(D3DRS_LIGHTING,
                       highway_mesh_uses_source_lighting(mesh.use_environ,
                                                         mesh.prelit,
                                                         mesh.point_lights)
                           ? TRUE
                           : FALSE);
  dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(tris.size() / 3),
                        tris.data(), sizeof(V3));
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
}

void HighwayRenderer::draw_authored_runtime_mesh_rotated(
    const RuntimeMesh& mesh,
    float origin_x,
    float origin_y,
    uint32_t tint,
    const std::array<float, 4>& quat_xyzw,
    float scale,
    bool use_texture_alpha,
    float z_offset) const {
  if (!dev_ || !mesh.ok || mesh.indices.empty() || mesh.verts.empty()) return;

  const std::array<float, 4> q = normalize_quat(quat_xyzw);
  const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
  const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
  const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
  const float tb = static_cast<float>(tint & 0xff) / 255.0f;

  std::vector<V3> tris;
  tris.reserve(mesh.indices.size());
  auto transformed_vertex = [&](uint16_t idx, V3& out) {
    if (idx >= mesh.verts.size()) return false;
    const auto& v = mesh.verts[idx];
    const int a = std::clamp(static_cast<int>(v.a * ta * 255.0f), 0, 255);
    const int r = std::clamp(static_cast<int>(v.r * tr * 255.0f), 0, 255);
    const int g = std::clamp(static_cast<int>(v.g * tg * 255.0f), 0, 255);
    const int b = std::clamp(static_cast<int>(v.b * tb * 255.0f), 0, 255);
    const std::array<float, 3> local = {
        v.x * scale,
        v.y * scale,
        v.z * scale,
    };
    const auto rotated = rotate_vec_by_quat(local, q);
    const auto n = normalize_vec3(
        rotate_vec_by_quat({v.nx, v.ny, v.nz}, q));
    out = V3{origin_x + rotated[0], origin_y + rotated[1],
             z_offset + rotated[2], n[0], n[1], n[2],
             D3DCOLOR_ARGB(a, r, g, b), v.u, v.v};
    return true;
  };

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    V3 tri[3];
    if (!transformed_vertex(mesh.indices[i], tri[0]) ||
        !transformed_vertex(mesh.indices[i + 1], tri[1]) ||
        !transformed_vertex(mesh.indices[i + 2], tri[2])) {
      continue;
    }
    tris.push_back(tri[0]);
    tris.push_back(tri[1]);
    tris.push_back(tri[2]);
  }
  if (tris.empty()) return;

  IDirect3DTexture9* texture = tex(mesh.texture_name);
  bind_texture_diffuse_stage(dev_, texture, use_texture_alpha,
                             mesh.intensify, mesh.texture_wrap);
  DWORD prev_lighting = FALSE;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->SetRenderState(D3DRS_LIGHTING,
                       highway_mesh_uses_source_lighting(mesh.use_environ,
                                                         mesh.prelit,
                                                         mesh.point_lights)
                           ? TRUE
                           : FALSE);
  dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(tris.size() / 3),
                        tris.data(), sizeof(V3));
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
}

void HighwayRenderer::draw_authored_runtime_mesh_transformed(
    const RuntimeMesh& mesh,
    float origin_x,
    float origin_y,
    uint32_t tint,
    const MeshTransformSample& transform,
    bool use_texture_alpha,
    float z_offset,
    bool use_vertex_color) const {
  if (!dev_ || !mesh.ok || mesh.indices.empty() || mesh.verts.empty()) return;

  const std::array<float, 4> q =
      transform.has_rotation ? normalize_quat(transform.rotation_xyzw)
                             : std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 3> scale =
      transform.has_scale ? transform.scale
                          : std::array<float, 3>{1.0f, 1.0f, 1.0f};
  const std::array<float, 3> translation =
      transform.has_translation ? transform.translation
                                : std::array<float, 3>{0.0f, 0.0f, 0.0f};
  const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
  const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
  const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
  const float tb = static_cast<float>(tint & 0xff) / 255.0f;

  std::vector<V3> tris;
  tris.reserve(mesh.indices.size());
  auto transformed_vertex = [&](uint16_t idx, V3& out) {
    if (idx >= mesh.verts.size()) return false;
    const auto& v = mesh.verts[idx];
    const float va = use_vertex_color ? v.a : 1.0f;
    const float vr = use_vertex_color ? v.r : 1.0f;
    const float vg = use_vertex_color ? v.g : 1.0f;
    const float vb = use_vertex_color ? v.b : 1.0f;
    const int a = std::clamp(static_cast<int>(va * ta * 255.0f), 0, 255);
    const int r = std::clamp(static_cast<int>(vr * tr * 255.0f), 0, 255);
    const int g = std::clamp(static_cast<int>(vg * tg * 255.0f), 0, 255);
    const int b = std::clamp(static_cast<int>(vb * tb * 255.0f), 0, 255);
    const std::array<float, 3> local = {
        v.x * scale[0],
        v.y * scale[1],
        v.z * scale[2],
    };
    const auto rotated = rotate_vec_by_quat(local, q);
    const auto n = normalize_vec3(
        rotate_vec_by_quat({v.nx, v.ny, v.nz}, q));
    out = V3{origin_x + translation[0] + rotated[0],
             origin_y + translation[1] + rotated[1],
             z_offset + translation[2] + rotated[2],
             n[0], n[1], n[2], D3DCOLOR_ARGB(a, r, g, b), v.u, v.v};
    return true;
  };

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    V3 tri[3];
    if (!transformed_vertex(mesh.indices[i], tri[0]) ||
        !transformed_vertex(mesh.indices[i + 1], tri[1]) ||
        !transformed_vertex(mesh.indices[i + 2], tri[2])) {
      continue;
    }
    tris.push_back(tri[0]);
    tris.push_back(tri[1]);
    tris.push_back(tri[2]);
  }
  if (tris.empty()) return;

  IDirect3DTexture9* texture = tex(mesh.texture_name);
  bind_texture_diffuse_stage(dev_, texture, use_texture_alpha,
                             mesh.intensify, mesh.texture_wrap);
  DWORD prev_lighting = FALSE;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->SetRenderState(D3DRS_LIGHTING,
                       highway_mesh_uses_source_lighting(mesh.use_environ,
                                                         mesh.prelit,
                                                         mesh.point_lights)
                           ? TRUE
                           : FALSE);
  dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(tris.size() / 3),
                        tris.data(), sizeof(V3));
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
}

void HighwayRenderer::draw_centered_runtime_mesh_transformed(
    const RuntimeMesh& mesh,
    float cx,
    float cy,
    uint32_t tint,
    const MeshTransformSample& transform,
    bool use_texture_alpha,
    float z_offset,
    bool use_vertex_color) const {
  if (!dev_ || !mesh.ok || mesh.indices.empty() || mesh.verts.empty()) return;

  const std::array<float, 4> q =
      transform.has_rotation ? normalize_quat(transform.rotation_xyzw)
                             : std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 3> scale =
      transform.has_scale ? transform.scale
                          : std::array<float, 3>{1.0f, 1.0f, 1.0f};
  const std::array<float, 3> translation =
      transform.has_translation ? transform.translation
                                : std::array<float, 3>{0.0f, 0.0f, 0.0f};
  const float center_z = (mesh.min_z + mesh.max_z) * 0.5f;
  const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
  const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
  const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
  const float tb = static_cast<float>(tint & 0xff) / 255.0f;

  std::vector<V3> tris;
  tris.reserve(mesh.indices.size());
  auto transformed_vertex = [&](uint16_t idx, V3& out) {
    if (idx >= mesh.verts.size()) return false;
    const auto& v = mesh.verts[idx];
    const float va = use_vertex_color ? v.a : 1.0f;
    const float vr = use_vertex_color ? v.r : 1.0f;
    const float vg = use_vertex_color ? v.g : 1.0f;
    const float vb = use_vertex_color ? v.b : 1.0f;
    const int a = std::clamp(static_cast<int>(va * ta * 255.0f), 0, 255);
    const int r = std::clamp(static_cast<int>(vr * tr * 255.0f), 0, 255);
    const int g = std::clamp(static_cast<int>(vg * tg * 255.0f), 0, 255);
    const int b = std::clamp(static_cast<int>(vb * tb * 255.0f), 0, 255);
    const std::array<float, 3> local = {
        (v.x - mesh.center_x) * scale[0],
        (v.y - mesh.center_y) * scale[1],
        (v.z - center_z) * scale[2],
    };
    const auto rotated = rotate_vec_by_quat(local, q);
    const auto n = normalize_vec3(
        rotate_vec_by_quat({v.nx, v.ny, v.nz}, q));
    out = V3{cx + translation[0] + rotated[0],
             cy + translation[1] + rotated[1],
             z_offset + center_z + translation[2] + rotated[2],
             n[0], n[1], n[2], D3DCOLOR_ARGB(a, r, g, b), v.u, v.v};
    return true;
  };

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    V3 tri[3];
    if (!transformed_vertex(mesh.indices[i], tri[0]) ||
        !transformed_vertex(mesh.indices[i + 1], tri[1]) ||
        !transformed_vertex(mesh.indices[i + 2], tri[2])) {
      continue;
    }
    tris.push_back(tri[0]);
    tris.push_back(tri[1]);
    tris.push_back(tri[2]);
  }
  if (tris.empty()) return;

  IDirect3DTexture9* texture = tex(mesh.texture_name);
  bind_texture_diffuse_stage(dev_, texture, use_texture_alpha,
                             mesh.intensify, mesh.texture_wrap);
  DWORD prev_lighting = FALSE;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->SetRenderState(D3DRS_LIGHTING,
                       highway_mesh_uses_source_lighting(mesh.use_environ,
                                                         mesh.prelit,
                                                         mesh.point_lights)
                           ? TRUE
                           : FALSE);
  dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(tris.size() / 3),
                        tris.data(), sizeof(V3));
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
}

void HighwayRenderer::draw_centered_runtime_mesh_with_texture(
    const RuntimeMesh& mesh,
    const std::string& texture_name,
    float cx,
    float cy,
    uint32_t tint,
    float scale,
    bool use_texture_alpha,
    float z_offset,
    bool clip_to_z_min,
    float z_min) const {
  draw_runtime_mesh_with_texture(mesh, texture_name, cx - mesh.center_x * scale,
                                 cy - mesh.center_y * scale, tint, scale,
                                 use_texture_alpha, z_offset, clip_to_z_min,
                                 z_min);
}

void HighwayRenderer::draw_runtime_particles(
    const std::vector<RuntimeParticleSystem>& particles,
    float origin_x,
    float origin_y,
    double song_time,
    float intensity,
    bool one_shot,
    float x_scale,
    bool apply_depth_fade,
    float depth_fade_top_y,
    float depth_fade_alpha_dist,
    float state_frame) const {
  intensity = std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f,
                         1.0f);
  if (!dev_ || particles.empty() || intensity <= 0.001f) return;

  DWORD prev_lighting = FALSE;
  DWORD prev_z_write = FALSE;
  DWORD prev_src_blend = D3DBLEND_SRCALPHA;
  DWORD prev_dest_blend = D3DBLEND_INVSRCALPHA;
  DWORD prev_blend_op = D3DBLENDOP_ADD;
  DWORD prev_point_sprite = FALSE;
  DWORD prev_point_scale = FALSE;
  DWORD prev_point_size = 0;
  DWORD prev_fvf = kFVF;
  IDirect3DBaseTexture9* prev_texture = nullptr;
  dev_->GetRenderState(D3DRS_LIGHTING, &prev_lighting);
  dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_z_write);
  dev_->GetRenderState(D3DRS_SRCBLEND, &prev_src_blend);
  dev_->GetRenderState(D3DRS_DESTBLEND, &prev_dest_blend);
  dev_->GetRenderState(D3DRS_BLENDOP, &prev_blend_op);
  dev_->GetRenderState(D3DRS_POINTSPRITEENABLE, &prev_point_sprite);
  dev_->GetRenderState(D3DRS_POINTSCALEENABLE, &prev_point_scale);
  dev_->GetRenderState(D3DRS_POINTSIZE, &prev_point_size);
  dev_->GetFVF(&prev_fvf);
  dev_->GetTexture(0, &prev_texture);

  dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev_->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
  dev_->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
  dev_->SetFVF(kParticleFvf);

  for (const RuntimeParticleSystem& source_particle : particles) {
    RuntimeParticleSystem particle = source_particle;
    if (state_frame >= 0.0f) {
      auto apply_color_keys = [&](const auto& keys,
                                  std::array<float, 4>& low,
                                  std::array<float, 4>& high) {
        if (keys.empty()) return;
        const std::array<float, 4> original_low = low;
        const std::array<float, 4> sampled =
            sample_particle_anim_color(keys, state_frame, low);
        for (size_t channel = 0; channel < sampled.size(); ++channel) {
          high[channel] = sampled[channel] + high[channel] - original_low[channel];
        }
        low = sampled;
      };
      // Mirrors RndParticleSysAnim::SetFrame: color keys replace the low
      // endpoint while retaining the authored low/high delta; Vector2 keys
      // replace the corresponding particle-system range.
      apply_color_keys(particle.anim_start_color_keys,
                       particle.start_color_low, particle.start_color_high);
      apply_color_keys(particle.anim_end_color_keys,
                       particle.end_color_low, particle.end_color_high);
      if (!particle.anim_speed_keys.empty()) {
        const auto speed = sample_particle_anim_vector2(
            particle.anim_speed_keys, state_frame,
            {particle.speed_min, particle.speed_max});
        particle.speed_min = speed[0];
        particle.speed_max = speed[1];
      }
      if (!particle.anim_life_keys.empty()) {
        const auto life_frames = sample_particle_anim_vector2(
            particle.anim_life_keys, state_frame,
            {particle.lifetime_min * 30.0f,
             particle.lifetime_max * 30.0f});
        particle.lifetime_min = std::max(0.05f, life_frames[0] / 30.0f);
        particle.lifetime_max =
            std::max(particle.lifetime_min, life_frames[1] / 30.0f);
      }
      if (!particle.anim_start_size_keys.empty()) {
        const auto start_size = sample_particle_anim_vector2(
            particle.anim_start_size_keys, state_frame,
            {particle.start_size_min, particle.start_size_max});
        particle.start_size_min = start_size[0];
        particle.start_size_max = start_size[1];
      }
    }
    if (!particle.ok || particle.texture_name.empty()) continue;
    IDirect3DTexture9* particle_tex = tex(particle.texture_name);
    if (!particle_tex) continue;

    const HighwayBlendState blend = highway_blend_state_for(particle.blend);
    dev_->SetRenderState(D3DRS_BLENDOP, blend.op);
    dev_->SetRenderState(D3DRS_SRCBLEND, blend.src);
    dev_->SetRenderState(D3DRS_DESTBLEND, blend.dest);

    const int count = static_cast<int>(std::clamp(
        static_cast<float>(particle.max_particles > 0
                               ? particle.max_particles
                               : 16u) *
            intensity,
        1.0f, 256.0f));
    const float lifetime = std::clamp(
        (particle.lifetime_min + particle.lifetime_max) * 0.5f, 0.05f,
        20.0f);
    const float base_speed =
        std::clamp((particle.speed_min + particle.speed_max) * 0.5f, 0.0f,
                   10000.0f);
    const float start_size =
        std::max(0.0f,
                 (particle.start_size_min + particle.start_size_max) * 0.5f);
    const float delta_size =
        (particle.delta_size_min + particle.delta_size_max) * 0.5f;
    const float preview_size =
        std::max(0.0f, start_size + delta_size * 0.5f) *
        sample_particle_grow_shrink(particle.grow_ratio,
                                    particle.shrink_ratio, 0.5f);
    const float preview_point_size =
        std::clamp(preview_size * 12.0f + base_speed * 0.02f, 2.0f, 72.0f);
    const float jitter =
        std::max(preview_point_size * 0.12f, base_speed * 0.010f);

    const std::array<float, 4> start_color = average_particle_color(
        particle.start_color_low, particle.start_color_high);
    const std::array<float, 4> mid_color =
        average_particle_color(particle.mid_color_low, particle.mid_color_high);
    const std::array<float, 4> end_color =
        average_particle_color(particle.end_color_low, particle.end_color_high);

    std::vector<ParticleVtx> points;
    points.reserve(static_cast<size_t>(count));
    const uint32_t seed_base = static_cast<uint32_t>(
        std::hash<std::string>{}(particle.name) & 0xffffffffu);
    for (int i = 0; i < count; ++i) {
      const uint32_t seed = seed_base + static_cast<uint32_t>(i) * 977u;
      const float h0 = highway_particle_hash01(seed + 1u);
      const float h1 = highway_particle_hash01(seed + 2u);
      const float h2 = highway_particle_hash01(seed + 3u);
      const float h3 = highway_particle_hash01(seed + 4u);
      const float h4 = highway_particle_hash01(seed + 5u);
      const float h5 = highway_particle_hash01(seed + 6u);
      const float h6 = highway_particle_hash01(seed + 7u);
      const float h7 = highway_particle_hash01(seed + 8u);
      const float h8 = highway_particle_hash01(seed + 9u);
      const float phase_driver =
          one_shot ? (1.0f - intensity) * lifetime
                   : static_cast<float>(song_time);
      const float phase = std::fmod(phase_driver / lifetime + h0, 1.0f);
      const float fade = 1.0f - phase;
      float local[3] = {};
      const float hashes[3] = {h1, h2, h3};
      for (int c = 0; c < 3; ++c) {
        const float lo =
            std::min(particle.box_extent_min[c], particle.box_extent_max[c]);
        const float hi =
            std::max(particle.box_extent_min[c], particle.box_extent_max[c]);
        local[c] = lo + (hi - lo) * hashes[c];
        local[c] +=
            (highway_particle_hash01(seed + 20u + static_cast<uint32_t>(c)) -
             0.5f) *
            jitter;
      }

      const float speed =
          particle.speed_min + (particle.speed_max - particle.speed_min) * h4;
      const float pitch =
          particle.pitch_min + (particle.pitch_max - particle.pitch_min) * h5;
      const float yaw =
          particle.yaw_min + (particle.yaw_max - particle.yaw_min) * h6;
      const float cp = std::cos(pitch);
      const float dir[3] = {std::sin(yaw) * cp, std::cos(yaw) * cp,
                            std::sin(pitch)};
      const float travel_frames = phase * lifetime * 30.0f;
      for (int c = 0; c < 3; ++c) {
        local[c] += dir[c] * speed * travel_frames;
        local[c] += 0.5f * particle.force_dir[c] * travel_frames *
                    travel_frames;
      }
      if (particle.bubble) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float bubble_period =
            std::max(0.001f,
                     particle.bubble_period_min +
                         (particle.bubble_period_max -
                          particle.bubble_period_min) *
                             h7);
        const float bubble_size =
            particle.bubble_size_min +
            (particle.bubble_size_max - particle.bubble_size_min) * h8;
        const float bubble_frame =
            static_cast<float>(song_time) * 30.0f + h0 * bubble_period;
        const float bubble_angle = bubble_frame / bubble_period * kTwoPi;
        local[0] += std::cos(bubble_angle) * bubble_size;
        local[2] += std::sin(bubble_angle + h1 * kTwoPi) * bubble_size;
      }

      const float point_start_size =
          particle.start_size_min +
          (particle.start_size_max - particle.start_size_min) * h7;
      const float point_delta_size =
          particle.delta_size_min +
          (particle.delta_size_max - particle.delta_size_min) * h8;
      const float grow_shrink =
          sample_particle_grow_shrink(particle.grow_ratio,
                                      particle.shrink_ratio, phase);
      const std::array<float, 4> sampled_color = sample_particle_color(
          start_color, mid_color, end_color, particle.mid_color_ratio, phase);
      const float world_y = origin_y + particle.local_pos[1] + local[1];
      const float particle_depth_fade =
          apply_depth_fade
              ? depth_fade_for(world_y, depth_fade_top_y,
                               depth_fade_alpha_dist)
              : 1.0f;
      const float alpha =
          particle.mat_color[3] * (0.25f + fade * 0.75f) *
          std::clamp(sampled_color[3], 0.0f, 1.0f) * intensity *
          particle_depth_fade;
      if (alpha <= 0.001f) continue;

      ParticleVtx v{};
      v.x = origin_x + (particle.local_pos[0] + local[0]) * x_scale;
      v.y = world_y;
      v.z = kBoardZ + particle.local_pos[2] + local[2];
      v.size = std::clamp(
          std::max(0.0f, point_start_size + point_delta_size * phase) *
                  grow_shrink * 12.0f +
              speed * 0.02f,
          2.0f, 72.0f);
      v.c = D3DCOLOR_ARGB(
          particle_color_channel(alpha),
          particle_color_channel(particle.mat_color[0] * sampled_color[0]),
          particle_color_channel(particle.mat_color[1] * sampled_color[1]),
          particle_color_channel(particle.mat_color[2] * sampled_color[2]));
      points.push_back(v);
    }

    if (points.empty()) continue;
    dev_->SetRenderState(D3DRS_POINTSIZE,
                         dword_from_float(preview_point_size));
    dev_->SetTexture(0, particle_tex);
    dev_->DrawPrimitiveUP(D3DPT_POINTLIST, static_cast<UINT>(points.size()),
                          points.data(), sizeof(ParticleVtx));
  }

  dev_->SetTexture(0, prev_texture);
  if (prev_texture) prev_texture->Release();
  dev_->SetRenderState(D3DRS_POINTSPRITEENABLE, prev_point_sprite);
  dev_->SetRenderState(D3DRS_POINTSCALEENABLE, prev_point_scale);
  dev_->SetRenderState(D3DRS_POINTSIZE, prev_point_size);
  dev_->SetRenderState(D3DRS_LIGHTING, prev_lighting);
  dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_z_write);
  dev_->SetRenderState(D3DRS_SRCBLEND, prev_src_blend);
  dev_->SetRenderState(D3DRS_DESTBLEND, prev_dest_blend);
  dev_->SetRenderState(D3DRS_BLENDOP, prev_blend_op);
  dev_->SetFVF(prev_fvf);
}

void HighwayRenderer::load_track_graphics_config(const std::string& hdr_path,
                                                 const std::string& ark_path) {
  lane_spacing_ = kDefaultLaneSpacing;
  board_half_x_ = kDefaultBoardHalfX;
  top_y_ = kDefaultTopY;
  remove_y_ = kDefaultRemoveY;
  alpha_dist_ = kDefaultAlphaDist;
  y_per_second_ = kDefaultYPerSecond;
  tail_glow_width_ = kDefaultTailGlowWidth;
  tail_glow_tight_width_ = kDefaultTailGlowTightWidth;
  horizon_tail_clip_ = kDefaultHorizonTailClip;
  nowbar_tail_clip_ = kDefaultNowbarTailClip;
  cam_near_ = kDefaultCamNear;
  cam_far_ = kDefaultCamFar;
  cam_z_start_ = kDefaultCamZStart;
  cam_z_end_ = kDefaultCamZEnd;
  burn_normal_frame_ = kDefaultBurnNormalFrame;
  burn_whammy_frame_ = kDefaultBurnWhammyFrame;
  burn_bonus_frame_ = kDefaultBurnBonusFrame;
  slot_color_names_ = kDefaultSlotColorNames;
  slot_lane_colors_ = kDefaultSlotLaneColors;
  track_speed_ = kDefaultTrackSpeed;
  const char* yps_source = "default";

  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    auto entry = ark.find("config/gen/track_graphics.dtb");
    if (!entry) entry = ark.find("../../system/run/config/gen/track_graphics.dtb");
    if (!entry) {
      std::fprintf(stderr,
                   "[highway] config/gen/track_graphics.dtb not found; using defaults\n");
    } else {
      const auto bytes = ark.read_entry(*entry, {ark_path});
      const auto tree = gh::dtb::parse(bytes);
      slot_color_names_ = keyed_slot_color_names(tree, slot_color_names_);
      const float track_width =
          keyed_float(tree, "track_width", kDefaultTrackWidth);
      if (std::isfinite(track_width) && track_width > 0.001f) {
        lane_spacing_ = track_width / 5.0f;
        board_half_x_ = track_width * 0.5f;
      }
      top_y_ = keyed_float(tree, "horizon_y", top_y_);
      remove_y_ = keyed_float(tree, "remove_y", remove_y_);
      alpha_dist_ = keyed_float(tree, "alpha_dist", alpha_dist_);
      tail_glow_width_ =
          keyed_float(tree, "tail_glow_width", tail_glow_width_);
      tail_glow_tight_width_ =
          keyed_float(tree, "tail_glow_tight_width", tail_glow_tight_width_);
      horizon_tail_clip_ =
          keyed_float(tree, "horizon_tail_clip", horizon_tail_clip_);
      nowbar_tail_clip_ =
          keyed_float(tree, "nowbar_tail_clip", nowbar_tail_clip_);
      if (auto cam_node = gh::dtb::find_keyed(tree, "cam")) {
        cam_near_ = keyed_child_float(*cam_node, "near_plane", cam_near_);
        cam_far_ = keyed_child_float(*cam_node, "far_plane", cam_far_);
        cam_z_start_ = keyed_child_float(*cam_node, "z_start", cam_z_start_);
        cam_z_end_ = keyed_child_float(*cam_node, "z_end", cam_z_end_);
      }
      if (auto particles_node = gh::dtb::find_keyed(tree, "particles")) {
        if (auto burn_node = gh::dtb::find_keyed(*particles_node, "burn")) {
          burn_normal_frame_ =
              keyed_child_float(*burn_node, "normal", burn_normal_frame_);
          burn_whammy_frame_ =
              keyed_child_float(*burn_node, "whammy", burn_whammy_frame_);
          burn_bonus_frame_ =
              keyed_child_float(*burn_node, "bonus", burn_bonus_frame_);
        }
      }
      if (!std::isfinite(top_y_) || top_y_ <= kStrikeY) top_y_ = kDefaultTopY;
      if (!std::isfinite(remove_y_) || remove_y_ >= kStrikeY) {
        remove_y_ = kDefaultRemoveY;
      }
      if (!std::isfinite(alpha_dist_) || alpha_dist_ <= 0.001f) {
        alpha_dist_ = kDefaultAlphaDist;
      }
      if (!std::isfinite(tail_glow_width_) || tail_glow_width_ <= 0.001f) {
        tail_glow_width_ = kDefaultTailGlowWidth;
      }
      if (!std::isfinite(tail_glow_tight_width_) ||
          tail_glow_tight_width_ <= 0.001f) {
        tail_glow_tight_width_ = kDefaultTailGlowTightWidth;
      }
      if (!std::isfinite(horizon_tail_clip_) || horizon_tail_clip_ < 0.0f) {
        horizon_tail_clip_ = kDefaultHorizonTailClip;
      }
      if (!std::isfinite(nowbar_tail_clip_) || nowbar_tail_clip_ < 0.0f) {
        nowbar_tail_clip_ = kDefaultNowbarTailClip;
      }
      if (!std::isfinite(cam_near_) || cam_near_ <= 0.001f) {
        cam_near_ = kDefaultCamNear;
      }
      if (!std::isfinite(cam_far_) || cam_far_ <= cam_near_ + 0.001f) {
        cam_far_ = kDefaultCamFar;
      }
      if (!std::isfinite(cam_z_start_) || cam_z_start_ < 0.0f ||
          cam_z_start_ > 1.0f) {
        cam_z_start_ = kDefaultCamZStart;
      }
      if (!std::isfinite(cam_z_end_) || cam_z_end_ < cam_z_start_ ||
          cam_z_end_ > 1.0f) {
        cam_z_end_ = kDefaultCamZEnd;
      }
      if (!std::isfinite(burn_normal_frame_)) {
        burn_normal_frame_ = kDefaultBurnNormalFrame;
      }
      if (!std::isfinite(burn_whammy_frame_)) {
        burn_whammy_frame_ = kDefaultBurnWhammyFrame;
      }
      if (!std::isfinite(burn_bonus_frame_)) {
        burn_bonus_frame_ = kDefaultBurnBonusFrame;
      }

      if (auto speed_node = gh::dtb::find_keyed(tree, "track_speed")) {
        track_speed_[0] =
            keyed_child_float(*speed_node, "kDifficultyEasy", track_speed_[0]);
        track_speed_[1] =
            keyed_child_float(*speed_node, "kDifficultyMedium", track_speed_[1]);
        track_speed_[2] =
            keyed_child_float(*speed_node, "kDifficultyHard", track_speed_[2]);
        track_speed_[3] =
            keyed_child_float(*speed_node, "kDifficultyExpert", track_speed_[3]);
        for (float& speed : track_speed_) {
          if (!std::isfinite(speed) || speed <= 0.001f) speed = 1.0f;
        }
      }
    }

    if (auto yps = load_track_panel_y_per_second(ark, ark_path)) {
      y_per_second_ = *yps;
      yps_source = "track.milo_ps2";
    }

    // The source DTB provides the authored horizon/fade values, but the
    // corrected PC dev projection needs the PCSX2-measured world values to put
    // the fade at the same screen rows as stock GH2 frame_16.
    top_y_ = kPcsx2MeasuredFadeTopY;
    alpha_dist_ = kPcsx2MeasuredFadeAlphaDist;

    std::fprintf(
        stderr,
        "[highway] track_graphics.dtb: width=%.3f lane=%.3f horizon=%.3f remove=%.3f alpha=%.3f tail=%.3f tight=%.3f horizon_tail_clip=%.3f nowbar_tail_clip=%.3f cam_near=%.3f cam_far=%.3f cam_z=%.3f..%.3f burn_frame=%.3f/%.3f/%.3f slots=%s/%s/%s/%s/%s speeds=%.3f/%.3f/%.3f/%.3f yps=%.3f(%s)\n",
        board_half_x_ * 2.0f, lane_spacing_, top_y_, remove_y_, alpha_dist_,
        tail_glow_width_, tail_glow_tight_width_, horizon_tail_clip_,
        nowbar_tail_clip_, cam_near_, cam_far_, cam_z_start_, cam_z_end_,
        burn_normal_frame_, burn_whammy_frame_, burn_bonus_frame_,
        slot_color_names_[0].c_str(), slot_color_names_[1].c_str(),
        slot_color_names_[2].c_str(), slot_color_names_[3].c_str(),
        slot_color_names_[4].c_str(), track_speed_[0], track_speed_[1],
        track_speed_[2], track_speed_[3], y_per_second_, yps_source);
  } catch (const std::exception& ex) {
    std::fprintf(stderr,
                 "[highway] track_graphics.dtb load failed: %s; using defaults\n",
                 ex.what());
  }
}

bool HighwayRenderer::load_textures(const std::string& hdr_path,
                                    const std::string& ark_path,
                                    const std::string& surface_ref,
                                    bool timing_preview) {
  if (!dev_) return false;
  if (!textures_.empty()) release_textures();
  load_track_graphics_config(hdr_path, ark_path);
  for (auto& mesh : gem_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : gem_specular_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : hopo_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : star_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : star_top_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : tail_mesh_) mesh = RuntimeMesh{};
  for (auto& mesh : held_tail_mesh_) mesh = RuntimeMesh{};
  for (auto& material : held_tail_line_material_) material = RuntimeLineMaterial{};
  held_tight_tail_mesh_ = RuntimeMesh{};
  held_tight_tail_line_material_ = RuntimeLineMaterial{};
  burn_castlight_mesh_ = RuntimeMesh{};
  burn_castlight_color_anim_ = ColorAnimState{};
  burn_tail_particles_.clear();
  smash_normal_particles_.clear();
  smash_bonus_particles_.clear();
  smash_star_particles_.clear();
  smash_combo_particles_.clear();
  star_base_mesh_ = RuntimeMesh{};
  star_overlay_mesh_ = RuntimeMesh{};
  star_black_top_mesh_ = RuntimeMesh{};
  moving_note_standard_has_top_ = true;
  moving_note_standard_has_glow_ = false;
  moving_note_star_has_base_ = true;
  moving_note_star_has_lane_ = true;
  moving_note_star_has_overlay_ = true;
  moving_note_star_has_top_ = true;
  moving_note_star_prefers_black_top_ = true;
  star_note_anim_ = MeshTransformAnim{};
  star_note_anim_duration_frames_ = 0.0f;
  star_note_rotation_keys_.clear();
  star_note_rotation_duration_frames_ = 0.0f;
  star_base_rotation_keys_.clear();
  star_base_rotation_duration_frames_ = 0.0f;
  gem_top_mesh_ = RuntimeMesh{};
  pc_standard_top_mesh_ = RuntimeMesh{};
  gem_glow_mesh_ = RuntimeMesh{};
  star_phrase_tail_mesh_ = RuntimeMesh{};
  star_tail_mesh_ = RuntimeMesh{};
  star_tail_line_material_ = RuntimeLineMaterial{};
  whammy_tail_mesh_ = RuntimeMesh{};
  whammy_tail_line_material_ = RuntimeLineMaterial{};
  bonus_tail_mesh_ = RuntimeMesh{};
  bonus_glow_tail_mesh_ = RuntimeMesh{};
  bonus_tail_line_material_ = RuntimeLineMaterial{};
  sustain_visual_states_.clear();
  whammy_envelope_ = 0.0f;
  last_sustain_visual_time_ = -1.0;
  bonus_gem_mesh_ = RuntimeMesh{};
  bonus_gem_overlay_mesh_ = RuntimeMesh{};
  gem_sparkle_mesh_ = RuntimeMesh{};
  gem_sparkle_anim_ = MeshTransformAnim{};
  gem_sparkle_anim_duration_frames_ = 0.0f;
  bonus_spark1_mesh_ = RuntimeMesh{};
  bonus_spark2_mesh_ = RuntimeMesh{};
  track_surface_mesh_ = RuntimeMesh{};
  track_mask_mesh_ = RuntimeMesh{};
  track_extend_anim_ = MeshTransformAnim{};
  surface_flash_2x_ = ColorAnimState{};
  surface_flash_3x_ = ColorAnimState{};
  surface_flash_4x_ = ColorAnimState{};
  track_side_rails_mesh_ = RuntimeMesh{};
  side_rails_building_ = SideRailColorState{};
  side_rails_none_ = SideRailColorState{};
  side_rails_warning_ = SideRailColorState{};
  side_rails_star_ = SideRailColorState{};
  side_rails_warning_star_ = SideRailColorState{};
  track_lane_lines_mesh_ = RuntimeMesh{};
  star_power_track_glow_mesh_ = RuntimeMesh{};
  bar_line_mesh_ = RuntimeMesh{};
  beat_line_mesh_ = RuntimeMesh{};
  half_beat_line_mesh_ = RuntimeMesh{};
  quarter_beat_line_mesh_ = RuntimeMesh{};
  gem_smasher_mesh_ = RuntimeMesh{};
  for (auto& mesh : smasher_add_meshes_) mesh = RuntimeMesh{};
  smasher_rim_mesh_ = RuntimeMesh{};
  for (auto& mesh : smasher_rim_meshes_) mesh = RuntimeMesh{};
  for (auto& mesh : smasher_ring_add_meshes_) mesh = RuntimeMesh{};
  bonus_smasher_rim_mesh_ = RuntimeMesh{};
  bonus_smasher_ring_add_mesh_ = RuntimeMesh{};
  bonus_smasher_add_mesh_ = RuntimeMesh{};
  smasher_shadow_mesh_ = RuntimeMesh{};
  smasher_press_anim_ = MeshTransformAnim{};
  smasher_press_anim_duration_frames_ = 0.0f;
  hit_flame_mesh_ = RuntimeMesh{};
  star_collect_flame_mesh_ = RuntimeMesh{};
  bonus_hit_flame_mesh_ = RuntimeMesh{};
  hit_flame_anim_ = MeshTransformAnim{};
  star_collect_flame_anim_ = MeshTransformAnim{};
  bonus_hit_flame_anim_ = MeshTransformAnim{};
  hit_flame_anim_duration_frames_ = 0.0f;
  star_collect_flame_anim_duration_frames_ = 0.0f;
  bonus_hit_flame_anim_duration_frames_ = 0.0f;
  hit_flame_color_anim_ = ColorAnimState{};
  star_collect_flame_color_anim_ = ColorAnimState{};
  bonus_hit_flame_color_anim_ = ColorAnimState{};
  miss_mesh_ = RuntimeMesh{};
  miss_top_mesh_ = RuntimeMesh{};
  star_miss_mesh_ = RuntimeMesh{};
  star_miss_top_mesh_ = RuntimeMesh{};
  for (auto& mesh : combo_lightning_mesh_) mesh = RuntimeMesh{};
  for (auto& anim : combo_lightning_anim_) anim = MeshTransformAnim{};
  combo_lightning_anim_duration_frames_.fill(0.0f);
  for (auto& anim : combo_lightning_color_anim_) anim = ColorAnimState{};
  smasher_normal_texture_name_.clear();
  smasher_texture_names_.fill({});
  smasher_add_texture_names_.fill({});
  smasher_add_blends_.fill(static_cast<uint8_t>(kHighwayBlendSrcAlpha));
  smasher_ring_texture_names_.fill({});
  bonus_smasher_texture_name_.clear();
  bonus_smasher_add_texture_name_.clear();
  bonus_smasher_add_blend_ = static_cast<uint8_t>(kHighwayBlendSrcAlpha);
  bonus_smasher_ring_texture_name_.clear();
  track_environment_color_ = {1.0f, 1.0f, 1.0f, 1.0f};
  track_environment_color_ok_ = false;
  track_lights_.clear();
  selected_surface_loaded_ = false;

  std::set<std::string> texture_names = {
      "track_surface.tex", "track_fade.tex", "barline_gw.tex",
      "gem.tex", "gem_glow.tex",
      "now_ring.tex", "now_ring_add.tex",
      "smasher_on.tex", "smasher_off.tex",
      "tail2.tex", "tail_tight.tex", "flame_part.tex",
  };
  std::set<std::string> smasher_ring_add_alpha_key_sources;
  std::set<std::string> smasher_add_alpha_key_sources;
  for (const auto& slot_color : slot_color_names_) {
    texture_names.insert(lane_texture_name("gem_", slot_color, ".tex"));
    texture_names.insert(lane_texture_name("now_", slot_color, "_add.tex"));
  }

  ghogx::milo_scene::Scene track_scene;
  if (ghogx::milo_scene::load_scene(hdr_path, ark_path,
                                    "track/gen/track.milo_ps2",
                                    track_scene)) {
    const auto track_particle_anims =
        load_track_particle_anims(hdr_path, ark_path);
    std::vector<std::string> track_env_light_refs;
    if (const auto* track_env = track_scene.find_environ("track.env")) {
      track_env_light_refs = track_env->lights;
      if (source_environment_color_sane(*track_env)) {
        track_environment_color_ = {
            track_env->color_a[0], track_env->color_a[1],
            track_env->color_a[2], track_env->color_a[3]};
        track_environment_color_ok_ = true;
      }
    }
    std::array<float, 3> approx_fill = {0.0f, 0.0f, 0.0f};
    size_t approx_light_count = 0;
    for (const auto& light : track_scene.lights) {
      if (!light.decoded || !source_light_color_sane(light)) continue;
      if (!track_env_light_refs.empty() &&
          std::find(track_env_light_refs.begin(), track_env_light_refs.end(),
                    light.name) == track_env_light_refs.end()) {
        continue;
      }
      if (source_light_is_approx(light.type)) {
        for (int channel = 0; channel < 3; ++channel) {
          approx_fill[channel] += std::clamp(
              light.color[channel], 0.0f, kHighwayMaxAuthoredLightColor);
        }
        ++approx_light_count;
      }
      RuntimeLight runtime_light;
      runtime_light.position = {light.world_stored.pos[0],
                                light.world_stored.pos[1],
                                light.world_stored.pos[2]};
      runtime_light.direction = source_light_direction(light);
      runtime_light.color = {light.color[0], light.color[1],
                             light.color[2], light.color[3]};
      runtime_light.range = light.range;
      runtime_light.type = light.type;
      track_lights_.push_back(runtime_light);
    }
    if (track_environment_color_ok_ && approx_light_count > 0) {
      const float inv_count = 1.0f / static_cast<float>(approx_light_count);
      for (int channel = 0; channel < 3; ++channel) {
        track_environment_color_[channel] = std::clamp(
            track_environment_color_[channel] +
                approx_fill[channel] * inv_count * kHighwayApproxFillScale,
            0.0f, 1.0f);
      }
    }
    if (env_enabled("GHOGX_DEBUG_HIGHWAY_NOTE_MESHES")) {
      size_t point_lights = 0;
      size_t spot_lights = 0;
      size_t approx_lights = 0;
      for (const auto& light : track_lights_) {
        if (source_light_is_approx(light.type)) {
          ++approx_lights;
        } else if (light.type == 2) {
          ++spot_lights;
        } else if (light.type == 0) {
          ++point_lights;
        }
      }
      std::fprintf(stderr,
                   "[highway] track.env lighting ambient_ok=%d "
                   "ambient=%.3f,%.3f,%.3f,%.3f refs=%zu point=%zu "
                   "spot=%zu approx=%zu\n",
                   track_environment_color_ok_ ? 1 : 0,
                   track_environment_color_[0], track_environment_color_[1],
                   track_environment_color_[2], track_environment_color_[3],
                   track_lights_.size(), point_lights, spot_lights,
                   approx_lights);
    }
    auto find_mesh = [&](const std::string& mesh_name)
        -> const ghogx::milo_scene::MeshObj* {
      for (const auto& mesh : track_scene.meshes) {
        if (mesh.name == mesh_name && mesh.decoded) return &mesh;
      }
      return nullptr;
    };
    auto find_group = [&](const std::string& group_name)
        -> const ghogx::milo_scene::GroupObj* {
      for (const auto& group : track_scene.groups) {
        if (group.name == group_name && group.has_transform) return &group;
      }
      return nullptr;
    };
    auto group_has_child = [&](const std::string& group_name,
                               const std::string& child_name) {
      const auto* group = find_group(group_name);
      if (!group) return false;
      return std::find(group->children.begin(), group->children.end(),
                       child_name) != group->children.end();
    };
    auto group_has_star_lane_child = [&](const std::string& group_name) {
      const auto* group = find_group(group_name);
      if (!group) return false;
      return std::any_of(group->children.begin(), group->children.end(),
                         [](const std::string& child) {
                           return child.size() > 10 &&
                                  child.compare(child.size() - 10, 10,
                                                "_star.mesh") == 0;
                         });
    };
    auto convert_mesh = [&](const std::string& mesh_name,
                            const std::string& material_override = std::string(),
                            const std::string& parent_override = std::string()) {
      RuntimeMesh out;
      const auto* mesh = find_mesh(mesh_name);
      if (!mesh || mesh->verts.empty() || mesh->indices.empty()) return out;
      const std::string& material_name =
          material_override.empty() ? mesh->material : material_override;
      const auto* mat = track_scene.find_mat(material_name);
      if (!mat) return out;
      out.texture_name = mat->diffuse_tex;
      out.blend = mat->blend;
      out.tex_gen = mat->tex_gen;
      out.z_mode = mat->z_mode;
      out.use_environ = mat->use_environ;
      out.prelit = mat->prelit;
      out.point_lights = mat->point_lights;
      out.intensify = mat->intensify;
      out.cull = mat->cull;
      out.texture_wrap = mat->tex_wrap != 0 || mat->tex_scale[0] > 1.01f ||
                         mat->tex_scale[1] > 1.01f;
      for (int i = 0; i < 4; ++i) out.material_color[i] = mat->color[i];
      if (!out.texture_name.empty()) texture_names.insert(out.texture_name);
      out.verts.reserve(mesh->verts.size());
      float min_x = 0.0f;
      float max_x = 0.0f;
      float min_y = 0.0f;
      float max_y = 0.0f;
      float min_z = 0.0f;
      float max_z = 0.0f;
      float min_u = 0.0f;
      float max_u = 0.0f;
      float min_v = 0.0f;
      float max_v = 0.0f;
      bool have_bounds = false;
      const std::string& parent_name =
          parent_override.empty() ? mesh->parent : parent_override;
      const auto* parent_group = find_group(parent_name);
      const float source_x_span = mesh->bb_max[0] - mesh->bb_min[0];
      const bool source_backed_tight_tail_cross_u =
          mesh_name == "tail02.mesh" &&
          material_name == "tail_glow_tight.mat";
      const bool synthesize_tail_cross_u =
          mesh_name == "tail02.mesh" && source_x_span > 0.001f &&
          (source_backed_tight_tail_cross_u ||
           env_enabled("GHOGX_EXPERIMENT_HIGHWAY_TAIL_SYNTH_U"));
      for (const auto& src : mesh->verts) {
        MeshVertex dst;
        float x = src.px * mesh->local.rot[0][0] +
                  src.py * mesh->local.rot[1][0] +
                  src.pz * mesh->local.rot[2][0] + mesh->local.pos[0];
        float y = src.px * mesh->local.rot[0][1] +
                  src.py * mesh->local.rot[1][1] +
                  src.pz * mesh->local.rot[2][1] + mesh->local.pos[1];
        float z = src.px * mesh->local.rot[0][2] +
                  src.py * mesh->local.rot[1][2] +
                  src.pz * mesh->local.rot[2][2] + mesh->local.pos[2];
        if (parent_group) {
          const auto& g = parent_group->local;
          const float gx = x * g.rot[0][0] + y * g.rot[1][0] +
                           z * g.rot[2][0] + g.pos[0];
          const float gy = x * g.rot[0][1] + y * g.rot[1][1] +
                           z * g.rot[2][1] + g.pos[1];
          const float gz = x * g.rot[0][2] + y * g.rot[1][2] +
                           z * g.rot[2][2] + g.pos[2];
          x = gx - g.pos[0];
          y = gy - g.pos[1];
          z = gz;
        }
        dst.x = x;
        dst.y = y;
        dst.z = z;
        float nx = src.nx * mesh->local.rot[0][0] +
                   src.ny * mesh->local.rot[1][0] +
                   src.nz * mesh->local.rot[2][0];
        float ny = src.nx * mesh->local.rot[0][1] +
                   src.ny * mesh->local.rot[1][1] +
                   src.nz * mesh->local.rot[2][1];
        float nz = src.nx * mesh->local.rot[0][2] +
                   src.ny * mesh->local.rot[1][2] +
                   src.nz * mesh->local.rot[2][2];
        if (parent_group) {
          const auto& g = parent_group->local;
          const float gnx = nx * g.rot[0][0] + ny * g.rot[1][0] +
                            nz * g.rot[2][0];
          const float gny = nx * g.rot[0][1] + ny * g.rot[1][1] +
                            nz * g.rot[2][1];
          const float gnz = nx * g.rot[0][2] + ny * g.rot[1][2] +
                            nz * g.rot[2][2];
          nx = gnx;
          ny = gny;
          nz = gnz;
        }
        const auto normal = normalize_vec3({nx, ny, nz});
        dst.nx = normal[0];
        dst.ny = normal[1];
        dst.nz = normal[2];
        dst.r = src.r * mat->color[0];
        dst.g = src.g * mat->color[1];
        dst.b = src.b * mat->color[2];
        dst.a = src.a * mat->color[3];
        // GH2's native tail ribbon stores a constant U and a cross-tail V on
        // tail02.mesh; broad lane-tail materials rely on texgen=5. The exact
        // environment-coordinate path is still unproven for those broad bodies,
        // so they keep decoded source UVs by default. PCSX2 frame17 local
        // tight-tail strips span ST 0..1, so tail_glow_tight promotes the
        // reconstructed cross-tail U without opting in the unresolved bodies.
        const float source_u =
            synthesize_tail_cross_u
                ? std::clamp((src.px - mesh->bb_min[0]) / source_x_span,
                             0.0f, 1.0f)
                : src.u;
        dst.u = source_u * mat->tex_xfm[0][0] +
                src.v * mat->tex_xfm[1][0] + mat->tex_xfm[2][0];
        dst.v = source_u * mat->tex_xfm[0][1] +
                src.v * mat->tex_xfm[1][1] + mat->tex_xfm[2][1];
        if (!have_bounds) {
          min_x = max_x = dst.x;
          min_y = max_y = dst.y;
          min_z = max_z = dst.z;
          min_u = max_u = dst.u;
          min_v = max_v = dst.v;
          have_bounds = true;
        } else {
          min_x = std::min(min_x, dst.x);
          max_x = std::max(max_x, dst.x);
          min_y = std::min(min_y, dst.y);
          max_y = std::max(max_y, dst.y);
          min_z = std::min(min_z, dst.z);
          max_z = std::max(max_z, dst.z);
          min_u = std::min(min_u, dst.u);
          max_u = std::max(max_u, dst.u);
          min_v = std::min(min_v, dst.v);
          max_v = std::max(max_v, dst.v);
        }
        out.verts.push_back(dst);
      }
      out.min_x = min_x;
      out.max_x = max_x;
      out.min_y = min_y;
      out.max_y = max_y;
      out.min_z = min_z;
      out.max_z = max_z;
      out.min_u = min_u;
      out.max_u = max_u;
      out.min_v = min_v;
      out.max_v = max_v;
      if (have_bounds && out.texture_name.size() > 0) {
        const bool uv_repeats =
            min_u < -0.05f || min_v < -0.05f ||
            max_u > 1.05f || max_v > 1.05f;
        out.texture_wrap = out.texture_wrap || uv_repeats;
      }
      out.center_x = (min_x + max_x) * 0.5f;
      out.center_y = (min_y + max_y) * 0.5f;
      out.indices = mesh->indices;
      out.ok = !out.verts.empty() && !out.indices.empty();
      return out;
    };
    auto convert_mesh_with_material_fallback =
        [&](const std::string& mesh_name, const std::string& material_name) {
          RuntimeMesh out = convert_mesh(mesh_name, material_name);
          if (out.ok) return out;
          return convert_mesh(mesh_name);
        };
    auto material_texture = [&](const std::string& mat_name) {
      const auto* mat = track_scene.find_mat(mat_name);
      if (!mat || mat->diffuse_tex.empty()) return std::string{};
      texture_names.insert(mat->diffuse_tex);
      return mat->diffuse_tex;
    };
    auto smasher_add_texture = [&](const std::string& mat_name,
                                   uint8_t& blend_out) {
      const auto* mat = track_scene.find_mat(mat_name);
      if (!mat || mat->diffuse_tex.empty()) return std::string{};
      texture_names.insert(mat->diffuse_tex);
      blend_out = mat->blend;
      if (mat->blend == kHighwayBlendSrcAlpha &&
          is_smasher_add_mask_tex_name(mat->diffuse_tex)) {
        smasher_add_alpha_key_sources.insert(mat->diffuse_tex);
        return smasher_add_alpha_key_alias(mat->diffuse_tex);
      }
      return mat->diffuse_tex;
    };
    auto alpha_key_source_alpha_ring_add = [&](RuntimeMesh& mesh) {
      if (!mesh.ok || mesh.blend != kHighwayBlendSrcAlpha ||
          !is_smasher_ring_add_mask_tex_name(mesh.texture_name)) {
        return;
      }
      smasher_ring_add_alpha_key_sources.insert(mesh.texture_name);
      mesh.texture_name = smasher_ring_add_alpha_key_alias(mesh.texture_name);
    };
    auto alpha_key_source_alpha_smasher_add = [&](RuntimeMesh& mesh) {
      if (!mesh.ok || mesh.blend != kHighwayBlendSrcAlpha ||
          !is_smasher_add_mask_tex_name(mesh.texture_name)) {
        return;
      }
      smasher_add_alpha_key_sources.insert(mesh.texture_name);
      mesh.texture_name = smasher_add_alpha_key_alias(mesh.texture_name);
    };
    auto convert_line_material = [&](const std::string& mat_name) {
      RuntimeLineMaterial out;
      const auto* mat = track_scene.find_mat(mat_name);
      if (!mat || mat->diffuse_tex.empty()) return out;
      out.material_name = mat_name;
      out.texture_name = mat->diffuse_tex;
      out.blend = mat->blend;
      out.z_mode = mat->z_mode;
      out.tex_gen = mat->tex_gen;
      out.prelit = mat->prelit;
      out.alpha_cut = mat->alpha_cut;
      out.alpha_write = mat->alpha_write;
      out.intensify = mat->intensify;
      out.cull = mat->cull;
      out.texture_wrap = mat->tex_wrap != 0 || mat->tex_scale[0] > 1.01f ||
                         mat->tex_scale[1] > 1.01f;
      for (int i = 0; i < 4; ++i) out.color[i] = mat->color[i];
      texture_names.insert(out.texture_name);
      out.ok = true;
      return out;
    };
    auto find_particle = [&](const std::string& particle_name)
        -> const ghogx::milo_scene::ParticleSysObj* {
      for (const auto& particle : track_scene.particles) {
        if (particle.name == particle_name && particle.decoded) {
          return &particle;
        }
      }
      return nullptr;
    };
    auto convert_particle = [&](const std::string& particle_name,
                                const std::string& group_name = std::string()) {
      RuntimeParticleSystem out;
      const auto* particle = find_particle(particle_name);
      if (!particle || particle->material.empty()) return out;
      const auto* mat = track_scene.find_mat(particle->material);
      if (!mat || mat->diffuse_tex.empty()) return out;
      const auto* group = group_name.empty() ? nullptr : find_group(group_name);
      out.name = particle->name;
      out.texture_name = mat->diffuse_tex;
      out.blend = mat->blend;
      out.max_particles = particle->max_particles;
      texture_names.insert(out.texture_name);
      for (int i = 0; i < 3; ++i) {
        out.local_pos[i] =
            particle->local.pos[i] + (group ? group->local.pos[i] : 0.0f);
        out.box_extent_min[i] = particle->box_extent_min[i];
        out.box_extent_max[i] = particle->box_extent_max[i];
        out.force_dir[i] = particle->force_dir[i];
      }
      for (int i = 0; i < 4; ++i) {
        out.mat_color[i] = mat->color[i];
        out.start_color_low[i] = particle->start_color_low[i];
        out.start_color_high[i] = particle->start_color_high[i];
        out.mid_color_low[i] = particle->mid_color_low[i];
        out.mid_color_high[i] = particle->mid_color_high[i];
        out.end_color_low[i] = particle->end_color_low[i];
        out.end_color_high[i] = particle->end_color_high[i];
      }
      out.speed_min = particle->speed_min;
      out.speed_max = particle->speed_max;
      out.pitch_min = particle->pitch_min;
      out.pitch_max = particle->pitch_max;
      out.yaw_min = particle->yaw_min;
      out.yaw_max = particle->yaw_max;
      out.start_size_min = particle->start_size_min;
      out.start_size_max = particle->start_size_max;
      out.delta_size_min = particle->delta_size_min;
      out.delta_size_max = particle->delta_size_max;
      out.lifetime_min = particle->lifetime_min;
      out.lifetime_max = particle->lifetime_max;
      out.grow_ratio = particle->grow_ratio;
      out.shrink_ratio = particle->shrink_ratio;
      out.mid_color_ratio = particle->mid_color_ratio;
      out.bubble = particle->bubble;
      out.bubble_period_min = particle->bubble_period_min;
      out.bubble_period_max = particle->bubble_period_max;
      out.bubble_size_min = particle->bubble_size_min;
      out.bubble_size_max = particle->bubble_size_max;
      std::string anim_name = particle_name;
      if (anim_name.size() >= 5 &&
          anim_name.rfind(".part") == anim_name.size() - 5) {
        anim_name.replace(anim_name.size() - 5, 5, ".panim");
      }
      const auto anim_it = track_particle_anims.find(anim_name);
      if (anim_it != track_particle_anims.end() &&
          anim_it->second.particle == particle_name) {
        out.anim_start_color_keys = anim_it->second.start_color_keys;
        out.anim_end_color_keys = anim_it->second.end_color_keys;
        out.anim_emit_rate_keys = anim_it->second.emit_rate_keys;
        out.anim_speed_keys = anim_it->second.speed_keys;
        out.anim_life_keys = anim_it->second.life_keys;
        out.anim_start_size_keys = anim_it->second.start_size_keys;
      }
      out.ok = true;
      return out;
    };
    smasher_normal_texture_name_ = material_texture("gem_smasher.mat");
    for (int lane = 0; lane < 5; ++lane) {
      const std::string& name = slot_color_names_[lane];
      gem_mesh_[lane] = convert_mesh(name + "_gem.mesh");
      gem_specular_mesh_[lane] =
          convert_mesh(name + "_gem.mesh", "gem_" + name + "_1.mat");
      hopo_mesh_[lane] =
          convert_mesh(name + "_hopo.mesh", std::string{}, "gem_template.view");
      // The generic track_graphics.dta names a gem_starpower_%s material
      // format, but GH2 track.milo does not contain those Mat objects. Keep
      // the decoded star material binding unless source assets prove a real
      // override exists.
      star_mesh_[lane] = convert_mesh(name + "_star.mesh");
      star_top_mesh_[lane] = convert_mesh(name + "_top_star.mesh");
      tail_mesh_[lane] = convert_mesh("tail02.mesh", "tail_" + name + ".mat");
      held_tail_mesh_[lane] =
          convert_mesh("tail02.mesh", "tail_glow_" + name + ".mat");
      held_tail_line_material_[lane] =
          convert_line_material("tail_glow_" + name + ".mat");
      smasher_texture_names_[lane] =
          material_texture("gem_smasher_" + name + ".mat");
      smasher_add_texture_names_[lane] =
          smasher_add_texture("gem_smasher_" + name + "_1.mat",
                              smasher_add_blends_[lane]);
      smasher_add_meshes_[lane] =
          convert_mesh("gem_smasher.mesh", "gem_smasher_" + name + "_1.mat");
      alpha_key_source_alpha_smasher_add(smasher_add_meshes_[lane]);
      smasher_ring_texture_names_[lane] =
          material_texture("now_ring_" + name + ".mat");
      smasher_rim_meshes_[lane] =
          convert_mesh_with_material_fallback("smasher_rim.mesh",
                                              "now_ring_" + name + ".mat");
      smasher_ring_add_meshes_[lane] =
          convert_mesh("smasher_rim.mesh", "now_ring_" + name + "_1.mat");
      alpha_key_source_alpha_ring_add(smasher_ring_add_meshes_[lane]);
    }
    star_base_mesh_ = convert_mesh("star_base.mesh");
    star_overlay_mesh_ = convert_mesh("star2.mesh");
    star_black_top_mesh_ = convert_mesh("top_star_black.mesh");
    if (star_black_top_mesh_.ok && star_black_top_mesh_.texture_name == "gem.tex") {
      star_black_top_mesh_.texture_name = kStarBlackTopTextureAlias;
    }
    moving_note_standard_has_top_ =
        group_has_child("gem_template.view", "top.mesh");
    moving_note_standard_has_glow_ =
        group_has_child("gem_template.view", "glow.mesh");
    moving_note_star_has_base_ =
        group_has_child("gem_star.view", "star_base.mesh");
    moving_note_star_has_lane_ = group_has_star_lane_child("gem_star.view");
    moving_note_star_has_overlay_ =
        group_has_child("gem_star.view", "star2.mesh");
    moving_note_star_has_top_ =
        group_has_child("gem_star.view", "top_star_black.mesh") ||
        std::any_of(star_top_mesh_.begin(), star_top_mesh_.end(),
                    [](const RuntimeMesh& mesh) { return mesh.ok; });
    moving_note_star_prefers_black_top_ =
        group_has_child("gem_star.view", "top_star_black.mesh");
    if (star_base_mesh_.ok) {
      star_note_anim_ = load_track_transanim_transform_anim(
          hdr_path, ark_path, "star_base.tnm");
      if (mesh_transform_anim_empty(star_note_anim_)) {
        star_note_anim_ =
            load_track_transanim_transform_anim(hdr_path, ark_path, "star.tnm");
      }
      star_note_anim_duration_frames_ =
          mesh_transform_anim_duration_frames(star_note_anim_);
      star_note_rotation_keys_ = star_note_anim_.rotation_keys;
      star_note_rotation_duration_frames_ =
          quat_anim_duration_frames(star_note_rotation_keys_);
      star_base_rotation_keys_ =
          load_track_transanim_rotation_keys(hdr_path, ark_path,
                                             "star_base.tnm");
      star_base_rotation_duration_frames_ =
          quat_anim_duration_frames(star_base_rotation_keys_);
      if (star_note_rotation_keys_.empty() && !star_base_rotation_keys_.empty()) {
        star_note_rotation_keys_ = star_base_rotation_keys_;
        star_note_rotation_duration_frames_ =
            star_base_rotation_duration_frames_;
        star_note_anim_.rotation_keys = star_base_rotation_keys_;
        star_note_anim_duration_frames_ =
            std::max(star_note_anim_duration_frames_,
                     star_base_rotation_duration_frames_);
      }
      if (!star_note_rotation_keys_.empty()) {
        std::fprintf(stderr,
                     "[highway] star_base.tnm transform pos=%zu rot=%zu scale=%zu "
                     "duration=%.1f rotation_duration=%.1f\n",
                     star_note_anim_.translation_keys.size(),
                     star_note_rotation_keys_.size(),
                     star_note_anim_.scale_keys.size(),
                     star_note_anim_duration_frames_,
                     star_note_rotation_duration_frames_);
      }
    }
    gem_top_mesh_ = convert_mesh("top.mesh");
    auto make_pc_standard_top_mesh = [](const RuntimeMesh& source) {
      RuntimeMesh out = source;
      if (!source.ok || source.verts.empty() || source.indices.empty() ||
          source.verts.size() * 3u > UINT16_MAX) {
        return RuntimeMesh{};
      }
      out.verts.clear();
      out.verts.reserve(source.verts.size() * 3u);
      out.indices.clear();
      out.indices.reserve(source.indices.size() * 3u);
      auto append_black_copy = [&](float scale, float alpha) {
        const uint16_t vertex_base =
            static_cast<uint16_t>(out.verts.size());
        for (MeshVertex vertex : source.verts) {
          vertex.x *= scale;
          vertex.y *= scale;
          vertex.r = 0.0f;
          vertex.g = 0.0f;
          vertex.b = 0.0f;
          vertex.a *= alpha;
          out.verts.push_back(vertex);
        }
        for (uint16_t index : source.indices) {
          out.indices.push_back(static_cast<uint16_t>(index + vertex_base));
        }
      };
      append_black_copy(kPcStandardRingFeatherScale,
                        kPcStandardRingFeatherAlpha);
      append_black_copy(kPcStandardRingSolidScale, 1.0f);
      const uint16_t source_vertex_base =
          static_cast<uint16_t>(out.verts.size());
      out.verts.insert(out.verts.end(), source.verts.begin(),
                       source.verts.end());
      for (uint16_t index : source.indices) {
        out.indices.push_back(
            static_cast<uint16_t>(index + source_vertex_base));
      }
      out.min_x = source.min_x * kPcStandardRingFeatherScale;
      out.max_x = source.max_x * kPcStandardRingFeatherScale;
      out.min_y = source.min_y * kPcStandardRingFeatherScale;
      out.max_y = source.max_y * kPcStandardRingFeatherScale;
      out.center_x = (out.min_x + out.max_x) * 0.5f;
      out.center_y = (out.min_y + out.max_y) * 0.5f;
      out.ok = true;
      return out;
    };
    pc_standard_top_mesh_ = make_pc_standard_top_mesh(gem_top_mesh_);
    gem_glow_mesh_ = convert_mesh("glow.mesh");
    held_tight_tail_mesh_ = convert_mesh("tail02.mesh", "tail_glow_tight.mat");
    held_tight_tail_line_material_ =
        convert_line_material("tail_glow_tight.mat");
    burn_castlight_mesh_ = convert_mesh("burn_castlight.mesh");
    auto append_particle = [&](std::vector<RuntimeParticleSystem>& dst,
                               const char* particle_name,
                               const char* group_name) {
      RuntimeParticleSystem particle =
          convert_particle(particle_name, group_name);
      if (particle.ok) dst.push_back(std::move(particle));
    };
    append_particle(burn_tail_particles_, "burn_tail_base.part",
                    "burn_tail.view");
    append_particle(burn_tail_particles_, "burn_tail_big.part",
                    "burn_tail.view");
    append_particle(smash_normal_particles_, "smash_normal_1.part",
                    "smash_normal.view");
    append_particle(smash_normal_particles_, "smash_normal_2.part",
                    "smash_normal.view");
    append_particle(smash_normal_particles_, "smash_normal_0.part",
                    "smash_normal.view");
    append_particle(smash_normal_particles_, "smash_normal_3.part",
                    "smash_normal.view");
    append_particle(smash_bonus_particles_, "smash_bonus_4.part",
                    "smash_bonus.view");
    append_particle(smash_bonus_particles_, "smash_bonus_2.part",
                    "smash_bonus.view");
    append_particle(smash_bonus_particles_, "smash_bonus_3.part",
                    "smash_bonus.view");
    append_particle(smash_bonus_particles_, "smash_bonus_1.part",
                    "smash_bonus.view");
    append_particle(smash_star_particles_, "smash_star_0.part",
                    "smash_star.view");
    append_particle(smash_star_particles_, "smash_star_1.part",
                    "smash_star.view");
    append_particle(smash_star_particles_, "smash_star_2.part",
                    "smash_star.view");
    append_particle(smash_star_particles_, "smash_star_3.part",
                    "smash_star.view");
    append_particle(smash_combo_particles_, "smash_combo_burst.part",
                    "smash_combo.view");
    star_phrase_tail_mesh_ = convert_mesh("tail02.mesh", "tail_star.mat");
    star_tail_mesh_ = convert_mesh("tail02.mesh", "tail_glow_star.mat");
    star_tail_line_material_ = convert_line_material("tail_glow_star.mat");
    if (!star_tail_mesh_.ok) {
      star_tail_mesh_ = convert_mesh("tail02.mesh", "tail_glow_tight.mat");
    }
    if (!star_tail_mesh_.ok) {
      star_tail_mesh_ = convert_mesh("tail02.mesh", "tail_star.mat");
    }
    whammy_tail_mesh_ = convert_mesh("tail02.mesh", "tail_glow_whammy.mat");
    whammy_tail_line_material_ =
        convert_line_material("tail_glow_whammy.mat");
    bonus_tail_mesh_ = convert_mesh("tail02.mesh", "tail_bonus.mat");
    bonus_glow_tail_mesh_ =
        convert_mesh("tail02.mesh", "tail_glow_bonus.mat");
    bonus_tail_line_material_ =
        convert_line_material("tail_glow_bonus.mat");
    bonus_gem_mesh_ = convert_mesh("gem_bonus.mesh");
    bonus_gem_overlay_mesh_ = convert_mesh("gem_bonus2.mesh");
    gem_sparkle_mesh_ = convert_mesh("gem_sparkle.mesh");
    if (gem_sparkle_mesh_.ok) {
      gem_sparkle_anim_ = load_track_transanim_transform_anim(
          hdr_path, ark_path, "gem_sparkle.tnm");
      gem_sparkle_anim_duration_frames_ =
          mesh_transform_anim_duration_frames(gem_sparkle_anim_);
      if (!mesh_transform_anim_empty(gem_sparkle_anim_)) {
        std::fprintf(stderr,
                     "[highway] gem_sparkle.tnm transform pos=%zu rot=%zu scale=%zu duration=%.1f\n",
                     gem_sparkle_anim_.translation_keys.size(),
                     gem_sparkle_anim_.rotation_keys.size(),
                     gem_sparkle_anim_.scale_keys.size(),
                     gem_sparkle_anim_duration_frames_);
      }
    }
    bonus_spark1_mesh_ = convert_mesh("gem_bonus_spark1.mesh");
    bonus_spark2_mesh_ = convert_mesh("gem_bonus_spark2.mesh");
    track_surface_mesh_ = convert_mesh("track_surface5.mesh");
    track_mask_mesh_ = convert_mesh("track_mask.mesh");
    if (track_surface_mesh_.ok) {
      float min_alpha = 1.0f;
      float max_alpha = 0.0f;
      float min_rgb = 1.0f;
      float max_rgb = 0.0f;
      for (const auto& vertex : track_surface_mesh_.verts) {
        min_alpha = std::min(min_alpha, vertex.a);
        max_alpha = std::max(max_alpha, vertex.a);
        min_rgb = std::min({min_rgb, vertex.r, vertex.g, vertex.b});
        max_rgb = std::max({max_rgb, vertex.r, vertex.g, vertex.b});
      }
      std::fprintf(
          stderr,
          "[highway] track surface source: texture=%s blend=%d prelit=%d "
          "environ=%d point_lights=%d intensify=%d rgba_rgb=%.3f..%.3f "
          "alpha=%.3f..%.3f uv=(%.3f..%.3f,%.3f..%.3f)\n",
          track_surface_mesh_.texture_name.c_str(), track_surface_mesh_.blend,
          track_surface_mesh_.prelit ? 1 : 0,
          track_surface_mesh_.use_environ ? 1 : 0,
          track_surface_mesh_.point_lights ? 1 : 0,
          track_surface_mesh_.intensify ? 1 : 0, min_rgb, max_rgb, min_alpha,
          max_alpha, track_surface_mesh_.min_u, track_surface_mesh_.max_u,
          track_surface_mesh_.min_v, track_surface_mesh_.max_v);
    }
    track_side_rails_mesh_ = convert_mesh("track_side_rails5.mesh");
    track_lane_lines_mesh_ = convert_mesh("track_lane_lines5.mesh");
    track_extend_anim_ = load_track_intro_transanim_source_order(
        hdr_path, ark_path, "extend_track_normal.tnm");
    if (!mesh_transform_anim_empty(track_extend_anim_)) {
      std::fprintf(
          stderr,
          "[highway] extend_track_normal.tnm transform pos=%zu rot=%zu scale=%zu source_range=0..1920\n",
          track_extend_anim_.translation_keys.size(),
          track_extend_anim_.rotation_keys.size(),
          track_extend_anim_.scale_keys.size());
    }
    star_power_track_glow_mesh_ = convert_mesh("lightning_trackglow.mesh");
    bar_line_mesh_ = convert_mesh("bar_line5.mesh");
    beat_line_mesh_ = convert_mesh("beat_line5.mesh");
    half_beat_line_mesh_ = convert_mesh("half_beat_line5.mesh");
    quarter_beat_line_mesh_ = convert_mesh("quarter_beat_line5.mesh");
    gem_smasher_mesh_ = convert_mesh("gem_smasher.mesh");
    smasher_rim_mesh_ = convert_mesh("smasher_rim.mesh");
    bonus_smasher_rim_mesh_ =
        convert_mesh_with_material_fallback("smasher_rim.mesh",
                                            "now_ring_bonus.mat");
    bonus_smasher_ring_add_mesh_ =
        convert_mesh("smasher_rim.mesh", "now_ring_bonus_1.mat");
    alpha_key_source_alpha_ring_add(bonus_smasher_ring_add_mesh_);
    smasher_shadow_mesh_ = convert_mesh("smasher shadow.mesh");
    smasher_press_anim_ = load_track_transanim_transform_anim(
        hdr_path, ark_path, "gem_smasher.tnm");
    if (mesh_transform_anim_empty(smasher_press_anim_)) {
      smasher_press_anim_ = load_track_transanim_transform_anim(
          hdr_path, ark_path, "gem_smasher_hit.tnm");
    }
    smasher_press_anim_duration_frames_ =
        mesh_transform_anim_duration_frames(smasher_press_anim_);
    if (!mesh_transform_anim_empty(smasher_press_anim_)) {
      std::fprintf(stderr,
                   "[highway] gem_smasher.tnm transform pos=%zu rot=%zu "
                   "scale=%zu duration=%.1f\n",
                   smasher_press_anim_.translation_keys.size(),
                   smasher_press_anim_.rotation_keys.size(),
                   smasher_press_anim_.scale_keys.size(),
                   smasher_press_anim_duration_frames_);
    }
    hit_flame_mesh_ = convert_mesh("smash_flamelight.mesh");
    star_collect_flame_mesh_ = convert_mesh("smash_flamelight_starcollect.mesh");
    bonus_hit_flame_mesh_ = convert_mesh("smash_flamelight_bonus.mesh");
    hit_flame_anim_ = load_track_transanim_transform_anim(
        hdr_path, ark_path, "smash_flamelight_normal.tnm");
    hit_flame_anim_duration_frames_ =
        mesh_transform_anim_duration_frames(hit_flame_anim_);
    star_collect_flame_anim_ = load_track_transanim_transform_anim(
        hdr_path, ark_path, "smash_flamelight_starcollect.tnm");
    if (mesh_transform_anim_empty(star_collect_flame_anim_) &&
        !mesh_transform_anim_empty(hit_flame_anim_)) {
      star_collect_flame_anim_ = hit_flame_anim_;
    }
    star_collect_flame_anim_duration_frames_ =
        mesh_transform_anim_duration_frames(star_collect_flame_anim_);
    bonus_hit_flame_anim_ = load_track_transanim_transform_anim(
        hdr_path, ark_path, "smash_flamelight_bonus.tnm");
    if (mesh_transform_anim_empty(bonus_hit_flame_anim_) &&
        !mesh_transform_anim_empty(hit_flame_anim_)) {
      bonus_hit_flame_anim_ = hit_flame_anim_;
    }
    bonus_hit_flame_anim_duration_frames_ =
        mesh_transform_anim_duration_frames(bonus_hit_flame_anim_);
    miss_mesh_ = convert_mesh("miss.mesh", "gem_miss.mat");
    miss_top_mesh_ = convert_mesh("top_miss.mesh", "gem_miss_1.mat");
    star_miss_mesh_ = convert_mesh("star_miss.mesh", "gem_miss.mat");
    star_miss_top_mesh_ = convert_mesh("top_star_miss.mesh", "gem_miss_1.mat");
    bonus_smasher_texture_name_ = material_texture("gem_smasher_bonus.mat");
    bonus_smasher_add_texture_name_ =
        smasher_add_texture("gem_smasher_bonus_1.mat",
                            bonus_smasher_add_blend_);
    bonus_smasher_add_mesh_ =
        convert_mesh("gem_smasher.mesh", "gem_smasher_bonus_1.mat");
    alpha_key_source_alpha_smasher_add(bonus_smasher_add_mesh_);
    bonus_smasher_ring_texture_name_ = material_texture("now_ring_bonus.mat");
    for (int i = 0; i < 3; ++i) {
      const std::string stem =
          "smash_combo_lightning0" + std::to_string(i + 1);
      combo_lightning_mesh_[i] = convert_mesh(stem + ".mesh");
      combo_lightning_anim_[i] = load_track_transanim_transform_anim(
          hdr_path, ark_path, (stem + ".tnm").c_str());
      combo_lightning_anim_duration_frames_[i] =
          mesh_transform_anim_duration_frames(combo_lightning_anim_[i]);
      if (!mesh_transform_anim_empty(combo_lightning_anim_[i])) {
        std::fprintf(stderr,
                     "[highway] %s.tnm transform pos=%zu rot=%zu scale=%zu "
                     "duration=%.1f\n",
                     stem.c_str(),
                     combo_lightning_anim_[i].translation_keys.size(),
                     combo_lightning_anim_[i].rotation_keys.size(),
                     combo_lightning_anim_[i].scale_keys.size(),
                     combo_lightning_anim_duration_frames_[i]);
      }
    }
    const auto side_rail_anims = load_track_mat_anim_colors(hdr_path, ark_path);
    side_rails_building_ = side_rail_color_from_anim(
        side_rail_anims, "side_rails_building.mnm", false);
    side_rails_none_ =
        side_rail_color_from_anim(side_rail_anims, "side_rails_none.mnm",
                                  false);
    side_rails_warning_ =
        side_rail_color_from_anim(side_rail_anims, "side_rails_warning.mnm",
                                  true);
    side_rails_star_ =
        side_rail_color_from_anim(side_rail_anims, "side_rails_star.mnm",
                                  false);
    side_rails_warning_star_ = side_rail_color_from_anim(
        side_rail_anims, "side_rails_warning_star.mnm", true);
    surface_flash_2x_ =
        mat_anim_color_curve(side_rail_anims, "surface_flash_2x.mnm");
    surface_flash_3x_ =
        mat_anim_color_curve(side_rail_anims, "surface_flash_3x.mnm");
    surface_flash_4x_ =
        mat_anim_color_curve(side_rail_anims, "surface_flash_4x.mnm");
    for (int i = 0; i < 3; ++i) {
      const std::string stem =
          "smash_combo_lightning0" + std::to_string(i + 1);
      combo_lightning_color_anim_[i] =
          mat_anim_color_curve(side_rail_anims, stem + ".mnm");
    }
    hit_flame_color_anim_ =
        mat_anim_color_curve(side_rail_anims, "smash_flamelight_normal.mnm");
    star_collect_flame_color_anim_ = mat_anim_color_curve(
        side_rail_anims, "smash_flamelight_starcollect.mnm");
    bonus_hit_flame_color_anim_ =
        mat_anim_color_curve(side_rail_anims, "smash_flamelight_bonus.mnm");
    burn_castlight_color_anim_ =
        mat_anim_color_curve(side_rail_anims, "burn_castlight.mnm");
    if (!bonus_hit_flame_color_anim_.ok && star_collect_flame_color_anim_.ok) {
      bonus_hit_flame_color_anim_ = star_collect_flame_color_anim_;
    }
    std::fprintf(stderr,
                 "[highway] native note meshes: gems=%d speculars=%d top=%d hopos=%d stars=%d glow=%d\n",
                 static_cast<int>(std::count_if(
                     gem_mesh_.begin(), gem_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 static_cast<int>(std::count_if(
                     gem_specular_mesh_.begin(), gem_specular_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 gem_top_mesh_.ok ? 1 : 0,
                 static_cast<int>(std::count_if(
                     hopo_mesh_.begin(), hopo_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 static_cast<int>(std::count_if(
                     star_mesh_.begin(), star_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 gem_glow_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native star-note group layers: base=%d overlay=%d top=%d\n",
                 star_base_mesh_.ok ? 1 : 0,
                 star_overlay_mesh_.ok ? 1 : 0,
                 star_black_top_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] moving note stack: standard top=%d glow=%d star base=%d lane=%d overlay=%d top=%d black_top=%d\n",
                 moving_note_standard_has_top_ ? 1 : 0,
                 moving_note_standard_has_glow_ ? 1 : 0,
                 moving_note_star_has_base_ ? 1 : 0,
                 moving_note_star_has_lane_ ? 1 : 0,
                 moving_note_star_has_overlay_ ? 1 : 0,
                 moving_note_star_has_top_ ? 1 : 0,
                 moving_note_star_prefers_black_top_ ? 1 : 0);
    if (env_enabled("GHOGX_DEBUG_HIGHWAY_NOTE_MESHES")) {
      auto log_runtime_mesh = [](const char* label, const RuntimeMesh& mesh) {
        std::fprintf(stderr,
                     "[highway] note mesh %-18s ok=%d tex=%s blend=%u "
                     "zmode=%u texgen=%u use_env=%d prelit=%d point_lights=%d "
                     "intensify=%d sampler=%s "
                     "mat=%.3f,%.3f,%.3f,%.3f "
                     "verts=%zu tris=%zu\n",
                     label, mesh.ok ? 1 : 0, mesh.texture_name.c_str(),
                     static_cast<unsigned>(mesh.blend),
                     static_cast<unsigned>(mesh.z_mode),
                     static_cast<unsigned>(mesh.tex_gen),
                     mesh.use_environ ? 1 : 0, mesh.prelit ? 1 : 0,
                     mesh.point_lights ? 1 : 0,
                     mesh.intensify ? 1 : 0,
                     mesh.texture_wrap ? "wrap" : "clamp",
                     mesh.material_color[0], mesh.material_color[1],
                     mesh.material_color[2], mesh.material_color[3],
                     mesh.verts.size(),
                     mesh.indices.size() / 3);
        float min_r = 1.0f, min_g = 1.0f, min_b = 1.0f, min_a = 1.0f;
        float max_r = 0.0f, max_g = 0.0f, max_b = 0.0f, max_a = 0.0f;
        float min_nx = 1.0f, min_ny = 1.0f, min_nz = 1.0f;
        float max_nx = -1.0f, max_ny = -1.0f, max_nz = -1.0f;
        for (const auto& v : mesh.verts) {
          min_r = std::min(min_r, v.r);
          min_g = std::min(min_g, v.g);
          min_b = std::min(min_b, v.b);
          min_a = std::min(min_a, v.a);
          max_r = std::max(max_r, v.r);
          max_g = std::max(max_g, v.g);
          max_b = std::max(max_b, v.b);
          max_a = std::max(max_a, v.a);
          min_nx = std::min(min_nx, v.nx);
          min_ny = std::min(min_ny, v.ny);
          min_nz = std::min(min_nz, v.nz);
          max_nx = std::max(max_nx, v.nx);
          max_ny = std::max(max_ny, v.ny);
          max_nz = std::max(max_nz, v.nz);
        }
        if (mesh.verts.empty()) {
          min_r = min_g = min_b = min_a = 0.0f;
          min_nx = min_ny = min_nz = 0.0f;
          max_nx = max_ny = max_nz = 0.0f;
        }
        std::fprintf(stderr,
                     "[highway] note mesh bounds %-11s x=%.3f..%.3f y=%.3f..%.3f "
                     "z=%.3f..%.3f uv=%.3f..%.3f/%.3f..%.3f center=%.3f,%.3f "
                     "normal=%.3f..%.3f/%.3f..%.3f/%.3f..%.3f "
                     "rgba=%.3f..%.3f/%.3f..%.3f/%.3f..%.3f/%.3f..%.3f\n",
                     label, mesh.min_x, mesh.max_x, mesh.min_y, mesh.max_y,
                     mesh.min_z, mesh.max_z, mesh.min_u, mesh.max_u,
                     mesh.min_v, mesh.max_v,
                     mesh.center_x, mesh.center_y,
                     min_nx, max_nx, min_ny, max_ny, min_nz, max_nz,
                     min_r, max_r, min_g, max_g, min_b, max_b,
                     min_a, max_a);
      };
      auto log_runtime_line_material =
          [](const char* label, const RuntimeLineMaterial& material) {
            std::fprintf(stderr,
                         "[highway] line material %-14s ok=%d mat=%s tex=%s "
                         "blend=%u zmode=%u texgen=%u prelit=%d alpha_cut=%d "
                         "alpha_write=%d intensify=%d cull=%d sampler=%s "
                         "color=%.3f,%.3f,%.3f,%.3f\n",
                         label, material.ok ? 1 : 0,
                         material.material_name.c_str(),
                         material.texture_name.c_str(),
                         static_cast<unsigned>(material.blend),
                         static_cast<unsigned>(material.z_mode),
                         static_cast<unsigned>(material.tex_gen),
                         material.prelit ? 1 : 0,
                         material.alpha_cut ? 1 : 0,
                         material.alpha_write ? 1 : 0,
                         material.intensify ? 1 : 0,
                         material.cull ? 1 : 0,
                         material.texture_wrap ? "wrap" : "clamp",
                         material.color[0], material.color[1],
                         material.color[2], material.color[3]);
          };
      auto log_source_mesh = [&](const char* label, const std::string& name) {
        const auto* mesh = find_mesh(name);
        if (!mesh) {
          std::fprintf(stderr,
                       "[highway] note source %-18s mesh=%s missing\n",
                       label, name.c_str());
          return;
        }
        const auto world = track_scene.world_matrix(*mesh);
        std::fprintf(stderr,
                     "[highway] note source %-18s mesh=%s parent=%s mat=%s geom=%s\n",
                     label, name.c_str(), mesh->parent.c_str(),
                     mesh->material.c_str(), mesh->geometry_owner.c_str());
        std::fprintf(stderr,
                     "[highway] note source xfm %-11s local=%.3f,%.3f,%.3f "
                     "stored=%.3f,%.3f,%.3f world=%.3f,%.3f,%.3f\n",
                     label, mesh->local.pos[0], mesh->local.pos[1],
                     mesh->local.pos[2],
                     mesh->world_stored.pos[0], mesh->world_stored.pos[1],
                     mesh->world_stored.pos[2], world[12], world[13],
                     world[14]);
      };
      log_runtime_mesh("green_gem", gem_mesh_[0]);
      log_runtime_mesh("red_gem", gem_mesh_[1]);
      log_runtime_mesh("yellow_gem", gem_mesh_[2]);
      log_runtime_mesh("blue_gem", gem_mesh_[3]);
      log_runtime_mesh("orange_gem", gem_mesh_[4]);
      log_runtime_mesh("green_gem_spec", gem_specular_mesh_[0]);
      log_runtime_mesh("red_gem_spec", gem_specular_mesh_[1]);
      log_runtime_mesh("yellow_gem_spec", gem_specular_mesh_[2]);
      log_runtime_mesh("blue_gem_spec", gem_specular_mesh_[3]);
      log_runtime_mesh("orange_gem_spec", gem_specular_mesh_[4]);
      log_runtime_mesh("top", gem_top_mesh_);
      log_runtime_mesh("green_hopo", hopo_mesh_[0]);
      log_runtime_mesh("red_hopo", hopo_mesh_[1]);
      log_runtime_mesh("yellow_hopo", hopo_mesh_[2]);
      log_runtime_mesh("blue_hopo", hopo_mesh_[3]);
      log_runtime_mesh("orange_hopo", hopo_mesh_[4]);
      log_runtime_mesh("green_star", star_mesh_[0]);
      log_runtime_mesh("red_star", star_mesh_[1]);
      log_runtime_mesh("yellow_star", star_mesh_[2]);
      log_runtime_mesh("blue_star", star_mesh_[3]);
      log_runtime_mesh("orange_star", star_mesh_[4]);
      log_runtime_mesh("green_top_star", star_top_mesh_[0]);
      log_runtime_mesh("red_top_star", star_top_mesh_[1]);
      log_runtime_mesh("yellow_top_star", star_top_mesh_[2]);
      log_runtime_mesh("blue_top_star", star_top_mesh_[3]);
      log_runtime_mesh("orange_top_star", star_top_mesh_[4]);
      log_runtime_mesh("star_base", star_base_mesh_);
      log_runtime_mesh("star2", star_overlay_mesh_);
      log_runtime_mesh("top_star_black", star_black_top_mesh_);
      log_runtime_mesh("green_tail", tail_mesh_[0]);
      log_runtime_mesh("red_tail", tail_mesh_[1]);
      log_runtime_mesh("yellow_tail", tail_mesh_[2]);
      log_runtime_mesh("blue_tail", tail_mesh_[3]);
      log_runtime_mesh("orange_tail", tail_mesh_[4]);
      log_runtime_mesh("green_held_tail", held_tail_mesh_[0]);
      log_runtime_mesh("red_held_tail", held_tail_mesh_[1]);
      log_runtime_mesh("yellow_held_tail", held_tail_mesh_[2]);
      log_runtime_mesh("blue_held_tail", held_tail_mesh_[3]);
      log_runtime_mesh("orange_held_tail", held_tail_mesh_[4]);
      log_runtime_line_material("green_held_line", held_tail_line_material_[0]);
      log_runtime_line_material("red_held_line", held_tail_line_material_[1]);
      log_runtime_line_material("yellow_held_line", held_tail_line_material_[2]);
      log_runtime_line_material("blue_held_line", held_tail_line_material_[3]);
      log_runtime_line_material("orange_held_line", held_tail_line_material_[4]);
      log_runtime_mesh("held_tight_tail", held_tight_tail_mesh_);
      log_runtime_mesh("star_phrase_tail", star_phrase_tail_mesh_);
      log_runtime_mesh("star_held_tail", star_tail_mesh_);
      log_runtime_mesh("whammy_held_tail", whammy_tail_mesh_);
      log_runtime_line_material("whammy_tail", whammy_tail_line_material_);
      log_runtime_mesh("bonus_tail", bonus_tail_mesh_);
      log_runtime_mesh("miss", miss_mesh_);
      log_runtime_mesh("top_miss", miss_top_mesh_);
      log_runtime_mesh("star_miss", star_miss_mesh_);
      log_runtime_mesh("top_star_miss", star_miss_top_mesh_);
      log_runtime_mesh("gem_smasher", gem_smasher_mesh_);
      log_runtime_mesh("red_smasher_add", smasher_add_meshes_[1]);
      log_runtime_mesh("blue_smasher_add", smasher_add_meshes_[3]);
      log_runtime_mesh("bonus_smasher_add", bonus_smasher_add_mesh_);
      log_source_mesh("green_gem", "green_gem.mesh");
      log_source_mesh("red_gem", "red_gem.mesh");
      log_source_mesh("green_star", "green_star.mesh");
      log_source_mesh("red_star", "red_star.mesh");
      log_source_mesh("yellow_star", "yellow_star.mesh");
      log_source_mesh("star2", "star2.mesh");
      log_source_mesh("star_base", "star_base.mesh");
      log_source_mesh("top_star_black", "top_star_black.mesh");
      log_source_mesh("miss", "miss.mesh");
      log_source_mesh("top_miss", "top_miss.mesh");
      log_source_mesh("star_miss", "star_miss.mesh");
      log_source_mesh("top_star_miss", "top_star_miss.mesh");
    }
    std::fprintf(stderr,
                 "[highway] native miss meshes: regular=%d regular_top=%d star=%d star_top=%d\n",
                 miss_mesh_.ok ? 1 : 0,
                 miss_top_mesh_.ok ? 1 : 0,
                 star_miss_mesh_.ok ? 1 : 0,
                 star_miss_top_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native track meshes: surface=%d mask=%d rails=%d lines=%d spglow=%d smasher=%d hitflame=%d starcollect=%d miss=%d combo=%d\n",
                 track_surface_mesh_.ok ? 1 : 0,
                 track_mask_mesh_.ok ? 1 : 0,
                 track_side_rails_mesh_.ok ? 1 : 0,
                 track_lane_lines_mesh_.ok ? 1 : 0,
                 star_power_track_glow_mesh_.ok ? 1 : 0,
                 gem_smasher_mesh_.ok ? 1 : 0,
                 hit_flame_mesh_.ok ? 1 : 0,
                 star_collect_flame_mesh_.ok ? 1 : 0,
                 miss_mesh_.ok ? 1 : 0,
                 static_cast<int>(std::count_if(
                     combo_lightning_mesh_.begin(),
                     combo_lightning_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })));
    std::fprintf(stderr,
                 "[highway] side-rail MatAnim states: none=%d warning=%d star=%d warning_star=%d\n",
                 side_rails_none_.ok ? 1 : 0,
                 side_rails_warning_.ok ? 1 : 0,
                 side_rails_star_.ok ? 1 : 0,
                 side_rails_warning_star_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] surface flash MatAnim states: 2x=%d 3x=%d 4x=%d\n",
                 surface_flash_2x_.ok ? 1 : 0,
                 surface_flash_3x_.ok ? 1 : 0,
                 surface_flash_4x_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native smasher lane materials: base=%d add=%d add_mesh=%d ring=%d\n",
                 static_cast<int>(std::count_if(
                     smasher_texture_names_.begin(), smasher_texture_names_.end(),
                     [](const std::string& name) { return !name.empty(); })),
                 static_cast<int>(std::count_if(
                     smasher_add_texture_names_.begin(),
                     smasher_add_texture_names_.end(),
                     [](const std::string& name) { return !name.empty(); })),
                 static_cast<int>(std::count_if(
                     smasher_add_meshes_.begin(), smasher_add_meshes_.end(),
                     [](const RuntimeMesh& mesh) { return mesh.ok; })),
                 static_cast<int>(std::count_if(
                     smasher_rim_meshes_.begin(), smasher_rim_meshes_.end(),
                     [](const RuntimeMesh& mesh) { return mesh.ok; })));
    std::fprintf(stderr,
                 "[highway] native timing meshes: quarter=%d half=%d beat=%d bar=%d\n",
                 quarter_beat_line_mesh_.ok ? 1 : 0,
                 half_beat_line_mesh_.ok ? 1 : 0,
                 beat_line_mesh_.ok ? 1 : 0,
                 bar_line_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native sustain tail meshes: lanes=%d held=%d tight=%d burn=%d burn_particles=%zu star_phrase=%d star_held=%d star_whammy=%d\n",
                 static_cast<int>(std::count_if(
                     tail_mesh_.begin(), tail_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 static_cast<int>(std::count_if(
                     held_tail_mesh_.begin(), held_tail_mesh_.end(),
                     [](const RuntimeMesh& m) { return m.ok; })),
                 held_tight_tail_mesh_.ok ? 1 : 0,
                 burn_castlight_mesh_.ok ? 1 : 0,
                 burn_tail_particles_.size(),
                 star_phrase_tail_mesh_.ok ? 1 : 0,
                 star_tail_mesh_.ok ? 1 : 0,
                 whammy_tail_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native hit particles: normal=%zu bonus=%zu star=%zu combo=%zu\n",
                 smash_normal_particles_.size(), smash_bonus_particles_.size(),
                 smash_star_particles_.size(), smash_combo_particles_.size());
    std::fprintf(stderr,
                 "[highway] native bonus meshes: gem=%d overlay=%d tail=%d smasher=%d flame=%d\n",
                 bonus_gem_mesh_.ok ? 1 : 0,
                 bonus_gem_overlay_mesh_.ok ? 1 : 0,
                 bonus_tail_mesh_.ok ? 1 : 0,
                 !bonus_smasher_texture_name_.empty() ? 1 : 0,
                 bonus_hit_flame_mesh_.ok ? 1 : 0);
    std::fprintf(stderr,
                 "[highway] native gem sparkle meshes: star=%d bonus1=%d bonus2=%d\n",
                 gem_sparkle_mesh_.ok ? 1 : 0,
                 bonus_spark1_mesh_.ok ? 1 : 0,
                 bonus_spark2_mesh_.ok ? 1 : 0);
  }

  if (timing_preview) {
    // Soundcheck renders the stock GH2 board, timing lines, green gems, and
    // fret targets only. Decoding every gameplay effect texture made opening
    // the timing pass feel like loading a song. Keep the source meshes and
    // materials authoritative while requesting only what this view can draw.
    std::set<std::string> preview_texture_names;
    const auto add_mesh_texture = [&](const RuntimeMesh& mesh) {
      if (!mesh.texture_name.empty())
        preview_texture_names.insert(mesh.texture_name);
    };
    add_mesh_texture(track_surface_mesh_);
    add_mesh_texture(track_mask_mesh_);
    add_mesh_texture(track_side_rails_mesh_);
    add_mesh_texture(track_lane_lines_mesh_);
    add_mesh_texture(bar_line_mesh_);
    add_mesh_texture(beat_line_mesh_);
    add_mesh_texture(half_beat_line_mesh_);
    add_mesh_texture(quarter_beat_line_mesh_);
    add_mesh_texture(gem_mesh_[0]);
    add_mesh_texture(gem_top_mesh_);
    add_mesh_texture(pc_standard_top_mesh_);
    add_mesh_texture(gem_glow_mesh_);
    add_mesh_texture(gem_smasher_mesh_);
    add_mesh_texture(smasher_rim_mesh_);
    add_mesh_texture(smasher_shadow_mesh_);
    for (int lane = 0; lane < 5; ++lane) {
      add_mesh_texture(smasher_add_meshes_[lane]);
      add_mesh_texture(smasher_rim_meshes_[lane]);
      add_mesh_texture(smasher_ring_add_meshes_[lane]);
      if (!smasher_texture_names_[lane].empty())
        preview_texture_names.insert(smasher_texture_names_[lane]);
      if (!smasher_add_texture_names_[lane].empty())
        preview_texture_names.insert(smasher_add_texture_names_[lane]);
      if (!smasher_ring_texture_names_[lane].empty())
        preview_texture_names.insert(smasher_ring_texture_names_[lane]);
    }
    if (!smasher_normal_texture_name_.empty())
      preview_texture_names.insert(smasher_normal_texture_name_);
    preview_texture_names.insert("track_surface.tex");
    texture_names = std::move(preview_texture_names);
    std::fprintf(stderr,
                 "[highway] timing-preview texture subset: %zu\n",
                 texture_names.size());
  }

  const std::vector<std::string> names(texture_names.begin(),
                                       texture_names.end());
  auto imgs = ghogx::asset::load_milo_textures(hdr_path, ark_path,
                                               "track/gen/track.milo_ps2", names);
  if (imgs.empty()) { std::fprintf(stderr, "[highway] no track textures\n"); return false; }

  slot_lane_colors_ = kDefaultSlotLaneColors;
  for (int lane = 0; lane < 5; ++lane) {
    const auto texture_name =
        lane_texture_name("gem_", slot_color_names_[lane], ".tex");
    auto it = imgs.find(texture_name);
    if (it != imgs.end()) {
      slot_lane_colors_[lane] =
          sample_lane_color_from_gem(it->second, slot_lane_colors_[lane]);
    }
  }
  std::fprintf(
      stderr,
      "[highway] sampled lane colors: %08x/%08x/%08x/%08x/%08x\n",
      slot_lane_colors_[0], slot_lane_colors_[1], slot_lane_colors_[2],
      slot_lane_colors_[3], slot_lane_colors_[4]);

  auto upload_image = [&](const ghogx::asset::Image& img,
                          HighwayTextureAlphaMode alpha_mode)
      -> IDirect3DTexture9* {
    if (!img.valid()) return nullptr;
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
          const uint8_t r = src[x*4+0];
          const uint8_t g = src[x*4+1];
          const uint8_t b = src[x*4+2];
          const uint8_t a =
              texture_alpha_for_mode(alpha_mode, r, g, b, src[x*4+3]);
          dst[x*4+0] = b; dst[x*4+1] = g;
          dst[x*4+2] = r; dst[x*4+3] = a;
        }
      }
      t->UnlockRect(0);
    }
    return t;
  };

  for (auto& kv : imgs) {
    const bool alpha_key_black_card =
        is_note_black_card_tex_name(kv.first, slot_color_names_);
    const HighwayTextureAlphaMode alpha_mode =
        alpha_key_black_card ? HighwayTextureAlphaMode::BlackKey
                             : HighwayTextureAlphaMode::Raw;
    if (IDirect3DTexture9* t = upload_image(kv.second, alpha_mode)) {
      textures_[kv.first] = t;
    }
    if (kv.first == "gem.tex") {
      if (IDirect3DTexture9* t =
              upload_image(kv.second, HighwayTextureAlphaMode::Raw)) {
        textures_[kStarBlackTopTextureAlias] = t;
      }
    }
  }
  for (const auto& source_name : smasher_ring_add_alpha_key_sources) {
    auto it = imgs.find(source_name);
    if (it == imgs.end()) continue;
    const std::string alias = smasher_ring_add_alpha_key_alias(source_name);
    if (IDirect3DTexture9* t =
            upload_image(it->second, HighwayTextureAlphaMode::RgbMask)) {
      textures_[alias] = t;
    }
  }
  if (!smasher_ring_add_alpha_key_sources.empty()) {
    std::fprintf(stderr,
                 "[highway] smasher ring-add alpha-key textures: %zu\n",
                 smasher_ring_add_alpha_key_sources.size());
  }
  for (const auto& source_name : smasher_add_alpha_key_sources) {
    auto it = imgs.find(source_name);
    if (it == imgs.end()) continue;
    const std::string alias = smasher_add_alpha_key_alias(source_name);
    if (IDirect3DTexture9* t =
            upload_image(it->second, HighwayTextureAlphaMode::RgbMask)) {
      textures_[alias] = t;
    }
  }
  if (!smasher_add_alpha_key_sources.empty()) {
    std::fprintf(stderr,
                 "[highway] smasher add alpha-key textures: %zu\n",
                 smasher_add_alpha_key_sources.size());
  }

  std::string surface_path;
  const ghogx::asset::Image surface =
      ghogx::asset::load_track_surface_bitmap(
          hdr_path, ark_path, surface_ref, &surface_path);
  if (!surface_path.empty()) {
    if (IDirect3DTexture9* t =
            upload_image(surface, HighwayTextureAlphaMode::Raw)) {
      auto existing = textures_.find("track_surface.tex");
      if (existing != textures_.end() && existing->second) {
        existing->second->Release();
      }
      textures_["track_surface.tex"] = t;
      selected_surface_loaded_ = true;
      std::fprintf(stderr, "[highway] selected guitarist surface: %s\n",
                   surface_path.c_str());
    } else {
      std::fprintf(stderr,
                   "[highway] selected surface unavailable: %s; using track.milo default\n",
                   surface_path.c_str());
    }
  }
  loaded_ = !textures_.empty();
  loaded_surface_ref_ = loaded_ ? surface_ref : std::string{};
  std::fprintf(stderr, "[highway] %zu track textures -> D3D\n", textures_.size());
  return loaded_;
}

void HighwayRenderer::draw(double song_time, const ghogx::chart::Chart& chart,
                           int difficulty, uint32_t fret_held_mask,
                           const float hit_flash[5], float lookahead_sec,
                            const std::vector<uint8_t>* consumed_notes,
                           const std::vector<FoFiXSessionSustain>* active_sustains,
                           bool star_power_active,
                           bool whammy_active,
                           float whammy_axis,
                           const float star_collect_flash[5],
                           const float miss_flash[5],
                           const float star_miss_flash[5],
                           const uint8_t hit_phrase_state[5],
                           int combo_multiplier,
                           float bad_feedback_flash,
                           float rock_fill,
                           float star_power_flash,
                           float surface_flash,
                           bool track_intro_active,
                           double track_intro_elapsed) {
  draw_impl(song_time, chart, difficulty, fret_held_mask, hit_flash,
            lookahead_sec, true, consumed_notes, active_sustains,
            star_power_active, whammy_active, whammy_axis,
            star_collect_flash, miss_flash,
            star_miss_flash, hit_phrase_state, combo_multiplier,
            bad_feedback_flash, rock_fill, star_power_flash, surface_flash,
            track_intro_active, track_intro_elapsed);
}

void HighwayRenderer::draw_over_scene(double song_time,
                                      const ghogx::chart::Chart& chart,
                                      int difficulty,
                                      uint32_t fret_held_mask,
                                      const float hit_flash[5],
                                      float lookahead_sec,
                                      const std::vector<uint8_t>* consumed_notes,
                                       const std::vector<FoFiXSessionSustain>* active_sustains,
                                       bool star_power_active,
                                       bool whammy_active,
                                       float whammy_axis,
                                       const float star_collect_flash[5],
                                       const float miss_flash[5],
                                       const float star_miss_flash[5],
                                       const uint8_t hit_phrase_state[5],
                                       int combo_multiplier,
                                        float bad_feedback_flash,
                                        float rock_fill,
                                        float star_power_flash,
                                        float surface_flash,
                                        bool track_intro_active,
                                        double track_intro_elapsed) {
  draw_impl(song_time, chart, difficulty, fret_held_mask, hit_flash,
            lookahead_sec, false, consumed_notes, active_sustains,
            star_power_active, whammy_active, whammy_axis,
            star_collect_flash, miss_flash,
            star_miss_flash, hit_phrase_state, combo_multiplier,
            bad_feedback_flash, rock_fill, star_power_flash, surface_flash,
            track_intro_active, track_intro_elapsed);
}

void HighwayRenderer::draw_debug_note_counter_overlay(
    double song_time, const ghogx::chart::Chart& chart, int difficulty) const {
  if (!dev_ || !win_ || !env_enabled("GHOGX_DEBUG_HIGHWAY_NOTE_COUNTER")) return;
  const int difficulty_index =
      (difficulty >= 0 && difficulty < 4) ? difficulty : 0;
  const auto& notes = chart.notes[difficulty_index];

  struct NoteGroupDebug {
    uint32_t tick = 0;
    int min_lane = 0;
    int max_lane = 4;
    int gems = 0;
    const char* kind = "DONE";
    double eta = 0.0;
    float y = 0.0f;
    float tag_x = 0.0f;
    float tag_y = 0.0f;
    D3DCOLOR color = D3DCOLOR_ARGB(255, 220, 220, 220);
    bool valid = false;
    bool tag_visible = false;
    bool final_star = false;
  };
  NoteGroupDebug last;
  last.kind = "NONE";
  NoteGroupDebug next;
  uint32_t crossed_groups = 0;
  uint32_t crossed_standard = 0;
  uint32_t crossed_star = 0;
  uint32_t crossed_hopo = 0;
  const float speed = y_per_second_ * track_speed_[difficulty_index];
  auto classify_group = [&](size_t group_start, size_t group_end,
                            NoteGroupDebug& target) {
    bool group_star = false;
    bool group_final_star = false;
    for (size_t i = group_start; i < group_end; ++i) {
      group_star = group_star || notes[i].star_power;
      group_final_star = group_final_star || notes[i].final_star;
    }
    const int group_gems =
        static_cast<int>(std::max<size_t>(1, group_end - group_start));
    const int group_hopo_tappable =
        group_gems == 1 ? notes[group_start].hopo_tappable : 0;
    const bool group_hopo =
        group_gems == 1 &&
        (notes[group_start].is_hopo || group_hopo_tappable >= 2);
    target.tick = notes[group_start].tick_on;
    target.gems = group_gems;
    target.valid = true;
    target.final_star = group_final_star;
    target.min_lane = 4;
    target.max_lane = 0;
    for (size_t i = group_start; i < group_end; ++i) {
      const int lane = std::clamp(notes[i].lane, 0, 4);
      target.min_lane = std::min(target.min_lane, lane);
      target.max_lane = std::max(target.max_lane, lane);
    }
    if (group_star) {
      target.kind = "STAR";
      target.color = D3DCOLOR_ARGB(255, 255, 220, 45);
    } else if (group_hopo) {
      target.kind = "HOPO";
      target.color = D3DCOLOR_ARGB(255, 70, 220, 255);
    } else {
      target.kind = "STANDARD";
      target.color = D3DCOLOR_ARGB(255, 245, 245, 245);
    }
  };
  auto project_track_point = [&](float wx, float wy, float wz, float& sx,
                                 float& sy) {
    if (!win_ || win_->bb_width() <= 0 || win_->bb_height() <= 0) return false;
    const float aspect = static_cast<float>(win_->bb_width()) /
                         static_cast<float>(win_->bb_height());
    const HighwayAspectProfile profile =
        pcsx2_measured_highway_profile_for_aspect(aspect);
    Mat4 view = highway_camera_view(profile);
    Mat4 proj = Mat4::perspective_lh(kCamFov, aspect, cam_near_, cam_far_);
    proj.m[0][0] = -proj.m[0][0];
    const Mat4 view_proj = view * proj;
    const float cx = wx * view_proj.m[0][0] + wy * view_proj.m[1][0] +
                     wz * view_proj.m[2][0] + view_proj.m[3][0];
    const float cy = wx * view_proj.m[0][1] + wy * view_proj.m[1][1] +
                     wz * view_proj.m[2][1] + view_proj.m[3][1];
    const float cw = wx * view_proj.m[0][3] + wy * view_proj.m[1][3] +
                     wz * view_proj.m[2][3] + view_proj.m[3][3];
    if (!std::isfinite(cw) || cw <= 0.001f) return false;
    const float ndc_x = cx / cw;
    const float ndc_y = cy / cw;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) return false;
    sx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(win_->bb_width());
    sy = (0.5f - ndc_y * 0.5f) * static_cast<float>(win_->bb_height());
    return sx >= -64.0f && sx <= static_cast<float>(win_->bb_width()) + 64.0f &&
           sy >= -64.0f && sy <= static_cast<float>(win_->bb_height()) + 64.0f;
  };
  for (size_t note_index = 0; note_index < notes.size();) {
    const uint32_t group_tick = notes[note_index].tick_on;
    size_t group_end = note_index + 1;
    while (group_end < notes.size() &&
           notes[group_end].tick_on == group_tick) {
      ++group_end;
    }
    const double on = chart.tick_to_sec(group_tick);
    const float group_y =
        kStrikeY + static_cast<float>(on - song_time) * speed;
    if (group_y <= kStrikeY) {
      ++crossed_groups;
      classify_group(note_index, group_end, last);
      if (std::strcmp(last.kind, "STAR") == 0) {
        ++crossed_star;
      } else if (std::strcmp(last.kind, "HOPO") == 0) {
        ++crossed_hopo;
      } else {
        ++crossed_standard;
      }
      last.eta = song_time - on;
      note_index = group_end;
      continue;
    }

    classify_group(note_index, group_end, next);
    next.eta = on - song_time;
    next.y = group_y;
    if (next.y >= kStrikeY && next.y <= top_y_) {
      const float center_lane =
          (static_cast<float>(next.min_lane) +
           static_cast<float>(next.max_lane)) * 0.5f;
      const float tag_world_x =
          (center_lane - 2.0f) * lane_spacing_;
      next.tag_visible = project_track_point(
          tag_world_x, next.y, kGemZ + 3.0f, next.tag_x, next.tag_y);
    }
    break;
  }

  char count_line[64];
  char type_line[96];
  char last_line[96];
  char next_line[96];
  char detail_line[96];
  std::snprintf(count_line, sizeof(count_line), "COUNT %u", crossed_groups);
  std::snprintf(type_line, sizeof(type_line), "STD %u STAR %u HOPO %u",
                crossed_standard, crossed_star, crossed_hopo);
  auto format_group_line = [](char* dst, size_t dst_size, const char* prefix,
                              const NoteGroupDebug& group) {
    if (!group.valid) {
      std::snprintf(dst, dst_size, "%s %s", prefix, group.kind);
      return;
    }
    if (group.min_lane == group.max_lane) {
      std::snprintf(dst, dst_size, "%s %s%s T%u G%d L%d", prefix, group.kind,
                    group.final_star ? " END" : "", group.tick, group.gems,
                    group.min_lane);
      return;
    }
    std::snprintf(dst, dst_size, "%s %s%s T%u G%d L%d-%d", prefix,
                  group.kind, group.final_star ? " END" : "", group.tick,
                  group.gems, group.min_lane, group.max_lane);
  };
  format_group_line(last_line, sizeof(last_line), "LAST", last);
  format_group_line(next_line, sizeof(next_line), "NEXT", next);
  if (next.valid) {
    std::snprintf(detail_line, sizeof(detail_line), "ETA %.2f Y %.1f",
                  next.eta, next.y);
  } else {
    std::snprintf(detail_line, sizeof(detail_line), "ETA 0.00 Y 0.0");
  }

  static double next_counter_log_time = -1.0;
  if (next_counter_log_time < 0.0 ||
      song_time + 1.0e-6 >= next_counter_log_time ||
      song_time < next_counter_log_time - 2.0) {
    auto lane_min = [](const NoteGroupDebug& group) {
      return group.valid ? group.min_lane : -1;
    };
    auto lane_max = [](const NoteGroupDebug& group) {
      return group.valid ? group.max_lane : -1;
    };
    std::fprintf(
        stderr,
        "[highway-note-counter] t=%.3f count=%u standard=%u star=%u "
        "hopo=%u last_kind=%s last_tick=%u last_gems=%d last_lanes=%d-%d "
        "last_final=%d last_age=%.3f next_kind=%s next_tick=%u "
        "next_gems=%d next_lanes=%d-%d next_final=%d next_eta=%.3f "
        "next_y=%.3f next_tag=%d\n",
        song_time, crossed_groups, crossed_standard, crossed_star,
        crossed_hopo, last.kind, last.tick, last.gems, lane_min(last),
        lane_max(last), last.final_star ? 1 : 0, last.eta, next.kind,
        next.tick, next.gems, lane_min(next), lane_max(next),
        next.final_star ? 1 : 0, next.eta, next.y,
        next.tag_visible ? 1 : 0);
    next_counter_log_time = song_time + 0.5;
  }

  const float scale = std::max(2.0f, std::min(4.0f,
      static_cast<float>(win_->bb_width()) / 420.0f));
  const float pad = 8.0f * scale;
  const float x = 14.0f;
  const float y = 38.0f;
  const float line_h = 9.0f * scale;
  const float upper_w =
      std::max(std::max(debug_text_width(count_line, scale),
                        debug_text_width(type_line, scale)),
               std::max(debug_text_width(last_line, scale),
                        debug_text_width(next_line, scale)));
  const float lower_w =
      debug_text_width(detail_line, scale);
  const float panel_w = std::max(upper_w, lower_w) + pad * 2.0f;
  const float panel_h = line_h * 5.0f + pad * 1.4f;

  DWORD prev_fvf = 0;
  DWORD prev_z = FALSE;
  DWORD prev_blend = FALSE;
  DWORD prev_src = D3DBLEND_SRCALPHA;
  DWORD prev_dest = D3DBLEND_INVSRCALPHA;
  DWORD prev_alpha_test = FALSE;
  DWORD prev_color_op = D3DTOP_MODULATE;
  DWORD prev_alpha_op = D3DTOP_MODULATE;
  IDirect3DBaseTexture9* prev_texture = nullptr;
  dev_->GetFVF(&prev_fvf);
  dev_->GetRenderState(D3DRS_ZENABLE, &prev_z);
  dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prev_blend);
  dev_->GetRenderState(D3DRS_SRCBLEND, &prev_src);
  dev_->GetRenderState(D3DRS_DESTBLEND, &prev_dest);
  dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prev_alpha_test);
  dev_->GetTextureStageState(0, D3DTSS_COLOROP, &prev_color_op);
  dev_->GetTextureStageState(0, D3DTSS_ALPHAOP, &prev_alpha_op);
  dev_->GetTexture(0, &prev_texture);

  dev_->SetFVF(kDebugFvf);
  dev_->SetTexture(0, nullptr);
  dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
  dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
  dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

  std::vector<V2> verts;
  verts.reserve(2048);
  append_debug_rect(verts, x, y, panel_w, panel_h,
                    D3DCOLOR_ARGB(185, 0, 0, 0));
  append_debug_rect(verts, x + pad * 0.5f, y + pad * 0.5f,
                    panel_w - pad, 3.0f, next.color);
  append_debug_text(verts, count_line, x + pad, y + pad,
                    scale, D3DCOLOR_ARGB(255, 255, 255, 255));
  append_debug_text(verts, type_line, x + pad, y + pad + line_h,
                    scale, D3DCOLOR_ARGB(255, 210, 210, 210));
  append_debug_text(verts, last_line, x + pad, y + pad + line_h * 2.0f,
                    scale, last.valid ? last.color
                                      : D3DCOLOR_ARGB(255, 180, 180, 180));
  append_debug_text(verts, next_line, x + pad, y + pad + line_h * 3.0f,
                    scale, next.color);
  append_debug_text(verts, detail_line, x + pad, y + pad + line_h * 4.0f,
                    scale, D3DCOLOR_ARGB(255, 210, 210, 210));
  if (next.tag_visible) {
    char tag_line[96];
    format_group_line(tag_line, sizeof(tag_line), "NEXT", next);
    const float tag_scale = std::max(2.0f, std::min(3.0f, scale * 0.82f));
    const float tag_pad = 5.0f * tag_scale;
    const float tag_w = debug_text_width(tag_line, tag_scale) +
                        tag_pad * 2.0f;
    const float tag_h = 8.0f * tag_scale + tag_pad * 1.7f;
    const float marker_h = 22.0f * tag_scale;
    const float tag_x = std::clamp(
        next.tag_x - tag_w * 0.5f, 6.0f,
        std::max(6.0f, static_cast<float>(win_->bb_width()) - tag_w - 6.0f));
    const float tag_y = std::clamp(
        next.tag_y - tag_h - marker_h, 6.0f,
        std::max(6.0f, static_cast<float>(win_->bb_height()) - tag_h - 6.0f));
    append_debug_rect(verts, tag_x, tag_y, tag_w, tag_h,
                      D3DCOLOR_ARGB(205, 0, 0, 0));
    append_debug_rect(verts, tag_x, tag_y, tag_w, 3.0f * tag_scale,
                      next.color);
    append_debug_text(verts, tag_line, tag_x + tag_pad,
                      tag_y + tag_pad * 0.85f, tag_scale,
                      D3DCOLOR_ARGB(255, 255, 255, 255));
    const float marker_x = std::clamp(
        next.tag_x - tag_scale, 2.0f,
        static_cast<float>(win_->bb_width()) - tag_scale * 2.0f);
    const float marker_y0 = tag_y + tag_h;
    const float marker_y1 = std::min(next.tag_y - 4.0f,
                                     marker_y0 + marker_h);
    if (marker_y1 > marker_y0) {
      append_debug_rect(verts, marker_x, marker_y0, tag_scale * 2.0f,
                        marker_y1 - marker_y0, next.color);
    }
  }
  if (!verts.empty()) {
    dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                          static_cast<UINT>(verts.size() / 3),
                          verts.data(), sizeof(V2));
  }

  dev_->SetTexture(0, prev_texture);
  if (prev_texture) prev_texture->Release();
  dev_->SetTextureStageState(0, D3DTSS_COLOROP, prev_color_op);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, prev_alpha_op);
  dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prev_alpha_test);
  dev_->SetRenderState(D3DRS_DESTBLEND, prev_dest);
  dev_->SetRenderState(D3DRS_SRCBLEND, prev_src);
  dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prev_blend);
  dev_->SetRenderState(D3DRS_ZENABLE, prev_z);
  dev_->SetFVF(prev_fvf);
}

void HighwayRenderer::draw_impl(double song_time,
                                const ghogx::chart::Chart& chart,
                                int difficulty,
                                uint32_t fret_held_mask,
                                const float hit_flash[5],
                                float lookahead_sec,
                                bool clear_target,
                                const std::vector<uint8_t>* consumed_notes,
                                 const std::vector<FoFiXSessionSustain>* active_sustains,
                                 bool star_power_active,
                                 bool whammy_active,
                                 float whammy_axis,
                                 const float star_collect_flash[5],
                                 const float miss_flash[5],
                                 const float star_miss_flash[5],
                                 const uint8_t hit_phrase_state[5],
                                 int combo_multiplier,
                                 float bad_feedback_flash,
                                 float rock_fill,
                                 float star_power_flash,
                                 float surface_flash,
                                 bool track_intro_active,
                                 double authored_track_intro_elapsed) {
  if (!dev_) return;
  if (clear_target) {
    dev_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                D3DCOLOR_XRGB(0,0,0), 1.0f, 0);
  } else {
    dev_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0,0,0), 1.0f,
                0);
  }

  D3DVIEWPORT9 previous_viewport = {};
  const bool have_previous_viewport = SUCCEEDED(dev_->GetViewport(&previous_viewport));
  if (have_previous_viewport) {
    D3DVIEWPORT9 track_viewport = previous_viewport;
    track_viewport.MinZ = cam_z_start_;
    track_viewport.MaxZ = cam_z_end_;
    dev_->SetViewport(&track_viewport);
  }

  dev_->BeginScene();

  // --- Camera: the exact track.cam transform ---
  const float aspect = win_->bb_height() > 0
      ? static_cast<float>(win_->bb_width()) / static_cast<float>(win_->bb_height())
      : 16.0f / 9.0f;
  HighwayAspectProfile highway_profile =
      pcsx2_measured_highway_profile_for_aspect(aspect);
  const float track_space_y_scale =
      track_surface_mesh_.ok
          ? native_track_y_scale(track_surface_mesh_.min_y,
                                 track_surface_mesh_.max_y, top_y_)
          : 1.0f;
  const double track_intro_elapsed =
      track_intro_active ? std::max(0.0, authored_track_intro_elapsed)
                         : kTrackIntroFirstSmasherSeconds +
                               kTrackIntroSmasherStepSeconds * 5.0;
  const float track_intro_frame = static_cast<float>(
      std::clamp(track_intro_elapsed / kTrackIntroExtendSeconds, 0.0, 1.0) *
      kTrackIntroEndFrame);
  if (track_intro_active &&
      !track_extend_anim_.translation_keys.empty() &&
      track_intro_frame < kTrackIntroEndFrame) {
    const auto source_now = sample_vec3_anim_clamped(
        track_extend_anim_.translation_keys, track_intro_frame,
        {highway_profile.cam_x, highway_profile.cam_y, highway_profile.cam_z});
    const auto source_out = sample_vec3_anim_clamped(
        track_extend_anim_.translation_keys, kTrackIntroEndFrame, source_now);
    // The animation targets track.cam. Apply its authored in-to-out delta to
    // the already validated PC/4:3 camera endpoint instead of replacing the
    // endpoint corrections. The static-fidelity path expands the source track
    // mesh in Y to the PCSX2-measured horizon, so the camera's source-space Y
    // delta must cross that same coordinate-space boundary. Leaving the camera
    // delta unscaled puts a large section of the expanded highway in front of
    // the near plane at frame zero, making it look pre-assembled.
    highway_profile.cam_x += source_now[0] - source_out[0];
    highway_profile.cam_y +=
        (source_now[1] - source_out[1]) * track_space_y_scale;
    highway_profile.cam_z += source_now[2] - source_out[2];
    static bool track_intro_mapping_reported = false;
    if (!track_intro_mapping_reported) {
      std::fprintf(
          stderr,
          "[highway] track intro coordinate map: source_y=%.3f..%.3f native_track_y=%.3f..%.3f y_scale=%.6f frame=%.3f camera_y=%.3f\n",
          track_surface_mesh_.min_y, track_surface_mesh_.max_y,
          track_surface_mesh_.min_y, top_y_, track_space_y_scale,
          track_intro_frame, highway_profile.cam_y);
      track_intro_mapping_reported = true;
    }
  }
  Mat4 view = highway_camera_view(highway_profile);
  Mat4 proj = Mat4::perspective_lh(kCamFov, aspect, cam_near_, cam_far_);
  // GH2 track space is right-handed (X right, +Y forward/into-screen, Z up);
  // our D3D pipeline is left-handed. Mirror clip-X so lane order matches the
  // original (Green leftmost, Orange rightmost) instead of mirrored.
  proj.m[0][0] = -proj.m[0][0];
  D3DMATRIX dv, dp, id; Mat4 ident = Mat4::identity();
  std::memcpy(&dv, &view, 64); std::memcpy(&dp, &proj, 64); std::memcpy(&id, &ident, 64);
  dev_->SetTransform(D3DTS_VIEW, &dv);
  dev_->SetTransform(D3DTS_PROJECTION, &dp);
  dev_->SetTransform(D3DTS_WORLD, &id);

  // Render states: alpha-blended overlay by default. Moving 3D note meshes
  // temporarily enable depth so their authored faces/layers occlude correctly.
  dev_->SetFVF(kFVF);
  configure_source_lighting();
  dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
  dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
  dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
  dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

  const int difficulty_index = (difficulty >= 0 && difficulty < 4) ? difficulty : 0;
  const float speed = y_per_second_ * track_speed_[difficulty_index];
  const HighwayRootSpace highway_root{
      highway_profile.track_x_scale,
      lane_spacing_ * highway_profile.track_x_scale,
      board_half_x_ * highway_profile.track_x_scale};
  auto lane_x = [&](int lane) {
    return highway_root.lane_x(lane);
  };
  auto depth_fade = [&](float y) {
    return depth_fade_for(y, top_y_, alpha_dist_);
  };
  auto note_y = [&](double t) {
    return kStrikeY + static_cast<float>(t - song_time) * speed;
  };
  D3DVIEWPORT9 draw_viewport = previous_viewport;
  if (!have_previous_viewport) {
    draw_viewport.X = 0;
    draw_viewport.Y = 0;
    draw_viewport.Width = static_cast<DWORD>(std::max(1, win_->bb_width()));
    draw_viewport.Height = static_cast<DWORD>(std::max(1, win_->bb_height()));
    draw_viewport.MinZ = 0.0f;
    draw_viewport.MaxZ = 1.0f;
  }
  const Mat4 view_proj = view * proj;
  auto project_screen = [&](float x, float y,
                            float z) -> std::optional<std::array<float, 2>> {
    const float cx = x * view_proj.m[0][0] + y * view_proj.m[1][0] +
                     z * view_proj.m[2][0] + view_proj.m[3][0];
    const float cy = x * view_proj.m[0][1] + y * view_proj.m[1][1] +
                     z * view_proj.m[2][1] + view_proj.m[3][1];
    const float cw = x * view_proj.m[0][3] + y * view_proj.m[1][3] +
                     z * view_proj.m[2][3] + view_proj.m[3][3];
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) ||
        std::abs(cw) <= 0.00001f) {
      return std::nullopt;
    }
    const float ndc_x = cx / cw;
    const float ndc_y = cy / cw;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) {
      return std::nullopt;
    }
    return std::array<float, 2>{
        static_cast<float>(draw_viewport.X) +
            (ndc_x * 0.5f + 0.5f) * static_cast<float>(draw_viewport.Width),
        static_cast<float>(draw_viewport.Y) +
            (-ndc_y * 0.5f + 0.5f) * static_cast<float>(draw_viewport.Height)};
  };
  auto project_screen_x = [&](float x, float y,
                              float z) -> std::optional<float> {
    const auto p = project_screen(x, y, z);
    if (!p) return std::nullopt;
    return (*p)[0];
  };
  auto local_tail_half_for_screen_width = [&](int lane, float y,
                                              float width_px_720,
                                              float fallback_half) {
    const float viewport_height = static_cast<float>(
        std::max<DWORD>(1, draw_viewport.Height));
    const float target_width_px = width_px_720 * (viewport_height / 720.0f);
    const float cx = lane_x(lane);
    const auto sx0 = project_screen_x(cx, y, kGemZ - 0.015f);
    const auto sx1 =
        project_screen_x(cx + highway_root.scale_x(1.0f), y, kGemZ - 0.015f);
    if (!sx0 || !sx1) return fallback_half;
    const float px_per_local_x = std::abs(*sx1 - *sx0);
    if (!std::isfinite(px_per_local_x) || px_per_local_x <= 0.001f) {
      return fallback_half;
    }
    const float half = (target_width_px * 0.5f) / px_per_local_x;
    return std::clamp(half, 0.05f, std::max(6.0f, fallback_half * 4.0f));
  };
  auto local_tail_x_offset_for_screen_px = [&](int lane, float y,
                                               float offset_px_720) {
    if (std::abs(offset_px_720) <= 0.0001f) return 0.0f;
    const float viewport_height = static_cast<float>(
        std::max<DWORD>(1, draw_viewport.Height));
    const float target_offset_px = offset_px_720 * (viewport_height / 720.0f);
    const float cx = lane_x(lane);
    const auto sx0 = project_screen_x(cx, y, kGemZ - 0.015f);
    const auto sx1 =
        project_screen_x(cx + highway_root.scale_x(1.0f), y, kGemZ - 0.015f);
    if (!sx0 || !sx1) return 0.0f;
    const float px_per_local_x = std::abs(*sx1 - *sx0);
    if (!std::isfinite(px_per_local_x) || px_per_local_x <= 0.001f) {
      return 0.0f;
    }
    return std::clamp(target_offset_px / px_per_local_x, -6.0f, 6.0f);
  };
  const bool debug_alignment =
      env_enabled("GHOGX_DEBUG_HIGHWAY_ALIGNMENT");
  if (debug_alignment) {
    static int idle_budget = 0;
    static int hit_budget = 0;
    bool any_hit = false;
    for (int lane = 0; lane < 5; ++lane) {
      const float hit =
          hit_flash ? std::clamp(hit_flash[lane], 0.0f, 1.0f) : 0.0f;
      if (hit > 0.01f) any_hit = true;
    }
    const bool log_frame =
        any_hit ? (hit_budget < 24) : (idle_budget < 3);
    if (log_frame) {
      const float aspect_log = win_->bb_height() > 0
          ? static_cast<float>(win_->bb_width()) /
                static_cast<float>(win_->bb_height())
          : 0.0f;
      for (int lane = 0; lane < 5; ++lane) {
        const float x = lane_x(lane);
        const float hit =
            hit_flash ? std::clamp(hit_flash[lane], 0.0f, 1.0f) : 0.0f;
        const bool held = (fret_held_mask >> lane) & 1;
        const auto strike = project_screen(x, kStrikeY, kGemZ);
        const auto smasher = project_screen(x, kStrikeY, kSmasherFixedRingTopZ);
        const auto far_point = project_screen(x, top_y_, kBoardZ);
        std::fprintf(
            stderr,
            "[highway-align] lane=%d aspect=%.6f root_x=%.3f hit=%.3f "
            "held=%d strike_px=(%.2f,%.2f) smasher_px=(%.2f,%.2f) "
            "dx=%.2f far_px=(%.2f,%.2f) root_shared=1\n",
            lane, aspect_log, x, hit, held ? 1 : 0,
            strike ? (*strike)[0] : -1.0f, strike ? (*strike)[1] : -1.0f,
            smasher ? (*smasher)[0] : -1.0f,
            smasher ? (*smasher)[1] : -1.0f,
            (strike && smasher) ? ((*strike)[0] - (*smasher)[0]) : 0.0f,
            far_point ? (*far_point)[0] : -1.0f,
            far_point ? (*far_point)[1] : -1.0f);
      }
      if (any_hit) {
        ++hit_budget;
      } else {
        ++idle_budget;
      }
    }
  }
  auto root_transform = [&](MeshTransformSample transform) {
    if (!transform.has_scale) {
      transform.has_scale = true;
      transform.scale = {1.0f, 1.0f, 1.0f};
    }
    transform.scale[0] = highway_root.scale_x(transform.scale[0]);
    if (transform.has_translation) {
      transform.translation[0] = highway_root.scale_x(transform.translation[0]);
    }
    return transform;
  };
  auto draw_authored_root_mesh =
      [&](const RuntimeMesh& mesh, float origin_x, float origin_y,
          uint32_t tint, float scale, bool use_texture_alpha,
          float z_offset, bool clip_to_z_min, float z_min,
          bool use_vertex_color) {
        draw_authored_runtime_mesh_scaled(
            mesh, origin_x, origin_y, tint, highway_root.scale_x(scale),
            scale, scale, use_texture_alpha, z_offset, clip_to_z_min, z_min,
            use_vertex_color);
      };
  auto draw_centered_root_mesh =
      [&](const RuntimeMesh& mesh, float cx, float cy, uint32_t tint,
          float scale, bool use_texture_alpha, float z_offset,
          bool clip_to_z_min, float z_min) {
        draw_centered_runtime_mesh_scaled(
            mesh, cx, cy, tint, highway_root.scale_x(scale), scale, scale,
            use_texture_alpha, z_offset, clip_to_z_min, z_min);
      };
  auto draw_centered_root_mesh_with_texture =
      [&](const RuntimeMesh& mesh, const std::string& texture_name, float cx,
          float cy, uint32_t tint, float scale, bool use_texture_alpha,
          float z_offset = 0.0f, bool clip_to_z_min = false,
          float z_min = 0.0f) {
        const float scale_x = highway_root.scale_x(scale);
        draw_runtime_mesh_scaled_with_texture(
            mesh, texture_name, cx - mesh.center_x * scale_x,
            cy - mesh.center_y * scale, tint, scale_x, scale, scale,
            use_texture_alpha, 0.0f, 0.0f, true, z_offset, clip_to_z_min,
            z_min);
      };
  // --- 1) Board surface / rails / lane lines ---
  const ColorAnimState* surface_flash_curve = nullptr;
  const int surface_flash_mult =
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_4X") ? 4 :
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_3X") ? 3 :
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_2X") ? 2 :
      std::clamp(combo_multiplier, 1, 4);
  if (surface_flash_mult >= 4) {
    surface_flash_curve = &surface_flash_4x_;
  } else if (surface_flash_mult == 3) {
    surface_flash_curve = &surface_flash_3x_;
  } else if (surface_flash_mult == 2) {
    surface_flash_curve = &surface_flash_2x_;
  }
  const bool surface_flash_forced =
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_2X") ||
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_3X") ||
      env_enabled("GHOGX_FORCE_HIGHWAY_SURFACE_FLASH_4X");
  const float surface_flash_strength =
      surface_flash_forced ? 1.0f : std::clamp(surface_flash, 0.0f, 1.0f);
  SideRailColorState surface_flash_color;
  float surface_flash_frame = 0.0f;
  if (surface_flash_curve && surface_flash_curve->ok &&
      surface_flash_strength > 0.0f) {
    surface_flash_frame =
        surface_flash_forced
            ? color_anim_peak_dark_frame(*surface_flash_curve)
            : (1.0f - surface_flash_strength) *
                  color_anim_last_frame(*surface_flash_curve);
    surface_flash_color =
        sample_color_anim(*surface_flash_curve, surface_flash_frame);
  }
  static int surface_flash_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_SURFACE_FLASH") &&
      surface_flash_strength > 0.0f && surface_flash_debug_budget < 96) {
    std::fprintf(
        stderr,
        "[highway-surface-flash] t=%.3f mult=%d strength=%.3f forced=%d "
        "curve=%d keys=%zu frame=%.3f rgb=%.3f,%.3f,%.3f color=%d\n",
        song_time, surface_flash_mult, surface_flash_strength,
        surface_flash_forced ? 1 : 0,
        (surface_flash_curve && surface_flash_curve->ok) ? 1 : 0,
        surface_flash_curve ? surface_flash_curve->keys.size() : 0u,
        surface_flash_frame, surface_flash_color.r, surface_flash_color.g,
        surface_flash_color.b, surface_flash_color.ok ? 1 : 0);
    ++surface_flash_debug_budget;
  }
  const D3DCOLOR track_surface_tint = multiply_rgb(
      D3DCOLOR_ARGB(255, 255, 255, 255), surface_flash_color,
      1.0f);
  const float source_fade_top_y = std::max(kStrikeY + 1.0f, top_y_);
  const float source_fade_alpha_dist =
      std::max(alpha_dist_, horizon_tail_clip_ * 4.0f);
  struct TrackHorizonFit {
    float origin_y = 0.0f;
    float scale_y = 1.0f;
    float min_world_y = 0.0f;
    float max_world_y = 0.0f;
    bool active = false;
  };
  auto track_horizon_fit_for = [&](const RuntimeMesh& mesh,
                                   bool fit_to_horizon) {
    TrackHorizonFit fit;
    fit.min_world_y = mesh.min_y;
    fit.max_world_y = mesh.max_y;
    fit.scale_y = fit_to_horizon && mesh.ok
                      ? native_track_y_scale(mesh.min_y, mesh.max_y,
                                             source_fade_top_y)
                      : 1.0f;
    if (fit.scale_y > 1.0f + 0.000001f) {
      fit.origin_y = mesh.min_y - mesh.min_y * fit.scale_y;
      fit.min_world_y = fit.origin_y + mesh.min_y * fit.scale_y;
      fit.max_world_y = fit.origin_y + mesh.max_y * fit.scale_y;
      fit.active = true;
    }
    return fit;
  };
  const TrackHorizonFit track_surface_horizon_fit =
      track_horizon_fit_for(track_surface_mesh_, true);
  const TrackHorizonFit track_mask_horizon_fit =
      track_horizon_fit_for(track_mask_mesh_, false);
  const TrackHorizonFit track_side_rails_horizon_fit =
      track_horizon_fit_for(track_side_rails_mesh_, true);
  const TrackHorizonFit track_lane_lines_horizon_fit =
      track_horizon_fit_for(track_lane_lines_mesh_, true);
  const TrackHorizonFit star_power_track_glow_horizon_fit =
      track_horizon_fit_for(star_power_track_glow_mesh_, true);
  auto faded_tail_color = [&](D3DCOLOR color, float y) {
    const int a = static_cast<int>(((color >> 24) & 0xff) *
                                   depth_fade_for(y, source_fade_top_y,
                                                  source_fade_alpha_dist));
    return D3DCOLOR_ARGB(std::clamp(a, 0, 255), (color >> 16) & 0xff,
                         (color >> 8) & 0xff, color & 0xff);
  };
  static int fade_profile_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_FADE_PROFILE") &&
      fade_profile_debug_budget < 6) {
    const float fade_start_y = source_fade_top_y - source_fade_alpha_dist;
    const float aspect_log = win_->bb_height() > 0
        ? static_cast<float>(win_->bb_width()) /
              static_cast<float>(win_->bb_height())
        : 0.0f;
    auto log_width_sample = [&](const char* label, float left_x, float right_x,
                                float y, float z) {
      const float center_x = (left_x + right_x) * 0.5f;
      const auto left = project_screen(left_x, y, z);
      const auto center = project_screen(center_x, y, z);
      const auto right = project_screen(right_x, y, z);
      const float width =
          (left && right) ? ((*right)[0] - (*left)[0]) : 0.0f;
      std::fprintf(
          stderr,
          "[highway-fade-profile] sample=%s y=%.3f z=%.3f fade=%.3f "
          "left_px=(%.2f,%.2f) center_px=(%.2f,%.2f) "
          "right_px=(%.2f,%.2f) width_px=%.2f\n",
          label, y, z, depth_fade_for(y, source_fade_top_y,
                                      source_fade_alpha_dist),
          left ? (*left)[0] : -1.0f, left ? (*left)[1] : -1.0f,
          center ? (*center)[0] : -1.0f, center ? (*center)[1] : -1.0f,
          right ? (*right)[0] : -1.0f, right ? (*right)[1] : -1.0f,
          width);
    };
    auto log_mesh_profile = [&](const char* label, const RuntimeMesh& mesh,
                                const TrackHorizonFit& fit) {
      std::fprintf(
          stderr,
          "[highway-fade-profile] mesh=%s ok=%d verts=%zu tris=%zu "
          "tex=%s x=%.3f..%.3f y=%.3f..%.3f z=%.3f..%.3f "
          "world_y=%.3f..%.3f y_origin=%.3f y_scale=%.6f fit=%d "
          "fade_min=%.3f fade_max=%.3f\n",
          label, mesh.ok ? 1 : 0, mesh.verts.size(), mesh.indices.size() / 3,
          mesh.texture_name.c_str(), highway_root.scale_x(mesh.min_x),
          highway_root.scale_x(mesh.max_x), mesh.min_y, mesh.max_y,
          mesh.min_z, mesh.max_z, fit.min_world_y, fit.max_world_y,
          fit.origin_y, fit.scale_y, fit.active ? 1 : 0,
          depth_fade_for(fit.min_world_y, source_fade_top_y,
                         source_fade_alpha_dist),
          depth_fade_for(fit.max_world_y, source_fade_top_y,
                         source_fade_alpha_dist));
      if (mesh.ok) {
        log_width_sample(label, highway_root.scale_x(mesh.min_x),
                         highway_root.scale_x(mesh.max_x), fit.min_world_y,
                         mesh.min_z);
        log_width_sample(label, highway_root.scale_x(mesh.min_x),
                         highway_root.scale_x(mesh.max_x), fit.max_world_y,
                         mesh.min_z);
      }
    };
    std::fprintf(
        stderr,
        "[highway-fade-profile] frame=%d aspect=%.6f viewport=%ux%u "
        "root_x_scale=%.6f cam_x=%.6f cam_yaw_deg=%.6f "
        "fade_top_y=%.3f fade_alpha_dist=%.3f fade_start_y=%.3f "
        "remove_y=%.3f horizon_tail_clip=%.3f nowbar_tail_clip=%.3f\n",
        fade_profile_debug_budget, aspect_log, draw_viewport.Width,
        draw_viewport.Height, highway_profile.track_x_scale,
        highway_profile.cam_x, highway_profile.cam_yaw_deg,
        source_fade_top_y, source_fade_alpha_dist, fade_start_y, remove_y_,
        horizon_tail_clip_, nowbar_tail_clip_);
    log_width_sample("board_remove", -highway_root.board_half_x,
                     highway_root.board_half_x, remove_y_, kBoardZ);
    log_width_sample("board_strike", -highway_root.board_half_x,
                     highway_root.board_half_x, kStrikeY, kBoardZ);
    log_width_sample("board_fade_start", -highway_root.board_half_x,
                     highway_root.board_half_x, fade_start_y, kBoardZ);
    log_width_sample("board_fade_top", -highway_root.board_half_x,
                     highway_root.board_half_x, source_fade_top_y, kBoardZ);
    log_mesh_profile("track_surface", track_surface_mesh_,
                     track_surface_horizon_fit);
    log_mesh_profile("track_mask", track_mask_mesh_, track_mask_horizon_fit);
    log_mesh_profile("track_side_rails", track_side_rails_mesh_,
                     track_side_rails_horizon_fit);
    log_mesh_profile("track_lane_lines", track_lane_lines_mesh_,
                     track_lane_lines_horizon_fit);
    ++fade_profile_debug_budget;
  }
  auto track_surface_v_per_y = [&]() {
    if (!track_surface_mesh_.ok || track_surface_mesh_.verts.size() < 2) {
      return 1.0f / std::max(1.0f, top_y_ - remove_y_);
    }
    const float y_span = track_surface_mesh_.max_y - track_surface_mesh_.min_y;
    if (!std::isfinite(y_span) || std::abs(y_span) <= 0.001f) {
      return 1.0f / std::max(1.0f, top_y_ - remove_y_);
    }
    const float near_cut = track_surface_mesh_.min_y + y_span * 0.20f;
    const float far_cut = track_surface_mesh_.max_y - y_span * 0.20f;
    float near_y = 0.0f;
    float near_v = 0.0f;
    float far_y = 0.0f;
    float far_v = 0.0f;
    int near_count = 0;
    int far_count = 0;
    for (const auto& v : track_surface_mesh_.verts) {
      if (v.y <= near_cut) {
        near_y += v.y;
        near_v += v.v;
        ++near_count;
      }
      if (v.y >= far_cut) {
        far_y += v.y;
        far_v += v.v;
        ++far_count;
      }
    }
    if (near_count > 0 && far_count > 0) {
      near_y /= static_cast<float>(near_count);
      near_v /= static_cast<float>(near_count);
      far_y /= static_cast<float>(far_count);
      far_v /= static_cast<float>(far_count);
      const float dy = far_y - near_y;
      const float dv = far_v - near_v;
      if (std::isfinite(dy) && std::isfinite(dv) &&
          std::abs(dy) > 0.001f && std::abs(dv) > 0.000001f) {
        return dv / dy;
      }
    }
    const float v_span = track_surface_mesh_.max_v - track_surface_mesh_.min_v;
    if (std::isfinite(v_span) && std::abs(v_span) > 0.000001f) {
      return v_span / y_span;
    }
    return 1.0f / std::max(1.0f, top_y_ - remove_y_);
  };
  const float source_surface_v_per_y = track_surface_v_per_y();
  const float surface_v_per_y =
      source_surface_v_per_y /
      std::max(0.001f, track_surface_horizon_fit.scale_y);
  const float surface_scroll_v =
      std::fmod(static_cast<float>(song_time) * speed * surface_v_per_y,
                2048.0f);
  static int surface_scroll_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_SURFACE_SCROLL") &&
      surface_scroll_debug_budget < 120) {
    const float note_world_dy_per_sec = -speed;
    const float surface_feature_dy_per_sec =
        std::abs(surface_v_per_y) > 0.000001f ? -speed : 0.0f;
    const bool same_pace =
        std::abs(note_world_dy_per_sec - surface_feature_dy_per_sec) < 0.001f;
    std::fprintf(stderr,
                 "[highway-surface-scroll] t=%.3f speed=%.3f v_per_y=%.6f "
                 "source_v_per_y=%.6f y_scale=%.6f uv_v=%.6f "
                 "note_dy=%.3f surface_dy=%.3f same_pace=%d mesh=%d "
                 "selected=%d\n",
                 song_time, speed, surface_v_per_y, source_surface_v_per_y,
                 track_surface_horizon_fit.scale_y, surface_scroll_v,
                 note_world_dy_per_sec, surface_feature_dy_per_sec,
                 same_pace ? 1 : 0, track_surface_mesh_.ok ? 1 : 0,
                 selected_surface_loaded_ ? 1 : 0);
    ++surface_scroll_debug_budget;
  }
  auto draw_track_surface_quad = [&]() {
    IDirect3DTexture9* board = tex("track_surface.tex");
    const float tile = selected_surface_loaded_
        ? std::max(1.0f, top_y_ - remove_y_)
        : 18.0f;
    const float voff = static_cast<float>(song_time) * speed / tile;
    const float yN = remove_y_, yF = top_y_;
    const float vN = yN / tile + voff, vF = yF / tile + voff;
    const D3DCOLOR near_base = selected_surface_loaded_
        ? D3DCOLOR_ARGB(255, 255, 255, 255)
        : D3DCOLOR_ARGB(255, 120, 120, 130);
    const D3DCOLOR far_base = selected_surface_loaded_
        ? D3DCOLOR_ARGB(255, 190, 190, 200)
        : D3DCOLOR_ARGB(255, 6, 6, 9);
    const D3DCOLOR near_c = multiply_rgb(near_base, surface_flash_color, 1.0f);
    const D3DCOLOR far_c = multiply_rgb(far_base, surface_flash_color, 1.0f);
    V3 q[4] = {
        {-highway_root.board_half_x, yF, kBoardZ, 0.0f, 0.0f, 1.0f, far_c, 0.0f, vF},
        { highway_root.board_half_x, yF, kBoardZ, 0.0f, 0.0f, 1.0f, far_c, 1.0f, vF},
        {-highway_root.board_half_x, yN, kBoardZ, 0.0f, 0.0f, 1.0f, near_c, 0.0f, vN},
        { highway_root.board_half_x, yN, kBoardZ, 0.0f, 0.0f, 1.0f, near_c, 1.0f, vN},
    };
    draw_quad(dev_, board, q, false);
  };

  const bool native_track_enabled =
      !env_enabled("GHOGX_DISABLE_HIGHWAY_NATIVE_TRACK");
  const bool side_rail_force_warning =
      env_enabled("GHOGX_FORCE_HIGHWAY_SIDE_RAIL_WARNING") ||
      env_enabled("GHOGX_FORCE_HIGHWAY_SIDE_RAIL_WARNING_STAR");
  const bool side_rail_force_star =
      env_enabled("GHOGX_FORCE_HIGHWAY_SIDE_RAIL_STAR") ||
      env_enabled("GHOGX_FORCE_HIGHWAY_SIDE_RAIL_WARNING_STAR");
  const float sane_rock_fill =
      std::isfinite(rock_fill) ? std::clamp(rock_fill, 0.0f, 1.0f) : 1.0f;
  const bool rock_warning_disabled =
      env_enabled("GHOGX_DISABLE_HIGHWAY_ROCK_WARNING");
  const float rock_side_rail_warning =
      rock_warning_disabled
          ? 0.0f
          : std::clamp((0.50f - sane_rock_fill) / 0.30f, 0.0f, 1.0f);
  const float side_rail_warning = side_rail_force_warning
      ? 1.0f
      : std::max(std::clamp(bad_feedback_flash, 0.0f, 1.0f),
                 rock_side_rail_warning);
  const bool side_rail_star_active =
      star_power_active || side_rail_force_star ||
      env_enabled("GHOGX_FORCE_HIGHWAY_STARPOWER_GLOW");
  constexpr int kRockWarningDebugBudget = 900;
  constexpr int kRockWarningHealthyDebugBudget = 12;
  static int rock_warning_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_ROCK_WARNING") &&
      (rock_warning_debug_budget < kRockWarningHealthyDebugBudget ||
       side_rail_warning > 0.001f || sane_rock_fill < 0.51f) &&
      rock_warning_debug_budget < kRockWarningDebugBudget) {
    std::fprintf(stderr,
                 "[highway-rock-warning] t=%.3f rock=%.3f warning=%.3f "
                 "side=%.3f bad=%.3f forced=%d disabled=%d rails=%d "
                 "warning_anim=%d\n",
                 song_time, sane_rock_fill, rock_side_rail_warning,
                 side_rail_warning, bad_feedback_flash,
                 side_rail_force_warning ? 1 : 0,
                 rock_warning_disabled ? 1 : 0,
                 track_side_rails_mesh_.ok ? 1 : 0,
                 side_rails_warning_.ok ? 1 : 0);
    ++rock_warning_debug_budget;
  }
  SideRailColorState side_rail_color = side_rails_none_;
  if (track_intro_active &&
      track_intro_elapsed < kTrackIntroExtendSeconds &&
      side_rails_building_.ok) {
    side_rail_color = side_rails_building_;
  } else if (side_rail_warning > 0.01f && side_rail_star_active &&
      side_rails_warning_star_.ok) {
    side_rail_color = lerp_side_rail_color(side_rails_none_,
                                           side_rails_warning_star_,
                                           side_rail_warning);
  } else if (side_rail_warning > 0.01f && side_rails_warning_.ok) {
    side_rail_color = lerp_side_rail_color(side_rails_none_,
                                           side_rails_warning_,
                                           side_rail_warning);
  } else if (side_rail_star_active && side_rails_star_.ok) {
    side_rail_color = side_rails_star_;
  }
  if (surface_quad_underlay_) draw_track_surface_quad();
  if (native_track_enabled && track_surface_mesh_.ok) {
    const HighwayBlendState surface_blend_state =
        highway_blend_state_for(track_surface_mesh_.blend);
    DWORD previous_surface_src_blend = D3DBLEND_SRCALPHA;
    DWORD previous_surface_dest_blend = D3DBLEND_INVSRCALPHA;
    DWORD previous_surface_blend_op = D3DBLENDOP_ADD;
    dev_->GetRenderState(D3DRS_SRCBLEND, &previous_surface_src_blend);
    dev_->GetRenderState(D3DRS_DESTBLEND, &previous_surface_dest_blend);
    dev_->GetRenderState(D3DRS_BLENDOP, &previous_surface_blend_op);
    dev_->SetRenderState(D3DRS_BLENDOP, surface_blend_state.op);
    dev_->SetRenderState(D3DRS_SRCBLEND, surface_blend_state.src);
    dev_->SetRenderState(D3DRS_DESTBLEND, surface_blend_state.dest);
    draw_runtime_mesh_scaled_with_texture(
        track_surface_mesh_, track_surface_mesh_.texture_name, 0.0f,
        track_surface_horizon_fit.origin_y, track_surface_tint,
        highway_root.x_scale, track_surface_horizon_fit.scale_y, 1.0f,
        false, 0.0f, surface_scroll_v, true, 0.0f, false, 0.0f, true,
        source_fade_top_y, source_fade_alpha_dist);
    dev_->SetRenderState(D3DRS_BLENDOP, previous_surface_blend_op);
    dev_->SetRenderState(D3DRS_SRCBLEND, previous_surface_src_blend);
    dev_->SetRenderState(D3DRS_DESTBLEND, previous_surface_dest_blend);
    if (track_mask_mesh_.ok &&
        !env_enabled("GHOGX_DISABLE_HIGHWAY_TRACK_MASK")) {
      draw_runtime_mesh_scaled_with_texture(
          track_mask_mesh_, track_mask_mesh_.texture_name, 0.0f, 0.0f,
          D3DCOLOR_ARGB(255, 255, 255, 255), highway_root.x_scale,
          1.0f, 1.0f, false, 0.0f, 0.0f, true, 0.0f, false, 0.0f, true,
          source_fade_top_y, source_fade_alpha_dist);
    }
    if (track_side_rails_mesh_.ok) {
      draw_runtime_mesh_scaled_with_texture(
          track_side_rails_mesh_, track_side_rails_mesh_.texture_name, 0.0f,
          track_side_rails_horizon_fit.origin_y,
          side_rail_d3d_color(side_rail_color), highway_root.x_scale,
          track_side_rails_horizon_fit.scale_y, 1.0f, false, 0.0f,
          0.0f, true, 0.0f, false, 0.0f, true, source_fade_top_y,
          source_fade_alpha_dist);
    }
    if (track_lane_lines_mesh_.ok) {
      draw_runtime_mesh_scaled_with_texture(
          track_lane_lines_mesh_, track_lane_lines_mesh_.texture_name, 0.0f,
          track_lane_lines_horizon_fit.origin_y,
          D3DCOLOR_ARGB(48, 32, 32, 32), highway_root.x_scale,
          track_lane_lines_horizon_fit.scale_y, 1.0f, true, 0.0f,
          0.0f, true, 0.0f, false, 0.0f, true, source_fade_top_y,
          source_fade_alpha_dist);
    }
  } else if (!surface_quad_underlay_) {
    draw_track_surface_quad();
  }

  if (!native_track_enabled || !track_lane_lines_mesh_.ok) {
    for (int i = 0; i <= 5; ++i) {
      const float x = highway_root.lane_boundary_x(i);
      const float hx = highway_root.scale_x(0.10f);
      const D3DCOLOR nc = D3DCOLOR_ARGB(130, 0, 0, 0), fc = D3DCOLOR_ARGB(15, 0, 0, 0);
      V3 q[4] = {
          {x - hx, top_y_,    kBoardZ + 0.01f, 0.0f, 0.0f, 1.0f, fc, 0.0f, 0.0f},
          {x + hx, top_y_,    kBoardZ + 0.01f, 0.0f, 0.0f, 1.0f, fc, 1.0f, 0.0f},
          {x - hx, remove_y_, kBoardZ + 0.01f, 0.0f, 0.0f, 1.0f, nc, 0.0f, 1.0f},
          {x + hx, remove_y_, kBoardZ + 0.01f, 0.0f, 0.0f, 1.0f, nc, 1.0f, 1.0f},
      };
      draw_quad(dev_, nullptr, q);
    }
  }
  dev_->SetTexture(0, nullptr);

  const bool star_power_glow_active =
      (star_power_active ||
       star_power_flash > 0.01f ||
       env_enabled("GHOGX_FORCE_HIGHWAY_STARPOWER_GLOW")) &&
      !env_enabled("GHOGX_DISABLE_HIGHWAY_STARPOWER_GLOW");
  const bool bonus_highway_active =
      (star_power_active || env_enabled("GHOGX_FORCE_HIGHWAY_BONUS")) &&
      !env_enabled("GHOGX_DISABLE_HIGHWAY_BONUS");
  static int star_power_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_STAR_POWER") &&
      star_power_debug_budget < 960) {
    std::fprintf(
        stderr,
        "[highway-star-power] t=%.3f active=%d whammy=%d flash=%.3f glow=%d bonus=%d "
        "track_glow=%d bonus_gem=%d bonus_tail=%d bonus_smasher=%d "
        "bonus_flame=%d\n",
        song_time, star_power_active ? 1 : 0, whammy_active ? 1 : 0,
        star_power_flash,
        star_power_glow_active ? 1 : 0, bonus_highway_active ? 1 : 0,
        star_power_track_glow_mesh_.ok ? 1 : 0,
        bonus_gem_mesh_.ok ? 1 : 0, bonus_tail_mesh_.ok ? 1 : 0,
        !bonus_smasher_texture_name_.empty() ? 1 : 0,
        bonus_hit_flame_mesh_.ok ? 1 : 0);
    ++star_power_debug_budget;
  }
  const auto mesh_half_x = [](const RuntimeMesh& mesh, float fallback) {
    return mesh.ok ? std::max(0.001f, (mesh.max_x - mesh.min_x) * 0.5f)
                   : fallback;
  };
  const auto mesh_half_y = [](const RuntimeMesh& mesh, float fallback) {
    return mesh.ok ? std::max(0.001f, (mesh.max_y - mesh.min_y) * 0.5f)
                   : fallback;
  };
  const auto lane_gem_half_x = [&](int lane) {
    return mesh_half_x(gem_mesh_[std::clamp(lane, 0, 4)], kGemHalf);
  };
  const auto lane_gem_half_y = [&](int lane) {
    return mesh_half_y(gem_mesh_[std::clamp(lane, 0, 4)], kGemHalf);
  };
  if (native_track_enabled && star_power_glow_active &&
      star_power_track_glow_mesh_.ok) {
    const int glow_alpha = star_power_active
                               ? 180
                               : std::clamp(
                                     static_cast<int>(80.0f +
                                                      star_power_flash * 175.0f),
                                     0, 255);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    draw_runtime_mesh_scaled_with_texture(
        star_power_track_glow_mesh_, star_power_track_glow_mesh_.texture_name,
        0.0f, star_power_track_glow_horizon_fit.origin_y,
        D3DCOLOR_ARGB(glow_alpha, 255, 255, 255), highway_root.x_scale,
        star_power_track_glow_horizon_fit.scale_y, 1.0f, true, 0.0f,
        0.0f, true, 0.0f, false, 0.0f, true, source_fade_top_y,
        source_fade_alpha_dist);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  }

  static int bad_feedback_debug_budget = 0;
  if (env_enabled("GHOGX_DEBUG_HIGHWAY_BAD_FEEDBACK") &&
      (bad_feedback_flash > 0.001f || side_rail_warning > 0.001f) &&
      bad_feedback_debug_budget < 240) {
    std::fprintf(stderr,
                 "[highway-bad-feedback] t=%.3f flash=%.3f side=%.3f "
                 "explode=purged fade_top=%.3f fade_dist=%.3f miss_mesh=%d\n",
                 song_time, bad_feedback_flash, side_rail_warning,
                 source_fade_top_y, source_fade_alpha_dist,
                 miss_mesh_.ok ? 1 : 0);
    ++bad_feedback_debug_budget;
  }

  if (difficulty < 0 || difficulty > 3) {
    dev_->EndScene();
    if (have_previous_viewport) dev_->SetViewport(&previous_viewport);
    return;
  }
  const auto& notes = chart.notes[difficulty];
  const float authored_lead = (top_y_ - kStrikeY) / speed; // seconds from spawn to strike
  const float lead = std::min(authored_lead, std::max(0.0f, lookahead_sec));
  const float trail = (kStrikeY - remove_y_) / speed;   // seconds strike to prune

  // --- 3) Beat/subdivision lines (native track.milo timing meshes) ---
  {
    IDirect3DTexture9* bar = tex("barline_gw.tex");
    if ((bar || beat_line_mesh_.ok || half_beat_line_mesh_.ok ||
         quarter_beat_line_mesh_.ok) &&
        chart.ticks_per_beat > 0) {
      const double first_sec = std::max(0.0, song_time - trail);
      const double last_sec = std::max(first_sec, song_time + lead);
      const uint32_t first_tick = chart.sec_to_tick(first_sec);
      const uint32_t last_tick = chart.sec_to_tick(last_sec);
      const uint32_t subdiv = std::max<uint32_t>(1, chart.ticks_per_beat / 4);
      uint32_t line_tick = (first_tick / subdiv) * subdiv;
      for (int b = 0; b < 1024 && line_tick <= last_tick; ++b) {
        const double bt = chart.tick_to_sec(line_tick);
        const float y = note_y(bt);
        if (y >= remove_y_ && y <= top_y_) {
          const uint32_t subdiv_index = line_tick / subdiv;
          const bool downbeat = (subdiv_index % 16u) == 0;
          const bool beat = (subdiv_index % 4u) == 0;
          const bool half = (subdiv_index % 2u) == 0;
          const RuntimeMesh* line_mesh = nullptr;
          float alpha = 40.0f;
          if (downbeat && bar_line_mesh_.ok) {
            line_mesh = &bar_line_mesh_;
            alpha = 120.0f;
          } else if (beat && beat_line_mesh_.ok) {
            line_mesh = &beat_line_mesh_;
            alpha = 92.0f;
          } else if (half && half_beat_line_mesh_.ok) {
            line_mesh = &half_beat_line_mesh_;
            alpha = 62.0f;
          } else if (quarter_beat_line_mesh_.ok) {
            line_mesh = &quarter_beat_line_mesh_;
            alpha = 40.0f;
          }
          const int a = static_cast<int>(alpha * depth_fade(y));
          if (line_mesh) {
            draw_runtime_mesh_scaled_with_texture(
                *line_mesh, line_mesh->texture_name,
                -line_mesh->center_x * highway_root.x_scale,
                y - line_mesh->center_y,
                D3DCOLOR_ARGB(a, 255, 255, 255),
                highway_root.x_scale, 1.0f, 1.0f, true);
          } else if (bar && (downbeat || beat)) {
            V3 q[4];
            flat_quad(q, 0.0f, y, kBoardZ + 0.02f, highway_root.board_half_x,
                      0.5f, D3DCOLOR_ARGB(a, 255, 255, 255));
            draw_quad(dev_, bar, q);
          }
        }
        if (line_tick > UINT32_MAX - subdiv) break;
        line_tick += subdiv;
      }
    }
  }

  // --- 4) Sustain tails (before gems) ---
  if (!env_enabled("GHOGX_DISABLE_HIGHWAY_SUSTAINS")) {
    IDirect3DTexture9* raw_tail = tex("tail2.tex");
    IDirect3DTexture9* held_tail = tex("tail_tight.tex");
    if (!held_tail) held_tail = raw_tail;
    const uint32_t sustain_min = chart.ticks_per_beat / 4;
    const float tail_near_y = kStrikeY + nowbar_tail_clip_;
    const float tail_far_y = std::max(
        tail_near_y + 1.0f,
        source_fade_top_y - std::max(horizon_tail_clip_, alpha_dist_ * 0.5f));
    const bool debug_tails = env_enabled("GHOGX_DEBUG_HIGHWAY_TAILS");
    // GemRepTemplate::SetupTailVerts (0x826A0100) uses 30 sections at the
    // normal/high video setting.  The X360 track config supplies horizon_y
    // 120 and remove_y -15, so each section spans exactly 4.5 authored units.
    // Tail::Poll (0x8269EAD0) converts each ring's authored Y distance to a
    // delay-line age with floor(y * 0.5); it never stretches the last 30
    // samples over the currently visible sustain.
    constexpr int kRetailTailSections = 30;
    constexpr float kRetailTailSectionLength =
        (120.0f - (-15.0f)) / static_cast<float>(kRetailTailSections);
    constexpr float kRetailHistoryAgePerY = 0.5f;
    auto whammy_history_sample = [](const SustainVisualState* visual,
                                    size_t age) {
      if (!visual) return 1.0f;
      const size_t clamped_age =
          std::min(age, visual->whammy_history.size() - 1);
      const size_t sample =
          (visual->whammy_cursor + visual->whammy_history.size() -
           clamped_age) %
          visual->whammy_history.size();
      return 1.0f + visual->whammy_history[sample];
    };
    // GH2 X360 Track::Poll (0x8269A778) advances the global whammy blend by
    // exactly 0.1 per update and Tail::Poll (0x8269EAD0) writes -3 times the
    // signed whammy axis into a 100-sample history for every live tail.  Keep
    // that state on the renderer clock so the body and glow consume one shared
    // source-shaped curve.
    if (last_sustain_visual_time_ >= 0.0 &&
        song_time + 0.000001 < last_sustain_visual_time_) {
      sustain_visual_states_.clear();
      whammy_envelope_ = 0.0f;
      last_sustain_visual_time_ = -1.0;
    }
    if (last_sustain_visual_time_ < 0.0 ||
        std::abs(song_time - last_sustain_visual_time_) > 0.000001) {
      whammy_envelope_ = std::clamp(
          whammy_envelope_ + (whammy_active ? 0.1f : -0.1f), 0.0f, 1.0f);
      std::set<uint64_t> live_tail_keys;
      if (active_sustains) {
        for (const auto& sustain : *active_sustains) {
          const uint64_t key =
              sustain.source_index != static_cast<size_t>(-1)
                  ? static_cast<uint64_t>(sustain.source_index)
                  : (UINT64_C(1) << 63) |
                        static_cast<uint64_t>(sustain.source_tick);
          live_tail_keys.insert(key);
          SustainVisualState& visual = sustain_visual_states_[key];
          // Tail::Poll advances the circular cursor before writing the current
          // filtered bar value.  The cursor therefore always names the newest
          // sample consumed by the tail geometry below.
          visual.whammy_cursor =
              (visual.whammy_cursor + 1) % visual.whammy_history.size();
          visual.whammy_history[visual.whammy_cursor] =
              -3.0f * std::clamp(whammy_axis, -1.0f, 1.0f);
          const float sustain_y0 = tail_near_y;
          const float sustain_y1 = std::min(
              note_y(sustain.end_time), tail_far_y);
          const float sustain_len = std::max(0.0f, sustain_y1 - sustain_y0);
          const int live_sections = std::clamp(
              static_cast<int>(sustain_len / kRetailTailSectionLength), 0,
              kRetailTailSections);
          float sample_y =
              sustain_len - live_sections * kRetailTailSectionLength;
          float max_body_scale = whammy_history_sample(&visual, 0);
          for (int section = 0; section < live_sections - 1; ++section) {
            const size_t age = static_cast<size_t>(
                std::max(0.0f,
                         std::floor(sample_y * kRetailHistoryAgePerY)));
            max_body_scale =
                std::max(max_body_scale,
                         whammy_history_sample(&visual, age));
            sample_y += kRetailTailSectionLength;
          }
          const float target_glow_scale =
              1.0f + 0.5f * (max_body_scale - 1.0f);
          visual.glow_width_scale +=
              (target_glow_scale - visual.glow_width_scale) * 0.2f;
        }
      }
      for (auto it = sustain_visual_states_.begin();
           it != sustain_visual_states_.end();) {
        if (live_tail_keys.find(it->first) == live_tail_keys.end()) {
          it = sustain_visual_states_.erase(it);
        } else {
          ++it;
        }
      }
      last_sustain_visual_time_ = song_time;
    }
    const std::optional<std::string> tail_layer_only =
        env_string_nonempty("GHOGX_HIGHWAY_TAIL_LAYER_ONLY");
    auto tail_layer_visible = [&](const char* source_label) {
      if (!tail_layer_only) return true;
      const char* label =
          source_label && source_label[0] ? source_label : "<unknown>";
      if (*tail_layer_only == label) return true;
      return false;
    };
    auto material_fill_color = [](const RuntimeMesh* mesh, D3DCOLOR fallback,
                                  uint8_t alpha) {
      const auto byte_from_float = [](float value) {
        return std::clamp(static_cast<int>(value * 255.0f), 0, 255);
      };
      if (!mesh || !mesh->ok) {
        return (fallback & 0x00ffffffu) |
               (static_cast<D3DCOLOR>(alpha) << 24);
      }
      const auto& source_color = mesh->material_color;
      // RndMat.intensify doubles the base map. These solid fills are native
      // geometry used to close the visible tail body, so they should inherit
      // the authored Mat color without applying the texture-stage boost again.
      return D3DCOLOR_ARGB(
          alpha, byte_from_float(source_color[0]),
          byte_from_float(source_color[1]), byte_from_float(source_color[2]));
    };
    static int tail_debug_budget = 0;
    auto draw_tail_segment_y = [&](int lane, float y0, float y1,
                                   const char* source_label,
                                   const RuntimeMesh* mesh,
                                   IDirect3DTexture9* texture,
                                    float half_width, D3DCOLOR color,
                                    bool active_segment, bool star_tail,
                                    bool whammy_tail, double on,
                                    double off,
                                    TailWidthProfileKind width_profile,
                                    float center_offset_px_720,
                                    const SustainVisualState* visual) {
      if (lane < 0 || lane >= 5) return;
      if (y1 <= y0) return;
      if (!tail_layer_visible(source_label)) return;
      const bool measured_body_width =
          width_profile != TailWidthProfileKind::kNone;
      if (debug_tails && tail_debug_budget < 96) {
        const char* tex_name = mesh && mesh->ok ? mesh->texture_name.c_str()
                                                : "<flat>";
        const bool has_mesh_tex =
            mesh && mesh->ok && tex(mesh->texture_name) != nullptr;
        std::fprintf(stderr,
                     "[highway-tail] source=%s active=%d star_tail=%d whammy=%d "
                     "lane=%d on=%.3f off=%.3f y=%.3f..%.3f "
                     "hy=%.3f fade=%.3f..%.3f mesh=%d tex=%s tex_ok=%d "
                      "blend=%u half=%.3f measured_body_width=%d "
                      "center_offset_px720=%.3f "
                      "target_mid_px720=%.3f solved_mid_half=%.3f argb=%08x\n",
                     source_label ? source_label : "<unknown>",
                     active_segment ? 1 : 0, star_tail ? 1 : 0,
                     whammy_tail ? 1 : 0, lane, on, off,
                     y0, y1, (y1 - y0) * 0.5f,
                     depth_fade_for(y0, source_fade_top_y,
                                    source_fade_alpha_dist),
                     depth_fade_for(y1, source_fade_top_y,
                                    source_fade_alpha_dist),
                     mesh && mesh->ok ? 1 : 0,
                     tex_name, has_mesh_tex ? 1 : (texture ? 1 : 0),
                      mesh && mesh->ok ? static_cast<unsigned>(mesh->blend) : 0,
                      half_width, measured_body_width ? 1 : 0,
                      center_offset_px_720,
                      measured_body_width
                          ? sample_mesh_body_geometry_width_px_720(
                                width_profile, 0.5f)
                          : 0.0f,
                      measured_body_width
                          ? local_tail_half_for_screen_width(
                                lane, (y0 + y1) * 0.5f,
                                sample_mesh_body_geometry_width_px_720(
                                    width_profile, 0.5f),
                                half_width)
                          : 0.0f,
                      static_cast<unsigned>(color));
        ++tail_debug_budget;
      }
      auto draw_tail_section = [&](float sy0, float sy1,
                                   float section_half_width,
                                   float near_history_scale,
                                   float far_history_scale) {
        const float cy = (sy0 + sy1) * 0.5f, hy = (sy1 - sy0) * 0.5f;
        if (hy <= 0.0001f) return;
        const float center_x =
            lane_x(lane) +
            local_tail_x_offset_for_screen_px(lane, cy,
                                              center_offset_px_720);
        if (mesh && mesh->ok) {
          const float mesh_hx =
              std::max(0.001f, (mesh->max_x - mesh->min_x) * 0.5f);
          const float source_half_width =
              measured_body_width ? section_half_width : mesh_hx;
          const float root_half_width =
              highway_root.scale_x(source_half_width);
          const float mesh_hy =
              std::max(0.001f, (mesh->max_y - mesh->min_y) * 0.5f);
          RuntimeMesh tapered_mesh;
          const RuntimeMesh* section_mesh = mesh;
          if (std::abs(near_history_scale - 1.0f) > 0.0001f ||
              std::abs(far_history_scale - 1.0f) > 0.0001f) {
            tapered_mesh = *mesh;
            const float mesh_y_span = std::max(0.001f, mesh->max_y - mesh->min_y);
            for (MeshVertex& vertex : tapered_mesh.verts) {
              const float rel_y =
                  std::clamp((vertex.y - mesh->min_y) / mesh_y_span, 0.0f,
                             1.0f);
              const float history_scale =
                  near_history_scale +
                  (far_history_scale - near_history_scale) * rel_y;
              vertex.x = mesh->center_x +
                         (vertex.x - mesh->center_x) * history_scale;
            }
            section_mesh = &tapered_mesh;
          }
          const HighwayBlendState tail_blend_state =
              highway_blend_state_for(mesh->blend);
          DWORD prev_tail_src_blend = D3DBLEND_SRCALPHA;
          DWORD prev_tail_dest_blend = D3DBLEND_INVSRCALPHA;
          DWORD prev_tail_blend_op = D3DBLENDOP_ADD;
          dev_->GetRenderState(D3DRS_SRCBLEND, &prev_tail_src_blend);
          dev_->GetRenderState(D3DRS_DESTBLEND, &prev_tail_dest_blend);
          dev_->GetRenderState(D3DRS_BLENDOP, &prev_tail_blend_op);
          dev_->SetRenderState(D3DRS_BLENDOP, tail_blend_state.op);
          dev_->SetRenderState(D3DRS_SRCBLEND, tail_blend_state.src);
          dev_->SetRenderState(D3DRS_DESTBLEND, tail_blend_state.dest);
          draw_runtime_mesh_scaled_with_texture(
              *section_mesh, section_mesh->texture_name,
              center_x - mesh->center_x * (root_half_width / mesh_hx),
              cy - mesh->center_y * (hy / mesh_hy), color,
              root_half_width / mesh_hx, hy / mesh_hy, 1.0f, true, 0.0f,
              0.0f, true, 0.0f, false, 0.0f, true, source_fade_top_y,
              source_fade_alpha_dist);
          dev_->SetRenderState(D3DRS_BLENDOP, prev_tail_blend_op);
          dev_->SetRenderState(D3DRS_SRCBLEND, prev_tail_src_blend);
          dev_->SetRenderState(D3DRS_DESTBLEND, prev_tail_dest_blend);
          return;
        }
        V3 q[4];
        flat_quad(q, center_x, cy, kGemZ - 0.02f,
                  highway_root.scale_x(section_half_width), hy, color);
        const float near_hx =
            highway_root.scale_x(section_half_width * near_history_scale);
        const float far_hx =
            highway_root.scale_x(section_half_width * far_history_scale);
        q[0].x = center_x - far_hx;
        q[1].x = center_x + far_hx;
        q[2].x = center_x - near_hx;
        q[3].x = center_x + near_hx;
        q[0].c = faded_tail_color(color, sy0);
        q[1].c = faded_tail_color(color, sy0);
        q[2].c = faded_tail_color(color, sy1);
        q[3].c = faded_tail_color(color, sy1);
      draw_quad(dev_, texture, q);
      };
      if (measured_body_width) {
        auto world_y_at = [&](float rel) {
          return y1 + (y0 - y1) * std::clamp(rel, 0.0f, 1.0f);
        };
        auto section_half_at = [&](float rel, float y) {
          return local_tail_half_for_screen_width(
              lane, y,
              sample_mesh_body_geometry_width_px_720(width_profile, rel),
              half_width);
        };
        auto draw_profile_section = [&](float rel_far, float rel_near) {
          const float sy_far = world_y_at(rel_far);
          const float sy_near = world_y_at(rel_near);
          const float rel_mid = (rel_far + rel_near) * 0.5f;
          const float y_mid = (sy_far + sy_near) * 0.5f;
          draw_tail_section(sy_near, sy_far,
                            section_half_at(rel_mid, y_mid), 1.0f, 1.0f);
        };
        const auto draw_profile = [&](const auto& profile,
                                      int subdivisions_per_interval = 1) {
          const int subdivisions = std::max(1, subdivisions_per_interval);
          for (size_t i = 1; i < profile.size(); ++i) {
            const float rel0 = profile[i - 1].rel_y;
            const float rel1 = profile[i].rel_y;
            for (int part = 0; part < subdivisions; ++part) {
              const float t0 = static_cast<float>(part) /
                               static_cast<float>(subdivisions);
              const float t1 = static_cast<float>(part + 1) /
                               static_cast<float>(subdivisions);
              draw_profile_section(rel0 + (rel1 - rel0) * t0,
                                   rel0 + (rel1 - rel0) * t1);
            }
          }
          draw_profile_section(profile.back().rel_y, 1.0f);
        };
        if (width_profile == TailWidthProfileKind::kNormalBodyFill) {
          draw_profile(kPcsx2NormalBodyWidthProfile4x3);
        } else if (width_profile == TailWidthProfileKind::kNormalBodyDetail) {
          draw_profile(kPcsx2NormalSilhouetteWidthProfile4x3);
        } else if (width_profile == TailWidthProfileKind::kNormalTightJoin) {
          draw_profile(kPcsx2NormalTightJoinWidthProfile4x3, 4);
        } else {
          draw_profile(kPcsx2WhammyBodyWidthProfile4x3);
        }
      } else if (visual) {
        // Exact live-ring layout from Tail::Poll (0x8269EAD0): unused
        // sections collapse at the near end, the first live section carries
        // the length remainder, all remaining sections stay 4.5 units apart,
        // the final live ring has half amplitude, and the cap closes at zero.
        const float len = std::max(0.0f, y1 - y0);
        const int live_sections = std::clamp(
            static_cast<int>(len / kRetailTailSectionLength), 0,
            kRetailTailSections);
        float sample_y = len - live_sections * kRetailTailSectionLength;
        std::vector<std::pair<float, float>> rings;
        rings.reserve(static_cast<size_t>(live_sections) + 2);
        rings.emplace_back(y0, whammy_history_sample(visual, 0));
        for (int section = 0; section < live_sections - 1; ++section) {
          const size_t age = static_cast<size_t>(
              std::max(0.0f,
                       std::floor(sample_y * kRetailHistoryAgePerY)));
          rings.emplace_back(y0 + sample_y,
                             whammy_history_sample(visual, age));
          sample_y += kRetailTailSectionLength;
        }
        if (live_sections > 0) {
          const size_t age = static_cast<size_t>(
              std::max(0.0f,
                       std::floor(sample_y * kRetailHistoryAgePerY)));
          const float full_scale = whammy_history_sample(visual, age);
          rings.emplace_back(y0 + sample_y,
                             1.0f + (full_scale - 1.0f) * 0.5f);
        }
        rings.emplace_back(y1, 0.0f);
        for (size_t ring = 1; ring < rings.size(); ++ring) {
          if (rings[ring].first <= rings[ring - 1].first + 0.0001f) continue;
          draw_tail_section(rings[ring - 1].first, rings[ring].first,
                            half_width, rings[ring - 1].second,
                            rings[ring].second);
        }
      } else {
        draw_tail_section(y0, y1, half_width, 1.0f, 1.0f);
      }
    };
    auto draw_tail_line_y = [&](int lane, float y0, float y1,
                                const char* source_label,
                                const RuntimeLineMaterial* material,
                                float half_width, D3DCOLOR tint,
                                bool active_segment, bool star_tail,
                                bool whammy_tail, double on, double off,
                                TailWidthProfileKind width_profile) {
      if (lane < 0 || lane >= 5) return false;
      if (y1 <= y0 || !material || !material->ok) return false;
      if (!tail_layer_visible(source_label)) return false;
      IDirect3DTexture9* texture = tex(material->texture_name);
      if (!texture) return false;
      const bool measured_body_width =
          width_profile != TailWidthProfileKind::kNone;
      const bool source_alpha_test =
          material->alpha_cut || material->alpha_write;
      const DWORD source_alpha_ref = material->alpha_cut ? kNoteCardAlphaRef : 0;
      if (debug_tails && tail_debug_budget < 96) {
        const float target_mid_width =
            measured_body_width
                ? sample_visible_tail_width_px_720(width_profile, 0.5f)
                : 0.0f;
        std::fprintf(stderr,
                     "[highway-tail-line] source=%s active=%d star_tail=%d "
                     "whammy=%d lane=%d on=%.3f off=%.3f y=%.3f..%.3f "
                     "fade=%.3f..%.3f mat=%s tex=%s tex_ok=1 blend=%u "
                     "zmode=%u texgen=%u prelit=%d alpha_cut=%d "
                     "alpha_write=%d intensify=%d cull=%d sampler=%s "
                     "alpha_test=%d alpha_ref=%lu "
                     "half=%.3f measured_body_width=%d profile_samples=%zu "
                     "target_mid_px720=%.3f authored_mid_px720=%.3f tint=%08x "
                     "color=%.3f,%.3f,%.3f,%.3f\n",
                     source_label ? source_label : "<unknown>",
                     active_segment ? 1 : 0, star_tail ? 1 : 0,
                     whammy_tail ? 1 : 0, lane, on, off, y0, y1,
                     depth_fade_for(y0, source_fade_top_y,
                                    source_fade_alpha_dist),
                     depth_fade_for(y1, source_fade_top_y,
                                    source_fade_alpha_dist),
                     material->material_name.c_str(),
                     material->texture_name.c_str(),
                     static_cast<unsigned>(material->blend),
                     static_cast<unsigned>(material->z_mode),
                     static_cast<unsigned>(material->tex_gen),
                     material->prelit ? 1 : 0,
                     material->alpha_cut ? 1 : 0,
                     material->alpha_write ? 1 : 0,
                     material->intensify ? 1 : 0,
                     material->cull ? 1 : 0,
                     material->texture_wrap ? "wrap" : "clamp",
                     source_alpha_test ? 1 : 0,
                     static_cast<unsigned long>(source_alpha_ref), half_width,
                     measured_body_width ? 1 : 0,
                     measured_body_width
                         ? tail_width_profile_sample_count(width_profile)
                         : 0u,
                     target_mid_width,
                     measured_body_width
                         ? sample_line_body_geometry_width_px_720(
                               width_profile, 0.5f)
                         : 0.0f,
                     static_cast<unsigned>(tint),
                     material->color[0], material->color[1],
                     material->color[2], material->color[3]);
        ++tail_debug_budget;
      }
      const auto line_color = [&](float y) {
        const float ta = static_cast<float>((tint >> 24) & 0xff) / 255.0f;
        const float tr = static_cast<float>((tint >> 16) & 0xff) / 255.0f;
        const float tg = static_cast<float>((tint >> 8) & 0xff) / 255.0f;
        const float tb = static_cast<float>(tint & 0xff) / 255.0f;
        const float fade =
            depth_fade_for(y, source_fade_top_y, source_fade_alpha_dist);
        const int a = std::clamp(
            static_cast<int>(255.0f * material->color[3] * ta * fade), 0,
            255);
        const int r = std::clamp(
            static_cast<int>(255.0f * material->color[0] * tr), 0, 255);
        const int g = std::clamp(
            static_cast<int>(255.0f * material->color[1] * tg), 0, 255);
        const int b = std::clamp(
            static_cast<int>(255.0f * material->color[2] * tb), 0, 255);
        return D3DCOLOR_ARGB(a, r, g, b);
      };

      const HighwayBlendState line_blend_state =
          highway_blend_state_for(material->blend);
      DWORD prev_line_src_blend = D3DBLEND_SRCALPHA;
      DWORD prev_line_dest_blend = D3DBLEND_INVSRCALPHA;
      DWORD prev_line_blend_op = D3DBLENDOP_ADD;
      DWORD prev_line_alpha_test = FALSE;
      DWORD prev_line_alpha_func = D3DCMP_ALWAYS;
      DWORD prev_line_alpha_ref = 0;
      dev_->GetRenderState(D3DRS_SRCBLEND, &prev_line_src_blend);
      dev_->GetRenderState(D3DRS_DESTBLEND, &prev_line_dest_blend);
      dev_->GetRenderState(D3DRS_BLENDOP, &prev_line_blend_op);
      dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prev_line_alpha_test);
      dev_->GetRenderState(D3DRS_ALPHAFUNC, &prev_line_alpha_func);
      dev_->GetRenderState(D3DRS_ALPHAREF, &prev_line_alpha_ref);
      dev_->SetRenderState(D3DRS_BLENDOP, line_blend_state.op);
      dev_->SetRenderState(D3DRS_SRCBLEND, line_blend_state.src);
      dev_->SetRenderState(D3DRS_DESTBLEND, line_blend_state.dest);
      dev_->SetRenderState(D3DRS_ALPHATESTENABLE,
                           source_alpha_test ? TRUE : FALSE);
      if (source_alpha_test) {
        dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        dev_->SetRenderState(D3DRS_ALPHAREF, source_alpha_ref);
      }

      const float z = kGemZ - 0.015f;
      auto half_at = [&](float rel, float y) {
        if (!measured_body_width) return highway_root.scale_x(half_width);
        const float local_half = local_tail_half_for_screen_width(
            lane, y,
            sample_line_body_geometry_width_px_720(width_profile, rel),
            half_width);
        return highway_root.scale_x(local_half);
      };
      auto world_y_at = [&](float rel) {
        return y1 + (y0 - y1) * std::clamp(rel, 0.0f, 1.0f);
      };
      auto draw_line_section = [&](float rel_far, float rel_near) {
        const float yf = world_y_at(rel_far);
        const float yn = world_y_at(rel_near);
        const float hxf = half_at(rel_far, yf);
        const float hxn = half_at(rel_near, yn);
        const float cxf = lane_x(lane);
        const float cxn = lane_x(lane);
        V3 q[4] = {
            {cxf - hxf, yf, z, 0.0f, 0.0f, 1.0f, line_color(yf), 0.0f, 0.5f},
            {cxf + hxf, yf, z, 0.0f, 0.0f, 1.0f, line_color(yf), 1.0f, 0.5f},
            {cxn - hxn, yn, z, 0.0f, 0.0f, 1.0f, line_color(yn), 0.0f, 0.5f},
            {cxn + hxn, yn, z, 0.0f, 0.0f, 1.0f, line_color(yn), 1.0f, 0.5f},
        };
        draw_quad(dev_, texture, q, true, material->intensify,
                  material->texture_wrap);
      };
      if (measured_body_width) {
        const auto draw_profile = [&](const auto& profile) {
          for (size_t i = 1; i < profile.size(); ++i) {
            draw_line_section(profile[i - 1].rel_y, profile[i].rel_y);
          }
          draw_line_section(profile.back().rel_y, 1.0f);
        };
        if (width_profile == TailWidthProfileKind::kNormalBodyDetail) {
          draw_profile(kPcsx2NormalSilhouetteWidthProfile4x3);
        } else if (width_profile == TailWidthProfileKind::kNormalBodyFill) {
          draw_profile(kPcsx2NormalBodyWidthProfile4x3);
        } else if (width_profile == TailWidthProfileKind::kNormalTightJoin) {
          draw_profile(kPcsx2NormalTightJoinWidthProfile4x3);
        } else {
          draw_profile(kPcsx2WhammyBodyWidthProfile4x3);
        }
      } else {
        draw_line_section(0.0f, 1.0f);
      }

      dev_->SetRenderState(D3DRS_BLENDOP, prev_line_blend_op);
      dev_->SetRenderState(D3DRS_SRCBLEND, prev_line_src_blend);
      dev_->SetRenderState(D3DRS_DESTBLEND, prev_line_dest_blend);
      dev_->SetRenderState(D3DRS_ALPHAREF, prev_line_alpha_ref);
      dev_->SetRenderState(D3DRS_ALPHAFUNC, prev_line_alpha_func);
      dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prev_line_alpha_test);
      return true;
    };
    auto draw_tail_segment = [&](int lane, double on, double off,
                                 const char* source_label,
                                 const RuntimeMesh* mesh,
                                 IDirect3DTexture9* texture,
                                  float half_width, D3DCOLOR color,
                                  bool active_segment, bool star_tail,
                                  bool whammy_tail,
                                  TailWidthProfileKind width_profile =
                                      TailWidthProfileKind::kNone,
                                  float center_offset_px_720 = 0.0f,
                                  const SustainVisualState* visual = nullptr) {
      if (off < song_time - trail) return;
      if (on > song_time + lead) return;
      const float y0 = std::max(note_y(on), tail_near_y);
      const float y1 = std::min(note_y(off), tail_far_y);
      draw_tail_segment_y(lane, y0, y1, source_label, mesh, texture,
                          half_width, color, active_segment, star_tail,
                           whammy_tail, on, off, width_profile,
                           center_offset_px_720, visual);
    };
    auto draw_tail_line_segment = [&](int lane, double on, double off,
                                      const char* source_label,
                                      const RuntimeLineMaterial* material,
                                      float half_width, D3DCOLOR color,
                                      bool active, bool star, bool whammy) {
      if (off < song_time - trail || on > song_time + lead) return false;
      const float y0 = std::max(note_y(on), tail_near_y);
      const float y1 = std::min(note_y(off), tail_far_y);
      if (y1 <= y0) return false;
      return draw_tail_line_y(lane, y0, y1, source_label, material,
                              half_width, color, active, star, whammy,
                              on, off, TailWidthProfileKind::kNone);
    };
    auto blend_mesh_color = [](const RuntimeMesh& from,
                               const RuntimeMesh& to, float amount) {
      RuntimeMesh out = from;
      const float t = std::clamp(amount, 0.0f, 1.0f);
      if (from.verts.size() != to.verts.size()) return out;
      for (size_t i = 0; i < out.verts.size(); ++i) {
        out.verts[i].r += (to.verts[i].r - from.verts[i].r) * t;
        out.verts[i].g += (to.verts[i].g - from.verts[i].g) * t;
        out.verts[i].b += (to.verts[i].b - from.verts[i].b) * t;
        out.verts[i].a += (to.verts[i].a - from.verts[i].a) * t;
      }
      for (size_t i = 0; i < out.material_color.size(); ++i) {
        out.material_color[i] +=
            (to.material_color[i] - from.material_color[i]) * t;
      }
      return out;
    };
    auto blend_line_color = [](const RuntimeLineMaterial& from,
                               const RuntimeLineMaterial& to, float amount) {
      RuntimeLineMaterial out = from;
      const float t = std::clamp(amount, 0.0f, 1.0f);
      for (size_t i = 0; i < out.color.size(); ++i) {
        out.color[i] += (to.color[i] - from.color[i]) * t;
      }
      return out;
    };
    for (size_t note_index = 0; note_index < notes.size(); ++note_index) {
      if (consumed_notes && note_index < consumed_notes->size() &&
          (*consumed_notes)[note_index]) {
        continue;
      }
      const auto& n = notes[note_index];
      const double on = chart.tick_to_sec(n.tick_on), off = chart.tick_to_sec(n.tick_off);
      if (off < song_time - trail) continue;
      if (on > song_time + lead) break;
      if (n.tick_off <= n.tick_on + sustain_min) continue;
      const int lane = std::clamp(n.lane, 0, 4);
      const bool bonus = bonus_highway_active;
      const bool star = !bonus && n.star_power;
      const RuntimeMesh* body =
          bonus && bonus_tail_mesh_.ok
              ? &bonus_tail_mesh_
              : (tail_mesh_[lane].ok ? &tail_mesh_[lane] : nullptr);
      draw_tail_segment(lane, on, off,
                        bonus ? "incoming_bonus_body" : "incoming_lane_body",
                        body, raw_tail, tail_glow_width_,
                        body ? D3DCOLOR_ARGB(255, 255, 255, 255)
                             : slot_lane_colors_[lane],
                        false, star, false);

      const RuntimeLineMaterial* glow =
          bonus ? (bonus_tail_line_material_.ok
                       ? &bonus_tail_line_material_ : nullptr)
                : (star ? (held_tight_tail_line_material_.ok
                               ? &held_tight_tail_line_material_ : nullptr)
                        : (held_tail_line_material_[lane].ok
                               ? &held_tail_line_material_[lane] : nullptr));
      const float glow_width = star ? tail_glow_tight_width_
                                    : tail_glow_width_;
      if (!draw_tail_line_segment(
              lane, on, off,
              bonus ? "incoming_bonus_glow"
                    : (star ? "incoming_star_tight_glow"
                            : "incoming_lane_glow"),
              glow, glow_width, D3DCOLOR_ARGB(255, 255, 255, 255),
              false, star, false)) {
        const RuntimeMesh* glow_mesh =
            bonus ? (bonus_glow_tail_mesh_.ok
                         ? &bonus_glow_tail_mesh_ : nullptr)
                  : (star ? (held_tight_tail_mesh_.ok
                                 ? &held_tight_tail_mesh_ : nullptr)
                          : (held_tail_mesh_[lane].ok
                                 ? &held_tail_mesh_[lane] : nullptr));
        draw_tail_segment(lane, on, off,
                          star ? "incoming_star_tight_glow_mesh"
                               : "incoming_glow_mesh",
                          glow_mesh, held_tail, glow_width,
                          D3DCOLOR_ARGB(255, 255, 255, 255),
                          false, star, false);
      }
    }
    if (active_sustains) {
      for (const auto& sustain : *active_sustains) {
        const uint64_t sustain_key =
            sustain.source_index != static_cast<size_t>(-1)
                ? static_cast<uint64_t>(sustain.source_index)
                : (UINT64_C(1) << 63) |
                      static_cast<uint64_t>(sustain.source_tick);
        const auto visual_it = sustain_visual_states_.find(sustain_key);
        const SustainVisualState* visual =
            visual_it != sustain_visual_states_.end() ? &visual_it->second
                                                      : nullptr;
        for (int lane = 0; lane < 5; ++lane) {
          if ((sustain.mask & (1u << lane)) == 0) continue;
          const bool retail_bonus = bonus_highway_active;
          const bool retail_star =
              !retail_bonus && sustain.star_power_tail;
          const bool retail_whammy = whammy_active;

          const RuntimeMesh* lane_body =
              tail_mesh_[lane].ok ? &tail_mesh_[lane] : nullptr;
          RuntimeMesh blended_star_body;
          const RuntimeMesh* retail_body =
              retail_bonus && bonus_tail_mesh_.ok ? &bonus_tail_mesh_
                                                  : lane_body;
          if (retail_star && retail_body && star_phrase_tail_mesh_.ok &&
              whammy_envelope_ > 0.0f) {
            blended_star_body = blend_mesh_color(
                *retail_body, star_phrase_tail_mesh_, whammy_envelope_);
            retail_body = &blended_star_body;
          }
          draw_tail_segment(
              lane, sustain.start_time, sustain.end_time,
              retail_bonus ? "held_bonus_body" : "held_lane_body",
              retail_body, raw_tail, tail_glow_width_,
              retail_body ? D3DCOLOR_ARGB(255, 255, 255, 255)
                          : slot_lane_colors_[lane],
              true, retail_star, retail_whammy,
              TailWidthProfileKind::kNone, 0.0f, visual);

          RuntimeLineMaterial blended_star_glow;
          const RuntimeLineMaterial* retail_glow = nullptr;
          if (retail_bonus) {
            retail_glow = bonus_tail_line_material_.ok
                              ? &bonus_tail_line_material_ : nullptr;
          } else if (retail_star) {
            retail_glow = star_tail_line_material_.ok
                              ? &star_tail_line_material_ : nullptr;
            if (retail_glow && whammy_tail_line_material_.ok &&
                whammy_envelope_ > 0.0f) {
              blended_star_glow = blend_line_color(
                  *retail_glow, whammy_tail_line_material_,
                  whammy_envelope_);
              retail_glow = &blended_star_glow;
            }
          } else {
            retail_glow = held_tail_line_material_[lane].ok
                              ? &held_tail_line_material_[lane] : nullptr;
          }
          const float retail_glow_width =
              tail_glow_width_ *
              (visual ? visual->glow_width_scale : 1.0f);
          if (!draw_tail_line_segment(
                  lane, sustain.start_time, sustain.end_time,
                  retail_bonus
                      ? "held_bonus_glow"
                      : (retail_star ? "held_star_glow"
                                     : "held_lane_glow"),
                  retail_glow, retail_glow_width,
                  D3DCOLOR_ARGB(255, 255, 255, 255), true, retail_star,
                  retail_whammy)) {
            const RuntimeMesh* retail_glow_mesh =
                retail_bonus
                    ? (bonus_glow_tail_mesh_.ok ? &bonus_glow_tail_mesh_
                                                : nullptr)
                    : (retail_star
                           ? (star_tail_mesh_.ok ? &star_tail_mesh_ : nullptr)
                           : (held_tail_mesh_[lane].ok
                                  ? &held_tail_mesh_[lane] : nullptr));
            draw_tail_segment(
                lane, sustain.start_time, sustain.end_time,
                "held_glow_mesh", retail_glow_mesh, held_tail,
                tail_glow_width_, D3DCOLOR_ARGB(255, 255, 255, 255),
                true, retail_star, retail_whammy,
                TailWidthProfileKind::kNone, 0.0f, visual);
          }

          // The decoded 0/10/20 values select frames on
          // burn_tail_state.view. They are not Y translations.
          const float retail_burn_y = kStrikeY;
          const float retail_burn_frame =
              retail_bonus ? burn_bonus_frame_
                           : (retail_whammy ? burn_whammy_frame_
                                            : burn_normal_frame_);
          const bool retail_draw_burn =
              !tail_layer_only || *tail_layer_only == "burn";
          if (retail_draw_burn && !burn_tail_particles_.empty()) {
            draw_runtime_particles(burn_tail_particles_, lane_x(lane),
                                   retail_burn_y, song_time, 1.0f, false,
                                   highway_root.x_scale, false, 0.0f, 0.0f,
                                   retail_burn_frame);
          }
          if (retail_draw_burn && burn_castlight_mesh_.ok) {
            const HighwayBlendState burn_blend_state =
                highway_blend_state_for(burn_castlight_mesh_.blend);
            DWORD previous_src = D3DBLEND_SRCALPHA;
            DWORD previous_dest = D3DBLEND_INVSRCALPHA;
            DWORD previous_op = D3DBLENDOP_ADD;
            dev_->GetRenderState(D3DRS_SRCBLEND, &previous_src);
            dev_->GetRenderState(D3DRS_DESTBLEND, &previous_dest);
            dev_->GetRenderState(D3DRS_BLENDOP, &previous_op);
            dev_->SetRenderState(D3DRS_BLENDOP, burn_blend_state.op);
            dev_->SetRenderState(D3DRS_SRCBLEND, burn_blend_state.src);
            dev_->SetRenderState(D3DRS_DESTBLEND, burn_blend_state.dest);
            const SideRailColorState burn_castlight_color =
                sample_color_anim(burn_castlight_color_anim_,
                                  retail_burn_frame);
            draw_authored_root_mesh(
                burn_castlight_mesh_, lane_x(lane), retail_burn_y,
                side_rail_d3d_color(burn_castlight_color), 1.0f, true, 0.0f,
                false, 0.0f, true);
            dev_->SetRenderState(D3DRS_BLENDOP, previous_op);
            dev_->SetRenderState(D3DRS_SRCBLEND, previous_src);
            dev_->SetRenderState(D3DRS_DESTBLEND, previous_dest);
          }
          continue;

        }
      }
    }
  }

  // --- 5) Fret-target rings at the strikeline (additive) ---
  if (!env_enabled("GHOGX_DISABLE_HIGHWAY_RINGS")) {
    const bool drew_native_smashers =
        !env_enabled("GHOGX_DISABLE_HIGHWAY_NATIVE_SMASHERS") &&
        gem_smasher_mesh_.ok;
    if (drew_native_smashers) {
      const bool debug_smashers =
          env_enabled("GHOGX_DEBUG_HIGHWAY_SMASHERS");
      static int smasher_idle_debug_budget = 0;
      static int smasher_active_debug_budget = 0;
      for (int lane = 0; lane < 5; ++lane) {
        const double source_pop_time =
            kTrackIntroFirstSmasherSeconds +
            static_cast<double>(lane) * kTrackIntroSmasherStepSeconds;
        // TrackPanel::do_extend_sequence leaves all five hollow rims present,
        // then pop_smasher/set_smasher_glowing initializes the colored inner
        // buttons one lane at a time.  refresh_track_buttons returns them to
        // live held/idle state at 2.5 seconds.
        const bool intro_lane_initialized =
            !track_intro_active || track_intro_elapsed >= source_pop_time;
        const bool intro_lane_glowing =
            track_intro_active && intro_lane_initialized &&
            track_intro_elapsed < kTrackIntroRefreshButtonsSeconds;
        const bool held = (fret_held_mask >> lane) & 1;
        const float press_flash =
            hit_flash ? std::clamp(hit_flash[lane], 0.0f, 1.0f) : 0.0f;
        const float press = std::max(held ? 1.0f : 0.0f, press_flash);
        const bool smasher_pressed = press > 0.01f;
        const bool smasher_glowing = smasher_pressed || intro_lane_glowing;
        float smasher_lift_z = 0.0f;
        if (smasher_pressed && !mesh_transform_anim_empty(smasher_press_anim_)) {
          const MeshTransformSample hit_transform = sample_transform_anim_delta(
              smasher_press_anim_, smasher_press_anim_duration_frames_,
              std::clamp(press, 0.0f, 1.0f));
          if (hit_transform.has_translation &&
              std::isfinite(hit_transform.translation[2])) {
            smasher_lift_z = std::max(0.0f, hit_transform.translation[2]);
          }
        }
        const float smasher_top_z = kSmasherBodyTopZ + smasher_lift_z;
        const float smasher_z_offset =
            smasher_top_z - gem_smasher_mesh_.max_z;
        const float rim_z_offset =
            kSmasherFixedRingTopZ - smasher_rim_mesh_.max_z;
        const float shadow_z_offset =
            (kBoardZ + 0.03f) - smasher_shadow_mesh_.max_z;
        const float x = lane_x(lane);
        const D3DCOLOR base = smasher_glowing
                                  ? D3DCOLOR_ARGB(255, 255, 255, 255)
                                  : D3DCOLOR_ARGB(240, 255, 255, 255);
        if (smasher_shadow_mesh_.ok) {
          draw_centered_root_mesh(
              smasher_shadow_mesh_, x, kStrikeY,
              D3DCOLOR_ARGB(135, 255, 255, 255), 1.0f, true,
              shadow_z_offset, false, 0.0f);
        }
        const std::string& pressed_smasher_texture =
            bonus_highway_active && !bonus_smasher_texture_name_.empty()
                ? bonus_smasher_texture_name_
                : smasher_texture_names_[lane];
        const std::string& idle_smasher_texture =
            !smasher_normal_texture_name_.empty()
                ? smasher_normal_texture_name_
                : smasher_texture_names_[lane];
        const std::string& smasher_texture =
            smasher_glowing ? pressed_smasher_texture : idle_smasher_texture;
        std::string ring_texture =
            bonus_highway_active && !bonus_smasher_ring_texture_name_.empty()
                ? bonus_smasher_ring_texture_name_
                : smasher_ring_texture_names_[lane];
        const RuntimeMesh* ring_mesh = &smasher_rim_meshes_[lane];
        const RuntimeMesh* ring_add_mesh = &smasher_ring_add_meshes_[lane];
        if (bonus_highway_active && bonus_smasher_rim_mesh_.ok) {
          ring_mesh = &bonus_smasher_rim_mesh_;
        }
        if (bonus_highway_active && bonus_smasher_ring_add_mesh_.ok) {
          ring_add_mesh = &bonus_smasher_ring_add_mesh_;
        }
        const bool ring_add_alpha_key_draw =
            ring_add_mesh && ring_add_mesh->ok &&
            ring_add_mesh->blend == kHighwayBlendSrcAlpha &&
            is_smasher_ring_add_alpha_key_alias(ring_add_mesh->texture_name);
        const bool ring_add_draw =
            ring_add_mesh && ring_add_mesh->ok &&
            (highway_blend_state_for(ring_add_mesh->blend).additive ||
             ring_add_alpha_key_draw);
        if (!ring_mesh->ok) ring_mesh = &smasher_rim_mesh_;
        if (ring_texture.empty()) ring_texture = ring_mesh->texture_name;
        const RuntimeMesh* smasher_add_mesh = &smasher_add_meshes_[lane];
        if (bonus_highway_active && bonus_smasher_add_mesh_.ok) {
          smasher_add_mesh = &bonus_smasher_add_mesh_;
        }
        const bool smasher_add_mesh_ok =
            smasher_add_mesh && smasher_add_mesh->ok;
        const std::string& fallback_smasher_add_texture =
            bonus_highway_active && !bonus_smasher_add_texture_name_.empty()
                ? bonus_smasher_add_texture_name_
                : smasher_add_texture_names_[lane];
        const uint8_t fallback_smasher_add_blend =
            bonus_highway_active && !bonus_smasher_add_texture_name_.empty()
                ? bonus_smasher_add_blend_
                : smasher_add_blends_[lane];
        const std::string& smasher_add_texture =
            smasher_add_mesh_ok ? smasher_add_mesh->texture_name
                                : fallback_smasher_add_texture;
        const uint8_t smasher_add_blend =
            smasher_add_mesh_ok ? smasher_add_mesh->blend
                                : fallback_smasher_add_blend;
        const bool smasher_add_rgb_mask =
            is_smasher_add_alpha_key_alias(smasher_add_texture);
        const bool log_smasher =
            debug_smashers &&
            ((smasher_glowing && smasher_active_debug_budget < 120) ||
             (!smasher_glowing && smasher_idle_debug_budget < 30));
        if (log_smasher) {
          std::fprintf(
              stderr,
              "[highway-smasher] lane=%d initialized=%d intro_glow=%d "
              "held=%d flash=%.3f press=%.3f "
              "body_top=%.3f lift_z=%.3f "
              "ring_top=%.3f body_mesh=%d ring_mesh=%d ring_add=%d "
              "ring_add_blend=%u ring_add_draw=%d shadow=%d "
              "body_tex=%s add_mesh=%d add_tex=%s add_blend=%u "
              "add_zmode=%u add_rgbmask=%d "
              "ring_tex=%s ring_add_tex=%s bonus=%d\n",
              lane, intro_lane_initialized ? 1 : 0,
              intro_lane_glowing ? 1 : 0, held ? 1 : 0, press_flash, press,
              smasher_top_z,
              smasher_lift_z, kSmasherFixedRingTopZ,
              gem_smasher_mesh_.ok ? 1 : 0,
              ring_mesh->ok ? 1 : 0,
              ring_add_mesh && ring_add_mesh->ok ? 1 : 0,
              ring_add_mesh && ring_add_mesh->ok
                  ? static_cast<unsigned>(ring_add_mesh->blend)
                  : 0u,
              ring_add_draw ? 1 : 0,
              smasher_shadow_mesh_.ok ? 1 : 0,
              smasher_texture.empty() ? "<mesh>" : smasher_texture.c_str(),
              smasher_add_mesh_ok ? 1 : 0,
              smasher_add_texture.empty() ? "<none>"
                                           : smasher_add_texture.c_str(),
              static_cast<unsigned>(smasher_add_blend),
              smasher_add_mesh_ok
                  ? static_cast<unsigned>(smasher_add_mesh->z_mode)
                  : static_cast<unsigned>(gem_smasher_mesh_.z_mode),
              smasher_add_rgb_mask ? 1 : 0,
              ring_texture.empty() ? "<mesh>" : ring_texture.c_str(),
              ring_add_mesh && ring_add_mesh->ok
                  ? ring_add_mesh->texture_name.c_str()
                  : "<none>",
              bonus_highway_active ? 1 : 0);
          if (smasher_glowing) {
            ++smasher_active_debug_budget;
          } else {
            ++smasher_idle_debug_budget;
          }
        }
        auto draw_smasher_solid_mesh =
            [&](const RuntimeMesh& mesh, bool write_depth, auto&& draw_mesh) {
              DWORD prev_z_enable = FALSE;
              DWORD prev_z_write = FALSE;
              DWORD prev_z_func = D3DCMP_LESSEQUAL;
              DWORD prev_cull_mode = D3DCULL_NONE;
              dev_->GetRenderState(D3DRS_ZENABLE, &prev_z_enable);
              dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_z_write);
              dev_->GetRenderState(D3DRS_ZFUNC, &prev_z_func);
              dev_->GetRenderState(D3DRS_CULLMODE, &prev_cull_mode);
              dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
              dev_->SetRenderState(D3DRS_ZWRITEENABLE,
                                   write_depth ? TRUE : FALSE);
              dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
              dev_->SetRenderState(D3DRS_CULLMODE,
                                   highway_source_cull_mode(mesh.cull));
              draw_mesh();
              dev_->SetRenderState(D3DRS_CULLMODE, prev_cull_mode);
              dev_->SetRenderState(D3DRS_ZFUNC, prev_z_func);
              dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_z_write);
              dev_->SetRenderState(D3DRS_ZENABLE, prev_z_enable);
            };
        auto draw_smasher_body = [&]() {
          draw_smasher_solid_mesh(gem_smasher_mesh_, true, [&]() {
            if (!smasher_texture.empty()) {
              draw_centered_root_mesh_with_texture(
                  gem_smasher_mesh_, smasher_texture, x, kStrikeY, base, 1.0f,
                  true, smasher_z_offset, true, kSmasherClipZ);
            } else {
              draw_centered_root_mesh(
                  gem_smasher_mesh_, x, kStrikeY, base, 1.0f, true,
                  smasher_z_offset, true, kSmasherClipZ);
            }
          });
        };
        auto draw_smasher_ring = [&]() {
          if (!ring_mesh->ok) return;
          if (ring_mesh != &smasher_rim_mesh_ || ring_texture.empty()) {
            draw_centered_root_mesh(
                *ring_mesh, x, kStrikeY,
                D3DCOLOR_ARGB(235, 255, 255, 255), 1.0f, true,
                rim_z_offset, false, 0.0f);
          } else {
            draw_centered_root_mesh_with_texture(
                *ring_mesh, ring_texture, x, kStrikeY,
                D3DCOLOR_ARGB(235, 255, 255, 255), 1.0f, true, rim_z_offset);
          }
        };
        auto draw_smasher_ring_add = [&]() {
          if (!ring_add_draw) return;
          const float ring_add_z_offset =
              kSmasherFixedRingTopZ - ring_add_mesh->max_z;
          const HighwayBlendState ring_add_blend_state =
              highway_blend_state_for(ring_add_mesh->blend);
          DWORD prev_ring_src_blend = D3DBLEND_SRCALPHA;
          DWORD prev_ring_dest_blend = D3DBLEND_INVSRCALPHA;
          DWORD prev_ring_blend_op = D3DBLENDOP_ADD;
          dev_->GetRenderState(D3DRS_SRCBLEND, &prev_ring_src_blend);
          dev_->GetRenderState(D3DRS_DESTBLEND, &prev_ring_dest_blend);
          dev_->GetRenderState(D3DRS_BLENDOP, &prev_ring_blend_op);
          dev_->SetRenderState(D3DRS_BLENDOP, ring_add_blend_state.op);
          dev_->SetRenderState(D3DRS_SRCBLEND, ring_add_blend_state.src);
          dev_->SetRenderState(D3DRS_DESTBLEND, ring_add_blend_state.dest);
          draw_centered_root_mesh(
              *ring_add_mesh, x, kStrikeY,
              D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, true,
              ring_add_z_offset, false, 0.0f);
          dev_->SetRenderState(D3DRS_BLENDOP, prev_ring_blend_op);
          dev_->SetRenderState(D3DRS_SRCBLEND, prev_ring_src_blend);
          dev_->SetRenderState(D3DRS_DESTBLEND, prev_ring_dest_blend);
        };
        auto draw_smasher_add = [&]() {
          if (!smasher_glowing || smasher_add_texture.empty()) return;
          const RuntimeMesh& add_mesh =
              smasher_add_mesh_ok ? *smasher_add_mesh : gem_smasher_mesh_;
          const float add_z_offset = smasher_top_z - add_mesh.max_z;
          const HighwayBlendState smasher_add_blend_state =
              highway_blend_state_for(smasher_add_blend);
          const bool disable_add_z_write =
              !highway_z_mode_writes(add_mesh.z_mode) ||
              smasher_add_blend_state.additive ||
              smasher_add_blend == kHighwayBlendSubtract ||
              smasher_add_blend == kHighwayBlendMultiply ||
              add_mesh.material_color[3] < 0.999f;
          DWORD prev_add_src_blend = D3DBLEND_SRCALPHA;
          DWORD prev_add_dest_blend = D3DBLEND_INVSRCALPHA;
          DWORD prev_add_blend_op = D3DBLENDOP_ADD;
          DWORD prev_add_z_enable = FALSE;
          DWORD prev_add_z_write = FALSE;
          DWORD prev_add_z_func = D3DCMP_LESSEQUAL;
          DWORD prev_add_cull_mode = D3DCULL_NONE;
          dev_->GetRenderState(D3DRS_SRCBLEND, &prev_add_src_blend);
          dev_->GetRenderState(D3DRS_DESTBLEND, &prev_add_dest_blend);
          dev_->GetRenderState(D3DRS_BLENDOP, &prev_add_blend_op);
          dev_->GetRenderState(D3DRS_ZENABLE, &prev_add_z_enable);
          dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_add_z_write);
          dev_->GetRenderState(D3DRS_ZFUNC, &prev_add_z_func);
          dev_->GetRenderState(D3DRS_CULLMODE, &prev_add_cull_mode);
          dev_->SetRenderState(D3DRS_BLENDOP, smasher_add_blend_state.op);
          dev_->SetRenderState(D3DRS_SRCBLEND, smasher_add_blend_state.src);
          dev_->SetRenderState(D3DRS_DESTBLEND, smasher_add_blend_state.dest);
          dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE,
                               disable_add_z_write ? FALSE : TRUE);
          dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
          dev_->SetRenderState(D3DRS_CULLMODE,
                               highway_source_cull_mode(add_mesh.cull));
          if (smasher_add_mesh_ok) {
            draw_centered_root_mesh(
                add_mesh, x, kStrikeY, D3DCOLOR_ARGB(255, 255, 255, 255),
                1.0f, true, add_z_offset, true, kSmasherClipZ);
          } else {
            draw_centered_root_mesh_with_texture(
                add_mesh, smasher_add_texture, x, kStrikeY,
                D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, true,
                add_z_offset, true, kSmasherClipZ);
          }
          dev_->SetRenderState(D3DRS_CULLMODE, prev_add_cull_mode);
          dev_->SetRenderState(D3DRS_ZFUNC, prev_add_z_func);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_add_z_write);
          dev_->SetRenderState(D3DRS_ZENABLE, prev_add_z_enable);
          dev_->SetRenderState(D3DRS_BLENDOP, prev_add_blend_op);
          dev_->SetRenderState(D3DRS_SRCBLEND, prev_add_src_blend);
          dev_->SetRenderState(D3DRS_DESTBLEND, prev_add_dest_blend);
        };
        if (!intro_lane_initialized) {
          draw_smasher_ring();
        } else if (smasher_glowing) {
          draw_smasher_ring();
          draw_smasher_body();
          draw_smasher_ring_add();
          draw_smasher_add();
        } else {
          draw_smasher_body();
          draw_smasher_ring();
        }
      }
    }
    if (!drew_native_smashers) {
      dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      for (int lane = 0; lane < 5; ++lane) {
        const bool held = (fret_held_mask >> lane) & 1;
        IDirect3DTexture9* ring =
            tex(lane_texture_name("now_", slot_color_names_[lane], "_add.tex"));
        if (!ring) ring = tex("now_ring_add.tex");
        const int b = held ? 255 : 150;
        V3 q[4];
        flat_quad(q, lane_x(lane), kStrikeY, kGemZ,
                  highway_root.scale_x(kGemHalf * 1.5f),
                  kGemHalf * 1.5f, D3DCOLOR_ARGB(255, b, b, b));
        draw_quad(dev_, ring, q);
      }
      dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
  }

  // --- 6) Active sustain held caps at the strikeline ---
  // The source-backed default is the grounded gem_smasher body-only inner cap.
  // The larger moving-note/gem-top fallback remains diagnostic until the exact
  // tail cap owner is recovered.
  if (active_sustains && !bonus_highway_active &&
      !env_enabled("GHOGX_DISABLE_HIGHWAY_GEMS") &&
      !env_enabled("GHOGX_DISABLE_HIGHWAY_ACTIVE_SUSTAIN_CAPS")) {
    DWORD prev_z_enable = FALSE;
    DWORD prev_z_write = FALSE;
    DWORD prev_z_func = D3DCMP_LESSEQUAL;
    DWORD prev_cull_mode = D3DCULL_NONE;
    dev_->GetRenderState(D3DRS_ZENABLE, &prev_z_enable);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_z_write);
    dev_->GetRenderState(D3DRS_ZFUNC, &prev_z_func);
    dev_->GetRenderState(D3DRS_CULLMODE, &prev_cull_mode);
    dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev_->SetRenderState(D3DRS_CULLMODE, highway_note_cull_mode());

    const bool debug_active_sustain_caps =
        env_enabled("GHOGX_DEBUG_HIGHWAY_ACTIVE_SUSTAIN_CAPS") ||
        env_enabled("GHOGX_DEBUG_HIGHWAY_NOTE_DRAW");
    static int active_sustain_cap_debug_budget = 0;
    constexpr int kActiveSustainCapDebugBudget = 120;
    const D3DCOLOR active_cap_tint = D3DCOLOR_ARGB(255, 255, 255, 255);
    auto draw_active_sustain_cap_layer_with_state =
        [&](const RuntimeMesh& mesh, auto&& draw_mesh) {
          if (!mesh.ok) return;
          const HighwayBlendState blend_state =
              highway_blend_state_for(mesh.blend);
          DWORD prev_layer_z_write = FALSE;
          DWORD prev_src_blend = D3DBLEND_SRCALPHA;
          DWORD prev_dest_blend = D3DBLEND_INVSRCALPHA;
          DWORD prev_blend_op = D3DBLENDOP_ADD;
          DWORD prev_alpha_test = FALSE;
          DWORD prev_alpha_func = D3DCMP_ALWAYS;
          DWORD prev_alpha_ref = 0;
          dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_layer_z_write);
          dev_->GetRenderState(D3DRS_SRCBLEND, &prev_src_blend);
          dev_->GetRenderState(D3DRS_DESTBLEND, &prev_dest_blend);
          dev_->GetRenderState(D3DRS_BLENDOP, &prev_blend_op);
          dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prev_alpha_test);
          dev_->GetRenderState(D3DRS_ALPHAFUNC, &prev_alpha_func);
          dev_->GetRenderState(D3DRS_ALPHAREF, &prev_alpha_ref);
          const bool alpha_test_note_card =
              is_note_black_card_tex_name(mesh.texture_name,
                                          slot_color_names_);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
          dev_->SetRenderState(D3DRS_BLENDOP, blend_state.op);
          dev_->SetRenderState(D3DRS_SRCBLEND, blend_state.src);
          dev_->SetRenderState(D3DRS_DESTBLEND, blend_state.dest);
          if (alpha_test_note_card) {
            dev_->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            dev_->SetRenderState(D3DRS_ALPHAREF, kNoteCardAlphaRef);
          }
          draw_mesh();
          dev_->SetRenderState(D3DRS_ALPHAREF, prev_alpha_ref);
          dev_->SetRenderState(D3DRS_ALPHAFUNC, prev_alpha_func);
          dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prev_alpha_test);
          dev_->SetRenderState(D3DRS_BLENDOP, prev_blend_op);
          dev_->SetRenderState(D3DRS_SRCBLEND, prev_src_blend);
          dev_->SetRenderState(D3DRS_DESTBLEND, prev_dest_blend);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_layer_z_write);
        };
    auto draw_active_sustain_cap_layer =
        [&](const RuntimeMesh& mesh, int lane, float origin_y,
            bool use_vertex_color = true) {
          draw_active_sustain_cap_layer_with_state(
              mesh, [&]() {
                draw_authored_root_mesh(mesh, lane_x(lane), origin_y,
                                        active_cap_tint, 1.0f, true, 0.0f,
                                        false, 0.0f, use_vertex_color);
              });
        };
    auto draw_active_sustain_cap_transformed_layer =
        [&](const RuntimeMesh& mesh, int lane, float origin_y,
            const MeshTransformSample& transform,
            bool use_vertex_color = true) {
          draw_active_sustain_cap_layer_with_state(
              mesh, [&]() {
                draw_authored_runtime_mesh_transformed(
                    mesh, lane_x(lane), origin_y, active_cap_tint,
                    root_transform(transform), true, 0.0f, use_vertex_color);
              });
        };
    auto active_star_top_for_lane = [&](int lane) -> const RuntimeMesh* {
      if (moving_note_star_prefers_black_top_ && star_black_top_mesh_.ok) {
        return &star_black_top_mesh_;
      }
      if (star_top_mesh_[lane].ok) return &star_top_mesh_[lane];
      if (star_black_top_mesh_.ok) return &star_black_top_mesh_;
      return nullptr;
    };
    auto add_cap_min_y = [](float& cap_min_local_y,
                            const RuntimeMesh* mesh) {
      if (mesh && mesh->ok) {
        cap_min_local_y = std::min(cap_min_local_y, mesh->min_y);
      }
    };
    const bool draw_star_effect_layers =
        !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_EFFECT_LAYERS");
    const bool draw_star_base_layer =
        draw_star_effect_layers &&
        !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_BASE");
    const bool draw_star_overlay_layer =
        draw_star_effect_layers &&
        !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_OVERLAY");
    const float active_star_frame =
        static_cast<float>(std::max(0.0, song_time) * 30.0);
    const MeshTransformSample active_star_transform =
        sample_transform_anim(star_note_anim_,
                              star_note_anim_duration_frames_,
                              active_star_frame);

    for (const auto& sustain : *active_sustains) {
      for (int lane = 0; lane < 5; ++lane) {
        if ((sustain.mask & (1u << lane)) == 0) continue;
        if (sustain.star_power_tail) {
          const RuntimeMesh* star_top = active_star_top_for_lane(lane);
          if (!(moving_note_star_has_lane_ &&
                !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE") &&
                star_mesh_[lane].ok) &&
              !(moving_note_star_has_base_ && draw_star_base_layer &&
                star_base_mesh_.ok) &&
              !(moving_note_star_has_overlay_ && draw_star_overlay_layer &&
                star_overlay_mesh_.ok) &&
              !(moving_note_star_has_top_ && star_top && star_top->ok &&
                !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP"))) {
            continue;
          }

          float cap_min_local_y = 0.0f;
          if (moving_note_star_has_base_ && draw_star_base_layer) {
            add_cap_min_y(cap_min_local_y, &star_base_mesh_);
          }
          if (moving_note_star_has_lane_ &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE")) {
            add_cap_min_y(cap_min_local_y, &star_mesh_[lane]);
          }
          if (moving_note_star_has_overlay_ && draw_star_overlay_layer) {
            add_cap_min_y(cap_min_local_y, &star_overlay_mesh_);
          }
          if (moving_note_star_has_top_ &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP")) {
            add_cap_min_y(cap_min_local_y, star_top);
          }
          const float active_sustain_cap_y =
              kStrikeY + nowbar_tail_clip_ - cap_min_local_y;
          if (debug_active_sustain_caps &&
              active_sustain_cap_debug_budget <
                  kActiveSustainCapDebugBudget) {
            std::fprintf(
                stderr,
                "[highway-active-sustain-cap] lane=%d tick=%u star=1 "
                "base=%d star_mesh=%d overlay=%d top=%d y=%.3f "
                "min_y=%.3f start=%.3f end=%.3f\n",
                lane, sustain.source_tick,
                (moving_note_star_has_base_ && draw_star_base_layer &&
                 star_base_mesh_.ok)
                    ? 1
                    : 0,
                (moving_note_star_has_lane_ &&
                 !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE") &&
                 star_mesh_[lane].ok)
                    ? 1
                    : 0,
                (moving_note_star_has_overlay_ && draw_star_overlay_layer &&
                 star_overlay_mesh_.ok)
                    ? 1
                    : 0,
                (moving_note_star_has_top_ && star_top && star_top->ok &&
                 !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP"))
                    ? 1
                    : 0,
                active_sustain_cap_y, cap_min_local_y, sustain.start_time,
                sustain.end_time);
            ++active_sustain_cap_debug_budget;
          }
          if (moving_note_star_has_base_ && draw_star_base_layer &&
              star_base_mesh_.ok) {
            if (active_star_transform.has_translation ||
                active_star_transform.has_rotation ||
                active_star_transform.has_scale) {
              draw_active_sustain_cap_transformed_layer(
                  star_base_mesh_, lane, active_sustain_cap_y,
                  active_star_transform);
            } else {
              draw_active_sustain_cap_layer(star_base_mesh_, lane,
                                            active_sustain_cap_y);
            }
          }
          if (moving_note_star_has_lane_ &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE") &&
              star_mesh_[lane].ok) {
            draw_active_sustain_cap_layer(star_mesh_[lane], lane,
                                          active_sustain_cap_y, false);
          }
          if (moving_note_star_has_overlay_ && draw_star_overlay_layer &&
              star_overlay_mesh_.ok) {
            draw_active_sustain_cap_layer(star_overlay_mesh_, lane,
                                          active_sustain_cap_y);
          }
          if (moving_note_star_has_top_ && star_top && star_top->ok &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP")) {
            draw_active_sustain_cap_layer(*star_top, lane,
                                          active_sustain_cap_y);
          }
          continue;
        }

        const bool draw_unproven_note_top_cap =
            env_enabled("GHOGX_EXPERIMENT_HIGHWAY_ACTIVE_SUSTAIN_CAPS") ||
            env_enabled("GHOGX_EXPERIMENT_HIGHWAY_NORMAL_ACTIVE_SUSTAIN_CAPS");
        if (!draw_unproven_note_top_cap) {
          continue;
        }

        if (!gem_mesh_[lane].ok &&
            !(moving_note_standard_has_top_ && gem_top_mesh_.ok)) {
          continue;
        }
        float cap_min_local_y = 0.0f;
        if (gem_mesh_[lane].ok) {
          cap_min_local_y = std::min(cap_min_local_y, gem_mesh_[lane].min_y);
        }
        if (moving_note_standard_has_top_ && gem_top_mesh_.ok) {
          cap_min_local_y = std::min(cap_min_local_y, gem_top_mesh_.min_y);
        }
        if (moving_note_standard_has_glow_ && gem_glow_mesh_.ok) {
          cap_min_local_y = std::min(cap_min_local_y, gem_glow_mesh_.min_y);
        }
        const float active_sustain_cap_y =
            kStrikeY + nowbar_tail_clip_ - cap_min_local_y;
        if (debug_active_sustain_caps &&
            active_sustain_cap_debug_budget <
                kActiveSustainCapDebugBudget) {
          std::fprintf(
              stderr,
              "[highway-active-sustain-cap] lane=%d tick=%u gem=%d top=%d "
              "glow=%d y=%.3f min_y=%.3f start=%.3f end=%.3f\n",
              lane, sustain.source_tick, gem_mesh_[lane].ok ? 1 : 0,
              (moving_note_standard_has_top_ && gem_top_mesh_.ok) ? 1 : 0,
              (moving_note_standard_has_glow_ && gem_glow_mesh_.ok) ? 1 : 0,
              active_sustain_cap_y, cap_min_local_y, sustain.start_time,
              sustain.end_time);
          ++active_sustain_cap_debug_budget;
        }
        if (gem_mesh_[lane].ok) {
          draw_active_sustain_cap_layer(gem_mesh_[lane], lane,
                                        active_sustain_cap_y);
        }
        if (moving_note_standard_has_top_ && gem_top_mesh_.ok) {
          draw_active_sustain_cap_layer(gem_top_mesh_, lane,
                                        active_sustain_cap_y);
        }
        if (moving_note_standard_has_glow_ && gem_glow_mesh_.ok) {
          draw_active_sustain_cap_layer(gem_glow_mesh_, lane,
                                        active_sustain_cap_y);
        }
      }
    }
    dev_->SetRenderState(D3DRS_ZFUNC, prev_z_func);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_z_write);
    dev_->SetRenderState(D3DRS_ZENABLE, prev_z_enable);
    dev_->SetRenderState(D3DRS_CULLMODE, prev_cull_mode);
  }

  // --- 7) Gems (far -> near) ---
  {
    struct VG {
      float y;
      double on_time;
      uint32_t tick;
      int lane;
      int group_gems;
      bool star;
      bool hopo;
      int hopo_tappable;
    };
    std::vector<VG> vis;
    for (size_t note_index = 0; note_index < notes.size();) {
      const uint32_t group_tick = notes[note_index].tick_on;
      size_t group_end = note_index + 1;
      while (group_end < notes.size() &&
             notes[group_end].tick_on == group_tick) {
        ++group_end;
      }

      bool group_consumed = true;
      bool group_star_power = false;
      const int group_gems =
          static_cast<int>(std::max<size_t>(1, group_end - note_index));
      for (size_t i = note_index; i < group_end; ++i) {
        group_star_power = group_star_power || notes[i].star_power;
        if (!consumed_notes || i >= consumed_notes->size() ||
            !(*consumed_notes)[i]) {
          group_consumed = false;
        }
      }
      if (group_consumed) {
        note_index = group_end;
        continue;
      }

      const double on = chart.tick_to_sec(group_tick);
      if (on > song_time + lead) break;
      if (on >= song_time - trail) {
        const int group_hopo_tappable =
            group_gems == 1 ? notes[note_index].hopo_tappable : 0;
        const bool group_hopo =
            group_gems == 1 &&
            (notes[note_index].is_hopo || group_hopo_tappable >= 2);
        for (size_t i = note_index; i < group_end; ++i) {
          if (consumed_notes && i < consumed_notes->size() &&
              (*consumed_notes)[i]) {
            continue;
          }
          vis.push_back({ note_y(on), on, group_tick,
                          std::clamp(notes[i].lane, 0, 4), group_gems,
                          group_star_power, group_hopo,
                          group_hopo_tappable });
        }
      }
      note_index = group_end;
    }
    std::sort(vis.begin(), vis.end(), [](const VG& a, const VG& b){ return a.y > b.y; });
    const bool debug_note_draw =
        env_enabled("GHOGX_DEBUG_HIGHWAY_NOTE_DRAW");
    constexpr int kNoteDrawDebugBudgetPerKind = 180;
    static std::array<int, 4> note_draw_debug_budget_by_kind = {};
    if (env_enabled("GHOGX_DEBUG_HIGHWAY_VISIBLE_NOTES")) {
      const size_t max_log = std::min<size_t>(vis.size(), 16);
      std::fprintf(stderr,
                   "[highway] visible notes t=%.3f count=%zu log=%zu\n",
                   song_time, vis.size(), max_log);
      for (size_t i = 0; i < max_log; ++i) {
        const auto& g = vis[i];
        std::fprintf(stderr,
                     "[highway] visible note %02zu tick=%u lane=%d gems=%d y=%.3f on=%.3f star=%d hopo=%d hopo_tappable=%d\n",
                     i, g.tick, g.lane, g.group_gems, g.y, g.on_time,
                     g.star ? 1 : 0, g.hopo ? 1 : 0,
                     g.hopo_tappable);
      }
    }
    for (const auto& g : vis) {
      const int a = static_cast<int>(255 * depth_fade(g.y));
      const float x = lane_x(g.lane);
      if (!env_enabled("GHOGX_DISABLE_HIGHWAY_GEMS")) {
        DWORD prev_z_enable = FALSE;
        DWORD prev_z_write = FALSE;
        DWORD prev_z_func = D3DCMP_LESSEQUAL;
        DWORD prev_cull_mode = D3DCULL_NONE;
        dev_->GetRenderState(D3DRS_ZENABLE, &prev_z_enable);
        dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_z_write);
        dev_->GetRenderState(D3DRS_ZFUNC, &prev_z_func);
        dev_->GetRenderState(D3DRS_CULLMODE, &prev_cull_mode);
        dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        dev_->SetRenderState(D3DRS_CULLMODE, highway_note_cull_mode());
        const D3DCOLOR tint = D3DCOLOR_ARGB(a, 255, 255, 255);
        auto draw_note_layer_with_state = [&](const RuntimeMesh& mesh,
                                              bool write_depth,
                                              bool depth_test,
                                              bool allow_note_card_alpha_test,
                                              auto&& draw_mesh) {
          if (!mesh.ok) return;
          const HighwayBlendState blend_state =
              highway_blend_state_for(mesh.blend);
          DWORD prev_layer_z_write = FALSE;
          DWORD prev_layer_z_func = D3DCMP_LESSEQUAL;
          DWORD prev_src_blend = D3DBLEND_SRCALPHA;
          DWORD prev_dest_blend = D3DBLEND_INVSRCALPHA;
          DWORD prev_blend_op = D3DBLENDOP_ADD;
          DWORD prev_alpha_test = FALSE;
          DWORD prev_alpha_func = D3DCMP_ALWAYS;
          DWORD prev_alpha_ref = 0;
          dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_layer_z_write);
          dev_->GetRenderState(D3DRS_ZFUNC, &prev_layer_z_func);
          dev_->GetRenderState(D3DRS_SRCBLEND, &prev_src_blend);
          dev_->GetRenderState(D3DRS_DESTBLEND, &prev_dest_blend);
          dev_->GetRenderState(D3DRS_BLENDOP, &prev_blend_op);
          dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prev_alpha_test);
          dev_->GetRenderState(D3DRS_ALPHAFUNC, &prev_alpha_func);
          dev_->GetRenderState(D3DRS_ALPHAREF, &prev_alpha_ref);
          const bool disable_zwrite =
              !highway_z_mode_writes(mesh.z_mode) ||
              blend_state.additive ||
              mesh.blend == kHighwayBlendSubtract ||
              mesh.blend == kHighwayBlendMultiply ||
              mesh.material_color[3] * (static_cast<float>(a) / 255.0f) <
                  0.999f;
          const bool alpha_test_note_card =
              allow_note_card_alpha_test &&
              is_note_black_card_tex_name(mesh.texture_name,
                                          slot_color_names_);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE,
                               (write_depth && !disable_zwrite) ? TRUE : FALSE);
          dev_->SetRenderState(D3DRS_ZFUNC,
                               depth_test ? D3DCMP_LESSEQUAL : D3DCMP_ALWAYS);
          dev_->SetRenderState(D3DRS_BLENDOP, blend_state.op);
          dev_->SetRenderState(D3DRS_SRCBLEND, blend_state.src);
          dev_->SetRenderState(D3DRS_DESTBLEND, blend_state.dest);
          if (alpha_test_note_card) {
            dev_->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            dev_->SetRenderState(D3DRS_ALPHAREF, kNoteCardAlphaRef);
          }
          draw_mesh();
          dev_->SetRenderState(D3DRS_ALPHAREF, prev_alpha_ref);
          dev_->SetRenderState(D3DRS_ALPHAFUNC, prev_alpha_func);
          dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prev_alpha_test);
          dev_->SetRenderState(D3DRS_BLENDOP, prev_blend_op);
          dev_->SetRenderState(D3DRS_SRCBLEND, prev_src_blend);
          dev_->SetRenderState(D3DRS_DESTBLEND, prev_dest_blend);
          dev_->SetRenderState(D3DRS_ZFUNC, prev_layer_z_func);
          dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_layer_z_write);
        };
        auto draw_note_layer = [&](const RuntimeMesh& mesh,
                                   bool write_depth,
                                   bool depth_test = true,
                                   bool clip_to_z_min = false,
                                   float z_min = 0.0f,
                                   bool use_vertex_color = true) {
          draw_note_layer_with_state(
              mesh, write_depth, depth_test, true, [&]() {
                draw_authored_root_mesh(mesh, x, g.y, tint, 1.0f, true,
                                        0.0f, clip_to_z_min, z_min,
                                        use_vertex_color);
              });
        };
        auto draw_transformed_note_layer =
            [&](const RuntimeMesh& mesh, bool write_depth, bool depth_test,
                const MeshTransformSample& transform) {
              draw_note_layer_with_state(
                  mesh, write_depth, depth_test, true, [&]() {
                    draw_authored_runtime_mesh_transformed(
                        mesh, x, g.y, tint, root_transform(transform));
                  });
            };
        auto draw_standard_top_over_body = [&]() {
          if (!moving_note_standard_has_top_ || !gem_top_mesh_.ok) return;
          if (pc_standard_top_mesh_.ok) {
            // The expanded black copies and untouched source cap are packed
            // into one source-textured draw. The HOPO path never enters here.
            draw_note_layer_with_state(
                pc_standard_top_mesh_, false, false, false, [&]() {
                  draw_authored_root_mesh(pc_standard_top_mesh_, x, g.y, tint,
                                          1.0f, true, 0.0f, false, 0.0f,
                                          true);
                });
            return;
          }
          draw_note_layer(gem_top_mesh_, false, false);
        };
        auto draw_hopo_top_over_body = [&]() {
          if (hopo_mesh_[g.lane].ok) {
            // The lane HOPO meshes are authored top-card variants in the same
            // template frame as top.mesh, so draw them as the visible top card
            // rather than letting the body depth buffer reintroduce the black
            // standard rim.
            draw_note_layer(hopo_mesh_[g.lane], false, false);
            return;
          }
          draw_standard_top_over_body();
        };
        auto draw_note_type_top_over_body = [&]() {
          if (g.hopo) {
            draw_hopo_top_over_body();
          } else {
            draw_standard_top_over_body();
          }
        };
        auto log_note_draw = [&](const char* kind) {
          int budget_slot = 0;
          if (std::strcmp(kind, "star") == 0) {
            budget_slot = 1;
          } else if (std::strcmp(kind, "hopo") == 0) {
            budget_slot = 2;
          } else if (std::strcmp(kind, "bonus") == 0) {
            budget_slot = 3;
          }
          if (!debug_note_draw ||
              note_draw_debug_budget_by_kind[budget_slot] >=
                  kNoteDrawDebugBudgetPerKind) {
            return;
          }
          const RuntimeMesh* star_top = nullptr;
          if (moving_note_star_prefers_black_top_ && star_black_top_mesh_.ok) {
            star_top = &star_black_top_mesh_;
          } else if (star_top_mesh_[g.lane].ok) {
            star_top = &star_top_mesh_[g.lane];
          } else if (star_black_top_mesh_.ok) {
            star_top = &star_black_top_mesh_;
          }
          const bool star_effect_layers_enabled =
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_EFFECT_LAYERS");
          const bool star_base_draw =
              !bonus_highway_active && g.star && moving_note_star_has_base_ &&
              star_effect_layers_enabled &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_BASE") &&
              star_base_mesh_.ok;
          const bool star_lane_draw =
              !bonus_highway_active && g.star && moving_note_star_has_lane_ &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE") &&
              star_mesh_[g.lane].ok;
          const bool star_overlay_draw =
              !bonus_highway_active && g.star && moving_note_star_has_overlay_ &&
              star_effect_layers_enabled &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_OVERLAY") &&
              star_overlay_mesh_.ok;
          const bool star_top_draw =
              !bonus_highway_active && g.star && moving_note_star_has_top_ &&
              star_top && star_top->ok &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP");
          const bool star_top_is_black =
              star_top_draw && star_top == &star_black_top_mesh_ &&
              star_black_top_mesh_.ok;
          const bool authored_note_type_top =
              bonus_highway_active || !g.star;
          const bool standard_top_draw =
              authored_note_type_top && !g.hopo && moving_note_standard_has_top_ &&
              gem_top_mesh_.ok;
          const bool hopo_top_draw =
              authored_note_type_top && g.hopo && hopo_mesh_[g.lane].ok;
          const bool hopo_fallback_top_draw =
              authored_note_type_top && g.hopo && !hopo_mesh_[g.lane].ok &&
              moving_note_standard_has_top_ && gem_top_mesh_.ok;
          std::fprintf(stderr,
                       "[highway-note-draw] kind=%s tick=%u lane=%d y=%.3f "
                       "gems=%d star=%d hopo=%d hopo_tappable=%d gem=%d "
                       "top=%d hopo_mesh=%d std_top=%d hopo_top=%d "
                       "hopo_fallback_top=%d star_top=%d star_black_top=%d "
                       "star_base=%d bonus=%d\n",
                       kind, g.tick, g.lane, g.y, g.group_gems,
                       g.star ? 1 : 0, g.hopo ? 1 : 0,
                       g.hopo_tappable,
                       gem_mesh_[g.lane].ok ? 1 : 0,
                       gem_top_mesh_.ok ? 1 : 0,
                       hopo_mesh_[g.lane].ok ? 1 : 0,
                       standard_top_draw ? 1 : 0,
                       hopo_top_draw ? 1 : 0,
                       hopo_fallback_top_draw ? 1 : 0,
                       star_top_draw ? 1 : 0,
                       star_top_is_black ? 1 : 0,
                       star_base_draw ? 1 : 0,
                       bonus_gem_mesh_.ok ? 1 : 0);
          std::fprintf(stderr,
                       "[highway-note-layer] kind=%s tick=%u lane=%d "
                       "std=%d hopo=%d hopo_fb=%d star=%d black=%d\n",
                       kind, g.tick, g.lane,
                       standard_top_draw ? 1 : 0,
                       hopo_top_draw ? 1 : 0,
                       hopo_fallback_top_draw ? 1 : 0,
                       star_top_draw ? 1 : 0,
                       star_top_is_black ? 1 : 0);
          if (g.star && !bonus_highway_active) {
            std::fprintf(stderr,
                         "[highway-star-layer] tick=%u lane=%d base=%d "
                         "lane_mesh=%d overlay=%d top=%d black_top=%d "
                         "anim=%d blend=%u,%u,%u,%u tex=%s,%s,%s,%s\n",
                         g.tick, g.lane,
                         star_base_draw ? 1 : 0,
                         star_lane_draw ? 1 : 0,
                         star_overlay_draw ? 1 : 0,
                         star_top_draw ? 1 : 0,
                         star_top_is_black ? 1 : 0,
                         !mesh_transform_anim_empty(star_note_anim_) ? 1 : 0,
                         star_base_mesh_.ok
                             ? static_cast<unsigned>(star_base_mesh_.blend)
                             : 0u,
                         star_mesh_[g.lane].ok
                             ? static_cast<unsigned>(star_mesh_[g.lane].blend)
                             : 0u,
                         star_overlay_mesh_.ok
                             ? static_cast<unsigned>(star_overlay_mesh_.blend)
                             : 0u,
                         star_top && star_top->ok
                             ? static_cast<unsigned>(star_top->blend)
                             : 0u,
                         star_base_mesh_.ok ? star_base_mesh_.texture_name.c_str()
                                            : "none",
                         star_mesh_[g.lane].ok
                             ? star_mesh_[g.lane].texture_name.c_str()
                             : "none",
                         star_overlay_mesh_.ok
                             ? star_overlay_mesh_.texture_name.c_str()
                             : "none",
                         star_top && star_top->ok
                             ? star_top->texture_name.c_str()
                             : "none");
          }
          ++note_draw_debug_budget_by_kind[budget_slot];
        };
        if (bonus_highway_active && bonus_gem_mesh_.ok) {
          log_note_draw("bonus");
          // Source contract: gem_template.view owns the ordinary body plus
          // top.mesh, while gem_bonus.mesh is the same authored body geometry
          // with the active-star-power material. Replace only the body so the
          // chart's normal-versus-HOPO top remains visible.
          draw_note_layer(bonus_gem_mesh_, true);
          if (bonus_gem_overlay_mesh_.ok) {
            draw_note_layer(bonus_gem_overlay_mesh_, false, false);
          }
          draw_note_type_top_over_body();
          if (bonus_spark1_mesh_.ok || bonus_spark2_mesh_.ok) {
            if (bonus_spark1_mesh_.ok) {
              draw_note_layer(bonus_spark1_mesh_, false, false);
            }
            if (bonus_spark2_mesh_.ok) {
              draw_note_layer(bonus_spark2_mesh_, false, false);
            }
          }
        } else if (g.star && star_mesh_[g.lane].ok) {
          log_note_draw("star");
          const bool draw_star_effect_layers =
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_EFFECT_LAYERS");
          const bool draw_star_base_layer =
              draw_star_effect_layers &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_BASE");
          const bool draw_star_overlay_layer =
              draw_star_effect_layers &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_OVERLAY");
          const float star_frame =
              static_cast<float>(std::max(0.0, song_time) * 30.0);
          const MeshTransformSample star_transform =
              sample_transform_anim(star_note_anim_,
                                    star_note_anim_duration_frames_,
                                    star_frame);
          if (moving_note_star_has_base_ && draw_star_base_layer &&
              star_base_mesh_.ok) {
            if (star_transform.has_translation || star_transform.has_rotation ||
                star_transform.has_scale) {
              draw_transformed_note_layer(star_base_mesh_, false, true,
                                          star_transform);
            } else {
              draw_note_layer(star_base_mesh_, false, true);
            }
          }
          if (moving_note_star_has_lane_ &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_LANE")) {
            // Lane star color is carried by the atlas UVs; the mesh vertex
            // colors tint every lane toward the red template object.
            draw_note_layer(star_mesh_[g.lane], false, true, false, 0.0f,
                            false);
          }
          if (moving_note_star_has_overlay_ && draw_star_overlay_layer &&
              star_overlay_mesh_.ok) {
            draw_note_layer(star_overlay_mesh_, false, true);
          }
          const RuntimeMesh* star_top = nullptr;
          if (moving_note_star_prefers_black_top_ && star_black_top_mesh_.ok) {
            star_top = &star_black_top_mesh_;
          } else if (star_top_mesh_[g.lane].ok) {
            star_top = &star_top_mesh_[g.lane];
          } else if (star_black_top_mesh_.ok) {
            star_top = &star_black_top_mesh_;
          }
          if (moving_note_star_has_top_ && star_top && star_top->ok &&
              !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_TOP")) {
            draw_note_layer(*star_top, false, true);
          }
        } else if (g.hopo &&
                   (gem_mesh_[g.lane].ok || hopo_mesh_[g.lane].ok ||
                    gem_top_mesh_.ok)) {
          log_note_draw("hopo");
          if (gem_mesh_[g.lane].ok) {
            draw_note_layer(gem_mesh_[g.lane], true);
            draw_hopo_top_over_body();
          } else if (hopo_mesh_[g.lane].ok) {
            draw_note_layer(hopo_mesh_[g.lane], true);
          } else {
            draw_standard_top_over_body();
          }
          if (moving_note_standard_has_glow_ && gem_glow_mesh_.ok) {
            draw_note_layer(gem_glow_mesh_, false, false);
          }
        } else if (gem_mesh_[g.lane].ok) {
          log_note_draw("standard");
          draw_note_layer(gem_mesh_[g.lane], true);
          draw_standard_top_over_body();
          if (moving_note_standard_has_glow_ && gem_glow_mesh_.ok) {
            draw_note_layer(gem_glow_mesh_, false, false);
          }
        }
        dev_->SetRenderState(D3DRS_ZFUNC, prev_z_func);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_z_write);
        dev_->SetRenderState(D3DRS_ZENABLE, prev_z_enable);
        dev_->SetRenderState(D3DRS_CULLMODE, prev_cull_mode);
      }
    }
  }

  // --- 7) Miss feedback (native miss gem at the strikeline) ---
  if (!env_enabled("GHOGX_DISABLE_HIGHWAY_MISS_FLASH") &&
      (miss_flash || env_enabled("GHOGX_FORCE_HIGHWAY_MISS_FLASH"))) {
    const bool force_miss = env_enabled("GHOGX_FORCE_HIGHWAY_MISS_FLASH");
    const bool debug_miss_feedback =
        env_enabled("GHOGX_DEBUG_HIGHWAY_MISS_FEEDBACK");
    static int miss_debug_budget = 0;
    for (int lane = 0; lane < 5; ++lane) {
      float f = miss_flash ? std::clamp(miss_flash[lane], 0.0f, 1.0f) : 0.0f;
      if (force_miss && lane == 2) f = std::max(f, 1.0f);
      const float star_f =
          star_miss_flash ? std::clamp(star_miss_flash[lane], 0.0f, 1.0f)
                          : 0.0f;
      if (f <= 0.01f) continue;
      const int a = static_cast<int>(f * 255.0f);
      const bool forced_lane = force_miss && lane == 2;
      const bool star_miss = !forced_lane && star_f > 0.01f;
      const RuntimeMesh* miss_body =
          star_miss && star_miss_mesh_.ok ? &star_miss_mesh_ : &miss_mesh_;
      const RuntimeMesh* miss_top =
          star_miss && star_miss_top_mesh_.ok ? &star_miss_top_mesh_
                                              : &miss_top_mesh_;
      const float scale = forced_lane ? 1.35f : 1.0f + 0.18f * f;
      if (debug_miss_feedback && miss_debug_budget < 80) {
        std::fprintf(stderr,
                     "[highway-miss] lane=%d f=%.3f alpha=%d miss_mesh=%d "
                     "top_mesh=%d star=%d star_mesh=%d star_top=%d "
                     "scale=%.3f forced=%d\n",
                     lane, f, a, miss_body && miss_body->ok ? 1 : 0,
                     miss_top && miss_top->ok ? 1 : 0, star_miss ? 1 : 0,
                     star_miss_mesh_.ok ? 1 : 0,
                     star_miss_top_mesh_.ok ? 1 : 0, scale,
                     forced_lane ? 1 : 0);
        ++miss_debug_budget;
      }
      auto draw_miss_layer = [&](const RuntimeMesh& mesh, int alpha) {
        if (!mesh.ok || alpha <= 0) return;
        const HighwayBlendState blend_state =
            highway_blend_state_for(mesh.blend);
        DWORD prev_z_enable = FALSE;
        DWORD prev_z_write = FALSE;
        DWORD prev_z_func = D3DCMP_ALWAYS;
        DWORD prev_src_blend = D3DBLEND_SRCALPHA;
        DWORD prev_dest_blend = D3DBLEND_INVSRCALPHA;
        DWORD prev_blend_op = D3DBLENDOP_ADD;
        dev_->GetRenderState(D3DRS_ZENABLE, &prev_z_enable);
        dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prev_z_write);
        dev_->GetRenderState(D3DRS_ZFUNC, &prev_z_func);
        dev_->GetRenderState(D3DRS_SRCBLEND, &prev_src_blend);
        dev_->GetRenderState(D3DRS_DESTBLEND, &prev_dest_blend);
        dev_->GetRenderState(D3DRS_BLENDOP, &prev_blend_op);
        dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
        dev_->SetRenderState(D3DRS_BLENDOP, blend_state.op);
        dev_->SetRenderState(D3DRS_SRCBLEND, blend_state.src);
        dev_->SetRenderState(D3DRS_DESTBLEND, blend_state.dest);
        draw_authored_runtime_mesh_scaled(
            mesh, lane_x(lane), kStrikeY,
            D3DCOLOR_ARGB(std::clamp(alpha, 0, 255), 255, 255, 255),
            highway_root.scale_x(scale), scale, scale);
        dev_->SetRenderState(D3DRS_BLENDOP, prev_blend_op);
        dev_->SetRenderState(D3DRS_SRCBLEND, prev_src_blend);
        dev_->SetRenderState(D3DRS_DESTBLEND, prev_dest_blend);
        dev_->SetRenderState(D3DRS_ZFUNC, prev_z_func);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, prev_z_write);
        dev_->SetRenderState(D3DRS_ZENABLE, prev_z_enable);
      };
      if (miss_body && miss_body->ok) {
        draw_miss_layer(*miss_body, a);
      }
      if (miss_top && miss_top->ok) {
        draw_miss_layer(*miss_top, static_cast<int>(a * 0.75f));
      }
    }
  }

  // --- 8) Hit flames (additive) ---
  if (hit_flash) {
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    IDirect3DTexture9* flame = tex("flame_part.tex");
    const bool debug_hit_feedback =
        env_enabled("GHOGX_DEBUG_HIGHWAY_HIT_FEEDBACK");
    const bool disable_hit_flames =
        env_enabled("GHOGX_DISABLE_HIGHWAY_HIT_FLAMES");
    constexpr int kHitDebugBudgetPerPhraseState = 80;
    static std::array<int, 4> hit_debug_budget_by_phrase_state = {};
    const bool force_combo_lightning =
        env_enabled("GHOGX_FORCE_HIGHWAY_COMBO_LIGHTNING");
    auto anim_frame = [](float duration, float intensity) {
      if (!std::isfinite(duration) || duration <= 0.001f) return 0.0f;
      return (1.0f - std::min(1.0f, intensity)) * duration;
    };
    struct SourceTint {
      D3DCOLOR color = D3DCOLOR_ARGB(255, 255, 255, 255);
      bool color_anim_used = false;
    };
    auto source_tint = [&](int alpha, const ColorAnimState& color_anim,
                           float intensity,
                           const RuntimeMesh* mesh_rgb_source = nullptr) {
      SourceTint out;
      if (color_anim.ok) {
        const float color_frame =
            anim_frame(color_anim_last_frame(color_anim), intensity);
        const SideRailColorState color =
            sample_color_anim(color_anim, color_frame);
        if (color.ok) {
          out.color_anim_used = true;
          float rgb[3] = {color.r, color.g, color.b};
          if (!color_anim.has_rgb && mesh_rgb_source &&
              !mesh_rgb_source->verts.empty()) {
            rgb[0] = mesh_rgb_source->verts.front().r;
            rgb[1] = mesh_rgb_source->verts.front().g;
            rgb[2] = mesh_rgb_source->verts.front().b;
          }
          const int source_alpha = std::clamp(
              static_cast<int>(
                  static_cast<float>(std::clamp(alpha, 0, 255)) *
                      (color_anim.has_alpha ? color.a : 1.0f) +
                  0.5f),
              0, 255);
          out.color = D3DCOLOR_ARGB(
              source_alpha,
              std::clamp(static_cast<int>(rgb[0] * 255.0f + 0.5f), 0, 255),
              std::clamp(static_cast<int>(rgb[1] * 255.0f + 0.5f), 0, 255),
              std::clamp(static_cast<int>(rgb[2] * 255.0f + 0.5f), 0, 255));
          return out;
        }
      }
      out.color = D3DCOLOR_ARGB(std::clamp(alpha, 0, 255), 255, 255, 255);
      return out;
    };
    for (int lane = 0; lane < 5; ++lane) {
      const float f = hit_flash[lane];
      if (f <= 0.01f) continue;
      const int a = static_cast<int>(std::min(1.0f, f) * 255);
      const int phrase_state = force_combo_lightning
                                   ? 3
                                   : (hit_phrase_state
                                          ? std::clamp<int>(
                                                hit_phrase_state[lane], 0, 3)
                                          : 0);
      // Retail SlotViews::Hit passes PlayerState::phraseState into the
      // controller stack.  combo1 is enabled for Hitting or Complete; the
      // two combo2/lightning controllers are enabled only for Complete.
      const bool combo1_active = phrase_state == 2 || phrase_state == 3;
      const bool combo2_active = phrase_state == 3;
      int combo_layers = 0;
      if (combo1_active && !smash_combo_particles_.empty() &&
          !env_enabled("GHOGX_DISABLE_HIGHWAY_COMBO_PARTICLES")) {
        draw_runtime_particles(smash_combo_particles_, lane_x(lane), kStrikeY,
                               song_time, f, true, highway_root.x_scale);
      }
      if (combo2_active &&
          !env_enabled("GHOGX_DISABLE_HIGHWAY_COMBO_LIGHTNING")) {
        for (int i = 0; i < static_cast<int>(combo_lightning_mesh_.size());
             ++i) {
          if (!combo_lightning_mesh_[i].ok) continue;
          ++combo_layers;
          const int layer_alpha =
              std::clamp(a - i * 45, 0, 255);
          const SourceTint combo_tint =
              source_tint(layer_alpha, combo_lightning_color_anim_[i], f,
                          &combo_lightning_mesh_[i]);
          if (!mesh_transform_anim_empty(combo_lightning_anim_[i])) {
            const float duration = combo_lightning_anim_duration_frames_[i];
            const float combo_frame = anim_frame(duration, f);
            const MeshTransformSample combo_transform =
                sample_transform_anim_delta(combo_lightning_anim_[i],
                                            duration, combo_frame);
            draw_centered_runtime_mesh_transformed(
                combo_lightning_mesh_[i], lane_x(lane), kStrikeY,
                combo_tint.color, root_transform(combo_transform), true, 0.0f,
                !combo_tint.color_anim_used);
          } else {
            const float layer_scale =
                1.0f + 0.18f * static_cast<float>(i) +
                0.25f * std::min(1.0f, f);
            draw_centered_runtime_mesh_scaled(
                combo_lightning_mesh_[i], lane_x(lane), kStrikeY,
                combo_tint.color,
                highway_root.scale_x(layer_scale), layer_scale, layer_scale);
          }
        }
      }
      const float star_f = star_collect_flash
                               ? std::clamp(star_collect_flash[lane], 0.0f, 1.0f)
                               : 0.0f;
      const auto& hit_particles =
          (bonus_highway_active && !smash_bonus_particles_.empty())
              ? smash_bonus_particles_
              : smash_normal_particles_;
      if (!hit_particles.empty() &&
          !env_enabled("GHOGX_DISABLE_HIGHWAY_HIT_PARTICLES")) {
        draw_runtime_particles(hit_particles, lane_x(lane), kStrikeY,
                               song_time, f, true, highway_root.x_scale);
      }
      if (star_f > 0.01f && !smash_star_particles_.empty() &&
          !env_enabled("GHOGX_DISABLE_HIGHWAY_STAR_HIT_PARTICLES")) {
        draw_runtime_particles(smash_star_particles_, lane_x(lane), kStrikeY,
                               song_time, star_f, true, highway_root.x_scale);
      }
      const MeshTransformAnim* base_flame_anim = nullptr;
      float base_flame_anim_duration = 0.0f;
      const ColorAnimState* base_flame_color_anim = nullptr;
      const RuntimeMesh* base_flame_mesh =
          !disable_hit_flames && bonus_highway_active && bonus_hit_flame_mesh_.ok
              ? (base_flame_anim = &bonus_hit_flame_anim_,
                 base_flame_anim_duration =
                     bonus_hit_flame_anim_duration_frames_,
                 base_flame_color_anim = &bonus_hit_flame_color_anim_,
                 &bonus_hit_flame_mesh_)
              : !disable_hit_flames && hit_flame_mesh_.ok
                    ? (base_flame_anim = &hit_flame_anim_,
                       base_flame_anim_duration =
                           hit_flame_anim_duration_frames_,
                       base_flame_color_anim = &hit_flame_color_anim_,
                       &hit_flame_mesh_)
                    : nullptr;
      const char* base_flame_label = "none";
      if (disable_hit_flames) {
        base_flame_label = "disabled";
      } else if (bonus_highway_active && bonus_hit_flame_mesh_.ok) {
        base_flame_label = "bonus_flame";
      } else if (hit_flame_mesh_.ok) {
        base_flame_label = "hit_flame";
      } else if (flame) {
        base_flame_label = "flat_flame";
      }
      const int star_a =
          static_cast<int>(std::min(1.0f, star_f) * 255.0f);
      const int hit_debug_phrase_slot = std::clamp(phrase_state, 0, 3);
      if (debug_hit_feedback &&
          hit_debug_budget_by_phrase_state[hit_debug_phrase_slot] <
              kHitDebugBudgetPerPhraseState) {
        std::fprintf(stderr,
                     "[highway-hit] lane=%d f=%.3f alpha=%d phrase_state=%d "
                     "combo1=%d combo2=%d combo_forced=%d combo_layers=%d "
                     "base=%s base_mesh=%d "
                     "star_collect=%d "
                     "star_alpha=%d fallback_tex=%d authored_origin=1 "
                     "base_anim=%d base_color_anim=%d star_anim=%d "
                     "star_color_anim=%d combo_mesh=%d/%d/%d "
                     "combo_anim=%d/%d/%d combo_color_anim=%d/%d/%d\n",
                     lane, f, a, phrase_state, combo1_active ? 1 : 0,
                     combo2_active ? 1 : 0,
                     force_combo_lightning ? 1 : 0, combo_layers,
                     base_flame_label,
                     base_flame_mesh ? 1 : 0,
                     (!disable_hit_flames && star_f > 0.01f &&
                      star_collect_flame_mesh_.ok)
                         ? 1
                         : 0,
                     star_a,
                     (!disable_hit_flames && !base_flame_mesh && flame) ? 1 : 0,
                     (base_flame_anim &&
                      !mesh_transform_anim_empty(*base_flame_anim))
                         ? 1
                         : 0,
                     (base_flame_color_anim && base_flame_color_anim->ok) ? 1
                                                                          : 0,
                     !mesh_transform_anim_empty(star_collect_flame_anim_) ? 1
                                                                          : 0,
                     star_collect_flame_color_anim_.ok ? 1 : 0,
                     combo_lightning_mesh_[0].ok ? 1 : 0,
                     combo_lightning_mesh_[1].ok ? 1 : 0,
                     combo_lightning_mesh_[2].ok ? 1 : 0,
                     !mesh_transform_anim_empty(combo_lightning_anim_[0]) ? 1
                                                                          : 0,
                     !mesh_transform_anim_empty(combo_lightning_anim_[1]) ? 1
                                                                          : 0,
                     !mesh_transform_anim_empty(combo_lightning_anim_[2]) ? 1
                                                                          : 0,
                     combo_lightning_color_anim_[0].ok ? 1 : 0,
                     combo_lightning_color_anim_[1].ok ? 1 : 0,
                     combo_lightning_color_anim_[2].ok ? 1 : 0);
        ++hit_debug_budget_by_phrase_state[hit_debug_phrase_slot];
      }
      static int hit_anim_detail_debug_budget = 0;
      auto draw_flame_mesh = [&](const char* label, const RuntimeMesh& mesh,
                                 int alpha,
                                 const MeshTransformAnim& anim,
                                 float anim_duration,
                                 const ColorAnimState& color_anim,
                                 float intensity) {
        const SourceTint tint = source_tint(alpha, color_anim, intensity);
        const bool has_source_anim =
            !mesh_transform_anim_empty(anim) && anim_duration > 0.001f;
        if (has_source_anim) {
          const float frame = anim_frame(anim_duration, intensity);
          const MeshTransformSample transform =
              sample_transform_anim(anim, anim_duration, frame);
          if (debug_hit_feedback && hit_anim_detail_debug_budget < 160) {
            const uint32_t tint_argb = static_cast<uint32_t>(tint.color);
            std::fprintf(stderr,
                         "[highway-hit-anim] lane=%d label=%s intensity=%.3f "
                         "frame=%.3f duration=%.3f "
                         "t=%d %.3f,%.3f,%.3f "
                         "r=%d %.3f,%.3f,%.3f,%.3f "
                         "s=%d %.3f,%.3f,%.3f color_anim=%d "
                         "argb=%08x\n",
                         lane, label ? label : "<null>", intensity, frame,
                         anim_duration, transform.has_translation ? 1 : 0,
                         transform.translation[0], transform.translation[1],
                         transform.translation[2],
                         transform.has_rotation ? 1 : 0,
                         transform.rotation_xyzw[0],
                         transform.rotation_xyzw[1],
                         transform.rotation_xyzw[2],
                         transform.rotation_xyzw[3],
                         transform.has_scale ? 1 : 0,
                         transform.scale[0], transform.scale[1],
                         transform.scale[2], tint.color_anim_used ? 1 : 0,
                         tint_argb);
            ++hit_anim_detail_debug_budget;
          }
          draw_authored_runtime_mesh_transformed(
              mesh, lane_x(lane), kStrikeY, tint.color,
              root_transform(transform), true, 0.0f, !tint.color_anim_used);
          return;
        }
        const float scale = 1.0f + 0.35f * std::min(1.0f, intensity);
        draw_authored_runtime_mesh_scaled(
            mesh, lane_x(lane), kStrikeY, tint.color,
            highway_root.scale_x(scale), scale, scale, true, 0.0f, false,
            0.0f, !tint.color_anim_used);
      };
      if (base_flame_mesh) {
        draw_flame_mesh(base_flame_label, *base_flame_mesh, a,
                        base_flame_anim ? *base_flame_anim
                                        : hit_flame_anim_,
                        base_flame_anim_duration,
                        base_flame_color_anim ? *base_flame_color_anim
                                              : hit_flame_color_anim_,
                        f);
      } else if (!disable_hit_flames && flame) {
        const float sz_x =
            highway_root.scale_x(lane_gem_half_x(lane) * (1.5f + 0.9f * f));
        const float sz_y = lane_gem_half_y(lane) * (1.5f + 0.9f * f);
        V3 q[4]; flat_quad(q, lane_x(lane), kStrikeY, kGemZ + 0.05f, sz_x, sz_y,
                           D3DCOLOR_ARGB(a, 255, 230, 180));
        draw_quad(dev_, flame, q);
      }
      if (!disable_hit_flames && star_f > 0.01f && star_collect_flame_mesh_.ok) {
        draw_flame_mesh("star_collect", star_collect_flame_mesh_, star_a,
                        star_collect_flame_anim_,
                        star_collect_flame_anim_duration_frames_,
                        star_collect_flame_color_anim_, star_f);
      }
    }
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  }

  draw_debug_note_counter_overlay(song_time, chart, difficulty);
  dev_->SetTexture(0, nullptr);
  dev_->EndScene();
  if (have_previous_viewport) dev_->SetViewport(&previous_viewport);
}

}  // namespace ghogx::game
