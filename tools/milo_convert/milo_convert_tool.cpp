#include "milo_convert.h"

#include "gh2_face_config_patch.h"
#include "gh1_character_package.h"
#include "gh1_venue_placement_conversion.h"
#include "gh1_venue_camera_conversion.h"
#include "acp.h"
#include "milo.h"
#include "milo_object.h"
#include "singer_face_track.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot create " + path.string());
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

void write_text(const fs::path& path, const std::string& text) {
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot create " + path.string());
    output << text;
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

bool parse_bool_arg(const std::string& value, const char* name) {
    if (value == "1" || value == "true" || value == "TRUE" ||
        value == "on" || value == "ON")
        return true;
    if (value == "0" || value == "false" || value == "FALSE" ||
        value == "off" || value == "OFF")
        return false;
    throw std::runtime_error(
        std::string("invalid ") + name + " value: " + value);
}

bool channel_ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

size_t gh2_channel_field_size(const std::string& channel, uint32_t compression) {
    if (channel_ends_with(channel, ".pos") ||
        channel_ends_with(channel, ".scale")) {
        return 12;
    }
    if (channel_ends_with(channel, ".quat")) {
        return compression == 0 ? 16 : 8;
    }
    if (channel_ends_with(channel, ".rotx") ||
        channel_ends_with(channel, ".roty") ||
        channel_ends_with(channel, ".rotz") ||
        channel_ends_with(channel, ".drotx") ||
        channel_ends_with(channel, ".droty") ||
        channel_ends_with(channel, ".drotz")) {
        return compression == 0 ? 4 : 2;
    }
    throw std::runtime_error("unknown GH2 CharBonesSamples channel " + channel);
}

size_t gh2_channel_category(const std::string& channel) {
    if (channel_ends_with(channel, ".pos")) return 0;
    if (channel_ends_with(channel, ".scale")) return 1;
    if (channel_ends_with(channel, ".quat")) return 2;
    if (channel_ends_with(channel, ".rotx")) return 3;
    if (channel_ends_with(channel, ".roty")) return 4;
    if (channel_ends_with(channel, ".rotz")) return 5;
    if (channel_ends_with(channel, ".drotx")) return 6;
    if (channel_ends_with(channel, ".droty")) return 7;
    if (channel_ends_with(channel, ".drotz")) return 8;
    throw std::runtime_error("unknown GH2 CharBonesSamples channel " + channel);
}

void refresh_char_bones_counts(
    gh::milo_object::CharBonesSamples10& samples) {
    std::array<uint32_t, 9> category_counts{};
    size_t previous = 0;
    bool have_previous = false;
    for (const auto& channel : samples.channels) {
        const size_t current = gh2_channel_category(channel);
        if (have_previous && current < previous)
            throw std::runtime_error(
                "GH2 CharBonesSamples channels are not in type order");
        previous = current;
        have_previous = true;
        ++category_counts[current];
    }
    samples.counts[0] = 0;
    uint32_t cumulative = 0;
    for (size_t index = 0; index < category_counts.size(); ++index) {
        cumulative += category_counts[index];
        samples.counts[index + 1] = cumulative;
    }
}

size_t strip_char_bones_channels(
    gh::milo_object::CharBonesSamples10& samples,
    const std::set<std::string>& channel_names) {
    if (samples.channels.empty() || channel_names.empty()) return 0;

    std::vector<size_t> field_sizes;
    field_sizes.reserve(samples.channels.size());
    size_t source_stride = 0;
    for (const auto& channel : samples.channels) {
        const size_t field_size =
            gh2_channel_field_size(channel, samples.compression);
        field_sizes.push_back(field_size);
        source_stride += field_size;
    }
    const size_t expected_bytes =
        source_stride * static_cast<size_t>(samples.sample_count);
    if (expected_bytes != samples.sample_bytes.size())
        throw std::runtime_error(
            "GH2 CharBonesSamples byte count differs before strip");

    std::vector<std::string> kept_channels;
    std::vector<size_t> kept_offsets;
    std::vector<size_t> kept_sizes;
    kept_channels.reserve(samples.channels.size());
    size_t offset = 0;
    size_t stripped = 0;
    for (size_t index = 0; index < samples.channels.size(); ++index) {
        const auto& channel = samples.channels[index];
        if (channel_names.count(channel) == 0) {
            kept_channels.push_back(channel);
            kept_offsets.push_back(offset);
            kept_sizes.push_back(field_sizes[index]);
        } else {
            ++stripped;
        }
        offset += field_sizes[index];
    }
    if (stripped == 0) return 0;

    size_t target_stride = 0;
    for (const size_t size : kept_sizes) target_stride += size;
    std::vector<uint8_t> kept_bytes;
    kept_bytes.resize(target_stride * static_cast<size_t>(samples.sample_count));
    for (uint32_t sample = 0; sample < samples.sample_count; ++sample) {
        const size_t source_base = static_cast<size_t>(sample) * source_stride;
        const size_t target_base = static_cast<size_t>(sample) * target_stride;
        size_t target_offset = 0;
        for (size_t index = 0; index < kept_offsets.size(); ++index) {
            const size_t count = kept_sizes[index];
            std::copy_n(
                samples.sample_bytes.begin() + source_base + kept_offsets[index],
                count,
                kept_bytes.begin() + target_base + target_offset);
            target_offset += count;
        }
    }

    samples.channels = std::move(kept_channels);
    samples.sample_bytes = std::move(kept_bytes);
    refresh_char_bones_counts(samples);
    return stripped;
}

struct PalmContactPatchStats {
    size_t sample_sets = 0;
    size_t samples = 0;
    float max_contact_error = 0.0f;
};

std::array<std::array<float, 3>, 3> hmx_quaternion_rotation(
    const std::array<float, 4>& value) {
    float x = value[0];
    float y = value[1];
    float z = value[2];
    float w = value[3];
    const float length_squared = x * x + y * y + z * z + w * w;
    if (length_squared > 1.0e-8f) {
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        x *= inverse_length;
        y *= inverse_length;
        z *= inverse_length;
        w *= inverse_length;
    }
    return {{
        {{1.0f - 2.0f * (y * y + z * z),
          2.0f * (x * y + z * w),
          2.0f * (x * z - y * w)}},
        {{2.0f * (x * y - z * w),
          1.0f - 2.0f * (x * x + z * z),
          2.0f * (y * z + x * w)}},
        {{2.0f * (x * z + y * w),
          2.0f * (y * z - x * w),
          1.0f - 2.0f * (x * x + y * y)}},
    }};
}

PalmContactPatchStats patch_palm_contact_samples(
    gh::milo_object::CharBonesSamples10& samples,
    const std::string& target,
    const std::array<float, 3>& palm_local) {
    PalmContactPatchStats stats;
    if (samples.channels.empty()) return stats;

    const std::string position_channel = target + ".pos";
    const std::string rotation_channel = target + ".quat";
    const auto position_it = std::find(
        samples.channels.begin(), samples.channels.end(), position_channel);
    const auto rotation_it = std::find(
        samples.channels.begin(), samples.channels.end(), rotation_channel);
    if (position_it == samples.channels.end() &&
        rotation_it == samples.channels.end()) {
        return stats;
    }
    if (position_it == samples.channels.end() ||
        rotation_it == samples.channels.end()) {
        throw std::runtime_error(
            "palm-contact target requires paired .pos and .quat channels: " +
            target);
    }

    const size_t position_index = static_cast<size_t>(
        std::distance(samples.channels.begin(), position_it));
    const size_t rotation_index = static_cast<size_t>(
        std::distance(samples.channels.begin(), rotation_it));
    size_t frame_size = 0;
    size_t position_offset = 0;
    for (size_t index = 0; index < samples.channels.size(); ++index) {
        if (index == position_index) position_offset = frame_size;
        frame_size +=
            gh2_channel_field_size(samples.channels[index], samples.compression);
    }
    const size_t expected_size =
        frame_size * static_cast<size_t>(samples.sample_count);
    if (expected_size != samples.sample_bytes.size()) {
        throw std::runtime_error(
            "GH2 CharBonesSamples byte count differs before palm-contact patch");
    }

    gh::acp::ChannelSet decoded_set;
    decoded_set.channels = samples.channels;
    decoded_set.sample_count = samples.sample_count;
    decoded_set.compression = samples.compression;
    decoded_set.frame_size = frame_size;
    decoded_set.sample_bytes = samples.sample_bytes;

    for (uint32_t sample = 0; sample < samples.sample_count; ++sample) {
        const auto position = gh::acp::decode_channel_sample(
            decoded_set, position_index, sample);
        const auto quaternion = gh::acp::decode_channel_sample(
            decoded_set, rotation_index, sample);
        if (position.component_count != 3 || quaternion.component_count != 4) {
            throw std::runtime_error(
                "palm-contact target channel component count differs");
        }
        const auto rotation = hmx_quaternion_rotation(quaternion.values);
        std::array<float, 3> rotated_palm{};
        std::array<float, 3> corrected{};
        for (size_t column = 0; column < 3; ++column) {
            for (size_t row = 0; row < 3; ++row) {
                rotated_palm[column] +=
                    palm_local[row] * rotation[row][column];
            }
            corrected[column] =
                position.values[column] - rotated_palm[column];
            if (!std::isfinite(corrected[column])) {
                throw std::runtime_error(
                    "palm-contact patch produced a non-finite position");
            }
            const float replayed_contact =
                corrected[column] + rotated_palm[column];
            stats.max_contact_error = std::max(
                stats.max_contact_error,
                std::fabs(replayed_contact - position.values[column]));
        }
        const size_t base =
            static_cast<size_t>(sample) * frame_size + position_offset;
        for (size_t axis = 0; axis < 3; ++axis) {
            std::memcpy(
                samples.sample_bytes.data() + base + axis * sizeof(float),
                &corrected[axis], sizeof(float));
        }
        ++stats.samples;
    }
    stats.sample_sets = 1;
    return stats;
}

void add_patch_stats(
    PalmContactPatchStats& target,
    const PalmContactPatchStats& source) {
    target.sample_sets += source.sample_sets;
    target.samples += source.samples;
    target.max_contact_error =
        std::max(target.max_contact_error, source.max_contact_error);
}

uint32_t generated_block_uncompressed_limit(
    size_t payload_size) {
    uint32_t block_uncompressed_limit = 0x20000;
    constexpr size_t kFixedBlockTableSlots = 128;
    if (payload_size >
        static_cast<size_t>(block_uncompressed_limit) *
            kFixedBlockTableSlots) {
        const size_t minimum =
            (payload_size + kFixedBlockTableSlots - 1) /
            kFixedBlockTableSlots;
        block_uncompressed_limit =
            static_cast<uint32_t>(
                (minimum + 0xFFFu) & ~size_t{0xFFFu});
    }
    return block_uncompressed_limit;
}

std::vector<uint8_t> serialize_generated_milo(
    const std::vector<uint8_t>& payload) {
    return gh::milo::serialize_container(
        gh::milo::make_object_aligned_container(
            payload,
            gh::milo::BlockStructure::MILO_B,
            generated_block_uncompressed_limit(payload.size())));
}

std::string channel_base_name(const std::string& channel) {
    const size_t dot = channel.rfind('.');
    return dot == std::string::npos
               ? channel
               : channel.substr(0, dot);
}

std::string channel_suffix(const std::string& channel) {
    const size_t dot = channel.rfind('.');
    return dot == std::string::npos
               ? std::string()
               : channel.substr(dot);
}

gh::milo::Entry make_entry(
    std::string type, std::string name,
    std::vector<uint8_t> body) {
    gh::milo::Entry entry;
    entry.type = std::move(type);
    entry.name = std::move(name);
    entry.body_bytes = std::move(body);
    entry.size = entry.body_bytes.size();
    entry.terminator_value = 0xDEADDEADu;
    return entry;
}

void set_identity(std::array<float, 12>& transform) {
    transform =
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
}

std::string character_model_relative_ref(const std::string& path) {
    constexpr const char* kCharPrefix = "char/";
    if (path.rfind(kCharPrefix, 0) == 0) {
        return "../../../" + path.substr(std::strlen(kCharPrefix));
    }
    return path;
}

uint8_t ps2_interleave_8bpp_index(uint8_t index) {
    return static_cast<uint8_t>(
        (index & ~uint8_t{0x18}) |
        ((index & uint8_t{0x08}) << 1) |
        ((index & uint8_t{0x10}) >> 1));
}

size_t quantize_character_textures_8bpp(
    gh::milo::Directory& directory, uint16_t max_dimension) {
    size_t converted = 0;
    for (auto& entry : directory.entries) {
        if (entry.type != "Tex") continue;
        auto texture = gh::milo_object::parse_tex10(entry.body_bytes);
        auto& bitmap = texture.bitmap;
        if (!texture.has_bitmap || bitmap.bits_per_pixel != 32 ||
            bitmap.encoding != 3)
            continue;
        const size_t source_width = bitmap.width;
        const size_t source_height = bitmap.height;
        const size_t source_bytes = source_width * source_height * 4;
        if (source_width == 0 || source_height == 0 ||
            bitmap.data.size() != source_bytes)
            throw std::runtime_error(
                "rebase-character-slot expected a packed RGBA32 texture: " +
                entry.name);

        const double scale = std::min(
            1.0,
            std::min(
                static_cast<double>(max_dimension) / source_width,
                static_cast<double>(max_dimension) / source_height));
        const uint16_t target_width = static_cast<uint16_t>(
            std::max<size_t>(1, static_cast<size_t>(source_width * scale)));
        const uint16_t target_height = static_cast<uint16_t>(
            std::max<size_t>(1, static_cast<size_t>(source_height * scale)));

        std::vector<uint8_t> indexed(256 * 4, 0);
        for (uint16_t red_bin = 0; red_bin < 6; ++red_bin) {
            for (uint16_t green_bin = 0; green_bin < 7; ++green_bin) {
                for (uint16_t blue_bin = 0; blue_bin < 6; ++blue_bin) {
                    const size_t palette_index =
                        1 + (red_bin * 7 + green_bin) * 6 + blue_bin;
                    indexed[palette_index * 4 + 0] = static_cast<uint8_t>(
                        (red_bin * 255 + 2) / 5);
                    indexed[palette_index * 4 + 1] = static_cast<uint8_t>(
                        (green_bin * 255 + 3) / 6);
                    indexed[palette_index * 4 + 2] = static_cast<uint8_t>(
                        (blue_bin * 255 + 2) / 5);
                    indexed[palette_index * 4 + 3] = 128;
                }
            }
        }
        indexed.reserve(indexed.size() +
                        static_cast<size_t>(target_width) * target_height);
        for (uint16_t y = 0; y < target_height; ++y) {
            const size_t source_y =
                static_cast<size_t>(y) * source_height / target_height;
            for (uint16_t x = 0; x < target_width; ++x) {
                const size_t source_x =
                    static_cast<size_t>(x) * source_width / target_width;
                const size_t source_offset =
                    (source_y * source_width + source_x) * 4;
                uint8_t palette_index = 0;
                if (bitmap.data[source_offset + 3] != 0) {
                    const uint16_t red_bin =
                        static_cast<uint16_t>(bitmap.data[source_offset]) * 6 /
                        256;
                    const uint16_t green_bin =
                        static_cast<uint16_t>(bitmap.data[source_offset + 1]) *
                        7 / 256;
                    const uint16_t blue_bin =
                        static_cast<uint16_t>(bitmap.data[source_offset + 2]) *
                        6 / 256;
                    palette_index = static_cast<uint8_t>(
                        1 + (red_bin * 7 + green_bin) * 6 + blue_bin);
                }
                indexed.push_back(
                    ps2_interleave_8bpp_index(palette_index));
            }
        }

        texture.width = target_width;
        texture.height = target_height;
        texture.bits_per_pixel = 8;
        bitmap.header_kind = 1;
        bitmap.bits_per_pixel = 8;
        bitmap.encoding = 3;
        bitmap.mipmap_count = 0;
        bitmap.width = target_width;
        bitmap.height = target_height;
        bitmap.bytes_per_line = target_width;
        bitmap.wii_alpha = 0;
        bitmap.data = std::move(indexed);
        entry.body_bytes = gh::milo_object::serialize_tex10(texture);
        entry.size = entry.body_bytes.size();
        ++converted;
    }
    return converted;
}

void append_guitarist_runtime_graph(
    gh::milo::Directory& directory,
    const std::string& main_anim,
    const std::string& strum_anim,
    const std::string& fret_anim) {
    gh::milo_object::CharServoBone2 servo;
    directory.entries.push_back(make_entry(
        "CharServoBone", "bone.servo",
        gh::milo_object::serialize_char_servo_bone2(servo)));

    gh::milo_object::CharDriver3 main_driver;
    main_driver.weightable.weight = 1.0f;
    main_driver.weightable.weight_owner = "main.drv";
    main_driver.bones = "bone.servo";
    main_driver.clips = character_model_relative_ref(main_anim);
    directory.entries.push_back(make_entry(
        "CharDriver", "main.drv",
        gh::milo_object::serialize_char_driver3(main_driver)));

    gh::milo_object::CharDriverMidi3 left_driver;
    left_driver.driver.weightable.weight = 1.0f;
    left_driver.driver.weightable.weight_owner = "left.weight";
    left_driver.driver.bones = "bone.servo";
    left_driver.driver.clips = character_model_relative_ref(fret_anim);
    directory.entries.push_back(make_entry(
        "CharDriverMidi", "left_hand.drv",
        gh::milo_object::serialize_char_driver_midi3(left_driver)));

    gh::milo_object::CharDriverMidi3 right_driver;
    right_driver.driver.weightable.weight = 1.0f;
    right_driver.driver.weightable.weight_owner = "right.weight";
    right_driver.driver.bones = "bone.servo";
    right_driver.driver.clips = character_model_relative_ref(strum_anim);
    directory.entries.push_back(make_entry(
        "CharDriverMidi", "right_hand.drv",
        gh::milo_object::serialize_char_driver_midi3(right_driver)));

    gh::milo_object::CharIKMidi4 fret_midi;
    fret_midi.bone = "bone_fret.mesh";
    directory.entries.push_back(make_entry(
        "CharIKMidi", "fret.ik",
        gh::milo_object::serialize_char_ik_midi4(fret_midi)));

    gh::milo_object::CharIKHand2 left_hand;
    left_hand.weightable.weight = 0.0f;
    left_hand.weightable.weight_owner = "left.weight";
    left_hand.hand = "bone_L-hand.mesh";
    left_hand.target = "bone_fret_hand.mesh";
    left_hand.orientation = true;
    left_hand.stretch = true;
    directory.entries.push_back(make_entry(
        "CharIKHand", "left_hand.ik",
        gh::milo_object::serialize_char_ik_hand2(left_hand)));

    gh::milo_object::CharIKHand2 right_hand;
    right_hand.weightable.weight = 0.0f;
    right_hand.weightable.weight_owner = "right.weight";
    right_hand.hand = "bone_R-hand.mesh";
    right_hand.target = "bone_strum_hand.mesh";
    right_hand.orientation = true;
    right_hand.stretch = true;
    directory.entries.push_back(make_entry(
        "CharIKHand", "right_hand.ik",
        gh::milo_object::serialize_char_ik_hand2(right_hand)));

    gh::milo_object::OutfitLoader1 outfit;
    outfit.object_fields.type = "guitar";
    outfit.directory = "../../../og";
    gh::milo_object::OutfitLoaderCategory1 category;
    category.outfits.resize(58);
    outfit.categories.push_back(std::move(category));
    directory.entries.push_back(make_entry(
        "OutfitLoader", "guitar.outfit",
        gh::milo_object::serialize_outfit_loader1(outfit)));

    const auto append_weight = [&](const char* name, uint32_t flags) {
        gh::milo_object::CharWeightSetter2 weight;
        weight.weightable.weight = 1.0f;
        weight.weightable.weight_owner = name;
        weight.driver = "main.drv";
        weight.flags = flags;
        directory.entries.push_back(make_entry(
            "CharWeightSetter", name,
            gh::milo_object::serialize_char_weight_setter2(weight)));
    };
    append_weight("left.weight", 0x00400000u);
    append_weight("right.weight", 0x00800000u);

    gh::milo_object::CharWalk1 walk;
    walk.object_fields.type = "guitarist";
    directory.entries.push_back(make_entry(
        "CharWalk", "walk",
        gh::milo_object::serialize_char_walk1(walk)));
}

std::array<float, 12> invert_affine_transform(
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
            "meshbundle: cannot invert singular bone bind world");
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

std::array<float, 12> multiply_affine_transform(
    const std::array<float, 12>& left,
    const std::array<float, 12>& right) {
    std::array<float, 12> result{};
    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            result[r * 3 + c] =
                left[r * 3] * right[c] +
                left[r * 3 + 1] * right[3 + c] +
                left[r * 3 + 2] * right[6 + c];
        }
    }
    for (size_t c = 0; c < 3; ++c) {
        result[9 + c] =
            left[9] * right[c] +
            left[10] * right[3 + c] +
            left[11] * right[6 + c] +
            right[9 + c];
    }
    return result;
}

bool load_trans_by_base_name(
    const gh::milo::Directory& directory,
    const std::string& name,
    gh::milo_object::Trans9& trans) {
    const std::string base = channel_base_name(name);
    for (const auto& entry : directory.entries) {
        if (entry.type != "Trans") continue;
        if (channel_base_name(entry.name) != base) continue;
        trans = gh::milo_object::parse_trans9(entry.body_bytes);
        return true;
    }
    return false;
}

void remove_trans_by_base_names(
    gh::milo::Directory& directory,
    const std::set<std::string>& bases) {
    directory.entries.erase(
        std::remove_if(
            directory.entries.begin(), directory.entries.end(),
            [&](const gh::milo::Entry& entry) {
                return entry.type == "Trans" &&
                       bases.find(channel_base_name(entry.name)) !=
                           bases.end();
            }),
        directory.entries.end());
}

gh::milo_object::Trans9 make_child_trans(
    const std::string& parent,
    const std::array<float, 12>& local,
    const std::array<float, 12>& parent_world) {
    gh::milo_object::Trans9 trans;
    trans.local = local;
    trans.world = multiply_affine_transform(local, parent_world);
    trans.parent = parent;
    return trans;
}

void append_trans(
    gh::milo::Directory& directory,
    const std::string& name,
    const gh::milo_object::Trans9& trans) {
    directory.entries.push_back(make_entry(
        "Trans", name, gh::milo_object::serialize_trans9(trans)));
}

size_t patch_guitarist_proxy_transforms(
    gh::milo::Directory& directory) {
    gh::milo_object::Trans9 guitar;
    if (!load_trans_by_base_name(directory, "bone_pos_guitar.mesh", guitar))
        throw std::runtime_error(
            "patch-guitarist-proxies: missing bone_pos_guitar.mesh");

    remove_trans_by_base_names(
        directory,
        {"bone_fret", "bone_strum", "bone_fret_hand", "bone_strum_hand"});

    const std::array<float, 12> fret_local = {
        0.999139f, -0.001143f, 0.0414729f,
        0.00131021f, 0.999991f, -0.00400718f,
        -0.041468f, 0.00405813f, 0.999132f,
        4.32514f, 0.30534f, 26.9909f};
    const std::array<float, 12> strum_local = {
        0.0821322f, 0.996588f, 0.00808181f,
        -0.996429f, 0.082273f, -0.0189768f,
        -0.019577f, -0.00649437f, 0.999787f,
        4.62285f, 0.270739f, 4.74301f};
    const std::array<float, 12> fret_hand_local = {
        0.910481f, 0.385046f, 0.15088f,
        0.385335f, -0.922336f, 0.0285125f,
        0.150141f, 0.0321795f, -0.988141f,
        -6.27464f, -0.453556f, -4.32057f};
    const std::array<float, 12> strum_hand_local = {
        0.683027f, -0.419636f, 0.597812f,
        0.289627f, 0.90699f, 0.305753f,
        -0.670515f, -0.0356949f, 0.741037f,
        -6.73835f, -1.31678f, -3.08712f};

    const auto fret = make_child_trans(
        "bone_pos_guitar.mesh", fret_local, guitar.world);
    const auto strum = make_child_trans(
        "bone_pos_guitar.mesh", strum_local, guitar.world);

    gh::milo_object::Trans9 fret_hand;
    fret_hand.parent = "bone_fret.mesh";
    fret_hand.local = fret_hand_local;
    fret_hand.world = multiply_affine_transform(fret_hand.local, fret.world);

    gh::milo_object::Trans9 strum_hand;
    strum_hand.parent = "bone_strum.mesh";
    strum_hand.local = strum_hand_local;
    strum_hand.world = multiply_affine_transform(strum_hand.local, strum.world);

    append_trans(directory, "bone_fret.mesh", fret);
    append_trans(directory, "bone_strum.mesh", strum);
    append_trans(directory, "bone_fret_hand.mesh", fret_hand);
    append_trans(directory, "bone_strum_hand.mesh", strum_hand);
    return 4;
}

struct BinaryCursor {
    explicit BinaryCursor(std::vector<uint8_t> bytes)
        : bytes(std::move(bytes)) {}

    std::vector<uint8_t> bytes;
    size_t offset = 0;

    void require(size_t count, const char* field) {
        if (count > bytes.size() || offset > bytes.size() - count)
            throw std::runtime_error(
                std::string("meshbundle: truncated ") + field);
    }

    uint16_t u16(const char* field) {
        require(2, field);
        const uint16_t value =
            static_cast<uint16_t>(bytes[offset]) |
            (static_cast<uint16_t>(bytes[offset + 1]) << 8);
        offset += 2;
        return value;
    }

    uint8_t u8(const char* field) {
        require(1, field);
        return bytes[offset++];
    }

    uint32_t u32(const char* field) {
        require(4, field);
        const uint32_t value =
            static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        offset += 4;
        return value;
    }

    float f32(const char* field) {
        const uint32_t raw = u32(field);
        float value = 0.0f;
        static_assert(sizeof(raw) == sizeof(value));
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    std::string string(const char* field) {
        const uint32_t size = u32(field);
        require(size, field);
        std::string value(
            reinterpret_cast<const char*>(bytes.data() + offset),
            reinterpret_cast<const char*>(bytes.data() + offset + size));
        offset += size;
        return value;
    }

    std::vector<uint8_t> byte_vector(const char* field) {
        const uint32_t size = u32(field);
        require(size, field);
        std::vector<uint8_t> value(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
        return value;
    }
};

struct MeshBundleChunk {
    gh::milo_object::Mesh28 mesh;
    std::string name;
    std::string material;
    std::string texture;
    bool alpha_cut = false;
    bool alpha_write = false;
    int32_t z_mode = 1;
    bool cull = true;
    int32_t blend = 1;
};

struct MeshBundle {
    struct BoneTransform {
        std::string source_name;
        std::string parent_name;
        std::array<float, 12> local{};
        std::array<float, 12> world{};
    };

    struct Texture {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bits_per_pixel = 0;
        gh::milo_object::HmxBitmap bitmap;
    };

    std::string outfit;
    std::string package_name;
    std::map<std::string, BoneTransform> bone_transforms;
    std::map<std::string, Texture> textures;
    std::vector<MeshBundleChunk> chunks;
};

void build_ps2_mesh_groups(gh::milo_object::Mesh28& mesh) {
    // Retail GH2 PS2 character groups stay at or below 155 strip indices.
    // Independent three-index strips are deterministic and preserve each
    // source triangle's winding exactly.
    constexpr size_t kFacesPerGroup = 48;
    mesh.group_sizes.clear();
    mesh.group_sections.clear();
    for (size_t face_begin = 0; face_begin < mesh.faces.size();
         face_begin += kFacesPerGroup) {
        const size_t face_count = std::min(
            kFacesPerGroup, mesh.faces.size() - face_begin);
        mesh.group_sizes.push_back(static_cast<uint8_t>(face_count));
        gh::milo_object::MeshStripResult section;
        section.cumulative_strip_lengths.reserve(face_count);
        section.strip_runs.reserve(face_count * 3);
        for (size_t face_index = 0; face_index < face_count; ++face_index) {
            const auto& face = mesh.faces[face_begin + face_index];
            section.strip_runs.insert(
                section.strip_runs.end(), face.begin(), face.end());
            section.cumulative_strip_lengths.push_back(
                static_cast<uint32_t>(section.strip_runs.size()));
        }
        mesh.group_sections.push_back(std::move(section));
    }
}

bool mesh_needs_ps2_groups(const gh::milo_object::Mesh28& mesh) {
    if (mesh.faces.empty())
        return !mesh.group_sizes.empty() || !mesh.group_sections.empty();
    if (mesh.group_sizes.empty() ||
        mesh.group_sections.size() != mesh.group_sizes.size())
        return true;
    size_t grouped_faces = 0;
    for (size_t index = 0; index < mesh.group_sizes.size(); ++index) {
        const auto& section = mesh.group_sections[index];
        grouped_faces += mesh.group_sizes[index];
        if (mesh.group_sizes[index] == 0 ||
            section.cumulative_strip_lengths.empty() ||
            section.strip_runs.empty() ||
            section.cumulative_strip_lengths.back() !=
                section.strip_runs.size())
            return true;
    }
    return grouped_faces != mesh.faces.size();
}

class CharacterSnapshotWriter {
public:
    void u8(uint8_t value) { bytes.push_back(value); }

    void u32(uint32_t value) {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
        bytes.push_back(static_cast<uint8_t>(value >> 16));
        bytes.push_back(static_cast<uint8_t>(value >> 24));
    }

    void f32(float value) {
        uint32_t raw = 0;
        static_assert(sizeof(raw) == sizeof(value));
        std::memcpy(&raw, &value, sizeof(raw));
        u32(raw);
    }

    void string(const std::string& value) {
        if (value.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("character snapshot: string is too large");
        u32(static_cast<uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> build_character_snapshot(const fs::path& path) {
    constexpr std::array<uint8_t, 8> kMagic =
        {'G', 'H', '2', 'M', '2', 'G', 'L', 'B'};
    const auto container = gh::milo::parse_container(
        gh::milo::read_file(path.string()));
    const auto directory = gh::milo::parse_directory(
        gh::milo::container_payload(container));
    if (directory.dir_type != "Character" &&
        directory.dir_type != "BandCharacter") {
        throw std::runtime_error(
            "export-character-snapshot: directory is not a character");
    }

    std::vector<const gh::milo::Entry*> transforms;
    std::vector<const gh::milo::Entry*> meshes;
    for (const auto& entry : directory.entries) {
        if (entry.type == "Trans") transforms.push_back(&entry);
        else if (entry.type == "Mesh") meshes.push_back(&entry);
    }

    CharacterSnapshotWriter writer;
    writer.bytes.insert(writer.bytes.end(), kMagic.begin(), kMagic.end());
    writer.u32(1);
    writer.string(directory.dir_type);
    writer.string(directory.dir_name);
    writer.u32(static_cast<uint32_t>(transforms.size()));
    for (const auto* entry : transforms) {
        const auto transform =
            gh::milo_object::parse_trans9(entry->body_bytes);
        writer.string(entry->name);
        writer.string(transform.parent);
        for (float value : transform.local) writer.f32(value);
        for (float value : transform.world) writer.f32(value);
    }

    writer.u32(static_cast<uint32_t>(meshes.size()));
    for (const auto* entry : meshes) {
        const auto mesh = gh::milo_object::parse_mesh28(
            entry->body_bytes,
            static_cast<uint32_t>(directory.dir_version));
        writer.string(entry->name);
        writer.string(mesh.material);
        writer.string(mesh.transformable.parent);
        for (float value : mesh.transformable.local) writer.f32(value);
        for (float value : mesh.transformable.world) writer.f32(value);
        writer.u8(mesh.has_bones ? 1 : 0);
        for (const auto& slot : mesh.bone_slots) {
            writer.string(slot.bone);
            for (float value : slot.offset) writer.f32(value);
        }
        writer.u32(static_cast<uint32_t>(mesh.vertices.size()));
        for (const auto& vertex : mesh.vertices) {
            for (float value : vertex.position) writer.f32(value);
            for (float value : vertex.normal) writer.f32(value);
            for (float value : vertex.color_or_weights) writer.f32(value);
            for (float value : vertex.uv) writer.f32(value);
        }
        writer.u32(static_cast<uint32_t>(mesh.faces.size()));
        for (const auto& face : mesh.faces) {
            for (uint16_t index : face) writer.u32(index);
        }
    }
    return writer.bytes;
}

MeshBundle parse_meshbundle(const fs::path& path) {
    constexpr std::array<uint8_t, 8> kMagic =
        {'G', 'H', '3', 'M', '2', 'M', 'B', 0};
    BinaryCursor cursor(gh::milo::read_file(path.string()));
    cursor.require(kMagic.size(), "magic");
    for (uint8_t expected : kMagic) {
        if (cursor.bytes[cursor.offset++] != expected)
            throw std::runtime_error("meshbundle: bad magic");
    }
    const uint32_t version = cursor.u32("version");
    if (version != 1 && version != 2 && version != 3 && version != 4 &&
        version != 5 && version != 6 && version != 7 && version != 8 &&
        version != 9)
        throw std::runtime_error("meshbundle: unsupported version");
    MeshBundle bundle;
    bundle.outfit = cursor.string("outfit");
    bundle.package_name = cursor.string("package");
    if (version >= 2) {
        const uint32_t bone_transform_count =
            cursor.u32("bone transform count");
        if (bone_transform_count > 10000)
            throw std::runtime_error(
                "meshbundle: implausible bone transform count");
        for (uint32_t i = 0; i < bone_transform_count; ++i) {
            const std::string name = cursor.string("bone transform name");
            MeshBundle::BoneTransform transform;
            transform.source_name = cursor.string("bone source name");
            if (version >= 4)
                transform.parent_name = cursor.string("bone parent name");
            for (float& value : transform.local)
                value = cursor.f32("bone local");
            for (float& value : transform.world)
                value = cursor.f32("bone world");
            if (!bundle.bone_transforms.emplace(name, transform).second)
                throw std::runtime_error(
                    "meshbundle: duplicate bone transform " + name);
        }
    }
    if (version >= 3) {
        const uint32_t texture_count = cursor.u32("texture count");
        if (texture_count > 10000)
            throw std::runtime_error(
                "meshbundle: implausible texture count");
        for (uint32_t i = 0; i < texture_count; ++i) {
            const std::string name = cursor.string("texture name");
            MeshBundle::Texture texture;
            texture.width = cursor.u32("texture width");
            texture.height = cursor.u32("texture height");
            texture.bits_per_pixel = cursor.u32("texture bits per pixel");
            texture.bitmap.header_kind = cursor.u8("bitmap header kind");
            texture.bitmap.bits_per_pixel =
                cursor.u8("bitmap bits per pixel");
            texture.bitmap.encoding =
                static_cast<int32_t>(cursor.u32("bitmap encoding"));
            texture.bitmap.mipmap_count = cursor.u8("bitmap mip count");
            texture.bitmap.width = cursor.u16("bitmap width");
            texture.bitmap.height = cursor.u16("bitmap height");
            texture.bitmap.bytes_per_line =
                cursor.u16("bitmap bytes per line");
            texture.bitmap.wii_alpha = cursor.u16("bitmap wii alpha");
            texture.bitmap.data = cursor.byte_vector("bitmap data");
            if (texture.width == 0 || texture.height == 0 ||
                texture.bitmap.width != texture.width ||
                texture.bitmap.height != texture.height)
                throw std::runtime_error(
                    "meshbundle: invalid texture dimensions " + name);
            if (!bundle.textures.emplace(name, std::move(texture)).second)
                throw std::runtime_error(
                    "meshbundle: duplicate texture " + name);
        }
    }
    const uint32_t chunk_count = cursor.u32("chunk count");
    if (chunk_count > 10000)
        throw std::runtime_error("meshbundle: implausible chunk count");
    bundle.chunks.reserve(chunk_count);
    for (uint32_t chunk_index = 0; chunk_index < chunk_count;
         ++chunk_index) {
        MeshBundleChunk chunk;
        chunk.name = cursor.string("mesh name");
        chunk.material = cursor.string("material");
        chunk.texture = cursor.string("texture");
        if (version >= 5) {
            chunk.alpha_cut = cursor.u8("material alpha cut") != 0;
            chunk.alpha_write = cursor.u8("material alpha write") != 0;
            chunk.z_mode =
                static_cast<int32_t>(cursor.u32("material z mode"));
            chunk.cull = cursor.u8("material cull") != 0;
            if (version >= 9)
                chunk.blend = static_cast<int32_t>(
                    cursor.u32("material blend"));
        }
        chunk.mesh.bsp_nodes.push_back({});
        set_identity(chunk.mesh.transformable.local);
        set_identity(chunk.mesh.transformable.world);
        chunk.mesh.material = chunk.material;
        chunk.mesh.drawable.sphere = {
            cursor.f32("sphere x"),
            cursor.f32("sphere y"),
            cursor.f32("sphere z"),
            cursor.f32("sphere r")};
        const uint32_t bone_count = cursor.u32("bone count");
        if (bone_count == 0 || bone_count > chunk.mesh.bone_slots.size())
            throw std::runtime_error(
                "meshbundle: invalid Mesh28 bone slot count");
        chunk.mesh.has_bones = true;
        for (uint32_t i = 0; i < bone_count; ++i) {
            chunk.mesh.bone_slots[i].bone = cursor.string("bone");
            const std::string bind_transform_name =
                version >= 8 ? cursor.string("bind transform")
                             : chunk.mesh.bone_slots[i].bone;
            const auto transform =
                bundle.bone_transforms.find(bind_transform_name);
            if (transform == bundle.bone_transforms.end())
                throw std::runtime_error(
                    "meshbundle: missing bind transform for bone " +
                    bind_transform_name);
            chunk.mesh.bone_slots[i].offset =
                invert_affine_transform(transform->second.world);
        }
        for (uint32_t i = bone_count; i < chunk.mesh.bone_slots.size(); ++i)
            set_identity(chunk.mesh.bone_slots[i].offset);
        const uint32_t vertex_count = cursor.u32("vertex count");
        if (vertex_count > 1000000)
            throw std::runtime_error(
                "meshbundle: implausible vertex count");
        chunk.mesh.vertices.reserve(vertex_count);
        for (uint32_t vertex_index = 0; vertex_index < vertex_count;
             ++vertex_index) {
            gh::milo_object::MeshVertex vertex;
            for (float& value : vertex.position)
                value = cursor.f32("position");
            for (float& value : vertex.normal)
                value = cursor.f32("normal");
            for (float& value : vertex.color_or_weights)
                value = cursor.f32("weights");
            for (float& value : vertex.uv)
                value = cursor.f32("uv");
            chunk.mesh.vertices.push_back(vertex);
        }
        const uint32_t face_count = cursor.u32("face count");
        if (face_count > 1000000)
            throw std::runtime_error(
                "meshbundle: implausible face count");
        chunk.mesh.faces.reserve(face_count);
        for (uint32_t face_index = 0; face_index < face_count;
             ++face_index) {
            chunk.mesh.faces.push_back({
                cursor.u16("face a"),
                cursor.u16("face b"),
                cursor.u16("face c")});
        }
        // GH2 PS2 character meshes self-own geometry and require cached strip
        // data whose group bounds fit the native renderer's packet envelope.
        chunk.mesh.geometry_owner = chunk.name;
        build_ps2_mesh_groups(chunk.mesh);
        bundle.chunks.push_back(std::move(chunk));
    }
    if (cursor.offset != cursor.bytes.size())
        throw std::runtime_error("meshbundle: trailing bytes");
    return bundle;
}

struct ChannelContext {
    bool position = false;
    bool scale = false;
    bool quat = false;
    bool rotx = false;
    bool roty = false;
    bool rotz = false;
};

bool ends_with_text(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string strip_mesh_channel_suffix(const std::string& base) {
    return ends_with_text(base, ".mesh")
        ? base.substr(0, base.size() - 5)
        : base;
}

std::string generated_char_bone_name(
    const std::string& base,
    const std::string& bone_extension) {
    if (ends_with_text(base, ".mesh"))
        return base;
    return base + bone_extension;
}

std::string generated_char_bone_parent_name(
    const std::string& parent_base,
    const std::string& child_base,
    const std::string& bone_extension) {
    if (parent_base.empty()) return {};
    if (ends_with_text(child_base, ".mesh"))
        return generated_char_bone_name(parent_base + ".mesh", bone_extension);
    return generated_char_bone_name(parent_base, bone_extension);
}

std::string generated_guitar_parent_base(
    gh::milo_convert::Gh2ClipSetRole role,
    const std::string& base,
    bool control_root_pelvis_parent) {
    const std::string lookup_base = strip_mesh_channel_suffix(base);
    if (control_root_pelvis_parent && lookup_base == "bone_pelvis")
        return "Control_Root";
    static const std::map<std::string, std::string> kCommonParents = {
        {"bone_pelvis", ""},
        {"bone_spine1", "bone_pelvis"},
        {"bone_spine2", "bone_spine1"},
        {"bone_spine3", "bone_spine2"},
        {"bone_neck", "bone_spine3"},
        {"bone_head", "bone_neck"},
        {"bone_L-clavicle", "bone_spine3"},
        {"bone_L-upperArm", "bone_L-clavicle"},
        {"bone_L-foreArm", "bone_L-upperArm"},
        {"bone_L-hand", "bone_L-foreArm"},
        {"bone_R-clavicle", "bone_spine3"},
        {"bone_R-upperArm", "bone_R-clavicle"},
        {"bone_R-foreArm", "bone_R-upperArm"},
        {"bone_R-hand", "bone_R-foreArm"},
        {"bone_L-thigh", "bone_pelvis"},
        {"bone_L-knee", "bone_L-thigh"},
        {"bone_L-ankle", "bone_L-knee"},
        {"bone_L-toe", "bone_L-ankle"},
        {"bone_R-thigh", "bone_pelvis"},
        {"bone_R-knee", "bone_R-thigh"},
        {"bone_R-ankle", "bone_R-knee"},
        {"bone_R-toe", "bone_R-ankle"},
        {"bone_pos_guitar", "bone_pelvis"},
        {"bone_fret", "bone_pos_guitar"},
        {"bone_strum", "bone_pos_guitar"},
        {"bone_fret_hand", "bone_fret"},
        {"bone_strum_hand", "bone_strum"},
        {"Bone_Jaw", "bone_head"},
        {"Bone_Eyelid_Upper_L", "bone_head"},
        {"Bone_Eyelid_Upper_R", "bone_head"},
        {"Bone_Lip_Lower_Mid", "bone_head"},
        {"Bone_Lip_Upper_Mid", "bone_head"},
        {"Bone_Mouth_L", "bone_head"},
        {"Bone_Mouth_R", "bone_head"},
    };
    const auto common = kCommonParents.find(lookup_base);
    if (common != kCommonParents.end()) return common->second;

    if (lookup_base.rfind("bone_L-", 0) == 0) {
        if (lookup_base.find("index01") != std::string::npos ||
            lookup_base.find("middlefinger01") != std::string::npos ||
            lookup_base.find("ringfinger01") != std::string::npos ||
            lookup_base.find("pinky01") != std::string::npos ||
            lookup_base.find("thumb01") != std::string::npos) {
            return role == gh::milo_convert::Gh2ClipSetRole::GuitarFret
                ? "bone_fret_hand"
                : "bone_L-hand";
        }
        if (lookup_base.find("index02") != std::string::npos) return "bone_L-index01";
        if (lookup_base.find("index03") != std::string::npos) return "bone_L-index02";
        if (lookup_base.find("middlefinger02") != std::string::npos) return "bone_L-middlefinger01";
        if (lookup_base.find("middlefinger03") != std::string::npos) return "bone_L-middlefinger02";
        if (lookup_base.find("ringfinger02") != std::string::npos) return "bone_L-ringfinger01";
        if (lookup_base.find("ringfinger03") != std::string::npos) return "bone_L-ringfinger02";
        if (lookup_base.find("pinky02") != std::string::npos) return "bone_L-pinky01";
        if (lookup_base.find("pinky03") != std::string::npos) return "bone_L-pinky02";
        if (lookup_base.find("thumb02") != std::string::npos) return "bone_L-thumb01";
        if (lookup_base.find("thumb03") != std::string::npos) return "bone_L-thumb02";
    }
    if (lookup_base.rfind("bone_R-", 0) == 0) {
        if (lookup_base.find("index01") != std::string::npos ||
            lookup_base.find("middlefinger01") != std::string::npos ||
            lookup_base.find("ringfinger01") != std::string::npos ||
            lookup_base.find("pinky01") != std::string::npos ||
            lookup_base.find("thumb01") != std::string::npos) {
            return role == gh::milo_convert::Gh2ClipSetRole::GuitarStrum
                ? "bone_strum_hand"
                : "bone_R-hand";
        }
        if (lookup_base.find("index02") != std::string::npos) return "bone_R-index01";
        if (lookup_base.find("index03") != std::string::npos) return "bone_R-index02";
        if (lookup_base.find("middlefinger02") != std::string::npos) return "bone_R-middlefinger01";
        if (lookup_base.find("middlefinger03") != std::string::npos) return "bone_R-middlefinger02";
        if (lookup_base.find("ringfinger02") != std::string::npos) return "bone_R-ringfinger01";
        if (lookup_base.find("ringfinger03") != std::string::npos) return "bone_R-ringfinger02";
        if (lookup_base.find("pinky02") != std::string::npos) return "bone_R-pinky01";
        if (lookup_base.find("pinky03") != std::string::npos) return "bone_R-pinky02";
        if (lookup_base.find("thumb02") != std::string::npos) return "bone_R-thumb01";
        if (lookup_base.find("thumb03") != std::string::npos) return "bone_R-thumb02";
    }
    return {};
}

bool generated_guitar_controller_local(
    const std::string& base,
    std::array<float, 12>& local) {
    const std::string lookup_base = strip_mesh_channel_suffix(base);
    if (lookup_base == "bone_fret") {
        local = {
            0.999139f, -0.001143f, 0.0414729f,
            0.00131021f, 0.999991f, -0.00400718f,
            -0.041468f, 0.00405813f, 0.999132f,
            4.32514f, 0.30534f, 26.9909f};
        return true;
    }
    if (lookup_base == "bone_strum") {
        local = {
            0.0821322f, 0.996588f, 0.00808181f,
            -0.996429f, 0.082273f, -0.0189768f,
            -0.019577f, -0.00649437f, 0.999787f,
            4.62285f, 0.270739f, 4.74301f};
        return true;
    }
    if (lookup_base == "bone_fret_hand") {
        local = {
            0.910481f, 0.385046f, 0.15088f,
            0.385335f, -0.922336f, 0.0285125f,
            0.150141f, 0.0321795f, -0.988141f,
            -6.27464f, -0.453556f, -4.32057f};
        return true;
    }
    if (lookup_base == "bone_strum_hand") {
        local = {
            0.683027f, -0.419636f, 0.597812f,
            0.289627f, 0.90699f, 0.305753f,
            -0.670515f, -0.0356949f, 0.741037f,
            -6.73835f, -1.31678f, -3.08712f};
        return true;
    }
    return false;
}

void add_generated_parent_contexts(
    std::map<std::string, ChannelContext>& contexts,
    gh::milo_convert::Gh2ClipSetRole role,
    const std::string& base,
    bool control_root_pelvis_parent) {
    const std::string parent = generated_guitar_parent_base(
        role, base, control_root_pelvis_parent);
    if (parent.empty()) return;
    const std::string context_parent =
        parent == "Control_Root"
            ? parent
            : ends_with_text(base, ".mesh") && !ends_with_text(parent, ".mesh")
            ? parent + ".mesh"
            : parent;
    if (!contexts.count(context_parent))
        contexts.emplace(context_parent, ChannelContext{});
    add_generated_parent_contexts(
        contexts, role, context_parent, control_root_pelvis_parent);
}

void add_channel_context(
    std::map<std::string, ChannelContext>& contexts,
    const std::string& channel) {
    const std::string base = channel_base_name(channel);
    const std::string type = channel_suffix(channel);
    // These channels are allocated by CharClipSet::move_self, not CharBone.
    if (base == "bone_facing" || base == "bone_facing_delta") return;
    ChannelContext& context = contexts[base];
    if (type == ".pos") context.position = true;
    else if (type == ".scale") context.scale = true;
    else if (type == ".quat") context.quat = true;
    else if (type == ".rotx" || type == ".drotx")
        context.rotx = true;
    else if (type == ".roty" || type == ".droty")
        context.roty = true;
    else if (type == ".rotz" || type == ".drotz")
        context.rotz = true;
    else
        throw std::runtime_error(
            "unsupported character channel " + channel);
}

int32_t rotation_context(const ChannelContext& context) {
    const int count =
        (context.quat ? 1 : 0) +
        (context.rotx ? 1 : 0) +
        (context.roty ? 1 : 0) +
        (context.rotz ? 1 : 0);
    if (count > 1)
        throw std::runtime_error(
            "multiple rotation encodings for one bone");
    if (context.quat) return 2;
    if (context.rotx) return 3;
    if (context.roty) return 4;
    if (context.rotz) return 5;
    return 9;
}

std::vector<gh::milo_object::ObjectDirViewport16>
standard_clip_set_viewports() {
    constexpr int32_t kLegacyViewportValue = 5583746;
    const std::array<std::array<float, 12>, 7> transforms = {{
        {0.70710677f, -0.70710677f, 0.0f,
         0.57735026f, 0.57735026f, -0.57735026f,
         0.40824828f, 0.40824828f, 0.81649655f,
         -443.405f, -443.405f, 443.405f},
        {0, -1, 0, 1, 0, 0, 0, 0, 1, -768, 0, 0},
        {0, 1, 0, -1, 0, 0, 0, 0, 1, 768, 0, 0},
        {1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 768},
        {1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 0, -768},
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -768, 0},
        {-1, 0, 0, 0, -1, 0, 0, 0, 1, 0, 768, 0},
    }};
    std::vector<gh::milo_object::ObjectDirViewport16> result;
    for (const auto& transform : transforms)
        result.push_back(
            {transform, kLegacyViewportValue});
    return result;
}

gh::milo_convert::Gh2ClipSetRole parse_clipset_role(
    const std::string& role) {
    using gh::milo_convert::Gh2ClipSetRole;
    if (role == "guitar-main") return Gh2ClipSetRole::GuitarMain;
    if (role == "guitar-ui") return Gh2ClipSetRole::GuitarUi;
    if (role == "guitar-fret") return Gh2ClipSetRole::GuitarFret;
    if (role == "guitar-strum") return Gh2ClipSetRole::GuitarStrum;
    if (role == "band") return Gh2ClipSetRole::Band;
    if (role == "crowd") return Gh2ClipSetRole::Crowd;
    if (role == "generic") return Gh2ClipSetRole::Generic;
    throw std::runtime_error("unknown clipset role: " + role);
}

std::pair<std::string, int32_t> clipset_legacy_type(
    gh::milo_convert::Gh2ClipSetRole role) {
    using gh::milo_convert::Gh2ClipSetRole;
    switch (role) {
        case Gh2ClipSetRole::GuitarMain:
            return {"guitarist", 15};
        case Gh2ClipSetRole::GuitarUi:
            return {"guitarist_ui", 2};
        case Gh2ClipSetRole::GuitarStrum:
            return {"guitarist_strum", 1};
        case Gh2ClipSetRole::Band:
            return {"band", 4};
        case Gh2ClipSetRole::Crowd:
            return {"crowd", 1};
        case Gh2ClipSetRole::GuitarFret:
        case Gh2ClipSetRole::Generic:
            return {"", -1};
    }
    throw std::runtime_error("unknown clipset role");
}

uint32_t root_play_flags_for_role(
    gh::milo_convert::Gh2ClipSetRole role,
    const std::vector<gh::acp::File>& clips) {
    using gh::milo_convert::Gh2ClipSetRole;
    if (role == Gh2ClipSetRole::GuitarFret)
        return 32;
    if (!clips.empty())
        return gh::milo_convert::convert_gh1_clip_time_flags_to_gh2(
            clips.front().play_flags);
    return 0;
}

std::vector<std::pair<std::string, uint32_t>>
guitar_group_masks_for_generated_clipsets() {
    return {
        {"walk_turn", 0x00000020u},
        {"walk_stop", 0x00000040u},
        {"walk_walk", 0x00000080u},
        {"bad", 0x00004000u},
        {"extreme", 0x00008000u},
        {"idle", 0x00010000u},
        {"intro", 0x00020000u},
        {"lose", 0x00040000u},
        {"normal", 0x00080000u},
        {"solo", 0x00100000u},
        {"win", 0x00200000u},
        {"star_power", 0x02000000u},
        {"win_finals", 0x04000000u},
    };
}

int generated_guitar_group_clip_priority(
    const std::string& group_name,
    const std::string& clip_name) {
    if (group_name == "idle") {
        if (clip_name == "idle_medium_01") return 0;
        if (clip_name.find("medium") != std::string::npos ||
            clip_name.find("_med_") != std::string::npos)
            return 10;
        if (clip_name.find("slow") != std::string::npos ||
            clip_name.find("_slw_") != std::string::npos)
            return 20;
        if (clip_name.find("fast") != std::string::npos ||
            clip_name.find("_fst_") != std::string::npos)
            return 30;
        return 40;
    }
    if (group_name == "normal") {
        if (clip_name == "idle_medium_01") return 0;
        if (clip_name.rfind("stand_medium_", 0) == 0) return 10;
        if (clip_name.rfind("stand_slow_", 0) == 0) return 20;
        if (clip_name.rfind("stand_fast_", 0) == 0) return 30;
        if (clip_name.find("medium") != std::string::npos ||
            clip_name.find("_med_") != std::string::npos)
            return 40;
        if (clip_name.find("slow") != std::string::npos ||
            clip_name.find("_slw_") != std::string::npos)
            return 50;
        if (clip_name.find("fast") != std::string::npos ||
            clip_name.find("_fst_") != std::string::npos)
            return 60;
        return 70;
    }
    return 0;
}

void sort_generated_guitar_group_clips(
    const std::string& group_name,
    std::vector<std::string>& clips) {
    std::stable_sort(
        clips.begin(), clips.end(),
        [&](const std::string& left, const std::string& right) {
            const int left_priority =
                generated_guitar_group_clip_priority(group_name, left);
            const int right_priority =
                generated_guitar_group_clip_priority(group_name, right);
            if (left_priority != right_priority)
                return left_priority < right_priority;
            return left < right;
        });
}

size_t add_generated_guitar_clip_groups(
    gh::milo::Directory& directory,
    const std::vector<gh::acp::File>& sources) {
    size_t groups_added = 0;
    for (const auto& [name, mask] :
         guitar_group_masks_for_generated_clipsets()) {
        gh::milo_object::CharClipGroup1 group;
        for (const auto& source : sources) {
            if ((source.flags & mask) != 0)
                group.clips.push_back(source.object_name);
        }
        if (group.clips.empty()) continue;
        sort_generated_guitar_group_clips(name, group.clips);
        group.which = 0;
        directory.entries.push_back(make_entry(
            "CharClipGroup", name,
            gh::milo_object::serialize_char_clip_group1(group)));
        ++groups_added;
    }
    if (groups_added == 0 && !sources.empty()) {
        gh::milo_object::CharClipGroup1 group;
        group.which = 0;
        group.clips.reserve(sources.size());
        for (const auto& source : sources)
            group.clips.push_back(source.object_name);
        sort_generated_guitar_group_clips("normal", group.clips);
        directory.entries.push_back(make_entry(
            "CharClipGroup", "normal",
            gh::milo_object::serialize_char_clip_group1(group)));
        ++groups_added;
    }
    return groups_added;
}

void usage() {
    std::cerr
        << "Usage:\n"
        << "  milo_convert_tool recompile-gh1-cameras <GH2.milo_ps2> <camera.dtb> "
           "<cam_paths.dtb> <campaths.milo_ps2> <shared.milo_ps2> <gh.dtb> --out <separate-output.milo_ps2>\n"
        << "  milo_convert_tool rebind-gh1-camera-paths <GH2.milo_ps2> "
           "<cam_paths.dtb> --out <separate-output.milo_ps2>\n"
        << "  milo_convert_tool convert <GH1.rnd_ps2> --name <dir-name> "
           "--out <GH2.milo_ps2> --manifest <manifest.tsv>\n"
        << "  milo_convert_tool build-clipset-from-acp <acp-dir> "
           "--name <dir-name> --role <role> --out <GH2.milo_ps2> "
           "[--move-self 0|1] [--control-root-pelvis-parent] "
           "[--group name=clip,clip ...] [--transitions links.tsv]\n"
        << "  milo_convert_tool build-character-from-meshbundle "
           "<meshbundle> --name <dir-name> --out <GH2.milo_ps2> "
           "[--main-anim <milo>] [--strum-anim <milo>] "
           "[--fret-anim <milo>] [--preserve-guitar-proxies]\n"
        << "  milo_convert_tool inspect-clipset <GH2.milo_ps2> "
            "[--channels] [--events]\n"
        << "  milo_convert_tool replace-clipset-clips <base.milo_ps2> "
           "--donor <donor.milo_ps2> --clip <name> [--clip <name>...] "
           "--out <patched.milo_ps2>\n"
        << "  milo_convert_tool alias-clipset-clips <base.milo_ps2> "
           "--alias <source=dest> [--alias <source=dest>...] "
           "[--replace] --out <patched.milo_ps2>\n"
        << "  milo_convert_tool merge-clipset-fallbacks <base.milo_ps2> "
           "--donor <stock.milo_ps2> --out <patched.milo_ps2>\n"
        << "  milo_convert_tool prune-clipset <base.milo_ps2> "
           "--keep <clip> [--keep <clip>...] --out <pruned.milo_ps2>\n"
        << "  milo_convert_tool rebase-clipset-template <base.milo_ps2> "
           "--template <retail.milo_ps2> --out <rebased.milo_ps2>\n"
        << "  milo_convert_tool strip-clip-channels <base.milo_ps2> "
           "--clip <name> [--clip <name>...] --channel <name> "
           "[--channel <name>...] --out <patched.milo_ps2>\n"
        << "  milo_convert_tool patch-clipset-palm-contact <base.milo_ps2> "
           "--target <bone> --palm-local <x> <y> <z> "
           "--out <patched.milo_ps2>\n"
        << "  milo_convert_tool sample-clip <GH2.milo_ps2> "
           "<clip> <sample-index|all> [channel-filter]\n"
        << "  milo_convert_tool inspect-character <GH2.milo_ps2> "
           "[--entries] [--controllers] [--transforms] [--meshes] "
           "[--mesh-vertices]\n"
        << "  milo_convert_tool export-character-snapshot "
           "<GH2.milo_ps2> --out <snapshot>\n"
        << "  milo_convert_tool patch-guitarist-proxies <GH2.milo_ps2> "
           "--out <patched.milo_ps2>\n"
        << "  milo_convert_tool rebase-character-slot <GH2.milo_ps2> "
           "--name <slot-name> --main-anim <milo> --strum-anim <milo> "
           "--fret-anim <milo> [--ps2-texture-max 256] "
           "--out <rebased.milo_ps2>\n"
        << "  milo_convert_tool merge-character-render-payload "
           "<template.milo_ps2> --donor <character.milo_ps2> "
           "[--mesh-limit <count>] [--rebind-template-rig] "
           "[--preserve-donor-bind-offsets] "
           "[--preserve-donor-hand-mesh-bind-offsets] "
           "[--retarget-rb2-face-rig] "
           "--out <merged.milo_ps2>\n"
        << "  milo_convert_tool repack-milo <GH2.milo_ps2> "
           "--out <repacked.milo_ps2>\n"
        << "  milo_convert_tool inspect-groups <GH2.milo_ps2>\n"
        << "  milo_convert_tool inspect-camshots <GH2.milo_ps2>\n"
        << "  milo_convert_tool inspect-skeleton <GH1.rnd_ps2> [--all]\n"
        << "  milo_convert_tool extract-entry <MILO> <type> <name> "
            "--out <object-body>\n"
        << "  milo_convert_tool rebuild-venue-waypoints <bundle-dir> "
           "<venue> --out <venue_chars.milo_ps2>\n"
        << "  milo_convert_tool patch-face-config <rnd_objects.dtb> "
           "--out <patched.dtb> [--dta <patched.dta>]\n"
        << "  milo_convert_tool patch-face-midi-config "
           "<midi_parsers.dtb> --out <patched.dtb> "
           "[--dta <patched.dta>]\n"
        << "  milo_convert_tool patch-face-character-config "
           "<char_objects.dtb> --out <patched.dtb> "
           "[--dta <patched.dta>]\n"
        << "  milo_convert_tool translate-singer-face <song.mid> "
           "--out <patched.mid> [--voc <song.voc>]\n";
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) usage();
    const std::string command = argv[1];
    fs::path input = argv[2];
    if (command == "recompile-gh1-cameras") {
        try {
            if (argc != 10 || std::string(argv[8]) != "--out")
                throw std::runtime_error("Usage: recompile-gh1-cameras input camera.dtb cam_paths.dtb campaths.milo_ps2 shared.milo_ps2 gh.dtb --out output");
            const fs::path output = argv[9];
            for (int i = 2; i <= 7; ++i)
                if (fs::absolute(argv[i]).lexically_normal() == fs::absolute(output).lexically_normal())
                    throw std::runtime_error("Use a separate output file; source assets must remain unchanged");
            const auto read_directory = [](const fs::path& path) {
                return gh::milo::parse_directory(gh::milo::container_payload(
                    gh::milo::parse_container(gh::milo::read_file(path.string()))));
            };
            auto main = read_directory(input);
            const auto paths = read_directory(argv[5]);
            const auto shared = read_directory(argv[6]);
            std::map<std::string, gh::milo_object::TransAnim6> animations;
            for (const auto& entry : shared.entries)
                if (entry.type == "TransAnim")
                    animations.emplace(entry.name, gh::milo_object::parse_trans_anim6(entry.body_bytes));
            // Replace only compiler-owned GH1 shots; keep geometry and every
            // unrelated/native-GH2 object byte-for-byte. Name collisions fail.
            main.entries.erase(std::remove_if(main.entries.begin(), main.entries.end(),
                [](const gh::milo::Entry& entry) {
                    return entry.type == "CamShot" &&
                        gh::milo_object::parse_cam_shot20(entry.body_bytes).object_fields.type == "gh1_venue_camera";
                }), main.entries.end());
            const auto result = gh::milo_convert::convert_gh1_venue_cameras_to_gh2_camshots(
                gh::milo::read_file(argv[3]), gh::milo::read_file(argv[4]), main, paths, animations,
                gh::milo_convert::gh1_camera_helper_filter_from_game_config(gh::milo::read_file(argv[7])));
            const auto payload = gh::milo::serialize_directory(result.main_directory);
            const auto bytes = gh::milo::serialize_container(gh::milo::make_object_aligned_container(payload));
            const auto verify = gh::milo::parse_directory(gh::milo::container_payload(gh::milo::parse_container(bytes)));
            if (!verify.boundaries_exact || gh::milo::serialize_directory(verify) != payload)
                throw std::runtime_error("Recompiled camera directory failed native round trip");
            write_file(output, bytes);
            std::cout << "recompiled " << result.records << " cameras / " << result.keyframes
                      << " frames / " << result.shaky_records << " shaky records\n";
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << '\n';
            return 2;
        }
    }
    if (command == "rebind-gh1-camera-paths") {
        try {
            if (argc != 6 || std::string(argv[4]) != "--out")
                throw std::runtime_error("Usage: rebind-gh1-camera-paths input.milo_ps2 cam_paths.dtb --out output.milo_ps2");
            const fs::path output = argv[5];
            if (fs::absolute(input).lexically_normal() == fs::absolute(output).lexically_normal())
                throw std::runtime_error("Use a separate output file; source assets must remain unchanged");
            const auto source = gh::milo::parse_container(gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(gh::milo::container_payload(source));
            const auto result = gh::milo_convert::rebind_gh1_venue_camera_paths(
                gh::milo::read_file(argv[3]), directory);
            const auto payload = gh::milo::serialize_directory(result.main_directory);
            const auto bytes = gh::milo::serialize_container(gh::milo::make_container(payload));
            const auto verify = gh::milo::parse_directory(gh::milo::container_payload(gh::milo::parse_container(bytes)));
            if (!verify.boundaries_exact || gh::milo::serialize_directory(verify) != payload)
                throw std::runtime_error("Rebound camera directory failed native round trip");
            write_file(output, bytes);
            std::cout << "rebound " << result.records << " cameras / " << result.keyframes << " frames\n";
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << '\n';
            return 2;
        }
    }
    if (command == "rebuild-venue-waypoints") {
        try {
            if (argc != 6 || std::string(argv[4]) != "--out")
                usage();
            const std::string venue = argv[3];
            const fs::path venue_dir =
                input / "world" / venue / "gen";
            if (!fs::is_directory(venue_dir))
                throw std::runtime_error(
                    "venue directory not found: " +
                    venue_dir.string());
            std::vector<gh::milo::Directory> sections;
            for (const auto& item :
                 fs::directory_iterator(venue_dir)) {
                if (!item.is_regular_file() ||
                    item.path().extension() != ".milo_ps2" ||
                    item.path().filename() ==
                        venue + "_chars.milo_ps2")
                    continue;
                const auto container = gh::milo::parse_container(
                    gh::milo::read_file(item.path().string()));
                sections.push_back(gh::milo::parse_directory(
                    gh::milo::container_payload(container)));
            }
            const auto converted =
                gh::milo_convert::
                    convert_gh1_venue_spots_to_gh2_waypoints(
                        venue, sections);
            const auto payload = gh::milo::serialize_directory(
                converted.characters_directory);
            const auto bytes = gh::milo::serialize_container(
                gh::milo::make_container(payload));
            write_file(argv[5], bytes);
            std::cout
                << "venue=" << venue
                << " sections=" << sections.size()
                << " waypoints=" << converted.waypoints
                << " bytes=" << bytes.size()
                << " output=" << argv[5] << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "inspect-camshots") {
        try {
            if (argc != 3) usage();
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            std::cout
                << "name\tcategory\tpath\tkeyframes\tduration\t"
                   "fov_min\tfov_max\ttargets\tparents\n";
            size_t count = 0;
            for (const auto& entry : directory.entries) {
                if (entry.type != "CamShot") continue;
                const auto shot =
                    gh::milo_object::parse_cam_shot20(entry.body_bytes);
                float duration = 0.0f;
                float fov_min = std::numeric_limits<float>::max();
                float fov_max = std::numeric_limits<float>::lowest();
                std::set<std::string> targets;
                std::set<std::string> parents;
                for (const auto& frame : shot.keyframes) {
                    duration += frame.duration;
                    fov_min = std::min(fov_min, frame.field_of_view);
                    fov_max = std::max(fov_max, frame.field_of_view);
                    for (const auto& target : frame.targets) {
                        targets.insert(
                            target.object +
                            (target.part.empty() ? "" : ":" + target.part));
                    }
                    if (!frame.parent.object.empty()) {
                        parents.insert(
                            frame.parent.object +
                            (frame.parent.part.empty()
                                 ? ""
                                 : ":" + frame.parent.part));
                    }
                }
                const auto join = [](const std::set<std::string>& values) {
                    std::string result;
                    for (const auto& value : values) {
                        if (!result.empty()) result += ",";
                        result += value;
                    }
                    return result;
                };
                std::cout << entry.name << '\t'
                          << shot.category << '\t'
                          << shot.path << '\t'
                          << shot.keyframes.size() << '\t'
                          << std::fixed << std::setprecision(6)
                          << duration << '\t'
                          << fov_min << '\t'
                          << fov_max << '\t'
                          << join(targets) << '\t'
                          << join(parents) << '\n';
                ++count;
            }
            std::cerr << "camshots=" << count << "\n";
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "extract-entry") {
        try {
            if (argc != 7 || std::string(argv[5]) != "--out")
                usage();
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            const std::string type = argv[3];
            const std::string name = argv[4];
            const auto found = std::find_if(
                directory.entries.begin(), directory.entries.end(),
                [&](const gh::milo::Entry& entry) {
                    return entry.type == type && entry.name == name;
                });
            if (found == directory.entries.end())
                throw std::runtime_error(
                    "entry not found: " + type + " " + name);
            write_file(argv[6], found->body_bytes);
            std::cout << "type=" << found->type
                      << " name=" << found->name
                      << " bytes=" << found->body_bytes.size()
                      << " output=" << argv[6] << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "replace-clipset-clips") {
        try {
            fs::path donor_path;
            fs::path output;
            std::set<std::string> clip_names;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--donor" && index + 1 < argc)
                    donor_path = argv[++index];
                else if (argument == "--clip" && index + 1 < argc)
                    clip_names.insert(argv[++index]);
                else if (argument == "--out" && index + 1 < argc)
                    output = argv[++index];
                else
                    usage();
            }
            if (donor_path.empty() || output.empty() || clip_names.empty())
                usage();

            const auto base_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto base_directory = gh::milo::parse_directory(
                gh::milo::container_payload(base_container));
            const auto donor_container = gh::milo::parse_container(
                gh::milo::read_file(donor_path.string()));
            const auto donor_directory = gh::milo::parse_directory(
                gh::milo::container_payload(donor_container));
            if (base_directory.dir_type != "CharClipSet" ||
                donor_directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "replace-clipset-clips requires CharClipSet MILOs");

            size_t base_clip_count = 0;
            for (const auto& entry : base_directory.entries)
                if (entry.type == "CharClipSamples") ++base_clip_count;
            auto root = gh::milo_object::parse_char_clip_set14(
                base_directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));

            size_t replaced = 0;
            for (const auto& clip_name : clip_names) {
                const gh::milo::Entry* donor_entry = nullptr;
                for (const auto& entry : donor_directory.entries) {
                    if (entry.type == "CharClipSamples" &&
                        entry.name == clip_name) {
                        donor_entry = &entry;
                        break;
                    }
                }
                if (!donor_entry)
                    throw std::runtime_error(
                        "donor missing CharClipSamples: " + clip_name);
                auto donor_clip =
                    gh::milo_object::parse_char_clip_samples10(
                        donor_entry->body_bytes);
                bool entry_replaced = false;
                for (auto& entry : base_directory.entries) {
                    if (entry.type == "CharClipSamples" &&
                        entry.name == clip_name) {
                        entry.body_bytes = donor_entry->body_bytes;
                        entry_replaced = true;
                        break;
                    }
                }
                if (!entry_replaced)
                    throw std::runtime_error(
                        "base missing CharClipSamples: " + clip_name);

                bool root_replaced = false;
                for (auto& summary : root.clips) {
                    if (summary.clip == clip_name) {
                        summary.flags = donor_clip.flags;
                        summary.size_bytes = static_cast<uint32_t>(
                            gh::milo_object::
                                char_clip_samples10_ps2_allocate_size(
                                    donor_clip));
                        root_replaced = true;
                        break;
                    }
                }
                if (!root_replaced)
                    throw std::runtime_error(
                        "base root missing clip summary: " + clip_name);
                ++replaced;
            }

            base_directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(base_directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "patched CharClipSet payload round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "patched CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " donor=" << donor_path.string()
                      << " replaced=" << replaced
                      << " entries=" << base_directory.entries.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "alias-clipset-clips") {
        try {
            fs::path output;
            std::vector<std::pair<std::string, std::string>> aliases;
            bool replace_existing = false;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--alias" && index + 1 < argc) {
                    const std::string value = argv[++index];
                    const size_t equals = value.find('=');
                    if (equals == std::string::npos || equals == 0 ||
                        equals + 1 >= value.size()) {
                        throw std::runtime_error(
                            "alias must be source=dest: " + value);
                    }
                    aliases.push_back(
                        {value.substr(0, equals), value.substr(equals + 1)});
                } else if (argument == "--replace") {
                    replace_existing = true;
                } else if (argument == "--out" && index + 1 < argc) {
                    output = argv[++index];
                } else {
                    usage();
                }
            }
            if (output.empty() || aliases.empty()) usage();

            const auto base_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(base_container));
            if (directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "alias-clipset-clips requires a CharClipSet MILO");

            size_t base_clip_count = 0;
            for (const auto& entry : directory.entries)
                if (entry.type == "CharClipSamples") ++base_clip_count;
            auto root = gh::milo_object::parse_char_clip_set14(
                directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));

            size_t added = 0;
            size_t replaced = 0;
            for (const auto& alias : aliases) {
                const std::string& source = alias.first;
                const std::string& dest = alias.second;
                const gh::milo::Entry* source_entry = nullptr;
                for (const auto& entry : directory.entries) {
                    if (entry.type == "CharClipSamples" &&
                        entry.name == source) {
                        source_entry = &entry;
                        break;
                    }
                }
                if (!source_entry)
                    throw std::runtime_error(
                        "source CharClipSamples not found: " + source);
                const auto source_clip =
                    gh::milo_object::parse_char_clip_samples10(
                        source_entry->body_bytes);
                const uint32_t allocate_size = static_cast<uint32_t>(
                    gh::milo_object::char_clip_samples10_ps2_allocate_size(
                        source_clip));
                auto dest_entry = std::find_if(
                    directory.entries.begin(), directory.entries.end(),
                    [&](const gh::milo::Entry& entry) {
                        return entry.type == "CharClipSamples" &&
                               entry.name == dest;
                    });
                auto dest_summary = std::find_if(
                    root.clips.begin(), root.clips.end(),
                    [&](const auto& summary) {
                        return summary.clip == dest;
                    });
                if ((dest_entry == directory.entries.end()) !=
                    (dest_summary == root.clips.end()))
                    throw std::runtime_error(
                        "destination clip body/summary mismatch: " + dest);
                if (dest_entry != directory.entries.end()) {
                    if (!replace_existing)
                        throw std::runtime_error(
                            "destination CharClipSamples already exists: " +
                            dest);
                    dest_entry->body_bytes = source_entry->body_bytes;
                    dest_entry->size = dest_entry->body_bytes.size();
                    dest_summary->flags = source_clip.flags;
                    dest_summary->size_bytes = allocate_size;
                    ++replaced;
                } else {
                    gh::milo::Entry new_entry = *source_entry;
                    new_entry.name = dest;
                    new_entry.offset = 0;
                    new_entry.size = 0;
                    new_entry.terminator_offset = 0;
                    directory.entries.push_back(std::move(new_entry));
                    root.clips.push_back(
                        {dest, source_clip.flags, allocate_size});
                    ++added;
                }
            }

            directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "aliased CharClipSet payload round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "aliased CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " aliases=" << added
                      << " replaced=" << replaced
                      << " entries=" << directory.entries.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "merge-clipset-fallbacks") {
        try {
            fs::path donor_path;
            fs::path output;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--donor" && index + 1 < argc)
                    donor_path = argv[++index];
                else if (argument == "--out" && index + 1 < argc)
                    output = argv[++index];
                else
                    usage();
            }
            if (donor_path.empty() || output.empty()) usage();

            const auto base_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(base_container));
            const auto donor_container = gh::milo::parse_container(
                gh::milo::read_file(donor_path.string()));
            const auto donor_directory = gh::milo::parse_directory(
                gh::milo::container_payload(donor_container));
            if (directory.dir_type != "CharClipSet" ||
                donor_directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "merge-clipset-fallbacks requires CharClipSet MILOs");

            size_t base_clip_count = 0;
            std::set<std::string> clip_names;
            std::set<std::string> group_names;
            for (const auto& entry : directory.entries) {
                if (entry.type == "CharClipSamples") {
                    ++base_clip_count;
                    if (!clip_names.insert(entry.name).second)
                        throw std::runtime_error(
                            "duplicate base CharClipSamples: " + entry.name);
                } else if (entry.type == "CharClipGroup") {
                    if (!group_names.insert(entry.name).second)
                        throw std::runtime_error(
                            "duplicate base CharClipGroup: " + entry.name);
                }
            }
            auto root = gh::milo_object::parse_char_clip_set14(
                directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));

            size_t clips_added = 0;
            for (const auto& donor_entry : donor_directory.entries) {
                if (donor_entry.type != "CharClipSamples" ||
                    clip_names.count(donor_entry.name) != 0)
                    continue;
                const auto donor_clip =
                    gh::milo_object::parse_char_clip_samples10(
                        donor_entry.body_bytes);
                gh::milo::Entry added = donor_entry;
                added.offset = 0;
                added.size = 0;
                added.terminator_offset = 0;
                directory.entries.push_back(std::move(added));
                root.clips.push_back(
                    {donor_entry.name, donor_clip.flags,
                     static_cast<uint32_t>(
                         gh::milo_object::
                             char_clip_samples10_ps2_allocate_size(
                                 donor_clip))});
                clip_names.insert(donor_entry.name);
                ++clips_added;
            }

            size_t groups_added = 0;
            for (const auto& donor_entry : donor_directory.entries) {
                if (donor_entry.type != "CharClipGroup" ||
                    group_names.count(donor_entry.name) != 0)
                    continue;
                const auto group =
                    gh::milo_object::parse_char_clip_group1(
                        donor_entry.body_bytes);
                for (const auto& member : group.clips) {
                    if (clip_names.count(member) == 0)
                        throw std::runtime_error(
                            "fallback group " + donor_entry.name +
                            " references missing clip " + member);
                }
                gh::milo::Entry added = donor_entry;
                added.offset = 0;
                added.size = 0;
                added.terminator_offset = 0;
                directory.entries.push_back(std::move(added));
                group_names.insert(donor_entry.name);
                ++groups_added;
            }

            directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "fallback CharClipSet payload round trip differs");
            const auto target_bytes = serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "fallback CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " donor=" << donor_path.string()
                      << " clips_added=" << clips_added
                      << " groups_added=" << groups_added
                      << " entries=" << directory.entries.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "rebase-clipset-template") {
        try {
            fs::path template_path;
            fs::path output;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--template" && i + 1 < argc)
                    template_path = argv[++i];
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else
                    usage();
            }
            if (template_path.empty() || output.empty()) usage();

            const auto source_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(source_container));
            const auto template_container = gh::milo::parse_container(
                gh::milo::read_file(template_path.string()));
            const auto template_directory = gh::milo::parse_directory(
                gh::milo::container_payload(template_container));
            if (directory.dir_type != "CharClipSet" ||
                template_directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "rebase-clipset-template requires two CharClipSet MILOs");
            const std::string source_directory_name = directory.dir_name;

            size_t source_clip_count = 0;
            size_t template_clip_count = 0;
            for (const auto& entry : directory.entries)
                if (entry.type == "CharClipSamples") ++source_clip_count;
            for (const auto& entry : template_directory.entries)
                if (entry.type == "CharClipSamples") ++template_clip_count;
            const auto source_root =
                gh::milo_object::parse_char_clip_set14(
                    directory.dir_body_bytes,
                    static_cast<uint32_t>(source_clip_count));
            auto target_root = gh::milo_object::parse_char_clip_set14(
                template_directory.dir_body_bytes,
                static_cast<uint32_t>(template_clip_count));
            target_root.clips = source_root.clips;
            directory.dir_name = template_directory.dir_name;
            directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(target_root);

            const auto target_payload = gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                verify_directory.dir_name != template_directory.dir_name ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "rebased CharClipSet payload round trip differs");
            const auto target_bytes = serialize_generated_milo(target_payload);
            const auto verify_container = gh::milo::parse_container(target_bytes);
            const auto verify_container_directory = gh::milo::parse_directory(
                gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_container_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "rebased CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " template=" << template_path.string()
                      << " directory_before=" << source_directory_name
                      << " directory_after=" << template_directory.dir_name
                      << " clips=" << source_clip_count
                      << " subdirs="
                      << target_root.object_directory.subdirectories.size()
                      << " blend_width=" << target_root.blend_width
                      << " play_flags=" << target_root.play_flags
                      << " move_self=" << target_root.move_self
                      << " bytes=" << target_bytes.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "prune-clipset") {
        try {
            fs::path output;
            std::set<std::string> keep_names;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--keep" && i + 1 < argc)
                    keep_names.insert(argv[++i]);
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else
                    usage();
            }
            if (keep_names.empty() || output.empty()) usage();

            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            if (directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "prune-clipset requires a CharClipSet MILO");

            size_t base_clip_count = 0;
            std::set<std::string> base_clip_names;
            for (const auto& entry : directory.entries) {
                if (entry.type != "CharClipSamples") continue;
                ++base_clip_count;
                if (!base_clip_names.insert(entry.name).second)
                    throw std::runtime_error(
                        "duplicate CharClipSamples: " + entry.name);
            }
            std::vector<std::string> missing;
            std::set_difference(
                keep_names.begin(), keep_names.end(),
                base_clip_names.begin(), base_clip_names.end(),
                std::back_inserter(missing));
            if (!missing.empty())
                throw std::runtime_error(
                    "prune-clipset requested missing clip: " +
                    missing.front());

            auto root = gh::milo_object::parse_char_clip_set14(
                directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));
            root.clips.erase(
                std::remove_if(
                    root.clips.begin(), root.clips.end(),
                    [&](const auto& clip) {
                        return !keep_names.count(clip.clip);
                    }),
                root.clips.end());
            if (root.clips.size() != keep_names.size())
                throw std::runtime_error(
                    "prune-clipset root/sample inventory differs");

            size_t groups_pruned = 0;
            size_t group_members_removed = 0;
            for (auto& entry : directory.entries) {
                if (entry.type != "CharClipGroup") continue;
                auto group = gh::milo_object::parse_char_clip_group1(
                    entry.body_bytes);
                const size_t original_size = group.clips.size();
                group.clips.erase(
                    std::remove_if(
                        group.clips.begin(), group.clips.end(),
                        [&](const std::string& clip) {
                            return !keep_names.count(clip);
                        }),
                    group.clips.end());
                if (original_size != 0 && group.clips.empty())
                    throw std::runtime_error(
                        "prune-clipset emptied group: " + entry.name);
                if (group.clips.size() != original_size) {
                    ++groups_pruned;
                    group_members_removed +=
                        original_size - group.clips.size();
                    entry.body_bytes =
                        gh::milo_object::serialize_char_clip_group1(group);
                    entry.size = entry.body_bytes.size();
                }
            }
            directory.entries.erase(
                std::remove_if(
                    directory.entries.begin(), directory.entries.end(),
                    [&](const gh::milo::Entry& entry) {
                        return entry.type == "CharClipSamples" &&
                               !keep_names.count(entry.name);
                    }),
                directory.entries.end());
            directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);

            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "pruned CharClipSet payload round trip differs");
            const auto target_bytes = serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "pruned CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " clips_before=" << base_clip_count
                      << " clips_after=" << keep_names.size()
                      << " clips_removed="
                      << base_clip_count - keep_names.size()
                      << " groups_pruned=" << groups_pruned
                      << " group_members_removed=" << group_members_removed
                      << " bytes=" << target_bytes.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "strip-clip-channels") {
        try {
            fs::path output;
            std::set<std::string> clip_names;
            std::set<std::string> channel_names;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--clip" && index + 1 < argc)
                    clip_names.insert(argv[++index]);
                else if (argument == "--channel" && index + 1 < argc)
                    channel_names.insert(argv[++index]);
                else if (argument == "--out" && index + 1 < argc)
                    output = argv[++index];
                else
                    usage();
            }
            if (output.empty() || clip_names.empty() || channel_names.empty())
                usage();

            const auto base_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto base_directory = gh::milo::parse_directory(
                gh::milo::container_payload(base_container));
            if (base_directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "strip-clip-channels requires a CharClipSet MILO");

            size_t base_clip_count = 0;
            for (const auto& entry : base_directory.entries)
                if (entry.type == "CharClipSamples") ++base_clip_count;
            auto root = gh::milo_object::parse_char_clip_set14(
                base_directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));

            size_t clips_seen = 0;
            size_t channels_stripped = 0;
            for (auto& entry : base_directory.entries) {
                if (entry.type != "CharClipSamples" ||
                    clip_names.count(entry.name) == 0) {
                    continue;
                }
                ++clips_seen;
                auto clip = gh::milo_object::parse_char_clip_samples10(
                    entry.body_bytes);
                channels_stripped +=
                    strip_char_bones_channels(clip.full, channel_names);
                channels_stripped +=
                    strip_char_bones_channels(clip.one, channel_names);
                entry.body_bytes =
                    gh::milo_object::serialize_char_clip_samples10(clip);

                bool root_updated = false;
                for (auto& summary : root.clips) {
                    if (summary.clip == entry.name) {
                        summary.flags = clip.flags;
                        summary.size_bytes = static_cast<uint32_t>(
                            gh::milo_object::
                                char_clip_samples10_ps2_allocate_size(clip));
                        root_updated = true;
                        break;
                    }
                }
                if (!root_updated)
                    throw std::runtime_error(
                        "base root missing clip summary: " + entry.name);
            }
            if (clips_seen != clip_names.size())
                throw std::runtime_error(
                    "one or more requested CharClipSamples were not found");

            base_directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(base_directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "stripped CharClipSet payload round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "stripped CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout << "base=" << input.string()
                      << " clips=" << clips_seen
                      << " stripped_channels=" << channels_stripped
                      << " entries=" << base_directory.entries.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "patch-clipset-palm-contact") {
        try {
            fs::path output;
            std::string target;
            std::array<float, 3> palm_local{};
            bool have_palm_local = false;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--target" && index + 1 < argc) {
                    target = argv[++index];
                } else if (argument == "--palm-local" && index + 3 < argc) {
                    for (float& value : palm_local)
                        value = std::stof(argv[++index]);
                    have_palm_local = true;
                } else if (argument == "--out" && index + 1 < argc) {
                    output = argv[++index];
                } else {
                    usage();
                }
            }
            if (output.empty() || target.empty() || !have_palm_local)
                usage();
            for (const float value : palm_local) {
                if (!std::isfinite(value))
                    throw std::runtime_error(
                        "--palm-local values must be finite");
            }

            const auto base_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto base_directory = gh::milo::parse_directory(
                gh::milo::container_payload(base_container));
            if (base_directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "patch-clipset-palm-contact requires a CharClipSet MILO");

            size_t base_clip_count = 0;
            for (const auto& entry : base_directory.entries)
                if (entry.type == "CharClipSamples") ++base_clip_count;
            auto root = gh::milo_object::parse_char_clip_set14(
                base_directory.dir_body_bytes,
                static_cast<uint32_t>(base_clip_count));

            PalmContactPatchStats total;
            size_t patched_clips = 0;
            for (auto& entry : base_directory.entries) {
                if (entry.type != "CharClipSamples") continue;
                auto clip = gh::milo_object::parse_char_clip_samples10(
                    entry.body_bytes);
                PalmContactPatchStats clip_stats;
                add_patch_stats(
                    clip_stats,
                    patch_palm_contact_samples(
                        clip.full, target, palm_local));
                add_patch_stats(
                    clip_stats,
                    patch_palm_contact_samples(
                        clip.one, target, palm_local));
                add_patch_stats(
                    clip_stats,
                    patch_palm_contact_samples(
                        clip.duplicate, target, palm_local));
                if (clip_stats.samples == 0) continue;
                ++patched_clips;
                add_patch_stats(total, clip_stats);
                entry.body_bytes =
                    gh::milo_object::serialize_char_clip_samples10(clip);

                bool root_updated = false;
                for (auto& summary : root.clips) {
                    if (summary.clip == entry.name) {
                        summary.flags = clip.flags;
                        summary.size_bytes = static_cast<uint32_t>(
                            gh::milo_object::
                                char_clip_samples10_ps2_allocate_size(clip));
                        root_updated = true;
                        break;
                    }
                }
                if (!root_updated)
                    throw std::runtime_error(
                        "base root missing clip summary: " + entry.name);
            }
            if (patched_clips == 0)
                throw std::runtime_error(
                    "no CharClipSamples contained palm-contact target " +
                    target);

            base_directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(base_directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload) {
                throw std::runtime_error(
                    "palm-contact CharClipSet payload round trip differs");
            }
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload) {
                throw std::runtime_error(
                    "palm-contact CharClipSet container round trip differs");
            }
            write_file(output, target_bytes);
            std::cout << std::setprecision(
                std::numeric_limits<float>::max_digits10);
            std::cout << "base=" << input.string()
                      << " target=" << target
                      << " clips=" << patched_clips
                      << " sample_sets=" << total.sample_sets
                      << " samples=" << total.samples
                      << " max_contact_error=" << total.max_contact_error
                      << " entries=" << base_directory.entries.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "patch-guitarist-proxies") {
        try {
            if (argc != 5 || std::string(argv[3]) != "--out")
                usage();
            const fs::path output = argv[4];
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            const size_t patched =
                patch_guitarist_proxy_transforms(directory);
            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "patched guitarist proxy directory round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "patched guitarist proxy container round trip differs");
            write_file(output, target_bytes);
            std::cout
                << "patched_guitarist_proxies=" << patched
                << " entries=" << directory.entries.size()
                << " bytes=" << target_bytes.size()
                << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "rebase-character-slot") {
        try {
            fs::path output;
            std::string name;
            std::string main_anim;
            std::string strum_anim;
            std::string fret_anim;
            uint16_t ps2_texture_max = 0;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--name" && i + 1 < argc) name = argv[++i];
                else if (arg == "--main-anim" && i + 1 < argc)
                    main_anim = argv[++i];
                else if (arg == "--strum-anim" && i + 1 < argc)
                    strum_anim = argv[++i];
                else if (arg == "--fret-anim" && i + 1 < argc)
                    fret_anim = argv[++i];
                else if (arg == "--ps2-texture-max" && i + 1 < argc) {
                    const int value = std::stoi(argv[++i]);
                    if (value < 1 || value > 4096)
                        throw std::runtime_error(
                            "--ps2-texture-max must be 1..4096");
                    ps2_texture_max = static_cast<uint16_t>(value);
                }
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else usage();
            }
            if (output.empty() || name.empty() || main_anim.empty() ||
                strum_anim.empty() || fret_anim.empty())
                usage();

            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            if (directory.dir_type != "BandCharacter")
                throw std::runtime_error(
                    "rebase-character-slot requires a BandCharacter MILO");

            auto band_character =
                gh::milo_object::parse_band_character1(
                    directory.dir_body_bytes);
            auto& character = band_character.character;
            if (character.lods.empty())
                throw std::runtime_error(
                    "rebase-character-slot character has no LOD groups");
            const std::string old_group = character.lods.front().group;
            if (old_group.empty())
                throw std::runtime_error(
                    "rebase-character-slot character has no root group");
            for (const auto& lod : character.lods) {
                if (lod.group != old_group)
                    throw std::runtime_error(
                        "rebase-character-slot requires one shared LOD group");
            }

            const std::string new_group = name + ".grp";
            size_t groups_rebased = 0;
            size_t drivers_rebased = 0;
            size_t runtime_meshes_patched = 0;
            for (auto& entry : directory.entries) {
                if (entry.type == "Mesh") {
                    auto mesh = gh::milo_object::parse_mesh28(
                        entry.body_bytes, directory.dir_version);
                    bool patched = false;
                    if (mesh.geometry_owner.empty()) {
                        mesh.geometry_owner = entry.name;
                        patched = true;
                    }
                    if (mesh_needs_ps2_groups(mesh)) {
                        build_ps2_mesh_groups(mesh);
                        patched = true;
                    }
                    if (patched) {
                        entry.body_bytes =
                            gh::milo_object::serialize_mesh28(
                                mesh, directory.dir_version);
                        entry.size = entry.body_bytes.size();
                        ++runtime_meshes_patched;
                    }
                    continue;
                }
                if (entry.type == "Group" && entry.name == old_group) {
                    entry.name = new_group;
                    ++groups_rebased;
                } else if (entry.type == "CharDriver" &&
                           entry.name == "main.drv") {
                    auto driver = gh::milo_object::parse_char_driver3(
                        entry.body_bytes);
                    driver.clips = character_model_relative_ref(main_anim);
                    entry.body_bytes =
                        gh::milo_object::serialize_char_driver3(driver);
                    entry.size = entry.body_bytes.size();
                    ++drivers_rebased;
                } else if (entry.type == "CharDriverMidi" &&
                           entry.name == "left_hand.drv") {
                    auto driver =
                        gh::milo_object::parse_char_driver_midi3(
                            entry.body_bytes);
                    driver.driver.clips =
                        character_model_relative_ref(fret_anim);
                    entry.body_bytes =
                        gh::milo_object::serialize_char_driver_midi3(driver);
                    entry.size = entry.body_bytes.size();
                    ++drivers_rebased;
                } else if (entry.type == "CharDriverMidi" &&
                           entry.name == "right_hand.drv") {
                    auto driver =
                        gh::milo_object::parse_char_driver_midi3(
                            entry.body_bytes);
                    driver.driver.clips =
                        character_model_relative_ref(strum_anim);
                    entry.body_bytes =
                        gh::milo_object::serialize_char_driver_midi3(driver);
                    entry.size = entry.body_bytes.size();
                    ++drivers_rebased;
                }
            }
            if (groups_rebased != 1 || drivers_rebased != 3)
                throw std::runtime_error(
                    "rebase-character-slot did not find the canonical "
                    "group and three guitarist drivers");

            directory.dir_name = name;
            for (auto& lod : character.lods) lod.group = new_group;
            if (character.sphere_base == old_group)
                character.sphere_base = new_group;
            directory.dir_body_bytes =
                gh::milo_object::serialize_band_character1(band_character);
            const size_t textures_quantized = ps2_texture_max
                ? quantize_character_textures_8bpp(
                      directory, ps2_texture_max)
                : 0;

            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "rebased character directory round trip differs");
            const auto target_bytes = serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "rebased character container round trip differs");
            write_file(output, target_bytes);
            std::cout << "character_slot=" << name
                      << " group=" << new_group
                      << " drivers=" << drivers_rebased
                      << " runtime_meshes_patched="
                      << runtime_meshes_patched
                      << " textures_quantized=" << textures_quantized
                      << " entries=" << directory.entries.size()
                      << " bytes=" << target_bytes.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "repack-milo") {
        try {
            if (argc != 5 || std::string(argv[3]) != "--out") usage();
            const fs::path output = argv[4];
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto payload = gh::milo::container_payload(container);
            const auto directory = gh::milo::parse_directory(payload);
            if (!directory.boundaries_exact ||
                gh::milo::serialize_directory(directory) != payload)
                throw std::runtime_error(
                    "repack-milo requires an exact directory round trip");
            const auto target_bytes = serialize_generated_milo(payload);
            const auto verify = gh::milo::parse_container(target_bytes);
            if (gh::milo::container_payload(verify) != payload)
                throw std::runtime_error(
                    "repack-milo changed the decompressed payload");
            write_file(output, target_bytes);
            std::cout << "payload_bytes=" << payload.size()
                      << " source_bytes="
                      << gh::milo::read_file(input.string()).size()
                      << " output_bytes=" << target_bytes.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "merge-character-render-payload") {
        try {
            fs::path donor_path;
            fs::path output;
            size_t mesh_limit = std::numeric_limits<size_t>::max();
            bool rebind_template_rig = false;
            bool preserve_donor_bind_offsets = false;
            bool preserve_donor_hand_mesh_bind_offsets = false;
            bool retarget_rb2_face_rig = false;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--donor" && i + 1 < argc)
                    donor_path = argv[++i];
                else if (arg == "--mesh-limit" && i + 1 < argc)
                    mesh_limit = static_cast<size_t>(std::stoull(argv[++i]));
                else if (arg == "--rebind-template-rig")
                    rebind_template_rig = true;
                else if (arg == "--preserve-donor-bind-offsets")
                    preserve_donor_bind_offsets = true;
                else if (arg ==
                         "--preserve-donor-hand-mesh-bind-offsets")
                    preserve_donor_hand_mesh_bind_offsets = true;
                else if (arg == "--retarget-rb2-face-rig")
                    retarget_rb2_face_rig = true;
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else usage();
            }
            if (donor_path.empty() || output.empty()) usage();
            if ((preserve_donor_bind_offsets ||
                 preserve_donor_hand_mesh_bind_offsets) &&
                !rebind_template_rig)
                throw std::runtime_error(
                    "donor bind-offset preservation requires "
                    "--rebind-template-rig");
            const auto template_container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(template_container));
            const auto donor_container = gh::milo::parse_container(
                gh::milo::read_file(donor_path.string()));
            const auto donor_directory = gh::milo::parse_directory(
                gh::milo::container_payload(donor_container));
            if (directory.dir_type != "BandCharacter" ||
                donor_directory.dir_type != "BandCharacter")
                throw std::runtime_error(
                    "merge-character-render-payload requires two "
                    "BandCharacter MILOs");
            if (directory.dir_name != donor_directory.dir_name)
                throw std::runtime_error(
                    "merge-character-render-payload directory names differ");

            auto character = gh::milo_object::parse_band_character1(
                directory.dir_body_bytes);
            const auto donor_character =
                gh::milo_object::parse_band_character1(
                    donor_directory.dir_body_bytes);
            if (donor_character.character.lods.size() != 1 ||
                donor_character.character.lods.front().group.empty())
                throw std::runtime_error(
                    "merge-character-render-payload donor must have one LOD");
            const std::string donor_group =
                donor_character.character.lods.front().group;
            const auto donor_group_entry = std::find_if(
                donor_directory.entries.begin(), donor_directory.entries.end(),
                [&](const gh::milo::Entry& entry) {
                    return entry.type == "Group" &&
                           entry.name == donor_group;
                });
            if (donor_group_entry == donor_directory.entries.end())
                throw std::runtime_error(
                    "merge-character-render-payload donor group missing");
            auto isolated_group = gh::milo_object::parse_group12(
                donor_group_entry->body_bytes);
            std::set<std::string> donor_mesh_names;
            for (const auto& entry : donor_directory.entries) {
                if (entry.type == "Mesh") donor_mesh_names.insert(entry.name);
            }
            std::set<std::string> enabled_mesh_names;
            for (const auto& object : isolated_group.objects) {
                if (donor_mesh_names.count(object) &&
                    enabled_mesh_names.size() < mesh_limit)
                    enabled_mesh_names.insert(object);
            }
            isolated_group.objects.erase(
                std::remove_if(
                    isolated_group.objects.begin(),
                    isolated_group.objects.end(),
                    [&](const std::string& object) {
                        return donor_mesh_names.count(object) &&
                               !enabled_mesh_names.count(object);
                    }),
                isolated_group.objects.end());

            std::map<std::pair<std::string, std::string>, size_t>
                template_entries;
            const std::set<std::string> controller_types = {
                "CharDriver", "CharDriverMidi", "CharEyes",
                "CharForeTwist", "CharHair", "CharIKHand", "CharIKMidi",
                "CharLookAt", "CharServoBone", "CharUpperTwist",
                "CharWalk", "CharWeightSetter", "FaceFxLipSyncServo"};
            std::map<std::pair<std::string, std::string>, std::vector<uint8_t>>
                template_controller_bodies;
            for (size_t index = 0; index < directory.entries.size(); ++index)
                template_entries.emplace(
                    std::make_pair(
                        directory.entries[index].type,
                        directory.entries[index].name),
                    index);
            for (const auto& entry : directory.entries) {
                if (controller_types.count(entry.type))
                    template_controller_bodies.emplace(
                        std::make_pair(entry.type, entry.name),
                        entry.body_bytes);
            }

            size_t template_meshes_hidden = 0;
            size_t template_meshes_unparsed = 0;
            for (auto& entry : directory.entries) {
                if (entry.type != "Mesh") continue;
                try {
                    auto mesh = gh::milo_object::parse_mesh28(
                        entry.body_bytes,
                        static_cast<uint32_t>(directory.dir_version));
                    if (mesh.drawable.showing) ++template_meshes_hidden;
                    mesh.drawable.showing = false;
                    entry.body_bytes = gh::milo_object::serialize_mesh28(
                        mesh,
                        static_cast<uint32_t>(directory.dir_version));
                    entry.size = entry.body_bytes.size();
                } catch (const std::exception&) {
                    // Some retail BandCharacter directories contain opaque
                    // Mesh revisions that the runtime deliberately skips.
                    // The merged Character LODs are redirected to the donor
                    // group below, so retaining those unreachable bodies is
                    // safer than rejecting an otherwise decodable template.
                    ++template_meshes_unparsed;
                }
            }

            std::map<std::string, gh::milo_object::Trans9>
                template_transforms;
            std::map<std::string, gh::milo_object::Trans9>
                donor_transforms;
            size_t template_transforms_unparsed = 0;
            size_t donor_transforms_unparsed = 0;
            for (const auto& entry : directory.entries) {
                if (entry.type != "Trans") continue;
                try {
                    template_transforms.emplace(
                        entry.name,
                        gh::milo_object::parse_trans9(entry.body_bytes));
                } catch (const std::exception&) {
                    ++template_transforms_unparsed;
                }
            }
            for (const auto& entry : donor_directory.entries) {
                if (entry.type != "Trans") continue;
                try {
                    donor_transforms.emplace(
                        entry.name,
                        gh::milo_object::parse_trans9(entry.body_bytes));
                } catch (const std::exception&) {
                    ++donor_transforms_unparsed;
                }
            }
            std::map<std::string, std::array<float, 12>>
                template_bind_worlds;
            std::set<std::string> unresolved_template_transforms;
            for (const auto& [name, transform] : template_transforms) {
                (void)transform;
                unresolved_template_transforms.insert(name);
            }
            while (!unresolved_template_transforms.empty()) {
                bool made_progress = false;
                for (auto it = unresolved_template_transforms.begin();
                     it != unresolved_template_transforms.end();) {
                    const auto& transform = template_transforms.at(*it);
                    const auto parent = template_transforms.find(
                        transform.parent);
                    if (parent != template_transforms.end() &&
                        template_bind_worlds.count(transform.parent) == 0) {
                        ++it;
                        continue;
                    }
                    const auto world = parent == template_transforms.end()
                        ? transform.local
                        : multiply_affine_transform(
                              transform.local,
                              template_bind_worlds.at(transform.parent));
                    template_bind_worlds.emplace(*it, world);
                    it = unresolved_template_transforms.erase(it);
                    made_progress = true;
                }
                if (!made_progress)
                    throw std::runtime_error(
                        "merge-character-render-payload template transform "
                        "hierarchy contains a cycle");
            }
            const std::map<std::string, std::string> rb2_face_aliases = {
                {"bone_L-eye.mesh", "eye-L.mesh"},
                {"bone_R-eye.mesh", "eye-R.mesh"},
                {"bone_L-lid.mesh", "bone_L-upperlid.mesh"},
                {"bone_R-lid.mesh", "bone_R-upperlid.mesh"},
                {"bone_L-lipcorner.mesh", "bone_lip-L-corner.mesh"},
                {"bone_R-lipcorner.mesh", "bone_lip-R-corner.mesh"},
                {"bone_liptop_left.mesh", "bone_upperlip-L1.mesh"},
                {"bone_liptop_mid.mesh", "bone_upperlip-center.mesh"},
                {"bone_liptop_right.mesh", "bone_upperlip-R1.mesh"},
                {"bone_lowlip_left.mesh", "bone_lowerlip-L1.mesh"},
                {"bone_lowlip_mid.mesh", "bone_lowerlip-center.mesh"},
                {"bone_lowlip_right.mesh", "bone_lowerlip-R1.mesh"},
            };
            size_t source_proportion_face_targets_repositioned = 0;
            if (retarget_rb2_face_rig && !rebind_template_rig) {
                // Source-proportion retargeting keeps the RB2 bind skeleton so
                // Casey's rotations act around Penelope's authored pivots. Move
                // the GH2 face-channel targets onto the equivalent RB2 pivots;
                // donor skin slots are aliased to these targets below without
                // changing their bind offsets.
                for (const auto& [source, target] : rb2_face_aliases) {
                    const auto source_transform = donor_transforms.find(source);
                    if (source_transform == donor_transforms.end())
                        throw std::runtime_error(
                            "RB2 face-rig donor is missing " + source);

                    const auto target_transform_entry =
                        template_entries.find({"Trans", target});
                    if (target_transform_entry != template_entries.end()) {
                        auto positioned = source_transform->second;
                        auto& target_entry =
                            directory.entries[target_transform_entry->second];
                        target_entry.body_bytes =
                            gh::milo_object::serialize_trans9(positioned);
                        target_entry.size = target_entry.body_bytes.size();
                        template_transforms[target] = positioned;
                        template_bind_worlds[target] = positioned.world;
                        ++source_proportion_face_targets_repositioned;
                        continue;
                    }

                    const auto target_mesh_entry =
                        template_entries.find({"Mesh", target});
                    if (target_mesh_entry != template_entries.end()) {
                        auto& target_entry =
                            directory.entries[target_mesh_entry->second];
                        auto target_mesh = gh::milo_object::parse_mesh28(
                            target_entry.body_bytes,
                            static_cast<uint32_t>(directory.dir_version));
                        target_mesh.transformable.local =
                            source_transform->second.local;
                        target_mesh.transformable.world =
                            source_transform->second.world;
                        target_mesh.transformable.parent =
                            source_transform->second.parent;
                        target_entry.body_bytes =
                            gh::milo_object::serialize_mesh28(
                                target_mesh,
                                static_cast<uint32_t>(directory.dir_version));
                        target_entry.size = target_entry.body_bytes.size();
                        template_bind_worlds[target] =
                            source_transform->second.world;
                        ++source_proportion_face_targets_repositioned;
                        continue;
                    }

                    throw std::runtime_error(
                        "RB2 face-rig target is missing " + target);
                }
            }
            if (retarget_rb2_face_rig) {
                for (const char* eye_name : {"eye-L.mesh", "eye-R.mesh"}) {
                    const auto eye = std::find_if(
                        directory.entries.begin(), directory.entries.end(),
                        [&](const gh::milo::Entry& entry) {
                            return entry.type == "Mesh" &&
                                   entry.name == eye_name;
                        });
                    if (eye == directory.entries.end())
                        throw std::runtime_error(
                            "RB2 face-rig target is missing eye mesh " +
                            std::string(eye_name));
                    const auto mesh = gh::milo_object::parse_mesh28(
                        eye->body_bytes,
                        static_cast<uint32_t>(directory.dir_version));
                    const auto parent = template_bind_worlds.find(
                        mesh.transformable.parent);
                    const auto bind_world =
                        parent == template_bind_worlds.end()
                            ? mesh.transformable.local
                            : multiply_affine_transform(
                                  mesh.transformable.local,
                                  parent->second);
                    template_bind_worlds.emplace(
                        eye_name, bind_world);
                }
                for (const auto& [source, target] : rb2_face_aliases) {
                    (void)source;
                    if (template_bind_worlds.count(target) == 0)
                        throw std::runtime_error(
                            "RB2 face-rig target is missing " + target);
                }
            }
            float max_template_stored_chain_delta = 0.0f;
            for (const auto& [name, transform] : template_transforms) {
                const auto& chain_world = template_bind_worlds.at(name);
                for (size_t value_index = 0;
                     value_index < chain_world.size(); ++value_index) {
                    max_template_stored_chain_delta = std::max(
                        max_template_stored_chain_delta,
                        std::abs(
                            chain_world[value_index] -
                            transform.world[value_index]));
                }
            }
            const auto resolve_template_bone =
                [&](const std::string& donor_bone) {
                    std::string current = donor_bone;
                    std::set<std::string> visited;
                    while (!current.empty() && visited.insert(current).second) {
                        if (retarget_rb2_face_rig) {
                            const auto alias = rb2_face_aliases.find(current);
                            if (alias != rb2_face_aliases.end())
                                return alias->second;
                        }
                        if (template_bind_worlds.count(current)) return current;
                        const auto donor_transform =
                            donor_transforms.find(current);
                        if (donor_transform == donor_transforms.end()) break;
                        current = donor_transform->second.parent;
                    }
                    return std::string{};
                };

            const std::set<std::string> render_types = {
                "Tex", "Mat", "Mesh", "Group"};
            size_t transforms_replaced = 0;
            size_t transforms_added = 0;
            size_t transforms_preserved = 0;
            size_t donor_transforms_skipped = 0;
            size_t mesh_bind_slots_rebased = 0;
            size_t mesh_bind_slots_preserved = 0;
            size_t meshes_with_bind_slots_preserved = 0;
            size_t mesh_bind_slots_remapped = 0;
            float max_bind_residual = 0.0f;
            size_t render_entries_added = 0;
            for (const auto& donor_entry : donor_directory.entries) {
                const auto key = std::make_pair(
                    donor_entry.type, donor_entry.name);
                const auto existing = template_entries.find(key);
                if (donor_entry.type == "Trans") {
                    if (rebind_template_rig) {
                        if (existing != template_entries.end())
                            ++transforms_preserved;
                        else
                            ++donor_transforms_skipped;
                        continue;
                    }
                    if (existing != template_entries.end()) {
                        directory.entries[existing->second].body_bytes =
                            donor_entry.body_bytes;
                        directory.entries[existing->second].size =
                            donor_entry.body_bytes.size();
                        ++transforms_replaced;
                    } else {
                        template_entries.emplace(
                            key, directory.entries.size());
                        directory.entries.push_back(donor_entry);
                        ++transforms_added;
                    }
                } else if (render_types.count(donor_entry.type)) {
                    if (donor_entry.type == "Mesh" &&
                        !enabled_mesh_names.count(donor_entry.name))
                        continue;
                    if (existing != template_entries.end())
                        throw std::runtime_error(
                            "merge-character-render-payload collision: " +
                            donor_entry.type + " " + donor_entry.name);
                    auto render_entry = donor_entry;
                    if (render_entry.type == "Group" &&
                        render_entry.name == donor_group) {
                        render_entry.body_bytes =
                            gh::milo_object::serialize_group12(isolated_group);
                        render_entry.size = render_entry.body_bytes.size();
                    } else if (render_entry.type == "Mesh" &&
                               rebind_template_rig) {
                        auto mesh = gh::milo_object::parse_mesh28(
                            render_entry.body_bytes,
                            static_cast<uint32_t>(directory.dir_version));
                        const auto is_hand_chain_bone =
                            [](const std::string& bone) {
                                static constexpr std::array<const char*, 6>
                                    markers = {
                                        "-hand.mesh", "-thumb", "-index",
                                        "-middlefinger", "-ringfinger",
                                        "-pinky"};
                                return std::any_of(
                                    markers.begin(), markers.end(),
                                    [&](const char* marker) {
                                        return bone.find(marker) !=
                                               std::string::npos;
                                    });
                            };
                        std::vector<bool> preserve_slot_bind_offsets(
                            mesh.bone_slots.size(),
                            preserve_donor_bind_offsets);
                        if (preserve_donor_hand_mesh_bind_offsets &&
                            !preserve_donor_bind_offsets) {
                            std::vector<std::vector<bool>> slot_links(
                                mesh.bone_slots.size(),
                                std::vector<bool>(
                                    mesh.bone_slots.size(), false));
                            for (size_t slot_index = 0;
                                 slot_index < mesh.bone_slots.size();
                                 ++slot_index) {
                                slot_links[slot_index][slot_index] = true;
                                preserve_slot_bind_offsets[slot_index] =
                                    is_hand_chain_bone(
                                        mesh.bone_slots[slot_index].bone);
                            }
                            for (const auto& face : mesh.faces) {
                                std::vector<bool> active_slots(
                                    mesh.bone_slots.size(), false);
                                for (const auto vertex_index : face) {
                                    if (vertex_index >= mesh.vertices.size())
                                        continue;
                                    const auto& vertex =
                                        mesh.vertices[vertex_index];
                                    for (size_t slot_index = 0;
                                         slot_index <
                                             mesh.bone_slots.size();
                                         ++slot_index) {
                                        if (std::abs(
                                                vertex.color_or_weights[
                                                    slot_index]) >
                                            1.0e-6f)
                                            active_slots[slot_index] = true;
                                    }
                                }
                                for (size_t a = 0; a < active_slots.size();
                                     ++a) {
                                    if (!active_slots[a]) continue;
                                    for (size_t b = a + 1;
                                         b < active_slots.size(); ++b) {
                                        if (!active_slots[b]) continue;
                                        slot_links[a][b] = true;
                                        slot_links[b][a] = true;
                                    }
                                }
                            }
                            std::vector<size_t> pending_slots;
                            for (size_t slot_index = 0;
                                 slot_index <
                                     preserve_slot_bind_offsets.size();
                                 ++slot_index) {
                                if (preserve_slot_bind_offsets[slot_index])
                                    pending_slots.push_back(slot_index);
                            }
                            for (size_t pending_index = 0;
                                 pending_index < pending_slots.size();
                                 ++pending_index) {
                                const size_t source_slot =
                                    pending_slots[pending_index];
                                for (size_t linked_slot = 0;
                                     linked_slot < slot_links.size();
                                     ++linked_slot) {
                                    if (!slot_links[source_slot]
                                                   [linked_slot] ||
                                        preserve_slot_bind_offsets[
                                            linked_slot])
                                        continue;
                                    preserve_slot_bind_offsets[linked_slot] =
                                        true;
                                    pending_slots.push_back(linked_slot);
                                }
                            }
                        }
                        if (std::any_of(
                                preserve_slot_bind_offsets.begin(),
                                preserve_slot_bind_offsets.end(),
                                [](bool preserve) { return preserve; }))
                            ++meshes_with_bind_slots_preserved;
                        for (size_t slot_index = 0;
                             slot_index < mesh.bone_slots.size();
                             ++slot_index) {
                            auto& slot = mesh.bone_slots[slot_index];
                            if (slot.bone.empty()) continue;
                            const std::string template_bone =
                                resolve_template_bone(slot.bone);
                            if (template_bone.empty())
                                throw std::runtime_error(
                                    "merge-character-render-payload cannot "
                                    "map donor bone to template rig: " +
                                    slot.bone);
                            if (template_bone != slot.bone) {
                                slot.bone = template_bone;
                                ++mesh_bind_slots_remapped;
                            }
                            if (preserve_slot_bind_offsets[slot_index]) {
                                ++mesh_bind_slots_preserved;
                            } else {
                                slot.offset = multiply_affine_transform(
                                    invert_affine_transform(
                                        template_bind_worlds.at(
                                            template_bone)),
                                    mesh.transformable.world);
                                const auto reconstructed_world =
                                    multiply_affine_transform(
                                        slot.offset,
                                        template_bind_worlds.at(
                                            template_bone));
                                for (size_t value_index = 0;
                                     value_index < reconstructed_world.size();
                                     ++value_index) {
                                    max_bind_residual = std::max(
                                        max_bind_residual,
                                        std::abs(
                                            reconstructed_world[value_index] -
                                            mesh.transformable
                                                .world[value_index]));
                                }
                                ++mesh_bind_slots_rebased;
                            }
                        }
                        render_entry.body_bytes =
                            gh::milo_object::serialize_mesh28(
                                mesh,
                                static_cast<uint32_t>(directory.dir_version));
                        render_entry.size = render_entry.body_bytes.size();
                    } else if (render_entry.type == "Mesh" &&
                               retarget_rb2_face_rig) {
                        auto mesh = gh::milo_object::parse_mesh28(
                            render_entry.body_bytes,
                            static_cast<uint32_t>(directory.dir_version));
                        for (auto& slot : mesh.bone_slots) {
                            const auto alias = rb2_face_aliases.find(slot.bone);
                            if (alias == rb2_face_aliases.end()) continue;
                            slot.bone = alias->second;
                            ++mesh_bind_slots_remapped;
                        }
                        render_entry.body_bytes =
                            gh::milo_object::serialize_mesh28(
                                mesh,
                                static_cast<uint32_t>(directory.dir_version));
                        render_entry.size = render_entry.body_bytes.size();
                    }
                    template_entries.emplace(key, directory.entries.size());
                    directory.entries.push_back(std::move(render_entry));
                    ++render_entries_added;
                }
            }
            if (render_entries_added == 0 ||
                template_entries.count({"Group", donor_group}) != 1)
                throw std::runtime_error(
                    "merge-character-render-payload donor render graph missing");
            for (const auto& [key, body] : template_controller_bodies) {
                const auto merged_entry = template_entries.find(key);
                if (merged_entry == template_entries.end() ||
                    directory.entries[merged_entry->second].body_bytes != body)
                    throw std::runtime_error(
                        "merge-character-render-payload changed template "
                        "controller: " + key.first + " " + key.second);
            }

            auto& target = character.character;
            const auto& donor_target = donor_character.character;
            for (auto& lod : target.lods) lod.group = donor_group;
            target.sphere_base = donor_group;
            target.shadow = donor_target.shadow;
            target.self_shadow = donor_target.self_shadow;
            target.render_directory.drawable.sphere =
                donor_target.render_directory.drawable.sphere;
            directory.dir_body_bytes =
                gh::milo_object::serialize_band_character1(character);

            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "merged character directory round trip differs");
            const auto target_bytes = serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "merged character container round trip differs");
            write_file(output, target_bytes);
            std::cout << "character_template=" << input.string()
                      << " donor=" << donor_path.string()
                      << " group=" << donor_group
                      << " meshes_enabled=" << enabled_mesh_names.size()
                      << " transforms_replaced=" << transforms_replaced
                      << " transforms_added=" << transforms_added
                      << " transforms_preserved=" << transforms_preserved
                      << " donor_transforms_skipped="
                      << donor_transforms_skipped
                      << " mesh_bind_slots_rebased="
                      << mesh_bind_slots_rebased
                      << " mesh_bind_slots_preserved="
                      << mesh_bind_slots_preserved
                      << " meshes_with_bind_slots_preserved="
                      << meshes_with_bind_slots_preserved
                      << " mesh_bind_slots_remapped="
                      << mesh_bind_slots_remapped
                      << " source_proportion_face_targets_repositioned="
                      << source_proportion_face_targets_repositioned
                      << " max_bind_residual=" << max_bind_residual
                      << " max_template_stored_chain_delta="
                      << max_template_stored_chain_delta
                      << " controller_entries_preserved="
                      << template_controller_bodies.size()
                      << " template_meshes_hidden="
                      << template_meshes_hidden
                      << " template_meshes_unparsed="
                      << template_meshes_unparsed
                      << " template_transforms_unparsed="
                      << template_transforms_unparsed
                      << " donor_transforms_unparsed="
                      << donor_transforms_unparsed
                      << " render_entries_added=" << render_entries_added
                      << " entries=" << directory.entries.size()
                      << " bytes=" << target_bytes.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "patch-face-config") {
        try {
            if (argc != 5 && argc != 7)
                usage();
            if (std::string(argv[3]) != "--out")
                usage();
            if (argc == 7 &&
                std::string(argv[5]) != "--dta")
                usage();
            const auto patch =
                gh::milo_convert::
                    patch_gh2_rnd_objects_for_gh1_faces(
                        gh::milo::read_file(input.string()));
            write_file(argv[4], patch.bytes);
            if (argc == 7)
                write_text(argv[6], patch.dta);
            std::cout
                << "types_added=" << patch.types_added
                << " bytes=" << patch.bytes.size()
                << " output=" << argv[4] << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "patch-face-midi-config") {
        try {
            if (argc != 5 && argc != 7)
                usage();
            if (std::string(argv[3]) != "--out")
                usage();
            if (argc == 7 &&
                std::string(argv[5]) != "--dta")
                usage();
            const auto patch =
                gh::milo_convert::
                    patch_gh2_midi_parsers_for_gh1_singer_face(
                        gh::milo::read_file(input.string()));
            write_file(argv[4], patch.bytes);
            if (argc == 7)
                write_text(argv[6], patch.dta);
            std::cout
                << "parsers_added=" << patch.parsers_added
                << " bytes=" << patch.bytes.size()
                << " output=" << argv[4] << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "patch-face-character-config") {
        try {
            if (argc != 5 && argc != 7)
                usage();
            if (std::string(argv[3]) != "--out")
                usage();
            if (argc == 7 &&
                std::string(argv[5]) != "--dta")
                usage();
            const auto patch =
                gh::milo_convert::
                    patch_gh2_char_objects_for_gh1_singer_face(
                        gh::milo::read_file(input.string()));
            write_file(argv[4], patch.bytes);
            if (argc == 7)
                write_text(argv[6], patch.dta);
            std::cout
                << "handlers_added=" << patch.handlers_added
                << " bytes=" << patch.bytes.size()
                << " output=" << argv[4] << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "translate-singer-face") {
        fs::path output;
        fs::path voc;
        for (int index = 3; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--out" && index + 1 < argc)
                output = argv[++index];
            else if (argument == "--voc" && index + 1 < argc)
                voc = argv[++index];
            else
                usage();
        }
        if (output.empty()) usage();
        try {
            const auto midi =
                gh::milo::read_file(input.string());
            std::vector<
                gh::milo_convert::SingerFaceTickSpan> spans;
            std::string mode;
            if (voc.empty()) {
                spans =
                    gh::milo_convert::
                        extract_gh1_singer_face_spans(midi);
                mode = "gh1-pitch108";
            } else {
                const auto animation =
                    gh::milo_convert::
                        parse_gh2_facefx_animation(
                            gh::milo::read_file(voc.string()));
                spans =
                    gh::milo_convert::
                        map_singer_face_times_to_midi(
                            midi,
                            gh::milo_convert::
                                derive_gh1_singer_open_spans(
                                    animation));
                mode = "gh2-facefx";
            }
            const auto patched =
                gh::milo_convert::
                    append_gh1_singer_face_track(midi, spans);
            write_file(output, patched);
            std::cout
                << "mode=" << mode
                << " spans=" << spans.size()
                << " source_bytes=" << midi.size()
                << " output_bytes=" << patched.size()
                << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr
                << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "export-character-snapshot") {
        if (argc != 5 || std::string(argv[3]) != "--out") usage();
        try {
            const fs::path output = argv[4];
            const auto snapshot = build_character_snapshot(input);
            write_file(output, snapshot);
            std::cout << "character_snapshot bytes=" << snapshot.size()
                      << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "inspect-character") {
        bool print_controllers = false;
        bool print_entries = false;
        bool print_transforms = false;
        bool print_meshes = false;
        bool print_mesh_vertices = false;
        for (int arg_index = 3; arg_index < argc; ++arg_index) {
            const std::string option = argv[arg_index];
            if (option == "--entries") {
                print_entries = true;
            } else if (option == "--controllers") {
                print_controllers = true;
            } else if (option == "--transforms") {
                print_transforms = true;
            } else if (option == "--meshes") {
                print_meshes = true;
            } else if (option == "--mesh-vertices") {
                print_meshes = true;
                print_mesh_vertices = true;
            } else {
                usage();
            }
        }
        try {
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            gh::milo_object::Character9 character;
            if (directory.dir_type == "BandCharacter") {
                character =
                    gh::milo_object::parse_band_character1(
                        directory.dir_body_bytes)
                        .character;
            } else if (directory.dir_type == "Character") {
                character = gh::milo_object::parse_character9(
                    directory.dir_body_bytes);
            } else {
                throw std::runtime_error(
                    "directory is not a Character or BandCharacter");
            }
            std::cout << "type=" << directory.dir_type
                      << " name=" << directory.dir_name
                      << " object_type="
                      << character.render_directory.object_directory
                             .object_fields.type
                      << " lods=" << character.lods.size()
                      << " shadow=" << character.shadow
                      << " self_shadow=" << character.self_shadow
                      << " sphere_base=" << character.sphere_base
                      << '\n';
            for (size_t index = 0;
                 index < character.lods.size(); ++index) {
                std::cout << "lod[" << index << "] screen_size="
                          << character.lods[index].screen_size
                          << " group="
                          << character.lods[index].group << '\n';
            }
            if (print_entries) {
                for (const auto& entry : directory.entries)
                    std::cout << "Entry type=" << entry.type
                              << " name=" << entry.name
                              << " bytes=" << entry.body_bytes.size()
                              << '\n';
            }
            if (print_transforms) {
                const auto print_transform =
                    [](const char* type, const std::string& name,
                       const gh::milo_object::Transformable9& transform) {
                        std::cout << type << ' ' << name
                                  << " parent=" << transform.parent
                                  << " local=[";
                        for (size_t value_index = 0;
                             value_index < transform.local.size();
                             ++value_index) {
                            if (value_index) std::cout << ',';
                            std::cout << transform.local[value_index];
                        }
                        std::cout << "] world=[";
                        for (size_t value_index = 0;
                             value_index < transform.world.size();
                             ++value_index) {
                            if (value_index) std::cout << ',';
                            std::cout << transform.world[value_index];
                        }
                        std::cout << "]\n";
                    };
                for (const auto& entry : directory.entries) {
                    if (entry.type == "Trans") {
                        const auto transform =
                            gh::milo_object::parse_trans9(entry.body_bytes);
                        gh::milo_object::Transformable9 fields;
                        fields.revision = transform.revision;
                        fields.local = transform.local;
                        fields.world = transform.world;
                        fields.constraint = transform.constraint;
                        fields.target = transform.target;
                        fields.preserve_scale = transform.preserve_scale;
                        fields.parent = transform.parent;
                        print_transform(
                            entry.type.c_str(), entry.name, fields);
                    } else if (entry.type == "Mesh") {
                        const auto mesh = gh::milo_object::parse_mesh28(
                            entry.body_bytes,
                            static_cast<uint32_t>(directory.dir_version));
                        print_transform(
                            entry.type.c_str(), entry.name,
                            mesh.transformable);
                    }
                }
            }
            if (print_meshes) {
                for (const auto& entry : directory.entries) {
                    if (entry.type != "Mesh") continue;
                    const auto mesh = gh::milo_object::parse_mesh28(
                        entry.body_bytes,
                        static_cast<uint32_t>(directory.dir_version));
                    std::array<float, 3> minimum = {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 3> maximum = {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };
                    for (const auto& vertex : mesh.vertices) {
                        for (size_t axis = 0; axis < 3; ++axis) {
                            minimum[axis] = std::min(
                                minimum[axis], vertex.position[axis]);
                            maximum[axis] = std::max(
                                maximum[axis], vertex.position[axis]);
                        }
                    }
                    float maximum_face_edge = 0.0f;
                    size_t faces_with_edge_over_5 = 0;
                    size_t faces_with_edge_over_10 = 0;
                    for (const auto& face : mesh.faces) {
                        float face_maximum_edge = 0.0f;
                        for (size_t corner = 0; corner < 3; ++corner) {
                            const auto& a = mesh.vertices[face[corner]];
                            const auto& b =
                                mesh.vertices[face[(corner + 1) % 3]];
                            float length_squared = 0.0f;
                            for (size_t axis = 0; axis < 3; ++axis) {
                                const float delta =
                                    a.position[axis] - b.position[axis];
                                length_squared += delta * delta;
                            }
                            face_maximum_edge = std::max(
                                face_maximum_edge, std::sqrt(length_squared));
                        }
                        maximum_face_edge = std::max(
                            maximum_face_edge, face_maximum_edge);
                        if (face_maximum_edge > 5.0f)
                            ++faces_with_edge_over_5;
                        if (face_maximum_edge > 10.0f)
                            ++faces_with_edge_over_10;
                    }
                    if (mesh.vertices.empty()) {
                        minimum.fill(0.0f);
                        maximum.fill(0.0f);
                    }
                    std::cout << "MeshSkin " << entry.name
                              << " vertices=" << mesh.vertices.size()
                              << " faces=" << mesh.faces.size()
                              << " material=" << mesh.material
                              << " showing=" << mesh.drawable.showing
                              << " has_bones=" << mesh.has_bones
                              << " mutable_flags=" << mesh.mutable_flags
                              << " groups=" << mesh.group_sizes.size()
                              << " cached_sections="
                              << mesh.group_sections.size()
                              << " geometry_owner=" << mesh.geometry_owner
                              << " max_face_edge=" << maximum_face_edge
                              << " faces_edge_gt_5=" << faces_with_edge_over_5
                              << " faces_edge_gt_10=" << faces_with_edge_over_10
                              << " bounds_min=[" << minimum[0] << ','
                              << minimum[1] << ',' << minimum[2] << ']'
                              << " bounds_max=[" << maximum[0] << ','
                              << maximum[1] << ',' << maximum[2] << "]\n";
                    std::cout << "MeshGroups " << entry.name
                              << " sizes=";
                    for (size_t group_index = 0;
                         group_index < mesh.group_sizes.size();
                         ++group_index) {
                        if (group_index != 0) std::cout << ',';
                        std::cout << static_cast<uint32_t>(
                            mesh.group_sizes[group_index]);
                    }
                    std::cout << " sections=";
                    for (size_t section_index = 0;
                         section_index < mesh.group_sections.size();
                         ++section_index) {
                        if (section_index != 0) std::cout << ',';
                        const auto& section =
                            mesh.group_sections[section_index];
                        std::cout
                            << section.cumulative_strip_lengths.size()
                            << ':' << section.strip_runs.size();
                        if (!section.cumulative_strip_lengths.empty()) {
                            std::cout
                                << ':'
                                << section.cumulative_strip_lengths.front()
                                << ':'
                                << section.cumulative_strip_lengths.back();
                        }
                    }
                    std::cout << '\n';
                    if (print_mesh_vertices) {
                        const auto old_precision = std::cout.precision();
                        std::cout << std::setprecision(
                            std::numeric_limits<float>::max_digits10);
                        for (size_t vertex_index = 0;
                             vertex_index < mesh.vertices.size();
                             ++vertex_index) {
                            const auto& vertex = mesh.vertices[vertex_index];
                            std::cout << "MeshSkinVertex " << entry.name
                                      << " index=" << vertex_index
                                      << " position=[";
                            for (size_t axis = 0; axis < 3; ++axis) {
                                if (axis) std::cout << ',';
                                std::cout << vertex.position[axis];
                            }
                            std::cout << "] normal=[";
                            for (size_t axis = 0; axis < 3; ++axis) {
                                if (axis) std::cout << ',';
                                std::cout << vertex.normal[axis];
                            }
                            std::cout << "] weights=[";
                            for (size_t slot_index = 0;
                                 slot_index < mesh.bone_slots.size();
                                 ++slot_index) {
                                if (slot_index) std::cout << ',';
                                std::cout <<
                                    vertex.color_or_weights[slot_index];
                            }
                            std::cout << "] bones=[";
                            for (size_t slot_index = 0;
                                 slot_index < mesh.bone_slots.size();
                                 ++slot_index) {
                                if (slot_index) std::cout << ',';
                                std::cout << mesh.bone_slots[slot_index].bone;
                            }
                            std::cout << "]\n";
                        }
                        std::cout.precision(old_precision);
                    }
                    if (!mesh.has_bones) continue;
                    for (size_t slot_index = 0;
                         slot_index < mesh.bone_slots.size(); ++slot_index) {
                        const auto& slot = mesh.bone_slots[slot_index];
                        if (slot.bone.empty()) continue;
                        double weight_sum = 0.0;
                        size_t weighted_vertex_count = 0;
                        std::array<double, 3> weighted_position{};
                        for (const auto& vertex : mesh.vertices) {
                            const double weight =
                                vertex.color_or_weights[slot_index];
                            if (weight <= 0.0) continue;
                            weight_sum += weight;
                            ++weighted_vertex_count;
                            for (size_t axis = 0; axis < 3; ++axis)
                                weighted_position[axis] +=
                                    vertex.position[axis] * weight;
                        }
                        std::cout << "MeshSkinSlot " << entry.name
                                  << " slot=" << slot_index
                                  << " bone=" << slot.bone
                                  << " weighted_vertices="
                                  << weighted_vertex_count
                                  << " weight_sum=" << weight_sum
                                  << " weighted_center=[";
                        for (size_t axis = 0; axis < 3; ++axis) {
                            if (axis) std::cout << ',';
                            std::cout << (weight_sum > 0.0
                                ? weighted_position[axis] / weight_sum
                                : 0.0);
                        }
                        std::cout << "] offset=[";
                        for (size_t value_index = 0;
                             value_index < slot.offset.size();
                             ++value_index) {
                            if (value_index) std::cout << ',';
                            std::cout << slot.offset[value_index];
                        }
                        std::cout << "]\n";
                    }
                }
            }
            for (const auto& entry : directory.entries) {
                if (entry.type == "Tex") {
                    const auto tex =
                        gh::milo_object::parse_tex10(entry.body_bytes);
                    std::cout << "Tex " << entry.name
                              << " width=" << tex.width
                              << " height=" << tex.height
                              << " bpp=" << tex.bits_per_pixel
                              << " use_external=" << tex.use_external
                              << " has_bitmap=" << tex.has_bitmap
                              << " bitmap_bpp="
                              << static_cast<int>(
                                     tex.bitmap.bits_per_pixel)
                              << " bitmap_encoding="
                              << tex.bitmap.encoding
                              << " bitmap_bytes="
                              << tex.bitmap.data.size()
                              << " external_path="
                              << tex.external_path << '\n';
                }
            }
            if (print_controllers) {
                for (const auto& entry : directory.entries) {
                    if (entry.type == "CharDriver") {
                        const auto driver =
                            gh::milo_object::parse_char_driver3(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type=" << driver.object_fields.type
                                  << " weight=" << driver.weightable.weight
                                  << " owner="
                                  << driver.weightable.weight_owner
                                  << " bones=" << driver.bones
                                  << " clips=" << driver.clips
                                  << " realign=" << driver.realign
                                  << '\n';
                    } else if (entry.type == "CharDriverMidi") {
                        const auto driver =
                            gh::milo_object::parse_char_driver_midi3(
                                entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type="
                            << driver.driver.object_fields.type
                            << " weight="
                            << driver.driver.weightable.weight
                            << " owner="
                            << driver.driver.weightable.weight_owner
                            << " bones=" << driver.driver.bones
                            << " clips=" << driver.driver.clips
                            << " default=" << driver.default_clip
                            << '\n';
                    } else if (entry.type == "CharIKRod") {
                        const auto rod =
                            gh::milo_object::parse_char_ik_rod2(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << rod.object_fields.type
                                  << " left=" << rod.left_end
                                  << " right=" << rod.right_end
                                  << " position=" << rod.dest_pos
                                  << " side=" << rod.side_axis
                                  << " vertical=" << rod.vertical
                                  << " dest=" << rod.dest
                                  << " transform=";
                        for (size_t index = 0;
                             index < rod.transform.size();
                             ++index) {
                            if (index) std::cout << ',';
                            std::cout << rod.transform[index];
                        }
                        std::cout << '\n';
                    } else if (
                        entry.type == "CharPosConstraint") {
                        const auto constraint =
                            gh::milo_object::
                                parse_char_pos_constraint2(
                                    entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type="
                            << constraint.object_fields.type
                            << " source=" << constraint.source
                            << " targets=";
                        for (const auto& target :
                             constraint.targets)
                            std::cout << target << ',';
                        std::cout << '\n';
                    } else if (entry.type == "CharServoBone") {
                        const auto servo =
                            gh::milo_object::
                                parse_char_servo_bone2(
                                    entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " rev=" << servo.revision
                                  << " type="
                                  << servo.object_fields.type
                                  << " clip_type="
                                  << servo.clip_type << '\n';
                    } else if (entry.type == "CharForeTwist") {
                        const auto twist =
                            gh::milo_object::
                                parse_char_fore_twist4(
                                    entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << twist.object_fields.type
                                  << " offset=" << twist.offset
                                  << " hand=" << twist.hand
                                  << " twist2=" << twist.twist2
                                  << '\n';
                    } else if (entry.type == "CharUpperTwist") {
                        const auto twist =
                            gh::milo_object::
                                parse_char_upper_twist1(
                                    entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << twist.object_fields.type
                                  << " upper=" << twist.upper_arm
                                  << " twist1=" << twist.twist1
                                  << " twist2=" << twist.twist2
                                  << '\n';
                    } else if (entry.type == "CharIKHand") {
                        const auto hand =
                            gh::milo_object::parse_char_ik_hand2(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << hand.object_fields.type
                                  << " weight=" << hand.weightable.weight
                                  << " owner="
                                  << hand.weightable.weight_owner
                                  << " hand=" << hand.hand
                                  << " target=" << hand.target
                                  << " orientation="
                                  << hand.orientation
                                  << " stretch=" << hand.stretch
                                  << " scalable=" << hand.scalable
                                  << '\n';
                    } else if (entry.type == "CharIKMidi") {
                        const auto midi =
                            gh::milo_object::parse_char_ik_midi4(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << midi.object_fields.type
                                  << " bone=" << midi.bone << '\n';
                    } else if (
                        entry.type == "CharWeightSetter") {
                        const auto setter =
                            gh::milo_object::
                                parse_char_weight_setter2(
                                    entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type=" << setter.object_fields.type
                            << " weight=" << setter.weightable.weight
                            << " owner="
                            << setter.weightable.weight_owner
                            << " driver=" << setter.driver
                            << " flags=" << setter.flags << '\n';
                    } else if (entry.type == "CharLookAt") {
                        const auto look =
                            gh::milo_object::parse_char_look_at2(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << look.object_fields.type
                                  << " source=" << look.source
                                  << " pivot=" << look.pivot
                                  << " target=" << look.target
                                  << " half_time=" << look.half_time
                                  << " yaw=" << look.min_yaw << ','
                                  << look.max_yaw
                                  << " pitch=" << look.min_pitch << ','
                                  << look.max_pitch << '\n';
                    } else if (entry.type == "CharEyes") {
                        const auto eyes =
                            gh::milo_object::parse_char_eyes3(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << eyes.object_fields.type
                                  << " eyes=";
                        for (const auto& eye : eyes.eyes)
                            std::cout << eye << ',';
                        std::cout << " legacy="
                                  << eyes.legacy_transform << '\n';
                    } else if (entry.type == "CharWalk") {
                        const auto walk =
                            gh::milo_object::parse_char_walk1(
                                entry.body_bytes);
                        std::cout << entry.type << ' ' << entry.name
                                  << " type="
                                  << walk.object_fields.type << '\n';
                    } else if (
                        entry.type == "FaceFxLipSyncServo") {
                        const auto servo =
                            gh::milo_object::
                                parse_facefx_lip_sync_servo5(
                                    entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type=" << servo.object_fields.type
                            << " weight=" << servo.weightable.weight
                            << " facefx=" << servo.facefx_path
                            << " visemes=" << servo.viseme_milo
                            << " targets=";
                        for (const auto& target : servo.targets)
                            std::cout
                                << target.object << ':'
                                << target.property_type << ':'
                                << target.property << ',';
                        std::cout << '\n';
                    } else if (entry.type == "OutfitLoader") {
                        const auto loader =
                            gh::milo_object::parse_outfit_loader1(
                                entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type=" << loader.object_fields.type
                            << " directory=" << loader.directory
                            << " categories=" << loader.categories.size();
                        for (size_t category_index = 0;
                             category_index < loader.categories.size();
                             ++category_index) {
                            const auto& category =
                                loader.categories[category_index];
                            std::cout
                                << " category[" << category_index
                                << "]=" << static_cast<int>(
                                    category.selected)
                                << ',' << static_cast<int>(
                                    category.shown)
                                << ',' << category.outfits.size()
                                << ':';
                            for (const auto& outfit :
                                 category.outfits)
                                std::cout
                                    << static_cast<int>(outfit.hide)
                                    << static_cast<int>(outfit.desire)
                                    << static_cast<int>(outfit.exclude)
                                    << ',';
                        }
                        std::cout << '\n';
                    } else if (entry.type == "AnimFilter") {
                        const auto filter =
                            gh::milo_object::parse_anim_filter1(
                                entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type=" << filter.object_fields.type
                            << " anim=" << filter.anim
                            << " scale=" << filter.scale
                            << " offset=" << filter.offset
                            << " start=" << filter.start
                            << " end=" << filter.end
                            << " filter_type=" << filter.type
                            << " period=" << filter.period << '\n';
                    } else if (entry.type == "EventTrigger") {
                        const auto trigger =
                            gh::milo_object::parse_event_trigger8(
                                entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " type=" << trigger.object_fields.type
                            << " event=" << trigger.trigger_event
                            << " animations=";
                        for (const auto& animation :
                             trigger.animations)
                            std::cout
                                << animation.animation << ':'
                                << animation.blend << ':'
                                << animation.wait << ':'
                                << animation.delay << ',';
                        std::cout << " sounds=";
                        for (const auto& sound : trigger.sounds)
                            std::cout << sound << ',';
                        std::cout << " shows=";
                        for (const auto& show : trigger.shows)
                            std::cout << show << ',';
                        std::cout << " hides=";
                        for (const auto& hide : trigger.legacy_hides)
                            std::cout << hide << ',';
                        std::cout << " enable=";
                        for (const auto& event :
                             trigger.enable_events)
                            std::cout << event << ',';
                        std::cout << " disable=";
                        for (const auto& event :
                             trigger.disable_events)
                            std::cout << event << ',';
                        std::cout << " wait_for=";
                        for (const auto& event :
                             trigger.wait_for_events)
                            std::cout << event << ',';
                        std::cout << " next=" << trigger.next_link
                                  << " proxies=";
                        for (const auto& proxy :
                             trigger.proxy_calls)
                            std::cout << proxy.proxy << ':'
                                      << proxy.call << ',';
                        std::cout << '\n';
                    } else if (entry.type == "Morph") {
                        const auto morph =
                            gh::milo_object::parse_morph4(
                                entry.body_bytes);
                        std::cout
                            << entry.type << ' ' << entry.name
                            << " target=" << morph.target
                            << " normals=" << morph.normals
                            << " spline=" << morph.spline
                            << " intensity=" << morph.intensity
                            << " poses=";
                        for (const auto& pose : morph.poses) {
                            std::cout << pose.mesh << '[';
                            for (const auto& key : pose.keys)
                                std::cout << key.frame << ':'
                                          << key.value << ',';
                            std::cout << "],";
                        }
                        std::cout << '\n';
                    }
                }
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "inspect-skeleton") {
        try {
            const bool print_all =
                argc == 4 && std::string(argv[3]) == "--all";
            if (argc > 4 || (argc == 4 && !print_all))
                usage();
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            const auto converted =
                gh::milo_convert::
                    convert_gh1_directory_to_gh2_rnddir(
                        directory, "skeleton_inspect");
            std::map<std::string, gh::milo_object::Mesh28>
                converted_meshes;
            for (const auto& entry : converted.directory.entries) {
                if (entry.type == "Mesh")
                    converted_meshes.emplace(
                        entry.name,
                        gh::milo_object::parse_mesh28(
                            entry.body_bytes));
                if (print_all && entry.type == "Group") {
                    const auto group =
                        gh::milo_object::parse_group12(
                            entry.body_bytes);
                    std::cout << "Group " << entry.name
                              << "\tobjects=";
                    for (size_t index = 0;
                         index < group.objects.size();
                         ++index) {
                        if (index) std::cout << ',';
                        std::cout << group.objects[index];
                    }
                    std::cout << '\n';
                }
                if (print_all && entry.type == "Morph") {
                    const auto morph =
                        gh::milo_object::parse_morph4(
                            entry.body_bytes);
                    std::cout << "Morph " << entry.name
                              << "\ttarget=" << morph.target
                              << "\tnormals=" << morph.normals
                              << "\tspline=" << morph.spline
                              << "\tintensity=" << morph.intensity
                              << "\tposes=";
                    for (const auto& pose : morph.poses) {
                        std::cout << pose.mesh << '[';
                        for (const auto& key : pose.keys)
                            std::cout << key.frame << ':'
                                      << key.value << ',';
                        std::cout << "],";
                    }
                    std::cout << '\n';
                }
            }
            for (const auto& entry : directory.entries) {
                if (entry.type != "Mesh") continue;
                const auto mesh =
                    gh::milo_object::parse_mesh(entry.body_bytes);
                if (!print_all &&
                    (!mesh.vertices.empty() || !mesh.faces.empty()))
                    continue;
                const auto effective =
                    converted_meshes.find(entry.name);
                if (effective == converted_meshes.end())
                    throw std::runtime_error(
                        "converted skeleton mesh missing");
                std::cout << entry.name
                          << "\tparent="
                          << effective->second.transformable.parent
                          << "\tconstraint="
                          << effective->second.transformable.constraint
                          << "\ttranslation="
                          << effective->second.transformable.local[9]
                          << ','
                          << effective->second.transformable.local[10]
                          << ','
                          << effective->second.transformable.local[11]
                          << "\tvertices=" << mesh.vertices.size()
                          << "\tfaces=" << mesh.faces.size()
                          << "\tmaterial="
                          << effective->second.material
                          << "\tgeometry_owner="
                          << effective->second.geometry_owner;
                if (print_all) {
                    std::cout << "\tlocal=";
                    for (size_t index = 0;
                         index < effective->second.transformable.local.size();
                         ++index) {
                        if (index) std::cout << ',';
                        std::cout <<
                            effective->second.transformable.local[index];
                    }
                }
                std::cout << '\n';
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "inspect-groups") {
        try {
            if (argc != 3) usage();
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            for (const auto& entry : directory.entries) {
                if (entry.type != "Group") continue;
                const auto group =
                    gh::milo_object::parse_group12(entry.body_bytes);
                std::cout << entry.name << "\tobjects=";
                for (size_t index = 0; index < group.objects.size();
                     ++index) {
                    if (index) std::cout << ',';
                    std::cout << group.objects[index];
                }
                std::cout << "\tenvironment=" << group.environment
                          << "\tlod=" << group.lod << '\n';
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "sample-clip") {
        if (argc != 5 && argc != 6) usage();
        const std::string clip_name = argv[3];
        const std::string sample_argument = argv[4];
        const bool sample_all = sample_argument == "all";
        const uint32_t sample_index = sample_all
            ? 0
            : static_cast<uint32_t>(std::stoul(sample_argument));
        const std::string filter = argc == 6 ? argv[5] : std::string();
        try {
            std::cout << std::setprecision(
                std::numeric_limits<float>::max_digits10);
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            const gh::milo::Entry* entry = nullptr;
            for (const auto& candidate : directory.entries) {
                if (candidate.type == "CharClipSamples" &&
                    candidate.name == clip_name) {
                    entry = &candidate;
                    break;
                }
            }
            if (!entry)
                throw std::runtime_error(
                    "CharClipSamples not found: " + clip_name);
            const auto body =
                gh::milo_object::parse_char_clip_samples10(
                    entry->body_bytes);
            auto print_set =
                [&](const char* label,
                    const gh::milo_object::CharBonesSamples10& source,
                    uint32_t requested_sample) {
                    if (source.channels.empty()) return;
                    gh::acp::ChannelSet set;
                    set.channels = source.channels;
                    set.sample_count = source.sample_count;
                    set.compression = source.compression;
                    set.sample_bytes = source.sample_bytes;
                    for (const auto& channel : set.channels)
                        set.frame_size += gh::acp::channel_file_size(
                            channel, set.compression);
                    for (size_t index = 0;
                         index < set.channels.size(); ++index) {
                        if (!filter.empty() &&
                            set.channels[index].find(filter) ==
                                std::string::npos) {
                            continue;
                        }
                        const auto sample =
                            gh::acp::decode_channel_sample(
                                set, index, requested_sample);
                        std::cout << label << "\tsample="
                                  << requested_sample << "\t"
                                  << set.channels[index];
                        for (size_t component = 0;
                             component < sample.component_count;
                             ++component) {
                            std::cout
                                << (component == 0 ? "\t" : ",")
                                << sample.values[component];
                        }
                        std::cout << '\n';
                    }
                };
            if (sample_all) {
                for (uint32_t index = 0; index < body.full.sample_count;
                     ++index) {
                    print_set("full", body.full, index);
                }
            } else {
                print_set("full", body.full, sample_index);
            }
            print_set("one", body.one, 0);
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "inspect-clipset") {
        bool print_channels = false;
        bool print_events = false;
        for (int index = 3; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--channels")
                print_channels = true;
            else if (argument == "--events")
                print_events = true;
            else
                usage();
        }
        try {
            const auto container = gh::milo::parse_container(
                gh::milo::read_file(input.string()));
            const auto directory = gh::milo::parse_directory(
                gh::milo::container_payload(container));
            if (directory.dir_type != "CharClipSet")
                throw std::runtime_error(
                    "directory is not a CharClipSet");
            size_t clip_count = 0;
            for (const auto& entry : directory.entries)
                if (entry.type == "CharClipSamples") ++clip_count;
            const auto root =
                gh::milo_object::parse_char_clip_set14(
                    directory.dir_body_bytes,
                    static_cast<uint32_t>(clip_count));
            std::cout
                << "name=" << directory.dir_name
                << " clips=" << clip_count
                << " entries=" << directory.entries.size()
                << " object_type=" << root.object_directory.object_fields.type
                << " viewports=" << root.object_directory.viewports.size()
                << " current_viewport="
                << root.object_directory.current_viewport
                << " proxy=" << root.object_directory.proxy_path
                << " subdirs="
                << root.object_directory.subdirectories.size()
                << " blend_width=" << root.blend_width
                << " play_flags=" << root.play_flags
                << " move_self=" << root.move_self
                << " recenter_targets="
                << root.recenter_targets.size()
                << " recenter_average="
                << root.recenter_average.size()
                << " recenter_slide=" << root.recenter_slide
                << " legacy_type=" << root.legacy_type
                << " legacy_type_version="
                << root.legacy_type_version
                << '\n';
            for (const auto& value : root.recenter_targets)
                std::cout << "recenter_target\t" << value << '\n';
            for (const auto& value : root.recenter_average)
                std::cout << "recenter_average\t" << value << '\n';
            for (size_t index = 0;
                 index < root.object_directory.viewports.size();
                 ++index) {
                const auto& viewport =
                    root.object_directory.viewports[index];
                std::cout << "viewport\t" << index;
                for (float value : viewport.transform)
                    std::cout << '\t' << value;
                std::cout << '\t' << viewport.legacy_value << '\n';
            }
            for (const auto& entry : directory.entries) {
                if (entry.type != "CharClipSamples")
                    std::cout << "object\t" << entry.type << '\t'
                              << entry.name << '\n';
                if (entry.type == "CharClipGroup") {
                    const auto group =
                        gh::milo_object::parse_char_clip_group1(
                            entry.body_bytes);
                    std::cout << "group\t" << entry.name
                              << "\twhich=" << group.which
                              << "\tclips=";
                    for (size_t index = 0;
                         index < group.clips.size(); ++index) {
                        if (index) std::cout << ',';
                        std::cout << group.clips[index];
                    }
                    std::cout << '\n';
                } else if (entry.type == "CharBone") {
                    const auto bone =
                        gh::milo_object::parse_char_bone2(
                            entry.body_bytes);
                    std::cout
                        << "bone\t" << entry.name
                        << "\tparent=" << bone.legacy_transform.parent
                        << "\tposition=" << bone.position_context
                        << "\tscale=" << bone.scale_context
                        << "\trotation=" << bone.rotation
                        << "\tlegacy_rotation="
                        << bone.legacy_rotation << '\n';
                }
            }
            for (const auto& clip : root.clips) {
                const gh::milo::Entry* entry = nullptr;
                for (const auto& candidate : directory.entries) {
                    if (candidate.type == "CharClipSamples" &&
                        candidate.name == clip.clip) {
                        entry = &candidate;
                        break;
                    }
                }
                if (!entry)
                    throw std::runtime_error(
                        "clip summary has no CharClipSamples body: " +
                        clip.clip);
                const auto body =
                    gh::milo_object::parse_char_clip_samples10(
                        entry->body_bytes);
                std::cout << "clip\t" << clip.clip << '\t'
                          << clip.flags << '\t' << clip.size_bytes
                          << "\tbody_flags=" << body.flags
                          << "\tbody_bytes=" << entry->body_bytes.size()
                          << "\tsample_bytes="
                          << body.full.sample_bytes.size() +
                                 body.one.sample_bytes.size() +
                                 body.duplicate.sample_bytes.size()
                          << "\tfull_samples=" << body.full.sample_count
                          << "\tone_samples=" << body.one.sample_count
                          << "\tduplicate_samples="
                          << body.duplicate.sample_count
                          << "\tevents=" << body.events.size()
                          << "\tenter=" << body.legacy_enter_event
                          << "\texit=" << body.legacy_exit_event
                          << '\n';
                if (print_events) {
                    for (const auto& event : body.events)
                        std::cout << "event\t" << clip.clip
                                  << "\t" << event.frame
                                  << "\t" << event.script << '\n';
                }
                if (print_channels) {
                    for (const auto& channel : body.full.channels)
                        std::cout << "channel\t" << clip.clip
                                  << "\tfull\t" << channel << '\n';
                    for (const auto& channel : body.one.channels)
                        std::cout << "channel\t" << clip.clip
                                  << "\tone\t" << channel << '\n';
                }
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "build-clipset-from-acp") {
        try {
            fs::path output;
            std::string name;
            std::string role_name;
            int move_self_override = -1;
            bool control_root_pelvis_parent = false;
            std::vector<std::string> explicit_groups;
            fs::path transition_path;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--name" && i + 1 < argc) name = argv[++i];
                else if (arg == "--role" && i + 1 < argc)
                    role_name = argv[++i];
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else if (arg == "--move-self" && i + 1 < argc)
                    move_self_override =
                        parse_bool_arg(argv[++i], "--move-self") ? 1 : 0;
                else if (arg == "--control-root-pelvis-parent")
                    control_root_pelvis_parent = true;
                else if (arg == "--group" && i + 1 < argc)
                    explicit_groups.push_back(argv[++i]);
                else if (arg == "--transitions" && i + 1 < argc)
                    transition_path = argv[++i];
                else usage();
            }
            if (output.empty() || name.empty() || role_name.empty())
                usage();
            if (!fs::is_directory(input))
                throw std::runtime_error(
                    "ACP directory not found: " + input.string());
            const auto role = parse_clipset_role(role_name);
            std::vector<fs::path> paths;
            for (const auto& item : fs::directory_iterator(input)) {
                if (item.is_regular_file() &&
                    item.path().extension() == ".acp")
                    paths.push_back(item.path());
            }
            std::sort(paths.begin(), paths.end());
            if (paths.empty())
                throw std::runtime_error(
                    "ACP directory has no .acp files: " +
                    input.string());

            std::vector<gh::acp::File> sources;
            sources.reserve(paths.size());
            for (const auto& path : paths)
                sources.push_back(
                    gh::acp::parse(gh::acp::read_file(path.string())));

            std::map<std::string, std::vector<gh::milo_object::CharClipTransition5>> transitions;
            if (!transition_path.empty()) {
                std::ifstream file(transition_path);
                if (!file) throw std::runtime_error("Cannot open transition table");
                std::map<std::string, const gh::acp::File*> by_name;
                for (const auto& source : sources) by_name.emplace(source.object_name, &source);
                std::string line;
                while (std::getline(file, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    std::istringstream row(line);
                    std::string from, to, extra;
                    float current, next;
                    if (!(row >> from >> to >> current >> next) || (row >> extra) ||
                        !std::isfinite(current) || !std::isfinite(next) ||
                        !by_name.count(from) || !by_name.count(to))
                        throw std::runtime_error("Invalid transition row: " + line);
                    const auto* a = by_name.at(from);
                    const auto* b = by_name.at(to);
                    if (current < a->start_beat || current > a->end_beat + 1e-5f ||
                        next < b->start_beat || next > b->end_beat + 1e-5f)
                        throw std::runtime_error("Transition outside clip interval: " + line);
                    auto& edges = transitions[from];
                    auto edge = std::find_if(edges.begin(), edges.end(),
                        [&](const auto& value) { return value.clip == to; });
                    if (edge == edges.end()) {
                        edges.push_back({to, {}});
                        edge = edges.end() - 1;
                    }
                    edge->nodes.push_back({current, next});
                }
                for (auto& [name, edges] : transitions)
                    for (auto& edge : edges)
                        std::sort(edge.nodes.begin(), edge.nodes.end(),
                            [](const auto& a, const auto& b) { return a.current_beat < b.current_beat; });
            }

            gh::milo::Directory directory;
            directory.dir_version = 24;
            directory.dir_type = "CharClipSet";
            directory.dir_name = name;
            directory.boundaries_exact = true;
            directory.dir_terminator_value = 0xDEADDEADu;

            gh::milo_object::CharClipSet14 root;
            root.object_directory.object_fields.type =
                clipset_legacy_type(role).first;
            root.object_directory.viewports =
                standard_clip_set_viewports();
            root.blend_width = sources.front().blend_width;
            root.play_flags = root_play_flags_for_role(role, sources);
            root.move_self =
                move_self_override >= 0
                    ? move_self_override != 0
                    : role == gh::milo_convert::Gh2ClipSetRole::GuitarMain;
            const auto [legacy_type, legacy_type_version] =
                clipset_legacy_type(role);
            root.legacy_type = legacy_type;
            root.legacy_type_version = legacy_type_version;

            std::map<std::string, ChannelContext> contexts;
            for (const auto& source : sources) {
                const auto clip =
                    gh::milo_convert::
                        convert_gh1_acp_to_gh2_char_clip_samples10(
                            source, transitions[source.object_name]);
                for (const auto& channel : clip.full.channels)
                    add_channel_context(contexts, channel);
                for (const auto& channel : clip.one.channels)
                    add_channel_context(contexts, channel);
                const auto body =
                    gh::milo_object::
                        serialize_char_clip_samples10(clip);
                const uint32_t allocation_size =
                    static_cast<uint32_t>(
                        gh::milo_object::
                            char_clip_samples10_ps2_allocate_size(
                                clip));
                directory.entries.push_back(make_entry(
                    "CharClipSamples", source.object_name, body));
                root.clips.push_back(
                    {source.object_name, clip.flags, allocation_size});
            }
            std::vector<std::string> context_bases;
            context_bases.reserve(contexts.size());
            for (const auto& [base, context] : contexts) {
                (void)context;
                context_bases.push_back(base);
            }
            for (const auto& base : context_bases)
                add_generated_parent_contexts(
                    contexts, role, base, control_root_pelvis_parent);

            if (!explicit_groups.empty()) {
                std::set<std::string> group_names;
                for (const auto& spec : explicit_groups) {
                    const auto equals = spec.find('=');
                    if (equals == std::string::npos || equals == 0)
                        throw std::runtime_error("--group expects name=clip,clip");
                    const auto group_name = spec.substr(0, equals);
                    if (!group_names.insert(group_name).second)
                        throw std::runtime_error("duplicate explicit group: " + group_name);
                    gh::milo_object::CharClipGroup1 group;
                    group.which = 0;
                    std::istringstream members(spec.substr(equals + 1));
                    std::string member;
                    while (std::getline(members, member, ',')) {
                        if (member.empty() || std::none_of(sources.begin(), sources.end(),
                                [&](const auto& clip) { return clip.object_name == member; }))
                            throw std::runtime_error("unresolved explicit group member: " + member);
                        if (std::find(group.clips.begin(), group.clips.end(), member) != group.clips.end())
                            throw std::runtime_error("duplicate explicit group member: " + member);
                        group.clips.push_back(member);
                    }
                    if (group.clips.empty())
                        throw std::runtime_error("empty explicit group: " + group_name);
                    directory.entries.push_back(make_entry("CharClipGroup", group_name,
                        gh::milo_object::serialize_char_clip_group1(group)));
                }
            } else if (role == gh::milo_convert::Gh2ClipSetRole::GuitarMain)
                add_generated_guitar_clip_groups(directory, sources);

            gh::milo_object::CharClipFilter0 filter;
            directory.entries.push_back(make_entry(
                "CharClipFilter", "clip_filter.ccf",
                gh::milo_object::serialize_char_clip_filter0(filter)));

            const std::string bone_extension =
                role == gh::milo_convert::Gh2ClipSetRole::GuitarFret
                    ? ".mesh"
                    : ".trans";
            for (const auto& [base, context] : contexts) {
                gh::milo_object::CharBone2 bone;
                set_identity(bone.legacy_transform.local);
                set_identity(bone.legacy_transform.world);
                generated_guitar_controller_local(
                    base, bone.legacy_transform.local);
                const std::string parent =
                    generated_guitar_parent_base(
                        role, base, control_root_pelvis_parent);
                if (!parent.empty())
                    bone.legacy_transform.parent =
                        generated_char_bone_parent_name(
                            parent, base, bone_extension);
                bone.position_context = context.position;
                bone.scale_context = context.scale;
                bone.rotation = rotation_context(context);
                bone.legacy_rotation = 9;
                directory.entries.push_back(make_entry(
                    "CharBone",
                    generated_char_bone_name(base, bone_extension),
                    gh::milo_object::serialize_char_bone2(bone)));
            }

            directory.dir_body_bytes =
                gh::milo_object::serialize_char_clip_set14(root);
            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "generated CharClipSet payload round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "generated CharClipSet container round trip differs");
            write_file(output, target_bytes);
            std::cout
                << "clipset=" << name
                << " role="
                << gh::milo_convert::gh2_clip_set_role_name(role)
                << " clips=" << sources.size()
                << " bones=" << contexts.size()
                << " entries=" << directory.entries.size()
                << " move_self=" << (root.move_self ? 1 : 0)
                << " bytes=" << target_bytes.size()
                << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command == "build-character-from-meshbundle") {
        try {
            fs::path output;
            std::string name;
            std::string main_anim;
            std::string strum_anim;
            std::string fret_anim;
            bool preserve_guitar_proxies = false;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--name" && i + 1 < argc) name = argv[++i];
                else if (arg == "--out" && i + 1 < argc)
                    output = argv[++i];
                else if (arg == "--preserve-guitar-proxies")
                    preserve_guitar_proxies = true;
                else if (arg == "--main-anim" && i + 1 < argc)
                    main_anim = argv[++i];
                else if (arg == "--strum-anim" && i + 1 < argc)
                    strum_anim = argv[++i];
                else if (arg == "--fret-anim" && i + 1 < argc)
                    fret_anim = argv[++i];
                else usage();
            }
            if (output.empty() || name.empty()) usage();
            const bool has_guitarist_graph =
                !main_anim.empty() || !strum_anim.empty() ||
                !fret_anim.empty();
            if (has_guitarist_graph &&
                (main_anim.empty() || strum_anim.empty() ||
                 fret_anim.empty())) {
                throw std::runtime_error(
                    "build-character-from-meshbundle: guitarist graph "
                    "requires --main-anim, --strum-anim, and --fret-anim");
            }

            MeshBundle bundle = parse_meshbundle(input);
            if (preserve_guitar_proxies) {
                if (!has_guitarist_graph)
                    throw std::runtime_error(
                        "--preserve-guitar-proxies requires a guitarist graph");
                for (const auto* proxy : {"bone_fret.mesh", "bone_strum.mesh",
                                          "bone_fret_hand.mesh", "bone_strum_hand.mesh"}) {
                    if (bundle.bone_transforms.find(proxy) == bundle.bone_transforms.end())
                        throw std::runtime_error(
                            std::string("source meshbundle is missing guitar proxy ") + proxy);
                }
            }
            if (bundle.chunks.empty())
                throw std::runtime_error(
                    "meshbundle has no Mesh28 chunks");

            gh::milo::Directory directory;
            directory.dir_version = 24;
            directory.dir_type =
                has_guitarist_graph ? "BandCharacter" : "Character";
            directory.dir_name = name;
            directory.boundaries_exact = true;
            directory.dir_terminator_value = 0xDEADDEADu;

            std::set<std::string> material_names;
            std::set<std::string> texture_names;
            std::set<std::string> bone_names;
            std::vector<std::string> mesh_names;
            std::array<float, 4> aggregate_sphere = {0, 0, 0, 0};
            bool have_sphere = false;
            for (auto& chunk : bundle.chunks) {
                material_names.insert(chunk.material);
                texture_names.insert(chunk.texture);
                mesh_names.push_back(chunk.name);
                if (!have_sphere) {
                    aggregate_sphere = chunk.mesh.drawable.sphere;
                    have_sphere = true;
                } else {
                    aggregate_sphere[3] = std::max(
                        aggregate_sphere[3],
                        chunk.mesh.drawable.sphere[3]);
                }
                for (const auto& slot : chunk.mesh.bone_slots) {
                    if (!slot.bone.empty())
                        bone_names.insert(slot.bone);
                }
            }
            for (const auto& [bone_name, transform] :
                 bundle.bone_transforms) {
                (void)transform;
                if (!bone_name.empty())
                    bone_names.insert(bone_name);
            }

            for (const auto& texture_name : texture_names) {
                gh::milo_object::Tex10 tex;
                const auto texture =
                    bundle.textures.find(texture_name);
                if (texture != bundle.textures.end()) {
                    tex.width = texture->second.width;
                    tex.height = texture->second.height;
                    tex.bits_per_pixel = texture->second.bits_per_pixel;
                    tex.has_bitmap = true;
                    tex.bitmap = texture->second.bitmap;
                    tex.use_external = false;
                } else {
                    tex.width = 512;
                    tex.height = 512;
                    tex.bits_per_pixel = 32;
                    tex.external_path =
                        "textures/" + texture_name + ".png";
                    tex.use_external = true;
                }
                directory.entries.push_back(make_entry(
                    "Tex", texture_name,
                    gh::milo_object::serialize_tex10(tex)));
            }
            for (const auto& material_name : material_names) {
                gh::milo_object::Mat27 mat;
                mat.use_environment = false;
                mat.prelit = true;
                mat.diffuse_texture.clear();
                for (const auto& chunk : bundle.chunks) {
                    if (chunk.material == material_name) {
                        mat.diffuse_texture = chunk.texture;
                        mat.alpha_cut = mat.alpha_cut || chunk.alpha_cut;
                        mat.alpha_write = mat.alpha_write || chunk.alpha_write;
                        mat.z_mode = chunk.z_mode;
                        mat.cull = mat.cull && chunk.cull;
                        mat.blend = chunk.blend;
                        break;
                    }
                }
                directory.entries.push_back(make_entry(
                    "Mat", material_name,
                    gh::milo_object::serialize_mat27(mat)));
            }
            for (const auto& bone_name : bone_names) {
                gh::milo_object::Trans9 trans;
                const auto transform =
                    bundle.bone_transforms.find(bone_name);
                if (transform != bundle.bone_transforms.end()) {
                    trans.local = transform->second.local;
                    trans.world = transform->second.world;
                    trans.parent = transform->second.parent_name;
                } else {
                    set_identity(trans.local);
                    set_identity(trans.world);
                }
                directory.entries.push_back(make_entry(
                    "Trans", bone_name,
                    gh::milo_object::serialize_trans9(trans)));
            }
            for (const auto& chunk : bundle.chunks) {
                directory.entries.push_back(make_entry(
                    "Mesh", chunk.name,
                    gh::milo_object::serialize_mesh28(chunk.mesh)));
            }

            const std::string group_name = name + ".grp";
            gh::milo_object::Group12 group;
            set_identity(group.transformable.local);
            set_identity(group.transformable.world);
            group.objects = mesh_names;
            group.drawable.sphere = aggregate_sphere;
            directory.entries.push_back(make_entry(
                "Group", group_name,
                gh::milo_object::serialize_group12(group)));

            gh::milo_object::Character9 character;
            character.render_directory.object_directory.object_fields.type =
                "guitarist";
            character.render_directory.object_directory.viewports =
                standard_clip_set_viewports();
            set_identity(character.render_directory.transformable.local);
            set_identity(character.render_directory.transformable.world);
            character.render_directory.drawable.sphere = aggregate_sphere;
            character.lods.push_back({0.0f, group_name});
            character.sphere_base = group_name;
            if (has_guitarist_graph) {
                gh::milo_object::BandCharacter1 band_character;
                band_character.character = character;
                directory.dir_body_bytes =
                    gh::milo_object::serialize_band_character1(
                        band_character);
            } else {
                directory.dir_body_bytes =
                    gh::milo_object::serialize_character9(character);
            }

            if (has_guitarist_graph)
                append_guitarist_runtime_graph(
                    directory, main_anim, strum_anim, fret_anim);
            if (has_guitarist_graph && !preserve_guitar_proxies)
                patch_guitarist_proxy_transforms(directory);

            const auto target_payload =
                gh::milo::serialize_directory(directory);
            const auto verify_directory =
                gh::milo::parse_directory(target_payload);
            if (!verify_directory.boundaries_exact ||
                gh::milo::serialize_directory(verify_directory) !=
                    target_payload)
                throw std::runtime_error(
                    "generated Character payload round trip differs");
            const auto target_bytes =
                serialize_generated_milo(target_payload);
            const auto verify_container =
                gh::milo::parse_container(target_bytes);
            const auto verify_container_directory =
                gh::milo::parse_directory(
                    gh::milo::container_payload(verify_container));
            if (!verify_container_directory.boundaries_exact ||
                gh::milo::serialize_directory(
                    verify_container_directory) != target_payload)
                throw std::runtime_error(
                    "generated Character container round trip differs");
            write_file(output, target_bytes);
            std::cout
                << "character=" << name
                << " outfit=" << bundle.outfit
                << " meshes=" << bundle.chunks.size()
                << " materials=" << material_names.size()
                << " textures=" << texture_names.size()
                << " bones=" << bone_names.size()
                << " guitarist_graph="
                << (has_guitarist_graph ? 1 : 0)
                << " entries=" << directory.entries.size()
                << " bytes=" << target_bytes.size()
                << " output=" << output.string() << '\n';
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "milo_convert_tool: " << ex.what() << "\n";
            return 2;
        }
    }
    if (command != "convert") usage();
    fs::path output;
    fs::path manifest;
    std::string name;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--name" && i + 1 < argc) name = argv[++i];
        else if (arg == "--out" && i + 1 < argc) output = argv[++i];
        else if (arg == "--manifest" && i + 1 < argc)
            manifest = argv[++i];
        else usage();
    }
    if (output.empty() || manifest.empty() || name.empty()) usage();

    try {
        const auto source_bytes = gh::milo::read_file(input.string());
        const auto source_container =
            gh::milo::parse_container(source_bytes);
        const auto source_payload =
            gh::milo::container_payload(source_container);
        const auto source_directory =
            gh::milo::parse_directory(source_payload);
        const auto result =
            gh::milo_convert::convert_gh1_directory_to_gh2_rnddir(
                source_directory, name);
        write_text(manifest, gh::milo_convert::manifest_tsv(result));
        if (!result.complete) {
            std::cerr
                << "conversion blocked; manifest written to "
                << manifest.string() << "\n";
            return 1;
        }

        const auto target_payload =
            gh::milo::serialize_directory(result.directory);
        const auto target_container =
            gh::milo::make_container(target_payload);
        const auto target_bytes =
            gh::milo::serialize_container(target_container);
        const auto verify_container =
            gh::milo::parse_container(target_bytes);
        const auto verify_directory = gh::milo::parse_directory(
            gh::milo::container_payload(verify_container));
        if (!verify_directory.boundaries_exact ||
            gh::milo::serialize_directory(verify_directory) !=
                target_payload)
            throw std::runtime_error(
                "converted directory failed native GH2 round trip");
        write_file(output, target_bytes);
        std::cout << "converted " << source_directory.entries.size()
                  << " source objects into "
                  << result.directory.entries.size()
                  << " GH2 objects\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "milo_convert_tool: " << ex.what() << "\n";
        return 2;
    }
}
