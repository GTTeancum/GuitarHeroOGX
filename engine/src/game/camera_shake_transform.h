#pragma once
#include "camera_path_transform.h"
#include "camera_rotation.h"

namespace ghogx::camera {
inline CameraAffineRows source_shake_transform(
    const CameraAffineRows& pose, const std::array<float, 3>& translation,
    const std::array<float, 3>& euler) {
    // GH2 USA 266CE8..266D90. Translate in the existing, unmodified basis,
    // then postmultiply each row by MakeRotMatrix. No look-at reconstruction
    // or Gram-Schmidt normalization occurs between Interp and these VU ops.
    const auto rotation = source_euler_rotation(euler);
    CameraAffineRows out{};
    for (int axis = 0; axis < 3; ++axis) {
        out[3][axis] = pose[3][axis] + pose[0][axis] * translation[0] +
                      pose[1][axis] * translation[1] + pose[2][axis] * translation[2];
        for (int row = 0; row < 3; ++row)
            for (int k = 0; k < 3; ++k)
                out[row][axis] += pose[row][k] * rotation[k][axis];
    }
    return out;
}
} // namespace ghogx::camera
