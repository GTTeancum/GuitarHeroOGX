#pragma once
#include <array>

namespace ghogx::camera {
using CameraAffineRows = std::array<std::array<float, 3>, 4>;

// GH2 PS2 BuildTransform 267344..267350 passes the FIRST CamShotFrame's
// world offset and the sampled path to Multiply (2DAF00). Hmx uses row
// vectors: offset * path. Keep all three matrix rows, including scale/skew.
inline CameraAffineRows source_path_transform(const CameraAffineRows& offset,
                                              const CameraAffineRows& path) {
    CameraAffineRows out{};
    for (int row = 0; row < 4; ++row) {
        for (int axis = 0; axis < 3; ++axis) {
            out[row][axis] = offset[row][0] * path[0][axis] +
                             offset[row][1] * path[1][axis] +
                             offset[row][2] * path[2][axis];
            if (row == 3) out[row][axis] += path[3][axis];
        }
    }
    return out;
}
} // namespace ghogx::camera
