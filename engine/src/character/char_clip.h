// engine/src/character/char_clip.h
//
// CharClipSamples decoder: loads all frames from a GH2 PS2 animation clip.

#pragma once

#include "character/char_mesh.h"
#include "character/char_clip_binding.h"
#include "character/char_bones_samples.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghogx::character {

class SourceCharBonesMeshesOutput;

enum SourceCharBonesType {
  kSourceCharBonesTypePos = 0,
  kSourceCharBonesTypeScale = 1,
  kSourceCharBonesTypeQuat = 2,
  kSourceCharBonesTypeRotX = 3,
  kSourceCharBonesTypeRotY = 4,
  kSourceCharBonesTypeRotZ = 5,
  kSourceCharBonesTypeEnd = 6,
};

struct SourceCharBonesLayout {
  std::array<int, kSourceCharBonesTypeEnd + 1> counts = {};
  std::array<int, kSourceCharBonesTypeEnd + 1> offsets = {};
  int total_size = 0;
};

struct SourceCharBonesCompressionUpdate {
  int compression = 0;
  SourceCharBonesLayout layout;
  bool changed = false;
};

struct SourceCharBonesBone {
  std::string name;
  float weight = 1.0f;
};

struct SourceCharBonesState {
  int compression = 0;
  SourceCharBonesLayout layout;
  std::vector<SourceCharBonesBone> bones;
};

struct SourceCharBonesFindPtrResult {
  bool found = false;
  int offset = -1;
};

struct SourceCharBonesScaleAddClipStep {
  bool call_clip_scale_add = true;
  float f1 = 0.0f;
  float f2 = 0.0f;
  float f3 = 0.0f;
};

struct SourceCharBonesPoseBodyBoundary {
  bool rb3_latest_declares_scale_add = true;
  bool rb3_latest_declares_rotate_by = true;
  bool rb3_latest_declares_rotate_to = true;
  bool rb3_latest_declares_blend = true;
  bool rb3_latest_declares_scale_down = true;
  bool rb3_latest_exposes_scale_add_body = false;
  bool rb3_latest_exposes_rotate_by_body = false;
  bool rb3_latest_exposes_rotate_to_body = false;
  bool rb3_latest_exposes_blend_body = false;
  bool rb3_latest_exposes_scale_down_body = false;
  bool rb2_dump_maps_scale_add = true;
  bool rb2_dump_maps_rotate_by = true;
  bool rb2_dump_maps_rotate_to = true;
  bool rb2_dump_maps_scale_down = true;
  bool rb2_dump_maps_scale_add_identity = true;
  bool rb2_dump_exposes_statement_body = false;
  bool safe_to_use_layout_helpers = true;
  bool safe_to_apply_pose_math = false;
  std::vector<std::string> fenced_bodies;
};

struct SourceCharBonesRuntimeDumpEvidence {
  std::string scale_down_range;
  std::string scale_add_range;
  std::string rotate_by_range;
  std::string rotate_to_range;
  std::string scale_add_identity_range;
  std::string blend_range;
  std::vector<std::string> scale_down_locals;
  std::vector<std::string> scale_add_locals;
  std::vector<std::string> rotate_by_locals;
  std::vector<std::string> rotate_to_locals;
  std::vector<std::string> scale_add_identity_locals;
  std::vector<std::string> blend_locals;
  bool rb2_dump_maps_blend = true;
  bool has_scale_down_statement_body = false;
  bool has_scale_add_statement_body = false;
  bool has_rotate_by_statement_body = false;
  bool has_rotate_to_statement_body = false;
  bool has_scale_add_identity_statement_body = false;
  bool safe_to_apply_pose_math = false;
};

struct SourceCharPoseRuntimeSymbolEvidence {
  std::string source;
  std::string function;
  std::string symbol;
  std::string address;
  uint32_t size = 0;
  bool has_statement_body = false;
  bool safe_to_import_runtime = false;
};

struct SourceCharBonesAddBonesSteps {
  std::vector<SourceCharBonesBone> add_bone_internal_calls;
  bool reallocate_internal = false;
};

struct SourceCharBonesAllocReallocateStep {
  bool free_m_start = true;
  int mem_alloc_size = 0;
  bool assign_m_start = true;
};

struct SourceCharBonesEnterStep {
  bool zero = true;
  bool set_weights = true;
  float set_weights_value = 0.0f;
};

struct SourceCharBonesBlenderPollStep {
  bool early_out = false;
  bool blend_dest = false;
  bool enter = false;
};

struct SourceCharBonesBlenderSetDestStep {
  bool changed = false;
  bool assign_dest = false;
  bool add_bones_to_dest = false;
};

struct SourceCharBonesBlenderSetClipTypeStep {
  bool changed = false;
  bool assign_clip_type = false;
  bool clear_bones = false;
  bool stuff_bones_from_dir = false;
};

struct SourceCharBonesBlenderReallocateStep {
  bool char_bones_alloc_reallocate_internal = true;
  bool add_bones_to_dest = false;
  bool enter = true;
};

struct SourceCharBonesBlenderLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> call_order;
  std::vector<std::string> branches;
};

struct SourceCharBonesBlenderSavePlan {
  int32_t save_id = 0x58;
};

struct SourceCharBonesBlenderCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> member_calls;
};

struct SourceCharBonesBlenderHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharBonesBlenderPropSyncPlan {
  std::vector<std::string> set_properties;
  std::vector<std::string> superclasses;
};

struct SourceCharBonesBlenderPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharBoneLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharBoneSavePlan {
  int32_t save_id = 0xBF;
};

struct SourceCharBoneCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharBoneHandlerPlan {
  std::vector<std::string> action_handlers;
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharBoneWeightContextPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharBoneWeightContextDefaultState {
  int context = 0;
  float weight = 0.0f;
};

struct SourceCharBoneWeightContextLoadPlan {
  std::vector<std::string> read_order;
};

struct SourceCharBoneContextFlagsStep {
  bool parent_is_char_bone_dir = false;
  bool returns_dir_context_flags = false;
  bool warns_no_char_bone_dir = false;
  bool returns_empty_array = false;
  std::string warning;
};

struct SourceCharBonePropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharBonesBonePropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  bool preview_uses_prop_bones_string_val = true;
};

struct SourceCharBonesObjectPropSyncPlan {
  bool assigns_prop_bones = true;
  std::vector<std::string> custom_branches;
};

struct SourceCharBoneDirDefaultState {
  bool recenter_targets_no_null = true;
  bool recenter_average_no_null = true;
  bool recenter_slide = false;
  int move_context = 0;
  bool bake_out_facing = true;
  bool context_flags_is_int = true;
  int context_flags_int = 0;
  int filter_context = 0;
  bool filter_bones_no_null = true;
  bool filter_names_empty = true;
};

struct SourceCharBoneDirLoadPlan {
  bool known_revision = false;
  std::vector<std::string> preload_order;
  std::vector<std::string> load_order;
  std::vector<std::string> postload_order;
  std::vector<std::string> branches;
};

struct SourceCharBoneDirSavePlan {
  int32_t save_id = 0x18c;
};

struct SourceCharBoneDirCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharBoneDirHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharBoneDirRecenterPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharBoneDirRecenterLoadPlan {
  std::vector<std::string> read_order;
};

struct SourceCharBoneDirPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> modify_properties;
  std::vector<std::string> modify_actions;
  std::vector<std::string> superclasses;
};

struct SourceCharBoneDirClipTypeResource {
  std::string clip_type;
  bool has_resource = false;
  std::string resource_name;
  int context_mask = 0;
  bool resource_found = false;
  std::string context_symbol;
};

struct SourceCharBoneDirInitClipTypeRow {
  std::string clip_type;
  bool has_resource = false;
  std::string resource_name;
  bool already_loaded = false;
  bool load_succeeds = true;
};

struct SourceCharBoneDirInitPlan {
  bool creates_char_resources = true;
  bool reads_resource_path = true;
  bool reads_char_clip_types = true;
  bool skipped_missing_clip_types = false;
  bool skipped_empty_resource_path = false;
  bool registers_get_clip_types = false;
  size_t scanned_rows = 0;
  std::vector<std::string> skipped_existing_resources;
  std::vector<std::string> load_requests;
  std::vector<std::string> named_loaded_resources;
  std::vector<std::string> failed_load_resources;
};

struct SourceCharBoneDirTerminatePlan {
  bool deletes_resources = true;
  bool clears_resources_pointer = false;
};

struct SourceCharBoneDirFindResourceResult {
  bool found = false;
  std::string resource_name;
};

struct SourceCharBoneDirResourceLookupResult {
  bool clip_type_found = false;
  bool resource_field_found = false;
  bool resource_found = false;
  std::string resource_name;
  int context_mask = 0;
  std::string warning;
};

struct SourceCharBoneDirStuffBonesSymbolStep {
  SourceCharBoneDirResourceLookupResult lookup;
  bool call_stuff_bones = false;
  int context_mask = 0;
};

struct SourceCharBoneDirContextFlagsStep {
  bool rebuilt = false;
  size_t scanned_rows = 0;
  std::vector<std::string> context_flags;
};

struct SourceCharBoneDirMergeTransform {
  std::string name;
  bool is_loaded_dir = false;
  bool animatable = false;
};

struct SourceCharBoneDirMergeCharacterPlan {
  bool load_attempted = true;
  bool loaded = false;
  bool warned_failed_load = false;
  size_t scanned_transforms = 0;
  std::vector<std::string> selected_transforms;
  bool merge_body_fenced = true;
};

struct SourceCharBonesMeshesReplaceStep {
  bool object_replace = true;
  bool scan_meshes = false;
  int replaced_index = -1;
  bool assigned_dummy = false;
  std::vector<std::string> meshes;
};

struct SourceCharBonesMeshesReallocateStep {
  bool char_bones_alloc_reallocate_internal = true;
  std::vector<std::string> meshes;
  std::vector<std::string> missing_non_facing_bones;
  bool acquire_pose = false;
};

struct SourceCharBonesMeshesLifetimePlan {
  bool constructs_mesh_vector_with_owner = true;
  bool creates_dummy_mesh_transform = true;
  bool destructor_clears_mesh_vector = true;
  bool destructor_deletes_dummy_mesh = true;
  bool dummy_mesh_is_fallback_target = true;
};

struct SourceCharBonesMeshesPoseDumpEvidence {
  std::string pose_meshes_range;
  std::string prop_sync_range;
  std::vector<std::string> pose_meshes_locals;
  std::string gh2_rexglue_pose_meshes_range;
  std::vector<std::string> gh2_rexglue_axis_setter_ranges;
  std::string latest_source_file;
  std::string latest_source_comment;
  std::vector<std::string> latest_source_stub_steps;
  bool latest_source_body_incomplete = true;
  bool latest_source_mesh_loop_present = false;
  bool latest_source_uses_uninitialized_angle = true;
  bool latest_source_publishes_transform_rows = false;
  bool rb2_dump_has_statement_body = false;
  bool gh2_rexglue_axis_setters_write_full_matrix = true;
  bool safe_to_publish_selected_axis_rows = false;
  bool safe_to_pose_meshes = false;
  bool safe_to_publish_mesh_transforms = false;
};

struct SourceCharServoBoneDefaultState {
  bool pelvis_null = true;
  bool facing_rot_delta_null = true;
  bool facing_pos_delta_null = true;
  bool facing_rot_null = true;
  bool facing_pos_null = true;
  bool move_self = false;
  bool delta_changed = false;
  bool regulate_empty = true;
};

struct SourceCharServoBoneSetNameStep {
  bool calls_hmx_object_set_name = true;
  bool reads_current_dir_after_set_name = true;
  bool assigns_character_owner = false;
};

struct SourceCharServoBoneSetClipTypeStep {
  bool changed = false;
  bool assign_clip_type = false;
  bool clear_bones = false;
  bool stuff_bones_from_dir = false;
};

struct SourceCharServoBoneEnterStep {
  bool zero_deltas = true;
  bool clear_regulate = true;
  bool delta_changed = false;
  bool move_self = false;
};

struct SourceCharServoBoneSetMoveSelfStep {
  bool changed = false;
  bool move_self = false;
  bool delta_changed = false;
};

struct SourceCharServoBoneReallocatePlan {
  bool calls_char_bones_meshes_reallocate = true;
  bool resets_facing_rot_delta = true;
  bool found_facing_pos_delta = false;
  bool lookup_facing_pos = false;
  bool lookup_pelvis = false;
  bool assert_facing_pos_and_pelvis = false;
  bool lookup_facing_rot = false;
  bool lookup_facing_rot_delta = false;
  std::vector<std::string> lookup_order;
};

struct SourceCharServoBoneCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  bool calls_set_clip_type = true;
};

struct SourceCharServoBoneLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> call_order;
  std::vector<std::string> branches;
};

struct SourceCharServoBoneSavePlan {
  int32_t save_id = 0x14a;
};

struct SourceCharServoBoneHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharServoBonePropSyncPlan {
  std::vector<std::string> set_properties;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharServoBoneRuntimeDumpEvidence {
  std::string poll_range;
  std::string regulate_override_range;
  std::string regulate_range;
  std::string poll_deps_range;
  std::vector<std::string> poll_locals;
  std::vector<std::string> regulate_override_locals;
  std::vector<std::string> regulate_locals;
  bool rb2_dump_has_statement_body = false;
  bool latest_source_has_poll_body = false;
  bool safe_to_run_poll = false;
  bool safe_to_run_regulate = false;
  bool safe_to_publish_servo_motion = false;
};

struct SourceCharBonesSamplesState {
  SourceCharBonesState bones;
  int num_samples = 0;
  int preview_sample = 0;
  int start_offset = 0;
  int raw_data_size = 0;
  std::vector<uint8_t> raw_data;
  std::vector<float> frames;
};

struct SourceCharBonesSampleStep {
  int start_offset = 0;
  float weight = 0.0f;
  std::string downstream_call;
};

struct SourceCharBonesSamplesLoadPlan {
  bool known_version = false;
  std::vector<std::string> read_order;
};

struct SourceGrimCharBonesSamplesHeaderPlan {
  bool known_version = false;
  int count_size = 0;
  bool defaults_weight = false;
  bool reads_weight = false;
  bool reads_frame_table = false;
  bool aligns_sample_data_to_4 = false;
  std::vector<std::string> read_order;
};

struct SourceGrimCharBonesSamplesDataPlan {
  bool known_version = false;
  int compression = 0;
  int sample_count = 0;
  size_t unaligned_sample_size = 0;
  size_t sample_size = 0;
  size_t total_sample_bytes = 0;
  bool aligns_sample_data_to_4 = false;
  std::vector<std::string> kept_channels;
  std::vector<std::string> ignored_channels;
  std::vector<size_t> channel_sizes;
};

struct SourceGrimCharBonesSamplesComputedSizes {
  bool valid = false;
  int compression = 0;
  std::array<uint32_t, kSourceCharBonesTypeEnd + 1> counts = {};
  std::array<uint32_t, kSourceCharBonesTypeEnd + 1> computed_sizes = {};
  uint32_t computed_flags = 0;
};

struct SourceGrimCharBonesSamplesDecodePlan {
  bool walks_serialized_bones = true;
  bool groups_by_mesh_name = true;
  bool stores_channel_weights = true;
  bool sorts_bone_samples_by_symbol = true;
  std::vector<int> decoded_types;
  std::vector<int> unsupported_types;
  std::vector<std::string> target_name_replacements;
};

struct SourceGrimCharBonesSamplesExportTranslationInput {
  std::array<float, 3> base_translation = {};
  float weight = 1.0f;
  std::vector<std::array<float, 3>> pos_samples;
};

struct SourceGrimCharBonesSamplesExportTranslationPlan {
  bool has_pos_samples = false;
  bool uses_default_translation_sample = false;
  bool uses_sample_index_times = true;
  bool multiplies_sample_index_by_fps = true;
  bool uses_frame_values = false;
  bool adds_base_translation_to_pos_samples = false;
  float sample_time_step = 1.0f / 30.0f;
  std::vector<float> input_times;
  std::vector<std::array<float, 3>> output_translations;
};

struct SourceGrimCharBonesSamplesExportRotationInput {
  std::array<float, 4> base_rotation_xyzw = {0.0f, 0.0f, 0.0f, 1.0f};
  float quat_weight = 1.0f;
  float rotz_weight = 1.0f;
  size_t pos_sample_count = 0;
  std::vector<std::array<float, 4>> quat_samples_xyzw;
  std::vector<float> rotz_samples;
};

struct SourceGrimCharBonesSamplesExportRotationPlan {
  bool has_rotation_samples = false;
  bool includes_pos_sample_count = true;
  bool initializes_from_node_rotation = true;
  bool quat_replaces_node_rotation = true;
  bool rotz_post_multiplies = true;
  bool rotz_uses_z_axis = true;
  bool rotz_angle_is_pi_scaled = true;
  bool uses_sample_index_times = true;
  bool multiplies_sample_index_by_fps = true;
  bool uses_frame_values = false;
  float sample_time_step = 1.0f / 30.0f;
  size_t sample_count = 0;
  std::vector<float> input_times;
  std::vector<std::array<float, 4>> output_rotations_xyzw;
};

struct SourceGrimCharClipLoadPlan {
  bool known_version = false;
  bool reads_object_meta = false;
  bool reads_range = false;
  bool skips_v5_unknown_bool = false;
  bool reads_relative = false;
  bool reads_unknown_1 = false;
  bool reads_do_not_decompress = false;
  bool reads_node_size = false;
  bool reads_deprecated_events = false;
  bool reads_events = false;
  std::vector<std::string> read_order;
};

struct SourceGrimCharClipSamplesLoadPlan {
  bool known_version = false;
  bool calls_char_clip_with_meta = false;
  bool reads_some_bool = false;
  bool legacy_split_headers_and_data = false;
  bool reads_duplicate_legacy_header = false;
  bool reads_extra_bones = false;
  int runtime_data_lists = 0;
  std::vector<std::string> read_order;
};

struct SourceGrimCharClipSamplesExtraBonesPlan {
  bool active = false;
  bool reads_count = false;
  bool reads_name = false;
  bool reads_weight = false;
  bool stores_runtime_rows = false;
  std::vector<std::string> read_order;
};

struct SourceReNotesCharBonesSamplesDecodePlan {
  bool sample_data_grouped_by_time = true;
  bool has_generic_rot_sample = true;
  bool active_reader_counts_pos = true;
  bool active_reader_counts_quat = true;
  bool active_reader_counts_rotz = true;
  bool active_reader_counts_rotx = false;
  bool active_reader_counts_roty = false;
  bool active_reader_counts_scale = false;
  std::vector<std::string> active_sample_order;
  std::vector<std::string> fenced_channels;
};

struct SourceProblemCharacterClipRawAxisAuditRow {
  std::string milo;
  int clips = 0;
  int accepted = 0;
  int frames = 0;
  int fenced_clips = 0;
  int raw_scale = 0;
  int raw_rotx = 0;
  int raw_roty = 0;
};

struct SourceProblemCharacterClipRawAxisAudit {
  std::string artifact;
  std::vector<SourceProblemCharacterClipRawAxisAuditRow> rows;
  bool all_rows_accepted = true;
  bool all_problem_rows_have_zero_fenced_raw = true;
  bool supports_publisher_gap_not_raw_axis_gap = true;
  std::vector<std::string> shared_animation_notes;
};

struct SourceCharBonesSamplesPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> custom_branches;
};

struct SourceCharBonesSamplesBodyBoundary {
  bool rb3_latest_load_delegates_header = true;
  bool rb3_latest_load_delegates_data = true;
  bool rb3_latest_declares_load_header = true;
  bool rb3_latest_declares_load_data = true;
  bool rb3_latest_declares_evaluate_channel = true;
  bool rb3_latest_exposes_load_header_body = false;
  bool rb3_latest_exposes_load_data_body = false;
  bool rb3_latest_exposes_evaluate_channel_body = false;
  bool rb2_dump_maps_load_header = true;
  bool rb2_dump_maps_load_data = true;
  bool rb2_dump_maps_evaluate_channel = true;
  bool rb2_dump_exposes_statement_body = false;
  bool safe_to_decode_logged_rows = true;
  bool safe_to_publish_pose = false;
  std::vector<std::string> fenced_bodies;
};

struct SourceCharBonesSamplesRuntimeDumpEvidence {
  std::string frac_to_sample_range;
  std::string evaluate_channel_range;
  std::string rotate_by_range;
  std::string rotate_to_range;
  std::string scale_add_sample_range;
  std::string relativize_range;
  std::string load_range;
  std::string read_counts_range;
  std::string load_header_range;
  std::string load_data_range;
  std::string sync_property_range;
  std::vector<std::string> frac_to_sample_locals;
  std::vector<std::string> evaluate_channel_locals;
  std::vector<std::string> relativize_locals;
  std::vector<std::string> load_header_locals;
  std::vector<std::string> load_data_locals;
  bool rb3_latest_declares_frac_to_sample = true;
  bool rb2_dump_maps_frac_to_sample = true;
  bool has_frac_to_sample_statement_body = false;
  bool has_load_header_statement_body = false;
  bool has_load_data_statement_body = false;
  bool has_evaluate_channel_statement_body = false;
  bool has_relativize_statement_body = false;
  bool safe_to_use_source_frac_to_sample = false;
  bool safe_to_decode_logged_rows = true;
  bool safe_to_publish_pose = false;
};

struct SourceCharClipSamplesRuntimeDumpEvidence {
  std::string facing_bones_set_range;
  std::string facing_set_scale_add_range;
  std::string frame_to_sample_range;
  std::string get_channel_range;
  std::string evaluate_channel_range;
  std::string evaluate_channel_sample_range;
  std::string rotate_by_range;
  std::string rotate_to_range;
  std::string scale_add_frame_range;
  std::string scale_add_sample_range;
  std::string relativize_range;
  std::string set_relative_range;
  std::string load_range;
  std::vector<std::string> facing_set_scale_add_locals;
  std::vector<std::string> evaluate_channel_locals;
  std::vector<std::string> evaluate_channel_sample_locals;
  std::vector<std::string> rotate_by_locals;
  std::vector<std::string> rotate_to_locals;
  std::vector<std::string> scale_add_frame_locals;
  std::vector<std::string> load_locals;
  bool has_evaluate_channel_statement_body = false;
  bool has_rotate_by_statement_body = false;
  bool has_scale_add_statement_body = false;
  bool has_load_statement_body = false;
  bool safe_to_publish_pose = false;
};

enum class SourceCharUtlObjectKind {
  kTransformable,
  kMesh,
  kCamera,
  kDirectory,
  kCharBone,
  kCharCollide,
  kCharCuff,
};

struct SourceCharUtlObject {
  std::string name;
  SourceCharUtlObjectKind kind = SourceCharUtlObjectKind::kTransformable;
  int mesh_bone_count = 0;
  std::string char_bone_transform;
};

struct SourceCharUtlBoneTransResult {
  std::string lookup_name;
  std::string resolved_name;
  bool via_char_bone = false;
};

struct SourceCharUtlMergeBone {
  std::string name;
  std::string target;
  int32_t position_context = 0;
  int32_t scale_context = 0;
  int32_t rotation_type = kSourceCharBonesTypeEnd;
  int32_t rotation_context = 0;
};

struct SourceCharUtlMergeWarning {
  std::string code;
  std::string bone_name;
  std::string source_name;
  std::string dest_name;
};

struct SourceCharUtlMergeResult {
  std::vector<SourceCharUtlMergeBone> dest_bones;
  std::vector<SourceCharUtlMergeWarning> warnings;
};

struct SourceCharUtlTransformRow {
  std::string name;
  bool has_parent = false;
};

struct SourceCharUtlClipPredictFrame {
  std::array<float, 3> facing_pos = {0.0f, 0.0f, 0.0f};
  float facing_rot = 0.0f;
};

// Virtual FacingBones output, not a transform in the resident skeleton.
// Recomputed from each driver's own beat/d_beat every poll; never accumulated
// by differencing two blended poses (which introduces crossfade root motion).
struct SourceCharFacingDelta {
  bool has_position = false;
  bool has_rotation = false;
  std::array<float, 3> position = {};
  float rotation = 0.0f;
};

struct SourceCharUtlClipPredictState {
  std::array<float, 3> pos = {0.0f, 0.0f, 0.0f};
  float ang = 0.0f;
  std::array<float, 3> last_pos = {0.0f, 0.0f, 0.0f};
  float last_ang = 0.0f;
};

struct CharClip;

struct SourceCharWalkScheduleEntry {
  const CharClip* clip = nullptr;
  float start_beat = 0.0f;
  float previous_end_beat = 0.0f;
};

struct SourceCharWalkPlanPoint {
  size_t clip_index = 0;
  float beat = 0.0f;
  float distance = 0.0f;
};

struct SourceCharWalkStopCandidate {
  const CharClip* clip = nullptr;
  uint32_t flags = 0;
};

struct SourceCharWalkMotionPlan {
  std::vector<SourceCharWalkScheduleEntry> schedule;
  std::vector<SourceCharWalkPlanPoint> points;
  std::vector<std::array<float, 3>> path;
  std::array<float, 3> end_position = {};
  size_t active_point_count = 0;
  size_t selected_point_index = 0;
  size_t selected_stop_index = 0;
  float beat_remainder = 0.0f;
  float score = 0.0f;
  bool valid = false;
};

struct SourceCharWalkForwardPrediction {
  SourceCharUtlClipPredictState state;
  size_t clip_index = 0;
  float beat = 0.0f;
};

struct SourceCharWalkOffsetRegulation {
  bool valid = false;
  bool point_advanced = false;
  size_t point_index = 0;
  float offset_speed = 0.0f;
  std::array<float, 3> position = {};
};

struct SourceCharUtlInitPlan {
  std::vector<std::string> registered_functions;
  std::vector<std::string> reset_hair_handler_steps;
  std::vector<std::string> char_merge_bones_handler_steps;
  bool char_merge_bones_deletes_loaded_dir = true;
};

struct SourceCharLookAtBounds {
  std::array<float, 3> min = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> max = {0.0f, 0.0f, 0.0f};
};

struct SourceCharLookAtLimitState {
  float min_yaw = -80.0f;
  float max_yaw = 80.0f;
  float min_pitch = -80.0f;
  float max_pitch = 80.0f;
  SourceCharLookAtBounds bounds;
};

struct SourceCharLookAtEnterState {
  std::array<float, 3> smoothed_dir = {1.0e29f, 0.0f, 0.0f};
  bool reset_pivot_local = false;
};

struct SourceCharLookAtPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharLookAtPollPlan {
  bool poll_gate_open = false;
  bool compute_dest_vector = false;
  bool apply_weight_yaw = false;
  bool skip_zero_weight = false;
  bool update_source_radius_history = false;
  bool clamp_source_radius_offset = false;
  bool write_pivot_world_to_source = false;
  bool normalize_dest_vector = false;
  bool transform_to_parent_space = false;
  bool clamp_bounds = false;
  bool smooth_half_time = false;
  bool use_test_range = false;
  bool use_show_range = false;
  bool apply_jitter = false;
  bool subtract_source_radius_offset = false;
  bool write_roll_local_rotation = false;
  bool write_no_roll_axes = false;
};

struct SourceCharLookAtYawWeightResult {
  bool applied = false;
  bool speed_limited = false;
  float dot_clamped = 0.0f;
  float target_yaw_weight = 1.0f;
  float updated_yaw_weight = 1.0f;
  float final_weight = 1.0f;
};

struct SourceCharLookAtNoRollAxesResult {
  std::array<float, 3> x = {1.0f, 0.0f, 0.0f};
  std::array<float, 3> y = {0.0f, 1.0f, 0.0f};
  std::array<float, 3> z = {0.0f, 0.0f, 1.0f};
  bool invalid_xx = false;
};

struct SourceCharLookAtSmoothResult {
  bool applied = false;
  float factor = 0.0f;
  std::array<float, 3> dir = {0.0f, 1.0f, 0.0f};
};

struct SourceCharLookAtRangeResult {
  bool applied = false;
  bool used_test_range = false;
  bool used_show_range = false;
  bool force_weight_one = false;
  int show_range_case = -1;
  std::array<float, 3> dir = {0.0f, 1.0f, 0.0f};
};

struct SourceCharLookAtSourceRadiusResult {
  bool active = false;
  bool updated_history = false;
  bool clamped_to_radius = false;
  float radius_radians = 0.0f;
  float pre_clamp_length_sq = 0.0f;
  std::array<float, 3> history = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> offset = {0.0f, 0.0f, 0.0f};
};

struct SourceCharLookAtLoadPlan {
  bool revision_supported = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool sync_limits = false;
};

struct SourceCharLookAtCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  bool sync_limits = false;
};

struct SourceCharLookAtHandlerPlan {
  std::vector<std::string> superclasses;
  int32_t check = 0;
};

struct SourceCharLookAtPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> set_actions;
  std::vector<std::string> superclasses;
};

struct SourceCharLookAtSavePlan {
  int32_t save_id = 0x178;
};

// One channel value for one frame.
struct ClipChannel {
  // GH2's runtime CharBones classifier has nine typed buckets. The public
  // later-engine CharBones header stops at kRotZ, but the GH2 XEX continues
  // with three post-compose delta axes before its type sentinel.
  enum Type {
    kPos,
    kScale,
    kQuat,
    kRotX,
    kRotY,
    kRotZ,
    kDeltaX,
    kDeltaY,
    kDeltaZ,
  } type = kPos;
  std::string bone_name;  // Grim-style target name, often "*.mesh"
  float source_weight = 1.0f;  // Serialized CharBone weight retained from source.
  float pos[3] = {};      // kPos: X,Y,Z
  float scale[3] = {1.0f, 1.0f, 1.0f};  // kScale: local X,Y,Z scale
  float quat[4] = {};     // kQuat: X,Y,Z,W
  // Scalar samples are radians after decompression (GH2 int16 * 0x3A200000).
  // PoseMeshes and the virtual bone_facing channels must not multiply by pi.
  float angle = 0.0f;
};

// All frames of one clip, indexed [frame][channel].
struct CharClip {
  struct BeatEvent {
    float beat = 0.0f;
    std::string event;
  };
  struct TransitionNode {
    float current_beat = 0.0f;
    float next_beat = 0.0f;
  };
  struct Transition {
    std::string clip;
    std::vector<TransitionNode> nodes;
  };

  std::string name;
  std::string source_milo_path;
  // Decoded once per loaded MILO; shared by all clips from that directory.
  // Null plus a nonempty error means an unsupported/unreadable profile,
  // NOT that the character has no facing channels.
  std::shared_ptr<const Gh2ClipSetBinding> gh2_binding;
  std::string gh2_binding_error;
  struct Gh2FacingSamples {
    // Keep the actual full/one channel counts. Expanded pose frames and the
    // nominal 30 fps are not GH2 EvaluateChannel's sampling domain.
    std::vector<std::array<float, 3>> positions;
    std::vector<float> rotations;
    // CharBonesSamples ctor 0x1935F0 initializes interpolation to true.
    bool interpolate_position = true;
    bool interpolate_rotation = true;
  };
  std::optional<Gh2FacingSamples> gh2_facing_samples;
  std::shared_ptr<const Gh2ClipPoseSamples> gh2_pose_samples;
  std::vector<std::vector<ClipChannel>> frames;  // frames[f][ch]
  struct RawChannelCounts {
    int pos = 0;
    int scale = 0;
    int quat = 0;
    int rotx = 0;
    int roty = 0;
    int rotz = 0;
    int dx = 0;
    int dy = 0;
    int dz = 0;
  };
  struct OutputBone {
    std::string name;    // CharBone entry name, normally bone_*.trans
    std::string parent;  // CharBone parent, normally another *.trans
    milo_scene::Xfm local;
    milo_scene::Xfm world_stored;
    uint32_t char_bone_version = 0;
    uint32_t trans_version = 0;
    uint32_t trans_constraint = 0;
    std::string trans_target;
    bool preserve_scale = false;
    int32_t position_context = 0;
    int32_t scale_context = 0;
    int32_t rotation_type = 6;  // ihatecompvir CharBones::TYPE_END.
    int32_t rotation_context = 0;
    int32_t legacy_pre_rev5_int = 0;
    bool has_legacy_pre_rev5_int = false;
    int32_t legacy_rev3_to_7_int = 0;
    bool has_legacy_rev3_to_7_int = false;
    std::string target;
    struct WeightContext {
      int32_t context = 0;
      float weight = 0.0f;
    };
    std::vector<WeightContext> weights;
    std::string trans;
    bool bake_out_as_top_level = false;
    size_t unread_bytes = 0;
  };
  // Animation MILOs carry CharBone output records beside CharClipSamples.
  // They declare the channel inventory and contexts; their serialized local
  // transforms are not the live pose base. GH2 CharBonesMeshes resolves the
  // target transform table and acquires the current target locals instead.
  std::vector<OutputBone> output_bones;
  // Raw header channel counts preserve the full GH2 nine-bucket inventory.
  RawChannelCounts raw_channel_counts;
  int fps = 30;        // authored clip playback rate
  float start_frame = 0.0f;
  float end_frame = 0.0f;
  float start_beat = 0.0f;
  float end_beat = 0.0f;
  float beats_per_second = 0.0f;
  uint32_t flags = 0;
  uint32_t default_play_flags = 0;
  float blend_width = 0.0f;
  float range = 0.0f;
  // GH2 CharClip revision 5 stores enter/exit script symbols before the
  // frame-event vector. These are executable animation metadata, not labels:
  // stock crowd clips use enter scripts to dispatch authored set_hand states.
  std::string legacy_enter_event;
  std::string legacy_exit_event;
  std::vector<Transition> transitions;
  std::vector<BeatEvent> beat_events;
  bool relative = false;
  bool loaded = false;

  float duration_seconds() const;
};

// Source-backed CharClipGroup load state. The public ihatecompvir source stores
// mClips, mWhich, and mFlags, and GetClip() advances mWhich in-place.
struct CharClipGroup {
  std::string name;
  std::string milo_path;
  std::vector<std::string> clips;
  uint32_t version = 0;
  int32_t which = 0;
  int32_t flags = 0;
  bool loaded = false;
};

// Metadata-only directory inventory used by CharDriver::FindClip(DataNode).
// It preserves serialized directory order and clip flags without retaining
// every decoded sample payload in memory.
struct CharClipCatalogEntry {
  std::string name;
  std::string milo_path;
  uint32_t flags = 0;
  float start_beat = 0.0f;
  float end_beat = 0.0f;
};

struct SourceCharClipGroupLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  bool read_flags = false;
  int32_t default_flags = 0;
};

struct SourceCharClipGroupHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharClipGroupPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharClipGroupSavePlan {
  int32_t save_id = 0x127;
};

struct SourceCharClipGroupDeleteRemainingPlan {
  int requested_remaining = 0;
  size_t visited_clip_count = 0;
  bool increments_local_clip_pointer = true;
  bool calls_lock_and_delete = false;
  bool mutates_group = false;
};

struct SourceCharClipRefOwner {
  bool is_clip_group = false;
  std::vector<std::string> group_clips;
};

struct ClipChannelLayer {
  std::vector<ClipChannel> channels;
  float weight = 1.0f;
  const std::vector<CharClip::OutputBone>* output_bones = nullptr;
  std::string debug_name;
  bool relative = false;
  bool overlay_override = false;
};

struct ClipChannelLayerStack {
  std::vector<ClipChannelLayer> layers;
  std::string debug_label;
  bool relative = false;
  bool relative_set = false;
};

enum CharPlayFlags : uint32_t {
  kCharPlayNoDefault = 0x00000000u,
  kCharPlayNow       = 0x00000001u,
  kCharPlayNoBlend   = 0x00000002u,
  kCharPlayFirst     = 0x00000003u,
  kCharPlayLast      = 0x00000004u,
  kCharPlayDirty     = 0x00000008u,
  kCharPlayNoLoop    = 0x00000010u,
  kCharPlayLoop      = 0x00000020u,
  kCharPlayGraphLoop = 0x00000030u,
  kCharPlayNodeLoop  = 0x00000040u,
  kCharPlayRealTime  = 0x00000200u,
  kCharPlayUserTime  = 0x00000400u,
};

// Lightweight viewer-side CharDriver play-node emulation. It owns clip time,
// loop/clamp behavior, and the previous-node blend that the game runtime uses
// when a new clip is started without kCharPlayNoBlend.
struct SourceCharServoTransition {
  const CharClip* outgoing_clip = nullptr;
  float outgoing_beat = 0.0f;
  float next_ramp_in = 0.0f;
};

class CharClipPlayer {
 public:
  struct CrossedEvent {
    const CharClip* clip = nullptr;
    size_t index = 0;
    float beat = 0.0f;
    std::string event;
  };

  void clear();
  void play(const CharClip& clip, uint32_t flags = kCharPlayLoop,
            float blend_width = -1.0f, float speed = 1.0f);
  void play_source(const CharClip& clip, uint32_t flags, float start_beat,
                   float delta_start, float blend_width = -1.0f,
                   float speed = 1.0f);
  void set_source_play_multiple_clips(bool play_multiple_clips);
  void set_source_realign(bool realign);
  void set_source_defer_node_loop_until_clip_end(bool defer_until_end);
  void set_source_starved_handler(std::function<void()> handler);
  void set_source_node_loop_resolver(
      std::function<const CharClip*()> resolver);
  void set_source_event_handler(
      std::function<void(const CharClip&, std::string_view)> handler);
  void set_speed(float speed);
  void seek_current_time_seconds(float time_seconds);
  void advance(float dt_seconds);
  // GH2 retail CharDriver::Poll passes the absolute task frame, task-frame
  // delta, and real-time delta separately to CharClipDriver::Evaluate.
  void advance_source(float frame, float dframe, float dt_seconds);
  void apply(Character& character, float weight = 1.0f) const;
  // Original GH2 CharDriver post-Advance buffer publication. Call after
  // advance_source; does not pose meshes or run the servo/controllers itself.
  void accumulate_source_pose(SourceCharBonesMeshesOutput& output, float weight = 1.0f);
  std::vector<ClipChannel> sampled_pose() const;
  SourceCharFacingDelta source_facing_delta(float weight = 1.0f) const;
  std::vector<ClipChannelLayer> sampled_pose_layers(
      float weight = 1.0f,
      bool overlay_override = false) const;
  bool sampled_pose_relative() const;
  float current_blend_weight() const;
  float evaluate_flags(uint32_t flags) const;
  bool source_starved() const;
  bool active() const { return !layers_.empty(); }
  const CharClip* current_clip() const;
  float current_time_seconds() const;
  const CharClip* source_first_playing_clip() const;
  // GH2 Regulate uses Last() and Before(Last()), regardless of blend weight.
  // This is the oldest pair, not current_clip()/the newest scheduled node.
  SourceCharServoTransition source_regulation_transition() const;
  uint32_t source_first_playing_flags() const;
  float source_first_playing_time_seconds() const;
  float source_first_playing_beat() const;
  const CharClip* source_most_playing_clip() const;
  float source_current_beat() const;
  float source_current_d_beat() const;
  float source_current_blend_fraction() const;
  size_t source_stack_depth() const { return layers_.size(); }
  const std::vector<CrossedEvent>& source_crossed_events() const {
    return crossed_events_;
  }
  std::vector<CrossedEvent> take_source_crossed_events();

 private:
  void play_internal(const CharClip& clip, uint32_t flags, float blend_width,
                     float speed, float start_beat, float delta_start);
  void poll_source_scheduler(float frame);
  void emit_source_event(const CharClip* clip, std::string_view event);
  void exit_source_layers(size_t begin, size_t end);

  struct Layer {
    const CharClip* clip = nullptr;
    uint32_t flags = 0;
    float time_seconds = 0.0f;
    float blend_width = 0.0f;
    float ramp_in = 0.0f;
    float beat = 0.0f;
    float d_beat = 0.0f;
    float blend_fraction = 1.0f;
    float advance_beat = 0.0f;
    float weight = 0.0f;
    float speed = 1.0f;
    int next_event = -1;
    float next_event_beat = -1.0e30f;
  };

  float source_frame_ = 0.0f;
  float source_old_beat_ = 1.0e30f;
  bool source_play_multiple_clips_ = false;
  bool source_realign_ = false;
  bool source_defer_node_loop_until_clip_end_ = false;
  std::function<void()> source_starved_handler_;
  std::function<const CharClip*()> source_node_loop_resolver_;
  std::function<void(const CharClip&, std::string_view)>
      source_event_handler_;
  std::vector<Layer> layers_;
  std::vector<CrossedEvent> crossed_events_;
};

// GH2 PS2 CharClipDriver::Evaluate/ScaleAdd use this cosine-eased node weight
// at 0x00198E10 and 0x00198EA4.
float source_gh2_char_clip_driver_eased_weight(float blend_fraction);

// GH2 PS2's global Random object is a 256-word table seeded with 666 at
// 0x002D9DA8. Its output path at 0x002D9BE8 supplies constructor range
// randomization without relying on a host-library RNG.
struct SourceGh2Ps2RandomState {
  uint32_t first = 0;
  uint32_t second = 103;
  std::array<uint32_t, 256> values = {};
};
SourceGh2Ps2RandomState source_gh2_ps2_random_construct(
    uint32_t seed = 666u);
uint32_t source_gh2_ps2_random_next(SourceGh2Ps2RandomState& state);
float source_gh2_ps2_random_unit(SourceGh2Ps2RandomState& state);
float source_gh2_ps2_random_range(SourceGh2Ps2RandomState& state,
                                  float minimum, float maximum);
float source_gh2_char_clip_driver_randomized_beat(
    const CharClip& clip, float beat, float random_offset);
bool source_gh2_ps2_char_driver_poll_starved(bool has_first,
                                             bool first_has_next);
float source_gh2_ps2_char_driver_play_if_safe_length(
    float requested_length, bool has_first, float first_end_beat,
    float first_beat);
bool source_gh2_ps2_char_driver_play_if_safe_candidate(
    uint32_t clip_flags, float clip_start_beat, float clip_end_beat,
    uint32_t safe_flags, float adjusted_length);

struct ClipPlayerLayerSource {
  const CharClipPlayer* player = nullptr;
  float weight = 1.0f;
  bool overlay_override = false;
};

struct ClipFrameLayerSource {
  const CharClip* clip = nullptr;
  int frame_idx = 0;
  float weight = 1.0f;
  bool overlay_override = false;
};

struct CharacterPosePlayerLayerSources {
  const CharClipPlayer* main = nullptr;
  const CharClipPlayer* face_base = nullptr;
  const CharClipPlayer* strum = nullptr;
  const CharClipPlayer* fret = nullptr;
  std::vector<const CharClipPlayer*> fret_extras;
  const CharClipPlayer* face = nullptr;
  float strum_weight = 1.0f;
  float fret_weight = 1.0f;
};

struct SourceCharMainDriverHandWeights;

struct CharacterPosePlayerLayerBuildSources {
  const CharClipPlayer* main = nullptr;
  const CharClipPlayer* face_base = nullptr;
  const CharClipPlayer* strum = nullptr;
  const CharClipPlayer* fret = nullptr;
  std::vector<const CharClipPlayer*> fret_extras;
  const CharClipPlayer* face = nullptr;
  const SourceCharMainDriverHandWeights* hand_weights = nullptr;
  bool hand_driver_active = true;
};

struct CharacterPoseFrameLayerSources {
  const CharClip* main = nullptr;
  const CharClip* face_base = nullptr;
  const CharClip* strum = nullptr;
  const CharClip* fret = nullptr;
  const CharClip* face = nullptr;
  int frame_idx = 0;
  float strum_weight = 1.0f;
  float fret_weight = 1.0f;
};

struct CharacterPoseFrameLayerBuildSources {
  const CharClip* main = nullptr;
  const CharClip* face_base = nullptr;
  const CharClip* strum = nullptr;
  const CharClip* fret = nullptr;
  const CharClip* face = nullptr;
  const SourceCharMainDriverHandWeights* hand_weights = nullptr;
  int frame_idx = 0;
  bool hand_driver_active = true;
};

struct CharacterRuntimeIkWeight {
  std::string weight_prop;
  float weight = 0.0f;
};

struct CharacterPoseControllerFrameSources {
  const ClipChannelLayerStack* pose_stack = nullptr;
  const SourceCharMainDriverHandWeights* driver_weights = nullptr;
  std::vector<CharacterRuntimeIkWeight> fallback_ik_weights;
  std::string midi_fret_target;
  float time_seconds = 0.0f;
  float current_beat = 0.0f;
  float delta_beat = 0.0f;
  float midi_fret_target_beat = 0.0f;
  float midi_fret_event_beat = 0.0f;
  bool controllers_enabled = true;
  bool midi_fret_target_enabled = false;
};

struct CharacterPoseStackFrameResult {
  bool applied_clip_layers = false;
  size_t applied_layer_count = 0;
  bool source_pose_publisher_active = false;
  // Retained for callers that previously surfaced the source gap. It remains
  // false now that the GH2 XEX acquire/mix/commit path is implemented.
  bool source_pose_publisher_fenced = false;
};

struct CharacterPoseControllerFrameResult {
  bool applied_clip_layers = false;
  size_t applied_layer_count = 0;
  bool source_pose_publisher_active = false;
  bool source_pose_publisher_fenced = false;
  bool fed_driver_flags = false;
  size_t fallback_ik_weights = 0;
  bool applied_midi_fret_target = false;
  bool applied_controllers = false;
};

bool append_clip_player_layer(ClipChannelLayerStack& stack,
                              const CharClipPlayer& player,
                              float weight = 1.0f,
                              bool overlay_override = false);
bool append_clip_player_layers(
    ClipChannelLayerStack& stack,
    const std::vector<ClipPlayerLayerSource>& sources);
bool append_clip_frame_layer(ClipChannelLayerStack& stack,
                             const CharClip& clip, int frame_idx,
                             float weight = 1.0f,
                             bool overlay_override = false);
bool append_clip_frame_layers(ClipChannelLayerStack& stack,
                              const std::vector<ClipFrameLayerSource>& sources);
CharacterPosePlayerLayerSources make_character_pose_player_layer_sources(
    const CharacterPosePlayerLayerBuildSources& sources);
bool append_character_pose_player_layers(
    ClipChannelLayerStack& stack,
    const CharacterPosePlayerLayerSources& sources);
CharacterPoseFrameLayerSources make_character_pose_frame_layer_sources(
    const CharacterPoseFrameLayerBuildSources& sources);
bool append_character_pose_frame_layers(
    ClipChannelLayerStack& stack,
    const CharacterPoseFrameLayerSources& sources);
void apply_clip_layer_stack(const ClipChannelLayerStack& stack,
                            Character& character);
CharacterPoseStackFrameResult apply_character_pose_stack_frame(
    Character& character,
    const ClipChannelLayerStack* stack);
CharacterPoseControllerFrameResult apply_character_pose_controller_frame(
    Character& character,
    const CharacterPoseControllerFrameSources& sources);

// Strict directory-level GH2 allocation metadata; throws on decode failure.
std::shared_ptr<const Gh2ClipSetBinding> load_gh2_clip_set_binding(
    const std::string& hdr_path, const std::string& ark_path,
    const std::string& milo_path);

// Load all frames of a named CharClipSamples entry from the PS2 ARK.
// Returns a CharClip with frames.empty() on failure.
CharClip load_clip(const std::string& hdr_path,
                   const std::string& ark_path,
                   const std::string& milo_path,
                   const std::string& clip_name);

// GH1 stores AnimClipSamples as standalone .acp entries: class/name strings
// followed by the same serialized sample body used inside later MILOs.
CharClip load_acp_clip(const std::string& hdr_path,
                       const std::string& ark_path,
                       const std::string& acp_path);

// Source-backed CharClipGroup::Load reader. Returns the group's serialized
// ObjPtr clip names and source mWhich/mFlags state from the first matching
// animation MILO.
CharClipGroup load_clip_group(
    const std::string& hdr_path, const std::string& ark_path,
    const std::vector<std::string>& milo_paths,
    const std::string& group_name);

std::vector<CharClipCatalogEntry> load_clip_catalog(
    const std::string& hdr_path, const std::string& ark_path,
    const std::vector<std::string>& milo_paths);

std::vector<CharClipGroup> load_clip_group_catalog(
    const std::string& hdr_path, const std::string& ark_path,
    const std::vector<std::string>& milo_paths);

// Source-backed CharClipGroup::GetClip index step. Mutates group.which.
std::optional<size_t> char_clip_group_get_clip_index(CharClipGroup& group);

std::optional<CharClip::TransitionNode>
source_char_clip_find_first_transition_node(
    const CharClip& clip,
    std::string_view next_clip,
    float current_beat);

std::optional<CharClip::TransitionNode>
source_char_clip_find_last_transition_node(
    const CharClip& clip,
    std::string_view next_clip,
    float current_beat);

// GH2 retail CharClip::FindNode at 0x00196888. Modes 3 and 4 search the
// authored transition graph first; when permitted and no row matches, the
// returned node is synthesized from clip timing and beat-align flags.
std::optional<CharClip::TransitionNode>
source_char_clip_find_transition_node(
    const CharClip& current_clip,
    const CharClip& next_clip,
    float current_beat,
    int mode);

// Source-backed CharClipGroup::NumFlagDuplicates helper. `clip_index` selects
// the source clip row whose flags are compared against every other row.
int source_char_clip_group_num_flag_duplicates(
    const std::vector<uint32_t>& clip_flags,
    size_t clip_index,
    uint32_t mask);
std::vector<std::string> source_char_clip_group_sorted_names(
    std::vector<std::string> clip_names);
std::vector<std::string> source_char_clip_group_add_clip(
    std::vector<std::string> clip_names,
    const std::string& clip_name);
std::vector<std::string> source_char_clip_group_remove_clip(
    std::vector<std::string> clip_names,
    const std::string& clip_name);
SourceCharClipGroupLoadPlan source_char_clip_group_load_plan(int revision);
SourceCharClipGroupHandlerPlan source_char_clip_group_handler_plan();
SourceCharClipGroupPropSyncPlan source_char_clip_group_prop_sync_plan();
SourceCharClipGroupSavePlan source_char_clip_group_save_plan();
SourceCharClipGroupDeleteRemainingPlan
source_char_clip_group_delete_remaining_plan(size_t clip_count,
                                             int requested_remaining);

struct SourceCharClipDriverState {
  uint32_t play_flags = 0;
  float blend_width = 0.0f;
  float time_scale = 1.0f;
  float d_beat = 0.0f;
  float advance_beat = 0.0f;
  bool has_clip = false;
  bool has_next = false;
  int next_event = -1;
  bool play_multiple_clips = false;
};

struct SourceCharClipDriverExitDecision {
  bool recurse_next = false;
  bool execute_exit_event = false;
  bool end_sync_anim = false;
  bool delete_self = false;
  std::optional<size_t> returned_stack_head;
  std::vector<size_t> deleted_indices;
};

struct SourceCharClipDriverDeleteClipResult {
  std::optional<size_t> deleted_index;
  std::vector<size_t> remaining_indices;
};

struct SourceCharClipDriverRuntimeDumpEvidence {
  std::string copy_ctor_range;
  std::string destructor_range;
  std::string exit_range;
  std::string delete_stack_range;
  std::string delete_clip_range;
  std::string evaluate_range;
  std::string scale_add_range;
  std::string rotate_to_range;
  std::string align_to_frame_range;
  std::string play_events_range;
  std::string execute_event_range;
  std::vector<std::string> copy_ctor_references;
  std::vector<std::string> destructor_references;
  std::vector<std::string> exit_locals;
  std::vector<std::string> exit_references;
  std::vector<std::string> evaluate_locals;
  std::vector<std::string> evaluate_references;
  std::vector<std::string> scale_add_locals;
  std::vector<std::string> scale_add_references;
  std::vector<std::string> rotate_to_locals;
  std::vector<std::string> rotate_to_references;
  std::vector<std::string> align_to_frame_locals;
  std::vector<std::string> align_to_frame_references;
  std::vector<std::string> play_events_locals;
  std::vector<std::string> play_events_references;
  std::vector<std::string> execute_event_references;
  bool has_evaluate_statement_body = false;
  bool has_scale_add_statement_body = false;
  bool has_rotate_to_statement_body = false;
  bool safe_to_import_runtime = false;
  std::string gh2_ps2_constructor_range;
  std::string gh2_ps2_copy_constructor_range;
  std::string gh2_ps2_destructor_range;
  std::string gh2_ps2_evaluate_range;
  std::string gh2_ps2_scale_add_range;
  std::string gh2_ps2_align_to_frame_range;
  std::string gh2_ps2_advance_event_range;
  std::vector<std::string> gh2_ps2_layout;
  bool gh2_ps2_evaluate_recovered = false;
  bool gh2_ps2_scale_add_recovered = false;
  bool gh2_ps2_align_to_frame_recovered = false;
};

// Source-backed CharClipDriver constructor play-flag masking.
uint32_t source_char_clip_driver_masked_play_flags(uint32_t clip_play_flags,
                                                   uint32_t mask);
uint32_t char_clip_driver_masked_play_flags(const CharClip& clip,
                                            uint32_t mask);
float source_char_driver_evaluate_flags_from_clip_flags(uint32_t clip_flags,
                                                        uint32_t flags);
SourceCharClipDriverState source_char_clip_driver_construct(
    uint32_t clip_play_flags,
    bool has_clip,
    bool has_next,
    uint32_t mask,
    float blend_width,
    bool play_multiple_clips);
std::vector<size_t> source_char_clip_driver_delete_stack_order(
    size_t stack_size);
SourceCharClipDriverExitDecision source_char_clip_driver_exit_decision(
    size_t stack_size,
    bool exit_next,
    bool has_sync_anim);
SourceCharClipDriverDeleteClipResult source_char_clip_driver_delete_clip_result(
    const std::vector<bool>& clip_matches_source_order);
bool source_char_clip_driver_should_execute_event(bool symbol_null,
                                                  bool clip_has_type_def);
SourceCharClipDriverRuntimeDumpEvidence
source_char_clip_driver_runtime_dump_evidence();

// Source-backed CharClip::BeatAlignString helper.
const char* source_char_clip_beat_align_string(uint32_t mask);

struct SourceCharClipFlagUpdate {
  uint32_t value = 0;
  bool dirty = false;
  bool changed = false;
};

struct SourceCharClipDefaultState {
  float frames_per_sec = 30.0f;
  uint32_t flags = 0;
  uint32_t play_flags = 0;
  float range = 0.0f;
  bool dirty = true;
  bool do_not_compress = false;
  int unk42 = -1;
  size_t beat_track_count = 1;
  float first_beat_frame = 0.0f;
  float first_beat_value = 0.0f;
};

struct SourceCharClipNumFramesPlan {
  int full_num_samples = 0;
  int full_frame_count = 0;
  int one_num_samples = 0;
  int num_frames = 1;
  bool clamps_minimum_to_one = true;
  bool uses_full_num_samples = true;
  bool uses_full_frame_count = true;
  bool ignores_one_num_samples = true;
};

struct SourceCharClipTimingBodyBoundary {
  bool constructor_sets_frames_per_sec_30 = true;
  bool inline_start_end_length_beats_present = true;
  bool prop_sync_exposes_length_seconds = true;
  bool prop_sync_exposes_average_beats_per_sec = true;
  bool length_seconds_body_visible = false;
  bool average_beats_per_second_body_visible = false;
  bool safe_to_import_seconds_math = false;
  std::vector<std::string> source_authorities;
  std::vector<std::string> fenced_bodies;
};

struct SourceCharClipBeatEvent {
  std::string event;
  float beat = 0.0f;
};

struct SourceCharClipResourceLookup {
  bool has_type_def = false;
  bool has_resource_array = false;
  std::string resource_name;
  bool found_resource = false;
  bool warn_no_resource = false;
};

struct SourceCharClipContextLookup {
  bool has_type_def = false;
  bool has_resource_array = false;
  std::string macro_name;
  int context = 0;
  bool reads_macro = false;
};

struct SourceCharClipTransitionsState {
  bool has_owner = false;
  std::vector<int> node_sizes;
};

struct SourceCharClipTransitionsClearResult {
  size_t released_clips = 0;
  bool resized_zero = false;
};

struct SourceCharClipTransitionsDumpEvidence {
  std::string remove_nodes_range;
  std::string resize_nodes_range;
  std::string add_node_range;
  bool has_remove_nodes_locals = true;
  bool has_resize_nodes_locals = true;
  bool has_add_node_locals = true;
  bool has_statement_bodies = false;
};

struct SourceCharClipRuntimeDumpEvidence {
  std::string find_nodes_range;
  std::string find_first_node_range;
  std::string find_last_node_range;
  std::string find_node_range;
  std::string replace_range;
  std::string clear_all_nodes_range;
  std::string load_range;
  std::string set_default_blend_range;
  std::string set_default_loop_range;
  std::string set_beat_align_mode_range;
  std::string in_groups_range;
  std::string make_mru_range;
  std::string lock_and_delete_range;
  std::string handle_range;
  std::string on_groups_range;
  std::string check_stick_range;
  std::string sync_property_range;
  std::vector<std::string> find_nodes_locals;
  std::vector<std::string> find_first_node_locals;
  std::vector<std::string> find_last_node_locals;
  std::vector<std::string> find_node_locals;
  std::vector<std::string> load_locals;
  std::vector<std::string> default_flag_setter_locals;
  std::vector<std::string> in_groups_locals;
  std::vector<std::string> make_mru_locals;
  std::vector<std::string> lock_and_delete_locals;
  std::vector<std::string> on_groups_locals;
  std::vector<std::string> check_stick_locals;
  bool has_load_statement_body = false;
  bool has_default_flag_setter_statement_bodies = false;
  bool has_group_helper_statement_bodies = false;
  bool has_check_stick_statement_body = false;
  bool has_sync_property_statement_body = false;
  bool safe_to_import_load = false;
  bool safe_to_import_default_flag_setters = false;
  bool safe_to_import_group_helpers = false;
  bool safe_to_import_check_stick = false;
  bool safe_to_import_sync_property = false;
};

struct SourceCharClipPoseMeshesSteps {
  std::string temp_meshes_name;
  std::vector<std::string> call_order;
  bool stuff_bones = false;
  std::string scale_down_target;
  bool scale_down = false;
  float scale_down_weight = 0.0f;
  std::string scale_add_target;
  bool scale_add = false;
  float scale_add_weight = 0.0f;
  float scale_add_frame = 0.0f;
  float scale_add_blend = 0.0f;
  std::string pose_meshes_target;
  bool pose_meshes = false;
};

struct SourceReleasePosePublisherBoundary {
  bool zero_weight_hand_ik_does_not_explain_pose = true;
  bool full_output_graph_changes_pose = true;
  bool safe_to_blame_ik_or_twist = false;
  bool safe_to_promote_full_output_graph = false;
  std::string remaining_source_gap;
  std::vector<std::string> source_evidence;
  std::vector<std::string> rejected_shortcuts;
};

struct SourceCharPosePublisherSourceRefresh {
  std::string rb3_commit;
  std::string gltf_milo_commit;
  std::string grim_commit;
  std::string re_notes_commit;
  std::string rb3_remote_ref;
  std::string rb3_remote_commit;
  std::string gltf_milo_remote_ref;
  std::string gltf_milo_remote_commit;
  std::string grim_remote_ref;
  std::string grim_remote_commit;
  std::string re_notes_remote_ref;
  std::string re_notes_remote_commit;
  bool rb3_after_fetch = false;
  bool gltf_milo_after_fetch = false;
  bool grim_after_fetch = false;
  bool re_notes_after_fetch = false;
  bool gltf_milo_hair_segment_source_present = false;
  bool non_rb3_pose_publisher_bodies_present = false;
  bool char_clip_pose_meshes_body = false;
  bool char_bones_samples_scale_add_sample_body = false;
  bool char_bones_scale_add_body = false;
  bool char_bones_samples_evaluate_channel_body = false;
  bool char_bones_meshes_pose_meshes_statement_body = false;
  bool char_bones_meshes_latest_pose_meshes_stub_only = false;
  bool rb2_dump_is_range_local_map = false;
  bool rb2_char_bones_scale_add_delegate_stub_empty = false;
  bool rb2_char_clip_samples_scale_add_sample_writer_empty = false;
  std::vector<std::string> still_fenced;
};

struct SourceCharClipPropSyncPlan {
  std::vector<std::string> graph_node_properties;
  bool node_vector_size_query = true;
  std::vector<std::string> node_vector_properties;
  std::vector<std::string> beat_event_set_properties;
  std::vector<std::string> clip_set_properties;
  std::vector<std::string> clip_properties;
  std::vector<std::string> sample_subobjects;
};

enum SourceCharDriverApplyMode {
  kSourceCharDriverApplyBlend = 0,
  kSourceCharDriverApplyAdd = 1,
  kSourceCharDriverApplyRotateTo = 2,
  kSourceCharDriverApplyBlendWeights = 3,
};

struct SourceCharDriverState {
  bool has_bones = false;
  bool has_clips = false;
  bool has_first = false;
  bool has_test_clip = false;
  bool has_default_clip = false;
  bool default_play_starved = false;
  std::string starved_handler;
  bool last_node_valid = false;
  float old_beat = 1.0e30f;
  bool realign = false;
  float beat_scale = 1.0f;
  float blend_width = 1.0f;
  std::string clip_type;
  SourceCharDriverApplyMode apply = kSourceCharDriverApplyBlend;
  bool has_internal_bones = false;
  bool play_multiple_clips = false;
};

struct SourceCharDriverDestructorPlan {
  bool delete_stack_when_first = true;
  bool delete_internal_bones = true;
  bool calls_clear = false;
  bool clears_first_pointer = false;
  bool clears_internal_bones_pointer = false;
};

struct SourceCharDriverExitPlan {
  bool call_rnd_pollable_exit = true;
  bool clear_stack = false;
  bool reset_last_node = false;
  bool evaluate_starved = false;
};

struct SourceCharDriverHighlightDecision {
  bool global_y_is_sentinel = false;
  bool defer_highlight = false;
  bool call_display = false;
  bool write_global_y_from_display = false;
  float display_input = 0.0f;
};

struct SourceCharDriverTransferPlan {
  bool clear_stack = true;
  bool create_first_driver_copy = false;
  std::vector<std::string> copied_members;
  std::vector<std::string> preserved_members;
};

struct SourceCharDriverSyncDecision {
  bool changed = false;
  bool clear_stack = false;
  bool reset_last_node = false;
  bool delete_internal_bones = false;
  bool allocate_internal_bones = false;
  bool clear_internal_bones = false;
  bool stuff_internal_bones = false;
  bool has_internal_bones = false;
};

struct SourceCharDriverEnterDecision {
  bool changed = false;
  bool clear_stack = false;
  bool reset_last_node = false;
  bool reset_old_beat = false;
  bool reset_beat_scale = false;
  bool play_default_clip = false;
  int default_play_flags = 1;
  float default_requested_blend_width = -1.0f;
  float default_old_beat = 1.0e30f;
  float default_start = 0.0f;
};

struct SourceCharDriverPlayDecision {
  bool found_clip = false;
  bool notify_missing_clip = false;
  bool set_last_node = false;
  bool duplicate_clip = false;
  bool create_clip_driver = false;
  bool new_stack_head = false;
  int play_flags = 0;
  float resolved_blend_width = 0.0f;
  float old_beat = 0.0f;
  float start = 0.0f;
  bool play_multiple_clips = false;
};

struct SourceCharDriverPlayNodeDecision {
  bool copied_requested_node = true;
  bool find_clip_warn = true;
  SourceCharDriverPlayDecision clip_play;
  bool final_last_node_from_request = false;
  bool returned_driver = false;
};

struct SourceCharDriverPlayGroupDecision {
  bool has_clip_dir = false;
  bool found_group = false;
  bool warn_no_clips = false;
  bool warn_missing_group = false;
  bool call_group_get_clip = false;
  bool request_play = false;
};

struct SourceCharDriverRuntimeDumpEvidence {
  std::string play_if_safe_range;
  std::string set_beat_scale_range;
  std::string evaluate_flags_range;
  std::string last_range;
  std::string before_range;
  std::string most_playing_range;
  std::string pre_load_range;
  std::string post_load_range;
  std::vector<std::string> play_if_safe_locals;
  std::vector<std::string> play_if_safe_references;
  std::vector<std::string> set_beat_scale_locals;
  std::vector<std::string> set_beat_scale_references;
  std::vector<std::string> evaluate_flags_locals;
  std::vector<std::string> evaluate_flags_references;
  std::vector<std::string> last_locals;
  std::vector<std::string> before_locals;
  std::vector<std::string> most_playing_locals;
  std::vector<std::string> most_playing_references;
  std::vector<std::string> pre_load_locals;
  std::vector<std::string> pre_load_references;
  std::vector<std::string> post_load_references;
  std::vector<std::string> header_declarations_without_checked_bodies;
  std::string gh2_ps2_poll_range;
  std::string gh2_ps2_poll_starved_range;
  std::vector<std::string> gh2_ps2_layout;
  bool rb3_latest_has_poll_body = false;
  bool rb2_dump_has_poll_range = false;
  bool has_evaluate_flags_statement_body = false;
  bool has_set_beat_scale_statement_body = false;
  bool safe_to_find_clip = false;
  bool safe_to_display = false;
  bool safe_to_evaluate_flags = false;
  bool safe_to_import_poll = false;
  bool gh2_ps2_poll_recovered = false;
};

struct SourceCharDriverPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharDriverMidiState {
  bool unk89 = false;
  std::string parser;
  std::string flag_parser;
  int clip_flags = 0;
  float blend_override_pct = 1.0f;
  bool has_default_clip = false;
};

struct SourceCharDriverMidiEnterDecision {
  SourceCharDriverEnterDecision driver_enter;
  bool set_unk89 = false;
  bool add_parser_sink = false;
  bool add_flag_parser_sink = false;
};

struct SourceCharDriverMidiExitDecision {
  bool call_driver_exit = false;
  bool remove_parser_sink = false;
  bool remove_flag_parser_sink = false;
};

struct SourceCharDriverMidiPollPlan {
  bool call_driver_poll = false;
  bool call_driver_poll_deps = false;
};

struct SourceCharDriverMidiParserDecision {
  bool used_default_clip = false;
  bool call_group_get_clip = false;
  int group_clip_flags = 0;
  bool request_play = false;
  int play_flags = 0;
  float requested_blend_width = 0.0f;
  float old_beat = 0.0f;
  float start = 0.0f;
  float assigned_blend_width = 0.0f;
};

struct SourceCharDriverMidiLoadPlan {
  bool known_revision = false;
  int32_t max_revision = 7;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharDriverMidiHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int32_t check = 0x99;
};

struct SourceCharDriverMidiPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharDriverMidiCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  std::vector<std::string> not_in_source_copy_members;
};

struct SourceCharDriverMidiSavePlan {
  int32_t save_id = 0x58;
};

struct SourceCharClipSetState {
  std::string char_file_root;
  bool has_preview_char = false;
  bool has_preview_clip = false;
  bool has_still_clip = false;
  int filter_flags = 0;
  int bpm = 90;
  bool preview_walk = false;
  bool rate_is_1_fpb = true;
};

struct SourceCharClipSetGroupStep {
  std::string group;
  bool randomize = false;
  bool sort = false;
};

struct SourceCharClipSetResetEditorResult {
  bool reset_preview_state = false;
  bool object_dir_reset_editor_state = false;
};

struct SourceCharClipSetPreSaveResult {
  bool preview_char_name_cleared = false;
  bool reset_preview_state = false;
  bool reset_editor_state = false;
};

struct SourceCharClipSetPostSaveResult {
  bool object_dir_post_save = false;
  bool preview_char_name_restored = false;
  bool preview_char_entered = false;
  bool sent_update_objects = false;
};

struct SourceCharClipSetPreLoadPlan {
  int32_t max_revision = 0x18;
  bool require_revision_gt_3 = true;
  bool push_packed_revision = true;
  bool object_dir_pre_load = true;
};

struct SourceCharClipSetLoadPlan {
  bool object_dir_load = true;
};

struct SourceCharClipSetPostLoadPlan {
  bool object_dir_post_load = true;
  bool returned_for_proxy = false;
  bool read_two_legacy_ints = false;
  bool read_rev_15_16_int = false;
  bool read_legacy_graph_path = false;
  bool read_legacy_reexport_string = false;
  bool read_rev_lt7_int = false;
  int32_t read_legacy_clip_triplets = 0;
  bool read_old_flag_bool = false;
  bool read_old_flag_second_bool = false;
  bool read_symbol_count = false;
  bool read_legacy_string_lists = false;
  bool read_legacy_symbol_and_int = false;
  bool read_rev_11_bool = false;
  bool warn_transition_bug = false;
  bool handle_filter_clips = false;
  bool read_char_file_path = false;
  bool read_preview_clip = false;
  bool read_filter_flags = false;
  bool read_bpm = false;
  bool read_preview_walk = false;
  bool read_still_clip = false;
};

struct SourceCharClipSetLoadCharacterResult {
  bool asserted_edit_mode = false;
  bool deleted_preview_char = false;
  bool loaded_objects = false;
  bool loaded_rnd_dir = false;
  bool selected_nested_character = false;
  bool preview_char_entered = false;
  bool preview_char_named = false;
  bool sent_update_objects = false;
};

struct SourceCharClipSetSetBpmResult {
  bool set_milo_property = false;
  int bpm = 90;
};

struct SourceCharClipSetCopyResult {
  bool copy_object_dir = false;
  bool copy_char_file_path = false;
  bool copy_preview_clip = false;
  bool copy_filter_flags = false;
  bool copy_bpm = false;
  bool copy_preview_walk = false;
  bool copy_still_clip = false;
};

struct SourceCharClipSetHandlerPlan {
  std::vector<std::string> action_handlers;
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  std::string check;
};

struct SourceCharClipSetSavePlan {
  int32_t save_id = 0x8E;
};

struct SourceCharClipDisplayGlobals {
  std::string dir;
  float em = 0.0f;
};

struct SourceCharClipDisplayState {
  std::string clip;
  std::string text;
  float text_width_plus_em = 0.0f;
  float start_beat = 0.0f;
  float end_beat = 0.0f;
  bool start_end_called = false;
  bool start_end_flag = false;
};

struct SourceCharClipDisplayMsgSource {
  std::string source;
  std::vector<std::string> sinks;
};

struct SourceCharClipDisplayFindSourceResult {
  bool found = false;
  std::string source;
};

struct SourceCharTaskMgrState {
  bool show_graph = false;
  bool registered_toggle_char_task_graph = false;
};

struct SourceClipGraphGeneratePairStep {
  bool remove_existing_nodes = true;
  bool captures_type_def = true;
  bool return_null_before_script = false;
  bool execute_on_transition = false;
  bool set_data_variables = false;
  bool stores_clip_pair = false;
  bool clears_dmap_before_script = false;
  bool clears_dmap_after_script = false;
  bool returns_dmap = false;
  bool set_nodes = false;
  std::string reason;
};

struct SourceClipGraphTransitionInputs {
  uint32_t clip_a_play_flags = 0;
  uint32_t clip_b_play_flags = 0;
  float max_error = 1.0e30f;
  float beat_align = 0.0f;
  float blend_width = 1.0f;
  float max_facing_degrees = 0.0f;
  float max_dist = 0.0f;
  float end_dist = 0.0f;
  bool has_restrict = false;
  bool has_bone_weights = false;
};

struct SourceClipGraphTransitionPlan {
  int clip_a_flag = 0;
  int clip_b_flag = 0;
  int min_flag = 0;
  float beat_align = 0.0f;
  float blend_width = 1.0f;
  int dist_map_sample_stride = 3;
  bool has_restrict = false;
  bool has_bone_weights = false;
  float find_dists_max_facing_radians = 0.0f;
  float find_nodes_max_error = 0.0f;
  float find_nodes_max_dist = 0.0f;
  float find_nodes_end_dist = 0.0f;
};

struct SourceClipCollideState {
  std::string char_path;
  std::string position = "front";
  bool clip_null = true;
  bool world_lines = false;
  bool move_camera = true;
  std::string mode;
};

struct SourceClipCollideLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  bool clears_clip = false;
};

struct SourceClipCollideSyncCharStep {
  bool set_proxy_file = false;
  bool sync_waypoint = true;
};

struct SourceClipCollideSetTypeDefStep {
  bool call_object_set_type_def = false;
  bool update_mode = false;
  bool assert_modes_array = false;
};

struct SourceClipCollideValidationStep {
  bool send_message = false;
  bool valid = true;
  std::string message;
};

struct SourceClipCollideDemonstrateStep {
  bool sync_waypoint = false;
  bool play_clip = false;
  int play_mode = 2;
  float play_start = -1.0f;
  float play_end = 1.0e30f;
  float play_blend = 0.0f;
};

struct SourceClipCollideClearReportStep {
  bool reset_graph = true;
  bool clear_reports = true;
  bool clear_report_string = true;
  bool sync_mode = true;
};

struct SourceClipCollideSyncModeStep {
  bool send_set_mode = false;
  std::string message = "set_mode";
};

struct SourceClipCollideListPlan {
  size_t source_array_size = 0;
  bool writes_null_first = true;
  size_t first_item_index = 1;
  std::vector<std::string> items;
};

struct SourceClipCollideTestClipsPlan {
  std::vector<std::string> directions;
  size_t collide_calls = 0;
};

struct SourceClipCollideTestWaypointsPlan {
  bool requires_character = true;
  size_t valid_waypoint_count = 0;
  size_t waypoint_assignments = 0;
  size_t test_clips_calls = 0;
};

struct SourceClipCollideTestCharsPlan {
  bool requires_character = true;
  bool requires_type_def = true;
  bool requires_chars_array = true;
  std::vector<std::string> tested_char_paths;
  size_t sync_char_calls = 0;
  size_t test_waypoints_calls = 0;
};

struct SourceClipCollideHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> action_handlers;
  std::vector<std::string> superclasses;
  int check = 0x1DC;
};

struct SourceClipCollidePropSyncRow {
  std::string property;
  std::string target;
  std::string side_effect;
  bool set_only = false;
};

struct SourceClipCollidePropSyncPlan {
  std::vector<SourceClipCollidePropSyncRow> rows;
};

struct SourceClipCollideSavePlan {
  int32_t save_id = 0x19D;
};

struct SourceFileMergerMergerState {
  bool proxy = false;
  bool pre_clear = false;
  int subdirs = 4;
  bool dir_null = true;
  bool loaded_objects_no_null = true;
  bool loaded_subdirs_no_null = true;
};

struct SourceFileMergerState {
  bool async_load = false;
  bool loading_load = false;
  int unk44 = 0;
  int unk50 = 0;
  bool callback_self = true;
  bool asserts_heap_when_heaps_exist = true;
};

struct SourceFileMergerCopyPlan {
  std::vector<std::string> copied_members;
};

struct SourceClipCompressorEvidence {
  bool has_runtime_class = false;
  std::string observed_function;
  std::string format_string;
};

// Source-backed CharClip constructor state.
SourceCharClipDefaultState source_char_clip_default_state();
SourceCharClipNumFramesPlan source_char_clip_num_frames_plan(
    int full_num_samples,
    int full_frame_count,
    int one_num_samples);
SourceCharClipTimingBodyBoundary source_char_clip_timing_body_boundary();
SourceCharClipBeatEvent source_char_clip_beat_event_default();
SourceCharClipBeatEvent source_char_clip_beat_event_copy(
    const SourceCharClipBeatEvent& source);
void source_char_clip_beat_event_assign(SourceCharClipBeatEvent& dest,
                                        const SourceCharClipBeatEvent& source);
SourceCharClipBeatEvent source_char_clip_beat_event_loaded(
    const std::string& event,
    float beat);
SourceCharClipPropSyncPlan source_char_clip_prop_sync_plan();
SourceCharClipResourceLookup source_char_clip_get_resource(
    bool has_type_def,
    bool has_resource_array,
    const std::string& resource_name,
    bool resource_found);
SourceCharClipContextLookup source_char_clip_get_context_lookup(
    bool has_type_def,
    bool has_resource_array,
    const std::string& macro_name,
    int resource_context);
int source_char_clip_get_context(bool has_type_def,
                                 bool has_resource_array,
                                 int resource_context);
SourceCharClipTransitionsState source_char_clip_transitions_construct(
    bool has_owner);
size_t source_char_clip_transitions_size(
    const SourceCharClipTransitionsState& transitions);
SourceCharClipTransitionsClearResult source_char_clip_transitions_clear(
    SourceCharClipTransitionsState& transitions);
SourceCharClipTransitionsDumpEvidence
source_char_clip_transitions_dump_evidence();
SourceCharClipRuntimeDumpEvidence source_char_clip_runtime_dump_evidence();
std::vector<SourceCharBonesBone> source_char_clip_stuff_bones(
    const std::vector<SourceCharBonesBone>& existing_bones,
    const std::vector<SourceCharBonesBone>& listed_bones);
SourceCharClipPoseMeshesSteps source_char_clip_pose_meshes_steps(float frame);
SourceReleasePosePublisherBoundary
source_release_pose_publisher_boundary();
SourceCharPosePublisherSourceRefresh
source_char_pose_publisher_source_refresh_20260714();

// Source-backed CharDriver constructor, Clear, Transfer, setter, and
// SyncInternalBones state helpers.
SourceCharDriverState source_char_driver_default_state();
SourceCharDriverDestructorPlan source_char_driver_destructor_plan();
SourceCharDriverExitPlan source_char_driver_exit_plan();
SourceCharDriverHighlightDecision source_char_driver_highlight_decision(
    float char_highlight_y);
void source_char_driver_clear(SourceCharDriverState& state);
SourceCharDriverEnterDecision source_char_driver_enter(
    SourceCharDriverState& state);
SourceCharDriverTransferPlan source_char_driver_transfer_plan(
    bool source_has_first);
void source_char_driver_transfer(SourceCharDriverState& state,
                                 const SourceCharDriverState& driver);
void source_char_driver_set_clips(SourceCharDriverState& state,
                                  bool has_clips);
void source_char_driver_set_bones(SourceCharDriverState& state,
                                  bool has_bones);
void source_char_driver_set_starved(SourceCharDriverState& state,
                                    const std::string& starved_handler);
void source_char_driver_set_blend_width(SourceCharDriverState& state,
                                        float blend_width);
SourceCharDriverSyncDecision source_char_driver_sync_internal_bones(
    SourceCharDriverState& state);
SourceCharDriverSyncDecision source_char_driver_set_apply(
    SourceCharDriverState& state,
    SourceCharDriverApplyMode apply);
SourceCharDriverSyncDecision source_char_driver_set_clip_type(
    SourceCharDriverState& state,
    const std::string& clip_type);
SourceCharDriverPlayGroupDecision source_char_driver_play_group_decision(
    bool has_clip_dir,
    bool found_group);
SourceCharDriverRuntimeDumpEvidence
source_char_driver_runtime_dump_evidence();
void source_char_driver_poll_deps(SourceCharDriverPollDeps& deps,
                                  const std::string& bones);
SourceCharDriverMidiState source_char_driver_midi_default_state();
SourceCharDriverMidiEnterDecision source_char_driver_midi_enter(
    SourceCharDriverState& driver_state,
    SourceCharDriverMidiState& midi_state,
    bool parser_found,
    bool flag_parser_found);
SourceCharDriverMidiExitDecision source_char_driver_midi_exit(
    bool parser_found,
    bool flag_parser_found);
SourceCharDriverMidiPollPlan source_char_driver_midi_poll_plan();
void source_char_driver_midi_poll_deps(SourceCharDriverPollDeps& deps,
                                       const std::string& bones);
void source_char_driver_midi_on_parser_flags(
    SourceCharDriverMidiState& midi_state,
    int clip_flags);
SourceCharDriverMidiParserDecision source_char_driver_midi_on_parser(
    const SourceCharDriverMidiState& midi_state,
    bool found_clip,
    bool clip_uses_real_time,
    float message_float,
    float beat_to_seconds_message_plus_current,
    float task_seconds,
    float average_beats_per_second);
SourceCharDriverMidiParserDecision source_char_driver_midi_on_parser_group(
    const SourceCharDriverMidiState& midi_state,
    bool found_group,
    bool found_group_clip,
    bool clip_uses_real_time,
    float message_float,
    float average_beats_per_second);
SourceCharDriverMidiLoadPlan source_char_driver_midi_load_plan(int revision);
SourceCharDriverMidiHandlerPlan source_char_driver_midi_handler_plan();
SourceCharDriverMidiPropSyncPlan source_char_driver_midi_prop_sync_plan();
SourceCharDriverMidiCopyPlan source_char_driver_midi_copy_plan();
SourceCharDriverMidiSavePlan source_char_driver_midi_save_plan();
SourceCharClipSetState source_char_clip_set_default_state();
void source_char_clip_set_reset_preview_state(
    SourceCharClipSetState& state);
SourceCharClipSetResetEditorResult source_char_clip_set_reset_editor_state(
    SourceCharClipSetState& state);
std::vector<SourceCharClipSetGroupStep> source_char_clip_set_randomize_groups(
    const std::vector<std::string>& groups);
std::vector<SourceCharClipSetGroupStep> source_char_clip_set_sort_groups(
    const std::vector<std::string>& groups);
SourceCharClipSetPreSaveResult source_char_clip_set_pre_save(
    SourceCharClipSetState& state,
    bool cached_stream);
SourceCharClipSetPostSaveResult source_char_clip_set_post_save(
    const SourceCharClipSetState& state,
    bool milo_found);
SourceCharClipSetPreLoadPlan source_char_clip_set_pre_load_plan();
SourceCharClipSetLoadPlan source_char_clip_set_load_plan();
SourceCharClipSetPostLoadPlan source_char_clip_set_post_load_plan(
    int32_t revision,
    bool is_proxy,
    int32_t clip_count,
    bool type_null);
SourceCharClipSetCopyResult source_char_clip_set_copy(
    SourceCharClipSetState& dest,
    const SourceCharClipSetState& source);
SourceCharClipSetLoadCharacterResult source_char_clip_set_load_character(
    SourceCharClipSetState& state,
    bool edit_mode,
    bool loaded_is_rnd_dir,
    bool loaded_is_character,
    bool nested_character_found,
    bool milo_found);
bool source_char_clip_set_draw_showing(bool has_preview_char);
float source_char_clip_set_start_frame(bool has_preview_clip,
                                       float preview_clip_start_beat);
float source_char_clip_set_end_frame(bool has_preview_clip,
                                     float preview_clip_end_beat);
SourceCharClipSetSetBpmResult source_char_clip_set_set_bpm(
    SourceCharClipSetState& state,
    int bpm,
    bool milo_found);
const char* source_char_clip_set_recenter_all_warning();
SourceCharClipSetHandlerPlan source_char_clip_set_handler_plan();
SourceCharClipSetSavePlan source_char_clip_set_save_plan();
void source_char_clip_display_init(SourceCharClipDisplayGlobals& globals,
                                   const std::string& dir,
                                   float draw_empty_y);
SourceCharClipDisplayFindSourceResult source_char_clip_display_find_source(
    const std::vector<SourceCharClipDisplayMsgSource>& sources,
    const std::string& object);
void source_char_clip_display_set_text(
    SourceCharClipDisplayState& state,
    const SourceCharClipDisplayGlobals& globals,
    const std::string& text,
    float draw_text_x);
void source_char_clip_display_set_start_end(
    SourceCharClipDisplayState& state,
    float start_beat,
    float end_beat,
    bool flag);
void source_char_clip_display_set_clip(
    SourceCharClipDisplayState& state,
    const SourceCharClipDisplayGlobals& globals,
    const std::string& clip_name,
    float start_beat,
    float end_beat,
    bool flag,
    float draw_text_x);
float source_char_clip_display_line_spacing(
    const SourceCharClipDisplayGlobals& globals);
SourceCharTaskMgrState source_char_task_mgr_default_state();
void source_char_task_mgr_init(SourceCharTaskMgrState& state);
bool source_char_task_mgr_toggle_graph(SourceCharTaskMgrState& state);
SourceClipGraphGeneratePairStep source_clip_graph_generate_pair_step(
    bool has_type_def,
    bool same_type,
    uint32_t clip_a_play_flags,
    bool has_on_transition,
    bool script_creates_dmap);
SourceClipGraphTransitionPlan source_clip_graph_on_generate_transitions(
    const SourceClipGraphTransitionInputs& inputs);
SourceClipCollideState source_clip_collide_default_state();
bool source_clip_collide_load_revision_known(int revision);
SourceClipCollideLoadPlan source_clip_collide_load_plan(int revision);
SourceClipCollideSyncCharStep source_clip_collide_sync_char_step(
    bool has_character,
    bool char_path_empty,
    bool path_matches_proxy);
SourceClipCollideSetTypeDefStep source_clip_collide_set_type_def_step(
    bool type_def_changed,
    bool has_type_def);
SourceClipCollideValidationStep source_clip_collide_valid_waypoint(
    bool handler_unhandled,
    bool handler_value);
SourceClipCollideValidationStep source_clip_collide_valid_clip(
    bool has_waypoint,
    bool handler_unhandled,
    bool handler_value);
SourceClipCollideDemonstrateStep source_clip_collide_demonstrate_step(
    bool has_character,
    bool has_waypoint,
    bool has_clip);
SourceClipCollideClearReportStep source_clip_collide_clear_report_step();
SourceClipCollideSyncModeStep source_clip_collide_sync_mode_step(
    bool mode_null);
SourceClipCollideListPlan source_clip_collide_list_objects_plan(
    const std::vector<std::string>& valid_objects);
SourceClipCollideListPlan source_clip_collide_list_report_plan(
    const std::vector<std::string>& reports);
SourceClipCollideTestClipsPlan source_clip_collide_test_clips_plan(
    size_t valid_clip_count);
SourceClipCollideTestWaypointsPlan source_clip_collide_test_waypoints_plan(
    bool has_character,
    size_t valid_waypoint_count);
SourceClipCollideTestCharsPlan source_clip_collide_test_chars_plan(
    bool has_character,
    bool has_type_def,
    bool has_chars_array,
    const std::vector<std::string>& char_paths);
SourceClipCollideHandlerPlan source_clip_collide_handler_plan();
SourceClipCollidePropSyncPlan source_clip_collide_prop_sync_plan();
SourceClipCollideSavePlan source_clip_collide_save_plan();
SourceFileMergerState source_file_merger_default_state();
SourceFileMergerMergerState source_file_merger_merger_default_state();
SourceFileMergerCopyPlan source_file_merger_merger_copy_plan();
SourceClipCompressorEvidence source_clip_compressor_evidence();

// Source-backed CharClip::SetFlags / SetPlayFlags dirty-state helpers.
SourceCharClipFlagUpdate source_char_clip_set_flags(uint32_t current_flags,
                                                    bool current_dirty,
                                                    uint32_t requested_flags);
SourceCharClipFlagUpdate source_char_clip_set_play_flags(
    uint32_t current_play_flags,
    bool current_dirty,
    uint32_t requested_play_flags);
bool source_char_clip_shares_groups(
    const std::vector<SourceCharClipRefOwner>& ref_owners,
    const std::string& candidate_clip_name);

// Source-backed CharDriver::Starved helper for the visible play stack state.
bool source_char_driver_starved(bool has_first, bool first_has_next,
                                uint32_t first_play_flags);

// Source-backed CharDriver::Play blend-width fallback.
float source_char_driver_resolve_blend_width(float requested_blend_width,
                                             float driver_blend_width);

// Source-backed CharDriver::Play duplicate-clip gate.
bool source_char_driver_should_start_clip(bool play_multiple_clips,
                                          bool clip_already_playing);

// Source-backed CharDriver::Play(CharClip*) state decision. This records the
// branch order without allocating a CharClipDriver node.
SourceCharDriverPlayDecision source_char_driver_play_decision(
    SourceCharDriverState& state,
    bool found_clip,
    bool clip_already_playing,
    int play_flags,
    float requested_blend_width,
    float old_beat,
    float start);

// Source-backed CharDriver::Play(DataNode) decision. The checked source copies
// the requested node, resolves a clip, calls Play(CharClip*), then restores
// mLastNode to the requested node even when no clip/driver was created.
SourceCharDriverPlayNodeDecision source_char_driver_play_node_decision(
    SourceCharDriverState& state,
    bool find_clip_succeeds,
    bool clip_already_playing,
    int play_flags,
    float requested_blend_width,
    float old_beat,
    float start);

// Source-backed CharDriver::FirstPlaying helper. The input is in source stack
// order: mFirst, then each mNext.
std::optional<size_t> source_char_driver_first_playing_index(
    const std::vector<float>& source_stack_blend_fracs);

// Source-backed CharBones channel helpers.
int source_char_bones_type_of(const std::string& channel);
const char* source_char_bones_suffix_of(int type);
std::string source_char_bones_channel_name(const std::string& name, int type);
size_t source_char_bones_type_size(int type, int compression);
SourceCharBonesLayout source_char_bones_recompute_layout(
    const std::array<int, kSourceCharBonesTypeEnd + 1>& counts,
    int compression);
SourceCharBonesCompressionUpdate source_char_bones_set_compression(
    int current_compression,
    const SourceCharBonesLayout& current_layout,
    int requested_compression);
SourceCharBonesState source_char_bones_empty_state();
void source_char_bones_clear(SourceCharBonesState& state);
void source_char_bones_set_weights(std::vector<SourceCharBonesBone>& bones,
                                   float weight);
void source_char_bones_set_weights(SourceCharBonesState& state, float weight);
void source_char_bones_list_bones(const SourceCharBonesState& state,
                                  std::vector<SourceCharBonesBone>& bones);
int source_char_bones_find_offset(const SourceCharBonesState& state,
                                  const std::string& channel);
SourceCharBonesFindPtrResult source_char_bones_find_ptr(
    const SourceCharBonesState& state,
    const std::string& channel);
void source_char_bones_zero(std::vector<uint8_t>& start, int total_size);
SourceCharBonesScaleAddClipStep source_char_bones_scale_add_clip_step(
    float f1, float f2, float f3);
SourceCharBonesPoseBodyBoundary source_char_bones_pose_body_boundary();
SourceCharBonesRuntimeDumpEvidence source_char_bones_runtime_dump_evidence();
std::vector<SourceCharPoseRuntimeSymbolEvidence>
source_char_pose_runtime_symbol_evidence();
SourceCharBonesAddBonesSteps source_char_bones_add_bones_steps(
    const std::vector<SourceCharBonesBone>& bones);
SourceCharBonesAllocReallocateStep source_char_bones_alloc_reallocate_step(
    int total_size);
SourceCharBonesEnterStep source_char_bones_enter_step();
SourceCharBonesBlenderPollStep source_char_bones_blender_poll_step(
    bool bones_empty,
    bool has_dest);
SourceCharBonesBlenderSetDestStep source_char_bones_blender_set_dest_step(
    bool dest_changed,
    bool new_dest_exists);
SourceCharBonesBlenderSetClipTypeStep
source_char_bones_blender_set_clip_type_step(bool clip_type_changed);
SourceCharBonesBlenderReallocateStep
source_char_bones_blender_reallocate_step(bool has_dest);
SourceCharBonesBlenderLoadPlan source_char_bones_blender_load_plan(
    int32_t revision);
SourceCharBonesBlenderSavePlan source_char_bones_blender_save_plan();
SourceCharBonesBlenderCopyPlan source_char_bones_blender_copy_plan();
SourceCharBonesBlenderHandlerPlan source_char_bones_blender_handler_plan();
SourceCharBonesBlenderPropSyncPlan source_char_bones_blender_prop_sync_plan();
void source_char_bones_blender_poll_deps(
    SourceCharBonesBlenderPollDeps& deps,
    const std::string& dest);

// Source-backed CharBone helpers for decoded CharClip output rows.
SourceCharBoneLoadPlan source_char_bone_load_plan(int32_t revision);
SourceCharBoneSavePlan source_char_bone_save_plan();
SourceCharBoneCopyPlan source_char_bone_copy_plan();
SourceCharBoneHandlerPlan source_char_bone_handler_plan();
SourceCharBoneWeightContextPropSyncPlan
source_char_bone_weight_context_prop_sync_plan();
SourceCharBoneWeightContextDefaultState
source_char_bone_weight_context_default_state();
SourceCharBoneWeightContextLoadPlan source_char_bone_weight_context_load_plan();
SourceCharBoneContextFlagsStep source_char_bone_get_context_flags_step(
    bool parent_is_char_bone_dir);
SourceCharBonePropSyncPlan source_char_bone_prop_sync_plan();
SourceCharBonesBonePropSyncPlan source_char_bones_bone_prop_sync_plan();
SourceCharBonesObjectPropSyncPlan source_char_bones_object_prop_sync_plan();
CharClip::OutputBone source_char_bone_copy_members(
    const CharClip::OutputBone& source);
std::optional<size_t> source_char_bone_find_weight_index(
    const CharClip::OutputBone& bone, int context_mask);
float source_char_bone_get_weight(const CharClip::OutputBone& bone,
                                  int context_mask);
void source_char_bone_clear_context(CharClip::OutputBone& bone,
                                    int context_mask);
void source_char_bone_stuff_bones(const CharClip::OutputBone& bone,
                                  int context_mask,
                                  std::vector<SourceCharBonesBone>& bones);
SourceCharBoneDirDefaultState source_char_bone_dir_default_state();
SourceCharBoneDirLoadPlan source_char_bone_dir_load_plan(int32_t revision);
SourceCharBoneDirSavePlan source_char_bone_dir_save_plan();
SourceCharBoneDirCopyPlan source_char_bone_dir_copy_plan();
SourceCharBoneDirHandlerPlan source_char_bone_dir_handler_plan();
SourceCharBoneDirRecenterPropSyncPlan
source_char_bone_dir_recenter_prop_sync_plan();
SourceCharBoneDirRecenterLoadPlan source_char_bone_dir_recenter_load_plan();
SourceCharBoneDirPropSyncPlan source_char_bone_dir_prop_sync_plan();
SourceCharBoneDirInitPlan source_char_bone_dir_init_plan(
    const std::string& resource_path,
    bool has_clip_types,
    const std::vector<SourceCharBoneDirInitClipTypeRow>& clip_types);
SourceCharBoneDirTerminatePlan source_char_bone_dir_terminate_plan();
SourceCharBoneDirFindResourceResult source_char_bone_dir_find_resource(
    const std::vector<std::string>& loaded_resources,
    const std::string& resource_name);
void source_char_bone_dir_list_bones(
    const std::vector<CharClip::OutputBone>& output_bones,
    int move_context,
    int context_mask,
    bool include_delta_facing,
    std::vector<SourceCharBonesBone>& bones);
std::vector<std::string> source_char_bone_dir_get_clip_types(
    const std::vector<SourceCharBoneDirClipTypeResource>& clip_types);
SourceCharBoneDirResourceLookupResult
source_char_bone_dir_find_resource_from_clip_type(
    const std::vector<SourceCharBoneDirClipTypeResource>& clip_types,
    const std::string& clip_type);
SourceCharBoneDirStuffBonesSymbolStep
source_char_bone_dir_stuff_bones_symbol_step(
    const std::vector<SourceCharBoneDirClipTypeResource>& clip_types,
    const std::string& clip_type);
SourceCharBoneDirContextFlagsStep
source_char_bone_dir_get_context_flags_step(
    const std::vector<SourceCharBoneDirClipTypeResource>& clip_types,
    const std::string& resource_name,
    const std::vector<std::string>& cached_context_flags,
    bool context_flags_is_int);
std::vector<std::string> source_char_bone_dir_sync_filter(
    const std::vector<CharClip::OutputBone>& output_bones,
    int filter_context);
SourceCharBoneDirMergeCharacterPlan source_char_bone_dir_merge_character_plan(
    bool load_succeeds,
    const std::vector<SourceCharBoneDirMergeTransform>& transforms);
SourceCharBonesMeshesReplaceStep source_char_bones_meshes_replace_step(
    const std::vector<std::string>& meshes,
    const std::string& from,
    const std::string& to,
    bool to_is_transformable,
    const std::string& dummy_mesh);
SourceCharBonesMeshesReallocateStep source_char_bones_meshes_reallocate_step(
    const std::vector<SourceCharBonesBone>& bones,
    const std::unordered_map<std::string, std::string>& transform_lookup,
    const std::string& dummy_mesh);
SourceCharBonesMeshesLifetimePlan source_char_bones_meshes_lifetime_plan();
std::vector<std::string> source_char_bones_meshes_stuff_meshes(
    const std::vector<std::string>& existing_objects,
    const std::vector<std::string>& meshes);
SourceCharBonesMeshesPoseDumpEvidence
source_char_bones_meshes_pose_dump_evidence();
void source_char_bones_meshes_set_axis_rotation(
    milo_scene::Xfm& xfm,
    ClipChannel::Type axis,
    float angle_radians);

// Source-backed CharServoBone movement helpers. These port the isolated math
// bodies only; broad CharBonesMeshes movement stays fenced to the clip stack.
SourceCharServoBoneDefaultState source_char_servo_bone_default_state();
SourceCharServoBoneSetNameStep source_char_servo_bone_set_name(
    bool dir_is_character);
SourceCharServoBoneSetClipTypeStep source_char_servo_bone_set_clip_type_step(
    bool clip_type_changed);
SourceCharServoBoneEnterStep source_char_servo_bone_enter(
    bool facing_pos_delta_present);
SourceCharServoBoneSetMoveSelfStep source_char_servo_bone_set_move_self(
    bool current_move_self,
    bool requested_move_self);
SourceCharServoBoneReallocatePlan source_char_servo_bone_reallocate_plan(
    bool found_facing_pos_delta);
SourceCharServoBoneCopyPlan source_char_servo_bone_copy_plan();
SourceCharServoBoneLoadPlan source_char_servo_bone_load_plan(int32_t revision);
SourceCharServoBoneSavePlan source_char_servo_bone_save_plan();
SourceCharServoBoneHandlerPlan source_char_servo_bone_handler_plan();
SourceCharServoBonePropSyncPlan source_char_servo_bone_prop_sync_plan();
SourceCharServoBoneRuntimeDumpEvidence
source_char_servo_bone_runtime_dump_evidence();
void source_char_servo_bone_zero_deltas(
    std::array<float, 3>& facing_pos_delta,
    float& facing_rot_delta_radians);
void source_char_servo_bone_move_to_facing(
    milo_scene::Xfm& xfm,
    const std::array<float, 3>& facing_pos,
    float facing_rot_radians);
void source_char_servo_bone_move_to_delta_facing(
    milo_scene::Xfm& xfm,
    const std::array<float, 3>& facing_pos_delta,
    float facing_rot_delta_radians);

// Source-backed CharBonesSamples state helpers.
SourceCharBonesSamplesState source_char_bones_samples_empty_state();
void source_char_bones_samples_set(SourceCharBonesSamplesState& samples,
                                   const SourceCharBonesState& bones,
                                   int num_samples,
                                   int compression);
SourceCharBonesSamplesState source_char_bones_samples_clone(
    const SourceCharBonesSamplesState& source);
int source_char_bones_samples_allocate_size(
    const SourceCharBonesSamplesState& samples);
bool source_char_bones_samples_set_preview(
    SourceCharBonesSamplesState& samples, int requested_sample);
std::vector<SourceCharBonesSampleStep> source_char_bones_samples_split_steps(
    const SourceCharBonesSamplesState& samples,
    int sample,
    float weight,
    float frac);
int source_char_bones_samples_rotate_by_offset(
    const SourceCharBonesSamplesState& samples,
    int sample);
SourceCharBonesSampleStep source_char_bones_samples_rotate_by_step(
    const SourceCharBonesSamplesState& samples,
    int sample);
std::vector<SourceCharBonesSampleStep> source_char_bones_samples_rotate_to_steps(
    const SourceCharBonesSamplesState& samples,
    int sample,
    float angle,
    float frac);
std::vector<SourceCharBonesSampleStep>
source_char_bones_samples_scale_add_steps(
    const SourceCharBonesSamplesState& samples,
    int sample,
    float weight,
    float frac);
bool source_char_bones_samples_set_ver_known(int version);
bool source_char_bones_samples_load_version_known(int version);
SourceCharBonesSamplesLoadPlan source_char_bones_samples_load_plan(int version);
bool source_grim_char_bones_samples_standalone_version_known(int version);
bool source_grim_char_clip_samples_version_known(int version);
bool source_grim_char_clip_version_known(int version);
int source_grim_char_bones_samples_get_type_of(const std::string& channel);
float source_grim_char_bones_samples_decode_snorm16(int16_t value);
float source_gh2_char_bones_samples_decode_scalar_angle(int16_t value);
std::array<float, 4> source_grim_char_bones_samples_decode_short_quat(
    int16_t x,
    int16_t y,
    int16_t z,
    int16_t w);
float source_grim_char_bones_samples_pose_axis_angle(
    ClipChannel::Type axis,
    float sample);
size_t source_grim_char_bones_samples_get_type_size(int type,
                                                    int compression);
size_t source_grim_char_bones_samples_get_type_size2(int type,
                                                     int compression);
SourceGrimCharBonesSamplesComputedSizes
source_grim_char_bones_samples_recompute_sizes(
    int compression,
    const std::array<uint32_t, kSourceCharBonesTypeEnd + 1>& counts);
std::string source_grim_char_bones_samples_channel_mesh_name(
    const std::string& channel);
float source_grim_char_bones_samples_channel_weight(
    const std::vector<float>& weights,
    size_t index);
void source_grim_char_bones_samples_sort_decoded_channels(
    std::vector<ClipChannel>& channels);
bool source_grim_char_bones_samples_decodes_channel_type(int type);
bool source_grim_char_bones_samples_panics_channel_type(int type);
SourceGrimCharBonesSamplesDecodePlan
source_grim_char_bones_samples_decode_plan();
SourceGrimCharBonesSamplesExportTranslationPlan
source_grim_char_bones_samples_export_translation_plan(
    const SourceGrimCharBonesSamplesExportTranslationInput& input);
SourceGrimCharBonesSamplesExportRotationPlan
source_grim_char_bones_samples_export_rotation_plan(
    const SourceGrimCharBonesSamplesExportRotationInput& input);
SourceGrimCharBonesSamplesHeaderPlan
source_grim_char_bones_samples_header_plan(int version);
SourceGrimCharBonesSamplesDataPlan source_grim_char_bones_samples_data_plan(
    int version,
    int compression,
    const std::vector<std::string>& channels,
    int sample_count);
SourceGrimCharClipLoadPlan source_grim_char_clip_load_plan(int version,
                                                           bool read_meta);
SourceGrimCharClipSamplesLoadPlan
source_grim_char_clip_samples_load_plan(int version);
SourceGrimCharClipSamplesExtraBonesPlan
source_grim_char_clip_samples_extra_bones_plan(int version);
SourceReNotesCharBonesSamplesDecodePlan
source_re_notes_char_bones_samples_decode_plan();
SourceProblemCharacterClipRawAxisAudit
source_problem_character_clip_raw_axis_audit_20260714();
SourceCharBonesSamplesPropSyncPlan source_char_bones_samples_prop_sync_plan();
SourceCharBonesSamplesBodyBoundary
source_char_bones_samples_body_boundary();
SourceCharBonesSamplesRuntimeDumpEvidence
source_char_bones_samples_runtime_dump_evidence();
SourceCharClipSamplesRuntimeDumpEvidence
source_char_clip_samples_runtime_dump_evidence();

// Source-backed CharUtl name/object helpers. CharUtlFindBone rewrites the
// incoming name to .cb. CharUtlFindBoneTrans checks .cb first and returns that
// CharBone row's transform before falling back to .trans, then .mesh.
std::string source_char_utl_name_with_suffix(const std::string& name,
                                             const std::string& suffix);
std::optional<SourceCharUtlObject> source_char_utl_find_bone(
    const std::string& name,
    const std::vector<SourceCharUtlObject>& objects);
std::optional<SourceCharUtlBoneTransResult> source_char_utl_find_bone_trans(
    const std::string& name,
    const std::vector<SourceCharUtlObject>& objects);
bool source_char_utl_is_animatable(const SourceCharUtlObject& object);
SourceCharUtlMergeResult source_char_utl_merge_bones(
    const std::vector<SourceCharUtlMergeBone>& source_bones,
    const std::vector<SourceCharUtlMergeBone>& dest_bones,
    int context_mask);
std::vector<std::string> source_char_utl_bone_saver_capture_names(
    const std::vector<SourceCharUtlTransformRow>& transforms);
std::vector<std::string> source_char_utl_reset_transform_names(
    const std::vector<SourceCharUtlTransformRow>& transforms);
std::vector<std::string> source_char_utl_reset_hair_names(
    const std::vector<std::string>& hair_names);
void source_char_utl_clip_predict(SourceCharUtlClipPredictState& state,
                                  const SourceCharUtlClipPredictFrame& first,
                                  const SourceCharUtlClipPredictFrame& second);
std::optional<SourceCharUtlClipPredictFrame>
source_char_walk_facing_sample(const std::vector<ClipChannel>& channels);
std::optional<SourceCharUtlClipPredictFrame>
source_char_clip_facing_sample_at_beat(const CharClip& clip, float beat);
std::optional<SourceCharUtlClipPredictFrame>
source_gh2_char_clip_facing_sample_at_beat(const CharClip& clip, float beat);
SourceCharFacingDelta source_char_clip_facing_delta_at_beat(
    const CharClip& clip, float weight, float beat, float d_beat);
std::optional<float> source_charwalk_find_stop_start_beat(
    const CharClip& stop_clip, float beat_remainder);
SourceCharWalkMotionPlan source_charwalk_build_motion_plan(
    const std::vector<SourceCharWalkScheduleEntry>& initial_schedule,
    const std::vector<std::array<float, 3>>& path,
    float target_yaw,
    float target_radius,
    const std::vector<SourceCharWalkStopCandidate>& stop_candidates,
    uint32_t required_stop_flags);
std::optional<SourceCharWalkForwardPrediction>
source_charwalk_forward_predict(
    const SourceCharWalkMotionPlan& plan,
    size_t clip_index,
    float beat,
    float look_ahead,
    const SourceCharUtlClipPredictState& initial);
std::optional<std::array<float, 3>> source_charwalk_back_predict(
    const SourceCharWalkMotionPlan& plan,
    size_t clip_index,
    float beat,
    size_t waypoint_index,
    float target_yaw);
SourceCharWalkOffsetRegulation source_charwalk_regulate_offset(
    const SourceCharWalkMotionPlan& plan,
    size_t point_index,
    size_t clip_index,
    float beat,
    size_t waypoint_index,
    const std::array<float, 3>& current_position,
    const std::array<float, 3>& predicted_position,
    float frame_delta,
    float prior_offset_speed);
SourceCharUtlInitPlan source_char_utl_init_plan();

// Source-backed CharLookAt::SyncLimits helper. Angles are serialized in degrees.
SourceCharLookAtBounds source_char_lookat_sync_limits(
    float min_yaw, float max_yaw, float min_pitch, float max_pitch);
SourceCharLookAtLimitState source_char_lookat_default_limit_state();
void source_char_lookat_set_min_yaw(SourceCharLookAtLimitState& state,
                                    float yaw);
void source_char_lookat_set_max_yaw(SourceCharLookAtLimitState& state,
                                    float yaw);
void source_char_lookat_set_min_pitch(SourceCharLookAtLimitState& state,
                                      float pitch);
void source_char_lookat_set_max_pitch(SourceCharLookAtLimitState& state,
                                      float pitch);
SourceCharLookAtLoadPlan source_char_lookat_load_plan(int32_t revision);
SourceCharLookAtCopyPlan source_char_lookat_copy_plan();
SourceCharLookAtHandlerPlan source_char_lookat_handler_plan();
SourceCharLookAtPropSyncPlan source_char_lookat_prop_sync_plan();
SourceCharLookAtSavePlan source_char_lookat_save_plan();
SourceCharLookAtEnterState source_char_lookat_enter(bool has_pivot);
void source_char_lookat_poll_deps(SourceCharLookAtPollDeps& deps,
                                  const std::string& source,
                                  const std::string& pivot,
                                  const std::string& dest);
SourceCharLookAtPollPlan source_char_lookat_poll_plan(
    bool has_resolved_source,
    bool has_pivot,
    bool has_dest,
    bool has_pivot_parent,
    float delta_seconds,
    float weight,
    float min_weight_yaw,
    float source_radius,
    bool source_is_pivot,
    bool has_smoothed_dir,
    float half_time,
    bool test_range,
    bool show_range,
    bool enable_jitter,
    bool static_disable_jitter,
    bool cheat_disable_eye_jitter,
    bool allow_roll);
SourceCharLookAtYawWeightResult source_char_lookat_yaw_weight_step(
    float row_weight,
    float previous_yaw_weight,
    float min_weight_yaw,
    float max_weight_yaw,
    float weight_yaw_speed,
    float delta_seconds,
    std::array<float, 3> source_world_y,
    std::array<float, 3> dest_delta);
SourceCharLookAtNoRollAxesResult source_char_lookat_no_roll_axes(
    std::array<float, 3> current_local_y,
    std::array<float, 3> desired_parent_space_dir,
    float weight);
SourceCharLookAtSmoothResult source_char_lookat_smooth_dir(
    bool has_previous,
    std::array<float, 3> previous_dir,
    std::array<float, 3> current_dir,
    float delta_seconds,
    float half_time);
SourceCharLookAtRangeResult source_char_lookat_range_dir(
    const SourceCharLookAtBounds& bounds,
    bool test_range,
    float test_range_pitch,
    float test_range_yaw,
    bool show_range,
    int seconds);
SourceCharLookAtSourceRadiusResult source_char_lookat_source_radius_offset(
    float source_radius_degrees,
    float delta_seconds,
    std::array<float, 3> previous_history,
    std::array<float, 3> source_world_y);

// Source-backed CharWeightable::Weight helper. The owner row is used when it
// resolves; otherwise this falls back to the row's own serialized weight.
struct SourceCharWeightableState {
  std::string name;
  float weight = 1.0f;
  std::string weight_owner;
};

struct SourceCharWeightableLoadPlan {
  bool revision_supported = false;
  std::vector<std::string> read_order;
};

struct SourceCharWeightableCopyPlan {
  std::vector<std::string> shallow_actions;
  std::vector<std::string> deep_actions;
};

struct SourceCharWeightableHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharWeightablePropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_actions;
  std::vector<std::string> get_actions;
  std::vector<std::string> blocked_ops;
};

struct SourceCharWeightableSavePlan {
  int32_t save_id = 0x21;
};

SourceCharWeightableLoadPlan source_char_weightable_load_plan(
    int32_t revision);
SourceCharWeightableCopyPlan source_char_weightable_copy_plan();
SourceCharWeightableHandlerPlan source_char_weightable_handler_plan();
SourceCharWeightablePropSyncPlan source_char_weightable_prop_sync_plan();
SourceCharWeightableSavePlan source_char_weightable_save_plan();
SourceCharWeightableState source_char_weightable_default_state(
    const std::string& name);
void source_char_weightable_set_weight(SourceCharWeightableState& state,
                                       float weight);
void source_char_weightable_set_weight_owner(SourceCharWeightableState& state,
                                             const std::string& weight_owner);
void source_char_weightable_replace(SourceCharWeightableState& state,
                                    const std::string& old_owner,
                                    const std::string& new_owner,
                                    bool new_owner_is_weightable);
void source_char_weightable_copy(SourceCharWeightableState& dest,
                                 const SourceCharWeightableState& source,
                                 bool shallow_copy,
                                 float source_owner_weight);
float source_char_weightable_weight(
    const CharWeightSetter& setter,
    const std::unordered_map<std::string, float>& weights_by_name);

struct SourceCharMirrorState {
  SourceCharWeightableState weightable;
  std::string servo;
  std::string mirror_servo;
  size_t bones_total_size = 0;
  size_t ops_count = 0;
};

struct SourceCharMirrorPollResult {
  float weight = 0.0f;
  bool weight_zero = false;
  bool bones_empty = false;
  bool scale_down = false;
  float scale_down_weight = 0.0f;
  std::string servo;
};

struct SourceCharMirrorSetServoResult {
  bool changed = false;
  bool synced_bones = false;
};

struct SourceCharMirrorPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharMirrorLoadSteps {
  int32_t max_revision = 1;
  bool load_hmx_object = false;
  bool load_weightable = false;
  bool load_mirror_servo = false;
  bool load_servo = false;
  bool sync_bones = false;
};

struct SourceCharMirrorCopyResult {
  bool copy_hmx_object = false;
  bool copy_weightable = false;
  SourceCharMirrorSetServoResult set_mirror_servo;
  SourceCharMirrorSetServoResult set_servo;
};

struct SourceCharMirrorSavePlan {
  int32_t save_id = 0x90;
};

SourceCharMirrorState source_char_mirror_default_state(
    const std::string& name);
SourceCharMirrorPollResult source_char_mirror_poll(
    const SourceCharMirrorState& state,
    const std::unordered_map<std::string, float>& weights_by_name);
SourceCharMirrorSetServoResult source_char_mirror_set_servo(
    SourceCharMirrorState& state,
    const std::string& servo);
SourceCharMirrorSetServoResult source_char_mirror_set_mirror_servo(
    SourceCharMirrorState& state,
    const std::string& mirror_servo);
void source_char_mirror_poll_deps(SourceCharMirrorPollDeps& deps,
                                  const SourceCharMirrorState& state);
SourceCharMirrorLoadSteps source_char_mirror_load_steps();
SourceCharMirrorCopyResult source_char_mirror_copy(
    SourceCharMirrorState& dest,
    const SourceCharMirrorState& source,
    bool shallow_copy,
    float source_owner_weight);
SourceCharMirrorSavePlan source_char_mirror_save_plan();

// Source-backed CharWeightSetter::Poll helper for rows that do not require the
// unavailable CharDriver::EvaluateFlags body. Returns false when the row is
// driver-backed and no source evaluator is present.
bool source_char_weight_setter_poll(
    const CharWeightSetter& setter,
    const std::unordered_map<std::string, float>& weights_by_name,
    float delta_beats,
    float& out_weight);
bool source_char_weight_setter_poll_with_driver_result(
    const CharWeightSetter& setter,
    const std::unordered_map<std::string, float>& weights_by_name,
    float delta_beats,
    std::optional<float> driver_evaluate_flags,
    float& out_weight);

struct SourceCharMainDriverFlagWeight {
  std::string driver;
  uint32_t flags = 0;
  float weight = 0.0f;
};

struct SourceCharMainDriverHandWeights {
  float left = 1.0f;
  float right = 1.0f;
  bool left_source = false;
  bool right_source = false;
  std::vector<SourceCharMainDriverFlagWeight> driver_flags;
};

SourceCharMainDriverHandWeights source_char_main_driver_hand_weights_from_clip_flags(
    const Character& character, uint32_t clip_flags, float fallback_left,
    float fallback_right);
SourceCharMainDriverHandWeights source_char_main_driver_hand_weights_from_player(
    const Character& character, const CharClipPlayer* player,
    float fallback_left, float fallback_right);

struct SourceCharWeightSetterRefOwner {
  std::string name;
  bool weight_owner_is_setter = false;
};

struct SourceCharWeightSetterPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharWeightSetterState {
  SourceCharWeightableState weightable;
  bool has_base = false;
  bool has_driver = false;
  size_t min_weight_count = 0;
  size_t max_weight_count = 0;
  uint32_t flags = 0;
  float offset = 0.0f;
  float scale = 1.0f;
  float base_weight = 0.0f;
  float beats_per_weight = 0.0f;
};

struct SourceCharWeightSetterLoadPlan {
  bool revision_supported = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharWeightSetterCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharWeightSetterHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharWeightSetterPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharWeightSetterSavePlan {
  int32_t save_id = 0x73;
};

struct SourceCharWeightSetterRuntimeDumpEvidence {
  std::string poll_range;
  std::string poll_deps_range;
  std::string load_range;
  std::string copy_range;
  std::vector<std::string> poll_locals;
  std::vector<std::string> poll_deps_locals;
  std::vector<std::string> load_locals;
  bool rb2_dump_has_statement_body = false;
  bool safe_to_run_driver_branch = false;
  bool safe_to_run_driver_branch_with_supplied_evaluate_flags = true;
  bool requires_external_evaluate_flags = true;
  bool safe_to_publish_driver_weight = false;
};

struct SourceCharIKHeadPoint {
  std::string transform;
  float local_length = 0.0f;
  float normalized_remaining = 0.0f;
};

struct SourceCharIKHeadState {
  SourceCharWeightableState weightable;
  std::string head;
  std::string spine;
  std::string mouth;
  std::string target;
  std::array<float, 3> head_filter = {0.0f, 0.0f, 0.0f};
  float target_radius = 0.75f;
  float head_mat = 0.5f;
  std::string offset;
  std::array<float, 3> offset_scale = {1.0f, 1.0f, 1.0f};
  float spine_length = 0.0f;
  bool update_points = true;
  std::string character_dir;
  std::vector<SourceCharIKHeadPoint> points;
};

struct SourceCharIKHeadSetNameResult {
  bool call_hmx_set_name = false;
  bool assigned_character = false;
};

struct SourceCharIKHeadPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharIKHeadUpdatePointsResult {
  bool entered_body = false;
  bool rebuilt_points = false;
  size_t point_count = 0;
  float spine_length = 0.0f;
};

struct SourceCharIKHeadLoadSteps {
  int32_t max_revision = 3;
  bool load_hmx_object = false;
  bool load_weightable = false;
  bool load_head = false;
  bool load_spine = false;
  bool load_mouth = false;
  bool load_target = false;
  bool load_target_radius = false;
  bool load_head_mat = false;
  bool load_offset = false;
  bool load_offset_scale = false;
  bool set_update_points = false;
};

struct SourceCharIKHeadCopyResult {
  bool copy_hmx_object = false;
  bool copy_weightable = false;
  bool copy_head = false;
  bool copy_spine = false;
  bool copy_mouth = false;
  bool copy_target = false;
  bool copy_target_radius = false;
  bool copy_head_mat = false;
  bool copy_offset = false;
  bool copy_offset_scale = false;
  bool set_update_points = false;
};

struct SourceCharIKHeadHandlerPlan {
  std::vector<std::string> superclasses;
  int32_t check = 0;
};

struct SourceCharIKHeadPropSyncPlan {
  std::vector<std::string> modify_alt_properties;
  std::vector<std::string> modify_alt_actions;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharIKHeadSavePlan {
  int32_t save_id = 0xF8;
};

struct SourceCharIKSliderMidiState {
  SourceCharWeightableState weightable;
  std::string target;
  std::string first_spot;
  std::string second_spot;
  std::array<float, 3> dest_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> old_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> cur_pos = {0.0f, 0.0f, 0.0f};
  float target_percentage = 1.0f;
  float old_percentage = 0.0f;
  float frac = 0.0f;
  float frac_per_beat = 0.0f;
  bool percentage_changed = false;
  bool reset_all = true;
  std::string character_dir;
  float tolerance = 0.0f;
};

struct SourceCharIKSliderMidiEnterResult {
  bool cleared_percentage_changed = false;
  bool reset_frac = false;
  bool reset_frac_per_beat = false;
  bool call_rnd_pollable_enter = false;
};

struct SourceCharIKSliderMidiSetNameResult {
  bool call_hmx_set_name = false;
  bool assigned_character = false;
};

struct SourceCharIKSliderMidiSetupResult {
  bool reset_all = false;
};

struct SourceCharIKSliderMidiPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharIKSliderMidiLoadSteps {
  int32_t max_revision = 2;
  bool known_revision = false;
  bool load_hmx_object = false;
  bool load_weightable = false;
  bool load_target = false;
  bool load_first_spot = false;
  bool load_second_spot = false;
  bool load_tolerance = false;
};

struct SourceCharIKSliderMidiCopyResult {
  bool copy_hmx_object = false;
  bool copy_weightable = false;
  bool copy_target = false;
  bool copy_first_spot = false;
  bool copy_second_spot = false;
  bool copy_tolerance = false;
};

struct SourceCharIKSliderMidiHandlerPlan {
  std::vector<std::string> actions;
  std::vector<std::string> superclasses;
  int32_t check = 0;
};

struct SourceCharIKSliderMidiPropSyncPlan {
  std::vector<std::string> modify_properties;
  std::vector<std::string> modify_actions;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharIKSliderMidiSavePlan {
  int32_t save_id = 0xC4;
};

struct SourceCharIKMidiState {
  std::string bone;
  std::string cur_spot;
  std::string new_spot;
  bool spot_changed = false;
  bool local_xfm_reset = true;
  bool old_local_xfm_reset = true;
  float frac = 0.0f;
  float frac_per_beat = 0.0f;
  std::string anim_blender;
  float max_anim_blend = 1.0f;
  float anim_frac_per_beat = 0.0f;
  float anim_frac = 0.0f;
};

struct SourceGh2CharIKMidiNewSpotResult {
  float remaining_beats = 0.0f;
  bool snapped = false;
  float fraction = 0.0f;
  float fraction_per_beat = 0.0f;
};

struct SourceGh2CharIKMidiPollResult {
  float delta_beat = 0.0f;
  float fraction = 0.0f;
  float eased_fraction = 0.0f;
};

struct SourceCharIKMidiEnterResult {
  bool clear_cur_spot = true;
  bool clear_new_spot = true;
  bool clear_spot_changed = true;
  bool reset_frac = true;
  bool reset_frac_per_beat = true;
  bool reset_local_xfm = true;
  bool reset_old_local_xfm = true;
  bool call_rnd_pollable_enter = true;
};

struct SourceCharIKMidiPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharIKMidiLoadSteps {
  bool known_revision = false;
  bool load_hmx_object = false;
  bool load_bone = false;
  bool load_legacy_spots = false;
  bool load_legacy_string = false;
  bool load_anim_blend = false;
};

struct SourceCharIKMidiCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharIKMidiHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  std::string check;
};

struct SourceCharIKMidiPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
};

struct SourceCharIKMidiSavePlan {
  int32_t save_id = 0xEA;
};

struct SourceCharLipSyncGeneratorState {
  bool lip_sync_null = true;
  int last_count = 0;
  std::vector<int> weights;
};

struct SourceCharLipSyncState {
  std::string prop_anim;
  std::vector<std::string> visemes;
  int frames = 0;
  std::vector<uint8_t> data;
};

struct SourceCharLipSyncLoadSteps {
  int32_t max_revision = 1;
  bool known_revision = false;
  bool load_hmx_object = false;
  bool load_visemes = false;
  bool load_frames = false;
  bool load_data = false;
  bool load_prop_anim = false;
};

struct SourceCharLipSyncSavePlan {
  int save_id = 0x155;
};

struct SourceCharLipSyncDriverState {
  SourceCharWeightableState weightable;
  std::string lip_sync;
  std::string clips;
  std::string blink_clip;
  std::string song_owner;
  float song_offset = 0.0f;
  bool loop = false;
  bool song_player_null = true;
  std::string bones;
  std::string test_clip;
  float test_weight = 1.0f;
  std::string override_clip;
  float override_weight = 0.0f;
  std::string override_options;
  bool apply_override_additively = false;
  std::string alternate_driver;
};

struct SourceCharLipSyncDriverPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharLipSyncDriverSavePlan {
  int32_t save_id = 0x111;
};

SourceCharWeightSetterState source_char_weight_setter_default_state(
    const std::string& name);
void source_char_weight_setter_set_weight(SourceCharWeightSetterState& state,
                                          float weight);
SourceCharWeightSetterLoadPlan source_char_weight_setter_load_plan(
    int32_t revision);
SourceCharWeightSetterCopyPlan source_char_weight_setter_copy_plan();
SourceCharWeightSetterHandlerPlan source_char_weight_setter_handler_plan();
SourceCharWeightSetterPropSyncPlan source_char_weight_setter_prop_sync_plan();
SourceCharWeightSetterSavePlan source_char_weight_setter_save_plan();
SourceCharWeightSetterRuntimeDumpEvidence
source_char_weight_setter_runtime_dump_evidence();

// Source-backed CharWeightSetter::PollDeps helper. Ref owners are supplied in
// source Refs() order; the helper scans them in reverse like the source body.
void source_char_weight_setter_poll_deps(
    SourceCharWeightSetterPollDeps& deps,
    const CharWeightSetter& setter,
    const std::vector<SourceCharWeightSetterRefOwner>& ref_owners);

SourceCharIKHeadState source_char_ik_head_default_state(
    const std::string& name);
SourceCharIKHeadSetNameResult source_char_ik_head_set_name(
    SourceCharIKHeadState& state,
    const std::string& dir_name,
    bool dir_is_character);
void source_char_ik_head_poll_deps(
    SourceCharIKHeadPollDeps& deps,
    const SourceCharIKHeadState& state,
    const std::vector<std::string>& head_to_spine_parent_chain,
    bool generation_count_nonzero);
SourceCharIKHeadUpdatePointsResult source_char_ik_head_update_points(
    SourceCharIKHeadState& state,
    bool force,
    const std::vector<std::string>& head_to_spine_chain,
    const std::vector<float>& local_lengths);
SourceCharIKHeadLoadSteps source_char_ik_head_load_steps(int32_t revision);
SourceCharIKHeadCopyResult source_char_ik_head_copy(
    SourceCharIKHeadState& dest,
    const SourceCharIKHeadState& source,
    bool shallow_copy,
    float source_owner_weight);
SourceCharIKHeadHandlerPlan source_char_ik_head_handler_plan();
SourceCharIKHeadPropSyncPlan source_char_ik_head_prop_sync_plan();
SourceCharIKHeadSavePlan source_char_ik_head_save_plan();
SourceCharIKSliderMidiState source_char_ik_slider_midi_default_state(
    const std::string& name);
SourceCharIKSliderMidiEnterResult source_char_ik_slider_midi_enter(
    SourceCharIKSliderMidiState& state);
SourceCharIKSliderMidiSetNameResult source_char_ik_slider_midi_set_name(
    SourceCharIKSliderMidiState& state,
    const std::string& dir_name,
    bool dir_is_character);
SourceCharIKSliderMidiSetupResult source_char_ik_slider_midi_setup_transforms(
    SourceCharIKSliderMidiState& state);
void source_char_ik_slider_midi_poll_deps(
    SourceCharIKSliderMidiPollDeps& deps,
    const SourceCharIKSliderMidiState& state);
SourceCharIKSliderMidiLoadSteps source_char_ik_slider_midi_load_steps(
    int32_t revision);
SourceCharIKSliderMidiCopyResult source_char_ik_slider_midi_copy(
    SourceCharIKSliderMidiState& dest,
    const SourceCharIKSliderMidiState& source,
    bool shallow_copy,
    float source_owner_weight);
SourceCharIKSliderMidiHandlerPlan
source_char_ik_slider_midi_handler_plan();
SourceCharIKSliderMidiPropSyncPlan
source_char_ik_slider_midi_prop_sync_plan();
SourceCharIKSliderMidiSavePlan
source_char_ik_slider_midi_save_plan();
SourceCharIKMidiState source_char_ik_midi_default_state();
SourceCharIKMidiEnterResult source_char_ik_midi_enter(
    SourceCharIKMidiState& state);
SourceGh2CharIKMidiNewSpotResult source_gh2_char_ik_midi_new_spot(
    SourceCharIKMidiState& state, const std::string& spot,
    float remaining_beats);
SourceGh2CharIKMidiPollResult source_gh2_char_ik_midi_poll(
    SourceCharIKMidiState& state, float delta_beat);
void source_char_ik_midi_poll_deps(SourceCharIKMidiPollDeps& deps,
                                   const SourceCharIKMidiState& state);
SourceCharIKMidiLoadSteps source_char_ik_midi_load_steps(int32_t revision);
SourceCharIKMidiCopyPlan source_char_ik_midi_copy_plan();
SourceCharIKMidiHandlerPlan source_char_ik_midi_handler_plan();
SourceCharIKMidiPropSyncPlan source_char_ik_midi_prop_sync_plan();
SourceCharIKMidiSavePlan source_char_ik_midi_save_plan();
SourceCharLipSyncGeneratorState source_char_lip_sync_generator_default_state();
SourceCharLipSyncState source_char_lip_sync_default_state();
SourceCharLipSyncLoadSteps source_char_lip_sync_load_steps(int32_t revision);
SourceCharLipSyncSavePlan source_char_lip_sync_save_plan();
SourceCharLipSyncDriverState source_char_lip_sync_driver_default_state(
    const std::string& name);
SourceCharLipSyncDriverSavePlan source_char_lip_sync_driver_save_plan();
void source_char_lip_sync_driver_poll_deps(
    SourceCharLipSyncDriverPollDeps& deps,
    const SourceCharLipSyncDriverState& state);
std::string source_char_lip_sync_driver_clip_dir(
    const SourceCharLipSyncDriverState& state);
std::string source_char_lip_sync_driver_override_dir(
    const SourceCharLipSyncDriverState& state);

struct SourceCharIKRodDefaultState {
  bool left_end_empty = true;
  bool right_end_empty = true;
  float dest_pos = 0.5f;
  bool side_axis_empty = true;
  bool vertical = false;
  bool dest_empty = true;
  bool xfm_identity = true;
};

struct SourceCharIKRodLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharIKRodCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharIKRodHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharIKRodPropSyncPlan {
  std::vector<std::string> modify_alt_properties;
  std::vector<std::string> modify_actions;
};

struct SourceCharIKRodSavePlan {
  int32_t save_id = 0x81;
};

struct SourceCharIKRodPollDeps {
  std::vector<std::string> change;
  std::vector<std::string> changed_by;
};

// Source-backed CharIKRod::ComputeRod/Poll helper. Returns false when any
// source-required endpoint or destination transform is unresolved.
SourceCharIKRodDefaultState source_char_ik_rod_default_state();
SourceCharIKRodLoadPlan source_char_ik_rod_load_plan(int32_t revision);
SourceCharIKRodCopyPlan source_char_ik_rod_copy_plan();
SourceCharIKRodHandlerPlan source_char_ik_rod_handler_plan();
SourceCharIKRodPropSyncPlan source_char_ik_rod_prop_sync_plan();
SourceCharIKRodSavePlan source_char_ik_rod_save_plan();
void source_char_ik_rod_poll_deps(SourceCharIKRodPollDeps& deps,
                                  const CharIKRod& rod);
bool source_char_ik_rod_compute_world(const CharIKRod& rod,
                                      const Character& character,
                                      std::array<float, 16>& dest_world);

struct SourceCharIKHandMeasure {
  bool has_elbow_chain = false;
  float inv_2ab = 0.0f;
  float a2_plus_b2 = 0.0f;
  float aa_plus_bb = 0.0f;
};

struct SourceCharIKHandElbowBendRows {
  bool applied = false;
  std::array<std::array<float, 3>, 3> rows = {};
};

struct SourceCharIKHandLoadPlan {
  int32_t max_revision = 0x0c;
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool calls_set_hand = false;
};

struct SourceCharIKHandCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> member_steps;
  bool creates_copy = true;
};

struct SourceCharIKHandHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  std::string check;
};

struct SourceCharIKHandPropSyncPlan {
  std::vector<std::string> target_properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> properties;
  std::string superclass;
};

struct SourceCharIKHandSavePlan {
  int32_t save_id = 0x2A8;
};

struct SourceCharIKHandSetHandResult {
  std::string assigned_hand;
  bool hand_changed = false;
};

struct SourceCharIKHandTargetInput {
  bool present = true;
  std::array<float, 3> world_pos = {0.0f, 0.0f, 0.0f};
  float extent = 0.0f;
  std::optional<std::array<float, 4>> world_quat;
};

struct SourceCharIKHandTargetBlendResult {
  bool entered = false;
  float sum = 0.0f;
  float adjusted_weight = 0.0f;
  bool reduced_weight_for_low_sum = false;
  std::array<float, 3> blended_pos = {0.0f, 0.0f, 0.0f};
  bool orientation_blended = false;
  bool orientation_normalized = false;
  std::array<float, 4> blended_quat = {0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<float> weights;
};

struct SourceCharIKHandFingerTargetResult {
  bool applied = false;
  milo_scene::Xfm adjusted_target;
};

struct SourceCharIKHandWristConstraintInput {
  bool constrain_wrist = false;
  float char_weight = 0.0f;
  bool has_parent = false;
  float wrist_radians = 0.0f;
  std::array<float, 3> parent_x = {1.0f, 0.0f, 0.0f};
  std::array<float, 3> hand_x = {1.0f, 0.0f, 0.0f};
  std::array<float, 3> hand_y = {0.0f, 1.0f, 0.0f};
  std::array<float, 3> hand_z = {0.0f, 0.0f, 1.0f};
  std::array<float, 3> hand_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> finger_before_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> finger_after_first_set_pos = {0.0f, 0.0f, 0.0f};
};

struct SourceCharIKHandWristConstraintResult {
  bool entered = false;
  bool angle_exceeded = false;
  float raw_angle = 0.0f;
  float correction_angle = 0.0f;
  std::array<float, 3> corrected_x = {1.0f, 0.0f, 0.0f};
  std::array<float, 3> corrected_y = {0.0f, 1.0f, 0.0f};
  std::array<float, 3> corrected_z = {0.0f, 0.0f, 1.0f};
  bool wrote_first_hand_xfm = false;
  bool compensated_finger_delta = false;
  std::array<float, 3> finger_delta = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> final_hand_pos = {0.0f, 0.0f, 0.0f};
  bool updates_world_dst = false;
  bool requests_elbow_resolve = false;
  bool rewrites_hand_after_elbow = false;
};

struct SourceCharIKHandElbowSwingInput {
  float elbow_swing = 0.0f;
  std::array<float, 2> current_yz = {0.0f, 0.0f};
  std::array<float, 2> target_yz = {0.0f, 0.0f};
};

struct SourceCharIKHandElbowSwingResult {
  bool entered = false;
  float current_len_sq = 0.0f;
  float target_len_sq = 0.0f;
  float denom = 0.0f;
  float cross = 0.0f;
  float unclamped = 0.0f;
  float clamped = 0.0f;
  float rotate_about_x = 0.0f;
  bool recompute_current_after_rotation = false;
};

struct SourceCharIKHandElbowCollisionInput {
  bool has_elbow_collide = false;
  bool collide_shape_is_sphere = false;
  float distance_to_elbow = 0.0f;
  float sphere_radius = 0.0f;
  bool clockwise = false;
};

struct SourceCharIKHandElbowCollisionResult {
  bool entered = false;
  bool needs_source_shoulder_offset = false;
  bool warn_non_sphere = false;
  bool sphere_branch = false;
  bool inside_sphere = false;
  bool needs_collision_rotation = false;
  bool uses_counterclockwise_candidate = false;
  bool uses_clockwise_candidate = false;
  bool updates_upper_arm_matrix = false;
  bool updates_forearm_matrix = false;
  bool final_shoulder_repull = true;
};

struct SourceCharIKHandPollFlowInput {
  bool has_hand = false;
  bool has_targets = false;
  bool has_parent = false;
  bool has_grandparent = false;
  bool move_elbow = true;
  bool always_ik_elbow = false;
  bool orientation = true;
  bool stretch = true;
  float char_weight = 0.0f;
};

struct SourceCharIKHandPollFlowResult {
  bool early_out = false;
  bool parent1_initial = false;
  bool parent1_after_move_elbow = false;
  bool parent2_resolved = false;
  bool parent1_after_grandparent_gate = false;
  bool calls_ik_elbow = false;
  bool ik_elbow_has_chain = false;
  bool final_hand_write = false;
  bool final_position_from_world_dst = false;
  bool final_orientation_from_target = false;
  bool interpolates_orientation = false;
};

struct SourceCharIKFootState {
  bool helper_target_created = true;
  bool helper_target_local_reset = true;
  int fsm_state = 0;
  std::string data;
  int data_index = 0;
  std::array<float, 3> planted_pos = {0.0f, 0.0f, 0.0f};
  float release_distance = 0.0f;
  std::string character_dir;
};

struct SourceCharIKFootEnterResult {
  bool reset_fsm_state = false;
  bool reset_release_distance = false;
};

struct SourceCharIKFootSetNameResult {
  bool call_hmx_set_name = false;
  bool assigned_character = false;
};

struct SourceCharIKFootPollPlan {
  bool should_poll = false;
  bool clear_targets_before = false;
  bool push_helper_target = false;
  bool run_do_fsm = false;
  bool call_char_ik_hand_poll = false;
  bool clear_targets_after = false;
};

struct SourceCharIKFootPollDepsPlan {
  bool call_char_ik_hand_poll_deps = false;
};

struct SourceCharIKFootFsmResult {
  bool copied_finger_matrix = false;
  bool clamped_negative_delta = false;
  bool planted = false;
  bool returned_from_planted_state = false;
  std::array<float, 3> target_pos = {0.0f, 0.0f, 0.0f};
  int fsm_state = 0;
  float release_distance = 0.0f;
};

struct SourceCharIKFootLoadSteps {
  int32_t max_revision = 6;
  bool known_revision = false;
  bool load_char_ik_hand = false;
  bool read_legacy_symbol = false;
  int legacy_int_reads = 0;
  bool load_data = false;
  bool load_data_index = false;
};

struct SourceCharIKFootCopyResult {
  bool copy_char_ik_hand = false;
  bool copy_data = false;
  bool copy_data_index = false;
};

struct SourceCharIKFootHandlerPlan {
  std::vector<std::string> superclasses;
  int32_t check = 0;
};

struct SourceCharIKFootPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharIKFootSavePlan {
  int32_t save_id = 0x138;
};

// Source-backed CharIKHand::MeasureLengths / IKElbow scalar helper. The length
// inputs correspond to mHand->mLocalXfm.v and mHand->TransParent()->mLocalXfm.v.
SourceCharIKHandLoadPlan source_char_ik_hand_load_plan(int32_t revision);
SourceCharIKHandCopyPlan source_char_ik_hand_copy_plan();
SourceCharIKHandHandlerPlan source_char_ik_hand_handler_plan();
SourceCharIKHandPropSyncPlan source_char_ik_hand_prop_sync_plan();
SourceCharIKHandSavePlan source_char_ik_hand_save_plan();
SourceCharIKHandSetHandResult source_char_ik_hand_set_hand(
    const std::string& hand);
SourceCharIKHandMeasure source_char_ik_hand_measure_lengths(
    bool has_elbow_chain,
    float hand_local_len,
    float parent_local_len);
bool source_char_ik_hand_update_measure_lengths(bool scalable,
                                                bool& hand_changed);
bool source_char_ik_hand_elbow_cosine(
    const SourceCharIKHandMeasure& measure,
    float distance_squared,
    float& out_cosine);
float source_gh2_ps2_char_ik_hand_elbow_cosine(float source_cosine);
SourceCharIKHandElbowBendRows source_char_ik_hand_elbow_bend_rows(
    float cosine, float sine);
SourceCharIKHandTargetBlendResult source_char_ik_hand_multi_target_blend(
    float char_weight,
    const std::vector<SourceCharIKHandTargetInput>& targets,
    bool orientation = false);
SourceCharIKHandFingerTargetResult source_char_ik_hand_finger_target(
    bool has_finger,
    const milo_scene::Xfm& hand_world,
    const milo_scene::Xfm& finger_world,
    std::array<float, 3> target_pos,
    std::array<float, 4> target_quat);
SourceCharIKHandWristConstraintResult source_char_ik_hand_wrist_constraint(
    const SourceCharIKHandWristConstraintInput& input);
SourceCharIKHandElbowSwingResult source_char_ik_hand_elbow_swing(
    const SourceCharIKHandElbowSwingInput& input);
SourceCharIKHandElbowCollisionResult source_char_ik_hand_elbow_collision_gate(
    const SourceCharIKHandElbowCollisionInput& input);
SourceCharIKHandPollFlowResult source_char_ik_hand_poll_flow(
    const SourceCharIKHandPollFlowInput& input);
SourceCharIKFootState source_char_ik_foot_default_state();
SourceCharIKFootEnterResult source_char_ik_foot_enter(
    SourceCharIKFootState& state);
SourceCharIKFootSetNameResult source_char_ik_foot_set_name(
    SourceCharIKFootState& state,
    const std::string& dir_name,
    bool dir_is_character);
SourceCharIKFootPollPlan source_char_ik_foot_poll_plan(
    bool has_finger,
    bool has_hand,
    bool has_data);
SourceCharIKFootPollDepsPlan source_char_ik_foot_poll_deps_plan();
SourceCharIKFootFsmResult source_char_ik_foot_do_fsm(
    SourceCharIKFootState& state,
    const std::array<float, 3>& current_target_pos,
    const std::array<float, 3>& finger_world_pos,
    float data_value,
    float delta_seconds,
    bool character_teleported);
SourceCharIKFootLoadSteps source_char_ik_foot_load_steps(int32_t revision);
SourceCharIKFootCopyResult source_char_ik_foot_copy(
    SourceCharIKFootState& dest,
    const SourceCharIKFootState& source);
SourceCharIKFootHandlerPlan source_char_ik_foot_handler_plan();
SourceCharIKFootPropSyncPlan source_char_ik_foot_prop_sync_plan();
SourceCharIKFootSavePlan source_char_ik_foot_save_plan();

struct SourceCharBoneOffsetSavePlan {
  int32_t save_id = 0x5E;
};

struct SourceCharBoneOffsetPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharBoneTwistSavePlan {
  int32_t save_id = 0x59;
};

struct SourceCharBoneTwistPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharForeTwistSavePlan {
  int32_t save_id = 0x79;
};

struct SourceCharUpperTwistSavePlan {
  int32_t save_id = 0x5D;
};

struct SourceCharForeTwistPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharUpperTwistPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

// Source-backed CharBoneOffset::Poll helper. Returns false when the source
// object pointer or its parent transform would be missing.
SourceCharBoneOffsetSavePlan source_char_bone_offset_save_plan();
void source_char_bone_offset_poll_deps(
    SourceCharBoneOffsetPollDeps& deps,
    const std::string& dest,
    const std::string& dest_parent);
bool source_char_bone_offset_poll_world(
    const CharBoneOffset& offset,
    bool has_dest,
    bool has_parent,
    const milo_scene::Xfm& dest_local,
    const std::array<float, 16>& parent_world,
    std::array<float, 16>& dest_world);
void source_char_bone_offset_apply_to_local(const CharBoneOffset& offset,
                                            milo_scene::Xfm& dest_local);

// Source-backed CharBoneTwist::Poll/PollDeps helpers. Poll returns false when
// the source bone or target list would be missing.
SourceCharBoneTwistSavePlan source_char_bone_twist_save_plan();
void source_char_bone_twist_poll_deps(
    SourceCharBoneTwistPollDeps& deps,
    const std::string& bone,
    const std::vector<std::string>& targets);
float source_char_bone_twist_weight(
    const CharBoneTwist& twist,
    const std::unordered_map<std::string, float>& weights_by_name);
bool source_char_bone_twist_poll_world(
    const CharBoneTwist& twist,
    bool has_bone,
    const std::array<float, 16>& bone_world,
    const std::vector<std::array<float, 16>>& target_worlds,
    const std::unordered_map<std::string, float>& weights_by_name,
    std::array<float, 16>& out_world);

struct SourceCharForeTwistPollWorldResult {
  bool applied = false;
  float source_angle_radians = 0.0f;
  float applied_rotation_radians = 0.0f;
  float twist2_position_ratio = 0.0f;
  std::array<float, 16> twist_parent_world = {};
  std::array<float, 16> twist2_world = {};
};

struct SourceCharUpperTwistPollWorldResult {
  bool applied = false;
  std::array<float, 16> twist1_world = {};
  std::array<float, 16> twist2_world = {};
};

struct SourceGh2TraceForeTwistLocalResult {
  bool applied = false;
  float roll_radians = 0.0f;
  milo_scene::Xfm twist1_local;
  milo_scene::Xfm twist2_local;
};

struct SourceGh2TraceUpperTwistLocalResult {
  bool applied = false;
  bool serial_chain = false;
  float roll_radians = 0.0f;
  float twist1_factor = 0.0f;
  float twist2_factor = 0.0f;
  milo_scene::Xfm twist1_local;
  milo_scene::Xfm twist2_local;
};

// Source-backed CharForeTwist::Poll/PollDeps and
// CharUpperTwist::Poll/PollDeps helpers. These are pure translations of the
// ihatecompvir routines; callers remain responsible for resolving object
// pointers and publishing SetWorldXfm results as live world-cache rows.
SourceCharForeTwistSavePlan source_char_fore_twist_save_plan();
void source_char_fore_twist_poll_deps(
    SourceCharForeTwistPollDeps& deps,
    const std::string& hand,
    const std::string& twist2,
    bool has_twist2,
    const std::string& twist2_parent);
bool source_char_fore_twist_poll_world(
    const CharForeTwist& twist,
    bool has_hand,
    bool has_twist2,
    bool has_hand_parent,
    bool has_twist2_parent,
    const std::array<float, 16>& hand_parent_world,
    const std::array<float, 16>& hand_world,
    float hand_local_x,
    float twist2_local_x,
    SourceCharForeTwistPollWorldResult& out);
SourceCharUpperTwistSavePlan source_char_upper_twist_save_plan();
void source_char_upper_twist_poll_deps(
    SourceCharUpperTwistPollDeps& deps,
    const std::string& upper_arm,
    const std::string& twist1,
    const std::string& twist2);
bool source_char_upper_twist_poll_world(
    bool has_source,
    bool has_twist1,
    bool has_twist2,
    bool has_source_parent,
    const std::array<float, 16>& source_parent_world,
    const std::array<float, 16>& source_world,
    const std::array<float, 16>& twist1_current_world,
    const std::array<float, 16>& twist2_current_world,
    SourceCharUpperTwistPollWorldResult& out);

// GH2 PS2 trace-local runtime helpers for the stock guitarist twist rows.
// They keep the RB3 source helpers above as documentation/testing authority,
// but publish the row shapes captured from GH2 SLUS traces for the live GH2
// runtime path.
float source_gh2_trace_local_twist_angle(const milo_scene::Xfm& source);
void source_gh2_trace_write_x_twist(milo_scene::Xfm& dst,
                                    const milo_scene::Xfm& basis,
                                    float angle);
bool source_gh2_trace_fore_twist_poll_local(
    const CharForeTwist& twist,
    bool has_hand,
    bool has_forearm,
    bool has_twist1,
    bool has_twist2,
    const milo_scene::Xfm& hand_local,
    const milo_scene::Xfm& forearm_live_local,
    const milo_scene::Xfm& twist2_bind_local,
    SourceGh2TraceForeTwistLocalResult& out);
bool source_gh2_trace_upper_twist_poll_local(
    bool has_source,
    bool has_twist1,
    bool has_twist2,
    bool twist2_parent_is_twist1,
    const milo_scene::Xfm& upper_live_local,
    const milo_scene::Xfm& twist2_bind_local,
    SourceGh2TraceUpperTwistLocalResult& out);

// Source-backed CharHair::FreezePoseRaw helper. Writes current runtime point
// positions back into point.unk5c in the strand root-parent local basis.
int source_char_hair_freeze_pose_raw(Character& character, CharHair& hair,
                                     SourceCharHairRuntime& state);

// Compatibility helper for callers that only need the stored clip names.
std::vector<std::string> load_clip_group_names(
    const std::string& hdr_path, const std::string& ark_path,
    const std::vector<std::string>& milo_paths,
    const std::string& group_name);

// Apply one frame of a clip to the character's bone local matrices.
// frame_idx is clamped to [0, frames.size()-1].
void apply_clip_frame(const CharClip& clip, int frame_idx, Character& character);
void apply_clip_frame_weighted(const CharClip& clip, int frame_idx,
                               float weight, Character& character);

// Publishes an already-materialized GH2 typed pose through the same exact
// AcquirePose -> PoseMeshes target resolver used by normal CharClip output.
// The caller must provide the decoded CharBone output inventory that authorizes
// each target; unlisted sample channels and missing targets remain unwritten.
bool apply_materialized_typed_pose(
    const std::vector<ClipChannel>& channels,
    const std::vector<CharClip::OutputBone>& output_bones,
    Character& character);

// Rebase an absolute local-space clip from its authored output-bone bind pose
// onto another character's matching transform graph. This is name-driven and
// leaves missing targets untouched; it does not invent bone aliases.
struct ClipRetargetAudit {
  size_t source_outputs = 0;
  size_t matched_outputs = 0;
  size_t frames = 0;
  size_t channels = 0;
  size_t nonfinite_values = 0;
  float max_abs_position = 0.0f;
  float max_scale = 0.0f;
};
ClipRetargetAudit retarget_clip_to_character(CharClip& clip,
                                             Character& source_character,
                                             Character& target_character);

// Install the animation owner's hand-controller semantics onto a differently
// proportioned target rig. Controllers are admitted only when the target has
// the exact hand chain and instrument transforms they reference. Existing
// target-authored controllers win; no aliases or numeric offsets are created.
struct ExternalRetargetGraphAudit {
  size_t installed_ik_hands = 0;
  size_t retained_target_ik_hands = 0;
  size_t skipped_missing_hand_chain = 0;
  size_t skipped_missing_target = 0;
  size_t installed_ik_midis = 0;
  size_t installed_hand_drivers = 0;
  size_t installed_weight_setters = 0;
  size_t installed_upper_twists = 0;
  size_t installed_fore_twists = 0;
  size_t normalized_attachment_roots = 0;
  size_t orientation_corrected_ik_hands = 0;
  size_t contact_corrected_ik_hands = 0;
  float mean_arm_reach_ratio = 1.0f;
};
ExternalRetargetGraphAudit install_external_retarget_controller_graph(
    const Character& source_character, Character& target_character);

// Apply decoded character-level controllers that sit outside CharClipSamples.
// Call after clip poses for the frame.
void apply_character_controllers(Character& character, float time_seconds);

void clear_runtime_ik_weights(Character& character);
void set_runtime_ik_weight(Character& character, const std::string& weight_prop,
                           float weight);
void set_runtime_driver_evaluate_flags(Character& character,
                                       const std::string& driver_name,
                                       uint32_t flags,
                                       float weight);
void clear_runtime_trans_worlds(Character& character);

// CharIKMidi bridge. GHDX/PS2 player*_fret_pos maps MIDI pitches 40..59 to
// spot_neck_fret01..20 and feeds the character's fret.ik object.
void apply_ik_midi_fret_target(Character& character,
                               const std::string& spot_name,
                               float current_beat,
                               float delta_beat,
                               float target_beat,
                               float event_beat);

// Legacy single-frame helpers kept for --clip screenshot mode.
std::vector<ClipChannel> load_clip_pose(const std::string& hdr_path,
                                        const std::string& ark_path,
                                        const std::string& milo_path,
                                        const std::string& clip_name);
void apply_clip_pose(const std::vector<ClipChannel>& channels, Character& character);
void apply_clip_pose_weighted(const std::vector<ClipChannel>& channels,
                              float weight, Character& character,
                              bool relative = false);
void apply_clip_pose_sampled(const std::vector<ClipChannel>& channels,
                             float weight, Character& character,
                             bool relative = false);
std::vector<ClipChannel> blend_channel_layers(
    const std::vector<ClipChannelLayer>& layers);
void apply_clip_channel_layers(const std::vector<ClipChannelLayer>& layers,
                               Character& character, bool relative = false);

}  // namespace ghogx::character
