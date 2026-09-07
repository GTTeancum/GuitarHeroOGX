#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghogx::camera {

// SLUS_212.24:16FA84..16FAE8. SwitchCam selects from the authored
// switch_cam name (argument 3, loaded at 16F414), NOT its category or path.
// The byte-prefix tests are case-sensitive and only apply to exactly 2 players.
inline int gh1_camera_shot_player_index(std::string_view shot_name,
                                        size_t player_count) {
    if (player_count != 2) return 0;
    if (shot_name.substr(0, 4) == "Left") return 0;
    if (shot_name.substr(0, 5) == "Right") return 1;
    return -1;
}

// SLUS_212.24:16E080. ArenaSinger's transform callback (18D3C0)
// resolves bone_head.mesh. Negative player index means the player centroid;
// an empty roster uses the arena fallback position, raised by 60 units.
inline std::optional<std::array<float, 3>> gh1_camera_player_point(
    const std::vector<std::array<float, 3>>& player_heads,
    int player_index, std::array<float, 3> arena_fallback) {
    if (player_heads.empty()) {
        arena_fallback[2] += 60.0f;
        return arena_fallback;
    }
    if (player_index >= 0) {
        if (static_cast<size_t>(player_index) >= player_heads.size())
            return std::nullopt; // invalid source index, not a guessed player
        return player_heads[static_cast<size_t>(player_index)];
    }
    std::array<float, 3> result{};
    for (const auto& head : player_heads)
        for (size_t i = 0; i < result.size(); ++i) result[i] += head[i];
    for (float& axis : result) axis /= static_cast<float>(player_heads.size());
    return result;
}

// SLUS_212.24:16E61C..16E6B8. Inputs use viewport coordinates (0..1),
// not the centered CamShot screen coordinates (-1..1).
inline float gh1_camera_helper_step(float source_filter,
    const std::array<float, 2>& projected,
    const std::array<float, 2>& desired) {
    const float x = projected[0] - desired[0];
    const float y = projected[1] - desired[1];
    const float error = std::sqrt(x*x + y*y);
    // 16E690 selects error only when error<1; unordered also selects 1.
    return source_filter * (error < 1.0f ? error : 1.0f);
}

// A VenueCam helper has controller lifetime, NOT CamShotFrame lifetime.
// Seed from the selected player in constructor (16DD74). Poll interpolates
// from its previous local position, then adds current shake (16E6CC..16E754).
struct Gh1CameraHelper {
    std::optional<std::array<float, 3>> position;
    std::optional<int> player_index;
    std::optional<std::string> pending_shot;
    size_t poll_count = 0;

    void seed(const std::array<float, 3>& point) { position = point; }
    void reset() {
        position.reset(); player_index.reset(); pending_shot.reset(); poll_count = 0;
    }

    // Selection is a SwitchCam operation. It must not reseed the persistent
    // helper, nor run each poll (which would undo explicit player selections).
    void begin_shot(std::string_view name) { pending_shot = std::string(name); }
    void apply_shot_selection(size_t player_count) {
        if (!pending_shot) return;
        player_index = gh1_camera_shot_player_index(*pending_shot, player_count);
        pending_shot.reset();
    }

    std::optional<std::array<float, 3>> poll(
        const std::array<float, 3>& player_point, float step,
        const std::array<float, 3>& shake_translation) {
        if (!position) return std::nullopt; // caller must run constructor seed
        ++poll_count;
        for (size_t i = 0; i < position->size(); ++i) {
            if (step == 1.0f) (*position)[i] = player_point[i];
            else if (step != 0.0f)
                (*position)[i] = player_point[i]*step + (*position)[i]*(1.0f-step);
            (*position)[i] += shake_translation[i];
        }
        return position;
    }
};

} // namespace ghogx::camera
