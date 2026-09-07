#include "game/gameplay_rules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>

namespace ghogx::game {

namespace {

constexpr double kMillisecondsPerSecond = 1000.0;
constexpr double kGh2WatcherSlopMs = 100.0;
constexpr double kRockMax = 30000.0;
constexpr double kMinBase = 400.0;
constexpr double kPlusBase = 15.0;
constexpr double kMinGain = 2.0;
constexpr double kPlusGain = 7.0;
constexpr double kStarPhraseAward = 25.0;
constexpr double kStarActivationThreshold = 50.0;
constexpr double kGh2StarDeployRate = 0.125;
constexpr double kBaseSustainScore = 0.1;

int popcount5(uint32_t mask) {
  int count = 0;
  mask &= 0x1fu;
  while (mask != 0) {
    count += static_cast<int>(mask & 1u);
    mask >>= 1;
  }
  return count;
}

int first_lane(uint32_t mask) {
  for (int lane = 0; lane < 5; ++lane) {
    if ((mask & (1u << lane)) != 0) return lane;
  }
  return 0;
}

}  // namespace

FoFiXHitWindow fofix_hit_window_for_bpm(double bpm) {
  (void)bpm;
  const double margin_sec = kGh2WatcherSlopMs / kMillisecondsPerSecond;
  return FoFiXHitWindow{margin_sec, margin_sec};
}

bool fofix_note_in_window(double song_time,
                          double note_time,
                          const FoFiXHitWindow& window) {
  return note_time >= song_time - window.late_sec &&
         note_time <= song_time + window.early_sec;
}

bool fofix_note_missed(double song_time,
                       double note_time,
                       const FoFiXHitWindow& window) {
  return note_time < song_time - window.late_sec;
}

bool fofix_match_frets(uint32_t held_frets, uint32_t required_frets) {
  held_frets &= 0x1fu;
  required_frets &= 0x1fu;
  const int required_count = popcount5(required_frets);
  if (required_count == 0) return held_frets == 0;

  if (required_count > 1) {
    return held_frets == required_frets;
  }

  const int lane = first_lane(required_frets);
  const uint32_t required_bit = 1u << lane;
  if ((held_frets & required_bit) == 0) return false;

  const uint32_t higher_frets = 0x1fu & ~((1u << (lane + 1)) - 1u);
  return (held_frets & higher_frets) == 0;
}

int fofix_multiplier_for_streak(int streak) {
  if (streak >= 30) return 4;
  if (streak >= 20) return 3;
  if (streak >= 10) return 2;
  return 1;
}

FoFiXScoreAward fofix_apply_hit(FoFiXScoreState& state,
                                int gem_count,
                                int power_multiplier) {
  ++state.streak;
  state.multiplier = fofix_multiplier_for_streak(state.streak);
  const int gems = std::max(1, gem_count);
  const int points = gems * 50 * state.multiplier *
                     std::max(1, power_multiplier);
  state.score += points;
  return FoFiXScoreAward{points, state.multiplier};
}

void fofix_apply_miss(FoFiXScoreState& state) {
  state.streak = 0;
  state.multiplier = 1;
}

void fofix_apply_rock_hit(FoFiXRockState& state, double power_multiplier) {
  power_multiplier = std::max(1.0, power_multiplier);
  if (state.value < kRockMax) {
    state.plus_amount += kPlusGain * power_multiplier;
    state.value += state.plus_amount * power_multiplier;
  }
  state.value = std::clamp(state.value, 0.0, kRockMax);
  if (state.minus_amount > kMinBase) {
    state.minus_amount -= (kMinGain / 2.0) * power_multiplier;
  }
  state.minus_amount = std::max(state.minus_amount, kMinBase);
  state.plus_amount = std::max(state.plus_amount, kPlusBase);
}

void fofix_apply_rock_miss(FoFiXRockState& state, double power_multiplier) {
  power_multiplier = std::max(1.0, power_multiplier);
  state.minus_amount += kMinGain / power_multiplier;
  const double rock_minus = state.minus_amount / power_multiplier;
  state.value -= rock_minus;
  if (state.plus_amount > kPlusBase) {
    state.plus_amount -= (kPlusGain * 2.0) / power_multiplier;
  }
  state.value = std::clamp(state.value, 0.0, kRockMax);
  state.minus_amount = std::max(state.minus_amount, kMinBase);
  state.plus_amount = std::max(state.plus_amount, kPlusBase);
}

void fofix_apply_rock_overstrum(FoFiXRockState& state,
                                double power_multiplier) {
  power_multiplier = std::max(1.0, power_multiplier);
  state.minus_amount += kMinGain / 5.0 / power_multiplier;
  const double rock_minus = state.minus_amount / 5.0 / power_multiplier;
  state.value -= rock_minus;
  if (state.plus_amount > kPlusBase) {
    state.plus_amount -= kPlusGain / 2.5 / power_multiplier;
  }
  state.value = std::clamp(state.value, 0.0, kRockMax);
  state.minus_amount = std::max(state.minus_amount, kMinBase);
  state.plus_amount = std::max(state.plus_amount, kPlusBase);
}

void fofix_set_rock_fill(FoFiXRockState& state, double fill) {
  const double sane_fill = std::isfinite(fill) ? fill : 0.0;
  state.value = std::clamp(sane_fill, 0.0, 1.0) * kRockMax;
  state.minus_amount = kMinBase;
  state.plus_amount = kPlusBase;
}

double fofix_rock_fill(const FoFiXRockState& state) {
  return std::clamp(state.value / kRockMax, 0.0, 1.0);
}

bool fofix_rock_failed(const FoFiXRockState& state) {
  return state.value <= 0.0;
}

void fofix_award_star_phrase(FoFiXStarPowerState& state) {
  state.value = std::clamp(state.value + kStarPhraseAward, 0.0, 100.0);
}

void fofix_set_star_power_fill(FoFiXStarPowerState& state, double fill) {
  const double sane_fill = std::isfinite(fill) ? fill : 0.0;
  state.value = std::clamp(sane_fill, 0.0, 1.0) * 100.0;
  if (state.value <= 0.0) state.active = false;
}

bool fofix_activate_star_power(FoFiXStarPowerState& state) {
  if (state.active || state.value < kStarActivationThreshold) return false;
  state.active = true;
  return true;
}

void fofix_update_star_power(FoFiXStarPowerState& state, double dt_seconds) {
  if (!state.active || dt_seconds <= 0.0) return;
  state.value -= dt_seconds * kGh2StarDeployRate * 100.0;
  if (state.value <= 0.0) {
    state.value = 0.0;
    state.active = false;
  }
}

double fofix_star_power_fill(const FoFiXStarPowerState& state) {
  return std::clamp(state.value / 100.0, 0.0, 1.0);
}

int fofix_star_power_score_multiplier(const FoFiXStarPowerState& state) {
  return state.active ? 2 : 1;
}

int fofix_sustain_score(double held_seconds,
                        int note_count,
                        double beat_seconds,
                        int multiplier) {
  if (held_seconds <= 0.0 || note_count <= 0 || beat_seconds <= 0.0)
    return 0;
  if (held_seconds <= 1.1 * beat_seconds / 4.0)
    return 0;
  const double held_ms = held_seconds * kMillisecondsPerSecond;
  const int base_score =
      static_cast<int>(kBaseSustainScore * held_ms *
                       static_cast<double>(note_count));
  return base_score * std::max(1, multiplier);
}

double calibrated_presentation_time(double audio_time_sec,
                                    int audio_offset_ms) {
  return audio_time_sec -
         static_cast<double>(std::clamp(audio_offset_ms, -500, 500)) /
             kMillisecondsPerSecond;
}

double calibrated_judgement_time(double audio_time_sec, int sync_offset_ms) {
  return audio_time_sec +
         static_cast<double>(std::clamp(sync_offset_ms, -500, 500)) /
             kMillisecondsPerSecond;
}

std::vector<std::string> native_driver_clip_candidates(
    const std::vector<std::string>& requested,
    std::string_view character_namespace) {
  std::vector<std::string> result;
  std::vector<std::string> semantic_names;
  auto push_unique = [&](std::string name) {
    if (name.empty() ||
        std::find(result.begin(), result.end(), name) != result.end()) {
      return;
    }
    result.push_back(std::move(name));
  };
  auto push_semantic = [&](std::string name) {
    if (name.empty() ||
        std::find(semantic_names.begin(), semantic_names.end(), name) !=
            semantic_names.end()) {
      return;
    }
    semantic_names.push_back(std::move(name));
  };

  for (const auto& name : requested) push_semantic(name);
  for (const auto& name : requested) {
    std::string stem = name;
    if (stem.size() >= 3 && stem[stem.size() - 3] == '_' &&
        std::isdigit(static_cast<unsigned char>(stem[stem.size() - 2])) &&
        std::isdigit(static_cast<unsigned char>(stem[stem.size() - 1]))) {
      stem.resize(stem.size() - 3);
      push_semantic(stem);
    }

    const size_t idle_tempo = stem.find("_idle_");
    if (idle_tempo != std::string::npos) {
      push_semantic(stem.substr(0, idle_tempo + 5));
    }
  }
  for (const auto& name : semantic_names) push_unique(name);

  if (!character_namespace.empty()) {
    const std::string prefix(character_namespace);
    for (const auto& name : semantic_names) {
      if (name == prefix ||
          (name.size() > prefix.size() &&
           name.compare(0, prefix.size(), prefix) == 0 &&
           name[prefix.size()] == '_')) {
        continue;
      }
      // Role-owned character namespaces commonly retain the role as their
      // suffix (for example <variant>_singer). In that case the native clip
      // replaces the semantic role prefix instead of duplicating it:
      // singer_idle -> <variant>_singer_idle.
      const size_t semantic_separator = name.find('_');
      if (semantic_separator != std::string::npos) {
        const std::string_view semantic_role(name.data(),
                                             semantic_separator);
        const bool namespace_ends_with_role =
            prefix == semantic_role ||
            (prefix.size() > semantic_role.size() &&
             prefix[prefix.size() - semantic_role.size() - 1] == '_' &&
             prefix.compare(prefix.size() - semantic_role.size(),
                            semantic_role.size(), semantic_role) == 0);
        if (namespace_ends_with_role) {
          push_unique(prefix + name.substr(semantic_separator));
          continue;
        }
      }
      push_unique(prefix + "_" + name);
    }
  }
  return result;
}

NativeDriverClipSearch native_driver_clip_search_paths(
    const std::vector<std::string>& driver_milos,
    std::string_view role_fallback_milo) {
  NativeDriverClipSearch result;
  for (const auto& milo : driver_milos) {
    if (milo.empty() ||
        std::find(result.milos.begin(), result.milos.end(), milo) !=
            result.milos.end()) {
      continue;
    }
    result.milos.push_back(milo);
  }
  result.driver_authoritative = !result.milos.empty();
  if (!result.driver_authoritative && !role_fallback_milo.empty()) {
    result.milos.emplace_back(role_fallback_milo);
  }
  return result;
}

SourceCharWalkDirectionRequest source_charwalk_direction_request(
    float local_x,
    float local_y,
    int excitement,
    float random_unit) {
  constexpr uint32_t kLeft = 0x00000100u;
  constexpr uint32_t kRight = 0x00000200u;
  constexpr uint32_t kForward = 0x00000400u;
  constexpr uint32_t kBackward = 0x00000800u;
  constexpr uint32_t kNormal = 0x00001000u;
  constexpr uint32_t kExtreme = 0x00002000u;

  SourceCharWalkDirectionRequest result;
  result.horizontal =
      std::fabs(local_y) < std::fabs(2.0f * local_x);
  if (result.horizontal) {
    const uint32_t lateral = local_x > 0.0f ? kRight : kLeft;
    result.turn_flags = lateral;
    result.walk_flags =
        random_unit < 0.25f ? kForward : lateral;
  } else {
    const uint32_t vertical =
        local_y > 0.0f ? kForward : kBackward;
    result.walk_flags = vertical;
    result.turn_flags = vertical == kBackward ? kBackward : 0u;
    result.stop_flags = vertical;
  }
  result.walk_flags |= excitement < 3 ? kNormal : kExtreme;
  return result;
}

float source_charwalk_delay_sample(bool enabled,
                                   float minimum,
                                   float maximum,
                                   float random_unit) {
  if (!enabled) return 1.0e30f;
  const float high = std::max(minimum, maximum);
  const float unit = std::clamp(random_unit, 0.0f, 1.0f);
  return minimum + (high - minimum) * unit;
}

double source_charwalk_delay_remaining(float sampled_delay,
                                       double epoch,
                                       double now) {
  return static_cast<double>(sampled_delay) - (now - epoch);
}

double source_charwalk_request_deadline(double now, float max_walk_wait) {
  return now + static_cast<double>(std::max(0.0f, max_walk_wait));
}

bool source_charwalk_request_expired(bool active,
                                     double deadline,
                                     double now) {
  return active && now >= deadline;
}

std::optional<size_t> source_charwalk_find_nearest_waypoint(
    const std::vector<SourceCharWalkWaypointGraphNode>& graph,
    const std::vector<size_t>& registry_order,
    const std::array<float, 3>& position,
    uint32_t required_mask) {
  std::optional<size_t> nearest;
  float nearest_distance_squared =
      std::numeric_limits<float>::max();
  for (const size_t index : registry_order) {
    if (index >= graph.size() ||
        (graph[index].flags & required_mask) == 0) {
      continue;
    }
    const float dx = graph[index].position[0] - position[0];
    const float dy = graph[index].position[1] - position[1];
    const float dz = graph[index].position[2] - position[2];
    const float distance_squared =
        dx * dx + dy * dy + dz * dz;
    if (distance_squared < nearest_distance_squared) {
      nearest_distance_squared = distance_squared;
      nearest = index;
    }
  }
  return nearest;
}

SourceCharWalkRouteSelection source_charwalk_find_route(
    const std::vector<SourceCharWalkWaypointGraphNode>& graph,
    size_t source_index,
    uint32_t destination_mask,
    uint32_t blocked_mask,
    std::vector<size_t>& registry_order) {
  SourceCharWalkRouteSelection result;
  if (source_index >= graph.size() || graph.empty()) return result;

  if (registry_order.size() != graph.size()) {
    registry_order.resize(graph.size());
    std::iota(registry_order.begin(), registry_order.end(), 0);
  } else {
    std::vector<bool> present(graph.size(), false);
    bool valid = true;
    for (const size_t index : registry_order) {
      if (index >= graph.size() || present[index]) {
        valid = false;
        break;
      }
      present[index] = true;
    }
    if (!valid) {
      std::iota(registry_order.begin(), registry_order.end(), 0);
    }
  }

  for (auto target_it = registry_order.begin();
       target_it != registry_order.end(); ++target_it) {
    const size_t target_index = *target_it;
    if (target_index == source_index ||
        (graph[target_index].flags & destination_mask) == 0) {
      continue;
    }

    std::vector<bool> visited(graph.size(), false);
    result.path.clear();
    const auto search = [&](auto&& self, const size_t node_index) -> bool {
      if (node_index == target_index) return true;
      visited[node_index] = true;
      for (const size_t connected_index :
           graph[node_index].connections) {
        if (connected_index >= graph.size() ||
            visited[connected_index]) {
          continue;
        }
        if (connected_index != target_index &&
            (graph[connected_index].flags & blocked_mask) != 0) {
          continue;
        }
        result.path.push_back(connected_index);
        if (self(self, connected_index)) return true;
        result.path.pop_back();
      }
      return false;
    };

    if (!search(search, source_index)) continue;
    result.destination_index = target_index;
    const size_t selected_registry_index =
        static_cast<size_t>(
            std::distance(registry_order.begin(), target_it));
    std::rotate(
        registry_order.begin() +
            static_cast<std::ptrdiff_t>(selected_registry_index),
        registry_order.begin() +
            static_cast<std::ptrdiff_t>(selected_registry_index + 1),
        registry_order.end());
    return result;
  }

  result.path.clear();
  return result;
}

SourceCharClipGroupFlagSelection source_char_clip_group_flag_selection(
    const std::vector<uint32_t>& source_order_flags,
    int32_t which,
    uint32_t required_flags) {
  SourceCharClipGroupFlagSelection result;
  if (source_order_flags.empty()) return result;

  const size_t count = source_order_flags.size();
  const size_t current =
      which >= 0 && static_cast<size_t>(which) < count
          ? static_cast<size_t>(which)
          : count - 1;
  result.promoted_index = (current + 1) % count;
  auto matches = [&](size_t index) {
    return (source_order_flags[index] & required_flags) ==
           required_flags;
  };
  for (size_t index = result.promoted_index; index < count; ++index) {
    if (matches(index)) {
      result.selected_index = index;
      return result;
    }
  }
  for (size_t index = 0; index <= current; ++index) {
    if (matches(index)) {
      result.selected_index = index;
      return result;
    }
  }
  return result;
}

SourceCharWalkTurnCandidateScore source_charwalk_turn_candidate_score(
    const std::array<float, 3>& baseline_position,
    const std::array<float, 3>& candidate_position,
    const std::array<float, 3>& target_position,
    float candidate_yaw,
    const std::array<float, 3>& walk_step_position,
    float walk_clip_range) {
  auto distance_squared = [&](const std::array<float, 3>& position) {
    const float dx = target_position[0] - position[0];
    const float dy = target_position[1] - position[1];
    const float dz = target_position[2] - position[2];
    return dx * dx + dy * dy + dz * dz;
  };

  SourceCharWalkTurnCandidateScore result;
  const float baseline_distance_squared =
      distance_squared(baseline_position);
  result.remaining_distance_squared =
      distance_squared(candidate_position);
  if (baseline_distance_squared < result.remaining_distance_squared) {
    return result;
  }

  const float walk_heading =
      std::atan2(walk_step_position[0], walk_step_position[1]);
  const float target_heading =
      std::atan2(target_position[0] - candidate_position[0],
                 target_position[1] - candidate_position[1]);
  const float correction =
      -walk_clip_range * 0.5f + walk_heading - target_heading;
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 6.28318530717958647692f;
  float wrapped =
      std::fmod(candidate_yaw - correction + kPi, kTwoPi);
  wrapped = wrapped < 0.0f ? wrapped + kPi : wrapped - kPi;
  result.accepted = true;
  result.angular_error = std::fabs(wrapped);
  return result;
}

SourceCharWalkCorridorRegulation source_charwalk_regulate_corridor(
    const std::vector<std::array<float, 3>>& path,
    size_t waypoint_index,
    const std::array<float, 3>& predicted_position,
    const std::array<float, 3>& backward_position,
    const std::array<float, 3>& current_position,
    float path_radius,
    float frame_delta) {
  SourceCharWalkCorridorRegulation result;
  result.regulated_back_position = backward_position;
  if (path.size() < 2) return result;
  waypoint_index = std::clamp<size_t>(waypoint_index, 1, path.size() - 1);

  auto subtract = [](const std::array<float, 3>& a,
                     const std::array<float, 3>& b) {
    return std::array<float, 3>{
        a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  };
  auto dot = [](const std::array<float, 3>& a,
                const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  auto length = [&](const std::array<float, 3>& v) {
    return std::sqrt(dot(v, v));
  };
  auto direction_for = [&](size_t index) {
    auto direction = subtract(path[index], path[index - 1]);
    const float magnitude = length(direction);
    if (magnitude > 0.0f) {
      for (float& component : direction) component /= magnitude;
    }
    return direction;
  };

  auto direction = direction_for(waypoint_index);
  if (length(direction) <= 0.0f) return result;
  if (waypoint_index < path.size() - 1 &&
      dot(subtract(predicted_position, backward_position), direction) >=
          0.0f) {
    ++waypoint_index;
    direction = direction_for(waypoint_index);
    if (length(direction) <= 0.0f) return result;
  }

  const auto delta =
      subtract(backward_position, path[waypoint_index - 1]);
  const float along = dot(delta, direction);
  std::array<float, 3> projection = {
      path[waypoint_index - 1][0] + direction[0] * along,
      path[waypoint_index - 1][1] + direction[1] * along,
      path[waypoint_index - 1][2] + direction[2] * along};
  const auto lateral = subtract(backward_position, projection);
  const float lateral_length = length(lateral);
  const float radius = std::max(0.0f, path_radius);
  if (lateral_length > radius && lateral_length > 0.0f) {
    const float excess = lateral_length - radius;
    for (size_t axis = 0; axis < 3; ++axis) {
      projection[axis] += direction[axis] * excess;
      result.regulated_back_position[axis] =
          projection[axis] + lateral[axis] * radius / lateral_length;
    }
  }

  const auto to_back =
      subtract(result.regulated_back_position, current_position);
  const auto to_predicted =
      subtract(predicted_position, current_position);
  const float back_length = std::max(5.0f, length(to_back));
  const float predicted_length = std::max(5.0f, length(to_predicted));
  const float signed_sine = std::clamp(
      (to_back[0] * to_predicted[1] -
       to_back[1] * to_predicted[0]) /
          (back_length * predicted_length),
      -1.0f, 1.0f);

  result.valid = true;
  result.waypoint_index = waypoint_index;
  result.waypoint_direction = direction;
  result.yaw_adjustment =
      -std::asin(signed_sine) * std::max(0.0f, frame_delta);
  return result;
}

bool gh1_arena_nondraw_helper_mesh(std::string_view name) {
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  if (lower == "target_parent.mesh") return true;

  const auto numbered_family = [&](std::string_view prefix,
                                   bool allow_zero) {
    constexpr std::string_view suffix = ".mesh";
    if (lower.size() <= prefix.size() + suffix.size() ||
        std::string_view(lower).substr(0, prefix.size()) != prefix ||
        std::string_view(lower).substr(lower.size() - suffix.size()) !=
            suffix) {
      return false;
    }
    const std::string_view digits(lower.data() + prefix.size(),
                                  lower.size() - prefix.size() -
                                      suffix.size());
    if (digits.empty()) return false;
    unsigned value = 0;
    for (const char c : digits) {
      if (c < '0' || c > '9') return false;
      value = value * 10u + static_cast<unsigned>(c - '0');
    }
    return allow_zero || value != 0u;
  };

  return numbered_family("stage_spot_", false) ||
         numbered_family("walk_spot_", false) ||
         numbered_family("fire_spot_", false) ||
         numbered_family("name_lights_spot_", false) ||
         numbered_family("crowd_limits", true);
}

}  // namespace ghogx::game
