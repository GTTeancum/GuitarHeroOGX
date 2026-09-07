// engine/src/game/highway_renderer.h
//
// HighwayRenderer — the Guitar Hero II 3-D note highway.
//
// Renders the classic GH perspective fretboard: a textured board receding into
// the distance, colored gems scrolling toward the strikeline, fret-target
// "smashers", sustain tails, beat lines and hit flames. All art is the
// authentic GH2 PS2 track texture set, loaded natively at runtime from
// track/gen/track.milo_ps2 in the ARK (ARK -> MILO -> PS2 Tex decode -> D3D9
// texture). Nothing is pre-extracted.

#pragma once

#include "chart/midi_reader.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace ghogx::render { class Window; }

namespace ghogx::game {

struct FoFiXSessionSustain;

class HighwayRenderer {
 public:
  explicit HighwayRenderer(ghogx::render::Window& win);
  ~HighwayRenderer();

  HighwayRenderer(const HighwayRenderer&) = delete;
  HighwayRenderer& operator=(const HighwayRenderer&) = delete;

  struct SideRailColorState {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    bool ok = false;
  };
  struct ColorAnimKey {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float frame = 0.0f;
  };
  struct ColorAnimAlphaKey {
    float a = 1.0f;
    float frame = 0.0f;
  };
  struct ColorAnimState {
    std::vector<ColorAnimKey> keys;
    std::vector<ColorAnimAlphaKey> alpha_keys;
    bool has_rgb = false;
    bool has_alpha = false;
    bool ok = false;
  };
  struct QuatAnimKey {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    float frame = 0.0f;
  };
  struct Vec3AnimKey {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float frame = 0.0f;
  };
  struct MeshTransformAnim {
    std::vector<Vec3AnimKey> translation_keys;
    std::vector<QuatAnimKey> rotation_keys;
    std::vector<Vec3AnimKey> scale_keys;
  };
  struct MeshTransformSample {
    bool has_translation = false;
    std::array<float, 3> translation = {0.0f, 0.0f, 0.0f};
    bool has_rotation = false;
    std::array<float, 4> rotation_xyzw = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_scale = false;
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
  };
  struct ParticleColorAnimKey {
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    float frame = 0.0f;
  };
  struct ParticleVector2AnimKey {
    std::array<float, 2> value = {0.0f, 0.0f};
    float frame = 0.0f;
  };

  // Load the GH2 track texture set natively from track/gen/track.milo_ps2.
  // `surface_ref` is the resolved guitarist highway bitmap entry; empty keeps
  // the track MILO default.
  bool load_textures(const std::string& hdr_path, const std::string& ark_path,
                     const std::string& surface_ref = std::string(),
                     bool timing_preview = false);
  bool textures_loaded() const { return loaded_; }
  bool textures_loaded_for_surface(const std::string& surface_ref) const {
    return loaded_ && loaded_surface_ref_ == surface_ref;
  }
  // Menu timing previews need an opaque source-textured board beneath the
  // native track mesh because they composite over a bright UI backdrop.
  void set_surface_quad_underlay(bool enabled) {
    surface_quad_underlay_ = enabled;
  }

  // Draw one frame of the 3-D note highway.
  //   song_time      — playback position in seconds (drives note scroll).
  //   chart          — the parsed chart.
  //   difficulty     — 0=Easy 1=Medium 2=Hard 3=Expert.
  //   fret_held_mask — bits 0-4 = lanes currently held (smasher glow).
  //   hit_flash      — per-lane flame intensity 0..1 (recent hits, decaying).
  //   lookahead_sec  — seconds of chart visible ahead of the strikeline.
  void draw(double song_time, const ghogx::chart::Chart& chart, int difficulty,
            uint32_t fret_held_mask, const float hit_flash[5],
            float lookahead_sec = 1.5f,
            const std::vector<uint8_t>* consumed_notes = nullptr,
            const std::vector<FoFiXSessionSustain>* active_sustains = nullptr,
            bool star_power_active = false,
            bool whammy_active = false,
            float whammy_axis = 0.0f,
            const float star_collect_flash[5] = nullptr,
             const float miss_flash[5] = nullptr,
             const float star_miss_flash[5] = nullptr,
             const uint8_t hit_phrase_state[5] = nullptr,
             int combo_multiplier = 1,
             float bad_feedback_flash = 0.0f,
             float rock_fill = 1.0f,
             float star_power_flash = 0.0f,
             float surface_flash = 0.0f,
             bool track_intro_active = true,
             double track_intro_elapsed = 0.0);
  void draw_over_scene(double song_time, const ghogx::chart::Chart& chart,
                       int difficulty, uint32_t fret_held_mask,
                       const float hit_flash[5], float lookahead_sec = 1.5f,
                       const std::vector<uint8_t>* consumed_notes = nullptr,
                       const std::vector<FoFiXSessionSustain>* active_sustains = nullptr,
                       bool star_power_active = false,
                       bool whammy_active = false,
                       float whammy_axis = 0.0f,
                       const float star_collect_flash[5] = nullptr,
                        const float miss_flash[5] = nullptr,
                        const float star_miss_flash[5] = nullptr,
                        const uint8_t hit_phrase_state[5] = nullptr,
                        int combo_multiplier = 1,
                        float bad_feedback_flash = 0.0f,
                        float rock_fill = 1.0f,
                        float star_power_flash = 0.0f,
                        float surface_flash = 0.0f,
                        bool track_intro_active = true,
                        double track_intro_elapsed = 0.0);

 private:
  struct MeshVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float u = 0.0f, v = 0.0f;
  };
  struct RuntimeMesh {
    std::string texture_name;
    std::vector<MeshVertex> verts;
    std::vector<uint16_t> indices;
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
    float center_x = 0.0f;
    float center_y = 0.0f;
    std::array<float, 4> material_color = {1.0f, 1.0f, 1.0f, 1.0f};
    uint8_t blend = 0;
    uint8_t tex_gen = 0;
    uint8_t z_mode = 1;
    bool use_environ = false;
    bool prelit = false;
    bool point_lights = false;
    bool intensify = false;
    bool cull = true;
    bool texture_wrap = false;
    bool ok = false;
  };
  struct RuntimeLight {
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> direction = {0.0f, 0.0f, -1.0f};
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    float range = 1.0f;
    int type = 0;
  };
  struct RuntimeLineMaterial {
    std::string material_name;
    std::string texture_name;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    uint8_t blend = 0;
    uint8_t z_mode = 1;
    uint8_t tex_gen = 0;
    bool prelit = false;
    bool alpha_cut = false;
    bool alpha_write = false;
    bool intensify = false;
    bool cull = true;
    bool texture_wrap = false;
    bool ok = false;
  };
  struct SustainVisualState {
    std::array<float, 100> whammy_history = {};
    size_t whammy_cursor = 0;
    float glow_width_scale = 1.0f;
  };
  struct RuntimeParticleSystem {
    std::string name;
    std::string texture_name;
    uint8_t blend = 0;
    uint32_t max_particles = 0;
    std::array<float, 3> local_pos = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> box_extent_min = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> box_extent_max = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> force_dir = {0.0f, 0.0f, 0.0f};
    std::array<float, 4> mat_color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> start_color_low = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> start_color_high = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> mid_color_low = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> mid_color_high = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> end_color_low = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> end_color_high = {1.0f, 1.0f, 1.0f, 1.0f};
    float speed_min = 0.0f;
    float speed_max = 0.0f;
    float pitch_min = 0.0f;
    float pitch_max = 0.0f;
    float yaw_min = 0.0f;
    float yaw_max = 0.0f;
    float start_size_min = 1.0f;
    float start_size_max = 1.0f;
    float delta_size_min = 0.0f;
    float delta_size_max = 0.0f;
    float lifetime_min = 1.0f;
    float lifetime_max = 1.0f;
    float grow_ratio = 0.0f;
    float shrink_ratio = 1.0f;
    float mid_color_ratio = 0.0f;
    bool bubble = false;
    float bubble_period_min = 10.0f;
    float bubble_period_max = 10.0f;
    float bubble_size_min = 1.0f;
    float bubble_size_max = 1.0f;
    std::vector<ParticleColorAnimKey> anim_start_color_keys;
    std::vector<ParticleColorAnimKey> anim_end_color_keys;
    std::vector<ParticleVector2AnimKey> anim_emit_rate_keys;
    std::vector<ParticleVector2AnimKey> anim_speed_keys;
    std::vector<ParticleVector2AnimKey> anim_life_keys;
    std::vector<ParticleVector2AnimKey> anim_start_size_keys;
    bool ok = false;
  };

  void draw_impl(double song_time, const ghogx::chart::Chart& chart,
                 int difficulty, uint32_t fret_held_mask,
                 const float hit_flash[5], float lookahead_sec,
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
                   double track_intro_elapsed);
  void draw_debug_note_counter_overlay(double song_time,
                                       const ghogx::chart::Chart& chart,
                                       int difficulty) const;
  void release_textures();
  IDirect3DTexture9* tex(const std::string& name) const;
  void draw_runtime_mesh(const RuntimeMesh& mesh, float cx, float cy,
                         uint32_t tint, float scale = 1.0f,
                         bool use_texture_alpha = true,
                         float z_offset = 0.0f,
                         bool clip_to_z_min = false,
                         float z_min = 0.0f) const;
  void draw_runtime_mesh_with_texture(const RuntimeMesh& mesh,
                                      const std::string& texture_name,
                                      float cx, float cy, uint32_t tint,
                                      float scale = 1.0f,
                                      bool use_texture_alpha = true,
                                      float z_offset = 0.0f,
                                      bool clip_to_z_min = false,
                                      float z_min = 0.0f) const;
  void draw_runtime_mesh_scaled_with_texture(
      const RuntimeMesh& mesh, const std::string& texture_name, float cx,
      float cy, uint32_t tint, float scale_x, float scale_y, float scale_z,
      bool use_texture_alpha = true, float uv_u_offset = 0.0f,
      float uv_v_offset = 0.0f, bool use_vertex_color = true,
      float z_offset = 0.0f, bool clip_to_z_min = false,
      float z_min = 0.0f, bool apply_depth_fade = false,
      float depth_fade_top_y = 0.0f, float depth_fade_alpha_dist = 0.0f) const;
  void draw_centered_runtime_mesh(const RuntimeMesh& mesh, float cx, float cy,
                                  uint32_t tint, float scale = 1.0f,
                                  bool use_texture_alpha = true,
                                  float z_offset = 0.0f,
                                  bool clip_to_z_min = false,
                                  float z_min = 0.0f) const;
  void draw_authored_runtime_mesh(const RuntimeMesh& mesh, float origin_x,
                                  float origin_y, uint32_t tint,
                                  float scale = 1.0f,
                                  bool use_texture_alpha = true,
                                  float z_offset = 0.0f,
                                  bool clip_to_z_min = false,
                                  float z_min = 0.0f,
                                  bool use_vertex_color = true) const;
  void draw_authored_runtime_mesh_scaled(
      const RuntimeMesh& mesh, float origin_x, float origin_y, uint32_t tint,
      float scale_x, float scale_y, float scale_z = 1.0f,
      bool use_texture_alpha = true, float z_offset = 0.0f,
      bool clip_to_z_min = false, float z_min = 0.0f,
      bool use_vertex_color = true) const;
  void draw_centered_runtime_mesh_scaled(const RuntimeMesh& mesh, float cx,
                                         float cy, uint32_t tint,
                                         float scale_x, float scale_y,
                                         float scale_z = 1.0f,
                                         bool use_texture_alpha = true,
                                         float z_offset = 0.0f,
                                         bool clip_to_z_min = false,
                                         float z_min = 0.0f) const;
  void draw_centered_runtime_mesh_rotated(
      const RuntimeMesh& mesh, float cx, float cy, uint32_t tint,
      const std::array<float, 4>& quat_xyzw, float scale = 1.0f,
      bool use_texture_alpha = true, float z_offset = 0.0f) const;
  void draw_authored_runtime_mesh_rotated(
      const RuntimeMesh& mesh, float origin_x, float origin_y, uint32_t tint,
      const std::array<float, 4>& quat_xyzw, float scale = 1.0f,
      bool use_texture_alpha = true, float z_offset = 0.0f) const;
  void draw_authored_runtime_mesh_transformed(
      const RuntimeMesh& mesh, float origin_x, float origin_y, uint32_t tint,
      const MeshTransformSample& transform,
      bool use_texture_alpha = true, float z_offset = 0.0f,
      bool use_vertex_color = true) const;
  void draw_centered_runtime_mesh_transformed(
      const RuntimeMesh& mesh, float cx, float cy, uint32_t tint,
      const MeshTransformSample& transform,
      bool use_texture_alpha = true, float z_offset = 0.0f,
      bool use_vertex_color = true) const;
  void draw_centered_runtime_mesh_with_texture(
      const RuntimeMesh& mesh, const std::string& texture_name, float cx,
      float cy, uint32_t tint, float scale = 1.0f,
      bool use_texture_alpha = true, float z_offset = 0.0f,
      bool clip_to_z_min = false, float z_min = 0.0f) const;
  void draw_runtime_particles(
      const std::vector<RuntimeParticleSystem>& particles, float origin_x,
      float origin_y, double song_time, float intensity = 1.0f,
      bool one_shot = false, float x_scale = 1.0f,
      bool apply_depth_fade = false, float depth_fade_top_y = 0.0f,
      float depth_fade_alpha_dist = 0.0f,
      float state_frame = -1.0f) const;
  void configure_source_lighting() const;
  void load_track_graphics_config(const std::string& hdr_path,
                                  const std::string& ark_path);

  ghogx::render::Window* win_;
  IDirect3DDevice9* dev_ = nullptr;
  std::map<std::string, IDirect3DTexture9*> textures_;
  float lane_spacing_ = 4.0f;
  float board_half_x_ = 10.0f;
  float top_y_ = 110.0f;
  float remove_y_ = -15.0f;
  float alpha_dist_ = 40.0f;
  float y_per_second_ = 80.0f;
  float tail_glow_width_ = 1.5f;
  float tail_glow_tight_width_ = 0.7f;
  float horizon_tail_clip_ = 7.0f;
  float nowbar_tail_clip_ = 1.5f;
  float cam_near_ = 50.0f;
  float cam_far_ = 200.0f;
  float cam_z_start_ = 0.0f;
  float cam_z_end_ = 0.1f;
  float burn_normal_frame_ = 0.0f;
  float burn_whammy_frame_ = 10.0f;
  float burn_bonus_frame_ = 20.0f;
  std::array<std::string, 5> slot_color_names_ = {
      "green", "red", "yellow", "blue", "orange"};
  std::array<uint32_t, 5> slot_lane_colors_ = {
      0xe13ce646u, 0xe1eb3c32u, 0xe1f0d228u, 0xe13c96ebu, 0xe1f58c1eu};
  std::array<float, 4> track_speed_ = {1.0f, 1.0f, 1.4f, 1.4f};
  std::array<RuntimeMesh, 5> gem_mesh_;
  std::array<RuntimeMesh, 5> gem_specular_mesh_;
  std::array<RuntimeMesh, 5> hopo_mesh_;
  std::array<RuntimeMesh, 5> star_mesh_;
  std::array<RuntimeMesh, 5> star_top_mesh_;
  std::array<RuntimeMesh, 5> tail_mesh_;
  std::array<RuntimeMesh, 5> held_tail_mesh_;
  std::array<RuntimeLineMaterial, 5> held_tail_line_material_;
  RuntimeMesh held_tight_tail_mesh_;
  RuntimeLineMaterial held_tight_tail_line_material_;
  RuntimeMesh burn_castlight_mesh_;
  ColorAnimState burn_castlight_color_anim_;
  std::vector<RuntimeParticleSystem> burn_tail_particles_;
  std::vector<RuntimeParticleSystem> smash_normal_particles_;
  std::vector<RuntimeParticleSystem> smash_bonus_particles_;
  std::vector<RuntimeParticleSystem> smash_star_particles_;
  std::vector<RuntimeParticleSystem> smash_combo_particles_;
  RuntimeMesh star_base_mesh_;
  RuntimeMesh star_overlay_mesh_;
  RuntimeMesh star_black_top_mesh_;
  MeshTransformAnim star_note_anim_;
  float star_note_anim_duration_frames_ = 0.0f;
  std::vector<QuatAnimKey> star_note_rotation_keys_;
  float star_note_rotation_duration_frames_ = 0.0f;
  std::vector<QuatAnimKey> star_base_rotation_keys_;
  float star_base_rotation_duration_frames_ = 0.0f;
  RuntimeMesh gem_top_mesh_;
  RuntimeMesh pc_standard_top_mesh_;
  RuntimeMesh gem_glow_mesh_;
  RuntimeMesh star_phrase_tail_mesh_;
  RuntimeMesh star_tail_mesh_;
  RuntimeLineMaterial star_tail_line_material_;
  RuntimeMesh whammy_tail_mesh_;
  RuntimeLineMaterial whammy_tail_line_material_;
  RuntimeMesh bonus_tail_mesh_;
  RuntimeMesh bonus_glow_tail_mesh_;
  RuntimeLineMaterial bonus_tail_line_material_;
  RuntimeMesh bonus_gem_mesh_;
  RuntimeMesh bonus_gem_overlay_mesh_;
  RuntimeMesh gem_sparkle_mesh_;
  bool moving_note_standard_has_top_ = true;
  bool moving_note_standard_has_glow_ = false;
  bool moving_note_star_has_base_ = true;
  bool moving_note_star_has_lane_ = true;
  bool moving_note_star_has_overlay_ = true;
  bool moving_note_star_has_top_ = true;
  bool moving_note_star_prefers_black_top_ = true;
  MeshTransformAnim gem_sparkle_anim_;
  float gem_sparkle_anim_duration_frames_ = 0.0f;
  RuntimeMesh bonus_spark1_mesh_;
  RuntimeMesh bonus_spark2_mesh_;
  RuntimeMesh track_surface_mesh_;
  RuntimeMesh track_mask_mesh_;
  ColorAnimState surface_flash_2x_;
  ColorAnimState surface_flash_3x_;
  ColorAnimState surface_flash_4x_;
  RuntimeMesh track_side_rails_mesh_;
  SideRailColorState side_rails_building_;
  SideRailColorState side_rails_none_;
  SideRailColorState side_rails_warning_;
  SideRailColorState side_rails_star_;
  SideRailColorState side_rails_warning_star_;
  RuntimeMesh track_lane_lines_mesh_;
  MeshTransformAnim track_extend_anim_;
  RuntimeMesh star_power_track_glow_mesh_;
  RuntimeMesh bar_line_mesh_;
  RuntimeMesh beat_line_mesh_;
  RuntimeMesh half_beat_line_mesh_;
  RuntimeMesh quarter_beat_line_mesh_;
  RuntimeMesh gem_smasher_mesh_;
  std::array<RuntimeMesh, 5> smasher_add_meshes_;
  RuntimeMesh smasher_rim_mesh_;
  std::array<RuntimeMesh, 5> smasher_rim_meshes_;
  std::array<RuntimeMesh, 5> smasher_ring_add_meshes_;
  RuntimeMesh bonus_smasher_rim_mesh_;
  RuntimeMesh bonus_smasher_ring_add_mesh_;
  RuntimeMesh bonus_smasher_add_mesh_;
  RuntimeMesh smasher_shadow_mesh_;
  MeshTransformAnim smasher_press_anim_;
  float smasher_press_anim_duration_frames_ = 0.0f;
  RuntimeMesh hit_flame_mesh_;
  RuntimeMesh star_collect_flame_mesh_;
  RuntimeMesh bonus_hit_flame_mesh_;
  MeshTransformAnim hit_flame_anim_;
  MeshTransformAnim star_collect_flame_anim_;
  MeshTransformAnim bonus_hit_flame_anim_;
  float hit_flame_anim_duration_frames_ = 0.0f;
  float star_collect_flame_anim_duration_frames_ = 0.0f;
  float bonus_hit_flame_anim_duration_frames_ = 0.0f;
  ColorAnimState hit_flame_color_anim_;
  ColorAnimState star_collect_flame_color_anim_;
  ColorAnimState bonus_hit_flame_color_anim_;
  RuntimeMesh miss_mesh_;
  RuntimeMesh miss_top_mesh_;
  RuntimeMesh star_miss_mesh_;
  RuntimeMesh star_miss_top_mesh_;
  std::array<RuntimeMesh, 3> combo_lightning_mesh_;
  std::array<MeshTransformAnim, 3> combo_lightning_anim_;
  std::array<float, 3> combo_lightning_anim_duration_frames_ = {};
  std::array<ColorAnimState, 3> combo_lightning_color_anim_;
  std::string smasher_normal_texture_name_;
  std::array<std::string, 5> smasher_texture_names_;
  std::array<std::string, 5> smasher_add_texture_names_;
  std::array<uint8_t, 5> smasher_add_blends_ = {};
  std::array<std::string, 5> smasher_ring_texture_names_;
  std::string bonus_smasher_texture_name_;
  std::string bonus_smasher_add_texture_name_;
  uint8_t bonus_smasher_add_blend_ = 0;
  std::string bonus_smasher_ring_texture_name_;
  std::array<float, 4> track_environment_color_ = {1.0f, 1.0f, 1.0f, 1.0f};
  bool track_environment_color_ok_ = false;
  std::vector<RuntimeLight> track_lights_;
  std::string loaded_surface_ref_;
  std::map<uint64_t, SustainVisualState> sustain_visual_states_;
  float whammy_envelope_ = 0.0f;
  double last_sustain_visual_time_ = -1.0;
  bool selected_surface_loaded_ = false;
  bool surface_quad_underlay_ = false;
  bool loaded_ = false;
};

}  // namespace ghogx::game
