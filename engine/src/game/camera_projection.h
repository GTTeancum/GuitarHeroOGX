#pragma once
#include "camera_path_transform.h"
#include "../core/ee_float.h"
#include <cmath>
#include <optional>

namespace ghogx::camera {
struct CameraScreenRect {
    float x = 0, y = 0, width = 1, height = 1;
};

// RndCam worldProjection = Invert(world) * localProjection. Keep the full
// affine basis: a transpose/look-at inverse loses the source scale and skew.
// localProjection rows are (sx,0,0), (0,0,1), (0,sz,0), (0,0,0).
// GH2 UpdateWorld1B2118 calls Matrix3Inverse2DB040. Follow its scalar
// cofactor order and zero-determinant branch; VU matrix products below are
// not claimed bit-exact for every EE execution mode.
inline std::optional<CameraAffineRows> source_camera_world_projection(
    const CameraAffineRows& world, float sx, float sz) {
    for (const auto& row : world)
        for (float v : row) if (!std::isfinite(v)) return std::nullopt;
    if (!std::isfinite(sx) || !std::isfinite(sz)) return std::nullopt;
    using ee::ee_mul; using ee::ee_sub; using ee::ee_add;
    const float a=world[0][0], b=world[0][1], c=world[0][2];
    const float d=world[1][0], e=world[1][1], f=world[1][2];
    const float g=world[2][0], h=world[2][1], i=world[2][2];
    const auto minor = [](float a, float b, float c, float d) {
        return ee_sub(ee_mul(a,b), ee_mul(c,d));
    };
    const float determinant = ee_add(ee_sub(ee_mul(a,minor(e,i,h,f)),
        ee_mul(b,minor(d,i,g,f))), ee_mul(c,minor(d,h,g,e)));
    if (!std::isfinite(determinant)) return std::nullopt;
    // 2DB0A0..2DB0C8: singular input sets reciprocal to zero, not a
    // projection failure or an identity/previous-camera fallback.
    const float reciprocal = determinant == 0 ? 0 : ee::ee_div(1,determinant);
    float inverse[4][3] = {
        {minor(e,i,f,h), -minor(b,i,h,c), minor(b,f,e,c)},
        {-minor(d,i,f,g), minor(a,i,g,c), -minor(a,f,d,c)},
        {minor(d,h,g,e), -minor(a,h,g,b), minor(a,e,b,d)},
        {0,0,0}};
    for (int row=0; row<3; ++row)
        for (float& v : inverse[row]) v = ee_mul(v,reciprocal);
    for (int axis=0; axis<3; ++axis)
        for (int row=0; row<3; ++row)
            inverse[3][axis] -= world[3][row]*inverse[row][axis];
    CameraAffineRows out{};
    for (int row=0; row<4; ++row) {
        out[row] = {static_cast<float>(inverse[row][0]*sx),
                    static_cast<float>(inverse[row][2]*sz),
                    static_cast<float>(inverse[row][1])};
        for (float v : out[row]) if (!std::isfinite(v)) return std::nullopt;
    }
    return out;
}

// GH1 UpdateWorld1B2028..1B2130 is deliberately different from GH2:
// normalize authored Y, normalize cross(Y, authored Z), derive Z=cross(X,Y),
// then transpose that basis and transform -translation. Authored X is unused.
// Keep this in the source-driver layer, never select it by venue name.
inline std::optional<CameraAffineRows> gh1_camera_world_projection(
    const CameraAffineRows& world, float sx, float sz) {
    const auto cross = [](const auto& a, const auto& b) {
        return std::array<float,3>{a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
    };
    const auto normalize = [](std::array<float,3>& v) {
        const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if (!(length>0) || !std::isfinite(length)) return false;
        for (float& axis : v) axis /= length;
        return true;
    };
    auto y=world[1];
    if (!normalize(y)) return std::nullopt;
    auto x=cross(y,world[2]);
    if (!normalize(x)) return std::nullopt;
    const auto z=cross(x,y);
    CameraAffineRows inverse{};
    for (int r=0; r<3; ++r) inverse[r]={x[r],y[r],z[r]};
    for (int axis=0; axis<3; ++axis)
        inverse[3][axis]=-(world[3][0]*inverse[0][axis]+
            world[3][1]*inverse[1][axis]+world[3][2]*inverse[2][axis]);
    CameraAffineRows result{};
    for (int row=0; row<4; ++row) {
        result[row]={inverse[row][0]*sx,inverse[row][2]*sz,inverse[row][1]};
        for (float v : result[row]) if (!std::isfinite(v)) return std::nullopt;
    }
    return result;
}

// GH2 USA WorldToScreen1B1270..1B134C: the cached affine product is applied
// first. Exact depth==0 skips division, but does NOT reject the point. Negative
// depth and off-screen coordinates are valid inputs to the target filter.
inline std::optional<std::array<float, 2>> source_camera_world_to_screen(
    const CameraAffineRows& projection, const std::array<float, 3>& point,
    CameraScreenRect rect = {}) {
    std::array<float, 3> projected{};
    for (int axis=0; axis<3; ++axis) {
        projected[axis] = point[0]*projection[0][axis] +
                          point[1]*projection[1][axis] +
                          point[2]*projection[2][axis] + projection[3][axis];
        if (!std::isfinite(projected[axis])) return std::nullopt;
    }
    if (projected[2] != 0.0f) {
        const float reciprocal = ee::ee_div(1.0f, projected[2]);
        projected[0] = ee::ee_mul(projected[0], reciprocal);
        projected[1] = ee::ee_mul(projected[1], reciprocal);
    }
    const float x = ee::ee_add(rect.x, ee::ee_mul(rect.width,
        ee::ee_mul(ee::ee_add(projected[0], 1.0f), 0.5f)));
    const float y = ee::ee_add(rect.y, ee::ee_mul(rect.height,
        ee::ee_mul(ee::ee_add(projected[1], 1.0f), 0.5f)));
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return std::array<float, 2>{x,y};
}
} // namespace ghogx::camera
