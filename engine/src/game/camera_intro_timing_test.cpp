#include "camera_intro_timing.h"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "camera intro timing failure: " << message << '\n';
    return false;
}

} // namespace

int main() {
    bool ok = true;

    int bars_left = 6;
    for (int expected = 5; expected >= 0; --expected) {
        bars_left = ghogx::camera::camera_bars_after_downbeats(bars_left, 1);
        ok &= expect(bars_left == expected,
                     "six-bar intro hold must decrement once per downbeat");
        ok &= expect(
            ghogx::camera::camera_pick_due_after_downbeat(bars_left, false) ==
                (expected == 0),
            "first normal pick must occur on the sixth downbeat");
    }

    ok &= expect(ghogx::camera::camera_bars_after_downbeats(6, 3) == 3,
                 "crossed downbeats must be consumed without losing bars");
    ok &= expect(ghogx::camera::camera_bars_after_downbeats(2, 4) == 0,
                 "crossed downbeats must clamp the source hold at zero");
    ok &= expect(ghogx::camera::camera_bars_after_downbeats(0, 1) == 0,
                 "expired camera hold must remain expired");
    ok &= expect(!ghogx::camera::camera_pick_due_after_downbeat(0, true),
                 "star mode must suppress the source check_camera_shot call");
    ok &= expect(ghogx::camera::camera_pick_due_after_downbeat(0, false),
                 "diagnostic seek's zero-bar state must pick immediately");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
