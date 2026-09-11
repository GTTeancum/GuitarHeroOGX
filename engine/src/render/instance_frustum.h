#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace ghogx::render {
// Conservative homogeneous D3D clipping of an actual posed mesh AABB. Crossing
// a plane stays visible; invalid inputs stay visible rather than dropping art.
inline bool instance_bounds_visible(const float* lo, const float* hi,
                                    const std::array<float, 16>& clip) {
  unsigned common = 63;
  for (unsigned corner = 0; corner < 8; ++corner) {
    const float x = (corner & 1) ? hi[0] : lo[0];
    const float y = (corner & 2) ? hi[1] : lo[1];
    const float z = (corner & 4) ? hi[2] : lo[2];
    float v[4];
    for (int k = 0; k < 4; ++k) {
      v[k] = x*clip[k] + y*clip[4+k] + z*clip[8+k] + clip[12+k];
      if (!std::isfinite(v[k])) return true;
    }
    const float e = 0.0001f * std::max(1.0f, std::fabs(v[3]));
    unsigned outside = 0;
    if (v[0] < -v[3]-e) outside |= 1;
    if (v[0] >  v[3]+e) outside |= 2;
    if (v[1] < -v[3]-e) outside |= 4;
    if (v[1] >  v[3]+e) outside |= 8;
    if (v[2] < -e) outside |= 16;
    if (v[2] > v[3]+e) outside |= 32;
    common &= outside;
  }
  return common == 0;
}
}
