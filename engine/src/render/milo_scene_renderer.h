// engine/src/render/milo_scene_renderer.h
//
// MiloSceneRenderer — draw a decoded MILO scene (Trans/Mat/Mesh) in 3-D via
// D3D9. Uploads each material's diffuse Tex to a D3D9 texture, composes each
// mesh's world matrix up its Trans parent chain, and draws the real decoded
// vertices/faces as indexed triangles with the Z-buffer + backface culling +
// simple directional+ambient lighting. An orbit/free camera lets the scene be
// inspected; the initial framing fits the scene's bounding box.
//
// This is the core "make it LOOK like Guitar Hero" renderer: it draws actual
// 3-D venue/stage geometry, not textured quads.

#pragma once

#include "milo_scene/milo_scene.h"
#include "render/gh1_crowd_regions.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DIndexBuffer9;
struct IDirect3DQuery9;
struct IDirect3DTexture9;
struct IDirect3DVertexBuffer9;

namespace ghogx::asset {
struct Image;  // asset/milo_image.h (RGBA8)
}

namespace ghogx::render {

class Window;

struct CameraResultFrame {
  bool valid = false;
  std::string source;
  float position[3] = {0, 0, 0};
  float forward[3] = {0, 1, 0};
  float right[3] = {1, 0, 0};
  float up[3] = {0, 0, 1};
  bool screen_offset_consumed = false;
  bool has_custom_view = false;
  bool has_custom_projection = false;
  float custom_view[16] = {1, 0, 0, 0,
                           0, 1, 0, 0,
                           0, 0, 1, 0,
                           0, 0, 0, 1};
  float custom_projection[16] = {1, 0, 0, 0,
                                 0, 1, 0, 0,
                                 0, 0, 1, 0,
                                 0, 0, 0, 1};
};

// An orbit camera around a target point: yaw/pitch/distance, with the GH2 world
// convention (X across, Y depth, Z up). Driven by arrow keys / WASD.
struct OrbitCamera {
  float target[3] = {0, 0, 0};
  float yaw = 0.0f;     // radians, around world +Z (up)
  float pitch = 0.4f;   // radians, elevation above the XY plane
  float distance = 100.0f;
  float fov = 0.7f;     // vertical fov radians
  float near_z = 1.0f;
  float far_z = 5000.0f;
  bool authored = false;
  float authored_eye[3] = {0, 0, 0};
  float authored_at[3] = {0, 1, 0};
  float authored_up[3] = {0, 0, 1};
  CameraResultFrame result_frame;
  float screen_offset[2] = {0, 0};
  bool dof_active = false;
  float dof_focus_distance = 0.0f;
  float dof_blur_depth = 0.35f;
  float dof_max_blur = 1.0f;
  float dof_min_blur = 0.0f;
  float dof_focus_blur_multiplier = 0.0f;
  bool shake_active = false;
  float shake_noise_amp = 0.0f;
  float shake_noise_freq = 0.0f;
  float shake_max_angular_offset[2] = {0.0f, 0.0f};

  // Compute the eye position from yaw/pitch/distance about the target.
  void eye(float out[3]) const;
};

// Report whether the submitted transform and the decoded source triangles each
// reverse the renderer's canonical winding. GH2 uses mirrored transforms
// together with deliberately reversed triangle order in assets such as
// cs_wall2.mesh, so the two parities must be combined rather than treating a
// negative determinant as sufficient on its own.
bool world_transform_reverses_winding(
    const std::array<float, 16>& world_transform);
bool mesh_source_winding_reversed(const milo_scene::MeshObj& mesh);
uint32_t source_mesh_cull_mode(bool material_cull,
                               bool debug_highlighted_mesh,
                               bool world_winding_reversed,
                               bool source_winding_reversed,
                               uint32_t authored_cull_mode);

struct SourceTexGenXfm2D {
  float m00 = 1.0f;
  float m01 = 0.0f;
  float m10 = 0.0f;
  float m11 = 1.0f;
  float tu = 0.0f;
  float tv = 0.0f;
};

// Convert an authored RndMat TexXfm into the centered, V-down texture
// generator used by retail GH2. Other TexGen modes keep their source matrix.
SourceTexGenXfm2D source_texgen_xfm_2d(
    uint8_t tex_gen, float m00, float m01, float m10, float m11,
    float tu, float tv);

// Retail GH2 PS2 enables its material light channel when the material uses an
// environment or when it is not prelit. Only a prelit material which opts out
// of environment lighting bypasses the fixed lighting channel.
bool source_material_bypasses_fixed_lighting(
    bool prelit, bool use_environment);

class MiloSceneRenderer {
 public:
  struct SpotlightState {
    std::string name;
    std::string target_mesh;
    bool has_target_override = false;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float intensity = 1.0f;
    std::array<float, 4> rotation_xyzw = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_rotation = false;
    bool flare_enabled = true;
    bool has_flare_enabled = false;
  };
  explicit MiloSceneRenderer(Window& win);
  ~MiloSceneRenderer();

  MiloSceneRenderer(const MiloSceneRenderer&) = delete;
  MiloSceneRenderer& operator=(const MiloSceneRenderer&) = delete;

  // Take ownership of a decoded scene + the decoded textures (keyed by .tex
  // entry name, as materials reference them). Uploads all textures to D3D9 and
  // frames the orbit camera on the scene bounding box.
  void set_scene(milo_scene::Scene scene,
                 const std::map<std::string, ghogx::asset::Image>& textures);
  bool select_authored_camera(const std::string& name);
  bool select_scene_panel_camera();
  const std::string& scene_panel_environment() const {
    return scene_.panel_environment;
  }

  // Mutable camera, so the app can drive it from input.
  OrbitCamera& camera() { return cam_; }

  // A world-space text vertex: position already composed into world space by the
  // caller (via each label's Trans), UV into the font atlas, ARGB tint.
  struct TextVertex {
    float x, y, z;
    float u, v;
    uint32_t argb;
  };
  struct TextTransformSpan {
    size_t first_vertex = 0;
    size_t vertex_count = 0;
    std::string name;
    std::string parent;
    std::array<float, 16> local = {1, 0, 0, 0, 0, 1, 0, 0,
                                   0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> bind_world = {1, 0, 0, 0, 0, 1, 0, 0,
                                        0, 0, 1, 0, 0, 0, 0, 1};
  };
  struct TextBatch {
    std::vector<TextVertex> verts;
    const ghogx::asset::Image* atlas = nullptr;
    std::vector<TextTransformSpan> transform_spans;
  };
  // Hand the renderer the menu's text (triangle list, 3 verts/tri) + the font
  // atlas it samples. Drawn as an alpha-blended overlay after the 3-D scene.
  // Pass an empty list to clear.
  void set_text(std::vector<TextVertex> verts, const ghogx::asset::Image& atlas);
  void set_text_batches(std::vector<TextBatch> batches);

  // Draw the whole scene for one frame (clear + camera + all meshes). Call
  // between the window's frame boundaries (it does its own Begin/EndScene).
  void draw();
  void draw_over_scene(const OrbitCamera& cam);
  void draw_scene_only();
  void draw_scene_only_over_scene();
  void draw_scene_only_over_scene_preserving_state();
  void draw_scene_only_over_scene_preserving_state(const OrbitCamera& cam);
  void draw_text_over_scene();
  void set_viewport(int x, int y, int width, int height);
  void set_default_camera_aspect(float aspect);
  void set_clear_color(uint8_t r, uint8_t g, uint8_t b);
  void set_clear_depth_on_overlay(bool enabled);
  void set_environment_dynamic_lights(bool enabled);
  void set_world_transform(const std::array<float, 16>& m);
  // Resolve a scene node through its authored parent chain after applying the
  // currently sampled TransAnim transforms. UI proxies rendered by a separate
  // renderer (for example CharsysPanel characters) use this to follow their
  // live BandPlacer instead of remaining at its bind pose.
  bool current_scene_node_world(const std::string& name,
                                std::array<float, 16>& world) const;
  void set_global_tint(float brightness, float alpha);
  void set_additive_blend(bool additive);
  void set_active_spotlights(std::vector<SpotlightState> spots);
  void set_active_particle_systems(std::unordered_set<std::string> particle_names);
  void set_particle_intensities(std::map<std::string, float> intensities);
  void set_particle_sizes(std::map<std::string, float> sizes);
  void set_particle_speeds(std::map<std::string, float> speeds);
  void set_particle_lifetimes(std::map<std::string, float> lifetimes);
  void set_particle_start_colors(
      std::map<std::string, std::array<float, 4>> colors);
  void set_particle_end_colors(
      std::map<std::string, std::array<float, 4>> colors);
  void set_hidden_meshes(std::unordered_set<std::string> mesh_names);
  // GH1 SwitchCam selects once, against the last submitted camera, not every
  // frame. Native GH2 renderers have no GH1 ownership group and are unaffected.
  void select_gh1_crowd_region(int index);
  void set_gh1_crowd_sizes(float promoted_fraction, float flat_fraction);
  std::vector<std::array<float, 16>> gh1_crowd_promoted_worlds() const;
  void exclude_gh1_crowd_already_owned_by(const MiloSceneRenderer& owner);
  void set_post_text_meshes(std::unordered_set<std::string> mesh_names);
  void set_post_text_mesh_world_offsets(
      std::map<std::string, std::array<float, 3>> offsets);
  void set_post_text_mesh_text_split(size_t batch_index);
  void set_material_alpha_multipliers(std::map<std::string, float> material_alpha);
  void set_material_alpha_overrides(
      std::map<std::string, float> material_alpha);
  void set_material_color_overrides(
      std::map<std::string, std::array<float, 4>> material_colors);
  void set_material_texture_overrides(
      std::map<std::string, std::string> material_textures);
  struct MaterialTexTransformSample {
    bool has_translation = false;
    std::array<float, 2> translation = {0.0f, 0.0f};
    bool has_scale = false;
    std::array<float, 2> scale = {1.0f, 1.0f};
    bool has_rotation = false;
    float rotation_radians = 0.0f;
  };
  struct MaterialUvBounds {
    bool valid = false;
    float min_u = 0.0f;
    float min_v = 0.0f;
    float max_u = 0.0f;
    float max_v = 0.0f;
  };
  struct MaterialUvSamplerDecision {
    bool uv_repeats = false;
    bool wrap = false;
  };
  void set_material_tex_transform_overrides(
      std::map<std::string, MaterialTexTransformSample> material_tex_transforms);
  void set_environment_lighting_enabled(bool enabled);
  void set_environment_color_overrides(
      std::map<std::string, std::array<float, 4>> environment_colors);
  struct EnvironmentFogOverride {
    bool has_enabled = false;
    bool enabled = false;
    bool has_color = false;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool has_range = false;
    std::array<float, 2> range = {0.0f, 0.0f};
  };
  void set_environment_fog_overrides(
      std::map<std::string, EnvironmentFogOverride> environment_fog);
  void set_light_color_overrides(
      std::map<std::string, std::array<float, 4>> light_colors);
  struct LightStateOverride {
    bool has_color = false;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool has_range = false;
    float range = 0.0f;
    bool has_type = false;
    int type = 0;
  };
  void set_light_state_overrides(
      std::map<std::string, LightStateOverride> light_states);
  void set_default_environment(std::string environment_name);
  bool apply_environment_lighting_state(const std::string& environment_name);
  void set_mesh_translation_offsets(
      std::map<std::string, std::array<float, 3>> offsets);
  struct MeshTransformSample {
    bool has_translation = false;
    bool translation_is_absolute = false;
    std::array<float, 3> translation = {0.0f, 0.0f, 0.0f};
    bool has_rotation = false;
    bool rotation_is_absolute = false;
    std::array<float, 4> rotation_xyzw = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_scale = false;
    bool scale_is_absolute = false;
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    float blend = 1.0f;
    bool has_source_frame = false;
    float source_frame = 0.0f;
    bool rotation_slerp = false;
    // SetFrame has already blended this snapshot at update time. Drawing
    // publishes it verbatim; it must not blend against the bind pose again.
    bool has_local_transform = false;
    std::array<float, 16> local_transform = {};
  };
  static MeshTransformSample compose_transform_animation_sample(
      const std::array<float, 16>& base_local,
      const MeshTransformSample* current,
      const MeshTransformSample& incoming);
  // Resolve a decoded local-space target. False leaves incoming unchanged for
  // external targets whose transform owner is not this scene.
  bool compose_transform_animation_sample(
      const std::string& target, const MeshTransformSample* current,
      MeshTransformSample& incoming) const;
  void set_mesh_transform_offsets(
      std::map<std::string, MeshTransformSample> offsets);
  // Update one live transform target without discarding the other sampled
  // PanelDir animations already installed for this frame. CharsysPanel uses
  // this for its external character-select door target.
  void set_mesh_transform_offset(const std::string& mesh_name,
                                 MeshTransformSample sample);
  void set_transform_parent_overrides(
      std::map<std::string, std::string> parents);
  void set_flare_steps(std::map<std::string, int> steps);
  void set_mesh_position_overrides(
      std::map<std::string, std::vector<std::array<float, 3>>> positions);
  void set_mesh_normal_overrides(
      std::map<std::string, std::vector<std::array<float, 3>>> normals);
  void set_mesh_texcoord_overrides(
      std::map<std::string, std::vector<std::array<float, 2>>> texcoords);
  void set_mesh_color_overrides(
      std::map<std::string, std::vector<std::array<float, 4>>> colors);
  void set_mesh_anim_blends(std::map<std::string, float> blends);
  void set_face_camera_meshes(std::unordered_set<std::string> mesh_names);
  void trigger_mesh_pulse(const std::string& mesh_name, float amplitude);
  struct MeshAnimKey {
    float frame = 0.0f;
    float pos[3] = {0.0f, 0.0f, 0.0f};
  };
  struct MeshQuatAnimKey {
    float frame = 0.0f;
    float quat_xyzw[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  };
  struct MeshTransformAnim {
    std::vector<MeshAnimKey> translation_keys;
    std::vector<MeshQuatAnimKey> rotation_keys;
    std::vector<MeshAnimKey> scale_keys;
    bool translation_spline = false;
    bool translation_repeat = false;
    bool scale_spline = false;
    bool rotation_slerp = false;
  };
  void trigger_mesh_translation_anim(const std::string& mesh_name,
                                     std::vector<MeshAnimKey> keys,
                                     float frames_per_second);
  void trigger_mesh_transform_anim(const std::string& mesh_name,
                                   MeshTransformAnim anim,
                                   float frames_per_second,
                                   bool loop = false);
  void update(float dt_seconds);

 private:
  IDirect3DTexture9* upload(const ghogx::asset::Image& img);
  const milo_scene::MatObj* find_material(const std::string& name) const;
  void frame_camera_on_bounds();
  void draw_impl(bool clear_target, bool draw_scene, bool draw_text);
  bool resolve_transform_parent_world(const std::string& name,
                                      std::array<float, 16>& world) const;

  Window* win_ = nullptr;
  IDirect3DDevice9* dev_ = nullptr;
  milo_scene::Scene scene_;
  Gh1CrowdRegions gh1_crowd_regions_;
  std::unordered_set<std::string> nonowning_gh1_crowd_drawables_;
  std::array<float, 16> last_crowd_camera_view_{};
  std::array<float, 16> last_crowd_camera_projection_{};
  bool last_crowd_camera_valid_ = false;
  bool pending_gh1_crowd_auto_ = false;
  struct OrderedMeshDraw {
    const milo_scene::MeshObj* mesh = nullptr;
    const milo_scene::MultiMeshObj* multi_mesh = nullptr;
    const milo_scene::Xfm* instance_world = nullptr;
  };
  // RndMultiMesh entries expand in-place in the authored drawable order.
  // Each instance uses its serialized world transform exactly as
  // RndMultiMesh::DrawShowing does in the source runtime.
  std::vector<OrderedMeshDraw> ordered_mesh_draws_;
  std::unordered_map<const milo_scene::MeshObj*, std::array<float, 16>>
      base_mesh_worlds_;
  std::unordered_set<std::string> spotlight_template_meshes_;
  // Spotlight::Generate builds these runtime meshes when a Spotlight is
  // loaded. GH2's MILO stores their dimensions/materials, not their vertices.
  std::map<std::string, milo_scene::MeshObj> generated_spotlight_beams_;
  milo_scene::MeshObj generated_spotlight_disc_;
  milo_scene::MeshObj generated_spotlight_flare_;
  std::map<std::string, const milo_scene::MatObj*> materials_by_name_;
  std::map<std::string, const milo_scene::MeshObj*> meshes_by_name_;
  std::map<std::string, const milo_scene::GroupObj*> groups_by_name_;
  std::map<std::string, std::string> authored_parents_;
  OrbitCamera cam_;
  std::array<float, 16> world_transform_ = {1, 0, 0, 0, 0, 1, 0, 0,
                                            0, 0, 1, 0, 0, 0, 0, 1};
  bool additive_blend_ = false;
  bool active_spotlight_filter_ = false;
  std::map<std::string, SpotlightState> active_spotlights_;
  bool active_particle_filter_ = false;
  std::unordered_set<std::string> active_particle_systems_;
  std::map<std::string, float> particle_intensities_;
  std::map<std::string, float> particle_sizes_;
  std::map<std::string, float> particle_speeds_;
  std::map<std::string, float> particle_lifetimes_;
  std::map<std::string, std::array<float, 4>> particle_start_colors_;
  std::map<std::string, std::array<float, 4>> particle_end_colors_;
  float particle_time_ = 0.0f;
  std::unordered_set<std::string> hidden_meshes_;
  std::unordered_set<std::string> post_text_meshes_;
  std::map<std::string, std::array<float, 3>> post_text_mesh_world_offsets_;
  size_t post_text_mesh_text_split_ = 0;
  std::map<std::string, float> material_alpha_;
  std::map<std::string, float> material_alpha_overrides_;
  std::map<std::string, std::array<float, 4>> material_colors_;
  std::map<std::string, std::string> material_textures_;
  std::map<std::string, MaterialTexTransformSample> material_tex_transforms_;
  bool environment_lighting_enabled_ = true;
  bool custom_viewport_ = false;
  int viewport_x_ = 0;
  int viewport_y_ = 0;
  int viewport_w_ = 0;
  int viewport_h_ = 0;
  float default_camera_aspect_ = 0.0f;
  bool clear_depth_on_overlay_ = false;
  bool force_environment_dynamic_lights_ = false;
  float global_brightness_ = 1.0f;
  float global_alpha_ = 1.0f;
  // Retail world passes clear to black unless a caller supplies a backdrop.
  // Legacy splash cards fade to black at their borders and rely on that clear.
  uint8_t clear_r_ = 0;
  uint8_t clear_g_ = 0;
  uint8_t clear_b_ = 0;
  std::map<std::string, std::string> mesh_environments_;
  std::unordered_set<std::string> legacy_environment_drawables_;
  std::string default_environment_;
  std::string selected_camera_name_;
  std::map<std::string, std::array<float, 4>> environment_color_overrides_;
  std::map<std::string, EnvironmentFogOverride> environment_fog_overrides_;
  std::map<std::string, std::array<float, 4>> light_color_overrides_;
  std::map<std::string, LightStateOverride> light_state_overrides_;
  std::map<std::string, std::array<float, 3>> mesh_translation_offsets_;
  std::map<std::string, MeshTransformSample> mesh_transform_offsets_;
  std::map<std::string, std::string> transform_parent_overrides_;
  std::map<std::string, int> flare_steps_;
  struct FlareVisibilityState {
    IDirect3DQuery9* query = nullptr;
    bool query_issued = false;
    bool target_visible = false;
    float fade = 0.0f;
  };
  std::map<std::string, FlareVisibilityState> flare_visibility_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      mesh_position_overrides_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      mesh_normal_overrides_;
  std::map<std::string, std::vector<std::array<float, 2>>>
      mesh_texcoord_overrides_;
  std::map<std::string, std::vector<std::array<float, 4>>>
      mesh_color_overrides_;
  std::map<std::string, float> mesh_anim_blends_;
  std::unordered_set<std::string> face_camera_meshes_;
  std::map<std::string, float> mesh_pulses_;
  struct ActiveMeshAnim {
    MeshTransformAnim anim;
    float frames_per_second = 30.0f;
    float elapsed = 0.0f;
    bool loop = false;
  };
  std::map<std::string, ActiveMeshAnim> active_mesh_anims_;

  struct StaticMeshBuffers {
    IDirect3DVertexBuffer9* vertices = nullptr;
    IDirect3DIndexBuffer9* indices = nullptr;
    bool wrap = false;
  };
  std::unordered_map<const milo_scene::MeshObj*, StaticMeshBuffers>
      static_mesh_buffers_;
  std::unordered_map<const milo_scene::MeshObj*, bool>
      source_mesh_winding_reversed_;

  // Texture cache keyed by .tex entry name.
  std::map<std::string, IDirect3DTexture9*> tex_;

  // Menu text overlay: the font atlas + world-space triangle list.
  std::vector<IDirect3DTexture9*> text_tex_;
  std::vector<std::vector<TextVertex>> text_;
  std::vector<std::vector<TextTransformSpan>> text_transform_spans_;

  // Scene bounding box in world space (after world-matrix composition).
  float bb_min_[3] = {0, 0, 0};
  float bb_max_[3] = {0, 0, 0};
  bool have_bounds_ = false;
};

MiloSceneRenderer::MaterialUvSamplerDecision choose_material_uv_sampler(
    const MiloSceneRenderer::MaterialUvBounds& final_uv_bounds,
    float scale_u, float scale_v, bool material_tex_anim);

}  // namespace ghogx::render
