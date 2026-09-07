# ghogx_character — skinned character mesh decode + bind-pose/animated renderer.
# Decodes BandCharacter PS2 MILOs (bone skeleton + skinned meshes + materials),
# provides CharRenderer for D3D9 display. Win32-only (D3D9 dependency).

if(NOT WIN32)
    return()
endif()

add_library(ghogx_character STATIC
    char_mesh.cpp
    char_mesh.h
    char_facefx.cpp
    char_facefx.h
    char_renderer.cpp
    char_renderer.h
    char_clip.cpp
    char_clip.h
    char_clip_binding.cpp
    char_clip_binding.h
    char_bones_output.cpp
    char_bones_output.h
    char_bones_samples.h
    char_servo.cpp
    char_servo.h
    char_servo_character.cpp
    char_servo_character.h
    character_type_script.cpp
    character_type_script.h
)
target_include_directories(ghogx_character PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(ghogx_character PUBLIC
    ghogx_milo_scene
    ghogx_asset
    ghogx_script
    ark_v3 dtb ps2_texture milo milo_object
    d3d9
)

add_executable(ghogx_character_type_script_test
    character_type_script_test.cpp)
target_link_libraries(ghogx_character_type_script_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_type_script_test
         COMMAND ghogx_character_type_script_test)

add_executable(ghogx_character_bind_audit char_bind_audit.cpp)
target_link_libraries(ghogx_character_bind_audit PRIVATE ghogx_character)

add_executable(ghogx_character_clip_audit char_clip_audit.cpp)
target_link_libraries(ghogx_character_clip_audit PRIVATE ghogx_character)

add_executable(ghogx_character_facefx_audit char_facefx_audit.cpp)
target_link_libraries(ghogx_character_facefx_audit PRIVATE ghogx_character)

add_executable(ghogx_character_pose_export char_pose_export.cpp)
target_link_libraries(ghogx_character_pose_export PRIVATE
    ghogx_character ghogx_render)

add_executable(ghogx_character_clip_driver_flags_test
    character_clip_driver_flags_test.cpp)
target_link_libraries(ghogx_character_clip_driver_flags_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_clip_driver_flags_test
         COMMAND ghogx_character_clip_driver_flags_test)

add_executable(ghogx_character_servo_runtime_test character_servo_runtime_test.cpp)
target_link_libraries(ghogx_character_servo_runtime_test PRIVATE ghogx_character)
add_test(NAME ghogx_character_servo_runtime_test COMMAND ghogx_character_servo_runtime_test)

add_executable(ghogx_character_servo_character_test character_servo_character_test.cpp)
target_link_libraries(ghogx_character_servo_character_test PRIVATE ghogx_character)
add_test(NAME ghogx_character_servo_character_test COMMAND ghogx_character_servo_character_test)

add_executable(ghogx_character_clip_binding_test character_clip_binding_test.cpp)
target_link_libraries(ghogx_character_clip_binding_test PRIVATE ghogx_character)
add_test(NAME ghogx_character_clip_binding_test COMMAND ghogx_character_clip_binding_test)

add_executable(ghogx_character_bones_output_test character_bones_output_test.cpp)
target_link_libraries(ghogx_character_bones_output_test PRIVATE ghogx_character)
add_test(NAME ghogx_character_bones_output_test COMMAND ghogx_character_bones_output_test)

add_executable(ghogx_character_facefx_materialization_test
    character_facefx_materialization_test.cpp)
target_link_libraries(ghogx_character_facefx_materialization_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_facefx_materialization_test
         COMMAND ghogx_character_facefx_materialization_test)

add_executable(ghogx_character_clip_set_source_test
    character_clip_set_source_test.cpp)
target_link_libraries(ghogx_character_clip_set_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_clip_set_source_test
         COMMAND ghogx_character_clip_set_source_test)

add_executable(ghogx_character_clip_display_source_test
    character_clip_display_source_test.cpp)
target_link_libraries(ghogx_character_clip_display_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_clip_display_source_test
         COMMAND ghogx_character_clip_display_source_test)

add_executable(ghogx_character_clip_editor_source_test
    character_clip_editor_source_test.cpp)
target_link_libraries(ghogx_character_clip_editor_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_clip_editor_source_test
         COMMAND ghogx_character_clip_editor_source_test)

add_executable(ghogx_character_char_bones_source_test
    character_char_bones_source_test.cpp)
target_link_libraries(ghogx_character_char_bones_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_char_bones_source_test
         COMMAND ghogx_character_char_bones_source_test)

add_executable(ghogx_character_char_utl_source_test
    character_char_utl_source_test.cpp)
target_link_libraries(ghogx_character_char_utl_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_char_utl_source_test
         COMMAND ghogx_character_char_utl_source_test)

add_executable(ghogx_character_ik_rod_source_test
    character_ik_rod_source_test.cpp)
target_link_libraries(ghogx_character_ik_rod_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_rod_source_test
         COMMAND ghogx_character_ik_rod_source_test)

add_executable(ghogx_character_ik_hand_source_test
    character_ik_hand_source_test.cpp)
target_link_libraries(ghogx_character_ik_hand_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_hand_source_test
         COMMAND ghogx_character_ik_hand_source_test)

add_executable(ghogx_character_ik_foot_source_test
    character_ik_foot_source_test.cpp)
target_link_libraries(ghogx_character_ik_foot_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_foot_source_test
         COMMAND ghogx_character_ik_foot_source_test)

add_executable(ghogx_character_ik_head_source_test
    character_ik_head_source_test.cpp)
target_link_libraries(ghogx_character_ik_head_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_head_source_test
         COMMAND ghogx_character_ik_head_source_test)

add_executable(ghogx_character_ik_slider_midi_source_test
    character_ik_slider_midi_source_test.cpp)
target_link_libraries(ghogx_character_ik_slider_midi_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_slider_midi_source_test
         COMMAND ghogx_character_ik_slider_midi_source_test)

add_executable(ghogx_character_ik_midi_source_test
    character_ik_midi_source_test.cpp)
target_link_libraries(ghogx_character_ik_midi_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_midi_source_test
         COMMAND ghogx_character_ik_midi_source_test)

add_executable(ghogx_character_bone_offset_source_test
    character_bone_offset_source_test.cpp)
target_link_libraries(ghogx_character_bone_offset_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_bone_offset_source_test
         COMMAND ghogx_character_bone_offset_source_test)

add_executable(ghogx_character_bone_twist_source_test
    character_bone_twist_source_test.cpp)
target_link_libraries(ghogx_character_bone_twist_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_bone_twist_source_test
         COMMAND ghogx_character_bone_twist_source_test)

add_executable(ghogx_character_fore_upper_twist_source_test
    character_fore_upper_twist_source_test.cpp)
target_link_libraries(ghogx_character_fore_upper_twist_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_fore_upper_twist_source_test
         COMMAND ghogx_character_fore_upper_twist_source_test)

add_executable(ghogx_character_lookat_source_test
    character_lookat_source_test.cpp)
target_link_libraries(ghogx_character_lookat_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_lookat_source_test
         COMMAND ghogx_character_lookat_source_test)

add_executable(ghogx_character_weight_setter_source_test
    character_weight_setter_source_test.cpp)
target_link_libraries(ghogx_character_weight_setter_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_weight_setter_source_test
         COMMAND ghogx_character_weight_setter_source_test)

add_executable(ghogx_character_mirror_source_test
    character_mirror_source_test.cpp)
target_link_libraries(ghogx_character_mirror_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_mirror_source_test
         COMMAND ghogx_character_mirror_source_test)

add_executable(ghogx_character_char_hair_source_test
    character_char_hair_source_test.cpp)
target_link_libraries(ghogx_character_char_hair_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_char_hair_source_test
         COMMAND ghogx_character_char_hair_source_test)

add_executable(ghogx_character_char_collide_source_test
    character_char_collide_source_test.cpp)
target_link_libraries(ghogx_character_char_collide_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_char_collide_source_test
         COMMAND ghogx_character_char_collide_source_test)

add_executable(ghogx_character_event_trigger_source_test
    character_event_trigger_source_test.cpp)
target_link_libraries(ghogx_character_event_trigger_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_event_trigger_source_test
         COMMAND ghogx_character_event_trigger_source_test)

add_executable(ghogx_character_anim_filter_source_test
    character_anim_filter_source_test.cpp)
target_link_libraries(ghogx_character_anim_filter_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_anim_filter_source_test
         COMMAND ghogx_character_anim_filter_source_test)

add_executable(ghogx_character_tex_source_test
    character_tex_source_test.cpp)
target_link_libraries(ghogx_character_tex_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_tex_source_test
         COMMAND ghogx_character_tex_source_test)

add_executable(ghogx_character_face_servo_source_test
    character_face_servo_source_test.cpp)
target_link_libraries(ghogx_character_face_servo_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_face_servo_source_test
         COMMAND ghogx_character_face_servo_source_test)

add_executable(ghogx_character_lip_sync_source_test
    character_lip_sync_source_test.cpp)
target_link_libraries(ghogx_character_lip_sync_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_lip_sync_source_test
         COMMAND ghogx_character_lip_sync_source_test)

add_executable(ghogx_character_mesh_hide_source_test
    character_mesh_hide_source_test.cpp)
target_link_libraries(ghogx_character_mesh_hide_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_mesh_hide_source_test
         COMMAND ghogx_character_mesh_hide_source_test)

add_executable(ghogx_character_trans_copy_source_test
    character_trans_copy_source_test.cpp)
target_link_libraries(ghogx_character_trans_copy_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_trans_copy_source_test
         COMMAND ghogx_character_trans_copy_source_test)

add_executable(ghogx_character_poll_group_source_test
    character_poll_group_source_test.cpp)
target_link_libraries(ghogx_character_poll_group_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_poll_group_source_test
         COMMAND ghogx_character_poll_group_source_test)

add_executable(ghogx_character_ik_scale_source_test
    character_ik_scale_source_test.cpp)
target_link_libraries(ghogx_character_ik_scale_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_scale_source_test
         COMMAND ghogx_character_ik_scale_source_test)

add_executable(ghogx_character_trans_draw_source_test
    character_trans_draw_source_test.cpp)
target_link_libraries(ghogx_character_trans_draw_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_trans_draw_source_test
         COMMAND ghogx_character_trans_draw_source_test)

add_executable(ghogx_character_cuff_source_test
    character_cuff_source_test.cpp)
target_link_libraries(ghogx_character_cuff_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_cuff_source_test
         COMMAND ghogx_character_cuff_source_test)

add_executable(ghogx_character_blend_bone_source_test
    character_blend_bone_source_test.cpp)
target_link_libraries(ghogx_character_blend_bone_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_blend_bone_source_test
         COMMAND ghogx_character_blend_bone_source_test)

add_executable(ghogx_character_sleeve_source_test
    character_sleeve_source_test.cpp)
target_link_libraries(ghogx_character_sleeve_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_sleeve_source_test
         COMMAND ghogx_character_sleeve_source_test)

add_executable(ghogx_character_pos_constraint_source_test
    character_pos_constraint_source_test.cpp)
target_link_libraries(ghogx_character_pos_constraint_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_pos_constraint_source_test
         COMMAND ghogx_character_pos_constraint_source_test)

add_executable(ghogx_character_waypoint_source_test
    character_waypoint_source_test.cpp)
target_link_libraries(ghogx_character_waypoint_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_waypoint_source_test
         COMMAND ghogx_character_waypoint_source_test)

add_executable(ghogx_character_guitar_string_source_test
    character_guitar_string_source_test.cpp)
target_link_libraries(ghogx_character_guitar_string_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_guitar_string_source_test
         COMMAND ghogx_character_guitar_string_source_test)

add_executable(ghogx_character_eyes_source_test
    character_eyes_source_test.cpp)
target_link_libraries(ghogx_character_eyes_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_eyes_source_test
         COMMAND ghogx_character_eyes_source_test)

add_executable(ghogx_character_eye_dart_ruleset_source_test
    character_eye_dart_ruleset_source_test.cpp)
target_link_libraries(ghogx_character_eye_dart_ruleset_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_eye_dart_ruleset_source_test
         COMMAND ghogx_character_eye_dart_ruleset_source_test)

add_executable(ghogx_character_interest_source_test
    character_interest_source_test.cpp)
target_link_libraries(ghogx_character_interest_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_interest_source_test
         COMMAND ghogx_character_interest_source_test)

add_executable(ghogx_character_neck_twist_source_test
    character_neck_twist_source_test.cpp)
target_link_libraries(ghogx_character_neck_twist_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_neck_twist_source_test
         COMMAND ghogx_character_neck_twist_source_test)

add_executable(ghogx_character_ik_fingers_source_test
    character_ik_fingers_source_test.cpp)
target_link_libraries(ghogx_character_ik_fingers_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_ik_fingers_source_test
         COMMAND ghogx_character_ik_fingers_source_test)

add_executable(ghogx_character_character_source_test
    character_character_source_test.cpp)
target_link_libraries(ghogx_character_character_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_character_source_test
         COMMAND ghogx_character_character_source_test)

add_executable(ghogx_character_character_test_source_test
    character_character_test_source_test.cpp)
target_link_libraries(ghogx_character_character_test_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_character_test_source_test
         COMMAND ghogx_character_character_test_source_test)

add_executable(ghogx_character_mesh_decode_test character_mesh_decode_test.cpp)
target_link_libraries(ghogx_character_mesh_decode_test PRIVATE ghogx_character)
add_test(NAME ghogx_character_mesh_decode_test
         COMMAND ghogx_character_mesh_decode_test)

add_executable(ghogx_character_skin_order_test character_skin_order_test.cpp)
target_link_libraries(ghogx_character_skin_order_test PRIVATE
    ghogx_character ghogx_render)
add_test(NAME ghogx_character_skin_order_test
         COMMAND ghogx_character_skin_order_test)

add_executable(ghogx_character_mesh_cache_source_test
    character_mesh_cache_source_test.cpp)
target_link_libraries(ghogx_character_mesh_cache_source_test PRIVATE
    ghogx_character)
add_test(NAME ghogx_character_mesh_cache_source_test
         COMMAND ghogx_character_mesh_cache_source_test)

add_executable(ghogx_character_no_named_fix_test character_no_named_fix_test.cpp)
target_compile_definitions(ghogx_character_no_named_fix_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ghogx_character_no_named_fix_test
         COMMAND ghogx_character_no_named_fix_test)

add_executable(ghogx_character_left_hand_contract_test
    character_left_hand_contract_test.cpp)
target_compile_definitions(ghogx_character_left_hand_contract_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GHOGX_GAME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../game"
    GHOGX_APP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../app")
add_test(NAME ghogx_character_left_hand_contract_test
         COMMAND ghogx_character_left_hand_contract_test)

add_executable(ghogx_character_eye_bridge_contract_test
    character_eye_bridge_contract_test.cpp)
target_compile_definitions(ghogx_character_eye_bridge_contract_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ghogx_character_eye_bridge_contract_test
         COMMAND ghogx_character_eye_bridge_contract_test)

set(GHOGX_IHATECOMPVIR_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/ihatecompvir-public-milo-sources")
set(GHOGX_IHATECOMPVIR_EXTRA_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/ihatecompvir-extra")
add_executable(ghogx_character_hand_driver_weight_contract_test
    character_hand_driver_weight_contract_test.cpp)
target_compile_definitions(ghogx_character_hand_driver_weight_contract_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GHOGX_GAME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../game")
add_test(NAME ghogx_character_hand_driver_weight_contract_test
         COMMAND ghogx_character_hand_driver_weight_contract_test)

add_executable(ghogx_character_ihatecompvir_inventory_test
    character_ihatecompvir_inventory_test.cpp)
target_compile_definitions(ghogx_character_ihatecompvir_inventory_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GHOGX_IHATECOMPVIR_EXTRA_DIR="${GHOGX_IHATECOMPVIR_EXTRA_DIR}")
add_test(NAME ghogx_character_ihatecompvir_inventory_test
         COMMAND ghogx_character_ihatecompvir_inventory_test)

add_executable(ghogx_character_source_truth_contract_test
    character_source_truth_contract_test.cpp)
target_compile_definitions(ghogx_character_source_truth_contract_test PRIVATE
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GHOGX_MILO_SCENE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../milo_scene"
    GHOGX_IHATECOMPVIR_SOURCE_DIR="${GHOGX_IHATECOMPVIR_SOURCE_DIR}"
    GHOGX_IHATECOMPVIR_EXTRA_DIR="${GHOGX_IHATECOMPVIR_EXTRA_DIR}")
add_test(NAME ghogx_character_source_truth_contract_test
         COMMAND ghogx_character_source_truth_contract_test)

add_custom_target(ghogx_character_source_tests
    DEPENDS
        ghogx_character_clip_driver_flags_test
        ghogx_character_servo_runtime_test
        ghogx_character_clip_binding_test
        ghogx_character_bones_output_test
        ghogx_character_facefx_materialization_test
        ghogx_character_clip_set_source_test
        ghogx_character_clip_display_source_test
        ghogx_character_clip_editor_source_test
        ghogx_character_char_bones_source_test
        ghogx_character_char_utl_source_test
        ghogx_character_ik_rod_source_test
        ghogx_character_ik_hand_source_test
        ghogx_character_ik_foot_source_test
        ghogx_character_ik_head_source_test
        ghogx_character_ik_slider_midi_source_test
        ghogx_character_ik_midi_source_test
        ghogx_character_bone_offset_source_test
        ghogx_character_bone_twist_source_test
        ghogx_character_fore_upper_twist_source_test
        ghogx_character_lookat_source_test
        ghogx_character_weight_setter_source_test
        ghogx_character_mirror_source_test
        ghogx_character_char_hair_source_test
        ghogx_character_char_collide_source_test
        ghogx_character_event_trigger_source_test
        ghogx_character_anim_filter_source_test
        ghogx_character_tex_source_test
        ghogx_character_face_servo_source_test
        ghogx_character_lip_sync_source_test
        ghogx_character_mesh_hide_source_test
        ghogx_character_trans_copy_source_test
        ghogx_character_poll_group_source_test
        ghogx_character_ik_scale_source_test
        ghogx_character_trans_draw_source_test
        ghogx_character_cuff_source_test
        ghogx_character_blend_bone_source_test
        ghogx_character_sleeve_source_test
        ghogx_character_pos_constraint_source_test
        ghogx_character_waypoint_source_test
        ghogx_character_guitar_string_source_test
        ghogx_character_eyes_source_test
        ghogx_character_eye_dart_ruleset_source_test
        ghogx_character_interest_source_test
        ghogx_character_neck_twist_source_test
        ghogx_character_ik_fingers_source_test
        ghogx_character_character_source_test
        ghogx_character_character_test_source_test
        ghogx_character_mesh_decode_test
        ghogx_character_skin_order_test
        ghogx_character_mesh_cache_source_test
        ghogx_character_no_named_fix_test
        ghogx_character_left_hand_contract_test
        ghogx_character_eye_bridge_contract_test
        ghogx_character_hand_driver_weight_contract_test
        ghogx_character_ihatecompvir_inventory_test
        ghogx_character_source_truth_contract_test)

if(MSVC)
target_compile_options(ghogx_character PRIVATE /W4)
target_compile_options(ghogx_character_bind_audit PRIVATE /W4)
target_compile_options(ghogx_character_clip_audit PRIVATE /W4)
target_compile_options(ghogx_character_facefx_audit PRIVATE /W4)
target_compile_options(ghogx_character_clip_driver_flags_test PRIVATE /W4)
target_compile_options(ghogx_character_servo_runtime_test PRIVATE /W4)
target_compile_options(ghogx_character_clip_binding_test PRIVATE /W4)
target_compile_options(ghogx_character_bones_output_test PRIVATE /W4)
target_compile_options(ghogx_character_facefx_materialization_test PRIVATE /W4)
target_compile_options(ghogx_character_clip_set_source_test PRIVATE /W4)
target_compile_options(ghogx_character_clip_display_source_test PRIVATE /W4)
target_compile_options(ghogx_character_clip_editor_source_test PRIVATE /W4)
target_compile_options(ghogx_character_char_bones_source_test PRIVATE /W4)
target_compile_options(ghogx_character_char_utl_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_rod_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_hand_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_foot_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_head_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_slider_midi_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_midi_source_test PRIVATE /W4)
target_compile_options(ghogx_character_bone_offset_source_test PRIVATE /W4)
target_compile_options(ghogx_character_bone_twist_source_test PRIVATE /W4)
target_compile_options(ghogx_character_fore_upper_twist_source_test PRIVATE /W4)
target_compile_options(ghogx_character_lookat_source_test PRIVATE /W4)
target_compile_options(ghogx_character_weight_setter_source_test PRIVATE /W4)
target_compile_options(ghogx_character_mirror_source_test PRIVATE /W4)
target_compile_options(ghogx_character_char_hair_source_test PRIVATE /W4)
target_compile_options(ghogx_character_char_collide_source_test PRIVATE /W4)
target_compile_options(ghogx_character_event_trigger_source_test PRIVATE /W4)
target_compile_options(ghogx_character_anim_filter_source_test PRIVATE /W4)
target_compile_options(ghogx_character_tex_source_test PRIVATE /W4)
target_compile_options(ghogx_character_face_servo_source_test PRIVATE /W4)
target_compile_options(ghogx_character_lip_sync_source_test PRIVATE /W4)
target_compile_options(ghogx_character_mesh_hide_source_test PRIVATE /W4)
target_compile_options(ghogx_character_trans_copy_source_test PRIVATE /W4)
target_compile_options(ghogx_character_poll_group_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_scale_source_test PRIVATE /W4)
target_compile_options(ghogx_character_trans_draw_source_test PRIVATE /W4)
target_compile_options(ghogx_character_cuff_source_test PRIVATE /W4)
target_compile_options(ghogx_character_blend_bone_source_test PRIVATE /W4)
target_compile_options(ghogx_character_sleeve_source_test PRIVATE /W4)
target_compile_options(ghogx_character_pos_constraint_source_test PRIVATE /W4)
target_compile_options(ghogx_character_waypoint_source_test PRIVATE /W4)
target_compile_options(ghogx_character_guitar_string_source_test PRIVATE /W4)
target_compile_options(ghogx_character_eyes_source_test PRIVATE /W4)
target_compile_options(ghogx_character_eye_dart_ruleset_source_test PRIVATE /W4)
target_compile_options(ghogx_character_interest_source_test PRIVATE /W4)
target_compile_options(ghogx_character_neck_twist_source_test PRIVATE /W4)
target_compile_options(ghogx_character_ik_fingers_source_test PRIVATE /W4)
target_compile_options(ghogx_character_character_source_test PRIVATE /W4)
target_compile_options(ghogx_character_character_test_source_test PRIVATE /W4)
target_compile_options(ghogx_character_mesh_decode_test PRIVATE /W4)
target_compile_options(ghogx_character_skin_order_test PRIVATE /W4)
target_compile_options(ghogx_character_mesh_cache_source_test PRIVATE /W4)
target_compile_options(ghogx_character_no_named_fix_test PRIVATE /W4)
target_compile_options(ghogx_character_left_hand_contract_test PRIVATE /W4)
target_compile_options(ghogx_character_eye_bridge_contract_test PRIVATE /W4)
target_compile_options(ghogx_character_hand_driver_weight_contract_test PRIVATE /W4)
target_compile_options(ghogx_character_ihatecompvir_inventory_test PRIVATE /W4)
target_compile_options(ghogx_character_source_truth_contract_test PRIVATE
    /W4 /bigobj)
else()
    target_compile_options(ghogx_character PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_bind_audit PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_clip_audit PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_facefx_audit PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_clip_driver_flags_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_servo_runtime_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_clip_binding_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_bones_output_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_facefx_materialization_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_clip_set_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_clip_display_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_clip_editor_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_char_bones_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_char_utl_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_rod_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_hand_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_foot_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_head_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_slider_midi_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_midi_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_bone_offset_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_bone_twist_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_fore_upper_twist_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_lookat_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_weight_setter_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_mirror_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_char_hair_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_char_collide_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_event_trigger_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_anim_filter_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_tex_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_face_servo_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_lip_sync_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_mesh_hide_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_trans_copy_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_poll_group_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_scale_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_trans_draw_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_cuff_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_blend_bone_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_sleeve_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_pos_constraint_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_waypoint_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_guitar_string_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_eyes_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_eye_dart_ruleset_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_interest_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_neck_twist_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_ik_fingers_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_character_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_character_test_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_mesh_decode_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_skin_order_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_mesh_cache_source_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_no_named_fix_test PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_left_hand_contract_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_eye_bridge_contract_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_hand_driver_weight_contract_test
        PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_character_ihatecompvir_inventory_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_character_source_truth_contract_test PRIVATE
        -Wall -Wextra)
endif()
