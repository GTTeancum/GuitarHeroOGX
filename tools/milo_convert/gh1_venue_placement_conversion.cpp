#include "gh1_venue_placement_conversion.h"

#include "milo_object.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace gh::milo_convert {
namespace {

constexpr uint32_t kObjectTerminator = 0xDEADDEADu;

void set_identity(std::array<float, 12>& transform) {
    transform = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
}

std::array<float, 12> placement_transform(
    const gh::milo_object::Mesh28& mesh,
    const std::string& helper_name) {
    auto transform = mesh.transformable.world;
    for (size_t row = 0; row < 3; ++row) {
        const size_t index = row * 3;
        const float length = std::sqrt(
            transform[index] * transform[index] +
            transform[index + 1] * transform[index + 1] +
            transform[index + 2] * transform[index + 2]);
        if (!std::isfinite(length) || length <= 1.0e-6f)
            throw std::runtime_error(
                "milo convert: degenerate GH1 venue spot " +
                helper_name);
        transform[index] /= length;
        transform[index + 1] /= length;
        transform[index + 2] /= length;
    }
    for (const float value : transform) {
        if (!std::isfinite(value))
            throw std::runtime_error(
                "milo convert: nonfinite GH1 venue spot " +
                helper_name);
    }
    return transform;
}

gh::milo::Entry make_waypoint(
    std::string name, uint32_t flags,
    const std::array<float, 12>& transform,
    std::vector<std::string> connections = {}) {
    gh::milo_object::Waypoint3 waypoint;
    waypoint.flags = flags;
    waypoint.connections = std::move(connections);
    waypoint.transformable.local = transform;
    waypoint.transformable.world = transform;
    gh::milo::Entry entry;
    entry.type = "Waypoint";
    entry.name = std::move(name);
    entry.body_bytes =
        gh::milo_object::serialize_waypoint3(waypoint);
    entry.size = entry.body_bytes.size();
    entry.terminator_value = kObjectTerminator;
    return entry;
}

void finish_directory(gh::milo::Directory& directory) {
    const uint64_t symbol_capacity =
        static_cast<uint64_t>(directory.dir_type.size()) + 1 +
        directory.dir_name.size() + 1 +
        std::accumulate(
            directory.entries.begin(), directory.entries.end(),
            uint64_t{0},
            [](uint64_t total, const gh::milo::Entry& entry) {
                return total + entry.type.size() + 1 +
                       entry.name.size() + 1;
            });
    if (symbol_capacity > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error(
            "milo convert: venue directory string table exceeds u32");
    directory.hash_table_hint =
        static_cast<uint32_t>((directory.entries.size() + 1) * 2);
    directory.string_table_hint =
        static_cast<uint32_t>(symbol_capacity);
    directory.dir_entry_size = directory.dir_body_bytes.size();
}

}  // namespace

Gh2VenuePlacementConversion
convert_gh1_venue_spots_to_gh2_waypoints(
    const std::string& target_venue,
    const std::vector<gh::milo::Directory>& converted_sections) {
    if (target_venue.empty() || converted_sections.empty())
        throw std::runtime_error(
            "milo convert: venue placement input is empty");

    std::map<std::string, gh::milo_object::Mesh28> helpers;
    for (const auto& section : converted_sections) {
        if (section.dir_version != 24 ||
            !section.boundaries_exact)
            throw std::runtime_error(
                "milo convert: venue placement section is not "
                "an exact GH2 directory");
        for (const auto& entry : section.entries) {
            if (entry.type != "Mesh") continue;
            if (entry.name != "stage_spot_01.mesh" &&
                entry.name != "stage_spot_02.mesh" &&
                entry.name != "stage_spot_03.mesh" &&
                entry.name != "walk_spot_01.mesh")
                continue;
            const auto parsed = gh::milo_object::parse_mesh28(
                entry.body_bytes, section.dir_version);
            const auto [found, inserted] =
                helpers.emplace(entry.name, parsed);
            if (!inserted &&
                gh::milo_object::serialize_mesh28(
                    found->second, section.dir_version) !=
                    entry.body_bytes)
                throw std::runtime_error(
                    "milo convert: conflicting GH1 venue spot " +
                    entry.name);
        }
    }

    const auto require_transform =
        [&](const std::string& name) {
            const auto found = helpers.find(name);
            if (found == helpers.end())
                throw std::runtime_error(
                    "milo convert: missing required GH1 venue spot " +
                    name + " in " + target_venue);
            return placement_transform(found->second, name);
        };

    Gh2VenuePlacementConversion result;
    auto& directory = result.characters_directory;
    directory.dir_version = 24;
    directory.dir_type = "RndDir";
    directory.dir_name = target_venue + "_chars";
    directory.boundaries_exact = true;
    directory.dir_terminator_value = kObjectTerminator;

    gh::milo_object::RndDir8 root;
    set_identity(root.transformable.local);
    set_identity(root.transformable.world);
    directory.dir_body_bytes =
        gh::milo_object::serialize_rnd_dir8(root);

    const auto add_placement =
        [&](std::string role, std::string helper,
            std::string waypoint, uint32_t flags,
            std::vector<std::string> connections = {}) {
            const auto target = require_transform(helper);
            directory.entries.push_back(
                make_waypoint(
                    waypoint, flags, target, std::move(connections)));
            Gh2VenuePlacementRecord record;
            record.role = std::move(role);
            record.source_helper = std::move(helper);
            record.target_waypoint = std::move(waypoint);
            record.flags = flags;
            record.source_world =
                helpers.at(record.source_helper).transformable.world;
            record.target_transform = target;
            result.records.push_back(std::move(record));
        };
    constexpr uint32_t kGuitaristStartAndWalk = 65;
    constexpr uint32_t kWalkSpot = 64;
    constexpr float kWalkRouteOffset = 72.0f;
    const auto walk_start = require_transform("walk_spot_01.mesh");
    auto walk_left = walk_start;
    auto walk_right = walk_start;
    for (size_t axis = 0; axis < 3; ++axis) {
        walk_left[9 + axis] -=
            walk_start[axis] * kWalkRouteOffset;
        walk_right[9 + axis] +=
            walk_start[axis] * kWalkRouteOffset;
    }
    add_placement(
        "guitarist0", "walk_spot_01.mesh",
        "start_guitarist0.way", kGuitaristStartAndWalk,
        {"walk_guitarist0_left.way",
         "walk_guitarist0_right.way"});
    add_placement(
        "singer", "stage_spot_01.mesh",
        "start_singer.way", 4);
    add_placement(
        "bassist", "stage_spot_02.mesh",
        "start_bassist.way", 16);
    add_placement(
        "drummer", "stage_spot_03.mesh",
        "start_drummer.way", 32);
    directory.entries.push_back(make_waypoint(
        "walk_guitarist0_left.way", kWalkSpot, walk_left,
        {"start_guitarist0.way",
         "walk_guitarist0_right.way"}));
    directory.entries.push_back(make_waypoint(
        "walk_guitarist0_right.way", kWalkSpot, walk_right,
        {"start_guitarist0.way",
         "walk_guitarist0_left.way"}));

    result.waypoints = directory.entries.size();
    finish_directory(directory);
    return result;
}

void link_gh2_venue_characters_directory(
    gh::milo::Directory& main_directory,
    const std::string& target_venue) {
    if (main_directory.dir_version != 24 ||
        !main_directory.boundaries_exact ||
        target_venue.empty())
        throw std::runtime_error(
            "milo convert: invalid venue characters link input");
    auto root = gh::milo_object::parse_rnd_dir8(
        main_directory.dir_body_bytes);
    const std::string reference =
        target_venue + "_chars.milo";
    if (std::find(
            root.object_directory.subdirectories.begin(),
            root.object_directory.subdirectories.end(),
            reference) ==
        root.object_directory.subdirectories.end())
        root.object_directory.subdirectories.push_back(reference);
    main_directory.dir_body_bytes =
        gh::milo_object::serialize_rnd_dir8(root);
    main_directory.dir_entry_size =
        main_directory.dir_body_bytes.size();
}

void finalize_gh2_venue_world_directory(
    gh::milo::Directory& main_directory,
    const std::string& target_venue,
    const std::vector<std::pair<std::string, std::string>>&
        loaded_sections,
    bool has_start_handler) {
    if (main_directory.dir_version != 24 ||
        main_directory.dir_type != "RndDir" ||
        main_directory.dir_name != target_venue ||
        !main_directory.boundaries_exact ||
        target_venue.empty())
        throw std::runtime_error(
            "milo convert: invalid venue WorldDir promotion input");

    auto render_directory =
        gh::milo_object::parse_rnd_dir8(
            main_directory.dir_body_bytes);
    std::set<std::string> section_names;
    std::set<std::string> directory_names;
    for (const auto& [section, directory] : loaded_sections) {
        if (section.empty() || directory.empty() ||
            !section_names.insert(section).second ||
            !directory_names.insert(directory).second)
            throw std::runtime_error(
                "milo convert: invalid or duplicate venue load_section");
        const std::string reference = directory + ".milo";
        if (std::find(
                render_directory.object_directory.subdirectories.begin(),
                render_directory.object_directory.subdirectories.end(),
                reference) ==
            render_directory.object_directory.subdirectories.end())
            render_directory.object_directory.subdirectories.push_back(
                reference);
    }
    if (has_start_handler)
        render_directory.test_event = "start";
    std::vector<std::string> preview_cameras;
    for (const auto& entry : main_directory.entries)
        if (entry.type == "Cam")
            preview_cameras.push_back(entry.name);
    if (preview_cameras.size() != 1)
        throw std::runtime_error(
            "milo convert: GH1 venue main directory must provide exactly "
            "one authored preview camera");

    gh::milo_object::WorldDir11 world;
    world.legacy_value = 0;
    world.legacy_float = 1.0f;
    world.fake_hud_filename =
        "../../../hud/hud_1p_nocam.milo";
    world.panel_directory.camera = preview_cameras.front();
    world.panel_directory.test_event = "ui_enter";
    world.panel_directory.render_directory =
        std::move(render_directory);
    set_identity(world.legacy_transform);

    main_directory.dir_type = "WorldDir";
    main_directory.dir_body_bytes =
        gh::milo_object::serialize_world_dir11(world);
    finish_directory(main_directory);
    if (gh::milo_object::serialize_world_dir11(
            gh::milo_object::parse_world_dir11(
                main_directory.dir_body_bytes)) !=
        main_directory.dir_body_bytes)
        throw std::runtime_error(
            "milo convert: promoted venue WorldDir does not round trip");
}

}  // namespace gh::milo_convert
