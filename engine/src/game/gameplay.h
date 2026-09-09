// engine/src/game/gameplay.h
//
// Gameplay — song gameplay state machine.
//
// Loads a song from the PS2 ARK, drives the note-hit detection loop,
// and renders the 2-D note highway.

#pragma once

#include "chart/midi_reader.h"
#include "character/char_clip.h"
#include "character/char_facefx.h"
#include "character/char_renderer.h"
#include "character/char_servo_character.h"
#include "character/character_type_script.h"
#include "game/audio_player.h"
#include "game/gameplay_session.h"
#include "game/gameplay_rules.h"
#include "game/camera_intro_timing.h"
#include "game/gh1_camera_helper.h"
#include "game/source_anim_task.h"
#include "game/highway_renderer.h"
#include "render/milo_scene_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ghogx::render { class Window; }

namespace ghogx::game {

struct VenueScriptExpression {
  std::string atom;
  std::vector<VenueScriptExpression> children;
};

struct VenueScriptStep {
  enum class Kind {
    SetState,
    CallHandler,
    FireFilter,
    AnimateEnv,
    AnimateObject,
    AnimateTo,
    AnimTask,
    SwitchAnim,
    SetObjectShowing,
    SetNamedObjectShowing,
    SetSingerEnvironment,
    AddTransformChild,
    SetFlareSteps,
    UnhookAnimParents,
    RemoveAnim,
    StopObjectAnimation,
    IfAllStates,
    IfStateEquals,
    IfObjectExists,
    IfExcitementGreater,
    IfElse,
    WithNamespace,
    ScheduleTask,
    CancelTask,
    TaskSleep,
    TaskLoop,
    TaskSetName,
    ForEach,
    Switch,
    Unsupported,
  };
  Kind kind = Kind::CallHandler;
  std::string name;
  std::string target;
  int value = 0;
  double delay = 0.0;
  double delay_max = 0.0;
  bool delay_random = false;
  bool task_thread = false;
  bool task_beat_units = false;
  bool target_is_state_ref = false;
  bool target_is_property_ref = false;
  float anim_dest_frame = 0.0f;
  float anim_period = 0.0f;
  float anim_period_max = 0.0f;
  bool anim_period_random = false;
  float anim_start_frame = 0.0f;
  float anim_start_frame_max = 0.0f;
  bool anim_start_random = false;
  float anim_end_frame = 0.0f;
  bool anim_has_range = false;
  bool anim_realtime = false;
  bool anim_units_explicit = false;
  bool anim_beat_units = false;
  float anim_scale = 1.0f;
  float anim_blend_seconds = 0.0f;
  std::string assign_state;
  std::vector<std::string> state_names;
  bool name_is_variable = false;
  struct Argument {
    bool variable = false;
    bool switch_expression = false;
    std::string selector;
    std::vector<std::string> values;
    std::map<int, std::vector<std::string>> switch_values;
  };
  std::vector<Argument> arguments;
  std::map<int, std::vector<VenueScriptStep>> switch_branches;
  std::vector<VenueScriptStep> children;
  std::vector<VenueScriptStep> else_children;
  VenueScriptExpression expression;
  bool has_expression = false;
};

struct VenueScriptHandler {
  std::vector<std::string> parameters;
  std::vector<VenueScriptStep> steps;
};

struct ActiveVenueScriptTask {
  uint32_t id = 0;
  std::string name;
  std::string object_name;
  std::string object_type;
  std::string state_slot;
  std::vector<VenueScriptStep> steps;
  size_t cursor = 0;
  double due_time = 0.0;
  bool thread = false;
  bool beat_units = false;
  bool canceled = false;
};

struct VenueScriptObjectInstance {
  std::string type;
  std::map<std::string, std::string> properties;
};

struct VenueScriptObjectMessage {
  std::string object;
  std::string message;
};

struct HitResult {
    bool hit;
    bool was_hopo;
};

struct CameraResultBuilderFrameState {
  bool has_filtered_target = false;
  std::array<float, 3> filtered_target = {0.0f, 0.0f, 0.0f};
  bool has_filtered_parent = false;
  std::array<float, 16> filtered_parent = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f};
};

struct CameraResultBuilderState {
  // CamShotFrame owns its cached target (unk34) and parent transform (unk44).
  // Keep the cache per decoded source frame; sharing one cache across an
  // outgoing/incoming pair changes the retail controller state machine.
  std::map<std::string, CameraResultBuilderFrameState> frames;
  std::string selected_outgoing_frame;
  std::string selected_incoming_frame;

  // Most recently updated target, retained for diagnostics that predate the
  // per-frame retail cache reconstruction.
  bool has_filtered_target = false;
  std::array<float, 3> filtered_target = {0.0f, 0.0f, 0.0f};

  // CamShot::Shake persistent members. GH2 stores the translation/angular
  // output, impulse target, and velocity vectors on the active CamShot and
  // clears all six in StartAnim. Keep the same lifetime here instead of
  // deriving frame-local noise in the renderer.
  std::array<float, 3> shake_translation_output = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> shake_angular_output = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> shake_translation_target = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> shake_angular_target = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> shake_translation_velocity = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> shake_angular_velocity = {0.0f, 0.0f, 0.0f};
  void reset() {
    frames.clear();
    selected_outgoing_frame.clear();
    selected_incoming_frame.clear();
    has_filtered_target = false;
    filtered_target = {0.0f, 0.0f, 0.0f};
    shake_translation_output = {0.0f, 0.0f, 0.0f};
    shake_angular_output = {0.0f, 0.0f, 0.0f};
    shake_translation_target = {0.0f, 0.0f, 0.0f};
    shake_angular_target = {0.0f, 0.0f, 0.0f};
    shake_translation_velocity = {0.0f, 0.0f, 0.0f};
    shake_angular_velocity = {0.0f, 0.0f, 0.0f};
  }
};

struct CameraManagerFreeCamState {
  bool active = false;
  int pad = 0;
  float rotate_rate = 0.001f;
  float slew_rate = 0.2f;
  float fov = 0.0f;
  float focal_plane = 0.0f;
  std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> forward = {0.0f, 1.0f, 0.0f};
  std::array<float, 3> up = {0.0f, 0.0f, 1.0f};
  std::array<float, 3> rot = {0.0f, 0.0f, 0.0f};
  bool frozen = false;
  bool use_parent_rotate_x = true;
  bool use_parent_rotate_y = true;
  bool use_parent_rotate_z = true;
  bool snapshot_valid = false;
  uint64_t poll_count = 0;
  bool poll_dof_enabled = false;
  float poll_blur_depth = 0.0f;
  float poll_max_blur = 0.0f;
  float poll_min_blur = 0.0f;
};

struct WorldCrowdAnimatedCameraSourceBinding {
  std::string source_name;
  std::array<float, 16> area_local_world{};
  std::array<float, 16>* animated_world = nullptr;
  std::array<std::array<float, 16>*, 3> animated_flat_worlds = {
      nullptr, nullptr, nullptr};
};

struct WorldCrowdAnimatedCameraSourceCache {
  std::string actor_path;
  std::vector<WorldCrowdAnimatedCameraSourceBinding> bindings;
};

class Gameplay {
 public:
  struct QuickplayRig {
    std::string character_outfit;
    std::string guitar;
    std::string venue;
    std::string anim_tempo;
    std::vector<std::string> band;
  };
  struct DiagnosticInputEvent {
    double song_time = 0.0;
    uint32_t mask = 0;
  };
  struct CameraKey {
    struct TargetRef {
      std::string entity;
      std::string subpart;
      std::string source_object;
    };
    struct SourceRecordHint {
      std::string source_ref;
      std::string owner_entity;
      std::string member;
    };
    struct CrowdRef {
      std::string ref;
      int rotate = 0;
      int modify_stamp = -1;
      std::vector<std::pair<int, int>> pairs;
    };
    std::string name;
    size_t source_object_order = 0;
    uint16_t camshot_revision = 0;
    uint16_t camshot_alt_revision = 0;
    bool camshot_source_cached_stream = false;
    float frame = 0.0f;
    float eye[3] = {};
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_quat = false;
    // Preserve all three authored Hmx::Matrix3 rows.  GH2 converts the raw
    // matrix to a quaternion during CamShotFrame::Interp; reconstructing m.x
    // from normalized look/up first silently discards authored scale/skew.
    float source_x[3] = {1.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 1.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, 1.0f};
    bool has_basis = false;
    bool camshot_zero_transform_reset = false;
    float fov = 0.0f;
    bool has_fov = false;
    float duration_frames = 0.0f;
    float blend_frames = 0.0f;
    float blend_ease = 0.0f;
    int blend_ease_mode = 0;
    bool has_timing = false;
    bool camshot_looping = true;
    int camshot_loop_keyframe = 0;
    bool has_camshot_looping = false;
    int camshot_anim_rate = 0;
    bool has_camshot_anim_rate = false;
    float camshot_anim_frame = 0.0f;
    bool has_camshot_anim_frame = false;
    float screen_offset[2] = {0.0f, 0.0f};
    bool has_screen_offset = false;
    float target_offset[3] = {0.0f, 0.0f, 0.0f};
    bool has_target_offset = false;
    bool rotate_for_screen_offset = false;
    float blur_depth = 0.35f;
    float max_blur = 1.0f;
    float min_blur = 0.0f;
    float focus_blur_multiplier = 0.0f;
    bool has_dof_fields = false;
    std::string focus_target_entity;
    std::string focus_target_subpart;
    std::string focus_target_source_object;
    float shake_noise_amp = 0.0f;
    float shake_noise_freq = 0.0f;
    float max_angular_offset[2] = {0.0f, 0.0f};
    bool has_shake_fields = false;
    float zoom_fov = 0.0f;
    bool has_zoom_fov = false;
    bool parent_first_frame = false;
    bool has_parent_first_frame = false;
    float selection_weight = 1.0f;
    bool selection_used = false;
    std::string category;
    float shot_filter = 0.9f;
    bool has_shot_filter = false;
    float clamp_height = -1.0f;
    bool has_clamp_height = false;
    float near_plane = 1.0f;
    float far_plane = 1000.0f;
    bool has_clip_planes = false;
    bool use_depth_of_field = false;
    bool has_use_depth_of_field = false;
    float path_frame = -1.0f;
    bool has_path_frame = false;
    float path_ease = 0.0f;
    bool has_legacy_path_ease = false;
    float legacy_path_frame_ignored = -1.0f;
    bool has_legacy_path_frame_ignored = false;
    std::string source_ref;
    bool camshot_shot_fields_decoded = false;
    size_t camshot_pose_body_offset = 0;
    bool has_camshot_pose_body_offset = false;
    size_t camshot_ref_tail_end = 0;
    bool has_camshot_ref_tail_end = false;
    size_t camshot_shot_tail_offset = 0;
    bool has_camshot_shot_tail_offset = false;
    std::string path_anim;
    bool has_path_anim = false;
    std::string path_trans_target;
    bool has_path_trans_target = false;
    size_t path_source_sample_frames = 0;
    size_t path_source_added_frames = 0;
    size_t path_source_translation_keys = 0;
    size_t path_source_rotation_keys = 0;
    size_t path_source_scale_keys = 0;
    // Preserve the decoded RndTransAnim pages on the owning CamShot.  The
    // sampled `positions` array is useful for diagnostics, but resampling that
    // array linearly drops the authored spline/slerp contract between keys.
    std::vector<CameraKey> path_source_translation_page;
    std::vector<CameraKey> path_source_rotation_page;
    std::vector<CameraKey> path_source_scale_page;
    float path_source_start_frame = 0.0f;
    float path_source_end_frame = 0.0f;
    bool has_path_source_frame_summary = false;
    bool path_trans_spline = false;
    bool path_repeat_trans = false;
    bool path_scale_spline = false;
    bool path_follow_path = false;
    bool path_rot_slerp = false;
    bool path_rot_spline = false;
    bool has_path_source_flags = false;
    float path_scale[3] = {1.0f, 1.0f, 1.0f};
    bool has_path_scale = false;
    float path_base_eye[3] = {};
    float path_base_forward[3] = {0.0f, 1.0f, 0.0f};
    float path_base_up[3] = {0.0f, 0.0f, 1.0f};
    bool has_path_base_pose = false;
    bool path_preserved_base_translation = false;
    float path_pose_span[3] = {};
    bool has_path_pose_span = false;
    float source_path_local_frame = 0.0f;
    float source_path_first_frame = 0.0f;
    float source_path_authored_frame = 0.0f;
    float source_path_submitted_frame = 0.0f;
    bool has_source_path_frame_mapping = false;
    float source_frame_local_frame = 0.0f;
    float source_frame_key_start_frame = 0.0f;
    float source_frame_duration_frames = 0.0f;
    float source_frame_blend_frames = 0.0f;
    float source_frame_key_blend = 0.0f;
    float source_frame_raw_local_frame = 0.0f;
    float source_frame_pre_loop_frames = 0.0f;
    float source_frame_loop_frames = 0.0f;
    size_t source_frame_loop_start_index = 0;
    size_t source_frame_key_index = 0;
    bool source_frame_loop_active = false;
    bool source_frame_loop_wrapped = false;
    bool has_source_frame_key_index = false;
    bool has_source_frame_mapping = false;
    bool source_frame_null_frame = false;
    float generated_source_position[3] = {};
    float generated_source_x[3] = {1.0f, 0.0f, 0.0f};
    float generated_source_forward[3] = {0.0f, 1.0f, 0.0f};
    float generated_source_up[3] = {0.0f, 0.0f, 1.0f};
    bool has_generated_source_rows = false;
    std::string target_entity;
    std::string target_subpart;
    std::string target_source_object;
    std::vector<TargetRef> target_refs;
    SourceRecordHint ps2_source_record;
    bool has_ps2_source_record = false;
    std::string parent_entity;
    std::string parent_subpart;
    std::string parent_source_object;
    bool use_parent_rotation = false;
    bool camshot_refs_decoded = false;
    // Compiled GH1 cam_paths exists/else policy. Empty for stock GH2 shots.
    std::string camera_target_fallback;
    std::string camera_parent_fallback;
    bool has_gh1_helper = false;
    float gh1_helper_filter = 0.0f;
    bool gh1_helper_target = false;
    bool gh1_helper_parent = false;
    std::array<float, 3> gh1_helper_shake{};
    bool has_gh1_crowd_region = false;
    int gh1_crowd_region = -1;
    std::string distance;
    std::string facing;
    std::string solo = "ok";
    bool special = false;
    bool walk_ok = true;
    bool starpower_ok = false;
    bool far_starpower_ok = false;
    bool low_excitement_ok = true;
    bool jump_ok = true;
    bool lighter = false;
    int platform_only = 0;
    bool ps3_per_pixel = false;
    int disabled_flags = 0;
    int flags = 0;
    bool hide_crowd = false;
    bool crowd_face_camera = false;
    int force_char_lod = -1;
    std::string next_shot_ref;
    bool has_crowd_selection = false;
    std::string crowd_selection_ref;
    std::vector<std::pair<int, int>> crowd_selection_pairs;
    std::vector<CrowdRef> crowd_refs;
    int legacy_crowd_modify_stamp = -1;
    bool has_legacy_crowd_modify_stamp = false;
    bool legacy_crowd_pairs_cleared_by_modify_stamp = false;
    bool legacy_crowd_modify_stamp_requires_resolved_crowd = false;
    std::vector<std::string> hide_list_refs;
    std::vector<std::string> show_list_refs;
    std::vector<std::string> gen_hide_list_refs;
    std::vector<std::string> bad_waypoint_refs;
    std::vector<std::string> draw_override_refs;
    std::vector<std::string> postproc_override_refs;
    std::string postprocess_ref;
    std::vector<std::string> camera_anim_refs;
    std::string glow_spot_ref;
    std::vector<CameraKey> source_camshot_keyframes;
    std::vector<CameraKey> positions;
  };
  struct VenueCameraFovAnim {
    struct FovKey {
      float fov = 0.0f;
      float frame = 0.0f;
    };
    std::string name;
    std::string cam;
    std::string keys_owner;
    uint16_t revision = 0;
    uint16_t anim_revision = 0;
    int anim_rate = 0;
    bool source_order_decoded = false;
    std::vector<FovKey> fov_keys;
    float duration_frames = 0.0f;
  };
  struct LightingPreset {
    struct TargetState {
      std::string spotlight;
      std::string target;
      size_t source_index = SIZE_MAX;
      float intensity = 0.0f;
      float color[3] = {1.0f, 1.0f, 1.0f};
      float rotation_xyzw[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      bool has_rotation = false;
      bool flare_enabled = true;
      bool has_flare_enabled = false;
    };
    struct EnvironmentState {
      std::string target;
      float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      bool fog_enabled = false;
      float fog_start = 0.0f;
      float fog_end = 0.0f;
      float fog_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };
    struct LightState {
      std::string target;
      float rotation_xyzw[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      float position[3] = {0.0f, 0.0f, 0.0f};
      float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      float range = 0.0f;
      int type = 0;
    };
    struct Keyframe {
      std::string name;
      size_t record_start = 0;
      size_t record_end = 0;
      size_t label_offset = 0;
      float duration = 0.0f;
      float fade_out = 0.0f;
      std::vector<std::string> mesh_targets;
      std::vector<TargetState> target_states;
      std::vector<EnvironmentState> environment_states;
      std::vector<LightState> light_states;
      std::vector<std::string> spot_refs;
      std::vector<std::string> spot_set_refs;
      std::vector<std::string> env_refs;
      std::vector<std::string> lit_refs;
    };
    std::string name;
    std::string category;
    std::string adjective;
    uint16_t revision = 0;
    uint16_t anim_revision = 0;
    int anim_rate = 0;
    bool source_order_decoded = false;
    bool looping = false;
    bool manual = false;
    bool locked = false;
    int platform_only = 0;
    uint32_t keyframe_count = 0;
    uint32_t min_excitement = 0;
    uint32_t max_excitement = 4;
    std::vector<std::string> keyframe_names;
    std::vector<size_t> keyframe_label_offsets;
    std::vector<Keyframe> keyframes;
    std::vector<std::string> spot_refs;
    std::vector<std::string> spot_set_refs;
    std::vector<std::string> env_refs;
    std::vector<std::string> lit_refs;
  };
  struct PendingLightingAdvance {
    std::string event;
    int pitch = 0;
    uint32_t cue_tick = 0;
    double apply_time = 0.0;
  };
  struct LightingSpotlight {
    std::string name;
    std::string target;
    std::string material;
    std::string group;
    float default_color[3] = {1.0f, 1.0f, 1.0f};
    float default_intensity = 1.0f;
    bool has_default_state = false;
    bool animate_color_from_preset = true;
    bool animate_orientation_from_preset = true;
  };
  struct VenueMeshAnim {
    struct Frame {
      std::vector<std::array<float, 3>> positions;
      float frame = 0.0f;
    };
    struct NormalFrame {
      std::vector<std::array<float, 3>> normals;
      float frame = 0.0f;
    };
    struct TexCoordFrame {
      std::vector<std::array<float, 2>> texcoords;
      float frame = 0.0f;
    };
    struct ColorFrame {
      std::vector<std::array<float, 4>> colors;
      float frame = 0.0f;
    };
    std::string name;
    std::string mesh;
    std::string keys_owner;
    uint32_t frame_count = 0;
    uint32_t vertex_count = 0;
    float duration_frames = 0.0f;
    std::vector<Frame> frames;
    std::vector<NormalFrame> normal_frames;
    std::vector<TexCoordFrame> texcoord_frames;
    std::vector<ColorFrame> color_frames;
  };
  struct VenueAnimFilterTarget {
    std::string mesh;
    ghogx::render::MiloSceneRenderer::MeshTransformAnim anim;
  };
  struct VenueAnimFilterMeshTarget {
    std::string mesh;
    VenueMeshAnim anim;
  };
  struct VenueAnimFilter {
    std::string name;
    std::string target_ref;
    std::string source_trigger;
    uint16_t revision = 0;
    uint16_t anim_revision = 0;
    int anim_rate = 0;
    float start_frame = 0.0f;
    float end_frame = 0.0f;
    float scale = 1.0f;
    float period = 0.0f;
    float offset_frame = 0.0f;
    float snap_frame = 0.0f;
    float jitter_frame = 0.0f;
    int type = 0;
    float event_blend_seconds = 0.0f;
    float event_delay_seconds = 0.0f;
    bool event_wait = false;
    std::vector<VenueAnimFilterTarget> targets;
    std::vector<VenueAnimFilterMeshTarget> mesh_anim_targets;
  };
  struct VenueGroupVisibility {
    std::vector<std::string> show_meshes;
    std::vector<std::string> hide_meshes;
  };
  struct VenueEventTriggerGate {
    std::string trigger_name;
    std::vector<std::string> route_keys;
    std::vector<std::string> enable_events;
    std::vector<std::string> disable_events;
    bool enabled = true;
  };
  struct VenueMaterialAnim {
    struct FloatKey {
      float value = 0.0f;
      float frame = 0.0f;
    };
    struct Vec3Key {
      float value[3] = {0.0f, 0.0f, 0.0f};
      float frame = 0.0f;
    };
    struct ColorKey {
      float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      float frame = 0.0f;
    };
    struct TextureKey {
      std::string texture;
      float frame = 0.0f;
    };
    std::string name;
    std::string material;
    std::string keys_owner;
    int anim_rate = 0;
    bool has_alpha = false;
    float start_alpha = 1.0f;
    float end_alpha = 1.0f;
    float duration_frames = 0.0f;
    std::vector<ColorKey> color_keys;
    std::vector<FloatKey> alpha_keys;
    std::vector<Vec3Key> tex_translation_keys;
    std::vector<Vec3Key> tex_scale_keys;
    std::vector<Vec3Key> tex_rotation_keys;
    std::vector<TextureKey> texture_keys;
  };
  struct VenueEnvironmentAnim {
    struct ColorKey {
      float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      float frame = 0.0f;
    };
    struct FogRangeKey {
      float range[2] = {0.0f, 0.0f};
      float frame = 0.0f;
    };
    std::string name;
    std::string environment;
    std::string keys_owner;
    float duration_frames = 0.0f;
    std::vector<ColorKey> color_keys;
    std::vector<ColorKey> fog_color_keys;
    std::vector<FogRangeKey> fog_range_keys;
  };
  struct VenueLightAnim {
    struct ColorKey {
      float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      float frame = 0.0f;
    };
    std::string name;
    std::string light;
    std::string keys_owner;
    float duration_frames = 0.0f;
    std::vector<ColorKey> color_keys;
  };
  struct VenueEventAnimRoute {
    std::string anim;
    std::string source_trigger;
    float source_blend_period_seconds = 0.0f;
    float source_delay_seconds = 0.0f;
    bool source_wait = false;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
  };
  struct ActiveVenueMaterialAnim {
    std::string name;
    std::string material;
    bool has_alpha = false;
    float start_alpha = 1.0f;
    float end_alpha = 1.0f;
    double start_time = 0.0;
    double duration_seconds = 0.0;
    float duration_frames = 0.0f;
    std::vector<VenueMaterialAnim::ColorKey> color_keys;
    std::vector<VenueMaterialAnim::FloatKey> alpha_keys;
    std::vector<VenueMaterialAnim::TextureKey> texture_keys;
    std::vector<VenueMaterialAnim::Vec3Key> tex_translation_keys;
    std::vector<VenueMaterialAnim::Vec3Key> tex_scale_keys;
    std::vector<VenueMaterialAnim::Vec3Key> tex_rotation_keys;
    bool persistent = true;
    float source_blend_period_seconds = 0.0f;
    float source_start_delay_seconds = 0.0f;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
    bool has_start_color = false;
    std::array<float, 4> start_color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool has_start_tex_transform = false;
    ghogx::render::MiloSceneRenderer::MaterialTexTransformSample
        start_tex_transform;
  };
  struct ActiveVenueEnvironmentAnim {
    std::string name;
    std::string environment;
    double start_time = 0.0;
    double duration_seconds = 0.0;
    float duration_frames = 0.0f;
    float start_frame = 0.0f;
    float end_frame = 0.0f;
    std::vector<VenueEnvironmentAnim::ColorKey> color_keys;
    std::vector<VenueEnvironmentAnim::ColorKey> fog_color_keys;
    std::vector<VenueEnvironmentAnim::FogRangeKey> fog_range_keys;
    bool persistent = true;
    bool target_frame_mode = false;
    float source_blend_period_seconds = 0.0f;
    float source_start_delay_seconds = 0.0f;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
  };
  struct ActiveVenueLightAnim {
    std::string name;
    std::string light;
    double start_time = 0.0;
    double duration_seconds = 0.0;
    float duration_frames = 0.0f;
    std::vector<VenueLightAnim::ColorKey> color_keys;
    bool persistent = true;
    float source_blend_period_seconds = 0.0f;
    float source_start_delay_seconds = 0.0f;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
  };
  struct VenueParticleRoute {
    struct ColorKey {
      std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
      float frame = 0.0f;
    };
    struct EmissionKey {
      float min_value = 0.0f;
      float max_value = 0.0f;
      float frame = 0.0f;
    };
    std::string anim;
    std::string particle;
    std::string keys_owner;
    std::string source_trigger;
    float source_blend_period_seconds = 0.0f;
    float source_delay_seconds = 0.0f;
    bool source_wait = false;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
    float duration_frames = 0.0f;
    std::vector<ColorKey> start_color_keys;
    std::vector<ColorKey> end_color_keys;
    std::vector<EmissionKey> emission_keys;
    std::vector<EmissionKey> speed_keys;
    std::vector<EmissionKey> life_keys;
    std::vector<EmissionKey> size_keys;
  };
  struct ActiveVenueParticleSystem {
    std::string particle;
    double start_time = 0.0;
    double duration_seconds = 0.0;
    float duration_frames = 0.0f;
    float source_blend_period_seconds = 0.0f;
    float source_start_delay_seconds = 0.0f;
    bool has_source_filter = false;
    VenueAnimFilter source_filter;
    std::vector<VenueParticleRoute::ColorKey> start_color_keys;
    std::vector<VenueParticleRoute::ColorKey> end_color_keys;
    std::vector<VenueParticleRoute::EmissionKey> emission_keys;
    std::vector<VenueParticleRoute::EmissionKey> speed_keys;
    std::vector<VenueParticleRoute::EmissionKey> life_keys;
    std::vector<VenueParticleRoute::EmissionKey> size_keys;
    bool persistent = true;
  };
  struct VenueProxyObject {
    std::string name;
    std::string type;
    std::string milo_path;
    std::unique_ptr<ghogx::render::MiloSceneRenderer> renderer;
    VenueAnimFilter directory_anim;
    std::map<std::string, std::array<float, 3>> source_local_positions;
    std::map<std::string, VenueMaterialAnim> mat_anims;
    std::vector<VenueParticleRoute> particle_routes;
    std::vector<std::string> all_meshes;
    std::map<std::string, std::vector<std::string>> group_meshes;
    std::vector<std::string> event_aliases;
    bool showing = false;
    bool animating = false;
    double anim_start_time = 0.0;
    float anim_start_frame = 0.0f;
    float anim_end_frame = 0.0f;
    float anim_period = 0.0f;
  };
  struct HandClipChoice {
    std::vector<std::string> short_names;
    std::vector<std::string> long_names;
    double length_threshold = 0.3;
  };
  struct HandChordRule {
    std::vector<int> keys;
    HandClipChoice choice;
  };
  struct FretHandMap {
    std::string name;
    std::array<HandClipChoice, 5> single;
    std::vector<HandChordRule> chords;
  };
  struct StrumHandMap {
    std::string name;
    HandClipChoice regular;
    HandClipChoice fallback;
    HandClipChoice single;
    HandClipChoice chord;
    bool source_has_chord_split = false;
  };
  struct SourceVenueAnimTaskPayload {
    VenueAnimFilter filter;
    std::string event_name;
    bool persistent = false;
    bool shot_scoped = false;
    bool polled = false;
  };
  using SourceVenueAnimTask = SourceAnimTask<SourceVenueAnimTaskPayload>;
  struct ActiveVenueAnimFilter {
    std::string event_name;
    std::vector<VenueAnimFilter> filters;
    double start_time = 0.0;
    bool persistent = true;
    bool shot_scoped = false;
    bool polled = false;
    std::map<std::string, SourceVenueAnimTask::Ptr> source_tasks;
  };

  Gameplay() = default;
  ~Gameplay();

  Gameplay(const Gameplay&) = delete;
  Gameplay& operator=(const Gameplay&) = delete;

  // Load a song from the ARK by short name (e.g. "shoutatthedevil").
  // hdr_path/ark_path are the paths to MAIN.HDR / MAIN_0.ARK.
  // difficulty: 0=Easy 1=Medium 2=Hard 3=Expert.
  bool load_song(const std::string& hdr_path, const std::string& ark_path,
                 const std::string& shortname, int difficulty = 3);
  // Keep GH2's gameplay presentation mounted while songs/worlds/performers
  // come from another game's content archive.  If unset, the content archive
  // remains the presentation source for backwards-compatible direct boots.
  void set_base_asset_paths(const std::string& hdr_path,
                            const std::string& ark_path) {
    base_hdr_path_ = hdr_path;
    base_ark_path_ = ark_path;
  }
  // Additional read-only content archives which may supply independently
  // selected characters (and that character's authored highway surface).
  // Selection is asset-backed: the runtime probes the selected content
  // archive, the GH2 base archive, then these archives in configured order.
  void set_auxiliary_asset_paths(
      std::vector<std::pair<std::string, std::string>> archives) {
    auxiliary_asset_paths_ = std::move(archives);
  }
  // The front-end owns the merged base-plus-DLC catalog. Hand its complete
  // authored projection to gameplay so a namespaced loose catalog never has
  // to replace config/gen/songs.dtb merely to be playable.
  void set_authored_song_runtime(std::string source_game,
                                 std::string midi_path,
                                 std::string audio_path,
                                 QuickplayRig rig) {
    authored_song_source_game_ = std::move(source_game);
    authored_song_midi_path_ = std::move(midi_path);
    authored_song_audio_path_ = std::move(audio_path);
    authored_song_rig_ = std::move(rig);
  }
  void clear_authored_song_runtime() {
    authored_song_source_game_.clear();
    authored_song_midi_path_.clear();
    authored_song_audio_path_.clear();
    authored_song_rig_.reset();
  }
  // Manage Band backing-performer preferences are role substitutions shared
  // by Career and Quickplay. Empty values preserve each song's authored cast.
  void set_backing_band_preferences(std::string bassist, std::string drummer,
                                    std::string keyboardist,
                                    std::string male_singer,
                                    std::string female_singer) {
    preferred_bassist_ = std::move(bassist);
    preferred_drummer_ = std::move(drummer);
    preferred_keyboardist_ = std::move(keyboardist);
    preferred_male_singer_ = std::move(male_singer);
    preferred_female_singer_ = std::move(female_singer);
  }
  // Character-select hands gameplay the exact variant routes generated from
  // the source archives. Empty strings clear the selection and restore the
  // song's authored quickplay character.
  void set_selected_character_variant(
      std::string selection, std::string model_path,
      std::string main_anim_path, std::string strum_anim_path,
      std::string fret_anim_path, std::string highway_surface_path,
      std::string animation_source_model_path = {},
      bool retarget_animation = false,
      std::vector<std::string> guitarist_hidden_roots = {}) {
    selected_character_selection_ = std::move(selection);
    selected_character_model_path_ = std::move(model_path);
    selected_character_main_anim_path_ = std::move(main_anim_path);
    selected_character_strum_anim_path_ = std::move(strum_anim_path);
    selected_character_fret_anim_path_ = std::move(fret_anim_path);
    selected_character_highway_surface_path_ =
        std::move(highway_surface_path);
    selected_character_animation_source_model_path_ =
        std::move(animation_source_model_path);
    selected_character_retarget_animation_ = retarget_animation;
    selected_character_guitarist_hidden_roots_ =
        std::move(guitarist_hidden_roots);
  }
  // Instrument selections committed by the retail menu flow. These are
  // distinct from diagnostic overrides: they are the player's equipped
  // profile choices and remain the normal gameplay source.
  void set_selected_instruments(
      std::string guitar, std::string guitar_outfit,
      std::string guitar_material, int guitar_paint_primary,
      int guitar_paint_secondary, std::string bass,
      std::string bass_outfit, std::string bass_material,
      int bass_paint_primary, int bass_paint_secondary) {
    selected_guitar_ = std::move(guitar);
    selected_guitar_outfit_ = std::move(guitar_outfit);
    selected_guitar_material_ = std::move(guitar_material);
    selected_guitar_paint_primary_ = guitar_paint_primary;
    selected_guitar_paint_secondary_ = guitar_paint_secondary;
    selected_bass_ = std::move(bass);
    selected_bass_outfit_ = std::move(bass_outfit);
    selected_bass_material_ = std::move(bass_material);
    selected_bass_paint_primary_ = bass_paint_primary;
    selected_bass_paint_secondary_ = bass_paint_secondary;
  }
  // Co-op player two owns the bassist slot. Carry that player's exact
  // character variant into the bassist performer rather than leaving the
  // song-authored NPC in control of the purchased bass.
  void set_selected_bassist_character_variant(
      std::string selection, std::string model_path,
      std::string main_anim_path, std::string strum_anim_path,
      std::string fret_anim_path,
      std::string animation_source_model_path = {},
      bool retarget_animation = false) {
    selected_bassist_selection_ = std::move(selection);
    selected_bassist_model_path_ = std::move(model_path);
    selected_bassist_main_anim_path_ = std::move(main_anim_path);
    selected_bassist_strum_anim_path_ = std::move(strum_anim_path);
    selected_bassist_fret_anim_path_ = std::move(fret_anim_path);
    selected_bassist_animation_source_model_path_ =
        std::move(animation_source_model_path);
    selected_bassist_retarget_animation_ = retarget_animation;
  }
  std::string_view quickplay_character_outfit() const {
    return quickplay_rig_ ? std::string_view(quickplay_rig_->character_outfit)
                          : std::string_view();
  }

  // Called each frame.
  // dt        — frame delta seconds.
  // fret_mask — current button bitmask:
  //             bit0=Green  bit1=Red  bit2=Yellow  bit3=Blue
  //             bit4=Orange bit5=Strum bit6=Star power bit7=Whammy.
  void tick(float dt, uint32_t fret_mask, float whammy_axis = 0.0f);
  // Continue character presentation after the scoring/audio clock has stopped.
  void advance_ending_animation(float dt);

  // Build venue, performer, camera, lighting, and prop render resources while
  // the caller is still presenting its loading screen. This keeps asset
  // decoding and GPU resource creation out of the first playable frame.
  bool prepare_world(ghogx::render::Window& win);

  // Draw the highway for this frame.
  void draw(ghogx::render::Window& win);
  void stop_audio();
  void set_paused(bool paused);

  bool is_loaded()   const { return chart_loaded_; }
  // Song is finished when the audio clock passes the chart duration.
  bool is_finished() const;
  double song_time() const { return song_time_; }
  double highway_song_time() const {
    return track_intro_active_ ? calibrated_presentation_time(
        ghogx::camera::intro_song_seconds(intro_presentation_time_, intro_camera_seconds_),
        audio_offset_ms_) : song_time_;
  }
  // The venue/song-card/highway opening has its own clock. Song audio, notes,
  // scoring, and gameplay MIDI remain at zero until this presentation ends.
  double intro_presentation_time() const {
    return intro_presentation_time_;
  }
  void set_calibration_offsets_ms(int audio_offset_ms,
                                  int video_input_offset_ms) {
    audio_offset_ms_ = std::clamp(audio_offset_ms, -500, 500);
    sync_offset_ms_ = std::clamp(video_input_offset_ms, -500, 500);
  }
  int audio_offset_ms() const { return audio_offset_ms_; }
  int video_input_offset_ms() const { return sync_offset_ms_; }
  void set_sync_offset_ms(int offset_ms) {
    sync_offset_ms_ = std::clamp(offset_ms, -500, 500);
  }
  int sync_offset_ms() const { return sync_offset_ms_; }
  void set_deterministic_clock(bool deterministic) {
    deterministic_clock_ = deterministic;
  }
  void set_diagnostic_autoplay(bool enabled) {
    diagnostic_autoplay_ = enabled;
    diagnostic_autoplay_last_note_tick_ = UINT32_MAX;
  }
  void set_diagnostic_character_override(const std::string& character) {
    diagnostic_character_override_ = character;
  }
  // General role-scoped proof route. The reference accepts the same
  // archive-qualified `source:model` form as the selected guitarist and is
  // resolved by the normal performer loader.
  void set_diagnostic_performer_override(std::string role,
                                         std::string character_reference) {
    diagnostic_performer_overrides_[std::move(role)] =
        std::move(character_reference);
  }
  // Decouples a diagnostic performer's animation owner from its rendered
  // model. The reference uses the same archive-qualified `source:model` form
  // as the performer override and is resolved through normal source assets.
  void set_diagnostic_performer_animation_override(
      std::string role, std::string character_reference) {
    diagnostic_performer_animation_overrides_[std::move(role)] =
        std::move(character_reference);
  }
  // Diagnostic venue breadth helper: keeps song/band data from songs.dtb, but
  // routes world/lighting/characters/drums through another authored venue.
  void set_diagnostic_venue_override(const std::string& venue) {
    diagnostic_venue_override_ = venue;
  }
  // Diagnostic prop helper: keeps the stock song/venue/character selection, but
  // substitutes guitarist0's quickplay guitar symbol for prop-anchor traces.
  void set_diagnostic_guitar_override(const std::string& guitar) {
    diagnostic_guitar_override_ = guitar;
  }
  void set_diagnostic_bass_override(const std::string& bass) {
    diagnostic_bass_override_ = bass;
  }
  void set_diagnostic_venue_event(const std::string& event_name) {
    diagnostic_venue_event_ = event_name;
    diagnostic_venue_event_applied_ = false;
  }
  void set_diagnostic_camera_shot(const std::string& shot_name) {
    diagnostic_camera_shot_ = shot_name;
  }
  void set_diagnostic_unlit_performers(bool enabled) {
    diagnostic_unlit_performers_ = enabled;
  }
  void set_diagnostic_front_camera(const std::string& role) {
    diagnostic_front_camera_role_ = role;
    diagnostic_front_camera_reported_ = false;
  }
  void set_diagnostic_camera_path_offset_frames(double frames) {
    diagnostic_camera_path_offset_frames_ = frames;
  }
  void set_diagnostic_camera_random_seed(int seed);
  void handle_camera_random_seed_like_source(int seed);
  bool cycle_camera_shot_like_source();
  size_t iterate_camera_shots_like_source() const;
  void set_diagnostic_rock_fill(double fill);
  void set_diagnostic_star_power_fill(double fill);
  void set_diagnostic_star_power_active(bool active);
  // Diagnostic capture helper: jump the deterministic song clock to a known
  // authored window without replaying all earlier note/cue events.
  void seek_for_diagnostic_capture(double seconds);
  std::vector<DiagnosticInputEvent> build_diagnostic_guitar_script_from_chart(
      double start_sec,
      double end_sec,
      bool activate_star_power = true,
      double hit_offset_sec = -(1.0 / 120.0),
      bool whammy_star_sustains = false,
      std::optional<double> star_power_at_sec = std::nullopt) const;
  int    score()     const { return score_; }
  int    streak()    const { return streak_; }
  int    longest_streak() const { return longest_streak_; }
  int    multiplier()const { return multiplier_; }
  int    hit_count() const { return hit_count_; }
  int    miss_count() const { return miss_count_; }
  int    overstrum_count() const { return overstrum_count_; }
  struct SectionResult {
    std::string name;
    int hit = 0;
    int total = 0;
  };
  std::vector<SectionResult> section_results() const;
  int completed_star_phrases() const { return completed_star_phrases_; }
  int total_star_phrases() const;
  float average_multiplier() const {
    return multiplier_sample_count_ > 0
               ? multiplier_sample_sum_ /
                     static_cast<float>(multiplier_sample_count_)
               : 1.0f;
  }
  // Retail menu/campaign selection, distinct from diagnostic overrides.
  void set_selected_venue(const std::string& venue) {
    selected_venue_ = venue;
  }
  void set_intro_camera_category(const std::string& category) {
    intro_camera_category_ = category.empty() ? "INTRO" : category;
  }
  bool   star_power_active() const { return star_power_.active; }
  float  star_power_fill() const;
  float  rock_fill() const;
  bool   failed() const { return failed_; }
  bool   track_intro_active() const { return track_intro_active_; }
  double track_intro_elapsed() const {
    return ghogx::camera::intro_track_elapsed(
        track_intro_active_ ? intro_presentation_time_ :
            intro_camera_seconds_ + std::max(0.0, audio_master_time_),
        intro_camera_seconds_, track_extend_sec_);
  }
  bool venue_intro_active() const {
    return track_intro_active_ && track_intro_elapsed() < 0.0;
  }
  int    difficulty()const { return difficulty_; }

 private:
  void draw_internal(ghogx::render::Window& win, bool initialize_only);

  struct LightPresetEnvLightStateSnapshot {
    std::map<std::string, std::array<float, 4>> lighting_environment_colors;
    std::map<std::string, std::array<float, 4>>
        lighting_environment_fog_colors;
    std::map<std::string, std::array<float, 2>>
        lighting_environment_fog_ranges;
    std::map<std::string, bool> lighting_environment_fog_enabled;
    std::map<std::string, std::array<float, 4>> lighting_light_colors;
    std::map<std::string, ghogx::render::MiloSceneRenderer::LightStateOverride>
        lighting_light_state_overrides;
    std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformSample>
        lighting_light_transforms;
    std::map<std::string, std::array<float, 4>> venue_environment_colors;
    std::map<std::string, std::array<float, 4>> venue_environment_fog_colors;
    std::map<std::string, std::array<float, 2>> venue_environment_fog_ranges;
    std::map<std::string, bool> venue_environment_fog_enabled;
    std::map<std::string, std::array<float, 4>> venue_light_colors;
    std::map<std::string, ghogx::render::MiloSceneRenderer::LightStateOverride>
        venue_light_state_overrides;
    std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformSample>
        venue_light_transforms;
  };

  void apply_venue_event(const std::string& event_name, bool persistent = true,
                         bool force_persistent = false,
                         int next_link_depth = 0);
  bool apply_venue_event_visibility(const std::string& event_name, bool log);
  void update_venue_event_trigger_gates(const std::string& event_name);
  bool venue_event_trigger_enabled_by_name(
      const std::string& trigger_name) const;
  bool venue_event_route_enabled_by_triggers(
      const std::string& event_name) const;
  void prime_diagnostic_venue_event_gates(const std::string& event_name);
  bool try_apply_diagnostic_venue_event();
  std::unordered_set<std::string> composed_venue_hidden_meshes() const;
  std::map<std::string, float> composed_venue_material_alpha() const;
  void apply_camera_crowd_visibility(const CameraKey& key,
                                     bool skip_script_crowd_update = false);
  void start_camera_shot_anims(const CameraKey& key,
                               const std::string& runtime_name);
  void end_camera_shot_anims();
  void apply_active_camera_fov_anims(ghogx::render::OrbitCamera& cam,
                                     const CameraKey& key);
  void queue_regular_camera_shot(const CameraKey& key,
                                 const char* source_handler,
                                 double source_local_frame = 0.0);
  const CameraKey* camera_manager_current_shot_like_source() const;
  const CameraKey* camera_manager_next_shot_like_source() const;
  bool force_camera_shot_like_source(const CameraKey& key,
                                     const char* source_handler,
                                     double source_local_frame = 0.0);
  bool apply_camshot_radio_message_like_source(std::string_view shot_name,
                                               int set_mask,
                                               int clear_mask);
  bool apply_camshot_crowd_message_like_source(std::string_view shot_name,
                                               std::string_view source_msg,
                                               int crowd_index);
  bool camera_manager_get_free_cam_like_source(int padnum,
                                               const char* source_handler);
  bool camera_manager_has_free_cam_like_source() const;
  bool camera_manager_delete_free_cam_like_source(const char* source_handler);
  void camera_manager_update_free_cam_from_camera_like_source();
  void camera_manager_poll_free_cam_like_source(const char* source_context);
  void camera_manager_startshot_side_effects_like_source();
  void free_camera_set_pos_like_source(float x, float y, float z,
                                       const char* source_handler);
  void free_camera_set_rot_like_source(float x_degrees, float y_degrees,
                                       float z_degrees,
                                       const char* source_handler);
  void free_camera_set_parent_dof_like_source(bool use_x, bool use_y,
                                              bool use_z,
                                              const char* source_handler);
  void free_camera_set_frozen_like_source(bool frozen,
                                          const char* source_handler);
  std::string camera_source_guitarist0_nearest_walkspot() const;
  const CameraKey* camera_source_intro_previous_key() const;
  bool queue_source_category_camera_shot(
      std::string_view category, const char* source_message,
      const CameraKey** queued_key_out = nullptr);
  void update_source_game_over_camera_messages(
      bool authored_gameplay_cameras_active,
      bool in_intro_camera_window);
  bool handle_camera_active_players_changed_like_source(int players);
  std::optional<int> consume_diagnostic_camera_active_players_changed(
      double song_time);
  const CameraKey* camera_manager_source_shot_after(
      const std::string& current_name) const;
  bool consume_pending_regular_camera_shot();
  void clear_pending_regular_camera_after_start_like_source();
  void start_camera_shot_runtime(const CameraKey& key,
                                 bool source_restart = false);
  void end_camera_shot_runtime(bool skip_script_crowd_update = false,
                               bool source_no_current_teardown = true);
  void reset_world_camera_script_state_like_source(const char* source_caller);
  void restore_camera_script_state_for_diagnostic_seek_like_source(
      const char* source_caller);
  void reset_camera_manager_like_source_enter(const char* context);
  void destroy_camera_manager_like_source(const char* context);
  std::optional<ghogx::render::MiloSceneRenderer::SpotlightState>
      camera_glow_spot_state_for_ref(const std::string& raw_ref) const;
  void set_camera_glow_spot_ref(const std::string& raw_ref);
  void refresh_worldcrowd_actor_source_targets_for_camera();
  void resend_active_venue_event();
  void clear_runtime_venue_animation_state();
  void update_active_venue_material_anims();
  void update_active_venue_environment_anims();
  void update_active_venue_light_anims();
  void update_active_venue_particles();
  void update_active_venue_anim_filters();
  void poll_venue_presentation_tasks();
  void rebase_venue_presentation_tasks(double elapsed_intro);
  bool apply_lighting_event(const std::string& event_name,
                            bool persistent = true);
  bool apply_lighting_event_visibility(const std::string& event_name,
                                       bool log);
  bool update_gameplay_session_mirror(uint32_t fret_mask,
                                       bool emit_presentation,
                                       bool session_already_ticked = false,
                                       float whammy_axis = 0.0f,
                                       double judgement_time = -1.0);
  void sync_consumed_notes_from_gameplay_session();
  std::unordered_set<std::string> composed_lighting_hidden_meshes() const;
  std::map<std::string, float> composed_lighting_material_alpha() const;
  void update_active_lighting_material_anims();
  void update_active_lighting_environment_anims();
  void update_active_lighting_light_anims();
  void update_active_lighting_particles();
  void update_active_lighting_anim_filters();
  void set_lighting_spot_targets(
      std::vector<ghogx::render::MiloSceneRenderer::SpotlightState> targets,
      double fade_seconds);
  void set_lighting_preset_env_light_targets(
      LightPresetEnvLightStateSnapshot targets, double fade_seconds);
  LightPresetEnvLightStateSnapshot
      current_lighting_preset_env_light_state_for_targets(
          const LightPresetEnvLightStateSnapshot& targets) const;
  void apply_lighting_preset_env_light_state_snapshot(
      const LightPresetEnvLightStateSnapshot& state);
  void update_lighting_preset_env_light_state();
  std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
      interpolated_lighting_spots() const;
  std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
      composed_lighting_spots_for_renderer(
          std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
              spots) const;
  void update_lighting_spotlight_renderer();
  void execute_venue_script_event(
      const std::string& event_name,
      const std::map<std::string, std::vector<std::string>>* variables =
          nullptr);
  bool execute_venue_script_object_messages(
      const std::map<std::string, std::vector<VenueScriptObjectMessage>>&
          routes,
      const std::string& event_name);
  bool execute_venue_script_object_message(
      const VenueScriptObjectMessage& message);
  void execute_venue_script_steps(const std::vector<VenueScriptStep>& steps,
                                  std::vector<std::string>& stack,
                                  const std::map<std::string,
                                                 std::vector<std::string>>*
                                      variables = nullptr);
  std::string evaluate_venue_script_expression(
      const VenueScriptExpression& expression);
  bool set_namespaced_performer_object_showing(
      const std::string& role, const std::string& object_name, bool showing);
  bool apply_venue_script_env_anim(const std::string& anim_name,
                                   float dest_frame,
                                   float period_seconds,
                                   const VenueScriptStep* switch_step = nullptr);
  bool apply_venue_script_animate_to(const VenueScriptStep& step);
  bool apply_venue_script_light_anim(const std::string& anim_name,
                                     const VenueScriptStep& switch_step);
  bool apply_venue_script_animation(const VenueScriptStep& step);
  bool set_legacy_venue_object_showing(const std::string& object_name,
                                       bool showing);
  bool execute_venue_proxy_object_message(
      const VenueScriptObjectMessage& message);
  void set_venue_proxy_object_showing(const std::string& object_name,
                                      bool showing);
  void start_venue_proxy_object_animation(const std::string& object_name,
                                          float start_frame,
                                          float end_frame,
                                          float period_seconds);
  void stop_venue_proxy_object_animation(const std::string& object_name);
  void update_venue_proxy_objects();
  void draw_venue_proxy_objects(const ghogx::render::OrbitCamera& cam);
  bool venue_proxy_camera_fully_hidden(
      const std::string& object_name,
      const VenueProxyObject& proxy) const;
  void update_venue_script_tasks();
  uint32_t schedule_venue_script_task(const VenueScriptStep& step);
  void cancel_venue_script_task_by_id(uint32_t id);
  void cancel_venue_script_task_by_name(const std::string& name);
  void cancel_venue_script_task_state_ref(const std::string& state_name);
  bool venue_script_task_exists(const std::string& name) const;
  void clear_venue_script_task_refs(uint32_t id);
  std::string venue_script_context_state_key(
      const std::string& state_name) const;
  double venue_script_delay_seconds(double amount, bool beat_units) const;
  double venue_script_delay_seconds(const VenueScriptStep& step,
                                    bool inherited_beat_units);
  double venue_script_random_float(double min_value, double max_value);
  void rebuild_worldcrowd_actor_runtime(ghogx::render::Window& win);
  bool worldcrowd_actor_runtime_enabled() const;
  void update_worldcrowd_actor_runtime(float dt);
  void update_worldcrowd_actor_lighting(
      const LightingPreset* preset = nullptr,
      const LightingPreset::Keyframe* keyframe = nullptr);
  void update_performer_lighting(
      const LightingPreset* preset = nullptr,
      const LightingPreset::Keyframe* keyframe = nullptr);
  void draw_worldcrowd_actor_runtime(
      const ghogx::render::OrbitCamera& cam);

  struct CharacterWorldFxRuntime {
    ghogx::character::WorldFx source;
    std::string proxy_milo;
    std::unique_ptr<ghogx::render::MiloSceneRenderer> renderer;
    std::vector<VenueAnimFilter> anim_filters;
    std::map<std::string, std::array<float, 3>> source_local_positions;
    double active_seconds = 0.0;
    bool was_active = false;
  };
  std::vector<CharacterWorldFxRuntime> load_character_world_fx_runtime(
      ghogx::render::Window& win, const std::string& source_hdr,
      const std::string& source_ark, const std::string& character_milo,
      const ghogx::character::Character& character,
      std::string_view owner_label);
  void update_character_world_fx_runtime(
      std::vector<CharacterWorldFxRuntime>& effects,
      ghogx::character::CharacterTypeScriptInstance* type_script,
      std::string_view owner_label, std::string_view state_label, float dt);
  size_t draw_character_world_fx_runtime(
      std::vector<CharacterWorldFxRuntime>& effects,
      const ghogx::character::Character& character,
      ghogx::character::CharacterTypeScriptInstance* type_script,
      const std::array<float, 16>& character_world,
      const ghogx::render::OrbitCamera& camera, bool front_pass);

  // Detect a strum-triggered or HOPO note hit in the given lane.
  HitResult try_hit(int lane, bool strummed, bool is_hopo_candidate);
  uint32_t diagnostic_autoplay_fret_mask(
      const std::vector<ghogx::chart::Note>& notes);

  ghogx::chart::Chart chart_;
  bool chart_loaded_ = false;

  AudioPlayer audio_;
  bool deterministic_clock_ = false;
  bool song_started_ = false;
  bool song_presentation_ready_ = false;
  bool track_intro_active_ = true;
  int next_track_intro_sfx_stage_ = 0;
  std::vector<double> pending_star_phrase_award_sfx_times_;
  uint32_t star_power_activation_serial_ = 0;
  bool sync_audit_started_ = false;
  double sync_audit_wall_start_sec_ = 0.0;
  double sync_audit_audio_start_sec_ = 0.0;
  double sync_audit_next_log_sec_ = 1.0;
  uint64_t sync_audit_frames_ = 0;
  std::unique_ptr<HighwayRenderer> highway_;
  std::unique_ptr<ghogx::render::MiloSceneRenderer> world_;
  std::unique_ptr<ghogx::render::MiloSceneRenderer> lighting_;
  std::unique_ptr<ghogx::render::MiloSceneRenderer> drum_kit_;

  struct Performer {
    struct Gh1WalkClip {
      ghogx::character::CharClip clip;
      std::vector<std::string> source_flags;
    };
    struct AuthoredDriver {
      std::string hdr_path;
      std::string ark_path;
      std::vector<std::string> milo_paths;
      std::vector<ghogx::character::CharClipCatalogEntry> clip_catalog;
      std::map<std::string, ghogx::character::CharClipGroup> groups;
      std::map<std::string, ghogx::character::CharClip> decoded_clips;
      std::vector<std::shared_ptr<const ghogx::character::Gh2ClipSetBinding>>
          clip_bindings;
      ghogx::character::CharClipPlayer player;
      ghogx::DataNode saved_node;
      bool saved_node_valid = false;
      std::string starved_handler;
      float beat_scale = 1.0f;
      size_t source_order = 0;
      std::string target;
      std::string weight_owner;
      std::string weight_prop;
      float weight = 1.0f;
    };
    std::string role;
    std::string character_name;
    std::string event_track;
    std::string track_surface_ref;
    std::string lighting_environment_ref;
    std::string prop_milo_ref;
    std::string prop_attach_bone;
    bool gh1_character_runtime = false;
    bool external_animation_retarget = false;
    std::unique_ptr<ghogx::character::Character> retarget_source_character;
    std::unique_ptr<ghogx::character::Character> retarget_target_character;
    bool charwalk_runtime = false;
    std::unique_ptr<ghogx::character::CharRenderer> renderer;
    std::unique_ptr<ghogx::character::CharacterTypeScriptInstance>
        type_script;
    std::map<std::string, AuthoredDriver> authored_drivers;
    std::map<std::string,
             std::unique_ptr<ghogx::character::SourceCharServoCharacter>>
        source_servos;
    bool source_servo_runtime = false;
    bool source_servo_runtime_reported = false;
    std::vector<CharacterWorldFxRuntime> world_fxes;
    bool type_script_solo_active = false;
    bool type_script_peak_active = false;
    bool type_script_god_effect_active = false;
    bool type_script_game_lost_active = false;
    ghogx::character::CharClip idle_clip;
    ghogx::character::CharClip intro_clip;
    ghogx::character::CharClip active_clip;
    ghogx::character::CharClip active_bad_clip;
    ghogx::character::CharClip active_extreme_clip;
    ghogx::character::CharClip active_allbeat_clip;
    ghogx::character::CharClip active_double_clip;
    ghogx::character::CharClip active_half_clip;
    ghogx::character::CharClip active_nosnare_clip;
    ghogx::character::CharClip band_jump_clip;
    ghogx::character::CharClip win_clip;
    ghogx::character::CharClip lose_clip;
    ghogx::character::CharClip win_final_clip;
    std::vector<ghogx::character::CharClip> star_power_group_clips;
    ghogx::character::CharClip face_base_clip;
    ghogx::character::CharClip face_visemes_clip;
    std::vector<ghogx::character::CharClip> active_group_clips;
    std::vector<Gh1WalkClip> gh1_walk_turn_clips;
    std::vector<Gh1WalkClip> gh1_walk_loop_clips;
    std::vector<Gh1WalkClip> gh1_walk_stop_clips;
    ghogx::character::CharClip strum_open_clip;
    ghogx::character::CharClip strum_clip;
    ghogx::character::CharClip fret_open_clip;
    ghogx::character::CharClip fret_clip;
    std::map<std::string, ghogx::character::CharClip> strum_named_clips;
    std::vector<ghogx::character::CharClip> fret_lane_clips;
    std::map<std::string, ghogx::character::CharClip> fret_named_clips;
    std::vector<ghogx::character::FaceFxLipSyncServo> facefx_servos;
    std::optional<ghogx::character::FaceFxGraph> facefx_graph;
    std::optional<ghogx::character::FaceFxAnimation> facefx_blink_animation;
    std::map<std::string, std::vector<std::string>>
        gh1_face_excitement_poses;
    std::vector<std::string> gh1_face_poses;
    std::string gh1_face_event_list;
    int gh1_face_event_offset = 0;
    float gh1_face_blend_seconds = 0.0f;
    float gh1_face_pose_seconds = 0.0f;
    std::string gh1_face_current_pose;
    std::string gh1_face_previous_pose;
    double gh1_face_pose_started = 0.0;
    double gh1_face_next_pose_time = 0.0;
    size_t gh1_face_random_draw_index = 0;
    std::string gh1_face_pose_group;
    ghogx::character::CharClipPlayer idle_player;
    ghogx::character::CharClipPlayer intro_player;
    ghogx::character::CharClipPlayer active_player;
    ghogx::character::CharClipPlayer gh1_walk_player;
    ghogx::character::CharClipPlayer face_base_player;
    ghogx::character::CharClipPlayer strum_open_player;
    ghogx::character::CharClipPlayer strum_player;
    ghogx::character::CharClipPlayer fret_open_player;
    ghogx::character::CharClipPlayer fret_player;
    std::vector<ghogx::character::CharClipPlayer> fret_extra_players;
    bool hand_driver_available = false;
    bool main_driver_realign = false;
    bool right_hand_driver_realign = false;
    bool left_hand_driver_realign = false;
    bool midi_playing = false;
    uint32_t last_note_tick = UINT32_MAX;
    double last_strum_started = -9999.0;
    double last_strum_duration = 0.0;
    uint32_t last_band_jump_tick = UINT32_MAX;
    double last_band_jump_started = -9999.0;
    double last_band_jump_duration = 0.0;
    size_t star_power_group_index = 0;
    int32_t star_power_group_which = 0;
    uint32_t last_star_power_activation_serial = 0;
    double star_power_animation_started = -9999.0;
    double star_power_animation_duration = 0.0;
    std::string transient_main_clip;
    std::string last_midi_marker;
    uint32_t last_midi_marker_tick = UINT32_MAX;
    uint32_t last_traced_performer_event_tick = UINT32_MAX;
    std::string active_clip_mode;
    size_t active_group_index = 0;
    int32_t active_group_which = 0;
    double active_group_started = 0.0;
    uint32_t active_group_last_bar = UINT32_MAX;
    bool gh1_walk_global_enabled = false;
    bool gh1_walk_venue_allowed = false;
    bool gh1_walk_venue_gate_explicit = false;
    std::array<bool, 5> gh1_walk_delay_enabled = {};
    std::array<float, 5> gh1_walk_delay_min = {};
    std::array<float, 5> gh1_walk_delay_max = {};
    std::array<float, 5> gh1_walk_delay_sample = {};
    float gh1_walk_slop = 12.0f;
    float charwalk_max_walk_wait = 0.0f;
    uint32_t charwalk_waypoint_flags = 0;
    float gh1_walk_undershoot = 0.0f;
    float gh1_walk_overshoot = -18.0f;
    // Retail CharWalk::mState is None/Going/Stopping. Keep the selected
    // turn/walk/stop clip phase separate from that controller state.
    int gh1_walk_state = 0;
    int gh1_walk_phase = 0;
    std::string gh1_walk_current_waypoint;
    std::string gh1_walk_target_waypoint;
    std::vector<std::string> charwalk_route_waypoints;
    std::vector<std::array<float, 3>> charwalk_route_positions;
    size_t charwalk_route_waypoint_index = 1;
    ghogx::character::SourceCharWalkMotionPlan charwalk_motion_plan;
    size_t charwalk_schedule_index = 0;
    size_t charwalk_plan_point_index = 0;
    float charwalk_offset_speed = 0.0f;
    std::array<float, 16> gh1_walk_target_world = {};
    std::array<float, 16> gh1_walk_root_world = {};
    bool gh1_walk_has_root_world = false;
    ghogx::character::SourceCharUtlClipPredictState gh1_walk_predict;
    ghogx::character::SourceCharUtlClipPredictFrame gh1_walk_last_facing;
    bool gh1_walk_predict_initialized = false;
    bool gh1_walk_has_last_facing = false;
    double gh1_walk_delay_epoch = -1.0;
    size_t gh1_walk_random_draw_index = 0;
    bool charwalk_start_request_active = false;
    double charwalk_start_request_deadline = -1.0;
    uint32_t charwalk_request_walk_flags = 0;
    uint32_t charwalk_request_turn_flags = 0;
    uint32_t charwalk_request_stop_flags = 0;
    int32_t charwalk_walk_group_which = 0;
    std::string charwalk_selected_walk_clip;
    float charwalk_phase_end_seconds = -1.0f;
    float charwalk_next_start_seconds = 0.0f;
    uint32_t last_anim_note_mask = UINT32_MAX;
    uint32_t last_anim_note_tick = UINT32_MAX;
    std::string last_anim_hand_event;
    uint32_t last_direct_hand_event_tick = UINT32_MAX;
    size_t strum_hand_scheduler_child_index = 0;
    size_t fret_hand_scheduler_child_index = 0;
    double next_performer_sync_log_time = 0.0;
    double next_performer_prop_log_time = 0.0;
    std::vector<std::string> active_strum_clip_names;
    std::vector<std::string> active_fret_clip_names;
    std::array<float, 16> world_transform = {1.0f, 0.0f, 0.0f, 0.0f,
                                             0.0f, 1.0f, 0.0f, 0.0f,
                                             0.0f, 0.0f, 1.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, 1.0f};
    // Character root LOCAL composed with the venue/stage placement above.
    // Camera targets and renderer consume this; CharWalk continues to own the
    // parent placement in world_transform.
    std::array<float, 16> effective_world_transform = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  };
  std::vector<Performer> performers_;
  ghogx::DataNode handle_performer_driver_message(
      size_t performer_index,
      const ghogx::character::CharacterTypeScriptDriverMessage& message);
  const ghogx::character::CharClip* resolve_performer_driver_node(
      Performer& performer, std::string_view driver_name,
      const ghogx::DataNode& node, bool save_node);
  ghogx::character::CharClipPlayer* performer_driver_player(
      Performer& performer, std::string_view driver_name);
  void bind_performer_type_script_driver(size_t performer_index);
  std::string last_performer_lighting_key_;

  std::optional<QuickplayRig> quickplay_rig_;
  std::string preferred_bassist_;
  std::string preferred_drummer_;
  std::string preferred_keyboardist_;
  std::string preferred_male_singer_;
  std::string preferred_female_singer_;
  std::string highway_surface_ref_;
  std::optional<ghogx::character::FaceFxAnimation> facefx_animation_;
  bool world_init_attempted_ = false;
  std::vector<CameraKey> camera_keys_;
  std::vector<CameraKey> regular_camera_keys_;
  bool gh1_arena_spot_route_active_ = false;
  size_t guitarist0_charwalk_object_count_ = 0;
  CameraKey source_intro_camera_previous_;
  bool has_source_intro_camera_previous_ = false;
  std::vector<std::string> regular_camera_source_record_member_table_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      camera_performer_targets_;
  std::string pending_regular_camera_;
  std::string active_regular_camera_;
  std::string previous_regular_camera_;
  double pending_regular_camera_start_ = 0.0;
  double pending_regular_camera_local_frame_ = 0.0;
  double active_regular_camera_start_ = 0.0;
  double intro_camera_seconds_ = 0.0;
  double track_extend_sec_ = -2.0; // overwritten by required ui/gen/game.dtb property
  std::map<std::string, std::pair<int, int>> camera_duration_bars_;
  int camera_bars_left_ = 0;
  uint32_t last_camera_bar_ = UINT32_MAX;
  uint32_t last_camera_beat_ = UINT32_MAX;
  uint32_t camera_beat_state_ = 0;
  size_t next_forced_camera_event_idx_ = 0;
  size_t next_camera_one_bar_to_event_idx_ = 0;
  bool camera_solo_active_ = false;
  int camera_faceoff_active_players_ = 0;
  bool diagnostic_camera_active_players_change_applied_ = false;
  size_t camera_shot_counter_ = 0;
  size_t camera_normal_category_cursor_ = 0;
  CameraManagerFreeCamState camera_manager_free_cam_;
  CameraResultBuilderState camera_result_builder_state_;
  ghogx::camera::Gh1CameraHelper gh1_camera_helper_;
  int active_force_char_lod_ = -1;
  bool source_game_lost_camera_dispatched_ = false;
  bool source_game_won_message_dispatched_ = false;
  bool source_game_won_camera_dispatched_ = false;
  double source_game_won_message_time_ = 0.0;
  std::vector<std::string> source_game_won_camera_categories_;
  bool source_game_won_outro_complete_scheduled_ = false;
  bool source_game_won_outro_complete_dispatched_ = false;
  double source_game_won_outro_complete_time_ = 0.0;
  float source_game_won_outro_complete_delay_ = 0.0f;
  std::string source_game_won_outro_complete_shot_;
  std::string source_game_won_outro_complete_duration_source_;
  bool did_lighter_cam_ = false;
  bool crowd_lighter_on_ = false;
  std::string active_worldcrowd_lighter_group_;
  bool intro_end_dispatched_ = false;
  bool should_resend_excitement_ = false;
  std::vector<LightingPreset> lighting_presets_;
  std::vector<LightingSpotlight> lighting_spotlights_;
  std::unordered_map<std::string, const LightingSpotlight*>
      lighting_spots_by_name_;
  std::unordered_map<std::string, std::vector<const LightingSpotlight*>>
      lighting_spots_by_target_;
  std::string active_lighting_preset_;
  std::string active_lighting_keyframe_;
  size_t active_lighting_keyframe_index_ = SIZE_MAX;
  double active_lighting_preset_start_ = 0.0;
  std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
      active_lighting_spot_targets_;
  std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
      lighting_transition_from_;
  std::vector<ghogx::render::MiloSceneRenderer::SpotlightState>
      lighting_transition_to_;
  double lighting_transition_start_ = 0.0;
  double lighting_transition_duration_ = 0.0;
  bool lighting_transition_active_ = false;
  bool lighting_spot_renderer_dirty_ = true;
  LightPresetEnvLightStateSnapshot lighting_preset_env_light_transition_from_;
  LightPresetEnvLightStateSnapshot lighting_preset_env_light_transition_to_;
  double lighting_preset_env_light_transition_start_ = 0.0;
  double lighting_preset_env_light_transition_duration_ = 0.0;
  bool lighting_preset_env_light_transition_active_ = false;
  size_t next_lighting_cue_idx_ = 0;
  std::vector<PendingLightingAdvance> pending_lighting_advances_;
  bool ignored_last_light_change_ = false;
  std::map<std::string, VenueMaterialAnim> lighting_mat_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      lighting_event_mat_anims_;
  std::map<std::string, VenueEnvironmentAnim> lighting_env_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      lighting_event_env_anims_;
  std::map<std::string, VenueLightAnim> lighting_light_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      lighting_event_light_anims_;
  std::map<std::string, std::vector<VenueParticleRoute>>
      lighting_event_particle_systems_;
  std::map<std::string, VenueParticleRoute> lighting_direct_particle_anims_;
  std::map<std::string, std::vector<VenueAnimFilter>>
      lighting_event_anim_filters_;
  std::map<std::string, std::vector<VenueAnimFilter>>
      lighting_direct_anim_filters_;
  std::map<std::string, VenueGroupVisibility> lighting_event_group_visibility_;
  std::map<std::string, std::vector<VenueScriptObjectMessage>>
      lighting_event_script_messages_;
  std::map<std::string, float> lighting_material_alpha_;
  std::map<std::string, std::array<float, 4>> lighting_material_colors_;
  std::map<std::string, std::string> lighting_material_textures_;
  std::map<std::string,
           ghogx::render::MiloSceneRenderer::MaterialTexTransformSample>
      lighting_material_tex_transforms_;
  std::vector<ActiveVenueMaterialAnim> active_lighting_material_anims_;
  std::map<std::string, std::vector<std::string>> lighting_material_meshes_;
  std::map<std::string, ghogx::milo_scene::MatObj> lighting_material_defaults_;
  double last_lighting_mat_anim_debug_time_ = -1.0;
  std::map<std::string, std::array<float, 4>> lighting_environment_colors_;
  std::map<std::string, std::array<float, 4>>
      lighting_environment_fog_colors_;
  std::map<std::string, std::array<float, 2>>
      lighting_environment_fog_ranges_;
  std::map<std::string, bool> lighting_environment_fog_enabled_;
  std::map<std::string, float> lighting_environment_frames_;
  std::vector<ActiveVenueEnvironmentAnim> active_lighting_environment_anims_;
  std::map<std::string, std::array<float, 4>> lighting_light_colors_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::LightStateOverride>
      lighting_light_state_overrides_;
  std::vector<ActiveVenueLightAnim> active_lighting_light_anims_;
  std::map<std::string, ghogx::milo_scene::LightObj> lighting_lights_;
  std::map<std::string, ghogx::milo_scene::EnvironObj> lighting_environs_;
  std::unordered_set<std::string> legacy_gh1_performer_environments_;
  std::string legacy_gh1_singer_environment_;
  std::unordered_set<std::string> logged_legacy_gh1_performer_environments_;
  double last_lighting_env_anim_debug_time_ = -1.0;
  double last_lighting_light_anim_debug_time_ = -1.0;
  std::unordered_set<std::string> lighting_active_particle_systems_;
  std::map<std::string, float> lighting_particle_intensities_;
  std::map<std::string, float> lighting_particle_sizes_;
  std::map<std::string, float> lighting_particle_speeds_;
  std::map<std::string, float> lighting_particle_lifetimes_;
  std::map<std::string, std::array<float, 4>> lighting_particle_start_colors_;
  std::map<std::string, std::array<float, 4>> lighting_particle_end_colors_;
  std::vector<ActiveVenueParticleSystem> active_lighting_particles_;
  double last_lighting_particle_debug_time_ = -1.0;
  std::map<std::string, std::array<float, 3>>
      lighting_mesh_source_local_positions_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformSample>
      lighting_mesh_transform_offsets_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      lighting_mesh_position_overrides_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      lighting_mesh_normal_overrides_;
  std::map<std::string, std::vector<std::array<float, 2>>>
      lighting_mesh_texcoord_overrides_;
  std::map<std::string, std::vector<std::array<float, 4>>>
      lighting_mesh_color_overrides_;
  std::vector<ActiveVenueAnimFilter> active_lighting_anim_filters_;
  double last_lighting_filter_debug_time_ = -1.0;
  std::unordered_set<std::string> lighting_base_hidden_meshes_;
  std::unordered_set<std::string> lighting_runtime_hidden_meshes_;
  std::map<std::string, std::vector<std::string>> lighting_group_meshes_;
  std::map<std::string, std::vector<std::string>>
      lighting_anim_group_children_;
  std::map<std::string, VenueMaterialAnim> venue_mat_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      venue_event_mat_anims_;
  std::map<std::string, VenueEnvironmentAnim> venue_env_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      venue_event_env_anims_;
  std::map<std::string, VenueLightAnim> venue_light_anims_;
  std::map<std::string, std::vector<VenueEventAnimRoute>>
      venue_event_light_anims_;
  std::map<std::string, std::vector<VenueParticleRoute>>
      venue_event_particle_systems_;
  std::map<std::string, VenueParticleRoute> venue_direct_particle_anims_;
  std::map<std::string, std::vector<std::string>> venue_event_filters_;
  std::map<std::string, std::vector<std::string>> venue_filter_mesh_targets_;
  std::map<std::string, VenueProxyObject> venue_proxy_objects_;
  double next_venue_proxy_draw_log_time_ = 0.0;
  std::map<std::string, std::vector<VenueAnimFilter>> venue_event_anim_filters_;
  std::map<std::string, std::vector<VenueAnimFilter>> venue_direct_anim_filters_;
  std::vector<VenueAnimFilter> venue_poll_anim_filters_;
  std::map<std::string, VenueCameraFovAnim> venue_camera_fov_anims_;
  std::map<std::string, VenueGroupVisibility> venue_event_group_visibility_;
  std::map<std::string, std::vector<std::string>> venue_event_next_links_;
  std::vector<VenueEventTriggerGate> venue_event_trigger_gates_;
  std::map<std::string, VenueScriptHandler> venue_script_handlers_;
  std::map<std::string, VenueScriptHandler> venue_script_functions_;
  std::map<std::string, std::map<std::string, VenueScriptHandler>>
      venue_script_object_handlers_;
  std::map<std::string, VenueScriptObjectInstance> venue_script_objects_;
  std::map<std::string, std::vector<VenueScriptObjectMessage>>
      venue_event_script_messages_;
  std::map<std::string, int> venue_script_initial_state_;
  std::map<std::string, int> venue_script_state_;
  std::map<std::string, uint32_t> venue_script_object_state_;
  std::vector<ActiveVenueScriptTask> venue_script_tasks_;
  uint32_t next_venue_script_task_id_ = 1;
  uint32_t venue_script_rng_state_ = 0x9e3779b9u;
  ActiveVenueScriptTask* running_venue_script_task_ = nullptr;
  std::string venue_script_context_object_;
  std::string venue_script_context_type_;
  std::string venue_script_namespace_role_;
  bool executing_venue_script_ = false;
  bool venue_script_sections_ready_ = true;
  std::string legacy_gh1_lighting_message_;
  std::unordered_set<std::string> venue_light_names_;
  std::unordered_set<std::string> venue_environ_names_;
  std::map<std::string, ghogx::milo_scene::LightObj> venue_lights_;
  std::map<std::string, ghogx::milo_scene::EnvironObj> venue_environs_;
  std::vector<std::string> pending_transient_venue_events_;
  std::map<std::string, std::vector<std::string>> venue_material_meshes_;
  std::map<std::string, float> venue_material_alpha_;
  std::map<std::string, std::array<float, 4>> venue_material_colors_;
  std::map<std::string, std::string> venue_material_textures_;
  std::map<std::string,
           ghogx::render::MiloSceneRenderer::MaterialTexTransformSample>
      venue_material_tex_transforms_;
  std::vector<ActiveVenueMaterialAnim> active_venue_material_anims_;
  std::map<std::string, ghogx::milo_scene::MatObj> venue_material_defaults_;
  double last_venue_mat_anim_debug_time_ = -1.0;
  std::map<std::string, std::array<float, 4>> venue_environment_colors_;
  std::map<std::string, std::array<float, 4>> venue_environment_fog_colors_;
  std::map<std::string, std::array<float, 2>> venue_environment_fog_ranges_;
  std::map<std::string, bool> venue_environment_fog_enabled_;
  std::map<std::string, float> venue_environment_frames_;
  std::vector<ActiveVenueEnvironmentAnim> active_venue_environment_anims_;
  std::map<std::string, std::array<float, 4>> venue_light_colors_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::LightStateOverride>
      venue_light_state_overrides_;
  std::vector<ActiveVenueLightAnim> active_venue_light_anims_;
  double last_venue_env_anim_debug_time_ = -1.0;
  double last_venue_light_anim_debug_time_ = -1.0;
  std::unordered_set<std::string> venue_active_particle_systems_;
  std::map<std::string, float> venue_particle_intensities_;
  std::map<std::string, float> venue_particle_sizes_;
  std::map<std::string, float> venue_particle_speeds_;
  std::map<std::string, float> venue_particle_lifetimes_;
  std::map<std::string, std::array<float, 4>> venue_particle_start_colors_;
  std::map<std::string, std::array<float, 4>> venue_particle_end_colors_;
  std::vector<ActiveVenueParticleSystem> active_venue_particles_;
  double last_venue_particle_debug_time_ = -1.0;
  std::map<std::string, std::array<float, 3>>
      venue_mesh_source_local_positions_;
  std::map<std::string, std::array<float, 3>> venue_mesh_translation_offsets_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformSample>
      venue_mesh_transform_offsets_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformSample>
      venue_latched_mesh_transform_offsets_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      venue_mesh_position_overrides_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      venue_latched_mesh_position_overrides_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      venue_mesh_normal_overrides_;
  std::map<std::string, std::vector<std::array<float, 3>>>
      venue_latched_mesh_normal_overrides_;
  std::map<std::string, std::vector<std::array<float, 2>>>
      venue_mesh_texcoord_overrides_;
  std::map<std::string, std::vector<std::array<float, 2>>>
      venue_latched_mesh_texcoord_overrides_;
  std::map<std::string, std::vector<std::array<float, 4>>>
      venue_mesh_color_overrides_;
  std::map<std::string, std::vector<std::array<float, 4>>>
      venue_latched_mesh_color_overrides_;
  std::vector<ActiveVenueAnimFilter> active_venue_anim_filters_;
  std::unordered_map<std::string, float> venue_animate_to_frames_;
  double last_venue_filter_debug_time_ = -1.0;
  std::unordered_set<std::string> venue_base_hidden_meshes_;
  std::unordered_set<std::string> venue_runtime_hidden_meshes_;
  // Exact decoded WorldCrowd object refs. CamShot hide_crowd and hide-list
  // WorldCrowd refs act on the crowd runtime, never on venue geometry.
  std::unordered_set<std::string> venue_worldcrowd_refs_;
  // Exact decoded RndMultiMesh drawable refs. GH1 crowd packages serialize
  // their audience as these authored instance lists; hide_crowd controls the
  // list objects, not arbitrary meshes whose names happen to contain crowd.
  std::unordered_set<std::string> venue_crowd_drawable_refs_;
  std::unordered_set<std::string> venue_mesh_names_;
  std::map<std::string, std::vector<std::string>> venue_group_meshes_;
  std::map<std::string, std::vector<std::string>> venue_anim_group_children_;
  std::map<std::string, std::string> venue_transform_parent_overrides_;
  std::map<std::string, int> venue_flare_steps_;
  std::map<std::string, std::array<float, 16>> venue_camera_target_worlds_;
  ghogx::milo_scene::Scene venue_chars_scene_;
  bool venue_chars_scene_loaded_ = false;
  std::vector<size_t> venue_charwalk_waypoint_registry_order_;
  std::map<std::string, ghogx::milo_scene::Scene> worldcrowd_actor_scenes_;
  std::map<std::string, ghogx::character::Character> worldcrowd_actor_characters_;
  std::map<std::string, ghogx::character::CharClip> worldcrowd_actor_clips_;
  std::vector<WorldCrowdAnimatedCameraSourceCache>
      worldcrowd_animated_camera_source_cache_;
  bool worldcrowd_animated_camera_source_cache_initialized_ = false;
  struct WorldCrowdActorRuntime {
    struct PlacementRef {
      std::string crowd_name;
      size_t actor_index = 0;
      size_t placement_index = 0;
      float source_character_height = 0.0f;
      std::array<float, 16> source_crowd_world = {
          1.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f};
      bool rotate_to_camera = false;
    };
    std::string actor_name;
    std::string actor_milo;
    std::unique_ptr<ghogx::character::CharRenderer> renderer;
    std::unique_ptr<ghogx::character::CharacterTypeScriptInstance>
        type_script;
    std::vector<CharacterWorldFxRuntime> world_fxes;
    ghogx::character::CharClip clip;
    std::map<std::string, ghogx::character::CharClip> clips_by_group;
    ghogx::character::CharClipPlayer player;
    std::string active_group;
    std::vector<std::array<float, 16>> placement_worlds;
    std::vector<PlacementRef> placement_refs;
    size_t animation_ordinal = 0;
    bool gh1_promoted = false;
    size_t gh1_promoted_ordinal = 0;
    bool gh1_transform_logged = false;
    float source_character_height = 0.0f;
    float fullness_fraction = 1.0f;
    double next_animation_proof_log_time = 0.0;
    std::uint64_t animation_proof_samples = 0;
  };
  std::map<std::string, WorldCrowdActorRuntime> worldcrowd_actor_runtime_;
  size_t worldcrowd_actor_runtime_placements_ = 0;
  bool worldcrowd_widescreen_ = false;
  std::string last_worldcrowd_actor_lighting_key_;
  double next_worldcrowd_actor_draw_log_time_ = 0.0;
  double last_worldcrowd_actor_source_sample_time_ = -1.0;
  double last_worldcrowd_actor_source_probe_log_time_ = -1.0;
  std::unordered_set<std::string> venue_camera_hidden_meshes_;
  std::unordered_set<std::string> venue_camera_shown_meshes_;
  std::map<std::string, std::unordered_set<std::string>>
      venue_camera_hidden_proxy_meshes_;
  std::map<std::string, std::unordered_set<std::string>>
      venue_camera_shown_proxy_meshes_;
  bool venue_camera_hide_crowd_ = false;
  bool venue_camera_crowd_face_camera_ = false;
  std::vector<CameraKey::CrowdRef> venue_camera_crowd_selections_;
  bool venue_camera_has_crowd_selection_ = false;
  std::string venue_camera_crowd_selection_ref_;
  std::vector<std::pair<int, int>> venue_camera_crowd_selection_pairs_;
  bool venue_camera_world_crowd_bounds_valid_ = false;
  size_t venue_camera_world_crowd_placements_ = 0;
  std::array<float, 3> venue_camera_world_crowd_min_ = {};
  std::array<float, 3> venue_camera_world_crowd_max_ = {};
  std::string active_camera_runtime_shot_;
  std::string active_camera_native_current_shot_;
  std::string active_camera_source_current_rndcam_;
  std::string active_camera_anim_event_;
  std::string active_camera_anim_target_;
  std::vector<std::string> active_camera_fov_anim_refs_;
  double active_camera_anim_start_time_ = 0.0;
  std::unordered_set<std::string> active_camera_fov_anim_reported_;
  std::string active_camera_glow_spot_ref_;
  std::string active_camera_postprocess_ref_;
  std::map<std::string, std::string> venue_camera_rndcam_summaries_;
  std::map<std::string, std::string> venue_camera_postprocess_summaries_;
  bool active_camera_shot_started_ = false;
  std::string active_camera_shot_started_reported_;
  size_t active_camera_setpreframe_calls_ = 0;
  size_t active_camera_setframe_calls_ = 0;
  std::string active_camera_frame_pair_reported_;
  std::string active_camera_interp_debug_reported_;
  std::string active_camera_last_prev_key_;
  std::string active_camera_last_next_key_;
  size_t active_camera_last_prev_index_ = SIZE_MAX;
  size_t active_camera_last_next_index_ = SIZE_MAX;
  bool active_camera_last_pair_null_frame_ = false;
  std::string active_camera_shot_over_reported_;
  std::string active_camera_shot_over_gate_reported_;
  std::unordered_set<std::string> active_camera_shots_over_;
  bool active_camera_skip_next_crowd_update_ = false;
  int camera_manager_random_seed_ = 0;
  std::string camera_manager_random_seed_source_ = "static_default";
  bool camera_manager_tri_frame_requested_ = false;
  int camera_manager_source_cooldown_ = 0;
  std::optional<ghogx::render::MiloSceneRenderer::SpotlightState>
      active_camera_glow_spot_;
  std::string active_venue_event_;
  std::map<std::string, ghogx::render::MiloSceneRenderer::MeshTransformAnim>
      drum_mesh_transform_anims_;
  std::map<std::string, std::vector<std::string>> drum_event_mesh_targets_;
  std::map<std::string, FretHandMap> fret_hand_maps_;
  std::map<std::string, StrumHandMap> strum_hand_maps_;
  std::map<int, std::string> hand_event_clips_;

  double last_anim_time_ = -1.0;
  uint32_t last_band_note_tick_ = UINT32_MAX;
  size_t next_drum_cue_idx_ = 0;
  bool drum_cue_crossed_this_frame_ = false;
  size_t next_bass_cue_idx_ = 0;
  size_t next_venue_cue_idx_ = 0;
  size_t next_section_venue_event_idx_ = 0;
  size_t next_music_start_event_idx_ = 0;

  struct ActiveSustain {
    uint32_t mask = 0;
    int gem_count = 0;
    double start_time = 0.0;
    double end_time = 0.0;
    double beat_seconds = 0.5;
  };
  std::vector<ActiveSustain> active_sustains_;

  double   audio_master_time_ = 0.0;
  double   intro_presentation_time_ = 0.0;
  double   song_time_      = 0.0;
  int      audio_offset_ms_ = 0;
  int      sync_offset_ms_ = 0;
  int      difficulty_     = 3;
  // Index of the next unprocessed note in chart_.notes[difficulty_].
  size_t   next_note_idx_  = 0;
  std::vector<uint8_t> note_consumed_[4];
  // 0=pending, 1=hit, 2=miss. Kept separately from consumed so the retail
  // section-results receipt can report real per-song section accuracy.
  std::vector<uint8_t> note_result_[4];

  int      score_          = 0;
  int      streak_         = 0;
  int      longest_streak_ = 0;
  int      multiplier_     = 1;
  int      hit_count_      = 0;
  int      miss_count_     = 0;
  int      overstrum_count_ = 0;
  int      completed_star_phrases_ = 0;
  float    multiplier_sample_sum_ = 0.0f;
  int      multiplier_sample_count_ = 0;
  FoFiXRockState rock_;
  FoFiXStarPowerState star_power_;
  std::optional<FoFiXGameplaySession> gameplay_session_mirror_;
  std::vector<FoFiXSessionSustain> active_session_sustains_;
  double gameplay_session_mirror_last_log_time_ = -1.0;
  std::string gameplay_session_sustain_log_signature_;
  bool     failed_         = false;
  bool     star_phrase_active_ = false;
  bool     star_phrase_missed_ = false;

  // Per-frame hit/miss feedback for the renderer (cleared each tick).
  uint32_t hit_flash_mask_  = 0;
  uint32_t miss_flash_mask_ = 0;

  // Per-lane hit-flame intensity (1.0 on hit, decays to 0). Drives the
  // strikeline flames in the renderer.
  float lane_flash_[5] = {};
  // Per-lane retail PlayerState::phraseState captured on the hit that started
  // lane_flash_: 0=none, 1=missed, 2=hitting, 3=complete.
  uint8_t hit_phrase_state_[5] = {};
  float star_collect_flash_[5] = {};
  float miss_flash_[5] = {};
  float star_miss_flash_[5] = {};
  float bad_highway_flash_ = 0.0f;
  float star_power_highway_flash_ = 0.0f;
  float multiplier_surface_flash_ = 0.0f;

  // Previous-frame fret mask for edge detection.
  uint32_t prev_fret_mask_  = 0;
  float prev_whammy_axis_ = 0.0f;
  bool diagnostic_autoplay_ = false;
  uint32_t diagnostic_autoplay_last_note_tick_ = UINT32_MAX;
  std::string diagnostic_character_override_;
  std::string diagnostic_character_archive_id_;
  std::unordered_map<std::string, std::string>
      diagnostic_performer_overrides_;
  std::unordered_map<std::string, std::string>
      diagnostic_performer_animation_overrides_;
  std::string diagnostic_venue_override_;
  std::string selected_venue_;
  std::string intro_camera_category_ = "INTRO";
  std::string diagnostic_guitar_override_;
  std::string diagnostic_bass_override_;
  std::string selected_guitar_;
  std::string selected_guitar_outfit_;
  std::string selected_guitar_material_;
  int selected_guitar_paint_primary_ = -1;
  int selected_guitar_paint_secondary_ = -1;
  std::string selected_bass_;
  std::string selected_bass_outfit_;
  std::string selected_bass_material_;
  int selected_bass_paint_primary_ = -1;
  int selected_bass_paint_secondary_ = -1;
  std::string diagnostic_venue_event_;
  std::string diagnostic_camera_shot_;
  std::string diagnostic_front_camera_role_;
  double diagnostic_camera_path_offset_frames_ = 0.0;
  std::optional<double> diagnostic_rock_fill_;
  std::optional<double> diagnostic_star_power_fill_;
  bool diagnostic_star_power_active_ = false;
  bool diagnostic_venue_event_applied_ = false;
  bool diagnostic_unlit_performers_ = false;
  bool diagnostic_front_camera_reported_ = false;

  // Per-lane: has this lane's gem been hit this pass (so we don't double-hit)?
  bool lane_hit_[5] = {};

  std::string hdr_path_;
  std::string ark_path_;
  std::string base_hdr_path_;
  std::string base_ark_path_;
  std::vector<std::pair<std::string, std::string>> auxiliary_asset_paths_;
  std::string authored_song_source_game_;
  std::string authored_song_midi_path_;
  std::string authored_song_audio_path_;
  std::optional<QuickplayRig> authored_song_rig_;
  std::string selected_character_selection_;
  std::string selected_character_model_path_;
  std::string selected_character_main_anim_path_;
  std::string selected_character_strum_anim_path_;
  std::string selected_character_fret_anim_path_;
  std::string selected_character_highway_surface_path_;
  std::string selected_character_animation_source_model_path_;
  bool selected_character_retarget_animation_ = false;
  std::vector<std::string> selected_character_guitarist_hidden_roots_;
  std::string selected_bassist_selection_;
  std::string selected_bassist_model_path_;
  std::string selected_bassist_main_anim_path_;
  std::string selected_bassist_strum_anim_path_;
  std::string selected_bassist_fret_anim_path_;
  std::string selected_bassist_animation_source_model_path_;
  bool selected_bassist_retarget_animation_ = false;
  std::string highway_asset_hdr_path_;
  std::string highway_asset_ark_path_;
  std::string song_shortname_;
};

}  // namespace ghogx::game
