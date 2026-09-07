#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghogx::game {

struct FoFiXHitWindow {
  double early_sec = 0.0;
  double late_sec = 0.0;
};

struct FoFiXScoreState {
  int score = 0;
  int streak = 0;
  int multiplier = 1;
};

struct FoFiXScoreAward {
  int points = 0;
  int multiplier = 1;
};

struct FoFiXRockState {
  double value = 15000.0;
  double minus_amount = 400.0;
  double plus_amount = 15.0;
};

struct FoFiXStarPowerState {
  double value = 0.0;
  bool active = false;
};

FoFiXHitWindow fofix_hit_window_for_bpm(double bpm);

// Soundcheck's Audio value delays/advances the complete chart presentation
// against the decoded audio-master position. Video/Input then shifts only the
// input judgement clock. Neither value changes the hit-window width.
double calibrated_presentation_time(double audio_time_sec,
                                    int audio_offset_ms);
double calibrated_judgement_time(double audio_time_sec, int sync_offset_ms);

bool fofix_note_in_window(double song_time,
                          double note_time,
                          const FoFiXHitWindow& window);

bool fofix_note_missed(double song_time,
                       double note_time,
                       const FoFiXHitWindow& window);

bool fofix_match_frets(uint32_t held_frets, uint32_t required_frets);

int fofix_multiplier_for_streak(int streak);

FoFiXScoreAward fofix_apply_hit(FoFiXScoreState& state,
                                int gem_count,
                                int power_multiplier = 1);

void fofix_apply_miss(FoFiXScoreState& state);

void fofix_apply_rock_hit(FoFiXRockState& state,
                          double power_multiplier = 1.0);

void fofix_apply_rock_miss(FoFiXRockState& state,
                           double power_multiplier = 1.0);

void fofix_apply_rock_overstrum(FoFiXRockState& state,
                                double power_multiplier = 1.0);

void fofix_set_rock_fill(FoFiXRockState& state, double fill);

double fofix_rock_fill(const FoFiXRockState& state);

bool fofix_rock_failed(const FoFiXRockState& state);

void fofix_award_star_phrase(FoFiXStarPowerState& state);

void fofix_set_star_power_fill(FoFiXStarPowerState& state, double fill);

bool fofix_activate_star_power(FoFiXStarPowerState& state);

void fofix_update_star_power(FoFiXStarPowerState& state, double dt_seconds);

double fofix_star_power_fill(const FoFiXStarPowerState& state);

int fofix_star_power_score_multiplier(const FoFiXStarPowerState& state);

int fofix_sustain_score(double held_seconds,
                        int note_count,
                        double beat_seconds,
                        int multiplier);

// Resolves GH2's numbered/tempo-qualified requests against the semantic clip
// families authored in a character-owned native driver package. A converted
// package may retain its source character namespace; that namespace remains a
// package fact rather than a character-specific gameplay rule. Callers must
// exhaust this ordered list before consulting shared fallback packages.
std::vector<std::string> native_driver_clip_candidates(
    const std::vector<std::string>& requested,
    std::string_view character_namespace = {});

struct NativeDriverClipSearch {
  std::vector<std::string> milos;
  bool driver_authoritative = false;
};

// CharDriver owns its clip-directory list. Shared role packages are only a
// compatibility fallback when the decoded driver publishes no usable path;
// they must not supplement a character-owned package from another provenance.
NativeDriverClipSearch native_driver_clip_search_paths(
    const std::vector<std::string>& driver_milos,
    std::string_view role_fallback_milo);

struct SourceCharWalkDirectionRequest {
  uint32_t walk_flags = 0;
  uint32_t turn_flags = 0;
  uint32_t stop_flags = 0;
  bool horizontal = false;
};

// Reproduces the retail GH2 start-task direction classifier and the flag
// tuple passed into CharWalk::Start. random_unit is consumed only for a
// horizontal request, where retail substitutes forward movement 25% of the
// time while retaining the lateral turn hint.
SourceCharWalkDirectionRequest source_charwalk_direction_request(
    float local_x,
    float local_y,
    int excitement,
    float random_unit);

// Retail stores 1e30 for a disabled walk-delay row. Enabled rows are sampled
// with RandomFloat(minimum, maximum).
float source_charwalk_delay_sample(bool enabled,
                                   float minimum,
                                   float maximum,
                                   float random_unit);

double source_charwalk_delay_remaining(float sampled_delay,
                                       double epoch,
                                       double now);

double source_charwalk_request_deadline(double now, float max_walk_wait);

bool source_charwalk_request_expired(bool active,
                                     double deadline,
                                     double now);

struct SourceCharWalkWaypointGraphNode {
  uint32_t flags = 0;
  std::vector<size_t> connections;
  std::array<float, 3> position = {};
};

struct SourceCharWalkRouteSelection {
  std::vector<size_t> path;
  std::optional<size_t> destination_index;
};

// GH2 Waypoint::FindNearest scans the mutable registry in order, accepts any
// overlapping flag bit, and keeps the first node at the strict minimum
// squared world-position distance.
std::optional<size_t> source_charwalk_find_nearest_waypoint(
    const std::vector<SourceCharWalkWaypointGraphNode>& graph,
    const std::vector<size_t>& registry_order,
    const std::array<float, 3>& position,
    uint32_t required_mask);

// GH2 Waypoint::FindPath scans the mutable global waypoint registry in its
// current order. For each non-source node sharing any destination-mask bit it
// performs a source-order depth-first connection search. Intermediate nodes
// sharing any blocked-mask bit are rejected, while the destination itself is
// allowed. A successful destination is moved to the registry tail.
SourceCharWalkRouteSelection source_charwalk_find_route(
    const std::vector<SourceCharWalkWaypointGraphNode>& graph,
    size_t source_index,
    uint32_t destination_mask,
    uint32_t blocked_mask,
    std::vector<size_t>& registry_order);

struct SourceCharClipGroupFlagSelection {
  std::optional<size_t> selected_index;
  size_t promoted_index = 0;
};

// GH2 CharClipGroup::GetClip(int) scans cyclically from mWhich + 1 for the
// first clip containing every requested flag. SetWhich then promotes that
// source entry into the next cyclic slot.
SourceCharClipGroupFlagSelection source_char_clip_group_flag_selection(
    const std::vector<uint32_t>& source_order_flags,
    int32_t which,
    uint32_t required_flags);

struct SourceCharWalkTurnCandidateScore {
  bool accepted = false;
  float remaining_distance_squared = 0.0f;
  float angular_error = 0.0f;
};

// GH2 CharWalk's turn chooser first rejects candidates that move farther from
// the destination, then minimizes the absolute wrapped heading error.
SourceCharWalkTurnCandidateScore source_charwalk_turn_candidate_score(
    const std::array<float, 3>& baseline_position,
    const std::array<float, 3>& candidate_position,
    const std::array<float, 3>& target_position,
    float candidate_yaw,
    const std::array<float, 3>& walk_step_position,
    float walk_clip_range);

struct SourceCharWalkCorridorRegulation {
  bool valid = false;
  size_t waypoint_index = 0;
  std::array<float, 3> waypoint_direction = {0.0f, 1.0f, 0.0f};
  std::array<float, 3> regulated_back_position = {};
  float yaw_adjustment = 0.0f;
};

// Reproduces the geometric half of GH2 CharWalk::RegulateWalk. The current
// waypoint is advanced only after the forward prediction crosses its plane;
// the backward prediction is then confined to the source path-radius corridor
// before the signed source rotation adjustment is returned.
SourceCharWalkCorridorRegulation source_charwalk_regulate_corridor(
    const std::vector<std::array<float, 3>>& path,
    size_t waypoint_index,
    const std::array<float, 3>& predicted_position,
    const std::array<float, 3>& backward_position,
    const std::array<float, 3>& current_position,
    float path_radius,
    float frame_delta);

// GH1 Arena consumes these transform/placement Mesh families as runtime data
// before ordinary venue drawing. The classification follows the recovered
// retail naming contract and is shared by main and section RndDirs.
bool gh1_arena_nondraw_helper_mesh(std::string_view name);

}  // namespace ghogx::game
