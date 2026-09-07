#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace ghogx::camera {
// GH2 PS2 BuildTransform, 0x2671a4..0x267218: filter==0 bypasses
// interpolation (copies the live target). It is not a zero-rate low-pass.
// With filtering enabled, zero projected error really does retain the cache.
inline float target_filter_step(float filter, float projected_error) {
    if (!std::isfinite(filter) || filter == 0.0f) return 1.0f;
    if (!std::isfinite(projected_error)) projected_error = 1.0f;
    // 267188..2671BC caps the nonnegative projected distance, NOT its
    // product with the authored filter. Only exact filter==0 bypasses it.
    return filter * std::clamp(projected_error, 0.0f, 1.0f);
}

inline std::array<float, 3> target_filter_blend(
    const std::array<float, 3>& cached,
    const std::array<float, 3>& live, float step) {
    // 2671C0..267214 tests exact 0/1, then uses live*step + cached*(1-step).
    // Do not replace extrapolation with a saturation shortcut.
    if (step == 0.0f) return cached;
    if (step == 1.0f) return live;
    std::array<float, 3> result{};
    for (int axis = 0; axis < 3; ++axis)
        result[axis] = cached[axis] * (1.0f - step) + live[axis] * step;
    return result;
}
} // namespace ghogx::camera
