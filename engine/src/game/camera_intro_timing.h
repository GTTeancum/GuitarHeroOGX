#pragma once

#include <algorithm>
#include <cstdint>

namespace ghogx::camera {

// GH2 SLUS_214.47:107578..107598 starts the task clock at -mDuration/30.
// 107A54..107A68 schedules extend_track at track_extend_sec relative to zero.
// ui/gen/game.dtb sets that property to -2: TrackPanel overlaps the camera,
// rather than following it. Its 2.5s refresh_track_buttons task is not a wait.
inline double intro_camera_seconds(float authored_duration_frames) {
    return static_cast<double>(authored_duration_frames / 30.0f);
}

inline double intro_track_elapsed(double presentation_seconds,
                                  double camera_seconds,
                                  double track_extend_sec) {
    return presentation_seconds - camera_seconds - track_extend_sec;
}

inline double intro_song_seconds(double presentation_seconds, double camera_seconds) {
    return presentation_seconds - camera_seconds;
}

// world_objects_worldbase.dta::downbeat decrements camera_bars_left once for
// each delivered downbeat before it calls check_camera_shot. Native may cross
// more than one authored bar in a single tick, so consume every crossed
// downbeat while preserving the same source-visible result.
inline int camera_bars_after_downbeats(int bars_left,
                                       std::uint32_t downbeats) {
    if (bars_left <= 0 || downbeats == 0) return bars_left;
    return std::max(0, bars_left - static_cast<int>(downbeats));
}

inline bool camera_pick_due_after_downbeat(int bars_left,
                                           bool guitarist_in_star_mode) {
    return !guitarist_in_star_mode && bars_left <= 0;
}

} // namespace ghogx::camera
