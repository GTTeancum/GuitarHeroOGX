// engine/src/app/app_main.cpp
//
// ghogx_app — the windowed engine entrypoint (PC dev build).
//
// Opens a D3D9 window and runs the Engine loop against real frame timing. The
// render/present phase hooks drive the window: clear to a colour that animates
// off the engine clock (proving the loop is live and time-driven), then flip.
// Textured-quad rendering + the splash MILO come next; this milestone confirms
// the windowed loop end to end.
//
//   ghogx_app                         interactive window (Esc or close to quit)
//                                     ARK defaults to <current directory>/gen
//   ghogx_app --frames N              run N frames headlessly-ish then exit
//   ghogx_app --ark-dir <dir>         load splash + gameplay from PS2 ARK
//   ghogx_app --hdr <main.hdr> --ark <main_0.ark>
//                                      load splash + gameplay from explicit ARK paths
//   ghogx_app --song <shortname>      which song to play (default: shoutatthedevil)
//   ghogx_app --difficulty <0-3>      chart difficulty (default: 1 = Medium)
//   ghogx_app --require-native-assets disable GH1 raw-layout compatibility
//                                      loaders and require converted GH2 paths
//   ghogx_app --diagnostic-song-start <sec>
//                                      seek deterministic capture to a song time
//   ghogx_app --diagnostic-autoplay    chart-driven native validation input
//   ghogx_app --diagnostic-fret-mask <mask>
//                                      force held fret lanes for capture
//   ghogx_app --diagnostic-guitar-mask <mask>
//                                      force raw guitar bits for capture
//   ghogx_app --diagnostic-guitar-script <sec:mask,...>
//                                      song-time raw guitar mask script
//   ghogx_app --diagnostic-guitar-script-from-chart <start:end[:hit_offset_sec]>
//                                      generate raw script from loaded chart
//   ghogx_app --diagnostic-guitar-script-star-power-at <sec>
//                                      add a real star-power button edge
//   ghogx_app --diagnostic-guitar-script-whammy
//                                      hold whammy on generated star sustains
//   ghogx_app --diagnostic-guitar-script-whammy-sweep
//                                      sweep the proof-only whammy axis while held
//   ghogx_app --debug-note-counter     show note count + next STANDARD/STAR/HOPO
//   ghogx_app --aspect <4:3|16:9>      render aspect preset (default: 4:3)
//   ghogx_app --diagnostic-character <c>
//                                      route guitarist/highway art through character c
//   ghogx_app --diagnostic-character-variant <selection>
//                                      route guitarist through a manifest variant
//   ghogx_app --diagnostic-bass <model>
//                                      route the bassist prop through a selected bass
//   ghogx_app --diagnostic-player-bassist <model>
//                                      use a player-character rig for the bassist slot
//   ghogx_app --diagnostic-performer <role>=<source:model>
//                                      route any performer through an archive-qualified
//                                      character reference without changing gameplay/UI
//   ghogx_app --diagnostic-performer-animation <role>=<source:model>
//                                      animate that role from a separately resolved
//                                      character source for retarget proofs
//   ghogx_app --diagnostic-venue <v>   route capture through another GH2 venue
//   ghogx_app --venue-only             render venue/camera only; suppress HUD,
//                                      highway, props, and performers
//   ghogx_app --diagnostic-character <c>
//   ghogx_app --diagnostic-venue-event <event>
//                                      force one persistent venue event after load
//   ghogx_app --diagnostic-camera-shot <shot>
//                                      pin a decoded regular CamShot for capture
//   ghogx_app --diagnostic-front-camera <role>
//                                      lock a close camera in front of a performer
//   ghogx_app --diagnostic-proof-lighting
//                                      replace venue cues with bright neutral
//                                      performer/instrument proof lighting
//   ghogx_app --diagnostic-camera-path-offset <frames>
//                                      start a forced CamShot at a local path frame
//                                      alias: --diagnostic-camera-path-offset-frames
//   ghogx_app --diagnostic-camera-cycle-shot-frame <frame>
//                                      dispatch source {world cycle_shot} once
//   ghogx_app --diagnostic-camera-iterate-shot-frame <frame>
//                                      dispatch source {world iterate_shot} once
//   ghogx_app --diagnostic-camera-random-seed <seed>
//                                      dispatch source camera_random_seed before
//                                      CameraManager::Randomize
//   ghogx_app --diagnostic-rock <0..1>
//                                      force initial rock meter fill for capture
//   ghogx_app --diagnostic-star-power <0..1>
//                                      force initial star meter fill for capture
//   ghogx_app --hud-test [--hud-score N] [--hud-streak N]
//                         [--hud-multiplier N] [--hud-sp 0..1] [--hud-rock 0..1]
//                         [--hud-star-active]
//                         [--hud-ref-highway]
//                         [--hud-tune <file>]
//                                      (same --hud-* flags force gameplay HUD
//                                       state for diagnostic screenshots)
//   ghogx_app --hud-tune <file>       load saved HUD layout in gameplay/capture
//   ghogx_app --show-window           keep screenshot runs visible/interactive
//   ghogx_app --defer-window-show     reveal only after auto-start loading,
//                                     without taking keyboard focus
//   ghogx_app --mute-audio            keep the audio graph running silently
//   ghogx_app --screenshot-dir <dir> --screenshot-frames <csv>
//                                      capture numbered BMPs in gameplay mode
//   ghogx_app --sparse-screenshots    long-run validation: tick every frame but
//                                      draw/present only requested screenshots

#include "asset/milo_image.h"
#include "ark_v3.h"
#include "catalog.h"
#include "character/char_clip.h"
#include "character/char_facefx.h"
#include "character/char_mesh.h"
#include "character/char_renderer.h"
#include "core/engine.h"
#include "game/gameplay.h"
#include "hud/hud_renderer.h"
#include "milo_scene/milo_scene.h"
#include "render/window_d3d9.h"
#include "render/scene_d3d9.h"
#include "render/milo_scene_renderer.h"
#include "dtb.h"
#include "ui/config_db.h"
#include "ui/menu_font.h"
#include "ui/menu_labels.h"
#include "ui/song_intro_overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using ScreenshotSequence = std::map<uint64_t, std::string>;

struct RenderSize {
  int width = 960;
  int height = 720;
};

bool env_flag(const char* name) {
  char value[16] = {};
  const DWORD len = GetEnvironmentVariableA(name, value,
                                            static_cast<DWORD>(sizeof(value)));
  return len > 0 && (len >= sizeof(value) || std::strcmp(value, "0") != 0);
}

class ScopedTimerResolution {
 public:
  ScopedTimerResolution() {
    active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
  }
  ~ScopedTimerResolution() {
    if (active_) timeEndPeriod(1);
  }

 private:
  bool active_ = false;
};

void wait_for_frame_deadline(
    std::chrono::steady_clock::time_point deadline) {
  // Windows sleep_for commonly returns one scheduler quantum late even with a
  // 1 ms timer period. Sleep most of the interval, then hold the final
  // millisecond locally so the 60 Hz presentation clock is not asset- or
  // render-path dependent.
  constexpr auto kSpinWindow = std::chrono::milliseconds(1);
  const auto now = std::chrono::steady_clock::now();
  if (now + kSpinWindow < deadline) {
    std::this_thread::sleep_until(deadline - kSpinWindow);
  }
  while (std::chrono::steady_clock::now() < deadline) {
  }
}

void wait_for_next_frame_deadline(
    std::chrono::steady_clock::time_point& deadline,
    std::chrono::steady_clock::duration interval) {
  deadline += interval;
  const auto now = std::chrono::steady_clock::now();
  // Recover a single late frame on the next under-budget frame instead of
  // permanently adding its overrun to every later deadline. Resynchronize
  // after a full-frame stall so loading/debugger pauses do not cause a long
  // unpaced catch-up burst.
  if (now > deadline + interval) deadline = now;
  wait_for_frame_deadline(deadline);
}

bool set_render_aspect_preset(const char* text, RenderSize& out) {
  if (!text) return false;
  if (std::strcmp(text, "4:3") == 0) {
    out = RenderSize{960, 720};
    return true;
  }
  if (std::strcmp(text, "16:9") == 0) {
    out = RenderSize{1280, 720};
    return true;
  }
  return false;
}

bool set_render_size(const char* text, RenderSize& out) {
  if (!text) return false;
  int width = 0;
  int height = 0;
  char trailing = '\0';
  if (std::sscanf(text, "%dx%d%c", &width, &height, &trailing) != 2)
    return false;
  if (width < 160 || height < 120 || width > 3840 || height > 2160)
    return false;
  out = RenderSize{width, height};
  return true;
}

// A timed fade-through-black sequence of splash images (boot logos -> title).
// Each non-final slide fades in, holds, fades out; the final slide fades in and
// holds. Which slide and its brightness are a pure function of the engine clock.
struct SplashSequence {
  std::vector<ghogx::asset::Image> slides;
  float fade = 0.6f;  // fade in/out seconds
  float hold = 1.8f;  // full-brightness seconds
  float slide_dur() const { return fade + hold + fade; }

  void eval(float t, int& index, float& brightness) const {
    index = -1;
    brightness = 0.0f;
    if (slides.empty()) return;
    const int n = static_cast<int>(slides.size());
    const float dur = slide_dur();
    const int k = static_cast<int>(t / dur);
    if (k >= n - 1) {  // final slide: fade in, then hold indefinitely
      index = n - 1;
      const float lt = t - static_cast<float>(n - 1) * dur;
      brightness = (lt < fade) ? (lt / fade) : 1.0f;
      return;
    }
    index = k;
    const float lt = t - static_cast<float>(k) * dur;
    if (lt < fade) brightness = lt / fade;             // fade in
    else if (lt < fade + hold) brightness = 1.0f;       // hold
    else brightness = (dur - lt) / fade;                // fade out
  }
};

struct DiagnosticGuitarScriptEvent {
  double song_time = 0.0;
  uint32_t mask = 0;
};

struct DiagnosticChartScriptWindow {
  double start_sec = 0.0;
  double end_sec = 0.0;
  double hit_offset_sec = -(1.0 / 120.0);
  bool whammy_star_sustains = false;
  bool whammy_sweep = false;
  std::optional<double> star_power_at_sec;
};

struct OverlayVertex {
  float x, y, z, rhw;
  D3DCOLOR color;
  float u, v;
};

constexpr DWORD kOverlayFVF =
    D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

IDirect3DTexture9* upload_overlay_texture(IDirect3DDevice9* dev,
                                          const ghogx::asset::Image& img,
                                          bool key_black_alpha) {
  if (!dev || !img.valid()) return nullptr;
  IDirect3DTexture9* texture = nullptr;
  if (FAILED(dev->CreateTexture(static_cast<UINT>(img.width),
                                static_cast<UINT>(img.height), 1, 0,
                                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture,
                                nullptr))) {
    return nullptr;
  }
  D3DLOCKED_RECT locked;
  if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
    texture->Release();
    return nullptr;
  }
  for (int y = 0; y < img.height; ++y) {
    auto* dst = static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch;
    const uint8_t* src =
        img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
    for (int x = 0; x < img.width; ++x) {
      const uint8_t r = src[x * 4 + 0];
      const uint8_t g = src[x * 4 + 1];
      const uint8_t b = src[x * 4 + 2];
      uint8_t a = src[x * 4 + 3];
      if (key_black_alpha && r < 8 && g < 8 && b < 8) a = 0;
      dst[x * 4 + 0] = b;
      dst[x * 4 + 1] = g;
      dst[x * 4 + 2] = r;
      dst[x * 4 + 3] = a;
    }
  }
  texture->UnlockRect(0);
  return texture;
}

void draw_overlay_quad(IDirect3DDevice9* dev, IDirect3DTexture9* texture,
                       float x0, float y0, float x1, float y1,
                       D3DCOLOR color) {
  if (!dev) return;
  const OverlayVertex quad[4] = {
      {x0 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, color, 0.0f, 0.0f},
      {x1 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, color, 1.0f, 0.0f},
      {x0 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, color, 0.0f, 1.0f},
      {x1 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, color, 1.0f, 1.0f},
  };
  dev->SetTexture(0, texture);
  if (texture) {
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  } else {
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  }
  dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(OverlayVertex));
}

std::vector<DiagnosticGuitarScriptEvent> parse_diagnostic_guitar_script(
    const std::string& spec) {
  std::vector<DiagnosticGuitarScriptEvent> events;
  size_t start = 0;
  while (start < spec.size()) {
    const size_t end = spec.find(',', start);
    const std::string token =
        spec.substr(start, end == std::string::npos ? std::string::npos
                                                    : end - start);
    const size_t colon = token.find(':');
    if (colon != std::string::npos) {
      const double song_time =
          std::max(0.0, std::strtod(token.substr(0, colon).c_str(), nullptr));
      const uint32_t mask = static_cast<uint32_t>(
          std::strtoul(token.substr(colon + 1).c_str(), nullptr, 0)) & 0xffu;
      events.push_back(DiagnosticGuitarScriptEvent{song_time, mask});
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  std::sort(events.begin(), events.end(),
            [](const DiagnosticGuitarScriptEvent& a,
               const DiagnosticGuitarScriptEvent& b) {
              return a.song_time < b.song_time;
            });
  return events;
}

std::optional<DiagnosticChartScriptWindow> parse_diagnostic_chart_script_window(
    const std::string& spec) {
  const size_t colon = spec.find(':');
  if (colon == std::string::npos) return std::nullopt;
  const size_t offset_colon = spec.find(':', colon + 1);
  const double start_sec =
      std::max(0.0, std::strtod(spec.substr(0, colon).c_str(), nullptr));
  const double end_sec =
      std::max(start_sec,
               std::strtod(spec.substr(colon + 1,
                                        offset_colon == std::string::npos
                                            ? std::string::npos
                                            : offset_colon - colon - 1)
                                .c_str(),
                            nullptr));
  const double hit_offset_sec =
      offset_colon == std::string::npos
          ? -(1.0 / 120.0)
          : std::strtod(spec.substr(offset_colon + 1).c_str(), nullptr);
  return DiagnosticChartScriptWindow{start_sec, end_sec, hit_offset_sec, false,
                                     false, std::nullopt};
}

bool diagnostic_hide_hud_enabled() {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool enabled =
      _dupenv_s(&value, &len, "GHOGX_HIDE_HUD") == 0 && value && value[0];
  std::free(value);
  return enabled;
#else
  const char* value = std::getenv("GHOGX_HIDE_HUD");
  return value && value[0];
#endif
}

// Engine whose render/present phases drive the window. Plays the splash
// sequence if set; else shows a single loaded image; else an animated
// procedural checkerboard (exercises the texture-upload + sampled-draw path).
class AppEngine : public ghogx::Engine {
 public:
  explicit AppEngine(ghogx::render::Window* win)
      : win_(win),
        scene_(*win),
        pixels_(static_cast<std::size_t>(kTexW) * kTexH * 4),
        song_intro_overlay_(*win) {
    profile_frame_times_ = env_flag("GHOGX_PROFILE_FRAME_TIMES");
  }
  ~AppEngine() override {
    if (fail_overlay_tex_) fail_overlay_tex_->Release();
    if (finish_overlay_tex_) finish_overlay_tex_->Release();
  }

  void log_profile_summary() const {
    if (!profile_frame_times_ || profile_frames_ == 0) return;
    const double inv = 1.0 / static_cast<double>(profile_frames_);
    std::fprintf(
        stderr,
        "[profile] frames=%llu pre=%.3fms render=%.3fms gameplay_draw=%.3fms hud=%.3fms present=%.3fms\n",
        static_cast<unsigned long long>(profile_frames_),
        profile_pre_seconds_ * inv * 1000.0,
        profile_render_seconds_ * inv * 1000.0,
        profile_gameplay_draw_seconds_ * inv * 1000.0,
        profile_hud_seconds_ * inv * 1000.0,
        profile_present_seconds_ * inv * 1000.0);
  }

  // A single static image (shown if no sequence is set).
  void set_image(ghogx::asset::Image img) { image_ = std::move(img); }
  // A timed splash sequence (takes priority over a single image).
  void set_sequence(SplashSequence seq) { seq_ = std::move(seq); }
  // A loaded set of milo images for the title scene (wall + poster).
  void set_title_images(ghogx::asset::Image wall, ghogx::asset::Image poster) {
    wall_ = std::move(wall);
    poster_ = std::move(poster);
  }
  // ARK location for song loading.
  void set_ark(const std::string& hdr, const std::string& ark) {
    hdr_path_ = hdr;
    ark_path_ = ark;
  }
  void set_auxiliary_asset_archives(
      std::vector<std::pair<std::string, std::string>> archives) {
    gameplay_.set_auxiliary_asset_paths(std::move(archives));
  }
  // Which song/difficulty to load on Start press.
  void set_song(const std::string& name, int diff) {
    song_name_ = name;
    song_diff_ = diff;
  }

 protected:
  void on_pre_frame(float dt) override {
    const auto profile_start = std::chrono::steady_clock::now();
    const bool confirm = win_->action_pressed(Action::Confirm) ||
                         win_->action_pressed(Action::Start);

    if (state_ == AppState::Splash) {
      bool seq_done = true;  // single image / checkerboard: treat as "at title"
      if (!seq_.slides.empty()) {
        const float end =
            static_cast<float>(seq_.slides.size() - 1) * seq_.slide_dur() + seq_.fade;
        seq_done = time() >= end;
      }
      if (confirm || seq_done) {
        if (confirm && !seq_done) std::fprintf(stderr, "[ghogx] splash skipped\n");
        state_ = AppState::Title;
      }
    } else if (state_ == AppState::Title) {
      if (win_->action_pressed(Action::Start) && !started_) {
        started_ = true;
        std::fprintf(stderr, "[ghogx] START pressed -> loading song '%s' diff=%d\n",
                     song_name_.c_str(), song_diff_);
        if (gameplay_.load_song(hdr_path_, ark_path_, song_name_, song_diff_)) {
          song_intro_overlay_.reset(hdr_path_, ark_path_, song_name_);
          reset_hud_load();
          rebuild_diagnostic_chart_guitar_script();
          if (diagnostic_song_start_ > 0.0) {
            gameplay_.seek_for_diagnostic_capture(diagnostic_song_start_);
          }
          if (!gameplay_.prepare_world(*win_)) {
            std::fprintf(stderr,
                         "[ghogx] gameplay world preparation failed\n");
            started_ = false;
            return;
          }
          ensure_hud_loaded();
          state_ = AppState::Playing;
          std::fprintf(stderr, "[ghogx] -> Playing\n");
        } else {
          std::fprintf(stderr, "[ghogx] song load failed; staying in Title\n");
          started_ = false;
        }
      }
    } else if (state_ == AppState::Playing) {
      // Guitar input: held frets plus edge-only strum/star-power actions.
      uint32_t fret_mask = 0;
      if (!diagnostic_guitar_script_.empty()) {
        fret_mask = diagnostic_guitar_script_mask(gameplay_.song_time());
      } else {
        fret_mask =
            win_->guitar_input_held() |
            (win_->guitar_input_edge() & ((1u << 5) | (1u << 6)));
        if (diagnostic_fret_mask_) {
          fret_mask |= *diagnostic_fret_mask_ & 0x1fu;
        }
        if (diagnostic_guitar_mask_) {
          fret_mask |= *diagnostic_guitar_mask_ & 0xffu;
        }
      }
      float whammy_axis = win_->guitar_whammy_axis();
      if ((fret_mask & (1u << 7)) != 0 &&
          diagnostic_chart_script_window_ &&
          diagnostic_chart_script_window_->whammy_sweep) {
        // Proof-only stand-in for a physical bar moving through its unipolar
        // travel. The live controller path above remains the direct signed
        // axis consumed by the decoded Tail::Poll contract.
        constexpr double kSweepHz = 2.0;
        constexpr double kTau = 6.28318530717958647692;
        whammy_axis = static_cast<float>(
            -(0.5 - 0.5 * std::cos(gameplay_.song_time() * kTau * kSweepHz)));
      } else if ((fret_mask & (1u << 7)) != 0 &&
          std::abs(whammy_axis) < 0.0001f) {
        // Harmonix guitar-controller whammy output is clamped to [-1, 0].
        // Scripted digital whammy therefore uses full negative travel.
        whammy_axis = -1.0f;
      }
      gameplay_.tick(dt, fret_mask, whammy_axis);
      if (gameplay_.failed()) {
        std::fprintf(stderr, "[ghogx] song failed; final score %d\n",
                     gameplay_.score());
        gameplay_.stop_audio();
        state_ = AppState::Failed;
        fail_hold_sec_ = kFailHoldSeconds;
      }
      else if (gameplay_.is_finished()) {
        std::fprintf(stderr, "[ghogx] song finished — final score %d\n", gameplay_.score());
        gameplay_.stop_audio();
        state_ = AppState::Finished;
        finish_hold_sec_ = kFinishHoldSeconds;
      }
    } else if (state_ == AppState::Failed) {
      fail_hold_sec_ = std::max(0.0f, fail_hold_sec_ - dt);
      if (fail_hold_sec_ <= 0.0f ||
          win_->action_pressed(Action::Confirm) ||
          win_->action_pressed(Action::Start)) {
        state_ = AppState::Title;
        started_ = false;
      }
    } else if (state_ == AppState::Finished) {
      finish_hold_sec_ = std::max(0.0f, finish_hold_sec_ - dt);
      if (finish_hold_sec_ <= 0.0f ||
          win_->action_pressed(Action::Confirm) ||
          win_->action_pressed(Action::Start)) {
        state_ = AppState::Title;
        started_ = false;
      }
    }
    if (profile_frame_times_) {
      profile_pre_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        profile_start)
              .count();
    }
  }

  void on_render(float /*dt*/) override {
    const auto profile_start = std::chrono::steady_clock::now();
    rendered_this_frame_ = true;
    if (sparse_screenshots_ && !should_render_this_frame()) {
      rendered_this_frame_ = false;
      if (profile_frame_times_) {
        profile_render_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          profile_start)
                .count();
      }
      return;
    }

    if (state_ == AppState::Playing || state_ == AppState::Failed ||
        state_ == AppState::Finished) {
      const auto gameplay_draw_start = std::chrono::steady_clock::now();
      gameplay_.draw(*win_);
      if (profile_frame_times_) {
        profile_gameplay_draw_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          gameplay_draw_start)
                .count();
      }
      const auto hud_start = std::chrono::steady_clock::now();
      if (std::getenv("GHOGX_HIDE_SONG_INTRO_OVERLAY") == nullptr)
        song_intro_overlay_.draw(gameplay_.intro_presentation_time());
      if (!diagnostic_hide_hud_enabled()) {
        draw_gameplay_hud();
        if (state_ == AppState::Failed) {
          draw_fail_overlay();
        } else if (state_ == AppState::Finished) {
          draw_finish_overlay();
        }
      } else {
        static bool logged_hide_hud = false;
        if (!logged_hide_hud) {
          logged_hide_hud = true;
          std::fprintf(stderr,
                       "[diagnostic-hud] GHOGX_HIDE_HUD active; skipping HUD draw\n");
        }
      }
      if (profile_frame_times_) {
        profile_hud_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          hud_start)
                .count();
      }
      if (profile_frame_times_) {
        profile_render_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          profile_start)
                .count();
      }
      return;
    }

    if (!seq_.slides.empty()) {
      if (state_ == AppState::Title) {
        // Splash sequence done/skipped — show the title as a 3-D scene.
        render_title_3d();
        return;
      }
      // Boot logo sequence still playing (Splash state).
      int idx = -1;
      float brightness = 0.0f;
      seq_.eval(time(), idx, brightness);
      win_->clear(0.0f, 0.0f, 0.0f);
      if (idx >= 0 && seq_.slides[idx].valid()) {
        const auto& s = seq_.slides[idx];
        win_->blit_fullscreen_rgba(s.rgba.data(), s.width, s.height, brightness);
      }
      return;
    }
    if (image_.valid()) {  // show a single loaded PS2 texture (static)
      win_->clear(0.0f, 0.0f, 0.0f);
      win_->blit_fullscreen_rgba(image_.rgba.data(), image_.width, image_.height);
      return;
    }

    const float t = time();
    const int scroll = static_cast<int>(t * 60.0f);              // diagonal drift
    const float pulse = 0.5f * (1.0f + std::sin(t * 1.2f));      // 0..1
    const auto lo = static_cast<unsigned char>(30 + 60 * pulse);
    const auto hi = static_cast<unsigned char>(150 + 80 * pulse);

    for (int y = 0; y < kTexH; ++y) {
      for (int x = 0; x < kTexW; ++x) {
        const bool cell = ((((x + scroll) / kCell) + ((y + scroll) / kCell)) & 1) != 0;
        unsigned char* p = &pixels_[(static_cast<std::size_t>(y) * kTexW + x) * 4];
        if (cell) {  // warm cell
          p[0] = hi; p[1] = static_cast<unsigned char>(hi * 0.45f); p[2] = lo;
        } else {     // cool cell
          p[0] = lo; p[1] = hi; p[2] = static_cast<unsigned char>(hi * 0.85f);
        }
        p[3] = 255;
      }
    }

    win_->clear(0.0f, 0.0f, 0.0f);
    win_->blit_fullscreen_rgba(pixels_.data(), kTexW, kTexH);
  }

  void on_present() override {
    const auto profile_start = std::chrono::steady_clock::now();
    if (!rendered_this_frame_) return;

    // Dev self-verification: capture the rendered frame before presenting.
    const uint64_t frame = frame_count();
    if (!screenshot_path_.empty() &&
        frame == static_cast<uint64_t>(screenshot_frame_)) {
      win_->save_screenshot(screenshot_path_.c_str());
    }
    const auto seq_it = screenshot_sequence_.find(frame);
    if (seq_it != screenshot_sequence_.end()) {
      win_->save_screenshot(seq_it->second.c_str());
      std::fprintf(stderr, "[ghogx] saved screenshot %s at frame %llu\n",
                   seq_it->second.c_str(),
                   static_cast<unsigned long long>(frame));
    }
    win_->present();
    if (profile_frame_times_) {
      profile_present_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        profile_start)
              .count();
      ++profile_frames_;
    }
  }

 private:
  using Action = ghogx::render::Window::Action;
  enum class AppState { Splash, Title, Playing, Failed, Finished };

  static const char* app_state_name(AppState state) {
    switch (state) {
    case AppState::Splash: return "splash";
    case AppState::Title: return "title";
    case AppState::Playing: return "playing";
    case AppState::Failed: return "failed";
    case AppState::Finished: return "finished";
    }
    return "unknown";
  }

  // Render a procedural 3-D representation of the GH2 title screen.
  void render_title_3d() {
    using namespace ghogx::render;

    const float t = time();
    const float pulse = 0.5f * (1.0f + std::sin(t * 1.4f));

    scene_.begin_frame(
        0.0f, 0.0f, -10.0f,
        0.0f, 0.0f,  0.0f,
        0.0f, 1.0f,  0.0f,
        0.70f,
        0.1f, 50.0f,
        0.04f, 0.04f, 0.06f);

    auto make_quad = [](float xmin, float xmax, float ymin, float ymax,
                        float z, uint32_t color = 0xFFFFFFFF) {
      using Vtx = Vtx3;
      std::vector<Vtx> v = {
          {xmin, ymax, z,  0,0,-1, color, 0.0f, 0.0f},
          {xmax, ymax, z,  0,0,-1, color, 1.0f, 0.0f},
          {xmin, ymin, z,  0,0,-1, color, 0.0f, 1.0f},
          {xmax, ymin, z,  0,0,-1, color, 1.0f, 1.0f},
      };
      std::vector<uint16_t> idx = {0, 1, 2,  1, 3, 2};
      return std::make_pair(v, idx);
    };

    if (wall_.valid()) {
      auto* tex = scene_.upload_rgba(wall_.rgba.data(), wall_.width, wall_.height);
      using Vtx = Vtx3;
      const uint32_t wc = 0xFFFFFFFF;
      std::vector<Vtx> wv = {
          {-4.5f, 3.2f, 0.0f, 0,0,-1, wc, 0.0f, 0.0f},
          { 4.5f, 3.2f, 0.0f, 0,0,-1, wc, 3.0f, 0.0f},
          {-4.5f,-3.2f, 0.0f, 0,0,-1, wc, 0.0f, 3.0f},
          { 4.5f,-3.2f, 0.0f, 0,0,-1, wc, 3.0f, 3.0f},
      };
      std::vector<uint16_t> wi = {0,1,2, 1,3,2};
      scene_.draw(wv.data(), static_cast<int>(wv.size()),
                  wi.data(), 2, tex);
    }

    if (poster_.valid()) {
      auto* tex = scene_.upload_rgba(poster_.rgba.data(),
                                     poster_.width, poster_.height);
      Mat4 world = Mat4::translation(-0.2f, 0.0f, -0.5f) *
                   Mat4::rotation_y(0.055f);
      scene_.set_world(world);
      auto [v, idx] = make_quad(-1.8f, 1.8f, -2.0f, 2.0f, 0.0f);
      scene_.draw(v.data(), static_cast<int>(v.size()),
                  idx.data(), 2, tex);
      scene_.set_world(Mat4::identity());
    }

    {
      const float ta = 0.80f;
      const uint32_t tape_col = static_cast<uint32_t>(ta * 255) << 24 |
                                0xEEDDCC;
      auto [v1, i1] = make_quad(-0.5f, 0.8f,  1.7f, 2.1f, -0.6f, tape_col);
      auto [v2, i2] = make_quad(-0.9f, 0.4f, -2.1f,-1.7f, -0.6f, tape_col);
      scene_.draw(v1.data(), 4, i1.data(), 2);
      scene_.draw(v2.data(), 4, i2.data(), 2);
    }

    {
      const float ga = 0.30f + 0.20f * pulse;
      const uint32_t glow_col = static_cast<uint32_t>(ga * 255) << 24 |
                                 0xFFEEAA;
      auto [gv, gi] = make_quad(-1.2f, 1.2f, 0.8f, 2.5f, -0.7f, glow_col);
      scene_.draw(gv.data(), 4, gi.data(), 2);
    }

    scene_.end_frame();
  }

  static constexpr int kTexW = 256;
  static constexpr int kTexH = 256;
  static constexpr int kCell = 32;

  ghogx::render::Window* win_;
  ghogx::render::SceneD3D9 scene_;
  std::vector<unsigned char> pixels_;
  ghogx::asset::Image image_;
  SplashSequence seq_;
  ghogx::asset::Image wall_;
  ghogx::asset::Image poster_;
  AppState state_ = AppState::Splash;
  bool started_ = false;
  static constexpr float kFailHoldSeconds = 3.0f;
  static constexpr float kFinishHoldSeconds = 4.0f;
  float fail_hold_sec_ = 0.0f;
  float finish_hold_sec_ = 0.0f;

  // Song gameplay.
  ghogx::game::Gameplay gameplay_;
  ghogx::ui::SongIntroOverlay song_intro_overlay_;
  ghogx::hud::HudRenderer hud_;
  bool hud_load_attempted_ = false;
  bool hud_ready_ = false;
  bool fail_overlay_load_attempted_ = false;
  bool fail_overlay_ready_ = false;
  IDirect3DTexture9* fail_overlay_tex_ = nullptr;
  int fail_overlay_width_ = 0;
  int fail_overlay_height_ = 0;
  bool finish_overlay_load_attempted_ = false;
  bool finish_overlay_ready_ = false;
  IDirect3DTexture9* finish_overlay_tex_ = nullptr;
  int finish_overlay_width_ = 0;
  int finish_overlay_height_ = 0;
  std::string hdr_path_;
  std::string ark_path_;
  std::string song_name_ = "shoutatthedevil";
  int         song_diff_ = 0;  // Easy
  std::string hud_tune_file_;

  // Dev screenshot capture.
  std::string screenshot_path_;
  int         screenshot_frame_ = 0;
  ScreenshotSequence screenshot_sequence_;
  bool sparse_screenshots_ = false;
  bool rendered_this_frame_ = true;
  double diagnostic_song_start_ = 0.0;
  std::optional<uint32_t> diagnostic_fret_mask_;
  std::optional<uint32_t> diagnostic_guitar_mask_;
  std::vector<DiagnosticGuitarScriptEvent> diagnostic_guitar_script_;
  std::optional<DiagnosticChartScriptWindow> diagnostic_chart_script_window_;
  std::optional<ghogx::hud::HudState> diagnostic_hud_override_;
  bool profile_frame_times_ = false;
  uint64_t profile_frames_ = 0;
  double profile_pre_seconds_ = 0.0;
  double profile_render_seconds_ = 0.0;
  double profile_gameplay_draw_seconds_ = 0.0;
  double profile_hud_seconds_ = 0.0;
  double profile_present_seconds_ = 0.0;

  bool should_render_this_frame() const {
    const uint64_t frame = frame_count();
    if (!screenshot_path_.empty() &&
        frame == static_cast<uint64_t>(screenshot_frame_)) {
      return true;
    }
    return screenshot_sequence_.find(frame) != screenshot_sequence_.end();
  }

  uint32_t diagnostic_guitar_script_mask(double song_time) const {
    uint32_t mask = 0;
    for (const auto& event : diagnostic_guitar_script_) {
      if (event.song_time > song_time + 1.0e-6) break;
      mask = event.mask & 0xffu;
    }
    return mask;
  }

  void rebuild_diagnostic_chart_guitar_script() {
    if (!diagnostic_chart_script_window_) return;
    const auto generated = gameplay_.build_diagnostic_guitar_script_from_chart(
        diagnostic_chart_script_window_->start_sec,
        diagnostic_chart_script_window_->end_sec,
        true,
        diagnostic_chart_script_window_->hit_offset_sec,
        diagnostic_chart_script_window_->whammy_star_sustains,
        diagnostic_chart_script_window_->star_power_at_sec);
    diagnostic_guitar_script_.clear();
    diagnostic_guitar_script_.reserve(generated.size());
    for (const auto& event : generated) {
      diagnostic_guitar_script_.push_back(
          DiagnosticGuitarScriptEvent{event.song_time, event.mask});
    }
    std::fprintf(
        stderr,
        "[ghogx] diagnostic chart guitar script events: %zu window=%.3f..%.3f "
        "hit_offset=%.4f whammy_star_sustains=%d star_power_at=%.3f\n",
        diagnostic_guitar_script_.size(),
        diagnostic_chart_script_window_->start_sec,
        diagnostic_chart_script_window_->end_sec,
        diagnostic_chart_script_window_->hit_offset_sec,
        diagnostic_chart_script_window_->whammy_star_sustains ? 1 : 0,
        diagnostic_chart_script_window_->star_power_at_sec
            ? *diagnostic_chart_script_window_->star_power_at_sec
            : -1.0);
  }

 public:
  // Capture frame `frame` to a BMP for dev self-verification.
  void set_screenshot(const std::string& path, int frame) {
    screenshot_path_ = path;
    screenshot_frame_ = frame;
  }

  void set_screenshot_sequence(ScreenshotSequence sequence) {
    screenshot_sequence_ = std::move(sequence);
  }

  void set_sparse_screenshots(bool enabled) { sparse_screenshots_ = enabled; }

  void set_deterministic_gameplay_clock(bool deterministic) {
    gameplay_.set_deterministic_clock(deterministic);
  }

  void set_diagnostic_autoplay(bool enabled) {
    gameplay_.set_diagnostic_autoplay(enabled);
  }

  void set_diagnostic_star_power_fill(double fill) {
    gameplay_.set_diagnostic_star_power_fill(fill);
  }

  void set_diagnostic_star_power_active(bool active) {
    gameplay_.set_diagnostic_star_power_active(active);
  }

  void set_diagnostic_fret_mask(uint32_t mask) {
    diagnostic_fret_mask_ = mask & 0x1fu;
  }

  void set_diagnostic_guitar_mask(uint32_t mask) {
    diagnostic_guitar_mask_ = mask & 0xffu;
  }

  void set_diagnostic_guitar_script(
      std::vector<DiagnosticGuitarScriptEvent> events) {
    diagnostic_guitar_script_ = std::move(events);
  }

  void set_diagnostic_chart_guitar_script(
      DiagnosticChartScriptWindow window) {
    diagnostic_chart_script_window_ = window;
  }

  void set_diagnostic_character_override(const std::string& character) {
    gameplay_.set_diagnostic_character_override(character);
  }

  void set_selected_character_variant(
      const ghogx::ui::CharacterVariant& variant) {
    gameplay_.set_selected_character_variant(
        variant.selection.c_str(), variant.model_path,
        variant.main_anim_path, variant.strum_anim_path,
        variant.fret_anim_path, variant.highway_surface_path,
        variant.animation_source_model_path, variant.retarget_animation,
        variant.guitarist_hidden_roots);
  }

  void set_diagnostic_performer_override(
      const std::string& role, const std::string& character_reference) {
    gameplay_.set_diagnostic_performer_override(role, character_reference);
  }

  void set_diagnostic_performer_animation_override(
      const std::string& role, const std::string& character_reference) {
    gameplay_.set_diagnostic_performer_animation_override(
        role, character_reference);
  }

  void set_diagnostic_venue_override(const std::string& venue) {
    gameplay_.set_diagnostic_venue_override(venue);
  }

  void set_diagnostic_guitar_override(const std::string& guitar) {
    gameplay_.set_diagnostic_guitar_override(guitar);
  }

  void set_diagnostic_bass_override(const std::string& bass) {
    gameplay_.set_diagnostic_bass_override(bass);
  }

  void set_diagnostic_player_bassist(const std::string& model) {
    const std::string base = "char/" + model;
    gameplay_.set_selected_bassist_character_variant(
        model, base + "/og/gen/" + model + ".milo_ps2",
        base + "/anims/gen/" + model + "_main.milo_ps2",
        base + "/anims/gen/" + model + "_strum.milo_ps2",
        base + "/anims/gen/" + model + "_fret.milo_ps2");
  }

  void set_diagnostic_venue_event(const std::string& event_name) {
    gameplay_.set_diagnostic_venue_event(event_name);
  }

  void set_diagnostic_camera_shot(const std::string& shot_name) {
    gameplay_.set_diagnostic_camera_shot(shot_name);
  }
  void set_diagnostic_front_camera(const std::string& role) {
    gameplay_.set_diagnostic_front_camera(role);
  }
  void set_diagnostic_unlit_performers(bool enabled) {
    gameplay_.set_diagnostic_unlit_performers(enabled);
  }
  void set_diagnostic_camera_path_offset_frames(double frames) {
    gameplay_.set_diagnostic_camera_path_offset_frames(frames);
  }
  void set_diagnostic_camera_random_seed(int seed) {
    gameplay_.set_diagnostic_camera_random_seed(seed);
  }
  bool cycle_camera_shot_like_source() {
    return gameplay_.cycle_camera_shot_like_source();
  }
  size_t iterate_camera_shots_like_source() {
    return gameplay_.iterate_camera_shots_like_source();
  }

  void set_diagnostic_rock_fill(double fill) {
    gameplay_.set_diagnostic_rock_fill(fill);
  }

  void set_diagnostic_song_start(double seconds) {
    diagnostic_song_start_ = std::max(0.0, seconds);
  }

  void set_diagnostic_hud_override(const ghogx::hud::HudState& state) {
    diagnostic_hud_override_ = state;
  }

  void set_hud_tuning_file(const std::string& path) {
    hud_tune_file_ = path;
    hud_.set_layout_tuning_file(hud_tune_file_);
    reset_hud_load();
  }

  void log_final_gameplay_summary() const {
    std::fprintf(
        stderr,
        "[ghogx] final gameplay summary: state=%s song=%s diff=%d "
        "t=%.3f score=%d streak=%d mult=%d hits=%d misses=%d "
        "overstrums=%d rock=%.3f sp=%.3f active=%d failed=%d finished=%d\n",
        app_state_name(state_), song_name_.c_str(), song_diff_,
        gameplay_.song_time(), gameplay_.score(), gameplay_.streak(),
        gameplay_.multiplier(), gameplay_.hit_count(), gameplay_.miss_count(),
        gameplay_.overstrum_count(), gameplay_.rock_fill(),
        gameplay_.star_power_fill(), gameplay_.star_power_active() ? 1 : 0,
        gameplay_.failed() ? 1 : 0, gameplay_.is_finished() ? 1 : 0);
  }

  // Force-load the song and skip directly to Playing state (for --auto-start).
  bool force_start_song() {
    if (gameplay_.load_song(hdr_path_, ark_path_, song_name_, song_diff_)) {
      song_intro_overlay_.reset(hdr_path_, ark_path_, song_name_);
      reset_hud_load();
      rebuild_diagnostic_chart_guitar_script();
      if (diagnostic_song_start_ > 0.0) {
        gameplay_.seek_for_diagnostic_capture(diagnostic_song_start_);
      }
      if (!gameplay_.prepare_world(*win_)) {
        std::fprintf(stderr, "[ghogx] gameplay world preparation failed\n");
        return false;
      }
      ensure_hud_loaded();
      state_ = AppState::Playing;
      started_ = true;
      return true;
    }
    return false;
  }

  void reset_hud_load() {
    hud_load_attempted_ = false;
    hud_ready_ = false;
  }

  void ensure_hud_loaded() {
    if (hud_load_attempted_) return;
    hud_load_attempted_ = true;
    if (hdr_path_.empty() || ark_path_.empty()) return;
    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    if (!hud_tune_file_.empty()) hud_.set_layout_tuning_file(hud_tune_file_);
    hud_ready_ = hud_.load(dev, hdr_path_, ark_path_);
  }

  void draw_gameplay_hud() {
    if (env_flag("GHOGX_DEBUG_VENUE_ONLY_CAPTURE") ||
        env_flag("GHOGX_DEBUG_HIGHWAY_ONLY_CAPTURE") ||
        env_flag("GHOGX_HIDE_GAMEPLAY_HUD")) {
      return;
    }
    ensure_hud_loaded();
    if (!hud_ready_) return;

    ghogx::hud::HudState state;
    state.score = gameplay_.score();
    state.streak = gameplay_.streak();
    state.multiplier = gameplay_.multiplier() *
                       (gameplay_.star_power_active() ? 2 : 1);
    state.sp_fill = gameplay_.star_power_fill();
    state.sp_active = gameplay_.star_power_active();
    state.rock_fill = gameplay_.rock_fill();
    if (diagnostic_hud_override_) state = *diagnostic_hud_override_;
    state.anim_seconds =
        static_cast<float>(gameplay_.track_intro_elapsed());
    state.track_intro_active = gameplay_.track_intro_active();
    static int hud_state_debug_budget = 0;
    if (env_flag("GHOGX_DEBUG_GAMEPLAY_HUD_STATE") &&
        hud_state_debug_budget < 240) {
      std::fprintf(
          stderr,
          "[hud-state] t=%.3f score=%d streak=%d gameplay_mult=%d "
          "hud_mult=%d sp=%.3f active=%d rock=%.3f override=%d\n",
          gameplay_.song_time(), state.score, state.streak,
          gameplay_.multiplier(), state.multiplier, state.sp_fill,
          state.sp_active ? 1 : 0, state.rock_fill,
          diagnostic_hud_override_ ? 1 : 0);
      ++hud_state_debug_budget;
    }

    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    hud_.draw(dev, state);
  }

  void ensure_fail_overlay_loaded() {
    if (fail_overlay_load_attempted_) return;
    fail_overlay_load_attempted_ = true;
    if (hdr_path_.empty() || ark_path_.empty()) return;
    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    const ghogx::asset::Image tile = ghogx::asset::load_milo_texture_named(
        hdr_path_, ark_path_, "ui/gen/pause_lose_tex.milo_ps2", "pl_tile.tex");
    fail_overlay_width_ = tile.width;
    fail_overlay_height_ = tile.height;
    fail_overlay_tex_ = upload_overlay_texture(dev, tile, true);
    fail_overlay_ready_ = fail_overlay_tex_ != nullptr;
    std::fprintf(stderr,
                 "[ghogx] fail overlay texture %s (%dx%d) from %s/%s\n",
                 fail_overlay_ready_ ? "ready" : "failed",
                 fail_overlay_width_, fail_overlay_height_,
                 "ui/gen/pause_lose_tex.milo_ps2", "pl_tile.tex");
  }

  void draw_fail_overlay() {
    ensure_fail_overlay_loaded();
    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    if (!dev) return;
    D3DVIEWPORT9 vp;
    if (FAILED(dev->GetViewport(&vp))) return;
    const float w = static_cast<float>(vp.Width);
    const float h = static_cast<float>(vp.Height);

    dev->BeginScene();
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetFVF(kOverlayFVF);

    draw_overlay_quad(dev, nullptr, 0.0f, 0.0f, w, h,
                      D3DCOLOR_ARGB(150, 0, 0, 0));
    if (fail_overlay_ready_ && fail_overlay_tex_) {
      const float target = std::min(w, h) * 0.44f;
      const float scale = std::min(
          target / std::max(1, fail_overlay_width_),
          target / std::max(1, fail_overlay_height_));
      const float tw = fail_overlay_width_ * scale;
      const float th = fail_overlay_height_ * scale;
      const float x0 = (w - tw) * 0.5f;
      const float y0 = (h - th) * 0.35f;
      draw_overlay_quad(dev, fail_overlay_tex_, x0, y0, x0 + tw, y0 + th,
                        D3DCOLOR_ARGB(215, 255, 255, 255));
    }
    dev->SetTexture(0, nullptr);
    dev->EndScene();
  }

  std::string finish_overlay_milo_path() const {
    static constexpr std::array<const char*, 4> kWinPanels = {
        "ui/gen/win_easy.milo_ps2",
        "ui/gen/win_medium.milo_ps2",
        "ui/gen/win_hard.milo_ps2",
        "ui/gen/win_expert.milo_ps2",
    };
    const int diff = std::clamp(song_diff_, 0, 3);
    return kWinPanels[static_cast<size_t>(diff)];
  }

  void ensure_finish_overlay_loaded() {
    if (finish_overlay_load_attempted_) return;
    finish_overlay_load_attempted_ = true;
    if (hdr_path_.empty() || ark_path_.empty()) return;
    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    const std::string milo_path = finish_overlay_milo_path();
    const ghogx::asset::Image tile = ghogx::asset::load_milo_texture_named(
        hdr_path_, ark_path_, milo_path, "newspaper.tex");
    finish_overlay_width_ = tile.width;
    finish_overlay_height_ = tile.height;
    finish_overlay_tex_ = upload_overlay_texture(dev, tile, true);
    finish_overlay_ready_ = finish_overlay_tex_ != nullptr;
    std::fprintf(stderr,
                 "[ghogx] finish overlay texture %s (%dx%d) from %s/%s\n",
                 finish_overlay_ready_ ? "ready" : "failed",
                 finish_overlay_width_, finish_overlay_height_,
                 milo_path.c_str(), "newspaper.tex");
  }

  void draw_finish_overlay() {
    ensure_finish_overlay_loaded();
    auto* dev = static_cast<IDirect3DDevice9*>(win_->device_ptr());
    if (!dev) return;
    D3DVIEWPORT9 vp;
    if (FAILED(dev->GetViewport(&vp))) return;
    const float w = static_cast<float>(vp.Width);
    const float h = static_cast<float>(vp.Height);

    dev->BeginScene();
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetFVF(kOverlayFVF);

    draw_overlay_quad(dev, nullptr, 0.0f, 0.0f, w, h,
                      D3DCOLOR_ARGB(130, 0, 0, 0));
    if (finish_overlay_ready_ && finish_overlay_tex_) {
      const float target_w = w * 0.62f;
      const float target_h = h * 0.68f;
      const float scale = std::min(
          target_w / std::max(1, finish_overlay_width_),
          target_h / std::max(1, finish_overlay_height_));
      const float tw = finish_overlay_width_ * scale;
      const float th = finish_overlay_height_ * scale;
      const float x0 = (w - tw) * 0.5f;
      const float y0 = (h - th) * 0.46f;
      draw_overlay_quad(dev, finish_overlay_tex_, x0, y0, x0 + tw, y0 + th,
                        D3DCOLOR_ARGB(230, 255, 255, 255));
    }
    dev->SetTexture(0, nullptr);
    dev->EndScene();
  }
};

bool is_screenshot_frame_separator(char c) {
  return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::vector<uint64_t> parse_screenshot_frames(const std::string& frames_arg) {
  std::vector<uint64_t> frames;
  const char* p = frames_arg.c_str();
  while (*p) {
    while (*p && is_screenshot_frame_separator(*p)) ++p;
    if (!*p) break;
    char* end = nullptr;
    const unsigned long long frame = std::strtoull(p, &end, 10);
    if (end == p) {
      while (*p && !is_screenshot_frame_separator(*p)) ++p;
      continue;
    }
    frames.push_back(static_cast<uint64_t>(frame));
    p = end;
  }
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
  return frames;
}

ScreenshotSequence make_screenshot_sequence(const std::string& out_dir,
                                            const std::string& frames_arg) {
  ScreenshotSequence sequence;
  const std::vector<uint64_t> frames = parse_screenshot_frames(frames_arg);
  if (out_dir.empty() || frames.empty()) return sequence;

  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "[ghogx] failed to create screenshot dir %s: %s\n",
                 out_dir.c_str(), ec.message().c_str());
    return sequence;
  }

  for (const uint64_t frame : frames) {
    char name[64];
    std::snprintf(name, sizeof(name), "frame_%05llu.bmp",
                  static_cast<unsigned long long>(frame));
    sequence.emplace(frame, (std::filesystem::path(out_dir) / name).string());
  }
  return sequence;
}

// ---------------------------------------------------------------------------
// --scene mode: load a .milo_ps2, decode its 3-D render objects, and draw the
// real geometry with an orbit camera the user can move. The deliverable path
// for "make the game LOOK like Guitar Hero" — a venue/stage rendered in 3-D.
// ---------------------------------------------------------------------------
struct CamOverride {
  bool has_yaw = false, has_pitch = false, has_dist = false, has_target = false;
  float yaw = 0, pitch = 0, dist = 0, target[3] = {0, 0, 0};
};

std::string normalize_milo_path(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos) j = path.size();
    std::string part = path.substr(i, j - i);
    if (part.empty() || part == ".") {
      // skip
    } else if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else {
      parts.push_back(std::move(part));
    }
    if (j == path.size()) break;
    i = j + 1;
  }
  std::string out;
  for (size_t k = 0; k < parts.size(); ++k) {
    if (k) out += '/';
    out += parts[k];
  }
  return out;
}

std::string replace_suffix(std::string s, const std::string& from,
                           const std::string& to) {
  if (s.size() >= from.size() &&
      s.compare(s.size() - from.size(), from.size(), from) == 0) {
    s.resize(s.size() - from.size());
    s += to;
  }
  return s;
}

std::string insert_gen_dir_for_anim(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return path;
  const std::string dir = path.substr(0, slash);
  if (dir.size() >= 4 && dir.compare(dir.size() - 4, 4, "/gen") == 0) {
    return path;
  }
  return dir + "/gen" + path.substr(slash);
}

std::vector<std::string> driver_milo_candidates(
    const std::string& character_milo, const std::string& driver_milo) {
  std::vector<std::string> out;
  auto add = [&](std::string p) {
    if (p.empty()) return;
    p = normalize_milo_path(std::move(p));
    if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
  };

  std::string char_dir = character_milo;
  std::replace(char_dir.begin(), char_dir.end(), '\\', '/');
  const size_t slash = char_dir.find_last_of('/');
  char_dir = slash == std::string::npos ? std::string() : char_dir.substr(0, slash);

  std::string rel = driver_milo;
  std::replace(rel.begin(), rel.end(), '\\', '/');
  const std::string resolved = normalize_milo_path(
      (!char_dir.empty() && rel.rfind("/", 0) != 0 && rel.find(':') == std::string::npos)
          ? char_dir + "/" + rel
          : rel);
  add(resolved);
  add(replace_suffix(resolved, ".milo", ".milo_ps2"));
  add(insert_gen_dir_for_anim(resolved));
  add(insert_gen_dir_for_anim(replace_suffix(resolved, ".milo", ".milo_ps2")));
  return out;
}

std::string animation_milo_role(std::string path) {
  path = normalize_milo_path(std::move(path));
  const size_t slash = path.find_last_of('/');
  std::string name =
      slash == std::string::npos ? path : path.substr(slash + 1);
  if (name.size() >= 9 &&
      name.compare(name.size() - 9, 9, ".milo_ps2") == 0) {
    name.resize(name.size() - 9);
  } else if (name.size() >= 5 &&
             name.compare(name.size() - 5, 5, ".milo") == 0) {
    name.resize(name.size() - 5);
  }
  const size_t underscore = name.find_last_of('_');
  if (underscore == std::string::npos || underscore + 1 >= name.size()) {
    return {};
  }
  return name.substr(underscore + 1);
}

bool facefx_neutral_enabled() {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool enabled =
      _dupenv_s(&value, &len, "GHOGX_FACEFX_NEUTRAL") == 0 && value && value[0];
  std::free(value);
  return enabled;
#else
  const char* value = std::getenv("GHOGX_FACEFX_NEUTRAL");
  return value && value[0];
#endif
}

bool face_debug_enabled() {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool enabled =
      _dupenv_s(&value, &len, "GHOGX_DEBUG_FACE") == 0 && value && value[0];
  std::free(value);
  return enabled;
#else
  const char* value = std::getenv("GHOGX_DEBUG_FACE");
  return value && value[0];
#endif
}

void dump_facefx_lip_sync_servos(
    const ghogx::character::Character& character) {
  if (!face_debug_enabled()) return;
  std::fprintf(stderr,
               "[facefx-servo] stock FaceFxLipSyncServo rows=%zu "
               "source=GH2-compat decoder boundary=not-CharFaceServo\n",
               character.lip_sync_servos.size());
  for (size_t i = 0; i < character.lip_sync_servos.size(); ++i) {
    const auto& servo = character.lip_sync_servos[i];
    std::fprintf(stderr,
                 "[facefx-servo] row=%zu name=%s facefx=%s viseme_milo=%s "
                 "targets=%zu\n",
                 i, servo.name.c_str(), servo.facefx_path.c_str(),
                 servo.viseme_milo.c_str(), servo.targets.size());
    for (size_t t = 0; t < servo.targets.size(); ++t) {
      const auto& target = servo.targets[t];
      std::fprintf(stderr,
                   "[facefx-servo]   target=%zu object=%s prop_type=%d "
                   "property=%s\n",
                   t, target.object.c_str(), target.prop_type,
                   target.property.c_str());
    }
  }
}

std::string lower_ascii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

bool is_face_asset_name(const std::string& name) {
  const std::string lower = lower_ascii(name);
  const size_t dot = lower.find('.');
  const std::string base = dot == std::string::npos ? lower : lower.substr(0, dot);
  if (base == "bone_gh3_e8e9bb36" || base == "bone_gh3_12e68655" ||
      base == "bone_gh3_2a8d0c00" || base == "bone_gh3_7c73f6cf" ||
      base == "bone_gh3_867ccbac" || base == "bone_gh3_5378af83" ||
      base == "bone_gh3_a97792e0" || base == "bone_gh3_b8ca856b" ||
      base == "bone_gh3_c8a071e4" || base == "bone_gh3_a6bc7033") {
    return true;
  }
  return lower.find("mouth") != std::string::npos ||
         lower.find("lip") != std::string::npos ||
         lower.find("jaw") != std::string::npos ||
         lower.find("teeth") != std::string::npos ||
         lower.find("tongue") != std::string::npos ||
         lower.find("tounge") != std::string::npos ||
         lower.find("eye") != std::string::npos ||
         lower.find("lid") != std::string::npos ||
         lower.find("brow") != std::string::npos;
}

void dump_face_asset_inventory(
    const ghogx::character::Character& character) {
  if (!face_debug_enabled()) return;
  std::fprintf(stderr,
               "[face-inventory] character=%s bones=%zu meshes=%zu "
               "source=stock-milo decoded-runtime\n",
               character.dir_name.c_str(), character.bones.size(),
               character.meshes.size());
  for (const auto& b : character.bones) {
    if (!is_face_asset_name(b.name)) continue;
    std::fprintf(stderr,
                 "[face-inventory] bone=%s parent=%s local=(%.3f %.3f %.3f)\n",
                 b.name.c_str(), b.parent.empty() ? "<none>" : b.parent.c_str(),
                 b.local.pos[0], b.local.pos[1], b.local.pos[2]);
  }
  for (const auto& m : character.meshes) {
    if (!is_face_asset_name(m.name) && !is_face_asset_name(m.parent)) continue;
    std::fprintf(stderr,
                 "[face-inventory] mesh=%s parent=%s material=%s palette=%zu "
                 "local=(%.3f %.3f %.3f)\n",
                 m.name.c_str(), m.parent.empty() ? "<none>" : m.parent.c_str(),
                 m.material.empty() ? "<none>" : m.material.c_str(),
                 m.bone_palette.size(), m.local.pos[0], m.local.pos[1],
                 m.local.pos[2]);
  }
}

void dump_face_clip_inventory(const char* label,
                              const ghogx::character::CharClip& clip) {
  if (!face_debug_enabled() || !clip.loaded) return;
  size_t channels = 0;
  std::unordered_set<std::string> face_rows;
  for (const auto& frame : clip.frames) {
    channels += frame.size();
    for (const auto& ch : frame) {
      if (is_face_asset_name(ch.bone_name)) face_rows.insert(ch.bone_name);
    }
  }
  size_t face_output_bones = 0;
  for (const auto& out : clip.output_bones) {
    if (is_face_asset_name(out.name)) ++face_output_bones;
  }
  std::fprintf(stderr,
               "[face-clip] label=%s clip=%s frames=%zu channels=%zu "
               "faceRows=%zu outputBones=%zu faceOutputBones=%zu "
               "relative=%d source=CharClip decoded from stock MILO\n",
               label, clip.name.c_str(), clip.frames.size(), channels,
               face_rows.size(), clip.output_bones.size(), face_output_bones,
               clip.relative ? 1 : 0);
  for (const auto& out : clip.output_bones) {
    if (!is_face_asset_name(out.name)) continue;
    std::fprintf(stderr,
                 "[face-clip]   output=%s parent=%s local=(%.3f %.3f %.3f) "
                 "worldStored=(%.3f %.3f %.3f)\n",
                 out.name.c_str(),
                 out.parent.empty() ? "<none>" : out.parent.c_str(),
                 out.local.pos[0], out.local.pos[1], out.local.pos[2],
                 out.world_stored.pos[0], out.world_stored.pos[1],
                 out.world_stored.pos[2]);
  }
}

std::optional<int> env_int(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool found = _dupenv_s(&value, &len, name) == 0 && value && value[0];
  std::optional<int> out;
  if (found) out = std::strtol(value, nullptr, 10);
  std::free(value);
  return out;
#else
  const char* value = std::getenv(name);
  if (!value || !value[0]) return std::nullopt;
  return static_cast<int>(std::strtol(value, nullptr, 10));
#endif
}

std::optional<float> env_float(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool found = _dupenv_s(&value, &len, name) == 0 && value && value[0];
  std::optional<float> out;
  if (found) out = std::strtof(value, nullptr);
  std::free(value);
  return out;
#else
  const char* value = std::getenv(name);
  if (!value || !value[0]) return std::nullopt;
  return std::strtof(value, nullptr);
#endif
}

bool is_face_channel_name(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  const size_t dot = lower.find('.');
  const std::string base = dot == std::string::npos ? lower : lower.substr(0, dot);
  // Some imported PS2 rigs only preserve checksum-stable fallback names for
  // face children. Keep the known Midori face checksum channels so expression
  // overlays do not accidentally pull in unrelated imported fallback bones.
  if (base == "bone_gh3_e8e9bb36" || base == "bone_gh3_12e68655" ||
      base == "bone_gh3_2a8d0c00" || base == "bone_gh3_7c73f6cf" ||
      base == "bone_gh3_867ccbac" || base == "bone_gh3_5378af83" ||
      base == "bone_gh3_a97792e0" || base == "bone_gh3_b8ca856b" ||
      base == "bone_gh3_c8a071e4" || base == "bone_gh3_a6bc7033") {
    return true;
  }
  return lower.find("face") != std::string::npos ||
         lower.find("mouth") != std::string::npos ||
         lower.find("lip") != std::string::npos ||
         lower.find("brow") != std::string::npos ||
         lower.find("jaw") != std::string::npos ||
         lower.find("lid") != std::string::npos ||
         lower.find("eye") != std::string::npos;
}

bool is_lower_body_clip_name(const std::string& name) {
  return name == "bone_facing" ||
         name.find("pelvis") != std::string::npos ||
         name.find("-thigh") != std::string::npos ||
         name.find("-knee") != std::string::npos ||
         name.find("-ankle") != std::string::npos ||
         name.find("-foot") != std::string::npos ||
         name.find("-toe") != std::string::npos;
}

void keep_face_channels_only(ghogx::character::CharClip& clip) {
  if (!clip.loaded) return;
  size_t kept = 0;
  size_t total = 0;
  for (auto& frame : clip.frames) {
    total += frame.size();
    frame.erase(std::remove_if(frame.begin(), frame.end(),
                               [](const ghogx::character::ClipChannel& ch) {
                                 return !is_face_channel_name(ch.bone_name);
                               }),
                frame.end());
    kept += frame.size();
  }
  clip.output_bones.erase(
      std::remove_if(clip.output_bones.begin(), clip.output_bones.end(),
                     [](const ghogx::character::CharClip::OutputBone& bone) {
                       return !is_face_channel_name(bone.name);
                     }),
      clip.output_bones.end());
  std::fprintf(stderr, "[char] face-filtered '%s': kept %zu/%zu channels\n",
               clip.name.c_str(), kept, total);
}

void keep_hand_overlay_channels_only(ghogx::character::CharClip& clip) {
  if (!clip.loaded) return;
  size_t kept = 0;
  size_t total = 0;
  for (auto& frame : clip.frames) {
    total += frame.size();
    frame.erase(std::remove_if(frame.begin(), frame.end(),
                               [](const ghogx::character::ClipChannel& ch) {
                                 return is_lower_body_clip_name(ch.bone_name);
                               }),
                frame.end());
    kept += frame.size();
  }
  clip.output_bones.erase(
      std::remove_if(clip.output_bones.begin(), clip.output_bones.end(),
                     [](const ghogx::character::CharClip::OutputBone& bone) {
                       return is_lower_body_clip_name(bone.name);
                     }),
      clip.output_bones.end());
  std::fprintf(stderr,
               "[char] hand-overlay filtered '%s': kept %zu/%zu channels\n",
               clip.name.c_str(), kept, total);
}

bool viewer_auto_hand_overlays_enabled() {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool enabled =
      _dupenv_s(&value, &len, "GHOGX_VIEWER_AUTO_HAND_OVERLAYS") == 0 &&
      value && value[0];
  std::free(value);
  return enabled;
#else
  const char* value = std::getenv("GHOGX_VIEWER_AUTO_HAND_OVERLAYS");
  return value && value[0];
#endif
}

bool controller_audit_env_enabled() {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  bool enabled =
      _dupenv_s(&value, &len, "GHOGX_AUDIT_CHARACTER_GRAPH") == 0 &&
      value && value[0];
  std::free(value);
  value = nullptr;
  len = 0;
  enabled = enabled ||
            (_dupenv_s(&value, &len, "GHOGX_CONTROLLER_AUDIT") == 0 &&
             value && value[0]);
  std::free(value);
  return enabled;
#else
  const char* value = std::getenv("GHOGX_AUDIT_CHARACTER_GRAPH");
  if (value && value[0]) return true;
  value = std::getenv("GHOGX_CONTROLLER_AUDIT");
  return value && value[0];
#endif
}

int run_scene_mode(const std::string& hdr, const std::string& ark,
                   const std::string& milo_path,
                   const std::string& screenshot_path, int screenshot_frame,
                   int max_frames, const CamOverride& cam_ovr,
                   const RenderSize& render_size) {
  using Action = ghogx::render::Window::Action;

  // Decode the scene (runtime-native: read .milo_ps2 from the ARK, decode in
  // memory — no intermediate extraction).
  ghogx::milo_scene::Scene scene;
  if (!ghogx::milo_scene::load_scene(hdr, ark, milo_path, scene)) {
    std::fprintf(stderr, "[scene3d] failed to load scene %s\n", milo_path.c_str());
    return 1;
  }

  // Collect the unique diffuse-texture names the materials reference, then load
  // them (RGBA) from the SAME milo in one parse.
  std::unordered_set<std::string> want;
  for (const auto& m : scene.mats)
    if (!m.diffuse_tex.empty()) want.insert(m.diffuse_tex);
  std::vector<std::string> names(want.begin(), want.end());
  std::map<std::string, ghogx::asset::Image> textures =
      ghogx::asset::load_milo_textures(hdr, ark, milo_path, names);

  auto win = ghogx::render::Window::create(render_size.width,
                                           render_size.height,
                                           "GuitarHeroOGX — scene");
  if (!win) {
    std::fprintf(stderr, "[scene3d] failed to create window/device\n");
    return 1;
  }

  ghogx::render::MiloSceneRenderer renderer(*win);
  renderer.set_scene(std::move(scene), textures);

  // Apply any camera overrides (for dialing in a good venue shot).
  {
    ghogx::render::OrbitCamera& c = renderer.camera();
    if (cam_ovr.has_yaw) c.yaw = cam_ovr.yaw;
    if (cam_ovr.has_pitch) c.pitch = cam_ovr.pitch;
    if (cam_ovr.has_dist) c.distance = cam_ovr.dist;
    if (cam_ovr.has_target) {
      for (int k = 0; k < 3; ++k) c.target[k] = cam_ovr.target[k];
    }
  }

  std::fprintf(stderr,
               "[scene3d] orbit: Left/Right = yaw, Up/Down = pitch, "
               "W/S (or +/-) = zoom. Esc to quit.\n");

  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  const auto target_frame = std::chrono::duration<double>(1.0 / 60.0);
  auto frame_deadline = clock::now();
  uint64_t frame = 0;
  if (!screenshot_path.empty() && max_frames == 0)
    max_frames = screenshot_frame + 3;

  while (!win->should_close()) {
    win->pump();
    if (win->should_close()) break;

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    // Camera controls. Use held guitar/keyboard mapping where possible; the
    // Window exposes edge-triggered Actions, so we also auto-orbit a touch when
    // capturing a screenshot so a single frame still shows a 3/4 view.
    ghogx::render::OrbitCamera& cam = renderer.camera();
    const float yaw_rate = 1.5f * dt, pitch_rate = 1.2f * dt, zoom_rate = 1.0f;
    if (win->action_pressed(Action::Left))  cam.yaw   -= yaw_rate * 10.0f;
    if (win->action_pressed(Action::Right)) cam.yaw   += yaw_rate * 10.0f;
    if (win->action_pressed(Action::Up))    cam.pitch += pitch_rate * 10.0f;
    if (win->action_pressed(Action::Down))  cam.pitch -= pitch_rate * 10.0f;
    // Zoom via guitar-held bits (W=strum-ish); also map Confirm/Back to zoom.
    if (win->action_pressed(Action::Confirm)) cam.distance *= (1.0f - 0.1f * zoom_rate);
    if (win->action_pressed(Action::Back))    cam.distance *= (1.0f + 0.1f * zoom_rate);
    if (cam.pitch > 1.5f) cam.pitch = 1.5f;
    if (cam.pitch < -1.5f) cam.pitch = -1.5f;

    renderer.draw();

    // Screenshot capture before present (DISCARD swap invalidates post-present).
    if (!screenshot_path.empty() && frame == static_cast<uint64_t>(screenshot_frame)) {
      win->save_screenshot(screenshot_path.c_str());
      std::fprintf(stderr, "[scene3d] screenshot -> %s (frame %llu)\n",
                   screenshot_path.c_str(), (unsigned long long)frame);
    }
    win->present();

    ++frame;
    if (max_frames && frame >= static_cast<uint64_t>(max_frames)) break;

    wait_for_next_frame_deadline(
        frame_deadline,
        std::chrono::duration_cast<clock::duration>(target_frame));
  }
  std::fprintf(stderr, "[scene3d] exited after %llu frames\n",
               (unsigned long long)frame);
  return 0;
}

// ---------------------------------------------------------------------------
// --hud-test mode: draw the in-song HUD overlay (score / streak / multiplier /
// star-power meter / rock-crowd meter) from the real hud/gen/*.milo_ps2 art.
// Disjoint from every other mode (own window + loop) so it can't disturb the
// gameplay/scene/char paths.
// ---------------------------------------------------------------------------
struct HudTestOptions {
  int score = 12345;
  int streak = 27;
  int multiplier = 3;
  float sp_fill = 0.6f;
  bool sp_active = false;
  float rock_fill = 0.7f;
  std::string tune_file;
  bool ref_highway = false;
  std::string ref_song = "shoutatthedevil";
  int ref_difficulty = 1;
  double ref_song_time = 12.0;
};

int run_hud_test_mode(const std::string& hdr, const std::string& ark,
                      const std::string& screenshot_path, int screenshot_frame,
                      int max_frames, const HudTestOptions& options,
                      const RenderSize& render_size) {
  auto win = ghogx::render::Window::create(render_size.width,
                                           render_size.height,
                                           "GuitarHeroOGX — HUD test");
  if (!win) {
    std::fprintf(stderr, "[hud-test] failed to create window/device\n");
    return 1;
  }
  auto* dev = static_cast<IDirect3DDevice9*>(win->device_ptr());

  ghogx::hud::HudRenderer hud;
  if (!options.tune_file.empty()) hud.set_layout_tuning_file(options.tune_file);
  if (!hud.load(dev, hdr, ark)) {
    std::fprintf(stderr, "[hud-test] HUD load failed\n");
    return 1;
  }
  ghogx::game::Gameplay ref_gameplay;
  bool ref_highway_ready = false;
  if (options.ref_highway) {
    ref_gameplay.set_deterministic_clock(true);
    ref_gameplay.set_diagnostic_autoplay(true);
    ref_highway_ready = ref_gameplay.load_song(
        hdr, ark, options.ref_song, options.ref_difficulty);
    if (ref_highway_ready && options.ref_song_time > 0.0) {
      ref_gameplay.seek_for_diagnostic_capture(options.ref_song_time);
    }
    std::fprintf(stderr, "[hud-test] highway reference %s song=%s diff=%d time=%.2f\n",
                 ref_highway_ready ? "ready" : "failed",
                 options.ref_song.c_str(), options.ref_difficulty,
                 options.ref_song_time);
  }

  ghogx::hud::HudState st;
  st.score = std::max(0, options.score);
  st.streak = std::max(0, options.streak);
  st.multiplier = std::clamp(options.multiplier, 1, 9);
  st.sp_fill = std::clamp(options.sp_fill, 0.0f, 1.0f);
  st.sp_active = options.sp_active;
  st.rock_fill = std::clamp(options.rock_fill, 0.0f, 1.0f);

  using clock = std::chrono::steady_clock;
  const auto target_frame = std::chrono::duration<double>(1.0 / 60.0);
  auto frame_deadline = clock::now();
  uint64_t frame = 0;
  if (!screenshot_path.empty() && max_frames == 0) max_frames = screenshot_frame + 3;

  std::fprintf(stderr,
               "[hud-test] score=%d streak=%d mult=%dx sp=%.2f active=%d rock=%.2f\n",
               st.score, st.streak, st.multiplier, st.sp_fill,
               st.sp_active ? 1 : 0, st.rock_fill);
  if (!options.tune_file.empty()) {
    std::fprintf(stderr,
                 "[hud-tune] Tab/[ ] select, arrows move, Shift+arrows scale, "
                 "Q/E yaw parents, PgUp/PgDn order, Ctrl=fine, Space=coarse, "
                 "Ctrl+S save -> %s\n",
                 options.tune_file.c_str());
  }

  std::array<bool, 256> prev_keys = {};
  size_t tune_index = 0;
  auto update_tune_title = [&]() {
    if (options.tune_file.empty()) return;
    char title[256];
    std::snprintf(title, sizeof(title), "GuitarHeroOGX - HUD tune [%zu/%zu] %s%s",
                  tune_index + 1, hud.layout_tuning_count(),
                  hud.layout_tuning_name(tune_index),
                  hud.layout_tuning_can_rotate(tune_index) ? " (parent)" : "");
    win->set_title(title);
  };
  update_tune_title();

  while (!win->should_close()) {
    win->pump();
    if (win->should_close()) break;
    auto key_edge = [&](int vk) {
      return vk >= 0 && vk < static_cast<int>(prev_keys.size()) &&
             win->key_down(vk) && !prev_keys[static_cast<size_t>(vk)];
    };

    if (!options.tune_file.empty() && hud.layout_tuning_count() > 0) {
      bool selection_changed = false;
      if (key_edge(VK_TAB) || key_edge(VK_OEM_6)) {
        if (win->key_down(VK_SHIFT)) {
          tune_index = tune_index == 0 ? hud.layout_tuning_count() - 1
                                       : tune_index - 1;
        } else {
          tune_index = (tune_index + 1) % hud.layout_tuning_count();
        }
        selection_changed = true;
      } else if (key_edge(VK_OEM_4)) {
        tune_index = tune_index == 0 ? hud.layout_tuning_count() - 1
                                     : tune_index - 1;
        selection_changed = true;
      }
      if (selection_changed) update_tune_title();

      const bool fine = win->key_down(VK_CONTROL);
      const bool coarse = win->key_down(VK_SPACE);
      const bool scale = win->key_down(VK_SHIFT);
      const float step = fine ? 0.0005f : (coarse ? 0.0200f : 0.0020f);
      const float rot_step = fine ? 0.25f : (coarse ? 10.0f : 1.0f);
      const int z_step = coarse ? 20 : 1;
      float dx = 0.0f, dy = 0.0f, dw = 0.0f, dh = 0.0f;
      float drot = 0.0f;
      int dz = 0;
      if (scale) {
        if (key_edge(VK_LEFT))  dw -= step;
        if (key_edge(VK_RIGHT)) dw += step;
        if (key_edge(VK_UP))    dh -= step;
        if (key_edge(VK_DOWN))  dh += step;
      } else {
        if (key_edge(VK_LEFT))  dx -= step;
        if (key_edge(VK_RIGHT)) dx += step;
        if (key_edge(VK_UP))    dy -= step;
        if (key_edge(VK_DOWN))  dy += step;
      }
      if (hud.layout_tuning_can_rotate(tune_index)) {
        if (key_edge('Q')) drot -= rot_step;
        if (key_edge('E')) drot += rot_step;
      }
      if (key_edge(VK_PRIOR)) dz += z_step;
      if (key_edge(VK_NEXT)) dz -= z_step;
      if ((dx != 0.0f || dy != 0.0f || dw != 0.0f || dh != 0.0f ||
           drot != 0.0f || dz != 0) &&
          hud.nudge_layout_tuning(tune_index, dx, dy, dw, dh, drot, dz)) {
        if (!hud.load(dev, hdr, ark)) {
          std::fprintf(stderr, "[hud-tune] HUD reload failed after nudge\n");
          return 1;
        }
      }
      if (win->key_down(VK_CONTROL) && key_edge('S')) {
        const bool saved = hud.save_layout_tuning_file();
        std::fprintf(stderr, "[hud-tune] %s %s\n",
                     saved ? "saved" : "failed to save",
                     options.tune_file.c_str());
      }
    }

    if (ref_highway_ready) {
      ref_gameplay.draw(*win);
      st.anim_seconds = static_cast<float>(std::max(0.0, ref_gameplay.song_time()));
    } else {
      // Dark stage-ish background so the HUD art reads clearly.
      win->clear(0.06f, 0.06f, 0.09f);
      st.anim_seconds = static_cast<float>(frame) / 60.0f;
    }
    hud.draw(dev, st);

    if (!screenshot_path.empty() && frame == static_cast<uint64_t>(screenshot_frame)) {
      win->save_screenshot(screenshot_path.c_str());
      std::fprintf(stderr, "[hud-test] screenshot -> %s (frame %llu)\n",
                   screenshot_path.c_str(), (unsigned long long)frame);
    }
    win->present();

    ++frame;
    if (max_frames && frame >= static_cast<uint64_t>(max_frames)) break;
    for (size_t i = 0; i < prev_keys.size(); ++i)
      prev_keys[i] = win->key_down(static_cast<int>(i));
    wait_for_next_frame_deadline(
        frame_deadline,
        std::chrono::duration_cast<clock::duration>(target_frame));
  }
  std::fprintf(stderr, "[hud-test] exited after %llu frames\n", (unsigned long long)frame);
  return 0;
}

// ---------------------------------------------------------------------------
// --char mode: load a BandCharacter MILO from the PS2 ARK (body meshes +
// skeleton + textures) and render it in bind pose with an orbit camera the
// user can move. The LOD1 duplicates and shadow decals are skipped; a
// screenshot is taken if --screenshot is supplied.
// ---------------------------------------------------------------------------
struct ViewerClipStackOptions {
  std::string prev_clip_arg;
  std::string prev_strum_clip_arg;
  std::string prev_fret_clip_arg;
  int clip_start_frame = -1;
  int strum_start_frame = -1;
  int fret_start_frame = -1;
  int clip_transition_frame = -1;
  int strum_transition_frame = -1;
  int fret_transition_frame = -1;
  float clip_blend_seconds = 0.25f;
  float hand_blend_seconds = 0.08f;
};

struct CharSelectPlacer {
  int player = -1;
  const char* name = "";
  std::array<float, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
};

std::optional<CharSelectPlacer> two_player_select_placer(int player) {
  if (player == 0) {
    return CharSelectPlacer{
        0,
        "char_multi0.placer",
        {-0.8189f, -0.5734f, 0.0f, 0.0f,
          0.5734f, -0.8189f, 0.0f, 0.0f,
          0.0f,     0.0f,    1.0f, 0.0f,
         -35.0f,  -30.0f,    0.0f, 1.0f}};
  }
  if (player == 1) {
    return CharSelectPlacer{
        1,
        "char_multi1.placer",
        {-0.8190f,  0.5734f, 0.0f, 0.0f,
         -0.5734f, -0.8190f, 0.0f, 0.0f,
          0.0f,     0.0f,    1.0f, 0.0f,
          35.0f,  -30.0f,    0.0f, 1.0f}};
  }
  return std::nullopt;
}

std::array<float, 3> transform_point(const std::array<float, 16>& m,
                                     const std::array<float, 3>& p) {
  return {
      p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
      p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
      p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14],
  };
}

void write_json_string(std::ofstream& out, const std::string& value) {
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          static const char* hex = "0123456789abcdef";
          out << "\\u00" << hex[ch >> 4] << hex[ch & 0x0f];
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  out << '"';
}

void dump_char_pose_mesh_bounds_jsonl(
    const ghogx::character::Character& character,
    const std::string& dump_path,
    uint64_t viewer_frame,
    int clip_frame_override) {
  if (dump_path.empty()) return;
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path path(dump_path);
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path(), error);
    if (error) {
      std::fprintf(stderr, "[char-pose-mesh-dump] mkdir failed %s: %s\n",
                   path.parent_path().string().c_str(), error.message().c_str());
      return;
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "[char-pose-mesh-dump] open failed %s\n",
                 dump_path.c_str());
    return;
  }

  const ghogx::character::SourceCharacterDrawClosure closure =
      ghogx::character::source_character_draw_closure(character, 0);
  std::vector<std::array<float, 3>> posed;
  std::vector<std::array<float, 3>> normals;
  size_t rows = 0;
  for (const auto& mesh : character.meshes) {
    if (!mesh.decoded || !mesh.showing || mesh.verts.empty()) continue;
    if (closure.authoritative &&
        closure.meshes.find(mesh.name) == closure.meshes.end()) {
      continue;
    }
    if (mesh.name.rfind("shadow", 0) == 0 ||
        mesh.parent.rfind("shadow", 0) == 0) {
      continue;
    }
    ghogx::character::skin_to_pose(mesh, character, posed, normals);
    const std::array<float, 16> submission =
        ghogx::character::source_character_mesh_submission_world(mesh,
                                                                 character);
    if (posed.empty()) continue;
    float minv[3] = {0.0f, 0.0f, 0.0f};
    float maxv[3] = {0.0f, 0.0f, 0.0f};
    bool have_bounds = false;
    std::vector<float> face_band_weights(mesh.bone_palette.size(), 0.0f);
    size_t face_band_vertices = 0;
    size_t face_band_positive_y_vertices = 0;
    for (size_t vertex_index = 0; vertex_index < posed.size(); ++vertex_index) {
      const auto& local = posed[vertex_index];
      const std::array<float, 3> world = transform_point(submission, local);
      if (!have_bounds) {
        for (int k = 0; k < 3; ++k) minv[k] = maxv[k] = world[k];
        have_bounds = true;
      } else {
        for (int k = 0; k < 3; ++k) {
          minv[k] = std::min(minv[k], world[k]);
          maxv[k] = std::max(maxv[k], world[k]);
        }
      }
      if (world[2] >= 45.0f && world[2] <= 70.0f) {
        ++face_band_vertices;
        if (world[1] > 5.0f) ++face_band_positive_y_vertices;
        if (vertex_index < mesh.verts.size()) {
          for (size_t i = 0; i < mesh.bone_palette.size(); ++i) {
            face_band_weights[i] += std::fabs(mesh.verts[vertex_index].w[i]);
          }
        }
      }
    }
    if (!have_bounds) continue;

    std::vector<std::pair<float, std::string>> bone_weights;
    bone_weights.reserve(mesh.bone_palette.size());
    for (size_t i = 0; i < mesh.bone_palette.size(); ++i) {
      float total = 0.0f;
      for (const auto& vertex : mesh.verts) total += std::fabs(vertex.w[i]);
      bone_weights.push_back({total, mesh.bone_palette[i]});
    }
    std::sort(bone_weights.begin(), bone_weights.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::pair<float, std::string>> face_band_bone_weights;
    face_band_bone_weights.reserve(mesh.bone_palette.size());
    for (size_t i = 0; i < mesh.bone_palette.size(); ++i) {
      face_band_bone_weights.push_back({face_band_weights[i],
                                        mesh.bone_palette[i]});
    }
    std::sort(face_band_bone_weights.begin(), face_band_bone_weights.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    const bool face_height_overlap = maxv[2] >= 45.0f && minv[2] <= 70.0f;

    out << "{\"viewer_frame\":" << viewer_frame
        << ",\"clip_frame\":" << clip_frame_override
        << ",\"mesh\":";
    write_json_string(out, mesh.name);
    out << ",\"parent\":";
    write_json_string(out, mesh.parent);
    out << ",\"material\":";
    write_json_string(out, mesh.material);
    out << ",\"vertices\":" << mesh.verts.size()
        << ",\"faces\":" << (mesh.indices.size() / 3)
        << ",\"face_height_overlap\":" << (face_height_overlap ? "true" : "false")
        << ",\"face_band_vertices\":" << face_band_vertices
        << ",\"face_band_positive_y_vertices\":"
        << face_band_positive_y_vertices
        << ",\"bounds\":{\"min\":[" << minv[0] << ',' << minv[1] << ',' << minv[2]
        << "],\"max\":[" << maxv[0] << ',' << maxv[1] << ',' << maxv[2]
        << "],\"center\":[" << (minv[0] + maxv[0]) * 0.5f << ','
        << (minv[1] + maxv[1]) * 0.5f << ','
        << (minv[2] + maxv[2]) * 0.5f << "]}"
        << ",\"palette\":[";
    for (size_t i = 0; i < mesh.bone_palette.size(); ++i) {
      if (i) out << ',';
      write_json_string(out, mesh.bone_palette[i]);
    }
    out << "],\"dominant_bones\":[";
    const size_t limit = std::min<size_t>(bone_weights.size(), 4);
    for (size_t i = 0; i < limit; ++i) {
      if (i) out << ',';
      out << "{\"bone\":";
      write_json_string(out, bone_weights[i].second);
      out << ",\"weight_sum\":" << bone_weights[i].first << '}';
    }
    out << "],\"face_band_dominant_bones\":[";
    const size_t face_limit = std::min<size_t>(face_band_bone_weights.size(), 4);
    for (size_t i = 0; i < face_limit; ++i) {
      if (i) out << ',';
      out << "{\"bone\":";
      write_json_string(out, face_band_bone_weights[i].second);
      out << ",\"weight_sum\":" << face_band_bone_weights[i].first << '}';
    }
    out << "]}\n";
    ++rows;
  }
  std::fprintf(stderr, "[char-pose-mesh-dump] wrote %zu rows -> %s\n",
               rows, dump_path.c_str());
}

void dump_char_deformed_pose_binary(
    const ghogx::character::Character& character,
    const std::string& dump_path,
    int clip_frame_override) {
  if (dump_path.empty()) return;
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path path(dump_path);
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), error);
  if (error) {
    std::fprintf(stderr, "[char-deformed-dump] mkdir failed %s: %s\n",
                 path.parent_path().string().c_str(), error.message().c_str());
    return;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "[char-deformed-dump] open failed %s\n",
                 dump_path.c_str());
    return;
  }
  auto write_u32 = [&](uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  auto write_f32 = [&](float value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  auto write_string = [&](const std::string& value) {
    write_u32(static_cast<uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
  };

  const ghogx::character::SourceCharacterDrawClosure closure =
      ghogx::character::source_character_draw_closure(character, 0);
  std::vector<const ghogx::character::SkinnedMesh*> meshes;
  for (const auto& mesh : character.meshes) {
    if (!mesh.decoded || !mesh.showing || mesh.verts.empty()) continue;
    if (closure.authoritative &&
        closure.meshes.find(mesh.name) == closure.meshes.end()) {
      continue;
    }
    if (mesh.name.rfind("shadow", 0) == 0 ||
        mesh.parent.rfind("shadow", 0) == 0) {
      continue;
    }
    meshes.push_back(&mesh);
  }

  out.write("GH2POSED", 8);
  write_u32(2);
  write_u32(static_cast<uint32_t>(std::max(clip_frame_override, 0)));
  write_u32(static_cast<uint32_t>(meshes.size()));
  std::vector<std::array<float, 3>> posed;
  std::vector<std::array<float, 3>> normals;
  for (const auto* mesh : meshes) {
    ghogx::character::skin_to_pose(*mesh, character, posed, normals);
    const auto submission =
        ghogx::character::source_character_mesh_submission_world(*mesh,
                                                                 character);
    write_string(mesh->name);
    write_string(mesh->material);
    write_u32(static_cast<uint32_t>(posed.size()));
    for (size_t index = 0; index < posed.size(); ++index) {
      const auto world_position = transform_point(submission, posed[index]);
      auto world_normal = std::array<float, 3>{
          normals[index][0] * submission[0] +
              normals[index][1] * submission[4] +
              normals[index][2] * submission[8],
          normals[index][0] * submission[1] +
              normals[index][1] * submission[5] +
              normals[index][2] * submission[9],
          normals[index][0] * submission[2] +
              normals[index][1] * submission[6] +
              normals[index][2] * submission[10]};
      const float length = std::sqrt(world_normal[0] * world_normal[0] +
                                     world_normal[1] * world_normal[1] +
                                     world_normal[2] * world_normal[2]);
      if (length > 1.0e-8f) {
        for (float& value : world_normal) value /= length;
      }
      for (float value : world_position) write_f32(value);
      for (float value : world_normal) write_f32(value);
      write_f32(mesh->verts[index].u);
      write_f32(mesh->verts[index].v);
    }
    write_u32(static_cast<uint32_t>(mesh->indices.size()));
    for (const uint16_t index : mesh->indices) write_u32(index);
  }
  out.close();
  std::fprintf(stderr,
               "[char-deformed-dump] wrote %zu meshes frame=%d -> %s\n",
               meshes.size(), clip_frame_override, dump_path.c_str());
}

int run_char_mode(const std::string& hdr, const std::string& ark,
                  const std::string& milo_path,
                  const std::string& screenshot_path, int screenshot_frame,
                  int max_frames, const CamOverride& cam_ovr = {},
                  const std::string& clip_arg = "",
                  int clip_frame_override = -1,
                  const std::string& guitar_milo = "",
                  const std::string& strum_clip_arg = "",
                  const std::string& fret_clip_arg = "",
                  const std::string& face_clip_arg = "",
                  const std::string& char_scene_milo = "",
                  const std::array<float, 3>& char_offset = {0.0f, 0.0f, 0.0f},
                  int two_player_select_placer_player = -1,
                  const std::string& two_player_select_event = "animate",
                  float fixed_dt = 0.0f,
                  bool character_controllers = true,
                   bool reference_base = false,
                   const std::string& midi_fret_target = "",
                   const ViewerClipStackOptions& clip_stack_options = {},
                   const ScreenshotSequence& screenshot_sequence = {},
                   const std::string& char_pose_mesh_dump = {},
                   const std::string& char_deformed_pose_dump = {},
                   const RenderSize& render_size = RenderSize{}) {
  ghogx::character::Character character;
  if (!ghogx::character::load_character(hdr, ark, milo_path, character)) {
    std::fprintf(stderr, "[char] failed to load %s\n", milo_path.c_str());
    return 1;
  }
  std::optional<ghogx::character::FaceFxPose> facefx_neutral =
      ghogx::character::load_facefx_pose(hdr, ark, milo_path, character,
                                         "Neutral");
  const bool facefx_neutral_loaded =
      facefx_neutral_enabled() && facefx_neutral.has_value();
  if (facefx_neutral_loaded) {
    ghogx::character::apply_facefx_pose(*facefx_neutral, 1.0f, character);
  }
  dump_facefx_lip_sync_servos(character);
  dump_face_asset_inventory(character);
  const std::vector<std::string> facefx_viseme_milos =
      ghogx::character::facefx_viseme_milo_candidates(
          milo_path, character.lip_sync_servos);
  const std::vector<ghogx::character::CharDriver> character_drivers =
      character.drivers;
  std::unordered_map<std::string, float> character_weights;
  for (const auto& setter : character.weight_setters) {
    character_weights[setter.name] = setter.weight;
    if (!setter.weight_prop.empty()) character_weights[setter.weight_prop] = setter.weight;
  }
  auto controller_weight = [&](const std::string& prop,
                               float fallback = 1.0f) -> float {
    auto it = character_weights.find(prop);
    return it == character_weights.end() ? fallback : it->second;
  };
  const auto right_hand_weight_override = env_float("GHOGX_RIGHT_WEIGHT");
  const auto left_hand_weight_override = env_float("GHOGX_LEFT_WEIGHT");
  float right_hand_weight =
      std::clamp(right_hand_weight_override.value_or(
                     controller_weight("right.weight", 0.0f)),
                 0.0f, 1.0f);
  float left_hand_weight =
      std::clamp(left_hand_weight_override.value_or(
                     controller_weight("left.weight", 0.0f)),
                 0.0f, 1.0f);
  std::fprintf(stderr, "[char] loaded '%s' — %zu meshes, %zu bones, %zu mats\n",
               character.dir_name.c_str(), character.meshes.size(),
               character.bones.size(), character.mats.size());
  if (facefx_neutral_loaded)
    std::fprintf(stderr, "[char] applied FaceFX neutral pose\n");

  // Collect texture names from materials and load them.
  auto tex_names = character.texture_names();
  auto textures = ghogx::asset::load_milo_textures(hdr, ark, milo_path, tex_names);
  std::fprintf(stderr, "[char] loaded %zu/%zu textures\n",
               textures.size(), tex_names.size());

  auto win = ghogx::render::Window::create(render_size.width,
                                           render_size.height,
                                           "GuitarHeroOGX — character");
  if (!win) { std::fprintf(stderr, "[char] window failed\n"); return 1; }

  ghogx::character::CharRenderer renderer(*win);
  renderer.set_character(std::move(character), textures);
  renderer.set_world_offset(char_offset[0], char_offset[1], char_offset[2]);
  if (two_player_select_placer_player >= 0) {
    const auto placer = two_player_select_placer(two_player_select_placer_player);
    if (!placer) {
      std::fprintf(stderr,
                   "[char] invalid --char-2p-select-placer value %d "
                   "(expected 0 or 1)\n",
                   two_player_select_placer_player);
      return 2;
    }
    if (two_player_select_event != "animate" &&
        two_player_select_event != "select") {
      std::fprintf(stderr,
                   "[char] invalid --char-2p-select-event value %s "
                   "(expected animate or select)\n",
                   two_player_select_event.c_str());
      return 2;
    }
    auto& cam = renderer.camera();
    const std::array<float, 3> old_target = {
        cam.target[0], cam.target[1], cam.target[2]};
    const auto new_target = transform_point(placer->matrix, old_target);
    renderer.set_world_transform(placer->matrix);
    cam.target[0] = new_target[0];
    cam.target[1] = new_target[1];
    cam.target[2] = new_target[2];
    std::fprintf(stderr,
                 "[2p-select] applied_placer=%s player=%d matrix=matrix0 "
                 "source=ui/gen/multi_sel_character.milo_ps2 "
                 "owner=multi_sel_character_panel target=spot_ui.mesh "
                 "translation=(%.1f %.1f %.1f)\n",
                 placer->name, placer->player, placer->matrix[12],
                 placer->matrix[13], placer->matrix[14]);
    std::fprintf(stderr,
                 "[2p-select] script=ui/gen/multiplayer.dtb "
                 "screen=multi_sel_character_screen panel=char_multi "
                 "event=%s clip=ui_loop skips_ui_enter=true "
                 "char_objects=char/gen/char_objects.dtb "
                 "reset_hair=%s "
                 "placers=char_multi0.placer,char_multi1.placer\n",
                 two_player_select_event.c_str(),
                 two_player_select_event == "animate" ? "true" : "false");
  }
  renderer.set_reference_base(reference_base);

  std::optional<ghogx::render::MiloSceneRenderer> scene_renderer;
  if (!char_scene_milo.empty()) {
    ghogx::milo_scene::Scene scene;
    if (ghogx::milo_scene::load_scene(hdr, ark, char_scene_milo, scene)) {
      std::unordered_set<std::string> want;
      for (const auto& mat : scene.mats)
        if (!mat.diffuse_tex.empty()) want.insert(mat.diffuse_tex);
      std::vector<std::string> scene_tex_names(want.begin(), want.end());
      auto scene_textures =
          ghogx::asset::load_milo_textures(hdr, ark, char_scene_milo,
                                           scene_tex_names);
      scene_renderer.emplace(*win);
      scene_renderer->set_scene(std::move(scene), scene_textures);
      std::fprintf(stderr, "[char] venue scene loaded: %s\n",
                   char_scene_milo.c_str());
    } else {
      std::fprintf(stderr, "[char] failed to load venue scene %s\n",
                   char_scene_milo.c_str());
    }
  }

  if (!guitar_milo.empty() && guitar_milo != "none") {
    ghogx::milo_scene::Scene guitar_scene;
    if (ghogx::milo_scene::load_scene(hdr, ark, guitar_milo, guitar_scene)) {
      std::unordered_set<std::string> unique_tex;
      for (const auto& mat : guitar_scene.mats) {
        if (!mat.diffuse_tex.empty()) unique_tex.insert(mat.diffuse_tex);
      }
      std::vector<std::string> guitar_tex_names(unique_tex.begin(),
                                                unique_tex.end());
      auto guitar_textures =
          ghogx::asset::load_milo_textures(hdr, ark, guitar_milo,
                                           guitar_tex_names);
      renderer.set_attached_prop(std::move(guitar_scene), guitar_textures,
                                 "bone_pos_guitar.mesh");
    } else {
      std::fprintf(stderr, "[char] failed to load guitar prop %s\n",
                   guitar_milo.c_str());
    }
  }

  // If a --clip was specified, load ALL frames for real-time playback.
  ghogx::character::CharClip loaded_clip;
  ghogx::character::CharClip prev_clip;
  ghogx::character::CharClip strum_clip;
  ghogx::character::CharClip prev_strum_clip;
  ghogx::character::CharClip fret_clip;
  ghogx::character::CharClip prev_fret_clip;
  ghogx::character::CharClip face_base_clip;
  ghogx::character::CharClip facefx_viseme_clip;
  ghogx::character::CharClip face_clip;
  ghogx::character::CharClipPlayer main_player;
  ghogx::character::CharClipPlayer strum_player;
  ghogx::character::CharClipPlayer fret_player;
  ghogx::character::CharClipPlayer face_base_player;
  ghogx::character::CharClipPlayer face_player;
  double pose_time = 0.0;
  auto load_clip_spec = [&](const std::string& spec) {
    ghogx::character::CharClip clip;
    if (spec.size() >= 4 && spec.substr(spec.size() - 4) == ".acp") {
      return ghogx::character::load_acp_clip(hdr, ark, spec);
    }
    auto colon = spec.find(':');
    if (colon != std::string::npos) {
      std::string clip_milo = spec.substr(0, colon);
      std::string clip_name = spec.substr(colon + 1);
      if (clip_milo.size() >= 4 &&
          clip_milo.substr(clip_milo.size() - 4) == ".acp") {
        clip = ghogx::character::load_acp_clip(hdr, ark, clip_milo);
        if (clip.loaded &&
            (clip_name.empty() || clip.name == clip_name)) {
          return clip;
        }
        return ghogx::character::CharClip{};
      }
      clip = ghogx::character::load_clip(hdr, ark, clip_milo, clip_name);
      if (clip.loaded) return clip;

      const std::string requested_role = animation_milo_role(clip_milo);
      if (!requested_role.empty()) {
        const std::string requested_milo = normalize_milo_path(clip_milo);
        for (const auto& driver : character_drivers) {
          if (driver.clip_milo.empty()) continue;
          for (const auto& candidate :
               driver_milo_candidates(milo_path, driver.clip_milo)) {
            if (candidate == requested_milo ||
                animation_milo_role(candidate) != requested_role) {
              continue;
            }
            clip = ghogx::character::load_clip(hdr, ark, candidate, clip_name);
            if (clip.loaded) {
              std::fprintf(stderr,
                           "[clip] resolved shared driver milo: %s -> %s "
                           "via %s\n",
                           requested_milo.c_str(), candidate.c_str(),
                           driver.name.c_str());
              return clip;
            }
          }
        }
      }
    }
    return clip;
  };
  auto load_first_clip = [&](const std::vector<std::string>& milos,
                             const std::string& clip_name) {
    ghogx::character::CharClip clip;
    for (const auto& m : milos) {
      clip = ghogx::character::load_clip(hdr, ark, m, clip_name);
      if (clip.loaded) return clip;
    }
    return clip;
  };
  auto load_driver_clip = [&](const std::string& driver_name,
                              const std::string& clip_name) {
    ghogx::character::CharClip clip;
    for (const auto& driver : character_drivers) {
      if (driver.name != driver_name || driver.clip_milo.empty()) continue;
      clip = load_first_clip(driver_milo_candidates(milo_path, driver.clip_milo),
                             clip_name);
      if (clip.loaded) return clip;
    }
    return clip;
  };
  auto main_driver_clip_milo = [&]() -> std::string {
    for (const auto& driver : character_drivers) {
      if (driver.name == "main.drv") return normalize_milo_path(driver.clip_milo);
    }
    return {};
  };
  auto default_main_clip_name = [&]() -> std::string {
    const std::string main_milo = main_driver_clip_milo();
    if (main_milo.find("bass_main") != std::string::npos) {
      return "bassist_idle_medium_01";
    }
    if (main_milo.find("singer_main") != std::string::npos) {
      return "singer_idle_medium_01";
    }
    if (main_milo.find("drummer_main") != std::string::npos) {
      return "drummer_idle";
    }
    return (!guitar_milo.empty() && guitar_milo != "none") ? "idle_medium_01"
                                                           : "stand_medium_01";
  };
  if (!clip_arg.empty()) {
    loaded_clip = load_clip_spec(clip_arg);
  }
  if (!clip_stack_options.prev_clip_arg.empty()) {
    prev_clip = load_clip_spec(clip_stack_options.prev_clip_arg);
  }
  std::string resolved_strum_clip_arg = strum_clip_arg;
  std::string resolved_fret_clip_arg = fret_clip_arg;
  std::string resolved_face_clip_arg = face_clip_arg;
  const std::string marker = "/og/gen/";
  const size_t marker_at = milo_path.find(marker);
  const size_t slash_at = milo_path.find_last_of("/\\");
  const size_t dot_at = milo_path.rfind(".milo_ps2");
  std::string base_dir;
  std::string stem;
  if (marker_at != std::string::npos && slash_at != std::string::npos &&
      dot_at != std::string::npos && dot_at > slash_at) {
    base_dir = milo_path.substr(0, marker_at);
    stem = milo_path.substr(slash_at + 1, dot_at - slash_at - 1);
    const std::string horse_suffix = "_horse";
    if (stem.size() > horse_suffix.size() &&
        stem.compare(stem.size() - horse_suffix.size(), horse_suffix.size(),
                     horse_suffix) == 0) {
      stem.resize(stem.size() - horse_suffix.size());
    }
  }
  if (viewer_auto_hand_overlays_enabled() &&
      (!guitar_milo.empty() && guitar_milo != "none") &&
      (resolved_strum_clip_arg.empty() || resolved_fret_clip_arg.empty())) {
    if (resolved_strum_clip_arg.empty()) {
      strum_clip = load_driver_clip("right_hand.drv", "strum_long_01");
    }
    if (resolved_fret_clip_arg.empty()) {
      fret_clip = load_driver_clip("left_hand.drv", "finger_powerchord_1");
    }
    if ((!strum_clip.loaded || !fret_clip.loaded) && !base_dir.empty() &&
        !stem.empty()) {
      if (resolved_strum_clip_arg.empty() && !strum_clip.loaded) {
        resolved_strum_clip_arg =
            base_dir + "/anims/gen/" + stem + "_strum.milo_ps2:strum_long_01";
      }
      if (resolved_fret_clip_arg.empty() && !fret_clip.loaded) {
        resolved_fret_clip_arg =
            base_dir + "/anims/gen/" + stem + "_fret.milo_ps2:finger_powerchord_1";
      }
    }
  }
  if (!resolved_strum_clip_arg.empty()) strum_clip = load_clip_spec(resolved_strum_clip_arg);
  if (!resolved_fret_clip_arg.empty()) fret_clip = load_clip_spec(resolved_fret_clip_arg);
  if (!clip_stack_options.prev_strum_clip_arg.empty()) {
    prev_strum_clip = load_clip_spec(clip_stack_options.prev_strum_clip_arg);
  }
  if (!clip_stack_options.prev_fret_clip_arg.empty()) {
    prev_fret_clip = load_clip_spec(clip_stack_options.prev_fret_clip_arg);
  }
  keep_hand_overlay_channels_only(strum_clip);
  keep_hand_overlay_channels_only(fret_clip);
  keep_hand_overlay_channels_only(prev_strum_clip);
  keep_hand_overlay_channels_only(prev_fret_clip);
  if (clip_arg.empty()) {
    const std::string default_main_clip = default_main_clip_name();
    loaded_clip = load_driver_clip("main.drv", default_main_clip);
    if (!loaded_clip.loaded && !base_dir.empty() && !stem.empty()) {
      loaded_clip = load_clip_spec(
          base_dir + "/anims/gen/" + stem + "_main.milo_ps2:" +
          default_main_clip);
    }
    if (!loaded_clip.loaded && default_main_clip != "stand_medium_01") {
      loaded_clip = load_driver_clip("main.drv", "stand_medium_01");
      if (!loaded_clip.loaded && !base_dir.empty() && !stem.empty()) {
        loaded_clip = load_clip_spec(
            base_dir + "/anims/gen/" + stem + "_main.milo_ps2:stand_medium_01");
      }
    }
  }
  if (resolved_face_clip_arg.empty()) {
    face_base_clip = load_first_clip(facefx_viseme_milos, "neutral");
  } else if (resolved_face_clip_arg != "none") {
    if (resolved_face_clip_arg.find(":neutral") == std::string::npos) {
      face_base_clip = load_first_clip(facefx_viseme_milos, "neutral");
    }
    face_clip = load_clip_spec(resolved_face_clip_arg);
  }
  if (resolved_face_clip_arg != "none") {
    facefx_viseme_clip = load_first_clip(facefx_viseme_milos, "visemes");
  }
  keep_face_channels_only(face_base_clip);
  keep_face_channels_only(face_clip);
  keep_face_channels_only(facefx_viseme_clip);
  dump_face_clip_inventory("face_base", face_base_clip);
  dump_face_clip_inventory("face", face_clip);
  dump_face_clip_inventory("facefx_visemes", facefx_viseme_clip);
  const bool viewer_hand_ik_weights_active =
      right_hand_weight_override || left_hand_weight_override ||
      ((!guitar_milo.empty() && guitar_milo != "none") &&
       (strum_clip.loaded || fret_clip.loaded));
  const bool viewer_hand_ik_manual_override_active =
      right_hand_weight_override || left_hand_weight_override;
  auto play_viewer_stack =
      [](ghogx::character::CharClipPlayer& player,
         const ghogx::character::CharClip& current,
         const ghogx::character::CharClip& previous, int transition_frame,
         int start_frame, float blend_seconds, uint32_t fallback_flags,
         const char* label) {
        if (start_frame >= 0) {
          std::fprintf(stderr,
                       "[char] viewer stack %s: delayed start frame=%d "
                       "prev=%s current=%s blend=%.3f transition_frame=%d\n",
                       label, start_frame,
                       previous.loaded ? previous.name.c_str() : "<none>",
                       current.loaded ? current.name.c_str() : "<none>",
                       blend_seconds, transition_frame);
          return;
        }
        if (previous.loaded) {
          player.play(previous, ghogx::character::kCharPlayNoLoop);
          if (current.loaded && transition_frame < 0) {
            player.play(current, ghogx::character::kCharPlayNoLoop,
                        blend_seconds);
            std::fprintf(stderr,
                         "[char] viewer stack %s: prev=%s current=%s "
                         "blend=%.3f immediate\n",
                         label, previous.name.c_str(), current.name.c_str(),
                         blend_seconds);
          } else {
            std::fprintf(stderr,
                         "[char] viewer stack %s: prev=%s current=%s "
                         "blend=%.3f transition_frame=%d\n",
                         label, previous.name.c_str(),
                         current.loaded ? current.name.c_str() : "<none>",
                         blend_seconds, transition_frame);
          }
        } else if (current.loaded) {
          player.play(current, fallback_flags);
        }
      };
  play_viewer_stack(main_player, loaded_clip, prev_clip,
                    clip_stack_options.clip_transition_frame,
                    clip_stack_options.clip_start_frame,
                    clip_stack_options.clip_blend_seconds,
                    ghogx::character::kCharPlayLoop |
                        ghogx::character::kCharPlayNoBlend,
                    "main");
  play_viewer_stack(strum_player, strum_clip, prev_strum_clip,
                    clip_stack_options.strum_transition_frame,
                    clip_stack_options.strum_start_frame,
                    clip_stack_options.hand_blend_seconds,
                    ghogx::character::kCharPlayLoop |
                        ghogx::character::kCharPlayNoBlend,
                    "strum");
  if (strum_clip.loaded || prev_strum_clip.loaded) {
    std::fprintf(stderr, "[char] right.weight = %.3f\n", right_hand_weight);
  }
  play_viewer_stack(fret_player, fret_clip, prev_fret_clip,
                    clip_stack_options.fret_transition_frame,
                    clip_stack_options.fret_start_frame,
                    clip_stack_options.hand_blend_seconds,
                    ghogx::character::kCharPlayLoop |
                        ghogx::character::kCharPlayNoBlend,
                    "fret");
  if (fret_clip.loaded || prev_fret_clip.loaded) {
    std::fprintf(stderr, "[char] left.weight = %.3f\n", left_hand_weight);
  }
  if (face_base_clip.loaded) {
    face_base_player.play(face_base_clip,
                          ghogx::character::kCharPlayLoop |
                              ghogx::character::kCharPlayNoBlend);
  }
  if (face_clip.loaded) {
    face_player.play(face_clip,
                     ghogx::character::kCharPlayLoop |
                         ghogx::character::kCharPlayNoBlend);
  }

  // Apply command-line camera overrides after frame_camera() sets defaults.
  { auto& c = renderer.camera();
    if (cam_ovr.has_yaw)    c.yaw      = cam_ovr.yaw;
    if (cam_ovr.has_pitch)  c.pitch    = cam_ovr.pitch;
    if (cam_ovr.has_dist)   c.distance = cam_ovr.dist;
    if (cam_ovr.has_target) { c.target[0]=cam_ovr.target[0]; c.target[1]=cam_ovr.target[1]; c.target[2]=cam_ovr.target[2]; }
  }

  std::fprintf(stderr,
               "[char] orbit: arrows = yaw/pitch, Q/E = zoom, W/S = target up/down. "
               "Esc to quit.\n");

  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  const auto target_frame = std::chrono::duration<double>(1.0 / 60.0);
  auto frame_deadline = clock::now();
  uint64_t frame = 0;
  if (!screenshot_path.empty() && max_frames == 0)
    max_frames = screenshot_frame + 3;
  if (!screenshot_sequence.empty() && max_frames == 0)
    max_frames =
        static_cast<int>(screenshot_sequence.rbegin()->first + 3);
  if (fixed_dt > 0.0f) {
    std::fprintf(stderr, "[char] fixed dt enabled: %.6f\n", fixed_dt);
  }
  if (!character_controllers) {
    std::fprintf(stderr, "[char] character controllers disabled for diagnostic capture\n");
  }
  if (reference_base) {
    std::fprintf(stderr, "[char] reference base enabled\n");
  }
  if (!midi_fret_target.empty()) {
    std::fprintf(stderr, "[char] midi fret target: %s\n",
                 midi_fret_target.c_str());
  }
  if (clip_frame_override >= 0) {
    std::fprintf(stderr, "[char] clip-frame override enabled: %d\n",
                 clip_frame_override);
  }
  bool char_pose_mesh_dump_written = false;

  auto evaluate_viewer_main_driver_hand_weights =
      [&]() -> ghogx::character::SourceCharMainDriverHandWeights {
    ghogx::character::SourceCharMainDriverHandWeights result;
    result.left = left_hand_weight;
    result.right = right_hand_weight;
    if (viewer_hand_ik_manual_override_active) return result;
    if (clip_frame_override >= 0 && loaded_clip.loaded) {
      return ghogx::character::source_char_main_driver_hand_weights_from_clip_flags(
          renderer.character(), loaded_clip.flags, left_hand_weight,
          right_hand_weight);
    }
    return ghogx::character::source_char_main_driver_hand_weights_from_player(
        renderer.character(), main_player.active() ? &main_player : nullptr,
        left_hand_weight, right_hand_weight);
  };
  auto log_viewer_main_driver_flags =
      [&](const ghogx::character::SourceCharMainDriverHandWeights& weights) {
    for (const auto& flag : weights.driver_flags) {
      if (controller_audit_env_enabled()) {
        std::fprintf(stderr,
                     "[driver-flags] %s flags=0x%08x weight=%.5f "
                     "clipFlags=0x%08x source=%s\n",
                     flag.driver.c_str(), flag.flags, flag.weight,
                     loaded_clip.loaded ? loaded_clip.flags : 0u,
                     clip_frame_override >= 0 ? "frame-override"
                                              : "player");
      }
    }
  };

  while (!win->should_close()) {
    win->pump();
    if (win->should_close()) break;

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (fixed_dt > 0.0f) dt = fixed_dt;
    if (dt > 0.1f) dt = 0.1f;
    pose_time += dt;

    ghogx::render::OrbitCamera& cam = renderer.camera();
    const float yaw_rate = 1.8f * dt, pitch_rate = 1.4f * dt;
    if (win->key_down(0x25)) cam.yaw   -= yaw_rate * 12.0f; // Left
    if (win->key_down(0x27)) cam.yaw   += yaw_rate * 12.0f; // Right
    if (win->key_down(0x26)) cam.pitch += pitch_rate * 12.0f; // Up
    if (win->key_down(0x28)) cam.pitch -= pitch_rate * 12.0f; // Down
    if (win->key_down('Q')) cam.distance *= std::pow(0.12f, dt);
    if (win->key_down('E')) cam.distance *= std::pow(8.5f, dt);
    if (win->key_down('W')) cam.target[2] += 55.0f * dt;
    if (win->key_down('S')) cam.target[2] -= 55.0f * dt;
    if (cam.pitch >  1.4f) cam.pitch =  1.4f;
    if (cam.pitch < -1.4f) cam.pitch = -1.4f;
    if (cam.distance < 8.0f) cam.distance = 8.0f;

    renderer.update(dt);
    // Real-time clip playback through a viewer-side CharDriver play stack.
    ghogx::character::SourceCharMainDriverHandWeights frame_hand_weights;
    ghogx::character::ClipChannelLayerStack pose_stack;
    pose_stack.debug_label = "viewer";
    if (clip_frame_override >= 0) {
      frame_hand_weights = evaluate_viewer_main_driver_hand_weights();
      ghogx::character::CharacterPoseFrameLayerBuildSources frame_inputs;
      frame_inputs.main = &loaded_clip;
      frame_inputs.face_base = &face_base_clip;
      frame_inputs.strum = &strum_clip;
      frame_inputs.fret = &fret_clip;
      frame_inputs.face = &face_clip;
      frame_inputs.hand_weights = &frame_hand_weights;
      frame_inputs.frame_idx = clip_frame_override;
      const ghogx::character::CharacterPoseFrameLayerSources frame_layers =
          ghogx::character::make_character_pose_frame_layer_sources(
              frame_inputs);
      ghogx::character::append_character_pose_frame_layers(pose_stack,
                                                           frame_layers);
    } else {
      main_player.advance(dt);
      strum_player.advance(dt);
      fret_player.advance(dt);
      face_base_player.advance(dt);
      face_player.advance(dt);
      auto start_scheduled_viewer_stack =
          [&](ghogx::character::CharClipPlayer& player,
              const ghogx::character::CharClip& current,
              const ghogx::character::CharClip& previous, int start_frame,
              int transition_frame, float blend_seconds,
              uint32_t fallback_flags, const char* label) {
            if (start_frame < 0 ||
                frame != static_cast<uint64_t>(start_frame)) {
              return;
            }
            if (previous.loaded) {
              player.play(previous, ghogx::character::kCharPlayNoLoop);
              if (current.loaded && transition_frame < 0) {
                player.play(current, ghogx::character::kCharPlayNoLoop,
                            blend_seconds);
                std::fprintf(stderr,
                             "[char] viewer stack %s: start prev=%s "
                             "current=%s blend=%.3f frame=%llu\n",
                             label, previous.name.c_str(), current.name.c_str(),
                             blend_seconds, (unsigned long long)frame);
              } else {
                std::fprintf(stderr,
                             "[char] viewer stack %s: start prev=%s "
                             "current=%s blend=%.3f frame=%llu "
                             "transition_frame=%d\n",
                             label, previous.name.c_str(),
                             current.loaded ? current.name.c_str() : "<none>",
                             blend_seconds, (unsigned long long)frame,
                             transition_frame);
              }
            } else if (current.loaded) {
              player.play(current, fallback_flags);
              std::fprintf(stderr,
                           "[char] viewer stack %s: start current=%s "
                           "flags=0x%08x frame=%llu\n",
                           label, current.name.c_str(), fallback_flags,
                           (unsigned long long)frame);
            }
          };
      auto trigger_scheduled_viewer_stack =
          [&](ghogx::character::CharClipPlayer& player,
              const ghogx::character::CharClip& clip, int transition_frame,
              float blend_seconds, const char* label) {
            if (!clip.loaded || transition_frame < 0 ||
                frame != static_cast<uint64_t>(transition_frame)) {
              return;
            }
            player.play(clip, ghogx::character::kCharPlayNoLoop,
                        blend_seconds);
            std::fprintf(stderr,
                         "[char] viewer stack %s: trigger current=%s "
                         "blend=%.3f frame=%llu\n",
                         label, clip.name.c_str(), blend_seconds,
                         (unsigned long long)frame);
          };
      start_scheduled_viewer_stack(
          main_player, loaded_clip, prev_clip, clip_stack_options.clip_start_frame,
          clip_stack_options.clip_transition_frame,
          clip_stack_options.clip_blend_seconds,
          ghogx::character::kCharPlayLoop |
              ghogx::character::kCharPlayNoBlend,
          "main");
      start_scheduled_viewer_stack(
          strum_player, strum_clip, prev_strum_clip,
          clip_stack_options.strum_start_frame,
          clip_stack_options.strum_transition_frame,
          clip_stack_options.hand_blend_seconds,
          ghogx::character::kCharPlayLoop |
              ghogx::character::kCharPlayNoBlend,
          "strum");
      start_scheduled_viewer_stack(
          fret_player, fret_clip, prev_fret_clip,
          clip_stack_options.fret_start_frame,
          clip_stack_options.fret_transition_frame,
          clip_stack_options.hand_blend_seconds,
          ghogx::character::kCharPlayLoop |
              ghogx::character::kCharPlayNoBlend,
          "fret");
      trigger_scheduled_viewer_stack(
          main_player, loaded_clip, clip_stack_options.clip_transition_frame,
          clip_stack_options.clip_blend_seconds, "main");
      trigger_scheduled_viewer_stack(
          strum_player, strum_clip, clip_stack_options.strum_transition_frame,
          clip_stack_options.hand_blend_seconds, "strum");
      trigger_scheduled_viewer_stack(
          fret_player, fret_clip, clip_stack_options.fret_transition_frame,
          clip_stack_options.hand_blend_seconds, "fret");
      frame_hand_weights = evaluate_viewer_main_driver_hand_weights();
      ghogx::character::CharacterPosePlayerLayerBuildSources player_inputs;
      player_inputs.main = &main_player;
      player_inputs.face_base = &face_base_player;
      player_inputs.strum = &strum_player;
      player_inputs.fret = &fret_player;
      player_inputs.face = &face_player;
      player_inputs.hand_weights = &frame_hand_weights;
      const ghogx::character::CharacterPosePlayerLayerSources player_layers =
          ghogx::character::make_character_pose_player_layer_sources(
              player_inputs);
      ghogx::character::append_character_pose_player_layers(pose_stack,
                                                            player_layers);
    }
    // Apply decoded controller data after sampled clip layers. Do not apply
    // FaceFX graph names as pose-bank frame indices: RE shows Good*/Bad*,
    // EyesClosed, Blink, and EyeZCombiner are graph scalar channels, not
    // standalone transform poses.
    std::optional<ghogx::character::SourceCharMainDriverHandWeights>
        controller_hand_weights;
    std::vector<ghogx::character::CharacterRuntimeIkWeight>
        fallback_ik_weights;
    if (character_controllers) {
      controller_hand_weights = evaluate_viewer_main_driver_hand_weights();
      log_viewer_main_driver_flags(*controller_hand_weights);
      if (viewer_hand_ik_weights_active) {
        if (right_hand_weight_override ||
            (strum_clip.loaded && !controller_hand_weights->right_source)) {
          fallback_ik_weights.push_back({"right.weight",
                                         frame_hand_weights.right});
        }
        if (left_hand_weight_override ||
            (fret_clip.loaded && !controller_hand_weights->left_source)) {
          fallback_ik_weights.push_back({"left.weight",
                                         frame_hand_weights.left});
        }
      }
    }
    ghogx::character::CharacterPoseControllerFrameSources controller_sources;
    controller_sources.pose_stack = &pose_stack;
    controller_sources.driver_weights =
        controller_hand_weights ? &*controller_hand_weights : nullptr;
    controller_sources.fallback_ik_weights = std::move(fallback_ik_weights);
    controller_sources.time_seconds = static_cast<float>(pose_time);
    // The standalone character diagnostic has no song tempo map. Model its
    // explicit --midi-fret-target message at 120 BPM with the stock
    // player*_fret_pos 0.22-beat destination window. Gameplay supplies the
    // authored parser event and tempo-derived beat values below.
    controller_sources.current_beat =
        static_cast<float>(pose_time * 2.0);
    controller_sources.delta_beat = dt * 2.0f;
    controller_sources.midi_fret_event_beat = -1.0e30f;
    controller_sources.midi_fret_target_beat = 0.22f;
    controller_sources.controllers_enabled = character_controllers;
    controller_sources.midi_fret_target = midi_fret_target;
    controller_sources.midi_fret_target_enabled =
        !midi_fret_target.empty();
    ghogx::character::apply_character_pose_controller_frame(
        renderer.character(), controller_sources);

    if (const auto viseme_frame = env_int("GHOGX_FACEFX_VISEME_FRAME")) {
      const float viseme_weight =
          std::clamp(env_float("GHOGX_FACEFX_VISEME_WEIGHT").value_or(1.0f),
                     0.0f, 1.0f);
      if (facefx_viseme_clip.loaded && viseme_weight > 0.0f) {
        ghogx::character::apply_clip_frame_weighted(
            facefx_viseme_clip, *viseme_frame, viseme_weight,
            renderer.character());
      }
    }
    if (scene_renderer) {
      scene_renderer->camera() = renderer.camera();
      scene_renderer->draw();
      renderer.draw_over_scene(scene_renderer->camera());
    } else {
      renderer.draw();
    }

    if (!char_pose_mesh_dump.empty() && !char_pose_mesh_dump_written &&
        frame == static_cast<uint64_t>(screenshot_frame)) {
      dump_char_pose_mesh_bounds_jsonl(renderer.character(),
                                       char_pose_mesh_dump, frame,
                                       clip_frame_override);
      char_pose_mesh_dump_written = true;
    }
    if (!char_deformed_pose_dump.empty() &&
        frame == static_cast<uint64_t>(screenshot_frame)) {
      dump_char_deformed_pose_binary(renderer.character(),
                                     char_deformed_pose_dump,
                                     clip_frame_override);
    }

    if (!screenshot_path.empty() && frame == static_cast<uint64_t>(screenshot_frame)) {
      win->save_screenshot(screenshot_path.c_str());
      std::fprintf(stderr, "[char] screenshot -> %s (frame %llu)\n",
                   screenshot_path.c_str(), (unsigned long long)frame);
    }
    const auto sequence_it = screenshot_sequence.find(frame);
    if (sequence_it != screenshot_sequence.end()) {
      win->save_screenshot(sequence_it->second.c_str());
      std::fprintf(stderr, "[char] screenshot -> %s (frame %llu)\n",
                   sequence_it->second.c_str(),
                   static_cast<unsigned long long>(frame));
    }
    win->present();

    ++frame;
    if (max_frames && frame >= static_cast<uint64_t>(max_frames)) break;

    wait_for_next_frame_deadline(
        frame_deadline,
        std::chrono::duration_cast<clock::duration>(target_frame));
  }
  std::fprintf(stderr, "[char] exited after %llu frames\n", (unsigned long long)frame);
  return 0;
}

std::optional<std::pair<std::string, std::string>>
archive_pair_in_directory(const std::filesystem::path& directory) {
  namespace fs = std::filesystem;
  if (!fs::is_directory(directory)) return std::nullopt;
  fs::path hdr;
  fs::path ark;
  for (const char* name : {"main.hdr", "MAIN.HDR"}) {
    const fs::path candidate = directory / name;
    if (fs::is_regular_file(candidate)) {
      hdr = candidate;
      break;
    }
  }
  for (const char* name : {"main_0.ark", "MAIN_0.ARK"}) {
    const fs::path candidate = directory / name;
    if (fs::is_regular_file(candidate)) {
      ark = candidate;
      break;
    }
  }
  if (hdr.empty() || ark.empty()) return std::nullopt;
  return std::pair<std::string, std::string>{
      fs::absolute(hdr).lexically_normal().string(),
      fs::absolute(ark).lexically_normal().string()};
}

std::vector<std::pair<std::string, std::string>>
discover_auxiliary_asset_archives(const std::string& primary_hdr,
                                  const std::string& primary_ark,
                                  const std::string& content_hdr,
                                  const std::string& content_ark) {
  namespace fs = std::filesystem;
  std::vector<std::pair<std::string, std::string>> archives;
  if (primary_hdr.empty()) return archives;

  std::unordered_set<std::string> seen;
  auto mark_seen = [&](const std::string& hdr, const std::string& ark) {
    seen.insert(lower_ascii(
        fs::absolute(fs::path(hdr)).lexically_normal().string() + "|" +
        fs::absolute(fs::path(ark)).lexically_normal().string()));
  };
  mark_seen(primary_hdr, primary_ark);
  if (!content_hdr.empty() && !content_ark.empty())
    mark_seen(content_hdr, content_ark);

  const fs::path primary_gen =
      fs::absolute(fs::path(primary_hdr)).lexically_normal().parent_path();
  std::vector<fs::path> roots = {
      primary_gen / "content",
      primary_gen.parent_path() / "content"};
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

  for (const auto& root : roots) {
    if (!fs::is_directory(root)) continue;
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      candidates.push_back(entry.path());
      candidates.push_back(entry.path() / "gen");
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& candidate : candidates) {
      const auto pair = archive_pair_in_directory(candidate);
      if (!pair) continue;
      const std::string key = lower_ascii(pair->first + "|" + pair->second);
      if (!seen.insert(key).second) continue;
      archives.push_back(*pair);
      std::fprintf(stderr, "[ghogx] auxiliary content archive: %s\n",
                   candidate.string().c_str());
    }
  }
  return archives;
}

std::unique_ptr<ghogx::ui::ConfigDb> mount_dlc_manifests_for_direct_loads(
    const std::string& primary_hdr, const std::string& primary_ark) {
  namespace fs = std::filesystem;
  if (primary_hdr.empty() || primary_ark.empty()) return nullptr;
  fs::path dlc_root;
  if (const char* addon_root = std::getenv("GHOGX_ADDONS_DIR");
      addon_root && *addon_root) {
    dlc_root = fs::path(addon_root).lexically_normal();
  }
  if (dlc_root.empty()) {
    const fs::path local_dlc =
        (fs::current_path() / "DLC").lexically_normal();
    std::error_code local_error;
    if (fs::is_directory(local_dlc, local_error) && !local_error)
      dlc_root = local_dlc;
  }
#ifdef GHOGX_SOURCE_ROOT
  if (dlc_root.empty()) {
    dlc_root = (fs::path(GHOGX_SOURCE_ROOT) / "DLC").lexically_normal();
  }
#endif
  if (dlc_root.empty()) return nullptr;
  std::error_code error;
  if (!fs::is_directory(dlc_root, error)) return nullptr;
  try {
#ifdef _WIN32
    _putenv_s("GHOGX_ADDONS_DIR", dlc_root.string().c_str());
#else
    setenv("GHOGX_ADDONS_DIR", dlc_root.string().c_str(), 1);
#endif
    auto db = std::make_unique<ghogx::ui::ConfigDb>();
    const auto base_ark = gh::ark::ArkV3Reader::load(primary_hdr);
    // Imported song manifests require the retail songs table to exist before
    // their complete source DTBs can be appended. Loading the base here also
    // mounts every loose payload for diagnostic/direct-song paths.
    db->load(base_ark, {primary_ark});
    return db;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[ghogx] DLC manifests skipped: %s\n", ex.what());
  }
  return nullptr;
}

}  // namespace

#include "ui/menu_app.h"  // ghogx::ui::run_menu_mode (windowed menu system)

int main(int argc, char** argv) {
  ScopedTimerResolution timer_resolution;
  const bool argument_free_launch = argc == 1;
  int max_frames = 0;
  std::string ark_dir;
  std::string addons_dir;
  std::string explicit_hdr;
  std::string explicit_ark;
  std::string content_hdr;
  std::string content_ark;
  std::string milo;
  std::string scene_milo;  // --scene: render a MILO's 3-D geometry directly
  std::string char_milo;   // --char: render a BandCharacter MILO in bind pose
  std::string char_scene_milo;  // --char-scene: draw venue behind --char
  std::string char_clip_arg; // --clip <milo>:<name>: apply a clip pose for screenshot
  std::string char_pose_mesh_dump;
  std::string char_deformed_pose_dump;
  std::string guitar_milo = "char/og/guitars/gen/xplorer.milo_ps2";
  std::string strum_clip_arg;
  std::string fret_clip_arg;
  std::string face_clip_arg;
  std::string midi_fret_target;
  ViewerClipStackOptions viewer_clip_stack;
  std::array<float, 3> char_offset = {0.0f, 0.0f, 0.0f};
  int char_2p_select_placer_player = -1;
  std::string char_2p_select_event = "animate";
  int clip_frame_override = -1;  // --clip-frame N: force anim frame N (no time playback)
  bool character_controllers = true;
  bool char_reference_base = false;
  bool hud_test = false;   // --hud-test: draw the in-song HUD overlay only
  HudTestOptions hud_test_options;
  bool hud_options_requested = false;
  bool menu_mode = false;  // --menu: the windowed menu system
  std::string menu_hook;
  std::string song_name = "shoutatthedevil";
  int difficulty = 0;  // Easy
  bool auto_start = false;  // skip splash/title, load song immediately
  bool require_native_assets = false;
  std::string screenshot_path;
  int screenshot_frame = 30;
  std::string screenshot_sequence_dir;
  std::string screenshot_sequence_frames_arg;
  bool sparse_screenshots = false;
  float fixed_dt = 0.0f;
  double diagnostic_song_start = 0.0;
  bool diagnostic_autoplay = false;
  std::optional<uint32_t> diagnostic_fret_mask;
  std::optional<uint32_t> diagnostic_guitar_mask;
  std::vector<DiagnosticGuitarScriptEvent> diagnostic_guitar_script;
  std::optional<DiagnosticChartScriptWindow> diagnostic_chart_script_window;
  bool debug_note_counter = false;
  std::string diagnostic_character;
  std::string diagnostic_character_variant;
  std::vector<std::pair<std::string, std::string>>
      diagnostic_performer_overrides;
  std::vector<std::pair<std::string, std::string>>
      diagnostic_performer_animation_overrides;
  std::string diagnostic_venue;
  bool venue_only = false;
  std::string diagnostic_guitar;
  std::string diagnostic_bass;
  std::string diagnostic_player_bassist;
  std::string diagnostic_venue_event;
  std::string diagnostic_camera_shot;
  std::string diagnostic_front_camera;
  bool diagnostic_unlit_performers = false;
  double diagnostic_camera_path_offset_frames = 0.0;
  int diagnostic_camera_cycle_shot_frame = -1;
  int diagnostic_camera_iterate_shot_frame = -1;
  std::optional<int> diagnostic_camera_random_seed;
  std::optional<double> diagnostic_rock_fill;
  std::optional<double> diagnostic_star_power_fill;
  bool diagnostic_star_power_active = false;
  RenderSize render_size;
  bool show_window = false;
  bool defer_window_show = false;
  bool mute_audio = false;
  CamOverride cam_ovr;  // optional --cam-* overrides for the scene viewer

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      max_frames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--ark-dir") == 0 && i + 1 < argc) {
      ark_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--addons-dir") == 0 && i + 1 < argc) {
      addons_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--hdr") == 0 && i + 1 < argc) {
      explicit_hdr = argv[++i];
    } else if (std::strcmp(argv[i], "--ark") == 0 && i + 1 < argc) {
      explicit_ark = argv[++i];
    } else if (std::strcmp(argv[i], "--content-hdr") == 0 &&
               i + 1 < argc) {
      content_hdr = argv[++i];
    } else if (std::strcmp(argv[i], "--content-ark") == 0 &&
               i + 1 < argc) {
      content_ark = argv[++i];
    } else if (std::strcmp(argv[i], "--milo") == 0 && i + 1 < argc) {
      milo = argv[++i];
    } else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
      scene_milo = argv[++i];
    } else if (std::strcmp(argv[i], "--char-scene") == 0 && i + 1 < argc) {
      char_scene_milo = argv[++i];
    } else if (std::strcmp(argv[i], "--hud-test") == 0) {
      hud_test = true;
    } else if (std::strcmp(argv[i], "--hud-score") == 0 && i + 1 < argc) {
      hud_test_options.score = std::atoi(argv[++i]);
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-streak") == 0 && i + 1 < argc) {
      hud_test_options.streak = std::atoi(argv[++i]);
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-multiplier") == 0 &&
               i + 1 < argc) {
      hud_test_options.multiplier = std::atoi(argv[++i]);
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-sp") == 0 && i + 1 < argc) {
      hud_test_options.sp_fill = std::atof(argv[++i]);
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-star-active") == 0) {
      hud_test_options.sp_active = true;
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-rock") == 0 && i + 1 < argc) {
      hud_test_options.rock_fill = std::atof(argv[++i]);
      hud_options_requested = true;
    } else if (std::strcmp(argv[i], "--hud-tune") == 0 && i + 1 < argc) {
      hud_test_options.tune_file = argv[++i];
    } else if (std::strcmp(argv[i], "--hud-ref-highway") == 0) {
      hud_test_options.ref_highway = true;
    } else if (std::strcmp(argv[i], "--menu") == 0) {
      menu_mode = true;
    } else if (std::strcmp(argv[i], "--manageband") == 0) {
      menu_hook = "manage-band";
      menu_mode = true;
    } else if (std::strcmp(argv[i], "--song") == 0 && i + 1 < argc) {
      song_name = argv[++i];
      hud_test_options.ref_song = song_name;
    } else if (std::strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc) {
      difficulty = std::atoi(argv[++i]);
      hud_test_options.ref_difficulty = difficulty;
    } else if (std::strcmp(argv[i], "--diagnostic-song-start") == 0 &&
               i + 1 < argc) {
      diagnostic_song_start = std::atof(argv[++i]);
      hud_test_options.ref_song_time = diagnostic_song_start;
    } else if (std::strcmp(argv[i], "--diagnostic-autoplay") == 0) {
      diagnostic_autoplay = true;
    } else if (std::strcmp(argv[i], "--diagnostic-fret-mask") == 0 &&
               i + 1 < argc) {
      diagnostic_fret_mask =
          static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0)) & 0x1fu;
    } else if (std::strcmp(argv[i], "--diagnostic-guitar-mask") == 0 &&
               i + 1 < argc) {
      diagnostic_guitar_mask =
          static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0)) & 0xffu;
    } else if (std::strcmp(argv[i], "--diagnostic-guitar-script") == 0 &&
               i + 1 < argc) {
      diagnostic_guitar_script =
          parse_diagnostic_guitar_script(argv[++i]);
    } else if (std::strcmp(argv[i], "--diagnostic-guitar-script-from-chart") == 0 &&
               i + 1 < argc) {
      const bool whammy_star_sustains =
          diagnostic_chart_script_window &&
          diagnostic_chart_script_window->whammy_star_sustains;
      const bool whammy_sweep =
          diagnostic_chart_script_window &&
          diagnostic_chart_script_window->whammy_sweep;
      const std::optional<double> star_power_at_sec =
          diagnostic_chart_script_window
              ? diagnostic_chart_script_window->star_power_at_sec
              : std::nullopt;
      diagnostic_chart_script_window =
          parse_diagnostic_chart_script_window(argv[++i]);
      if (diagnostic_chart_script_window) {
        diagnostic_chart_script_window->whammy_star_sustains =
            whammy_star_sustains;
        diagnostic_chart_script_window->whammy_sweep = whammy_sweep;
        diagnostic_chart_script_window->star_power_at_sec = star_power_at_sec;
      }
    } else if (std::strcmp(argv[i], "--diagnostic-guitar-script-star-power-at") == 0 &&
               i + 1 < argc) {
      if (!diagnostic_chart_script_window) {
        diagnostic_chart_script_window = DiagnosticChartScriptWindow{};
      }
      diagnostic_chart_script_window->star_power_at_sec =
          std::max(0.0, std::strtod(argv[++i], nullptr));
    } else if (std::strcmp(argv[i], "--diagnostic-guitar-script-whammy") == 0) {
      if (!diagnostic_chart_script_window) {
        diagnostic_chart_script_window = DiagnosticChartScriptWindow{};
      }
      diagnostic_chart_script_window->whammy_star_sustains = true;
    } else if (std::strcmp(
                   argv[i],
                   "--diagnostic-guitar-script-whammy-sweep") == 0) {
      if (!diagnostic_chart_script_window) {
        diagnostic_chart_script_window = DiagnosticChartScriptWindow{};
      }
      diagnostic_chart_script_window->whammy_star_sustains = true;
      diagnostic_chart_script_window->whammy_sweep = true;
    } else if (std::strcmp(argv[i], "--debug-note-counter") == 0) {
      debug_note_counter = true;
    } else if ((std::strcmp(argv[i], "--aspect") == 0 ||
                std::strcmp(argv[i], "--render-aspect") == 0) &&
               i + 1 < argc) {
      if (!set_render_aspect_preset(argv[++i], render_size)) {
        std::fprintf(stderr,
                     "[ghogx] --aspect expects 4:3 or 16:9\n");
        return 2;
      }
    } else if (std::strcmp(argv[i], "--render-size") == 0 &&
               i + 1 < argc) {
      if (!set_render_size(argv[++i], render_size)) {
        std::fprintf(
            stderr,
            "[ghogx] --render-size expects WIDTHxHEIGHT in 160x120..3840x2160\n");
        return 2;
      }
    } else if (std::strcmp(argv[i], "--diagnostic-character") == 0 &&
               i + 1 < argc) {
      diagnostic_character = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-character-variant") == 0 &&
               i + 1 < argc) {
      diagnostic_character_variant = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-performer") == 0 &&
               i + 1 < argc) {
      std::string value = argv[++i];
      const size_t equals = value.find('=');
      if (equals == std::string::npos || equals == 0 ||
          equals + 1 >= value.size()) {
        std::fprintf(
            stderr,
            "[ghogx] --diagnostic-performer expects role=source:model\n");
        return 2;
      }
      diagnostic_performer_overrides.emplace_back(
          value.substr(0, equals), value.substr(equals + 1));
    } else if (std::strcmp(argv[i],
                           "--diagnostic-performer-animation") == 0 &&
               i + 1 < argc) {
      std::string value = argv[++i];
      const size_t equals = value.find('=');
      if (equals == std::string::npos || equals == 0 ||
          equals + 1 >= value.size()) {
        std::fprintf(
            stderr,
            "[ghogx] --diagnostic-performer-animation expects "
            "role=source:model\n");
        return 2;
      }
      diagnostic_performer_animation_overrides.emplace_back(
          value.substr(0, equals), value.substr(equals + 1));
    } else if (std::strcmp(argv[i], "--diagnostic-venue") == 0 && i + 1 < argc) {
      diagnostic_venue = argv[++i];
    } else if (std::strcmp(argv[i], "--venue-only") == 0) {
      venue_only = true;
    } else if (std::strcmp(argv[i], "--diagnostic-character") == 0 &&
               i + 1 < argc) {
      diagnostic_character = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-guitar") == 0 &&
               i + 1 < argc) {
      diagnostic_guitar = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-bass") == 0 &&
               i + 1 < argc) {
      diagnostic_bass = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-player-bassist") == 0 &&
               i + 1 < argc) {
      diagnostic_player_bassist = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-venue-event") == 0 &&
               i + 1 < argc) {
      diagnostic_venue_event = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-camera-shot") == 0 &&
               i + 1 < argc) {
      diagnostic_camera_shot = argv[++i];
    } else if (std::strcmp(argv[i], "--diagnostic-front-camera") == 0 &&
               i + 1 < argc) {
      diagnostic_front_camera = argv[++i];
    } else if (std::strcmp(argv[i],
                           "--diagnostic-unlit-performers") == 0 ||
               std::strcmp(argv[i],
                           "--diagnostic-proof-lighting") == 0) {
      diagnostic_unlit_performers = true;
    } else if ((std::strcmp(argv[i], "--diagnostic-camera-path-offset") == 0 ||
                std::strcmp(argv[i],
                            "--diagnostic-camera-path-offset-frames") == 0) &&
               i + 1 < argc) {
      diagnostic_camera_path_offset_frames = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i],
                           "--diagnostic-camera-cycle-shot-frame") == 0 &&
               i + 1 < argc) {
      diagnostic_camera_cycle_shot_frame = std::atoi(argv[++i]);
    } else if ((std::strcmp(argv[i],
                            "--diagnostic-camera-iterate-shot-frame") == 0 ||
                std::strcmp(argv[i],
                            "--diagnostic-camera-iterate-shots-frame") == 0) &&
               i + 1 < argc) {
      diagnostic_camera_iterate_shot_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i],
                           "--diagnostic-camera-random-seed") == 0 &&
               i + 1 < argc) {
      diagnostic_camera_random_seed = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--diagnostic-rock") == 0 &&
               i + 1 < argc) {
      diagnostic_rock_fill = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--diagnostic-star-power") == 0 &&
               i + 1 < argc) {
      diagnostic_star_power_fill = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--diagnostic-star-power-active") == 0) {
      diagnostic_star_power_active = true;
    } else if (std::strcmp(argv[i], "--auto-start") == 0) {
      auto_start = true;
    } else if (std::strcmp(argv[i], "--require-native-assets") == 0) {
      require_native_assets = true;
    } else if (std::strcmp(argv[i], "--show-window") == 0) {
      show_window = true;
    } else if (std::strcmp(argv[i], "--defer-window-show") == 0) {
      defer_window_show = true;
    } else if (std::strcmp(argv[i], "--mute-audio") == 0) {
      mute_audio = true;
    } else if (std::strcmp(argv[i], "--char") == 0 && i + 1 < argc) {
      char_milo = argv[++i];
    } else if (std::strcmp(argv[i], "--char-pose-mesh-dump") == 0 &&
               i + 1 < argc) {
      char_pose_mesh_dump = argv[++i];
    } else if (std::strcmp(argv[i], "--char-deformed-pose-dump") == 0 &&
               i + 1 < argc) {
      char_deformed_pose_dump = argv[++i];
    } else if (std::strcmp(argv[i], "--clip") == 0 && i + 1 < argc) {
      // --clip <milo>:<name>  e.g.  "char/metal1/anims/gen/metal1_main.milo_ps2:band_jump"
      // Applies the named CharClipSamples pose to the character before rendering.
      // Overrides the procedural idle sway for a screenshot of real game animation data.
      // (Stored as a pair in char_clip_arg below; parsed after ARK paths are resolved.)
      char_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--prev-clip") == 0 && i + 1 < argc) {
      viewer_clip_stack.prev_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--clip-frame") == 0 && i + 1 < argc) {
      clip_frame_override = std::atoi(argv[++i]);  // force a specific anim frame (no playback)
    } else if (std::strcmp(argv[i], "--guitar") == 0 && i + 1 < argc) {
      guitar_milo = argv[++i];
    } else if (std::strcmp(argv[i], "--strum-clip") == 0 && i + 1 < argc) {
      strum_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--prev-strum-clip") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.prev_strum_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--fret-clip") == 0 && i + 1 < argc) {
      fret_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--prev-fret-clip") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.prev_fret_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--face-clip") == 0 && i + 1 < argc) {
      face_clip_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--midi-fret-target") == 0 &&
               i + 1 < argc) {
      midi_fret_target = argv[++i];
    } else if (std::strcmp(argv[i], "--clip-start-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.clip_start_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--strum-start-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.strum_start_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--fret-start-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.fret_start_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--clip-transition-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.clip_transition_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--strum-transition-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.strum_transition_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--fret-transition-frame") == 0 &&
               i + 1 < argc) {
      viewer_clip_stack.fret_transition_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--clip-blend") == 0 && i + 1 < argc) {
      viewer_clip_stack.clip_blend_seconds =
          static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--hand-blend") == 0 && i + 1 < argc) {
      viewer_clip_stack.hand_blend_seconds =
          static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--char-offset") == 0 && i + 3 < argc) {
      char_offset[0] = static_cast<float>(std::atof(argv[++i]));
      char_offset[1] = static_cast<float>(std::atof(argv[++i]));
      char_offset[2] = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--char-2p-select-placer") == 0 &&
               i + 1 < argc) {
      const char* value = argv[++i];
      if (std::strcmp(value, "p1") == 0 ||
          std::strcmp(value, "player1") == 0 ||
          std::strcmp(value, "char_multi0.placer") == 0) {
        char_2p_select_placer_player = 0;
      } else if (std::strcmp(value, "p2") == 0 ||
                 std::strcmp(value, "player2") == 0 ||
                 std::strcmp(value, "char_multi1.placer") == 0) {
        char_2p_select_placer_player = 1;
      } else {
        char_2p_select_placer_player = std::atoi(value);
      }
    } else if (std::strcmp(argv[i], "--char-2p-select-event") == 0 &&
               i + 1 < argc) {
      char_2p_select_event = argv[++i];
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot_path = argv[++i];
    } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
      screenshot_frame = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
      screenshot_sequence_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--screenshot-frames") == 0 && i + 1 < argc) {
      screenshot_sequence_frames_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--sparse-screenshots") == 0) {
      sparse_screenshots = true;
    } else if (std::strcmp(argv[i], "--fixed-dt") == 0 && i + 1 < argc) {
      fixed_dt = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--no-character-controllers") == 0) {
      character_controllers = false;
    } else if (std::strcmp(argv[i], "--char-reference-base") == 0) {
      char_reference_base = true;
    } else if (std::strcmp(argv[i], "--cam-yaw") == 0 && i + 1 < argc) {
      cam_ovr.yaw = static_cast<float>(std::atof(argv[++i])); cam_ovr.has_yaw = true;
    } else if (std::strcmp(argv[i], "--cam-yaw-deg") == 0 && i + 1 < argc) {
      cam_ovr.yaw =
          static_cast<float>(std::atof(argv[++i]) * 3.14159265358979323846 / 180.0);
      cam_ovr.has_yaw = true;
    } else if (std::strcmp(argv[i], "--cam-pitch") == 0 && i + 1 < argc) {
      cam_ovr.pitch = static_cast<float>(std::atof(argv[++i])); cam_ovr.has_pitch = true;
    } else if (std::strcmp(argv[i], "--cam-pitch-deg") == 0 && i + 1 < argc) {
      cam_ovr.pitch =
          static_cast<float>(std::atof(argv[++i]) * 3.14159265358979323846 / 180.0);
      cam_ovr.has_pitch = true;
    } else if (std::strcmp(argv[i], "--cam-dist") == 0 && i + 1 < argc) {
      cam_ovr.dist = static_cast<float>(std::atof(argv[++i])); cam_ovr.has_dist = true;
    } else if (std::strcmp(argv[i], "--cam-target") == 0 && i + 3 < argc) {
      cam_ovr.target[0] = static_cast<float>(std::atof(argv[++i]));
      cam_ovr.target[1] = static_cast<float>(std::atof(argv[++i]));
      cam_ovr.target[2] = static_cast<float>(std::atof(argv[++i]));
      cam_ovr.has_target = true;
    } else {
      std::fprintf(stderr, "[ghogx] unknown or incomplete option: %s\n", argv[i]);
      return 2;
    }
  }

  const ScreenshotSequence screenshot_sequence =
      make_screenshot_sequence(screenshot_sequence_dir, screenshot_sequence_frames_arg);
  if (!screenshot_sequence_dir.empty() && screenshot_sequence.empty()) {
    std::fprintf(stderr,
                 "[ghogx] --screenshot-dir requires at least one valid --screenshot-frames value\n");
    return 2;
  }
  if (!screenshot_sequence.empty() &&
      (!scene_milo.empty() || hud_test)) {
    std::fprintf(stderr,
                 "[ghogx] --screenshot-dir/--screenshot-frames are not "
                 "available in scene or HUD modes\n");
    return 2;
  }

  if (require_native_assets) {
    _putenv_s("GHOGX_REQUIRE_NATIVE_ASSETS", "1");
    std::fprintf(stderr,
                 "[ghogx] native-only asset gate enabled: GH1 raw-layout "
                 "compatibility loaders disabled\n");
  }
  const bool capture_enabled = !screenshot_path.empty() || !screenshot_sequence.empty();
  if (sparse_screenshots && !capture_enabled) {
    std::fprintf(stderr,
                 "[ghogx] --sparse-screenshots requires --screenshot or "
                 "--screenshot-dir/--screenshot-frames\n");
    return 2;
  }

  std::fprintf(stderr, "[ghogx] render size: %dx%d\n",
               render_size.width, render_size.height);

  // RndCam::UpdateLocal and the venue renderer consume one output-aspect
  // value in retail. Publish the actual backbuffer ratio through the existing
  // renderer contract so CamShot BuildTransform uses the identical ratio.
  char camera_aspect_value[32] = {};
  const double camera_aspect =
      render_size.height > 0
          ? static_cast<double>(render_size.width) /
                static_cast<double>(render_size.height)
          : 16.0 / 9.0;
  std::snprintf(camera_aspect_value, sizeof(camera_aspect_value), "%.9g",
                camera_aspect);
  _putenv_s("GHOGX_CAMERA_ASPECT", camera_aspect_value);
  std::fprintf(stderr, "[ghogx] camera aspect: %s\n", camera_aspect_value);

  if ((capture_enabled && !show_window) || defer_window_show) {
    _putenv_s("GHOGX_HIDE_WINDOW", "1");
  }
  if (mute_audio) {
    _putenv_s("GHOGX_MUTE_AUDIO", "1");
    std::fprintf(stderr, "[ghogx] audio muted for diagnostic run\n");
  }

  // Resolve ARK paths.
  std::string hdr;
  std::string ark;
  namespace fs = std::filesystem;
  const bool use_default_ark_dir =
      ark_dir.empty() && explicit_hdr.empty() && explicit_ark.empty();
  if (use_default_ark_dir) {
    ark_dir = (fs::current_path() / "gen").string();
    std::fprintf(stderr, "[ghogx] default ARK directory: %s\n",
                 ark_dir.c_str());
  }
  if (argument_free_launch) {
    menu_mode = true;
    std::fprintf(stderr,
                 "[ghogx] argument-free launch: normal menu boot\n");
  }
  if (!explicit_hdr.empty() || !explicit_ark.empty()) {
    if (explicit_hdr.empty() || explicit_ark.empty()) {
      std::fprintf(stderr, "[ghogx] --hdr and --ark must be supplied together\n");
      return 2;
    }
    if (!ark_dir.empty()) {
      std::fprintf(stderr, "[ghogx] use either --ark-dir or explicit --hdr/--ark, not both\n");
      return 2;
    }
    if (!fs::exists(explicit_hdr) || !fs::exists(explicit_ark)) {
      std::fprintf(stderr, "[ghogx] --hdr/--ark path not found\n");
      return 2;
    }
    hdr = explicit_hdr;
    ark = explicit_ark;
  } else if (!ark_dir.empty()) {
    for (const char* n : {"main.hdr", "MAIN.HDR"}) {
      if (fs::exists(fs::path(ark_dir) / n)) { hdr = (fs::path(ark_dir) / n).string(); break; }
    }
    for (const char* n : {"main_0.ark", "MAIN_0.ARK"}) {
      if (fs::exists(fs::path(ark_dir) / n)) { ark = (fs::path(ark_dir) / n).string(); break; }
    }
    if (hdr.empty() || ark.empty()) {
      std::fprintf(stderr,
                   use_default_ark_dir
                       ? "[ghogx] default ./gen lacks main.hdr/main_0.ark: %s\n"
                       : "[ghogx] --ark-dir lacks main.hdr/main_0.ark: %s\n",
                   ark_dir.c_str());
      return 2;
    }
  }
  if (!addons_dir.empty()) {
    const fs::path requested =
        fs::absolute(fs::path(addons_dir)).lexically_normal();
    std::error_code addon_error;
    if (!fs::is_directory(requested, addon_error) || addon_error) {
      std::fprintf(stderr, "[ghogx] --addons-dir is not a directory: %s\n",
                   requested.string().c_str());
      return 2;
    }
#ifdef _WIN32
    _putenv_s("GHOGX_ADDONS_DIR", requested.string().c_str());
#else
    setenv("GHOGX_ADDONS_DIR", requested.string().c_str(), 1);
#endif
    std::fprintf(stderr, "[ghogx] DLC directory: %s\n",
                 requested.string().c_str());
  }
  const auto auxiliary_asset_archives = discover_auxiliary_asset_archives(
      hdr, ark, content_hdr, content_ark);
  const auto direct_load_dlc_db =
      mount_dlc_manifests_for_direct_loads(hdr, ark);

  if (debug_note_counter) {
    _putenv_s("GHOGX_DEBUG_HIGHWAY_NOTE_COUNTER", "1");
    std::fprintf(stderr, "[ghogx] debug note counter enabled\n");
  }

  // --menu: the windowed menu system (boots the menu engine + renders screens).
  if (menu_mode) {
    if (hdr.empty() || ark.empty()) {
      std::fprintf(stderr, "[ghogx] --menu requires --ark-dir\n");
      return 2;
    }
    ghogx::ui::MenuRunOptions menu_options;
    if (content_hdr.empty() != content_ark.empty()) {
      std::fprintf(
          stderr,
          "[ghogx] --content-hdr and --content-ark must be supplied together\n");
      return 2;
    }
    if ((!content_hdr.empty() && !fs::exists(content_hdr)) ||
        (!content_ark.empty() && !fs::exists(content_ark))) {
      std::fprintf(stderr, "[ghogx] content archive path not found\n");
      return 2;
    }
    menu_options.gameplay_autoplay = diagnostic_autoplay;
    if (!menu_hook.empty()) {
      if (menu_hook == "manage-band" || menu_hook == "manage_band")
        menu_options.start_screen = "manage_band_screen";
      else return 2;
    }
    menu_options.gameplay_front_camera_role = diagnostic_front_camera;
    menu_options.gameplay_proof_lighting = diagnostic_unlit_performers;
    menu_options.automate_full_loop = env_flag("GHOGX_MENU_AUTO_LOOP");
    menu_options.preferred_song = song_name;
    menu_options.preferred_difficulty = difficulty;
    menu_options.content_hdr = content_hdr;
    menu_options.content_ark = content_ark;
    menu_options.auxiliary_asset_archives = auxiliary_asset_archives;
    menu_options.screenshot_sequence = screenshot_sequence;
    return ghogx::ui::run_menu_mode(
        hdr, ark, screenshot_path, screenshot_frame, max_frames,
        render_size.width, render_size.height, fixed_dt, menu_options);
  }

  if (venue_only) {
    // This is deliberately a presentation switch, not an asset conversion
    // path. The original GH1 files remain the source for native venue tests.
    _putenv_s("GHOGX_DEBUG_VENUE_ONLY_CAPTURE", "1");
    _putenv_s("GHOGX_HIDE_GAMEPLAY_HUD", "1");
    _putenv_s("GHOGX_ONLY_PERFORMER", "__venue_only__");
    _putenv_s("GHOGX_DISABLE_PERFORMER_PROPS", "1");
    std::fprintf(stderr,
                 "[ghogx] venue-only mode: highway/HUD/performers suppressed\n");
  }

  // --scene: dedicated 3-D MILO scene viewer (venue/stage/track geometry).
  if (!scene_milo.empty()) {
    if (hdr.empty() || ark.empty()) {
      std::fprintf(stderr, "[ghogx] --scene requires --ark-dir\n");
      return 2;
    }
    return run_scene_mode(hdr, ark, scene_milo, screenshot_path,
                          screenshot_frame, max_frames, cam_ovr, render_size);
  }

  // --char: dedicated BandCharacter viewer (bind-pose skinned mesh).
  if (!char_milo.empty()) {
    if (hdr.empty() || ark.empty()) {
      std::fprintf(stderr, "[ghogx] --char requires --ark-dir\n");
      return 2;
    }
    return run_char_mode(hdr, ark, char_milo, screenshot_path,
                         screenshot_frame, max_frames, cam_ovr, char_clip_arg,
                         clip_frame_override, guitar_milo, strum_clip_arg,
                         fret_clip_arg, face_clip_arg, char_scene_milo,
                         char_offset, char_2p_select_placer_player,
                         char_2p_select_event, fixed_dt,
                         character_controllers, char_reference_base, midi_fret_target,
                         viewer_clip_stack, screenshot_sequence,
                         char_pose_mesh_dump, char_deformed_pose_dump,
                         render_size);
  }

  // --hud-test: dedicated in-song HUD overlay preview (own window + loop).
  if (hud_test) {
    if (hdr.empty() || ark.empty()) {
      std::fprintf(stderr, "[ghogx] --hud-test requires --ark-dir\n");
      return 2;
    }
    return run_hud_test_mode(hdr, ark, screenshot_path, screenshot_frame,
                             max_frames, hud_test_options, render_size);
  }

  ghogx::asset::Image image;
  SplashSequence seq;
  ghogx::asset::Image title_wall;
  ghogx::asset::Image title_poster;
  if (!hdr.empty() && !ark.empty()) {
    if (!milo.empty()) {
      image = ghogx::asset::load_milo_texture(hdr, ark, milo);
    } else {
      for (const char* m : {"ui/gen/pub_splash.milo_ps2",
                            "ui/gen/activision_splash.milo_ps2",
                            "ui/gen/harmonix_splash.milo_ps2",
                            "ui/gen/splash.milo_ps2"}) {
        auto img = ghogx::asset::load_milo_texture(hdr, ark, m);
        if (img.valid()) seq.slides.push_back(std::move(img));
      }
      const std::string splash_milo = "ui/gen/splash.milo_ps2";
      title_poster = ghogx::asset::load_milo_texture_named(
          hdr, ark, splash_milo, "splash_poster.tex");
      title_wall   = ghogx::asset::load_milo_texture_named(
          hdr, ark, splash_milo, "mm_brick03.tex");
    }
  }

  auto win = ghogx::render::Window::create(render_size.width,
                                           render_size.height,
                                           "GuitarHeroOGX");
  if (!win) {
    std::fprintf(stderr, "[ghogx] failed to create window/device\n");
    return 1;
  }

  AppEngine engine(win.get());
  engine.set_ark(hdr, ark);
  engine.set_auxiliary_asset_archives(auxiliary_asset_archives);
  engine.set_song(song_name, difficulty);
  if (!diagnostic_character_variant.empty()) {
    if (!direct_load_dlc_db) {
      std::fprintf(stderr,
                   "[ghogx] --diagnostic-character-variant requires mounted "
                   "DLC manifests\n");
      return 2;
    }
    const ghogx::ui::CharacterVariant* variant =
        direct_load_dlc_db->character_variant(
            ghogx::Symbol(diagnostic_character_variant));
    if (!variant) {
      std::fprintf(stderr,
                   "[ghogx] unknown diagnostic character variant: %s\n",
                   diagnostic_character_variant.c_str());
      return 2;
    }
    engine.set_selected_character_variant(*variant);
    std::fprintf(
        stderr,
        "[ghogx] diagnostic character variant: selection=%s model=%s "
        "main_anim=%s\n",
        variant->selection.c_str(), variant->model_path.c_str(),
        variant->main_anim_path.c_str());
  }
  if (!diagnostic_character.empty()) {
    engine.set_diagnostic_character_override(diagnostic_character);
    std::fprintf(stderr, "[ghogx] diagnostic character override: %s\n",
                 diagnostic_character.c_str());
  }
  for (const auto& [role, character_reference] :
       diagnostic_performer_overrides) {
    engine.set_diagnostic_performer_override(role, character_reference);
    std::fprintf(stderr,
                 "[ghogx] diagnostic performer override: role=%s ref=%s\n",
                 role.c_str(), character_reference.c_str());
  }
  for (const auto& [role, character_reference] :
       diagnostic_performer_animation_overrides) {
    engine.set_diagnostic_performer_animation_override(role,
                                                       character_reference);
    std::fprintf(
        stderr,
        "[ghogx] diagnostic performer animation override: role=%s ref=%s\n",
        role.c_str(), character_reference.c_str());
  }
  if (!diagnostic_venue.empty()) {
    engine.set_diagnostic_venue_override(diagnostic_venue);
    std::fprintf(stderr, "[ghogx] diagnostic venue override: %s\n",
                 diagnostic_venue.c_str());
  }
  if (!diagnostic_character.empty()) {
    engine.set_diagnostic_character_override(diagnostic_character);
    std::fprintf(stderr, "[ghogx] diagnostic character override: %s\n",
                 diagnostic_character.c_str());
  }
  if (!diagnostic_guitar.empty()) {
    engine.set_diagnostic_guitar_override(diagnostic_guitar);
    std::fprintf(stderr, "[ghogx] diagnostic guitar override: %s\n",
                 diagnostic_guitar.c_str());
  }
  if (!diagnostic_bass.empty()) {
    engine.set_diagnostic_bass_override(diagnostic_bass);
    std::fprintf(stderr, "[ghogx] diagnostic bass override: %s\n",
                 diagnostic_bass.c_str());
  }
  if (!diagnostic_player_bassist.empty()) {
    engine.set_diagnostic_player_bassist(diagnostic_player_bassist);
    std::fprintf(stderr, "[ghogx] diagnostic player bassist: %s\n",
                 diagnostic_player_bassist.c_str());
  }
  if (!diagnostic_venue_event.empty()) {
    engine.set_diagnostic_venue_event(diagnostic_venue_event);
    std::fprintf(stderr, "[ghogx] diagnostic venue event: %s\n",
                 diagnostic_venue_event.c_str());
  }
  if (!diagnostic_camera_shot.empty()) {
    engine.set_diagnostic_camera_shot(diagnostic_camera_shot);
    engine.set_diagnostic_camera_path_offset_frames(
        diagnostic_camera_path_offset_frames);
    std::fprintf(stderr, "[ghogx] diagnostic camera shot: %s\n",
                 diagnostic_camera_shot.c_str());
    if (diagnostic_camera_path_offset_frames != 0.0) {
      std::fprintf(stderr,
                   "[ghogx] diagnostic camera path offset frames: %.3f\n",
                   diagnostic_camera_path_offset_frames);
    }
  }
  if (diagnostic_camera_cycle_shot_frame >= 0) {
    std::fprintf(stderr,
                 "[ghogx] diagnostic camera cycle_shot frame: %d\n",
                 diagnostic_camera_cycle_shot_frame);
  }
  if (diagnostic_camera_iterate_shot_frame >= 0) {
    std::fprintf(stderr,
                 "[ghogx] diagnostic camera iterate_shot frame: %d\n",
                 diagnostic_camera_iterate_shot_frame);
  }
  if (!diagnostic_camera_random_seed) {
    char seed_env[64] = {};
    const DWORD seed_len = GetEnvironmentVariableA(
        "GHOGX_CAMERA_RANDOM_SEED", seed_env,
        static_cast<DWORD>(sizeof(seed_env)));
    if (seed_len > 0 && seed_len < sizeof(seed_env)) {
      diagnostic_camera_random_seed = std::atoi(seed_env);
    }
  }
  if (diagnostic_camera_random_seed) {
    engine.set_diagnostic_camera_random_seed(*diagnostic_camera_random_seed);
    std::fprintf(stderr,
                 "[ghogx] diagnostic camera random seed: %d\n",
                 *diagnostic_camera_random_seed);
  }
  if (diagnostic_autoplay) {
    engine.set_diagnostic_autoplay(true);
    std::fprintf(stderr, "[ghogx] diagnostic autoplay enabled\n");
  }
  if (diagnostic_fret_mask) {
    engine.set_diagnostic_fret_mask(*diagnostic_fret_mask);
    std::fprintf(stderr, "[ghogx] diagnostic fret mask: 0x%02x\n",
                 *diagnostic_fret_mask);
  }
  if (diagnostic_guitar_mask) {
    engine.set_diagnostic_guitar_mask(*diagnostic_guitar_mask);
    std::fprintf(stderr, "[ghogx] diagnostic guitar mask: 0x%02x\n",
                 *diagnostic_guitar_mask);
  }
  if (!diagnostic_guitar_script.empty()) {
    engine.set_diagnostic_guitar_script(diagnostic_guitar_script);
    std::fprintf(stderr, "[ghogx] diagnostic guitar script events: %zu\n",
                 diagnostic_guitar_script.size());
  }
  if (diagnostic_chart_script_window) {
    engine.set_diagnostic_chart_guitar_script(*diagnostic_chart_script_window);
    std::fprintf(
        stderr,
        "[ghogx] diagnostic chart guitar script requested: %.3f..%.3f "
        "hit_offset=%.4f whammy_star_sustains=%d whammy_sweep=%d "
        "star_power_at=%.3f\n",
        diagnostic_chart_script_window->start_sec,
        diagnostic_chart_script_window->end_sec,
        diagnostic_chart_script_window->hit_offset_sec,
        diagnostic_chart_script_window->whammy_star_sustains ? 1 : 0,
        diagnostic_chart_script_window->whammy_sweep ? 1 : 0,
        diagnostic_chart_script_window->star_power_at_sec
            ? *diagnostic_chart_script_window->star_power_at_sec
            : -1.0);
  }
  if (diagnostic_rock_fill) {
    engine.set_diagnostic_rock_fill(*diagnostic_rock_fill);
    std::fprintf(stderr, "[ghogx] diagnostic rock fill: %.2f\n",
                 std::clamp(*diagnostic_rock_fill, 0.0, 1.0));
  }
  if (diagnostic_star_power_fill) {
    engine.set_diagnostic_star_power_fill(*diagnostic_star_power_fill);
    std::fprintf(stderr, "[ghogx] diagnostic star power fill: %.2f\n",
                 std::clamp(*diagnostic_star_power_fill, 0.0, 1.0));
  }
  if (diagnostic_star_power_active) {
    engine.set_diagnostic_star_power_active(true);
    std::fprintf(stderr, "[ghogx] diagnostic star power active\n");
  }
  if (diagnostic_song_start > 0.0) {
    engine.set_diagnostic_song_start(diagnostic_song_start);
  }
  if (hud_options_requested) {
    ghogx::hud::HudState override_state;
    override_state.score = std::max(0, hud_test_options.score);
    override_state.streak = std::max(0, hud_test_options.streak);
    override_state.multiplier = std::clamp(hud_test_options.multiplier, 1, 9);
    override_state.sp_fill = std::clamp(hud_test_options.sp_fill, 0.0f, 1.0f);
    override_state.sp_active = hud_test_options.sp_active;
    override_state.rock_fill = std::clamp(hud_test_options.rock_fill, 0.0f, 1.0f);
    engine.set_diagnostic_hud_override(override_state);
    std::fprintf(stderr,
                 "[ghogx] diagnostic HUD override: score=%d streak=%d mult=%dx sp=%.2f active=%d rock=%.2f\n",
                 override_state.score, override_state.streak,
                 override_state.multiplier, override_state.sp_fill,
                 override_state.sp_active ? 1 : 0, override_state.rock_fill);
  }
  if (!hud_test_options.tune_file.empty()) {
    engine.set_hud_tuning_file(hud_test_options.tune_file);
    std::fprintf(stderr, "[ghogx] HUD layout tuning: %s\n",
                 hud_test_options.tune_file.c_str());
  }
  if (capture_enabled && fixed_dt <= 0.0f) fixed_dt = 1.0f / 60.0f;
  if (fixed_dt > 0.0f) {
    engine.set_deterministic_gameplay_clock(true);
    std::fprintf(stderr, "[ghogx] fixed dt enabled: %.6f\n", fixed_dt);
  }
  if (!screenshot_path.empty()) {
    engine.set_screenshot(screenshot_path, screenshot_frame);
    // Auto-exit a couple frames after the capture.
    if (max_frames == 0) max_frames = screenshot_frame + 3;
  }
  if (!screenshot_sequence.empty()) {
    engine.set_screenshot_sequence(screenshot_sequence);
    if (max_frames == 0) {
      max_frames = static_cast<int>(screenshot_sequence.rbegin()->first + 3);
    }
  }
  engine.set_sparse_screenshots(sparse_screenshots);
  // These affect renderer construction and therefore must be installed before
  // --auto-start builds the venue performers.
  if (!diagnostic_front_camera.empty()) {
    engine.set_diagnostic_front_camera(diagnostic_front_camera);
    std::fprintf(stderr, "[ghogx] diagnostic front camera: %s\n",
                 diagnostic_front_camera.c_str());
  }
  if (diagnostic_unlit_performers) {
    engine.set_diagnostic_unlit_performers(true);
    std::fprintf(stderr,
                 "[ghogx] diagnostic bright proof lighting enabled\n");
  }
  if (auto_start && !hdr.empty()) {
    // Skip splash + title, load song immediately for diagnostic/dev runs.
    if (!engine.force_start_song()) {
      std::fprintf(stderr, "[ghogx] auto-start song load failed\n");
      return 3;
    }
  }

  if (!seq.slides.empty()) {
    engine.set_sequence(std::move(seq));
    engine.set_title_images(std::move(title_wall), std::move(title_poster));
  } else if (image.valid()) {
    engine.set_image(std::move(image));
  }

  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  const auto target_frame = std::chrono::duration<double>(1.0 / 60.0);
  auto frame_deadline = clock::now();

  std::fprintf(stderr, "[ghogx] entering main loop%s\n",
               max_frames ? " (bounded)" : " (Esc or close to quit)");
  std::fprintf(stderr, "[ghogx] song='%s' difficulty=%d\n",
               song_name.c_str(), difficulty);
  std::fprintf(stderr, "[ghogx] Keyboard: A/S/D/F/G = frets; Space=strum; Shift/H=star power; K=whammy; Enter=Start/confirm\n");

  bool diagnostic_camera_cycle_shot_dispatched = false;
  bool diagnostic_camera_iterate_shot_dispatched = false;
  bool deferred_window_revealed = false;
  const auto loop_wall_start = clock::now();
  constexpr uint64_t kPerformanceWarmupFrames = 30;
  std::optional<clock::time_point> steady_wall_start;
  uint64_t steady_start_frame = 0;
  while (!win->should_close()) {
    win->pump();
    if (win->should_close()) break;

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (fixed_dt > 0.0f) {
      dt = fixed_dt;
    } else if (dt > 0.1f) {
      dt = 0.1f;
    }

    if (diagnostic_camera_cycle_shot_frame >= 0 &&
        !diagnostic_camera_cycle_shot_dispatched &&
        engine.frame_count() >=
            static_cast<uint64_t>(diagnostic_camera_cycle_shot_frame)) {
      diagnostic_camera_cycle_shot_dispatched = true;
      const bool queued = engine.cycle_camera_shot_like_source();
      std::fprintf(
          stderr,
          "[ghogx] diagnostic camera cycle_shot dispatched frame=%llu queued=%d\n",
          static_cast<unsigned long long>(engine.frame_count()),
          queued ? 1 : 0);
    }
    if (diagnostic_camera_iterate_shot_frame >= 0 &&
        !diagnostic_camera_iterate_shot_dispatched &&
        engine.frame_count() >=
            static_cast<uint64_t>(diagnostic_camera_iterate_shot_frame)) {
      diagnostic_camera_iterate_shot_dispatched = true;
      const size_t visited = engine.iterate_camera_shots_like_source();
      std::fprintf(
          stderr,
          "[ghogx] diagnostic camera iterate_shot dispatched frame=%llu visited=%zu\n",
          static_cast<unsigned long long>(engine.frame_count()),
          visited);
    }

    engine.tick(dt);
    if (defer_window_show && !deferred_window_revealed &&
        engine.frame_count() > 0) {
      win->show_no_activate();
      deferred_window_revealed = true;
      std::fprintf(stderr,
                   "[ghogx] deferred window revealed after first frame without activation\n");
    }
    if (!steady_wall_start &&
        engine.frame_count() >= kPerformanceWarmupFrames) {
      steady_wall_start = clock::now();
      steady_start_frame = engine.frame_count();
    }

    if (max_frames && engine.frame_count() >= static_cast<uint64_t>(max_frames)) {
      break;
    }

    wait_for_next_frame_deadline(
        frame_deadline,
        std::chrono::duration_cast<clock::duration>(target_frame));
  }

  const double loop_wall_seconds =
      std::chrono::duration<double>(clock::now() - loop_wall_start).count();
  const double average_fps =
      loop_wall_seconds > 0.0
          ? static_cast<double>(engine.frame_count()) / loop_wall_seconds
          : 0.0;
  const double average_frame_ms =
      engine.frame_count() > 0
          ? loop_wall_seconds * 1000.0 /
                static_cast<double>(engine.frame_count())
          : 0.0;
  const uint64_t steady_frames =
      steady_wall_start && engine.frame_count() > steady_start_frame
          ? engine.frame_count() - steady_start_frame
          : 0;
  const double steady_wall_seconds =
      steady_wall_start
          ? std::chrono::duration<double>(clock::now() - *steady_wall_start)
                .count()
          : 0.0;
  const double steady_average_fps =
      steady_wall_seconds > 0.0
          ? static_cast<double>(steady_frames) / steady_wall_seconds
          : 0.0;
  const double steady_average_frame_ms =
      steady_frames > 0
          ? steady_wall_seconds * 1000.0 /
                static_cast<double>(steady_frames)
          : 0.0;
  std::fprintf(stderr,
               "[ghogx] loop performance: frames=%llu wall=%.3fs "
               "average_fps=%.3f average_frame_ms=%.3f "
               "warmup=%llu steady_frames=%llu steady_wall=%.3fs "
               "steady_fps=%.3f steady_frame_ms=%.3f\n",
               static_cast<unsigned long long>(engine.frame_count()),
               loop_wall_seconds, average_fps, average_frame_ms,
               static_cast<unsigned long long>(kPerformanceWarmupFrames),
               static_cast<unsigned long long>(steady_frames),
               steady_wall_seconds, steady_average_fps,
               steady_average_frame_ms);
  std::fprintf(stderr, "[ghogx] exited after %llu frames, %.2fs engine time\n",
               static_cast<unsigned long long>(engine.frame_count()), engine.time());
  engine.log_profile_summary();
  engine.log_final_gameplay_summary();
  return 0;
}
