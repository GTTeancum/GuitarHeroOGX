#pragma once
#include <cmath>

namespace ghogx::camera {
// GH2 267230..2672F0: map mFrame/mDuration, multiply by path EndFrame.
// 2D96B8 is LinearInterpolator::Reset; 2D9890/2D9A30 are ATan Reset/Eval.
// No clamp or first-key offset: the path sampler owns its bounds.
inline float source_path_frame(float frame, float duration, float end_frame, float ease) {
    if (ease == 0.0f) {
        const float slope = std::abs(duration) < 0.000001f ? 0.0f : 1.0f / duration;
        return (slope * frame) * end_frame;
    }
    const float slope = std::abs(duration) < 0.000001f ? 0.0f : (ease + ease) / duration;
    const float mapped = slope * frame - ease;
    const float negative_angle = std::atan(-ease);
    const float scale = 1.0f / (-negative_angle - negative_angle);
    return (std::atan(mapped) * scale + 0.5f) * end_frame;
}
} // namespace ghogx::camera
