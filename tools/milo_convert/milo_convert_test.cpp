#include "milo_convert.h"

#include "gh1_character_model_package.h"
#include "gh1_character_manifest.h"
#include "gh1_character_package.h"
#include "gh1_venue_camera_conversion.h"
#include "gh1_venue_placement_conversion.h"
#include "gh1_venue_script_conversion.h"
#include "gh2_face_config_patch.h"
#include "dtb.h"
#include "milo.h"
#include "milo_object.h"
#include "singer_face_track.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

int main() {
    try {
        const std::map<std::string, uint32_t> expected_milo_contracts = {
            {"ACP", 18},
            {"Cam", 9},
            {"CamAnim", 0},
            {"DTB", 0xFFFFFFFFu},
            {"Environ", 1},
            {"EnvAnim", 3},
            {"Flare", 3},
            {"Font", 7},
            {"Light", 3},
            {"LightAnim", 1},
            {"Mat", 21},
            {"MatAnim", 5},
            {"Mesh", 25},
            {"MeshAnim", 0},
            {"Morph", 3},
            {"Movie", 6},
            {"MultiMesh", 0},
            {"ParticleSys", 22},
            {"ParticleSysAnim", 2},
            {"Tex", 8},
            {"Text", 15},
            {"TransAnim", 4},
            {"VenueCamRecord", 1},
            {"VenueScript", 1},
            {"View", 7},
        };
        const std::set<std::string> allowed_dispositions = {
            "retained",
            "translated",
            "synthesized",
            "intentionally_discarded",
        };
        const auto& field_contracts =
            gh::milo_convert::gh1_to_gh2_semantic_field_contracts();
        std::set<std::pair<std::string, uint32_t>> covered_contracts;
        std::set<std::tuple<
            std::string, uint32_t, std::string, std::string, std::string>>
            unique_rows;
        for (const auto& row : field_contracts) {
            if (row.source_type.empty() || row.source_field.empty() ||
                row.target_type.empty() || row.target_revision.empty() ||
                row.target_field.empty() || row.rule.empty() ||
                row.verification.empty() ||
                allowed_dispositions.find(row.disposition) ==
                    allowed_dispositions.end()) {
                std::fprintf(
                    stderr,
                    "milo_convert_test: malformed semantic field "
                    "contract %s/%u/%s\n",
                    row.source_type.c_str(), row.source_revision,
                    row.source_field.c_str());
                return 1;
            }
            const auto expected =
                expected_milo_contracts.find(row.source_type);
            if (expected == expected_milo_contracts.end() ||
                expected->second != row.source_revision) {
                std::fprintf(
                    stderr,
                    "milo_convert_test: unexpected semantic field "
                    "contract %s/%u\n",
                    row.source_type.c_str(), row.source_revision);
                return 1;
            }
            covered_contracts.emplace(
                row.source_type, row.source_revision);
            const auto key = std::make_tuple(
                row.source_type, row.source_revision, row.source_field,
                row.target_type, row.target_field);
            if (!unique_rows.insert(key).second) {
                std::fprintf(
                    stderr,
                    "milo_convert_test: duplicate semantic field "
                    "contract %s/%u/%s\n",
                    row.source_type.c_str(), row.source_revision,
                    row.source_field.c_str());
                return 1;
            }
        }
        if (covered_contracts.size() != expected_milo_contracts.size()) {
            std::fprintf(
                stderr,
                "milo_convert_test: semantic field contract class "
                "coverage mismatch expected=%zu actual=%zu\n",
                expected_milo_contracts.size(), covered_contracts.size());
            return 1;
        }
        for (const auto& [type, revision] : expected_milo_contracts) {
            const auto rows =
                gh::milo_convert::
                    gh1_to_gh2_semantic_field_contracts_for(
                        type, revision);
            const auto schema =
                gh::milo_convert::
                    gh1_serialized_semantic_fields_for(
                        type, revision);
            if (rows.empty() || schema.empty()) {
                std::fprintf(
                    stderr,
                    "milo_convert_test: missing semantic field schema "
                    "%s/%u\n",
                    type.c_str(), revision);
                return 1;
            }
            const std::set<std::string> expected_fields(
                schema.begin(), schema.end());
            std::set<std::string> contracted_fields;
            for (const auto& row : rows) {
                if (row.source_field != "<synthesized>")
                    contracted_fields.insert(row.source_field);
            }
            if (contracted_fields != expected_fields) {
                std::string detail;
                for (const auto& field : expected_fields) {
                    if (contracted_fields.find(field) ==
                        contracted_fields.end()) {
                        detail = "missing " + field;
                        break;
                    }
                }
                if (detail.empty()) {
                    for (const auto& field : contracted_fields) {
                        if (expected_fields.find(field) ==
                            expected_fields.end()) {
                            detail = "extra " + field;
                            break;
                        }
                    }
                }
                std::fprintf(
                    stderr,
                    "milo_convert_test: semantic field schema mismatch "
                    "%s/%u: %s\n",
                    type.c_str(), revision, detail.c_str());
                return 1;
            }
        }
        const auto seq_field_contracts =
            gh::milo_convert::
                gh1_to_gh2_semantic_field_contracts_for(
                    "DTB", 779);
        const auto seq_field_schema =
            gh::milo_convert::
                gh1_serialized_semantic_fields_for(
                    "DTB", 779);
        if (seq_field_contracts.empty() ||
            seq_field_schema.empty() ||
            std::any_of(
                seq_field_contracts.begin(),
                seq_field_contracts.end(),
                [](const auto& row) {
                    return row.source_revision != 779;
                })) {
            std::fprintf(
                stderr,
                "milo_convert_test: DTB/SEQ control-word fallback "
                "mismatch\n");
            return 1;
        }

        const auto track_surface_source =
            gh::dtb::serialize(
                gh::dtb::parse_dta(
                    "(metal "
                    "(track_surface \"track/surfaces/metal.bmp\"))\n"
                    "(hair_metal "
                    "(track_surface \"track/surfaces/hair.bmp\"))\n"));
        const auto track_surfaces =
            gh::milo_convert::
                compile_gh1_character_track_surfaces(
                    track_surface_source);
        if (track_surfaces.size() != 2 ||
            track_surfaces[0].authored_name != "metal" ||
            track_surfaces[0].source_surface !=
                "track/surfaces/metal.bmp" ||
            track_surfaces[1].authored_name != "hair_metal" ||
            track_surfaces[1].source_surface !=
                "track/surfaces/hair.bmp") {
            std::fprintf(
                stderr,
                "milo_convert_test: GH1 character track surface "
                "manifest mismatch\n");
            return 1;
        }

        auto venue_source_tree =
            gh::dtb::parse_dta(
                    "{arena load_section lighting lighting}\n"
                    "{set $reactorState FALSE}\n"
                    "{func pulse ($target) "
                    "{arena switch_anim $target "
                    "(loop 0 480) (scale 2) (blend 240)}}\n"
                    "{func speakers ($targets) "
                    "{foreach $target $targets "
                    "{arena switch_anim_rt $target "
                    "(range {random_int 1 2} 0) "
                    "(scale 1) (blend 100)}}}\n"
                    "{arena add_handlers "
                    "(intro "
                    "{set $reactorState TRUE} "
                    "{pulse light.anim} "
                    "{speakers (a.anim b.anim)} "
                    "{speakers {switch $slot "
                    "(0 (c.anim d.anim)) (1 (e.anim))}} "
                    "{game anim_task kick.tnm 500 0 20} "
                    "{animate_to arena door.tnm 10 250} "
                    "{arena delay_task 240 "
                    "{light.mesh set_showing TRUE}}) "
                    "(state {arena switch_anim fan.view "
                    "(loop 0 100) (scale 1)}) "
                    "(state {arena switch_anim fan.view "
                    "(loop 0 100) (scale 1)} "
                    "{arena switch_anim fan.view "
                    "(scale 0.5) (blend 0)})}\n");
        venue_source_tree.trailing_bytes = {0xA5, 0x5A};
        const auto venue_source =
            gh::dtb::serialize(venue_source_tree);
        const auto venue_target =
            gh::milo_convert::
                convert_gh1_venue_script_to_gh2_worlddir(
                    venue_source, "arena");
        if (venue_target.source_roots != 5 ||
            venue_target.recognized_roots != 5 ||
            venue_target.unrecognized_roots != 0 ||
            venue_target.loaded_sections.size() != 1 ||
            venue_target.loaded_sections.front() !=
                std::make_pair(
                    std::string("lighting"),
                    std::string("lighting")) ||
            venue_target.initialized_states !=
                std::vector<std::string>{"reactorState"} ||
            venue_target.source_functions != 2 ||
            venue_target.handlers != 2 ||
            venue_target.function_calls_inlined != 3 ||
            venue_target.foreach_loops_unrolled != 2 ||
            venue_target.switch_anim_calls != 3 ||
            venue_target.switch_anim_rt_calls != 5 ||
            venue_target.anim_task_calls != 1 ||
            venue_target.animate_to_calls != 1 ||
            venue_target.delay_task_calls != 1 ||
            venue_target.random_ranges_expanded != 5 ||
            venue_target.stateful_ranges_resolved != 1 ||
            venue_target.dta.find("WorldDir") ==
                std::string::npos ||
            venue_target.dta.find("kTaskBeats") ==
                std::string::npos ||
            venue_target.dta.find("kTaskSeconds") ==
                std::string::npos ||
            venue_target.dta.find("c.anim") ==
                std::string::npos ||
            venue_target.dta.find("e.anim") ==
                std::string::npos ||
            venue_target.dta.find("(reactorState FALSE)") ==
                std::string::npos ||
            venue_target.dta.find("[reactorState]") ==
                std::string::npos ||
            venue_target.dta.find("switch_anim") !=
                std::string::npos ||
            venue_target.dta.find("delay_task") !=
                std::string::npos ||
             gh::dtb::serialize(
                 gh::dtb::parse(venue_target.bytes)) !=
                venue_target.bytes ||
            gh::dtb::parse(venue_target.bytes).trailing_bytes !=
                venue_source_tree.trailing_bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: venue script conversion mismatch\n");
            return 1;
        }
        gh::milo::Directory camera_main;
        gh::milo::Directory camera_paths;
        gh::milo_object::TransAnim6 camera_path;
        camera_path.translation_keys = {
            {{0.0f, 10.0f, 20.0f}, 0.0f},
            {{100.0f, 30.0f, 40.0f}, 100.0f},
        };
        camera_path.keys_owner = "Cam_test.tnm";
        gh::milo::Entry camera_path_entry;
        camera_path_entry.type = "TransAnim";
        camera_path_entry.name = "Cam_test.tnm";
        camera_path_entry.body_bytes =
            gh::milo_object::serialize_trans_anim6(camera_path);
        camera_path_entry.size = camera_path_entry.body_bytes.size();
        camera_path_entry.terminator_value = 0xDEADDEADu;
        camera_paths.entries.push_back(std::move(camera_path_entry));
        gh::milo_object::TransAnim6 shake_path;
        shake_path.translation_keys = {
            {{0.0f, 0.0f, 0.0f}, 0.0f},
            {{0.0f, 0.0f, 0.0f}, 3200.0f},
        };
        shake_path.keys_owner = "shaky_cam1.tnm";
        const std::map<std::string, gh::milo_object::TransAnim6>
            shared_camera_animations = {
                {"shaky_cam1.tnm", shake_path},
            };
        const auto camera_source =
            gh::dtb::serialize(
                gh::dtb::parse_dta(
                    "{set $camedit.TEST "
                    "({arena switch_cam Cam_test test01 "
                    "(start 0) (end 100) (duration 10000) "
                    "(singer_in 0 0) (singer_out 0.5 0.4) "
                    "(offset_in 1 2 3) (offset_out 4 5 6) "
                    "(near 10) (far 10000) "
                    "(fov_in 45) (fov_out 60) "
                    "(crowd_region 3) (shaky 1) "
                    "(bad_walk_spots (0 2)) "
                    "(enable_dof 1) (hide_crowd 1) "
                    "(real_time 1) (ease 0)})}\n"));
        const auto camera_path_policy = gh::dtb::serialize(gh::dtb::parse_dta(
            "(Cam_test (parent \"arena::venue.view\"))"));
        const auto camera_target =
            gh::milo_convert::
                convert_gh1_venue_cameras_to_gh2_camshots(
                    camera_source, camera_path_policy, camera_main, camera_paths,
                    shared_camera_animations);
        if (camera_target.records != 1 ||
            camera_target.main_directory.entries.size() != 1 ||
            camera_target.main_directory.entries[0].type != "CamShot") {
            std::fprintf(
                stderr,
                "milo_convert_test: venue camera inventory mismatch\n");
            return 1;
        }
        const auto parsed_camera =
            gh::milo_object::parse_cam_shot20(
                camera_target.main_directory.entries[0].body_bytes);
        const auto& camera_properties =
            parsed_camera.object_fields.type_properties;
        const auto bad_waypoints = std::find_if(
            camera_properties.begin(), camera_properties.end(),
            [](const gh::milo_object::TypePropertyNode& property) {
                return property.type == 0x05 &&
                       property.symbol == "bad_waypoints";
            });
        float camera_duration = 0;
        bool retained_shake_boundary = false;
        for (const auto& frame : parsed_camera.keyframes) {
            camera_duration += frame.duration + frame.blend;
            if (std::fabs(camera_duration - 96.0f) < 0.0001f) retained_shake_boundary = true;
        }
        if (parsed_camera.keyframes.size() < 3 ||
            parsed_camera.category != "TEST" ||
            parsed_camera.legacy_category_frame != 1.0f ||
            parsed_camera.path != "" ||
            parsed_camera.animatable.rate != 0 ||
            std::fabs(camera_duration - 300.0f) > 0.001f || !retained_shake_boundary ||
            parsed_camera.keyframes[0].world_offset[9] != 1.0f ||
            parsed_camera.keyframes.back().world_offset[9] != 104.0f ||
            parsed_camera.keyframes.back().screen_offset[0] != 0.5f ||
            parsed_camera.keyframes.back().screen_offset[1] != 0.4f ||
            parsed_camera.keyframes[0].targets.size() != 1 ||
            parsed_camera.keyframes[0].targets[0].object != "guitarist0" ||
            parsed_camera.keyframes[0].targets[0].part !=
                "bone_head.mesh" ||
            parsed_camera.keyframes[0].parent.object != "arena" ||
            parsed_camera.keyframes[0].parent.part != "venue.view" ||
            !parsed_camera.keyframes[0].use_parent_rotation ||
            !parsed_camera.use_depth_of_field ||
            camera_properties.size() < 22 ||
            bad_waypoints == camera_properties.end() ||
            std::next(bad_waypoints) == camera_properties.end() ||
            std::next(bad_waypoints)->type != 0x10 ||
            std::next(bad_waypoints)->children.size() != 2 ||
            std::next(bad_waypoints)->children[0].symbol !=
                "gh1_walk_spot_0" ||
            std::next(bad_waypoints)->children[1].symbol !=
                "gh1_walk_spot_2") {
            std::fprintf(
                stderr,
                "milo_convert_test: venue camera conversion mismatch\n");
            return 1;
        }
        // GH1 16E804/24B608: Multiply(shake, path), row-vector convention.
        // Different rotation axes make reversed multiplication observable.
        for (bool rotating_path : {false, true}) {
            auto composed_paths = camera_paths;
            auto path_with_rotation = camera_path;
            const float quarter = std::sqrt(0.5f);
            if (rotating_path)
                path_with_rotation.rotation_keys = {{{0, 0, quarter, quarter}, 0}};
            composed_paths.entries[0].body_bytes =
                gh::milo_object::serialize_trans_anim6(path_with_rotation);
            composed_paths.entries[0].size = composed_paths.entries[0].body_bytes.size();
            auto shake_with_rotation = shake_path;
            shake_with_rotation.translation_keys = {{{2, 3, 4}, 0}};
            shake_with_rotation.rotation_keys = {{{quarter, 0, 0, quarter}, 0}};
            const auto composed = gh::milo_convert::convert_gh1_venue_cameras_to_gh2_camshots(
                camera_source, camera_path_policy, camera_main, composed_paths,
                {{"shaky_cam1.tnm", shake_with_rotation}});
            const auto shot = gh::milo_object::parse_cam_shot20(
                composed.main_directory.entries[0].body_bytes);
            const std::array<float, 12> expected = rotating_path
                ? std::array<float, 12>{0,-1,0, 0,0,-1, 1,0,0, 4,10,27}
                : std::array<float, 12>{1,0,0, 0,0,-1, 0,1,0, 3,15,27};
            for (size_t component = 0; component < expected.size(); ++component) {
                if (std::fabs(shot.keyframes[0].world_offset[component] - expected[component]) > 0.0001f) {
                    std::fprintf(stderr,
                        "milo_convert_test: GH1 shake/path composition mismatch (%d, %zu)\n",
                        rotating_path, component);
                    return 1;
                }
            }
        }
        const float helper_filter = gh::milo_convert::gh1_camera_helper_filter_from_game_config(
            gh::dtb::serialize(gh::dtb::parse_dta("(arena (cam_filter 0.3))")));
        const auto helper_result = gh::milo_convert::convert_gh1_venue_cameras_to_gh2_camshots(
            camera_source, camera_path_policy, camera_main, camera_paths, shared_camera_animations,
            helper_filter);
        const auto helper_shot = gh::milo_object::parse_cam_shot20(helper_result.main_directory.entries[0].body_bytes);
        const auto& helper_properties = helper_shot.object_fields.type_properties;
        const auto find_helper_property = [&](const char* name) -> const gh::milo_object::TypePropertyNode& {
            for (size_t i = 0; i+1 < helper_properties.size(); i += 2)
                if (helper_properties[i].symbol == name) return helper_properties[i+1];
            throw std::runtime_error("missing helper property");
        };
        if (std::fabs(helper_filter-0.3f) > 0.00001f || helper_shot.filter != 0 ||
            find_helper_property("gh1_helper_filter").floating != helper_filter ||
            find_helper_property("gh1_helper_target").integer != 1 ||
            find_helper_property("gh1_helper_parent").integer != 0 ||
            find_helper_property("gh1_helper_shake_keys").children.size() != helper_shot.keyframes.size()) {
            std::fprintf(stderr, "milo_convert_test: GH1 helper metadata/config mismatch\n");
            return 1;
        }
        bool missing_config_rejected = false;
        try { (void)gh::milo_convert::gh1_camera_helper_filter_from_game_config(
            gh::dtb::serialize(gh::dtb::parse_dta("(arena (other 1))"))); }
        catch (const std::exception&) { missing_config_rejected = true; }
        if (!missing_config_rejected) return 1;
        const auto policy_camera = [&](const std::string& policy) {
            const auto result = gh::milo_convert::convert_gh1_venue_cameras_to_gh2_camshots(
                camera_source, gh::dtb::serialize(gh::dtb::parse_dta(policy)),
                camera_main, camera_paths, shared_camera_animations);
            return gh::milo_object::parse_cam_shot20(result.main_directory.entries[0].body_bytes);
        };
        const auto default_policy_camera = policy_camera("(Cam_test)");
        const auto stage_policy_camera = policy_camera(
            "(Cam_test (target \"arena::stage_spot_01.mesh\") (parent \"arena::venue.view\"))");
        const auto singer_policy_camera = policy_camera(
            "(Cam_test (target \"singer::bone_neck.mesh\") (parent \"arena::venue.view\"))");
        const auto conditional_policy_camera = policy_camera(
            "(Cam_test (target {if_else {exists \"guitarist0::spot_neck_fret20.mesh\"} "
            "\"guitarist0::spot_neck_fret20.mesh\" \"arena::stage_spot_01.mesh\"}) "
            "(parent {if_else {exists \"guitarist0::spot_neck_fret01.mesh\"} "
            "\"guitarist0::spot_neck_fret01.mesh\" \"arena::stage_spot_01.mesh\"}))");
        const auto& default_frame = default_policy_camera.keyframes[0];
        const auto& stage_frame = stage_policy_camera.keyframes[0];
        const auto& conditional_frame = conditional_policy_camera.keyframes[0];
        size_t fallback_properties = 0;
        for (const auto& property : conditional_policy_camera.object_fields.type_properties)
            if (property.symbol == "gh1_camera_target_fallback" ||
                property.symbol == "gh1_camera_parent_fallback") ++fallback_properties;
        if (default_frame.parent.object != "guitarist0" ||
            default_frame.parent.part != "bone_head.mesh" || default_frame.use_parent_rotation ||
            stage_frame.targets[0].object != "arena" ||
            stage_frame.targets[0].part != "stage_spot_01.mesh" ||
            stage_frame.parent.part != "venue.view" || !stage_frame.use_parent_rotation ||
            singer_policy_camera.keyframes[0].targets[0].object != "singer" ||
            singer_policy_camera.keyframes[0].targets[0].part != "bone_neck.mesh" ||
            conditional_frame.targets[0].part != "spot_neck_fret20.mesh" ||
            conditional_frame.parent.part != "spot_neck_fret01.mesh" ||
            !conditional_frame.use_parent_rotation || fallback_properties != 2) {
            std::fprintf(stderr, "milo_convert_test: shared camera path policy mismatch\n");
            return 1;
        }
        for (const auto& invalid_policy : {
                 "(Unknown_path)",
                 "(Cam_test (parent {some_unknown_command}))"}) {
            bool rejected = false;
            try { (void)policy_camera(invalid_policy); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) {
                std::fprintf(stderr, "milo_convert_test: unsupported camera policy accepted\n");
                return 1;
            }
        }
        const auto migrated = gh::milo_convert::rebind_gh1_venue_camera_paths(
            gh::dtb::serialize(gh::dtb::parse_dta(
                "(Cam_test (target \"arena::stage_spot_01.mesh\") (parent \"arena::venue.view\"))")),
            camera_target.main_directory);
        const auto migrated_again = gh::milo_convert::rebind_gh1_venue_camera_paths(
            gh::dtb::serialize(gh::dtb::parse_dta(
                "(Cam_test (target \"arena::stage_spot_01.mesh\") (parent \"arena::venue.view\"))")),
            migrated.main_directory);
        const auto migrated_shot = gh::milo_object::parse_cam_shot20(
            migrated.main_directory.entries[0].body_bytes);
        // Retail GH1 45-degree source camera: mxx=2.4142137, mzx=-3.2189515.
        // GH2 takes vertical FOV, so passing 45 degrees unchanged is wrong.
        const float converted_45 = parsed_camera.keyframes.front().field_of_view;
        if (std::fabs(0.75f / std::tan(converted_45 * 0.5f) - 2.4142137f) > 0.00001f ||
            std::fabs(-1.0f / std::tan(converted_45 * 0.5f) + 3.2189515f) > 0.00001f) {
            std::fprintf(stderr, "milo_convert_test: GH1/GH2 projection scale mismatch\n");
            return 1;
        }
        auto old_directory = camera_target.main_directory;
        auto old_shot = parsed_camera;
        auto& old_props = old_shot.object_fields.type_properties;
        for (size_t i = 0; i + 1 < old_props.size(); i += 2) {
            if (old_props[i].symbol == "gh1_fov_space") {
                old_props.erase(old_props.begin() + i, old_props.begin() + i + 2);
                break;
            }
        }
        for (auto& frame : old_shot.keyframes) frame.field_of_view = 0.785398163f;
        old_directory.entries[0].body_bytes = gh::milo_object::serialize_cam_shot20(old_shot);
        const auto upgraded = gh::milo_convert::rebind_gh1_venue_camera_paths(camera_path_policy, old_directory);
        const auto upgraded_again = gh::milo_convert::rebind_gh1_venue_camera_paths(camera_path_policy, upgraded.main_directory);
        const auto upgraded_shot = gh::milo_object::parse_cam_shot20(upgraded.main_directory.entries[0].body_bytes);
        if (std::fabs(upgraded_shot.keyframes.front().field_of_view - converted_45) > 0.000001f ||
            upgraded.main_directory.entries[0].body_bytes != upgraded_again.main_directory.entries[0].body_bytes ||
            upgraded_shot.keyframes.front().world_offset != old_shot.keyframes.front().world_offset ||
            upgraded_shot.keyframes.front().blend != old_shot.keyframes.front().blend) {
            std::fprintf(stderr, "milo_convert_test: old camera FOV upgrade incorrect or applied twice\n");
            return 1;
        }
        if (migrated.main_directory.entries[0].body_bytes !=
                migrated_again.main_directory.entries[0].body_bytes ||
            migrated_shot.keyframes.back().world_offset != parsed_camera.keyframes.back().world_offset ||
            migrated_shot.keyframes.back().screen_offset != parsed_camera.keyframes.back().screen_offset ||
            migrated_shot.keyframes.back().targets[0].part != "stage_spot_01.mesh") {
            std::fprintf(stderr, "milo_convert_test: camera policy migration changed curve or was not idempotent\n");
            return 1;
        }
        const auto fixed_path_camera_source =
            gh::dtb::serialize(
                gh::dtb::parse_dta(
                    "{set $camedit.TEST "
                    "({arena switch_cam Cam_test fixed01 "
                    "(start 0) (end 0) (duration 5000) "
                    "(singer_in 0 0) (singer_out -0.5 0.4) "
                    "(offset_in 1 2 3) (offset_out 4 5 6) "
                    "(near 10) (far 10000) "
                    "(fov_in 45) (fov_out 60)})}\n"));
        const auto fixed_path_camera_target =
            gh::milo_convert::
                convert_gh1_venue_cameras_to_gh2_camshots(
                    fixed_path_camera_source, camera_path_policy, camera_main, camera_paths,
                    shared_camera_animations);
        const auto fixed_path_camera =
            gh::milo_object::parse_cam_shot20(
                fixed_path_camera_target.main_directory.entries[0]
                    .body_bytes);
        if (fixed_path_camera_target.records != 1 ||
            fixed_path_camera.keyframes.size() != 2 ||
            fixed_path_camera.keyframes.front().world_offset[9] != 4.0f ||
            fixed_path_camera.keyframes.back().world_offset[9] != 4.0f ||
            fixed_path_camera.keyframes.front().screen_offset[0] != -0.5f ||
            fixed_path_camera.keyframes.back().screen_offset[0] != -0.5f ||
            fixed_path_camera.keyframes.front().screen_offset[1] != 0.4f ||
            fixed_path_camera.keyframes.back().screen_offset[1] != 0.4f ||
            std::fabs(fixed_path_camera.keyframes.front().field_of_view - 0.81727571f) > 0.000001f ||
            std::fabs(fixed_path_camera.keyframes.back().field_of_view - 0.81727571f) > 0.000001f) {
            std::fprintf(
                stderr,
                "milo_convert_test: fixed-path VenueCam endpoint mismatch\n");
            return 1;
        }
        const auto clean_rnd_objects =
            gh::dtb::serialize(
                gh::dtb::parse_dta("(Group (types))\n"));
        const auto face_config =
            gh::milo_convert::
                patch_gh2_rnd_objects_for_gh1_faces(
                    clean_rnd_objects);
        if (face_config.types_added != 2 ||
            face_config.dta.find(
                "gh1_guitarist_morph_face") ==
                std::string::npos ||
            face_config.dta.find(
                "gh1_singer_morph_face") ==
                std::string::npos ||
            face_config.dta.find(
                "gh1_face_0_9.trig") ==
                std::string::npos ||
            face_config.dta.find(
                "0.5 - 0.5") != std::string::npos ||
            gh::dtb::serialize(
                gh::dtb::parse(face_config.bytes)) !=
                face_config.bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: face config patch mismatch\n");
            return 1;
        }
        const auto clean_midi_parsers =
            gh::dtb::serialize(
                gh::dtb::parse_dta(
                    "(singer_parser (type echo) "
                    "(track_name BAND SINGER))\n"));
        const auto face_midi_config =
            gh::milo_convert::
                patch_gh2_midi_parsers_for_gh1_singer_face(
                    clean_midi_parsers);
        if (face_midi_config.parsers_added != 1 ||
            face_midi_config.dta.find(
                "gh1_singer_face_parser") ==
                std::string::npos ||
            face_midi_config.dta.find(
                "singer_face_open") ==
                std::string::npos ||
            face_midi_config.dta.find(
                "singer_face_close") ==
                std::string::npos ||
            face_midi_config.dta.find(
                "{singer_parser singer_face_open}") ==
                std::string::npos ||
            face_midi_config.dta.find(
                "{singer_parser singer_face_close}") ==
                std::string::npos ||
            gh::dtb::serialize(
                gh::dtb::parse(face_midi_config.bytes)) !=
                face_midi_config.bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: face MIDI config patch mismatch\n");
            return 1;
        }
        const auto clean_char_objects =
            gh::dtb::serialize(
                gh::dtb::parse_dta(
                    "(Character "
                    "(types (singer (parser singer_parser))))\n"));
        const auto character_face_config =
            gh::milo_convert::
                patch_gh2_char_objects_for_gh1_singer_face(
                    clean_char_objects);
        if (character_face_config.handlers_added != 2 ||
            character_face_config.dta.find(
                "singer_face_open") ==
                std::string::npos ||
            character_face_config.dta.find(
                "singer_face_close") ==
                std::string::npos ||
            character_face_config.dta.find(
                "lip.servo") ==
                std::string::npos ||
            gh::dtb::serialize(
                gh::dtb::parse(character_face_config.bytes)) !=
                character_face_config.bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: character face config patch "
                "mismatch\n");
            return 1;
        }
        const std::vector<uint8_t> test_midi = {
            'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,0xE0,
            'M','T','r','k', 0,0,0,0x14,
            0,0xFF,3,5,'T','E','M','P','O',
            0,0xFF,0x51,3,0x07,0xA1,0x20,
            0,0xFF,0x2F,0,
            'M','T','r','k', 0,0,0,0x1D,
            0,0xFF,3,0x0B,
            'P','A','R','T',' ','G','U','I','T','A','R',
            0x81,0x70,0x90,108,100,
            0x81,0x70,0x80,108,0,
            0,0xFF,0x2F,0};
        const auto source_face_spans =
            gh::milo_convert::extract_gh1_singer_face_spans(
                test_midi);
        const auto patched_midi =
            gh::milo_convert::append_gh1_singer_face_track(
                test_midi, source_face_spans);
        if (source_face_spans.size() != 1 ||
            source_face_spans[0].tick_on != 240 ||
            source_face_spans[0].tick_off != 480 ||
            patched_midi.size() <= test_midi.size() ||
            patched_midi[10] != 0 || patched_midi[11] != 3 ||
            !std::equal(
                test_midi.begin() + 12, test_midi.end(),
                patched_midi.begin() + 12)) {
            std::fprintf(
                stderr,
                "milo_convert_test: singer face MIDI mismatch\n");
            return 1;
        }
        bool duplicate_face_track_rejected = false;
        try {
            (void)gh::milo_convert::append_gh1_singer_face_track(
                patched_midi, source_face_spans);
        } catch (const std::runtime_error&) {
            duplicate_face_track_rejected = true;
        }
        if (!duplicate_face_track_rejected) {
            std::fprintf(
                stderr,
                "milo_convert_test: duplicate singer face track "
                "accepted\n");
            return 1;
        }

        std::vector<uint8_t> test_voc = {'F','A','C','E'};
        const auto append_le16 =
            [&test_voc](uint16_t value) {
                test_voc.push_back(
                    static_cast<uint8_t>(value));
                test_voc.push_back(
                    static_cast<uint8_t>(value >> 8));
            };
        const auto append_le32 =
            [&test_voc](uint32_t value) {
                for (int shift = 0; shift < 32; shift += 8)
                    test_voc.push_back(
                        static_cast<uint8_t>(value >> shift));
            };
        const auto append_float =
            [&append_le32](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                append_le32(bits);
            };
        const auto append_face_string =
            [&test_voc, &append_le16, &append_le32](
                const char* value) {
                const size_t size = std::strlen(value);
                append_le16(1);
                append_le32(static_cast<uint32_t>(size));
                test_voc.insert(
                    test_voc.end(), value, value + size);
            };
        append_le32(1500);
        append_face_string("test");
        append_face_string("");
        append_le32(1000);
        append_le32(0);
        append_le16(0);
        append_face_string("test_song");
        append_le16(3);
        const size_t voc_size_offset = test_voc.size();
        append_le32(0);
        append_le16(0);
        append_le32(1);
        append_le32(0);
        append_le16(0);
        append_face_string("Eat");
        append_le32(0);
        append_le32(0);
        append_le32(3);
        for (const auto key :
             {std::pair<float, float>{0.25f, 0.0f},
              {0.50f, 1.0f}, {0.75f, 0.0f}}) {
            append_le16(0);
            append_float(key.first);
            append_float(key.second);
            append_float(0.0f);
            append_le32(0);
        }
        test_voc.insert(test_voc.end(), 36, 0);
        const uint32_t voc_size =
            static_cast<uint32_t>(test_voc.size());
        for (int index = 0; index < 4; ++index)
            test_voc[voc_size_offset + index] =
                static_cast<uint8_t>(
                    voc_size >> (index * 8));
        const auto parsed_voc =
            gh::milo_convert::parse_gh2_facefx_animation(
                test_voc);
        const auto time_spans =
            gh::milo_convert::derive_gh1_singer_open_spans(
                parsed_voc);
        const auto mapped_spans =
            gh::milo_convert::map_singer_face_times_to_midi(
                test_midi, time_spans);
        if (parsed_voc.version != 1500 ||
            parsed_voc.name != "test_song" ||
            time_spans.size() != 1 ||
            std::abs(time_spans[0].time_on - 0.25f) >
                0.000001f ||
            std::abs(time_spans[0].time_off - 0.75f) >
                0.000001f ||
            mapped_spans.size() != 1 ||
            mapped_spans[0].tick_on != 240 ||
            mapped_spans[0].tick_off != 720) {
            std::fprintf(
                stderr,
                "milo_convert_test: FaceFX singer span mismatch\n");
            return 1;
        }

        gh::milo::Directory source;
        source.dir_version = 10;
        source.boundaries_exact = true;

        gh::milo_object::Tex texture;
        texture.width = 4;
        texture.height = 4;
        texture.bits_per_pixel = 32;
        gh::milo::Entry texture_entry;
        texture_entry.type = "Tex";
        texture_entry.name = "stage.tex";
        texture_entry.body_bytes =
            gh::milo_object::serialize_tex(texture);
        texture_entry.terminator_value = 0xDEADDEADu;
        source.entries.push_back(texture_entry);

        gh::milo_object::Mat material;
        gh::milo_object::MatTexture stage0;
        stage0.stage_blend = 2;
        stage0.texture = "stage.tex";
        gh::milo_object::MatTexture stage1 = stage0;
        stage1.stage_blend = 3;
        material.textures = {stage0, stage1};
        gh::milo::Entry material_entry;
        material_entry.type = "Mat";
        material_entry.name = "stage.mat";
        material_entry.body_bytes =
            gh::milo_object::serialize_mat(material);
        material_entry.terminator_value = 0xDEADDEADu;
        source.entries.push_back(material_entry);

        gh::milo_object::View view;
        view.transformable.revision = 8;
        view.drawable.revision = 1;
        view.children_owner = "stage.view";
        gh::milo::Entry view_entry;
        view_entry.type = "View";
        view_entry.name = "stage.view";
        view_entry.body_bytes =
            gh::milo_object::serialize_view(view);
        view_entry.terminator_value = 0xDEADDEADu;
        source.entries.push_back(view_entry);

        const auto result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                source, "stage");
        if (!result.complete ||
            result.directory.dir_version != 24 ||
            result.directory.dir_type != "RndDir" ||
            result.directory.entries.size() != 4 ||
            result.directory.entries[0].type != "Tex" ||
            result.directory.entries[1].name != "stage.mat" ||
            result.directory.entries[2].name != "stage_1.mat" ||
            result.directory.entries[3].type != "Group") {
            std::fprintf(stderr, "milo_convert_test: conversion mismatch\n");
            return 1;
        }
        const auto bytes = gh::milo::serialize_directory(result.directory);
        const auto parsed = gh::milo::parse_directory(bytes);
        if (!parsed.boundaries_exact ||
            gh::milo::serialize_directory(parsed) != bytes) {
            std::fprintf(stderr, "milo_convert_test: round trip mismatch\n");
            return 1;
        }

        gh::milo::Directory environment_scope_source;
        environment_scope_source.dir_version = 10;
        environment_scope_source.boundaries_exact = true;
        auto add_environment_scope_entry =
            [&](const std::string& type, const std::string& name,
                std::vector<uint8_t> body) {
                gh::milo::Entry entry;
                entry.type = type;
                entry.name = name;
                entry.body_bytes = std::move(body);
                entry.terminator_value = 0xDEADDEADu;
                environment_scope_source.entries.push_back(
                    std::move(entry));
            };
        gh::milo_object::Environ base_environment;
        base_environment.legacy_drawable.revision = 1;
        add_environment_scope_entry(
            "Environ", "base.env",
            gh::milo_object::serialize_environ(base_environment));
        gh::milo_object::Environ smoke_environment;
        smoke_environment.legacy_drawable.revision = 1;
        add_environment_scope_entry(
            "Environ", "smoke.env",
            gh::milo_object::serialize_environ(smoke_environment));
        gh::milo_object::Mesh rig_mesh;
        rig_mesh.drawable.revision = 1;
        add_environment_scope_entry(
            "Mesh", "rig.mesh",
            gh::milo_object::serialize_mesh(rig_mesh));
        gh::milo_object::Mesh smoke_mesh;
        smoke_mesh.drawable.revision = 1;
        add_environment_scope_entry(
            "Mesh", "smoke.mesh",
            gh::milo_object::serialize_mesh(smoke_mesh));
        gh::milo_object::View environment_scope_view;
        environment_scope_view.drawable.revision = 1;
        environment_scope_view.drawable.objects = {
            "base.env", "rig.mesh", "smoke.env", "smoke.mesh"};
        environment_scope_view.children_owner = "scope.view";
        add_environment_scope_entry(
            "View", "scope.view",
            gh::milo_object::serialize_view(environment_scope_view));
        const auto environment_scope_result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                environment_scope_source, "scope", "scope.view");
        const auto find_environment_scope_entry =
            [&](const std::string& name) {
                return std::find_if(
                    environment_scope_result.directory.entries.begin(),
                    environment_scope_result.directory.entries.end(),
                    [&](const gh::milo::Entry& entry) {
                        return entry.name == name;
                    });
            };
        const auto environment_scope_root_entry =
            find_environment_scope_entry("scope.view");
        const auto base_scope_entry =
            find_environment_scope_entry(
                "scope.view.__environment_0.grp");
        const auto smoke_scope_entry =
            find_environment_scope_entry(
                "scope.view.__environment_1.grp");
        const auto environment_draw_only_entry =
            find_environment_scope_entry(
                "scope.view.__draw_only.grp");
        if (environment_scope_root_entry ==
                environment_scope_result.directory.entries.end() ||
            base_scope_entry ==
                environment_scope_result.directory.entries.end() ||
            smoke_scope_entry ==
                environment_scope_result.directory.entries.end() ||
            environment_draw_only_entry ==
                environment_scope_result.directory.entries.end()) {
            std::fprintf(
                stderr,
                "milo_convert_test: View environment scope Group missing\n");
            return 1;
        }
        const auto environment_scope_root =
            gh::milo_object::parse_group12(
                environment_scope_root_entry->body_bytes);
        const auto base_scope =
            gh::milo_object::parse_group12(
                base_scope_entry->body_bytes);
        const auto smoke_scope =
            gh::milo_object::parse_group12(
                smoke_scope_entry->body_bytes);
        const auto environment_draw_only =
            gh::milo_object::parse_group12(
                environment_draw_only_entry->body_bytes);
        if (!environment_scope_result.complete ||
            environment_scope_root.revision != 13 ||
            !environment_scope_root.environment.empty() ||
            environment_scope_root.draw_only !=
                "scope.view.__draw_only.grp" ||
            environment_scope_root.objects.size() != 2 ||
            environment_scope_root.objects[0] !=
                "scope.view.__environment_0.grp" ||
            environment_scope_root.objects[1] !=
                "scope.view.__environment_1.grp" ||
            base_scope.environment != "base.env" ||
            base_scope.objects.size() != 1 ||
            base_scope.objects[0] != "rig.mesh" ||
            smoke_scope.environment != "smoke.env" ||
            smoke_scope.objects.size() != 1 ||
            smoke_scope.objects[0] != "smoke.mesh" ||
            environment_draw_only.objects !=
                environment_scope_root.objects) {
            std::fprintf(
                stderr,
                "milo_convert_test: ordered View environment scope mismatch\n");
            return 1;
        }

        gh::milo::Directory drawable_graph_source;
        drawable_graph_source.dir_version = 10;
        drawable_graph_source.boundaries_exact = true;
        gh::milo_object::Mesh drawable_parent;
        drawable_parent.drawable.revision = 1;
        drawable_parent.drawable.objects = {"cable.mesh"};
        drawable_parent.transformable.parent = "record.mesh";
        gh::milo::Entry drawable_parent_entry;
        drawable_parent_entry.type = "Mesh";
        drawable_parent_entry.name = "record.mesh";
        drawable_parent_entry.body_bytes =
            gh::milo_object::serialize_mesh(drawable_parent);
        drawable_parent_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(drawable_parent_entry));
        gh::milo_object::Mesh drawable_child;
        drawable_child.drawable.revision = 1;
        drawable_child.transformable.parent = "record.mesh";
        gh::milo::Entry drawable_child_entry;
        drawable_child_entry.type = "Mesh";
        drawable_child_entry.name = "cable.mesh";
        drawable_child_entry.body_bytes =
            gh::milo_object::serialize_mesh(drawable_child);
        drawable_child_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(drawable_child_entry));
        gh::milo_object::Mesh orphan_drawable;
        orphan_drawable.drawable.revision = 1;
        orphan_drawable.transformable.parent = "orphan.mesh";
        gh::milo::Entry orphan_drawable_entry;
        orphan_drawable_entry.type = "Mesh";
        orphan_drawable_entry.name = "orphan.mesh";
        orphan_drawable_entry.body_bytes =
            gh::milo_object::serialize_mesh(orphan_drawable);
        orphan_drawable_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(orphan_drawable_entry));
        gh::milo_object::Mesh nested_drawable;
        nested_drawable.drawable.revision = 1;
        nested_drawable.transformable.parent = "nested.mesh";
        gh::milo::Entry nested_drawable_entry;
        nested_drawable_entry.type = "Mesh";
        nested_drawable_entry.name = "nested.mesh";
        nested_drawable_entry.body_bytes =
            gh::milo_object::serialize_mesh(nested_drawable);
        nested_drawable_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(nested_drawable_entry));
        gh::milo_object::View nested_view;
        nested_view.drawable.revision = 1;
        nested_view.drawable.objects = {"nested.mesh"};
        nested_view.children_owner = "nested.view";
        nested_view.transformable.parent = "nested.view";
        gh::milo::Entry nested_view_entry;
        nested_view_entry.type = "View";
        nested_view_entry.name = "nested.view";
        nested_view_entry.body_bytes =
            gh::milo_object::serialize_view(nested_view);
        nested_view_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(nested_view_entry));
        gh::milo_object::View drawable_graph_view;
        drawable_graph_view.drawable.revision = 1;
        drawable_graph_view.drawable.objects = {
            "record.mesh", "nested.view"};
        drawable_graph_view.animatable.objects = {"orphan.mesh"};
        drawable_graph_view.children_owner = "records.view";
        drawable_graph_view.transformable.parent = "records.view";
        gh::milo::Entry drawable_graph_view_entry;
        drawable_graph_view_entry.type = "View";
        drawable_graph_view_entry.name = "records.view";
        drawable_graph_view_entry.body_bytes =
            gh::milo_object::serialize_view(drawable_graph_view);
        drawable_graph_view_entry.terminator_value = 0xDEADDEADu;
        drawable_graph_source.entries.push_back(
            std::move(drawable_graph_view_entry));
        const auto drawable_graph_result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                drawable_graph_source, "records", "records.view");
        const auto drawable_graph_group_entry =
            std::find_if(
                drawable_graph_result.directory.entries.begin(),
                drawable_graph_result.directory.entries.end(),
                [](const gh::milo::Entry& entry) {
                    return entry.name == "records.view";
                });
        const auto hidden_group_entry =
            std::find_if(
                drawable_graph_result.directory.entries.begin(),
                drawable_graph_result.directory.entries.end(),
                [](const gh::milo::Entry& entry) {
                    return entry.name ==
                           "__gh1_unreachable_drawables.grp";
                });
        const auto draw_only_group_entry =
            std::find_if(
                drawable_graph_result.directory.entries.begin(),
                drawable_graph_result.directory.entries.end(),
                [](const gh::milo::Entry& entry) {
                    return entry.name ==
                           "records.view.__draw_only.grp";
                });
        if (drawable_graph_group_entry ==
                drawable_graph_result.directory.entries.end() ||
            hidden_group_entry ==
                drawable_graph_result.directory.entries.end() ||
            draw_only_group_entry ==
                drawable_graph_result.directory.entries.end()) {
            std::fprintf(
                stderr,
                "milo_convert_test: drawable root Group missing\n");
            return 1;
        }
        const auto drawable_graph_group =
            gh::milo_object::parse_group12(
                drawable_graph_group_entry->body_bytes);
        const auto hidden_group =
            gh::milo_object::parse_group12(
                hidden_group_entry->body_bytes);
        const auto draw_only_group =
            gh::milo_object::parse_group12(
                draw_only_group_entry->body_bytes);
        if (!drawable_graph_result.complete ||
            drawable_graph_group.revision != 13 ||
            drawable_graph_group.draw_only !=
                "records.view.__draw_only.grp" ||
            drawable_graph_group.objects.size() != 4 ||
            drawable_graph_group.objects[0] != "orphan.mesh" ||
            drawable_graph_group.objects[1] != "record.mesh" ||
            drawable_graph_group.objects[2] != "cable.mesh" ||
            drawable_graph_group.objects[3] != "nested.view" ||
            draw_only_group.objects.size() != 3 ||
            draw_only_group.objects[0] != "record.mesh" ||
            draw_only_group.objects[1] != "cable.mesh" ||
            draw_only_group.objects[2] != "nested.view" ||
            draw_only_group.transformable.parent != "records.view" ||
            hidden_group.drawable.showing ||
            hidden_group.objects.size() != 1 ||
            hidden_group.objects[0] != "orphan.mesh") {
            std::fprintf(
                stderr,
                "milo_convert_test: legacy drawable root mismatch\n");
            return 1;
        }

        // View7 children_owner is not the source of a View's serialized
        // member lists. GH1 Theatre's verse.anim legitimately points at
        // chorus.anim there while retaining its own verselight EnvAnims.
        gh::milo::Directory distinct_owner_source;
        distinct_owner_source.dir_version = 10;
        distinct_owner_source.boundaries_exact = true;
        gh::milo_object::View owner_view;
        owner_view.animatable.objects = {"chorus.envanim"};
        owner_view.drawable.objects = {"chorus.mesh"};
        owner_view.children_owner = "chorus.anim";
        owner_view.transformable.parent = "chorus.anim";
        gh::milo::Entry owner_view_entry;
        owner_view_entry.type = "View";
        owner_view_entry.name = "chorus.anim";
        owner_view_entry.body_bytes =
            gh::milo_object::serialize_view(owner_view);
        owner_view_entry.terminator_value = 0xDEADDEADu;
        distinct_owner_source.entries.push_back(std::move(owner_view_entry));
        gh::milo_object::View member_view;
        member_view.animatable.objects = {"verse.envanim"};
        member_view.drawable.objects = {"verse.mesh"};
        member_view.children_owner = "chorus.anim";
        member_view.transformable.parent = "verse.anim";
        gh::milo::Entry member_view_entry;
        member_view_entry.type = "View";
        member_view_entry.name = "verse.anim";
        member_view_entry.body_bytes =
            gh::milo_object::serialize_view(member_view);
        member_view_entry.terminator_value = 0xDEADDEADu;
        distinct_owner_source.entries.push_back(std::move(member_view_entry));
        for (const char* name : {"chorus.mesh", "verse.mesh"}) {
            gh::milo_object::Mesh mesh;
            mesh.drawable.revision = 1;
            mesh.transformable.parent = name;
            gh::milo::Entry mesh_entry;
            mesh_entry.type = "Mesh";
            mesh_entry.name = name;
            mesh_entry.body_bytes = gh::milo_object::serialize_mesh(mesh);
            mesh_entry.terminator_value = 0xDEADDEADu;
            distinct_owner_source.entries.push_back(std::move(mesh_entry));
        }
        for (const char* name : {"chorus.envanim", "verse.envanim"}) {
            gh::milo_object::EnvAnim anim;
            anim.animatable.revision = 0;
            gh::milo::Entry anim_entry;
            anim_entry.type = "EnvAnim";
            anim_entry.name = name;
            anim_entry.body_bytes =
                gh::milo_object::serialize_env_anim(anim);
            anim_entry.terminator_value = 0xDEADDEADu;
            distinct_owner_source.entries.push_back(std::move(anim_entry));
        }
        const auto distinct_owner_result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                distinct_owner_source, "distinct_owner", "verse.anim");
        const auto distinct_member_entry = std::find_if(
            distinct_owner_result.directory.entries.begin(),
            distinct_owner_result.directory.entries.end(),
            [](const gh::milo::Entry& entry) {
                return entry.name == "verse.anim";
            });
        if (distinct_member_entry ==
            distinct_owner_result.directory.entries.end()) {
            std::fprintf(stderr,
                         "milo_convert_test: distinct-owner View missing\n");
            return 1;
        }
        const auto distinct_member = gh::milo_object::parse_group12(
            distinct_member_entry->body_bytes);
        if (distinct_member.objects.size() != 2 ||
            distinct_member.objects[0] != "verse.envanim" ||
            distinct_member.objects[1] != "verse.mesh") {
            std::fprintf(
                stderr,
                "milo_convert_test: View incorrectly inherited children_owner members\n");
            return 1;
        }

        gh::milo::Directory crowd_source;
        crowd_source.dir_version = 10;
        crowd_source.boundaries_exact = true;
        gh::milo_object::Mesh crowd_template;
        crowd_template.drawable.revision = 1;
        crowd_template.transformable.parent = "Crowd_A00.mesh";
        gh::milo::Entry crowd_template_entry;
        crowd_template_entry.type = "Mesh";
        crowd_template_entry.name = "Crowd_A00.mesh";
        crowd_template_entry.body_bytes =
            gh::milo_object::serialize_mesh(crowd_template);
        crowd_template_entry.terminator_value = 0xDEADDEADu;
        crowd_source.entries.push_back(std::move(crowd_template_entry));
        gh::milo_object::MultiMesh crowd_instances;
        crowd_instances.drawable.revision = 1;
        crowd_instances.mesh = "Crowd_A00.mesh";
        crowd_instances.transforms.push_back(
            {1, 0, 0, 0, 1, 0, 0, 0, 1, 10, 20, 30});
        gh::milo::Entry crowd_instances_entry;
        crowd_instances_entry.type = "MultiMesh";
        crowd_instances_entry.name = "Crowd01.mm";
        crowd_instances_entry.body_bytes =
            gh::milo_object::serialize_multi_mesh(crowd_instances);
        crowd_instances_entry.terminator_value = 0xDEADDEADu;
        crowd_source.entries.push_back(std::move(crowd_instances_entry));
        gh::milo_object::View crowd_view;
        crowd_view.drawable.revision = 1;
        crowd_view.children_owner = "crowd.view";
        crowd_view.transformable.parent = "crowd.view";
        gh::milo::Entry crowd_view_entry;
        crowd_view_entry.type = "View";
        crowd_view_entry.name = "crowd.view";
        crowd_view_entry.body_bytes =
            gh::milo_object::serialize_view(crowd_view);
        crowd_view_entry.terminator_value = 0xDEADDEADu;
        crowd_source.entries.push_back(std::move(crowd_view_entry));
        const auto crowd_result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                crowd_source, "crowd", "crowd.view");
        const auto find_crowd_target = [&](const std::string& name) {
            return std::find_if(
                crowd_result.directory.entries.begin(),
                crowd_result.directory.entries.end(),
                [&](const gh::milo::Entry& entry) {
                    return entry.name == name;
                });
        };
        const auto runtime_crowd_entry =
            find_crowd_target("__gh1_runtime_multimeshes.grp");
        const auto hidden_crowd_entry =
            find_crowd_target("__gh1_unreachable_drawables.grp");
        if (!crowd_result.complete ||
            runtime_crowd_entry == crowd_result.directory.entries.end() ||
            hidden_crowd_entry != crowd_result.directory.entries.end()) {
            std::fprintf(
                stderr,
                "milo_convert_test: runtime crowd ownership missing\n");
            return 1;
        }
        const auto runtime_crowd_group =
            gh::milo_object::parse_group12(
                runtime_crowd_entry->body_bytes);
        if (!runtime_crowd_group.drawable.showing ||
            runtime_crowd_group.objects !=
                std::vector<std::string>{"Crowd01.mm"}) {
            std::fprintf(
                stderr,
                "milo_convert_test: runtime crowd Group mismatch\n");
            return 1;
        }

        gh::milo::Directory model_source;
        model_source.dir_version = 10;
        model_source.boundaries_exact = true;
        for (int index = 0; index < 2; ++index) {
            gh::milo_object::View lod;
            lod.children_owner =
                "lod" + std::to_string(index) + ".view";
            lod.drawable.sphere = {0.0f, 0.0f, 0.0f, 40.0f};
            gh::milo::Entry entry;
            entry.type = "View";
            entry.name = lod.children_owner;
            entry.body_bytes =
                gh::milo_object::serialize_view(lod);
            entry.terminator_value = 0xDEADDEADu;
            model_source.entries.push_back(std::move(entry));
        }
        gh::milo_object::Mesh pelvis;
        pelvis.geometry_owner = "bone_pelvis.mesh";
        pelvis.transformable.local =
            {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        pelvis.transformable.world = pelvis.transformable.local;
        gh::milo::Entry pelvis_entry;
        pelvis_entry.type = "Mesh";
        pelvis_entry.name = "bone_pelvis.mesh";
        pelvis_entry.body_bytes =
            gh::milo_object::serialize_mesh(pelvis);
        pelvis_entry.terminator_value = 0xDEADDEADu;
        model_source.entries.push_back(std::move(pelvis_entry));
        gh::milo_convert::Gh1CharacterModelBuildInput model_input;
        model_input.spec.authored_name = "bassist";
        model_input.spec.package_name = "test_bass";
        model_input.spec.band_character = true;
        model_input.spec.role =
            gh::milo_convert::Gh1CharacterRole::Bassist;
        model_input.spec.compiled_skeleton =
            "charsys/test/gen/test_bass.rnd_ps2";
        model_input.spec.lod_screen_sizes = {0.4f};
        model_input.spec.sphere = {0.0f, 0.0f, 0.0f, 40.0f};
        model_input.spec.sphere_base = "bone_pelvis.mesh";
        model_input.source_model = model_source;
        model_input.animations.push_back(
            {gh::milo_convert::Gh2ClipSetRole::Band,
             model_input.spec.compiled_skeleton,
             "../../anims/test_bass_main.milo"});
        const auto model_package =
            gh::milo_convert::
                convert_gh1_character_to_gh2_model_package(
                    model_input);
        const auto model_root =
            gh::milo_object::parse_character9(
                model_package.directory.dir_body_bytes);
        const auto model_bytes =
            gh::milo::serialize_directory(model_package.directory);
        const auto parsed_model =
            gh::milo::parse_directory(model_bytes);
        const auto model_pelvis = std::find_if(
            model_package.directory.entries.begin(),
            model_package.directory.entries.end(),
            [](const gh::milo::Entry& entry) {
                return entry.name == "bone_pelvis.mesh";
            });
        if (!model_package.complete ||
            model_package.directory.dir_type != "Character" ||
            model_root.render_directory.object_directory.object_fields.type !=
                "bassist" ||
            model_package.native_transform_count != 1 ||
            model_pelvis == model_package.directory.entries.end() ||
            model_pelvis->type != "Trans" ||
            gh::milo_object::parse_trans9(
                model_pelvis->body_bytes).parent !=
                pelvis.transformable.parent ||
            model_root.lods.size() != 2 ||
            std::abs(model_root.lods[0].screen_size - 0.01f) >
                0.000001f ||
            model_root.lods[1].screen_size != 0.0f ||
            model_package.directory.entries.size() != 5 ||
            model_package.internal_reference_count == 0 ||
            !parsed_model.boundaries_exact ||
            gh::milo::serialize_directory(parsed_model) !=
                model_bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: character model package mismatch\n");
            return 1;
        }

        auto reference_directory = model_package.directory;
        const auto append_target_entry =
            [&reference_directory](
                const char* type, const char* name,
                std::vector<uint8_t> body) {
                gh::milo::Entry entry;
                entry.type = type;
                entry.name = name;
                entry.body_bytes = std::move(body);
                entry.size = entry.body_bytes.size();
                reference_directory.entries.push_back(std::move(entry));
            };
        gh::milo_object::TransAnim6 reference_transform_animation;
        reference_transform_animation.target = "bone_pelvis.mesh";
        reference_transform_animation.keys_owner = "reference.tnm";
        append_target_entry(
            "TransAnim", "reference.tnm",
            gh::milo_object::serialize_trans_anim6(
                reference_transform_animation));
        gh::milo_object::ParticleSys27 reference_particles;
        reference_particles.transformable.target =
            "bone_pelvis.mesh";
        reference_particles.transformable.parent =
            "bone_pelvis.mesh";
        reference_particles.relative_parent = "bone_pelvis.mesh";
        reference_particles.emitter_mesh = "bone_pelvis.mesh";
        append_target_entry(
            "ParticleSys", "reference.part",
            gh::milo_object::serialize_particle_sys27(
                reference_particles));
        gh::milo_object::ParticleSysAnim3
            reference_particle_animation;
        reference_particle_animation.particle_system =
            "reference.part";
        reference_particle_animation.keys_owner =
            "reference.partanim";
        append_target_entry(
            "ParticleSysAnim", "reference.partanim",
            gh::milo_object::serialize_particle_sys_anim3(
                reference_particle_animation));
        const size_t extended_reference_count =
            gh::milo_convert::
                validate_gh2_character_model_references(
                    reference_directory);
        if (extended_reference_count !=
            model_package.internal_reference_count + 8) {
            std::fprintf(
                stderr,
                "milo_convert_test: extended character reference "
                "count mismatch\n");
            return 1;
        }
        auto dangling_reference_directory = reference_directory;
        auto& dangling_animation =
            dangling_reference_directory.entries[
                dangling_reference_directory.entries.size() - 1];
        reference_particle_animation.particle_system =
            "missing.part";
        dangling_animation.body_bytes =
            gh::milo_object::serialize_particle_sys_anim3(
                reference_particle_animation);
        bool dangling_reference_rejected = false;
        try {
            (void)gh::milo_convert::
                validate_gh2_character_model_references(
                    dangling_reference_directory);
        } catch (const std::runtime_error&) {
            dangling_reference_rejected = true;
        }
        if (!dangling_reference_rejected) {
            std::fprintf(
                stderr,
                "milo_convert_test: dangling character reference "
                "accepted\n");
            return 1;
        }

        auto guitar_input = model_input;
        guitar_input.spec.authored_name = "guitarist";
        guitar_input.spec.package_name = "test_guitar";
        guitar_input.spec.band_character = false;
        guitar_input.spec.role =
            gh::milo_convert::Gh1CharacterRole::Guitarist;
        guitar_input.animations.clear();
        guitar_input.animations.push_back(
            {gh::milo_convert::Gh2ClipSetRole::GuitarMain,
             guitar_input.spec.compiled_skeleton,
             "../../anims/test_guitar_main.milo"});
        const auto append_guitar_mesh =
            [&guitar_input](
                const char* name, const char* parent) {
                gh::milo_object::Mesh mesh;
                mesh.geometry_owner = name;
                mesh.transformable.parent = parent;
                gh::milo::Entry entry;
                entry.type = "Mesh";
                entry.name = name;
                entry.body_bytes =
                    gh::milo_object::serialize_mesh(mesh);
                entry.terminator_value = 0xDEADDEADu;
                guitar_input.source_model.entries.push_back(
                    std::move(entry));
            };
        append_guitar_mesh("bone_head.mesh", "bone_pelvis.mesh");
        const auto append_guitar_twist_mesh =
            [&guitar_input](
                const char* name, float world_x) {
                gh::milo_object::Mesh mesh;
                mesh.geometry_owner = name;
                mesh.transformable.parent =
                    "bone_pelvis.mesh";
                mesh.transformable.local = {
                    1, 0, 0, 0, 1, 0,
                    0, 0, 1, world_x, 0, 0};
                mesh.transformable.world =
                    mesh.transformable.local;
                gh::milo::Entry entry;
                entry.type = "Mesh";
                entry.name = name;
                entry.body_bytes =
                    gh::milo_object::serialize_mesh(mesh);
                entry.terminator_value = 0xDEADDEADu;
                guitar_input.source_model.entries.push_back(
                    std::move(entry));
            };
        append_guitar_twist_mesh(
            "bone_L-upperTwist1.mesh", 1.0f);
        append_guitar_twist_mesh(
            "bone_L-upperTwist2.mesh", 3.0f);
        append_guitar_twist_mesh(
            "bone_L-upperArm.mesh", 4.0f);
        append_guitar_mesh("L-eye.mesh", "bone_head.mesh");
        append_guitar_mesh("R-eye.mesh", "bone_head.mesh");
        gh::milo_convert::Gh1CharacterControllerSpec
            upper_twist;
        upper_twist.kind =
            gh::milo_convert::
                Gh1CharacterControllerKind::UpperTwist;
        upper_twist.class_name = "AnimServoUpperTwist";
        upper_twist.name = "upperTwist_L.servo";
        upper_twist.bones = {
            "bone_L-upperTwist1.mesh",
            "bone_L-upperTwist2.mesh",
            "bone_L-upperArm.mesh"};
        guitar_input.spec.controllers.push_back(
            upper_twist);
        guitar_input.spec.eyes.present = true;
        guitar_input.spec.eyes.parent = "bone_head.mesh";
        guitar_input.spec.eyes.constraint = 0.925f;
        guitar_input.spec.eyes.lower_lid = 0.5f;
        const auto guitar_package =
            gh::milo_convert::
                convert_gh1_character_to_gh2_model_package(
                    guitar_input);
        const auto find_guitar_entry =
            [&guitar_package](
                const char* type,
                const char* name) -> const gh::milo::Entry* {
                for (const auto& entry :
                     guitar_package.directory.entries)
                    if (entry.type == type &&
                        entry.name == name)
                        return &entry;
                return nullptr;
            };
        const auto* guitar_outfit_entry =
            find_guitar_entry(
                "OutfitLoader", "guitar.outfit");
        const auto* fret_entry =
            find_guitar_entry("CharIKMidi", "fret.ik");
        const auto* left_weight_entry =
            find_guitar_entry(
                "CharWeightSetter", "left.weight");
        const auto* right_weight_entry =
            find_guitar_entry(
                "CharWeightSetter", "right.weight");
        const auto* left_eye_entry =
            find_guitar_entry("CharLookAt", "l-eye.lookat");
        const auto* right_eye_entry =
            find_guitar_entry("CharLookAt", "r-eye.lookat");
        const auto* eyes_entry =
            find_guitar_entry("CharEyes", "CharEyes.eyes");
        const auto* left_eye_mesh_entry =
            find_guitar_entry("Mesh", "L-eye.mesh");
        const auto* right_eye_mesh_entry =
            find_guitar_entry("Mesh", "R-eye.mesh");
        const auto* guitar_pelvis_entry =
            find_guitar_entry("Trans", "bone_pelvis.mesh");
        const auto* guitar_head_entry =
            find_guitar_entry("Trans", "bone_head.mesh");
        const auto* guitar_upper_twist_entry =
            find_guitar_entry(
                "CharUpperTwist",
                "upperTwist_L.servo");
        const auto* guitar_upper_twist2_entry =
            find_guitar_entry(
                "Trans",
                "bone_L-upperTwist2.mesh");
        if (!guitar_package.complete ||
            guitar_package.directory.dir_type != "BandCharacter" ||
            guitar_package.native_transform_count != 5 ||
            guitar_package.native_upper_twist_sibling_count != 1 ||
            guitar_package.directory.entries.size() != 19 ||
            !guitar_outfit_entry || !fret_entry ||
            !left_weight_entry || !right_weight_entry ||
            !left_eye_entry || !right_eye_entry || !eyes_entry ||
            !left_eye_mesh_entry || !right_eye_mesh_entry ||
            !guitar_pelvis_entry || !guitar_head_entry ||
            !guitar_upper_twist_entry ||
            !guitar_upper_twist2_entry) {
            std::fprintf(
                stderr,
                "milo_convert_test: guitarist stock graph missing\n");
            return 1;
        }
        const auto guitar_upper_twist2 =
            gh::milo_object::parse_trans9(
                guitar_upper_twist2_entry->body_bytes);
        if (guitar_upper_twist2.parent !=
                "bone_pelvis.mesh" ||
            std::abs(
                guitar_upper_twist2.local[9] - 3.0f) >
                0.000001f ||
            std::abs(
                guitar_upper_twist2.world[9] - 3.0f) >
                0.000001f) {
            std::fprintf(
                stderr,
                "milo_convert_test: shared GH1/GH2 upper-twist "
                "sibling graph was not preserved\n");
            return 1;
        }
        const auto guitar_outfit =
            gh::milo_object::parse_outfit_loader1(
                guitar_outfit_entry->body_bytes);
        const auto fret =
            gh::milo_object::parse_char_ik_midi4(
                fret_entry->body_bytes);
        const auto left_weight =
            gh::milo_object::parse_char_weight_setter2(
                left_weight_entry->body_bytes);
        const auto right_weight =
            gh::milo_object::parse_char_weight_setter2(
                right_weight_entry->body_bytes);
        const auto left_eye =
            gh::milo_object::parse_char_look_at2(
                left_eye_entry->body_bytes);
        const auto right_eye =
            gh::milo_object::parse_char_look_at2(
                right_eye_entry->body_bytes);
        const auto eyes =
            gh::milo_object::parse_char_eyes3(
                eyes_entry->body_bytes);
        const auto left_eye_mesh =
            gh::milo_object::parse_mesh28(
                left_eye_mesh_entry->body_bytes);
        const auto right_eye_mesh =
            gh::milo_object::parse_mesh28(
                right_eye_mesh_entry->body_bytes);
        const float eye_limit =
            std::acos(0.925f) *
            (180.0f / 3.14159265358979323846f);
        if (guitar_outfit.object_fields.type != "guitar" ||
            guitar_outfit.directory != "../../../og" ||
            guitar_outfit.categories.size() != 1 ||
            guitar_outfit.categories[0].selected != 0 ||
            guitar_outfit.categories[0].shown != 0 ||
            guitar_outfit.categories[0].outfits.size() != 58 ||
            fret.bone != "bone_fret.mesh" ||
            left_weight.weightable.weight != 1.0f ||
            left_weight.weightable.weight_owner != "left.weight" ||
            left_weight.driver != "main.drv" ||
            left_weight.flags != 0x00400000u ||
            right_weight.weightable.weight != 1.0f ||
            right_weight.weightable.weight_owner != "right.weight" ||
            right_weight.driver != "main.drv" ||
            right_weight.flags != 0x00800000u ||
            left_eye.source != "L-eye.mesh" ||
            left_eye.pivot != "L-eye.mesh" ||
            left_eye.half_time != 0.0f ||
            std::abs(left_eye.min_yaw + eye_limit) > 0.000001f ||
            std::abs(left_eye.max_yaw - eye_limit) > 0.000001f ||
            std::abs(left_eye.min_pitch + eye_limit) > 0.000001f ||
            std::abs(left_eye.max_pitch - eye_limit) > 0.000001f ||
            right_eye.source != "R-eye.mesh" ||
            right_eye.pivot != "R-eye.mesh" ||
            right_eye.min_yaw != left_eye.min_yaw ||
            right_eye.max_yaw != left_eye.max_yaw ||
            right_eye.min_pitch != left_eye.min_pitch ||
            right_eye.max_pitch != left_eye.max_pitch ||
            eyes.eyes !=
                std::vector<std::string>{
                    "l-eye.lookat", "r-eye.lookat"} ||
            !eyes.legacy_transform.empty() ||
            left_eye_mesh.transformable.parent != "bone_head.mesh" ||
            right_eye_mesh.transformable.parent != "bone_head.mesh") {
            std::fprintf(
                stderr,
                "milo_convert_test: guitarist stock graph mismatch\n");
            return 1;
        }

        auto drummer_input = model_input;
        drummer_input.spec.authored_name = "drummer";
        drummer_input.spec.package_name = "test_drummer";
        drummer_input.spec.role =
            gh::milo_convert::Gh1CharacterRole::Drummer;
        const auto drummer_package =
            gh::milo_convert::
                convert_gh1_character_to_gh2_model_package(
                    drummer_input);
        const gh::milo::Entry* drums_outfit_entry = nullptr;
        for (const auto& entry : drummer_package.directory.entries)
            if (entry.type == "OutfitLoader" &&
                entry.name == "drums.outfit")
                drums_outfit_entry = &entry;
        if (!drummer_package.complete ||
            drummer_package.directory.entries.size() != 6 ||
            !drums_outfit_entry) {
            std::fprintf(
                stderr,
                "milo_convert_test: drummer stock graph missing\n");
            return 1;
        }
        const auto drums_outfit =
            gh::milo_object::parse_outfit_loader1(
                drums_outfit_entry->body_bytes);
        if (drums_outfit.object_fields.type != "drummer" ||
            drums_outfit.directory != "../../../og" ||
            drums_outfit.categories.size() != 1 ||
            drums_outfit.categories[0].selected != 1 ||
            drums_outfit.categories[0].shown != 1 ||
            drums_outfit.categories[0].outfits.size() != 8) {
            std::fprintf(
                stderr,
                "milo_convert_test: drummer stock graph mismatch\n");
            return 1;
        }

        auto merged_input = model_input;
        merged_input.spec.package_name = "test_merge";
        merged_input.spec.role =
            gh::milo_convert::Gh1CharacterRole::Singer;
        merged_input.spec.face_file = "test_face.rnd_ps2";
        merged_input.spec.shadow_file = "test_shadow.rnd_ps2";
        merged_input.spec.face.present = true;
        merged_input.spec.face.poses = {"ref", "smile"};
        merged_input.spec.face.blend_time = 0.1f;
        merged_input.spec.face.event_list = "singer";
        gh::milo_object::Mesh face_target;
        face_target.geometry_owner = "head.mesh";
        face_target.vertices.resize(3);
        face_target.vertices[1].position[0] = 1.0f;
        face_target.vertices[2].position[1] = 1.0f;
        face_target.faces.push_back({0, 1, 2});
        gh::milo::Entry face_target_entry;
        face_target_entry.type = "Mesh";
        face_target_entry.name = "head.mesh";
        face_target_entry.body_bytes =
            gh::milo_object::serialize_mesh(face_target);
        face_target_entry.terminator_value = 0xDEADDEADu;
        merged_input.source_model.entries.push_back(
            std::move(face_target_entry));

        gh::milo::Directory face_source;
        face_source.dir_version = 10;
        face_source.boundaries_exact = true;
        const auto append_face_mesh =
            [&face_source, &face_target](const char* name) {
                auto mesh = face_target;
                mesh.geometry_owner = name;
                gh::milo::Entry entry;
                entry.type = "Mesh";
                entry.name = name;
                entry.body_bytes =
                    gh::milo_object::serialize_mesh(mesh);
                entry.terminator_value = 0xDEADDEADu;
                face_source.entries.push_back(std::move(entry));
            };
        append_face_mesh("ref.mesh");
        append_face_mesh("smile.mesh");
        gh::milo_object::Morph face_morph;
        face_morph.target = "ref.mesh";
        face_morph.poses.push_back(
            {"ref.mesh", {{1.0f, 0.0f}, {0.0f, 1.0f}}});
        face_morph.poses.push_back(
            {"smile.mesh", {{0.0f, 0.0f}, {1.0f, 1.0f}}});
        gh::milo::Entry face_morph_entry;
        face_morph_entry.type = "Morph";
        face_morph_entry.name = "face.morph";
        face_morph_entry.body_bytes =
            gh::milo_object::serialize_morph(face_morph);
        face_morph_entry.terminator_value = 0xDEADDEADu;
        face_source.entries.push_back(std::move(face_morph_entry));
        merged_input.face_model = std::move(face_source);

        gh::milo::Directory shadow_source;
        shadow_source.dir_version = 10;
        shadow_source.boundaries_exact = true;
        const auto append_shadow_entry =
            [&shadow_source](
                const char* type, const char* name,
                std::vector<uint8_t> body) {
                gh::milo::Entry entry;
                entry.type = type;
                entry.name = name;
                entry.body_bytes = std::move(body);
                entry.terminator_value = 0xDEADDEADu;
                shadow_source.entries.push_back(std::move(entry));
            };
        gh::milo_object::View shadow_root;
        shadow_root.children_owner = "shadow_root.view";
        shadow_root.drawable.objects = {"shadow_helper.view"};
        append_shadow_entry(
            "View", "shadow_root.view",
            gh::milo_object::serialize_view(shadow_root));
        gh::milo_object::View shadow_helper;
        shadow_helper.children_owner = "shadow_helper.view";
        shadow_helper.drawable.objects = {"shadow_body.mesh"};
        append_shadow_entry(
            "View", "shadow_helper.view",
            gh::milo_object::serialize_view(shadow_helper));
        gh::milo_object::Mesh shadow_body;
        shadow_body.geometry_owner = "shadow_body.mesh";
        append_shadow_entry(
            "Mesh", "shadow_body.mesh",
            gh::milo_object::serialize_mesh(shadow_body));
        gh::milo_object::Mesh unused_shadow_skeleton;
        unused_shadow_skeleton.geometry_owner =
            "bone_pelvis.mesh";
        append_shadow_entry(
            "Mesh", "bone_pelvis.mesh",
            gh::milo_object::serialize_mesh(
                unused_shadow_skeleton));
        merged_input.shadow_model = std::move(shadow_source);

        const auto merged_package =
            gh::milo_convert::
                convert_gh1_character_to_gh2_model_package(
                    merged_input);
        const auto merged_root =
            gh::milo_object::parse_character9(
                merged_package.directory.dir_body_bytes);
        const gh::milo::Entry* merged_shadow_root = nullptr;
        const gh::milo::Entry* merged_shadow_helper = nullptr;
        const gh::milo::Entry* merged_shadow_body = nullptr;
        const gh::milo::Entry* merged_face_morph = nullptr;
        const gh::milo::Entry* merged_face_ref = nullptr;
        const gh::milo::Entry* merged_face_smile = nullptr;
        const gh::milo::Entry* merged_transition = nullptr;
        const gh::milo::Entry* merged_transition_filter = nullptr;
        const gh::milo::Entry* merged_transition_trigger = nullptr;
        const gh::milo::Entry* merged_face_controller = nullptr;
        size_t pelvis_count = 0;
        for (const auto& entry : merged_package.directory.entries) {
            if (entry.name == "test_merge_shadow.grp")
                merged_shadow_root = &entry;
            else if (
                entry.name ==
                "test_merge_shadow__shadow_helper.view")
                merged_shadow_helper = &entry;
            else if (
                entry.name ==
                "test_merge_shadow__shadow_body.mesh")
                merged_shadow_body = &entry;
            else if (
                entry.name ==
                "test_merge_face__face.morph")
                merged_face_morph = &entry;
            else if (entry.name == "test_merge_face__ref.mesh")
                merged_face_ref = &entry;
            else if (entry.name == "test_merge_face__smile.mesh")
                merged_face_smile = &entry;
            else if (
                entry.name == "gh1_face_0_1_0.mrf")
                merged_transition = &entry;
            else if (
                entry.name == "gh1_face_0_1.filt")
                merged_transition_filter = &entry;
            else if (
                entry.name == "gh1_face_0_1.trig")
                merged_transition_trigger = &entry;
            else if (entry.name == "lip.servo")
                merged_face_controller = &entry;
            if (entry.name == "bone_pelvis.mesh")
                ++pelvis_count;
        }
        if (!merged_package.complete ||
            !merged_package.unresolved_dependencies.empty() ||
            merged_package.generated_dependencies.size() != 1 ||
            merged_package.generated_dependencies[0] !=
                "face-control-config:gh1_singer_morph_face" ||
            merged_package.face_transition_count != 4 ||
            merged_package.face_controller_type !=
                "gh1_singer_morph_face" ||
            merged_root.shadow != "test_merge_shadow.grp" ||
            merged_package.directory.entries.size() != 29 ||
            pelvis_count != 1 || !merged_shadow_root ||
            !merged_shadow_helper || !merged_shadow_body ||
            !merged_face_morph || !merged_face_ref ||
            !merged_face_smile || !merged_transition ||
            !merged_transition_filter ||
            !merged_transition_trigger ||
            !merged_face_controller) {
            std::fprintf(
                stderr,
                "milo_convert_test: face/shadow merge missing\n");
            return 1;
        }
        const auto merged_root_group =
            gh::milo_object::parse_group12(
                merged_shadow_root->body_bytes);
        const auto merged_helper_group =
            gh::milo_object::parse_group12(
                merged_shadow_helper->body_bytes);
        const auto merged_body =
            gh::milo_object::parse_mesh28(
                merged_shadow_body->body_bytes);
        const auto merged_morph =
            gh::milo_object::parse_morph4(
                merged_face_morph->body_bytes);
        const auto merged_ref =
            gh::milo_object::parse_mesh28(
                merged_face_ref->body_bytes);
        const auto merged_smile =
            gh::milo_object::parse_mesh28(
                merged_face_smile->body_bytes);
        const auto transition_morph =
            gh::milo_object::parse_morph4(
                merged_transition->body_bytes);
        const auto transition_filter =
            gh::milo_object::parse_anim_filter1(
                merged_transition_filter->body_bytes);
        const auto transition_trigger =
            gh::milo_object::parse_event_trigger8(
                merged_transition_trigger->body_bytes);
        const auto face_controller =
            gh::milo_object::parse_group12(
                merged_face_controller->body_bytes);
        if (merged_root_group.objects !=
                std::vector<std::string>{
                    "test_merge_shadow__shadow_helper.view"} ||
            merged_helper_group.objects !=
                std::vector<std::string>{
                    "test_merge_shadow__shadow_body.mesh"} ||
            merged_body.geometry_owner !=
                "test_merge_shadow__shadow_body.mesh" ||
            merged_morph.target != "head.mesh" ||
            merged_morph.poses.size() != 2 ||
            merged_morph.poses[0].mesh !=
                "test_merge_face__ref.mesh" ||
            merged_morph.poses[1].mesh !=
                "test_merge_face__smile.mesh" ||
            merged_ref.drawable.showing ||
            merged_smile.drawable.showing ||
            transition_morph.target != "head.mesh" ||
            transition_morph.poses.size() != 2 ||
            transition_morph.poses[0].keys.size() != 4 ||
            transition_morph.poses[1].keys.size() != 4 ||
            std::abs(
                transition_morph.poses[1].keys[1].value -
                0.25f) > 0.000001f ||
            std::abs(
                transition_morph.poses[1].keys[2].value -
                0.75f) > 0.000001f ||
            transition_filter.anim != "gh1_face_0_1.grp" ||
            transition_filter.start != 0.0f ||
            std::abs(transition_filter.end - 3.0f) >
                0.000001f ||
            transition_filter.period != 0.1f ||
            transition_trigger.animations.size() != 1 ||
            transition_trigger.animations[0].animation !=
                "gh1_face_0_1.filt" ||
            face_controller.object_fields.type !=
                "gh1_singer_morph_face" ||
            merged_package.internal_reference_count == 0) {
            std::fprintf(
                stderr,
                "milo_convert_test: face/shadow merge mismatch\n");
            return 1;
        }

        gh::milo::Directory graph_source;
        graph_source.dir_version = 10;
        graph_source.boundaries_exact = true;
        auto add_source_entry = [&graph_source](
                                    const char* type, const char* name,
                                    std::vector<uint8_t> body) {
            gh::milo::Entry entry;
            entry.type = type;
            entry.name = name;
            entry.body_bytes = std::move(body);
            entry.terminator_value = 0xDEADDEADu;
            graph_source.entries.push_back(std::move(entry));
        };

        gh::milo_object::View parent;
        parent.animatable.objects = {"root.tnm"};
        parent.transformable.children = {"child.lt"};
        parent.transformable.parent = "parent.view";
        parent.children_owner = "parent.view";
        add_source_entry(
            "View", "parent.view",
            gh::milo_object::serialize_view(parent));

        gh::milo_object::TransAnim root_animation;
        root_animation.animatable.objects = {"child.mnm"};
        add_source_entry(
            "TransAnim", "root.tnm",
            gh::milo_object::serialize_trans_anim(root_animation));

        gh::milo_object::MatAnim child_animation;
        add_source_entry(
            "MatAnim", "child.mnm",
            gh::milo_object::serialize_mat_anim(child_animation));

        gh::milo_object::Light child;
        child.transformable.constraint = 3;
        child.transformable.parent = "child.lt";
        add_source_entry(
            "Light", "child.lt",
            gh::milo_object::serialize_light(child));

        gh::milo_object::Light early_child;
        early_child.transformable.constraint = 1;
        add_source_entry(
            "Light", "early.lt",
            gh::milo_object::serialize_light(early_child));

        gh::milo_object::View late_parent;
        late_parent.transformable.children = {"early.lt"};
        late_parent.transformable.parent = "late.view";
        late_parent.children_owner = "late.view";
        add_source_entry(
            "View", "late.view",
            gh::milo_object::serialize_view(late_parent));

        const auto graph_result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                graph_source, "transform_graph");
        auto find_target = [&graph_result](const char* name)
            -> const gh::milo::Entry* {
            for (const auto& entry : graph_result.directory.entries)
                if (entry.name == name) return &entry;
            return nullptr;
        };
        const auto* child_target = find_target("child.lt");
        const auto* early_target = find_target("early.lt");
        const auto* parent_target = find_target("parent.view");
        if (!graph_result.complete || !child_target || !early_target ||
            !parent_target) {
            std::fprintf(
                stderr, "milo_convert_test: transform graph missing\n");
            return 1;
        }
        const auto parsed_child =
            gh::milo_object::parse_light6(child_target->body_bytes);
        const auto parsed_early =
            gh::milo_object::parse_light6(early_target->body_bytes);
        const auto parsed_parent =
            gh::milo_object::parse_group12(
                parent_target->body_bytes);
        if (parsed_child.transformable.parent != "parent.view" ||
            parsed_child.transformable.constraint != 4 ||
            parsed_early.transformable.parent != "late.view" ||
            parsed_early.transformable.constraint != 2 ||
            parsed_parent.objects !=
                std::vector<std::string>{
                    "root.tnm", "child.mnm"}) {
            std::fprintf(
                stderr,
                "milo_convert_test: transform graph semantics mismatch\n");
            return 1;
        }

        gh::acp::File acp;
        acp.class_name = "AnimClipSamples";
        acp.object_name = "walk";
        acp.revision = 18;
        acp.start_beat = 1.0f;
        acp.end_beat = 5.0f;
        acp.beats_per_second = 2.0f;
        acp.flags = 0x80000040u;
        acp.play_flags = 2;
        acp.blend_width = 0.25f;
        acp.sample_set_revision = 5;
        acp.channel_sets[0].channels = {
            "root.pos", "root.quat", "knee.rotz"};
        acp.channel_sets[0].sample_count = 2;
        acp.channel_sets[0].compression = 1;
        acp.channel_sets[0].frame_size = 22;
        acp.channel_sets[0].sample_bytes.resize(44, 0x5a);
        acp.channel_sets[1].compression = 1;
        gh::milo_object::CharClipTransition5 transition;
        transition.clip = "idle";
        transition.nodes.push_back({4.0f, 0.0f});
        const auto converted_clip =
            gh::milo_convert::
                convert_gh1_acp_to_gh2_char_clip_samples10(
                    acp, {transition});
        const auto clip_bytes =
            gh::milo_object::serialize_char_clip_samples10(
                converted_clip);
        const auto parsed_clip =
            gh::milo_object::parse_char_clip_samples10(clip_bytes);
        if (gh::milo_object::serialize_char_clip_samples10(
                parsed_clip) != clip_bytes ||
            parsed_clip.start_beat != 1.0f ||
            parsed_clip.flags != 0x40 ||
            parsed_clip.play_flags != 0x2000 ||
            parsed_clip.transitions.size() != 1 ||
            parsed_clip.full.counts[1] != 1 ||
            parsed_clip.full.counts[3] != 2 ||
            parsed_clip.full.counts[6] != 3 ||
            parsed_clip.full.sample_bytes.size() != 44 ||
            !parsed_clip.duplicate.channels.empty() ||
            !parsed_clip.duplicate.sample_bytes.empty() ||
            parsed_clip.duplicate.sample_count != 2 ||
            gh::milo_object::
                    char_clip_samples10_ps2_allocate_size(parsed_clip) !=
                1028) {
            std::fprintf(
                stderr,
                "milo_convert_test: ACP->CharClipSamples mismatch\n");
            return 1;
        }

        gh::acg::Graph graph;
        graph.clips.resize(3);
        graph.clips[0].nodes = {
            {2, 1.0f, 2.0f},
            {1, 3.0f, 4.0f},
            {2, 5.0f, 6.0f}};
        const auto converted_transitions =
            gh::milo_convert::
                convert_gh1_acg_to_gh2_char_clip_transitions(
                    graph, {"first", "second", "third"});
        if (converted_transitions.size() != 3 ||
            converted_transitions[0].size() != 2 ||
            converted_transitions[0][0].clip != "third" ||
            converted_transitions[0][0].nodes.size() != 2 ||
            converted_transitions[0][0].nodes[1].current_beat !=
                5.0f ||
            converted_transitions[0][1].clip != "second") {
            std::fprintf(
                stderr,
                "milo_convert_test: ACG->CharClipTransitions mismatch\n");
            return 1;
        }

        gh::milo_convert::Gh1ClipSetBuildInput package_input;
        package_input.spec.invocation = "TEST_BAND";
        package_input.spec.qualified_name = "main::band.cset";
        package_input.spec.target_name = "band";
        package_input.spec.archetype_rnd =
            "charsys/test_band/test_band.rnd";
        package_input.spec.play_flags = 2;
        package_input.spec.recenter_channels = {"root", "prop"};
        package_input.spec.recenter_bones = {"left_foot", "right_foot"};
        package_input.archetype.dir_version = 10;
        package_input.archetype.boundaries_exact = true;
        auto add_skeleton_mesh =
            [&package_input](const char* name) {
                gh::milo_object::Mesh mesh;
                mesh.transformable.revision = 8;
                mesh.drawable.revision = 1;
                mesh.geometry_owner = name;
                gh::milo::Entry entry;
                entry.type = "Mesh";
                entry.name = name;
                entry.body_bytes =
                    gh::milo_object::serialize_mesh(mesh);
                entry.terminator_value = 0xDEADDEADu;
                package_input.archetype.entries.push_back(
                    std::move(entry));
            };
        add_skeleton_mesh("root.mesh");
        add_skeleton_mesh("prop.mesh");
        add_skeleton_mesh("left_foot.mesh");
        add_skeleton_mesh("right_foot.mesh");
        auto make_package_clip = [](
                                     const char* name,
                                     bool prop_dynamic) {
            gh::acp::File clip;
            clip.class_name = "AnimClipSamples";
            clip.object_name = name;
            clip.revision = 18;
            clip.start_beat = 0.0f;
            clip.end_beat = 2.0f;
            clip.beats_per_second = 1.0f;
            clip.flags = 0x80000001u;
            clip.play_flags = 2;
            clip.blend_width = 0.5f;
            clip.sample_set_revision = 5;
            clip.channel_sets[0].channels = {"root.pos"};
            if (prop_dynamic)
                clip.channel_sets[0].channels.push_back("prop.pos");
            clip.channel_sets[0].sample_count = 2;
            clip.channel_sets[0].compression = 1;
            clip.channel_sets[0].frame_size =
                clip.channel_sets[0].channels.size() * 12;
            clip.channel_sets[0].sample_bytes.resize(
                clip.channel_sets[0].frame_size * 2);
            if (!prop_dynamic) {
                clip.channel_sets[1].channels = {"prop.pos"};
                clip.channel_sets[1].frame_size = 12;
                clip.channel_sets[1].sample_bytes.resize(12);
            }
            clip.channel_sets[1].sample_count = 1;
            clip.channel_sets[1].compression = 1;
            return clip;
        };
        package_input.clips = {
            make_package_clip("first", true),
            make_package_clip("second", false)};
        for (const auto& clip : package_input.clips) {
            gh::milo_convert::Gh1AnimationSpec animation;
            animation.name = clip.object_name;
            animation.flags = 1;
            animation.play_flags = 2;
            animation.blend_width = 0.5f;
            animation.channels =
                clip.channel_sets[0].channels;
            animation.channels.insert(
                animation.channels.end(),
                clip.channel_sets[1].channels.begin(),
                clip.channel_sets[1].channels.end());
            package_input.spec.animations.push_back(
                std::move(animation));
        }
        const auto stationary_packages =
            gh::milo_convert::
                convert_gh1_clip_set_to_gh2_packages(
                    package_input);
        if (stationary_packages.size() != 1 ||
            stationary_packages[0].role !=
                gh::milo_convert::Gh2ClipSetRole::Band) {
            std::fprintf(
                stderr,
                "milo_convert_test: native package role mismatch\n");
            return 1;
        }
        const auto stationary_root =
            gh::milo_object::parse_char_clip_set14(
                stationary_packages[0].directory.dir_body_bytes,
                2);
        if (stationary_root.move_self ||
            stationary_root.recenter_targets !=
                std::vector<std::string>(
                    {"root.trans", "prop.trans"})) {
            std::fprintf(
                stderr,
                "milo_convert_test: stationary recenter mismatch\n");
            return 1;
        }
        package_input.clips[1] =
            make_package_clip("second", true);
        package_input.spec.animations[1].channels =
            package_input.clips[1].channel_sets[0].channels;
        const auto moving_packages =
            gh::milo_convert::
                convert_gh1_clip_set_to_gh2_packages(
                    package_input);
        const auto moving_root =
            gh::milo_object::parse_char_clip_set14(
                moving_packages[0].directory.dir_body_bytes,
                2);
        if (!moving_root.move_self ||
            gh::milo::serialize_directory(
                moving_packages[0].directory) !=
                gh::milo::serialize_directory(
                    gh::milo::parse_directory(
                        gh::milo::serialize_directory(
                            moving_packages[0].directory)))) {
            std::fprintf(
                stderr,
                "milo_convert_test: moving recenter mismatch\n");
            return 1;
        }

        auto hand_input = package_input;
        hand_input.spec.invocation = "TEST_HAND";
        hand_input.spec.qualified_name =
            "main::test_hand.cset";
        hand_input.spec.target_name = "test_hand";
        hand_input.spec.archetype_rnd =
            "charsys/test_hand/test_hand.rnd";
        hand_input.spec.animations.clear();
        hand_input.spec.recenter_channels.clear();
        hand_input.spec.recenter_bones.clear();
        hand_input.clips.clear();
        const auto add_hand_clip =
            [&](const char* name, const char* anchor) {
                auto clip = make_package_clip(name, true);
                clip.channel_sets[0].channels = {
                    std::string(anchor) + ".pos"};
                clip.channel_sets[0].frame_size = 12;
                clip.channel_sets[0].sample_bytes.resize(24);
                clip.channel_sets[1].channels.clear();
                clip.channel_sets[1].frame_size = 0;
                clip.channel_sets[1].sample_bytes.clear();
                gh::milo_convert::Gh1AnimationSpec animation;
                animation.name = name;
                animation.flags = clip.flags & 0x7fffffffu;
                animation.play_flags = clip.play_flags;
                animation.blend_width = clip.blend_width;
                animation.channels =
                    clip.channel_sets[0].channels;
                hand_input.clips.push_back(std::move(clip));
                hand_input.spec.animations.push_back(
                    std::move(animation));
            };
        add_hand_clip("finger_open", "bone_fret_hand");
        add_hand_clip("strum_open", "bone_strum_hand");
        add_hand_clip(
            "strum_pluck_short", "bone_strum_hand");
        add_hand_clip(
            "strum_pluck_down", "bone_strum_hand");
        add_hand_clip(
            "strum_down_long", "bone_strum_hand");
        add_skeleton_mesh("bone_fret_hand.mesh");
        add_skeleton_mesh("bone_strum_hand.mesh");
        hand_input.archetype = package_input.archetype;
        const auto hand_packages =
            gh::milo_convert::
                convert_gh1_clip_set_to_gh2_packages(
                    hand_input);
        const auto strum_package = std::find_if(
            hand_packages.begin(), hand_packages.end(),
            [](const auto& package) {
                return package.role ==
                       gh::milo_convert::
                           Gh2ClipSetRole::GuitarStrum;
            });
        std::set<std::string> strum_names;
        if (strum_package != hand_packages.end()) {
            for (const auto& entry :
                 strum_package->directory.entries)
                if (entry.type == "CharClipSamples")
                    strum_names.insert(entry.name);
        }
        const std::set<std::string> expected_strum_names = {
            "strum_open",
            "strum_pluck_short", "strum_pluck_down",
            "strum_down_long",
            "strum_short_01", "strum_short_02",
            "strum_short_03", "strum_short_04",
            "strum_long_01", "strum_long_02",
            "strum_long_03", "strum_long_04",
            "strum_pick_01", "strum_pick_02"};
        if (hand_packages.size() != 2 ||
            strum_package == hand_packages.end() ||
            strum_names != expected_strum_names ||
            strum_package->source_clips.size() != 4) {
            std::fprintf(
                stderr,
                "milo_convert_test: GH1/GH2 strum semantic "
                "alias contract mismatch\n");
            return 1;
        }

        if (gh::milo_convert::
                convert_gh1_guitar_clip_flags_to_gh2(
                    0x06FFC0E3u) != 0x00C00003u ||
            gh::milo_convert::
                convert_gh1_guitar_clip_flags_to_gh2(
                    0x00C00842u) != 0x00C00802u ||
            gh::milo_convert::
                convert_gh1_guitar_clip_flags_to_gh2(
                    0x00C02882u) != 0x00C02802u) {
            std::fprintf(
                stderr,
                "milo_convert_test: GH1 guitarist family flags leaked "
                "into GH2 CharClip semantics\n");
            return 1;
        }
        if (gh::milo_convert::
                convert_gh1_band_clip_flags_to_gh2(
                    0x00000040u, false) != 0x00001440u ||
            gh::milo_convert::
                convert_gh1_band_clip_flags_to_gh2(
                    0x00000007u, false) != 0x00000007u ||
            gh::milo_convert::
                convert_gh1_band_clip_flags_to_gh2(
                    0x00000006u, true) != 0x00000206u ||
            gh::milo_convert::
                convert_gh1_band_clip_flags_to_gh2(
                    0x00000012u, true) != 0x00000012u) {
            std::fprintf(
                stderr,
                "milo_convert_test: GH1 band target-domain promotion "
                "mismatch\n");
            return 1;
        }

        gh::milo::Directory venue_main;
        venue_main.dir_version = 24;
        venue_main.dir_type = "RndDir";
        venue_main.dir_name = "gh1_fixture";
        venue_main.boundaries_exact = true;
        gh::milo_object::RndDir8 venue_root;
        venue_root.transformable.local =
            {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        venue_root.transformable.world =
            venue_root.transformable.local;
        venue_main.dir_body_bytes =
            gh::milo_object::serialize_rnd_dir8(venue_root);

        gh::milo::Directory venue_lighting = venue_main;
        venue_lighting.dir_name = "lighting";
        auto add_spot =
            [&](const std::string& name, float x) {
                gh::milo_object::Mesh28 mesh;
                mesh.transformable.local =
                    {2, 0, 0, 0, 3, 0, 0, 0, 4, x, 20, 30};
                mesh.transformable.world =
                    mesh.transformable.local;
                mesh.bsp_nodes.emplace_back();
                gh::milo::Entry entry;
                entry.type = "Mesh";
                entry.name = name;
                entry.body_bytes =
                    gh::milo_object::serialize_mesh28(mesh);
                entry.size = entry.body_bytes.size();
                entry.terminator_value = 0xDEADDEADu;
                venue_lighting.entries.push_back(std::move(entry));
            };
        add_spot("walk_spot_01.mesh", 10);
        add_spot("stage_spot_01.mesh", 11);
        add_spot("stage_spot_02.mesh", 12);
        add_spot("stage_spot_03.mesh", 13);
        const auto placement =
            gh::milo_convert::
                convert_gh1_venue_spots_to_gh2_waypoints(
                    "gh1_fixture", {venue_main, venue_lighting});
        if (placement.waypoints != 6 ||
            placement.characters_directory.entries.size() != 6 ||
            placement.records.size() != 4 ||
            placement.characters_directory.dir_name !=
                "gh1_fixture_chars") {
            std::fprintf(
                stderr,
                "milo_convert_test: venue placement package mismatch\n");
            return 1;
        }
        const auto placement_bytes =
            gh::milo::serialize_directory(
                placement.characters_directory);
        const auto placement_verify =
            gh::milo::parse_directory(placement_bytes);
        if (!placement_verify.boundaries_exact ||
            gh::milo::serialize_directory(placement_verify) !=
                placement_bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: venue placement directory "
                "round trip mismatch\n");
            return 1;
        }
        const std::array<uint32_t, 4> expected_flags =
            {65, 4, 16, 32};
        const std::array<float, 4> expected_x =
            {10, 11, 12, 13};
        const std::array<std::string, 4> expected_roles = {
            "guitarist0", "singer", "bassist", "drummer"};
        const std::array<std::string, 4> expected_helpers = {
            "walk_spot_01.mesh", "stage_spot_01.mesh",
            "stage_spot_02.mesh", "stage_spot_03.mesh"};
        for (size_t index = 0; index < 4; ++index) {
            const auto waypoint =
                gh::milo_object::parse_waypoint3(
                    placement.characters_directory.entries[index]
                        .body_bytes);
            const auto& record = placement.records[index];
            if (waypoint.flags != expected_flags[index] ||
                waypoint.transformable.local[0] != 1.0f ||
                waypoint.transformable.local[4] != 1.0f ||
                waypoint.transformable.local[8] != 1.0f ||
                waypoint.transformable.local[9] != expected_x[index] ||
                record.role != expected_roles[index] ||
                record.source_helper != expected_helpers[index] ||
                record.target_waypoint !=
                    placement.characters_directory.entries[index].name ||
                record.flags != expected_flags[index] ||
                record.source_world[0] != 2.0f ||
                record.source_world[4] != 3.0f ||
                record.source_world[8] != 4.0f ||
                record.source_world[9] != expected_x[index] ||
                record.target_transform !=
                    waypoint.transformable.local ||
                waypoint.transformable.world !=
                    waypoint.transformable.local) {
                std::fprintf(
                    stderr,
                    "milo_convert_test: venue placement waypoint mismatch\n");
                return 1;
            }
        }
        const auto walk_left =
            gh::milo_object::parse_waypoint3(
                placement.characters_directory.entries[4].body_bytes);
        const auto walk_right =
            gh::milo_object::parse_waypoint3(
                placement.characters_directory.entries[5].body_bytes);
        if (placement.characters_directory.entries[4].name !=
                "walk_guitarist0_left.way" ||
            placement.characters_directory.entries[5].name !=
                "walk_guitarist0_right.way" ||
            walk_left.flags != 64 ||
            walk_right.flags != 64 ||
            walk_left.transformable.local[9] != -62.0f ||
            walk_right.transformable.local[9] != 82.0f ||
            walk_left.connections !=
                std::vector<std::string>(
                    {"start_guitarist0.way",
                     "walk_guitarist0_right.way"}) ||
            walk_right.connections !=
                std::vector<std::string>(
                    {"start_guitarist0.way",
                     "walk_guitarist0_left.way"})) {
            std::fprintf(
                stderr,
                "milo_convert_test: derived guitarist walk route mismatch\n");
            return 1;
        }
        gh::milo_convert::link_gh2_venue_characters_directory(
            venue_main, "gh1_fixture");
        const auto linked_root =
            gh::milo_object::parse_rnd_dir8(
                venue_main.dir_body_bytes);
        if (linked_root.object_directory.subdirectories !=
            std::vector<std::string>(
                {"gh1_fixture_chars.milo"})) {
            std::fprintf(
                stderr,
                "milo_convert_test: venue placement link mismatch\n");
            return 1;
        }
        gh::milo::Entry preview_camera;
        preview_camera.type = "Cam";
        preview_camera.name = "fixture_preview.cam";
        preview_camera.body_bytes =
            gh::milo_object::serialize_cam12(
                gh::milo_object::Cam12{});
        preview_camera.size = preview_camera.body_bytes.size();
        preview_camera.terminator_value = 0xDEADDEADu;
        venue_main.entries.push_back(std::move(preview_camera));
        gh::milo_convert::finalize_gh2_venue_world_directory(
            venue_main, "gh1_fixture",
            {{"lighting", "lighting"}, {"crowd", "crowd"}},
            true);
        const auto world_root =
            gh::milo_object::parse_world_dir11(
                venue_main.dir_body_bytes);
        if (venue_main.dir_type != "WorldDir" ||
            world_root.legacy_value != 0 ||
            world_root.legacy_float != 1.0f ||
            world_root.fake_hud_filename !=
                "../../../hud/hud_1p_nocam.milo" ||
            world_root.panel_directory.camera !=
                "fixture_preview.cam" ||
            world_root.panel_directory.test_event != "ui_enter" ||
            world_root.panel_directory.render_directory.test_event !=
                "start" ||
            world_root.panel_directory.render_directory
                    .object_directory.subdirectories !=
                std::vector<std::string>(
                    {"gh1_fixture_chars.milo",
                     "lighting.milo", "crowd.milo"}) ||
            world_root.legacy_transform !=
                std::array<float, 12>{
                    1, 0, 0, 0, 1, 0,
                    0, 0, 1, 0, 0, 0} ||
            gh::milo_object::serialize_world_dir11(world_root) !=
                venue_main.dir_body_bytes) {
            std::fprintf(
                stderr,
                "milo_convert_test: venue WorldDir promotion mismatch\n");
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_convert_test: %s\n", ex.what());
        return 1;
    }
    std::printf("milo_convert_test: all checks passed\n");
    return 0;
}
