#include "render/scene_d3d9.h"
#include "render/instance_frustum.h"
#include <limits>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

#define CHECK_NEAR(actual, expected, epsilon)                              \
  do {                                                                     \
    const float a = (actual);                                              \
    const float e = (expected);                                            \
    if (std::fabs(a - e) > (epsilon)) {                                    \
      std::printf("  [FAIL] %s:%d  got %.6f expected %.6f\n", __FILE__,    \
                  __LINE__, a, e);                                        \
      std::exit(1);                                                        \
    }                                                                      \
  } while (0)

}  // namespace

int main() {
  {
    std::array<float, 16> clip{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float lo[]{-.5f,-.5f,.1f}, hi[]{.5f,.5f,.9f};
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),1,0);
    clip[12]=2;
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),0,0);
    clip[12]=1; // AABB crosses the right plane: must not disappear.
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),1,0);
    clip[12]=0; clip[14]=-2;
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),0,0);
    clip[14]=2;
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),0,0);
    clip[14]=std::numeric_limits<float>::quiet_NaN();
    CHECK_NEAR(ghogx::render::instance_bounds_visible(lo,hi,clip),1,0);
  }
  // Accepted PS2 row block: gdx_cam_output_00ceaa20 from
  // gh2dxu_arena_camera_relocated_rows_20260623.json. Rows are:
  // forward, position, right, up, then the derived D3D-style view matrix.
  const float forward[3] = {-0.0418621562f, 0.198364615f, 0.9786189f};
  const float eye[3] = {94.6387558f, -18.8070621f, 82.71588f};
  const float up[3] = {-0.261744559f, 0.94304806f, -0.202350974f};
  const ghogx::render::Mat4 view = ghogx::render::Mat4::look_at_lh(
      eye[0], eye[1], eye[2], eye[0] + forward[0] * 100.0f,
      eye[1] + forward[1] * 100.0f, eye[2] + forward[2] * 100.0f, up[0],
      up[1], up[2]);

  CHECK_NEAR(view.m[0][0], 0.964765966f, 0.0010f);
  CHECK_NEAR(view.m[0][1], -0.262060076f, 0.0010f);
  CHECK_NEAR(view.m[0][2], -0.0419126153f, 0.0010f);
  CHECK_NEAR(view.m[1][0], 0.265097678f, 0.0010f);
  CHECK_NEAR(view.m[1][1], 0.944185138f, 0.0010f);
  CHECK_NEAR(view.m[1][2], 0.198603675f, 0.0010f);
  CHECK_NEAR(view.m[2][0], -0.0124653429f, 0.0010f);
  CHECK_NEAR(view.m[2][1], -0.202595f, 0.0010f);
  CHECK_NEAR(view.m[2][2], 0.9797987f, 0.0010f);
  CHECK_NEAR(view.m[3][0], -85.28745f, 0.10f);
  CHECK_NEAR(view.m[3][1], 59.3162041f, 0.10f);
  CHECK_NEAR(view.m[3][2], -73.34319f, 0.10f);
  CHECK_NEAR(view.m[3][3], 1.0f, 0.0001f);

  std::printf("  [ok] camera view matrix matches accepted PS2 output rows\n");
  return 0;
}
