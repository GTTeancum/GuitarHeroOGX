#include "gh1_venue_camera_conversion.h"

#include "dtb.h"
#include "milo_object.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gh::milo_convert {
namespace {

using Node = gh::dtb::Node;
using NodePtr = std::shared_ptr<Node>;

constexpr uint32_t kObjectTerminator = 0xDEADDEADu;
constexpr double kPositionTolerance = 1.0e-4;
constexpr double kRotationTolerance = 1.0e-5;
constexpr double kScreenTolerance = 1.0e-6;
constexpr double kFovTolerance = 1.0e-7;
constexpr unsigned kMaximumSubdivisionDepth = 20;

float gh1_horizontal_to_gh2_vertical_fov(float horizontal) {
    // GH1 SLUS_212.24 UpdateLocal 0x1B1FBC..0x1B2004:
    // mxx=1/tan(fov/2), mzx=-1/(tan(fov/2)*YRatio).
    // GH2 SLUS_214.47 0x1B2054..0x1B2094 instead has
    // mxx=YRatio/tan(fov/2), mzx=-1/tan(fov/2).
    // Preserve GH1's authored 4:3 framing as GH2 vertical FOV. The source
    // savestate's 45-degree GH1 camera confirms mxx=2.414214, mzx=-3.218952.
    return 2.0f * std::atan(std::tan(horizontal * 0.5f) * 0.75f);
}

struct CameraRecord {
    std::string category;
    std::string path;
    std::string name;
    float start = 0.0f;
    float end = 0.0f;
    float duration = 0.0f;
    std::array<float, 2> singer_in{};
    std::array<float, 2> singer_out{};
    std::array<float, 3> offset_in{};
    std::array<float, 3> offset_out{};
    float near_plane = 10.0f;
    float far_plane = 10000.0f;
    float fov_in = 50.0f;
    float fov_out = 50.0f;
    int crowd_region = -1;
    bool shaky = false;
    bool enable_dof = false;
    bool hide_crowd = false;
    bool walk_ok = true;
    bool low_excitement_ok = true;
    bool real_time = false;
    float ease = 0.0f;
    int force_char_lod = -1;
    int force_cam_facing = 0;
    int eyes = 3;
    std::vector<int> bad_walk_spots;
    std::array<float, 2> guard{};
    bool has_guard = false;
};

struct CurveState {
    std::array<float, 3> helper_shake{};
    std::array<float, 3> position{};
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_rotation = false;
    std::array<float, 2> screen{};
    float fov = 0.872664626f;
};

struct CameraReferencePolicy {
    gh::milo_object::CamShotSubPart20 ref{0, "guitarist0", "bone_head.mesh"};
    std::string fallback;
    bool helper = true;
};

struct CameraPathPolicy {
    CameraReferencePolicy target;
    CameraReferencePolicy parent;
    // The default GH1 helper is translated to ArenaSinger's head, not rotated
    // with that bone. Explicit cam_paths parents use their full WorldXfm.
    bool parent_rotation = false;
};

std::optional<float> number(const NodePtr& node) {
    if (!node) return std::nullopt;
    if (const auto value = gh::dtb::as_float(*node)) return *value;
    if (const auto value = gh::dtb::as_int(*node))
        return static_cast<float>(*value);
    return std::nullopt;
}

std::string text(const NodePtr& node) {
    return node ? gh::dtb::as_string(*node).value_or("") : std::string{};
}

gh::milo_object::CamShotSubPart20 split_reference(const std::string& value) {
    const auto split = value.find("::");
    if (split == std::string::npos || split == 0 || split + 2 == value.size())
        throw std::runtime_error("GH1 camera policy: invalid reference " + value);
    return {0, value.substr(0, split), value.substr(split + 2)};
}

CameraReferencePolicy parse_reference_policy(const NodePtr& node) {
    CameraReferencePolicy policy;
    policy.helper = false;
    if (const std::string value = text(node); !value.empty()) {
        policy.ref = split_reference(value);
        return policy;
    }
    if (node && gh::dtb::is_array(*node)) {
        const auto& expression = gh::dtb::children(*node);
        if (expression.size() == 4 && text(expression[0]) == "if_else" &&
            expression[1] && gh::dtb::is_array(*expression[1])) {
            const auto& condition = gh::dtb::children(*expression[1]);
            const auto primary = text(expression[2]);
            if (condition.size() == 2 && text(condition[0]) == "exists" &&
                text(condition[1]) == primary && !primary.empty()) {
                policy.ref = split_reference(primary);
                policy.fallback = text(expression[3]);
                (void)split_reference(policy.fallback);
                return policy;
            }
        }
    }
    throw std::runtime_error("GH1 camera policy: unsupported reference expression");
}

std::map<std::string, CameraPathPolicy> parse_path_policies(
    const std::vector<uint8_t>& bytes) {
    const auto tree = gh::dtb::parse(bytes);
    std::map<std::string, CameraPathPolicy> policies;
    for (const auto& node : tree.root) {
        if (!node || !gh::dtb::is_array(*node))
            throw std::runtime_error("GH1 camera policy: expected path row");
        const auto& row = gh::dtb::children(*node);
        if (row.empty() || text(row[0]).empty())
            throw std::runtime_error("GH1 camera policy: unnamed path");
        CameraPathPolicy policy;
        for (size_t i = 1; i < row.size(); ++i) {
            if (!row[i] || !gh::dtb::is_array(*row[i]))
                throw std::runtime_error("GH1 camera policy: expected property");
            const auto& property = gh::dtb::children(*row[i]);
            if (property.size() != 2)
                throw std::runtime_error("GH1 camera policy: invalid property");
            const auto name = text(property[0]);
            if (name == "target") policy.target = parse_reference_policy(property[1]);
            else if (name == "parent") {
                policy.parent = parse_reference_policy(property[1]);
                policy.parent_rotation = true;
            } else throw std::runtime_error("GH1 camera policy: unknown property " + name);
        }
        if (!policies.emplace(text(row[0]), std::move(policy)).second)
            throw std::runtime_error("GH1 camera policy: duplicate path " + text(row[0]));
    }
    return policies;
}

std::optional<CameraRecord> parse_record(
    const Node& node, std::string category) {
    if (!gh::dtb::is_array(node)) return std::nullopt;
    const auto& rows = gh::dtb::children(node);
    if (rows.size() < 4 || text(rows[1]) != "switch_cam")
        return std::nullopt;
    CameraRecord record;
    record.category = std::move(category);
    record.path = text(rows[2]);
    record.name = text(rows[3]);
    static const std::set<std::string> known = {
        "bad_walk_spots", "crowd_region", "duration", "ease", "enable_dof", "end",
        "eyes", "far", "force_cam_facing", "force_char_lod",
        "fov_in", "fov_out", "guard", "hide_crowd",
        "low_excitement_ok", "near", "offset_in", "offset_out",
        "real_time", "shaky", "singer_in", "singer_out", "start",
        "walk_ok",
    };
    for (size_t i = 4; i < rows.size(); ++i) {
        if (!rows[i] || !gh::dtb::is_array(*rows[i])) continue;
        const auto& property = gh::dtb::children(*rows[i]);
        if (property.empty()) continue;
        const std::string key = text(property[0]);
        if (known.find(key) == known.end())
            throw std::runtime_error(
                "GH1 VenueCam: unknown switch_cam property " + key);
        const float scalar =
            property.size() > 1
                ? number(property[1]).value_or(0.0f)
                : 0.0f;
        if (key == "bad_walk_spots") {
            if (property.size() != 2 || !property[1] ||
                !gh::dtb::is_array(*property[1])) {
                throw std::runtime_error(
                    "GH1 VenueCam: bad_walk_spots is not an array");
            }
            for (const auto& child :
                 gh::dtb::children(*property[1])) {
                const auto value =
                    child ? gh::dtb::as_int(*child) : std::nullopt;
                if (!value || *value < 0) {
                    throw std::runtime_error(
                        "GH1 VenueCam: bad_walk_spots contains a "
                        "non-negative-integer violation");
                }
                record.bad_walk_spots.push_back(*value);
            }
        } else if (key == "start") record.start = scalar;
        else if (key == "end") record.end = scalar;
        else if (key == "duration") record.duration = scalar;
        else if (key == "near") record.near_plane = scalar;
        else if (key == "far") record.far_plane = scalar;
        else if (key == "fov_in") record.fov_in = scalar;
        else if (key == "fov_out") record.fov_out = scalar;
        else if (key == "crowd_region")
            record.crowd_region = static_cast<int>(scalar);
        else if (key == "shaky") record.shaky = scalar != 0.0f;
        else if (key == "enable_dof")
            record.enable_dof = scalar != 0.0f;
        else if (key == "hide_crowd")
            record.hide_crowd = scalar != 0.0f;
        else if (key == "walk_ok") record.walk_ok = scalar != 0.0f;
        else if (key == "low_excitement_ok")
            record.low_excitement_ok = scalar != 0.0f;
        else if (key == "real_time")
            record.real_time = scalar != 0.0f;
        else if (key == "ease") record.ease = scalar;
        else if (key == "force_char_lod")
            record.force_char_lod = static_cast<int>(scalar);
        else if (key == "force_cam_facing")
            record.force_cam_facing = static_cast<int>(scalar);
        else if (key == "eyes")
            record.eyes = static_cast<int>(scalar);
        else if ((key == "singer_in" || key == "singer_out") &&
                 property.size() >= 3) {
            auto& target =
                key == "singer_in" ? record.singer_in : record.singer_out;
            target = {number(property[1]).value_or(0.0f),
                      number(property[2]).value_or(0.0f)};
        } else if ((key == "offset_in" || key == "offset_out") &&
                   property.size() >= 4) {
            auto& target =
                key == "offset_in" ? record.offset_in : record.offset_out;
            target = {number(property[1]).value_or(0.0f),
                      number(property[2]).value_or(0.0f),
                      number(property[3]).value_or(0.0f)};
        } else if (key == "guard" && property.size() >= 3) {
            record.guard = {number(property[1]).value_or(0.0f),
                            number(property[2]).value_or(0.0f)};
            record.has_guard = true;
        }
    }
    if (record.path.empty() || record.name.empty())
        throw std::runtime_error(
            "GH1 VenueCam: switch_cam has an empty path or name");
    return record;
}

std::vector<CameraRecord> parse_records(
    const std::vector<uint8_t>& bytes) {
    const auto tree = gh::dtb::parse(bytes);
    std::vector<CameraRecord> records;
    std::function<void(const Node&)> visit = [&](const Node& node) {
        if (!gh::dtb::is_array(node)) return;
        const auto& children = gh::dtb::children(node);
        if (children.size() >= 3 && text(children[0]) == "set" &&
            children[1] && children[1]->tag == 0x02 &&
            children[2] && gh::dtb::is_array(*children[2])) {
            const std::string variable = text(children[1]);
            constexpr std::string_view prefix = "camedit.";
            if (variable.rfind(prefix, 0) == 0) {
                const std::string category =
                    variable.substr(prefix.size());
                for (const auto& child :
                     gh::dtb::children(*children[2])) {
                    if (!child) continue;
                    if (auto record = parse_record(*child, category))
                        records.push_back(std::move(*record));
                }
                return;
            }
        }
        for (const auto& child : children)
            if (child) visit(*child);
    };
    for (const auto& root : tree.root)
        if (root) visit(*root);
    if (records.empty())
        throw std::runtime_error(
            "GH1 VenueCam: camera DTB contains no switch_cam records");
    return records;
}

float first_frame(const gh::milo_object::TransAnim6& anim) {
    float first = std::numeric_limits<float>::infinity();
    for (const auto& key : anim.translation_keys)
        first = std::min(first, key.frame);
    for (const auto& key : anim.rotation_keys)
        first = std::min(first, key.frame);
    for (const auto& key : anim.scale_keys)
        first = std::min(first, key.frame);
    return std::isfinite(first) ? first : 0.0f;
}

float last_frame(const gh::milo_object::TransAnim6& anim) {
    float last = -std::numeric_limits<float>::infinity();
    for (const auto& key : anim.translation_keys)
        last = std::max(last, key.frame);
    for (const auto& key : anim.rotation_keys)
        last = std::max(last, key.frame);
    for (const auto& key : anim.scale_keys)
        last = std::max(last, key.frame);
    return std::isfinite(last) ? last : 0.0f;
}

gh::milo_object::TransAnim6 resolve_keys(
    const std::string& name,
    const std::map<std::string, gh::milo_object::TransAnim6>& animations,
    std::set<std::string>& visiting) {
    const auto found = animations.find(name);
    if (found == animations.end())
        throw std::runtime_error(
            "GH1 VenueCam: TransAnim path is absent: " + name);
    auto result = found->second;
    if (result.keys_owner.empty() || result.keys_owner == name)
        return result;
    if (!visiting.insert(name).second)
        throw std::runtime_error(
            "GH1 VenueCam: TransAnim keys_owner cycle: " + name);
    const auto owner = resolve_keys(result.keys_owner, animations, visiting);
    visiting.erase(name);
    result.translation_keys = owner.translation_keys;
    result.rotation_keys = owner.rotation_keys;
    result.scale_keys = owner.scale_keys;
    result.translation_spline = owner.translation_spline;
    result.repeat_translation = owner.repeat_translation;
    result.scale_spline = owner.scale_spline;
    result.follow_path = owner.follow_path;
    result.rotation_slerp = owner.rotation_slerp;
    result.keys_owner = name;
    return result;
}

template <typename Key>
std::pair<size_t, size_t> surrounding(
    const std::vector<Key>& keys, float frame) {
    if (keys.size() < 2 || frame <= keys.front().frame) return {0, 0};
    if (frame >= keys.back().frame)
        return {keys.size() - 1, keys.size() - 1};
    for (size_t i = 1; i < keys.size(); ++i)
        if (frame <= keys[i].frame) return {i - 1, i};
    return {keys.size() - 1, keys.size() - 1};
}

std::array<float, 3> tangent(
    const std::vector<gh::milo_object::Vec3Key>& keys, size_t index) {
    std::array<float, 3> out{};
    if (keys.size() < 2) return out;
    auto difference = [&](size_t a, size_t b) {
        std::array<float, 3> value{};
        for (size_t axis = 0; axis < 3; ++axis)
            value[axis] = keys[a].value[axis] - keys[b].value[axis];
        return value;
    };
    if (keys.size() == 2) return difference(1, 0);
    if (index == 0) {
        const auto a = difference(1, 0);
        const auto b = difference(2, 0);
        for (size_t axis = 0; axis < 3; ++axis)
            out[axis] = a[axis] * 1.5f - b[axis] * 0.25f;
    } else if (index + 1 >= keys.size()) {
        const auto a = difference(keys.size() - 1, keys.size() - 2);
        const auto b = difference(keys.size() - 1, keys.size() - 3);
        for (size_t axis = 0; axis < 3; ++axis)
            out[axis] = a[axis] * 1.5f - b[axis] * 0.25f;
    } else {
        const auto value = difference(index + 1, index - 1);
        for (size_t axis = 0; axis < 3; ++axis)
            out[axis] = value[axis] * 0.5f;
    }
    return out;
}

std::array<float, 3> sample_translation(
    const gh::milo_object::TransAnim6& anim, float frame) {
    const auto& keys = anim.translation_keys;
    if (keys.empty()) return {};
    const auto [a_index, b_index] = surrounding(keys, frame);
    const auto& a = keys[a_index];
    const auto& b = keys[b_index];
    if (a_index == b_index) return a.value;
    const float span = b.frame - a.frame;
    const float t =
        span <= 0.000001f
            ? 0.0f
            : std::clamp((frame - a.frame) / span, 0.0f, 1.0f);
    std::array<float, 3> out{};
    if (!anim.translation_spline || keys.size() < 3) {
        for (size_t axis = 0; axis < 3; ++axis)
            out[axis] =
                a.value[axis] + (b.value[axis] - a.value[axis]) * t;
        return out;
    }
    const auto ta = tangent(keys, a_index);
    const auto tb = tangent(keys, b_index);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    for (size_t axis = 0; axis < 3; ++axis) {
        out[axis] = h00 * a.value[axis] + h10 * ta[axis] +
                    h01 * b.value[axis] + h11 * tb[axis];
    }
    return out;
}

std::array<float, 4> normalize_quaternion(std::array<float, 4> value) {
    float length_squared = 0.0f;
    for (float component : value)
        length_squared += component * component;
    if (length_squared <= 0.0000001f)
        return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inverse = 1.0f / std::sqrt(length_squared);
    for (float& component : value) component *= inverse;
    return value;
}

std::array<float, 4> sample_rotation(
    const gh::milo_object::TransAnim6& anim, float frame) {
    const auto& keys = anim.rotation_keys;
    if (keys.empty()) return {0.0f, 0.0f, 0.0f, 1.0f};
    const auto [a_index, b_index] = surrounding(keys, frame);
    auto a = normalize_quaternion(keys[a_index].value);
    auto b = normalize_quaternion(keys[b_index].value);
    if (a_index == b_index) return a;
    float dot = 0.0f;
    for (size_t axis = 0; axis < 4; ++axis) dot += a[axis] * b[axis];
    if (dot < 0.0f) {
        for (float& component : b) component = -component;
        dot = -dot;
    }
    const float span = keys[b_index].frame - keys[a_index].frame;
    const float t =
        span <= 0.000001f
            ? 0.0f
            : std::clamp(
                  (frame - keys[a_index].frame) / span, 0.0f, 1.0f);
    std::array<float, 4> out{};
    if (anim.rotation_slerp && dot < 0.9995f) {
        const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
        const float sine = std::sin(angle);
        const float wa = std::sin((1.0f - t) * angle) / sine;
        const float wb = std::sin(t * angle) / sine;
        for (size_t axis = 0; axis < 4; ++axis)
            out[axis] = a[axis] * wa + b[axis] * wb;
    } else {
        for (size_t axis = 0; axis < 4; ++axis)
            out[axis] = a[axis] + (b[axis] - a[axis]) * t;
    }
    return normalize_quaternion(out);
}

std::array<float, 9> quaternion_matrix(const std::array<float, 4>& quaternion);

std::array<float, 4> multiply_quaternions(
    const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return normalize_quaternion({
        a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
        a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
        a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
        a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2],
    });
}

float ease(float t, float severity) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (!std::isfinite(severity) || severity == 0.0f) return t;
    const float angle = std::atan(severity);
    if (std::fabs(angle) < 0.000001f) return t;
    return std::atan(-severity + 2.0f * severity * t) /
               (2.0f * angle) +
           0.5f;
}

float inverse_ease(float value, float severity) {
    value = std::clamp(value, 0.0f, 1.0f);
    if (!std::isfinite(severity) || severity == 0.0f) return value;
    const float angle = std::atan(severity);
    if (std::fabs(severity) < 0.000001f) return value;
    const float mapped =
        std::tan((2.0f * value - 1.0f) * angle);
    return std::clamp(
        (mapped + severity) / (2.0f * severity), 0.0f, 1.0f);
}

CurveState evaluate(
    const CameraRecord& record,
    const gh::milo_object::TransAnim6& animation,
    const gh::milo_object::TransAnim6* shake_animation,
    float main_time, float elapsed_source_units) {
    const float progress = ease(main_time, record.ease);
    const float source_first = first_frame(animation);
    const float source_last = last_frame(animation);
    const float source_start =
        source_first + (source_last - source_first) *
                           std::clamp(record.start * 0.01f, 0.0f, 1.0f);
    const float source_end =
        source_first + (source_last - source_first) *
                           std::clamp(record.end * 0.01f, 0.0f, 1.0f);
    const float source_frame =
        source_start + (source_end - source_start) * progress;
    const float source_low = std::min(source_start, source_end);
    const float source_high = std::max(source_start, source_end);
    const float framing_progress =
        std::fabs(source_high - source_low) <= 0.000001f
            ? 1.0f
            : std::clamp(
                  (source_frame - source_low) /
                      (source_high - source_low),
                  0.0f, 1.0f);
    CurveState state;
    state.position = sample_translation(animation, source_frame);
    state.rotation = sample_rotation(animation, source_frame);
    state.has_rotation = !animation.rotation_keys.empty();
    for (size_t axis = 0; axis < 3; ++axis) {
        state.position[axis] +=
            record.offset_in[axis] +
            (record.offset_out[axis] - record.offset_in[axis]) *
                framing_progress;
    }
    if (shake_animation) {
        // SLUS_212.24:16E804 calls Multiply(shake, path, path).
        // 24B608..24B660 uses row transforms: shake translation is rotated
        // by the path basis before adding path translation. Offsets are
        // added afterward, not rotated by the shake. Preserve shake rotation
        // even when the path itself has no rotation keys.
        const auto shake =
            sample_translation(*shake_animation, elapsed_source_units);
        state.helper_shake = shake;
        const auto path_basis = quaternion_matrix(state.rotation);
        for (size_t axis = 0; axis < 3; ++axis) {
            for (size_t row = 0; row < 3; ++row)
                state.position[axis] += shake[row] * path_basis[row * 3 + axis];
        }
        state.rotation = multiply_quaternions(
            sample_rotation(*shake_animation, elapsed_source_units), state.rotation);
        state.has_rotation = state.has_rotation || !shake_animation->rotation_keys.empty();
    }
    const float singer_x =
        record.singer_in[0] +
        (record.singer_out[0] - record.singer_in[0]) *
            framing_progress;
    const float singer_y =
        record.singer_in[1] +
        (record.singer_out[1] - record.singer_in[1]) *
            framing_progress;
    // GH1 VenueCam stores the selected ArenaSinger's desired centered screen
    // coordinate.  GH2 CamShotFrame stores that same normalized coordinate:
    // CamShot::SetPos converts a viewport point back with
    // ((u - 0.5) * 2, (v - 0.5) * -2), exactly inverting GH1's
    // ((x + 1) / 2, (1 - y) / 2) viewport mapping.
    state.screen = {singer_x, singer_y};
    const float horizontal_fov =
        (record.fov_in +
         (record.fov_out - record.fov_in) * framing_progress) *
        0.01745329251994329577f;
    state.fov = gh1_horizontal_to_gh2_vertical_fov(horizontal_fov);
    return state;
}

double vector_error(
    const std::array<float, 3>& actual,
    const std::array<float, 3>& a,
    const std::array<float, 3>& b) {
    double squared = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        const double expected =
            (static_cast<double>(a[axis]) + b[axis]) * 0.5;
        const double delta = actual[axis] - expected;
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

double vector_error(
    const std::array<float, 2>& actual,
    const std::array<float, 2>& a,
    const std::array<float, 2>& b) {
    double squared = 0.0;
    for (size_t axis = 0; axis < 2; ++axis) {
        const double expected =
            (static_cast<double>(a[axis]) + b[axis]) * 0.5;
        const double delta = actual[axis] - expected;
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

void append_property_key(
    std::vector<gh::milo_object::TypePropertyNode>& properties,
    std::string name) {
    gh::milo_object::TypePropertyNode key;
    key.type = 0x05;
    key.symbol = std::move(name);
    properties.push_back(std::move(key));
}

void append_integer_property(
    std::vector<gh::milo_object::TypePropertyNode>& properties,
    std::string name, int32_t value) {
    append_property_key(properties, std::move(name));
    gh::milo_object::TypePropertyNode node;
    node.type = 0x00;
    node.integer = static_cast<uint32_t>(value);
    properties.push_back(std::move(node));
}

void append_float_property(
    std::vector<gh::milo_object::TypePropertyNode>& properties,
    std::string name, float value) {
    append_property_key(properties, std::move(name));
    gh::milo_object::TypePropertyNode node;
    node.type = 0x01;
    node.floating = value;
    properties.push_back(std::move(node));
}

void append_symbol_property(
    std::vector<gh::milo_object::TypePropertyNode>& properties,
    std::string name, std::string value) {
    append_property_key(properties, std::move(name));
    gh::milo_object::TypePropertyNode node;
    node.type = 0x05;
    node.symbol = std::move(value);
    properties.push_back(std::move(node));
}

void append_symbol_array_property(
    std::vector<gh::milo_object::TypePropertyNode>& properties,
    std::string name, const std::vector<std::string>& values) {
    append_property_key(properties, std::move(name));
    gh::milo_object::TypePropertyNode node;
    node.type = 0x10;
    node.children.reserve(values.size());
    for (const auto& value : values) {
        gh::milo_object::TypePropertyNode child;
        child.type = 0x05;
        child.symbol = value;
        node.children.push_back(std::move(child));
    }
    properties.push_back(std::move(node));
}

std::array<float, 9> quaternion_matrix(
    const std::array<float, 4>& quaternion) {
    const auto q = normalize_quaternion(quaternion);
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    return {
        1.0f - 2.0f * (y * y + z * z),
        2.0f * (x * y - z * w),
        2.0f * (x * z + y * w),
        2.0f * (x * y + z * w),
        1.0f - 2.0f * (x * x + z * z),
        2.0f * (y * z - x * w),
        2.0f * (x * z - y * w),
        2.0f * (y * z + x * w),
        1.0f - 2.0f * (x * x + y * y),
    };
}

gh::milo_object::CamShot20 compile_record(
    const CameraRecord& record,
    const CameraPathPolicy& policy,
    const gh::milo_object::TransAnim6& animation,
    const gh::milo_object::TransAnim6* shake_animation,
    Gh2VenueCameraConversion& metrics, std::optional<float> helper_filter) {
    gh::milo_object::CamShot20 shot;
    shot.object_fields.type = "gh1_venue_camera";
    shot.object_fields.has_type_properties = true;
    auto& properties = shot.object_fields.type_properties;
    append_symbol_property(properties, "gh1_fov_space", "gh2_vertical");
    if (helper_filter) {
        append_float_property(properties, "gh1_helper_filter", *helper_filter);
        append_integer_property(properties, "gh1_helper_target", policy.target.helper);
        append_integer_property(properties, "gh1_helper_parent", policy.parent.helper);
    }
    // Preserve source exists/else decisions until the live character and
    // instrument are known. Do not freeze these to the converter's roster.
    if (!policy.target.fallback.empty())
        append_symbol_property(properties, "gh1_camera_target_fallback", policy.target.fallback);
    if (!policy.parent.fallback.empty())
        append_symbol_property(properties, "gh1_camera_parent_fallback", policy.parent.fallback);
    append_integer_property(
        properties, "hide_crowd", record.hide_crowd);
    append_integer_property(properties, "walk_ok", record.walk_ok);
    append_integer_property(
        properties, "low_excitement_ok", record.low_excitement_ok);
    append_integer_property(
        properties, "force_char_lod", record.force_char_lod);
    append_integer_property(
        properties, "crowd_region", record.crowd_region);
    append_integer_property(properties, "shaky", record.shaky);
    append_integer_property(
        properties, "force_cam_facing", record.force_cam_facing);
    append_integer_property(properties, "eyes", record.eyes);
    append_integer_property(
        properties, "real_time", record.real_time);
    if (!record.bad_walk_spots.empty()) {
        std::vector<std::string> bad_waypoints;
        bad_waypoints.reserve(record.bad_walk_spots.size());
        for (const int index : record.bad_walk_spots) {
            bad_waypoints.push_back(
                "gh1_walk_spot_" + std::to_string(index));
        }
        append_symbol_array_property(
            properties, "bad_waypoints", bad_waypoints);
    }
    append_float_property(
        properties, "gh1_start_percent", record.start);
    append_float_property(
        properties, "gh1_end_percent", record.end);
    append_symbol_property(
        properties, "gh1_source_path", record.path + ".tnm");
    if (record.has_guard) {
        append_float_property(
            properties, "guard_x", record.guard[0]);
        append_float_property(
            properties, "guard_y", record.guard[1]);
    }
    shot.animatable.rate = record.real_time ? 0 : 1;
    shot.near_plane = record.near_plane;
    shot.far_plane = record.far_plane;
    shot.use_depth_of_field = record.enable_dof;
    shot.filter = helper_filter ? 0.0f : 1.0f;
    shot.clamp_height = -1.0f;
    shot.category = record.category;
    // GH2 Load2650A8 reads this float into the selection weight (+44), not a
    // frame number. GH1 switch_cam records have no individual weight, so each
    // converted entry contributes one equal unit to the GH2 category pool.
    // Zero is not neutral: it makes RandomFloat(0,total) degenerate.
    shot.legacy_category_frame = 1.0f;
    shot.looping = false;

    std::map<float, CurveState> samples;
    const float effective_source_duration =
        record.duration > 0.0f
            ? record.duration
            : (shake_animation ? last_frame(*shake_animation) : 0.0f);
    auto add_sample = [&](float time) {
        time = std::clamp(time, 0.0f, 1.0f);
        const float main_time =
            record.duration > 0.0f ? time : 1.0f;
        samples.emplace(
            time,
            evaluate(
                record, animation, shake_animation, main_time,
                time * effective_source_duration));
    };
    if (effective_source_duration <= 0.0f) {
        add_sample(1.0f);
    } else {
        add_sample(0.0f);
        add_sample(1.0f);
        if (record.duration > 0.0f) {
            const float source_first = first_frame(animation);
            const float source_last = last_frame(animation);
            const float source_start =
                source_first + (source_last - source_first) *
                                   std::clamp(
                                       record.start * 0.01f, 0.0f, 1.0f);
            const float source_end =
                source_first + (source_last - source_first) *
                                   std::clamp(
                                       record.end * 0.01f, 0.0f, 1.0f);
            const float source_span = source_end - source_start;
            auto add_authored_frame = [&](float frame) {
                if (std::fabs(source_span) <= 0.000001f) return;
                const float progress =
                    (frame - source_start) / source_span;
                if (progress > 0.0f && progress < 1.0f)
                    add_sample(
                        inverse_ease(progress, record.ease));
            };
            for (const auto& key : animation.translation_keys)
                add_authored_frame(key.frame);
            for (const auto& key : animation.rotation_keys)
                add_authored_frame(key.frame);
        }
        if (shake_animation) {
            auto add_shake_frame = [&](float frame) {
                const float time =
                    frame / effective_source_duration;
                if (time > 0.0f && time < 1.0f)
                    add_sample(time);
            };
            for (const auto& key :
                 shake_animation->translation_keys) {
                add_shake_frame(key.frame);
            }
            for (const auto& key :
                 shake_animation->rotation_keys) {
                add_shake_frame(key.frame);
            }
        }

        std::function<void(float, const CurveState&, float,
                           const CurveState&, unsigned)> subdivide =
            [&](float a_time, const CurveState& a, float b_time,
                const CurveState& b, unsigned depth) {
                const float middle_time = (a_time + b_time) * 0.5f;
                const CurveState middle =
                    evaluate(
                        record, animation, shake_animation,
                        record.duration > 0.0f ? middle_time : 1.0f,
                        middle_time * effective_source_duration);
                const double position_error =
                    vector_error(middle.position, a.position, b.position);
                const auto middle_matrix =
                    quaternion_matrix(middle.rotation);
                const auto a_matrix = quaternion_matrix(a.rotation);
                const auto b_matrix = quaternion_matrix(b.rotation);
                double rotation_squared_error = 0.0;
                for (size_t element = 0;
                     element < middle_matrix.size(); ++element) {
                    const double expected =
                        (static_cast<double>(a_matrix[element]) +
                         b_matrix[element]) *
                        0.5;
                    const double delta =
                        middle_matrix[element] - expected;
                    rotation_squared_error += delta * delta;
                }
                const double rotation_error =
                    std::sqrt(rotation_squared_error);
                const double screen_error =
                    vector_error(middle.screen, a.screen, b.screen);
                const double fov_error = std::fabs(
                    static_cast<double>(middle.fov) -
                    (static_cast<double>(a.fov) + b.fov) * 0.5);
                if (depth >= kMaximumSubdivisionDepth ||
                    (position_error <= kPositionTolerance &&
                     vector_error(middle.helper_shake, a.helper_shake, b.helper_shake) <= kPositionTolerance &&
                     rotation_error <= kRotationTolerance &&
                     screen_error <= kScreenTolerance &&
                     fov_error <= kFovTolerance)) {
                    metrics.maximum_position_linearization_error =
                        std::max(
                            metrics.maximum_position_linearization_error,
                            position_error);
                    metrics.maximum_rotation_linearization_error =
                        std::max(
                            metrics.maximum_rotation_linearization_error,
                            rotation_error);
                    metrics.maximum_screen_linearization_error =
                        std::max(
                            metrics.maximum_screen_linearization_error,
                            screen_error);
                    metrics.maximum_fov_linearization_error =
                        std::max(
                            metrics.maximum_fov_linearization_error,
                            fov_error);
                    return;
                }
                samples.emplace(middle_time, middle);
                ++metrics.adaptive_subdivisions;
                subdivide(
                    a_time, a, middle_time, middle, depth + 1);
                subdivide(
                    middle_time, middle, b_time, b, depth + 1);
            };
        for (;;) {
            const size_t before = samples.size();
            std::vector<std::pair<float, CurveState>> current(
                samples.begin(), samples.end());
            for (size_t i = 1; i < current.size(); ++i) {
                subdivide(
                    current[i - 1].first, current[i - 1].second,
                    current[i].first, current[i].second, 0);
            }
            if (samples.size() == before) break;
        }
    }

    const float duration_frames =
        effective_source_duration <= 0.0f
            ? 0.0f
            : (record.real_time
                   ? effective_source_duration * 0.03f
                   : effective_source_duration);
    std::vector<std::pair<float, CurveState>> ordered(
        samples.begin(), samples.end());
    if (ordered.size() > 65535)
        throw std::runtime_error(
            "GH1 VenueCam: adaptive CamShot exceeds 65535 keyframes");
    shot.keyframes.reserve(ordered.size());
    gh::milo_object::TypePropertyNode helper_shake_keys;
    helper_shake_keys.type = 0x10;
    for (size_t i = 0; i < ordered.size(); ++i) {
        const float time = ordered[i].first;
        const CurveState& state = ordered[i].second;
        if (helper_filter && record.shaky) {
            gh::milo_object::TypePropertyNode sample;
            sample.type = 0x10;
            for (float value : state.helper_shake) {
                gh::milo_object::TypePropertyNode axis;
                axis.type = 0x01;
                axis.floating = value;
                sample.children.push_back(std::move(axis));
            }
            helper_shake_keys.children.push_back(std::move(sample));
        }
        gh::milo_object::CamShotFrame20 frame;
        frame.duration = 0.0f;
        frame.blend =
            i + 1 < ordered.size()
                ? (ordered[i + 1].first - time) * duration_frames
                : 0.0f;
        frame.field_of_view = state.fov;
        const auto matrix = quaternion_matrix(state.rotation);
        for (size_t element = 0; element < matrix.size(); ++element)
            frame.world_offset[element] = matrix[element];
        for (size_t axis = 0; axis < 3; ++axis)
            frame.world_offset[9 + axis] = state.position[axis];
        frame.screen_offset = state.screen;
        frame.blur_depth = 0.5f;
        // SwitchCam 0x16fbc8..0x16fcc4 resolves arena/cam_paths by path name.
        // 0x16e080 resolves only the default ArenaSinger helper position;
        // it is not the complete camera-update routine.
        frame.targets.push_back(policy.target.ref);
        frame.parent = policy.parent.ref;
        frame.use_parent_rotation = policy.parent_rotation;
        shot.keyframes.push_back(std::move(frame));
    }
    if (helper_filter && record.shaky) {
        append_property_key(properties, "gh1_helper_shake_keys");
        properties.push_back(std::move(helper_shake_keys));
    }
    return shot;
}

gh::milo::Entry make_entry(
    std::string name, std::vector<uint8_t> bytes) {
    gh::milo::Entry entry;
    entry.type = "CamShot";
    entry.name = std::move(name);
    entry.body_bytes = std::move(bytes);
    entry.size = entry.body_bytes.size();
    entry.terminator_value = kObjectTerminator;
    return entry;
}

}  // namespace

float gh1_camera_helper_filter_from_game_config(const std::vector<uint8_t>& bytes) {
    const auto tree = gh::dtb::parse(bytes);
    for (const auto& node : tree.root) {
        if (!node || !gh::dtb::is_array(*node)) continue;
        const auto& row = gh::dtb::children(*node);
        if (row.empty() || text(row[0]) != "arena") continue;
        for (size_t i = 1; i < row.size(); ++i) {
            if (!row[i] || !gh::dtb::is_array(*row[i])) continue;
            const auto& field = gh::dtb::children(*row[i]);
            if (field.size() != 2 || text(field[0]) != "cam_filter") continue;
            const auto value = number(field[1]);
            if (!value || !std::isfinite(*value))
                throw std::runtime_error("Invalid GH1 arena cam_filter");
            return *value;
        }
    }
    throw std::runtime_error("GH1 game config has no explicit arena cam_filter; resolve source merge before compiling");
}

Gh2VenueCameraConversion rebind_gh1_venue_camera_paths(
    const std::vector<uint8_t>& camera_paths_dtb,
    const gh::milo::Directory& converted_main_directory) {
    Gh2VenueCameraConversion result;
    result.main_directory = converted_main_directory;
    const auto policies = parse_path_policies(camera_paths_dtb);
    for (auto& entry : result.main_directory.entries) {
        if (entry.type != "CamShot") continue;
        auto shot = gh::milo_object::parse_cam_shot20(entry.body_bytes);
        if (shot.object_fields.type != "gh1_venue_camera") continue;
        auto& properties = shot.object_fields.type_properties;
        std::string path;
        std::string fov_space;
        for (size_t i = 0; i + 1 < properties.size(); i += 2) {
            if (properties[i].symbol == "gh1_source_path") path = properties[i + 1].symbol;
            if (properties[i].symbol == "gh1_fov_space") fov_space = properties[i + 1].symbol;
        }
        if (!fov_space.empty() && fov_space != "gh2_vertical")
            throw std::runtime_error("Unsupported converted GH1 FOV space: " + fov_space);
        if (fov_space.empty()) {
            for (auto& frame : shot.keyframes)
                frame.field_of_view = gh1_horizontal_to_gh2_vertical_fov(frame.field_of_view);
            append_symbol_property(properties, "gh1_fov_space", "gh2_vertical");
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".tnm") path.resize(path.size() - 4);
        const auto found = policies.find(path);
        if (found == policies.end())
            throw std::runtime_error("GH1 camera policy: missing path " + path);
        const auto& policy = found->second;
        // Remove old lowered conditions so this operation is idempotent.
        for (size_t i = 0; i + 1 < properties.size();) {
            if (properties[i].symbol == "gh1_camera_target_fallback" ||
                properties[i].symbol == "gh1_camera_parent_fallback")
                properties.erase(properties.begin() + i, properties.begin() + i + 2);
            else i += 2;
        }
        if (!policy.target.fallback.empty())
            append_symbol_property(properties, "gh1_camera_target_fallback", policy.target.fallback);
        if (!policy.parent.fallback.empty())
            append_symbol_property(properties, "gh1_camera_parent_fallback", policy.parent.fallback);
        for (auto& frame : shot.keyframes) {
            frame.targets = {policy.target.ref};
            frame.parent = policy.parent.ref;
            frame.use_parent_rotation = policy.parent_rotation;
        }
        result.keyframes += shot.keyframes.size();
        ++result.records;
        entry.body_bytes = gh::milo_object::serialize_cam_shot20(shot);
        entry.size = entry.body_bytes.size();
    }
    if (!result.records) throw std::runtime_error("No converted GH1 cameras to rebind");
    return result;
}

Gh2VenueCameraConversion convert_gh1_venue_cameras_to_gh2_camshots(
    const std::vector<uint8_t>& camera_dtb,
    const std::vector<uint8_t>& camera_paths_dtb,
    const gh::milo::Directory& converted_main_directory,
    const gh::milo::Directory& converted_campaths_directory,
    const std::map<std::string, gh::milo_object::TransAnim6>&
        shared_animations, std::optional<float> helper_filter) {
    Gh2VenueCameraConversion result;
    result.main_directory = converted_main_directory;
    const auto records = parse_records(camera_dtb);
    const auto policies = parse_path_policies(camera_paths_dtb);
    std::map<std::string, gh::milo_object::TransAnim6> animations;
    for (const auto& entry : converted_campaths_directory.entries) {
        if (entry.type != "TransAnim") continue;
        animations.emplace(
            entry.name,
            gh::milo_object::parse_trans_anim6(entry.body_bytes));
    }
    std::set<std::string> target_names;
    for (const auto& entry : result.main_directory.entries)
        target_names.insert(entry.name);
    for (const auto& record : records) {
        const auto policy = policies.find(record.path);
        if (policy == policies.end())
            throw std::runtime_error("GH1 camera policy: missing path " + record.path);
        if (!target_names.insert(record.name).second)
            throw std::runtime_error(
                "GH1 VenueCam: CamShot name collides: " + record.name);
        std::set<std::string> visiting;
        const auto animation =
            resolve_keys(record.path + ".tnm", animations, visiting);
        if (animation.translation_keys.empty() &&
            animation.rotation_keys.empty()) {
            throw std::runtime_error(
                "GH1 VenueCam: camera path has no transform keys: " +
                record.path);
        }
        const gh::milo_object::TransAnim6* shake_animation = nullptr;
        if (record.shaky) {
            const auto shake =
                shared_animations.find("shaky_cam1.tnm");
            if (shake == shared_animations.end()) {
                throw std::runtime_error(
                    "GH1 VenueCam: shared shaky_cam1.tnm is absent");
            }
            shake_animation = &shake->second;
            ++result.shaky_records;
        }
        const auto shot =
            compile_record(
                record, policy->second, animation, shake_animation, result, helper_filter);
        result.keyframes += shot.keyframes.size();
        result.main_directory.entries.push_back(
            make_entry(
                record.name,
                gh::milo_object::serialize_cam_shot20(shot)));
        ++result.records;
    }
    return result;
}

}  // namespace gh::milo_convert
