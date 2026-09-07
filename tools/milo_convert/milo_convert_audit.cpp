#include "ark_v3.h"
#include "acg.h"
#include "dtb.h"
#include "gh1_animation_manifest.h"
#include "gh1_character_manifest.h"
#include "gh1_character_model_package.h"
#include "gh1_character_package.h"
#include "gh1_venue_camera_conversion.h"
#include "gh1_venue_placement_conversion.h"
#include "gh1_venue_script_conversion.h"
#include "gh2_face_config_patch.h"
#include "milo.h"
#include "milo_convert.h"
#include "milo_object.h"
#include "ps2_texture.h"
#include "singer_face_track.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

std::string extension(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos
               ? std::string()
               : lower(path.substr(dot));
}

std::string stem(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string compiled_ps2_asset_path(
    std::string source_path) {
    std::replace(
        source_path.begin(), source_path.end(), '\\', '/');
    const fs::path source(source_path);
    if (source.filename().empty())
        throw std::runtime_error(
            "invalid source asset path: " + source_path);
    return (
        source.parent_path() / "gen" /
        (source.filename().string() + "_ps2")).generic_string();
}

std::string cell(std::string value) {
    for (char& ch : value) {
        if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
    }
    return value;
}

std::string joined_strings(
    const std::vector<std::string>& values) {
    std::string result;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index) result += '|';
        result += values[index];
    }
    return result;
}

template <size_t N>
std::string float_values(const std::array<float, N>& values) {
    std::ostringstream out;
    out << std::setprecision(9);
    for (size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << values[index];
    }
    return out.str();
}

void fnv1a_bytes(
    uint64_t& digest, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
}

template <typename T>
void fnv1a_value(uint64_t& digest, const T& value) {
    fnv1a_bytes(digest, &value, sizeof(value));
}

void fnv1a_string(uint64_t& digest, const std::string& value) {
    const uint64_t size = value.size();
    fnv1a_value(digest, size);
    fnv1a_bytes(digest, value.data(), value.size());
}

std::string fnv1a_hex(uint64_t digest) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16)
        << digest;
    return out.str();
}

std::string string_value_digest(const std::string& value) {
    uint64_t digest = UINT64_C(14695981039346656037);
    fnv1a_string(digest, value);
    return fnv1a_hex(digest);
}

template <size_t N>
void fnv1a_float_array(
    uint64_t& digest, const std::array<float, N>& values) {
    fnv1a_bytes(
        digest, values.data(), values.size() * sizeof(float));
}

template <size_t N>
std::string float_array_digest(
    const std::array<float, N>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    fnv1a_float_array(digest, values);
    return fnv1a_hex(digest);
}

template <size_t N>
std::string float_array_pair_digest(
    const std::array<float, N>& first,
    const std::array<float, N>& second) {
    uint64_t digest = UINT64_C(14695981039346656037);
    fnv1a_float_array(digest, first);
    fnv1a_float_array(digest, second);
    return fnv1a_hex(digest);
}

std::string float_value(float value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

template <size_t N>
std::string float_array_vector_digest(
    const std::vector<std::array<float, N>>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = values.size();
    fnv1a_value(digest, count);
    for (const auto& value : values)
        fnv1a_float_array(digest, value);
    return fnv1a_hex(digest);
}

std::string mesh_vertex_digest(
    const std::vector<gh::milo_object::MeshVertex>& vertices) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = vertices.size();
    fnv1a_value(digest, count);
    for (const auto& vertex : vertices) {
        fnv1a_float_array(digest, vertex.position);
        fnv1a_float_array(digest, vertex.normal);
        fnv1a_float_array(digest, vertex.color_or_weights);
        fnv1a_float_array(digest, vertex.uv);
    }
    return fnv1a_hex(digest);
}

std::string mesh_face_digest(
    const std::vector<std::array<uint16_t, 3>>& faces) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = faces.size();
    fnv1a_value(digest, count);
    for (const auto& face : faces)
        fnv1a_bytes(
            digest, face.data(), face.size() * sizeof(uint16_t));
    return fnv1a_hex(digest);
}

std::string mesh_group_digest(
    const std::vector<uint8_t>& groups) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = groups.size();
    fnv1a_value(digest, count);
    fnv1a_bytes(digest, groups.data(), groups.size());
    return fnv1a_hex(digest);
}

std::string string_vector_digest(
    const std::vector<std::string>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = values.size();
    fnv1a_value(digest, count);
    for (const auto& value : values)
        fnv1a_string(digest, value);
    return fnv1a_hex(digest);
}

std::string byte_vector_digest(
    const std::vector<uint8_t>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = values.size();
    fnv1a_value(digest, count);
    fnv1a_bytes(digest, values.data(), values.size());
    return fnv1a_hex(digest);
}

template <size_t N>
std::string byte_array_digest(
    const std::array<uint8_t, N>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = values.size();
    fnv1a_value(digest, count);
    fnv1a_bytes(digest, values.data(), values.size());
    return fnv1a_hex(digest);
}

template <size_t N>
std::string u32_array_digest(
    const std::array<uint32_t, N>& values) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = values.size();
    fnv1a_value(digest, count);
    fnv1a_bytes(
        digest, values.data(), values.size() * sizeof(uint32_t));
    return fnv1a_hex(digest);
}

uint32_t expected_gh2_clip_time_flags(uint32_t source) {
    switch (source) {
        case 0: return 0;
        case 1: return 0x1000;
        case 2: return 0x2000;
        case 4: return 0x4000;
        case 8: return 0x8000;
        case 16: return 0x0200;
        case 32: return 0x0400;
        default:
            throw std::runtime_error(
                "ACP value audit: unsupported play flags " +
                std::to_string(source));
    }
}

std::array<uint32_t, 10> expected_char_bones_counts(
    const std::vector<std::string>& channels) {
    std::array<uint32_t, 9> category_counts{};
    size_t previous = 0;
    bool have_previous = false;
    for (const auto& channel : channels) {
        const std::string suffix = extension(channel);
        size_t category = 0;
        if (suffix == ".pos") category = 0;
        else if (suffix == ".scale") category = 1;
        else if (suffix == ".quat") category = 2;
        else if (suffix == ".rotx") category = 3;
        else if (suffix == ".roty") category = 4;
        else if (suffix == ".rotz") category = 5;
        else if (suffix == ".drotx") category = 6;
        else if (suffix == ".droty") category = 7;
        else if (suffix == ".drotz") category = 8;
        else
            throw std::runtime_error(
                "ACP value audit: unsupported channel " + channel);
        if (have_previous && category < previous)
            throw std::runtime_error(
                "ACP value audit: channels are not in native order");
        previous = category;
        have_previous = true;
        ++category_counts[category];
    }
    std::array<uint32_t, 10> result{};
    uint32_t cumulative = 0;
    for (size_t index = 0; index < category_counts.size(); ++index) {
        cumulative += category_counts[index];
        result[index + 1] = cumulative;
    }
    return result;
}

std::string mesh_bone_digest(
    const std::array<gh::milo_object::MeshBoneSlot, 4>& slots) {
    uint64_t digest = UINT64_C(14695981039346656037);
    for (const auto& slot : slots) {
        fnv1a_string(digest, slot.bone);
        fnv1a_float_array(digest, slot.offset);
    }
    return fnv1a_hex(digest);
}

std::string mesh_strip_digest(
    const std::vector<gh::milo_object::MeshStripResult>& strips) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = strips.size();
    fnv1a_value(digest, count);
    for (const auto& strip : strips) {
        const uint64_t cumulative_count =
            strip.cumulative_strip_lengths.size();
        fnv1a_value(digest, cumulative_count);
        for (uint32_t value : strip.cumulative_strip_lengths)
            fnv1a_value(digest, value);
        const uint64_t run_count = strip.strip_runs.size();
        fnv1a_value(digest, run_count);
        for (uint16_t value : strip.strip_runs)
            fnv1a_value(digest, value);
    }
    return fnv1a_hex(digest);
}

template <size_t N>
bool same_float_bits(
    const std::array<float, N>& left,
    const std::array<float, N>& right) {
    return std::memcmp(
               left.data(), right.data(),
               left.size() * sizeof(float)) == 0;
}

bool same_float_bits(float left, float right) {
    return std::memcmp(&left, &right, sizeof(float)) == 0;
}

template <size_t N>
bool same_float_array_vector(
    const std::vector<std::array<float, N>>& left,
    const std::vector<std::array<float, N>>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index)
        if (!same_float_bits(left[index], right[index]))
            return false;
    return true;
}

template <typename ParticleType>
std::string particle_value_digest(
    const std::vector<ParticleType>& particles) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = particles.size();
    fnv1a_value(digest, count);
    for (const auto& particle : particles) {
        fnv1a_float_array(digest, particle.position);
        fnv1a_float_array(digest, particle.color);
        fnv1a_value(digest, particle.size);
    }
    return fnv1a_hex(digest);
}

template <typename SourceParticle, typename TargetParticle>
bool same_particle_values(
    const std::vector<SourceParticle>& source,
    const std::vector<TargetParticle>& target) {
    if (source.size() != target.size()) return false;
    for (size_t index = 0; index < source.size(); ++index) {
        if (!same_float_bits(
                source[index].position, target[index].position) ||
            !same_float_bits(
                source[index].color, target[index].color) ||
            !same_float_bits(
                source[index].size, target[index].size))
            return false;
    }
    return true;
}

std::string font_kerning_digest(
    const std::vector<gh::milo_object::FontKerning>& rows) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = rows.size();
    fnv1a_value(digest, count);
    for (const auto& row : rows) {
        fnv1a_value(digest, row.packed_char_pair);
        fnv1a_value(digest, row.kerning);
    }
    return fnv1a_hex(digest);
}

bool same_font_kerning(
    const std::vector<gh::milo_object::FontKerning>& left,
    const std::vector<gh::milo_object::FontKerning>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index)
        if (left[index].packed_char_pair !=
                right[index].packed_char_pair ||
            !same_float_bits(
                left[index].kerning, right[index].kerning))
            return false;
    return true;
}

std::string font_char_info_value(
    const gh::milo_object::FontCharInfo15& info) {
    return float_values(std::array<float, 4>{
        info.texture_u, info.texture_v, info.character_width,
        info.character_advance});
}

bool same_font_char_info(
    const gh::milo_object::FontCharInfo15& left,
    const gh::milo_object::FontCharInfo15& right) {
    return same_float_bits(left.texture_u, right.texture_u) &&
           same_float_bits(left.texture_v, right.texture_v) &&
           same_float_bits(
               left.character_width, right.character_width) &&
           same_float_bits(
               left.character_advance, right.character_advance);
}

template <typename Key>
bool same_array_keys(
    const std::vector<Key>& left,
    const std::vector<Key>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (!same_float_bits(
                left[index].value, right[index].value) ||
            !same_float_bits(
                left[index].frame, right[index].frame))
            return false;
    }
    return true;
}

template <typename Key>
std::string array_key_digest(const std::vector<Key>& keys) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = keys.size();
    fnv1a_value(digest, count);
    for (const auto& key : keys) {
        fnv1a_float_array(digest, key.value);
        fnv1a_value(digest, key.frame);
    }
    return fnv1a_hex(digest);
}

bool same_morph_keys(
    const std::vector<gh::milo_object::MorphKey>& left,
    const std::vector<gh::milo_object::MorphKey>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (!same_float_bits(
                left[index].value, right[index].value) ||
            !same_float_bits(
                left[index].frame, right[index].frame))
            return false;
    }
    return true;
}

std::string morph_key_digest(
    const std::vector<gh::milo_object::MorphKey>& keys) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = keys.size();
    fnv1a_value(digest, count);
    for (const auto& key : keys) {
        fnv1a_value(digest, key.value);
        fnv1a_value(digest, key.frame);
    }
    return fnv1a_hex(digest);
}

template <size_t N>
bool same_vector_keys(
    const std::vector<
        gh::milo_object::VectorKey<std::array<float, N>>>& left,
    const std::vector<
        gh::milo_object::VectorKey<std::array<float, N>>>& right) {
    if (left.size() != right.size()) return false;
    for (size_t key_index = 0;
         key_index < left.size(); ++key_index) {
        if (left[key_index].values.size() !=
                right[key_index].values.size() ||
            !same_float_bits(
                left[key_index].frame,
                right[key_index].frame))
            return false;
        for (size_t value_index = 0;
             value_index <
                 left[key_index].values.size();
             ++value_index)
            if (!same_float_bits(
                    left[key_index].values[value_index],
                    right[key_index].values[value_index]))
                return false;
    }
    return true;
}

template <size_t N>
std::string vector_key_digest(
    const std::vector<
        gh::milo_object::VectorKey<std::array<float, N>>>& keys) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = keys.size();
    fnv1a_value(digest, count);
    for (const auto& key : keys) {
        const uint64_t value_count = key.values.size();
        fnv1a_value(digest, value_count);
        for (const auto& value : key.values)
            fnv1a_float_array(digest, value);
        fnv1a_value(digest, key.frame);
    }
    return fnv1a_hex(digest);
}

bool same_morph_poses(
    const std::vector<gh::milo_object::MorphPose>& left,
    const std::vector<gh::milo_object::MorphPose>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index)
        if (left[index].mesh != right[index].mesh ||
            !same_morph_keys(
                left[index].keys, right[index].keys))
            return false;
    return true;
}

std::string morph_pose_digest(
    const std::vector<gh::milo_object::MorphPose>& poses) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = poses.size();
    fnv1a_value(digest, count);
    for (const auto& pose : poses) {
        fnv1a_string(digest, pose.mesh);
        fnv1a_string(digest, morph_key_digest(pose.keys));
    }
    return fnv1a_hex(digest);
}

bool same_object_keys(
    const std::vector<gh::milo_object::ObjectKey>& left,
    const std::vector<gh::milo_object::ObjectKey>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index)
        if (left[index].object != right[index].object ||
            !same_float_bits(
                left[index].frame, right[index].frame))
            return false;
    return true;
}

std::string object_key_digest(
    const std::vector<gh::milo_object::ObjectKey>& keys) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = keys.size();
    fnv1a_value(digest, count);
    for (const auto& key : keys) {
        fnv1a_string(digest, key.object);
        fnv1a_value(digest, key.frame);
    }
    return fnv1a_hex(digest);
}

std::string legacy_anim_operation_digest(
    const std::vector<gh::milo_object::LegacyAnimOperation>&
        operations) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = operations.size();
    fnv1a_value(digest, count);
    for (const auto& operation : operations) {
        fnv1a_value(digest, operation.type);
        fnv1a_value(digest, operation.first);
        fnv1a_value(digest, operation.second);
        const uint8_t loop = operation.loop ? 1 : 0;
        fnv1a_value(digest, loop);
        for (int32_t value : operation.integers)
            fnv1a_value(digest, value);
    }
    return fnv1a_hex(digest);
}

gh::milo_object::LegacyAnimSettings expected_legacy_anim_settings(
    const gh::milo_object::LegacyAnimatable& source) {
    if (source.revision != 0)
        throw std::runtime_error(
            "animation value audit: unsupported Animatable revision");
    gh::milo_object::LegacyAnimSettings result;
    for (const auto& operation : source.operations) {
        if (operation.type == 0) {
            result.scale = operation.first;
            result.offset = operation.second;
        } else if (operation.type == 1) {
            result.minimum = operation.first;
            result.maximum = operation.second;
            result.loop = operation.loop;
        } else if (
            operation.type != 2 && operation.type != 3 &&
            operation.type != 4) {
            throw std::runtime_error(
                "animation value audit: unsupported operation");
        }
    }
    return result;
}

bool native_animation_bases_are_default(
    const gh::milo_object::ObjectFields0& object_fields,
    const gh::milo_object::Animatable4& animatable) {
    return object_fields.revision == 0 &&
           object_fields.type.empty() &&
           !object_fields.has_type_properties &&
           object_fields.type_property_id == 0 &&
           object_fields.type_properties.empty() &&
           animatable.revision == 4 &&
           animatable.frame == 0.0f &&
           animatable.rate == 0;
}

bool validate_legacy_animation_filter(
    const std::vector<gh::milo::Entry>& targets,
    const std::string& source_name,
    const gh::milo_object::LegacyAnimatable& animatable) {
    const auto settings =
        expected_legacy_anim_settings(animatable);
    const bool required =
        settings.scale != 1.0f ||
        settings.offset != 0.0f ||
        settings.minimum != settings.maximum;
    if (!required) return false;
    const std::string filter_name =
        stem(source_name) + ".filt";
    const auto found = std::find_if(
        targets.begin(), targets.end(),
        [&](const gh::milo::Entry& target) {
            return target.type == "AnimFilter" &&
                   target.name == filter_name;
        });
    if (found == targets.end())
        throw std::runtime_error(
            "animation value audit: filter missing: " +
            source_name);
    const auto filter =
        gh::milo_object::parse_anim_filter1(found->body_bytes);
    if (!native_animation_bases_are_default(
            filter.object_fields, filter.animatable) ||
        filter.revision != 1 ||
        filter.anim != source_name ||
        !same_float_bits(
            filter.scale, std::fabs(settings.scale)) ||
        !same_float_bits(
            filter.offset, settings.offset) ||
        !same_float_bits(
            filter.start, settings.minimum) ||
        !same_float_bits(
            filter.end, settings.maximum) ||
        filter.type != (settings.loop ? 1 : 0) ||
        filter.period != 0.0f)
        throw std::runtime_error(
            "animation value audit: filter differs: " +
            source_name);
    return true;
}

gh::milo_object::Trans9 expected_bounce_trans(
    const std::array<float, 4>& plane) {
    const float length_squared =
        plane[0] * plane[0] +
        plane[1] * plane[1] +
        plane[2] * plane[2];
    if (!(length_squared > 0.0f) ||
        !std::isfinite(length_squared))
        throw std::runtime_error(
            "particle value audit: bounce plane has no finite normal");
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    const std::array<float, 3> normal = {
        plane[0] * inverse_length,
        plane[1] * inverse_length,
        plane[2] * inverse_length};
    const float point_scale = -plane[3] / length_squared;
    const std::array<float, 3> point = {
        plane[0] * point_scale,
        plane[1] * point_scale,
        plane[2] * point_scale};
    const std::array<float, 3> helper =
        std::fabs(normal[2]) < 0.999f
            ? std::array<float, 3>{0, 0, 1}
            : std::array<float, 3>{0, 1, 0};
    std::array<float, 3> x = {
        helper[1] * normal[2] - helper[2] * normal[1],
        helper[2] * normal[0] - helper[0] * normal[2],
        helper[0] * normal[1] - helper[1] * normal[0]};
    const float x_length =
        std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& value : x) value /= x_length;
    const std::array<float, 3> y = {
        normal[1] * x[2] - normal[2] * x[1],
        normal[2] * x[0] - normal[0] * x[2],
        normal[0] * x[1] - normal[1] * x[0]};
    gh::milo_object::Trans9 target;
    target.local = {
        x[0], x[1], x[2],
        y[0], y[1], y[2],
        normal[0], normal[1], normal[2],
        point[0], point[1], point[2]};
    target.world = target.local;
    return target;
}

bool same_mesh_vertices(
    const std::vector<gh::milo_object::MeshVertex>& left,
    const std::vector<gh::milo_object::MeshVertex>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (!same_float_bits(
                left[index].position, right[index].position) ||
            !same_float_bits(
                left[index].normal, right[index].normal) ||
            !same_float_bits(
                left[index].color_or_weights,
                right[index].color_or_weights) ||
            !same_float_bits(left[index].uv, right[index].uv))
            return false;
    }
    return true;
}

bool same_mesh_bones(
    const std::array<gh::milo_object::MeshBoneSlot, 4>& left,
    const std::array<gh::milo_object::MeshBoneSlot, 4>& right) {
    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].bone != right[index].bone ||
            !same_float_bits(
                left[index].offset, right[index].offset))
            return false;
    }
    return true;
}

std::optional<gh::milo_object::LegacyTransformable>
source_transformable(const gh::milo::Entry& entry) {
    if (entry.type == "Cam")
        return gh::milo_object::parse_cam(entry.body_bytes).transformable;
    if (entry.type == "Flare")
        return gh::milo_object::parse_flare(entry.body_bytes).transformable;
    if (entry.type == "Light")
        return gh::milo_object::parse_light(entry.body_bytes).transformable;
    if (entry.type == "Mesh")
        return gh::milo_object::parse_mesh(entry.body_bytes).transformable;
    if (entry.type == "ParticleSys")
        return gh::milo_object::parse_particle_sys(entry.body_bytes)
            .transformable;
    if (entry.type == "Text")
        return gh::milo_object::parse_text(entry.body_bytes).transformable;
    if (entry.type == "View")
        return gh::milo_object::parse_view(entry.body_bytes).transformable;
    return std::nullopt;
}

std::optional<gh::milo_object::LegacyDrawable>
source_drawable(const gh::milo::Entry& entry) {
    if (entry.type == "Cam")
        return gh::milo_object::parse_cam(entry.body_bytes).drawable;
    if (entry.type == "Environ")
        return gh::milo_object::parse_environ(entry.body_bytes)
            .legacy_drawable;
    if (entry.type == "Flare")
        return gh::milo_object::parse_flare(entry.body_bytes).drawable;
    if (entry.type == "Mesh")
        return gh::milo_object::parse_mesh(entry.body_bytes).drawable;
    if (entry.type == "MultiMesh")
        return gh::milo_object::parse_multi_mesh(entry.body_bytes)
            .drawable;
    if (entry.type == "ParticleSys")
        return gh::milo_object::parse_particle_sys(entry.body_bytes)
            .drawable;
    if (entry.type == "Text")
        return gh::milo_object::parse_text(entry.body_bytes).drawable;
    if (entry.type == "TransAnim")
        return gh::milo_object::parse_trans_anim(entry.body_bytes)
            .drawable;
    if (entry.type == "View")
        return gh::milo_object::parse_view(entry.body_bytes).drawable;
    return std::nullopt;
}

std::optional<gh::milo_object::LegacyAnimatable>
source_animatable(const gh::milo::Entry& entry) {
    if (entry.type == "CamAnim")
        return gh::milo_object::parse_cam_anim(entry.body_bytes).animatable;
    if (entry.type == "EnvAnim")
        return gh::milo_object::parse_env_anim(entry.body_bytes).animatable;
    if (entry.type == "LightAnim")
        return gh::milo_object::parse_light_anim(entry.body_bytes)
            .animatable;
    if (entry.type == "MatAnim")
        return gh::milo_object::parse_mat_anim(entry.body_bytes).animatable;
    if (entry.type == "MeshAnim")
        return gh::milo_object::parse_mesh_anim(entry.body_bytes).animatable;
    if (entry.type == "Morph")
        return gh::milo_object::parse_morph(entry.body_bytes).animatable;
    if (entry.type == "Movie")
        return gh::milo_object::parse_movie(entry.body_bytes).animatable;
    if (entry.type == "ParticleSys")
        return gh::milo_object::parse_particle_sys(entry.body_bytes)
            .animatable;
    if (entry.type == "ParticleSysAnim")
        return gh::milo_object::parse_particle_sys_anim(entry.body_bytes)
            .animatable;
    if (entry.type == "TransAnim")
        return gh::milo_object::parse_trans_anim(entry.body_bytes)
            .animatable;
    if (entry.type == "View")
        return gh::milo_object::parse_view(entry.body_bytes).animatable;
    return std::nullopt;
}

std::string inferred_reference_type_for_audit(
    const std::string& name) {
    static const std::map<std::string, std::string> types = {
        {".anim", "View"},
        {".cam", "Cam"},
        {".env", "Environ"},
        {".lt", "Light"},
        {".mat", "Mat"},
        {".mesh", "Mesh"},
        {".mnm", "MatAnim"},
        {".panim", "ParticleSysAnim"},
        {".part", "ParticleSys"},
        {".tnm", "TransAnim"},
        {".view", "View"},
    };
    const auto found = types.find(extension(name));
    return found == types.end() ? std::string() : found->second;
}

bool target_drawable_type_for_audit(const std::string& type) {
    return type == "Flare" || type == "Group" ||
           type == "Mesh" || type == "MultiMesh" ||
           type == "ParticleSys" || type == "Text" ||
           type == "View";
}

std::string resolved_reference_digest(
    const std::vector<gh::milo_object::ResolvedObjectReference>&
        references) {
    uint64_t digest = UINT64_C(14695981039346656037);
    const uint64_t count = references.size();
    fnv1a_value(digest, count);
    for (const auto& reference : references) {
        fnv1a_string(digest, reference.name);
        fnv1a_string(digest, reference.type);
    }
    return fnv1a_hex(digest);
}

struct TargetTransformable {
    uint32_t revision = 0;
    gh::milo_object::Transformable9 value;
};

std::optional<TargetTransformable> target_transformable(
    const gh::milo::Entry& entry, uint32_t directory_version) {
    if (entry.type == "Cam") {
        const auto object =
            gh::milo_object::parse_cam12(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "Flare") {
        const auto object =
            gh::milo_object::parse_flare4(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "Light") {
        const auto object =
            gh::milo_object::parse_light6(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "Mesh") {
        const auto object =
            gh::milo_object::parse_mesh28(
                entry.body_bytes, directory_version);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "ParticleSys") {
        const auto object =
            gh::milo_object::parse_particle_sys27(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "Text") {
        const auto object =
            gh::milo_object::parse_text17(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    if (entry.type == "Group") {
        const auto object =
            gh::milo_object::parse_group12(entry.body_bytes);
        return TargetTransformable{
            object.revision, object.transformable};
    }
    return std::nullopt;
}

struct ExpectedTransformable {
    std::string source_type;
    uint32_t source_revision = 0;
    size_t source_index = 0;
    gh::milo_object::LegacyTransformable source;
    std::string parent;
    uint32_t constraint = 0;
    std::vector<std::string> child_owners;
};

bool same_mesh_strips(
    const std::vector<gh::milo_object::MeshStripResult>& left,
    const std::vector<gh::milo_object::MeshStripResult>& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].cumulative_strip_lengths !=
                right[index].cumulative_strip_lengths ||
            left[index].strip_runs != right[index].strip_runs)
            return false;
    }
    return true;
}

uint32_t little_u32(
    const std::vector<uint8_t>& bytes, const std::string& context) {
    if (bytes.size() < 4)
        throw std::runtime_error(
            context + " is too short to contain a class revision");
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

using DiscardedValueKey =
    std::tuple<std::string, uint32_t, std::string, std::string>;

struct DiscardedValueInstance {
    std::string archive_path;
    std::string source_type;
    uint32_t source_revision = 0;
    std::string object_name;
    std::string source_field;
    std::string value_class;
};

std::string discarded_value_status(
    const std::string& source_type,
    const std::string& source_field,
    const std::string& value_class) {
    const bool default_or_absent =
        value_class == "false" ||
        value_class == "zero" ||
        value_class == "empty" ||
        value_class == "not_serialized";
    const bool structural_revision =
        source_field.size() >= 8 &&
        source_field.substr(
            source_field.size() - 8) == "revision";
    const bool retail_traced_target_discard =
        source_type == "Mat" &&
        (source_field == "vertex_dynamic" ||
         source_field == "normalize");
    const bool target_class_not_drawable =
        value_class == "true" &&
        (source_type == "Cam" ||
         source_type == "Environ" ||
         source_type == "TransAnim") &&
        source_field.find("drawable.showing") !=
            std::string::npos;
    return default_or_absent
               ? "default_or_absent"
               : (retail_traced_target_discard
                      ? "target_discard_retail_traced"
                      : (target_class_not_drawable
                             ? "target_class_not_drawable"
                             : (structural_revision
                                    ? "structural_revision"
                                    : "nondefault_requires_retail_"
                                      "proof")));
}

void observe_discarded_values(
    const gh::milo::Entry& object,
    const std::string& archive_path,
    std::map<DiscardedValueKey, size_t>& observations,
    std::vector<DiscardedValueInstance>& instances) {
    const auto record =
        [&](uint32_t revision, const std::string& field,
            const std::string& value) {
            ++observations[
                {object.type, revision, field, value}];
            instances.push_back({
                archive_path, object.type, revision, object.name,
                field, value});
        };
    const auto observe_drawable =
        [&](uint32_t class_revision,
            const gh::milo_object::LegacyDrawable& drawable,
            const std::string& prefix, bool target_has_drawable) {
            if (!target_has_drawable) {
                record(
                    class_revision, prefix + "revision",
                    std::to_string(drawable.revision));
                record(
                    class_revision, prefix + "showing",
                    drawable.showing ? "true" : "false");
                const bool sphere_nonzero =
                    std::any_of(
                        drawable.sphere.begin(), drawable.sphere.end(),
                        [](float value) { return value != 0.0f; });
                record(
                    class_revision, prefix + "sphere",
                    drawable.revision > 0
                        ? (sphere_nonzero ? "nonzero" : "zero")
                        : "not_serialized");
                record(
                    class_revision, prefix + "draw_order",
                    drawable.revision > 2
                        ? std::to_string(drawable.draw_order)
                        : "not_serialized");
            }
            record(
                class_revision, prefix + "legacy_target",
                drawable.revision > 3
                    ? (drawable.legacy_target.empty()
                           ? "empty"
                           : "nonempty")
                    : "not_serialized");
        };

    if (object.type == "Cam") {
        const auto value =
            gh::milo_object::parse_cam(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", false);
    } else if (object.type == "Environ") {
        const auto value =
            gh::milo_object::parse_environ(object.body_bytes);
        observe_drawable(
            value.revision, value.legacy_drawable,
            "legacy_drawable.", false);
    } else if (object.type == "Flare") {
        const auto value =
            gh::milo_object::parse_flare(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
    } else if (object.type == "Mat") {
        const auto value =
            gh::milo_object::parse_mat(object.body_bytes);
        record(
            value.revision, "vertex_dynamic",
            value.vertex_dynamic ? "true" : "false");
        record(
            value.revision, "normalize",
            value.normalize ? "true" : "false");
    } else if (object.type == "Mesh") {
        const auto value =
            gh::milo_object::parse_mesh(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
    } else if (object.type == "MultiMesh") {
        const auto value =
            gh::milo_object::parse_multi_mesh(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
    } else if (object.type == "ParticleSys") {
        const auto value =
            gh::milo_object::parse_particle_sys(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
    } else if (object.type == "Text") {
        const auto value =
            gh::milo_object::parse_text(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
    } else if (object.type == "TransAnim") {
        const auto value =
            gh::milo_object::parse_trans_anim(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", false);
    } else if (object.type == "View") {
        const auto value =
            gh::milo_object::parse_view(object.body_bytes);
        observe_drawable(
            value.revision, value.drawable, "drawable.", true);
        record(
            value.revision, "showing_range",
            value.showing_range[0] == 0.0f &&
                    value.showing_range[1] == 0.0f
                ? "zero"
                : "nonzero");
    }
}

struct BundleRecord {
    std::string relative_path;
    std::string kind;
    std::string source;
    size_t bytes = 0;
};

struct Gh1VenuePath {
    std::string source_venue;
    std::string target_venue;
    std::string target_path;
    bool primary = false;
};

std::optional<Gh1VenuePath> gh1_venue_target_path(
    const std::string& source_path) {
    constexpr std::string_view prefix = "venues/";
    if (source_path.rfind(prefix, 0) != 0) return std::nullopt;
    const size_t venue_end = source_path.find('/', prefix.size());
    if (venue_end == std::string::npos) return std::nullopt;
    const std::string venue =
        source_path.substr(prefix.size(), venue_end - prefix.size());
    const std::string gen_prefix =
        "venues/" + venue + "/gen/";
    if (venue.empty() || source_path.rfind(gen_prefix, 0) != 0)
        return std::nullopt;
    std::string leaf = source_path.substr(gen_prefix.size());
    if (leaf.empty() || leaf.find('/') != std::string::npos)
        return std::nullopt;
    const std::string target = "gh1_" + venue;
    const std::string leaf_extension = extension(leaf);
    const std::string leaf_stem = stem(leaf);
    const bool primary = leaf_stem == venue;
    if (leaf_extension == ".rnd_ps2") {
        leaf = (primary ? target : leaf_stem) + ".milo_ps2";
    } else if (primary) {
        leaf = target + leaf_extension;
    }
    return Gh1VenuePath{
        venue, target, "world/" + target + "/gen/" + leaf, primary};
}

void write_bundle_file(
    const fs::path& root,
    const std::string& relative_path,
    const std::vector<uint8_t>& bytes) {
    const fs::path relative =
        fs::path(relative_path).lexically_normal();
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..")
        throw std::runtime_error(
            "bundle output path escapes root: " + relative_path);
    const fs::path output_path = root / relative;
    fs::create_directories(output_path.parent_path());
    std::ofstream output(output_path, std::ios::binary);
    if (!output)
        throw std::runtime_error(
            "cannot create " + output_path.string());
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output)
        throw std::runtime_error(
            "cannot write " + output_path.string());
}

void link_bundle_file(
    const fs::path& root,
    const std::string& relative_path,
    const fs::path& source_path) {
    const fs::path relative =
        fs::path(relative_path).lexically_normal();
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == "..")
        throw std::runtime_error(
            "bundle output path escapes root: " + relative_path);
    const fs::path output_path = root / relative;
    fs::create_directories(output_path.parent_path());
    std::error_code error;
    if (fs::exists(output_path)) {
        if (fs::equivalent(source_path, output_path, error) && !error)
            return;
        error.clear();
        if (!fs::remove(output_path, error) || error)
            throw std::runtime_error(
                "cannot replace " + output_path.string());
    }
    fs::create_hard_link(source_path, output_path, error);
    if (!error)
        return;
    error.clear();
    fs::copy_file(
        source_path, output_path,
        fs::copy_options::overwrite_existing, error);
    if (error)
        throw std::runtime_error(
            "cannot link or copy " + output_path.string());
}

void write_bundle_manifest(
    const fs::path& root,
    const std::string& name,
    std::vector<BundleRecord> records) {
    std::sort(
        records.begin(), records.end(),
        [](const BundleRecord& left, const BundleRecord& right) {
            return left.relative_path < right.relative_path;
        });
    for (size_t index = 1; index < records.size(); ++index)
        if (records[index - 1].relative_path ==
            records[index].relative_path)
            throw std::runtime_error(
                "duplicate bundle path: " +
                records[index].relative_path);
    fs::create_directories(root);
    const fs::path manifest_path = root / name;
    std::ofstream output(manifest_path, std::ios::binary);
    if (!output)
        throw std::runtime_error(
            "cannot create " + manifest_path.string());
    output << "relative_path\tkind\tsource\tbytes\n";
    for (const auto& record : records)
        output << cell(record.relative_path) << '\t'
               << cell(record.kind) << '\t'
               << cell(record.source) << '\t'
               << record.bytes << '\n';
    if (!output)
        throw std::runtime_error(
            "cannot write " + manifest_path.string());
}

void collect_dtb_strings(
    const std::shared_ptr<gh::dtb::Node>& node,
    std::vector<std::string>& output) {
    if (!node) return;
    if (gh::dtb::is_array(*node)) {
        for (const auto& child : gh::dtb::children(*node))
            collect_dtb_strings(child, output);
        return;
    }
    if (const auto value = gh::dtb::as_string(*node))
        output.push_back(*value);
}

size_t count_dtb_nodes(const gh::dtb::Node& node) {
    size_t count = 1;
    if (gh::dtb::is_array(node)) {
        for (const auto& child : gh::dtb::children(node))
            count += count_dtb_nodes(*child);
    }
    return count;
}

const char* dtb_storage_name(gh::dtb::Storage storage) {
    switch (storage) {
    case gh::dtb::Storage::Plain:
        return "plain";
    case gh::dtb::Storage::ZeroPrefixedPlain:
        return "zero_prefixed_plain";
    case gh::dtb::Storage::Encrypted:
        return "encrypted";
    }
    return "unknown";
}

void usage() {
    std::fprintf(
        stderr,
        "Usage:\n"
        "  milo_convert_audit scan <main.hdr> <main_0.ark> "
        "[main_1.ark ...] --report <report.tsv> "
        "[--bundle <dir> --target-rnd-objects <file> "
        "--target-midi-parsers <file> --target-char-objects <file>]\n"
        "  milo_convert_audit target-summaries <main.hdr> "
        "<main_0.ark> [main_1.ark ...] --report <report.tsv>\n"
        "  milo_convert_audit target-face-sweep <main.hdr> "
        "<main_0.ark> [main_1.ark ...] --report <report.tsv> "
        "[--out <dir>]\n");
    std::exit(2);
}

int target_summaries(
    const gh::ark::ArkV3Reader& archive,
    const std::vector<std::string>& ark_paths,
    const std::string& report_path) {
    fs::create_directories(fs::path(report_path).parent_path());
    std::ofstream report(report_path, std::ios::binary);
    if (!report)
        throw std::runtime_error("cannot create " + report_path);
    report
        << "archive_path\tclip\tflags\tsize_bytes\tbody_flags"
           "\tbody_play_flags\tbody_blend_width\tbody_range"
           "\tbody_legacy_flag\tbody_bytes"
           "\tsample_bytes\tfull_bytes\tone_bytes\tduplicate_bytes"
           "\tfull_channels\tone_channels"
           "\tduplicate_channels\tfull_samples\tone_samples"
           "\tduplicate_samples\ttransition_groups"
           "\ttransition_nodes\tevents\tstring_bytes"
           "\tfull_compression\tone_compression"
           "\tduplicate_compression\tfull_counts\tone_counts"
           "\tduplicate_counts\tfull_facing_channels"
           "\tone_facing_channels\n";
    const std::string root_report_path =
        report_path + ".roots.tsv";
    std::ofstream root_report(root_report_path, std::ios::binary);
    if (!root_report)
        throw std::runtime_error("cannot create " + root_report_path);
    root_report
        << "archive_path\tdirectory\tclips\tentries\tobject_type"
           "\tviewports\tblend_width\tplay_flags\tmove_self"
           "\trecenter_targets\trecenter_average\trecenter_slide"
           "\tlegacy_type\tlegacy_type_version\n";
    const std::string world_report_path =
        report_path + ".worlds.tsv";
    std::ofstream world_report(world_report_path, std::ios::binary);
    if (!world_report)
        throw std::runtime_error("cannot create " + world_report_path);
    world_report
        << "archive_path\tdirectory\tentries\tworld_revision"
           "\tlegacy_value\tlegacy_float\tfake_hud"
           "\tpanel_revision\tpanel_camera\tpanel_test_event"
           "\trnd_revision\trnd_environment\trnd_test_event"
           "\tobject_dir_revision\tsubdirectories\tlegacy_transform\n";
    size_t directories = 0;
    size_t clips = 0;
    size_t world_directories = 0;
    for (const auto& entry : archive.entries()) {
        if (extension(entry.name) != ".milo_ps2") continue;
        const auto container = gh::milo::parse_container(
            archive.read_entry(entry, ark_paths));
        const auto directory = gh::milo::parse_directory(
            gh::milo::container_payload(container));
        if (directory.dir_type == "WorldDir") {
            const auto world =
                gh::milo_object::parse_world_dir11(
                    directory.dir_body_bytes);
            const auto& panel = world.panel_directory;
            const auto& rnd = panel.render_directory;
            const auto& object_dir = rnd.object_directory;
            const auto join_strings =
                [](const std::vector<std::string>& values) {
                    std::string joined;
                    for (size_t index = 0;
                         index < values.size(); ++index) {
                        if (index) joined += ',';
                        joined += values[index];
                    }
                    return joined;
                };
            const auto join_floats =
                [](const std::array<float, 12>& values) {
                    std::string joined;
                    for (size_t index = 0;
                         index < values.size(); ++index) {
                        if (index) joined += ',';
                        joined += std::to_string(values[index]);
                    }
                    return joined;
                };
            world_report
                << cell(entry.full_path) << '\t'
                << cell(directory.dir_name) << '\t'
                << directory.entries.size() << '\t'
                << world.revision << '\t'
                << world.legacy_value << '\t'
                << world.legacy_float << '\t'
                << cell(world.fake_hud_filename) << '\t'
                << panel.revision << '\t'
                << cell(panel.camera) << '\t'
                << cell(panel.test_event) << '\t'
                << rnd.revision << '\t'
                << cell(rnd.environment) << '\t'
                << cell(rnd.test_event) << '\t'
                << object_dir.revision << '\t'
                << cell(join_strings(object_dir.subdirectories))
                << '\t'
                << cell(join_floats(world.legacy_transform))
                << '\n';
            ++world_directories;
        }
        if (directory.dir_type != "CharClipSet") continue;
        uint32_t clip_count = 0;
        for (const auto& object : directory.entries)
            if (object.type == "CharClipSamples") ++clip_count;
        const auto root = gh::milo_object::parse_char_clip_set14(
            directory.dir_body_bytes, clip_count);
        const auto join =
            [](const std::vector<std::string>& values) {
                std::string result;
                for (size_t index = 0;
                     index < values.size(); ++index) {
                    if (index) result += ',';
                    result += values[index];
                }
                return result;
            };
        root_report
            << cell(entry.full_path) << '\t'
            << cell(directory.dir_name) << '\t'
            << clip_count << '\t'
            << directory.entries.size() << '\t'
            << cell(root.object_directory.object_fields.type) << '\t'
            << root.object_directory.viewports.size() << '\t'
            << root.blend_width << '\t'
            << root.play_flags << '\t'
            << (root.move_self ? 1 : 0) << '\t'
            << cell(join(root.recenter_targets)) << '\t'
            << cell(join(root.recenter_average)) << '\t'
            << (root.recenter_slide ? 1 : 0) << '\t'
            << cell(root.legacy_type) << '\t'
            << root.legacy_type_version << '\n';
        for (const auto& summary : root.clips) {
            const gh::milo::Entry* object = nullptr;
            for (const auto& candidate : directory.entries) {
                if (candidate.type == "CharClipSamples" &&
                    candidate.name == summary.clip) {
                    object = &candidate;
                    break;
                }
            }
            if (!object)
                throw std::runtime_error(
                    "clip summary body missing: " +
                    entry.full_path + "::" + summary.clip);
            const auto clip =
                gh::milo_object::parse_char_clip_samples10(
                    object->body_bytes);
            size_t transition_nodes = 0;
            size_t string_bytes =
                clip.legacy_enter_event.size() +
                clip.legacy_exit_event.size();
            for (const auto& transition : clip.transitions) {
                transition_nodes += transition.nodes.size();
                string_bytes += transition.clip.size();
            }
            for (const auto& event : clip.events)
                string_bytes += event.script.size();
            for (const auto& channel : clip.full.channels)
                string_bytes += channel.size();
            for (const auto& channel : clip.one.channels)
                string_bytes += channel.size();
            for (const auto& channel : clip.duplicate.channels)
                string_bytes += channel.size();
            const size_t sample_bytes =
                clip.full.sample_bytes.size() +
                clip.one.sample_bytes.size() +
                clip.duplicate.sample_bytes.size();
            report << cell(entry.full_path) << '\t'
                   << cell(summary.clip) << '\t'
                   << summary.flags << '\t'
                   << summary.size_bytes << '\t'
                   << clip.flags << '\t'
                   << clip.play_flags << '\t'
                   << clip.blend_width << '\t'
                   << clip.range << '\t'
                   << (clip.legacy_flag ? 1 : 0) << '\t'
                   << object->body_bytes.size() << '\t'
                   << sample_bytes << '\t'
                   << clip.full.sample_bytes.size() << '\t'
                   << clip.one.sample_bytes.size() << '\t'
                   << clip.duplicate.sample_bytes.size() << '\t'
                   << clip.full.channels.size() << '\t'
                   << clip.one.channels.size() << '\t'
                   << clip.duplicate.channels.size() << '\t'
                   << clip.full.sample_count << '\t'
                   << clip.one.sample_count << '\t'
                   << clip.duplicate.sample_count << '\t'
                   << clip.transitions.size() << '\t'
                   << transition_nodes << '\t'
                   << clip.events.size() << '\t'
                   << string_bytes << '\t'
                   << clip.full.compression << '\t'
                   << clip.one.compression << '\t'
                   << clip.duplicate.compression << '\t';
            const auto write_counts =
                [&](const auto& counts) {
                    for (size_t index = 0;
                         index < counts.size(); ++index) {
                        if (index) report << ',';
                        report << counts[index];
                    }
                };
            write_counts(clip.full.counts);
            report << '\t';
            write_counts(clip.one.counts);
            report << '\t';
            write_counts(clip.duplicate.counts);
            const auto facing_channels =
                [](const auto& channels) {
                    size_t count = 0;
                    for (const auto& channel : channels) {
                        if (channel.rfind("bone_facing.", 0) == 0 ||
                            channel.rfind("bone_facing_delta.", 0) == 0)
                            ++count;
                    }
                    return count;
                };
            report << '\t' << facing_channels(clip.full.channels)
                   << '\t' << facing_channels(clip.one.channels)
                   << '\n';
            ++clips;
        }
        ++directories;
    }
    std::printf(
        "target_summaries directories=%zu clips=%zu worlds=%zu "
        "report=%s roots=%s world_report=%s\n",
        directories, clips, world_directories, report_path.c_str(),
        root_report_path.c_str(), world_report_path.c_str());
    return 0;
}

int target_face_sweep(
    const gh::ark::ArkV3Reader& archive,
    const std::vector<std::string>& ark_paths,
    const std::string& report_path,
    const std::string& output_root) {
    fs::create_directories(fs::path(report_path).parent_path());
    std::ofstream report(report_path, std::ios::binary);
    if (!report)
        throw std::runtime_error("cannot create " + report_path);
    report
        << "voc\tmidi\tversion\tcurves\tfooter_bytes"
           "\ttime_spans\ttick_spans\tmidi_bytes"
           "\tpatched_bytes\tprefix_preserved\tstatus\n";
    size_t voc_count = 0;
    size_t paired_count = 0;
    size_t version_1200 = 0;
    size_t version_1500 = 0;
    size_t total_spans = 0;
    std::vector<BundleRecord> bundle_records;
    for (const auto& entry : archive.entries()) {
        if (extension(entry.name) != ".voc" ||
            entry.full_path.rfind("songs/", 0) != 0)
            continue;
        const auto animation =
            gh::milo_convert::parse_gh2_facefx_animation(
                archive.read_entry(entry, ark_paths));
        if (animation.version == 1200)
            ++version_1200;
        else if (animation.version == 1500)
            ++version_1500;
        const auto time_spans =
            gh::milo_convert::derive_gh1_singer_open_spans(
                animation);
        std::string midi_path = entry.full_path;
        midi_path.replace(
            midi_path.size() - 4, 4, ".mid");
        const auto midi_entry = archive.find(midi_path);
        if (!midi_entry) {
            report
                << cell(entry.full_path) << '\t'
                << cell(midi_path) << '\t'
                << animation.version << '\t'
                << animation.curves.size() << '\t'
                << animation.archive_footer.size() << '\t'
                << time_spans.size()
                << "\t0\t0\t0\t0\tmissing-midi\n";
            ++voc_count;
            continue;
        }
        const auto midi =
            archive.read_entry(*midi_entry, ark_paths);
        const auto tick_spans =
            gh::milo_convert::map_singer_face_times_to_midi(
                midi, time_spans);
        const auto patched =
            gh::milo_convert::append_gh1_singer_face_track(
                midi, tick_spans);
        const auto repeated =
            gh::milo_convert::append_gh1_singer_face_track(
                midi, tick_spans);
        if (repeated != patched)
            throw std::runtime_error(
                "target face sweep: nondeterministic MIDI: " +
                midi_path);
        const bool prefix_preserved =
            patched.size() > midi.size() &&
            std::equal(
                midi.begin(), midi.begin() + 10,
                patched.begin()) &&
            std::equal(
                midi.begin() + 12, midi.end(),
                patched.begin() + 12);
        if (!prefix_preserved)
            throw std::runtime_error(
                "target face sweep: MIDI prefix differs: " +
                midi_path);
        if (!output_root.empty()) {
            write_bundle_file(
                fs::path(output_root), midi_path, patched);
            bundle_records.push_back(
                {midi_path, "singer-face-midi",
                 entry.full_path, patched.size()});
        }
        report
            << cell(entry.full_path) << '\t'
            << cell(midi_path) << '\t'
            << animation.version << '\t'
            << animation.curves.size() << '\t'
            << animation.archive_footer.size() << '\t'
            << time_spans.size() << '\t'
            << tick_spans.size() << '\t'
            << midi.size() << '\t'
            << patched.size() << "\t1\tconverted\n";
        total_spans += tick_spans.size();
        ++paired_count;
        ++voc_count;
    }
    if (!output_root.empty())
        write_bundle_manifest(
            fs::path(output_root),
            "gh1-singer-face-bundle.tsv",
            std::move(bundle_records));
    std::printf(
        "target_face_sweep voc=%zu paired=%zu v1200=%zu "
        "v1500=%zu spans=%zu report=%s output=%s\n",
        voc_count, paired_count, version_1200, version_1500,
        total_spans, report_path.c_str(),
        output_root.empty() ? "<none>" : output_root.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) usage();
    const std::string command = argv[1];
    if (command != "scan" && command != "target-summaries" &&
        command != "target-face-sweep")
        usage();
    const std::string hdr_path = argv[2];
    std::vector<std::string> ark_paths;
    std::string report_path;
    std::string bundle_path;
    std::string output_path;
    std::string target_rnd_objects;
    std::string target_midi_parsers;
    std::string target_char_objects;
    for (int i = 3; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--report") {
            if (++i >= argc) usage();
            report_path = argv[i];
        } else if (argument == "--bundle") {
            if (++i >= argc) usage();
            bundle_path = argv[i];
        } else if (argument == "--out") {
            if (++i >= argc) usage();
            output_path = argv[i];
        } else if (argument == "--target-rnd-objects") {
            if (++i >= argc) usage();
            target_rnd_objects = argv[i];
        } else if (argument == "--target-midi-parsers") {
            if (++i >= argc) usage();
            target_midi_parsers = argv[i];
        } else if (argument == "--target-char-objects") {
            if (++i >= argc) usage();
            target_char_objects = argv[i];
        } else if (argument.rfind("--", 0) == 0) {
            usage();
        } else {
            ark_paths.push_back(argument);
        }
    }
    if (ark_paths.empty() || report_path.empty()) usage();
    if (command == "scan") {
        if (!output_path.empty()) usage();
        const bool any_target =
            !target_rnd_objects.empty() ||
            !target_midi_parsers.empty() ||
            !target_char_objects.empty();
        if (bundle_path.empty() != !any_target ||
            (!bundle_path.empty() &&
             (target_rnd_objects.empty() ||
              target_midi_parsers.empty() ||
              target_char_objects.empty())))
            usage();
    } else {
        if (!bundle_path.empty() ||
            !target_rnd_objects.empty() ||
            !target_midi_parsers.empty() ||
            !target_char_objects.empty())
            usage();
        if (command != "target-face-sweep" &&
            !output_path.empty())
            usage();
    }

    try {
        const auto archive = gh::ark::ArkV3Reader::load(hdr_path);
        if (command == "target-summaries")
            return target_summaries(
                archive, ark_paths, report_path);
        if (command == "target-face-sweep")
            return target_face_sweep(
                archive, ark_paths, report_path,
                output_path);
        fs::create_directories(fs::path(report_path).parent_path());
        std::vector<BundleRecord> character_bundle_records;
        std::vector<BundleRecord> venue_bundle_records;
        std::set<std::string> generated_model_dependencies;
        {
            const auto read_virtual =
                [&](const std::string& path) {
                    const auto entry = archive.find(path);
                    if (!entry)
                        throw std::runtime_error(
                            "animation manifest dependency missing: " +
                            path);
                    return archive.read_entry(*entry, ark_paths);
                };
            gh::milo_convert::Gh1AnimationManifest
                animation_manifest;
            size_t acs_count = 0;
            for (const auto& entry : archive.entries()) {
                if (extension(entry.name) != ".acs") continue;
                auto manifest =
                    gh::milo_convert::
                        compile_gh1_animation_manifest(
                            entry.full_path,
                            archive.read_entry(entry, ark_paths),
                            read_virtual);
                animation_manifest.clip_sets.insert(
                    animation_manifest.clip_sets.end(),
                    manifest.clip_sets.begin(),
                    manifest.clip_sets.end());
                ++acs_count;
            }
            const auto character_manifest =
                gh::milo_convert::compile_gh1_character_manifest(
                    "charsys/gen/charsys.dtb",
                    read_virtual("charsys/gen/charsys.dtb"),
                    read_virtual);
            const auto character_track_surfaces =
                gh::milo_convert::
                    compile_gh1_character_track_surfaces(
                        read_virtual("config/gen/characters.dtb"));
            std::map<std::string, std::string>
                character_package_by_archetype;
            for (const auto& character :
                 character_manifest.characters) {
                if (!character_package_by_archetype.emplace(
                         character.compiled_skeleton,
                         character.package_name).second)
                    throw std::runtime_error(
                        "duplicate character archetype owner: " +
                        character.compiled_skeleton);
            }
            size_t guitarist_characters = 0;
            size_t band_characters = 0;
            size_t character_controllers = 0;
            for (const auto& character :
                 character_manifest.characters) {
                if (!archive.find(character.compiled_skeleton))
                    throw std::runtime_error(
                        "character manifest skeleton missing: " +
                        character.compiled_skeleton);
                if (character.band_character)
                    ++band_characters;
                else
                    ++guitarist_characters;
                character_controllers +=
                    character.controllers.size();
            }
            size_t animation_count = 0;
            size_t graph_count = 0;
            size_t graph_converted_clips = 0;
            size_t package_count = 0;
            size_t package_clip_count = 0;
            std::map<
                std::string,
                std::vector<
                    gh::milo_convert::
                        Gh2CharacterAnimationBinding>>
                character_animation_bindings;
            std::map<std::string, std::string> graph_owners;
            const std::string package_report_path =
                report_path + ".packages.tsv";
            std::ofstream package_output(
                package_report_path, std::ios::binary);
            if (!package_output)
                throw std::runtime_error(
                    "cannot create " + package_report_path);
            package_output
                << "clip_set\trole\tdirectory\tclips\tentries"
                   "\tpayload_bytes\tcontainer_bytes\n";
            const fs::path package_directory =
                fs::path(report_path).parent_path() /
                "gh1-character-packages";
            fs::create_directories(package_directory);
            const std::string skeleton_report =
                report_path + ".skeletons.tsv";
            std::ofstream skeleton_output(
                skeleton_report, std::ios::binary);
            if (!skeleton_output)
                throw std::runtime_error(
                    "cannot create " + skeleton_report);
            skeleton_output
                << "clip_set\tarchetype\tchannel_bases"
                   "\tzero_geometry_meshes\tsource_skeleton"
                   "\tsynthetic_hand_anchors\tfacing_channels"
                   "\tunresolved\n";
            size_t skeleton_channel_bases = 0;
            size_t skeleton_resolved = 0;
            size_t skeleton_hand_anchors = 0;
            size_t skeleton_facing_channels = 0;
            size_t skeleton_missing = 0;
            for (const auto& clip_set :
                 animation_manifest.clip_sets) {
                animation_count += clip_set.animations.size();
                const std::string archetype_path =
                    gh::milo_convert::gh1_compiled_rnd_path(
                        clip_set.archetype_rnd);
                const auto archetype_entry =
                    archive.find(archetype_path);
                if (!archetype_entry)
                    throw std::runtime_error(
                        "animation archetype missing: " +
                        archetype_path);
                const auto archetype_container =
                    gh::milo::parse_container(
                        archive.read_entry(
                            *archetype_entry, ark_paths));
                const auto archetype =
                    gh::milo::parse_directory(
                        gh::milo::container_payload(
                            archetype_container));
                gh::milo_convert::Gh1ClipSetBuildInput
                    package_input;
                package_input.spec = clip_set;
                package_input.archetype = archetype;
                package_input.clips.reserve(
                    clip_set.animations.size());
                for (const auto& animation :
                     clip_set.animations) {
                    const std::string acp_path =
                        clip_set.source_directory + "/gen/" +
                        animation.name + ".acp";
                    const auto acp_entry = archive.find(acp_path);
                    if (!acp_entry)
                        throw std::runtime_error(
                            "animation package ACP missing: " +
                            acp_path);
                    package_input.clips.push_back(
                        gh::acp::parse(
                            archive.read_entry(
                                *acp_entry, ark_paths)));
                }
                std::set<std::string> zero_geometry_meshes;
                for (const auto& object : archetype.entries) {
                    if (object.type != "Mesh") continue;
                    const auto mesh =
                        gh::milo_object::parse_mesh(
                            object.body_bytes);
                    if (mesh.vertices.empty() &&
                        mesh.faces.empty())
                        zero_geometry_meshes.insert(object.name);
                }
                std::set<std::string> channel_bases;
                for (const auto& animation :
                     clip_set.animations) {
                    const std::string acp_path =
                        clip_set.source_directory + "/gen/" +
                        animation.name + ".acp";
                    if (!archive.find(acp_path))
                        throw std::runtime_error(
                            "animation manifest ACP missing: " +
                            acp_path);
                    for (const auto& channel :
                         animation.channels) {
                        const size_t dot =
                            channel.rfind('.');
                        if (dot == std::string::npos)
                            throw std::runtime_error(
                                "animation channel lacks suffix: " +
                                channel);
                        channel_bases.insert(
                            channel.substr(0, dot));
                    }
                }
                std::vector<std::string> hand_anchor_bases;
                std::vector<std::string> facing_bases;
                std::vector<std::string> unresolved_bases;
                size_t resolved_bases = 0;
                for (const auto& base : channel_bases) {
                    if (zero_geometry_meshes.find(
                            base + ".mesh") !=
                            zero_geometry_meshes.end())
                        ++resolved_bases;
                    else if (
                        base == "bone_facing" ||
                        base == "bone_facing_delta")
                        facing_bases.push_back(base);
                    else if (
                        base.size() >= 5 &&
                        base.compare(
                            base.size() - 5, 5, "_hand") == 0)
                        hand_anchor_bases.push_back(base);
                    else
                        unresolved_bases.push_back(base);
                }
                skeleton_output
                    << cell(clip_set.qualified_name) << '\t'
                    << cell(archetype_path) << '\t'
                    << channel_bases.size() << '\t'
                    << zero_geometry_meshes.size() << '\t'
                    << resolved_bases << '\t'
                    << hand_anchor_bases.size() << '\t'
                    << facing_bases.size() << '\t';
                for (size_t index = 0;
                     index < unresolved_bases.size(); ++index) {
                    if (index) skeleton_output << ',';
                    skeleton_output << cell(unresolved_bases[index]);
                }
                skeleton_output << '\n';
                skeleton_channel_bases += channel_bases.size();
                skeleton_resolved += resolved_bases;
                skeleton_hand_anchors += hand_anchor_bases.size();
                skeleton_facing_channels += facing_bases.size();
                skeleton_missing += unresolved_bases.size();
                if (const auto graph =
                        archive.find(clip_set.dependency_acg)) {
                    const auto parsed = gh::acg::parse(
                        archive.read_entry(*graph, ark_paths));
                    package_input.graph = parsed;
                    if (parsed.clips.size() !=
                        clip_set.animations.size())
                        throw std::runtime_error(
                            "animation graph/manifest clip count "
                            "differs: " +
                            clip_set.dependency_acg);
                    std::vector<std::string> clip_names;
                    clip_names.reserve(
                        clip_set.animations.size());
                    for (const auto& animation :
                         clip_set.animations)
                        clip_names.push_back(animation.name);
                    const auto transitions =
                        gh::milo_convert::
                            convert_gh1_acg_to_gh2_char_clip_transitions(
                                parsed, clip_names);
                    for (size_t index = 0;
                         index < clip_set.animations.size();
                         ++index) {
                        const std::string acp_path =
                            clip_set.source_directory + "/gen/" +
                            clip_set.animations[index].name +
                            ".acp";
                        const auto acp_entry =
                            archive.find(acp_path);
                        if (!acp_entry)
                            throw std::runtime_error(
                                "animation graph ACP missing: " +
                                acp_path);
                        const auto source_clip = gh::acp::parse(
                            archive.read_entry(
                                *acp_entry, ark_paths));
                        const auto target_clip =
                            gh::milo_convert::
                                convert_gh1_acp_to_gh2_char_clip_samples10(
                                    source_clip,
                                    transitions[index]);
                        const auto target_bytes =
                            gh::milo_object::
                                serialize_char_clip_samples10(
                                    target_clip);
                        if (gh::milo_object::
                                serialize_char_clip_samples10(
                                    gh::milo_object::
                                        parse_char_clip_samples10(
                                            target_bytes)) !=
                            target_bytes)
                            throw std::runtime_error(
                                "animation graph target clip "
                                "round trip differs: " +
                                acp_path);
                        (void)gh::milo_object::
                            char_clip_samples10_ps2_allocate_size(
                                target_clip);
                        ++graph_converted_clips;
                    }
                    graph_owners[clip_set.dependency_acg] =
                        clip_set.qualified_name;
                    ++graph_count;
                }
                const auto packages =
                    gh::milo_convert::
                        convert_gh1_clip_set_to_gh2_packages(
                            package_input);
                const auto repeat_packages =
                    gh::milo_convert::
                        convert_gh1_clip_set_to_gh2_packages(
                            package_input);
                if (repeat_packages.size() != packages.size())
                    throw std::runtime_error(
                        "animation package count is nondeterministic");
                for (size_t package_index = 0;
                     package_index < packages.size();
                     ++package_index) {
                    const auto& package =
                        packages[package_index];
                    const auto payload =
                        gh::milo::serialize_directory(
                            package.directory);
                    const auto container =
                        gh::milo::serialize_container(
                            gh::milo::make_container(payload));
                    const auto repeat_payload =
                        gh::milo::serialize_directory(
                            repeat_packages[package_index].directory);
                    const auto repeat_container =
                        gh::milo::serialize_container(
                            gh::milo::make_container(
                                repeat_payload));
                    if (payload != repeat_payload ||
                        container != repeat_container)
                        throw std::runtime_error(
                            "animation package bytes are "
                            "nondeterministic: " +
                            package.directory_name);
                    const fs::path output_path =
                        package_directory /
                        (package.directory_name + ".milo_ps2");
                    std::ofstream output(
                        output_path, std::ios::binary);
                    if (!output)
                        throw std::runtime_error(
                            "cannot create " +
                            output_path.string());
                    output.write(
                        reinterpret_cast<const char*>(
                            container.data()),
                        static_cast<std::streamsize>(
                            container.size()));
                    if (!output)
                        throw std::runtime_error(
                            "cannot write " +
                            output_path.string());
                    if (!bundle_path.empty()) {
                        const auto owner =
                            character_package_by_archetype.find(
                                archetype_path);
                        if (owner !=
                            character_package_by_archetype.end()) {
                            const std::string relative =
                                "char/" + owner->second +
                                "/anims/gen/" +
                                package.directory_name +
                                ".milo_ps2";
                        link_bundle_file(
                            fs::path(bundle_path),
                            relative, output_path);
                            character_bundle_records.push_back(
                                {relative, "character-animation",
                                 clip_set.qualified_name,
                                 container.size()});
                        } else if (
                            archetype_path.rfind(
                                "charsys/crowd/", 0) == 0) {
                            const std::string relative =
                                "char/crowd/anims/gen/" +
                                package.directory_name +
                                ".milo_ps2";
                            link_bundle_file(
                                fs::path(bundle_path),
                                relative, output_path);
                            venue_bundle_records.push_back(
                                {relative, "venue-crowd-animation",
                                 clip_set.qualified_name,
                                 container.size()});
                        }
                    }
                    character_animation_bindings[archetype_path]
                        .push_back(
                            {package.role, archetype_path,
                             "../../anims/" +
                                 package.directory_name +
                                 ".milo"});
                    package_output
                        << cell(clip_set.qualified_name) << '\t'
                        << gh::milo_convert::
                               gh2_clip_set_role_name(
                                   package.role)
                        << '\t'
                        << cell(package.directory_name) << '\t'
                        << package.source_clips.size() << '\t'
                        << package.directory.entries.size()
                        << '\t' << payload.size() << '\t'
                        << container.size() << '\n';
                    ++package_count;
                    package_clip_count +=
                        package.source_clips.size();
                }
            }
            const std::string model_report_path =
                report_path + ".models.tsv";
            std::ofstream model_output(
                model_report_path, std::ios::binary);
            if (!model_output)
                throw std::runtime_error(
                    "cannot create " + model_report_path);
            model_output
                << "character\tdirectory\troot_type\tentries"
                   "\tnative_transforms"
                   "\tnative_upper_twist_siblings"
                   "\tpayload_bytes\tcontainer_bytes"
                   "\tinternal_references\tface_transitions"
                   "\tface_controller_type\tcomplete"
                   "\tunresolved_dependencies"
                   "\tgenerated_dependencies\n";
            const fs::path model_directory =
                fs::path(report_path).parent_path() /
                "gh1-character-models";
            fs::create_directories(model_directory);
            size_t model_count = 0;
            size_t model_complete = 0;
            size_t model_native_transform_count = 0;
            size_t model_native_upper_twist_sibling_count = 0;
            for (const auto& character :
                 character_manifest.characters) {
                const auto source_container =
                    gh::milo::parse_container(
                        read_virtual(
                            character.compiled_skeleton));
                const auto source_model =
                    gh::milo::parse_directory(
                        gh::milo::container_payload(
                            source_container));
                gh::milo_convert::Gh1CharacterModelBuildInput
                    model_input;
                model_input.spec = character;
                model_input.source_model = source_model;
                if (!character.shadow_file.empty()) {
                    const std::string shadow_path =
                        gh::milo_convert::
                            gh1_compiled_rnd_path(
                                character.shadow_file);
                    const auto shadow_container =
                        gh::milo::parse_container(
                            read_virtual(shadow_path));
                    model_input.shadow_model =
                        gh::milo::parse_directory(
                            gh::milo::container_payload(
                                shadow_container));
                }
                if (!character.face_file.empty()) {
                    const std::string face_path =
                        gh::milo_convert::
                            gh1_compiled_rnd_path(
                                character.face_file);
                    const auto face_container =
                        gh::milo::parse_container(
                            read_virtual(face_path));
                    model_input.face_model =
                        gh::milo::parse_directory(
                            gh::milo::container_payload(
                                face_container));
                }
                model_input.animations =
                    character_animation_bindings[
                        character.compiled_skeleton];
                const auto model =
                    gh::milo_convert::
                        convert_gh1_character_to_gh2_model_package(
                            model_input);
                const auto repeat_model =
                    gh::milo_convert::
                        convert_gh1_character_to_gh2_model_package(
                            model_input);
                const auto payload =
                    gh::milo::serialize_directory(
                        model.directory);
                const auto repeat_payload =
                    gh::milo::serialize_directory(
                        repeat_model.directory);
                const auto container =
                    gh::milo::serialize_container(
                        gh::milo::make_container(payload));
                const auto repeat_container =
                    gh::milo::serialize_container(
                        gh::milo::make_container(
                            repeat_payload));
                if (payload != repeat_payload ||
                    container != repeat_container)
                    throw std::runtime_error(
                        "character model bytes are nondeterministic: " +
                        character.package_name);
                const auto reparsed =
                    gh::milo::parse_directory(payload);
                if (!reparsed.boundaries_exact ||
                    gh::milo::serialize_directory(reparsed) !=
                        payload)
                    throw std::runtime_error(
                        "character model does not reparse exactly: " +
                        character.package_name);
                const fs::path output_path =
                    model_directory /
                    (model.directory_name + ".milo_ps2");
                std::ofstream output(
                    output_path, std::ios::binary);
                if (!output)
                    throw std::runtime_error(
                        "cannot create " +
                        output_path.string());
                output.write(
                    reinterpret_cast<const char*>(
                        container.data()),
                    static_cast<std::streamsize>(
                        container.size()));
                if (!output)
                    throw std::runtime_error(
                        "cannot write " +
                        output_path.string());
                if (!bundle_path.empty()) {
                    const std::string relative =
                        "char/" + character.package_name +
                        "/og/gen/" +
                        model.directory_name +
                        ".milo_ps2";
                    link_bundle_file(
                        fs::path(bundle_path),
                        relative, output_path);
                    character_bundle_records.push_back(
                        {relative, "character-model",
                         character.compiled_skeleton,
                         container.size()});
                }
                generated_model_dependencies.insert(
                    model.generated_dependencies.begin(),
                    model.generated_dependencies.end());
                model_output
                    << cell(character.authored_name) << '\t'
                    << cell(model.directory_name) << '\t'
                    << cell(model.directory.dir_type) << '\t'
                    << model.directory.entries.size() << '\t'
                    << model.native_transform_count << '\t'
                    << model.native_upper_twist_sibling_count << '\t'
                    << payload.size() << '\t'
                    << container.size() << '\t'
                    << model.internal_reference_count << '\t'
                    << model.face_transition_count << '\t'
                    << cell(model.face_controller_type) << '\t'
                    << (model.complete ? 1 : 0) << '\t';
                for (size_t index = 0;
                     index <
                     model.unresolved_dependencies.size();
                     ++index) {
                    if (index) model_output << ',';
                    model_output << cell(
                        model.unresolved_dependencies[index]);
                }
                model_output << '\t';
                for (size_t index = 0;
                     index <
                     model.generated_dependencies.size();
                     ++index) {
                    if (index) model_output << ',';
                    model_output << cell(
                        model.generated_dependencies[index]);
                }
                model_output << '\n';
                ++model_count;
                if (model.complete) ++model_complete;
                model_native_transform_count +=
                    model.native_transform_count;
                model_native_upper_twist_sibling_count +=
                    model.native_upper_twist_sibling_count;
            }
            if (!bundle_path.empty()) {
                // Arena::Crowd promotes selected GH1 flat-card transforms to
                // one of these six source RndDir archetypes. They are runtime
                // venue assets, not selectable band characters, so preserve
                // their RndDir structure and package them beside crowd_main.
                // The DTA-authored actor/head/material recipe is applied by
                // gameplay when it instantiates the selected region.
                constexpr std::array<std::string_view, 6>
                    crowd_archetypes = {
                        "crowd_male01", "crowd_male02", "crowd_male03",
                        "crowd_female01", "crowd_female02",
                        "crowd_female03"};
                for (const std::string_view crowd : crowd_archetypes) {
                    const std::string source_path =
                        "charsys/crowd/gen/" + std::string(crowd) +
                        ".rnd_ps2";
                    const auto source_container = gh::milo::parse_container(
                        read_virtual(source_path));
                    const auto source_directory = gh::milo::parse_directory(
                        gh::milo::container_payload(source_container));
                    const auto converted =
                        gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                            source_directory, std::string(crowd));
                    if (!converted.complete)
                        throw std::runtime_error(
                            "crowd model conversion incomplete: " +
                            source_path);
                    const auto target_payload =
                        gh::milo::serialize_directory(converted.directory);
                    const auto target_bytes = gh::milo::serialize_container(
                        gh::milo::make_container(target_payload));
                    const auto repeat =
                        gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                            source_directory, std::string(crowd));
                    if (gh::milo::serialize_container(gh::milo::make_container(
                            gh::milo::serialize_directory(repeat.directory))) !=
                        target_bytes)
                        throw std::runtime_error(
                            "crowd model conversion is nondeterministic: " +
                            source_path);
                    const std::string relative =
                        "char/crowd/og/gen/" + std::string(crowd) +
                        ".milo_ps2";
                    write_bundle_file(fs::path(bundle_path), relative,
                                      target_bytes);
                    venue_bundle_records.push_back(
                        {relative, "venue-crowd-model", source_path,
                         target_bytes.size()});
                }
            }
            if (!bundle_path.empty()) {
                std::map<std::string, std::string>
                    source_surface_by_character;
                for (const auto& surface :
                     character_track_surfaces) {
                    source_surface_by_character.emplace(
                        surface.authored_name,
                        surface.source_surface);
                }
                size_t bundled_guitarist_surfaces = 0;
                for (const auto& character :
                     character_manifest.characters) {
                    if (character.role !=
                        gh::milo_convert::
                            Gh1CharacterRole::Guitarist)
                        continue;
                    const auto surface =
                        source_surface_by_character.find(
                            character.authored_name);
                    if (surface ==
                        source_surface_by_character.end())
                        throw std::runtime_error(
                            "character track surface missing for " +
                            character.authored_name);
                    const std::string source_path =
                        compiled_ps2_asset_path(surface->second);
                    const auto source_entry =
                        archive.find(source_path);
                    if (!source_entry)
                        throw std::runtime_error(
                            "character track surface asset missing: " +
                            source_path);
                    const auto bytes =
                        archive.read_entry(
                            *source_entry, ark_paths);
                    const std::string relative =
                        "track/surfaces/gen/" +
                        character.package_name +
                        "_keep.bmp_ps2";
                    write_bundle_file(
                        fs::path(bundle_path),
                        relative, bytes);
                    character_bundle_records.push_back(
                        {relative, "character-highway-surface",
                         source_path, bytes.size()});
                    ++bundled_guitarist_surfaces;
                }
                if (bundled_guitarist_surfaces !=
                    character_track_surfaces.size())
                    throw std::runtime_error(
                        "character track surface domain differs from "
                        "guitarist manifest");
                const std::set<std::string> expected_dependencies = {
                    "face-control-config:gh1_guitarist_morph_face",
                    "face-control-config:gh1_singer_morph_face",
                };
                if (generated_model_dependencies !=
                    expected_dependencies)
                    throw std::runtime_error(
                        "generated character dependency set differs");
                const auto clean_rnd =
                    gh::milo::read_file(target_rnd_objects);
                const auto clean_midi =
                    gh::milo::read_file(target_midi_parsers);
                const auto clean_char =
                    gh::milo::read_file(target_char_objects);
                const auto rnd_patch =
                    gh::milo_convert::
                        patch_gh2_rnd_objects_for_gh1_faces(
                            clean_rnd);
                const auto midi_patch =
                    gh::milo_convert::
                        patch_gh2_midi_parsers_for_gh1_singer_face(
                            clean_midi);
                const auto char_patch =
                    gh::milo_convert::
                        patch_gh2_char_objects_for_gh1_singer_face(
                            clean_char);
                if (rnd_patch.types_added != 2 ||
                    midi_patch.parsers_added != 1 ||
                    char_patch.handlers_added != 2 ||
                    gh::milo_convert::
                        patch_gh2_rnd_objects_for_gh1_faces(
                            clean_rnd).bytes != rnd_patch.bytes ||
                    gh::milo_convert::
                        patch_gh2_midi_parsers_for_gh1_singer_face(
                            clean_midi).bytes != midi_patch.bytes ||
                    gh::milo_convert::
                        patch_gh2_char_objects_for_gh1_singer_face(
                            clean_char).bytes != char_patch.bytes)
                    throw std::runtime_error(
                        "character config bundle is incomplete or "
                        "nondeterministic");
                const auto append_config =
                    [&](const std::string& relative,
                        const std::string& source,
                        const std::vector<uint8_t>& bytes) {
                        write_bundle_file(
                            fs::path(bundle_path),
                            relative, bytes);
                        character_bundle_records.push_back(
                            {relative, "character-config",
                             source, bytes.size()});
                    };
                append_config(
                    "system/run/milo/gen/rnd_objects.dtb",
                    target_rnd_objects, rnd_patch.bytes);
                append_config(
                    "config/gen/midi_parsers.dtb",
                    target_midi_parsers, midi_patch.bytes);
                append_config(
                    "char/gen/char_objects.dtb",
                    target_char_objects, char_patch.bytes);
                write_bundle_manifest(
                    fs::path(bundle_path),
                    "gh1-character-bundle.tsv",
                    character_bundle_records);
            }
            if (acs_count != 0) {
                const std::string animation_report =
                    report_path + ".animations.tsv";
                std::ofstream animation_output(
                    animation_report, std::ios::binary);
                if (!animation_output)
                    throw std::runtime_error(
                        "cannot create " + animation_report);
                animation_output <<
                    gh::milo_convert::gh1_animation_manifest_tsv(
                        animation_manifest);
                const std::string character_report =
                    report_path + ".characters.tsv";
                std::ofstream character_output(
                    character_report, std::ios::binary);
                if (!character_output)
                    throw std::runtime_error(
                        "cannot create " + character_report);
                character_output <<
                    gh::milo_convert::gh1_character_manifest_tsv(
                        character_manifest);
                std::printf(
                    "animation_manifest acs=%zu sets=%zu "
                    "animations=%zu graphs=%zu graph_clips=%zu "
                    "report=%s\n",
                    acs_count, animation_manifest.clip_sets.size(),
                    animation_count, graph_count,
                    graph_converted_clips,
                    animation_report.c_str());
                std::printf(
                    "character_manifest characters=%zu guitarists=%zu "
                    "band=%zu controllers=%zu report=%s\n",
                    character_manifest.characters.size(),
                    guitarist_characters, band_characters,
                    character_controllers,
                    character_report.c_str());
                std::printf(
                    "character_model_inventory packages=%zu "
                    "complete=%zu incomplete=%zu native_transforms=%zu "
                    "native_upper_twist_siblings=%zu "
                    "report=%s "
                    "output=%s bundle=%s\n",
                    model_count, model_complete,
                    model_count - model_complete,
                    model_native_transform_count,
                    model_native_upper_twist_sibling_count,
                    model_report_path.c_str(),
                    model_directory.string().c_str(),
                    bundle_path.empty()
                        ? "<none>" : bundle_path.c_str());

                const std::string graph_report =
                    report_path + ".graphs.tsv";
                std::ofstream graph_output(
                    graph_report, std::ios::binary);
                if (!graph_output)
                    throw std::runtime_error(
                        "cannot create " + graph_report);
                std::vector<
                    std::pair<std::string, std::string>>
                    dtb_strings;
                for (const auto& entry : archive.entries()) {
                    if (extension(entry.name) != ".dtb" &&
                        extension(entry.name) != ".seq")
                        continue;
                    const auto tree = gh::dtb::parse(
                        archive.read_entry(entry, ark_paths));
                    std::vector<std::string> values;
                    for (const auto& root : tree.root)
                        collect_dtb_strings(root, values);
                    for (auto& value : values)
                        dtb_strings.emplace_back(
                            entry.full_path, std::move(value));
                }
                graph_output
                    << "archive_path\tclips\tnodes\tmanifest_owner"
                       "\tfirst_target\tfirst_current_beat"
                       "\tfirst_next_beat\tdtb_references"
                       "\treference_count\tclassification\n";
                size_t graph_assets = 0;
                size_t graph_nodes = 0;
                size_t graph_runtime_owned = 0;
                size_t graph_unreferenced_residuals = 0;
                size_t graph_unresolved = 0;
                for (const auto& entry : archive.entries()) {
                    if (extension(entry.name) != ".acg") continue;
                    const auto graph = gh::acg::parse(
                        archive.read_entry(entry, ark_paths));
                    size_t nodes = 0;
                    for (const auto& clip : graph.clips)
                        nodes += clip.nodes.size();
                    graph_output << cell(entry.full_path) << '\t'
                                 << graph.clips.size() << '\t'
                                 << nodes << '\t';
                    if (const auto owner =
                            graph_owners.find(entry.full_path);
                        owner != graph_owners.end())
                        graph_output << cell(owner->second);
                    graph_output << '\t';
                    if (!graph.clips.empty() &&
                        !graph.clips.front().nodes.empty()) {
                        const auto& first =
                            graph.clips.front().nodes.front();
                        graph_output << first.target_clip_index;
                        graph_output << '\t' << first.current_beat;
                        graph_output << '\t' << first.next_beat;
                    } else {
                        graph_output << "\t\t";
                    }
                    graph_output << '\t';
                    const std::string graph_path =
                        lower(entry.full_path);
                    const std::string graph_name =
                        lower(entry.name);
                    bool first_reference = true;
                    size_t reference_count = 0;
                    for (const auto& [dtb_path, value] :
                          dtb_strings) {
                        const std::string normalized =
                            lower(value);
                        if (normalized != graph_path &&
                            normalized != graph_name)
                            continue;
                        if (!first_reference)
                            graph_output << ',';
                        graph_output
                            << cell(dtb_path + "::" + value);
                        first_reference = false;
                        ++reference_count;
                    }
                    const bool manifest_owned =
                        graph_owners.find(entry.full_path) !=
                        graph_owners.end();
                    std::string classification;
                    if (manifest_owned) {
                        classification = "runtime_manifest_owned";
                        ++graph_runtime_owned;
                    } else if (
                        reference_count == 0 &&
                        graph.clips.size() == 1 &&
                        nodes == 1) {
                        classification =
                            "unreferenced_single_node_residual";
                        ++graph_unreferenced_residuals;
                    } else {
                        classification = "unresolved_ownership";
                        ++graph_unresolved;
                    }
                    graph_output
                        << '\t' << reference_count << '\t'
                        << classification << '\n';
                    ++graph_assets;
                    graph_nodes += nodes;
                }
                if (graph_unresolved != 0)
                    throw std::runtime_error(
                        "animation graph inventory has unresolved "
                        "ownership");
                std::printf(
                    "animation_graph_inventory assets=%zu nodes=%zu "
                    "runtime_owned=%zu unreferenced_residuals=%zu "
                    "unresolved=%zu report=%s\n",
                    graph_assets, graph_nodes,
                    graph_runtime_owned,
                    graph_unreferenced_residuals,
                    graph_unresolved,
                    graph_report.c_str());
                if (skeleton_missing != 0)
                    throw std::runtime_error(
                        "animation skeleton inventory has unresolved "
                        "channel bases");
                std::printf(
                    "animation_skeleton_inventory channel_bases=%zu "
                    "source_skeleton=%zu synthetic_hand_anchors=%zu "
                    "facing_channels=%zu unresolved=%zu report=%s\n",
                    skeleton_channel_bases, skeleton_resolved,
                    skeleton_hand_anchors,
                    skeleton_facing_channels, skeleton_missing,
                    skeleton_report.c_str());
                std::printf(
                    "animation_package_inventory packages=%zu "
                    "clips=%zu report=%s output=%s\n",
                    package_count, package_clip_count,
                    package_report_path.c_str(),
                    package_directory.string().c_str());
            }
        }
        std::ofstream report(report_path, std::ios::binary);
        if (!report)
            throw std::runtime_error("cannot create " + report_path);
        const std::string acp_report_path =
            report_path + ".acp.tsv";
        std::ofstream acp_report(acp_report_path, std::ios::binary);
        if (!acp_report)
            throw std::runtime_error("cannot create " + acp_report_path);
        const std::string acp_value_report_path =
            report_path + ".acp-values.tsv";
        std::ofstream acp_value_report(
            acp_value_report_path, std::ios::binary);
        if (!acp_value_report)
            throw std::runtime_error(
                "cannot create " + acp_value_report_path);
        acp_value_report
            << "archive_path\tsource_object\tsource_class"
               "\tsource_revision\tsource_sample_set_revision"
               "\ttarget_revision\ttarget_char_clip_revision"
               "\ttarget_object_fields_revision\ttarget_object_type"
               "\ttarget_has_type_properties"
               "\tsource_start_beat\ttarget_start_beat"
               "\tsource_end_beat\ttarget_end_beat"
               "\tsource_beats_per_second\ttarget_beats_per_second"
               "\tsource_flags\texpected_flags\ttarget_flags"
               "\tsource_play_flags\texpected_play_flags"
               "\ttarget_play_flags"
               "\tsource_blend_width\ttarget_blend_width"
               "\tsource_trailing_bytes\ttarget_range"
               "\ttarget_legacy_flag\ttarget_transitions"
               "\ttarget_enter_event\ttarget_exit_event\ttarget_events";
        for (size_t set_index = 0; set_index < 2; ++set_index) {
            acp_value_report
                << "\tset" << set_index << "_source_channels"
                << "\tset" << set_index << "_target_channels"
                << "\tset" << set_index << "_source_channel_digest"
                << "\tset" << set_index << "_target_channel_digest"
                << "\tset" << set_index << "_source_sample_count"
                << "\tset" << set_index << "_target_sample_count"
                << "\tset" << set_index << "_source_compression"
                << "\tset" << set_index << "_target_compression"
                << "\tset" << set_index << "_source_frame_size"
                << "\tset" << set_index << "_recomputed_frame_size"
                << "\tset" << set_index << "_source_sample_bytes"
                << "\tset" << set_index << "_target_sample_bytes"
                << "\tset" << set_index << "_source_sample_digest"
                << "\tset" << set_index << "_target_sample_digest"
                << "\tset" << set_index << "_expected_counts_digest"
                << "\tset" << set_index << "_target_counts_digest";
        }
        acp_value_report
            << "\tduplicate_channels\tduplicate_counts_digest"
               "\tduplicate_compression\tduplicate_sample_count"
               "\tduplicate_sample_bytes\tstatus\n";
        const std::string venue_report_path =
            report_path + ".venues.tsv";
        std::ofstream venue_report(
            venue_report_path, std::ios::binary);
        if (!venue_report)
            throw std::runtime_error(
                "cannot create " + venue_report_path);
        venue_report
            << "archive_path\ttarget_path\tdirectory_type"
               "\tsubdirectories\tentries\tinternal_references"
               "\tpayload_bytes\tcontainer_bytes\tstatus\n";
        const std::string venue_placement_report_path =
            report_path + ".venue-placements.tsv";
        std::ofstream venue_placement_report(
            venue_placement_report_path, std::ios::binary);
        if (!venue_placement_report)
            throw std::runtime_error(
                "cannot create " + venue_placement_report_path);
        venue_placement_report
            << "source_venue\ttarget_venue\trole\tsource_helper"
               "\ttarget_waypoint\tflags";
        for (size_t index = 0; index < 12; ++index)
            venue_placement_report
                << "\tsource_world_" << index;
        for (size_t index = 0; index < 12; ++index)
            venue_placement_report
                << "\ttarget_transform_" << index;
        venue_placement_report << "\tstatus\n";
        const std::string venue_script_report_path =
            report_path + ".venue-scripts.tsv";
        std::ofstream venue_script_report(
            venue_script_report_path, std::ios::binary);
        if (!venue_script_report)
            throw std::runtime_error(
                "cannot create " + venue_script_report_path);
        venue_script_report
            << "source_path\ttarget_path\tsource_roots"
               "\trecognized_roots\tunrecognized_roots"
               "\tload_sections\tinitial_states"
               "\tfunctions\thandlers"
               "\tfunction_calls\tforeach_loops\tswitch_anim"
               "\tswitch_anim_rt\tanim_task\tanimate_to"
               "\tdelay_task\trandom_ranges\ttarget_bytes\tstatus"
               "\tstateful_ranges\tdetail\n";
        const std::string venue_camera_report_path =
            report_path + ".venue-cameras.tsv";
        std::ofstream venue_camera_report(
            venue_camera_report_path, std::ios::binary);
        if (!venue_camera_report)
            throw std::runtime_error(
                "cannot create " + venue_camera_report_path);
        venue_camera_report
            << "source_path\ttarget_path\trecords\tkeyframes"
               "\tshaky_records\tadaptive_subdivisions"
               "\tmax_position_error"
               "\tmax_rotation_error\tmax_screen_error"
               "\tmax_fov_error\tstatus\n";
        const std::string field_report_path =
            report_path + ".fields.tsv";
        std::ofstream field_report(
            field_report_path, std::ios::binary);
        if (!field_report)
            throw std::runtime_error(
                "cannot create " + field_report_path);
        field_report
            << "source_type\tsource_revision\tsource_field"
               "\ttarget_type\ttarget_revision\ttarget_field"
               "\tdisposition\tsource_objects\tstatus\trule"
               "\tverification\n";
        const std::string discarded_value_report_path =
            report_path + ".discarded-values.tsv";
        std::ofstream discarded_value_report(
            discarded_value_report_path, std::ios::binary);
        if (!discarded_value_report)
            throw std::runtime_error(
                "cannot create " + discarded_value_report_path);
        discarded_value_report
            << "source_type\tsource_revision\tsource_field\tvalue_class"
               "\tobjects\tstatus\n";
        const std::string discarded_value_instance_report_path =
            report_path + ".discarded-value-instances.tsv";
        std::ofstream discarded_value_instance_report(
            discarded_value_instance_report_path, std::ios::binary);
        if (!discarded_value_instance_report)
            throw std::runtime_error(
                "cannot create " +
                discarded_value_instance_report_path);
        discarded_value_instance_report
            << "archive_path\tsource_type\tsource_revision\tobject"
               "\tsource_field\tvalue_class\tstatus\n";
        const std::string mat_root_value_report_path =
            report_path + ".mat-root-values.tsv";
        std::ofstream mat_root_value_report(
            mat_root_value_report_path, std::ios::binary);
        if (!mat_root_value_report)
            throw std::runtime_error(
                "cannot create " + mat_root_value_report_path);
        mat_root_value_report
            << "archive_path\tsource_object\ttarget_object"
               "\tsource_revision\ttarget_revision\tsource_texture_count"
               "\ttarget_pass_count\tsource_blend\ttarget_blend"
               "\tsource_color\ttarget_color\tsource_use_environment"
               "\ttarget_use_environment\tsource_vertex_ambient"
               "\ttarget_prelit\tsource_vertex_dynamic\tsource_cull"
               "\ttarget_cull\tsource_multipass\tsource_normalize"
               "\tsource_z_mode\ttarget_z_mode\tsource_alpha_cut"
               "\ttarget_alpha_cut\tsource_alpha_write\ttarget_alpha_write"
               "\troot_stage_blend\ttarget_intensify\tstatus\n";
        const std::string mat_stage_value_report_path =
            report_path + ".mat-stage-values.tsv";
        std::ofstream mat_stage_value_report(
            mat_stage_value_report_path, std::ios::binary);
        if (!mat_stage_value_report)
            throw std::runtime_error(
                "cannot create " + mat_stage_value_report_path);
        mat_stage_value_report
            << "archive_path\tsource_object\tstage_index\ttarget_object"
               "\tsource_stage_blend\ttarget_blend\tsource_tex_gen"
               "\ttarget_tex_gen\tsource_wrap\ttarget_wrap"
               "\tsource_transform\ttarget_transform\tsource_texture"
               "\ttarget_diffuse_texture\ttarget_z_mode\ttarget_color"
               "\ttarget_use_environment\ttarget_prelit"
               "\ttarget_intensify\ttarget_next_pass\trule\tstatus\n";
        const std::string tex_value_report_path =
            report_path + ".tex-values.tsv";
        std::ofstream tex_value_report(
            tex_value_report_path, std::ios::binary);
        if (!tex_value_report)
            throw std::runtime_error(
                "cannot create " + tex_value_report_path);
        tex_value_report
            << "archive_path\tsource_object\ttarget_object"
               "\tsource_revision\ttarget_revision"
               "\ttarget_object_fields_revision"
               "\ttarget_object_type\ttarget_has_type_properties"
               "\tsource_width\ttarget_width"
               "\tsource_height\ttarget_height"
               "\tsource_bits_per_pixel\ttarget_bits_per_pixel"
               "\tsource_external_path\ttarget_external_path"
               "\tsource_mipmap_bias\ttarget_mipmap_bias"
               "\tsource_type\ttarget_type"
               "\tsource_use_external\ttarget_use_external"
               "\tsource_has_bitmap\ttarget_has_bitmap"
               "\tsource_bitmap_header\ttarget_bitmap_header"
               "\tsource_bitmap_bpp\ttarget_bitmap_bpp"
               "\tsource_bitmap_encoding\ttarget_bitmap_encoding"
               "\tsource_bitmap_mipmaps\ttarget_bitmap_mipmaps"
               "\tsource_bitmap_width\ttarget_bitmap_width"
               "\tsource_bitmap_height\ttarget_bitmap_height"
               "\tsource_bitmap_bpl\ttarget_bitmap_bpl"
               "\tsource_bitmap_wii_alpha\ttarget_bitmap_wii_alpha"
               "\tsource_reserved_digest\ttarget_reserved_digest"
               "\tsource_bitmap_bytes\ttarget_bitmap_bytes"
               "\tsource_bitmap_digest\ttarget_bitmap_digest\tstatus\n";
        const std::string trans_anim_value_report_path =
            report_path + ".trans-anim-values.tsv";
        std::ofstream trans_anim_value_report(
            trans_anim_value_report_path, std::ios::binary);
        if (!trans_anim_value_report)
            throw std::runtime_error(
                "cannot create " + trans_anim_value_report_path);
        trans_anim_value_report
            << "archive_path\tsource_object\ttarget_object"
               "\tsource_revision\ttarget_revision"
               "\ttarget_object_fields_revision\ttarget_object_type"
               "\ttarget_has_type_properties"
               "\tsource_animatable_revision"
               "\tsource_operation_count\tsource_operation_digest"
               "\tsource_animation_objects"
               "\tsource_animation_object_digest"
               "\tsource_animation_object_values"
               "\tsource_drawable_objects"
               "\tsource_drawable_object_digest"
               "\tfilter_required\tfilter_name\tfilter_scale"
               "\tfilter_offset\tfilter_start\tfilter_end\tfilter_type"
               "\tfilter_period"
               "\ttarget_animatable_revision\ttarget_frame\ttarget_rate"
               "\tsource_target\ttarget_target"
               "\tsource_rotation_keys\ttarget_rotation_keys"
               "\tsource_rotation_digest\ttarget_rotation_digest"
               "\tsource_translation_keys\ttarget_translation_keys"
               "\tsource_translation_digest\ttarget_translation_digest"
               "\tsource_keys_owner\ttarget_keys_owner"
               "\tsource_translation_spline\ttarget_translation_spline"
               "\tsource_repeat_translation\ttarget_repeat_translation"
               "\tsource_scale_keys\ttarget_scale_keys"
               "\tsource_scale_digest\ttarget_scale_digest"
               "\tsource_scale_spline\ttarget_scale_spline"
               "\tsource_follow_path\ttarget_follow_path"
               "\tsource_rotation_slerp\ttarget_rotation_slerp\tstatus\n";
        const std::string view_value_report_path =
            report_path + ".view-values.tsv";
        std::ofstream view_value_report(
            view_value_report_path, std::ios::binary);
        if (!view_value_report)
            throw std::runtime_error(
                "cannot create " + view_value_report_path);
        view_value_report
            << "archive_path\tsource_object\tsource_children_owner"
               "\tsource_revision\ttarget_revision"
               "\tsource_showing\ttarget_showing"
               "\tsource_sphere_digest\ttarget_sphere_digest"
               "\tsource_showing_range"
               "\tsource_operation_count\tsource_operation_digest"
               "\tfilter_required\tfilter_name"
               "\tanimation_references\tanimation_reference_digest"
               "\ttrans_anim_references"
               "\tnested_animation_references"
               "\tmat_anim_expansions"
               "\tdrawable_references\tdrawable_reference_digest"
               "\tenvironment_segments\tenvironment_scope_groups"
               "\tneeds_draw_only\tdraw_only_group"
               "\texpected_objects\ttarget_objects"
               "\texpected_object_digest\ttarget_object_digest"
               "\texpected_environment\ttarget_environment"
               "\texpected_draw_only\ttarget_draw_only"
               "\ttarget_lod\ttarget_lod_screen_size\tstatus\n";
        const std::string animation_payload_report_path =
            report_path + ".animation-payload-values.tsv";
        std::ofstream animation_payload_report(
            animation_payload_report_path, std::ios::binary);
        if (!animation_payload_report)
            throw std::runtime_error(
                "cannot create " + animation_payload_report_path);
        animation_payload_report
            << "archive_path\tsource_type\tsource_object"
               "\tsource_revision\ttarget_type\ttarget_revision"
               "\tsource_operations\tsource_operation_digest"
               "\tsource_memberships\tsource_membership_values"
               "\tfilter_required\tfilter_name\tkey_rows"
               "\tsource_payload_digest\ttarget_payload_digest\tstatus\n";
        const std::string mat_anim_value_report_path =
            report_path + ".mat-anim-values.tsv";
        std::ofstream mat_anim_value_report(
            mat_anim_value_report_path, std::ios::binary);
        if (!mat_anim_value_report)
            throw std::runtime_error(
                "cannot create " + mat_anim_value_report_path);
        mat_anim_value_report
            << "archive_path\tsource_object\tsource_revision"
               "\tsource_stages\tsource_stage_index\ttarget_object"
               "\ttarget_revision\ttarget_material\ttarget_keys_owner"
               "\ttranslation_keys\tscale_keys\trotation_keys"
               "\ttexture_keys\tcolor_keys\talpha_keys"
               "\tstage_payload_digest\ttarget_payload_digest"
               "\tfilter_required\temission\tstatus\n";
        const std::string object_field_value_report_path =
            report_path + ".object-field-values.tsv";
        std::ofstream object_field_value_report(
            object_field_value_report_path, std::ios::binary);
        if (!object_field_value_report)
            throw std::runtime_error(
                "cannot create " + object_field_value_report_path);
        object_field_value_report
            << "archive_path\tsource_type\tsource_object"
               "\tsource_revision\ttarget_type\ttarget_revision"
               "\tfield\tsource_value\texpected_target_value"
               "\ttarget_value\tconversion_rule\tstatus\n";
        const std::string particle_value_report_path =
            report_path + ".particle-values.tsv";
        std::ofstream particle_value_report(
            particle_value_report_path, std::ios::binary);
        if (!particle_value_report)
            throw std::runtime_error(
                "cannot create " + particle_value_report_path);
        particle_value_report
            << "archive_path\tsource_object\tsource_revision"
               "\ttarget_revision\tfield\tsource_value"
               "\texpected_target_value\ttarget_value"
               "\tconversion_rule\tstatus\n";
        const std::string font_value_report_path =
            report_path + ".font-values.tsv";
        std::ofstream font_value_report(
            font_value_report_path, std::ios::binary);
        if (!font_value_report)
            throw std::runtime_error(
                "cannot create " + font_value_report_path);
        font_value_report
            << "archive_path\tsource_object\tsource_revision"
               "\ttarget_revision\tfield\tsource_value"
               "\texpected_target_value\ttarget_value"
               "\tconversion_rule\tstatus\n";
        const std::string transform_value_report_path =
            report_path + ".transform-values.tsv";
        std::ofstream transform_value_report(
            transform_value_report_path, std::ios::binary);
        if (!transform_value_report)
            throw std::runtime_error(
                "cannot create " + transform_value_report_path);
        transform_value_report
            << "archive_path\tsource_index\tsource_type\tsource_object"
               "\tsource_revision\ttarget_type\ttarget_revision"
               "\tsource_local_world_digest\ttarget_local_world_digest"
               "\tsource_constraint\tmapped_constraint"
               "\texpected_constraint\ttarget_constraint"
               "\tsource_target\ttarget_target"
               "\tsource_preserve_scale\ttarget_preserve_scale"
               "\tsource_explicit_parent\texpected_parent\ttarget_parent"
               "\tsource_children\tchild_digest"
               "\tchild_owner_assignments\tchild_owner_digest"
               "\tlast_child_owner\tlast_child_owner_index"
               "\texplicit_parent_rule\tresolution_outcome\tstatus\n";
        const std::string mesh_value_report_path =
            report_path + ".mesh-values.tsv";
        std::ofstream mesh_value_report(
            mesh_value_report_path, std::ios::binary);
        if (!mesh_value_report)
            throw std::runtime_error(
                "cannot create " + mesh_value_report_path);
        mesh_value_report
            << "archive_path\tsource_object\ttarget_object"
               "\tsource_revision\ttarget_revision"
               "\tsource_transform_digest\ttarget_transform_digest"
               "\tsource_constraint\ttarget_constraint"
               "\tsource_target\ttarget_target"
               "\tsource_preserve_scale\ttarget_preserve_scale"
               "\tsource_parent\ttarget_parent"
               "\tsource_child_count\tsource_child_digest"
               "\tchild_fixup_status"
               "\tsource_showing\ttarget_showing"
               "\tsource_sphere_digest\ttarget_sphere_digest"
               "\tsource_material\ttarget_material"
               "\tsource_geometry_owner\ttarget_geometry_owner"
               "\tsource_mutable_flags\ttarget_mutable_flags"
               "\tsource_volume\ttarget_volume"
               "\tsource_has_bsp\ttarget_bsp_nodes"
               "\tsource_vertices\ttarget_vertices"
               "\tsource_vertex_digest\ttarget_vertex_digest"
               "\tsource_faces\ttarget_faces"
               "\tsource_face_digest\ttarget_face_digest"
               "\tsource_groups\ttarget_groups"
               "\tsource_group_digest\ttarget_group_digest"
               "\tsource_has_bones\ttarget_has_bones"
               "\tsource_bone_digest\ttarget_bone_digest"
               "\tsource_strip_groups\ttarget_strip_groups"
               "\tsource_strip_digest\ttarget_strip_digest"
               "\tstatus\n";
        const std::string dtb_report_path =
            report_path + ".dtb.tsv";
        std::ofstream dtb_report(
            dtb_report_path, std::ios::binary);
        if (!dtb_report)
            throw std::runtime_error(
                "cannot create " + dtb_report_path);
        dtb_report
            << "source_path\textension\tcontainer_control_word"
               "\tstorage\tcipher_seed\troot_nodes\ttotal_nodes"
               "\ttrailing_bytes\tfile_bytes\tstatus\n";
        acp_report
            << "archive_path\tobject\tset0_channels\tset0_samples"
               "\tset0_compression\tset0_frame_bytes"
               "\tset0_sample_bytes\tset0_facing_channels"
               "\tset1_channels\tset1_samples\tset1_compression"
               "\tset1_frame_bytes\tset1_sample_bytes"
               "\tset1_facing_channels\toverlap_channels\n";
        report
            << "archive_path\tsource_type\tsource_name\ttarget_type"
               "\ttarget_name\tstatus\tdetail\n";

        size_t assets = 0;
        size_t complete_assets = 0;
        size_t acp_assets = 0;
        size_t complete_acp_assets = 0;
        size_t acp_value_rows = 0;
        size_t acp_value_channels = 0;
        size_t acp_value_sample_bytes = 0;
        size_t converted_objects = 0;
        size_t synthesized_objects = 0;
        size_t blocked_objects = 0;
        size_t emitted_assets = 0;
        size_t semantic_objects = 0;
        size_t mat_root_value_rows = 0;
        size_t mat_stage_value_rows = 0;
        size_t tex_value_rows = 0;
        size_t tex_bitmap_rows = 0;
        size_t tex_bitmap_bytes = 0;
        size_t trans_anim_value_rows = 0;
        size_t trans_anim_filter_rows = 0;
        size_t trans_anim_key_rows = 0;
        size_t view_value_rows = 0;
        size_t view_animation_references = 0;
        size_t view_trans_anim_references = 0;
        size_t view_nested_animation_references = 0;
        size_t view_drawable_references = 0;
        size_t view_mat_anim_expansions = 0;
        size_t view_environment_scope_groups = 0;
        size_t view_draw_only_groups = 0;
        size_t view_filter_rows = 0;
        size_t animation_payload_rows = 0;
        size_t animation_payload_keys = 0;
        size_t animation_payload_filters = 0;
        size_t animation_payload_memberships = 0;
        size_t mat_anim_value_rows = 0;
        size_t mat_anim_source_objects = 0;
        size_t mat_anim_key_rows = 0;
        size_t mat_anim_filter_rows = 0;
        size_t mat_anim_override_rows = 0;
        size_t mat_anim_static_rows = 0;
        size_t object_field_value_objects = 0;
        size_t object_field_value_rows = 0;
        size_t particle_value_objects = 0;
        size_t particle_value_rows = 0;
        size_t particle_value_particles = 0;
        size_t particle_value_filters = 0;
        size_t particle_value_bounce_transforms = 0;
        size_t particle_value_memberships = 0;
        size_t font_value_objects = 0;
        size_t font_value_rows = 0;
        size_t font_value_glyphs = 0;
        size_t font_value_kerning_rows = 0;
        size_t font_value_nbsp_normalizations = 0;
        size_t transform_value_rows = 0;
        size_t transform_child_links = 0;
        size_t unresolved_transform_child_links = 0;
        size_t mesh_value_rows = 0;
        size_t acp_set1_nonconstant = 0;
        size_t acp_overlapping_channels = 0;
        size_t acp_facing_in_set1 = 0;
        size_t venue_assets = 0;
        size_t venue_references = 0;
        size_t venue_scripts = 0;
        size_t venue_script_load_sections = 0;
        size_t venue_script_initial_states = 0;
        size_t venue_script_bytes = 0;
        size_t venue_script_blocked = 0;
        size_t venue_camera_records = 0;
        size_t venue_camera_keyframes = 0;
        size_t venue_camera_blocked = 0;
        size_t venue_placement_assets = 0;
        size_t venue_placement_waypoints = 0;
        std::map<std::string, gh::milo::Directory>
            venue_directories;
        std::map<std::string, std::string>
            venue_directory_sources;
        std::map<
            std::string,
            std::vector<std::pair<std::string, std::string>>>
            venue_loaded_sections;
        std::set<std::string> venue_start_handlers;
        std::map<std::string, gh::milo_object::TransAnim6>
            shared_camera_animations;
        std::vector<uint8_t> shared_camera_path_policy;
        std::optional<float> shared_camera_helper_filter;
        std::map<std::string, size_t> blockers;
        std::map<std::pair<std::string, uint32_t>, size_t>
            semantic_field_instances;
        std::map<DiscardedValueKey, size_t>
            discarded_value_observations;
        std::vector<DiscardedValueInstance>
            discarded_value_instances;
        size_t dtb_assets = 0;
        size_t dtb_nodes = 0;
        size_t dtb_trailing_bytes = 0;
        std::map<uint32_t, size_t> dtb_control_words;
        for (const auto& entry : archive.entries()) {
            if (entry.full_path == "arena/gen/cam_paths.dtb")
                shared_camera_path_policy = archive.read_entry(entry, ark_paths);
            if (entry.full_path == "config/gen/gh.dtb")
                shared_camera_helper_filter = gh::milo_convert::gh1_camera_helper_filter_from_game_config(
                    archive.read_entry(entry, ark_paths));
            const std::string source_extension =
                extension(entry.name);
            if (source_extension != ".dtb" &&
                source_extension != ".seq")
                continue;
            const auto bytes =
                archive.read_entry(entry, ark_paths);
            const auto source = gh::dtb::parse(bytes);
            if (gh::dtb::serialize(source) != bytes)
                throw std::runtime_error(
                    "DTB/SEQ semantic round trip differs: " +
                    entry.full_path);
            const auto contracts =
                gh::milo_convert::
                    gh1_to_gh2_semantic_field_contracts_for(
                        "DTB", source.version);
            const auto source_schema =
                gh::milo_convert::
                    gh1_serialized_semantic_fields_for(
                        "DTB", source.version);
            std::set<std::string> contracted_source_fields;
            for (const auto& contract : contracts) {
                if (contract.source_field != "<synthesized>")
                    contracted_source_fields.insert(
                        contract.source_field);
            }
            const std::set<std::string> expected_source_fields(
                source_schema.begin(), source_schema.end());
            if (contracts.empty() || source_schema.empty() ||
                contracted_source_fields != expected_source_fields)
                throw std::runtime_error(
                    "incomplete semantic field contract for DTB "
                    "version " +
                    std::to_string(source.version) + ": " +
                    entry.full_path);
            ++semantic_field_instances[
                {"DTB", source.version}];
            ++dtb_assets;
            ++dtb_control_words[source.version];
            dtb_trailing_bytes +=
                source.trailing_bytes.size();
            size_t asset_nodes = 0;
            for (const auto& root : source.root)
                asset_nodes += count_dtb_nodes(*root);
            dtb_nodes += asset_nodes;
            dtb_report
                << cell(entry.full_path) << '\t'
                << cell(source_extension) << '\t'
                << source.version << '\t'
                << dtb_storage_name(source.storage) << '\t'
                << source.cipher_seed << '\t'
                << source.root.size() << '\t'
                << asset_nodes << '\t'
                << source.trailing_bytes.size() << '\t'
                << bytes.size() << "\texact_round_trip\n";
        }
        for (const auto& entry : archive.entries()) {
            if (extension(entry.name) == ".acp") {
                ++acp_assets;
                try {
                    const auto bytes =
                        archive.read_entry(entry, ark_paths);
                    const auto source = gh::acp::parse(bytes);
                    const auto contracts =
                        gh::milo_convert::
                            gh1_to_gh2_semantic_field_contracts_for(
                                "ACP", source.revision);
                    const auto source_schema =
                        gh::milo_convert::
                            gh1_serialized_semantic_fields_for(
                                "ACP", source.revision);
                    std::set<std::string> contracted_source_fields;
                    for (const auto& contract : contracts) {
                        if (contract.source_field != "<synthesized>")
                            contracted_source_fields.insert(
                                contract.source_field);
                    }
                    const std::set<std::string> expected_source_fields(
                        source_schema.begin(), source_schema.end());
                    if (contracts.empty() || source_schema.empty() ||
                        contracted_source_fields !=
                            expected_source_fields)
                        throw std::runtime_error(
                            "incomplete semantic field contract for ACP "
                            "revision " +
                            std::to_string(source.revision));
                    ++semantic_field_instances[
                        {"ACP", source.revision}];
                    ++discarded_value_observations[
                        {"ACP", source.revision, "trailing_bytes",
                         source.trailing_bytes.empty()
                             ? "empty"
                             : "nonempty"}];
                    discarded_value_instances.push_back({
                        entry.full_path, "ACP", source.revision,
                        entry.name, "trailing_bytes",
                        source.trailing_bytes.empty()
                            ? "empty"
                            : "nonempty"});
                    const auto facing_count =
                        [](const auto& channels) {
                            size_t count = 0;
                            for (const auto& channel : channels) {
                                if (channel.rfind(
                                        "bone_facing.", 0) == 0 ||
                                    channel.rfind(
                                        "bone_facing_delta.", 0) == 0)
                                    ++count;
                            }
                            return count;
                        };
                    std::set<std::string> set0_channels(
                        source.channel_sets[0].channels.begin(),
                        source.channel_sets[0].channels.end());
                    size_t overlap = 0;
                    for (const auto& channel :
                         source.channel_sets[1].channels) {
                        if (set0_channels.find(channel) !=
                            set0_channels.end())
                            ++overlap;
                    }
                    const size_t set0_facing =
                        facing_count(
                            source.channel_sets[0].channels);
                    const size_t set1_facing =
                        facing_count(
                            source.channel_sets[1].channels);
                    acp_report
                        << cell(entry.full_path) << '\t'
                        << cell(source.object_name) << '\t'
                        << source.channel_sets[0].channels.size()
                        << '\t'
                        << source.channel_sets[0].sample_count
                        << '\t'
                        << source.channel_sets[0].compression
                        << '\t'
                        << source.channel_sets[0].frame_size
                        << '\t'
                        << source.channel_sets[0].sample_bytes.size()
                        << '\t' << set0_facing << '\t'
                        << source.channel_sets[1].channels.size()
                        << '\t'
                        << source.channel_sets[1].sample_count
                        << '\t'
                        << source.channel_sets[1].compression
                        << '\t'
                        << source.channel_sets[1].frame_size
                        << '\t'
                        << source.channel_sets[1].sample_bytes.size()
                        << '\t' << set1_facing << '\t'
                        << overlap << '\n';
                    if (!source.channel_sets[1].channels.empty() &&
                        source.channel_sets[1].sample_count != 1)
                        ++acp_set1_nonconstant;
                    if (overlap != 0)
                        ++acp_overlapping_channels;
                    if (set1_facing != 0)
                        ++acp_facing_in_set1;
                    const auto target =
                        gh::milo_convert::
                            convert_gh1_acp_to_gh2_char_clip_samples10(
                                source);
                    const auto target_bytes =
                        gh::milo_object::
                            serialize_char_clip_samples10(target);
                    const auto verify =
                        gh::milo_object::
                            parse_char_clip_samples10(target_bytes);
                    if (gh::milo_object::
                            serialize_char_clip_samples10(verify) !=
                        target_bytes)
                        throw std::runtime_error(
                            "target CharClipSamples round trip differs");
                    const auto repeat =
                        gh::milo_convert::
                            convert_gh1_acp_to_gh2_char_clip_samples10(
                                source);
                    if (gh::milo_object::
                            serialize_char_clip_samples10(repeat) !=
                        target_bytes)
                        throw std::runtime_error(
                            "target CharClipSamples conversion is "
                            "nondeterministic");
                    const uint32_t expected_flags =
                        source.flags & 0x7fffffffu;
                    const uint32_t expected_play_flags =
                        expected_gh2_clip_time_flags(
                            source.play_flags);
                    std::array<std::array<uint32_t, 10>, 2>
                        expected_counts{};
                    std::array<size_t, 2> recomputed_frame_sizes{};
                    const std::array<
                        const gh::milo_object::CharBonesSamples10*, 2>
                        target_sets = {&verify.full, &verify.one};
                    for (size_t set_index = 0;
                         set_index < target_sets.size();
                         ++set_index) {
                        const auto& source_set =
                            source.channel_sets[set_index];
                        const auto& target_set =
                            *target_sets[set_index];
                        expected_counts[set_index] =
                            expected_char_bones_counts(
                                source_set.channels);
                        for (const auto& channel :
                             source_set.channels)
                            recomputed_frame_sizes[set_index] +=
                                gh::acp::channel_file_size(
                                    channel,
                                    source_set.compression);
                        const uint64_t expected_bytes =
                            static_cast<uint64_t>(
                                recomputed_frame_sizes[set_index]) *
                            source_set.sample_count;
                        if (source_set.channels !=
                                target_set.channels ||
                            source_set.sample_count !=
                                target_set.sample_count ||
                            source_set.compression !=
                                target_set.compression ||
                            source_set.frame_size !=
                                recomputed_frame_sizes[set_index] ||
                            expected_bytes !=
                                source_set.sample_bytes.size() ||
                            source_set.sample_bytes !=
                                target_set.sample_bytes ||
                            expected_counts[set_index] !=
                                target_set.counts)
                            throw std::runtime_error(
                                "converted ACP sample-set values differ: " +
                                source.object_name);
                    }
                    if (source.class_name != "AnimClipSamples" ||
                        source.revision != 18 ||
                        source.sample_set_revision != 5 ||
                        !source.trailing_bytes.empty() ||
                        verify.revision != 10 ||
                        verify.char_clip_revision != 5 ||
                        verify.object_fields.revision != 0 ||
                        !verify.object_fields.type.empty() ||
                        verify.object_fields.has_type_properties ||
                        verify.object_fields.type_property_id != 0 ||
                        !verify.object_fields.type_properties.empty() ||
                        !same_float_bits(
                            source.start_beat,
                            verify.start_beat) ||
                        !same_float_bits(
                            source.end_beat,
                            verify.end_beat) ||
                        !same_float_bits(
                            source.beats_per_second,
                            verify.beats_per_second) ||
                        verify.flags != expected_flags ||
                        verify.play_flags != expected_play_flags ||
                        !same_float_bits(
                            source.blend_width,
                            verify.blend_width) ||
                        verify.range != 0.0f ||
                        verify.legacy_flag ||
                        !verify.transitions.empty() ||
                        !verify.legacy_enter_event.empty() ||
                        !verify.legacy_exit_event.empty() ||
                        !verify.events.empty() ||
                        !verify.duplicate.channels.empty() ||
                        verify.duplicate.counts !=
                            std::array<uint32_t, 10>{} ||
                        verify.duplicate.compression !=
                            verify.full.compression ||
                        verify.duplicate.sample_count !=
                            verify.full.sample_count ||
                        !verify.duplicate.sample_bytes.empty())
                        throw std::runtime_error(
                            "converted ACP wrapper/default values differ: " +
                            source.object_name);
                    acp_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source.object_name) << '\t'
                        << cell(source.class_name) << '\t'
                        << source.revision << '\t'
                        << source.sample_set_revision << '\t'
                        << verify.revision << '\t'
                        << verify.char_clip_revision << '\t'
                        << verify.object_fields.revision << '\t'
                        << cell(verify.object_fields.type) << '\t'
                        << (verify.object_fields.has_type_properties
                                ? 1
                                : 0)
                        << '\t' << std::setprecision(9)
                        << source.start_beat << '\t'
                        << verify.start_beat << '\t'
                        << source.end_beat << '\t'
                        << verify.end_beat << '\t'
                        << source.beats_per_second << '\t'
                        << verify.beats_per_second << '\t'
                        << source.flags << '\t'
                        << expected_flags << '\t'
                        << verify.flags << '\t'
                        << source.play_flags << '\t'
                        << expected_play_flags << '\t'
                        << verify.play_flags << '\t'
                        << source.blend_width << '\t'
                        << verify.blend_width << '\t'
                        << source.trailing_bytes.size() << '\t'
                        << verify.range << '\t'
                        << (verify.legacy_flag ? 1 : 0) << '\t'
                        << verify.transitions.size() << '\t'
                        << cell(verify.legacy_enter_event) << '\t'
                        << cell(verify.legacy_exit_event) << '\t'
                        << verify.events.size();
                    for (size_t set_index = 0;
                         set_index < target_sets.size();
                         ++set_index) {
                        const auto& source_set =
                            source.channel_sets[set_index];
                        const auto& target_set =
                            *target_sets[set_index];
                        acp_value_report
                            << '\t' << source_set.channels.size()
                            << '\t' << target_set.channels.size()
                            << '\t'
                            << string_vector_digest(
                                   source_set.channels)
                            << '\t'
                            << string_vector_digest(
                                   target_set.channels)
                            << '\t' << source_set.sample_count
                            << '\t' << target_set.sample_count
                            << '\t' << source_set.compression
                            << '\t' << target_set.compression
                            << '\t' << source_set.frame_size
                            << '\t'
                            << recomputed_frame_sizes[set_index]
                            << '\t'
                            << source_set.sample_bytes.size()
                            << '\t'
                            << target_set.sample_bytes.size()
                            << '\t'
                            << byte_vector_digest(
                                   source_set.sample_bytes)
                            << '\t'
                            << byte_vector_digest(
                                   target_set.sample_bytes)
                            << '\t'
                            << u32_array_digest(
                                   expected_counts[set_index])
                            << '\t'
                            << u32_array_digest(
                                   target_set.counts);
                        acp_value_channels +=
                            source_set.channels.size();
                        acp_value_sample_bytes +=
                            source_set.sample_bytes.size();
                    }
                    acp_value_report
                        << '\t' << verify.duplicate.channels.size()
                        << '\t'
                        << u32_array_digest(
                               verify.duplicate.counts)
                        << '\t'
                        << verify.duplicate.compression << '\t'
                        << verify.duplicate.sample_count << '\t'
                        << verify.duplicate.sample_bytes.size()
                        << "\texact\n";
                    ++acp_value_rows;
                    report << cell(entry.full_path) << "\tACP\t"
                           << cell(source.object_name)
                           << "\tCharClipSamples\t"
                           << cell(source.object_name)
                           << "\tconverted\trevision-18/5 ACP mapped to "
                              "native revision-10 CharClipSamples\n";
                    ++complete_acp_assets;
                    ++converted_objects;
                    ++semantic_objects;
                } catch (const std::exception& ex) {
                    ++blocked_objects;
                    ++blockers["ACP"];
                    report << cell(entry.full_path)
                           << "\tACP\t\tCharClipSamples\t\tblocked\t"
                           << cell(ex.what()) << '\n';
                }
                continue;
            }
            if (extension(entry.name) != ".rnd_ps2") continue;
            ++assets;
            try {
                const auto bytes = archive.read_entry(entry, ark_paths);
                const auto container = gh::milo::parse_container(bytes);
                const auto source = gh::milo::parse_directory(
                    gh::milo::container_payload(container));
                for (const auto& object : source.entries) {
                    const uint32_t revision = little_u32(
                        object.body_bytes,
                        entry.full_path + "::" + object.name);
                    const auto contracts =
                        gh::milo_convert::
                            gh1_to_gh2_semantic_field_contracts_for(
                                object.type, revision);
                    const auto source_schema =
                        gh::milo_convert::
                            gh1_serialized_semantic_fields_for(
                                object.type, revision);
                    std::set<std::string> contracted_source_fields;
                    for (const auto& contract : contracts) {
                        if (contract.source_field != "<synthesized>")
                            contracted_source_fields.insert(
                                contract.source_field);
                    }
                    const std::set<std::string> expected_source_fields(
                        source_schema.begin(), source_schema.end());
                    if (contracts.empty() || source_schema.empty() ||
                        contracted_source_fields !=
                            expected_source_fields)
                        throw std::runtime_error(
                            "incomplete semantic field contract for " +
                            object.type + " revision " +
                            std::to_string(revision));
                    ++semantic_field_instances[
                        {object.type, revision}];
                    observe_discarded_values(
                        object, entry.full_path,
                        discarded_value_observations,
                        discarded_value_instances);
                }
                std::string target_directory_name = stem(entry.name);
                const auto venue_path =
                    gh1_venue_target_path(entry.full_path);
                if (venue_path && venue_path->primary)
                    target_directory_name = venue_path->target_venue;
                std::string authored_draw_root;
                if (venue_path) {
                    const std::string candidate =
                        venue_path->primary
                            ? "venue.view"
                            : stem(entry.name) + ".view";
                    const bool candidate_exists =
                        std::any_of(
                            source.entries.begin(), source.entries.end(),
                            [&](const gh::milo::Entry& object) {
                                return object.type == "View" &&
                                       object.name == candidate;
                            });
                    if (candidate_exists)
                        authored_draw_root = candidate;
                }
                const auto result =
                    gh::milo_convert::
                        convert_gh1_directory_to_gh2_rnddir(
                            source, target_directory_name,
                            authored_draw_root);
                for (const auto& row : result.manifest) {
                    report << cell(entry.full_path) << '\t'
                           << cell(row.source_type) << '\t'
                           << cell(row.source_name) << '\t'
                           << cell(row.target_type) << '\t'
                           << cell(row.target_name) << '\t'
                           << cell(row.status) << '\t'
                           << cell(row.detail) << '\n';
                    if (row.status == "converted")
                        ++converted_objects;
                    else if (row.status == "synthesized")
                        ++synthesized_objects;
                    else if (row.status == "blocked") {
                        ++blocked_objects;
                        ++blockers[row.source_type];
                    }
                }
                if (!result.complete) continue;

                const auto target_payload =
                    gh::milo::serialize_directory(result.directory);
                const auto target_container =
                    gh::milo::make_container(target_payload);
                const auto target_bytes =
                    gh::milo::serialize_container(target_container);
                const auto verify_container =
                    gh::milo::parse_container(target_bytes);
                if (gh::milo::serialize_container(verify_container) !=
                    target_bytes)
                    throw std::runtime_error(
                        "target MILO container round trip differs");
                const auto verify_directory =
                    gh::milo::parse_directory(
                        gh::milo::container_payload(verify_container));
                if (!verify_directory.boundaries_exact ||
                    gh::milo::serialize_directory(verify_directory) !=
                        target_payload)
                    throw std::runtime_error(
                        "target GH2 directory round trip differs");
                if (gh::milo_object::serialize_rnd_dir8(
                        gh::milo_object::parse_rnd_dir8(
                            verify_directory.dir_body_bytes)) !=
                    verify_directory.dir_body_bytes)
                    throw std::runtime_error(
                        "target GH2 RndDir root round trip differs");
                for (const auto& target : verify_directory.entries) {
                    if (gh::milo_object::round_trip_gh2_object_body(
                            target.type, target.body_bytes,
                            verify_directory.dir_version) !=
                        target.body_bytes)
                        throw std::runtime_error(
                            "target GH2 object round trip differs: " +
                            target.type + " " + target.name);
                    ++semantic_objects;
                    if (target.type == "TransAnim" &&
                        target.name == "shaky_cam1.tnm") {
                        const auto animation =
                            gh::milo_object::parse_trans_anim6(
                                target.body_bytes);
                        const auto [found, inserted] =
                            shared_camera_animations.emplace(
                                target.name, animation);
                        if (!inserted &&
                            gh::milo_object::serialize_trans_anim6(
                                found->second) !=
                                target.body_bytes) {
                            throw std::runtime_error(
                                "conflicting shared camera animation: " +
                                target.name);
                        }
                    }
                }
                std::map<std::string, ExpectedTransformable>
                    expected_transforms;
                for (size_t source_index = 0;
                     source_index < source.entries.size();
                     ++source_index) {
                    const auto& source_object =
                        source.entries[source_index];
                    const auto transform =
                        source_transformable(source_object);
                    if (!transform) continue;
                    ExpectedTransformable expected;
                    expected.source_type = source_object.type;
                    expected.source_revision = little_u32(
                        source_object.body_bytes,
                        entry.full_path + "::" + source_object.name);
                    expected.source_index = source_index;
                    expected.source = *transform;
                    expected.constraint =
                        gh::milo_object::
                            convert_transformable_constraint8_to_9(
                                transform->constraint);
                    if (!expected_transforms.emplace(
                            source_object.name,
                            std::move(expected)).second)
                        throw std::runtime_error(
                            "duplicate source transform name: " +
                            source_object.name);
                }
                for (const auto& source_object : source.entries) {
                    const auto current =
                        expected_transforms.find(source_object.name);
                    if (current == expected_transforms.end())
                        continue;
                    for (const auto& child :
                         current->second.source.children) {
                        const auto found =
                            expected_transforms.find(child);
                        if (found == expected_transforms.end()) {
                            ++unresolved_transform_child_links;
                            continue;
                        }
                        found->second.parent = source_object.name;
                        found->second.child_owners.push_back(
                            source_object.name);
                        ++transform_child_links;
                    }
                    if (current->second.source.parent !=
                        source_object.name) {
                        current->second.parent =
                            current->second.source.parent;
                        current->second.constraint = 2;
                    }
                }
                for (const auto& [source_name, expected] :
                     expected_transforms) {
                    const std::string target_type =
                        expected.source_type == "View"
                            ? "Group"
                            : expected.source_type;
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == target_type &&
                                   target.name == source_name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted transformable is missing: " +
                            source_name);
                    const auto target = target_transformable(
                        *found, verify_directory.dir_version);
                    if (!target)
                        throw std::runtime_error(
                            "converted target is not transformable: " +
                            source_name);
                    const uint32_t mapped_constraint =
                        gh::milo_object::
                            convert_transformable_constraint8_to_9(
                                expected.source.constraint);
                    if (!same_float_bits(
                            expected.source.local,
                            target->value.local) ||
                        !same_float_bits(
                            expected.source.world,
                            target->value.world) ||
                        target->value.constraint !=
                            expected.constraint ||
                        target->value.target !=
                            expected.source.target ||
                        target->value.preserve_scale !=
                            expected.source.preserve_scale ||
                        target->value.parent != expected.parent)
                        throw std::runtime_error(
                            "converted transform graph values differ: " +
                            source_name);
                    std::string last_child_owner;
                    size_t last_child_owner_index = 0;
                    if (!expected.child_owners.empty()) {
                        last_child_owner =
                            expected.child_owners.back();
                        last_child_owner_index =
                            expected_transforms.at(
                                last_child_owner).source_index;
                    }
                    const bool self_parent =
                        expected.source.parent == source_name;
                    std::string resolution_outcome;
                    if (self_parent)
                        resolution_outcome =
                            expected.child_owners.empty()
                                ? "self_parent_no_child_owner"
                                : "self_parent_child_owner";
                    else
                        resolution_outcome =
                            !expected.child_owners.empty() &&
                                    last_child_owner_index >
                                        expected.source_index
                                ? "later_child_owner_wins"
                                : "explicit_parent_wins";
                    transform_value_report
                        << cell(entry.full_path) << '\t'
                        << expected.source_index << '\t'
                        << cell(expected.source_type) << '\t'
                        << cell(source_name) << '\t'
                        << expected.source_revision << '\t'
                        << cell(target_type) << '\t'
                        << target->revision << '\t'
                        << float_array_pair_digest(
                               expected.source.local,
                               expected.source.world)
                        << '\t'
                        << float_array_pair_digest(
                               target->value.local,
                               target->value.world)
                        << '\t'
                        << expected.source.constraint << '\t'
                        << mapped_constraint << '\t'
                        << expected.constraint << '\t'
                        << target->value.constraint << '\t'
                        << cell(expected.source.target) << '\t'
                        << cell(target->value.target) << '\t'
                        << (expected.source.preserve_scale ? 1 : 0)
                        << '\t'
                        << (target->value.preserve_scale ? 1 : 0)
                        << '\t'
                        << cell(expected.source.parent) << '\t'
                        << cell(expected.parent) << '\t'
                        << cell(target->value.parent) << '\t'
                        << expected.source.children.size() << '\t'
                        << string_vector_digest(
                               expected.source.children)
                        << '\t'
                        << expected.child_owners.size() << '\t'
                        << string_vector_digest(
                               expected.child_owners)
                        << '\t'
                        << cell(last_child_owner) << '\t'
                        << last_child_owner_index << '\t'
                        << (self_parent
                                ? "self_parent_keeps_loader_state"
                                : "explicit_parent_override_constraint_2")
                        << '\t' << resolution_outcome
                        << "\texact\n";
                    ++transform_value_rows;
                }
                for (const auto& source_object : source.entries) {
                    const bool audited =
                        source_object.type == "Cam" ||
                        source_object.type == "Environ" ||
                        source_object.type == "Flare" ||
                        source_object.type == "Light" ||
                        source_object.type == "MultiMesh" ||
                        source_object.type == "Text";
                    if (!audited) continue;
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == source_object.type &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted simple object is missing: " +
                            source_object.type + " " +
                            source_object.name);
                    const uint32_t source_revision = little_u32(
                        source_object.body_bytes,
                        entry.full_path + "::" + source_object.name);
                    const uint32_t target_revision = little_u32(
                        found->body_bytes,
                        entry.full_path + "::converted::" +
                            source_object.name);
                    const auto emit_field =
                        [&](const std::string& field,
                            const std::string& source_value,
                            const std::string& expected_value,
                            const std::string& target_value,
                            const std::string& rule,
                            bool matches) {
                            object_field_value_report
                                << cell(entry.full_path) << '\t'
                                << cell(source_object.type) << '\t'
                                << cell(source_object.name) << '\t'
                                << source_revision << '\t'
                                << cell(found->type) << '\t'
                                << target_revision << '\t'
                                << cell(field) << '\t'
                                << cell(source_value) << '\t'
                                << cell(expected_value) << '\t'
                                << cell(target_value) << '\t'
                                << cell(rule) << '\t'
                                << (matches ? "exact" : "mismatch")
                                << '\n';
                            ++object_field_value_rows;
                            if (!matches)
                                throw std::runtime_error(
                                    "converted simple object field differs: " +
                                    source_object.type + " " +
                                    source_object.name + "::" + field);
                        };
                    const auto emit_native_object_fields =
                        [&](const gh::milo_object::ObjectFields0& fields) {
                            emit_field(
                                "object_fields.revision", "<absent>", "0",
                                std::to_string(fields.revision),
                                "target_native_default",
                                fields.revision == 0);
                            emit_field(
                                "object_fields.type", "<absent>", "",
                                fields.type, "target_native_default",
                                fields.type.empty());
                            emit_field(
                                "object_fields.has_type_properties",
                                "<absent>", "0",
                                fields.has_type_properties ? "1" : "0",
                                "target_native_default",
                                !fields.has_type_properties);
                            emit_field(
                                "object_fields.type_property_id",
                                "<absent>", "0",
                                std::to_string(fields.type_property_id),
                                "target_native_default",
                                fields.type_property_id == 0);
                            emit_field(
                                "object_fields.type_properties.count",
                                "<absent>", "0",
                                std::to_string(
                                    fields.type_properties.size()),
                                "target_native_default",
                                fields.type_properties.empty());
                        };
                    const auto emit_drawable =
                        [&](const gh::milo_object::LegacyDrawable& source_value,
                            const gh::milo_object::Drawable3& target_value) {
                            const std::array<float, 4> default_sphere{};
                            const auto& expected_sphere =
                                source_value.revision > 0
                                    ? source_value.sphere
                                    : default_sphere;
                            const float expected_draw_order =
                                source_value.revision > 2
                                    ? source_value.draw_order
                                    : 0.0f;
                            emit_field(
                                "drawable.revision",
                                std::to_string(source_value.revision), "3",
                                std::to_string(target_value.revision),
                                "target_revision",
                                target_value.revision == 3);
                            emit_field(
                                "drawable.showing",
                                source_value.showing ? "1" : "0",
                                source_value.showing ? "1" : "0",
                                target_value.showing ? "1" : "0",
                                "retained",
                                source_value.showing ==
                                    target_value.showing);
                            emit_field(
                                "drawable.sphere",
                                float_values(source_value.sphere),
                                float_values(expected_sphere),
                                float_values(target_value.sphere),
                                source_value.revision > 0
                                    ? "retained"
                                    : "target_native_default",
                                same_float_bits(
                                    expected_sphere,
                                    target_value.sphere));
                            emit_field(
                                "drawable.draw_order",
                                float_value(source_value.draw_order),
                                float_value(expected_draw_order),
                                float_value(target_value.draw_order),
                                source_value.revision > 2
                                    ? "retained"
                                    : "target_native_default",
                                same_float_bits(
                                    expected_draw_order,
                                    target_value.draw_order));
                            emit_field(
                                "drawable.objects",
                                joined_strings(source_value.objects),
                                "<target_field_absent>",
                                "<target_field_absent>",
                                "target_class_field_absent",
                                source_value.objects.empty());
                            emit_field(
                                "drawable.legacy_target",
                                source_value.legacy_target,
                                "<target_field_absent>",
                                "<target_field_absent>",
                                "target_class_field_absent",
                                source_value.legacy_target.empty());
                        };

                    if (source_object.type == "Cam") {
                        const auto source_value =
                            gh::milo_object::parse_cam(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_cam12(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_field(
                            "near_plane", float_value(source_value.near_plane),
                            float_value(source_value.near_plane),
                            float_value(target_value.near_plane), "retained",
                            same_float_bits(
                                source_value.near_plane,
                                target_value.near_plane));
                        emit_field(
                            "far_plane", float_value(source_value.far_plane),
                            float_value(source_value.far_plane),
                            float_value(target_value.far_plane), "retained",
                            same_float_bits(
                                source_value.far_plane,
                                target_value.far_plane));
                        const float expected_y_fov =
                            std::atan(
                                0.75f *
                                std::tan(source_value.fov * 0.5f)) *
                            2.0f;
                        emit_field(
                            "y_fov", float_value(source_value.fov),
                            float_value(expected_y_fov),
                            float_value(target_value.y_fov),
                            "horizontal_to_vertical_4_3",
                            same_float_bits(
                                expected_y_fov, target_value.y_fov));
                        emit_field(
                            "screen_rect",
                            float_values(source_value.screen_rect),
                            float_values(source_value.screen_rect),
                            float_values(target_value.screen_rect),
                            "retained",
                            same_float_bits(
                                source_value.screen_rect,
                                target_value.screen_rect));
                        emit_field(
                            "z_range", float_values(source_value.z_range),
                            float_values(source_value.z_range),
                            float_values(target_value.z_range), "retained",
                            same_float_bits(
                                source_value.z_range,
                                target_value.z_range));
                        emit_field(
                            "target_texture", source_value.target_texture,
                            source_value.target_texture,
                            target_value.target_texture, "retained",
                            source_value.target_texture ==
                                target_value.target_texture);
                    } else if (source_object.type == "Environ") {
                        const auto source_value =
                            gh::milo_object::parse_environ(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_environ5(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_field(
                            "lights.count",
                            std::to_string(source_value.lights.size()),
                            std::to_string(source_value.lights.size()),
                            std::to_string(target_value.lights.size()),
                            "retained",
                            source_value.lights.size() ==
                                target_value.lights.size());
                        emit_field(
                            "lights.values",
                            joined_strings(source_value.lights),
                            string_vector_digest(source_value.lights),
                            string_vector_digest(target_value.lights),
                            "retained_digest",
                            source_value.lights == target_value.lights);
                        emit_field(
                            "ambient_color",
                            float_values(source_value.ambient_color),
                            float_values(source_value.ambient_color),
                            float_values(target_value.ambient_color),
                            "retained",
                            same_float_bits(
                                source_value.ambient_color,
                                target_value.ambient_color));
                        emit_field(
                            "fog_range",
                            float_values(source_value.fog_range),
                            float_values(source_value.fog_range),
                            float_values(target_value.fog_range), "retained",
                            same_float_bits(
                                source_value.fog_range,
                                target_value.fog_range));
                        emit_field(
                            "fog_color",
                            float_values(source_value.fog_color),
                            float_values(source_value.fog_color),
                            float_values(target_value.fog_color), "retained",
                            same_float_bits(
                                source_value.fog_color,
                                target_value.fog_color));
                        emit_field(
                            "fog_enabled",
                            source_value.fog_enabled ? "1" : "0",
                            source_value.fog_enabled ? "1" : "0",
                            target_value.fog_enabled ? "1" : "0",
                            "retained",
                            source_value.fog_enabled ==
                                target_value.fog_enabled);
                        emit_field(
                            "animate_from_preset", "<absent>", "1",
                            target_value.animate_from_preset ? "1" : "0",
                            "target_native_default",
                            target_value.animate_from_preset);
                        emit_field(
                            "fade_out", "<absent>", "0",
                            target_value.fade_out ? "1" : "0",
                            "target_native_default",
                            !target_value.fade_out);
                        emit_field(
                            "fade_start", "<absent>", "0",
                            float_value(target_value.fade_start),
                            "target_native_default",
                            same_float_bits(
                                0.0f, target_value.fade_start));
                        emit_field(
                            "fade_end", "<absent>", "1000",
                            float_value(target_value.fade_end),
                            "target_native_default",
                            same_float_bits(
                                1000.0f, target_value.fade_end));
                    } else if (source_object.type == "Flare") {
                        const auto source_value =
                            gh::milo_object::parse_flare(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_flare4(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_drawable(
                            source_value.drawable,
                            target_value.drawable);
                        emit_field(
                            "material", source_value.material,
                            source_value.material, target_value.material,
                            "retained",
                            source_value.material == target_value.material);
                        emit_field(
                            "sizes", float_values(source_value.sizes),
                            float_values(source_value.sizes),
                            float_values(target_value.sizes), "retained",
                            same_float_bits(
                                source_value.sizes,
                                target_value.sizes));
                        emit_field(
                            "range", float_values(source_value.range),
                            float_values(source_value.range),
                            float_values(target_value.range), "retained",
                            same_float_bits(
                                source_value.range,
                                target_value.range));
                        emit_field(
                            "steps", std::to_string(source_value.steps),
                            std::to_string(source_value.steps),
                            std::to_string(target_value.steps), "retained",
                            source_value.steps == target_value.steps);
                    } else if (source_object.type == "Light") {
                        const auto source_value =
                            gh::milo_object::parse_light(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_light6(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_field(
                            "color", float_values(source_value.color),
                            float_values(source_value.color),
                            float_values(target_value.color), "retained",
                            same_float_bits(
                                source_value.color, target_value.color));
                        emit_field(
                            "range", float_value(source_value.range),
                            float_value(source_value.range),
                            float_value(target_value.range), "retained",
                            same_float_bits(
                                source_value.range, target_value.range));
                        emit_field(
                            "serialized_type",
                            std::to_string(source_value.serialized_type),
                            std::to_string(source_value.serialized_type),
                            std::to_string(target_value.serialized_type),
                            "retained_pre_revision_14_encoding",
                            source_value.serialized_type ==
                                target_value.serialized_type);
                        emit_field(
                            "animate_color_from_preset", "<absent>", "1",
                            target_value.animate_color_from_preset
                                ? "1"
                                : "0",
                            "target_native_default",
                            target_value.animate_color_from_preset);
                        emit_field(
                            "animate_position_from_preset", "<absent>", "1",
                            target_value.animate_position_from_preset
                                ? "1"
                                : "0",
                            "target_native_default",
                            target_value.animate_position_from_preset);
                    } else if (source_object.type == "MultiMesh") {
                        const auto source_value =
                            gh::milo_object::parse_multi_mesh(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_multi_mesh1(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_drawable(
                            source_value.drawable,
                            target_value.drawable);
                        emit_field(
                            "mesh", source_value.mesh, source_value.mesh,
                            target_value.mesh, "retained",
                            source_value.mesh == target_value.mesh);
                        emit_field(
                            "transforms.count",
                            std::to_string(source_value.transforms.size()),
                            std::to_string(source_value.transforms.size()),
                            std::to_string(target_value.transforms.size()),
                            "retained",
                            source_value.transforms.size() ==
                                target_value.transforms.size());
                        emit_field(
                            "transforms.values",
                            float_array_vector_digest(
                                source_value.transforms),
                            float_array_vector_digest(
                                source_value.transforms),
                            float_array_vector_digest(
                                target_value.transforms),
                            "retained_digest",
                            same_float_array_vector(
                                source_value.transforms,
                                target_value.transforms));
                    } else if (source_object.type == "Text") {
                        const auto source_value =
                            gh::milo_object::parse_text(
                                source_object.body_bytes);
                        const auto target_value =
                            gh::milo_object::parse_text17(
                                found->body_bytes);
                        emit_native_object_fields(
                            target_value.object_fields);
                        emit_drawable(
                            source_value.drawable,
                            target_value.drawable);
                        emit_field(
                            "font", source_value.font, source_value.font,
                            target_value.font, "retained",
                            source_value.font == target_value.font);
                        emit_field(
                            "alignment",
                            std::to_string(source_value.alignment),
                            std::to_string(source_value.alignment),
                            std::to_string(target_value.alignment),
                            "retained",
                            source_value.alignment ==
                                target_value.alignment);
                        emit_field(
                            "text", source_value.text, source_value.text,
                            target_value.text, "retained",
                            source_value.text == target_value.text);
                        emit_field(
                            "color", float_values(source_value.color),
                            float_values(source_value.color),
                            float_values(target_value.color), "retained",
                            same_float_bits(
                                source_value.color, target_value.color));
                        emit_field(
                            "wrap_width",
                            float_value(source_value.wrap_width),
                            float_value(source_value.wrap_width),
                            float_value(target_value.wrap_width), "retained",
                            same_float_bits(
                                source_value.wrap_width,
                                target_value.wrap_width));
                        emit_field(
                            "leading", float_value(source_value.leading),
                            float_value(source_value.leading),
                            float_value(target_value.leading), "retained",
                            same_float_bits(
                                source_value.leading,
                                target_value.leading));
                        emit_field(
                            "fixed_length",
                            std::to_string(source_value.fixed_length),
                            std::to_string(source_value.fixed_length),
                            std::to_string(target_value.fixed_length),
                            "retained",
                            source_value.fixed_length ==
                                target_value.fixed_length);
                        emit_field(
                            "italics", float_value(source_value.italics),
                            float_value(source_value.italics),
                            float_value(target_value.italics), "retained",
                            same_float_bits(
                                source_value.italics,
                                target_value.italics));
                        emit_field(
                            "size", float_value(source_value.size),
                            float_value(source_value.size),
                            float_value(target_value.size), "retained",
                            same_float_bits(
                                source_value.size, target_value.size));
                        emit_field(
                            "markup", source_value.markup ? "1" : "0",
                            source_value.markup ? "1" : "0",
                            target_value.markup ? "1" : "0", "retained",
                            source_value.markup == target_value.markup);
                        emit_field(
                            "caps_mode",
                            std::to_string(source_value.caps_mode),
                            std::to_string(source_value.caps_mode),
                            std::to_string(target_value.caps_mode),
                            "retained",
                            source_value.caps_mode ==
                                target_value.caps_mode);
                    }
                    ++object_field_value_objects;
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "ParticleSys") continue;
                    const auto source_value =
                        gh::milo_object::parse_particle_sys(
                            source_object.body_bytes);
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "ParticleSys" &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted ParticleSys is missing: " +
                            source_object.name);
                    const auto target_value =
                        gh::milo_object::parse_particle_sys27(
                            found->body_bytes);
                    const auto emit_field =
                        [&](const std::string& field,
                            const std::string& source_field,
                            const std::string& expected_field,
                            const std::string& target_field,
                            const std::string& rule,
                            bool matches) {
                            particle_value_report
                                << cell(entry.full_path) << '\t'
                                << cell(source_object.name) << '\t'
                                << source_value.revision << '\t'
                                << target_value.revision << '\t'
                                << cell(field) << '\t'
                                << cell(source_field) << '\t'
                                << cell(expected_field) << '\t'
                                << cell(target_field) << '\t'
                                << cell(rule) << '\t'
                                << (matches ? "exact" : "mismatch")
                                << '\n';
                            ++particle_value_rows;
                            if (!matches)
                                throw std::runtime_error(
                                    "converted ParticleSys field differs: " +
                                    source_object.name + "::" + field);
                        };
                    const auto emit_array =
                        [&](const std::string& field,
                            const auto& source_field,
                            const auto& target_field) {
                            emit_field(
                                field, float_values(source_field),
                                float_values(source_field),
                                float_values(target_field), "retained",
                                same_float_bits(
                                    source_field, target_field));
                        };
                    const auto emit_float =
                        [&](const std::string& field, float source_field,
                            float target_field) {
                            emit_field(
                                field, float_value(source_field),
                                float_value(source_field),
                                float_value(target_field), "retained",
                                same_float_bits(
                                    source_field, target_field));
                        };
                    const auto emit_string =
                        [&](const std::string& field,
                            const std::string& source_field,
                            const std::string& target_field) {
                            emit_field(
                                field, source_field, source_field,
                                target_field, "retained",
                                source_field == target_field);
                        };
                    const auto emit_bool =
                        [&](const std::string& field, bool source_field,
                            bool target_field) {
                            emit_field(
                                field, source_field ? "1" : "0",
                                source_field ? "1" : "0",
                                target_field ? "1" : "0", "retained",
                                source_field == target_field);
                        };
                    const auto emit_u32 =
                        [&](const std::string& field,
                            uint32_t source_field,
                            uint32_t target_field) {
                            emit_field(
                                field, std::to_string(source_field),
                                std::to_string(source_field),
                                std::to_string(target_field), "retained",
                                source_field == target_field);
                        };

                    emit_field(
                        "object_fields.revision", "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.revision),
                        "target_native_default",
                        target_value.object_fields.revision == 0);
                    emit_field(
                        "object_fields.type", "<absent>", "",
                        target_value.object_fields.type,
                        "target_native_default",
                        target_value.object_fields.type.empty());
                    emit_field(
                        "object_fields.has_type_properties",
                        "<absent>", "0",
                        target_value.object_fields.has_type_properties
                            ? "1"
                            : "0",
                        "target_native_default",
                        !target_value.object_fields.has_type_properties);
                    emit_field(
                        "object_fields.type_property_id", "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.type_property_id),
                        "target_native_default",
                        target_value.object_fields.type_property_id == 0);
                    emit_field(
                        "object_fields.type_properties.count",
                        "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.type_properties
                                .size()),
                        "target_native_default",
                        target_value.object_fields.type_properties.empty());
                    emit_field(
                        "animatable.revision",
                        std::to_string(
                            source_value.animatable.revision),
                        "4",
                        std::to_string(target_value.animatable.revision),
                        "target_revision",
                        target_value.animatable.revision == 4);
                    emit_field(
                        "animatable.frame", "<absent>", "0",
                        float_value(target_value.animatable.frame),
                        "target_native_default",
                        same_float_bits(
                            0.0f, target_value.animatable.frame));
                    emit_field(
                        "animatable.rate", "<absent>", "0",
                        std::to_string(target_value.animatable.rate),
                        "target_native_default",
                        target_value.animatable.rate == 0);
                    const auto settings =
                        expected_legacy_anim_settings(
                            source_value.animatable);
                    const bool filter_required =
                        settings.scale != 1.0f ||
                        settings.offset != 0.0f ||
                        settings.minimum != settings.maximum;
                    const bool filter_present =
                        validate_legacy_animation_filter(
                            verify_directory.entries,
                            source_object.name,
                            source_value.animatable);
                    emit_field(
                        "animatable.operations",
                        legacy_anim_operation_digest(
                            source_value.animatable.operations),
                        filter_required
                            ? stem(source_object.name) + ".filt"
                            : "<no_filter>",
                        filter_present
                            ? stem(source_object.name) + ".filt"
                            : "<no_filter>",
                        "legacy_operations_to_filter",
                        filter_present == filter_required);
                    emit_field(
                        "animatable.memberships",
                        joined_strings(source_value.animatable.objects),
                        string_vector_digest(
                            source_value.animatable.objects),
                        string_vector_digest(
                            source_value.animatable.objects),
                        source_value.animatable.objects.empty()
                            ? "empty"
                            : "view_animation_graph_closure",
                        true);
                    particle_value_memberships +=
                        source_value.animatable.objects.size();
                    if (filter_present) ++particle_value_filters;

                    const std::array<float, 4> default_sphere{};
                    const auto& expected_sphere =
                        source_value.drawable.revision > 0
                            ? source_value.drawable.sphere
                            : default_sphere;
                    const float expected_draw_order =
                        source_value.drawable.revision > 2
                            ? source_value.drawable.draw_order
                            : 0.0f;
                    emit_field(
                        "drawable.revision",
                        std::to_string(source_value.drawable.revision),
                        "3",
                        std::to_string(target_value.drawable.revision),
                        "target_revision",
                        target_value.drawable.revision == 3);
                    emit_bool(
                        "drawable.showing",
                        source_value.drawable.showing,
                        target_value.drawable.showing);
                    emit_field(
                        "drawable.sphere",
                        float_values(source_value.drawable.sphere),
                        float_values(expected_sphere),
                        float_values(target_value.drawable.sphere),
                        source_value.drawable.revision > 0
                            ? "retained"
                            : "target_native_default",
                        same_float_bits(
                            expected_sphere,
                            target_value.drawable.sphere));
                    emit_field(
                        "drawable.draw_order",
                        float_value(source_value.drawable.draw_order),
                        float_value(expected_draw_order),
                        float_value(target_value.drawable.draw_order),
                        source_value.drawable.revision > 2
                            ? "retained"
                            : "target_native_default",
                        same_float_bits(
                            expected_draw_order,
                            target_value.drawable.draw_order));
                    emit_field(
                        "drawable.objects",
                        joined_strings(source_value.drawable.objects),
                        "<target_field_absent>",
                        "<target_field_absent>",
                        "target_class_field_absent",
                        source_value.drawable.objects.empty());
                    emit_field(
                        "drawable.legacy_target",
                        source_value.drawable.legacy_target,
                        "<target_field_absent>",
                        "<target_field_absent>",
                        "target_class_field_absent",
                        source_value.drawable.legacy_target.empty());

                    emit_array("life", source_value.life, target_value.life);
                    emit_array(
                        "box_extent_1", source_value.box_extent_1,
                        target_value.box_extent_1);
                    emit_array(
                        "box_extent_2", source_value.box_extent_2,
                        target_value.box_extent_2);
                    emit_array(
                        "speed", source_value.speed, target_value.speed);
                    emit_array(
                        "pitch", source_value.pitch, target_value.pitch);
                    emit_array("yaw", source_value.yaw, target_value.yaw);
                    emit_array(
                        "emit_rate", source_value.emit_rate,
                        target_value.emit_rate);
                    emit_array(
                        "start_size", source_value.start_size,
                        target_value.start_size);
                    emit_array(
                        "delta_size", source_value.delta_size,
                        target_value.delta_size);
                    emit_array(
                        "start_color_low",
                        source_value.start_color_low,
                        target_value.start_color_low);
                    emit_array(
                        "start_color_high",
                        source_value.start_color_high,
                        target_value.start_color_high);
                    emit_array(
                        "end_color_low", source_value.end_color_low,
                        target_value.end_color_low);
                    emit_array(
                        "end_color_high", source_value.end_color_high,
                        target_value.end_color_high);
                    emit_array(
                        "force_direction", source_value.force_direction,
                        target_value.force_direction);
                    emit_string(
                        "material", source_value.material,
                        target_value.material);
                    emit_u32("type", source_value.type, target_value.type);
                    emit_float(
                        "grow_ratio", source_value.grow_ratio,
                        target_value.grow_ratio);
                    emit_float(
                        "shrink_ratio", source_value.shrink_ratio,
                        target_value.shrink_ratio);
                    emit_float(
                        "mid_color_ratio", source_value.mid_color_ratio,
                        target_value.mid_color_ratio);
                    emit_array(
                        "mid_color_low", source_value.mid_color_low,
                        target_value.mid_color_low);
                    emit_array(
                        "mid_color_high", source_value.mid_color_high,
                        target_value.mid_color_high);
                    emit_u32(
                        "max_particles", source_value.max_particles,
                        target_value.max_particles);
                    emit_array(
                        "bubble_period", source_value.bubble_period,
                        target_value.bubble_period);
                    emit_array(
                        "bubble_size", source_value.bubble_size,
                        target_value.bubble_size);
                    emit_bool(
                        "bubble", source_value.bubble,
                        target_value.bubble);
                    emit_float(
                        "relative_motion", source_value.relative_motion,
                        target_value.relative_motion);
                    emit_field(
                        "relative_parent", "<absent>", "",
                        target_value.relative_parent,
                        "target_native_default",
                        target_value.relative_parent.empty());
                    emit_string(
                        "emitter_mesh", source_value.emitter_mesh,
                        target_value.emitter_mesh);
                    emit_bool(
                        "preserve_particles",
                        source_value.preserve_particles,
                        target_value.preserve_particles);
                    emit_field(
                        "particles.count",
                        std::to_string(source_value.particles.size()),
                        std::to_string(source_value.particles.size()),
                        std::to_string(target_value.particles.size()),
                        "retained",
                        source_value.particles.size() ==
                            target_value.particles.size());
                    emit_field(
                        "particles.values",
                        particle_value_digest(source_value.particles),
                        particle_value_digest(source_value.particles),
                        particle_value_digest(target_value.particles),
                        "retained_digest",
                        same_particle_values(
                            source_value.particles,
                            target_value.particles));
                    particle_value_particles +=
                        source_value.particles.size();

                    const float bounce_normal_squared =
                        source_value.bounce_plane[0] *
                            source_value.bounce_plane[0] +
                        source_value.bounce_plane[1] *
                            source_value.bounce_plane[1] +
                        source_value.bounce_plane[2] *
                            source_value.bounce_plane[2];
                    if (!std::isfinite(bounce_normal_squared))
                        throw std::runtime_error(
                            "ParticleSys source bounce plane is non-finite: " +
                            source_object.name);
                    const bool has_bounce_transform =
                        source_value.bounce_enabled &&
                        bounce_normal_squared > 0.0f;
                    const std::string expected_bounce_name =
                        has_bounce_transform
                            ? stem(source_object.name) + "_bounce.trans"
                            : std::string();
                    emit_field(
                        "bounce",
                        source_value.bounce_enabled ? "1" : "0",
                        expected_bounce_name, target_value.bounce,
                        has_bounce_transform
                            ? "plane_to_native_trans_reference"
                            : (source_value.bounce_enabled
                                   ? "zero_normal_unbound"
                                   : "disabled_unbound"),
                        expected_bounce_name == target_value.bounce);
                    emit_field(
                        "bounce_plane",
                        float_values(source_value.bounce_plane),
                        has_bounce_transform
                            ? "synthesized_trans"
                            : "<no_trans>",
                        target_value.bounce,
                        "plane_field_removed",
                        expected_bounce_name == target_value.bounce);
                    if (has_bounce_transform) {
                        const auto bounce_found = std::find_if(
                            verify_directory.entries.begin(),
                            verify_directory.entries.end(),
                            [&](const gh::milo::Entry& target) {
                                return target.type == "Trans" &&
                                       target.name ==
                                           expected_bounce_name;
                            });
                        if (bounce_found ==
                            verify_directory.entries.end())
                            throw std::runtime_error(
                                "ParticleSys bounce Trans is missing: " +
                                expected_bounce_name);
                        const auto target_bounce =
                            gh::milo_object::parse_trans9(
                                bounce_found->body_bytes);
                        const auto expected_bounce =
                            expected_bounce_trans(
                                source_value.bounce_plane);
                        const bool bounce_exact =
                            target_bounce.revision == 9 &&
                            target_bounce.object_fields.revision == 0 &&
                            target_bounce.object_fields.type.empty() &&
                            !target_bounce.object_fields
                                 .has_type_properties &&
                            target_bounce.object_fields.type_property_id ==
                                0 &&
                            target_bounce.object_fields.type_properties
                                .empty() &&
                            same_float_bits(
                                expected_bounce.local,
                                target_bounce.local) &&
                            same_float_bits(
                                expected_bounce.world,
                                target_bounce.world) &&
                            target_bounce.constraint == 0 &&
                            target_bounce.target.empty() &&
                            !target_bounce.preserve_scale &&
                            target_bounce.parent.empty();
                        emit_field(
                            "bounce_trans",
                            float_values(source_value.bounce_plane),
                            float_array_pair_digest(
                                expected_bounce.local,
                                expected_bounce.world),
                            float_array_pair_digest(
                                target_bounce.local,
                                target_bounce.world),
                            "independent_plane_to_transform_formula",
                            bounce_exact);
                        ++particle_value_bounce_transforms;
                    }
                    ++particle_value_objects;
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "Font") continue;
                    const auto source_value =
                        gh::milo_object::parse_font(
                            source_object.body_bytes);
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "Font" &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted Font is missing: " +
                            source_object.name);
                    const auto target_value =
                        gh::milo_object::parse_font15(
                            found->body_bytes);
                    const auto target_material_found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "Mat" &&
                                   target.name == source_value.material;
                        });
                    if (target_material_found ==
                        verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted Font material is missing: " +
                            source_value.material);
                    const auto target_material =
                        gh::milo_object::parse_mat27(
                            target_material_found->body_bytes);
                    if (target_material.diffuse_texture.empty())
                        throw std::runtime_error(
                            "converted Font material has no diffuse "
                            "texture: " + source_value.material);
                    const auto source_texture_found = std::find_if(
                        source.entries.begin(), source.entries.end(),
                        [&](const gh::milo::Entry& source_entry) {
                            return source_entry.type == "Tex" &&
                                   source_entry.name ==
                                       target_material.diffuse_texture;
                        });
                    if (source_texture_found == source.entries.end())
                        throw std::runtime_error(
                            "Font source texture is missing: " +
                            target_material.diffuse_texture);
                    const auto source_texture =
                        gh::milo_object::parse_tex(
                            source_texture_found->body_bytes);
                    if (!source_texture.has_bitmap)
                        throw std::runtime_error(
                            "Font source texture has no bitmap: " +
                            target_material.diffuse_texture);
                    gh::tex::HmxBitmap bitmap;
                    bitmap.magic =
                        source_texture.bitmap.header_kind;
                    bitmap.bpp =
                        source_texture.bitmap.bits_per_pixel;
                    bitmap.encoding =
                        source_texture.bitmap.encoding;
                    bitmap.mipmaps =
                        source_texture.bitmap.mipmap_count;
                    bitmap.width = source_texture.bitmap.width;
                    bitmap.height = source_texture.bitmap.height;
                    bitmap.bpl =
                        source_texture.bitmap.bytes_per_line;
                    bitmap.wii_alpha =
                        source_texture.bitmap.wii_alpha;
                    bitmap.raw = source_texture.bitmap.data;
                    const auto rgba = gh::tex::decode_to_rgba(bitmap);
                    if (bitmap.width == 0 || bitmap.height == 0)
                        throw std::runtime_error(
                            "Font source texture has zero dimensions: " +
                            target_material.diffuse_texture);
                    const float cell_width =
                        source_value.cell_size[0];
                    const float cell_height =
                        source_value.cell_size[1];
                    if (!std::isfinite(cell_width) ||
                        !std::isfinite(cell_height) ||
                        cell_width <= 0.0f ||
                        cell_height <= 0.0f)
                        throw std::runtime_error(
                            "Font source has invalid cell dimensions: " +
                            source_object.name);

                    std::string expected_characters =
                        source_value.characters;
                    const bool normalized_nbsp =
                        !expected_characters.empty() &&
                        static_cast<uint8_t>(
                            expected_characters.front()) == 0xA0;
                    if (normalized_nbsp)
                        expected_characters.front() = ' ';
                    std::array<
                        gh::milo_object::FontCharInfo15, 256>
                        expected_character_info{};
                    const auto column_non_transparent =
                        [&](int x, int top, int bottom) {
                            if (x < 0 ||
                                x >= static_cast<int>(bitmap.width))
                                return false;
                            top = std::max(top, 0);
                            bottom = std::min(
                                bottom,
                                static_cast<int>(bitmap.height));
                            for (int y = top; y < bottom; ++y) {
                                const size_t alpha =
                                    (static_cast<size_t>(y) *
                                         bitmap.width +
                                     static_cast<uint32_t>(x)) *
                                        4 +
                                    3;
                                if (rgba[alpha] != 0) return true;
                            }
                            return false;
                        };
                    float x = 0.0f;
                    float y = 0.0f;
                    size_t rasterized_characters = 0;
                    for (unsigned char character :
                         expected_characters) {
                        if (x + cell_width >
                            static_cast<float>(bitmap.width)) {
                            x = 0.0f;
                            y += cell_height;
                        }
                        if (y + cell_height >
                            static_cast<float>(bitmap.height))
                            break;
                        const int left_edge =
                            static_cast<int>(std::lrint(x));
                        const int right_edge =
                            static_cast<int>(
                                std::lrint(x + cell_width));
                        const int top_edge =
                            static_cast<int>(std::lrint(y));
                        const int bottom_edge =
                            static_cast<int>(
                                std::lrint(y + cell_height));
                        int left = left_edge;
                        while (
                            left < right_edge &&
                            !column_non_transparent(
                                left, top_edge, bottom_edge))
                            ++left;
                        int right = right_edge - 1;
                        while (
                            right >= left_edge &&
                            !column_non_transparent(
                                right, top_edge, bottom_edge))
                            --right;
                        auto& info =
                            expected_character_info[character];
                        const int glyph_width = right + 1 - left;
                        info.texture_v =
                            y / static_cast<float>(bitmap.height);
                        if (glyph_width <= 0) {
                            info.texture_u =
                                x / static_cast<float>(bitmap.width);
                            info.character_width = 0.25f;
                            info.character_advance = 0.25f;
                        } else {
                            info.texture_u =
                                static_cast<float>(left) /
                                static_cast<float>(bitmap.width);
                            info.character_width =
                                static_cast<float>(glyph_width) /
                                cell_width;
                            info.character_advance =
                                info.character_width;
                        }
                        x += cell_width;
                        ++rasterized_characters;
                    }
                    expected_character_info[9] =
                        expected_character_info[32];
                    expected_character_info[9].character_advance *=
                        3.0f;

                    const auto emit_field =
                        [&](const std::string& field,
                            const std::string& source_field,
                            const std::string& expected_field,
                            const std::string& target_field,
                            const std::string& rule,
                            bool matches) {
                            font_value_report
                                << cell(entry.full_path) << '\t'
                                << cell(source_object.name) << '\t'
                                << source_value.revision << '\t'
                                << target_value.revision << '\t'
                                << cell(field) << '\t'
                                << cell(source_field) << '\t'
                                << cell(expected_field) << '\t'
                                << cell(target_field) << '\t'
                                << cell(rule) << '\t'
                                << (matches ? "exact" : "mismatch")
                                << '\n';
                            ++font_value_rows;
                            if (!matches)
                                throw std::runtime_error(
                                    "converted Font field differs: " +
                                    source_object.name + "::" + field);
                        };
                    emit_field(
                        "object_fields.revision", "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.revision),
                        "target_native_default",
                        target_value.object_fields.revision == 0);
                    emit_field(
                        "object_fields.type", "<absent>", "",
                        target_value.object_fields.type,
                        "target_native_default",
                        target_value.object_fields.type.empty());
                    emit_field(
                        "object_fields.has_type_properties",
                        "<absent>", "0",
                        target_value.object_fields.has_type_properties
                            ? "1"
                            : "0",
                        "target_native_default",
                        !target_value.object_fields.has_type_properties);
                    emit_field(
                        "object_fields.type_property_id", "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.type_property_id),
                        "target_native_default",
                        target_value.object_fields.type_property_id == 0);
                    emit_field(
                        "object_fields.type_properties.count",
                        "<absent>", "0",
                        std::to_string(
                            target_value.object_fields.type_properties
                                .size()),
                        "target_native_default",
                        target_value.object_fields.type_properties.empty());
                    emit_field(
                        "material", source_value.material,
                        source_value.material, target_value.material,
                        "retained",
                        source_value.material == target_value.material);
                    emit_field(
                        "resolved_diffuse_texture",
                        source_value.material,
                        target_material.diffuse_texture,
                        target_material.diffuse_texture,
                        "material_contract_resolution", true);
                    emit_field(
                        "cell_size",
                        float_values(source_value.cell_size),
                        float_values(source_value.cell_size),
                        float_values(target_value.cell_size),
                        "retained",
                        same_float_bits(
                            source_value.cell_size,
                            target_value.cell_size));
                    emit_field(
                        "deprecated_size",
                        float_value(source_value.deprecated_size),
                        float_value(source_value.deprecated_size),
                        float_value(target_value.deprecated_size),
                        "retained",
                        same_float_bits(
                            source_value.deprecated_size,
                            target_value.deprecated_size));
                    emit_field(
                        "base_kerning",
                        float_value(source_value.base_kerning),
                        float_value(source_value.base_kerning),
                        float_value(target_value.base_kerning),
                        "retained",
                        same_float_bits(
                            source_value.base_kerning,
                            target_value.base_kerning));
                    emit_field(
                        "characters",
                        string_value_digest(source_value.characters),
                        string_value_digest(expected_characters),
                        string_value_digest(target_value.characters),
                        normalized_nbsp
                            ? "leading_nbsp_to_space"
                            : "retained",
                        expected_characters ==
                            target_value.characters);
                    emit_field(
                        "has_kerning_table",
                        source_value.has_kerning_table ? "1" : "0",
                        source_value.has_kerning_table ? "1" : "0",
                        target_value.has_kerning_table ? "1" : "0",
                        "retained",
                        source_value.has_kerning_table ==
                            target_value.has_kerning_table);
                    emit_field(
                        "kerning.count",
                        std::to_string(source_value.kerning.size()),
                        std::to_string(source_value.kerning.size()),
                        std::to_string(target_value.kerning.size()),
                        "retained",
                        source_value.kerning.size() ==
                            target_value.kerning.size());
                    emit_field(
                        "kerning.values",
                        font_kerning_digest(source_value.kerning),
                        font_kerning_digest(source_value.kerning),
                        font_kerning_digest(target_value.kerning),
                        "retained_digest",
                        same_font_kerning(
                            source_value.kerning,
                            target_value.kerning));
                    emit_field(
                        "texture_owner", "<absent>",
                        source_object.name,
                        target_value.texture_owner,
                        "source_object_name",
                        target_value.texture_owner ==
                            source_object.name);
                    emit_field(
                        "monospace", "<absent>", "0",
                        target_value.monospace ? "1" : "0",
                        "target_native_default",
                        !target_value.monospace);
                    emit_field(
                        "packed", "<absent>", "0",
                        target_value.packed ? "1" : "0",
                        "target_native_default",
                        !target_value.packed);
                    emit_field(
                        "bitmap_width",
                        std::to_string(bitmap.width),
                        std::to_string(bitmap.width),
                        std::to_string(target_value.bitmap_width),
                        "resolved_texture_bitmap",
                        target_value.bitmap_width ==
                            static_cast<int32_t>(bitmap.width));
                    emit_field(
                        "bitmap_height",
                        std::to_string(bitmap.height),
                        std::to_string(bitmap.height),
                        std::to_string(target_value.bitmap_height),
                        "resolved_texture_bitmap",
                        target_value.bitmap_height ==
                            static_cast<int32_t>(bitmap.height));
                    const std::array<float, 2>
                        expected_texture_cell_size = {
                            cell_width /
                                static_cast<float>(bitmap.width),
                            cell_height /
                                static_cast<float>(bitmap.height)};
                    emit_field(
                        "texture_cell_size", "<absent>",
                        float_values(expected_texture_cell_size),
                        float_values(target_value.texture_cell_size),
                        "cell_over_bitmap_dimensions",
                        same_float_bits(
                            expected_texture_cell_size,
                            target_value.texture_cell_size));
                    emit_field(
                        "character_info.count", "<absent>", "256", "256",
                        "target_fixed_table", true);
                    for (size_t character = 0;
                         character < expected_character_info.size();
                         ++character) {
                        emit_field(
                            "character_info[" +
                                std::to_string(character) + "]",
                            "<absent>",
                            font_char_info_value(
                                expected_character_info[character]),
                            font_char_info_value(
                                target_value
                                    .character_info[character]),
                            character == 9
                                ? "space_info_with_triple_advance"
                                : "bitmap_alpha_column_scan",
                            same_font_char_info(
                                expected_character_info[character],
                                target_value
                                    .character_info[character]));
                    }
                    font_value_glyphs += rasterized_characters;
                    font_value_kerning_rows +=
                        source_value.kerning.size();
                    if (normalized_nbsp)
                        ++font_value_nbsp_normalizations;
                    ++font_value_objects;
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "TransAnim") continue;
                    const auto source_anim =
                        gh::milo_object::parse_trans_anim(
                            source_object.body_bytes);
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "TransAnim" &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted TransAnim is missing: " +
                            source_object.name);
                    const auto target_anim =
                        gh::milo_object::parse_trans_anim6(
                            found->body_bytes);
                    const auto settings =
                        expected_legacy_anim_settings(
                            source_anim.animatable);
                    const bool filter_required =
                        settings.scale != 1.0f ||
                        settings.offset != 0.0f ||
                        settings.minimum != settings.maximum;
                    const std::string filter_name =
                        filter_required
                            ? stem(source_object.name) + ".filt"
                            : std::string();
                    if (filter_required) {
                        const auto filter_entry = std::find_if(
                            verify_directory.entries.begin(),
                            verify_directory.entries.end(),
                            [&](const gh::milo::Entry& target) {
                                return target.type == "AnimFilter" &&
                                       target.name == filter_name;
                            });
                        if (filter_entry ==
                            verify_directory.entries.end())
                            throw std::runtime_error(
                                "converted TransAnim filter is missing: " +
                                source_object.name);
                        const auto filter =
                            gh::milo_object::parse_anim_filter1(
                                filter_entry->body_bytes);
                        if (filter.revision != 1 ||
                            filter.object_fields.revision != 0 ||
                            !filter.object_fields.type.empty() ||
                            filter.object_fields.has_type_properties ||
                            filter.object_fields.type_property_id != 0 ||
                            !filter.object_fields.type_properties.empty() ||
                            filter.animatable.revision != 4 ||
                            filter.animatable.frame != 0.0f ||
                            filter.animatable.rate != 0 ||
                            filter.anim != source_object.name ||
                            !same_float_bits(
                                filter.scale,
                                std::fabs(settings.scale)) ||
                            !same_float_bits(
                                filter.offset, settings.offset) ||
                            !same_float_bits(
                                filter.start, settings.minimum) ||
                            !same_float_bits(
                                filter.end, settings.maximum) ||
                            filter.type != (settings.loop ? 1 : 0) ||
                            filter.period != 0.0f)
                            throw std::runtime_error(
                                "converted TransAnim filter values differ: " +
                                source_object.name);
                        ++trans_anim_filter_rows;
                    }
                    if (target_anim.revision != 6 ||
                        target_anim.object_fields.revision != 0 ||
                        !target_anim.object_fields.type.empty() ||
                        target_anim.object_fields.has_type_properties ||
                        target_anim.object_fields.type_property_id != 0 ||
                        !target_anim.object_fields.type_properties.empty() ||
                        target_anim.animatable.revision != 4 ||
                        target_anim.animatable.frame != 0.0f ||
                        target_anim.animatable.rate != 0 ||
                        source_anim.target != target_anim.target ||
                        !same_array_keys(
                            source_anim.rotation_keys,
                            target_anim.rotation_keys) ||
                        !same_array_keys(
                            source_anim.translation_keys,
                            target_anim.translation_keys) ||
                        source_anim.keys_owner !=
                            target_anim.keys_owner ||
                        source_anim.translation_spline !=
                            target_anim.translation_spline ||
                        source_anim.repeat_translation !=
                            target_anim.repeat_translation ||
                        !same_array_keys(
                            source_anim.scale_keys,
                            target_anim.scale_keys) ||
                        source_anim.scale_spline !=
                            target_anim.scale_spline ||
                        source_anim.follow_path !=
                            target_anim.follow_path ||
                        source_anim.rotation_slerp !=
                            target_anim.rotation_slerp)
                        throw std::runtime_error(
                            "converted TransAnim values differ: " +
                            source_object.name);
                    trans_anim_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.name) << '\t'
                        << cell(found->name) << '\t'
                        << source_anim.revision << '\t'
                        << target_anim.revision << '\t'
                        << target_anim.object_fields.revision << '\t'
                        << cell(target_anim.object_fields.type) << '\t'
                        << (target_anim.object_fields
                                    .has_type_properties
                                ? 1
                                : 0)
                        << '\t' << source_anim.animatable.revision
                        << '\t'
                        << source_anim.animatable.operations.size()
                        << '\t'
                        << legacy_anim_operation_digest(
                               source_anim.animatable.operations)
                        << '\t'
                        << source_anim.animatable.objects.size()
                        << '\t'
                        << string_vector_digest(
                               source_anim.animatable.objects)
                        << '\t'
                        << cell(joined_strings(
                               source_anim.animatable.objects))
                        << '\t'
                        << source_anim.drawable.objects.size()
                        << '\t'
                        << string_vector_digest(
                               source_anim.drawable.objects)
                        << '\t' << (filter_required ? 1 : 0)
                        << '\t' << cell(filter_name)
                        << '\t' << std::setprecision(9)
                        << std::fabs(settings.scale)
                        << '\t' << settings.offset
                        << '\t' << settings.minimum
                        << '\t' << settings.maximum
                        << '\t' << (settings.loop ? 1 : 0)
                        << "\t0"
                        << '\t' << target_anim.animatable.revision
                        << '\t' << target_anim.animatable.frame
                        << '\t' << target_anim.animatable.rate
                        << '\t' << cell(source_anim.target)
                        << '\t' << cell(target_anim.target)
                        << '\t' << source_anim.rotation_keys.size()
                        << '\t' << target_anim.rotation_keys.size()
                        << '\t'
                        << array_key_digest(
                               source_anim.rotation_keys)
                        << '\t'
                        << array_key_digest(
                               target_anim.rotation_keys)
                        << '\t'
                        << source_anim.translation_keys.size()
                        << '\t'
                        << target_anim.translation_keys.size()
                        << '\t'
                        << array_key_digest(
                               source_anim.translation_keys)
                        << '\t'
                        << array_key_digest(
                               target_anim.translation_keys)
                        << '\t' << cell(source_anim.keys_owner)
                        << '\t' << cell(target_anim.keys_owner)
                        << '\t'
                        << (source_anim.translation_spline ? 1 : 0)
                        << '\t'
                        << (target_anim.translation_spline ? 1 : 0)
                        << '\t'
                        << (source_anim.repeat_translation ? 1 : 0)
                        << '\t'
                        << (target_anim.repeat_translation ? 1 : 0)
                        << '\t' << source_anim.scale_keys.size()
                        << '\t' << target_anim.scale_keys.size()
                        << '\t'
                        << array_key_digest(source_anim.scale_keys)
                        << '\t'
                        << array_key_digest(target_anim.scale_keys)
                        << '\t'
                        << (source_anim.scale_spline ? 1 : 0)
                        << '\t'
                        << (target_anim.scale_spline ? 1 : 0)
                        << '\t'
                        << (source_anim.follow_path ? 1 : 0)
                        << '\t'
                        << (target_anim.follow_path ? 1 : 0)
                        << '\t'
                        << (source_anim.rotation_slerp ? 1 : 0)
                        << '\t'
                        << (target_anim.rotation_slerp ? 1 : 0)
                        << "\texact\n";
                    ++trans_anim_value_rows;
                    trans_anim_key_rows +=
                        source_anim.rotation_keys.size() +
                        source_anim.translation_keys.size() +
                        source_anim.scale_keys.size();
                }
                std::map<std::string, const gh::milo::Entry*>
                    source_entries_by_name;
                std::map<std::string, std::string>
                    source_types_by_name;
                std::map<std::string, gh::milo_object::View>
                    source_views;
                for (const auto& source_object : source.entries) {
                    source_entries_by_name.emplace(
                        source_object.name, &source_object);
                    source_types_by_name.emplace(
                        source_object.name, source_object.type);
                    if (source_object.type == "View")
                        source_views.emplace(
                            source_object.name,
                            gh::milo_object::parse_view(
                                source_object.body_bytes));
                }
                const auto reference_type_for_audit =
                    [&](const std::string& name) {
                        const auto found =
                            source_types_by_name.find(name);
                        return found != source_types_by_name.end()
                                   ? found->second
                                   : inferred_reference_type_for_audit(
                                         name);
                    };
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "View") continue;
                    const auto& source_view =
                        source_views.at(source_object.name);
                    const auto owner =
                        source_views.find(
                            source_view.children_owner);
                    if (owner == source_views.end())
                        throw std::runtime_error(
                            "View value audit: children owner missing: " +
                            source_object.name);
                    std::vector<
                        gh::milo_object::ResolvedObjectReference>
                        animation_references;
                    size_t mat_anim_expansions = 0;
                    size_t nested_animation_references = 0;
                    std::set<std::string> visiting_animations;
                    const auto append_animation =
                        [&](auto&& self,
                            const std::string& name) -> void {
                            const std::string type =
                                reference_type_for_audit(name);
                            if (type.empty())
                                throw std::runtime_error(
                                    "View value audit: animation type "
                                    "unresolved: " + name);
                            animation_references.push_back(
                                {name, type});
                            const auto source_entry =
                                source_entries_by_name.find(name);
                            if (type == "MatAnim" &&
                                source_entry !=
                                    source_entries_by_name.end()) {
                                const auto mat_anim =
                                    gh::milo_object::parse_mat_anim(
                                        source_entry->second
                                            ->body_bytes);
                                const std::string anim_base =
                                    stem(name);
                                for (size_t stage_index = 0;
                                     stage_index + 1 <
                                         mat_anim.stages.size();
                                     ++stage_index) {
                                    float end_frame = 0.0f;
                                    const auto include_keys =
                                        [&end_frame](
                                            const auto& keys) {
                                            for (const auto& key :
                                                 keys)
                                                end_frame =
                                                    std::max(
                                                        end_frame,
                                                        key.frame);
                                        };
                                    include_keys(
                                        mat_anim
                                            .stages[stage_index]
                                            .translation_keys);
                                    include_keys(
                                        mat_anim
                                            .stages[stage_index]
                                            .scale_keys);
                                    include_keys(
                                        mat_anim
                                            .stages[stage_index]
                                            .rotation_keys);
                                    include_keys(
                                        mat_anim
                                            .stages[stage_index]
                                            .texture_keys);
                                    if (end_frame == 0.0f)
                                        continue;
                                    animation_references.push_back({
                                        anim_base + "_" +
                                            std::to_string(
                                                stage_index + 1) +
                                            ".mnm",
                                        "MatAnim"});
                                    ++mat_anim_expansions;
                                }
                            }
                            if (type == "View" ||
                                source_entry ==
                                    source_entries_by_name.end())
                                return;
                            const auto animatable =
                                source_animatable(
                                    *source_entry->second);
                            if (!animatable ||
                                animatable->objects.empty())
                                return;
                            nested_animation_references +=
                                animatable->objects.size();
                            if (!visiting_animations
                                     .insert(name).second)
                                throw std::runtime_error(
                                    "View value audit: animation cycle: " +
                                    name);
                            for (const auto& child :
                                 animatable->objects)
                                self(self, child);
                            visiting_animations.erase(name);
                        };
                    // children_owner is an inheritance/ownership link; the
                    // serialized member lists on this View are the members
                    // it actually dispatches.  Using the owner's lists here
                    // falsely aliases distinct lighting views (notably the
                    // GH1 Theatre verse/chorus/solo groups).
                    for (const auto& name :
                         source_view.animatable.objects)
                        append_animation(
                            append_animation, name);
                    std::vector<
                        gh::milo_object::ResolvedObjectReference>
                        drawable_references;
                    const size_t trans_anim_references =
                        static_cast<size_t>(std::count_if(
                            animation_references.begin(),
                            animation_references.end(),
                            [](const auto& reference) {
                                return reference.type ==
                                    "TransAnim";
                            }));
                    std::set<std::string> visiting_drawables;
                    const auto append_drawable =
                        [&](auto&& self,
                            const std::string& name) -> void {
                            const std::string type =
                                reference_type_for_audit(name);
                            if (type.empty())
                                throw std::runtime_error(
                                    "View value audit: drawable type "
                                    "unresolved: " + name);
                            drawable_references.push_back(
                                {name, type});
                            if (type == "View") return;
                            const auto source_entry =
                                source_entries_by_name.find(name);
                            if (source_entry ==
                                source_entries_by_name.end())
                                return;
                            const auto drawable =
                                source_drawable(
                                    *source_entry->second);
                            if (!drawable ||
                                drawable->objects.empty())
                                return;
                            if (!visiting_drawables.insert(name).second)
                                throw std::runtime_error(
                                    "View value audit: drawable cycle: " +
                                    name);
                            for (const auto& child :
                                 drawable->objects)
                                self(self, child);
                            visiting_drawables.erase(name);
                        };
                    for (const auto& name :
                         source_view.drawable.objects)
                        append_drawable(
                            append_drawable, name);
                    std::vector<
                        gh::milo_object::
                            ResolvedViewEnvironmentSegment>
                        environment_segments;
                    std::string current_environment;
                    for (const auto& object :
                         drawable_references) {
                        if (object.type == "Environ") {
                            current_environment = object.name;
                            continue;
                        }
                        if (object.type == "Cam") continue;
                        if (environment_segments.empty() ||
                            environment_segments.back().environment !=
                                current_environment) {
                            gh::milo_object::
                                ResolvedViewEnvironmentSegment segment;
                            segment.environment =
                                current_environment;
                            environment_segments.push_back(
                                std::move(segment));
                        }
                        auto& objects =
                            environment_segments.back()
                                .drawable_objects;
                        const auto existing = std::find_if(
                            objects.begin(), objects.end(),
                            [&](const auto& candidate) {
                                return candidate.name ==
                                    object.name;
                            });
                        if (existing != objects.end())
                            objects.erase(existing);
                        objects.push_back(object);
                    }
                    const bool multiple_environment_segments =
                        environment_segments.size() > 1;
                    std::vector<std::string> expected_objects;
                    const auto append_unique =
                        [&](const std::string& name) {
                            if (std::find(
                                    expected_objects.begin(),
                                    expected_objects.end(),
                                    name) ==
                                expected_objects.end())
                                expected_objects.push_back(name);
                        };
                    for (const auto& object :
                         animation_references)
                        append_unique(object.name);
                    std::string expected_environment;
                    std::vector<std::string>
                        environment_scope_names;
                    if (multiple_environment_segments) {
                        std::set<std::string> scoped_drawables;
                        for (const auto& segment :
                             environment_segments)
                            for (const auto& object :
                                 segment.drawable_objects)
                                scoped_drawables.insert(
                                    object.name);
                        expected_objects.erase(
                            std::remove_if(
                                expected_objects.begin(),
                                expected_objects.end(),
                                [&](const std::string& name) {
                                    return scoped_drawables.find(
                                               name) !=
                                           scoped_drawables.end();
                                }),
                            expected_objects.end());
                        for (size_t segment_index = 0;
                             segment_index <
                                 environment_segments.size();
                             ++segment_index) {
                            const std::string scope_name =
                                source_object.name +
                                ".__environment_" +
                                std::to_string(segment_index) +
                                ".grp";
                            environment_scope_names.push_back(
                                scope_name);
                            expected_objects.push_back(scope_name);
                        }
                    } else if (!environment_segments.empty()) {
                        expected_environment =
                            environment_segments.front()
                                .environment;
                        for (const auto& object :
                             environment_segments.front()
                                 .drawable_objects) {
                            const auto existing = std::find(
                                expected_objects.begin(),
                                expected_objects.end(),
                                object.name);
                            if (existing !=
                                expected_objects.end())
                                expected_objects.erase(existing);
                            expected_objects.push_back(
                                object.name);
                        }
                    }
                    std::set<std::string> drawable_names;
                    for (const auto& object :
                         drawable_references)
                        if (target_drawable_type_for_audit(
                                object.type))
                            drawable_names.insert(object.name);
                    const bool needs_draw_only =
                        std::any_of(
                            animation_references.begin(),
                            animation_references.end(),
                            [&](const auto& object) {
                                return
                                    target_drawable_type_for_audit(
                                        object.type) &&
                                    drawable_names.find(
                                        object.name) ==
                                        drawable_names.end();
                            });
                    const bool has_draw_only =
                        needs_draw_only ||
                        multiple_environment_segments;
                    const std::string draw_only_name =
                        has_draw_only
                            ? source_object.name +
                                  ".__draw_only.grp"
                            : std::string();
                    const auto validate_synthesized_group =
                        [&](const std::string& group_name,
                            const std::vector<std::string>& objects,
                            const std::string& environment) {
                            const auto found = std::find_if(
                                verify_directory.entries.begin(),
                                verify_directory.entries.end(),
                                [&](const gh::milo::Entry& target) {
                                    return target.type == "Group" &&
                                           target.name ==
                                               group_name;
                                });
                            if (found ==
                                verify_directory.entries.end())
                                throw std::runtime_error(
                                    "View value audit: synthesized Group "
                                    "missing: " + group_name);
                            const auto group =
                                gh::milo_object::parse_group12(
                                    found->body_bytes);
                            const std::array<float, 12> identity = {
                                1, 0, 0, 0, 1, 0,
                                0, 0, 1, 0, 0, 0};
                            const std::array<float, 4> zero_sphere{};
                            if (group.revision != 12 ||
                                group.object_fields.revision != 0 ||
                                !group.object_fields.type.empty() ||
                                group.object_fields
                                    .has_type_properties ||
                                group.object_fields.type_property_id !=
                                    0 ||
                                !group.object_fields.type_properties
                                     .empty() ||
                                group.animatable.revision != 4 ||
                                group.animatable.frame != 0.0f ||
                                group.animatable.rate != 0 ||
                                !same_float_bits(
                                    group.transformable.local,
                                    identity) ||
                                !same_float_bits(
                                    group.transformable.world,
                                    identity) ||
                                group.transformable.constraint != 2 ||
                                !group.transformable.target.empty() ||
                                group.transformable.preserve_scale ||
                                group.transformable.parent !=
                                    source_object.name ||
                                group.drawable.revision != 3 ||
                                !group.drawable.showing ||
                                !same_float_bits(
                                    group.drawable.sphere,
                                    zero_sphere) ||
                                group.drawable.draw_order != 0.0f ||
                                group.objects != objects ||
                                group.environment != environment ||
                                !group.draw_only.empty() ||
                                !group.lod.empty() ||
                                group.lod_screen_size != 0.0f)
                                throw std::runtime_error(
                                    "View value audit: synthesized Group "
                                    "differs: " + group_name);
                        };
                    for (size_t segment_index = 0;
                         segment_index <
                             environment_scope_names.size();
                         ++segment_index) {
                        std::vector<std::string> scope_objects;
                        for (const auto& object :
                             environment_segments[segment_index]
                                 .drawable_objects)
                            scope_objects.push_back(object.name);
                        validate_synthesized_group(
                            environment_scope_names[segment_index],
                            scope_objects,
                            environment_segments[segment_index]
                                .environment);
                    }
                    if (has_draw_only) {
                        std::vector<std::string>
                            draw_only_objects;
                        if (multiple_environment_segments) {
                            draw_only_objects =
                                environment_scope_names;
                        } else {
                            for (const auto& object :
                                 drawable_references) {
                                if (!target_drawable_type_for_audit(
                                        object.type))
                                    continue;
                                if (std::find(
                                        draw_only_objects.begin(),
                                        draw_only_objects.end(),
                                        object.name) ==
                                    draw_only_objects.end())
                                    draw_only_objects.push_back(
                                        object.name);
                            }
                        }
                        validate_synthesized_group(
                            draw_only_name, draw_only_objects, "");
                    }
                    const auto target_entry = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "Group" &&
                                   target.name ==
                                       source_object.name;
                        });
                    if (target_entry ==
                        verify_directory.entries.end())
                        throw std::runtime_error(
                            "View value audit: target missing: " +
                            source_object.name);
                    const auto target_group =
                        gh::milo_object::parse_group12(
                            target_entry->body_bytes);
                    if (source_view.showing_range[0] != 0.0f ||
                        source_view.showing_range[1] != 0.0f ||
                        target_group.revision !=
                            (has_draw_only ? 13u : 12u) ||
                        target_group.object_fields.revision != 0 ||
                        !target_group.object_fields.type.empty() ||
                        target_group.object_fields
                            .has_type_properties ||
                        target_group.object_fields.type_property_id !=
                            0 ||
                        !target_group.object_fields.type_properties
                             .empty() ||
                        target_group.animatable.revision != 4 ||
                        target_group.animatable.frame != 0.0f ||
                        target_group.animatable.rate != 0 ||
                        target_group.drawable.revision != 3 ||
                        source_view.drawable.showing !=
                            target_group.drawable.showing ||
                        !same_float_bits(
                            source_view.drawable.sphere,
                            target_group.drawable.sphere) ||
                        target_group.drawable.draw_order != 0.0f ||
                        target_group.objects != expected_objects ||
                        target_group.environment !=
                            expected_environment ||
                        target_group.draw_only != draw_only_name ||
                        !target_group.lod.empty() ||
                        target_group.lod_screen_size != 0.0f)
                        throw std::runtime_error(
                            "View value audit: target values differ: " +
                            source_object.name);
                    const auto settings =
                        expected_legacy_anim_settings(
                            source_view.animatable);
                    const bool filter_required =
                        settings.scale != 1.0f ||
                        settings.offset != 0.0f ||
                        settings.minimum != settings.maximum;
                    const std::string filter_name =
                        filter_required
                            ? stem(source_object.name) + ".filt"
                            : std::string();
                    if (filter_required) {
                        const auto filter_entry = std::find_if(
                            verify_directory.entries.begin(),
                            verify_directory.entries.end(),
                            [&](const gh::milo::Entry& target) {
                                return target.type == "AnimFilter" &&
                                       target.name == filter_name;
                            });
                        if (filter_entry ==
                            verify_directory.entries.end())
                            throw std::runtime_error(
                                "View value audit: filter missing: " +
                                source_object.name);
                        const auto filter =
                            gh::milo_object::parse_anim_filter1(
                                filter_entry->body_bytes);
                        if (filter.revision != 1 ||
                            filter.object_fields.revision != 0 ||
                            !filter.object_fields.type.empty() ||
                            filter.object_fields
                                .has_type_properties ||
                            filter.object_fields.type_property_id != 0 ||
                            !filter.object_fields.type_properties.empty() ||
                            filter.animatable.revision != 4 ||
                            filter.animatable.frame != 0.0f ||
                            filter.animatable.rate != 0 ||
                            filter.anim != source_object.name ||
                            !same_float_bits(
                                filter.scale,
                                std::fabs(settings.scale)) ||
                            !same_float_bits(
                                filter.offset, settings.offset) ||
                            !same_float_bits(
                                filter.start, settings.minimum) ||
                            !same_float_bits(
                                filter.end, settings.maximum) ||
                            filter.type != (settings.loop ? 1 : 0) ||
                            filter.period != 0.0f)
                            throw std::runtime_error(
                                "View value audit: filter differs: " +
                                source_object.name);
                        ++view_filter_rows;
                    }
                    view_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.name) << '\t'
                        << cell(source_view.children_owner)
                        << '\t' << source_view.revision
                        << '\t' << target_group.revision
                        << '\t'
                        << (source_view.drawable.showing ? 1 : 0)
                        << '\t'
                        << (target_group.drawable.showing ? 1 : 0)
                        << '\t'
                        << float_array_digest(
                               source_view.drawable.sphere)
                        << '\t'
                        << float_array_digest(
                               target_group.drawable.sphere)
                        << '\t'
                        << float_values(
                               source_view.showing_range)
                        << '\t'
                        << source_view.animatable.operations.size()
                        << '\t'
                        << legacy_anim_operation_digest(
                               source_view.animatable.operations)
                        << '\t' << (filter_required ? 1 : 0)
                        << '\t' << cell(filter_name)
                        << '\t' << animation_references.size()
                        << '\t'
                        << resolved_reference_digest(
                               animation_references)
                        << '\t' << trans_anim_references
                        << '\t' << nested_animation_references
                        << '\t' << mat_anim_expansions
                        << '\t' << drawable_references.size()
                        << '\t'
                        << resolved_reference_digest(
                               drawable_references)
                        << '\t' << environment_segments.size()
                        << '\t' << environment_scope_names.size()
                        << '\t' << (needs_draw_only ? 1 : 0)
                        << '\t' << cell(draw_only_name)
                        << '\t' << expected_objects.size()
                        << '\t' << target_group.objects.size()
                        << '\t'
                        << string_vector_digest(expected_objects)
                        << '\t'
                        << string_vector_digest(
                               target_group.objects)
                        << '\t' << cell(expected_environment)
                        << '\t' << cell(target_group.environment)
                        << '\t' << cell(draw_only_name)
                        << '\t' << cell(target_group.draw_only)
                        << '\t' << cell(target_group.lod)
                        << '\t' << target_group.lod_screen_size
                        << "\texact\n";
                    ++view_value_rows;
                    view_animation_references +=
                        animation_references.size();
                    view_trans_anim_references +=
                        trans_anim_references;
                    view_nested_animation_references +=
                        nested_animation_references;
                    view_drawable_references +=
                        drawable_references.size();
                    view_mat_anim_expansions +=
                        mat_anim_expansions;
                    view_environment_scope_groups +=
                        environment_scope_names.size();
                    if (has_draw_only)
                        ++view_draw_only_groups;
                }
                for (const auto& source_object : source.entries) {
                    const bool supported =
                        source_object.type == "CamAnim" ||
                        source_object.type == "EnvAnim" ||
                        source_object.type == "LightAnim" ||
                        source_object.type == "MeshAnim" ||
                        source_object.type == "Morph" ||
                        source_object.type == "Movie" ||
                        source_object.type == "ParticleSysAnim";
                    if (!supported) continue;
                    const auto legacy =
                        source_animatable(source_object);
                    if (!legacy)
                        throw std::runtime_error(
                            "animation payload audit: source has no "
                            "Animatable: " + source_object.name);
                    const auto target_entry = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type ==
                                       source_object.type &&
                                   target.name ==
                                       source_object.name;
                        });
                    if (target_entry ==
                        verify_directory.entries.end())
                        throw std::runtime_error(
                            "animation payload audit: target missing: " +
                            source_object.name);
                    uint64_t source_digest =
                        UINT64_C(14695981039346656037);
                    uint64_t target_digest =
                        UINT64_C(14695981039346656037);
                    uint32_t source_revision = 0;
                    uint32_t target_revision = 0;
                    size_t key_rows = 0;
                    if (source_object.type == "CamAnim") {
                        const auto source_anim =
                            gh::milo_object::parse_cam_anim(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_cam_anim2(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 2 ||
                            source_anim.camera !=
                                target_anim.camera ||
                            source_anim.fov_keys.size() !=
                                target_anim.fov_keys.size() ||
                            source_anim.keys_owner !=
                                target_anim.keys_owner)
                            throw std::runtime_error(
                                "CamAnim payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest, source_anim.camera);
                        fnv1a_string(
                            target_digest, target_anim.camera);
                        for (size_t key_index = 0;
                             key_index <
                                 source_anim.fov_keys.size();
                             ++key_index) {
                            const auto& source_key =
                                source_anim.fov_keys[key_index];
                            const auto& target_key =
                                target_anim.fov_keys[key_index];
                            const float expected_fov =
                                std::atan(
                                    0.75f *
                                    std::tan(
                                        source_key.value *
                                        0.5f)) *
                                2.0f;
                            if (!same_float_bits(
                                    expected_fov,
                                    target_key.value) ||
                                !same_float_bits(
                                    source_key.frame,
                                    target_key.frame))
                                throw std::runtime_error(
                                    "CamAnim FOV mapping differs: " +
                                    source_object.name);
                            fnv1a_value(
                                source_digest, expected_fov);
                            fnv1a_value(
                                source_digest, source_key.frame);
                            fnv1a_value(
                                target_digest, target_key.value);
                            fnv1a_value(
                                target_digest, target_key.frame);
                        }
                        fnv1a_string(
                            source_digest, source_anim.keys_owner);
                        fnv1a_string(
                            target_digest, target_anim.keys_owner);
                        key_rows = source_anim.fov_keys.size();
                    } else if (
                        source_object.type == "EnvAnim") {
                        const auto source_anim =
                            gh::milo_object::parse_env_anim(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_env_anim4(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 4 ||
                            source_anim.environment !=
                                target_anim.environment ||
                            !same_array_keys(
                                source_anim.ambient_color_keys,
                                target_anim.ambient_color_keys) ||
                            source_anim.keys_owner !=
                                target_anim.keys_owner ||
                            !same_array_keys(
                                source_anim.fog_color_keys,
                                target_anim.fog_color_keys) ||
                            !same_array_keys(
                                source_anim.fog_range_keys,
                                target_anim.fog_range_keys))
                            throw std::runtime_error(
                                "EnvAnim payload differs: " +
                                source_object.name);
                        for (uint64_t* digest :
                             {&source_digest, &target_digest})
                            fnv1a_string(
                                *digest, source_anim.environment);
                        fnv1a_string(
                            source_digest,
                            array_key_digest(
                                source_anim.ambient_color_keys));
                        fnv1a_string(
                            target_digest,
                            array_key_digest(
                                target_anim.ambient_color_keys));
                        fnv1a_string(
                            source_digest, source_anim.keys_owner);
                        fnv1a_string(
                            target_digest, target_anim.keys_owner);
                        fnv1a_string(
                            source_digest,
                            array_key_digest(
                                source_anim.fog_color_keys));
                        fnv1a_string(
                            target_digest,
                            array_key_digest(
                                target_anim.fog_color_keys));
                        fnv1a_string(
                            source_digest,
                            array_key_digest(
                                source_anim.fog_range_keys));
                        fnv1a_string(
                            target_digest,
                            array_key_digest(
                                target_anim.fog_range_keys));
                        key_rows =
                            source_anim.ambient_color_keys.size() +
                            source_anim.fog_color_keys.size() +
                            source_anim.fog_range_keys.size();
                    } else if (
                        source_object.type == "LightAnim") {
                        const auto source_anim =
                            gh::milo_object::parse_light_anim(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_light_anim2(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 2 ||
                            source_anim.light != target_anim.light ||
                            !same_array_keys(
                                source_anim.color_keys,
                                target_anim.color_keys) ||
                            source_anim.keys_owner !=
                                target_anim.keys_owner)
                            throw std::runtime_error(
                                "LightAnim payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest, source_anim.light);
                        fnv1a_string(
                            target_digest, target_anim.light);
                        fnv1a_string(
                            source_digest,
                            array_key_digest(
                                source_anim.color_keys));
                        fnv1a_string(
                            target_digest,
                            array_key_digest(
                                target_anim.color_keys));
                        fnv1a_string(
                            source_digest, source_anim.keys_owner);
                        fnv1a_string(
                            target_digest, target_anim.keys_owner);
                        key_rows = source_anim.color_keys.size();
                    } else if (
                        source_object.type == "MeshAnim") {
                        const auto source_anim =
                            gh::milo_object::parse_mesh_anim(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_mesh_anim1(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 1 ||
                            source_anim.mesh != target_anim.mesh ||
                            !same_vector_keys(
                                source_anim.point_keys,
                                target_anim.point_keys) ||
                            !same_vector_keys(
                                source_anim.texcoord_keys,
                                target_anim.texcoord_keys) ||
                            !same_vector_keys(
                                source_anim.color_keys,
                                target_anim.color_keys) ||
                            source_anim.keys_owner !=
                                target_anim.keys_owner)
                            throw std::runtime_error(
                                "MeshAnim payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest, source_anim.mesh);
                        fnv1a_string(
                            target_digest, target_anim.mesh);
                        fnv1a_string(
                            source_digest,
                            vector_key_digest(
                                source_anim.point_keys));
                        fnv1a_string(
                            target_digest,
                            vector_key_digest(
                                target_anim.point_keys));
                        fnv1a_string(
                            source_digest,
                            vector_key_digest(
                                source_anim.texcoord_keys));
                        fnv1a_string(
                            target_digest,
                            vector_key_digest(
                                target_anim.texcoord_keys));
                        fnv1a_string(
                            source_digest,
                            vector_key_digest(
                                source_anim.color_keys));
                        fnv1a_string(
                            target_digest,
                            vector_key_digest(
                                target_anim.color_keys));
                        fnv1a_string(
                            source_digest, source_anim.keys_owner);
                        fnv1a_string(
                            target_digest, target_anim.keys_owner);
                        key_rows =
                            source_anim.point_keys.size() +
                            source_anim.texcoord_keys.size() +
                            source_anim.color_keys.size();
                    } else if (
                        source_object.type == "Morph") {
                        const auto source_anim =
                            gh::milo_object::parse_morph(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_morph4(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 4 ||
                            !same_morph_poses(
                                source_anim.poses,
                                target_anim.poses) ||
                            source_anim.target !=
                                target_anim.target ||
                            source_anim.normals !=
                                target_anim.normals ||
                            source_anim.spline !=
                                target_anim.spline ||
                            !same_float_bits(
                                source_anim.intensity,
                                target_anim.intensity))
                            throw std::runtime_error(
                                "Morph payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest,
                            morph_pose_digest(
                                source_anim.poses));
                        fnv1a_string(
                            target_digest,
                            morph_pose_digest(
                                target_anim.poses));
                        fnv1a_string(
                            source_digest, source_anim.target);
                        fnv1a_string(
                            target_digest, target_anim.target);
                        for (const auto value :
                             {source_anim.normals ? 1u : 0u,
                              source_anim.spline ? 1u : 0u})
                            fnv1a_value(source_digest, value);
                        for (const auto value :
                             {target_anim.normals ? 1u : 0u,
                              target_anim.spline ? 1u : 0u})
                            fnv1a_value(target_digest, value);
                        fnv1a_value(
                            source_digest, source_anim.intensity);
                        fnv1a_value(
                            target_digest, target_anim.intensity);
                        for (const auto& pose : source_anim.poses)
                            key_rows += pose.keys.size();
                    } else if (
                        source_object.type == "Movie") {
                        const auto source_anim =
                            gh::milo_object::parse_movie(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_movie8(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 8 ||
                            source_anim.file != target_anim.file ||
                            source_anim.texture !=
                                target_anim.texture ||
                            source_anim.stream !=
                                target_anim.stream ||
                            source_anim.loop != target_anim.loop)
                            throw std::runtime_error(
                                "Movie payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest, source_anim.file);
                        fnv1a_string(
                            target_digest, target_anim.file);
                        fnv1a_string(
                            source_digest, source_anim.texture);
                        fnv1a_string(
                            target_digest, target_anim.texture);
                        const uint8_t source_stream =
                            source_anim.stream ? 1 : 0;
                        const uint8_t target_stream =
                            target_anim.stream ? 1 : 0;
                        const uint8_t source_loop =
                            source_anim.loop ? 1 : 0;
                        const uint8_t target_loop =
                            target_anim.loop ? 1 : 0;
                        fnv1a_value(
                            source_digest, source_stream);
                        fnv1a_value(
                            target_digest, target_stream);
                        fnv1a_value(
                            source_digest, source_loop);
                        fnv1a_value(
                            target_digest, target_loop);
                    } else if (
                        source_object.type == "ParticleSysAnim") {
                        const auto source_anim =
                            gh::milo_object::parse_particle_sys_anim(
                                source_object.body_bytes);
                        const auto target_anim =
                            gh::milo_object::parse_particle_sys_anim3(
                                target_entry->body_bytes);
                        source_revision = source_anim.revision;
                        target_revision = target_anim.revision;
                        if (!native_animation_bases_are_default(
                                target_anim.object_fields,
                                target_anim.animatable) ||
                            target_anim.revision != 3 ||
                            source_anim.particle_system !=
                                target_anim.particle_system ||
                            !same_array_keys(
                                source_anim.start_color_keys,
                                target_anim.start_color_keys) ||
                            !same_array_keys(
                                source_anim.end_color_keys,
                                target_anim.end_color_keys) ||
                            !same_array_keys(
                                source_anim.emit_rate_keys,
                                target_anim.emit_rate_keys) ||
                            source_anim.keys_owner !=
                                target_anim.keys_owner ||
                            !same_array_keys(
                                source_anim.speed_keys,
                                target_anim.speed_keys) ||
                            !same_array_keys(
                                source_anim.life_keys,
                                target_anim.life_keys) ||
                            !same_array_keys(
                                source_anim.start_size_keys,
                                target_anim.start_size_keys))
                            throw std::runtime_error(
                                "ParticleSysAnim payload differs: " +
                                source_object.name);
                        fnv1a_string(
                            source_digest,
                            source_anim.particle_system);
                        fnv1a_string(
                            target_digest,
                            target_anim.particle_system);
                        for (const auto& digest :
                             {array_key_digest(
                                  source_anim.start_color_keys),
                              array_key_digest(
                                  source_anim.end_color_keys),
                              array_key_digest(
                                  source_anim.emit_rate_keys)})
                            fnv1a_string(
                                source_digest, digest);
                        for (const auto& digest :
                             {array_key_digest(
                                  target_anim.start_color_keys),
                              array_key_digest(
                                  target_anim.end_color_keys),
                              array_key_digest(
                                  target_anim.emit_rate_keys)})
                            fnv1a_string(
                                target_digest, digest);
                        fnv1a_string(
                            source_digest, source_anim.keys_owner);
                        fnv1a_string(
                            target_digest, target_anim.keys_owner);
                        for (const auto& digest :
                             {array_key_digest(
                                  source_anim.speed_keys),
                              array_key_digest(
                                  source_anim.life_keys),
                              array_key_digest(
                                  source_anim.start_size_keys)})
                            fnv1a_string(
                                source_digest, digest);
                        for (const auto& digest :
                             {array_key_digest(
                                  target_anim.speed_keys),
                              array_key_digest(
                                  target_anim.life_keys),
                              array_key_digest(
                                  target_anim.start_size_keys)})
                            fnv1a_string(
                                target_digest, digest);
                        key_rows =
                            source_anim.start_color_keys.size() +
                            source_anim.end_color_keys.size() +
                            source_anim.emit_rate_keys.size() +
                            source_anim.speed_keys.size() +
                            source_anim.life_keys.size() +
                            source_anim.start_size_keys.size();
                    }
                    if (source_digest != target_digest)
                        throw std::runtime_error(
                            "animation payload digest differs: " +
                            source_object.name);
                    const bool filter_required =
                        validate_legacy_animation_filter(
                            verify_directory.entries,
                            source_object.name, *legacy);
                    animation_payload_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.type) << '\t'
                        << cell(source_object.name) << '\t'
                        << source_revision << '\t'
                        << cell(source_object.type) << '\t'
                        << target_revision << '\t'
                        << legacy->operations.size() << '\t'
                        << legacy_anim_operation_digest(
                               legacy->operations)
                        << '\t' << legacy->objects.size()
                        << '\t'
                        << cell(joined_strings(legacy->objects))
                        << '\t' << (filter_required ? 1 : 0)
                        << '\t'
                        << cell(
                               filter_required
                                   ? stem(source_object.name) +
                                         ".filt"
                                   : std::string())
                        << '\t' << key_rows
                        << '\t' << fnv1a_hex(source_digest)
                        << '\t' << fnv1a_hex(target_digest)
                        << "\texact\n";
                    ++animation_payload_rows;
                    animation_payload_keys += key_rows;
                    animation_payload_memberships +=
                        legacy->objects.size();
                    if (filter_required)
                        ++animation_payload_filters;
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "MatAnim") continue;
                    const auto source_anim =
                        gh::milo_object::parse_mat_anim(
                            source_object.body_bytes);
                    ++mat_anim_source_objects;
                    const auto stage_end_frame =
                        [](const gh::milo_object::MatAnimStage& stage) {
                            float end = 0.0f;
                            const auto include =
                                [&end](const auto& keys) {
                                    for (const auto& key : keys)
                                        end = std::max(
                                            end, key.frame);
                                };
                            include(stage.translation_keys);
                            include(stage.scale_keys);
                            include(stage.rotation_keys);
                            include(stage.texture_keys);
                            return end;
                        };
                    const gh::milo_object::MatAnimStage empty_stage;
                    const auto audit_pass =
                        [&](int64_t source_stage_index,
                            const std::string& target_name,
                            const std::string& emission,
                            bool is_root,
                            bool serialized_override) {
                            const auto& stage =
                                source_stage_index < 0
                                    ? empty_stage
                                    : source_anim.stages[
                                          static_cast<size_t>(
                                              source_stage_index)];
                            const std::string expected_material =
                                is_root
                                    ? source_anim.material
                                    : (source_anim.material.empty()
                                           ? std::string()
                                           : stem(
                                                 source_anim.material) +
                                                 "_" +
                                                 std::to_string(
                                                     source_stage_index +
                                                     1) +
                                                 ".mat");
                            const std::string expected_owner =
                                is_root
                                    ? source_anim.keys_owner
                                    : target_name;
                            const auto& expected_color =
                                is_root
                                    ? source_anim.color_keys
                                    : std::vector<
                                          gh::milo_object::ColorKey>{};
                            const auto& expected_alpha =
                                is_root
                                    ? source_anim.alpha_keys
                                    : std::vector<
                                          gh::milo_object::MorphKey>{};
                            uint64_t source_digest =
                                UINT64_C(14695981039346656037);
                            fnv1a_string(
                                source_digest, expected_material);
                            fnv1a_string(
                                source_digest, expected_owner);
                            fnv1a_string(
                                source_digest,
                                array_key_digest(
                                    stage.translation_keys));
                            fnv1a_string(
                                source_digest,
                                array_key_digest(
                                    stage.scale_keys));
                            fnv1a_string(
                                source_digest,
                                array_key_digest(
                                    stage.rotation_keys));
                            fnv1a_string(
                                source_digest,
                                object_key_digest(
                                    stage.texture_keys));
                            fnv1a_string(
                                source_digest,
                                array_key_digest(
                                    expected_color));
                            fnv1a_string(
                                source_digest,
                                morph_key_digest(
                                    expected_alpha));
                            const size_t key_rows =
                                stage.translation_keys.size() +
                                stage.scale_keys.size() +
                                stage.rotation_keys.size() +
                                stage.texture_keys.size() +
                                expected_color.size() +
                                expected_alpha.size();
                            if (emission ==
                                "static_early_stage_consumed") {
                                mat_anim_value_report
                                    << cell(entry.full_path) << '\t'
                                    << cell(source_object.name) << '\t'
                                    << source_anim.revision << '\t'
                                    << source_anim.stages.size() << '\t'
                                    << source_stage_index << "\t\t0\t\t\t"
                                    << stage.translation_keys.size()
                                    << '\t' << stage.scale_keys.size()
                                    << '\t' << stage.rotation_keys.size()
                                    << '\t' << stage.texture_keys.size()
                                    << '\t' << expected_color.size()
                                    << '\t' << expected_alpha.size()
                                    << '\t'
                                    << fnv1a_hex(source_digest)
                                    << "\t\t0\t"
                                    << emission
                                    << "\tretail_consumed\n";
                                ++mat_anim_value_rows;
                                ++mat_anim_static_rows;
                                mat_anim_key_rows += key_rows;
                                return;
                            }
                            const auto target_entry =
                                std::find_if(
                                    verify_directory.entries.begin(),
                                    verify_directory.entries.end(),
                                    [&](const gh::milo::Entry& target) {
                                        return target.type ==
                                                   "MatAnim" &&
                                               target.name ==
                                                   target_name;
                                    });
                            if (target_entry ==
                                verify_directory.entries.end())
                                throw std::runtime_error(
                                    "MatAnim target missing: " +
                                    target_name);
                            const auto target_anim =
                                gh::milo_object::parse_mat_anim7(
                                    target_entry->body_bytes);
                            uint64_t target_digest =
                                UINT64_C(14695981039346656037);
                            fnv1a_string(
                                target_digest,
                                target_anim.material);
                            fnv1a_string(
                                target_digest,
                                target_anim.keys_owner);
                            fnv1a_string(
                                target_digest,
                                array_key_digest(
                                    target_anim.translation_keys));
                            fnv1a_string(
                                target_digest,
                                array_key_digest(
                                    target_anim.scale_keys));
                            fnv1a_string(
                                target_digest,
                                array_key_digest(
                                    target_anim.rotation_keys));
                            fnv1a_string(
                                target_digest,
                                object_key_digest(
                                    target_anim.texture_keys));
                            fnv1a_string(
                                target_digest,
                                array_key_digest(
                                    target_anim.color_keys));
                            fnv1a_string(
                                target_digest,
                                morph_key_digest(
                                    target_anim.alpha_keys));
                            const bool values_match =
                                native_animation_bases_are_default(
                                    target_anim.object_fields,
                                    target_anim.animatable) &&
                                target_anim.revision == 7 &&
                                target_anim.material ==
                                    expected_material &&
                                target_anim.keys_owner ==
                                    expected_owner &&
                                same_array_keys(
                                    stage.translation_keys,
                                    target_anim.translation_keys) &&
                                same_array_keys(
                                    stage.scale_keys,
                                    target_anim.scale_keys) &&
                                same_array_keys(
                                    stage.rotation_keys,
                                    target_anim.rotation_keys) &&
                                same_object_keys(
                                    stage.texture_keys,
                                    target_anim.texture_keys) &&
                                same_array_keys(
                                    expected_color,
                                    target_anim.color_keys) &&
                                same_morph_keys(
                                    expected_alpha,
                                    target_anim.alpha_keys) &&
                                source_digest == target_digest;
                            if (!values_match &&
                                !serialized_override)
                                throw std::runtime_error(
                                    "MatAnim values differ: " +
                                    target_name);
                            bool filter_required = false;
                            if (!serialized_override) {
                                filter_required =
                                    validate_legacy_animation_filter(
                                        verify_directory.entries,
                                        target_name,
                                        source_anim.animatable);
                                if (filter_required)
                                    ++mat_anim_filter_rows;
                            }
                            const std::string status =
                                serialized_override
                                    ? (values_match
                                           ? "serialized_override_exact"
                                           : "serialized_override")
                                    : "exact";
                            mat_anim_value_report
                                << cell(entry.full_path) << '\t'
                                << cell(source_object.name) << '\t'
                                << source_anim.revision << '\t'
                                << source_anim.stages.size() << '\t'
                                << source_stage_index << '\t'
                                << cell(target_name) << '\t'
                                << target_anim.revision << '\t'
                                << cell(target_anim.material) << '\t'
                                << cell(target_anim.keys_owner) << '\t'
                                << stage.translation_keys.size()
                                << '\t' << stage.scale_keys.size()
                                << '\t' << stage.rotation_keys.size()
                                << '\t' << stage.texture_keys.size()
                                << '\t' << expected_color.size()
                                << '\t' << expected_alpha.size()
                                << '\t' << fnv1a_hex(source_digest)
                                << '\t' << fnv1a_hex(target_digest)
                                << '\t'
                                << (filter_required ? 1 : 0)
                                << '\t' << emission
                                << '\t' << status << '\n';
                            ++mat_anim_value_rows;
                            mat_anim_key_rows += key_rows;
                            if (serialized_override)
                                ++mat_anim_override_rows;
                        };
                    const int64_t root_stage =
                        source_anim.stages.empty()
                            ? -1
                            : static_cast<int64_t>(
                                  source_anim.stages.size() - 1);
                    audit_pass(
                        root_stage, source_object.name,
                        "root", true, false);
                    const std::string anim_base =
                        stem(source_object.name);
                    for (size_t stage_index = 0;
                         stage_index + 1 <
                             source_anim.stages.size();
                         ++stage_index) {
                        if (stage_end_frame(
                                source_anim.stages[stage_index]) ==
                            0.0f) {
                            audit_pass(
                                static_cast<int64_t>(stage_index),
                                "", "static_early_stage_consumed",
                                false, false);
                            continue;
                        }
                        const std::string target_name =
                            anim_base + "_" +
                            std::to_string(stage_index + 1) +
                            ".mnm";
                        const auto existing =
                            source_types_by_name.find(target_name);
                        const bool serialized_override =
                            existing != source_types_by_name.end() &&
                            existing->second == "MatAnim";
                        audit_pass(
                            static_cast<int64_t>(stage_index),
                            target_name,
                            serialized_override
                                ? "serialized_override"
                                : "split_pass",
                            false, serialized_override);
                    }
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "Tex") continue;
                    const auto source_tex =
                        gh::milo_object::parse_tex(
                            source_object.body_bytes);
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "Tex" &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted Tex is missing: " +
                            source_object.name);
                    const auto target_tex =
                        gh::milo_object::parse_tex10(
                            found->body_bytes);
                    if (target_tex.revision != 10 ||
                        target_tex.object_fields.revision != 0 ||
                        !target_tex.object_fields.type.empty() ||
                        target_tex.object_fields.has_type_properties ||
                        target_tex.object_fields.type_property_id != 0 ||
                        !target_tex.object_fields.type_properties.empty() ||
                        source_tex.width != target_tex.width ||
                        source_tex.height != target_tex.height ||
                        source_tex.bits_per_pixel !=
                            target_tex.bits_per_pixel ||
                        source_tex.external_path !=
                            target_tex.external_path ||
                        !same_float_bits(
                            source_tex.mipmap_bias,
                            target_tex.mipmap_bias) ||
                        source_tex.type != target_tex.type ||
                        source_tex.use_external !=
                            target_tex.use_external ||
                        source_tex.has_bitmap !=
                            target_tex.has_bitmap ||
                        source_tex.bitmap.header_kind !=
                            target_tex.bitmap.header_kind ||
                        source_tex.bitmap.bits_per_pixel !=
                            target_tex.bitmap.bits_per_pixel ||
                        source_tex.bitmap.encoding !=
                            target_tex.bitmap.encoding ||
                        source_tex.bitmap.mipmap_count !=
                            target_tex.bitmap.mipmap_count ||
                        source_tex.bitmap.width !=
                            target_tex.bitmap.width ||
                        source_tex.bitmap.height !=
                            target_tex.bitmap.height ||
                        source_tex.bitmap.bytes_per_line !=
                            target_tex.bitmap.bytes_per_line ||
                        source_tex.bitmap.wii_alpha !=
                            target_tex.bitmap.wii_alpha ||
                        source_tex.bitmap.reserved !=
                            target_tex.bitmap.reserved ||
                        source_tex.bitmap.data !=
                            target_tex.bitmap.data)
                        throw std::runtime_error(
                            "converted Tex values differ: " +
                            source_object.name);
                    tex_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.name) << '\t'
                        << cell(found->name) << '\t'
                        << source_tex.revision << '\t'
                        << target_tex.revision << '\t'
                        << target_tex.object_fields.revision << '\t'
                        << cell(target_tex.object_fields.type) << '\t'
                        << (target_tex.object_fields.has_type_properties
                                ? 1
                                : 0)
                        << '\t'
                        << source_tex.width << '\t'
                        << target_tex.width << '\t'
                        << source_tex.height << '\t'
                        << target_tex.height << '\t'
                        << source_tex.bits_per_pixel << '\t'
                        << target_tex.bits_per_pixel << '\t'
                        << cell(source_tex.external_path) << '\t'
                        << cell(target_tex.external_path) << '\t'
                        << std::setprecision(9)
                        << source_tex.mipmap_bias << '\t'
                        << target_tex.mipmap_bias << '\t'
                        << source_tex.type << '\t'
                        << target_tex.type << '\t'
                        << (source_tex.use_external ? 1 : 0) << '\t'
                        << (target_tex.use_external ? 1 : 0) << '\t'
                        << (source_tex.has_bitmap ? 1 : 0) << '\t'
                        << (target_tex.has_bitmap ? 1 : 0) << '\t'
                        << static_cast<unsigned>(
                               source_tex.bitmap.header_kind)
                        << '\t'
                        << static_cast<unsigned>(
                               target_tex.bitmap.header_kind)
                        << '\t'
                        << static_cast<unsigned>(
                               source_tex.bitmap.bits_per_pixel)
                        << '\t'
                        << static_cast<unsigned>(
                               target_tex.bitmap.bits_per_pixel)
                        << '\t'
                        << source_tex.bitmap.encoding << '\t'
                        << target_tex.bitmap.encoding << '\t'
                        << static_cast<unsigned>(
                               source_tex.bitmap.mipmap_count)
                        << '\t'
                        << static_cast<unsigned>(
                               target_tex.bitmap.mipmap_count)
                        << '\t'
                        << source_tex.bitmap.width << '\t'
                        << target_tex.bitmap.width << '\t'
                        << source_tex.bitmap.height << '\t'
                        << target_tex.bitmap.height << '\t'
                        << source_tex.bitmap.bytes_per_line << '\t'
                        << target_tex.bitmap.bytes_per_line << '\t'
                        << source_tex.bitmap.wii_alpha << '\t'
                        << target_tex.bitmap.wii_alpha << '\t'
                        << byte_array_digest(
                               source_tex.bitmap.reserved)
                        << '\t'
                        << byte_array_digest(
                               target_tex.bitmap.reserved)
                        << '\t'
                        << source_tex.bitmap.data.size() << '\t'
                        << target_tex.bitmap.data.size() << '\t'
                        << byte_vector_digest(
                               source_tex.bitmap.data)
                        << '\t'
                        << byte_vector_digest(
                               target_tex.bitmap.data)
                        << "\texact\n";
                    ++tex_value_rows;
                    if (source_tex.has_bitmap) {
                        ++tex_bitmap_rows;
                        tex_bitmap_bytes +=
                            source_tex.bitmap.data.size();
                    }
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "Mat") continue;
                    const auto source_mat =
                        gh::milo_object::parse_mat(
                            source_object.body_bytes);
                    const auto expected_passes =
                        gh::milo_object::
                            convert_mat21_to_mat27_passes(
                                source_mat, source_object.name);
                    if (expected_passes.empty())
                        throw std::runtime_error(
                            "Mat conversion emitted no root pass: " +
                            source_object.name);
                    std::vector<gh::milo_object::Mat27>
                        actual_passes;
                    actual_passes.reserve(expected_passes.size());
                    for (const auto& expected : expected_passes) {
                        const auto found = std::find_if(
                            verify_directory.entries.begin(),
                            verify_directory.entries.end(),
                            [&](const gh::milo::Entry& target) {
                                return target.type == "Mat" &&
                                       target.name == expected.name;
                            });
                        if (found == verify_directory.entries.end())
                            throw std::runtime_error(
                                "converted Mat pass is missing: " +
                                expected.name);
                        if (gh::milo_object::serialize_mat27(
                                expected.material) !=
                            found->body_bytes)
                            throw std::runtime_error(
                                "converted Mat values differ: " +
                                expected.name);
                        actual_passes.push_back(
                            gh::milo_object::parse_mat27(
                                found->body_bytes));
                    }
                    const auto& root = actual_passes.front();
                    const int32_t root_stage_blend =
                        source_mat.textures.empty()
                            ? -1
                            : static_cast<int32_t>(
                                  source_mat.textures.front()
                                      .stage_blend);
                    mat_root_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.name) << '\t'
                        << cell(expected_passes.front().name) << '\t'
                        << source_mat.revision << '\t'
                        << root.revision << '\t'
                        << source_mat.textures.size() << '\t'
                        << actual_passes.size() << '\t'
                        << source_mat.blend << '\t'
                        << root.blend << '\t'
                        << float_values(source_mat.color) << '\t'
                        << float_values(root.color) << '\t'
                        << (source_mat.use_environment ? 1 : 0)
                        << '\t'
                        << (root.use_environment ? 1 : 0) << '\t'
                        << (source_mat.vertex_ambient ? 1 : 0)
                        << '\t'
                        << (root.prelit ? 1 : 0) << '\t'
                        << (source_mat.vertex_dynamic ? 1 : 0)
                        << '\t'
                        << (source_mat.cull ? 1 : 0) << '\t'
                        << (root.cull ? 1 : 0) << '\t'
                        << source_mat.multipass << '\t'
                        << (source_mat.normalize ? 1 : 0) << '\t'
                        << source_mat.z_mode << '\t'
                        << root.z_mode << '\t'
                        << (source_mat.alpha_cut ? 1 : 0) << '\t'
                        << (root.alpha_cut ? 1 : 0) << '\t'
                        << (source_mat.alpha_write ? 1 : 0)
                        << '\t'
                        << (root.alpha_write ? 1 : 0) << '\t'
                        << root_stage_blend << '\t'
                        << (root.intensify ? 1 : 0)
                        << "\texact\n";
                    ++mat_root_value_rows;

                    for (size_t stage_index = 0;
                         stage_index < source_mat.textures.size();
                         ++stage_index) {
                        const auto& source_stage =
                            source_mat.textures[stage_index];
                        const auto& target_stage =
                            actual_passes[stage_index];
                        std::string rule =
                            stage_index == 0
                                ? "root_global_blend"
                                : "nonroot_native_pass";
                        if (stage_index == 0 &&
                            source_stage.stage_blend == 0)
                            rule = "root_disabled_texture";
                        else if (stage_index == 0 &&
                                 source_stage.stage_blend == 1)
                            rule = "root_multipass_src_reset";
                        else if (stage_index == 0 &&
                                 source_stage.stage_blend == 3)
                            rule = "root_src_alpha_intensify";
                        else if (stage_index != 0 &&
                                 source_mat.multipass == 1)
                            rule = "nonroot_multipass_src_reset";
                        mat_stage_value_report
                            << cell(entry.full_path) << '\t'
                            << cell(source_object.name) << '\t'
                            << stage_index << '\t'
                            << cell(expected_passes[stage_index].name)
                            << '\t'
                            << source_stage.stage_blend << '\t'
                            << target_stage.blend << '\t'
                            << source_stage.tex_gen << '\t'
                            << target_stage.tex_gen << '\t'
                            << source_stage.wrap << '\t'
                            << target_stage.tex_wrap << '\t'
                            << float_values(source_stage.transform)
                            << '\t'
                            << float_values(
                                   target_stage.texture_transform)
                            << '\t'
                            << cell(source_stage.texture) << '\t'
                            << cell(target_stage.diffuse_texture)
                            << '\t'
                            << target_stage.z_mode << '\t'
                            << float_values(target_stage.color) << '\t'
                            << (target_stage.use_environment ? 1 : 0)
                            << '\t'
                            << (target_stage.prelit ? 1 : 0) << '\t'
                            << (target_stage.intensify ? 1 : 0)
                            << '\t'
                            << cell(target_stage.next_pass) << '\t'
                            << rule << "\texact\n";
                        ++mat_stage_value_rows;
                    }
                }
                for (const auto& source_object : source.entries) {
                    if (source_object.type != "Mesh") continue;
                    const auto source_mesh =
                        gh::milo_object::parse_mesh(
                            source_object.body_bytes);
                    auto expected_mesh =
                        gh::milo_object::convert_mesh25_to_mesh28(
                            source_mesh);
                    const auto found = std::find_if(
                        verify_directory.entries.begin(),
                        verify_directory.entries.end(),
                        [&](const gh::milo::Entry& target) {
                            return target.type == "Mesh" &&
                                   target.name == source_object.name;
                        });
                    if (found == verify_directory.entries.end())
                        throw std::runtime_error(
                            "converted Mesh is missing: " +
                            source_object.name);
                    const auto target_mesh =
                        gh::milo_object::parse_mesh28(
                            found->body_bytes,
                            verify_directory.dir_version);
                    const auto& direct_transform =
                        expected_mesh.transformable;
                    const bool direct_transform_exact =
                        same_float_bits(
                            direct_transform.local,
                            target_mesh.transformable.local) &&
                        same_float_bits(
                            direct_transform.world,
                            target_mesh.transformable.world) &&
                        direct_transform.constraint ==
                            target_mesh.transformable.constraint &&
                        direct_transform.target ==
                            target_mesh.transformable.target &&
                        direct_transform.preserve_scale ==
                            target_mesh.transformable.preserve_scale &&
                        direct_transform.parent ==
                            target_mesh.transformable.parent;
                    expected_mesh.transformable =
                        target_mesh.transformable;
                    if (gh::milo_object::serialize_mesh28(
                            expected_mesh) != found->body_bytes)
                        throw std::runtime_error(
                            "converted Mesh non-graph values differ: " +
                            source_object.name);
                    if (!same_mesh_vertices(
                            source_mesh.vertices,
                            target_mesh.vertices) ||
                        source_mesh.faces != target_mesh.faces ||
                        source_mesh.patches !=
                            target_mesh.group_sizes ||
                        source_mesh.has_bones !=
                            target_mesh.has_bones ||
                        !same_mesh_bones(
                            source_mesh.bone_slots,
                            target_mesh.bone_slots) ||
                        !same_mesh_strips(
                            source_mesh.strip_results,
                            target_mesh.group_sections) ||
                        source_mesh.drawable.showing !=
                            target_mesh.drawable.showing ||
                        !same_float_bits(
                            source_mesh.drawable.sphere,
                            target_mesh.drawable.sphere) ||
                        source_mesh.material !=
                            target_mesh.material ||
                        source_mesh.geometry_owner !=
                            target_mesh.geometry_owner ||
                        source_mesh.mutable_flags !=
                            target_mesh.mutable_flags ||
                        source_mesh.volume != target_mesh.volume ||
                        source_mesh.has_bsp_tree ||
                        target_mesh.bsp_nodes.size() != 1 ||
                        target_mesh.bsp_nodes.front().has_value)
                        throw std::runtime_error(
                            "converted Mesh value mapping differs: " +
                            source_object.name);
                    const std::string source_vertex_hash =
                        mesh_vertex_digest(source_mesh.vertices);
                    const std::string target_vertex_hash =
                        mesh_vertex_digest(target_mesh.vertices);
                    const std::string source_face_hash =
                        mesh_face_digest(source_mesh.faces);
                    const std::string target_face_hash =
                        mesh_face_digest(target_mesh.faces);
                    const std::string source_group_hash =
                        mesh_group_digest(source_mesh.patches);
                    const std::string target_group_hash =
                        mesh_group_digest(target_mesh.group_sizes);
                    const std::string source_bone_hash =
                        mesh_bone_digest(source_mesh.bone_slots);
                    const std::string target_bone_hash =
                        mesh_bone_digest(target_mesh.bone_slots);
                    const std::string source_strip_hash =
                        mesh_strip_digest(source_mesh.strip_results);
                    const std::string target_strip_hash =
                        mesh_strip_digest(target_mesh.group_sections);
                    mesh_value_report
                        << cell(entry.full_path) << '\t'
                        << cell(source_object.name) << '\t'
                        << cell(found->name) << '\t'
                        << source_mesh.revision << '\t'
                        << target_mesh.revision << '\t'
                        << float_array_pair_digest(
                               source_mesh.transformable.local,
                               source_mesh.transformable.world)
                        << '\t'
                        << float_array_pair_digest(
                               target_mesh.transformable.local,
                               target_mesh.transformable.world)
                        << '\t'
                        << source_mesh.transformable.constraint
                        << '\t'
                        << target_mesh.transformable.constraint
                        << '\t'
                        << cell(source_mesh.transformable.target)
                        << '\t'
                        << cell(target_mesh.transformable.target)
                        << '\t'
                        << (source_mesh.transformable.preserve_scale
                                ? 1
                                : 0)
                        << '\t'
                        << (target_mesh.transformable.preserve_scale
                                ? 1
                                : 0)
                        << '\t'
                        << cell(source_mesh.transformable.parent)
                        << '\t'
                        << cell(target_mesh.transformable.parent)
                        << '\t'
                        << source_mesh.transformable.children.size()
                        << '\t'
                        << string_vector_digest(
                               source_mesh.transformable.children)
                        << '\t'
                        << (source_mesh.transformable.children.empty()
                                ? "empty"
                                : "directory_transform_graph")
                        << '\t'
                        << (source_mesh.drawable.showing ? 1 : 0)
                        << '\t'
                        << (target_mesh.drawable.showing ? 1 : 0)
                        << '\t'
                        << float_array_digest(
                               source_mesh.drawable.sphere)
                        << '\t'
                        << float_array_digest(
                               target_mesh.drawable.sphere)
                        << '\t'
                        << cell(source_mesh.material) << '\t'
                        << cell(target_mesh.material) << '\t'
                        << cell(source_mesh.geometry_owner) << '\t'
                        << cell(target_mesh.geometry_owner) << '\t'
                        << source_mesh.mutable_flags << '\t'
                        << target_mesh.mutable_flags << '\t'
                        << source_mesh.volume << '\t'
                        << target_mesh.volume << '\t'
                        << (source_mesh.has_bsp_tree ? 1 : 0)
                        << '\t'
                        << target_mesh.bsp_nodes.size() << '\t'
                        << source_mesh.vertices.size() << '\t'
                        << target_mesh.vertices.size() << '\t'
                        << source_vertex_hash << '\t'
                        << target_vertex_hash << '\t'
                        << source_mesh.faces.size() << '\t'
                        << target_mesh.faces.size() << '\t'
                        << source_face_hash << '\t'
                        << target_face_hash << '\t'
                        << source_mesh.patches.size() << '\t'
                        << target_mesh.group_sizes.size() << '\t'
                        << source_group_hash << '\t'
                        << target_group_hash << '\t'
                        << (source_mesh.has_bones ? 1 : 0) << '\t'
                        << (target_mesh.has_bones ? 1 : 0) << '\t'
                        << source_bone_hash << '\t'
                        << target_bone_hash << '\t'
                        << source_mesh.strip_results.size() << '\t'
                        << target_mesh.group_sections.size() << '\t'
                        << source_strip_hash << '\t'
                        << target_strip_hash << '\t'
                        << (direct_transform_exact
                                ? "exact"
                                : "exact_with_directory_transform")
                        << '\n';
                    ++mesh_value_rows;
                }
                if (entry.full_path.rfind("venues/", 0) == 0) {
                    const auto target_path =
                        gh1_venue_target_path(entry.full_path);
                    if (!target_path)
                        throw std::runtime_error(
                            "invalid GH1 venue bundle path: " +
                            entry.full_path);
                    if (!venue_directories.emplace(
                             target_path->target_path,
                             verify_directory).second)
                        throw std::runtime_error(
                            "duplicate target venue directory: " +
                            target_path->target_path);
                    venue_directory_sources.emplace(
                        target_path->target_path,
                        entry.full_path);
                }

                const auto repeat =
                    gh::milo_convert::
                        convert_gh1_directory_to_gh2_rnddir(
                            source, target_directory_name,
                            authored_draw_root);
                const auto repeat_payload =
                    gh::milo::serialize_directory(repeat.directory);
                const auto repeat_bytes =
                    gh::milo::serialize_container(
                        gh::milo::make_container(repeat_payload));
                if (repeat_payload != target_payload ||
                    repeat_bytes != target_bytes ||
                    gh::milo_convert::manifest_tsv(repeat) !=
                        gh::milo_convert::manifest_tsv(result))
                    throw std::runtime_error(
                        "target GH2 conversion is nondeterministic");
                ++emitted_assets;
                ++complete_assets;
            } catch (const std::exception& ex) {
                ++blocked_objects;
                ++blockers["<directory>"];
                report << cell(entry.full_path)
                       << "\t\t\t\t\tblocked\t"
                       << cell(ex.what()) << '\n';
            }
        }
        size_t discarded_value_rows = 0;
        size_t discarded_nondefault_rows = 0;
        for (const auto& [key, count] :
             discarded_value_observations) {
            const auto& [source_type, source_revision, source_field,
                         value_class] = key;
            const std::string status =
                discarded_value_status(
                    source_type, source_field, value_class);
            discarded_value_report
                << cell(source_type) << '\t'
                << source_revision << '\t'
                << cell(source_field) << '\t'
                << cell(value_class) << '\t'
                << count << '\t'
                << status << '\n';
            ++discarded_value_rows;
            if (status == "nondefault_requires_retail_proof")
                ++discarded_nondefault_rows;
        }
        for (const DiscardedValueInstance& instance :
             discarded_value_instances) {
            discarded_value_instance_report
                << cell(instance.archive_path) << '\t'
                << cell(instance.source_type) << '\t'
                << instance.source_revision << '\t'
                << cell(instance.object_name) << '\t'
                << cell(instance.source_field) << '\t'
                << cell(instance.value_class) << '\t'
                << discarded_value_status(
                       instance.source_type,
                       instance.source_field,
                       instance.value_class)
                << '\n';
        }
        const std::set<std::string> venue_support_extensions = {
            ".bnk", ".dtb", ".nse", ".seq",
        };
        for (const auto& entry : archive.entries()) {
            if (entry.full_path.rfind("venues/", 0) != 0 ||
                venue_support_extensions.find(
                    extension(entry.name)) ==
                    venue_support_extensions.end())
                continue;
            const auto target_info =
                gh1_venue_target_path(entry.full_path);
            if (!target_info)
                throw std::runtime_error(
                    "invalid GH1 venue support path: " +
                    entry.full_path);
            if (lower(entry.name) == "camera.dtb") {
                const std::string main_path =
                    "world/" + target_info->target_venue + "/gen/" +
                    target_info->target_venue + ".milo_ps2";
                const std::string campaths_path =
                    "world/" + target_info->target_venue +
                    "/gen/campaths.milo_ps2";
                try {
                    const auto main = venue_directories.find(main_path);
                    const auto campaths =
                        venue_directories.find(campaths_path);
                    if (main == venue_directories.end())
                        throw std::runtime_error(
                            "converted main venue directory is missing");
                    if (campaths == venue_directories.end())
                        throw std::runtime_error(
                            "converted campaths directory is missing");
                    const auto camera_bytes =
                        archive.read_entry(entry, ark_paths);
                    if (!shared_camera_helper_filter)
                        throw std::runtime_error("GH1 game config cam_filter is required for camera conversion");
                    const auto converted =
                        gh::milo_convert::
                            convert_gh1_venue_cameras_to_gh2_camshots(
                                camera_bytes, shared_camera_path_policy, main->second,
                                campaths->second,
                                shared_camera_animations, shared_camera_helper_filter);
                    const auto repeat =
                        gh::milo_convert::
                            convert_gh1_venue_cameras_to_gh2_camshots(
                                camera_bytes, shared_camera_path_policy, main->second,
                                campaths->second,
                                shared_camera_animations, shared_camera_helper_filter);
                    if (gh::milo::serialize_directory(
                            converted.main_directory) !=
                            gh::milo::serialize_directory(
                                repeat.main_directory) ||
                        converted.records != repeat.records ||
                        converted.keyframes != repeat.keyframes ||
                        converted.shaky_records !=
                            repeat.shaky_records ||
                        converted.adaptive_subdivisions !=
                            repeat.adaptive_subdivisions)
                        throw std::runtime_error(
                            "native venue camera conversion is "
                            "nondeterministic");
                    venue_directories[main_path] =
                        converted.main_directory;
                    venue_camera_records += converted.records;
                    venue_camera_keyframes += converted.keyframes;
                    semantic_field_instances[
                        {"VenueCamRecord", 1}] += converted.records;
                    synthesized_objects += converted.records;
                    semantic_objects += converted.records;
                    venue_camera_report
                        << cell(entry.full_path) << '\t'
                        << cell(main_path) << '\t'
                        << converted.records << '\t'
                        << converted.keyframes << '\t'
                        << converted.shaky_records << '\t'
                        << converted.adaptive_subdivisions << '\t'
                        << converted
                               .maximum_position_linearization_error
                        << '\t'
                        << converted
                               .maximum_rotation_linearization_error
                        << '\t'
                        << converted
                               .maximum_screen_linearization_error
                        << '\t'
                        << converted
                               .maximum_fov_linearization_error
                        << "\tconverted\n";
                } catch (const std::exception& ex) {
                    ++venue_camera_blocked;
                    ++blocked_objects;
                    ++blockers["<venue-camera>"];
                    venue_camera_report
                        << cell(entry.full_path) << '\t'
                        << cell(main_path)
                        << "\t0\t0\t0\t0\t0\t0\t0\t0\tblocked:"
                        << cell(ex.what()) << '\n';
                }
                continue;
            }
            if (target_info->primary &&
                extension(entry.name) == ".dtb") {
                const std::string target_path =
                    target_info->target_path;
                try {
                    const auto bytes =
                        archive.read_entry(entry, ark_paths);
                    const auto converted =
                        gh::milo_convert::
                            convert_gh1_venue_script_to_gh2_worlddir(
                                bytes, target_info->target_venue);
                    const auto repeat =
                        gh::milo_convert::
                            convert_gh1_venue_script_to_gh2_worlddir(
                                bytes, target_info->target_venue);
                    if (repeat.bytes != converted.bytes ||
                        repeat.dta != converted.dta ||
                        repeat.loaded_sections !=
                            converted.loaded_sections ||
                        repeat.initialized_states !=
                            converted.initialized_states ||
                        repeat.handler_names !=
                            converted.handler_names)
                        throw std::runtime_error(
                            "native venue script conversion is "
                            "nondeterministic");
                    for (const auto& [section, directory] :
                         converted.loaded_sections) {
                        (void)section;
                        const std::string section_path =
                            "venues/" + target_info->source_venue +
                            "/gen/" + directory + ".rnd_ps2";
                        if (!archive.find(section_path))
                            throw std::runtime_error(
                                "venue load_section source is absent: " +
                                section_path);
                        const auto section_target =
                            gh1_venue_target_path(section_path);
                        if (!section_target ||
                            section_target->target_venue !=
                                target_info->target_venue)
                            throw std::runtime_error(
                                "venue load_section target mapping is "
                                "invalid: " + section_path);
                    }
                    if (!venue_loaded_sections.emplace(
                            target_info->target_venue,
                            converted.loaded_sections).second)
                        throw std::runtime_error(
                            "duplicate native venue script for " +
                            target_info->target_venue);
                    if (std::find(
                            converted.handler_names.begin(),
                            converted.handler_names.end(),
                            "start") !=
                        converted.handler_names.end())
                        venue_start_handlers.insert(
                            target_info->target_venue);
                    venue_script_report
                        << cell(entry.full_path) << '\t'
                        << cell(target_path) << '\t'
                        << converted.source_roots << '\t'
                        << converted.recognized_roots << '\t'
                        << converted.unrecognized_roots << '\t'
                        << converted.loaded_sections.size() << '\t'
                        << converted.initialized_states.size() << '\t'
                        << converted.source_functions << '\t'
                        << converted.handlers << '\t'
                        << converted.function_calls_inlined << '\t'
                        << converted.foreach_loops_unrolled << '\t'
                        << converted.switch_anim_calls << '\t'
                        << converted.switch_anim_rt_calls << '\t'
                        << converted.anim_task_calls << '\t'
                        << converted.animate_to_calls << '\t'
                        << converted.delay_task_calls << '\t'
                        << converted.random_ranges_expanded << '\t'
                        << converted.bytes.size()
                        << "\tconverted\t"
                        << converted.stateful_ranges_resolved
                        << "\tGH1 Arena wrappers lowered to "
                           "native GH2 WorldDir script\n";
                    ++venue_scripts;
                    venue_script_load_sections +=
                        converted.loaded_sections.size();
                    venue_script_initial_states +=
                        converted.initialized_states.size();
                    ++semantic_field_instances[
                        {"VenueScript", 1}];
                    venue_script_bytes += converted.bytes.size();
                    if (!bundle_path.empty()) {
                        write_bundle_file(
                            fs::path(bundle_path),
                            target_path, converted.bytes);
                        venue_bundle_records.push_back(
                            {target_path, "venue-native-script",
                             entry.full_path, converted.bytes.size()});
                    }
                } catch (const std::exception& ex) {
                    ++venue_script_blocked;
                    ++blockers["<venue-script>"];
                    venue_script_report
                        << cell(entry.full_path) << '\t';
                    for (size_t column = 0; column < 16; ++column)
                        venue_script_report << "\t0";
                    venue_script_report
                        << "\tblocked\t0\t"
                        << cell(ex.what()) << '\n';
                }
                continue;
            }
            if (!bundle_path.empty()) {
                const auto bytes =
                    archive.read_entry(entry, ark_paths);
                write_bundle_file(
                    fs::path(bundle_path),
                    target_info->target_path, bytes);
                venue_bundle_records.push_back(
                    {target_info->target_path, "venue-support",
                     entry.full_path, bytes.size()});
            }
        }
        std::map<std::string, std::string>
            target_venue_source_names;
        for (const auto& [target_path, source_path] :
             venue_directory_sources) {
            (void)target_path;
            const auto info =
                gh1_venue_target_path(source_path);
            if (!info)
                throw std::runtime_error(
                    "invalid converted venue source path: " +
                    source_path);
            target_venue_source_names.emplace(
                info->target_venue, info->source_venue);
        }
        for (const auto& [target_venue, source_venue] :
             target_venue_source_names) {
            const std::string prefix =
                "world/" + target_venue + "/gen/";
            std::vector<gh::milo::Directory> sections;
            for (const auto& [target_path, directory] :
                 venue_directories) {
                if (target_path.rfind(prefix, 0) == 0)
                    sections.push_back(directory);
            }
            const auto converted =
                gh::milo_convert::
                    convert_gh1_venue_spots_to_gh2_waypoints(
                        target_venue, sections);
            const auto repeat =
                gh::milo_convert::
                    convert_gh1_venue_spots_to_gh2_waypoints(
                        target_venue, sections);
            if (gh::milo::serialize_directory(
                    converted.characters_directory) !=
                    gh::milo::serialize_directory(
                        repeat.characters_directory) ||
                converted.waypoints != repeat.waypoints ||
                converted.records.size() != repeat.records.size())
                throw std::runtime_error(
                    "native venue placement conversion is "
                    "nondeterministic");
            for (size_t index = 0;
                 index < converted.records.size(); ++index) {
                const auto& record = converted.records[index];
                const auto& repeated = repeat.records[index];
                if (record.role != repeated.role ||
                    record.source_helper != repeated.source_helper ||
                    record.target_waypoint !=
                        repeated.target_waypoint ||
                    record.flags != repeated.flags ||
                    record.source_world != repeated.source_world ||
                    record.target_transform !=
                        repeated.target_transform)
                    throw std::runtime_error(
                        "native venue placement ledger is "
                        "nondeterministic");
                venue_placement_report
                    << cell(source_venue) << '\t'
                    << cell(target_venue) << '\t'
                    << cell(record.role) << '\t'
                    << cell(record.source_helper) << '\t'
                    << cell(record.target_waypoint) << '\t'
                    << record.flags;
                venue_placement_report
                    << std::setprecision(
                           std::numeric_limits<float>::max_digits10);
                for (const float value : record.source_world)
                    venue_placement_report << '\t' << value;
                for (const float value : record.target_transform)
                    venue_placement_report << '\t' << value;
                venue_placement_report << "\tpass\n";
            }

            const std::string main_path =
                prefix + target_venue + ".milo_ps2";
            auto main = venue_directories.find(main_path);
            if (main == venue_directories.end())
                throw std::runtime_error(
                    "converted main venue directory is missing "
                    "for placement");
            gh::milo_convert::link_gh2_venue_characters_directory(
                main->second, target_venue);
            const auto section_spec =
                venue_loaded_sections.find(target_venue);
            if (section_spec == venue_loaded_sections.end())
                throw std::runtime_error(
                    "converted venue load_section manifest is missing");
            for (const auto& [section, directory] :
                 section_spec->second) {
                (void)section;
                const std::string section_path =
                    prefix + directory + ".milo_ps2";
                if (venue_directories.find(section_path) ==
                    venue_directories.end())
                    throw std::runtime_error(
                        "converted venue load_section directory is "
                        "missing: " + section_path);
            }
            gh::milo_convert::finalize_gh2_venue_world_directory(
                main->second, target_venue,
                section_spec->second,
                venue_start_handlers.find(target_venue) !=
                    venue_start_handlers.end());

            const std::string characters_path =
                prefix + target_venue + "_chars.milo_ps2";
            if (!venue_directories.emplace(
                     characters_path,
                     converted.characters_directory).second)
                throw std::runtime_error(
                    "duplicate converted venue characters directory");
            venue_directory_sources.emplace(
                characters_path,
                "venues/" + source_venue +
                    "/gen/<stage-spot-waypoints>");
            ++venue_placement_assets;
            venue_placement_waypoints += converted.waypoints;
            synthesized_objects += converted.waypoints;
            semantic_objects += converted.waypoints;
        }
        for (const auto& [target_path, directory] :
             venue_directories) {
            const auto payload =
                gh::milo::serialize_directory(directory);
            const auto bytes = gh::milo::serialize_container(
                gh::milo::make_container(payload));
            const auto verify_container =
                gh::milo::parse_container(bytes);
            const auto verify_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    payload ||
                gh::milo::serialize_container(verify_container) !=
                    bytes)
                throw std::runtime_error(
                    "final native venue directory round trip differs: " +
                    target_path);
            for (const auto& object : verify_directory.entries) {
                if (gh::milo_object::round_trip_gh2_object_body(
                        object.type, object.body_bytes,
                        verify_directory.dir_version) !=
                    object.body_bytes)
                    throw std::runtime_error(
                        "final native venue object round trip differs: " +
                        target_path + "::" + object.name);
            }
            const size_t references =
                gh::milo_convert::
                    validate_gh2_directory_references(
                        verify_directory);
            const std::string& source_path =
                venue_directory_sources.at(target_path);
            std::vector<std::string> subdirectories;
            if (verify_directory.dir_type == "WorldDir") {
                subdirectories =
                    gh::milo_object::parse_world_dir11(
                        verify_directory.dir_body_bytes)
                        .panel_directory.render_directory
                        .object_directory.subdirectories;
            } else if (verify_directory.dir_type == "RndDir") {
                subdirectories =
                    gh::milo_object::parse_rnd_dir8(
                        verify_directory.dir_body_bytes)
                        .object_directory.subdirectories;
            }
            std::string joined_subdirectories;
            for (size_t index = 0;
                 index < subdirectories.size(); ++index) {
                if (index) joined_subdirectories += ',';
                joined_subdirectories += subdirectories[index];
            }
            venue_report
                << cell(source_path) << '\t'
                << cell(target_path) << '\t'
                << cell(verify_directory.dir_type) << '\t'
                << cell(joined_subdirectories) << '\t'
                << verify_directory.entries.size() << '\t'
                << references << '\t'
                << payload.size() << '\t'
                << bytes.size()
                << "\tconverted\n";
            ++venue_assets;
            venue_references += references;
            if (!bundle_path.empty()) {
                write_bundle_file(
                    fs::path(bundle_path), target_path, bytes);
                venue_bundle_records.push_back(
                    {target_path, "venue-rnd", source_path,
                     bytes.size()});
            }
        }
        if (!bundle_path.empty()) {
            write_bundle_manifest(
                fs::path(bundle_path),
                "gh1-venue-bundle.tsv",
                venue_bundle_records);
        }
        size_t semantic_field_rows = 0;
        size_t semantic_field_source_rows = 0;
        size_t semantic_field_synthesized_rows = 0;
        for (const auto& [source_key, observed] :
             semantic_field_instances) {
            const auto contracts =
                gh::milo_convert::
                    gh1_to_gh2_semantic_field_contracts_for(
                        source_key.first, source_key.second);
            for (const auto& contract : contracts) {
                field_report
                    << cell(contract.source_type) << '\t'
                    << contract.source_revision << '\t'
                    << cell(contract.source_field) << '\t'
                    << cell(contract.target_type) << '\t'
                    << cell(contract.target_revision) << '\t'
                    << cell(contract.target_field) << '\t'
                    << cell(contract.disposition) << '\t'
                    << observed << "\tcontracted\t"
                    << cell(contract.rule) << '\t'
                    << cell(contract.verification) << '\n';
                ++semantic_field_rows;
                if (contract.source_field == "<synthesized>")
                    ++semantic_field_synthesized_rows;
                else
                    ++semantic_field_source_rows;
            }
        }
        if (unresolved_transform_child_links != 0)
            throw std::runtime_error(
                "transform graph contains " +
                std::to_string(unresolved_transform_child_links) +
                " unresolved legacy child links");
        std::printf(
            "assets=%zu complete=%zu incomplete=%zu acp=%zu "
            "acp_complete=%zu acp_incomplete=%zu converted=%zu "
            "synthesized=%zu blocked=%zu emitted=%zu semantic=%zu\n",
            assets, complete_assets, assets - complete_assets,
            acp_assets, complete_acp_assets,
            acp_assets - complete_acp_assets,
            converted_objects, synthesized_objects, blocked_objects,
            emitted_assets, semantic_objects);
        std::printf(
            "venue_conversion assets=%zu references=%zu "
            "report=%s bundle=%s\n",
            venue_assets, venue_references,
            venue_report_path.c_str(),
            bundle_path.empty()
                ? "<none>" : bundle_path.c_str());
        std::printf(
            "venue_scripts converted=%zu blocked=%zu bytes=%zu "
            "load_sections=%zu initial_states=%zu report=%s\n",
            venue_scripts, venue_script_blocked, venue_script_bytes,
            venue_script_load_sections, venue_script_initial_states,
            venue_script_report_path.c_str());
        std::printf(
            "venue_cameras records=%zu keyframes=%zu blocked=%zu "
            "report=%s\n",
            venue_camera_records, venue_camera_keyframes,
            venue_camera_blocked,
            venue_camera_report_path.c_str());
        std::printf(
            "venue_placements assets=%zu waypoints=%zu report=%s\n",
            venue_placement_assets, venue_placement_waypoints,
            venue_placement_report_path.c_str());
        std::printf(
            "acp_channel_inventory set1_nonconstant=%zu overlap=%zu "
            "facing_in_set1=%zu report=%s\n",
            acp_set1_nonconstant, acp_overlapping_channels,
            acp_facing_in_set1, acp_report_path.c_str());
        std::printf(
            "acp_value_differentials clips=%zu channels=%zu "
            "sample_bytes=%zu report=%s\n",
            acp_value_rows, acp_value_channels,
            acp_value_sample_bytes,
            acp_value_report_path.c_str());
        std::printf(
            "semantic_field_contracts classes=%zu rows=%zu "
            "source_rows=%zu synthesized_rows=%zu report=%s\n",
            semantic_field_instances.size(), semantic_field_rows,
            semantic_field_source_rows,
            semantic_field_synthesized_rows,
            field_report_path.c_str());
        std::printf(
            "mat_value_differentials roots=%zu stages=%zu "
            "root_report=%s stage_report=%s\n",
            mat_root_value_rows, mat_stage_value_rows,
            mat_root_value_report_path.c_str(),
            mat_stage_value_report_path.c_str());
        std::printf(
            "tex_value_differentials textures=%zu bitmaps=%zu "
            "bitmap_bytes=%zu report=%s\n",
            tex_value_rows, tex_bitmap_rows, tex_bitmap_bytes,
            tex_value_report_path.c_str());
        std::printf(
            "trans_anim_value_differentials animations=%zu "
            "filters=%zu keys=%zu report=%s\n",
            trans_anim_value_rows, trans_anim_filter_rows,
            trans_anim_key_rows,
            trans_anim_value_report_path.c_str());
        std::printf(
            "view_value_differentials views=%zu animation_refs=%zu "
            "trans_anim_refs=%zu nested_animation_refs=%zu "
            "drawable_refs=%zu "
            "mat_anim_expansions=%zu "
            "environment_scopes=%zu draw_only_groups=%zu "
            "filters=%zu report=%s\n",
            view_value_rows, view_animation_references,
            view_trans_anim_references,
            view_nested_animation_references,
            view_drawable_references, view_mat_anim_expansions,
            view_environment_scope_groups,
            view_draw_only_groups, view_filter_rows,
            view_value_report_path.c_str());
        std::printf(
            "animation_payload_differentials objects=%zu keys=%zu "
            "filters=%zu memberships=%zu report=%s\n",
            animation_payload_rows, animation_payload_keys,
            animation_payload_filters,
            animation_payload_memberships,
            animation_payload_report_path.c_str());
        std::printf(
            "mat_anim_value_differentials objects=%zu rows=%zu "
            "keys=%zu filters=%zu overrides=%zu static_consumed=%zu "
            "report=%s\n",
            mat_anim_source_objects, mat_anim_value_rows,
            mat_anim_key_rows, mat_anim_filter_rows,
            mat_anim_override_rows, mat_anim_static_rows,
            mat_anim_value_report_path.c_str());
        std::printf(
            "object_field_value_differentials objects=%zu rows=%zu "
            "report=%s\n",
            object_field_value_objects, object_field_value_rows,
            object_field_value_report_path.c_str());
        std::printf(
            "particle_value_differentials objects=%zu rows=%zu "
            "particles=%zu filters=%zu memberships=%zu bounce_transforms=%zu "
            "report=%s\n",
            particle_value_objects, particle_value_rows,
            particle_value_particles, particle_value_filters,
            particle_value_memberships,
            particle_value_bounce_transforms,
            particle_value_report_path.c_str());
        std::printf(
            "font_value_differentials objects=%zu rows=%zu glyphs=%zu "
            "kerning_rows=%zu nbsp_normalizations=%zu report=%s\n",
            font_value_objects, font_value_rows, font_value_glyphs,
            font_value_kerning_rows,
            font_value_nbsp_normalizations,
            font_value_report_path.c_str());
        std::printf(
            "transform_value_differentials transforms=%zu "
            "child_links=%zu unresolved_child_links=%zu report=%s\n",
            transform_value_rows, transform_child_links,
            unresolved_transform_child_links,
            transform_value_report_path.c_str());
        std::printf(
            "mesh_value_differentials meshes=%zu report=%s\n",
            mesh_value_rows, mesh_value_report_path.c_str());
        std::printf(
            "discarded_field_values rows=%zu instances=%zu nondefault=%zu "
            "report=%s instance_report=%s\n",
            discarded_value_rows, discarded_value_instances.size(),
            discarded_nondefault_rows,
            discarded_value_report_path.c_str(),
            discarded_value_instance_report_path.c_str());
        std::printf(
            "dtb_format assets=%zu nodes=%zu control_words=%zu "
            "trailing_bytes=%zu exact_round_trips=%zu report=%s\n",
            dtb_assets, dtb_nodes, dtb_control_words.size(),
            dtb_trailing_bytes, dtb_assets,
            dtb_report_path.c_str());
        for (const auto& blocker : blockers)
            std::printf(
                "blocked %-16s %zu\n", blocker.first.c_str(),
                blocker.second);
        std::printf("report: %s\n", report_path.c_str());
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_convert_audit: %s\n", ex.what());
        return 2;
    }
}
