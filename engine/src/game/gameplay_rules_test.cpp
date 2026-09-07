#include "game/gameplay_rules.h"
#include "game/camera_intro_timing.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr, msg)                                                    \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::fprintf(stderr, "gameplay_rules_test: FAIL: %s\n", (msg));      \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

}  // namespace

int main() {
  using namespace ghogx::game;

  // The camera runs on elapsed presentation time, but the highway must cross
  // task-clock zero without adding/removing the user's audio calibration.
  for (int offset : {-500, -150, 0, 150, 500}) {
    const double prior_raw = ghogx::camera::intro_song_seconds(10.0 - 1.0/30.0, 10.0);
    const double prior_visual = calibrated_presentation_time(prior_raw, offset);
    const double zero_visual = calibrated_presentation_time(0.0, offset);
    CHECK(std::fabs((zero_visual - prior_visual) - 1.0/30.0) < 1.0e-9,
          "calibrated highway crosses preroll boundary without offset jump");
    CHECK(std::fabs(zero_visual + offset/1000.0) < 1.0e-9,
          "preroll endpoint matches the calibrated song-clock origin");
    // UI/SFX task timing uses the raw clock, not the calibrated chart clock.
    CHECK(std::fabs(ghogx::camera::intro_track_elapsed(10.0 + 0.05, 10.0, -2.0) - 2.05) < 1.0e-9,
          "meter task remains at +2.05 seconds regardless of calibration");
  }

  CHECK(gh1_arena_nondraw_helper_mesh("target_parent.mesh"),
        "GH1 Arena consumes target_parent as temporary runtime data");
  CHECK(gh1_arena_nondraw_helper_mesh("STAGE_SPOT_01.MESH"),
        "GH1 Arena helper classification is case-insensitive");
  CHECK(gh1_arena_nondraw_helper_mesh("walk_spot_03.mesh"),
        "GH1 Arena consumes numbered walk spots");
  CHECK(gh1_arena_nondraw_helper_mesh("fire_spot_01.mesh"),
        "GH1 Arena consumes numbered fire spots");
  CHECK(gh1_arena_nondraw_helper_mesh("name_lights_spot_02.mesh"),
        "GH1 Arena consumes numbered name-light spots");
  CHECK(gh1_arena_nondraw_helper_mesh("crowd_limits00.mesh"),
        "GH1 Arena accepts zero-based crowd-limit helpers");
  CHECK(!gh1_arena_nondraw_helper_mesh("stage_spot_00.mesh"),
        "one-based stage spots reject index zero");
  CHECK(!gh1_arena_nondraw_helper_mesh("target_parent_extra.mesh"),
        "helper classification does not use a broad target-parent prefix");
  CHECK(!gh1_arena_nondraw_helper_mesh("main_room_stage.mesh"),
        "ordinary venue geometry is not classified as an Arena helper");

  const FoFiXHitWindow window = fofix_hit_window_for_bpm(120.0);
  CHECK(window.early_sec > 0.099 && window.early_sec < 0.101,
        "GH2 watcher slop gives a 100 ms early margin");
  CHECK(window.late_sec > 0.099 && window.late_sec < 0.101,
        "GH2 watcher slop gives a 100 ms late margin");
  CHECK(fofix_note_in_window(10.0, 10.100, window),
        "note at positive edge is hittable");
  CHECK(!fofix_note_in_window(10.0, 10.101, window),
        "note beyond positive edge is too early");
  CHECK(fofix_note_missed(10.101, 10.0, window),
        "note beyond late edge is missed");
  CHECK(std::fabs(calibrated_judgement_time(10.0, 0) - 10.0) < 1.0e-9,
        "zero calibration preserves the audio master clock");
  CHECK(std::fabs(calibrated_judgement_time(10.0, 75) - 10.075) < 1.0e-9,
        "positive sync offset advances input judgement");
  CHECK(std::fabs(calibrated_judgement_time(10.0, -75) - 9.925) < 1.0e-9,
        "negative sync offset delays input judgement");
  CHECK(fofix_note_in_window(calibrated_judgement_time(10.075, -75),
                             10.0, window),
        "negative stored offset compensates a physically late input");
  CHECK(std::fabs(calibrated_presentation_time(10.0, 75) - 9.925) <
            1.0e-9,
        "positive audio latency delays the complete chart presentation");
  CHECK(std::fabs(calibrated_presentation_time(10.0, -75) - 10.075) <
            1.0e-9,
        "negative audio latency advances the complete chart presentation");

  CHECK(fofix_match_frets(0b00001, 0b00001), "green matches green");
  CHECK(fofix_match_frets(0b00011, 0b00010),
        "lower fret may be held under a single note");
  CHECK(!fofix_match_frets(0b00110, 0b00010),
        "higher fret blocks a single note");
  CHECK(fofix_match_frets(0b00110, 0b00110), "chord requires exact frets");
  CHECK(!fofix_match_frets(0b00111, 0b00110),
        "extra fret blocks chord");

  const std::vector<std::string> native_bassist =
      native_driver_clip_candidates(
          {"bassist_active_medium_01", "bassist_active_medium_02"});
  CHECK(native_bassist ==
            std::vector<std::string>({"bassist_active_medium_01",
                                      "bassist_active_medium_02",
                                      "bassist_active_medium"}),
        "numbered GH2 requests resolve to the authored native clip family");
  const std::vector<std::string> native_idle =
      native_driver_clip_candidates(
          {"bassist_idle_medium_01", "bassist_idle_medium_02"});
  CHECK(native_idle ==
            std::vector<std::string>({"bassist_idle_medium_01",
                                      "bassist_idle_medium_02",
                                      "bassist_idle_medium",
                                      "bassist_idle"}),
        "tempo-qualified idle requests resolve to native idle semantics");
  const std::vector<std::string> native_exact =
      native_driver_clip_candidates({"drummer_active_medium_normal"});
  CHECK(native_exact ==
            std::vector<std::string>({"drummer_active_medium_normal"}),
        "exact native clip names remain first and are not rewritten");
  const std::vector<std::string> native_namespaced =
      native_driver_clip_candidates(
          {"intro_01", "intro_03", "intro_04"}, "alterna");
  CHECK(native_namespaced ==
            std::vector<std::string>({"intro_01", "intro_03", "intro_04",
                                      "intro", "alterna_intro_01",
                                      "alterna_intro_03",
                                      "alterna_intro_04",
                                      "alterna_intro"}),
        "character-owned packages resolve their authored namespace");
  const std::vector<std::string> native_already_namespaced =
      native_driver_clip_candidates({"alterna_intro_01"}, "alterna");
  CHECK(native_already_namespaced ==
            std::vector<std::string>({"alterna_intro_01", "alterna_intro"}),
        "already namespaced clips are not prefixed twice");
  const std::vector<std::string> native_role_namespace =
      native_driver_clip_candidates({"singer_idle"}, "female_singer");
  CHECK(native_role_namespace ==
            std::vector<std::string>({"singer_idle", "female_singer_idle"}),
        "role-suffixed character namespaces replace the semantic role prefix");
  const NativeDriverClipSearch converted_bass_search =
      native_driver_clip_search_paths(
          {"char/metal_bass/anims/gen/metal_bass_main.milo_ps2"},
          "char/metal_bass/anims/gen/bass_main.milo_ps2");
  CHECK(converted_bass_search.driver_authoritative &&
            converted_bass_search.milos ==
                std::vector<std::string>({
                    "char/metal_bass/anims/gen/metal_bass_main.milo_ps2"}),
        "decoded driver paths exclude cross-provenance role packages");
  const NativeDriverClipSearch legacy_role_fallback =
      native_driver_clip_search_paths(
          {}, "char/metal_bass/anims/gen/bass_main.milo_ps2");
  CHECK(!legacy_role_fallback.driver_authoritative &&
            legacy_role_fallback.milos ==
                std::vector<std::string>({
                    "char/metal_bass/anims/gen/bass_main.milo_ps2"}),
        "role package remains the fallback for a driver with no clip path");
  const NativeDriverClipSearch deduplicated_driver_search =
      native_driver_clip_search_paths(
          {"", "owned.milo_ps2", "owned.milo_ps2"}, "fallback.milo_ps2");
  CHECK(deduplicated_driver_search.driver_authoritative &&
            deduplicated_driver_search.milos ==
                std::vector<std::string>({"owned.milo_ps2"}),
        "driver search ignores empty and duplicate paths");

  const auto horizontal_right =
      source_charwalk_direction_request(10.0f, 19.0f, 2, 0.5f);
  CHECK(horizontal_right.horizontal &&
            horizontal_right.walk_flags == 0x00001200u &&
            horizontal_right.turn_flags == 0x00000200u &&
            horizontal_right.stop_flags == 0u,
        "CharWalk classifies abs(y) < abs(2*x) as a lateral normal request");
  const auto horizontal_forward =
      source_charwalk_direction_request(-10.0f, 0.0f, 4, 0.1f);
  CHECK(horizontal_forward.horizontal &&
            horizontal_forward.walk_flags == 0x00002400u &&
            horizontal_forward.turn_flags == 0x00000100u &&
            horizontal_forward.stop_flags == 0u,
        "CharWalk's 25-percent branch walks forward but retains the turn hint");
  const auto threshold_forward =
      source_charwalk_direction_request(10.0f, 20.0f, 2, 0.5f);
  CHECK(!threshold_forward.horizontal &&
            threshold_forward.walk_flags == 0x00001400u &&
            threshold_forward.turn_flags == 0u &&
            threshold_forward.stop_flags == 0x00000400u,
        "CharWalk's strict lateral threshold falls through to forward");
  const auto backward_extreme =
      source_charwalk_direction_request(0.0f, -1.0f, 3, 0.5f);
  CHECK(!backward_extreme.horizontal &&
            backward_extreme.walk_flags == 0x00002800u &&
            backward_extreme.turn_flags == 0x00000800u &&
            backward_extreme.stop_flags == 0x00000800u,
        "CharWalk carries backward hints through an extreme request");
  CHECK(source_charwalk_delay_sample(false, 1.0f, 2.0f, 0.5f) >
            9.0e29f,
        "disabled CharWalk delay rows use retail's 1e30 sentinel");
  CHECK(source_charwalk_delay_sample(true, 20.0f, 40.0f, 0.25f) ==
            25.0f,
        "enabled CharWalk delay rows use a bounded float sample");
  CHECK(source_charwalk_delay_remaining(25.0f, 10.0, 35.5) == -0.5,
        "CharWalk delay remaining subtracts elapsed time from its sample");
  const double request_deadline =
      source_charwalk_request_deadline(35.5, 6.0f);
  CHECK(request_deadline == 41.5 &&
            !source_charwalk_request_expired(true, request_deadline, 41.49) &&
            source_charwalk_request_expired(true, request_deadline, 41.5),
        "CharWalk request window lasts max_walk_wait seconds");
  const std::vector<SourceCharWalkWaypointGraphNode> authored_waypoints = {
      {0x00u, {1u, 2u, 3u}, {0.0f, 10.0f, 0.0f}},
      {0x40u, {0u}, {-10.0f, 0.0f, 0.0f}},
      {0x41u, {0u}, {10.0f, 0.0f, 0.0f}},
      {0xcc0u, {0u, 4u}, {0.0f, 20.0f, 0.0f}},
      {0x40u, {3u}, {0.0f, 30.0f, 0.0f}}};
  std::vector<size_t> waypoint_registry = {0u, 1u, 2u, 3u, 4u};
  CHECK(source_charwalk_find_nearest_waypoint(
            authored_waypoints, waypoint_registry,
            {-9.0f, 0.0f, 0.0f}, 0x40u) ==
            std::optional<size_t>(1u),
        "CharWalk nearest uses overlapping flags and squared world distance");
  CHECK(source_charwalk_find_nearest_waypoint(
            authored_waypoints, {2u, 1u, 0u, 3u, 4u},
            {0.0f, 0.0f, 0.0f}, 0x40u) ==
            std::optional<size_t>(2u),
        "CharWalk nearest retains registry order when distances tie");
  const auto route_from_left = source_charwalk_find_route(
      authored_waypoints, 1u, 0x40u, 0x40u, waypoint_registry);
  CHECK(route_from_left.destination_index == std::optional<size_t>(2u) &&
            route_from_left.path ==
                std::vector<size_t>({0u, 2u}) &&
            waypoint_registry ==
                std::vector<size_t>({0u, 1u, 3u, 4u, 2u}),
        "CharWalk follows authored connections and moves its destination last");
  const auto route_from_right = source_charwalk_find_route(
      authored_waypoints, 2u, 0x40u, 0x40u, waypoint_registry);
  CHECK(route_from_right.destination_index == std::optional<size_t>(1u) &&
            route_from_right.path ==
                std::vector<size_t>({0u, 1u}) &&
            waypoint_registry ==
                std::vector<size_t>({0u, 3u, 4u, 2u, 1u}),
        "CharWalk reuses the rotated global registry on its next route");
  const auto blocked_route = source_charwalk_find_route(
      authored_waypoints, 4u, 0x01u, 0x40u, waypoint_registry);
  CHECK(!blocked_route.destination_index && blocked_route.path.empty(),
        "CharWalk rejects routes that require a blocked intermediate walk spot");
  const SourceCharWalkTurnCandidateScore aligned_turn =
      source_charwalk_turn_candidate_score(
          {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
          {10.0f, 0.0f, 0.0f}, -1.5707963267948966f,
          {0.0f, 1.0f, 0.0f}, 0.0f);
  CHECK(aligned_turn.accepted && aligned_turn.angular_error < 1.0e-5f &&
            aligned_turn.remaining_distance_squared == 81.0f,
        "CharWalk turn scorer accepts the closer aligned candidate");
  const SourceCharWalkTurnCandidateScore farther_turn =
      source_charwalk_turn_candidate_score(
          {0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
          {10.0f, 0.0f, 0.0f}, 0.0f,
          {0.0f, 1.0f, 0.0f}, 0.0f);
  CHECK(!farther_turn.accepted &&
            farther_turn.remaining_distance_squared == 121.0f,
        "CharWalk turn scorer rejects a farther candidate");
  const auto corridor =
      source_charwalk_regulate_corridor(
          {{{0.0f, 0.0f, 0.0f},
            {0.0f, 10.0f, 0.0f}}},
          1, {4.0f, 8.0f, 0.0f}, {3.0f, 5.0f, 0.0f},
          {0.0f, 0.0f, 0.0f}, 2.0f, 1.0f);
  CHECK(corridor.valid && corridor.waypoint_index == 1 &&
            std::fabs(corridor.regulated_back_position[0] - 2.0f) <
                1.0e-5f &&
            std::fabs(corridor.regulated_back_position[1] - 6.0f) <
                1.0e-5f &&
            corridor.yaw_adjustment > 0.0f,
        "CharWalk regulator confines backward prediction to path radius");
  const auto mirrored_corridor =
      source_charwalk_regulate_corridor(
          {{{0.0f, 0.0f, 0.0f},
            {0.0f, 10.0f, 0.0f}}},
          1, {-4.0f, 8.0f, 0.0f}, {-3.0f, 5.0f, 0.0f},
          {0.0f, 0.0f, 0.0f}, 2.0f, 1.0f);
  CHECK(mirrored_corridor.valid &&
            mirrored_corridor.yaw_adjustment < 0.0f,
        "CharWalk regulator preserves the signed source cross-product turn");
  const auto crossed =
      source_charwalk_regulate_corridor(
          {{{0.0f, 0.0f, 0.0f},
            {0.0f, 10.0f, 0.0f},
            {10.0f, 10.0f, 0.0f}}},
          1, {1.0f, 11.0f, 0.0f}, {0.0f, 10.0f, 0.0f},
          {0.0f, 0.0f, 0.0f}, 12.0f, 1.0f);
  CHECK(crossed.valid && crossed.waypoint_index == 2 &&
            crossed.waypoint_direction[0] == 1.0f &&
            crossed.waypoint_direction[1] == 0.0f,
        "CharWalk regulator advances after crossing the waypoint plane");
  const auto flagged_after =
      source_char_clip_group_flag_selection(
          {0x10u, 0x30u, 0x20u, 0x31u}, 1, 0x21u);
  CHECK(flagged_after.selected_index == std::optional<size_t>(3) &&
            flagged_after.promoted_index == 2,
        "flagged CharClipGroup selection scans forward from mWhich plus one");
  const auto flagged_wrap =
      source_char_clip_group_flag_selection(
          {0x31u, 0x10u, 0x20u, 0x30u}, 3, 0x21u);
  CHECK(flagged_wrap.selected_index == std::optional<size_t>(0) &&
            flagged_wrap.promoted_index == 0,
        "flagged CharClipGroup selection wraps to source-order zero");
  const auto flagged_missing =
      source_char_clip_group_flag_selection(
          {0x10u, 0x20u}, 0, 0x03u);
  CHECK(!flagged_missing.selected_index,
        "flagged CharClipGroup selection rejects partial flag matches");

  FoFiXScoreState score;
  for (int i = 0; i < 9; ++i) {
    fofix_apply_hit(score, 1);
  }
  CHECK(score.score == 450 && score.streak == 9 && score.multiplier == 1,
        "first nine notes score at 1x");
  const FoFiXScoreAward tenth = fofix_apply_hit(score, 1);
  CHECK(tenth.points == 100 && score.multiplier == 2,
        "tenth note scores at 2x");
  fofix_apply_hit(score, 2);
  CHECK(score.score == 750, "two-note chord scores both gems at multiplier");
  fofix_apply_miss(score);
  CHECK(score.streak == 0 && score.multiplier == 1,
        "miss resets streak and multiplier");
  FoFiXScoreAward powered = fofix_apply_hit(score, 1, 2);
  CHECK(powered.points == 100 && score.score == 850,
        "active star power doubles note score before streak multiplier");

  FoFiXRockState rock;
  CHECK(fofix_rock_fill(rock) > 0.499 && fofix_rock_fill(rock) < 0.501,
        "rock meter starts halfway");
  fofix_apply_rock_hit(rock);
  CHECK(rock.value == 15022.0 && rock.plus_amount == 22.0,
        "first normal hit raises rock by ramped plus amount");
  fofix_apply_rock_miss(rock);
  CHECK(rock.value == 14620.0 && rock.minus_amount == 402.0 &&
            rock.plus_amount == 15.0,
        "normal miss ramps penalty and clamps recovery amount");
  fofix_apply_rock_overstrum(rock);
  CHECK(rock.value > 14539.5 && rock.value < 14540.0 &&
            rock.minus_amount > 402.3 && rock.minus_amount < 402.5,
        "overstrum applies FoFiX lessMissed-style rock penalty");
  FoFiXRockState powered_rock;
  fofix_apply_rock_hit(powered_rock, 2.0);
  CHECK(powered_rock.value == 15058.0 &&
            powered_rock.plus_amount == 29.0,
        "active star power doubles FoFiX rock gain via multi");
  fofix_apply_rock_miss(powered_rock, 2.0);
  CHECK(powered_rock.value == 14857.5 &&
            powered_rock.minus_amount == 401.0,
        "active star power softens FoFiX rock miss penalty via multi");

  FoFiXStarPowerState star;
  CHECK(!fofix_activate_star_power(star), "cannot activate below half meter");
  fofix_award_star_phrase(star);
  CHECK(fofix_star_power_fill(star) > 0.249 &&
            fofix_star_power_fill(star) < 0.251,
        "completed star phrase awards a quarter meter");
  for (int i = 0; i < 8; ++i) {
    fofix_award_star_phrase(star);
  }
  CHECK(fofix_star_power_fill(star) == 1.0,
        "star power is capped at a full meter");
  fofix_set_star_power_fill(star, 0.5);
  CHECK(!star.active && fofix_star_power_fill(star) == 0.5,
        "diagnostic star power fill seeds meter without forcing activation");
  fofix_set_star_power_fill(star, 1.0);
  CHECK(fofix_activate_star_power(star), "can activate at or above half meter");
  CHECK(fofix_star_power_score_multiplier(star) == 2,
        "active star power doubles scoring");
  fofix_update_star_power(star, 4.0);
  CHECK(star.active && fofix_star_power_fill(star) > 0.499 &&
            fofix_star_power_fill(star) < 0.501,
        "star power drains at the GH2 deploy rate");
  fofix_update_star_power(star, 4.0);
  CHECK(!star.active && fofix_star_power_fill(star) == 0.0,
        "star power deactivates when drained");

  CHECK(fofix_sustain_score(0.13, 1, 0.5, 4) == 0,
        "sustain scoring waits until past FoFiX quarter-beat threshold");
  CHECK(fofix_sustain_score(1.0, 1, 0.5, 4) == 400,
        "single-note one-second sustain scores through multiplier");
  CHECK(fofix_sustain_score(1.0, 2, 0.5, 2) == 400,
        "chord sustain scores each held gem");

  if (failures == 0) {
    std::fprintf(stderr, "gameplay_rules_test: PASS\n");
  }
  return failures == 0 ? 0 : 1;
}
