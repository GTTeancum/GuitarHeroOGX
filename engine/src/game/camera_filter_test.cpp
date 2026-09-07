#include "camera_filter.h"
#include "camera_rotation.h"
#include "camera_reference_policy.h"
#include "gh1_camera_helper.h"
#include "camera_intro_timing.h"
#include "camera_weighted_selection.h"
#include "camera_source_random.h"
#include "camera_path_timing.h"
#include "camera_path_transform.h"
#include "camera_shake_transform.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

int main() {
    int failures = 0;
    const auto check = [&](float actual, float expected, const char* name) {
        if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00001f) {
            std::cerr << name << ": " << actual << " != " << expected << '\n';
            ++failures;
        }
    };
    using ghogx::camera::target_filter_step;
    check(ghogx::camera::intro_camera_seconds(300), 10, "authored intro 300 frames = 10 sec");
    check(ghogx::camera::intro_camera_seconds(240), 8, "authored intro independent of song tempo");
    check(ghogx::camera::intro_track_elapsed(7, 10, -2), -1, "track stays hidden before scheduled start");
    check(ghogx::camera::intro_track_elapsed(8, 10, -2), 0, "track starts two seconds before zero");
    check(ghogx::camera::intro_track_elapsed(9.3, 10, -2), 1.3f, "first nowbar cue relative to overlapping start");
    check(ghogx::camera::intro_track_elapsed(10, 10, -2), 2, "song starts before 2.5s refresh task");
    check(ghogx::camera::intro_song_seconds(8.5, 10), -1.5f, "highway preroll must not consume camera elapsed time");
    check(ghogx::camera::intro_song_seconds(10, 10), 0, "highway clock reaches zero at camera end");
    using namespace ghogx::camera;
    {
        using Ids = std::vector<std::string>;
        const auto identity_check = [&](bool result, const char* name) {
            if (!result) { ++failures; std::cerr << name << '\n'; }
        };
        identity_check(same_target_identities(Ids{}, Ids{}), "empty target lists match");
        identity_check(same_target_identities(Ids{"a", "b"}, Ids{"b", "a"}),
                       "target order does not change the branch");
        identity_check(!same_target_identities(Ids{"a", ""}, Ids{"a"}),
                       "null target slots count toward identity");
        identity_check(!same_target_identities(Ids{""}, Ids{}),
                       "null slot differs from an empty list");
        identity_check(!same_target_identities(Ids{"a:neck"}, Ids{"a:head"}),
                       "subpart identity is retained");
        identity_check(same_target_identities(Ids{"a", "a"}, Ids{"a", "b"}),
                       "retail membership search does not consume duplicate matches");
        identity_check(!same_target_identities(Ids{"a", "b"}, Ids{"a", "a"}),
                       "retail duplicate membership comparison is directional");
    }
    {
        check(source_sine(0.0f), 0.0f, "source sine zero");
        check(source_sine(1.5707963705062866f), 0.9996988773345947f,
              "source sine table pi/2 endpoint");
        check(source_cosine(0.0f), 0.9996988773345947f,
              "source cosine zero is table endpoint");
        check(source_sine(-1.5707963705062866f), -1.0f,
              "source sine negative quadrant");
        check(source_sine(0.1f), 0.09983118623495102f,
              "source sine positive interpolation");
        check(source_sine(-0.1f), -0.09983132034540176f,
              "source sine negative interpolation asymmetry");
        const auto zero_rotation = source_euler_rotation({0.0f, 0.0f, 0.0f});
        for (int axis = 0; axis < 3; ++axis) {
            check(zero_rotation[axis][axis], 0.9993978142738342f,
                  "source zero rotation retains executable scale");
        }
        check(zero_rotation[0][1], 0.0f,
              "source zero rotation off-diagonal");
        const auto sampled_rotation = source_euler_rotation(
            {0.12345670163631439f, -0.08765430003404617f, 0.03125f});
        const std::array<std::array<float, 3>, 3> sampled_expected = {{{
            0.9958781599998474f, 0.02034713514149189f,
            0.08686881512403488f},
            {-0.031004756689071655f, 0.9918363690376282f,
             0.12314215302467346f},
            {-0.08365558832883835f, -0.1253279596567154f,
             0.9884973168373108f}}};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                check(sampled_rotation[row][column],
                      sampled_expected[row][column],
                      "source sampled MakeRotMatrix instruction order");
            }
        }
        std::array<float, 3> spring_output{};
        std::array<float, 3> spring_velocity{};
        source_shake_spring_step({3.0f, 4.0f, 0.0f}, spring_output,
                                 spring_velocity);
        check(spring_output[0], 0.06f,
              "source shake spring normalized correction x");
        check(spring_output[1], 0.08f,
              "source shake spring normalized correction y");
        check(spring_velocity[0], 0.054f,
              "source shake spring damped velocity x");
        check(spring_velocity[1], 0.072f,
              "source shake spring damped velocity y");
    }
    {
        const auto scaled = quat_from_row_matrix({{{0.5f,0,0},{0,0.5f,0},{0,0,0.5f}}});
        check(scaled[3], std::sqrt(2.5f)*0.5f, "Matrix3->Quat preserves magnitude before blend");
        const auto x180 = quat_from_row_matrix({{{1,0,0},{0,-1,0},{0,0,-1}}});
        const auto y180 = quat_from_row_matrix({{{-1,0,0},{0,1,0},{0,0,-1}}});
        const auto z180 = quat_from_row_matrix({{{-1,0,0},{0,-1,0},{0,0,1}}});
        check(x180[0], 1, "negative trace x branch");
        check(y180[1], 1, "negative trace y branch");
        check(z180[2], 1, "negative trace z branch");
        // Scaled half-turn about (1,1,1): equal diagonals are not permission
        // to choose a different conversion branch. Retail retains x first.
        const auto tied = quat_from_row_matrix(
            {{{-.5f,1,1},{1,-.5f,1},{1,1,-.5f}}});
        check(tied[0], std::sqrt(1.5f)*.5f, "retail diagonal tie keeps x first");
        check(tied[1], 1.0f/std::sqrt(1.5f), "retail diagonal tie cyclic y");
        check(tied[2], 1.0f/std::sqrt(1.5f), "retail diagonal tie cyclic z");
        const auto tie_yz = quat_from_row_matrix(
            {{{-1,1,1},{1,0,1},{1,1,0}}});
        check(tie_yz[1], std::sqrt(2.0f)*.5f, "retail y/z tie keeps y first");
        // Original PCSX2 slot1 lose01 keys, frame189.170654/duration270.
        // Independent saved-pose oracle includes source sine-table shake;
        // this regression checks the actual runtime quaternion path only.
        const auto qa = quat_from_row_matrix({{{0.810732365f,0.584774613f,0.0000285533f},
            {-0.549483597f,0.761821568f,-0.342606574f},
            {-0.200490117f,0.277913958f,0.939097822f}}});
        const auto qb = quat_from_row_matrix({{{0.683783710f,0.692862391f,0.228870243f},
            {-0.550101876f,0.695556223f,-0.462157488f},
            {-0.479403645f,0.190113798f,0.856754899f}}});
        const auto q = interp_quat(qa,qb,189.170654296875f/270);
        const std::array<float,4> expected{-0.177039516f,-0.154338933f,0.333505359f,0.913022825f};
        for (int i=0; i<4; ++i) check(q[i],expected[i],"saved retail lose01 quaternion");
    }
    check(gh1_camera_helper_step(0.3f, {0.3f, 0.4f}, {0, 0}), 0.15f,
          "GH1 viewport error gain");
    check(gh1_camera_helper_step(0.3f, {3, 4}, {0, 0}), 0.3f,
          "GH1 projected error saturation");
    check(gh1_camera_helper_step(0, {3, 4}, {0, 0}), 0,
          "GH1 disabled filter retains helper, unlike GH2 bypass");
    check(gh1_camera_helper_step(0.3f, {std::numeric_limits<float>::quiet_NaN(), 0}, {0, 0}),
          0.3f, "GH1 unordered projected error selects saturated step");
    Gh1CameraHelper helper;
    if (helper.poll({20, 30, 40}, 1, {})) ++failures;
    helper.seed({10, 20, 30});
    auto helper_position = helper.poll({20, 30, 40}, 0.15f, {1, 2, 3});
    check((*helper_position)[0], 12.5f, "GH1 helper interpolation then shake x");
    check((*helper_position)[1], 23.5f, "GH1 helper interpolation then shake y");
    check((*helper_position)[2], 34.5f, "GH1 helper interpolation then shake z");
    helper_position = helper.poll({100, 200, 300}, 0, {1, 2, 3});
    check((*helper_position)[0], 13.5f, "GH1 zero step retains previous shake feedback");
    helper_position = helper.poll({20, 30, 40}, 1, {1, 2, 3});
    check((*helper_position)[2], 43, "GH1 unit step copies live point then shake");
    const std::vector<std::array<float, 3>> heads{{10, 20, 30}, {30, 40, 50}};
    check((*gh1_camera_player_point(heads, 1, {}))[0], 30, "GH1 selected second player");
    check((*gh1_camera_player_point(heads, -1, {}))[1], 30, "GH1 multiplayer centroid");
    check((*gh1_camera_player_point({}, 0, {1, 2, 3}))[2], 63, "GH1 empty roster fallback");
    if (gh1_camera_player_point(heads, 2, {})) ++failures;
    check(gh1_camera_shot_player_index("Left_close", 2), 0, "GH1 Left shot P1");
    check(gh1_camera_shot_player_index("Right_close", 2), 1, "GH1 Right shot P2");
    check(gh1_camera_shot_player_index("Right", 2), 1, "GH1 exact Right prefix");
    check(gh1_camera_shot_player_index("Wide", 2), -1, "GH1 other shot averages");
    check(gh1_camera_shot_player_index("right_close", 2), -1, "GH1 prefix case-sensitive");
    check(gh1_camera_shot_player_index("Rig", 2), -1, "GH1 prefix must be complete");
    check(gh1_camera_shot_player_index("Right_close", 1), 0, "GH1 solo always P1");
    check(gh1_camera_shot_player_index("Right_close", 0), 0, "GH1 no-player switch index");
    check(gh1_camera_shot_player_index("Right_close", 3), 0, "GH1 exactly two gate");
    helper.seed({9, 8, 7});
    helper.begin_shot("Right_close");
    helper.apply_shot_selection(2);
    check(*helper.player_index, 1, "GH1 SwitchCam chooses P2");
    check((*helper.position)[0], 9, "GH1 SwitchCam preserves filtered position");
    helper.player_index = 0; // later explicit source selection must survive polls
    helper.apply_shot_selection(2);
    check(*helper.player_index, 0, "GH1 no per-poll selection overwrite");
    helper.begin_shot("Wide");
    helper.apply_shot_selection(2);
    check(*helper.player_index, -1, "GH1 next shot selects centroid");
    check((*helper.position)[0], 9, "GH1 next shot does not reseed");
    helper.begin_shot("Left_close");
    helper.reset();
    if (helper.pending_shot || helper.player_index || helper.poll_count) ++failures;
    if (helper.position) ++failures;
    check(target_filter_step(0, 0), 1, "disabled filter at screen center");
    check(target_filter_step(0, 0.4f), 1, "disabled filter off center");
    check(target_filter_step(0.9f, 0), 0, "enabled filter at screen center");
    check(target_filter_step(0.9f, 0.4f), 0.36f, "projected error gain");
    check(target_filter_step(0.9f, 2), 0.9f, "projected error saturation");
    check(target_filter_step(2.0f, 0.75f), 1.5f, "authored gain is not saturated");
    check(target_filter_step(-2.0f, 0.25f), -0.5f, "signed authored gain is not disabled");
    check(target_filter_step(-2.0f, 0), 0, "signed gain at screen center retains cache");
    const std::array<float, 3> filter_old{2, 4, 6}, filter_live{6, 8, 10};
    for (int axis = 0; axis < 3; ++axis) {
        check(ghogx::camera::target_filter_blend(filter_old, filter_live, 1.5f)[axis],
              filter_old[axis] + 6, "source target extrapolation above one");
        check(ghogx::camera::target_filter_blend(filter_old, filter_live, -0.5f)[axis],
              filter_old[axis] - 2, "source target extrapolation below zero");
        check(ghogx::camera::target_filter_blend(filter_old, filter_live, 0)[axis],
              filter_old[axis], "source target exact zero");
        check(ghogx::camera::target_filter_blend(filter_old, filter_live, 1)[axis],
              filter_live[axis], "source target exact one");
    }
    // Multiple polls of a moving performer must not latch its initial target.
    std::array<float, 3> cached{1, 2, 3};
    for (int frame = 1; frame <= 16; ++frame) {
        const float live = static_cast<float>(frame * 10);
        const float step = target_filter_step(0, 0.25f);
        for (auto& axis : cached) axis += (live - axis) * step;
        check(cached[0], live, "live x");
        check(cached[1], live, "live y");
        check(cached[2], live, "live z");
    }
    const std::array<float, 4> identity{0, 0, 0, 1};
    const std::array<float, 4> half_turn{0, 0, 1, 0};
    const auto middle = ghogx::camera::interp_quat(identity, half_turn, 0.5f);
    check(middle[2], std::sqrt(0.5f), "180-degree turn midpoint z");
    check(middle[3], std::sqrt(0.5f), "180-degree turn midpoint w");
    const auto same_rotation = ghogx::camera::interp_quat(identity, {0, 0, 0, -1}, 0.5f);
    check(same_rotation[3], 1, "opposite quaternion signs do not collapse");
    float previous_angle = -1;
    for (int frame = 0; frame <= 100; ++frame) {
        const auto q = ghogx::camera::interp_quat(identity, half_turn, frame / 100.0f);
        check(q[2] * q[2] + q[3] * q[3], 1, "rotation stays normalized");
        const float angle = 2 * std::atan2(q[2], q[3]);
        if (angle < previous_angle) ++failures;
        previous_angle = angle;
    }
    const auto exists = [](std::string_view entity, std::string_view part) {
        return entity == "guitarist0" && part == "spot_neck_fret20.mesh";
    };
    if (ghogx::camera::reference_fallback("guitarist0", "spot_neck_fret20.mesh",
                                         "arena::stage_spot_01.mesh", exists)) ++failures;
    const auto fallback = ghogx::camera::reference_fallback("guitarist0", "spot_neck_fret01.mesh",
                                                           "arena::stage_spot_01.mesh", exists);
    if (!fallback || fallback->first != "arena" || fallback->second != "stage_spot_01.mesh") ++failures;
    if (ghogx::camera::reference_fallback("guitarist0", "missing", "", exists)) ++failures;
    if (ghogx::camera::reference_fallback("guitarist0", "missing", "malformed", exists)) ++failures;
    {
        struct Shot {
            std::string category;
            float selection_weight;
            bool selection_used = false;
            bool eligible = true;
        };
        const auto expect = [&](bool value, const char* name) {
            if (!value) { std::cerr << "weighted camera: " << name << '\n'; ++failures; }
        };
        std::vector<Shot> shots{{"A",1}, {"A",3}, {"B",2}};
        const auto eligible = [](const Shot& shot) { return shot.eligible; };
        ghogx::camera::WeightedSelection first;
        first.append_category(shots, "A", eligible);
        first.append_category(shots, "B", eligible);
        expect(first.total() == 6 && first.size() == 3, "all requested categories contribute");
        expect(first.choose(1.0f) == 0, "inclusive cumulative boundary selects first");
        expect(shots[0].selection_used && !shots[1].selection_used, "only winner marked used");
        ghogx::camera::WeightedSelection next;
        next.append_category(shots, "A", eligible);
        expect(next.total() == 3 && next.size() == 1, "used shot excluded before exhaustion");
        expect(next.choose(2.99f) == 1, "unused weighted choice");
        shots[0].eligible = false;
        ghogx::camera::WeightedSelection reset;
        reset.append_category(shots, "A", eligible);
        expect(shots[0].selection_used && !shots[1].selection_used,
               "exhaustion clears eligible used flags only");
        expect(reset.total() == 3 && reset.choose(0) == 1, "exhausted category retries");
        ghogx::camera::WeightedSelection multi;
        multi.append_category(shots, "B", eligible);
        multi.append_category(shots, "A", eligible);
        expect(multi.total() == 5 && !shots[1].selection_used,
               "each category retries independently despite earlier category candidates");
        expect(multi.choose(2.001f) == 1, "cross-category weights not round-robin priority");
        ghogx::camera::WeightedSelection empty;
        empty.append_category(shots, "missing", eligible);
        expect(!empty.choose(0), "empty pool has no fabricated fallback");
        std::vector<Shot> zero{{"Z",0}, {"Z",1}};
        ghogx::camera::WeightedSelection boundary;
        boundary.append_category(zero, "Z", eligible);
        expect(boundary.choose(0) == 0, "source zero-weight boundary preserved");
        expect(!boundary.choose(std::numeric_limits<float>::quiet_NaN()), "unordered draw wins nothing");
        expect(shots[0].category == "A" && shots[2].category == "B", "source order preserved");

        ghogx::camera::SourceRand random;
        random.values[0] = 0x1234ffff;
        random.values[0x67] = 0;
        check(random.unit_float(), 65535.0f / 65536.0f, "source RNG low16 scaling");
        expect(random.index_a == 1 && random.index_b == 0x68, "RNG advances once");
        random.seed(0x29A);
        auto copy = random;
        const auto integer = copy.next();
        check(random.float_range(2,6), 2 + (integer & 65535) / 65536.0f * 4,
              "source RandomFloat(low,high)");
        expect(random.int_range(7) == copy.next() % 7, "duration shares subsequent RNG draw");
    }
    using ghogx::camera::source_path_frame;
    check(source_path_frame(150, 300, 60, 0), 30, "path duration maps halfway, not early end clamp");
    check(source_path_frame(0, 300, 60, 0), 0, "path starts at zero, not first key");
    check(source_path_frame(300, 300, 60, 0), 60, "path ends at EndFrame");
    check(source_path_frame(450, 300, 60, 0), 90, "page sampler owns extrapolation");
    check(source_path_frame(15, 0, 60, 0), 0, "source zero duration");
    check(source_path_frame(15, 0.0000001f, 60, 0), 0, "source linear epsilon");
    check(source_path_frame(0, 300, 60, 4), 0, "ATan path start");
    check(source_path_frame(150, 300, 60, 4), 30, "ATan path midpoint");
    check(source_path_frame(300, 300, 60, 4), 60, "ATan path endpoint");
    check(source_path_frame(75, 300, 1, 4),
          0.5f - std::atan(2.0f) / (2.0f * std::atan(4.0f)), "ATan quarter not linear/smoothstep");
    {
        const CameraAffineRows offset{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {2, 3, 4}}};
        const CameraAffineRows path{{{2, 0, 0}, {0, 0, 3}, {0, -4, 0}, {10, 20, 30}}};
        const CameraAffineRows expected{{{0, 0, 3}, {-2, 0, 0}, {0, -4, 0}, {14, 4, 39}}};
        const auto result = source_path_transform(offset, path);
        for (int row = 0; row < 4; ++row)
            for (int axis = 0; axis < 3; ++axis)
                check(result[row][axis], expected[row][axis],
                      "first offset * path preserves noncommuting rotations and scale");
    }
    {
        const std::array<std::array<float,3>,3> authored{{{0,.5f,0},{-.5f,0,0},{0,0,.5f}}};
        const auto start = ghogx::camera::interp_row_matrix(authored, authored, 0);
        const auto end = ghogx::camera::interp_row_matrix(authored, authored, 1);
        check(start[0][0], 2.0f/3.0f, "matrix endpoint converts through non-unit quaternion");
        check(end[0][1], .5f, "matrix endpoint must not normalize converted rows");
        check(end[0][0], start[0][0], "both exact endpoints use Matrix3 conversion");
        const CameraAffineRows pose{{{2,0,0},{.25f,3,0},{0,.5f,4},{10,20,30}}};
        const auto shaken = ghogx::camera::source_shake_transform(pose, {1,2,3}, {0,0,0});
        check(shaken[3][0], 12.5f, "shake translates in raw source x/y rows");
        check(shaken[3][1], 27.5f, "shake translates in raw source y/z rows");
        check(shaken[3][2], 42, "shake retains source up scale");
        for (int row=0; row<3; ++row)
            for (int axis=0; axis<3; ++axis)
                check(shaken[row][axis], pose[row][axis]*.9993978142738342f,
                      "zero angular shake preserves incoming skew and source sine scale");
    }
    {
        // Actual retail slot1 lose01 camera, not an expected matrix generated
        // by this implementation. EE SHA c21cdb03a2aafc11aa9977dbf1ae8e99c
        // 6235875d413b2e36d174521fba20515. Parent/target/path all absent here.
        const CameraAffineRows a{{{.810732365f,.584774613f,.000028553295f},
            {-.549483597f,.761821568f,-.342606574f},
            {-.200490117f,.277913958f,.939097822f},
            {164.215026855f,-32.087184906f,118.243629456f}}};
        const CameraAffineRows b{{{.683783710f,.692862391f,.228870243f},
            {-.550101876f,.695556223f,-.462157488f},
            {-.479403645f,.190113798f,.856754899f},
            {183.603332520f,-76.654457092f,170.465118408f}}};
        const float t=189.170654296875f/270;
        const auto basis=ghogx::camera::interp_row_matrix(
            {{a[0],a[1],a[2]}}, {{b[0],b[1],b[2]}}, t);
        CameraAffineRows pose{{basis[0],basis[1],basis[2],{}}};
        for (int i=0;i<3;++i) pose[3][i]=a[3][i]+(b[3][i]-a[3][i])*t;
        const auto actual=ghogx::camera::source_shake_transform(pose,
            {-.0440137684f,-.0146565875f,.0379826054f},{0,0,0});
        const CameraAffineRows retail{{{.7294678092f,.6632444859f,.1636440307f},
            {-.5540139675f,.7144318819f,-.4259712100f},
            {-.3996762931f,.2202038169f,.8891372681f},
            {177.7598724365f,-63.3437538147f,154.8644866943f}}};
        for(int row=0;row<4;++row) for(int axis=0;axis<3;++axis) {
            const float tolerance=row==3 ? .0001f : .00001f;
            if(!std::isfinite(actual[row][axis]) ||
               std::abs(actual[row][axis]-retail[row][axis])>tolerance) {
                std::cerr<<"saved retail final camera row "<<row<<','<<axis
                         <<": "<<actual[row][axis]<<" != "<<retail[row][axis]<<'\n';
                ++failures;
            }
        }
    }
    if (!failures) std::cout << "camera filter/rotation/reference-policy/weighted-selection/path-timing/transform/retail-pose checks passed\n";
    return failures ? 1 : 0;
}
