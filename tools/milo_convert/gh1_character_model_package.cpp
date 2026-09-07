#include "gh1_character_model_package.h"

#include "milo_convert.h"
#include "milo_object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace gh::milo_convert {
namespace {

constexpr uint32_t kObjectTerminator = 0xDEADDEADu;

bool equal_ignoring_ieee_zero_sign(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right) {
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i] == right[i])
            continue;
        if (i < 3 ||
            (left[i] ^ right[i]) != 0x80u ||
            left[i - 1] != 0 || right[i - 1] != 0 ||
            left[i - 2] != 0 || right[i - 2] != 0 ||
            left[i - 3] != 0 || right[i - 3] != 0)
            return false;
    }
    return true;
}

gh::milo::Entry make_entry(
    std::string type, std::string name,
    std::vector<uint8_t> body) {
    gh::milo::Entry entry;
    entry.type = std::move(type);
    entry.name = std::move(name);
    entry.size = body.size();
    entry.body_bytes = std::move(body);
    entry.terminator_value = kObjectTerminator;
    return entry;
}

void append_stock_instrument_graph(
    gh::milo::Directory& directory,
    Gh1CharacterRole role) {
    if (role == Gh1CharacterRole::Guitarist) {
        gh::milo_object::OutfitLoader1 outfit;
        outfit.object_fields.type = "guitar";
        outfit.directory = "../../../og";
        gh::milo_object::OutfitLoaderCategory1 category;
        category.outfits.resize(58);
        outfit.categories.push_back(std::move(category));
        directory.entries.push_back(make_entry(
            "OutfitLoader", "guitar.outfit",
            gh::milo_object::serialize_outfit_loader1(outfit)));

        gh::milo_object::CharIKMidi4 fret;
        fret.bone = "bone_fret.mesh";
        directory.entries.push_back(make_entry(
            "CharIKMidi", "fret.ik",
            gh::milo_object::serialize_char_ik_midi4(fret)));

        const auto append_weight =
            [&](const char* name, uint32_t flags) {
                gh::milo_object::CharWeightSetter2 weight;
                weight.weightable.weight = 1.0f;
                weight.weightable.weight_owner = name;
                weight.driver = "main.drv";
                weight.flags = flags;
                directory.entries.push_back(make_entry(
                    "CharWeightSetter", name,
                    gh::milo_object::
                        serialize_char_weight_setter2(weight)));
            };
        append_weight("left.weight", 0x00400000u);
        append_weight("right.weight", 0x00800000u);
    } else if (role == Gh1CharacterRole::Drummer) {
        gh::milo_object::OutfitLoader1 outfit;
        outfit.object_fields.type = "drummer";
        outfit.directory = "../../../og";
        gh::milo_object::OutfitLoaderCategory1 category;
        category.selected = 1;
        category.shown = 1;
        category.outfits.resize(8);
        outfit.categories.push_back(std::move(category));
        directory.entries.push_back(make_entry(
            "OutfitLoader", "drums.outfit",
            gh::milo_object::serialize_outfit_loader1(outfit)));
    }
}

int lod_index(const std::string& name) {
    if (name.size() < 4 ||
        std::tolower(static_cast<unsigned char>(name[0])) != 'l' ||
        std::tolower(static_cast<unsigned char>(name[1])) != 'o' ||
        std::tolower(static_cast<unsigned char>(name[2])) != 'd' ||
        !std::isdigit(static_cast<unsigned char>(name[3])))
        return -1;
    size_t end = 3;
    while (end < name.size() &&
           std::isdigit(static_cast<unsigned char>(name[end])))
        ++end;
    return std::stoi(name.substr(3, end - 3));
}

std::string controller_stem(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

bool starts_with_ascii_case_insensitive(
    const std::string& value,
    const char* prefix) {
    for (size_t index = 0; prefix[index] != '\0'; ++index) {
        if (index >= value.size() ||
            std::tolower(static_cast<unsigned char>(value[index])) !=
                std::tolower(static_cast<unsigned char>(prefix[index])))
            return false;
    }
    return true;
}

bool source_should_strip_character_bone_mesh(
    const std::string& name) {
    // RndUtl::ShouldStrip uses case-insensitive bone_/exo_ prefixes and a
    // case-sensitive spot_ prefix. Character::SyncObjects invokes
    // ConvertBonesToTranses(dir, false) when bone_pelvis.mesh exists.
    return starts_with_ascii_case_insensitive(name, "bone_") ||
           starts_with_ascii_case_insensitive(name, "exo_") ||
           name.compare(0, 5, "spot_") == 0;
}

size_t promote_character_bone_meshes_to_transes(
    gh::milo::Directory& directory) {
    const bool has_pelvis_mesh = std::any_of(
        directory.entries.begin(), directory.entries.end(),
        [](const gh::milo::Entry& entry) {
            return entry.type == "Mesh" &&
                   entry.name == "bone_pelvis.mesh";
        });
    if (!has_pelvis_mesh) return 0;

    size_t promoted = 0;
    for (auto& entry : directory.entries) {
        if (entry.type != "Mesh" ||
            !source_should_strip_character_bone_mesh(entry.name))
            continue;
        const auto mesh =
            gh::milo_object::parse_mesh28(entry.body_bytes);
        gh::milo_object::Trans9 trans;
        trans.object_fields = mesh.object_fields;
        trans.local = mesh.transformable.local;
        trans.world = mesh.transformable.world;
        trans.constraint = mesh.transformable.constraint;
        trans.target = mesh.transformable.target;
        trans.preserve_scale =
            mesh.transformable.preserve_scale;
        trans.parent = mesh.transformable.parent;
        entry.type = "Trans";
        entry.body_bytes =
            gh::milo_object::serialize_trans9(trans);
        entry.size = entry.body_bytes.size();
        ++promoted;
    }
    return promoted;
}

std::string find_eye_mesh(
    const gh::milo::Directory& directory, char side) {
    const std::string side_first =
        std::string(1, side) + "-eye.mesh";
    const std::string eye_first =
        "eye-" + std::string(1, side) + ".mesh";
    std::vector<std::string> matches;
    for (const auto& entry : directory.entries) {
        if (entry.type != "Mesh") continue;
        std::string name = entry.name;
        std::transform(
            name.begin(), name.end(), name.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        if (name == side_first || name == eye_first)
            matches.push_back(entry.name);
    }
    if (matches.size() != 1)
        throw std::runtime_error(
            "milo convert: authored eyes do not resolve one mesh per side");
    return matches.front();
}

float gh1_eye_limit_degrees(float constraint) {
    if (!std::isfinite(constraint) ||
        constraint < -1.0f || constraint > 1.0f)
        throw std::runtime_error(
            "milo convert: GH1 eye constraint is outside cosine range");
    constexpr float kRadiansToDegrees =
        180.0f / 3.14159265358979323846f;
    const float degrees = std::acos(constraint) * kRadiansToDegrees;
    if (degrees > 80.0f)
        throw std::runtime_error(
            "milo convert: GH1 eye cone exceeds GH2 CharLookAt range");
    return degrees;
}

std::array<float, 4> derive_model_sphere(
    const gh::milo::Directory& directory) {
    std::vector<std::array<float, 3>> points;
    for (const auto& entry : directory.entries) {
        if (entry.type != "Mesh") continue;
        const auto mesh =
            gh::milo_object::parse_mesh28(entry.body_bytes);
        const auto& transform = mesh.transformable.world;
        for (const auto& vertex : mesh.vertices) {
            const auto& value = vertex.position;
            points.push_back({
                value[0] * transform[0] +
                    value[1] * transform[3] +
                    value[2] * transform[6] + transform[9],
                value[0] * transform[1] +
                    value[1] * transform[4] +
                    value[2] * transform[7] + transform[10],
                value[0] * transform[2] +
                    value[1] * transform[5] +
                    value[2] * transform[8] + transform[11],
            });
        }
    }
    if (points.empty())
        throw std::runtime_error(
            "milo convert: character has no geometry for auto sphere");
    std::array<float, 3> minimum = points.front();
    std::array<float, 3> maximum = points.front();
    for (const auto& point : points)
        for (size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] =
                std::min(minimum[axis], point[axis]);
            maximum[axis] =
                std::max(maximum[axis], point[axis]);
        }
    std::array<float, 4> sphere = {
        (minimum[0] + maximum[0]) * 0.5f,
        (minimum[1] + maximum[1]) * 0.5f,
        (minimum[2] + maximum[2]) * 0.5f,
        0.0f,
    };
    for (const auto& point : points) {
        const float x = point[0] - sphere[0];
        const float y = point[1] - sphere[1];
        const float z = point[2] - sphere[2];
        sphere[3] =
            std::max(
                sphere[3],
                std::sqrt(x * x + y * y + z * z));
    }
    return sphere;
}

const Gh2CharacterAnimationBinding& require_animation(
    const Gh1CharacterModelBuildInput& input,
    Gh2ClipSetRole role) {
    const auto found = std::find_if(
        input.animations.begin(), input.animations.end(),
        [&](const Gh2CharacterAnimationBinding& binding) {
            return binding.role == role &&
                   binding.source_archetype ==
                       input.spec.compiled_skeleton;
        });
    if (found == input.animations.end())
        throw std::runtime_error(
            "milo convert: character lacks authored animation binding for " +
            std::string(gh2_clip_set_role_name(role)));
    if (found->relative_path.empty())
        throw std::runtime_error(
            "milo convert: character animation binding path is empty");
    return *found;
}

Gh2ClipSetRole hand_driver_role(
    const Gh1CharacterSpec& spec,
    const Gh1CharacterControllerSpec& driver) {
    const std::string stem = controller_stem(driver.name);
    const auto hand = std::find_if(
        spec.controllers.begin(), spec.controllers.end(),
        [&](const Gh1CharacterControllerSpec& controller) {
            return controller.kind ==
                       Gh1CharacterControllerKind::HandIk &&
                   controller_stem(controller.name) == stem;
        });
    if (hand == spec.controllers.end())
        return Gh2ClipSetRole::Generic;
    if (hand->destination == "bone_fret_hand.mesh")
        return Gh2ClipSetRole::GuitarFret;
    if (hand->destination == "bone_strum_hand.mesh")
        return Gh2ClipSetRole::GuitarStrum;
    throw std::runtime_error(
        "milo convert: hand driver destination is not a GH2 anchor");
}

std::array<float, 3> row(
    const std::array<float, 12>& transform, size_t index) {
    return {
        transform[index * 3],
        transform[index * 3 + 1],
        transform[index * 3 + 2]};
}

std::array<float, 3> translation(
    const std::array<float, 12>& transform) {
    return {transform[9], transform[10], transform[11]};
}

std::array<float, 3> interpolate(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right, float amount) {
    return {
        left[0] + (right[0] - left[0]) * amount,
        left[1] + (right[1] - left[1]) * amount,
        left[2] + (right[2] - left[2]) * amount};
}

std::array<float, 3> subtract(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) {
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2]};
}

std::array<float, 3> cross(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) {
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]};
}

std::array<float, 3> normalize(
    const std::array<float, 3>& value) {
    const float length = std::sqrt(
        value[0] * value[0] +
        value[1] * value[1] +
        value[2] * value[2]);
    if (length <= std::numeric_limits<float>::epsilon())
        throw std::runtime_error(
            "milo convert: cannot normalize zero-length rod axis");
    return {
        value[0] / length,
        value[1] / length,
        value[2] / length};
}

std::array<float, 12> multiply_transform(
    const std::array<float, 12>& left,
    const std::array<float, 12>& right) {
    std::array<float, 12> result{};
    for (size_t r = 0; r < 3; ++r)
        for (size_t c = 0; c < 3; ++c)
            for (size_t k = 0; k < 3; ++k)
                result[r * 3 + c] +=
                    left[r * 3 + k] *
                    right[k * 3 + c];
    for (size_t c = 0; c < 3; ++c)
        result[9 + c] =
            left[9] * right[c] +
            left[10] * right[3 + c] +
            left[11] * right[6 + c] +
            right[9 + c];
    return result;
}

std::array<float, 12> invert_transform(
    const std::array<float, 12>& value) {
    const float determinant =
        value[0] * (value[4] * value[8] -
                    value[5] * value[7]) -
        value[1] * (value[3] * value[8] -
                    value[5] * value[6]) +
        value[2] * (value[3] * value[7] -
                    value[4] * value[6]);
    if (std::abs(determinant) <=
        std::numeric_limits<float>::epsilon())
        throw std::runtime_error(
            "milo convert: cannot invert singular rod frame");
    const float inverse_determinant = 1.0f / determinant;
    std::array<float, 12> result{};
    result[0] = (value[4] * value[8] -
                 value[5] * value[7]) * inverse_determinant;
    result[1] = (value[2] * value[7] -
                 value[1] * value[8]) * inverse_determinant;
    result[2] = (value[1] * value[5] -
                 value[2] * value[4]) * inverse_determinant;
    result[3] = (value[5] * value[6] -
                 value[3] * value[8]) * inverse_determinant;
    result[4] = (value[0] * value[8] -
                 value[2] * value[6]) * inverse_determinant;
    result[5] = (value[2] * value[3] -
                 value[0] * value[5]) * inverse_determinant;
    result[6] = (value[3] * value[7] -
                 value[4] * value[6]) * inverse_determinant;
    result[7] = (value[1] * value[6] -
                 value[0] * value[7]) * inverse_determinant;
    result[8] = (value[0] * value[4] -
                 value[1] * value[3]) * inverse_determinant;
    for (size_t c = 0; c < 3; ++c)
        result[9 + c] = -(
            value[9] * result[c] +
            value[10] * result[3 + c] +
            value[11] * result[6 + c]);
    return result;
}

size_t validate_upper_twist_sibling_hierarchy_for_gh2(
    const gh::milo::Directory& directory,
    const std::vector<Gh1CharacterControllerSpec>& controllers) {
    size_t validated = 0;
    for (const auto& controller : controllers) {
        if (controller.kind != Gh1CharacterControllerKind::UpperTwist)
            continue;
        if (controller.bones.size() != 3)
            throw std::runtime_error(
                "milo convert: upper-twist requires three authored bones");

        const auto find_trans =
            [&](const std::string& name) -> const gh::milo::Entry* {
                const auto found = std::find_if(
                    directory.entries.begin(),
                    directory.entries.end(),
                    [&](const gh::milo::Entry& entry) {
                        return entry.type == "Trans" &&
                               entry.name == name;
                    });
                return found == directory.entries.end()
                           ? nullptr
                           : &*found;
            };
        const gh::milo::Entry* twist1_entry =
            find_trans(controller.bones[0]);
        const gh::milo::Entry* twist2_entry =
            find_trans(controller.bones[1]);
        const gh::milo::Entry* upper_arm_entry =
            find_trans(controller.bones[2]);
        if (!twist1_entry || !twist2_entry ||
            !upper_arm_entry) {
            throw std::runtime_error(
                "milo convert: upper-twist native transform missing");
        }

        const auto twist1 =
            gh::milo_object::parse_trans9(
                twist1_entry->body_bytes);
        const auto twist2 =
            gh::milo_object::parse_trans9(
                twist2_entry->body_bytes);
        const auto upper_arm =
            gh::milo_object::parse_trans9(
                upper_arm_entry->body_bytes);

        // GH1 AnimServoUpperTwist and stock GH2 CharUpperTwist both author
        // twist1, twist2, and upperArm as siblings under the same parent.
        // GH2's local-row poll writes each helper in that shared parent space;
        // changing the hierarchy composes twist2 through twist1 a second time.
        if (twist1.parent.empty() ||
            twist1.parent != twist2.parent ||
            twist1.parent != upper_arm.parent) {
            throw std::runtime_error(
                "milo convert: upper-twist source graph is not the "
                "shared GH1/GH2 sibling contract");
        }
        ++validated;
    }
    return validated;
}

const gh::milo_object::Mesh28& transform_mesh(
    const gh::milo::Directory& directory,
    const std::string& name,
    std::map<std::string, gh::milo_object::Mesh28>& cache) {
    const auto cached = cache.find(name);
    if (cached != cache.end())
        return cached->second;
    const auto entry = std::find_if(
        directory.entries.begin(), directory.entries.end(),
        [&](const gh::milo::Entry& candidate) {
            return candidate.type == "Mesh" &&
                   candidate.name == name;
        });
    if (entry == directory.entries.end())
        throw std::runtime_error(
            "milo convert: rod transform mesh missing: " + name);
    return cache.emplace(
        name,
        gh::milo_object::parse_mesh28(entry->body_bytes)).first->second;
}

std::array<float, 12> compute_rod_offset(
    const gh::milo::Directory& directory,
    const Gh1CharacterControllerSpec& controller,
    float destination_position) {
    std::map<std::string, gh::milo_object::Mesh28> meshes;
    const auto& left = transform_mesh(
        directory, controller.rig_bones[0].bone, meshes);
    const auto& right = transform_mesh(
        directory, controller.rig_bones[1].bone, meshes);
    const auto& destination = transform_mesh(
        directory, controller.destinations.front(), meshes);
    std::array<float, 12> rod{};
    auto x = controller.vertical
                 ? std::array<float, 3>{0.0f, 0.0f, -1.0f}
                 : normalize(interpolate(
                       row(left.transformable.world, 0),
                       row(right.transformable.world, 0),
                       destination_position));
    auto z = controller.side_axis.empty()
                 ? subtract(
                       translation(left.transformable.world),
                       translation(right.transformable.world))
                 : row(
                       transform_mesh(
                           directory, controller.side_axis, meshes)
                           .transformable.world,
                       2);
    const auto y = normalize(cross(z, x));
    z = cross(x, y);
    for (size_t i = 0; i < 3; ++i) {
        rod[i] = x[i];
        rod[3 + i] = y[i];
        rod[6 + i] = z[i];
    }
    const auto position = interpolate(
        translation(left.transformable.world),
        translation(right.transformable.world),
        destination_position);
    rod[9] = position[0];
    rod[10] = position[1];
    rod[11] = position[2];
    return multiply_transform(
        destination.transformable.world,
        invert_transform(rod));
}

void append_controller(
    gh::milo::Directory& directory,
    const Gh1CharacterControllerSpec& controller) {
    using Kind = Gh1CharacterControllerKind;
    if (controller.kind == Kind::ForeTwist) {
        if (controller.bones.size() != 4)
            throw std::runtime_error(
                "milo convert: fore-twist requires four authored bones");
        gh::milo_object::CharForeTwist4 target;
        target.offset = controller.offset;
        target.hand = controller.bones[3];
        target.twist2 = controller.bones[2];
        directory.entries.push_back(make_entry(
            "CharForeTwist", controller.name,
            gh::milo_object::serialize_char_fore_twist4(target)));
    } else if (controller.kind == Kind::UpperTwist) {
        if (controller.bones.size() != 3)
            throw std::runtime_error(
                "milo convert: upper-twist requires three authored bones");
        gh::milo_object::CharUpperTwist1 target;
        target.twist1 = controller.bones[0];
        target.twist2 = controller.bones[1];
        target.upper_arm = controller.bones[2];
        directory.entries.push_back(make_entry(
            "CharUpperTwist", controller.name,
            gh::milo_object::serialize_char_upper_twist1(target)));
    } else if (controller.kind == Kind::HandIk) {
        gh::milo_object::CharIKHand2 target;
        target.hand = controller.source;
        target.target = controller.destination;
        target.orientation = controller.align_quaternion;
        target.stretch = controller.stretch;
        directory.entries.push_back(make_entry(
            "CharIKHand", controller.name,
            gh::milo_object::serialize_char_ik_hand2(target)));
    } else if (controller.kind == Kind::Rig) {
        if (controller.rig_bones.size() != 2 ||
            controller.destinations.size() != 1)
            throw std::runtime_error(
                "milo convert: servo rig requires two weighted bones "
                "and one destination");
        const float total =
            controller.rig_bones[0].weight +
            controller.rig_bones[1].weight;
        if (std::abs(total) <=
            std::numeric_limits<float>::epsilon())
            throw std::runtime_error(
                "milo convert: servo rig weights sum to zero");
        gh::milo_object::CharIKRod2 target;
        target.left_end = controller.rig_bones[0].bone;
        target.right_end = controller.rig_bones[1].bone;
        target.dest_pos =
            controller.rig_bones[1].weight / total;
        target.side_axis = controller.side_axis;
        target.vertical = controller.vertical;
        target.dest = controller.destinations.front();
        target.transform = compute_rod_offset(
            directory, controller, target.dest_pos);
        directory.entries.push_back(make_entry(
            "CharIKRod", controller.name,
            gh::milo_object::serialize_char_ik_rod2(target)));
    } else if (controller.kind == Kind::PosConstraint) {
        if (controller.destinations.size() != 1)
            throw std::runtime_error(
                "milo convert: position constraint requires one source");
        gh::milo_object::CharPosConstraint2 target;
        target.targets = controller.sources;
        target.source = controller.destinations.front();
        target.box_min = {1.0f, 1.0f, 0.0f};
        target.box_max = {-1.0f, -1.0f, 1000.0f};
        directory.entries.push_back(make_entry(
            "CharPosConstraint", controller.name,
            gh::milo_object::serialize_char_pos_constraint2(target)));
    }
}

bool matching_morph_topology(
    const gh::milo_object::Mesh28& reference,
    const gh::milo_object::Mesh28& target) {
    return reference.vertices.size() == target.vertices.size() &&
           reference.faces == target.faces;
}

void merge_face_model(
    gh::milo::Directory& target,
    const gh::milo::Directory& source,
    const std::string& package_name) {
    const auto converted =
        convert_gh1_directory_to_gh2_rnddir(
            source, package_name + "_face");
    if (!converted.complete)
        throw std::runtime_error(
            "milo convert: character face conversion incomplete");

    std::map<std::string, gh::milo_object::Mesh28> face_meshes;
    std::map<std::string, gh::milo_object::Morph4> morphs;
    for (const auto& entry : converted.directory.entries) {
        if (entry.type == "Mesh")
            face_meshes.emplace(
                entry.name,
                gh::milo_object::parse_mesh28(entry.body_bytes));
        else if (entry.type == "Morph")
            morphs.emplace(
                entry.name,
                gh::milo_object::parse_morph4(entry.body_bytes));
        else
            throw std::runtime_error(
                "milo convert: unsupported character face object: " +
                entry.type);
    }
    if (morphs.empty())
        throw std::runtime_error(
            "milo convert: character face has no morphs");

    std::map<std::string, std::string> renamed_meshes;
    for (const auto& [name, morph] : morphs)
        for (const auto& pose : morph.poses) {
            if (face_meshes.find(pose.mesh) == face_meshes.end())
                throw std::runtime_error(
                    "milo convert: face morph pose mesh missing: " +
                    pose.mesh);
            renamed_meshes.emplace(
                pose.mesh,
                package_name + "_face__" + pose.mesh);
        }

    for (auto& [name, morph] : morphs) {
        std::vector<std::string> reference_poses;
        for (const auto& pose : morph.poses) {
            const auto key = std::find_if(
                pose.keys.begin(), pose.keys.end(),
                [](const gh::milo_object::MorphKey& candidate) {
                    return candidate.frame == 0.0f &&
                           candidate.value == 1.0f;
                });
            if (key != pose.keys.end())
                reference_poses.push_back(pose.mesh);
        }
        if (reference_poses.size() != 1)
            throw std::runtime_error(
                "milo convert: face morph has no unique frame-zero basis");
        const auto& reference =
            face_meshes.at(reference_poses.front());
        std::vector<std::string> target_meshes;
        std::vector<std::string> size_candidates;
        for (const auto& entry : target.entries) {
            if (entry.type != "Mesh")
                continue;
            const auto candidate =
                gh::milo_object::parse_mesh28(entry.body_bytes);
            if (reference.vertices.size() ==
                    candidate.vertices.size() &&
                reference.faces.size() ==
                    candidate.faces.size())
                size_candidates.push_back(
                    entry.name +
                    (reference.faces == candidate.faces
                         ? ":topology"
                         : ":counts"));
            if (matching_morph_topology(reference, candidate))
                target_meshes.push_back(entry.name);
        }
        if (target_meshes.size() != 1)
            throw std::runtime_error(
                "milo convert: face morph requires one geometry target: " +
                name + " has " +
                std::to_string(target_meshes.size()) +
                " size candidates=" +
                std::accumulate(
                    size_candidates.begin(),
                    size_candidates.end(), std::string{},
                    [](std::string value, const std::string& candidate) {
                        if (!value.empty()) value += ",";
                        return value + candidate;
                    }));
        morph.target = target_meshes.front();
        for (auto& pose : morph.poses)
            pose.mesh = renamed_meshes.at(pose.mesh);
    }

    for (const auto& [name, source_mesh] : face_meshes) {
        const auto renamed = renamed_meshes.find(name);
        if (renamed == renamed_meshes.end())
            continue;
        auto mesh = source_mesh;
        // GH1 keeps facial pose meshes in a separate face RndDir.  They are
        // geometry donors for RndMorph and are never members of the
        // character's drawable LOD groups.  Merging that directory into the
        // GH2 character package changes the ownership boundary, so retaining
        // the source RndDrawable::showing bit would make every pose donor a
        // live render mesh until a morph happened to touch it.  Keep the
        // authored pose geometry intact, but make its donor-only role
        // explicit in the combined package.
        mesh.drawable.showing = false;
        const auto parent = renamed_meshes.find(
            mesh.transformable.parent);
        if (parent != renamed_meshes.end())
            mesh.transformable.parent = parent->second;
        const auto owner = renamed_meshes.find(mesh.geometry_owner);
        if (owner != renamed_meshes.end())
            mesh.geometry_owner = owner->second;
        for (auto& slot : mesh.bone_slots) {
            const auto bone = renamed_meshes.find(slot.bone);
            if (bone != renamed_meshes.end())
                slot.bone = bone->second;
        }
        target.entries.push_back(make_entry(
            "Mesh", renamed->second,
            gh::milo_object::serialize_mesh28(mesh)));
    }
    for (const auto& [name, morph] : morphs)
        target.entries.push_back(make_entry(
            "Morph", package_name + "_face__" + name,
            gh::milo_object::serialize_morph4(morph)));
}

std::string face_pose_label(
    const std::string& mesh,
    const std::string& face_prefix) {
    if (mesh.compare(0, face_prefix.size(), face_prefix) != 0)
        throw std::runtime_error(
            "milo convert: face pose mesh is outside package namespace: " +
            mesh);
    const std::string tail = mesh.substr(face_prefix.size());
    const size_t dot = tail.find('.');
    if (dot == std::string::npos || dot == 0)
        throw std::runtime_error(
            "milo convert: face pose mesh has no authored label: " +
            mesh);
    return tail.substr(0, dot);
}

std::vector<gh::milo_object::MorphKey>
half_cosine_face_keys(
    float duration_frames,
    bool destination) {
    if (!(duration_frames > 0.0f) ||
        !std::isfinite(duration_frames))
        throw std::runtime_error(
            "milo convert: face blend duration is not positive");
    constexpr float kPi = 3.14159265358979323846f;
    std::vector<float> frames;
    for (float frame = 0.0f;
         frame < duration_frames; frame += 1.0f)
        frames.push_back(frame);
    if (frames.empty() ||
        frames.back() != duration_frames)
        frames.push_back(duration_frames);
    std::vector<gh::milo_object::MorphKey> keys;
    keys.reserve(frames.size());
    for (const float frame : frames) {
        const float t = frame / duration_frames;
        const float weight =
            0.5f - 0.5f * std::cos(kPi * t);
        keys.push_back(
            {destination ? weight : 1.0f - weight, frame});
    }
    return keys;
}

size_t append_face_transition_graph(
    gh::milo::Directory& directory,
    const Gh1CharacterFaceSpec& spec,
    const std::string& package_name,
    std::string& controller_type) {
    if (!spec.present || spec.poses.empty())
        throw std::runtime_error(
            "milo convert: face package lacks authored face contract");
    const std::string face_prefix =
        package_name + "_face__";
    struct SourceMorph {
        std::string name;
        gh::milo_object::Morph4 morph;
        std::map<std::string, gh::milo_object::MorphPose> poses;
    };
    std::vector<SourceMorph> morphs;
    for (const auto& entry : directory.entries) {
        if (entry.type != "Morph" ||
            entry.name.compare(
                0, face_prefix.size(), face_prefix) != 0)
            continue;
        SourceMorph source;
        source.name = entry.name;
        source.morph =
            gh::milo_object::parse_morph4(entry.body_bytes);
        for (const auto& pose : source.morph.poses) {
            const std::string label =
                face_pose_label(pose.mesh, face_prefix);
            if (!source.poses.emplace(label, pose).second)
                throw std::runtime_error(
                    "milo convert: duplicate face pose label " +
                    label + " in " + entry.name);
        }
        for (const auto& pose : spec.poses)
            if (source.poses.find(pose) == source.poses.end())
                throw std::runtime_error(
                    "milo convert: face morph " + entry.name +
                    " lacks authored pose " + pose);
        morphs.push_back(std::move(source));
    }
    if (morphs.empty())
        throw std::runtime_error(
            "milo convert: merged face package has no morphs");

    const float duration_frames = spec.blend_time * 30.0f;
    const auto previous_keys =
        half_cosine_face_keys(duration_frames, false);
    const auto destination_keys =
        half_cosine_face_keys(duration_frames, true);
    size_t transition_count = 0;
    for (size_t from = 0; from < spec.poses.size(); ++from)
        for (size_t to = 0; to < spec.poses.size(); ++to) {
            const std::string stem =
                "gh1_face_" + std::to_string(from) +
                "_" + std::to_string(to);
            std::vector<std::string> pair_morph_names;
            pair_morph_names.reserve(morphs.size());
            for (size_t index = 0; index < morphs.size(); ++index) {
                auto pair = morphs[index].morph;
                pair.animatable.frame = 0.0f;
                pair.poses.clear();
                auto previous =
                    morphs[index].poses.at(spec.poses[from]);
                previous.keys =
                    from == to
                        ? std::vector<gh::milo_object::MorphKey>{
                              {1.0f, 0.0f},
                              {1.0f, duration_frames}}
                        : previous_keys;
                pair.poses.push_back(std::move(previous));
                if (from != to) {
                    auto destination =
                        morphs[index].poses.at(spec.poses[to]);
                    destination.keys = destination_keys;
                    pair.poses.push_back(
                        std::move(destination));
                }
                const std::string name =
                    stem + "_" + std::to_string(index) +
                    ".mrf";
                pair_morph_names.push_back(name);
                directory.entries.push_back(make_entry(
                    "Morph", name,
                    gh::milo_object::serialize_morph4(pair)));
            }

            gh::milo_object::Group12 group;
            group.objects = pair_morph_names;
            directory.entries.push_back(make_entry(
                "Group", stem + ".grp",
                gh::milo_object::serialize_group12(group)));

            gh::milo_object::AnimFilter1 filter;
            filter.anim = stem + ".grp";
            filter.start = 0.0f;
            filter.end = duration_frames;
            filter.period = spec.blend_time;
            directory.entries.push_back(make_entry(
                "AnimFilter", stem + ".filt",
                gh::milo_object::serialize_anim_filter1(filter)));

            gh::milo_object::EventTrigger8 trigger;
            trigger.animations.push_back(
                {stem + ".filt", 0.0f, false, 0.0f});
            directory.entries.push_back(make_entry(
                "EventTrigger", stem + ".trig",
                gh::milo_object::serialize_event_trigger8(trigger)));
            ++transition_count;
        }

    gh::milo_object::Group12 controller;
    if (spec.event_list == "hero")
        controller_type = "gh1_guitarist_morph_face";
    else if (spec.event_list == "singer")
        controller_type = "gh1_singer_morph_face";
    else
        throw std::runtime_error(
            "milo convert: unsupported face controller event list " +
            spec.event_list);
    controller.object_fields.type = controller_type;
    directory.entries.push_back(make_entry(
        "Group", "lip.servo",
        gh::milo_object::serialize_group12(controller)));
    return transition_count;
}

std::string merge_shadow_model(
    gh::milo::Directory& target,
    const gh::milo::Directory& source,
    const std::string& package_name) {
    const auto converted =
        convert_gh1_directory_to_gh2_rnddir(
            source, package_name + "_shadow");
    if (!converted.complete)
        throw std::runtime_error(
            "milo convert: character shadow conversion incomplete");
    const std::string shadow_group_name =
        package_name + "_shadow.grp";
    std::map<std::string, gh::milo_object::Group12> groups;
    for (const auto& entry : converted.directory.entries)
        if (entry.type == "Group")
            groups.emplace(
                entry.name,
                gh::milo_object::parse_group12(entry.body_bytes));
    if (groups.empty())
        throw std::runtime_error(
            "milo convert: converted character shadow has no groups");
    std::set<std::string> referenced_groups;
    for (const auto& [name, group] : groups) {
        for (const auto& object : group.objects)
            if (groups.find(object) != groups.end())
                referenced_groups.insert(object);
        if (!group.draw_only.empty() &&
            groups.find(group.draw_only) != groups.end())
            referenced_groups.insert(group.draw_only);
    }
    std::vector<std::string> root_groups;
    for (const auto& [name, group] : groups)
        if (referenced_groups.find(name) ==
            referenced_groups.end())
            root_groups.push_back(name);
    if (root_groups.size() != 1)
        throw std::runtime_error(
            "milo convert: character shadow requires one root group: " +
            package_name + " has " +
            std::to_string(root_groups.size()));

    std::set<std::string> reachable_groups;
    std::set<std::string> drawable_names;
    std::vector<std::string> pending{root_groups.front()};
    while (!pending.empty()) {
        const std::string name = pending.back();
        pending.pop_back();
        if (!reachable_groups.insert(name).second)
            continue;
        for (const auto& object : groups.at(name).objects) {
            if (groups.find(object) != groups.end()) {
                pending.push_back(object);
                continue;
            }
            const auto drawable = std::find_if(
                converted.directory.entries.begin(),
                converted.directory.entries.end(),
                [&](const gh::milo::Entry& entry) {
                    return entry.type == "Mesh" &&
                           entry.name == object;
                });
            if (drawable == converted.directory.entries.end())
                throw std::runtime_error(
                    "milo convert: shadow group has unsupported object: " +
                    object);
            drawable_names.insert(object);
        }
    }
    std::map<std::string, std::string> renamed_groups;
    for (const auto& name : reachable_groups)
        renamed_groups.emplace(
            name,
            name == root_groups.front()
                ? shadow_group_name
                : package_name + "_shadow__" + name);
    std::map<std::string, std::string> shadow_drawables;
    for (const auto& object : drawable_names)
        shadow_drawables.emplace(
            object, package_name + "_shadow__" + object);

    for (const auto& converted_entry : converted.directory.entries) {
        auto entry = converted_entry;
        if (entry.type == "Group") {
            const auto renamed =
                renamed_groups.find(entry.name);
            if (renamed == renamed_groups.end())
                continue;
            auto group = groups.at(entry.name);
            for (auto& object : group.objects) {
                const auto nested = renamed_groups.find(object);
                if (nested != renamed_groups.end())
                    object = nested->second;
                else
                    object = shadow_drawables.at(object);
            }
            const auto parent =
                shadow_drawables.find(
                    group.transformable.parent);
            if (parent != shadow_drawables.end())
                group.transformable.parent = parent->second;
            const auto parent_group =
                renamed_groups.find(group.transformable.parent);
            if (parent_group != renamed_groups.end())
                group.transformable.parent = parent_group->second;
            entry.name = renamed->second;
            entry.body_bytes =
                gh::milo_object::serialize_group12(group);
            entry.size = entry.body_bytes.size();
        } else if (entry.type == "Mesh") {
            const auto renamed =
                shadow_drawables.find(entry.name);
            if (renamed == shadow_drawables.end())
                continue;
            auto mesh =
                gh::milo_object::parse_mesh28(entry.body_bytes);
            const auto parent =
                shadow_drawables.find(
                    mesh.transformable.parent);
            if (parent != shadow_drawables.end())
                mesh.transformable.parent = parent->second;
            const auto parent_group =
                renamed_groups.find(mesh.transformable.parent);
            if (parent_group != renamed_groups.end())
                mesh.transformable.parent = parent_group->second;
            const auto owner =
                shadow_drawables.find(mesh.geometry_owner);
            if (owner != shadow_drawables.end())
                mesh.geometry_owner = owner->second;
            for (auto& slot : mesh.bone_slots) {
                const auto bone =
                    shadow_drawables.find(slot.bone);
                if (bone != shadow_drawables.end())
                    slot.bone = bone->second;
            }
            entry.name = renamed->second;
            entry.body_bytes =
                gh::milo_object::serialize_mesh28(mesh);
            entry.size = entry.body_bytes.size();
        }
        const auto existing = std::find_if(
            target.entries.begin(), target.entries.end(),
            [&](const gh::milo::Entry& candidate) {
                return candidate.name == entry.name;
            });
        if (existing == target.entries.end()) {
            target.entries.push_back(entry);
            continue;
        }
        if (existing->type == entry.type &&
            equal_ignoring_ieee_zero_sign(
                existing->body_bytes, entry.body_bytes))
            continue;
        throw std::runtime_error(
            "milo convert: shadow object collision differs: " +
            entry.name + " (" + existing->type + "/" +
            entry.type + ") in " + package_name);
    }
    return shadow_group_name;
}

size_t validate_character_references_impl(
    const gh::milo::Directory& directory) {
    std::set<std::string> names;
    for (const auto& entry : directory.entries)
        if (!names.insert(entry.name).second)
            throw std::runtime_error(
                "milo convert: duplicate character object name: " +
                entry.name);
    size_t count = 0;
    const auto require =
        [&](const std::string& reference, const std::string& context) {
            if (reference.empty())
                return;
            ++count;
            if (reference == directory.dir_name ||
                names.find(reference) != names.end())
                return;
            throw std::runtime_error(
                "milo convert: dangling character reference " +
                reference + " from " + context);
        };
    const auto require_transformable =
        [&](const gh::milo_object::Transformable9& transformable,
            const std::string& context) {
            require(transformable.target, context + ".target");
            require(transformable.parent, context + ".parent");
        };
    const auto require_render_directory =
        [&](const gh::milo_object::RndDir8& render_directory,
            const std::string& context) {
            require_transformable(
                render_directory.transformable, context);
            require(
                render_directory.environment,
                context + ".environment");
            require(
                render_directory.object_directory.legacy_camera,
                context + ".legacy_camera");
        };

    if (directory.dir_type == "RndDir") {
        require_render_directory(
            gh::milo_object::parse_rnd_dir8(
                directory.dir_body_bytes),
            "render directory root");
    } else if (directory.dir_type == "WorldDir") {
        const auto world =
            gh::milo_object::parse_world_dir11(
                directory.dir_body_bytes);
        require_render_directory(
            world.panel_directory.render_directory,
            "world directory root");
        require(
            world.panel_directory.camera,
            "world directory root.camera");
    } else {
        gh::milo_object::Character9 character;
        if (directory.dir_type == "BandCharacter")
        character =
            gh::milo_object::parse_band_character1(
                directory.dir_body_bytes).character;
        else if (directory.dir_type == "Character")
            character =
                gh::milo_object::parse_character9(
                    directory.dir_body_bytes);
        else
            throw std::runtime_error(
                "milo convert: reference audit root type invalid");
        require_render_directory(
            character.render_directory, "character root");
        for (const auto& lod : character.lods)
            require(lod.group, "character LOD");
        require(character.shadow, "character shadow");
        require(character.sphere_base, "character sphere base");
    }

    for (const auto& entry : directory.entries) {
        if (entry.type == "Mesh") {
            const auto mesh =
                gh::milo_object::parse_mesh28(entry.body_bytes);
            require_transformable(mesh.transformable, entry.name);
            require(mesh.material, entry.name + ".material");
            require(
                mesh.geometry_owner,
                entry.name + ".geometry_owner");
            if (mesh.has_bones)
                for (const auto& slot : mesh.bone_slots)
                    require(slot.bone, entry.name + ".bone");
        } else if (entry.type == "Mat") {
            const auto material =
                gh::milo_object::parse_mat27(entry.body_bytes);
            require(
                material.diffuse_texture,
                entry.name + ".diffuse_texture");
            require(material.next_pass, entry.name + ".next_pass");
            require(material.normal_map, entry.name + ".normal_map");
            require(material.emissive_map, entry.name + ".emissive_map");
            require(material.specular_map, entry.name + ".specular_map");
            require(
                material.legacy_unknown_map,
                entry.name + ".legacy_unknown_map");
            require(
                material.environment_map,
                entry.name + ".environment_map");
            require(material.fur, entry.name + ".fur");
        } else if (entry.type == "Group") {
            const auto group =
                gh::milo_object::parse_group12(entry.body_bytes);
            require_transformable(group.transformable, entry.name);
            for (const auto& object : group.objects)
                require(object, entry.name + ".objects");
            require(group.environment, entry.name + ".environment");
            require(group.lod, entry.name + ".lod");
        } else if (entry.type == "Trans") {
            const auto transform =
                gh::milo_object::parse_trans9(entry.body_bytes);
            require(transform.target, entry.name + ".target");
            require(transform.parent, entry.name + ".parent");
        } else if (entry.type == "TransAnim") {
            const auto animation =
                gh::milo_object::parse_trans_anim6(entry.body_bytes);
            require(animation.target, entry.name + ".target");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "Cam") {
            const auto camera =
                gh::milo_object::parse_cam12(entry.body_bytes);
            require_transformable(camera.transformable, entry.name);
            require(
                camera.target_texture,
                entry.name + ".target_texture");
        } else if (entry.type == "CamAnim") {
            const auto animation =
                gh::milo_object::parse_cam_anim2(entry.body_bytes);
            require(animation.camera, entry.name + ".camera");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "Flare") {
            const auto flare =
                gh::milo_object::parse_flare4(entry.body_bytes);
            require_transformable(flare.transformable, entry.name);
            require(flare.material, entry.name + ".material");
        } else if (entry.type == "Light") {
            const auto light =
                gh::milo_object::parse_light6(entry.body_bytes);
            require_transformable(light.transformable, entry.name);
        } else if (entry.type == "Environ") {
            const auto environment =
                gh::milo_object::parse_environ5(entry.body_bytes);
            for (const auto& light : environment.lights)
                require(light, entry.name + ".lights");
        } else if (entry.type == "EnvAnim") {
            const auto animation =
                gh::milo_object::parse_env_anim4(entry.body_bytes);
            require(
                animation.environment,
                entry.name + ".environment");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "LightAnim") {
            const auto animation =
                gh::milo_object::parse_light_anim2(entry.body_bytes);
            require(animation.light, entry.name + ".light");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "MeshAnim") {
            const auto animation =
                gh::milo_object::parse_mesh_anim1(entry.body_bytes);
            require(animation.mesh, entry.name + ".mesh");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "MatAnim") {
            const auto animation =
                gh::milo_object::parse_mat_anim7(entry.body_bytes);
            require(animation.material, entry.name + ".material");
            require(animation.keys_owner, entry.name + ".keys_owner");
            for (const auto& key : animation.texture_keys)
                require(key.object, entry.name + ".texture_keys");
        } else if (entry.type == "MultiMesh") {
            const auto meshes =
                gh::milo_object::parse_multi_mesh1(entry.body_bytes);
            require(meshes.mesh, entry.name + ".mesh");
        } else if (entry.type == "Movie") {
            const auto movie =
                gh::milo_object::parse_movie8(entry.body_bytes);
            require(movie.texture, entry.name + ".texture");
        } else if (entry.type == "ParticleSys") {
            const auto particles =
                gh::milo_object::parse_particle_sys27(
                    entry.body_bytes);
            require_transformable(particles.transformable, entry.name);
            require(particles.bounce, entry.name + ".bounce");
            require(particles.material, entry.name + ".material");
            require(
                particles.relative_parent,
                entry.name + ".relative_parent");
            require(
                particles.emitter_mesh,
                entry.name + ".emitter_mesh");
        } else if (entry.type == "ParticleSysAnim") {
            const auto animation =
                gh::milo_object::parse_particle_sys_anim3(
                    entry.body_bytes);
            require(
                animation.particle_system,
                entry.name + ".particle_system");
            require(animation.keys_owner, entry.name + ".keys_owner");
        } else if (entry.type == "Font") {
            const auto font =
                gh::milo_object::parse_font15(entry.body_bytes);
            require(font.material, entry.name + ".material");
            require(
                font.texture_owner,
                entry.name + ".texture_owner");
        } else if (entry.type == "Text") {
            const auto text =
                gh::milo_object::parse_text17(entry.body_bytes);
            require_transformable(text.transformable, entry.name);
            require(text.font, entry.name + ".font");
        } else if (entry.type == "Morph") {
            const auto morph =
                gh::milo_object::parse_morph4(entry.body_bytes);
            require(morph.target, entry.name + ".target");
            for (const auto& pose : morph.poses)
                require(pose.mesh, entry.name + ".pose");
        } else if (entry.type == "AnimFilter") {
            const auto filter =
                gh::milo_object::parse_anim_filter1(
                    entry.body_bytes);
            require(filter.anim, entry.name + ".anim");
        } else if (entry.type == "EventTrigger") {
            const auto trigger =
                gh::milo_object::parse_event_trigger8(
                    entry.body_bytes);
            for (const auto& animation : trigger.animations)
                require(
                    animation.animation,
                    entry.name + ".animation");
            for (const auto& sound : trigger.sounds)
                require(sound, entry.name + ".sound");
            for (const auto& object : trigger.shows)
                require(object, entry.name + ".show");
            for (const auto& object : trigger.legacy_hides)
                require(object, entry.name + ".hide");
            require(trigger.next_link, entry.name + ".next");
            for (const auto& proxy : trigger.proxy_calls)
                require(proxy.proxy, entry.name + ".proxy");
        } else if (entry.type == "CharDriver") {
            const auto driver =
                gh::milo_object::parse_char_driver3(entry.body_bytes);
            require(driver.weightable.weight_owner, entry.name + ".weight");
            require(driver.bones, entry.name + ".bones");
        } else if (entry.type == "CharDriverMidi") {
            const auto driver =
                gh::milo_object::parse_char_driver_midi3(
                    entry.body_bytes).driver;
            require(driver.weightable.weight_owner, entry.name + ".weight");
            require(driver.bones, entry.name + ".bones");
        } else if (entry.type == "CharIKMidi") {
            const auto ik =
                gh::milo_object::parse_char_ik_midi4(
                    entry.body_bytes);
            if (ik.bone != "bone_fret.mesh")
                require(ik.bone, entry.name + ".bone");
        } else if (entry.type == "CharWeightSetter") {
            const auto weight =
                gh::milo_object::parse_char_weight_setter2(
                    entry.body_bytes);
            require(
                weight.weightable.weight_owner,
                entry.name + ".weight");
            require(weight.driver, entry.name + ".driver");
        } else if (entry.type == "CharIKHand") {
            const auto hand =
                gh::milo_object::parse_char_ik_hand2(entry.body_bytes);
            require(hand.weightable.weight_owner, entry.name + ".weight");
            require(hand.hand, entry.name + ".hand");
            if (hand.target != "bone_fret_hand.mesh" &&
                hand.target != "bone_strum_hand.mesh")
                require(hand.target, entry.name + ".target");
        } else if (entry.type == "CharIKRod") {
            const auto rod =
                gh::milo_object::parse_char_ik_rod2(entry.body_bytes);
            require(rod.left_end, entry.name + ".left_end");
            require(rod.right_end, entry.name + ".right_end");
            require(rod.side_axis, entry.name + ".side_axis");
            require(rod.dest, entry.name + ".dest");
        } else if (entry.type == "CharPosConstraint") {
            const auto constraint =
                gh::milo_object::parse_char_pos_constraint2(
                    entry.body_bytes);
            require(constraint.source, entry.name + ".source");
            for (const auto& target : constraint.targets)
                require(target, entry.name + ".targets");
        } else if (entry.type == "CharForeTwist") {
            const auto twist =
                gh::milo_object::parse_char_fore_twist4(
                    entry.body_bytes);
            require(twist.hand, entry.name + ".hand");
            require(twist.twist2, entry.name + ".twist2");
        } else if (entry.type == "CharUpperTwist") {
            const auto twist =
                gh::milo_object::parse_char_upper_twist1(
                    entry.body_bytes);
            require(twist.upper_arm, entry.name + ".upper_arm");
            require(twist.twist1, entry.name + ".twist1");
            require(twist.twist2, entry.name + ".twist2");
        } else if (entry.type == "CharLookAt") {
            const auto look =
                gh::milo_object::parse_char_look_at2(entry.body_bytes);
            require(look.weightable.weight_owner, entry.name + ".weight");
            require(look.source, entry.name + ".source");
            require(look.pivot, entry.name + ".pivot");
            require(look.target, entry.name + ".target");
        } else if (entry.type == "CharEyes") {
            const auto eyes =
                gh::milo_object::parse_char_eyes3(entry.body_bytes);
            for (const auto& eye : eyes.eyes)
                require(eye, entry.name + ".eyes");
            require(
                eyes.legacy_transform,
                entry.name + ".legacy_transform");
        } else if (entry.type == "CharHair") {
            const auto hair =
                gh::milo_object::parse_char_hair2(entry.body_bytes);
            for (const auto& strand : hair.strands) {
                require(strand.root, entry.name + ".root");
                for (const auto& point : strand.points)
                    require(point.bone, entry.name + ".point_bone");
            }
        } else if (entry.type == "FaceFxLipSyncServo") {
            const auto servo =
                gh::milo_object::parse_facefx_lip_sync_servo5(
                    entry.body_bytes);
            require(
                servo.weightable.weight_owner,
                entry.name + ".weight");
            for (const auto& target : servo.targets)
                require(target.object, entry.name + ".target");
        } else if (entry.type == "WorldFx") {
            const auto world =
                gh::milo_object::parse_world_fx1(entry.body_bytes);
            require_render_directory(
                world.render_directory, entry.name);
        }
    }
    return count;
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
            "milo convert: character string table exceeds u32");
    directory.hash_table_hint =
        static_cast<uint32_t>((directory.entries.size() + 1) * 2);
    directory.string_table_hint =
        static_cast<uint32_t>(symbol_capacity);
    directory.dir_entry_size = directory.dir_body_bytes.size();
}

}  // namespace

size_t validate_gh2_character_model_references(
    const gh::milo::Directory& directory) {
    return validate_gh2_directory_references(directory);
}

size_t validate_gh2_directory_references(
    const gh::milo::Directory& directory) {
    return validate_character_references_impl(directory);
}

Gh2CharacterModelPackage
convert_gh1_character_to_gh2_model_package(
    const Gh1CharacterModelBuildInput& input) {
    if (input.spec.package_name.empty())
        throw std::runtime_error(
            "milo convert: character package name is empty");
    if (input.source_model.dir_version != 10 ||
        !input.source_model.boundaries_exact)
        throw std::runtime_error(
            "milo convert: character source model is not exact GH1");
    for (const auto& binding : input.animations) {
        if (binding.source_archetype !=
            input.spec.compiled_skeleton)
            throw std::runtime_error(
                "milo convert: character animation archetype mismatch");
    }

    const auto converted =
        convert_gh1_directory_to_gh2_rnddir(
            input.source_model, input.spec.package_name);
    if (!converted.complete)
        throw std::runtime_error(
            "milo convert: character source model conversion incomplete");

    Gh2CharacterModelPackage output;
    output.directory_name = input.spec.package_name;
    output.directory = converted.directory;
    output.directory.dir_type =
        input.spec.band_character ? "Character" : "BandCharacter";
    if (input.face_model)
        merge_face_model(
            output.directory, *input.face_model,
            input.spec.package_name);
    if (input.face_model)
        output.face_transition_count =
            append_face_transition_graph(
                output.directory, input.spec.face,
                input.spec.package_name,
                output.face_controller_type);
    std::string shadow_group;
    if (input.shadow_model)
        shadow_group = merge_shadow_model(
            output.directory, *input.shadow_model,
            input.spec.package_name);

    std::map<int, std::string> lod_groups;
    for (const auto& source_entry : input.source_model.entries) {
        if (source_entry.type != "View") continue;
        const int index = lod_index(source_entry.name);
        if (index < 0) continue;
        if (!lod_groups.emplace(index, source_entry.name).second)
            throw std::runtime_error(
                "milo convert: duplicate character LOD index");
    }
    if (lod_groups.size() !=
        input.spec.lod_screen_sizes.size() + 1)
        throw std::runtime_error(
            "milo convert: character LOD group/threshold mismatch");
    for (size_t index = 0; index < lod_groups.size(); ++index)
        if (!lod_groups.count(static_cast<int>(index)))
            throw std::runtime_error(
                "milo convert: character LOD indices are not contiguous");

    gh::milo_object::Character9 character;
    character.render_directory =
        gh::milo_object::parse_rnd_dir8(
            output.directory.dir_body_bytes);
    switch (input.spec.role) {
    case Gh1CharacterRole::Guitarist:
        character.render_directory.object_directory.object_fields.type =
            "guitarist";
        break;
    case Gh1CharacterRole::Singer:
        character.render_directory.object_directory.object_fields.type =
            "singer";
        break;
    case Gh1CharacterRole::Bassist:
        character.render_directory.object_directory.object_fields.type =
            "bassist";
        break;
    case Gh1CharacterRole::Drummer:
        character.render_directory.object_directory.object_fields.type =
            "drummer";
        break;
    case Gh1CharacterRole::Keyboardist:
        character.render_directory.object_directory.object_fields.type =
            "keyboardist";
        break;
    }
    if (input.spec.sphere[3] > 0.0f) {
        character.render_directory.drawable.sphere =
            input.spec.sphere;
    } else {
        const std::string& first_lod = lod_groups.at(0);
        const auto found = std::find_if(
            output.directory.entries.begin(),
            output.directory.entries.end(),
            [&](const gh::milo::Entry& entry) {
                return entry.type == "Group" &&
                       entry.name == first_lod;
            });
        if (found == output.directory.entries.end())
            throw std::runtime_error(
                "milo convert: converted first LOD group is missing");
        character.render_directory.drawable.sphere =
            gh::milo_object::parse_group12(found->body_bytes)
                .drawable.sphere;
        if (!(character.render_directory.drawable.sphere[3] >
              0.0f))
            character.render_directory.drawable.sphere =
                derive_model_sphere(output.directory);
    }
    const float radius =
        character.render_directory.drawable.sphere[3];
    if (!(radius > 0.0f))
        throw std::runtime_error(
            "milo convert: character bounding sphere has no radius");
    for (size_t index = 0;
         index < input.spec.lod_screen_sizes.size(); ++index) {
        character.lods.push_back(
            {input.spec.lod_screen_sizes[index] / radius,
             lod_groups.at(static_cast<int>(index))});
    }
    character.lods.push_back(
        {0.0f, lod_groups.at(
             static_cast<int>(lod_groups.size() - 1))});
    character.sphere_base =
        input.spec.sphere_base.empty()
            ? input.spec.package_name
            : input.spec.sphere_base;
    character.shadow = shadow_group;

    if (input.spec.band_character) {
        output.directory.dir_body_bytes =
            gh::milo_object::serialize_character9(character);
    } else {
        gh::milo_object::BandCharacter1 band_character;
        band_character.character = character;
        output.directory.dir_body_bytes =
            gh::milo_object::serialize_band_character1(
                band_character);
    }

    gh::milo_object::CharServoBone2 servo;
    output.directory.entries.push_back(make_entry(
        "CharServoBone", "bone.servo",
        gh::milo_object::serialize_char_servo_bone2(servo)));

    gh::milo_object::CharDriver3 main_driver;
    main_driver.bones = "bone.servo";
    main_driver.clips = require_animation(
        input,
        input.spec.band_character
            ? Gh2ClipSetRole::Band
            : Gh2ClipSetRole::GuitarMain)
                            .relative_path;
    main_driver.realign = input.spec.main_driver_realign;
    output.directory.entries.push_back(make_entry(
        "CharDriver", "main.drv",
        gh::milo_object::serialize_char_driver3(main_driver)));

    append_stock_instrument_graph(
        output.directory, input.spec.role);

    for (const auto& controller : input.spec.controllers) {
        if (controller.kind ==
            Gh1CharacterControllerKind::Driver) {
            const Gh2ClipSetRole role =
                hand_driver_role(input.spec, controller);
            if (role == Gh2ClipSetRole::GuitarFret ||
                role == Gh2ClipSetRole::GuitarStrum) {
                gh::milo_object::CharDriverMidi3 driver;
                driver.driver.bones = "bone.servo";
                driver.driver.clips =
                    require_animation(input, role).relative_path;
                output.directory.entries.push_back(make_entry(
                    "CharDriverMidi", controller.name,
                    gh::milo_object::
                        serialize_char_driver_midi3(driver)));
            } else {
                gh::milo_object::CharDriver3 driver;
                driver.bones = "bone.servo";
                driver.clips =
                    require_animation(
                        input, Gh2ClipSetRole::Generic)
                        .relative_path;
                output.directory.entries.push_back(make_entry(
                    "CharDriver", controller.name,
                    gh::milo_object::serialize_char_driver3(driver)));
            }
            continue;
        }
        if (controller.kind ==
            Gh1CharacterControllerKind::ServoBone)
            continue;
        append_controller(output.directory, controller);
    }

    if (input.spec.walk.present) {
        gh::milo_object::CharWalk1 walk;
        walk.object_fields.type = "guitarist";
        output.directory.entries.push_back(make_entry(
            "CharWalk", "walk",
            gh::milo_object::serialize_char_walk1(walk)));
    }
    if (input.spec.eyes.present) {
        if (input.spec.eyes.parent.empty() ||
            !std::isfinite(input.spec.eyes.lower_lid))
            throw std::runtime_error(
                "milo convert: authored eyes have invalid parent/lid facts");
        const auto parent = std::find_if(
            output.directory.entries.begin(),
            output.directory.entries.end(),
            [&](const gh::milo::Entry& entry) {
                return entry.name == input.spec.eyes.parent;
            });
        if (parent == output.directory.entries.end())
            throw std::runtime_error(
                "milo convert: authored eye parent is missing");
        const float limit =
            gh1_eye_limit_degrees(input.spec.eyes.constraint);
        const std::array<std::string, 2> eye_meshes = {
            find_eye_mesh(output.directory, 'l'),
            find_eye_mesh(output.directory, 'r')};
        const std::array<std::string, 2> look_names = {
            "l-eye.lookat", "r-eye.lookat"};
        for (size_t index = 0; index < eye_meshes.size(); ++index) {
            const auto found = std::find_if(
                output.directory.entries.begin(),
                output.directory.entries.end(),
                [&](const gh::milo::Entry& entry) {
                    return entry.type == "Mesh" &&
                           entry.name == eye_meshes[index];
                });
            if (found == output.directory.entries.end())
                throw std::runtime_error(
                    "milo convert: authored eyes require standard eye mesh");
            const auto eye_mesh =
                gh::milo_object::parse_mesh28(found->body_bytes);
            if (eye_mesh.transformable.parent !=
                input.spec.eyes.parent)
                throw std::runtime_error(
                    "milo convert: authored eye mesh parent differs from "
                    "create_eyes parent");
            gh::milo_object::CharLookAt2 look;
            look.source = eye_meshes[index];
            look.pivot = eye_meshes[index];
            look.half_time = 0.0f;
            look.min_yaw = -limit;
            look.max_yaw = limit;
            look.min_pitch = -limit;
            look.max_pitch = limit;
            output.directory.entries.push_back(make_entry(
                "CharLookAt", look_names[index],
                gh::milo_object::serialize_char_look_at2(look)));
        }
        gh::milo_object::CharEyes3 eyes;
        eyes.eyes.assign(look_names.begin(), look_names.end());
        output.directory.entries.push_back(make_entry(
            "CharEyes", "CharEyes.eyes",
            gh::milo_object::serialize_char_eyes3(eyes)));
    }

    if (!input.spec.shadow_file.empty() && !input.shadow_model)
        output.unresolved_dependencies.push_back(
            input.spec.shadow_file);
    if (!input.spec.face_file.empty() && !input.face_model)
        output.unresolved_dependencies.push_back(
            input.spec.face_file);
    else if (input.face_model)
        output.generated_dependencies.push_back(
            "face-control-config:" +
            output.face_controller_type);
    output.native_transform_count =
        promote_character_bone_meshes_to_transes(
            output.directory);
    output.native_upper_twist_sibling_count =
        validate_upper_twist_sibling_hierarchy_for_gh2(
            output.directory, input.spec.controllers);
    output.complete = output.unresolved_dependencies.empty();
    output.internal_reference_count =
        validate_gh2_character_model_references(output.directory);
    finish_directory(output.directory);
    return output;
}

}  // namespace gh::milo_convert
