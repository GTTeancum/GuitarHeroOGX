// engine/src/ui/menu_app.h
//
// Windowed menu mode: boots the screen manager (all 40 stock screens loaded
// verbatim + the config-DTB-backed game-side), renders the CURRENT screen's
// panel MILOs as the real 3-D scene (GH2 menus are 3-D scenes through a camera,
// not 2-D quads -- so this reuses the MiloSceneRenderer), and drives navigation
// from controller/keyboard input through the real SELECT_START_MSG scripts.
//
// This is the presentation layer over the (committed) menu logic engine.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ghogx::ui {

struct PanelExternalDependencyPlan {
  bool import_metacam_cameras = false;
  bool import_metacam_environment = false;
};

// GH2 PanelDir references can resolve through the stock metacam subdirectory.
// A proxy camera remains owned by the panel path, while a direct meta.cam or
// missing environment reference is imported from that external directory.
inline PanelExternalDependencyPlan source_panel_external_dependency_plan(
    const std::string& panel_camera, const std::string& panel_environment,
    bool panel_environment_is_local) {
  PanelExternalDependencyPlan plan;
  plan.import_metacam_cameras = panel_camera == "meta.cam";
  plan.import_metacam_environment =
      !panel_environment.empty() && !panel_environment_is_local;
  return plan;
}

// GH2 PS2 CharsysPanel::Poll reads the selected character's bone_door Z
// angle, forces the external panel door to Euler (pi/2, 0, z), and writes the
// resulting Matrix3 while leaving the panel mesh's authored translation in
// place. Hmx::MakeRotMatrix uses row-vector Ry * Rx * Rz order.
inline std::array<float, 9> source_charsys_external_door_rotation(
    float bone_door_z) {
  const float cz = std::cos(bone_door_z);
  const float sz = std::sin(bone_door_z);
  return {
      cz, sz, 0.0f,
      0.0f, 0.0f, 1.0f,
      sz, -cz, 0.0f,
  };
}

struct MenuRunOptions {
  // Developer proof hook.  Uses the normal ScreenManager transition into the
  // named stock screen after init.dtb has booted; it never fabricates a panel.
  std::string start_screen;
  // Menu navigation stays human-driven.  This flag affects only the in-song
  // controller so the normal front-end flow can be exercised hands-free.
  bool gameplay_autoplay = false;
  // Gameplay-only presentation controls also apply when gameplay is reached
  // through the live menu flow.
  std::string gameplay_front_camera_role;
  bool gameplay_proof_lighting = false;
  // Optional state-aware proof harness.  It presses the real menu components;
  // it does not bypass the stock screen scripts.
  bool automate_full_loop = false;
  std::string preferred_song;
  int preferred_difficulty = 1;
  bool play_boot_presentation = true;
  // Optional independent gameplay/content archive. The primary archive still
  // owns the GH2 retail front end; this mount owns songs, venues, and band.
  std::string content_hdr;
  std::string content_ark;
  // Read-only sidecar archives used for independently selected foreign
  // characters. They never replace the primary GH2 presentation archive.
  std::vector<std::pair<std::string, std::string>> auxiliary_asset_archives;
  std::map<uint64_t, std::string> screenshot_sequence;
};

// hdr/ark = the PS2 ARK; screenshot_path (optional) captures one frame at
// screenshot_frame; max_frames>0 auto-exits (0 = run until the window closes).
int run_menu_mode(const std::string& hdr, const std::string& ark,
                  const std::string& screenshot_path, int screenshot_frame,
                  int max_frames, int window_width = 960,
                  int window_height = 720, float fixed_dt = 0.0f,
                  const MenuRunOptions& options = {});

}  // namespace ghogx::ui
