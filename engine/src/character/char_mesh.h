// engine/src/character/char_mesh.h
//
// Decode a GH2 BandCharacter MILO into drawable, SKINNED 3-D geometry: the band
// member's body meshes, the bone skeleton, the per-mesh bone palette + bind
// matrices, and the materials/textures (skin / outfit / face / hair).
//
// Why a separate decoder from milo_scene::decode_mesh:
//   GH2 venue/prop meshes are STATIC (the 48-byte vertex's last 4 floats are a
//   vertex colour). Character meshes are SKINNED and reuse the SAME 48-byte
//   stride, but those 4 floats are LINEAR-BLEND BONE WEIGHTS (they sum to 1.0),
//   and a BONE TABLE (palette names + bind matrices) follows the face list.
//   Treating the weights as colour tints the character with garbage, so the
//   character path must decode them as weights and a BONE PALETTE.
//
// The mesh decoder follows ihatecompvir's MiloLib/RB3 source names for
// ObjectFields, RndTrans, RndDrawable, and RndMesh. GH2 PS2 character dirs are
// version 24, so object superclasses carry ObjectFields; most stock rows happen
// to have an empty subtype/root, but the parser consumes the fields rather than
// treating them as anonymous padding.
//
//   Skinned Mesh (version 0x1c = 28) — identical header to a static Mesh:
//     Object     : ObjectFields (combined object revision, subtype Symbol,
//                  root DTB parent, optional note)
//     Trans base : combined RndTrans revision, local matrix, world matrix,
//                  optional rev<9 child list, constraint, target,
//                  preserve-scale, parent Symbol
//     Draw  base : i32 ver(3) + 21 bytes (showing flag + bounding sphere +
//                  draw-order)
//     str   material name
//     str   geometry-owner name
//     9     bytes
//     i32   vertex_count
//     verts : vertex_count × 48 bytes, each =
//                position (3×f32) + normal (3×f32) + WEIGHTS (4×f32, sum=1) +
//                uv (2×f32)
//     i32   face_count
//     faces : face_count × (3 × u16)
//     --- skinning tail (this is what static meshes lack) ---
//     ...   groupSizes / patch data
//     bones : for rev < 33, exactly four old-style RndMesh::BoneTransform
//             Symbol rows followed by four transform rows; these raw rows are
//             kept for audit, then converted to the source runtime active bone
//             list by trimming at the first null/unresolved row.
//     bind  : one 3x4 RndBone offset row per source palette slot.
//     groups: for last-gen parent dirs before revision 25, source GroupSection
//             rows follow when groupSizes is non-empty and starts above zero.
//
//   Bones are the BandCharacter dir's Trans entries named "bone_*"/"spot_*".
//   Their composed parent chain gives each bone's bind-pose WORLD matrix.
//
// IMPORTANT (bind pose): ihatecompvir's RB3 RndMesh::SetBone computes each
// offset row as mesh WorldXfm * inverse(bone WorldXfm). The render path
// consumes it as vertex * offset * current bone WorldXfm. The decoder keeps
// raw MILO bone-transform rows for audit, while the active palette mirrors
// source runtime ObjPtr semantics by trimming at the first null/unresolved row.

#pragma once

#include "milo_scene/milo_scene.h"  // Xfm, TransObj, MatObj

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ghogx::character {

// GH1/GH2 PS2 mesh revisions serialize one shared four-float slot. After bone
// resolution it remains signed linear-blend weights for skinned geometry or is
// packed through Hmx::Color32 for unskinned vertex color, so alias both views
// without changing the packed 48-byte source stride.
struct SkinVertex {
  float px, py, pz;
  float nx, ny, nz;
  union {
    float w[4];       // skinned: w[i] applies to palette bone i
    struct {
      float r, g, b, a;  // unskinned: packed Hmx::Color32 channels
    };
  };
  float u, v;
};
static_assert(sizeof(SkinVertex) == 48,
              "GH2 PS2 skinned vertex stride must be 48 bytes");

struct SourceRndMeshVertLoadPlan {
  int32_t mesh_revision = 0;
  bool reads_position = true;
  bool reads_legacy_weight_pair = false;
  bool reads_normal = true;
  bool reads_color = true;
  bool reads_uv = true;
  bool reads_separate_weights = false;
  bool computes_legacy_pair_weights = false;
  bool reads_legacy_extra_vec2 = false;
  bool reads_bone_indices = false;
  bool reads_post_indices_vec4 = false;
  bool quantizes_unskinned_color32 = false;
  bool preserves_signed_skinned_float_weights = false;
  bool postload_color_to_weights = false;
  bool postload_clears_color = false;
  bool gh2_rev28_color_payload_is_skin_weights = false;
};

SourceRndMeshVertLoadPlan source_rndmesh_vert_load_plan(
    int32_t mesh_revision,
    bool is_skinned);

struct SourceRndMeshBoneTailPlan {
  int32_t mesh_revision = 0;
  bool reads_new_bone_vector = false;
  bool clamps_new_bone_vector_to_max = false;
  bool reads_old_first_bone = false;
  bool clears_when_first_bone_null = false;
  bool resizes_old_bones_to_four = false;
  bool reads_old_slots_1_to_3 = false;
  bool reads_four_old_offsets = false;
  bool recomputes_pre25_legacy_weights = false;
  bool older_parent_or_self_slot0_path = false;
  bool trims_old_slots_at_first_null = false;
  bool calls_remove_invalid_bones = true;
  bool calls_zero_weight_fixup = false;
  bool gh2_rev28_old_four_slot_tail = false;
  int32_t active_bone_count = 0;
};

SourceRndMeshBoneTailPlan source_rndmesh_bone_tail_plan(
    int32_t mesh_revision,
    const std::vector<bool>& resolved_slots);

struct SourceMiloEditorRndMeshRevisionWordPlan {
  uint32_t combined_word = 0;
  uint16_t revision = 0;
  uint16_t alt_revision = 0;
  uint32_t written_word = 0;
  bool host_little_endian = true;
  bool read_low_word_as_revision = false;
  bool read_low_word_as_alt_revision = false;
  bool write_alt_revision_high_word = false;
  bool write_revision_high_word = false;
};

SourceMiloEditorRndMeshRevisionWordPlan
source_milo_editor_rndmesh_revision_word_plan(
    uint32_t combined_word,
    uint16_t revision_to_write,
    uint16_t alt_revision_to_write,
    bool host_little_endian);

struct SourceMiloEditorRndMeshNewPlan {
  int32_t mesh_revision = 0;
  int32_t alt_revision = 0;
  uint32_t requested_vertex_count = 0;
  uint32_t requested_face_count = 0;
  bool sets_revision = true;
  bool sets_alt_revision = true;
  bool ignores_requested_vertex_count = true;
  bool ignores_requested_face_count = true;
  bool leaves_vertices_default_constructed = true;
  bool leaves_faces_empty = true;
  bool factory_only_sets_revision_fields = true;
  bool gh2_rev28_factory_is_revision_only = false;
};

SourceMiloEditorRndMeshNewPlan source_milo_editor_rndmesh_new_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    uint32_t requested_vertex_count,
    uint32_t requested_face_count);

struct SourceMiloEditorRndMeshBoneTransformIoPlan {
  int32_t mesh_revision = 0;
  int32_t input_bone_transform_count = 0;
  bool read_uses_presence_probe = true;
  bool read_rewinds_probe_when_positive = false;
  bool read_skips_bone_block_when_probe_nonpositive = false;
  bool read_modern_counted_vector = false;
  bool read_legacy_four_names_then_four_transforms = false;
  int32_t read_legacy_slot_count = 0;
  bool bone_transform_row_is_symbol_then_matrix = true;
  bool write_modern_counted_vector = false;
  bool write_legacy_pads_to_four_when_nonempty = false;
  bool write_legacy_four_names_then_four_transforms = false;
  bool write_legacy_zero_sentinel_when_empty = false;
  int32_t write_serialized_slot_count = 0;
};

SourceMiloEditorRndMeshBoneTransformIoPlan
source_milo_editor_rndmesh_bone_transform_io_plan(
    int32_t mesh_revision,
    bool read_probe_positive,
    int32_t bone_transform_count);

struct SourceMiloEditorRndMeshFaceIoPlan {
  int32_t face_count = 0;
  bool row_is_three_uint16_indices = true;
  bool reads_face_count_before_rows = true;
  bool writes_face_count_before_rows = true;
  bool reads_faces_in_count_order = false;
  bool writes_faces_in_vector_order = false;
  int32_t read_face_rows = 0;
  int32_t write_face_rows = 0;
};

SourceMiloEditorRndMeshFaceIoPlan source_milo_editor_rndmesh_face_io_plan(
    int32_t face_count);

struct SourceMiloEditorRndMeshVertexIoPlan {
  int32_t mesh_version = 0;
  int32_t vertex_count = 0;
  bool reads_vertex_count_before_rows = true;
  bool writes_vertex_count_before_rows = true;
  bool reads_next_gen_header = false;
  bool writes_next_gen_header = false;
  bool next_gen_header_has_vertex_size_and_compression = false;
  bool uses_last_gen_uncompressed_rows = false;
  bool row_layout_freq_le10 = false;
  bool row_layout_bones_first_11_to_22 = false;
  bool row_layout_modern_uncompressed_23_plus = false;
  bool row_layout_packed_uncompressed_38_plus = false;
  bool row_reads_position_xyz = true;
  bool row_reads_position_w = false;
  bool row_reads_normal_xyz = false;
  bool row_reads_normal_w = false;
  bool row_reads_weights = false;
  bool row_reads_weights_before_uv = false;
  bool row_reads_uv_before_weights = false;
  bool row_reads_uv = false;
  bool row_reads_bone_indices = false;
  bool row_reads_bone_indices_before_normal = false;
  bool row_reads_tangent = false;
  bool row_reads_tangent_unknown_float_pair = false;
  bool row_reads_pre_normal_packed_pairs = false;
  bool row_reads_post_bone_packed_pairs = false;
  bool row_reads_packed_next_gen = false;
  int32_t row_float_count = 0;
  int32_t row_uint32_count = 0;
  int32_t row_uint16_count = 0;
  int32_t row_byte_size = 0;
  int32_t read_vertex_rows = 0;
  int32_t write_vertex_rows = 0;
  bool gh2_rev28_row_is_skin_vertex_48 = false;
};

SourceMiloEditorRndMeshVertexIoPlan
source_milo_editor_rndmesh_vertex_io_plan(
    int32_t mesh_version,
    bool is_next_gen,
    int32_t vertex_count);

struct SourceMiloEditorRndMeshCompressedVertexIoPlan {
  int32_t mesh_version = 0;
  bool is_next_gen = false;
  int32_t compression_type = 0;
  bool uses_next_gen_compressed_branch = false;
  bool compression1_xbox_layout = false;
  bool compression2_ps3_layout = false;
  bool reads_rgba_color_word = false;
  bool writes_rgba_color_word = false;
  bool reads_argb_color_word = false;
  bool writes_argb_color_word = false;
  bool reads_half_uv = false;
  bool writes_half_uv = false;
  bool reads_signed_compressed_vec4_normals = false;
  bool writes_signed_compressed_vec4_normals = false;
  bool reads_signed_compressed_vec4_tangents = false;
  bool writes_signed_compressed_vec4_tangents = false;
  bool reads_unsigned_compressed_vec4_weights = false;
  bool writes_unsigned_compressed_vec4_weights = false;
  bool reads_ps3_signed_compressed_vec3_normals = false;
  bool writes_ps3_signed_compressed_vec3_normals = false;
  bool reads_ps3_signed_compressed_vec3_tangents = false;
  bool writes_ps3_signed_compressed_vec3_tangents = false;
  bool reads_ps3_unsigned_compressed_vec3_weights = false;
  bool writes_ps3_unsigned_compressed_vec3_weights = false;
  bool reads_bone_indices_as_bytes = false;
  bool writes_bone_indices_as_bytes = false;
  bool reads_bone_indices_as_uint16 = false;
  bool writes_bone_indices_as_uint16_with_byte_cast = false;
  int32_t bone_index_storage_bytes_per_slot = 0;
  bool unsupported_compression_type = false;
  bool gh2_rev28_is_not_next_gen_compressed = false;
};

SourceMiloEditorRndMeshCompressedVertexIoPlan
source_milo_editor_rndmesh_compressed_vertex_io_plan(
    int32_t mesh_version,
    bool is_next_gen,
    int32_t compression_type);

struct SourceMiloEditorCompressedVectorBoundary {
  bool rndmesh_call_sites_source_backed = true;
  bool milo_classes_source_present = false;
  bool can_port_call_order = true;
  bool can_port_bit_packing_math = false;
  bool safe_to_decode_signed_compressed_values = false;
  bool safe_to_decode_unsigned_compressed_values = false;
  bool safe_to_decode_ps3_compressed_values = false;
  bool safe_to_treat_compressed_vector_names_as_math = false;
  std::vector<std::string> call_sites;
  std::vector<std::string> missing_helpers;
};

SourceMiloEditorCompressedVectorBoundary
source_milo_editor_compressed_vector_boundary();

struct SourceRndMeshSkinIndexPlan {
  bool rb3_stream_reads_bone_indices = false;
  bool milo_editor_reads_bone_indices = false;
  bool zero_weight_fixup_runs = false;
  bool gh2_legacy_slots_without_serialized_indices = false;
};

SourceRndMeshSkinIndexPlan source_rndmesh_skin_index_plan(
    int32_t mesh_revision);

struct SourceRndMeshSkinRuntimeBoundary {
  bool latest_header_declares_skin_vertex = true;
  bool latest_header_declares_remove_invalid_bones = true;
  bool latest_header_declares_has_valid_bones = true;
  bool latest_cpp_calls_skin_vertex_from_collide_showing = true;
  bool latest_cpp_calls_remove_invalid_bones_from_post_load = true;
  bool latest_cpp_uses_has_valid_bones_prop_sync = true;
  bool latest_cpp_has_skin_vertex_body = false;
  bool latest_cpp_has_remove_invalid_bones_body = false;
  bool latest_cpp_has_has_valid_bones_body = false;
  bool rb2_dump_has_skin_vertex_range = false;
  bool rb2_dump_has_remove_invalid_bones_range = false;
  bool rb2_dump_has_has_valid_bones_range = false;
  bool native_skin_to_pose_uses_source_offset_order = true;
  bool safe_to_claim_source_skin_vertex_body = false;
  bool safe_to_import_remove_invalid_bones = false;
  bool safe_to_rewrite_skinning_from_dump = false;
};

SourceRndMeshSkinRuntimeBoundary source_rndmesh_skin_runtime_boundary();

struct SourceRndMeshFieldGatePlan {
  int32_t mesh_revision = 0;
  int32_t alt_revision = 0;
  int32_t parent_dir_revision = 0;
  bool reads_material = true;
  bool reads_second_material = false;
  bool reads_geom_owner = true;
  bool reads_alt_geom_owner = false;
  bool reads_trans_parent = false;
  bool reads_unknown_trans_refs = false;
  bool reads_unknown_vector3 = false;
  bool reads_legacy_sphere = false;
  bool reads_legacy_bool = false;
  bool reads_unknown_symbol_float = false;
  bool reads_legacy_bool1 = false;
  bool reads_mutable = false;
  bool reads_volume = false;
  bool reads_bsp_node = false;
  bool reads_rev7_bool = false;
  bool reads_legacy_int = false;
  bool reads_vertices = true;
  bool reads_faces = true;
  bool reads_group_sizes_modern = false;
  bool reads_patch_vector_loop_legacy = false;
  bool reads_group_sizes_legacy = false;
  bool uses_bone_block_presence_probe = true;
  bool reads_modern_bone_transform_vector = false;
  bool reads_old_four_bone_names_and_offsets = false;
  bool striper_block_todo = false;
  bool legacy_usvec_todo = false;
  bool revision_zero_todo = false;
  bool reads_keep_mesh_data = false;
  bool reads_has_ao_calculation = false;
  bool reads_no_quant = false;
  bool reads_alt_bool3 = false;
  bool reads_group_sections = false;
};

SourceRndMeshFieldGatePlan source_rndmesh_field_gate_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    int32_t parent_dir_revision,
    int32_t group_sizes_count,
    bool group_sizes_first_positive);

struct SourceMiloEditorRndMeshCoreFieldsIoPlan {
  int32_t mesh_revision = 0;
  bool reads_material_symbol = true;
  bool writes_material_symbol = true;
  bool reads_second_material_symbol = false;
  bool writes_second_material_symbol = false;
  bool reads_geom_owner_symbol = true;
  bool writes_geom_owner_symbol = true;
  bool reads_alt_geom_owner_symbol = false;
  bool writes_alt_geom_owner_symbol = false;
  bool reads_trans_parent_symbol = false;
  bool writes_trans_parent_symbol = false;
  bool reads_unknown_transform_refs = false;
  bool writes_unknown_transform_refs = false;
  bool reads_unknown_vector3 = false;
  bool writes_unknown_vector3 = false;
  bool reads_legacy_sphere = false;
  bool writes_legacy_sphere = false;
  bool reads_legacy_bool = false;
  bool writes_legacy_bool = false;
  bool reads_unknown_symbol_float = false;
  bool writes_unknown_symbol_float = false;
  bool reads_legacy_bool1 = false;
  bool writes_legacy_bool1 = false;
  bool reads_mutable_uint32 = false;
  bool writes_mutable_uint32 = false;
  bool reads_volume_uint32 = false;
  bool writes_volume_uint32 = false;
  bool reads_bsp_node = false;
  bool writes_bsp_node = false;
  bool reads_rev7_bool = false;
  bool writes_rev7_bool = false;
  bool reads_legacy_int = false;
  bool writes_legacy_int = false;
  int32_t read_symbol_count = 0;
  int32_t write_symbol_count = 0;
  int32_t read_bool_count = 0;
  int32_t write_bool_count = 0;
  int32_t read_uint32_count = 0;
  int32_t write_uint32_count = 0;
  bool gh2_rev28_core_is_mat_geom_mutable_volume_bsp = false;
};

SourceMiloEditorRndMeshCoreFieldsIoPlan
source_milo_editor_rndmesh_core_fields_io_plan(int32_t mesh_revision);

struct SourceMiloEditorRndMeshEnumPlan {
  uint32_t mutable_none = 0;
  uint32_t mutable_verts = 31;
  uint32_t mutable_faces = 32;
  uint32_t mutable_all = 63;
  uint32_t volume_empty = 0;
  uint32_t volume_triangles = 1;
  uint32_t volume_bsp = 2;
  uint32_t volume_box = 3;
  bool mutable_serializes_as_uint32 = true;
  bool volume_serializes_as_uint32 = true;
  bool volume_values_are_empty_triangles_bsp_box = true;
  bool gh2_rev28_volume_value_is_triangles = false;
};

SourceMiloEditorRndMeshEnumPlan source_milo_editor_rndmesh_enum_plan(
    int32_t mesh_revision,
    uint32_t volume_value);

struct SourceMiloEditorRndMeshBspNodeIoPlan {
  int32_t mesh_revision = 0;
  bool has_value = false;
  bool left_present = false;
  bool right_present = false;
  bool reads_bsp_node = false;
  bool writes_bsp_node = false;
  bool row_starts_with_has_value_bool = true;
  bool reads_vector4_when_has_value = false;
  bool reads_left_right_children_when_has_value = false;
  bool writes_vector4_when_has_value = false;
  bool writes_left_child_only_if_present = false;
  bool writes_right_child_only_if_present = false;
  bool write_does_not_allocate_missing_children = true;
  bool empty_node_is_bool_only = false;
  int32_t read_child_count = 0;
  int32_t write_child_count = 0;
  bool gh2_rev28_bsp_node_is_source_bool_tree = false;
};

SourceMiloEditorRndMeshBspNodeIoPlan
source_milo_editor_rndmesh_bsp_node_io_plan(
    int32_t mesh_revision,
    bool has_value,
    bool left_present,
    bool right_present);

struct SourceMiloEditorRndMeshSectionOrderPlan {
  int32_t mesh_revision = 0;
  int32_t alt_revision = 0;
  std::vector<std::string> read_sections;
  std::vector<std::string> write_sections;
  bool revision_word_first = true;
  bool base_before_trans_draw = true;
  bool trans_draw_before_core_fields = true;
  bool vertices_before_faces = true;
  bool faces_before_group_sizes = true;
  bool group_sizes_before_bone_transforms = true;
  bool bone_transforms_before_tail_flags = true;
  bool tail_flags_before_group_sections = true;
  bool group_sections_after_tail_flags = true;
  bool standalone_end_bytes_last = true;
  bool read_write_orders_match = true;
  bool gh2_rev28_order_is_source_layout = false;
};

SourceMiloEditorRndMeshSectionOrderPlan
source_milo_editor_rndmesh_section_order_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    bool standalone);

struct SourceMiloEditorRndMeshGroupSizesIoPlan {
  int32_t mesh_revision = 0;
  int32_t input_group_sizes_count = 0;
  bool group_size_row_is_uint8 = true;
  bool reads_modern_group_sizes = false;
  bool writes_modern_group_sizes = false;
  bool reads_legacy_group_sizes = false;
  bool writes_legacy_group_sizes = false;
  bool leaves_patch_vector_loop_todo = false;
  bool reads_count_before_rows = false;
  bool writes_count_from_group_sizes_vector = false;
  int32_t read_group_size_rows = 0;
  int32_t write_group_size_rows = 0;
  bool gh2_rev28_counted_byte_rows = false;
};

SourceMiloEditorRndMeshGroupSizesIoPlan
source_milo_editor_rndmesh_group_sizes_io_plan(
    int32_t mesh_revision,
    int32_t group_sizes_count);

struct SourceMiloEditorRndMeshUnsupportedTailPlan {
  int32_t mesh_revision = 0;
  int32_t alt_revision = 0;
  bool read_alt_revision_striper_todo = false;
  bool read_legacy_usvec_todo = false;
  bool read_revision_zero_comment_todo = false;
  bool read_todo_blocks_consume_no_bytes = true;
  bool read_todos_before_tail_flags = true;
  bool write_has_no_alt_revision_striper_todo = true;
  bool write_has_no_legacy_usvec_todo = true;
  bool write_has_no_revision_zero_todo = true;
  int32_t read_todo_block_count = 0;
  bool gh2_rev28_has_no_unsupported_tail = false;
};

SourceMiloEditorRndMeshUnsupportedTailPlan
source_milo_editor_rndmesh_unsupported_tail_plan(
    int32_t mesh_revision,
    int32_t alt_revision);

struct SourceMiloEditorRndMeshTailFlagsIoPlan {
  int32_t mesh_revision = 0;
  int32_t alt_revision = 0;
  bool flags_are_serialized_booleans = true;
  bool order_is_keep_mesh_has_ao_no_quant_unk3 = true;
  bool reads_keep_mesh_data = false;
  bool writes_keep_mesh_data = false;
  bool reads_has_ao_calculation = false;
  bool writes_has_ao_calculation = false;
  bool reads_no_quant = false;
  bool writes_no_quant = false;
  bool reads_unk_bool3 = false;
  bool writes_unk_bool3 = false;
  int32_t read_bool_count = 0;
  int32_t write_bool_count = 0;
  bool gh2_rev28_has_no_tail_flags = false;
};

SourceMiloEditorRndMeshTailFlagsIoPlan
source_milo_editor_rndmesh_tail_flags_io_plan(
    int32_t mesh_revision,
    int32_t alt_revision);

struct SourceMiloEditorRndMeshGroupSectionIoPlan {
  int32_t group_sizes_count = 0;
  int32_t existing_group_section_count = 0;
  bool group_section_row_is_counts_then_sections_then_offsets = true;
  bool reads_group_sections = false;
  bool writes_group_sections = false;
  bool write_pads_to_group_sizes_count = false;
  int32_t read_group_section_count = 0;
  int32_t write_group_section_count = 0;
};

SourceMiloEditorRndMeshGroupSectionIoPlan
source_milo_editor_rndmesh_group_section_io_plan(
    int32_t group_sizes_count,
    bool group_sizes_first_positive,
    int32_t parent_dir_revision,
    int32_t existing_group_section_count);

struct SourceGltfMiloSkinInfluence {
  int32_t remapped_bone = -1;
  float weight = 0.0f;
};

struct SourceGltfMiloRawSkinInfluence {
  float joint_value = 0.0f;
  float weight = 0.0f;
};

struct SourceGltfMiloValidatedSkinInfluence {
  int32_t joint_index = -1;
  float weight = 0.0f;
};

struct SourceGltfMiloSkinValidationResult {
  std::vector<SourceGltfMiloValidatedSkinInfluence> influences;
  bool logged_invalid_weights = false;
  bool logged_invalid_joint_indices = false;
  bool logged_excluded_joint_influences = false;
  bool logged_trimmed_influences = false;
  int32_t ignored_invalid_weights = 0;
  int32_t ignored_invalid_joint_indices = 0;
  int32_t ignored_excluded_joint_influences = 0;
  int32_t dropped_influence_count = 0;
  float dropped_weight = 0.0f;
};

struct SourceGltfMiloSkinAccessorVertexRow {
  bool present = false;
  std::array<float, 4> joints = {0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 4> weights = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct SourceGltfMiloVertexSkinInfluencePlan {
  bool read_joints0_weights0 = false;
  bool read_joints1_weights1 = false;
  std::vector<std::string> accessor_order;
  std::vector<SourceGltfMiloRawSkinInfluence> raw_influences;
  SourceGltfMiloSkinValidationResult validation;
};

struct SourceGltfMiloSkinAccessorSetPlan {
  bool valid = false;
  bool cleared_joints = false;
  bool cleared_weights = false;
  bool ignored_empty_pair = false;
  bool warned_missing_pair = false;
  bool warned_mismatched_counts = false;
  bool warned_position_count_mismatch = false;
};

struct SourceGltfMiloPrimitiveReadInput {
  bool position_accessor_present = true;
  bool position_read_failed = false;
  bool normal_read_failed = false;
  int32_t normal_count = 0;
  bool normals_all_zero = false;
  bool uv_read_failed = false;
  int32_t uv_count = 0;
  bool uvs_all_zero = false;
  bool indices_read_failed = false;
  bool has_skin = false;
  bool has_any_skin_accessors = false;
  bool primary_skin_set_valid = false;
  bool secondary_skin_set_valid = false;
  int32_t source_triangle_count = 0;
};

struct SourceGltfMiloPrimitiveReadPlan {
  bool logs_position_read_error = false;
  bool logs_normal_read_error = false;
  bool logs_bad_normals = false;
  bool logs_uv_read_error = false;
  bool logs_bad_uvs = false;
  bool logs_index_read_error = false;
  bool logs_cannot_continue_mesh = false;
  bool skips_primitive = false;
  std::string skip_reason;
  bool warns_missing_position = false;
  bool warns_skin_accessors_without_skin = false;
  bool clears_skin_accessors = false;
  bool validates_primary_skin_set = false;
  bool validates_secondary_skin_set = false;
  bool warns_secondary_without_primary = false;
  bool clears_secondary_skin_set = false;
  bool builds_vertex_skin_influences = false;
  bool builds_empty_vertex_skin_influences = false;
  bool warns_no_valid_triangles = false;
  bool reaches_chunking = false;
};

struct SourceGltfMiloPrimitiveFilenamePlan {
  std::string node_name;
  int32_t primitive_index = 0;
  std::string base_filename;
  bool first_primitive_uses_plain_node_name = false;
  bool later_primitive_uses_index_suffix = false;
  bool index_is_original_primitive_ordinal = true;
};

struct SourceGltfMiloTriangle {
  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  uint32_t idx2 = 0;
};

struct SourceGltfMiloBuildTrianglesResult {
  std::vector<SourceGltfMiloTriangle> triangles;
  bool used_index_buffer = false;
  bool warned_unindexed_trailing_vertices = false;
  bool warned_index_count_not_multiple_of_three = false;
  bool warned_invalid_index = false;
  int32_t ignored_trailing_vertices = 0;
  int32_t ignored_trailing_indices = 0;
  int32_t ignored_invalid_triangles = 0;
};

struct SourceGltfMiloMeshChunk {
  std::vector<int32_t> triangle_indices;
  std::vector<int32_t> joint_indices;
  int32_t unique_vertex_count = 0;
};

struct SourceGltfMiloMeshChunkPlan {
  int32_t max_influencing_bones = 40;
  int32_t max_vertices = 65535;
  bool source_limits_exceeded = false;
  std::vector<int32_t> rejected_triangle_indices;
  std::vector<SourceGltfMiloMeshChunk> chunks;
};

struct SourceGltfMiloMeshChunkBuilderInput {
  std::vector<int32_t> existing_triangle_indices;
  std::vector<int32_t> existing_joint_indices;
  std::vector<uint32_t> existing_vertex_indices;
  SourceGltfMiloTriangle triangle;
  std::vector<int32_t> triangle_joint_indices;
  int32_t triangle_index = 0;
  int32_t max_joint_count = 40;
  int32_t max_vertex_count = 65535;
  bool add_triangle = true;
};

struct SourceGltfMiloMeshChunkBuilderPlan {
  int32_t starting_joint_count = 0;
  int32_t starting_unique_vertex_count = 0;
  int32_t additional_joint_count = 0;
  int32_t additional_vertex_count = 0;
  bool can_add_triangle = false;
  bool duplicate_vertex_indices_count_once = true;
  bool duplicate_joint_indices_append_once = true;
  std::vector<int32_t> triangle_indices_after_add;
  std::vector<int32_t> joint_indices_after_add;
  int32_t unique_vertex_count_after_add = 0;
};

struct SourceGltfMiloMeshSplitWarningPlan {
  int32_t max_influencing_bones = 40;
  int32_t max_vertices = 65535;
  int32_t chunk_count = 0;
  int32_t total_influencing_bone_count = 0;
  int32_t source_vertex_count = 0;
  bool logs_warning = false;
  std::vector<std::string> split_reasons;
  std::string split_reason;
  int32_t exported_chunk_count = 0;
};

struct SourceGltfMiloChunkFace {
  uint16_t idx1 = 0;
  uint16_t idx2 = 0;
  uint16_t idx3 = 0;
};

struct SourceGltfMiloJointLocalBoneMapRow {
  int32_t joint_index = -1;
  uint16_t local_bone_index = 0;
};

struct SourceGltfMiloPopulateMeshChunkPlan {
  bool clears_vertices = true;
  bool clears_faces = true;
  bool builds_joint_index_to_local_bone_index = true;
  bool builds_bone_transforms = false;
  bool clears_bone_transforms = false;
  bool exceeded_max_vertices = false;
  std::vector<SourceGltfMiloJointLocalBoneMapRow> joint_local_bones;
  std::vector<uint32_t> original_indices_in_vertex_order;
  std::vector<SourceGltfMiloChunkFace> faces;
  std::vector<int32_t> bone_transform_joint_indices;
};

struct SourceGltfMiloMeshChunkFinalizeInput {
  std::string base_filename;
  std::string filename_after_milo_extras;
  int32_t mesh_chunk_count = 1;
  int32_t chunk_index = 0;
  int32_t face_count = 0;
  std::vector<std::string> chunk_joint_names;
  std::string node_name;
  std::string object_type_from_extras;
};

struct SourceGltfMiloHairCollisionMeshDecision {
  bool object_type_char_collide = false;
  bool entry_suffix_coll = false;
  bool entry_suffix_collide = false;
  bool node_suffix_coll = false;
  bool node_suffix_collide = false;
  bool node_contains_hair_collide = false;
  bool records_hair_collision_mesh = false;
};

struct SourceGltfMiloMeshChunkFinalizePlan {
  bool calls_milo_extras_add_to_mesh = true;
  std::vector<uint8_t> group_sizes;
  std::vector<std::string> collected_hair_strand_bones;
  std::string entry_type;
  std::string entry_name;
  std::string geom_owner;
  SourceGltfMiloHairCollisionMeshDecision hair_collision_decision;
  bool records_hair_collision_mesh = false;
};

struct SourceGltfMiloChunkJoint {
  std::string name;
  std::array<float, 16> world_matrix = {1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f};
};

struct SourceGltfMiloBoneTransform {
  std::string name;
  std::array<float, 16> transform = {1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f};
  bool used_identity_for_noninvertible_joint = false;
};

struct SourceGltfMiloBoneTransformPlan {
  std::vector<SourceGltfMiloBoneTransform> bone_transforms;
};

struct SourceGltfMiloMatrixHelpersBoundary {
  bool matrix_helpers_source_present = true;
  bool copy_matrix_call_sites_source_backed = true;
  bool bone_transform_order_source_backed = true;
  bool can_port_copy_matrix_order = true;
  bool can_port_axis_conversion_math = true;
  bool safe_to_adjust_bind_pose_from_axis_conversion = false;
  std::vector<std::string> copy_matrix_call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloCoordinateConversionPlan {
  std::array<float, 3> input_vector = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> milo_vector = {0.0f, 0.0f, 0.0f};
  std::array<float, 4> input_quaternion = {0.0f, 0.0f, 0.0f, 1.0f};
  std::array<float, 4> milo_quaternion = {0.0f, 0.0f, 0.0f, 1.0f};
  std::array<float, 3> input_scale = {1.0f, 1.0f, 1.0f};
  std::array<float, 3> milo_scale = {1.0f, 1.0f, 1.0f};
  bool vector_rule_x_negz_y = true;
  bool quaternion_rule_x_negz_y_w = true;
  bool scale_rule_x_z_y = true;
};

struct SourceGltfMiloNodeHelpersBoundary {
  bool node_helpers_source_present = true;
  bool traversal_call_sites_source_backed = true;
  bool parent_lookup_call_sites_source_backed = true;
  bool can_port_call_order = true;
  bool can_port_node_classification_logic = true;
  bool can_port_parent_bone_search_logic = true;
  bool safe_to_adjust_hierarchy_from_node_helpers = true;
  std::vector<std::string> traversal_call_sites;
  std::vector<std::string> parent_call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloMiloExtrasBoundary {
  bool milo_extras_source_present = true;
  bool mesh_group_light_call_sites_source_backed = true;
  bool object_type_call_site_source_backed = true;
  bool can_port_call_order = true;
  bool can_port_filename_override_logic = true;
  bool can_port_object_mutation_logic = true;
  bool safe_to_adjust_names_or_groups_from_milo_extras = true;
  std::vector<std::string> call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloMiloExtrasInput {
  bool node_extras_present = false;
  bool deserialize_succeeds = false;
  std::string filename;
  std::string object_type;
  std::string note;
  int is_showing = 0;
  float draw_order = 0.0f;
  float sphere_radius = 0.0f;
  std::array<float, 3> sphere_center = {0.0f, 0.0f, 0.0f};
};

struct SourceGltfMiloMiloExtrasApplyPlan {
  bool reads_node_extras = false;
  bool warns_deserialize_failed = false;
  bool overrides_filename = false;
  std::string filename;
  bool writes_object_type = false;
  std::string object_type;
  bool writes_note = false;
  std::string note;
  bool writes_drawable_fields = false;
  bool showing = false;
  float draw_order = 0.0f;
  float sphere_radius = 0.0f;
  std::array<float, 3> sphere_center = {0.0f, 0.0f, 0.0f};
};

struct SourceGltfMiloGameRevisionsBoundary {
  bool game_revisions_source_present = true;
  bool revision_lookup_call_sites_source_backed = true;
  bool can_port_lookup_call_order = true;
  bool can_port_revision_values = true;
  bool safe_to_select_runtime_revisions_from_missing_table = true;
  std::vector<std::string> revision_call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloDirectoryBuilderBoundary {
  bool dir_builder_source_present = true;
  bool outfit_config_builder_source_present = true;
  bool finalizer_call_sites_source_backed = true;
  bool can_port_finalizer_call_order = true;
  bool can_port_character_directory_internals = true;
  bool can_port_rnd_directory_internals = true;
  bool can_port_outfit_config_internals = true;
  bool safe_to_rewrite_directory_assembly_from_missing_builders = true;
  std::vector<std::string> finalizer_call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloPackedSkinSlots {
  std::array<float, 4> weights = {0.0f, 0.0f, 0.0f, 0.0f};
  std::array<uint16_t, 4> bones = {0, 0, 0, 0};
};

struct SourceGltfMiloVertexInput {
  std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
  bool has_normal = false;
  std::array<float, 3> normal = {0.0f, 0.0f, 0.0f};
  bool has_uv = false;
  std::array<float, 2> uv = {0.0f, 0.0f};
  bool has_tangent = false;
  std::array<float, 4> tangent = {0.0f, 0.0f, 0.0f, 0.0f};
  bool has_color = false;
  std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<SourceGltfMiloSkinInfluence> influences;
};

struct SourceGltfMiloChunkVertex {
  uint32_t original_index = 0;
  std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> normal = {0.0f, 0.0f, 0.0f};
  std::array<float, 2> uv = {0.0f, 0.0f};
  std::array<float, 4> tangent = {0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 0.0f};
  SourceGltfMiloPackedSkinSlots skin;
};

struct SourceGltfMiloAddVertexResult {
  bool skipped_existing = false;
  bool added_vertex = false;
  bool exceeded_max_vertices = false;
  bool applied_ao_color_override = false;
  uint16_t new_index = 0;
  SourceGltfMiloChunkVertex vertex;
};

enum class SourceGltfMiloTextureWrapMode {
  kRepeat,
  kClampToEdge,
  kMirroredRepeat,
};

enum class SourceGltfMiloAlphaMode {
  kOpaque,
  kMask,
  kBlend,
};

enum class SourceGltfMiloGame {
  kOther,
  kRockBand2,
  kRockBand3,
  kTheBeatlesRockBand,
  kDanceCentral1,
};

enum class SourceGltfMiloSceneType {
  kCharacter,
  kInstrument,
  kDancer,
  kVenue,
  kOther,
};

struct SourceGltfMiloCharacterDirectoryInput {
  std::string raw_type;
  SourceGltfMiloGame game = SourceGltfMiloGame::kRockBand3;
  std::string meta_name;
};

struct SourceGltfMiloCharacterDirectoryPlan {
  bool creates_character_directory = true;
  int character_revision = 0;
  int object_fields_revision = 2;
  int current_viewport_idx = 6;
  bool inline_proxy = false;
  bool creates_character_testing = true;
  int character_testing_revision = 0;
  std::string character_testing_dist_map = "none";
  std::vector<std::string> sub_dirs;
  int viewport_count = 7;
  bool viewports_are_identity = true;
  int animatable_revision = 0;
  int drawable_revision = 0;
  float draw_sphere_radius = 10000.0f;
  int trans_revision = 0;
  std::string sphere_base;
  int rnd_dir_revision = 0;
  int object_dir_revision = 0;
  bool assigns_meta_directory = true;
};

enum class SourceGltfMiloNodeTraversalKind {
  kMesh,
  kBone,
  kGroup,
  kLight,
  kOther,
};

struct SourceGltfMiloDirectoryEntryInput {
  std::string type;
  std::string name;
};

struct SourceGltfMiloRunOptionsInput {
  SourceGltfMiloSceneType type = SourceGltfMiloSceneType::kOther;
  std::string platform;
  std::string game_arg;
};

struct SourceGltfMiloRunOptionsPlan {
  bool character_directory_type = false;
  bool convert_world_coordinates = true;
  std::string meta_type = "RndDir";
  std::string normalized_platform = "xbox";
  bool warns_invalid_platform = false;
  SourceGltfMiloGame selected_game = SourceGltfMiloGame::kRockBand3;
  bool warns_invalid_game = false;
};

struct SourceGltfMiloRunTypeInput {
  std::string raw_type;
};

struct SourceGltfMiloRunTypePlan {
  std::string raw_type;
  std::string normalized_type;
  bool normalized_character_family = false;
  bool convert_world_coordinates = true;
  bool meta_type_uses_raw_case_sensitive_type = true;
  bool meta_type_character = false;
  std::string meta_type = "RndDir";
  bool finalizer_uses_raw_case_sensitive_type = true;
  bool finalizer_uses_normalized_type = false;
  bool calls_character_directory_builder = false;
  bool calls_rnd_directory_builder = true;
};

struct SourceGltfMiloRunPreflightInput {
  bool input_file_exists = true;
  std::string input_path;
  std::string outfit_config_path;
  bool outfit_config_exists = true;
};

struct SourceGltfMiloRunPreflightPlan {
  bool exits_missing_input = false;
  bool accepts_gltf_extension = false;
  bool accepts_glb_extension = false;
  bool extension_check_is_case_sensitive = true;
  bool exits_non_gltf_extension = false;
  bool lowercases_outfit_config_path_before_check = true;
  std::string normalized_outfit_config_path;
  bool checks_outfit_config_exists = false;
  bool exits_missing_outfit_config = false;
  bool reaches_model_load = false;
};

struct SourceGltfMiloBaseMeshInput {
  SourceGltfMiloGame game = SourceGltfMiloGame::kOther;
  std::string platform;
  int32_t model_revision = 0;
  std::string parent_name;
  std::string node_name;
  bool has_material = false;
  std::string material_name;
  bool material_has_diffuse = false;
  bool material_has_normal = false;
  bool material_has_specular = false;
};

struct SourceGltfMiloBaseMeshPlan {
  bool creates_mesh = true;
  bool calls_milo_editor_rndmesh_new = false;
  int32_t mesh_revision = 0;
  int32_t mesh_alt_revision = 0;
  bool factory_requested_zero_vertices = false;
  bool factory_requested_zero_faces = false;
  bool factory_ignores_requested_counts = false;
  int32_t object_fields_revision = 0;
  int32_t trans_revision = 0;
  std::string parent_name;
  bool copies_local_matrix = false;
  bool copies_world_matrix = false;
  int32_t drawable_revision = 0;
  bool initializes_draw_sphere = false;
  float draw_sphere_radius = 0.0f;
  bool volume_triangles = false;
  bool keep_mesh_data = false;
  bool has_ao_calculation = false;
  bool vertices_is_next_gen = false;
  int32_t vertex_compression_type = 0;
  int32_t vertex_size = 0;
  bool binds_material = false;
  std::string material_name;
  bool logs_missing_diffuse_or_maps = false;
};

struct SourceGltfMiloBandConfigurationBlockPlan {
  bool source_block_present = true;
  bool source_block_is_commented_todo = true;
  bool emits_directory_entry = false;
  int32_t object_fields_revision = 2;
  std::string entry_type = "BandConfiguration";
  std::string entry_name;
};

struct SourceGltfMiloVenueAllGeomGroupPlan {
  bool creates_group = false;
  std::string entry_type;
  std::string entry_name;
  int32_t group_revision = 0;
  int32_t trans_revision = 0;
  int32_t drawable_revision = 0;
  bool initializes_draw_sphere = false;
  float draw_sphere_radius = 0.0f;
  int32_t animatable_revision = 0;
  int32_t object_fields_revision = 0;
  std::vector<std::string> objects;
};

struct SourceGltfMiloSceneAssemblyInput {
  SourceGltfMiloSceneType type = SourceGltfMiloSceneType::kOther;
  std::string filename;
  int32_t group_revision = 0;
  int32_t trans_revision = 0;
  int32_t drawable_revision = 0;
  int32_t animatable_revision = 0;
  std::vector<SourceGltfMiloDirectoryEntryInput> existing_entries;
};

struct SourceGltfMiloSceneAssemblyPlan {
  SourceGltfMiloBandConfigurationBlockPlan band_configuration;
  SourceGltfMiloVenueAllGeomGroupPlan venue_all_geom_group;
  bool calls_outfit_config_builder = true;
  bool calls_character_directory_builder = false;
  bool calls_rnd_directory_builder = false;
  bool creates_milo_file = true;
  std::string save_type = "Uncompressed";
  int32_t save_version = 0x810;
  std::string save_stream_endian = "LittleEndian";
  std::string save_object_endian = "BigEndian";
  bool report_generator_runs_after_save_when_requested = true;
};

struct SourceGltfMiloReportGeneratorInput {
  std::string raw_report;
  std::string normalized_type;
};

struct SourceGltfMiloReportGeneratorPlan {
  std::string raw_report;
  std::string normalized_report;
  std::string report_type_arg;
  bool lowercases_report_option = true;
  bool branch_after_milo_file_save = true;
  bool calls_report_generator = false;
  bool passes_meta = false;
  bool passes_selected_game = false;
  bool report_generator_source_present = true;
  bool can_port_report_contents = true;
};

struct SourceGltfMiloNodeTraversalInput {
  SourceGltfMiloNodeTraversalKind kind =
      SourceGltfMiloNodeTraversalKind::kOther;
  bool mesh_present = false;
  std::string node_name;
  std::vector<std::string> chunk_joint_names;
  bool node_extras_present = false;
  bool extras_contains_hair_marker = false;
  bool parsed_hair_settings = false;
  bool settings_already_detected = false;
  bool has_hair_strand_bones_before = false;
  bool disable_splitting = false;
};

struct SourceGltfMiloNodeTraversalPlan {
  bool calls_create_base_mesh = false;
  bool calls_populate_mesh_chunk = false;
  bool aborts_meshless_mesh_node = false;
  bool calls_process_bone_node = false;
  bool calls_process_group_node = false;
  bool calls_process_light_node = false;
  bool ignores_node = false;
  std::vector<std::string> hair_strand_bones_added;
  bool tries_hair_settings_detection = false;
  bool bad_hair_extras_are_nonfatal = true;
  bool sets_detected_hair_settings = false;
  bool calls_process_char_hair_after_traversal = false;
  bool calls_process_empty_hair_collides_after_traversal = false;
  bool split_strands_at_branches = true;
  bool uses_default_char_hair_extras_when_missing = true;
};

struct SourceGltfMiloMaterialExtras {
  bool present = false;
  int prelit = 0;
  int alpha_cut = 0;
  float alpha_threshold = 0.0f;
  int alpha_write = 0;
  int z_mode = 0;
  int blend_mode = 0;
  int use_environment = 0;
  float emissive_multiplier = 1.0f;
  int cull = 0;
  int point_lights = 0;
  int projected_lights = 0;
  std::string material_type;
  std::string normal_detail_map;
  int shader_variation = 0;
};

struct SourceGltfMiloPrelitOptionInput {
  std::string raw_prelit;
  bool has_base_color_texture = false;
  bool extras_present = false;
  int extras_prelit = 0;
};

struct SourceGltfMiloPrelitOptionPlan {
  std::string raw_prelit;
  std::string normalized_prelit;
  bool lowercases_prelit_option = true;
  bool base_branch_uses_raw_case_sensitive_prelit = true;
  bool extras_branch_uses_normalized_empty_prelit = true;
  bool base_color_branch_entered = false;
  bool base_branch_sets_pre_lit = false;
  bool extras_branch_reads_prelit = false;
  bool extras_branch_sets_pre_lit = false;
  bool final_pre_lit = false;
};

struct SourceGltfMiloMaterialInput {
  std::string name;
  std::string platform;
  bool has_base_color_texture = false;
  bool double_sided = false;
  bool prelit_option_equals_false = false;
  bool prelit_option_empty = false;
  bool sampler_present = false;
  SourceGltfMiloTextureWrapMode wrap_s =
      SourceGltfMiloTextureWrapMode::kRepeat;
  SourceGltfMiloTextureWrapMode wrap_t =
      SourceGltfMiloTextureWrapMode::kRepeat;
  bool image_has_alpha = false;
  SourceGltfMiloAlphaMode alpha_mode = SourceGltfMiloAlphaMode::kOpaque;
  float alpha_cutoff = 0.5f;
  bool has_normal_texture = false;
  bool has_emissive_texture = false;
  bool has_specular_color_texture = false;
  bool has_specular_color = false;
  std::array<float, 4> specular_color = {0.0f, 0.0f, 0.0f, 0.0f};
  bool has_specular_factor = false;
  float specular_factor = 0.0f;
  SourceGltfMiloMaterialExtras extras;
};

struct SourceGltfMiloMaterialPlan {
  bool creates_mat_entry = true;
  bool creates_diffuse_tex_entry = false;
  std::string mat_entry_name;
  std::string diffuse_tex;
  std::string diffuse_tex_entry_name;
  std::string texture_external_path;
  std::string diffuse_compression_format;
  std::string diffuse_bitmap_encoding;
  float diffuse_mip_map_k = 0.0f;
  bool diffuse_type_regular = false;
  bool diffuse_optimize_for_ps3 = false;
  bool diffuse_bitmap_mip_maps_zero = false;
  bool diffuse_bpl_width_bpp_over_8 = false;
  bool diffuse_xbox_byte_swap = false;
  bool creates_normal_tex_entry = false;
  std::string normal_map;
  std::string normal_tex_entry_name;
  std::string normal_texture_external_path;
  std::string normal_compression_format;
  std::string normal_bitmap_encoding;
  float normal_mip_map_k = 0.0f;
  bool normal_type_regular = false;
  bool normal_optimize_for_ps3 = false;
  bool normal_bitmap_mip_maps_zero = false;
  bool normal_bpl_width_bpp_over_8 = false;
  bool normal_xbox_byte_swap = false;
  bool creates_emissive_tex_entry = false;
  std::string emissive_map;
  std::string emissive_tex_entry_name;
  std::string emissive_texture_external_path;
  std::string emissive_compression_format;
  std::string emissive_bitmap_encoding;
  float emissive_mip_map_k = 0.0f;
  bool emissive_type_regular = false;
  bool emissive_optimize_for_ps3 = false;
  bool emissive_bitmap_mip_maps_zero = false;
  bool emissive_bpl_width_bpp_over_8 = false;
  bool emissive_xbox_byte_swap = false;
  bool creates_specular_tex_entry = false;
  std::string specular_map;
  std::string specular_tex_entry_name;
  std::string specular_texture_external_path;
  std::string specular_compression_format;
  std::string specular_bitmap_encoding;
  float specular_mip_map_k = 0.0f;
  bool specular_type_regular = false;
  bool specular_optimize_for_ps3 = false;
  bool specular_bitmap_mip_maps_zero = false;
  bool specular_bpl_width_bpp_over_8 = false;
  bool specular_xbox_byte_swap = false;
  bool stencil_ignore = false;
  bool per_pixel_lit = false;
  bool pre_lit = false;
  bool point_lights = false;
  bool projected_lights = false;
  bool fog = true;
  bool cull = true;
  int shader_variation = 0;
  int blend = 1;
  int z_mode = 1;
  int tex_wrap = 1;
  bool alpha_cut = false;
  int alpha_threshold = 0;
  bool alpha_write = false;
  int texture_compression = 1;
  bool use_environment = false;
  float emissive_multiplier = 1.0f;
  float normal_detail_tiling = 1.0f;
  bool rim_power_zeroed_before_final = false;
  bool rim_power_final_overrides_zero = false;
  float rim_power = 4.0f;
  bool rim_rgb_zeroed = false;
  std::array<float, 4> rim_rgb = {0.0f, 0.0f, 0.0f, 0.0f};
  float specular_power = 0.0f;
  float specular2_power = 0.0f;
  bool has_specular_rgb = false;
  std::array<float, 4> specular_rgb = {0.0f, 0.0f, 0.0f, 0.0f};
  std::string normal_detail_map;
  bool extras_applied = false;
  bool extras_projected_lights_declared = false;
  bool extras_projected_lights_applied = false;
  int extras_projected_lights = 0;
  bool extras_material_type_declared = false;
  bool extras_material_type_applied = false;
  std::string extras_material_type;
  bool obj_fields_revision2 = false;
};

struct SourceGltfMiloTextureTempOutputInput {
  int32_t curmat = 0;
  bool has_base_color_texture = false;
  bool has_normal_texture = false;
  bool has_emissive_texture = false;
  bool has_specular_color_texture = false;
};

struct SourceGltfMiloTextureTempOutputPlan {
  int32_t curmat_start = 0;
  int32_t curmat_after_base = 0;
  int32_t curmat_final = 0;
  bool base_texture_increments_curmat = false;
  bool side_maps_reuse_current_curmat = true;
  std::vector<std::string> convert_temp_paths;
  std::vector<std::string> parse_temp_paths;
  std::vector<std::string> delete_temp_paths;
};

struct SourceGltfMiloXboxTextureByteSwapResult {
  bool source_loop_requires_complete_dwords = true;
  bool input_size_multiple_of_four = true;
  bool would_index_past_end = false;
  std::vector<uint8_t> bytes;
};

struct SourceGltfMiloMaterialRuntimeBoundary {
  bool gltf_material_plan_is_exporter_side = true;
  bool stock_runtime_authority_is_decoded_rndmat = true;
  bool double_sided_maps_to_cull_only = true;
  bool project_hair_override_is_cull_only = true;
  bool permits_depth_priority_change = false;
  bool permits_material_sort_change = false;
  bool permits_blend_or_z_rewrite = false;
  bool permits_synthesized_skin_indices = false;
  std::vector<std::string> source_authorities;
  std::vector<std::string> forbidden_runtime_edits;
};

struct SourceGltfMiloBoneNodeInput {
  std::string name;
  std::string type;
  std::string fallback_parent;
  bool has_parent_bone = false;
  std::string parent_bone;
  bool is_rb3_skeleton_bone = false;
};

struct SourceGltfMiloBoneNodePlan {
  bool skipped_neutral_bone = false;
  bool skipped_character_rb3_skeleton_bone = false;
  bool creates_trans_entry = false;
  std::string entry_type;
  std::string entry_name;
  int trans_revision = 0;
  int object_fields_revision = 0;
  bool copies_local_matrix = false;
  bool copies_world_matrix = false;
  std::string parent_name;
};

std::vector<std::string> source_gltf_milo_rb3_skeleton_bone_names();
std::size_t source_gltf_milo_rb3_skeleton_bone_name_count();
bool source_gltf_milo_is_rb3_skeleton_bone_name(const std::string& name);

struct SourceGltfMiloGroupNodeInput {
  std::string name;
  int group_revision = 0;
  int trans_revision = 0;
  int drawable_revision = 0;
  int animatable_revision = 0;
  std::vector<std::string> descendant_names;
};

struct SourceGltfMiloGroupNodePlan {
  bool skipped_armature = false;
  bool creates_group_entry = false;
  std::string entry_type;
  std::string entry_name;
  int group_revision = 0;
  int object_fields_revision = 0;
  int trans_revision = 0;
  int drawable_revision = 0;
  int animatable_revision = 0;
  bool copies_local_matrix = false;
  bool copies_world_matrix = false;
  bool calls_milo_extras_add_to_group = false;
  std::vector<std::string> objects;
};

struct SourceGltfMiloLightNodeInput {
  std::string name;
  int light_revision = 0;
  int trans_revision = 0;
  float range = 0.0f;
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
  std::string punctual_light_type;
};

struct SourceGltfMiloLightNodePlan {
  bool creates_light_entry = false;
  std::string entry_type;
  std::string entry_name;
  int light_revision = 0;
  int object_fields_revision = 0;
  float range = 0.0f;
  std::string color_owner;
  std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 1.0f};
  std::string light_type;
  int trans_revision = 0;
  bool copies_local_matrix = false;
  bool copies_world_matrix = false;
  bool calls_milo_extras_add_to_object = false;
};

struct SourceRndLightDefaultState {
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
  bool color_owner_self = true;
  float range = 1000.0f;
  float falloff_start = 0.0f;
  std::string type = "kPoint";
  bool animate_color_from_preset = true;
  bool animate_position_from_preset = true;
  bool animate_range_from_preset = true;
  bool showing = true;
  bool texture_null = true;
  bool shadow_override_null = true;
  float top_radius = 0.0f;
  float bot_radius = 30.0f;
  int projected_blend = 0;
  bool only_projection = false;
  bool texture_xfm_reset = true;
};

struct SourceRndLightSavePlan {
  int32_t save_id = 0x33;
};

struct SourceRndLightLoadPlan {
  int32_t revision = 0;
  int32_t alt_revision = 0;
  bool accepted_revision = false;
  bool accepted_alt_revision = false;
  bool reads_object_fields = false;
  bool reads_transformable = true;
  bool reads_color = true;
  bool reads_legacy_colors = false;
  bool reads_legacy_pre_range_ints = false;
  bool reads_range = true;
  bool reads_legacy_post_range_ints = false;
  bool reads_type = false;
  int32_t serialized_type = 0;
  int32_t effective_type = 0;
  bool legacy_type_decrements_above_one = false;
  bool reads_falloff_start = false;
  bool reads_animate_color_position = false;
  bool reads_top_bot_radius = false;
  bool reads_legacy_radius_ints = false;
  bool reads_texture = false;
  bool reads_rev9_shadow_draw_list = false;
  bool reads_rev8_shadow_draw_ptr = false;
  bool reads_color_owner = false;
  bool null_color_owner_defaults_to_self = false;
  bool reads_texture_xfm = false;
  bool reads_legacy_texture_ptr = false;
  bool reads_only_projection = false;
  bool reads_shadow_objects = false;
  bool reads_projected_blend = false;
  bool reads_animate_range = false;
  bool animate_range_defaults_from_color = false;
};

struct SourceRndLightCopyPlan {
  std::vector<std::string> superclasses;
  std::vector<std::string> copied_members;
  bool copy_type_shallow = false;
  bool copy_type_from_max = false;
  bool source_color_owner_self = true;
  bool copies_range = true;
  bool copies_color_owner = false;
  bool resets_color_owner_to_self = false;
  bool copies_color_in_owner_fallback = false;
};

struct SourceRndLightReplacePlan {
  bool calls_transformable_replace = true;
  bool color_owner_matches_from = false;
  bool replacement_is_light = false;
  bool copies_replacement_color_owner = false;
  bool resets_color_owner_to_self = false;
};

struct SourceRndLightIntensityPlan {
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
};

struct SourceRndLightHandlerPlan {
  std::vector<std::string> actions;
  std::vector<std::string> superclasses;
  int32_t check = 0x186;
};

struct SourceRndLightPropSyncPlan {
  std::vector<std::string> props;
  std::vector<std::string> set_props;
  std::vector<std::string> superclasses;
};

struct SourceRndFurSavePlan {
  int32_t save_id = 29;
};

struct SourceRndFurCopyPlan {
  std::vector<std::string> superclasses;
  bool asserts_source_fur = true;
  bool copies_visible_members = false;
};

struct SourceRndFurLoadPlan {
  int32_t revision = 0;
  int32_t alt_revision = 0;
  bool accepted_revision = false;
  bool accepted_alt_revision = false;
  bool reads_object = false;
  bool reads_base_filler_block = false;
  bool reads_rev2_extra_fillers = false;
  bool reads_second_filler_block = false;
  bool reads_base_tint = false;
  bool reads_end_tint = false;
  bool reads_fur_detail_tex = false;
  bool reads_fur_tiling = false;
  bool reads_wind = false;
  std::vector<std::string> read_order;
};

struct SourceRndFurHandlerPlan {
  std::vector<std::string> superclasses;
  int32_t check = 0x3C;
};

struct SourceRndFurPropSyncPlan {
  bool empty = true;
};

struct SourceRndFurRuntimeBoundary {
  bool source_is_format_contract_only = true;
  bool stock_character_inventory_has_no_rows = true;
  bool permits_renderer_change = false;
  bool permits_material_change = false;
  bool permits_hair_physics_change = false;
};

struct SourceRndFurRb2DumpLayout {
  std::vector<std::string> members;
  bool statement_level_load_body = false;
};

struct SourceGltfMiloTransAnimChannelInput {
  std::string target_node;
  std::string target_path;
  int32_t linear_key_count = 0;
};

struct SourceGltfMiloTransAnimExportPlan {
  bool has_channels = false;
  bool transform_only = false;
  bool creates_trans_anim = false;
  bool uses_reflection_revision = false;
  bool logs_mismatched_target = false;
  bool converts_translation_keys = false;
  bool converts_rotation_keys = false;
  bool converts_scale_keys = false;
  uint16_t trans_anim_revision = 0;
  int animatable_revision = 0;
  bool anim_rate_30_fps = false;
  int drawable_revision = 0;
  float draw_sphere_radius = 0.0f;
  std::string trans_target;
  std::string keys_owner;
  int object_fields_revision = 0;
  std::string entry_type;
  std::string entry_name;
  int32_t translation_key_count = 0;
  int32_t rotation_key_count = 0;
  int32_t scale_key_count = 0;
  std::vector<std::string> processed_channel_paths;
  std::vector<std::string> mismatched_target_nodes;
};

SourceGltfMiloSkinAccessorSetPlan source_gltf_milo_validate_skin_accessor_set(
    bool has_joints,
    bool has_weights,
    int32_t joints_count,
    int32_t weights_count,
    int32_t expected_position_count);
SourceGltfMiloPrimitiveReadPlan source_gltf_milo_primitive_read_plan(
    const SourceGltfMiloPrimitiveReadInput& input);
SourceGltfMiloPrimitiveFilenamePlan
source_gltf_milo_primitive_filename_plan(
    const std::string& node_name,
    int32_t primitive_index);

SourceGltfMiloSkinValidationResult source_gltf_milo_validate_skin_influences(
    const std::vector<SourceGltfMiloRawSkinInfluence>& raw_influences,
    int32_t skin_joint_count,
    const std::vector<int32_t>& excluded_joint_indices);

SourceGltfMiloVertexSkinInfluencePlan
source_gltf_milo_get_vertex_skin_influences_plan(
    const SourceGltfMiloSkinAccessorVertexRow& set0,
    const SourceGltfMiloSkinAccessorVertexRow& set1,
    int32_t skin_joint_count,
    const std::vector<int32_t>& excluded_joint_indices);

SourceGltfMiloPackedSkinSlots source_gltf_milo_pack_skin_slots(
    const std::vector<SourceGltfMiloSkinInfluence>& influences,
    bool compressed_vertex_layout);

SourceGltfMiloAddVertexResult source_gltf_milo_add_vertex_to_chunk_mesh(
    uint32_t original_index,
    const std::vector<uint32_t>& existing_original_indices,
    const SourceGltfMiloVertexInput& input,
    bool mesh_has_skin,
    bool compressed_vertex_layout,
    bool mesh_has_ao_calculation,
    int32_t current_vertex_count);

SourceGltfMiloMaterialPlan source_gltf_milo_material_base_plan(
    const SourceGltfMiloMaterialInput& input);
SourceGltfMiloPrelitOptionPlan source_gltf_milo_prelit_option_plan(
    const SourceGltfMiloPrelitOptionInput& input);
SourceGltfMiloTextureTempOutputPlan
source_gltf_milo_texture_temp_output_plan(
    const SourceGltfMiloTextureTempOutputInput& input);
SourceGltfMiloXboxTextureByteSwapResult
source_gltf_milo_xbox_texture_byte_swap(const std::vector<uint8_t>& pixels);
SourceGltfMiloMaterialRuntimeBoundary
source_gltf_milo_material_runtime_boundary();

SourceGltfMiloRunOptionsPlan source_gltf_milo_run_options_plan(
    const SourceGltfMiloRunOptionsInput& input);
SourceGltfMiloRunTypePlan source_gltf_milo_run_type_plan(
    const SourceGltfMiloRunTypeInput& input);
SourceGltfMiloRunPreflightPlan source_gltf_milo_run_preflight_plan(
    const SourceGltfMiloRunPreflightInput& input);

SourceGltfMiloBaseMeshPlan source_gltf_milo_create_base_mesh_plan(
    const SourceGltfMiloBaseMeshInput& input);

SourceGltfMiloSceneAssemblyPlan source_gltf_milo_scene_assembly_plan(
    const SourceGltfMiloSceneAssemblyInput& input);
SourceGltfMiloReportGeneratorPlan source_gltf_milo_report_generator_plan(
    const SourceGltfMiloReportGeneratorInput& input);

SourceGltfMiloNodeTraversalPlan source_gltf_milo_node_traversal_plan(
    const SourceGltfMiloNodeTraversalInput& input);

SourceGltfMiloBoneNodePlan source_gltf_milo_process_bone_node_plan(
    const SourceGltfMiloBoneNodeInput& input);

SourceGltfMiloGroupNodePlan source_gltf_milo_process_group_node_plan(
    const SourceGltfMiloGroupNodeInput& input);

SourceGltfMiloLightNodePlan source_gltf_milo_process_light_node_plan(
    const SourceGltfMiloLightNodeInput& input);

SourceRndLightDefaultState source_rndlight_default_state();
SourceRndLightSavePlan source_rndlight_save_plan();
SourceRndLightLoadPlan source_rndlight_load_plan(
    int32_t revision,
    int32_t alt_revision,
    int32_t serialized_type);
SourceRndLightCopyPlan source_rndlight_copy_plan(
    bool copy_type_shallow,
    bool copy_type_from_max,
    bool source_color_owner_self);
SourceRndLightReplacePlan source_rndlight_replace_plan(
    bool color_owner_matches_from,
    bool replacement_is_light);
SourceRndLightIntensityPlan source_rndlight_intensity_plan(
    std::array<float, 3> color);
SourceRndLightHandlerPlan source_rndlight_handler_plan();
SourceRndLightPropSyncPlan source_rndlight_prop_sync_plan();

SourceRndFurSavePlan source_rndfur_save_plan();
SourceRndFurCopyPlan source_rndfur_copy_plan();
SourceRndFurLoadPlan source_rndfur_load_plan(
    int32_t revision,
    int32_t alt_revision);
SourceRndFurHandlerPlan source_rndfur_handler_plan();
SourceRndFurPropSyncPlan source_rndfur_prop_sync_plan();
SourceRndFurRuntimeBoundary source_rndfur_runtime_boundary();
SourceRndFurRb2DumpLayout source_rndfur_rb2_dump_layout();

SourceGltfMiloTransAnimExportPlan
source_gltf_milo_export_trans_anim_plan(
    const std::string& anim_name,
    const std::vector<SourceGltfMiloTransAnimChannelInput>& channels,
    int animatable_revision,
    int drawable_revision,
    bool convert_world_coordinates);

SourceGltfMiloBuildTrianglesResult source_gltf_milo_build_source_triangles(
    const std::vector<uint32_t>& indices,
    int32_t position_count,
    bool has_index_buffer);

SourceGltfMiloMeshChunkBuilderPlan source_gltf_milo_mesh_chunk_builder_plan(
    const SourceGltfMiloMeshChunkBuilderInput& input);
SourceGltfMiloMeshChunkPlan source_gltf_milo_split_mesh_chunks(
    const std::vector<SourceGltfMiloTriangle>& triangles,
    const std::vector<std::vector<int32_t>>& vertex_joint_indices);
SourceGltfMiloMeshSplitWarningPlan
source_gltf_milo_mesh_split_warning_plan(
    const std::vector<SourceGltfMiloMeshChunk>& chunks,
    int32_t source_vertex_count);

SourceGltfMiloPopulateMeshChunkPlan
source_gltf_milo_populate_mesh_chunk_plan(
    const std::vector<SourceGltfMiloTriangle>& triangles,
    const std::vector<int32_t>& chunk_joint_indices,
    bool mesh_has_skin);

bool source_gltf_milo_is_hair_bone_name(const std::string& bone_name);

SourceGltfMiloHairCollisionMeshDecision
source_gltf_milo_hair_collision_mesh_decision(
    const std::string& entry_name,
    const std::string& node_name,
    const std::string& object_type_from_extras);

SourceGltfMiloMeshChunkFinalizePlan
source_gltf_milo_finalize_mesh_chunk_plan(
    const SourceGltfMiloMeshChunkFinalizeInput& input);

SourceGltfMiloBoneTransformPlan source_gltf_milo_build_bone_transforms(
    const std::vector<SourceGltfMiloChunkJoint>& joints,
    const std::vector<int32_t>& chunk_joint_indices,
    std::array<float, 16> mesh_world_matrix);

SourceGltfMiloMatrixHelpersBoundary
source_gltf_milo_matrix_helpers_boundary();
SourceGltfMiloCoordinateConversionPlan
source_gltf_milo_coordinate_conversion_plan(
    const std::array<float, 3>& vector,
    const std::array<float, 4>& quaternion,
    const std::array<float, 3>& scale);

SourceGltfMiloNodeHelpersBoundary
source_gltf_milo_node_helpers_boundary();

SourceGltfMiloMiloExtrasBoundary
source_gltf_milo_milo_extras_boundary();
SourceGltfMiloMiloExtrasApplyPlan source_gltf_milo_milo_extras_apply_plan(
    const SourceGltfMiloMiloExtrasInput& input,
    bool drawable_target);

SourceGltfMiloGameRevisionsBoundary
source_gltf_milo_game_revisions_boundary();

SourceGltfMiloDirectoryBuilderBoundary
source_gltf_milo_directory_builder_boundary();
SourceGltfMiloCharacterDirectoryPlan
source_gltf_milo_character_directory_plan(
    const SourceGltfMiloCharacterDirectoryInput& input);

struct SourceRndMeshZeroWeightVertex {
  float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  int32_t bone_indices[4] = {0, 0, 0, 0};
};

struct SourceRndMeshZeroWeightPlan {
  bool ran = false;
  std::vector<SourceRndMeshZeroWeightVertex> vertices;
};

SourceRndMeshZeroWeightPlan source_rndmesh_set_zero_weight_bones(
    int32_t bone_count,
    std::vector<SourceRndMeshZeroWeightVertex> vertices);

struct SourceRndMeshSetBonePlan {
  bool assigned_bone = true;
  bool recomputed_offset = false;
  milo_scene::Xfm offset;
};

SourceRndMeshSetBonePlan source_rndmesh_set_bone_plan(
    const milo_scene::Xfm& mesh_world,
    const milo_scene::Xfm& bone_world,
    bool recompute_offset);

struct SourceRndMeshScaleBonesPlan {
  bool scaled = false;
  float scale = 1.0f;
  std::vector<milo_scene::Xfm> offsets;
};

SourceRndMeshScaleBonesPlan source_rndmesh_scale_bones(
    std::vector<milo_scene::Xfm> offsets,
    float scale);

struct SourceRndMeshCopyBonesPlan {
  bool copied = false;
  bool cleared = false;
  std::vector<std::string> bones;
};

SourceRndMeshCopyBonesPlan source_rndmesh_copy_bones(
    const std::vector<std::string>* source_bones);

struct SourceRndMeshCopyGeometryFromOwnerPlan {
  bool owner_is_self = true;
  bool copied_geometry = false;
  bool copy_with_volume = false;
  bool sync = false;
  int32_t sync_mask = 0;
};

SourceRndMeshCopyGeometryFromOwnerPlan
source_rndmesh_copy_geometry_from_owner(bool owner_is_self);

struct SourceRndMeshSetGeomOwnerPlan {
  bool asserts_owner_present = true;
  bool owner_present = false;
  bool assertion_would_fail = false;
  bool assigned_geom_owner = false;
};

SourceRndMeshSetGeomOwnerPlan source_rndmesh_set_geom_owner_plan(
    bool owner_present);

struct SourceRndMeshCopyGeometryPlan {
  bool geom_owner_becomes_self = true;
  int32_t copied_vert_count = 0;
  int32_t copied_face_count = 0;
  int32_t copied_patch_count = 0;
  bool copied_volume = false;
  int32_t copied_volume_value = 0;
  std::vector<std::string> copied_bones;
  bool cleared_striper_results = true;
};

SourceRndMeshCopyGeometryPlan source_rndmesh_copy_geometry_plan(
    int32_t owner_vert_count,
    int32_t owner_face_count,
    int32_t owner_patch_count,
    int32_t owner_volume,
    std::vector<std::string> mesh_bones,
    bool copy_volume);

struct SourceRndMeshReplacePlan {
  bool calls_trans_replace = true;
  bool geom_owner_matches_from = false;
  bool changed_geom_owner = false;
  bool to_is_mesh = false;
  bool new_owner_from_to_geom_owner = false;
  bool new_owner_is_self = false;
};

SourceRndMeshReplacePlan source_rndmesh_replace_plan(
    bool geom_owner_matches_from,
    bool to_is_mesh);

struct SourceRndMeshCopyPlan {
  bool copies_object = true;
  bool copies_transformable = true;
  bool copies_drawable = true;
  bool copies_material = true;
  bool copies_keep_mesh_data = false;
  bool ors_mutable = false;
  bool copies_mutable = false;
  bool clears_has_ao_calc = true;
  bool copies_force_no_quantize = true;
  bool copies_geom_owner = false;
  bool copies_bones = false;
  bool copies_geometry = false;
  bool copy_geometry_with_volume = false;
  bool copies_has_ao_calc = false;
  bool sync = true;
  int32_t sync_mask = 0xbf;
};

SourceRndMeshCopyPlan source_rndmesh_copy_plan(
    bool copy_shallow,
    bool copy_from_max,
    bool source_geom_owner_is_self);

struct SourceRndMeshDefaultState {
  bool material_null = true;
  bool geom_owner_self = true;
  bool bones_empty = true;
  int32_t mutable_flags = 0;
  int32_t volume = 1;
  bool bsp_tree_null = true;
  bool multi_mesh_null = true;
  bool compressed_verts_null = true;
  uint32_t num_compressed_verts = 0;
  bool file_loader_null = true;
  bool has_ao_calc = false;
  bool keep_mesh_data = false;
  bool unk9p2 = true;
  bool force_no_quantize = false;
};

struct SourceRndMeshSavePlan {
  int32_t save_id = 1135;
};

struct SourceRndMeshDestructorPlan {
  bool release_file_loader = true;
  bool release_bsp_tree = true;
  bool release_multi_mesh = true;
  bool clear_compressed_verts = true;
  bool clear_compressed_verts_zeros_count = true;
  bool directly_releases_material = false;
  bool directly_releases_geom_owner = false;
};

struct SourceRndMeshSetMatPlan {
  bool material_pointer_present = false;
  bool assigns_material_pointer = true;
  bool syncs_mesh = false;
  bool mutates_render_state = false;
  bool has_name_special_case = false;
};

struct SourceRndMeshDebugCountsPlan {
  int32_t face_count = 0;
  int32_t vert_count = 0;
  bool milo_debug_only = true;
  int32_t num_faces_result = 0;
  int32_t num_verts_result = 0;
};

struct SourceRndMeshVolumeTextPlan {
  int32_t volume = 0;
  bool known_volume = false;
  std::string label;
};

struct SourceRndMeshPrintPlan {
  bool uses_debug_stream = true;
  std::vector<std::string> rows;
};

struct SourceRndMeshSyncPlan {
  int32_t input_mask = 0;
  bool keep_mesh_data = false;
  int32_t on_sync_mask = 0;
};

struct SourceRndMeshClearCompressedVertsPlan {
  bool release_compressed_verts = true;
  int32_t num_compressed_verts = 0;
};

struct SourceRndMeshCountPlan {
  int32_t requested_count = 0;
  bool resize_verts = false;
  bool resize_faces = false;
  int32_t sync_input_mask = 0x3f;
  int32_t on_sync_mask = 0x3f;
};

struct SourceRndMeshFaceLoadPlan {
  int32_t mesh_revision = 0;
  bool reads_three_indices = true;
  bool reads_legacy_vector = false;
};

struct SourceRndMeshFace {
  int32_t idx0 = 0;
  int32_t idx1 = 0;
  int32_t idx2 = 0;
};

struct SourceRndMeshFaceCenterResult {
  bool invalid_index = false;
  std::array<float, 3> center = {0.0f, 0.0f, 0.0f};
};

struct SourceRndMeshHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> expressions;
  std::vector<std::string> actions;
  std::vector<std::string> superclasses;
  int32_t check = 2306;
};

struct SourceRndMeshMutableBitPlan {
  bool increments_property_index = true;
  bool has_bit_subproperty = false;
  bool resolves_int_or_bit_symbol = false;
  bool asserts_prop_insert_or_less = false;
  bool delegates_whole_mutable = false;
  bool get_returns_bit_set = false;
  bool set_or_clear_bit = false;
  uint32_t bit_mask = 0;
  uint32_t result_flags = 0;
};

struct SourceRndMeshPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> mutable_rows;
  std::vector<std::string> flag_rows;
  std::vector<std::string> superclasses;
};

struct SourceRndMeshPointCollidePlan {
  bool has_bsp_tree = false;
  bool reads_bsp_tree = true;
  bool reads_message_xyz = true;
  bool multiplies_world_xfm = true;
  bool calls_intersect = false;
  bool intersected = false;
  bool returns_hit = false;
};

struct SourceRndMeshAttachMeshPlan {
  bool reads_mesh_arg_2 = true;
  bool calls_attach_mesh_this = true;
  bool deletes_mesh_arg = true;
  bool returns_zero = true;
};

struct SourceRndMeshConfigureMeshPlan {
  bool type_is_configurable = false;
  bool warns_nonconfigurable = false;
  bool reads_left_right_height = false;
  bool assigns_four_vertex_positions = false;
  std::array<std::array<float, 3>, 4> positions = {};
  bool syncs = false;
  int32_t sync_mask = 0;
  bool returns_zero = true;
};

struct SourceRndMeshIndexedEditPlan {
  std::string row;
  int32_t count = 0;
  int32_t index = 0;
  int32_t value_count = 0;
  bool write = false;
  bool valid_index = false;
  int32_t assert_line = 0;
  int32_t sync_mask = 0;
  bool returns_zero = true;
};

struct SourceRndMeshUnitizeNormalsPlan {
  int32_t vertex_count = 0;
  int32_t normalized_count = 0;
  bool returns_zero = true;
};

struct SourceRndMeshVertVectorResizePlan {
  int32_t requested_count = 0;
  bool requested_unka = false;
  bool stores_unka = true;
  bool capacity_path = false;
  bool dynamic_path = false;
  bool assertion_would_fail = false;
  bool releases_verts = false;
  bool allocates_new_verts = false;
  bool copies_old_verts = false;
  int32_t copied_vert_count = 0;
  bool deletes_old_verts = false;
  int32_t resulting_count = 0;
};

struct SourceRndMeshVertVectorReservePlan {
  int32_t requested_capacity = 0;
  bool requested_unka = false;
  bool asserts_capacity_grows = true;
  bool asserts_above_current_count = true;
  bool assertion_would_fail = false;
  bool overflow_fail = false;
  bool clears_capacity_before_resize = false;
  SourceRndMeshVertVectorResizePlan resize_step;
  int32_t resulting_capacity = 0;
  int32_t resulting_count = 0;
};

struct SourceRndMeshKeepMeshDataPlan {
  bool changed = false;
  bool keep_mesh_data = false;
  bool clear_verts = false;
  bool clear_faces = false;
  bool clear_patches = false;
};

struct SourceRndMeshCollideShowingPlan {
  bool resets_last_collide = true;
  bool use_original_segment = false;
  bool invert_world_for_segment = false;
  bool multiply_segment_start_end = false;
  bool checks_bsp_tree = false;
  bool checks_triangle_volume = false;
  bool skins_triangle_vertices = false;
  bool uses_raw_vertex_positions = false;
  bool interpolates_segment_end = false;
  bool multiplies_hit_fraction = false;
  bool sets_plane_from_triangle = false;
  bool records_last_collide_face = false;
  bool transforms_bsp_plane_to_world = false;
  bool transforms_triangle_plane_to_world = false;
  bool returns_mesh = false;
};

struct SourceRndMeshUpdateSpherePlan {
  bool has_bones = false;
  bool make_world_sphere = false;
  bool make_world_sphere_uses_showing = true;
  bool invert_world = false;
  bool multiply_sphere_to_local = false;
  bool zero_sphere = false;
  bool set_drawable_sphere = true;
};

struct SourceRndMeshDistanceToPlaneResult {
  bool empty_vertices = false;
  bool uses_world_xfm = true;
  bool starts_from_first_vertex = false;
  size_t selected_vertex = 0;
  float distance = 0.0f;
};

struct SourceRndMeshSetVolumePlan {
  int32_t requested_volume = 0;
  bool owner_is_self = true;
  bool forwards_to_geom_owner = false;
  bool assigns_volume = false;
  bool releases_bsp_tree = false;
  bool checks_nonempty_geometry = false;
  bool enters_volume_box_branch = false;
  bool grows_box_from_vertices = false;
  bool creates_bsp_tree = false;
  bool volume_box_body_incomplete = false;
  bool enters_volume_bsp_branch = false;
  bool volume_bsp_body_incomplete = false;
};

struct SourceRndMeshPreLoadVerticesPlan {
  int32_t alt_revision = 0;
  bool creates_file_loader = false;
  bool load_front = true;
  bool keeps_bin_stream = true;
};

struct SourceRndMeshPostLoadVerticesPlan {
  int32_t mesh_revision = 0;
  int32_t compressed_size = 0;
  bool had_file_loader = false;
  bool releases_file_loader = false;
  bool wraps_buffer_stream = false;
  bool frees_temp_buffer = false;
  bool reads_compressed_flag = false;
  bool compressed_flag = false;
  int32_t loaded_compressed_size = 0;
  int32_t loaded_version = 0;
  bool asserts_vertex_compression_supported = false;
  bool unsupported_compression_fail = false;
  bool compressed_metadata_zero = false;
  bool warns_stale_compressed_data = false;
  bool stores_num_compressed_verts = false;
  int32_t num_compressed_verts = 0;
  bool debug_fail_if_compressed_size_nonzero = false;
  bool allocates_compressed_verts = false;
  bool reads_compressed_chunks = false;
  bool asserts_positive_seek = false;
  int32_t seek_bytes = 0;
  bool uncompressed_path = false;
  bool resize_verts = false;
  bool resize_bool = false;
  int32_t vertex_read_count = 0;
  int32_t temp_eof_poll_count = 0;
};

struct SourceRndMeshCreateMultiMeshPlan {
  bool owner_had_multimesh = false;
  bool creates_multimesh = false;
  bool sets_mesh_to_owner = false;
  bool clears_instances = true;
  bool returns_owner_multimesh = true;
};

struct SourceRndMeshCacheStripsPlan {
  bool stream_cached = false;
  bool platform_wii = false;
  bool owner_is_self = false;
  bool has_faces = false;
  bool has_verts = false;
  bool mutable_strip_disabled = false;
  bool cache_strips = false;
};

struct SourceRndMeshStriperResultReadPlan {
  int32_t nb_strips = 0;
  int32_t runs = 0;
  bool reads_nb_strips = true;
  bool reads_runs = true;
  bool allocates_lengths_and_runs = false;
  int32_t strip_lengths_bytes = 0;
  int32_t strip_runs_bytes = 0;
};

struct SourceRndMeshCreateStripPlan {
  int32_t face_start = 0;
  int32_t face_count = 0;
  bool wfaces_points_to_face_idx0 = true;
  bool connect_all_strips = false;
  bool one_sided = false;
  bool sgi_algorithm = false;
  bool asserts_striper_init = true;
  bool asserts_striper_compute = true;
  int32_t loop_start_index = 1;
  int32_t final_nb_strips = 0;
  bool missing_strip_length = false;
};

SourceRndMeshDefaultState source_rndmesh_default_state();
SourceRndMeshSavePlan source_rndmesh_save_plan();
SourceRndMeshDestructorPlan source_rndmesh_destructor_plan();
SourceRndMeshSetMatPlan source_rndmesh_set_mat_plan(
    bool material_pointer_present);
SourceRndMeshDebugCountsPlan source_rndmesh_debug_counts_plan(
    int32_t face_count,
    int32_t vert_count);
SourceRndMeshVolumeTextPlan source_rndmesh_volume_text_plan(int32_t volume);
SourceRndMeshPrintPlan source_rndmesh_print_plan();
int32_t source_rndmesh_max_bones();
SourceRndMeshSyncPlan source_rndmesh_sync_plan(int32_t mask,
                                               bool keep_mesh_data);
SourceRndMeshClearCompressedVertsPlan
source_rndmesh_clear_compressed_verts_plan();
SourceRndMeshCountPlan source_rndmesh_set_num_verts_plan(
    int32_t count,
    bool keep_mesh_data);
SourceRndMeshCountPlan source_rndmesh_set_num_faces_plan(
    int32_t count,
    bool keep_mesh_data);
SourceRndMeshFaceLoadPlan source_rndmesh_face_load_plan(
    int32_t mesh_revision);
SourceRndMeshFaceCenterResult source_rndmesh_face_center(
    const std::vector<std::array<float, 3>>& vertices,
    const SourceRndMeshFace& face);
SourceRndMeshHandlerPlan source_rndmesh_handler_plan();
SourceRndMeshPropSyncPlan source_rndmesh_prop_sync_plan();
SourceRndMeshMutableBitPlan source_rndmesh_mutable_bit_plan(
    uint32_t current_flags,
    uint32_t bit_mask,
    bool has_bit_subproperty,
    bool prop_get,
    bool set_value);
SourceRndMeshPointCollidePlan source_rndmesh_point_collide_plan(
    bool has_bsp_tree,
    bool intersected);
SourceRndMeshAttachMeshPlan source_rndmesh_attach_mesh_plan();
SourceRndMeshConfigureMeshPlan source_rndmesh_configure_mesh_plan(
    bool type_is_configurable,
    float left,
    float right,
    float height);
SourceRndMeshIndexedEditPlan source_rndmesh_vertex_edit_plan(
    int32_t vertex_count,
    int32_t index,
    const std::string& row,
    bool write);
SourceRndMeshIndexedEditPlan source_rndmesh_face_edit_plan(
    int32_t face_count,
    int32_t index,
    bool write);
SourceRndMeshUnitizeNormalsPlan source_rndmesh_unitize_normals_plan(
    int32_t vertex_count);
SourceRndMeshVertVectorResizePlan source_rndmesh_vert_vector_resize_plan(
    int32_t current_capacity,
    int32_t current_count,
    int32_t requested_count,
    bool resize_bool);
SourceRndMeshVertVectorReservePlan source_rndmesh_vert_vector_reserve_plan(
    int32_t current_capacity,
    int32_t current_count,
    int32_t requested_capacity,
    bool resize_bool);
SourceRndMeshKeepMeshDataPlan source_rndmesh_set_keep_mesh_data_plan(
    bool current_keep_mesh_data,
    bool requested_keep_mesh_data);
SourceRndMeshCollideShowingPlan source_rndmesh_collide_showing_plan(
    bool is_skinned,
    bool raw_collide,
    bool has_bsp_tree,
    bool volume_is_triangles,
    bool hit);
SourceRndMeshUpdateSpherePlan source_rndmesh_update_sphere_plan(
    bool has_bones);
SourceRndMeshDistanceToPlaneResult source_rndmesh_get_distance_to_plane(
    const std::vector<float>& world_plane_dots);
SourceRndMeshSetVolumePlan source_rndmesh_set_volume_plan(
    int32_t requested_volume,
    bool owner_is_self,
    bool has_vertices,
    bool has_faces);
SourceRndMeshPreLoadVerticesPlan source_rndmesh_pre_load_vertices_plan(
    int32_t alt_revision);
SourceRndMeshPostLoadVerticesPlan source_rndmesh_post_load_vertices_plan(
    int32_t mesh_revision,
    int32_t compressed_size,
    bool stream_compressed_flag,
    int32_t loaded_compressed_size,
    int32_t loaded_version,
    uint32_t mutable_flags,
    bool keep_mesh_data,
    bool has_file_loader);
SourceRndMeshCreateMultiMeshPlan source_rndmesh_create_multi_mesh_plan(
    bool owner_had_multimesh);
SourceRndMeshCacheStripsPlan source_rndmesh_cache_strips_plan(
    bool stream_cached,
    bool platform_wii,
    bool owner_is_self,
    int32_t face_count,
    int32_t vert_count,
    uint32_t mutable_flags);
SourceRndMeshStriperResultReadPlan source_rndmesh_striper_result_read_plan(
    int32_t nb_strips,
    int32_t runs);
SourceRndMeshCreateStripPlan source_rndmesh_create_strip_plan(
    int32_t face_start,
    int32_t face_count,
    int32_t nb_strips_after_compute,
    const std::vector<int32_t>& strip_lengths,
    bool one_sided);

struct RndMeshGroupSection {
  std::vector<int32_t> sections;
  std::vector<uint16_t> vert_offsets;
};

struct SkinnedMesh {
  std::string name;
  std::string parent;     // Trans parent (links into the skeleton/group tree)
  std::vector<std::string> legacy_children;  // Trans rev<9 authored child list
  std::string material;   // Mat entry this mesh draws with
  std::string geometry_owner;  // Mesh whose vertex/index buffers are shared
  milo_scene::Xfm local;  // the mesh's own Trans local matrix (usually identity)
  milo_scene::Xfm world_stored;
  uint32_t constraint = 0;   // RndTransformable::Constraint
  std::string target;        // dynamic constraint target, "" if absent
  bool preserve_scale = false;
  bool showing = true;
  float draw_order = 0.0f;
  uint32_t mutable_flags = 0;
  uint32_t volume = 1;  // RndMesh::kVolumeTriangles
  std::vector<uint8_t> group_sizes;
  std::vector<RndMeshGroupSection> group_sections;

  std::vector<SkinVertex> verts;
  std::vector<uint16_t> indices;  // face_count*3

  // Raw MILO bone-transform rows, before runtime ObjPtr/null trimming.
  std::vector<std::string> raw_bone_palette;
  std::vector<milo_scene::Xfm> raw_bind;

  // Runtime-active bone palette. Weight slot i of every vertex refers to
  // bone_palette[i] after source-style null/unresolved row trimming.
  std::vector<std::string> bone_palette;
  // One RndBone offset row per active palette bone. ihatecompvir's RB3 RndMesh
  // source computes this as mesh WorldXfm * inverse(bone WorldXfm), and
  // skinning consumes it as v * offset * current bone WorldXfm.
  std::vector<milo_scene::Xfm> bind;
  // True when the decoded offset rows reconstruct this mesh's own bind-space
  // row rather than model space. This is a generic PS2 format shape used by
  // the highway branch's attachment renderer.
  bool mesh_local_bind_space = false;
  float bb_min[3] = {0, 0, 0};
  float bb_max[3] = {0, 0, 0};
  bool decoded = false;
  std::string error;      // non-empty if decode failed (mesh still listed)
};

struct RndMorphKey {
  float weight = 0.0f;
  float frame = 0.0f;
};

struct RndMorphPose {
  std::string mesh;
  std::vector<RndMorphKey> keys;
};

// GH1 RndMorph revision 3 and GH2 revision 4. Each pose names a
// same-directory Mesh and carries authored weight keys; SetFrame blends those
// source vertices into target.
struct RndMorph {
  std::string name;
  int32_t revision = 0;
  int32_t anim_revision = 0;
  std::vector<std::string> anim_objects;
  std::vector<RndMorphPose> poses;
  std::string target;
  bool normals = false;
  bool spline = false;
  float intensity = 1.0f;
  bool decoded = false;
  std::string error;
};

struct SourceCharMeshCacher {
  std::string mesh;
  int32_t unk4 = 0;
  bool disabled = false;
  std::vector<int32_t> verts;
  std::vector<int32_t> unk14;
  std::vector<int32_t> unk1c;
};

struct SourceCharMeshCacheState {
  std::vector<SourceCharMeshCacher> cache;
  bool disabled = false;
};

struct SourceCharMeshCacheDisableResult {
  bool accepted = false;
  bool asserted_non_empty_cache = false;
};

struct SourceCharMeshCacheSyncResult {
  bool added = false;
  bool asserted_null_mesh = false;
  bool inline_cacher_body_visible = false;
  int32_t mask = 0;
  size_t index_after_scan = 0;
};

struct SourceCharMeshCacheVertsResult {
  bool found = false;
  std::vector<int32_t> verts;
};

struct CharUpperTwist {
  std::string name;
  std::string upper_arm;
  std::string twist1;
  std::string twist2;
};

struct CharForeTwist {
  std::string name;
  int32_t version = 0;
  float offset_degrees = 0.0f;
  std::string hand;
  std::string twist2;
  float bias_degrees = 0.0f;
};

// GH1 creates these controllers from charsys/gen/charbase.dtb at runtime.
// They are deliberately separate from the later MILO-serialized Char*Twist
// objects: GH1's revision-10 character transforms are zero-geometry RndMesh
// rows, while GH2's transforms live in Character::bones.
struct Gh1AnimServoForeTwist {
  std::string name;
  std::string fore_arm;
  std::string twist1;
  std::string twist2;
  std::string hand;
  float offset_degrees = 0.0f;
};

struct Gh1AnimServoUpperTwist {
  std::string name;
  std::string twist1;
  std::string twist2;
  std::string upper_arm;
};

struct CharNeckTwist {
  std::string name;
  int32_t version = 0;
  std::string head;
  std::string twist;
  size_t unread_bytes = 0;
};

struct CharIKRod {
  std::string name;
  int32_t version = 0;
  std::string left_end;
  std::string right_end;
  float dest_pos = 0.0f;
  std::string side_axis;
  bool vertical = false;
  std::string dest;
  float xfm[4][3] = {};
};

struct CharIKTarget {
  std::string target;
  float extent = 0.0f;
};

struct CharIKHand {
  std::string name;
  int32_t version = 0;
  int32_t unknown = 0;
  float weight = 1.0f;
  std::string weight_prop;
  std::string hand;
  std::string finger;
  std::string target;
  std::vector<CharIKTarget> targets;
  bool orientation = true;
  bool stretch = true;
  bool scalable = false;
  bool move_elbow = true;
  float elbow_swing = 0.0f;
  bool always_ik_elbow = false;
  bool constrain_wrist = false;
  float wrist_radians = 0.0f;
  std::string elbow_collide;
  bool clockwise = false;
  // GH1 creates AnimServoIK objects from charsys/gen/charbase.dtb at runtime.
  // These fields retain that older controller's explicit `(bones root count)`
  // contract without pretending it is a serialized GH2 CharIKHand revision.
  bool legacy_anim_servo_ik = false;
  std::string legacy_chain_root;
  int32_t legacy_chain_bones = 0;
  // Runtime-only bind-frame correction used when an external animation
  // owner's authored IK target drives a differently oriented target hand.
  // This is derived from the two factual bind skeletons; it is not serialized.
  bool external_retarget_orientation_correction = false;
  float external_retarget_orientation[3][3] = {
      {1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
  };
  bool external_retarget_contact_correction = false;
  float external_retarget_source_contact[3] = {};
  float external_retarget_target_contact[3] = {};
  size_t unread_bytes = 0;
};

struct CharIKMidi {
  std::string name;
  int32_t version = 0;
  std::string bone;
  std::vector<std::string> legacy_spots;
  std::string legacy_string;
  std::string anim_blender;
  float max_anim_blend = 1.0f;
  size_t unread_bytes = 0;
};

struct CharServoBone {
  std::string name;
  int32_t version = 0;
  std::string clip_type;
  size_t unread_bytes = 0;
};

struct CharLookAt {
  std::string name;
  int32_t version = 0;
  int32_t weightable_version = 0;
  float weight = 1.0f;
  std::string weight_owner;
  std::string source;
  std::string pivot;
  std::string dest;
  float half_time = 0.0f;
  float min_yaw = -80.0f;
  float max_yaw = 80.0f;
  float min_pitch = -80.0f;
  float max_pitch = 80.0f;
  float min_weight_yaw = -1.0f;
  float max_weight_yaw = 1.0f;
  float weight_yaw_speed = 10000.0f;
  bool allow_roll = true;
  bool enable_jitter = false;
  float yaw_jitter_limit = 0.0f;
  float pitch_jitter_limit = 0.0f;
  float source_radius = 0.0f;
  size_t unread_bytes = 0;
};

struct CharEyes {
  std::string name;
  int32_t version = 0;
  std::vector<std::string> lookats;
  std::string legacy_transform;
  size_t unread_bytes = 0;
};

struct CharHairPoint {
  float pos[3] = {0, 0, 0};
  // Source schema name: bone. This is the Trans row CharHair drives.
  std::string bone;
  float length = 0.0f;
  // GH2 v2 field names from ihatecompvir's CharHair source. Older revisions
  // carry legacy inline collision rows, but source then clears Point.collides;
  // native logs these fields but does not promote them into guessed collides.
  uint32_t collide_type = 0;
  std::string collision;
  float radius = 0.0f;
  float outer_radius = -1.0f;
  float side_length = -1.0f;
  float unk5c[3] = {0, 0, 0};
};

struct CharHairStrand {
  bool show_spheres = false;
  bool show_collide = false;
  bool show_pose = false;
  std::string root;
  float angle = 0.0f;  // degrees
  std::vector<CharHairPoint> points;
  float base_mat[9] = {1.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f};
  float root_mat[9] = {1.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f};
  int32_t hookup_flags = 0;
};

struct CharHair {
  std::string name;
  int32_t version = 0;
  float stiffness = 0.04f;
  float torsion = 0.1f;
  float inertia = 0.7f;
  float gravity = 1.0f;
  float weight = 0.5f;
  float friction = 0.3f;
  float min_slack = 0.0f;
  float max_slack = 0.0f;
  std::vector<CharHairStrand> strands;
  bool simulate = true;
  std::string wind;
  size_t unread_bytes = 0;
  std::string unread_tail_hex;
};

struct SourceCharHairDefaultState {
  float stiffness = 0.04f;
  float torsion = 0.1f;
  float inertia = 0.7f;
  float gravity = 1.0f;
  float weight = 0.5f;
  float friction = 0.3f;
  float min_slack = 0.0f;
  float max_slack = 0.0f;
  int reset = 1;
  bool simulate = true;
  bool use_post_proc = true;
  bool managed_hookup = false;
};

struct SourceCharHairPointDefaultState {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  bool bone_null = true;
  float length = 0.0f;
  bool collides_empty = true;
  float radius = 0.0f;
  float outer_radius = -1.0f;
  float force[3] = {0.0f, 0.0f, 0.0f};
  float last_friction[3] = {0.0f, 0.0f, 0.0f};
  float last_z[3] = {0.0f, 0.0f, 0.0f};
  float unk5c[3] = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairStrandDefaultState {
  bool show_spheres = false;
  bool show_collide = false;
  bool show_pose = false;
  bool root_null = true;
  float angle = 0.0f;
  bool points_empty = true;
  float base_mat[9] = {1.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f};
  float root_mat[9] = {1.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f};
  int32_t hookup_flags = 0;
};

struct SourceCharHairPointLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharHairStrandLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharHairLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceGrimCharHairPointLoadPlan {
  bool known_version = false;
  std::vector<std::string> grim_read_order;
  std::vector<std::string> rb3_rev2_equivalents;
};

struct SourceGrimCharHairLoadPlan {
  bool known_version = false;
  bool reads_object_meta = false;
  bool reads_min_slack = false;
  bool reads_wind = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  SourceGrimCharHairPointLoadPlan point;
};

struct SourceCharHairSavePlan {
  int32_t save_id = 0x41b;
};

struct SourceCharHairDestructorPlan {
  bool strand_body_no_op = true;
  bool hair_body_no_op = true;
  bool explicit_strand_cleanup = false;
  bool explicit_hair_cleanup = false;
  bool writes_transforms = false;
};

struct SourceCharHairSetNamePlan {
  bool call_hmx_object_set_name = true;
  bool assigns_character_owner = false;
  bool use_post_proc = false;
};

struct SourceCharHairHandlerPlan {
  std::vector<std::string> actions;
  std::vector<std::string> superclasses;
  int32_t check = 0x46f;
};

struct SourceCharHairPropSyncPlan {
  bool sets_global_point_owner = false;
  bool sets_global_strand_owner = true;
  bool sets_global_hair_owner = true;
  std::vector<std::string> point_properties;
  std::vector<std::string> strand_set_properties;
  std::vector<std::string> strand_properties;
  std::vector<std::string> hair_properties;
};

struct SourceCharHairDoResetPlan {
  bool walks_strands = true;
  bool requires_root_parent = true;
  std::vector<std::string> point_steps;
  bool temporarily_forces_simulate = true;
  float forced_inertia = 0.0f;
  float forced_friction = 0.0f;
  int32_t simulate_loop_count = 0;
  bool simulate_loop_uses_get_fps = true;
  bool restores_simulate = true;
  bool restores_inertia = true;
  bool restores_friction = true;
  int32_t next_reset = 0;
};

struct SourceCharHairRootNode {
  std::string bone;
  float local_y = 0.0f;
  std::array<float, 3> world_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> world_y_axis = {0.0f, 1.0f, 0.0f};
  std::array<float, 9> local_mat = {1.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f};
};

struct SourceGltfMiloHairNode {
  std::string name;
  int parent = -1;
  bool is_bone = false;
  bool weighted = false;
};

struct SourceGltfMiloHairPointNode {
  std::string name;
  std::array<float, 3> world_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> world_y_axis = {0.0f, 1.0f, 0.0f};
  bool has_parent_world_pos = false;
  std::array<float, 3> parent_world_pos = {0.0f, 0.0f, 0.0f};
};

struct SourceGltfMiloHairStrandHeaderNode {
  std::string name;
  std::array<float, 9> local_mat = {1.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f};
};

struct SourceGltfMiloHairStrandHeaderPlan {
  bool skipped_empty_chain = false;
  bool creates_strand = false;
  std::string root;
  bool copy_matrix3_call_site_source_backed = true;
  bool copies_first_local_matrix_to_base_mat = false;
  bool copies_first_local_matrix_to_root_mat = false;
  bool convert_coordinates_arg = false;
  bool uses_matrix_helper_when_converting = false;
  bool requires_unvendored_matrix_helper_when_converting = false;
  bool can_port_axis_conversion_math = false;
  std::array<float, 9> base_mat = {1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f};
  std::array<float, 9> root_mat = {1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f};
};

struct SourceGltfMiloHairPointExport {
  std::string bone;
  std::array<float, 3> pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> reset_pos = {0.0f, 0.0f, 0.0f};
  float length = 0.0f;
  float radius = 0.0f;
  float outer_radius = 0.0f;
  float side_length = -1.0f;
  bool used_next_bone_position = false;
  bool used_tip_direction = false;
  bool used_unit_y_fallback = false;
  bool length_from_next_bone = false;
  bool length_from_previous_point = false;
  bool length_from_parent = false;
  bool length_defaulted_to_five = false;
  bool reset_in_strand_root_parent_space = true;
};

struct SourceGltfMiloHairChainsResult {
  bool has_weighted_hair_bones = false;
  std::vector<std::string> roots;
  std::vector<std::vector<std::string>> chains;
  std::vector<std::string> warnings;
};

struct SourceGltfMiloHairRootDiscoveryResult {
  bool has_weighted_hair_bones = false;
  bool weighted_set_is_case_insensitive = true;
  bool root_dedupe_is_case_insensitive = true;
  std::vector<std::string> roots;
  std::vector<std::string> skipped_duplicate_roots;
};

struct SourceGltfMiloHairChildClassification {
  std::vector<std::string> hair_children;
  std::vector<std::string> non_hair_bone_children;
  std::vector<std::string> warnings;
};

struct SourceGltfMiloHairCollideExport {
  std::string collide_name;
  std::string mesh_name;
  std::string parent_name;
  int revision = 7;
  int object_revision = 2;
  int shape = 1;
  int flags = 0;
  bool mesh_y_bias = false;
  bool unknown_transform_identity = true;
  int struct_count = 8;
  bool exporter_marks_inferred = true;
};

struct SourceGltfMiloCharHairExportPlan {
  bool exits_for_empty_weighted_set = false;
  bool constructs_char_hair_object = false;
  bool exits_for_empty_strands = false;
  bool creates_entry = false;
  int revision = 0;
  int object_revision = 0;
  bool simulate = false;
  std::vector<std::string> physics_fields;
  bool uses_default_wind = false;
  std::string wind_source;
  std::string wind_value;
  std::string strand_collector;
  std::string entry_type;
  std::string entry_name;
};

struct SourceGltfMiloCharHairExtrasBoundary {
  bool char_hair_extras_source_present = true;
  bool detection_call_sites_source_backed = true;
  bool process_char_hair_call_sites_source_backed = true;
  bool can_port_discovery_gates = true;
  bool can_port_default_physics_values = true;
  bool can_port_default_wind_value = true;
  bool safe_to_tune_hair_physics_from_extras_defaults = true;
  std::vector<std::string> process_call_sites;
  std::vector<std::string> missing_helpers;
};

struct SourceGltfMiloCharHairExtrasDefaults {
  float stiffness = 0.04f;
  float torsion = 0.1f;
  float inertia = 0.7f;
  float gravity = 1.0f;
  float friction = 0.3f;
  float weight = 0.5f;
  std::string wind = "world.wind";
};

struct SourceGltfMiloHairSettingsDetectionPlan {
  bool is_hair_bone = false;
  bool checks_extras = false;
  bool contains_milo_hair_marker = false;
  bool attempts_deserialize = false;
  bool bad_extras_nonfatal = false;
  bool assigns_detected_settings = false;
  bool preserves_existing_settings = false;
};

struct SourceCharHairRuntimePoint {
  bool initialized = false;
  std::array<float, 3> pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> force = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> last_friction = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> last_z = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairRuntimeStrand {
  std::vector<SourceCharHairRuntimePoint> points;
};

struct SourceCharHairRuntime {
  bool initialized = false;
  bool use_post_proc = true;
  bool managed_hookup = false;
  bool band_character_hookup = false;
  bool default_hookup_returned_for_managed = false;
  bool hookup_collected_from_object_dir = false;
  bool hookup_overload_body_statement_visible = false;
  int legacy_inline_point_count = 0;
  int reset = 1;
  float last_time_seconds = -1.0f;
  std::vector<std::string> hookup_collides;
  std::vector<SourceCharHairRuntimeStrand> strands;
};

struct SourceCharHairPollDecision {
  bool hookup = false;
  bool teleported_reset = false;
  bool do_reset = false;
  int reset_count = 0;
  bool return_after_reset = false;
  bool simulate_loops = false;
  bool simulate_zero_time = false;
  int next_reset = 0;
};

struct SourceCharHairGetFpsResult {
  bool used_post_proc = false;
  bool adjusted_non_sixty = false;
  float emulated_fps = 0.0f;
  float fps = 60.0f;
};

struct SourceCharHairHookupPlan {
  bool returned_for_managed_hookup = false;
  std::vector<std::string> collected_collides;
  bool collected_from_object_dir = false;
  bool has_overload_declaration = true;
  bool overload_body_statement_visible = false;
  bool called_overloaded_hookup = false;
};

struct SourceBandCharacterHairHookupPlan {
  bool sets_managed_hookup = true;
  bool collects_hair_rows = true;
  bool collects_collide_rows = true;
  bool calls_overloaded_hookup_before_character_sync = true;
  bool default_hookup_would_return_for_managed = true;
  bool clears_collide_meshes_after_sync_when_not_in_closet = true;
  bool in_closet = false;
  std::vector<std::string> hair_rows;
  std::vector<std::string> collide_rows;
};

struct SourceCharHairPointCollideResolution {
  bool has_legacy_inline_rows = false;
  bool has_collision_name = false;
  bool has_collide_type = false;
  bool has_positive_radius = false;
  bool point_collides_cleared_by_loader = true;
  bool hookup_default_collects_dir_collides = true;
  bool hookup_overload_body_available = false;
  bool resolved_runtime_collides = false;
  bool may_write_world_xfm = false;
};

struct SourceCharHairWritebackGate {
  bool has_bone = false;
  int resolved_point_collide_count = 0;
  bool enters_collision_branch = false;
  bool rebuilds_basis = false;
  bool may_set_world_xfm = false;
  bool updates_force_state = false;
};

struct SourceCharHairHookupDumpEvidence {
  std::string range;
  std::vector<std::string> locals;
  std::vector<std::string> references;
  bool has_vector_collides = true;
  bool has_obj_dir_iterator = true;
  bool has_nested_loop_counters = true;
  bool has_char_collide_candidate = true;
  bool has_delta_root_distance_length = true;
  bool has_max_radius = true;
  bool has_statement_body = false;
};

struct SourceCharHairSimulateZeroTimeDumpEvidence {
  std::string range;
  std::vector<std::string> locals;
  bool has_outer_loop_counter = true;
  bool has_transform_local = true;
  bool has_point_vector = true;
  bool has_inner_loop_counter = true;
  bool has_matrix_local = true;
  bool has_statement_body = false;
};

struct SourceCharHairRb2MappedBodyEvidence {
  bool latest_header_declares_poll_deps = true;
  bool latest_cpp_has_poll_deps_statement_body = false;
  std::string set_root_range;
  std::vector<std::string> set_root_locals;
  std::string set_cloth_range;
  std::vector<std::string> set_cloth_locals;
  std::string set_angle_range;
  std::vector<std::string> set_angle_locals;
  std::string do_reset_range;
  std::vector<std::string> do_reset_locals;
  std::string poll_range;
  std::vector<std::string> poll_locals;
  std::vector<std::string> poll_references;
  std::string simulate_range;
  std::vector<std::string> simulate_locals;
  std::vector<std::string> simulate_references;
  std::string poll_deps_range;
  std::vector<std::string> poll_deps_locals;
  std::vector<std::string> poll_deps_references;
  bool poll_deps_has_loop_counter = true;
  bool poll_deps_has_statement_body = false;
  std::string copy_range;
  std::vector<std::string> copy_locals;
  std::vector<std::string> copy_references;
  bool copy_has_source_hair_local = true;
  bool copy_has_statement_body = false;
};

struct SourceCharHairEnterPlan {
  int next_reset = 1;
  bool called_rnd_pollable_enter = true;
  SourceCharHairHookupPlan hookup;
};

struct SourceCharHairSimulateLoopsPlan {
  bool entered = false;
  int collide_maintenance_count = 0;
  int simulate_internal_calls = 0;
  float fps = 0.0f;
};

struct SourceCharHairSimulateInternalScalars {
  float sixty_over_fps = 0.0f;
  float f19 = 0.0f;
  float stiffness_pow = 0.0f;
  std::array<float, 3> external_force = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairClothPairStep {
  bool entered = false;
  bool min_slack_applied = false;
  bool max_slack_applied = false;
  float lensq = 0.0f;
  float min_slack_length = 0.0f;
  float max_slack_length = 0.0f;
  std::array<float, 3> point_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> next_point_pos = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairLengthStep {
  std::array<float, 3> original_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> point_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> root_to_point = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> previous_force_delta = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> target_pos = {0.0f, 0.0f, 0.0f};
  float reciprocal_length = 0.0f;
  float length_scale = 0.0f;
};

struct SourceCharHairForceStep {
  std::array<float, 3> force = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> last_friction = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> friction_delta = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> motion_delta = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairCollisionInput {
  int shape = 1;
  float radius = 0.0f;
  std::array<float, 3> delta = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> axis = {0.0f, 1.0f, 0.0f};
};

struct SourceCharHairCollisionStep {
  bool entered = false;
  bool adjusted_point = false;
  bool z_overridden = false;
  bool z_interpolated = false;
  bool set_world_xfm = false;
  int collide_count = 0;
  std::array<float, 3> point_pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> pre_collision_z = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> last_z = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> basis_x = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> basis_y = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> basis_z = {0.0f, 0.0f, 0.0f};
};

struct SourceCharHairFreezePosePlan {
  bool called_hookup = true;
  SourceCharHairSimulateLoopsPlan simulate_loops;
  bool restored_simulate = true;
  bool restored_simulate_value = true;
  bool called_freeze_pose_raw = true;
};

struct SourceCharFaceServoBlinkClips {
  std::string left;
  std::string left2;
  std::string right;
  std::string right2;
};

struct SourceCharFaceServoBlinkState {
  float left = 0.0f;
  float right = 0.0f;
  bool need_scale_down = false;
};

struct SourceCharFaceServoTryScaleDownResult {
  bool consumed_need_scale_down = false;
  bool invoked_base_scale_down = false;
  bool reset_blink_weights = false;
};

struct SourceCharFaceServoScaleAddResult {
  bool accepted = false;
  bool scale_down = false;
  bool matched_left = false;
  bool matched_right = false;
  std::string downstream_call;
  float forwarded_weight = 0.0f;
  float forwarded_f2 = 0.0f;
  float forwarded_f3 = 0.0f;
};

struct SourceCharFaceServoLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool calls_set_clips = true;
  bool calls_set_clip_type = true;
};

struct SourceCharFaceServoCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  std::vector<std::string> post_copy_calls;
};

struct SourceCharFaceServoHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharFaceServoPropSyncPlan {
  std::vector<std::string> set_properties;
  std::vector<std::string> set_actions;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharFaceServoSavePlan {
  int32_t save_id = 0xCE;
};

struct SourceCharFaceServoEnterPlan {
  std::vector<std::string> calls;
  bool need_scale_down = true;
  float procedural_blink_weight = 0.0f;
};

struct SourceCharFaceServoSetClipsPlan {
  bool assigns_clips = true;
  std::vector<std::string> clip_lookups;
};

struct SourceCharFaceServoSetClipTypePlan {
  bool only_when_changed = true;
  std::vector<std::string> changed_calls;
};

struct SourceCharFaceServoPollPlan {
  std::vector<std::string> base_clip_calls;
  bool sets_need_scale_down = true;
  bool clears_applied_procedural_blink = true;
};

struct SourceCharFaceServoProceduralWeightsPlan {
  bool gated_by_positive_weight = true;
  bool gated_by_not_applied = true;
  std::vector<std::string> calls;
  bool skips_right_when_same_as_left = true;
  bool marks_applied = true;
};

struct SourceCharFaceServoProceduralWeightsResult {
  bool accepted = false;
  bool scale_down = false;
  bool left_applied = false;
  bool right_applied = false;
  bool applied_procedural_blink = false;
  float left_weight = 0.0f;
  float right_weight = 0.0f;
};

struct SourceCharFaceServoPollDepsPlan {
  bool change_list_gets_stuff_meshes = true;
};

struct SourceCharMeshHideRow {
  int32_t flags = 0;
  bool draw_showing = false;
  bool has_draw = false;
  bool show = false;
};

struct SourceCharMeshHideObject {
  int32_t flags = 0;
  std::vector<SourceCharMeshHideRow> hides;
};

struct SourceCharMeshHideRowLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharMeshHideLoadPlan {
  int32_t max_revision = 2;
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharMeshHideSavePlan {
  int32_t save_id = 0x6A;
};

struct SourceCharMeshHideCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  bool guards_hides_self_copy = true;
};

struct SourceCharMeshHideHandlerPlan {
  std::vector<std::string> superclasses;
  int32_t check = 0xA1;
};

struct SourceCharMeshHidePropSyncPlan {
  std::vector<std::string> hide_properties;
  std::vector<std::string> properties;
};

struct SourceCharTransCopyPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharTransCopyLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharTransCopySavePlan {
  int32_t save_id = 0x2D;
};

struct SourceCharTransCopyCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharTransCopyHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharTransCopyPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharPollGroupChildDeps {
  std::string changed_by;
  std::string change;
};

struct SourceCharPollGroupPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharPollGroupLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharPollGroupSavePlan {
  int32_t save_id = 0x58;
};

struct SourceCharPollGroupCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  std::vector<std::string> copy_from_max_steps;
};

struct SourceCharPollGroupHandlerPlan {
  std::vector<std::string> action_handlers;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharPollGroupPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharPollGroupSortPlan {
  std::vector<std::string> steps;
};

struct SourceWaypointState {
  int flags = 0;
  float radius = 12.0f;
  float y_radius = 0.0f;
  float ang_radius = 0.0f;
  float strict_ang_delta = 0.0f;
  float strict_radius_delta = 0.0f;
  std::vector<std::string> connections;
};

struct SourceWaypointRegistryState {
  bool allocated = false;
  std::vector<std::string> registered_functions;
  bool exit_callback_registered = false;
  std::vector<SourceWaypointState> waypoints;
};

struct SourceWaypointConstructorStep {
  SourceWaypointState waypoint;
  bool registry_push = false;
  bool random_branch_is_noop = true;
  size_t registry_size = 0;
};

struct SourceWaypointFindResult {
  int index = -1;
  bool found = false;
  int mask = 0;
};

struct SourceWaypointRegisteredCommandDumpEvidence {
  bool latest_registers_nearest = true;
  bool latest_registers_last = true;
  bool latest_source_has_nearest_body = false;
  bool latest_source_has_last_body = false;
  bool rb2_dump_has_find_nearest = true;
  bool rb2_dump_has_on_nearest = true;
  bool rb2_dump_has_on_last = true;
  bool rb2_dump_is_statement_body = false;
  bool promoted_to_native_runtime = false;
  std::vector<std::string> rb2_ranges;
  std::vector<std::string> rb2_locals;
};

struct SourceWaypointLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> revision_branches;
};

struct SourceWaypointCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceWaypointHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceWaypointPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> superclasses;
};

struct SourceWaypointSavePlan {
  int32_t save_id = 460;
};

struct SourceWaypointConstrainResult {
  milo_scene::Xfm constrained;
  std::array<float, 3> position_delta = {0.0f, 0.0f, 0.0f};
  float angle_delta = 0.0f;
  bool applied_radius = false;
  bool applied_angle = false;
};

struct SourceCharIKScaleDefaultState {
  float scale = 1.0f;
  float bottom_height = 0.0f;
  float top_height = 0.0f;
  bool auto_weight = false;
};

struct SourceCharIKScalePollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharIKScaleLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharIKScaleCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharIKScaleHandlerPlan {
  std::vector<std::string> superclasses;
  std::vector<std::string> actions;
  int check = 0;
};

struct SourceCharIKScalePropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharIKScaleSavePlan {
  int32_t save_id = 0x93;
};

enum class SourceCharacterDrawMode : int32_t {
  kNone = 0,
  kOpaque = 1,
  kTranslucent = 2,
  kAll = 3,
};

enum class SourceCharacterPollState : int32_t {
  kCreated = 0,
  kSyncObject = 1,
  kEntered = 2,
  kPolled = 3,
  kExited = 4,
};

struct SourceCharacterState {
  int32_t min_lod = 0;
  int32_t last_lod = 0;
  SourceCharacterPollState poll_state = SourceCharacterPollState::kCreated;
  bool frozen = false;
  SourceCharacterDrawMode draw_mode = SourceCharacterDrawMode::kAll;
  bool teleported = true;
  bool sphere_base_is_self = true;
  bool sphere_base_is_null = false;
  bool has_driver = false;
  std::string interest_to_force;
};

struct SourceCharacterLodState {
  float screen_size = 0.0f;
  std::string group;
  std::string trans_group;
};

struct SourceCharacterLodCopyPlan {
  std::vector<std::string> copied_members;
  bool assignment_returns_self = true;
};

struct SourceCharacterLodPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharacterLoadPlan {
  bool known_revision = false;
  std::vector<std::string> preload_steps;
  std::vector<std::string> postload_steps;
  std::vector<std::string> postload_reads;
  std::vector<std::string> branches;
};

struct SourceObjectDirDefaultState {
  bool proxy_override = false;
  bool inline_proxy = true;
  bool loader_null = true;
  bool is_subdir = false;
  int32_t inline_subdir_type = 0;
  bool path_name_null = true;
  bool current_camera_null = true;
  bool always_inlined = false;
  bool always_inline_hash_null = true;
};

struct SourceObjectDirSavePlan {
  int32_t save_id = 0x1A2;
};

struct SourceObjectDirPreLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool pushes_revision = false;
};

struct SourceObjectDirPostLoadPlan {
  std::vector<std::string> steps;
  std::vector<std::string> branches;
  bool pops_revision = true;
};

struct SourceObjectDirFindObjectPlan {
  std::vector<std::string> search_order;
  std::string result;
};

struct SourceObjectDirSubDirPlan {
  bool set_subdir_true = false;
  bool clears_name_and_type = false;
  bool added_sets_subdir_true = false;
  bool added_publishes_nested_objects = false;
  bool removing_sets_subdir_false = false;
  bool removing_publishes_nested_objects = false;
};

struct SourceRndDirDefaultState {
  bool env_null = true;
  int32_t draw_count = 0;
  int32_t anim_count = 0;
  int32_t poll_count = 0;
  std::string test_event;
};

struct SourceRndDirSavePlan {
  int32_t save_id = 0x1C1;
};

struct SourceRndDirLoadPlan {
  bool known_revision = false;
  std::vector<std::string> preload_steps;
  std::vector<std::string> postload_steps;
  std::vector<std::string> postload_reads;
  std::vector<std::string> branches;
};

struct SourceRndDirSyncObjectsPlan {
  bool clears_anims = true;
  bool clears_polls = true;
  bool calls_sync_drawables = false;
  bool collects_animatables = false;
  bool removes_anim_children = false;
  bool collects_pollables = false;
  bool removes_poll_children = false;
  bool sorts_polls = false;
  bool chains_source_subdir = false;
  bool calls_object_dir_sync = false;
};

struct SourceRndDirSyncDrawablesPlan {
  bool clears_draws = true;
  bool collects_drawables = false;
  bool updates_preclear_state = false;
  bool removes_draw_children = false;
  bool sorts_draws = false;
};

struct SourceRndDirCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::string member_gate;
  std::vector<std::string> copied_members;
};

struct SourceRndDirHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int32_t check = 0;
};

struct SourceRndDirPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharacterCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::string member_gate;
  std::vector<std::string> copied_members;
  bool creates_copy = true;
};

struct SourceCharacterHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> debug_handlers;
  std::string superclass;
  std::string check;
};

struct SourceCharacterPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> set_properties;
  std::vector<std::string> modify_properties;
  std::vector<std::string> debug_properties;
  std::string superclass;
};

struct SourceCharacterSavePlan {
  int32_t save_id = 0x495;
};

struct SourceCharacterPlayClipDecision {
  bool has_driver = false;
  bool would_assert_size = false;
  bool called_driver_play = false;
  int32_t play_flags = 4;
  float blend_width = -1.0f;
  float end_beat = 1.0e30f;
  float start_beat = 0.0f;
  bool returns_true = false;
};

struct SourceCharacterCopyBoundingSphereHandlerResult {
  bool copied = false;
  bool returns_zero = true;
};

struct SourceCharacterPollResult {
  bool called_rnd_dir_poll = false;
  bool skipped_for_frozen = false;
};

struct SourceCharacterSyncObjectsResult {
  bool converted_bones_to_transes = false;
  bool called_rnd_dir_sync_objects = false;
  bool removed_trans_group = false;
  int32_t removed_lod_draws = 0;
  bool synced_shadow = false;
  bool sorted_polls = false;
};

struct SourceCharacterRuntimeDumpEvidence {
  std::string poll_range;
  std::string bone_servo_range;
  std::string convert_bones_to_transes_range;
  std::string sync_objects_range;
  std::vector<std::string> poll_locals;
  std::vector<std::string> bone_servo_references;
  std::vector<std::string> convert_bones_to_transes_locals;
  std::vector<std::string> sync_objects_locals;
  bool has_statement_bodies = false;
  bool safe_to_publish_pose = false;
  bool safe_to_replace_pose_publisher = false;
};

struct SourceCharacterReplaceResult {
  bool called_rnd_dir_replace = false;
  bool repointed_sphere_base = false;
  bool fell_back_to_self = false;
};

struct SourceCharacterAddedObjectResult {
  bool accepted_pollable = false;
  bool assigned_main_driver = false;
};

struct SourceCharacterRemoveObjectResult {
  bool cleared_driver = false;
  bool called_rnd_dir_removing_object = false;
};

struct SourceCharacterInterestResult {
  bool found_eyes = false;
  bool invoked_eyes = false;
};

struct SourceCharacterCurrentInterestsResult {
  bool found_eyes = false;
  int32_t interest_count = 0;
  bool first_node_empty_symbol = true;
  std::vector<std::string> data_array_symbols;
};

struct SourceCharacterDebugDrawInterestResult {
  bool assigned = false;
  bool debug_draw_interest_objects = false;
};

struct SourceCharacterSetSphereBaseResult {
  bool defaulted_to_self = false;
  bool made_world_sphere = false;
  bool multiplied_by_trans_world = false;
  bool set_sphere = false;
};

struct SourceCharacterSetInterestObjectsResult {
  bool found_eyes = false;
  bool cleared_all = false;
  int32_t validated_count = 0;
  int32_t add_count = 0;
  int32_t used_override_dir_count = 0;
  int32_t used_interest_dir_count = 0;
};

struct SourceCharacterAddShadowBoneResult {
  bool returned_null = false;
  bool returned_existing = false;
  bool created = false;
  int32_t final_shadow_bones = 0;
};

struct SourceCharacterUnhookShadowResult {
  int32_t deleted_shadow_bones = 0;
  bool deleted_all = false;
};

struct SourceCharacterSyncShadowResult {
  bool unhooked_shadow = false;
  int32_t hooked_bone_count = 0;
  int32_t hooked_mesh_parent_count = 0;
  bool removed_shadow_draw = false;
};

struct SourceCharacterCopyBoundingSphereResult {
  bool set_sphere = false;
  bool copied_bounding = false;
  bool copied_sphere_base = false;
  bool cleared_sphere_base = false;
};

struct SourceCharacterRepointSphereBaseResult {
  bool had_sphere_base = false;
  bool looked_up_by_name = false;
  bool repointed = false;
};

struct SourceCharacterPreSaveResult {
  bool unhooked_shadow = false;
};

struct SourceBandCharacterDeformationPlan {
  bool has_deform_clip = false;
  bool edit_mode_bone_servo = false;
  bool in_closet = false;
  int32_t deform_weight_count = 18;
  int32_t sync_mesh_mask = 0xBF;
  bool poses_neutral_before_cache = false;
  bool poses_weighted_after_cache = false;
  bool captures_ik_scale_before = false;
  bool captures_ik_scale_after = false;
  bool measures_ik_hand_lengths_after_deform = false;
  bool clears_dirty_bit = false;
  std::vector<std::string> steps;
};

struct SourceCharPollableSorterDep {
  std::string name;
  std::vector<int32_t> changed_by;
  int32_t search_id = 0;
};

struct SourceCharPollableSorterChangedByResult {
  bool changed_by = false;
  bool same_dep_short_circuit = false;
  int32_t search_id = 0;
  std::vector<int32_t> visited_indices;
};

struct SourceCharLifecyclePlan {
  std::vector<std::string> init_steps;
  std::vector<std::string> terminate_steps;
};

struct SourceCharacterTestState {
  std::string show_dist_map = "none";
  int32_t transition = 0;
  bool cycle_transition = true;
  bool metronome = false;
  bool zero_travel = false;
  bool show_screen_size = false;
  bool show_foot_extents = false;
  int32_t unk68 = 0;
  bool overlay_requested = true;
};

struct SourceCharacterTestDestroyResult {
  bool looked_up_overlay = true;
  bool cleared_callback = false;
  bool hid_overlay = false;
  bool restarted_timer = false;
};

struct SourceCharacterTestDrawResult {
  bool highlighted_driver = false;
  std::string draw_transform;
  bool drew_screen_size = false;
};

struct SourceCharacterTestPollInput {
  bool has_driver = false;
  bool has_clip_dir = false;
  bool has_clip1 = false;
  bool has_clip2 = false;
  bool static_click_present = false;
  bool metronome = false;
  float beat = 0.0f;
  float delta_beat = 0.0f;
  bool has_first_driver = false;
  bool first_clip_is_clip1 = false;
  bool first_clip_is_clip2 = false;
  float transition_beat = 0.0f;
  float first_driver_beat = 0.0f;
  bool zero_travel = false;
  bool has_bone_servo = false;
};

struct SourceCharacterTestPollResult {
  bool entered_clip_branch = false;
  bool loaded_click_cue = false;
  bool restored_click_static = false;
  bool metronome_edge = false;
  bool would_play_click = false;
  bool play_new = false;
  bool reset_bone_servo_regulate = false;
  bool recenter = false;
};

struct SourceCharacterTestExisting {
  bool has_main_driver = false;
  bool has_bone_servo = false;
  bool has_bone_servo_object = false;
  bool has_fore_twist_l = false;
  bool has_fore_twist_r = false;
  bool has_upper_twist_l = false;
  bool has_upper_twist_r = false;
};

struct SourceCharacterTestBones {
  bool bone_l_hand = false;
  bool bone_l_fore_twist2 = false;
  bool bone_r_hand = false;
  bool bone_r_fore_twist2 = false;
  bool bone_l_upper_twist1 = false;
  bool bone_l_upper_twist2 = false;
  bool bone_l_upper_arm = false;
  bool bone_r_upper_twist1 = false;
  bool bone_r_upper_twist2 = false;
  bool bone_r_upper_arm = false;
};

struct SourceCharacterTestControllerSetup {
  std::string name;
  std::string hand;
  std::string twist1;
  std::string twist2;
  std::string upper_arm;
  bool has_offset = false;
  float offset = 0.0f;
};

struct SourceCharacterTestAddDefaultsResult {
  bool created_main_driver = false;
  bool created_bone_servo = false;
  bool set_driver_bones_to_bone_servo = false;
  std::vector<SourceCharacterTestControllerSetup> controllers;
};

struct SourceCharacterTestStartEndBeatResult {
  bool found_milo = false;
  bool current_anim_is_object = false;
  bool current_anim_is_me = false;
  bool unfroze_character = false;
  bool set_bpm = false;
  bool sent_set_anim_frame = false;
  float start_frame = 0.0f;
  float end_frame = 0.0f;
  int32_t bpm = 0;
};

struct SourceCharacterTestLoadResult {
  bool fail_new_revision = false;
  bool fail_new_alt_revision = false;
  bool loaded_driver = false;
};

struct SourceCharTransDrawCharacter {
  std::string name;
  bool showing = false;
};

struct SourceCharTransDrawStep {
  std::string character;
  SourceCharacterDrawMode mode = SourceCharacterDrawMode::kAll;
  bool draw = false;
};

struct SourceCharTransDrawLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  SourceCharacterDrawMode post_load_mode = SourceCharacterDrawMode::kOpaque;
};

struct SourceCharTransDrawSavePlan {
  int32_t save_id = 0x23;
};

struct SourceCharTransDrawCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharTransDrawHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharTransDrawPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharCuffShape {
  float offset = 0.0f;
  float radius = 0.0f;
};

struct SourceCharCuffState {
  SourceCharCuffShape shape[3];
  float outer_radius = 0.0f;
  bool open_end = false;
  std::string bone;
  float eccentricity = 1.0f;
  std::string category;
  std::vector<std::string> ignore;
};

struct SourceCharCuffLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool warns_old_revision = false;
};

struct SourceCharCuffSavePlan {
  int32_t save_id = 0x1A2;
};

struct SourceCharCuffCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharCuffHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharCuffPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharCuffTransformNode {
  std::string name;
  std::vector<SourceCharCuffTransformNode> children;
};

struct SourceCharCuffDeformRuntimeMap {
  bool rb3_latest_deform_declared = true;
  bool rb3_latest_bone_mask_body_incomplete = true;
  bool rb2_dump_maps_runtime_functions = true;
  bool rb2_dump_has_statement_body = false;
  bool safe_to_publish_mesh_writes = false;
  std::vector<std::string> runtime_functions;
  std::vector<std::string> deform_locals;
  std::vector<std::string> deform_mesh_locals;
};

struct SourceCharBlendBoneConstraint {
  std::string target;
  float weight = 0.5f;
};

struct SourceCharBlendBoneState {
  std::vector<SourceCharBlendBoneConstraint> targets;
  std::string src1;
  std::string src2;
  bool trans_x = false;
  bool trans_y = false;
  bool trans_z = false;
  bool rotation = false;
};

struct SourceCharBlendBonePollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharBlendBoneConstraintLoadPlan {
  std::vector<std::string> read_order;
};

struct SourceCharBlendBoneLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharBlendBoneSavePlan {
  int32_t save_id = 0x44;
};

struct SourceCharBlendBoneCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharBlendBoneHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharBlendBoneConstraintPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharBlendBonePropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharBlendBoneRuntimeDumpEvidence {
  std::string replace_range;
  std::string poll_range;
  std::string poll_deps_range;
  std::string load_range;
  std::string sync_property_range;
  std::vector<std::string> replace_locals;
  std::vector<std::string> poll_locals;
  std::vector<std::string> poll_deps_locals;
  std::vector<std::string> load_locals;
  std::vector<std::string> sync_property_symbols;
  bool rb3_latest_declares_poll = true;
  bool rb3_latest_has_poll_body = false;
  bool rb2_dump_has_statement_body = false;
  bool safe_to_import_replace = false;
  bool safe_to_import_poll = false;
};

struct SourceCharSleeveState {
  std::array<float, 3> pos = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> last_pos = {0.0f, 0.0f, 0.0f};
  float last_dt = 0.0f;
  float inertia = 0.5f;
  float gravity = 1.0f;
  float range = 0.0f;
  float neg_length = 0.0f;
  float pos_length = 0.0f;
  float stiffness = 0.02f;
};

struct SourceCharSleevePollResult {
  bool wrote_sleeve = false;
  bool wrote_top_sleeve = false;
  milo_scene::Xfm sleeve_world;
  milo_scene::Xfm top_sleeve_world;
};

struct SourceCharSleevePollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharSleeveSetNameStep {
  bool calls_hmx_object_set_name = true;
  bool assigns_character_owner = false;
};

struct SourceCharSleeveHighlightPlan {
  bool exits_without_sleeve_or_parent = false;
  std::vector<std::string> draw_steps;
};

struct SourceCharSleeveLoadPlan {
  bool revision_supported = false;
  std::vector<std::string> read_order;
};

struct SourceCharSleeveSavePlan {
  int32_t save_id = 0xE1;
};

struct SourceCharSleeveCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharSleeveHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharSleevePropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharGuitarStringPollResult {
  bool wrote_bend = false;
  std::array<float, 3> bend_pos = {0.0f, 0.0f, 0.0f};
};

struct SourceCharGuitarStringPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharGuitarStringDefaultState {
  bool open = false;
};

struct SourceCharGuitarStringLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharGuitarStringCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharGuitarStringHandlerPlan {
  std::vector<std::string> actions;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharGuitarStringPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharGuitarStringSavePlan {
  int32_t save_id = 0x47;
};

struct SourceCharEyesInterest {
  std::string interest;
  bool same_dir = false;
};

struct SourceCharEyesEyeDesc {
  std::string eye;
  std::string upper_lid;
  std::string lower_lid;
  std::string lower_lid_blink;
  std::string upper_lid_blink;
};

struct SourceCharEyesEyeDescLoadPlan {
  std::vector<std::string> read_order;
};

struct SourceCharEyesClampRow {
  bool has_eye = false;
  bool clamped = false;
};

struct SourceCharEyesPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharEyesRuntimeDumpEvidence {
  std::string poll_range;
  std::string next_look_range;
  std::string replace_range;
  std::string list_poll_children_range;
  std::string poll_deps_range;
  std::string gh2_xex_char_lookat_poll_range;
  std::string gh2_xex_char_eyes_next_look_range;
  std::string gh2_xex_char_eyes_poll_range;
  std::vector<std::string> poll_locals;
  std::vector<std::string> next_look_locals;
  bool rb2_dump_has_statement_body = false;
  bool latest_source_has_poll_body = false;
  bool latest_source_has_get_target_body = false;
  bool gh2_xex_owns_generated_target = false;
  bool gh2_xex_interest_can_replace_target = false;
  bool gh2_xex_assigns_target_to_every_lookat = false;
  bool safe_to_publish_destination_links = false;
  bool safe_to_publish_stock_v2_lookat_local = false;
  bool safe_to_publish_eye_runtime_rows = false;
  bool safe_to_infer_facefx_rows = false;
};

// Direct GH2 XEX NextLook publication evidence. This models only the resolved
// object-link write proved by sub_82170130: CharEyes writes its owned target's
// local transform, optionally chooses a qualifying interest instead, then
// assigns that one chosen object to every child CharLookAt destination.
struct SourceGh2CharEyesNextLookPublication {
  std::string generated_target;
  std::string chosen_target;
  bool generated_target_local_written = false;
  bool used_interest = false;
  std::vector<std::string> lookats;
  std::vector<std::string> destination_targets;
  bool reset_last_look = false;
  bool reset_average_delta = false;
  bool reset_last_cang = false;
};

// Direct GH2 XEX generated-target math from CharEyes::NextLook. random_unit is
// the already-sampled [0,1] value consumed by RandomFloat(20, 100). The
// optional floor is the WorldXfm().v.z of this object's ObjectDir when that
// directory dynamically casts to RndTransformable.
struct SourceGh2CharEyesGeneratedTargetResult {
  std::array<float, 3> facing_delta = {};
  float facing_delta_limit = 0.0f;
  bool facing_delta_clamped = false;
  std::array<float, 3> projected_facing = {};
  float random_distance = 0.0f;
  std::array<float, 3> target = {};
  bool floor_clamped = false;
  float floor_scale = 1.0f;
};

// Direct GH2 XEX random state used by CharEyes. The seed routine fills 256
// words, while the lagged-XOR sampler advances only through the first 250 with
// cursors initially separated by 103 entries.
struct SourceGh2RandomState {
  std::array<uint32_t, 256> words = {};
  uint32_t first_index = 0;
  uint32_t second_index = 103;
};

// Direct GH2 XEX CharEyes::Enter/Poll scheduler state. The field names express
// the values proved at sub_82170078 and sub_82170BA8 rather than assigning
// meanings to any untraced neighboring controller storage.
struct SourceGh2CharEyesPollState {
  std::array<float, 3> last_facing = {};
  float seconds_since_look = 0.0f;
  float last_cang = 1.0f;
  float average_delta = 0.0f;
  float previous_blink_weight = 0.0f;
  float previous_blink_delta = 0.0f;
};

struct SourceGh2CharEyesPollResult {
  std::array<float, 3> previous_facing = {};
  float cang = 0.0f;
  float blink_delta = 0.0f;
  bool blink_trigger = false;
  bool timeout_trigger = false;
  bool facing_trigger = false;
  bool called_next_look = false;
};

struct SourceCharEyesFocusResult {
  bool accepted = false;
  std::string focus_interest;
  int focus_priority = -1;
};

struct SourceCharEyesOverlayToggleResult {
  bool has_overlay = false;
  bool showing = false;
  bool timer_restarted = false;
};

struct SourceCharEyesForceBlinkState {
  bool pending_blink = false;
  float blink_time = -1.0f;
  int blink_count_delta = 0;
};

struct SourceCharEyesDefaultState {
  size_t eye_count = 0;
  size_t interest_count = 0;
  bool has_face_servo = false;
  bool has_cam_weight = false;
  std::array<float, 3> unk58 = {0.0f, 0.0f, 0.0f};
  int default_filter_flags = 0;
  bool has_view_direction = false;
  bool has_head_lookat = false;
  float max_extrapolation = 19.5f;
  float min_target_dist = 35.0f;
  float upper_lid_track_up = 1.0f;
  float upper_lid_track_down = 1.0f;
  float lower_lid_track_up = 0.75f;
  float lower_lid_track_down = 0.75f;
  int lower_lid_track_rotate = 0;
  int interest_filter_flags = 0;
  std::array<float, 3> unka4 = {0.0f, 0.0f, 0.0f};
  int unkb4 = 0;
  float unkb8 = 0.0f;
  float unkc0 = 0.0f;
  int unkc4 = 0;
  bool unkc5 = false;
  bool has_current_interest = false;
  bool has_focus_interest = false;
  int focus_priority = -1;
  bool unke4 = false;
  bool unke8 = false;
  float unkec = 1.0f;
  bool unkf0 = false;
  bool unkf4 = false;
  bool unk124 = false;
  float unk128 = -1.0f;
  int unk12c = -1;
  bool unk13c = false;
  float unk140 = -1.0f;
  int unk144 = 0;
  float unk148 = -1.0f;
  float unk14c = -1.0f;
  bool unk15c = false;
  bool unk15d = true;
  std::string overlay_name;
};

struct SourceCharEyesLoadPlan {
  bool revision_supported = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharEyesCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharEyesHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> action_handlers;
  std::vector<std::string> debug_handlers;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharEyesPropSyncPlan {
  std::vector<std::string> eye_desc_properties;
  std::vector<std::string> interest_state_properties;
  std::vector<std::string> properties;
  std::vector<std::string> bitfield_properties;
  std::vector<std::string> debug_properties;
  std::vector<std::string> superclasses;
};

struct SourceCharEyesSavePlan {
  int32_t save_id = 0x575;
};

struct SourceCharEyesBitfieldPropResult {
  int flags = 0;
  bool get_value = false;
};

struct SourceCharEyesFilterFlagsResult {
  int flags = 0;
  bool marked_changed = false;
};

struct SourceCharEyesEnterState {
  std::array<float, 3> unka4 = {0.0f, 0.0f, 0.0f};
  int unkb4 = 0;
  int unkbc = 0;
  float unkb0 = 1.0f;
  float unkc0 = -1.0f;
  int unkc4 = 0;
  bool unk124 = false;
  float unk128 = -1.0f;
  int unk12c = -1;
  bool unk13c = false;
  float unk140 = -1.0f;
  int unk144 = 0;
  float unk148 = -1.0f;
  float unk14c = -1.0f;
  bool unkc5 = false;
  int interest_filter_flags = 0;
  bool unk15c = false;
  bool unke4 = false;
  bool unkf4 = false;
  size_t eye_enter_count = 0;
  size_t interest_reset_count = 0;
  bool pollable_enter = true;
};

struct SourceCharEyesExitState {
  std::string focus_interest;
  int focus_priority = -1;
  bool clear_interests = true;
  size_t eye_exit_count = 0;
  bool pollable_exit = true;
};

struct SourceCharEyesInterestRuntime {
  std::string interest;
  float refractory_start = -1.0f;
};

struct SourceCharEyeDartRulesetData {
  float min_radius = 0.5f;
  float max_radius = 3.0f;
  float on_target_angle_thresh = 5.0f;
  int min_darts_per_sequence = 2;
  int max_darts_per_sequence = 5;
  float min_secs_between_darts = 0.25f;
  float max_secs_between_darts = 0.65f;
  float min_secs_between_sequences = 1.0f;
  float max_secs_between_sequences = 2.0f;
  bool scale_with_distance = true;
  float reference_distance = 70.0f;
};

struct SourceCharEyeDartRulesetLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharEyeDartRulesetCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  bool max_radius_from_min_radius = true;
};

struct SourceCharEyeDartRulesetPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharEyeDartRulesetHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharEyeDartRulesetSavePlan {
  int32_t save_id = 0x2B;
};

struct SourceCharInterestState {
  float max_view_angle = 20.0f;
  float priority = 1.0f;
  float min_look_time = 1.0f;
  float max_look_time = 3.0f;
  float refractory_period = 6.1f;
  std::string dart_override;
  int category_flags = 0;
  bool override_min_target_distance = false;
  float min_target_distance_override = 35.0f;
  float max_view_angle_cos = 0.0f;
};

struct SourceCharInterestLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  bool sync_max_view_angle = false;
};

struct SourceCharInterestCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  bool sync_max_view_angle = true;
};

struct SourceCharInterestPropSyncPlan {
  std::vector<std::string> modify_properties;
  std::vector<std::string> modify_actions;
  std::vector<std::string> properties;
  std::vector<std::string> custom_branches;
  std::vector<std::string> superclasses;
};

struct SourceCharInterestCategoryFlagsPropPlan {
  bool accepts_raw_category_flags = true;
  bool accepts_int_bit = true;
  bool accepts_symbol_bit_prefix = true;
  std::string required_symbol_prefix = "BIT_";
  std::vector<std::string> operations;
};

struct SourceCharInterestHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharInterestSavePlan {
  int32_t save_id = 0x52;
};

struct SourceCharInterestHighlightPlan {
  std::vector<std::string> graph_calls;
  bool projects_label = false;
  float label_offset_x = -30.0f;
  float label_offset_y = 15.0f;
  bool queries_dart_min_radius = false;
  bool queries_dart_max_radius = false;
  bool safe_to_publish_runtime_target = false;
};

struct SourceCharInterestComputeScorePlan {
  std::vector<std::string> gates;
  std::vector<std::string> score_steps;
  bool contains_random_float = true;
  bool safe_to_publish_runtime_score = false;
};

struct SourceCharInterestScoreResult {
  bool category_gate = false;
  bool default_category_gate = false;
  bool returned_reject = false;
  float distance_squared = 0.0f;
  float view_dot = 0.0f;
  bool view_dot_gate = false;
  float interest_dot = 0.0f;
  bool interest_dot_gate = false;
  float distance_score = 0.0f;
  bool distance_score_was_nan = false;
  float pre_jitter_score = 0.0f;
  bool applied_random_jitter = false;
  float score = -1.0f;
};

struct SourceCharNeckTwistState {
  std::string twist;
  std::string head;
};

struct SourceCharNeckTwistPollDeps {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

struct SourceCharNeckTwistLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharNeckTwistCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharNeckTwistHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharNeckTwistPropSyncPlan {
  std::vector<std::string> properties;
};

struct SourceCharNeckTwistSavePlan {
  int32_t save_id = 0x4A;
};

struct SourceCharNeckTwistPollPlan {
  bool entered_head_twist_gate = false;
  bool entered_twist_parent_gate = false;
  bool reached_twist_parent = false;
  size_t parent_multiply_count = 0;
  std::array<float, 9> accumulated_matrix = {1.0f, 0.0f, 0.0f,
                                             0.0f, 1.0f, 0.0f,
                                             0.0f, 0.0f, 1.0f};
  std::array<float, 3> accumulated_x = {1.0f, 0.0f, 0.0f};
  std::array<float, 3> accumulated_y = {0.0f, 1.0f, 0.0f};
  bool applied_make_rot_quat_unit_x = false;
  std::array<float, 3> rotated_y_after_make_rot_quat_unit_x =
      {0.0f, 1.0f, 0.0f};
  bool writes_twist_local_rotate_x = false;
  float rotate_about_x_radians = 0.0f;
};

struct SourceCharIKFingersState {
  int blend_in_frames = 0;
  int blend_out_frames = 0;
  bool reset_hand_dest = true;
  bool reset_cur_hand_trans = true;
  float finger_curled_length = 0.85f;
  std::array<float, 3> hand_keyboard_offset = {0.3f, -6.0f, 0.4f};
  float hand_move_forward = 1.0f;
  float hand_pinky_rotation = -0.06f;
  float hand_thumb_rotation = 0.23f;
  float hand_dest_offset = -0.4f;
  bool is_right_hand = true;
  bool move_hand = false;
  bool is_setup = false;
  std::string output_trans;
  std::string keyboard_ref_bone;
  size_t finger_count = 5;
};

struct SourceCharIKFingersFingerRefs {
  std::string finger01;
  std::string finger02;
  std::string finger03;
  std::string fingertip;
};

struct SourceCharIKFingersSetupRefs {
  bool is_right_hand = true;
  std::string hand;
  std::string forearm;
  std::string upperarm;
  std::array<SourceCharIKFingersFingerRefs, 5> fingers;
  std::array<float, 9> raw_matrix = {};
};

struct SourceCharIKFingersSetFingerPlan {
  bool known_finger = false;
  int finger = -1;
  bool assign_primary_vector = false;
  bool assign_secondary_vector = false;
  bool set_active = false;
  bool mark_dirty = false;
  bool multiply_finger01_by_current_hand = false;
  int blend_in_frames = 5;
  int finger_blend_in_frames = 5;
  int finger_blend_out_frames = 0;
};

struct SourceCharIKFingersReleaseFingerPlan {
  bool known_finger = false;
  int finger = -1;
  bool clear_active = false;
  bool mark_dirty = false;
  int finger_blend_out_frames = 0;
  int finger_blend_in_frames = 5;
};

struct SourceCharIKFingersLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
};

struct SourceCharIKFingersCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharIKFingersHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharIKFingersPropSyncPlan {
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharIKFingersRuntimeBoundary {
  bool rb3_latest_has_set_finger_body = true;
  bool rb3_latest_set_finger_transform_math_incomplete = true;
  bool rb3_latest_has_poll_body = true;
  bool rb3_latest_poll_body_is_stub = true;
  bool rb3_latest_declares_measure_lengths = true;
  bool rb3_latest_has_measure_lengths_body = false;
  bool rb3_latest_declares_poll_deps = true;
  bool rb3_latest_has_poll_deps_body = false;
  bool rb2_dump_has_char_ik_fingers_cpp = false;
  bool safe_to_import_finger_solve = false;
  bool safe_to_publish_runtime_finger_transforms = false;
  std::vector<std::string> unresolved_bodies;
};

struct SourceCharIKFingersSavePlan {
  int32_t save_id = 0x36A;
};

// Port of ihatecompvir RB3 CharHair::SetCloth: side_length is derived only
// from the matching point in the next strand, wrapping around the strand list.
void source_char_hair_set_cloth(CharHair& hair, bool enabled);
SourceCharHairDefaultState source_char_hair_default_state();
SourceCharHairPointDefaultState source_char_hair_point_default_state();
SourceCharHairStrandDefaultState source_char_hair_strand_default_state();
SourceCharHairPointLoadPlan source_char_hair_point_load_plan(int revision);
SourceCharHairStrandLoadPlan source_char_hair_strand_load_plan(int revision);
SourceCharHairLoadPlan source_char_hair_load_plan(int revision);
SourceGrimCharHairLoadPlan source_grim_char_hair_load_plan(int version);
uint32_t source_grim_char_hair_collide_type(uint32_t raw);
SourceCharHairSavePlan source_char_hair_save_plan();
SourceCharHairDestructorPlan source_char_hair_destructor_plan();
SourceCharHairSetNamePlan source_char_hair_set_name_plan(
    bool owner_is_character,
    bool owner_is_world_dir);
SourceCharHairHandlerPlan source_char_hair_handler_plan();
SourceCharHairPropSyncPlan source_char_hair_prop_sync_plan();
SourceCharHairDoResetPlan source_char_hair_do_reset_plan(int reset);
bool source_char_hair_set_name_use_post_proc(bool owner_is_character,
                                             bool owner_is_world_dir);
void source_char_hair_set_managed_hookup(SourceCharHairDefaultState& state,
                                         bool managed_hookup);
SourceCharHairGetFpsResult source_char_hair_get_fps_result(
    bool use_post_proc,
    float emulated_fps);
float source_char_hair_get_fps(bool use_post_proc, float emulated_fps);
SourceCharHairHookupDumpEvidence source_char_hair_hookup_dump_evidence();
SourceCharHairSimulateZeroTimeDumpEvidence
source_char_hair_simulate_zero_time_dump_evidence();
SourceCharHairRb2MappedBodyEvidence
source_char_hair_rb2_mapped_body_evidence();
SourceCharHairHookupPlan source_char_hair_hookup_plan(
    bool managed_hookup,
    const std::vector<std::string>& dir_collides);
SourceBandCharacterHairHookupPlan source_band_character_hair_hookup_plan(
    const std::vector<std::string>& hair_rows,
    const std::vector<std::string>& collide_rows,
    bool in_closet);
SourceCharHairPointCollideResolution
source_char_hair_point_collide_resolution(const CharHairPoint& point);
SourceCharHairWritebackGate source_char_hair_writeback_gate(
    bool has_bone,
    int resolved_point_collide_count);
SourceCharHairEnterPlan source_char_hair_enter_plan(
    bool managed_hookup,
    const std::vector<std::string>& dir_collides);
SourceCharHairSimulateLoopsPlan source_char_hair_simulate_loops_plan(
    bool simulate,
    int strand_count,
    int collide_count,
    int loop_count,
    float fps);
SourceCharHairSimulateInternalScalars
source_char_hair_simulate_internal_scalars(
    float fps,
    float stiffness,
    float gravity,
    bool has_wind,
    bool has_wind_root,
    std::array<float, 3> wind);
SourceCharHairClothPairStep source_char_hair_simulate_internal_cloth_pair(
    std::array<float, 3> point_pos,
    std::array<float, 3> next_point_pos,
    float side_length,
    float min_slack,
    float max_slack);
SourceCharHairLengthStep source_char_hair_simulate_internal_length_step(
    std::array<float, 3> point_pos,
    std::array<float, 3> point_force,
    std::array<float, 3> external_force,
    std::array<float, 3> root_pos,
    std::array<float, 3> root_y_axis,
    float point_length,
    float sixty_over_fps,
    bool has_previous_point);
SourceCharHairForceStep source_char_hair_simulate_internal_force_step(
    std::array<float, 3> target_pos,
    std::array<float, 3> point_pos,
    std::array<float, 3> original_pos,
    std::array<float, 3> last_friction,
    float stiffness_pow,
    float friction,
    float inertia);
SourceCharHairCollisionStep source_char_hair_simulate_internal_collision_step(
    std::array<float, 3> point_pos,
    std::array<float, 3> root_to_point,
    float reciprocal_length,
    std::array<float, 3> last_z,
    std::array<float, 3> root_z_axis,
    float torsion,
    float point_radius,
    float point_outer_radius,
    const std::vector<SourceCharHairCollisionInput>& collides);
SourceCharHairFreezePosePlan source_char_hair_freeze_pose_plan(
    bool simulate,
    int strand_count,
    int collide_count);
SourceCharHairPollDecision source_char_hair_poll_decision(
    bool owner_is_character,
    bool character_syncing,
    bool character_teleported,
    int character_min_lod,
    int current_reset,
    float delta_seconds);
std::array<float, 9> source_char_hair_set_angle_root_mat(
    float angle_degrees, const float base_mat[9]);
SourceCharFaceServoTryScaleDownResult source_char_face_servo_try_scale_down(
    SourceCharFaceServoBlinkState& state,
    bool has_base_clip,
    bool clip_type_valid);
float source_char_face_servo_blink_weight_left(
    const SourceCharFaceServoBlinkState& state);
SourceCharFaceServoScaleAddResult source_char_face_servo_scale_add_blink(
    SourceCharFaceServoBlinkState& state,
    const SourceCharFaceServoBlinkClips& clips,
    const std::string& clip_name,
    bool clip_is_relative,
    float weight,
    float f2 = 0.0f,
    float f3 = 0.0f);
SourceCharFaceServoLoadPlan source_char_face_servo_load_plan(int revision);
SourceCharFaceServoCopyPlan source_char_face_servo_copy_plan();
SourceCharFaceServoHandlerPlan source_char_face_servo_handler_plan();
SourceCharFaceServoPropSyncPlan source_char_face_servo_prop_sync_plan();
SourceCharFaceServoSavePlan source_char_face_servo_save_plan();
SourceCharFaceServoEnterPlan source_char_face_servo_enter_plan();
SourceCharFaceServoSetClipsPlan source_char_face_servo_set_clips_plan();
SourceCharFaceServoSetClipTypePlan
source_char_face_servo_set_clip_type_plan(bool changed);
SourceCharFaceServoPollPlan source_char_face_servo_poll_plan(bool has_base_clip);
SourceCharFaceServoProceduralWeightsPlan
source_char_face_servo_procedural_weights_plan(
    bool positive_weight,
    bool already_applied);
SourceCharFaceServoProceduralWeightsResult
source_char_face_servo_apply_procedural_weights(
    SourceCharFaceServoBlinkState& state,
    float procedural_weight,
    bool already_applied,
    bool has_left_clip,
    bool has_right_clip,
    bool right_same_as_left);
SourceCharFaceServoPollDepsPlan source_char_face_servo_poll_deps_plan();
SourceCharMeshHideRowLoadPlan source_char_mesh_hide_row_load_plan(
    int32_t revision);
SourceCharMeshHideLoadPlan source_char_mesh_hide_load_plan(int32_t revision);
SourceCharMeshHideSavePlan source_char_mesh_hide_save_plan();
SourceCharMeshHideCopyPlan source_char_mesh_hide_copy_plan();
SourceCharMeshHideHandlerPlan source_char_mesh_hide_handler_plan();
SourceCharMeshHidePropSyncPlan source_char_mesh_hide_prop_sync_plan();
int32_t source_char_mesh_hide_combined_flags(
    const std::vector<SourceCharMeshHideObject>& objects,
    int32_t initial_flags);
void source_char_mesh_hide_draws(SourceCharMeshHideObject& object,
                                 int32_t flags);
int32_t source_char_mesh_hide_all(
    std::vector<SourceCharMeshHideObject>& objects,
    int32_t initial_flags);
SourceCharMeshCacheState source_char_mesh_cache_default_state();
SourceCharMeshCacheDisableResult source_char_mesh_cache_disable(
    SourceCharMeshCacheState& state,
    bool disabled);
bool source_char_mesh_cache_has_mesh(
    const SourceCharMeshCacheState& state,
    const std::string& mesh);
SourceCharMeshCacheVertsResult source_char_mesh_cache_get_verts(
    const SourceCharMeshCacheState& state,
    const std::string& mesh);
SourceCharMeshCacheSyncResult source_char_mesh_cache_sync_mesh(
    SourceCharMeshCacheState& state,
    const std::string& mesh,
    int32_t mask = 0);
std::vector<std::string> source_char_mesh_cache_stuff_meshes(
    const SourceCharMeshCacheState& state);
bool source_char_trans_copy_poll(const milo_scene::Xfm* src,
                                 milo_scene::Xfm* dest);
SourceCharTransCopyLoadPlan source_char_trans_copy_load_plan(int revision);
SourceCharTransCopySavePlan source_char_trans_copy_save_plan();
SourceCharTransCopyCopyPlan source_char_trans_copy_copy_plan();
SourceCharTransCopyHandlerPlan source_char_trans_copy_handler_plan();
SourceCharTransCopyPropSyncPlan source_char_trans_copy_prop_sync_plan();
void source_char_trans_copy_poll_deps(
    SourceCharTransCopyPollDeps& deps,
    const std::string& src,
    const std::string& dest);
std::vector<std::string> source_char_poll_group_poll_order(
    float weight,
    const std::vector<std::string>& polls);
std::vector<std::string> source_char_poll_group_enter_order(
    const std::vector<std::string>& polls);
std::vector<std::string> source_char_poll_group_exit_order(
    const std::vector<std::string>& polls);
std::vector<std::string> source_char_poll_group_list_children(
    const std::vector<std::string>& polls);
void source_char_poll_group_poll_deps(
    SourceCharPollGroupPollDeps& deps,
    const std::vector<SourceCharPollGroupChildDeps>& child_deps,
    const std::string& changed_by_override,
    const std::string& change_override);
SourceCharPollGroupLoadPlan source_char_poll_group_load_plan(int revision);
SourceCharPollGroupSavePlan source_char_poll_group_save_plan();
SourceCharPollGroupCopyPlan source_char_poll_group_copy_plan();
SourceCharPollGroupHandlerPlan source_char_poll_group_handler_plan();
SourceCharPollGroupPropSyncPlan source_char_poll_group_prop_sync_plan();
SourceCharPollGroupSortPlan source_char_poll_group_sort_plan();
SourceWaypointState source_waypoint_default_state();
SourceWaypointRegistryState source_waypoint_init_registry();
void source_waypoint_terminate_registry(SourceWaypointRegistryState& registry);
SourceWaypointConstructorStep source_waypoint_construct(
    SourceWaypointRegistryState& registry);
SourceWaypointFindResult source_waypoint_find_by_flags(
    const SourceWaypointRegistryState& registry,
    int flags_mask);
SourceWaypointRegisteredCommandDumpEvidence
source_waypoint_registered_command_dump_evidence();
bool source_waypoint_load_revision_known(int revision);
SourceWaypointLoadPlan source_waypoint_load_plan(int revision);
SourceWaypointCopyPlan source_waypoint_copy_plan();
SourceWaypointHandlerPlan source_waypoint_handler_plan();
SourceWaypointPropSyncPlan source_waypoint_prop_sync_plan();
SourceWaypointSavePlan source_waypoint_save_plan();
std::array<float, 3> source_waypoint_shape_delta_box(
    const milo_scene::Xfm& waypoint_world,
    const std::array<float, 3>& point,
    float radius,
    float y_radius);
float source_waypoint_shape_delta_ang(float waypoint_z_angle,
                                      float radius,
                                      float subject_z_angle);
SourceWaypointConstrainResult source_waypoint_constrain(
    const SourceWaypointState& waypoint,
    const milo_scene::Xfm& waypoint_world,
    const milo_scene::Xfm& subject);
SourceCharIKScaleDefaultState source_char_ik_scale_default_state();
bool source_char_ik_scale_poll_enters(bool has_dest, float weight);
float source_char_ik_scale_capture_before(bool has_dest, float dest_local_z,
                                          float current_scale);
float source_char_ik_scale_capture_after(bool has_dest, float dest_local_z,
                                         float current_scale);
void source_char_ik_scale_poll_deps(
    SourceCharIKScalePollDeps& deps,
    const std::string& dest,
    const std::vector<std::string>& secondary_targets);
SourceCharIKScaleLoadPlan source_char_ik_scale_load_plan(int revision);
SourceCharIKScaleCopyPlan source_char_ik_scale_copy_plan();
SourceCharIKScaleHandlerPlan source_char_ik_scale_handler_plan();
SourceCharIKScalePropSyncPlan source_char_ik_scale_prop_sync_plan();
SourceCharIKScaleSavePlan source_char_ik_scale_save_plan();
SourceCharacterState source_character_default_state();
SourceCharacterLodState source_character_lod_default_state();
SourceCharacterLodState source_character_lod_copy_state(
    const SourceCharacterLodState& lod);
void source_character_lod_assign(SourceCharacterLodState& dest,
                                 const SourceCharacterLodState& src);
SourceCharacterLodCopyPlan source_character_lod_copy_plan();
SourceCharacterLodPropSyncPlan source_character_lod_prop_sync_plan();
SourceObjectDirDefaultState source_object_dir_default_state();
SourceObjectDirSavePlan source_object_dir_save_plan();
SourceObjectDirPreLoadPlan source_object_dir_preload_plan(
    int revision,
    bool loading_proxy_from_disk,
    bool proxy_override);
SourceObjectDirPostLoadPlan source_object_dir_postload_plan(
    int revision,
    int inlined_dir_count,
    bool stream_cached,
    bool is_proxy,
    bool proxy_file_empty,
    bool proxy_override,
    bool edit_mode,
    bool allows_inline_proxy);
SourceObjectDirFindObjectPlan source_object_dir_find_object_plan(
    bool entry_hit,
    bool subdir_hit,
    bool name_matches_self,
    bool parent_dirs,
    bool has_parent_dir,
    bool parent_is_self,
    bool is_main_dir);
SourceObjectDirSubDirPlan source_object_dir_subdir_plan(bool add_subdir);
SourceRndDirDefaultState source_rnddir_default_state();
SourceRndDirSavePlan source_rnddir_save_plan();
SourceRndDirLoadPlan source_rnddir_load_plan(int revision,
                                             bool loading_proxy_from_disk);
SourceRndDirSyncObjectsPlan source_rnddir_sync_objects_plan(
    bool is_subdir,
    bool parent_dir_is_msg_source);
SourceRndDirSyncDrawablesPlan source_rnddir_sync_drawables_plan(
    bool is_subdir);
SourceRndDirCopyPlan source_rnddir_copy_plan();
SourceRndDirHandlerPlan source_rnddir_handler_plan();
SourceRndDirPropSyncPlan source_rnddir_prop_sync_plan();
SourceCharacterLoadPlan source_character_load_plan(int revision,
                                                   bool is_proxy,
                                                   int legacy_other_revision);
SourceCharacterCopyPlan source_character_copy_plan();
SourceCharacterHandlerPlan source_character_handler_plan();
SourceCharacterPropSyncPlan source_character_prop_sync_plan();
SourceCharacterSavePlan source_character_save_plan();
SourceCharacterPlayClipDecision source_character_on_play_clip(
    bool has_driver,
    int32_t message_size,
    int32_t supplied_play_flags,
    bool driver_play_returned);
SourceCharacterCopyBoundingSphereHandlerResult
source_character_on_copy_bounding_sphere(bool has_source_character);
void source_character_enter(SourceCharacterState& state);
void source_character_exit(SourceCharacterState& state);
SourceCharacterPollResult source_character_poll(SourceCharacterState& state);
bool source_character_bone_servo_resolves(bool has_driver,
                                          bool driver_bones_is_servo);
SourceCharacterReplaceResult source_character_replace(
    SourceCharacterState& state,
    bool from_is_sphere_base,
    bool to_is_transformable);
SourceCharacterAddedObjectResult source_character_added_object(
    SourceCharacterState& state,
    bool is_char_pollable,
    bool is_char_driver,
    const std::string& object_name);
SourceCharacterRemoveObjectResult source_character_removing_object(
    SourceCharacterState& state,
    bool object_is_current_driver);
SourceCharacterSyncObjectsResult source_character_sync_objects(
    SourceCharacterState& state,
    bool has_bone_pelvis_mesh,
    int32_t lod_count);
SourceCharacterRuntimeDumpEvidence source_character_runtime_dump_evidence();
SourceCharacterInterestResult source_character_force_blink(bool has_eyes);
SourceCharacterInterestResult source_character_enable_blinks(bool has_eyes);
SourceCharacterInterestResult source_character_set_focus_interest(
    bool has_eyes);
SourceCharacterInterestResult source_character_set_interest_filter_flags(
    bool has_eyes);
SourceCharacterInterestResult source_character_clear_interest_filter_flags(
    bool has_eyes);
SourceCharacterCurrentInterestsResult source_character_on_get_current_interests(
    bool has_eyes,
    const std::vector<std::string>& interest_names);
SourceCharacterDebugDrawInterestResult
source_character_set_debug_draw_interest_objects(bool enabled);
SourceCharacterSetSphereBaseResult source_character_set_sphere_base(
    SourceCharacterState& state,
    bool has_transform);
SourceCharacterSetInterestObjectsResult source_character_set_interest_objects(
    bool has_eyes,
    const std::vector<bool>& validate_results,
    bool has_override_dir);
SourceCharacterAddShadowBoneResult source_character_add_shadow_bone(
    int32_t current_shadow_bones,
    bool has_transform,
    bool already_hooked);
SourceCharacterUnhookShadowResult source_character_unhook_shadow(
    int32_t current_shadow_bones);
SourceCharacterSyncShadowResult source_character_sync_shadow(
    bool has_shadow,
    bool old_gfx,
    const std::vector<int32_t>& mesh_bone_counts);
SourceCharacterCopyBoundingSphereResult source_character_copy_bounding_sphere(
    SourceCharacterState& state,
    bool source_has_sphere_base);
SourceCharacterRepointSphereBaseResult source_character_repoint_sphere_base(
    SourceCharacterState& state,
    bool found_matching_transform);
SourceCharacterPreSaveResult source_character_pre_save();
SourceBandCharacterDeformationPlan source_band_character_deformation_plan(
    bool has_deform_clip,
    bool edit_mode_bone_servo,
    bool in_closet);
SourceCharPollableSorterChangedByResult source_char_pollable_sorter_changed_by(
    std::vector<SourceCharPollableSorterDep>& deps,
    int32_t target_index,
    int32_t query_index,
    int32_t current_search_id);
SourceCharLifecyclePlan source_char_lifecycle_plan();
SourceCharacterTestState source_character_test_default_state();
SourceCharacterTestDestroyResult source_character_test_destroy(
    bool overlay_found,
    bool overlay_callback_is_this);
SourceCharacterTestDrawResult source_character_test_draw(
    bool has_driver,
    bool has_clip1,
    bool has_clip2,
    bool has_bone_head,
    bool show_screen_size);
SourceCharacterTestPollResult source_character_test_poll(
    const SourceCharacterTestPollInput& input);
SourceCharacterTestAddDefaultsResult source_character_test_add_defaults(
    const SourceCharacterTestExisting& existing,
    const SourceCharacterTestBones& bones);
std::vector<std::string> source_character_test_walk(
    const std::vector<std::string>& walk_path);
std::string source_character_test_teleport_to(const std::string& waypoint);
SourceCharacterTestStartEndBeatResult source_character_test_set_start_end_beat(
    bool milo_found,
    bool cur_anim_is_object,
    bool cur_anim_is_me,
    float start_beat,
    float end_beat,
    int32_t bpm);
bool source_character_test_set_move_self(bool has_bone_servo);
SourceCharacterTestLoadResult source_character_test_load(
    int32_t revision,
    int32_t alt_revision);
SourceCharTransDrawLoadPlan source_char_trans_draw_load_plan(int revision);
SourceCharTransDrawSavePlan source_char_trans_draw_save_plan();
SourceCharTransDrawCopyPlan source_char_trans_draw_copy_plan();
SourceCharTransDrawHandlerPlan source_char_trans_draw_handler_plan();
SourceCharTransDrawPropSyncPlan source_char_trans_draw_prop_sync_plan();
std::vector<SourceCharTransDrawStep> source_char_trans_draw_set_draw_modes(
    const std::vector<std::string>& chars,
    SourceCharacterDrawMode mode);
std::vector<SourceCharTransDrawStep> source_char_trans_draw_load_modes(
    const std::vector<std::string>& chars);
std::vector<SourceCharTransDrawStep> source_char_trans_draw_destruct_modes(
    const std::vector<std::string>& chars);
std::vector<SourceCharTransDrawStep> source_char_trans_draw_draw_showing(
    const std::vector<SourceCharTransDrawCharacter>& chars);
SourceCharCuffState source_char_cuff_default_state();
SourceCharCuffLoadPlan source_char_cuff_load_plan(int revision);
SourceCharCuffSavePlan source_char_cuff_save_plan();
SourceCharCuffCopyPlan source_char_cuff_copy_plan();
SourceCharCuffHandlerPlan source_char_cuff_handler_plan();
SourceCharCuffPropSyncPlan source_char_cuff_prop_sync_plan();
float source_char_cuff_eccentricity(float x, float y, float eccentricity);
void source_char_cuff_apply_revision_defaults(SourceCharCuffState& cuff,
                                              int32_t revision,
                                              const std::string& trans_parent);
std::vector<std::string> source_char_cuff_add_bone_children(
    const SourceCharCuffTransformNode* trans);
SourceCharCuffDeformRuntimeMap source_char_cuff_deform_runtime_map();
SourceCharBlendBoneState source_char_blend_bone_default_state();
SourceCharBlendBoneConstraintLoadPlan
source_char_blend_bone_constraint_load_plan();
SourceCharBlendBoneLoadPlan source_char_blend_bone_load_plan(int revision);
SourceCharBlendBoneSavePlan source_char_blend_bone_save_plan();
SourceCharBlendBoneCopyPlan source_char_blend_bone_copy_plan();
SourceCharBlendBoneHandlerPlan source_char_blend_bone_handler_plan();
SourceCharBlendBoneConstraintPropSyncPlan
source_char_blend_bone_constraint_prop_sync_plan();
SourceCharBlendBonePropSyncPlan source_char_blend_bone_prop_sync_plan();
SourceCharBlendBoneRuntimeDumpEvidence
source_char_blend_bone_runtime_dump_evidence();
void source_char_blend_bone_poll_deps(
    SourceCharBlendBonePollDeps& deps,
    const SourceCharBlendBoneState& blend);
SourceCharSleeveState source_char_sleeve_default_state();
SourceCharSleeveSetNameStep source_char_sleeve_set_name_step(
    bool dir_is_character);
SourceCharSleevePollResult source_char_sleeve_poll(
    SourceCharSleeveState& state,
    bool has_sleeve,
    bool has_parent,
    bool has_top_sleeve,
    bool character_teleported,
    float delta_seconds,
    float sleeve_local_z,
    const milo_scene::Xfm& sleeve_world,
    const milo_scene::Xfm& parent_world);
SourceCharSleeveHighlightPlan source_char_sleeve_highlight_plan(
    bool has_sleeve,
    bool has_parent,
    bool has_top_sleeve);
void source_char_sleeve_poll_deps(SourceCharSleevePollDeps& deps,
                                  const std::string& sleeve_parent,
                                  const std::string& sleeve,
                                  const std::string& top_sleeve,
                                  bool has_sleeve);
SourceCharSleeveLoadPlan source_char_sleeve_load_plan(int32_t revision);
SourceCharSleeveSavePlan source_char_sleeve_save_plan();
SourceCharSleeveCopyPlan source_char_sleeve_copy_plan();
SourceCharSleeveHandlerPlan source_char_sleeve_handler_plan();
SourceCharSleevePropSyncPlan source_char_sleeve_prop_sync_plan();
SourceCharGuitarStringPollResult source_char_guitar_string_poll(
    bool has_nut,
    bool has_bridge,
    bool has_bend,
    bool has_target,
    bool open,
    const std::array<float, 3>& nut_pos,
    const std::array<float, 3>& bridge_pos,
    const std::array<float, 3>& bend_pos,
    const std::array<float, 3>& target_pos);
void source_char_guitar_string_poll_deps(
    SourceCharGuitarStringPollDeps& deps,
    const std::string& nut,
    const std::string& bridge,
    const std::string& target,
    const std::string& bend);
SourceCharGuitarStringDefaultState
source_char_guitar_string_default_state();
SourceCharGuitarStringLoadPlan source_char_guitar_string_load_plan(
    int revision);
SourceCharGuitarStringCopyPlan source_char_guitar_string_copy_plan();
SourceCharGuitarStringHandlerPlan source_char_guitar_string_handler_plan();
SourceCharGuitarStringPropSyncPlan
source_char_guitar_string_prop_sync_plan();
SourceCharGuitarStringSavePlan source_char_guitar_string_save_plan();
std::vector<std::string> source_char_eyes_list_poll_children(
    const std::vector<std::string>& eye_lookats);
bool source_char_eyes_either_eye_clamped(
    const std::vector<SourceCharEyesClampRow>& eyes);
SourceCharEyesEyeDescLoadPlan source_char_eyes_eye_desc_load_plan(
    int32_t revision);
SourceCharEyesLoadPlan source_char_eyes_load_plan(int32_t revision);
SourceCharEyesCopyPlan source_char_eyes_copy_plan();
SourceCharEyesHandlerPlan source_char_eyes_handler_plan();
SourceCharEyesPropSyncPlan source_char_eyes_prop_sync_plan();
SourceCharEyesSavePlan source_char_eyes_save_plan();
SourceCharEyesBitfieldPropResult source_char_eyes_default_interest_categories_sync(
    int current_flags,
    int bit_mask,
    bool get_operation,
    bool requested_enabled);
SourceCharEyesFilterFlagsResult source_char_eyes_set_interest_filter_flags(
    int requested_flags);
SourceCharEyesFilterFlagsResult source_char_eyes_clear_interest_filter_flags(
    int default_flags);
SourceCharEyesDefaultState source_char_eyes_default_state();
SourceCharEyesDefaultState source_char_eyes_copy_state(
    const SourceCharEyesDefaultState& source);
SourceCharEyesEyeDesc source_char_eyes_eye_desc_default();
SourceCharEyesEyeDesc source_char_eyes_eye_desc_copy(
    const SourceCharEyesEyeDesc& source);
void source_char_eyes_eye_desc_assign(
    SourceCharEyesEyeDesc& dest,
    const SourceCharEyesEyeDesc& source);
std::string source_char_eyes_get_head(
    const std::string& view_direction,
    const std::string& first_eye_source_parent);
std::string source_char_eyes_current_interest(
    const std::string& focus_interest,
    const std::string& current_interest);
SourceCharEyesFocusResult source_char_eyes_set_focus_interest(
    const std::string& current_focus,
    int current_priority,
    const std::string& requested_interest,
    int requested_priority);
SourceCharEyesFocusResult source_char_eyes_toggle_force_focus(
    const std::string& current_focus,
    int current_priority,
    const std::string& current_interest);
SourceCharEyesOverlayToggleResult source_char_eyes_toggle_interest_overlay(
    bool has_overlay,
    bool current_showing);
SourceCharEyesForceBlinkState source_char_eyes_force_blink(
    float task_seconds);
SourceCharEyesEnterState source_char_eyes_enter_state(
    int default_filter_flags,
    bool has_head,
    const std::array<float, 3>& head_world_y,
    size_t eye_count,
    size_t interest_count);
SourceCharEyesExitState source_char_eyes_exit_state(size_t eye_count);
SourceCharEyesInterestRuntime source_char_eyes_interest_state(
    const std::string& interest);
void source_char_eyes_interest_reset(
    SourceCharEyesInterestRuntime& state);
void source_char_eyes_interest_begin_refractory(
    SourceCharEyesInterestRuntime& state,
    float task_seconds);
bool source_char_eyes_interest_in_refractory(
    const SourceCharEyesInterestRuntime& state,
    float task_seconds,
    float refractory_period);
float source_char_eyes_interest_refractory_remaining(
    const SourceCharEyesInterestRuntime& state,
    float task_seconds,
    float refractory_period);
void source_char_eyes_clear_interest_objects(
    std::vector<SourceCharEyesInterestRuntime>& interests);
bool source_char_eyes_add_interest_object(
    std::vector<SourceCharEyesInterestRuntime>& interests,
    const std::string& interest);
void source_char_eyes_poll_deps(
    SourceCharEyesPollDeps& deps,
    const std::vector<SourceCharEyesInterest>& interests,
    bool has_eyes,
    const std::string& head,
    const std::string& target,
    const std::string& head_lookat,
    const std::string& face_servo);
SourceCharEyesRuntimeDumpEvidence
source_char_eyes_runtime_dump_evidence();
SourceGh2CharEyesNextLookPublication
source_gh2_char_eyes_next_look_publication(
    const std::vector<std::string>& lookats,
    const std::string& generated_target,
    const std::string& qualifying_interest);
SourceGh2CharEyesGeneratedTargetResult
source_gh2_char_eyes_generated_target(
    const std::array<float, 3>& current_facing,
    const std::array<float, 3>& last_facing,
    const std::array<float, 3>& source_world_position,
    float random_unit,
    bool object_dir_is_transformable,
    float object_dir_world_z);
void source_gh2_random_seed(SourceGh2RandomState& state, uint32_t seed);
uint32_t source_gh2_random_u32(SourceGh2RandomState& state);
float source_gh2_random_unit(SourceGh2RandomState& state);
SourceGh2CharEyesPollState source_gh2_char_eyes_enter(
    const std::array<float, 3>& first_eye_world_y,
    bool has_first_eye);
SourceGh2CharEyesPollResult source_gh2_char_eyes_poll(
    SourceGh2CharEyesPollState& state,
    const std::array<float, 3>& first_eye_world_y,
    const std::array<float, 3>& first_eye_world_position,
    const std::array<float, 3>& target_world_position,
    float delta_seconds,
    bool has_blink_weight,
    float blink_weight,
    float random_unit);
SourceCharEyeDartRulesetData source_char_eye_dart_ruleset_defaults();
bool source_char_eye_dart_ruleset_load_revision_known(int revision);
SourceCharEyeDartRulesetLoadPlan source_char_eye_dart_ruleset_load_plan(
    int revision);
SourceCharEyeDartRulesetData source_char_eye_dart_ruleset_copy(
    const SourceCharEyeDartRulesetData& src);
SourceCharEyeDartRulesetCopyPlan source_char_eye_dart_ruleset_copy_plan();
SourceCharEyeDartRulesetPropSyncPlan
source_char_eye_dart_ruleset_prop_sync_plan();
SourceCharEyeDartRulesetHandlerPlan
source_char_eye_dart_ruleset_handler_plan();
SourceCharEyeDartRulesetSavePlan
source_char_eye_dart_ruleset_save_plan();
SourceCharInterestState source_char_interest_defaults();
bool source_char_interest_load_revision_known(int revision);
SourceCharInterestLoadPlan source_char_interest_load_plan(int revision);
float source_char_interest_sync_max_view_angle(float max_view_angle_degrees);
bool source_char_interest_is_matching_filter_flags(int category_flags,
                                                   int mask);
bool source_char_interest_is_within_view_cone(
    const std::array<float, 3>& interest_world,
    const std::array<float, 3>& viewer_world,
    const std::array<float, 3>& view_direction,
    float max_view_angle_cos);
SourceCharInterestState source_char_interest_copy(
    const SourceCharInterestState& src);
SourceCharInterestCopyPlan source_char_interest_copy_plan();
SourceCharInterestPropSyncPlan source_char_interest_prop_sync_plan();
SourceCharInterestCategoryFlagsPropPlan
source_char_interest_category_flags_prop_plan();
SourceCharInterestHandlerPlan source_char_interest_handler_plan();
SourceCharInterestSavePlan source_char_interest_save_plan();
SourceCharInterestHighlightPlan source_char_interest_highlight_plan(
    bool world_to_screen_positive,
    bool has_dart_override,
    bool has_min_radius_property,
    bool has_max_radius_property);
SourceCharInterestComputeScorePlan source_char_interest_compute_score_plan();
SourceCharInterestScoreResult source_char_interest_compute_score_deterministic(
    const std::array<float, 3>& view_direction,
    const std::array<float, 3>& viewer_world,
    const std::array<float, 3>& interest_direction,
    const std::array<float, 3>& interest_world,
    float distance_scale,
    int mask,
    bool allow_default_category,
    int category_flags,
    float priority,
    float max_view_angle_cos,
    float random_jitter);
SourceCharNeckTwistState source_char_neck_twist_defaults();
bool source_char_neck_twist_load_revision_known(int revision);
SourceCharNeckTwistLoadPlan source_char_neck_twist_load_plan(int revision);
SourceCharNeckTwistCopyPlan source_char_neck_twist_copy_plan();
SourceCharNeckTwistHandlerPlan source_char_neck_twist_handler_plan();
SourceCharNeckTwistPropSyncPlan source_char_neck_twist_prop_sync_plan();
SourceCharNeckTwistSavePlan source_char_neck_twist_save_plan();
void source_char_neck_twist_poll_deps(SourceCharNeckTwistPollDeps& deps,
                                      const std::string& head,
                                      const std::string& twist);
float source_char_neck_twist_half_limited_angle(float rotated_y_y,
                                                float rotated_y_z);
SourceCharNeckTwistPollPlan source_char_neck_twist_poll_plan(
    bool has_head,
    bool has_twist,
    bool has_twist_parent,
    bool reaches_twist_parent,
    const std::array<float, 9>& head_local_matrix,
    const std::vector<std::array<float, 9>>& parent_local_matrices);
SourceCharIKFingersState source_char_ik_fingers_defaults();
bool source_char_ik_fingers_load_revision_known(int revision);
SourceCharIKFingersSetupRefs source_char_ik_fingers_set_name_refs(
    bool is_right_hand);
bool source_char_ik_fingers_setup_complete(
    const SourceCharIKFingersSetupRefs& refs,
    const std::vector<std::string>& present_transforms);
SourceCharIKFingersSetFingerPlan source_char_ik_fingers_set_finger_plan(
    int finger);
SourceCharIKFingersReleaseFingerPlan
source_char_ik_fingers_release_finger_plan(int finger);
SourceCharIKFingersLoadPlan source_char_ik_fingers_load_plan(int revision);
SourceCharIKFingersCopyPlan source_char_ik_fingers_copy_plan();
SourceCharIKFingersHandlerPlan source_char_ik_fingers_handler_plan();
SourceCharIKFingersPropSyncPlan source_char_ik_fingers_prop_sync_plan();
SourceCharIKFingersRuntimeBoundary
source_char_ik_fingers_runtime_boundary();
SourceCharIKFingersSavePlan source_char_ik_fingers_save_plan();
void source_char_hair_strand_set_angle(CharHairStrand& strand,
                                       float angle_degrees);
void source_char_hair_strand_set_root(
    CharHairStrand& strand,
    const std::vector<SourceCharHairRootNode>& first_child_chain);
bool source_gltf_milo_is_hair_bone_node(
    const SourceGltfMiloHairNode& node);
SourceGltfMiloHairChildClassification
source_gltf_milo_classify_hair_children(
    const SourceGltfMiloHairNode& parent,
    const std::vector<SourceGltfMiloHairNode>& children);
SourceGltfMiloHairRootDiscoveryResult
source_gltf_milo_discover_hair_roots(
    const std::vector<SourceGltfMiloHairNode>& nodes);
SourceGltfMiloHairChainsResult
source_gltf_milo_collect_hair_chains_split_at_branches(
    const std::vector<SourceGltfMiloHairNode>& nodes);
SourceGltfMiloHairChainsResult
source_gltf_milo_collect_hair_chains_without_splitting(
    const std::vector<SourceGltfMiloHairNode>& nodes);
SourceGltfMiloHairStrandHeaderPlan
source_gltf_milo_export_hair_strand_header_plan(
    const std::vector<SourceGltfMiloHairStrandHeaderNode>& chain,
    bool convert_coordinates);
SourceGltfMiloHairPointExport source_gltf_milo_export_hair_point(
    const std::vector<SourceGltfMiloHairPointNode>& chain,
    int point_index,
    std::array<float, 16> strand_root_parent_world_inverse);
std::string source_gltf_milo_hair_collide_name(
    const std::string& mesh_name);
std::vector<SourceGltfMiloHairCollideExport>
source_gltf_milo_process_empty_hair_collides(
    const std::vector<std::string>& hair_mesh_names,
    const std::vector<std::string>& existing_collide_names,
    const std::string& parent_name);
SourceGltfMiloCharHairExportPlan source_gltf_milo_process_char_hair_plan(
    int weighted_hair_bone_count,
    int strand_count,
    const std::string& requested_wind,
    bool split_strands_at_branches);
SourceGltfMiloCharHairExtrasBoundary
source_gltf_milo_char_hair_extras_boundary();
SourceGltfMiloCharHairExtrasDefaults
source_gltf_milo_char_hair_extras_defaults();
SourceGltfMiloHairSettingsDetectionPlan
source_gltf_milo_detect_hair_settings_plan(
    const std::string& bone_name,
    const std::string& extras_json,
    bool already_detected_settings,
    bool deserialize_succeeds);

struct CharCollideMeshSphere {
  int32_t vertex = 0;
  float vec[3] = {0.0f, 0.0f, 0.0f};
};

struct CharCollide {
  std::string name;
  int32_t version = 0;
  milo_scene::Xfm local;
  milo_scene::Xfm world_stored;
  uint32_t constraint = 0;
  std::string target;
  bool preserve_scale = false;
  std::string parent;
  int32_t shape = 1;  // CharCollide::kSphere
  int32_t flags = 0;
  std::string mesh;
  bool mesh_y_bias = false;
  milo_scene::Xfm mesh_transform;
  std::array<CharCollideMeshSphere, 8> mesh_spheres;
  std::array<uint8_t, 20> digest = {};
  float orig_radius[2] = {0.0f, 0.0f};
  float orig_length[2] = {0.0f, 0.0f};
  float cur_radius[2] = {0.0f, 0.0f};
  float cur_length[2] = {0.0f, 0.0f};
};

struct SourceCharCollideRadiusCache {
  std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> axis = {0.0f, 1.0f, 0.0f};
  float length_scale = 1.0f;
  float radius_lerp_scale = 1.0f;
};

struct SourceCharCollideRadiusRuntimeEvidence {
  std::string compute_radius_range;
  bool compute_radius_has_offset_local = true;
  bool compute_radius_has_statement_body = false;
  bool sync_radius_empty_body = true;
  bool radius_getter_mapped_only = true;
  bool get_radius_inline_body = true;
};

struct SourceCharCollideAccessorsPlan {
  int32_t shape = 1;
  bool get_shape_returns_member = true;
  bool axis_declared = true;
  bool axis_body_available = false;
};

struct SourceCharCollideDefaultState {
  int32_t shape = 1;
  int32_t flags = 0;
  bool mesh_empty = true;
  bool mesh_y_bias = false;
  std::array<float, 2> orig_radius = {0.0f, 0.0f};
  std::array<float, 2> orig_length = {0.0f, 0.0f};
  std::array<float, 2> cur_radius = {0.0f, 0.0f};
  std::array<float, 2> cur_length = {0.0f, 0.0f};
  bool mesh_transform_reset = true;
  int32_t mesh_sphere_count = 8;
  bool mesh_spheres_zeroed = true;
};

struct SourceCharCollideSavePlan {
  int32_t save_id = 0x58;
};

struct SourceCharCollideCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  std::vector<std::string> not_in_source_copy_members;
};

struct SourceCharCollideLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
  int mesh_sphere_rows = 0;
};

struct SourceCharCollideHandlerPlan {
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceCharCollidePropSyncPlan {
  std::vector<std::string> modify_properties;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

struct SourceCharCollideHighlightPlan {
  std::vector<std::string> draw_calls;
  int mesh_sphere_draws = 0;
};

struct SourceCharCollideDeformPlan {
  bool no_op = true;
};

SourceCharCollideDefaultState source_char_collide_default_state();
SourceCharCollideSavePlan source_char_collide_save_plan();
SourceCharCollideLoadPlan source_char_collide_load_plan(int revision);
SourceCharCollideCopyPlan source_char_collide_copy_plan();
SourceCharCollideHandlerPlan source_char_collide_handler_plan();
SourceCharCollidePropSyncPlan source_char_collide_prop_sync_plan();
SourceCharCollideHighlightPlan source_char_collide_highlight_plan(
    const CharCollide& collide,
    bool has_mesh);
SourceCharCollideDeformPlan source_char_collide_deform_plan();
SourceCharCollideRadiusRuntimeEvidence
source_char_collide_radius_runtime_evidence();
SourceCharCollideAccessorsPlan source_char_collide_accessors_plan(
    const CharCollide& collide);
void source_char_collide_copy_original_to_cur(CharCollide& collide);
void source_char_collide_clear_mesh(CharCollide& collide);
void source_char_collide_sync_shape(CharCollide& collide);
int source_char_collide_num_spheres(const CharCollide& collide);
float source_char_collide_get_radius(
    const CharCollide& collide,
    const SourceCharCollideRadiusCache& cache,
    const std::array<float, 3>& point,
    std::array<float, 3>& out_delta);

struct CharPosConstraint {
  std::string name;
  int32_t version = 0;
  std::vector<std::string> targets;
  std::string source;
  float box_min[3] = {1.0f, 1.0f, 0.0f};
  float box_max[3] = {-1.0f, -1.0f, 1000.0f};
};

struct SourceCharPosConstraintLoadPlan {
  bool known_revision = false;
  std::vector<std::string> read_order;
  std::vector<std::string> branches;
};

struct SourceCharPosConstraintSavePlan {
  int32_t save_id = 0x64;
};

struct SourceCharPosConstraintCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
};

struct SourceCharPosConstraintPollDepsPlan {
  std::vector<std::string> changed_by;
  std::vector<std::string> change;
};

SourceCharPosConstraintLoadPlan source_char_pos_constraint_load_plan(
    int revision);
SourceCharPosConstraintSavePlan source_char_pos_constraint_save_plan();
SourceCharPosConstraintCopyPlan source_char_pos_constraint_copy_plan();
SourceCharPosConstraintPollDepsPlan source_char_pos_constraint_poll_deps_plan(
    const std::string& source,
    const std::vector<std::string>& targets);
std::array<float, 3> source_char_pos_constraint_target_position(
    const std::array<float, 3>& source_pos,
    const std::array<float, 3>& target_pos,
    const std::array<float, 3>& box_min,
    const std::array<float, 3>& box_max);

struct CharBoneOffset {
  std::string name;
  int32_t version = 0;
  std::string dest;
  float offset[3] = {0.0f, 0.0f, 0.0f};
  size_t unread_bytes = 0;
};

struct CharBoneTwist {
  std::string name;
  int32_t version = 0;
  int32_t weightable_version = 0;
  float weight = 1.0f;
  std::string weight_owner;
  std::string bone;
  std::vector<std::string> targets;
  size_t unread_bytes = 0;
};

struct RuntimeIKMidiState {
  bool initialized = false;
  std::string active_spot;
  float active_event_beat = 0.0f;
  float target_beat = 0.0f;
  float fraction = 0.0f;
  float fraction_per_beat = 0.0f;
  std::array<float, 16> spot_relative_xfm =
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct RuntimeIKHandMeasureState {
  bool hand_changed = true;
  bool has_elbow_chain = false;
  float inv_2ab = 0.0f;
  float a2_plus_b2 = 0.0f;
  float aa_plus_bb = 0.0f;
};

struct RuntimeGh2CharLookAtState {
  bool entered = false;
  bool has_smoothed_dir = false;
  std::array<float, 3> smoothed_dir = {1.0e29f, 0.0f, 0.0f};
  float yaw_weight = 1.0f;
  std::array<float, 3> source_history = {};
};

struct RuntimeGh2CharEyesState {
  bool entered = false;
  bool has_last_time = false;
  float last_time_seconds = 0.0f;
  std::array<float, 3> generated_target = {};
  SourceGh2CharEyesPollState poll;
};

struct FaceFxServoTarget {
  std::string object;
  int32_t prop_type = 0;
  std::string property;
};

struct FaceFxLipSyncServo {
  std::string name;
  std::string facefx_path;
  std::string viseme_milo;
  std::vector<FaceFxServoTarget> targets;
};

struct RndAnimFilter {
  std::string name;
  int32_t version = 0;
  int32_t animatable_version = 0;
  float frame = 0.0f;
  int32_t rate = 0;
  std::string anim;
  float scale = 1.0f;
  float offset = 0.0f;
  float start = 0.0f;
  float end = 0.0f;
  int32_t type = 0;
  float period = 0.0f;
  float snap = 0.0f;
  float jitter = 0.0f;
  size_t unread_bytes = 0;
};

struct SourceRndAnimFilterAnimInfo {
  bool present = false;
  int32_t rate = 0;
  float start = 0.0f;
  float end = 0.0f;
};

struct SourceRndAnimFilterSetAnimPlan {
  bool assigns_anim = true;
  bool anim_present = false;
  bool copies_rate = false;
  bool copies_start_end = false;
};

struct SourceRndAnimFilterScaleResult {
  bool period_path = false;
  bool reversed_range = false;
  float scale = 0.0f;
};

struct SourceRndAnimFilterFrameOffsetResult {
  bool reversed_range = false;
  float frame_offset = 0.0f;
};

struct SourceRndAnimFilterFrameBoundsResult {
  bool has_anim = false;
  float scale = 0.0f;
  bool scale_was_zero = false;
  float frame_offset = 0.0f;
  bool shuttle = false;
  float start_frame = 0.0f;
  float end_frame = 0.0f;
};

struct SourceRndAnimFilterCopyPlan {
  std::vector<std::string> copied_superclasses;
  bool copy_from_max = false;
  std::vector<std::string> copied_members;
};

struct SourceRndAnimFilterSafeAnimsResult {
  std::vector<std::string> safe_anims;
  bool appends_null = true;
};

struct SourceRndAnimFilterHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> superclasses;
  int32_t check = 0xe3;
};

struct SourceRndAnimFilterPropSyncPlan {
  std::vector<std::string> set_properties;
  std::vector<std::string> properties;
  std::vector<std::string> modify_properties;
  std::vector<std::string> superclasses;
};

struct SourceRndAnimFilterSavePlan {
  int32_t save_id = 0x4a;
};

SourceRndAnimFilterSetAnimPlan source_rnd_anim_filter_set_anim(
    RndAnimFilter& filter,
    const std::string& anim,
    const SourceRndAnimFilterAnimInfo& anim_info);
bool source_rnd_anim_filter_loop(int32_t type);
SourceRndAnimFilterScaleResult source_rnd_anim_filter_scale(
    float start,
    float end,
    float scale,
    float period,
    float frames_per_unit);
SourceRndAnimFilterFrameOffsetResult source_rnd_anim_filter_frame_offset(
    float start,
    float end,
    float offset);
SourceRndAnimFilterFrameBoundsResult source_rnd_anim_filter_frame_bounds(
    const RndAnimFilter& filter,
    float frames_per_unit);
std::string source_rnd_anim_filter_anim_target(const RndAnimFilter& filter);
std::vector<std::string> source_rnd_anim_filter_list_anim_children(
    const RndAnimFilter& filter);
SourceRndAnimFilterCopyPlan source_rnd_anim_filter_copy_plan(
    bool copy_from_max);
SourceRndAnimFilterSafeAnimsResult source_rnd_anim_filter_safe_anims(
    const std::vector<std::pair<std::string, bool>>& anim_contains_self);
SourceRndAnimFilterHandlerPlan source_rnd_anim_filter_handler_plan();
SourceRndAnimFilterPropSyncPlan source_rnd_anim_filter_prop_sync_plan();
SourceRndAnimFilterSavePlan source_rnd_anim_filter_save_plan();

struct EventTriggerAnim {
  std::string anim;
  float blend = 0.0f;
  bool wait = false;
  float delay = 0.0f;
  bool enable = false;
  int32_t rate = 0;
  float start = 0.0f;
  float end = 0.0f;
  float period = 0.0f;
  std::string type;
  float scale = 1.0f;
};

struct EventTriggerProxyCall {
  std::string proxy;
  std::string call;
  std::string event;
};

struct EventTriggerHideDelay {
  std::string hide;
  float delay = 0.0f;
  int32_t rate = 0;
};

struct EventTrigger {
  std::string name;
  int32_t version = 0;
  int32_t alt_version = 0;
  int32_t animatable_version = 0;
  float frame = 0.0f;
  int32_t anim_rate = 0;
  std::vector<std::string> trigger_events;
  std::vector<EventTriggerAnim> anims;
  std::vector<std::string> sounds;
  std::vector<std::string> shows;
  std::vector<EventTriggerHideDelay> hide_delays;
  std::vector<std::string> enable_events;
  std::vector<std::string> disable_events;
  std::vector<std::string> wait_for_events;
  std::string next_link;
  std::vector<EventTriggerProxyCall> proxy_calls;
  int32_t trigger_order = 0;
  std::vector<std::string> reset_triggers;
  bool reset_self = false;
  int32_t anim_trigger = 0;
  float anim_frame = 0.0f;
  std::vector<std::string> part_launchers;
  size_t unread_bytes = 0;
  std::string unread_tail_hex;
};

struct SourceEventTriggerAnimLoadPlan {
  std::vector<std::string> read_order;
  bool reset_anim_for_legacy = false;
};

struct SourceEventTriggerProxyCallLoadPlan {
  std::vector<std::string> read_order;
};

struct SourceEventTriggerSupportedEventsPlan {
  std::vector<std::string> config_path;
  int32_t array_index = 1;
  bool uses_endgame_action_type_path = false;
};

struct SourceEventTriggerLoadPlan {
  bool known_revision = false;
  std::vector<std::string> load_steps;
  SourceEventTriggerAnimLoadPlan anim;
  SourceEventTriggerProxyCallLoadPlan proxy_call;
  std::vector<std::string> hide_delay_read_order;
};

struct SourceEventTriggerDefaultState {
  float anim_frame = 0.0f;
  int32_t trigger_order = 0;
  int32_t anim_trigger = 0;
  int32_t unkde = -1;
  bool reset_self = false;
  bool enabled = true;
  bool enabled_at_start = true;
  bool constructor_registers_events = true;
};

struct SourceEventTriggerSinkRow {
  std::string event;
  std::string message;
  std::string mode;
};

struct SourceEventTriggerEventRegistrationPlan {
  bool dir_is_msg_source = false;
  bool clears_enabled_at_start = false;
  std::vector<SourceEventTriggerSinkRow> add_sinks;
  std::vector<SourceEventTriggerSinkRow> remove_sinks;
};

struct SourceEventTriggerCopyPlan {
  std::vector<std::string> copied_superclasses;
  std::vector<std::string> copied_members;
  std::vector<std::string> pre_copy_steps;
  std::vector<std::string> post_copy_steps;
  std::vector<std::string> not_copied_members;
};

struct SourceEventTriggerHandlerPlan {
  std::vector<std::string> handlers;
  std::vector<std::string> action_handlers;
  std::vector<std::string> direct_returns;
  std::vector<std::string> superclasses;
  int check = 0;
};

struct SourceEventTriggerPropSyncPlan {
  std::vector<std::string> anim_props;
  std::vector<std::string> proxy_call_props;
  std::vector<std::string> hide_delay_props;
  std::vector<std::string> event_list_props;
  bool event_lists_unregister_before_mutation = false;
  bool event_lists_register_after_mutation = false;
  std::vector<std::string> properties;
  std::vector<std::string> superclasses;
};

SourceEventTriggerLoadPlan source_event_trigger_load_plan(int revision);
SourceEventTriggerDefaultState source_event_trigger_default_state();
SourceEventTriggerSupportedEventsPlan source_event_trigger_supported_events_plan(
    bool type_is_endgame_action);
SourceEventTriggerEventRegistrationPlan source_event_trigger_register_events_plan(
    const EventTrigger& trigger,
    bool dir_is_msg_source);
SourceEventTriggerEventRegistrationPlan
source_event_trigger_unregister_events_plan(const EventTrigger& trigger,
                                            bool dir_is_msg_source);
SourceEventTriggerCopyPlan source_event_trigger_copy_plan();
SourceEventTriggerHandlerPlan source_event_trigger_handler_plan();
SourceEventTriggerPropSyncPlan source_event_trigger_prop_sync_plan();

struct ObjectRow {
  std::string name;
  int32_t version = 0;
  int32_t alt_version = 0;
  std::string subtype;
  bool root_has_tree = false;
  uint32_t root_id = 0;
  uint16_t root_child_count = 0;
  std::string note;
  size_t unread_bytes = 0;
  std::string unread_tail_hex;
};

struct OpaqueObjectRow {
  std::string name;
  std::string type;
  size_t body_bytes = 0;
  std::string head_hex;
  std::string tail_hex;
};

struct OutfitLoaderOutfit {
  uint8_t hide = 0;
  uint8_t desire = 0;
  uint8_t exclude = 0;
};

struct OutfitLoaderCategory {
  uint8_t selected = 0;
  uint8_t shown = 0;
  std::vector<OutfitLoaderOutfit> outfits;
};

struct OutfitLoader {
  std::string name;
  int32_t revision = 0;
  std::string object_type;
  std::string directory;
  std::vector<OutfitLoaderCategory> categories;
  bool decoded = false;
  std::string error;
};

OutfitLoader decode_outfit_loader(
    const std::string& entry_name,
    const std::vector<uint8_t>& body);

struct CharWalk {
  std::string name;
  int32_t revision = 0;
  std::string object_type;
  bool decoded = false;
  std::string error;
};

CharWalk decode_char_walk(const std::string& entry_name,
                          const std::vector<uint8_t>& body);

struct WorldFx {
  std::string name;
  int32_t revision = 0;
  int32_t render_directory_revision = 0;
  int32_t object_directory_revision = 0;
  std::string object_type;
  std::string proxy_path;
  std::vector<std::string> subdirectories;
  std::string environment;
  std::string test_event;
  std::string legacy_symbol_1;
  std::string legacy_symbol_2;
  std::string parent;
  milo_scene::Xfm local;
  milo_scene::Xfm world;
  int32_t constraint = 0;
  std::string target;
  bool preserve_scale = false;
  bool showing = false;
  float draw_order = 0.0f;
  float frame = 0.0f;
  int32_t anim_rate = 0;
  bool decoded = false;
  std::string error;
};

WorldFx decode_world_fx(const std::string& entry_name,
                        const std::vector<uint8_t>& body);

struct CharMeshHideRow {
  std::string drawable;
  int32_t flags = 0;
  bool show = false;
};

struct CharMeshHide {
  std::string name;
  int32_t revision = 0;
  int32_t alt_revision = 0;
  int32_t flags = 0;
  std::vector<CharMeshHideRow> hides;
  size_t unread_bytes = 0;
  bool decoded = false;
  std::string error;
};

struct SourceRndTexLoadPlan {
  int32_t revision = 0;
  int32_t alt_revision = 0;
  bool stream_cached = false;
  bool accepted_revision = false;
  bool reads_object_fields = false;
  bool reads_short_dimensions = false;
  bool reads_int_dimensions = true;
  bool calls_set_power_of_two = true;
  bool reads_bpp = true;
  bool reads_filepath = true;
  bool creates_uncached_loader = false;
  bool creates_cached_loader = false;
  bool pushes_revision = true;
  bool reads_legacy_cubemap_mask = false;
  bool reads_legacy_bool = false;
  bool reads_float_mip_map_k = false;
  bool reads_fixed_mip_map_k = false;
  bool reads_direct_type = false;
  bool reads_legacy_type_index = false;
  bool reads_rendered_bool_type = false;
  bool reads_post_flag = false;
  bool reads_optimize_for_ps3 = false;
  bool delegates_cached_payload_to_bitmap = false;
};

struct SourceRndTexSavePlan {
  int32_t save_id = 744;
};

SourceRndTexLoadPlan source_rndtex_load_plan(
    int32_t revision,
    int32_t alt_revision,
    bool stream_cached);
SourceRndTexSavePlan source_rndtex_save_plan();

struct SourceRndTexPowerOfTwoPlan {
  int32_t width = 0;
  int32_t height = 0;
  bool width_is_power_of_two = true;
  bool height_is_power_of_two = true;
  bool result = true;
};

struct SourceRndTexCheckDimPlan {
  int32_t dim = 0;
  int32_t type = 1;
  bool file = false;
  bool gfx_mode_zero = true;
  bool zero_dimension_ok = false;
  bool movie_multiple_of_16_required = false;
  bool gfx_max_1024_required = false;
  bool gfx_max_2048_required = false;
  bool gfx_multiple_of_8_required = false;
  bool file_power_of_two_required = false;
  std::string error;
};

struct SourceRndTexCheckSizePlan {
  int32_t width = 0;
  int32_t height = 0;
  int32_t bpp = 32;
  int32_t num_mips = 0;
  int32_t type = 1;
  bool file = false;
  bool gfx_mode_zero = true;
  bool bypass_device_or_density = false;
  bool checked_width = false;
  bool checked_height = false;
  bool checked_bpp = false;
  bool bpp_valid = false;
  bool checked_total_size = false;
  int64_t byte_size = 0;
  bool checked_mip_count = false;
  std::string error;
};

struct SourceRndTexRenderedClampPlan {
  std::string name;
  int32_t initial_width = 0;
  int32_t initial_height = 0;
  int32_t type = 1;
  bool filepath_empty = false;
  bool rendered_type = false;
  bool movie_exception = false;
  bool clamped = false;
  int32_t result_width = 0;
  int32_t result_height = 0;
  bool result_power_of_two = true;
};

struct SourceRndTexCopyPlan {
  bool copy_from_max = false;
  int32_t source_type = 1;
  int32_t destination_type = 1;
  bool copies_superclass = true;
  bool creates_copy = true;
  bool copies_mip_map_k = true;
  bool aborts_for_copy_from_max_type_mismatch = false;
  bool calls_presync_bitmap = false;
  bool copies_type = false;
  bool copies_dimensions = false;
  bool recomputes_power_of_two = false;
  bool copies_bpp = false;
  bool copies_filepath = false;
  bool copies_num_mips = false;
  bool asserts_no_mips = false;
  bool copies_optimize_for_ps3 = false;
  bool creates_bitmap_from_source_bpp_order = false;
  bool calls_sync_bitmap = false;
};

struct SourceRndTexPrintPlan {
  std::vector<std::string> fields;
};

struct SourceRndTexHandlerPlan {
  std::vector<std::string> handlers;
  bool size_kb_formula_uses_width_height_bpp = true;
  int32_t check_line = 1082;
};

struct SourceRndTexOnSetBitmapPlan {
  int32_t data_array_size = 0;
  bool uses_file_path_overload = false;
  bool uses_explicit_bitmap_overload = false;
  std::vector<std::string> explicit_argument_order;
};

struct SourceRndTexOnSetRenderedPlan {
  bool is_render_target = false;
  int32_t num_mips = 0;
  bool asserts_is_render_target = true;
  bool calls_set_bitmap = false;
  bool uses_existing_dimensions_type_and_bpp = true;
  bool use_mips = false;
};

struct SourceRndTexPropSyncPlan {
  std::vector<std::string> get_only_props;
  std::vector<std::string> direct_props;
  std::vector<std::string> modify_alt_props;
};

struct SourceRndTexPlatformBppOrderPlan {
  std::string platform;
  std::string path_hint;
  bool has_alpha = false;
  int32_t input_bpp = 32;
  int32_t result_bpp = 32;
  int32_t result_order = 0;
  bool normal_texture = false;
  bool ps2_leaves_existing_values = false;
};

struct SourceRndTexSetBitmapPlan {
  int32_t width = 0;
  int32_t height = 0;
  int32_t bpp = 32;
  int32_t type = 1;
  bool use_mips = false;
  bool calls_presync_bitmap = true;
  bool clears_filepath = true;
  bool resets_bitmap = true;
  bool calls_set_power_of_two = true;
  bool back_buffer_uses_screen_values = false;
  bool rendered_counts_mips = false;
  int32_t rendered_mip_count = 0;
  bool checks_size = false;
  std::string size_error;
  bool creates_bitmap = false;
  bool skips_bitmap_for_special_type = false;
  bool asserts_before_generate_mips = false;
  bool calls_sync_bitmap = true;
  int32_t result_width = 0;
  int32_t result_height = 0;
  int32_t result_bpp = 32;
  int32_t result_num_mips = 0;
};

struct SourceRndTexSetBitmapFromBitmapPlan {
  std::string platform;
  int32_t bitmap_width = 0;
  int32_t bitmap_height = 0;
  int32_t bitmap_bpp = 32;
  int32_t bitmap_order = 0;
  int32_t bitmap_num_mips = 0;
  bool preserve_bitmap_format = false;
  bool calls_platform_bpp_order = true;
  bool resets_on_size_error = false;
  std::string size_error;
  int32_t create_bpp = 32;
  int32_t create_order = 0;
};

struct SourceRndTexSetBitmapFromLoaderPlan {
  bool has_loader = false;
  bool has_buffer = false;
  bool loader_is_current = false;
  bool edit_mode = false;
  std::string filepath;
  bool warns_disc_build_without_keep = false;
  bool uses_bottom_mip = false;
  bool creates_bitmap_from_buffer = false;
  bool copies_bottom_mip = false;
  bool resets_bitmap_and_dimensions = false;
  int32_t result_width = 0;
  int32_t result_height = 0;
  int32_t result_bpp = 32;
  int32_t result_num_mips = 0;
};

struct SourceRndTexCopyBottomMipPlan {
  int32_t source_mip_count = 0;
  bool asserts_distinct_bitmaps = true;
  bool walks_to_last_mip = false;
  int32_t selected_mip_index = 0;
};

struct SourceRndTexLockBitmapPlan {
  int32_t bitmap_order = 0;
  bool converts_ordered_bitmap_to_32bpp = false;
  bool creates_direct_bitmap_view = false;
  int32_t create_bpp = 32;
  int32_t create_order = 0;
};

struct SourceRndBitmapResetPlan {
  int32_t row_bytes = 0;
  int32_t height = 0;
  int32_t width = 0;
  int32_t bpp = 0x20;
  int32_t order = 1;
  bool clears_palette = true;
  bool clears_pixels = true;
  bool frees_buffer_when_present = false;
  bool resets_and_frees_mip_when_present = false;
};

struct SourceRndBitmapCreatePlan {
  int32_t width = 0;
  int32_t height = 0;
  int32_t row_bytes = 0;
  int32_t bpp = 32;
  int32_t order = 0;
  bool valid_dimensions = true;
  bool valid_bpp = true;
  bool deletes_existing_mip = true;
  bool frees_palette_argument_after_assignment = false;
  bool allocates_when_no_palette_and_no_buffer = false;
};

struct SourceRndBitmapSetMipPlan {
  int32_t width = 0;
  int32_t height = 0;
  int32_t bpp = 32;
  int32_t order = 0;
  bool has_mip = false;
  int32_t mip_width = 0;
  int32_t mip_height = 0;
  int32_t mip_bpp = 32;
  int32_t mip_order = 0;
  bool deletes_existing_mip = true;
  bool checks_half_dimensions = false;
  bool accepts_mip = false;
};

struct SourceRndBitmapLoadSafelyPlan {
  int32_t width = 0;
  int32_t height = 0;
  int32_t bpp = 32;
  int32_t row_bytes = 0;
  int32_t max_width = 0;
  int32_t max_height = 0;
  int32_t mip_count = 0;
  bool dimension_fallback = false;
  bool row_bytes_fallback = false;
  bool creates_8x8_32bpp_fallback = false;
  bool reads_palette_and_pixels = false;
  bool builds_mip_chain = false;
  bool result = false;
};

struct SourceRndBitmapNumMipsPlan {
  int32_t linked_mip_count = 0;
  int32_t returned_mip_count = 0;
  bool starts_from_this_bitmap = true;
  bool walks_mip_links = true;
};

struct SourceRndBitmapPixelBytesPlan {
  int32_t row_bytes = 0;
  int32_t height = 0;
  int32_t result = 0;
};

struct SourceReadChunksPlan {
  int32_t total_len = 0;
  int32_t max_chunk_size = 0;
  std::vector<int32_t> chunk_sizes;
};

struct SourceRndBitmapSaveHeaderPlan {
  int32_t bitmap_revision = 1;
  int32_t bpp = 32;
  int32_t order = 0;
  int32_t num_mips = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t row_bytes = 0;
  int32_t pad_bytes = 0x13;
  std::vector<std::string> write_order;
};

struct SourceRndBitmapSavePlan {
  bool writes_header = true;
  bool has_palette = false;
  size_t palette_bytes = 0;
  bool writes_palette = false;
  int32_t chunk_size = 0x8000;
  std::vector<int32_t> pixel_write_bytes;
};

struct SourceRndBitmapDetachMipPlan {
  bool had_mip = false;
  bool returns_existing_mip = false;
  bool clears_mip = true;
};

struct SourceRndBitmapSamePixelFormatPlan {
  int32_t lhs_bpp = 32;
  int32_t rhs_bpp = 32;
  int32_t lhs_order = 0;
  int32_t rhs_order = 0;
  bool lhs_has_palette = false;
  bool rhs_has_palette = false;
  bool calls_same_palette_colors = false;
  bool same_palette_colors = true;
  bool result = false;
};

struct SourceRndBitmapBltPlan {
  int32_t dest_width = 0;
  int32_t dest_height = 0;
  int32_t source_width = 0;
  int32_t source_height = 0;
  int32_t dest_x = 0;
  int32_t dest_y = 0;
  int32_t source_x = 0;
  int32_t source_y = 0;
  int32_t width = 0;
  int32_t height = 0;
  bool dest_width_assert = false;
  bool dest_height_assert = false;
  bool source_width_assert = false;
  bool source_height_assert = false;
  bool same_pixel_format = false;
  bool reaches_empty_mismatch_body = false;
};

struct SourceBitmapFileHeaderStreamPlan {
  std::vector<std::string> read_order;
  std::vector<std::string> write_order;
};

struct SourceBitmapInfoHeaderStreamPlan {
  std::vector<std::string> read_order;
  std::vector<std::string> write_order;
};

struct SourcePreMultiplyAlphaPlan {
  bool has_empty_body = true;
  bool mutates_channels = false;
};

struct SourceRndBitmapColumnNonTransparentPlan {
  int32_t x = 0;
  int32_t y = 0;
  int32_t height = 0;
  std::vector<uint8_t> alpha_samples;
  bool samples_pixel_color = true;
  bool writes_last_transparent_y = false;
  int32_t last_transparent_y = 0;
  bool returns_true = false;
};

SourceRndTexPowerOfTwoPlan source_rndtex_power_of_two_plan(
    int32_t width,
    int32_t height);
SourceRndTexCheckDimPlan source_rndtex_check_dim_plan(
    int32_t dim,
    int32_t type,
    bool file,
    bool gfx_mode_zero);
SourceRndTexCheckSizePlan source_rndtex_check_size_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t num_mips,
    int32_t type,
    bool file,
    bool gfx_mode_zero);
SourceRndTexRenderedClampPlan source_rndtex_rendered_clamp_plan(
    const std::string& name,
    int32_t width,
    int32_t height,
    int32_t type,
    bool filepath_empty);
SourceRndTexCopyPlan source_rndtex_copy_plan(
    bool copy_from_max,
    int32_t source_type,
    int32_t destination_type);
std::string source_rndtex_type_name(int32_t type);
SourceRndTexPrintPlan source_rndtex_print_plan();
SourceRndTexHandlerPlan source_rndtex_handler_plan();
SourceRndTexOnSetBitmapPlan source_rndtex_on_set_bitmap_plan(
    int32_t data_array_size);
SourceRndTexOnSetRenderedPlan source_rndtex_on_set_rendered_plan(
    bool is_render_target,
    int32_t num_mips);
SourceRndTexPropSyncPlan source_rndtex_prop_sync_plan();
SourceRndTexPlatformBppOrderPlan source_rndtex_platform_bpp_order_plan(
    const std::string& platform,
    const std::string& path_hint,
    int32_t input_bpp,
    bool has_alpha);
SourceRndTexSetBitmapPlan source_rndtex_set_bitmap_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t type,
    bool use_mips,
    int32_t screen_width,
    int32_t screen_height,
    int32_t screen_bpp);
SourceRndTexSetBitmapFromBitmapPlan source_rndtex_set_bitmap_from_bitmap_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t order,
    int32_t num_mips,
    bool preserve_bitmap_format,
    const std::string& platform,
    const std::string& path_hint,
    bool has_alpha);
SourceRndTexSetBitmapFromLoaderPlan source_rndtex_set_bitmap_from_loader_plan(
    bool has_loader,
    bool has_buffer,
    bool loader_is_current,
    bool edit_mode,
    const std::string& filepath,
    bool use_bottom_mip,
    int32_t bitmap_width,
    int32_t bitmap_height,
    int32_t bitmap_bpp,
    int32_t bitmap_num_mips);
SourceRndTexCopyBottomMipPlan source_rndtex_copy_bottom_mip_plan(
    int32_t source_mip_count);
SourceRndTexLockBitmapPlan source_rndtex_lock_bitmap_plan(
    int32_t bitmap_order,
    int32_t bitmap_bpp);
SourceRndBitmapResetPlan source_rndbitmap_reset_plan(
    bool has_buffer,
    bool has_mip);
SourceRndBitmapCreatePlan source_rndbitmap_create_plan(
    int32_t width,
    int32_t height,
    int32_t row_bytes,
    int32_t bpp,
    int32_t order,
    bool has_palette,
    bool has_buffer);
SourceRndBitmapSetMipPlan source_rndbitmap_set_mip_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t order,
    bool has_mip,
    int32_t mip_width,
    int32_t mip_height,
    int32_t mip_bpp,
    int32_t mip_order);
SourceRndBitmapLoadSafelyPlan source_rndbitmap_load_safely_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t row_bytes,
    int32_t max_width,
    int32_t max_height,
    int32_t mip_count);
SourceRndBitmapNumMipsPlan source_rndbitmap_num_mips_plan(
    int32_t linked_mip_count);
SourceRndBitmapPixelBytesPlan source_rndbitmap_pixel_bytes_plan(
    int32_t row_bytes,
    int32_t height);
SourceReadChunksPlan source_read_chunks_plan(
    int32_t total_len,
    int32_t max_chunk_size);
SourceRndBitmapSaveHeaderPlan source_rndbitmap_save_header_plan(
    int32_t bpp,
    int32_t order,
    int32_t num_mips,
    int32_t width,
    int32_t height,
    int32_t row_bytes);
SourceRndBitmapSavePlan source_rndbitmap_save_plan(
    int32_t bpp,
    int32_t order,
    bool has_palette,
    const std::vector<int32_t>& row_bytes,
    const std::vector<int32_t>& heights);
SourceRndBitmapDetachMipPlan source_rndbitmap_detach_mip_plan(bool had_mip);
SourceRndBitmapSamePixelFormatPlan source_rndbitmap_same_pixel_format_plan(
    int32_t lhs_bpp,
    int32_t rhs_bpp,
    int32_t lhs_order,
    int32_t rhs_order,
    bool lhs_has_palette,
    bool rhs_has_palette,
    bool same_palette_colors);
SourceRndBitmapBltPlan source_rndbitmap_blt_plan(
    int32_t dest_width,
    int32_t dest_height,
    int32_t source_width,
    int32_t source_height,
    int32_t dest_x,
    int32_t dest_y,
    int32_t source_x,
    int32_t source_y,
    int32_t width,
    int32_t height,
    bool same_pixel_format);
SourceBitmapFileHeaderStreamPlan source_bitmap_file_header_stream_plan();
SourceBitmapInfoHeaderStreamPlan source_bitmap_info_header_stream_plan();
SourcePreMultiplyAlphaPlan source_premultiply_alpha_plan();
SourceRndBitmapColumnNonTransparentPlan
source_rndbitmap_column_nontransparent_plan(
    int32_t x,
    int32_t y,
    const std::vector<uint8_t>& alpha_samples);

struct RndTex {
  std::string name;
  int32_t version = 0;
  int32_t alt_version = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t bpp = 32;
  std::string filepath;
  bool power_of_two = true;
  int32_t cubemap_mask = 0;
  bool has_legacy_flag = false;
  bool legacy_flag = false;
  float mip_map_k = -8.0f;
  int32_t type = 1;  // RndTex::Regular
  bool has_post_flag = false;
  bool post_flag = false;
  bool optimize_for_ps3 = false;
  size_t cached_bitmap_bytes = 0;
  bool bitmap_header_decoded = false;
  int32_t bitmap_version = 0;
  int32_t bitmap_bpp = 0;
  uint32_t bitmap_order = 0;
  int32_t bitmap_mip_count = 0;
  int32_t bitmap_width = 0;
  int32_t bitmap_height = 0;
  int32_t bitmap_row_bytes = 0;
  size_t bitmap_palette_bytes = 0;
  size_t bitmap_base_pixel_bytes = 0;
  size_t bitmap_mip_pixel_bytes = 0;
  size_t bitmap_expected_payload_bytes = 0;
  size_t cached_bitmap_payload_bytes = 0;
  bool bitmap_payload_size_matches = false;
  std::string cached_bitmap_payload_prefix_hex;
  std::string bitmap_header_error;
};

struct CharDriver {
  std::string name;
  int32_t version = 0;
  int32_t weightable_version = 0;
  float weight = 1.0f;
  std::string weight_owner;
  std::string weight_prop;
  std::string target;
  std::string clip_milo;
  bool realign = false;
  bool midi = false;
  int32_t midi_version = 0;
  size_t midi_unread_bytes = 0;
  std::string midi_default_clip;
  std::string midi_legacy_string;
  std::string midi_parser;
  std::string midi_flag_parser;
  float midi_blend_override_pct = 1.0f;
};

struct CharWeightSetter {
  std::string name;
  int32_t version = 0;
  int32_t weightable_version = 0;
  float weight = 0.0f;
  std::string weight_owner;
  std::string weight_prop;
  std::string driver;
  uint32_t flags = 0;
  uint32_t mask = 0;
  float offset = 0.0f;
  float scale = 1.0f;
  float base_weight = 0.0f;
  float beats_per_weight = 0.0f;
  std::string base;
  std::vector<std::string> min_weights;
  std::vector<std::string> max_weights;
  size_t unread_bytes = 0;
};

struct AttachedPropTransformProxy {
  std::string name;
  std::string parent;
  milo_scene::Xfm local;
  milo_scene::Xfm bind_local;
};

struct CharacterLod {
  float screen_size = 0.0f;
  std::string group;
};

// A whole decoded band character.
struct Character {
  std::string dir_name;
  std::string dir_type;   // "BandCharacter"
  int32_t dir_version = 0;
  uint64_t dir_entry_offset = 0;
  uint64_t dir_entry_size = 0;
  // Exact serialized root bytes, plus the source-backed GH2 Character9 /
  // BandCharacter1 fields used by runtime selection. The bytes remain for
  // residual accounting and future revision work.
  std::vector<uint8_t> dir_entry_bytes;
  bool root_decoded = false;
  std::string root_decode_error;
  std::string root_object_type;
  std::vector<CharacterLod> root_lods;
  std::string root_shadow;
  bool root_self_shadow = false;
  std::string root_sphere_base;
  std::string root_environment;
  // Character is itself a RndTransformable. CharServoBone owns this LOCAL
  // transform; it must never receive the renderer's composed stage/world row.
  // Keep the stored world and parent/constraint metadata separately.
  milo_scene::TransObj root_transform;

  std::vector<SkinnedMesh> meshes;
  std::vector<milo_scene::TransObj> bones;  // skeleton (Trans "bone_*"/"spot_*")
  std::vector<milo_scene::Xfm> bind_mesh_local;
  std::vector<milo_scene::Xfm> bind_bone_local;
  std::vector<milo_scene::MatObj> mats;
  std::vector<milo_scene::GroupObj> groups;
  std::vector<RndMorph> morphs;
  std::vector<CharUpperTwist> upper_twists;
  std::vector<CharForeTwist> fore_twists;
  std::vector<Gh1AnimServoUpperTwist> gh1_upper_twists;
  std::vector<Gh1AnimServoForeTwist> gh1_fore_twists;
  std::vector<CharNeckTwist> neck_twists;
  std::vector<CharIKRod> ik_rods;
  std::vector<CharIKHand> ik_hands;
  std::vector<CharIKMidi> ik_midis;
  std::vector<CharServoBone> servo_bones;
  std::vector<CharLookAt> lookats;
  std::vector<CharEyes> eyes;
  std::vector<CharHair> hairs;
  std::vector<CharCollide> collides;
  std::vector<CharPosConstraint> pos_constraints;
  std::vector<CharBoneOffset> bone_offsets;
  std::vector<CharBoneTwist> bone_twists;
  std::vector<FaceFxLipSyncServo> lip_sync_servos;
  std::vector<RndAnimFilter> anim_filters;
  std::vector<EventTrigger> event_triggers;
  std::vector<ObjectRow> object_rows;
  std::vector<OutfitLoader> outfit_loaders;
  std::vector<CharWalk> char_walks;
  std::vector<WorldFx> world_fxes;
  std::vector<CharMeshHide> mesh_hides;
  std::vector<RndTex> tex_rows;
  std::vector<OpaqueObjectRow> opaque_rows;
  std::vector<CharDriver> drivers;
  std::vector<CharWeightSetter> weight_setters;
  std::map<std::string, int> object_type_counts;
  std::map<std::string, float> runtime_weight_props;
  std::map<std::string, std::map<uint32_t, float>> runtime_driver_flag_weights;
  std::map<std::string, RuntimeIKMidiState> runtime_ik_midi_states;
  // Persistent CharIKHand controller +0x50 vectors. PS2 blends the destination
  // Trans world position into this row and uses it for the hand solve/stretch
  // write; it is controller state, not a per-frame authored bone local.
  std::map<std::string, std::array<float, 3>> runtime_ik_hand_targets;
  std::map<std::string, RuntimeIKHandMeasureState>
      runtime_ik_hand_measures;
  std::map<std::string, RuntimeGh2CharEyesState> runtime_gh2_char_eyes;
  std::map<std::string, RuntimeGh2CharLookAtState> runtime_gh2_char_lookats;
  std::map<std::string, SourceCharHairRuntime> source_char_hair_runtime;
  // PS2 Trans controllers can submit live world rows through the shared
  // writer without replacing the authored local rows that later controllers
  // still read. These are cleared per sampled frame.
  std::map<std::string, std::array<float, 16>> runtime_world_overrides;
  // CharBones output graphs can contain runtime-only Trans names which are not
  // resident Character meshes/bones. Keep their sampled worlds alive from the
  // clip publisher through the controller pass. GH1 AnimServoIK consumes the
  // bone_fret_hand/bone_strum_hand rows from this source-authored graph.
  std::map<std::string, std::array<float, 16>> runtime_pose_output_worlds;
  // Runtime ObjectDir assembly bridge. Instrument RndMesh objects also expose
  // transform rows to character drivers in GH1, but their geometry remains in
  // the attached prop renderer.
  std::map<std::string, AttachedPropTransformProxy>
      attached_prop_transform_proxies;
  // Names actually published by the authored fret-hand overlay this frame.
  // GH1 uses these to associate the MIDI fret-position stream with the
  // AnimServoIK destination graph without character- or instrument-name rules.
  std::vector<std::string> runtime_fret_driver_outputs;

  // Distinct diffuse-texture names referenced by the character's materials.
  std::vector<std::string> texture_names() const;

  // Resolve a material by name (nullptr if absent).
  const milo_scene::MatObj* find_mat(const std::string& name) const;

  // Compute a transform's CURRENT POSE world matrix with source
  // RndTransformable constraint rules.
  std::array<float, 16> bone_world(const std::string& bone_name) const;

  // Compute a transform's BIND POSE world matrix from source local rows and
  // constraints.
  std::array<float, 16> bone_world_bind(const std::string& bone_name) const;

  // Compatibility names for existing animation/controller code; these now use
  // the same source transform evaluator as bone_world().
  std::array<float, 16> bone_world_local_chain(const std::string& bone_name) const;
  std::array<float, 16> bone_world_local_chain_authored(const std::string& bone_name) const;
  std::array<float, 16> bone_world_bind_local_chain(const std::string& bone_name) const;

  // Compose a mesh's own source transform world matrix.
  std::array<float, 16> mesh_world(const SkinnedMesh& m) const;
  std::array<float, 16> model_space_parent_delta(
      const std::string& parent) const;
  std::array<float, 16> attachment_parent_world(
      const std::string& parent) const;
  std::array<float, 16> mesh_attachment_world(const SkinnedMesh& m,
                                              bool bind_local) const;
  bool has_transform(const std::string& name) const;
};

struct SourceCharacterDrawClosure {
  bool authoritative = false;
  std::unordered_set<std::string> meshes;
};

// Native render-policy helper for the project hair two-sided override. The
// source-backed part is the decoded CharHair point-bone membership; the helper
// intentionally does not infer blend, depth, sort, or physics behavior.
bool character_mesh_uses_char_hair_point_bone(const Character& character,
                                              const SkinnedMesh& mesh);

// Decode one skinned-mesh entry body. Never throws: on failure returns a
// SkinnedMesh with decoded=false and a populated .error.
SkinnedMesh decode_skinned_mesh(const std::string& entry_name,
                                const std::vector<uint8_t>& body,
                                int32_t parent_dir_revision = 24);
CharHair decode_hair(const std::string& entry_name,
                     const std::vector<uint8_t>& body);
CharCollide decode_collide(const std::string& entry_name,
                           const std::vector<uint8_t>& body,
                           int32_t parent_dir_revision = 24);
CharPosConstraint decode_pos_constraint(const std::string& entry_name,
                                        const std::vector<uint8_t>& body);
CharLookAt decode_lookat(const std::string& entry_name,
                         const std::vector<uint8_t>& body);
CharEyes decode_eyes(const std::string& entry_name,
                     const std::vector<uint8_t>& body);
RndTex decode_rnd_tex(const std::string& entry_name,
                      const std::vector<uint8_t>& body);
RndMorph decode_rnd_morph(const std::string& entry_name,
                           const std::vector<uint8_t>& body);
CharMeshHide decode_char_mesh_hide(const std::string& entry_name,
                                   const std::vector<uint8_t>& body);
// Apply an authored RndMorph frame to its same-directory target mesh. GH1
// setup supplies an empty serialized target and binds by morph stem
// (face.mrf -> face.mesh, lashes.mrf -> lashes.mesh).
bool apply_rnd_morph_frame(Character& character,
                           std::string_view morph_name,
                           float frame);
// Resolve the authored animation frame at which a named pose reaches its
// greatest weight. Pose names come from GH1 face_data; RndMorph stores the
// corresponding same-directory mesh name.
std::optional<float> rnd_morph_pose_peak_frame(
    const Character& character, std::string_view morph_name,
    std::string_view pose_name);

// Load + decode a whole BandCharacter MILO from a PS2 ARK (runtime-native: read
// the .milo_ps2 from the ARK, decode in memory — no intermediate extraction).
// Returns false (with a logged reason) if the MILO cannot be read; a partial
// decode (some meshes fail) still returns true with those meshes flagged.
bool load_character(const std::string& hdr_path, const std::string& ark_path,
                    const std::string& milo_path, Character& out);

}  // namespace ghogx::character
