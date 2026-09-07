#include "camera_projection.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace ghogx::camera;
void near(float actual, float expected, float tolerance=1e-6f) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance) {
        std::printf("projection mismatch: %.9g vs %.9g (tol %.9g)\n", actual, expected, tolerance);
        std::exit(1);
    }
}
void require(bool value) { if (!value) std::exit(2); }

int main() {
    const CameraAffineRows identity{{{1,0,0},{0,1,0},{0,0,1},{0,0,0}}};
    auto projection=source_camera_world_projection(identity, .75f, -1);
    require(projection.has_value());
    auto p=source_camera_world_to_screen(*projection, {1,2,3});
    require(p.has_value()); near((*p)[0],.6875f); near((*p)[1],-.25f);
    p=source_camera_world_to_screen(*projection, {1,-2,3});
    require(p.has_value()); near((*p)[0],.3125f); near((*p)[1],1.25f);
    p=source_camera_world_to_screen(*projection, {1,0,3});
    require(p.has_value()); near((*p)[0],.875f); near((*p)[1],-1);
    p=source_camera_world_to_screen(*projection, {1,1e-8f,3});
    require(p.has_value()); require((*p)[0]>1e7f && (*p)[1]<-1e8f);
    p=source_camera_world_to_screen(*projection, {1,2,3}, {.25f,.5f,.5f,.25f});
    require(p.has_value()); near((*p)[0],.59375f); near((*p)[1],.4375f);
    const CameraAffineRows scaled{{{2,0,0},{1,3,0},{0,0,4},{10,20,30}}};
    projection=source_camera_world_projection(scaled,.75f,-1);
    require(projection.has_value());
    p=source_camera_world_to_screen(*projection,{14,26,42});
    require(p.has_value()); near((*p)[0],.6875f); near((*p)[1],-.25f);
    auto invalid=identity; invalid[1]=invalid[0];
    projection=source_camera_world_projection(invalid,.75f,-1);
    require(projection.has_value());
    p=source_camera_world_to_screen(*projection,{1,2,3});
    require(p.has_value()); near((*p)[0],.5f); near((*p)[1],.5f);
    // Mathematical determinant is 2^-46, but source scalar products round
    // away that term: 2^-22 - 2^-23 - 2^-23 == 0 at2DB09C. A double
    // cofactor inverse therefore follows the wrong source branch here.
    const float adjacent=std::nextafter(1.0f,2.0f);
    const CameraAffineRows cancellation{{{1,1,1},{1,adjacent,1},
                                         {1,1,adjacent},{0,0,0}}};
    projection=source_camera_world_projection(cancellation,.75f,-1);
    require(projection.has_value());
    for (const auto& row : *projection) for (float value : row) near(value,0,0);
    invalid=identity; invalid[0][0]=std::numeric_limits<float>::quiet_NaN();
    require(!source_camera_world_projection(invalid,.75f,-1));

    // GH1 uses Y/Z only, removes their scale and skew, and retains translation.
    const CameraAffineRows gh1_skew{{{9,8,7},{0,3,0},{0,2,4},{10,20,30}}};
    projection=gh1_camera_world_projection(gh1_skew,.75f,-1);
    require(projection.has_value());
    p=source_camera_world_to_screen(*projection,{11,22,33});
    require(p.has_value()); near((*p)[0],.6875f); near((*p)[1],-.25f);
    auto gh2_projection=source_camera_world_projection(gh1_skew,.75f,-1);
    require(gh2_projection.has_value());
    auto gh2_point=source_camera_world_to_screen(*gh2_projection,{11,22,33});
    require(gh2_point.has_value()); require(std::abs((*gh2_point)[0]-(*p)[0])>.05f);

    // Untouched GH2 USA saved EE memory c21cdb03...: default.cam0xB92EF0.
    // +0x180 cached worldProjection, full-screen Rect at+0x2D4. Fixture values
    // are read from retail, not generated from the function under test.
    const CameraAffineRows world{{
        {.7294678092f,.6632444859f,.1636440307f},
        {-.5540139675f,.7144318819f,-.4259712100f},
        {-.3996762931f,.2202038169f,.8891372681f},
        {177.7605438232f,-63.3434677124f,154.8636627197f}}};
    const CameraAffineRows saved{{
        {1.4822468758f,1.0828329325f,-.5546818972f},
        {1.3476839066f,-.5965927243f,.7152931690f},
        {.3325175941f,-2.4089174271f,-.4264847338f},
        {-229.6128845215f,142.7785797119f,209.9566802979f}}};
    projection=source_camera_world_projection(world,2.0295097828f,-2.7060129642f);
    require(projection.has_value());
    for (int r=0;r<4;++r) for (int c=0;c<3;++c)
        near((*projection)[r][c],saved[r][c],1e-4f);
    for (const auto& point : std::array<std::array<float,3>,3>{{{0,0,0},{100,20,30},{170,-80,120}}}) {
        auto actual=source_camera_world_to_screen(*projection,point);
        auto expected=source_camera_world_to_screen(saved,point);
        require(actual.has_value() && expected.has_value());
        near((*actual)[0],(*expected)[0],1e-4f);
        near((*actual)[1],(*expected)[1],1e-4f);
    }
    // GH1 WorldToScreen1B1100..1B11DC has the same arithmetic contract,
    // cached matrix+1C0 and Rect+314. Untouched Basement EE28f750ae...,
    // camera0xC51190; use its native projection scales (horizontal FOV).
    const CameraAffineRows gh1_world{{
        {.7251802087f,-.6885591149f,0},
        {.6885468960f,.7251673341f,.0059642801f},
        {-.0041067591f,-.0043251775f,.9999821782f},
        {-21.2710838318f,34.0371475220f,55.0388603210f}}};
    const CameraAffineRows gh1_saved{{
        {1.7507400513f,.0132194599f,.6885468960f},
        {-1.6623289585f,.0139225377f,.7251673341f},
        {0,-3.2188944817f,.0059642801f},
        {93.8210678101f,176.9715728760f,-10.3647556305f}}};
    projection=gh1_camera_world_projection(gh1_world,2.4142138958f,-3.2189519405f);
    require(projection.has_value());
    for (int r=0;r<4;++r) for (int c=0;c<3;++c)
        near((*projection)[r][c],gh1_saved[r][c],1e-4f);
    std::puts("camera projection: affine, depth, rectangle, GH1/GH2 saved retail matrices passed");
}
