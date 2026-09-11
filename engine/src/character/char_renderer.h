// engine/src/character/char_renderer.h
//
// Draw a decoded GH2 band Character in 3-D via D3D9.
//
// Sibling to render::MiloSceneRenderer, specialised for skinned characters:
//   * the 4 per-vertex floats are bone weights, not colour;
//   * texture is chosen per mesh from its material's diffuse Tex;
//   * the orbit camera frames the character and faces the front by default;
//   * meshes named "shadow*" are skipped.

#pragma once

#include "character/char_mesh.h"
#include "milo_scene/milo_scene.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace ghogx::asset {
struct Image;
}

namespace ghogx::render {
class Window;
struct OrbitCamera;  // render/milo_scene_renderer.h
}  // namespace ghogx::render

namespace ghogx::character {

class CharRenderer {
 public:
  explicit CharRenderer(ghogx::render::Window& win);
  ~CharRenderer();

  CharRenderer(const CharRenderer&) = delete;
  CharRenderer& operator=(const CharRenderer&) = delete;

  // Take ownership of a decoded character + its decoded textures (keyed by .tex
  // entry name, as materials reference them). Uploads all textures and frames
  // the orbit camera on the character's bind-pose bounding box (front view).
  void set_character(Character character,
                     const std::map<std::string, ghogx::asset::Image>& textures);
  void set_attached_prop(milo_scene::Scene scene,
                         const std::map<std::string, ghogx::asset::Image>& textures,
                         const std::string& attach_bone);

  ghogx::render::OrbitCamera& camera();
  // Fit the current pose once for a stable, instrument-free menu preview.
  void frame_menu_preview(float aspect);
  void set_world_offset(float x, float y, float z);
  void set_world_transform(const std::array<float, 16>& m);
  void set_min_lod(int min_lod);
  void set_use_scene_lighting(bool enabled);
  void set_proof_lighting(bool enabled);
  void set_reference_base(bool enabled);
  void set_color_modulation(float r, float g, float b, float a = 1.0f);
  bool has_drawable_geometry() const;
  bool set_object_showing(std::string_view object_name, bool showing);
  std::optional<std::array<float, 16>> attached_prop_world(
      std::string_view object_name) const;
  // Direct access to the character for pose modification (e.g. apply_clip_pose).
  Character& character();

  // Advance renderer-owned transient state for one frame. Character motion is
  // supplied by decoded CharClip/controller paths; update resets from bind so
  // removed procedural sway does not contaminate sampled poses.
  void update(float dt);

  // Draw the character for one frame (clear + camera + all meshes).
  // Uses linear-blend skinning (skin_to_pose) with the current bone poses.
  void draw();
  // Draw with an already-established camera, preserving the current render
  // target and depth buffer. Used to composite the animated character into a
  // venue scene after the venue renderer has drawn its pass.
  void draw_over_scene(const ghogx::render::OrbitCamera& cam);
  // Reuse one animated pose/material setup across crowd placements. Geometry
  // remains fully 3D; no render-to-texture impostor is involved.
  void draw_instances_over_scene(const ghogx::render::OrbitCamera& cam,
      const std::vector<std::array<float, 16>>& worlds);

  // WorldCrowd's source renderer keeps only CamShot-selected members as full
  // 3-D characters. The remaining members are drawn through a camera-facing
  // billboard built from a live character impostor texture
  // (WorldCrowd::BuildBillboard/DrawShowing and gImpostorCamera/textures).
  // Refresh the actor image from the current decoded pose, then batch all of
  // that actor's flat placements into the already-rendered venue scene.
  bool refresh_worldcrowd_impostor(
      const ghogx::render::OrbitCamera& scene_cam,
      const std::array<float, 16>& source_character_world,
      float source_character_height,
      const std::function<void(const ghogx::render::OrbitCamera&)>&
          draw_attached = {});
  void draw_worldcrowd_impostors_over_scene(
      const ghogx::render::OrbitCamera& cam,
      const std::vector<std::array<float, 16>>& placement_worlds,
      float source_character_height);

 private:
  IDirect3DTexture9* upload(const ghogx::asset::Image& img);
  void frame_camera();
  void draw_impl(bool clear_target, uint32_t clear_color);

  struct Impl;
  Impl* impl_;
};

// Linear-blend skinning of one mesh into world space. Source RndMesh rows are
// consumed as RB3 runtime-authored offsets:
// offset_i = mesh_world_bind * inverse(bone_world_bind_i). The native path uses
// skinned = sum_i w_i * (v * offset_i * bone_world_curr_i). All four serialized
// slots remain in order; null or unresolved source slots contribute identity.
void skin_to_pose(const SkinnedMesh& mesh, const Character& character,
                  std::vector<std::array<float, 3>>& out_pos,
                  std::vector<std::array<float, 3>>& out_nrm);

// RndMesh skinning already emits source world-space vertices. Weighted meshes
// therefore submit under identity, while unweighted meshes retain their
// decoded Trans world row. Keeping this decision shared with the focused test
// prevents attachment code from applying a second transform to hair or limbs.
std::array<float, 16> source_character_mesh_submission_world(
    const SkinnedMesh& mesh, const Character& character);

// True when the normal draw path must consume skin_to_pose output rather than
// raw authored vertices. This keeps the renderer's pose-selection decision
// covered alongside the skin equation itself.
bool source_character_mesh_renders_decoded_skinning(const SkinnedMesh& mesh);

// Resolve the selected body branch from the decoded Character9 LOD references.
// The referenced object may be a Group or a converted legacy View; its authored
// spelling is not part of the runtime contract.
std::optional<std::string> source_character_active_lod_view(
    const Character& character, int min_lod);

// Resolve the complete authored draw closure for the selected Character9 LOD.
// The closure starts at the selected root LOD and walks its decoded ancestor
// groups while suppressing sibling LOD branches. Direct mesh children of those
// ancestors (for example authored instruments or accessories) remain visible;
// ungrouped skeleton/editor helper meshes do not become implicit draw roots.
SourceCharacterDrawClosure source_character_draw_closure(
    const Character& character, int min_lod);

// Character meshes repurpose the serialized vertex-color bytes as skin
// weights, so RndMat.prelit cannot be treated as a generic lighting bypass on
// this path. RndMat.use_environ remains the gate for venue lighting, while
// non-gameplay diagnostic rendering keeps its lights independent of scene
// state.
struct SourceCharacterMaterialLightingPlan {
  bool fixed_function_lighting = false;
};

inline SourceCharacterMaterialLightingPlan
source_character_material_lighting_plan(bool use_scene_lighting,
                                        bool use_environ, bool prelit) {
  (void)prelit;
  return {!use_scene_lighting || use_environ};
}

// GH2's authored shader-domain light colors can exceed 1.0 (the Festival
// character rim reaches 7.0). A DX8/9 fixed-function D3DCOLORVALUE is a
// normalized channel, so preserve hue while fitting only over-range lights to
// that hardware-era interval. In-range authored lights remain byte-for-byte
// equivalent as floats.
inline std::array<float, 3> source_character_fixed_function_light_rgb(
    float r, float g, float b) {
  r = r < 0.0f ? 0.0f : r;
  g = g < 0.0f ? 0.0f : g;
  b = b < 0.0f ? 0.0f : b;
  float peak = r > g ? r : g;
  peak = peak > b ? peak : b;
  if (peak > 1.0f) {
    r /= peak;
    g /= peak;
    b /= peak;
  }
  return {r, g, b};
}

}  // namespace ghogx::character
