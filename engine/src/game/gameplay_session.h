#pragma once

#include "game/gameplay_rules.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghogx::chart {
struct Chart;
}

namespace ghogx::game {

struct FoFiXSessionNote {
  double time = 0.0;
  double end_time = 0.0;
  uint32_t mask = 0;
  bool hopo = false;
  bool star_power = false;
  double beat_seconds = 0.0;
  double hit_early_sec = 0.0;
  double hit_late_sec = 0.0;
  size_t source_index = static_cast<size_t>(-1);
  uint32_t source_tick = UINT32_MAX;
  int hopo_tappable = 0;
  bool final_star = false;
};

enum class FoFiXSessionEventType {
  Hit,
  Miss,
  Overstrum,
  HopoStrumIgnored,
  Sustain,
  StarPhraseComplete,
  StarPhraseMiss,
  StarPowerActivate,
  StarPowerDeactivate,
  StarPowerWhammy,
};

struct FoFiXSessionEvent {
  FoFiXSessionEventType type = FoFiXSessionEventType::Hit;
  double time = 0.0;
  uint32_t mask = 0;
  int gem_count = 0;
  int score_delta = 0;
  int score = 0;
  int streak = 0;
  int multiplier = 1;
  size_t source_index = static_cast<size_t>(-1);
  uint32_t source_tick = UINT32_MAX;
  double rock_fill = 0.0;
  double star_power_fill = 0.0;
  bool failed = false;
  // Retail GH2 PlayerState::phraseState at the instant of the event:
  // 0=none, 1=missed, 2=hitting, 3=complete.
  uint8_t phrase_state = 0;
};

struct FoFiXSessionSustain {
  uint32_t mask = 0;
  double start_time = 0.0;
  double end_time = 0.0;
  bool star_power_tail = false;
  size_t source_index = static_cast<size_t>(-1);
  uint32_t source_tick = UINT32_MAX;
};

class FoFiXGameplaySession {
 public:
  explicit FoFiXGameplaySession(std::vector<FoFiXSessionNote> notes,
                                double bpm = 120.0);
  static FoFiXGameplaySession FromChart(const ghogx::chart::Chart& chart,
                                        int difficulty);

  void tick(double song_time, uint32_t fret_mask, float whammy_axis = 0.0f);
  void seek_without_scoring(double song_time);
  uint32_t diagnostic_autoplay_mask(double song_time,
                                    bool activate_star_power = false);
  uint32_t tick_diagnostic_autoplay(double song_time,
                                    bool activate_star_power = false);
  void set_rock_fill_for_diagnostic(double fill);
  void set_star_power_fill_for_diagnostic(double fill);
  void set_star_power_active_for_diagnostic(bool active);
  void copy_source_consumed(std::vector<uint8_t>& out) const;
  void copy_active_sustains(std::vector<FoFiXSessionSustain>& out) const;

  int score() const { return score_.score; }
  int streak() const { return score_.streak; }
  int multiplier() const { return score_.multiplier; }
  double rock_fill() const { return fofix_rock_fill(rock_); }
  double star_power_fill() const { return fofix_star_power_fill(star_power_); }
  const FoFiXRockState& rock_state() const { return rock_; }
  const FoFiXStarPowerState& star_power_state() const { return star_power_; }
  bool star_power_active() const { return star_power_.active; }
  void set_failure_enabled(bool enabled) { failure_enabled_ = enabled; }
  bool failed() const { return failure_enabled_ && fofix_rock_failed(rock_); }
  int hits() const { return hits_; }
  int misses() const { return misses_; }
  int overstrums() const { return overstrums_; }
  const std::vector<FoFiXSessionEvent>& last_events() const {
    return last_events_;
  }

 private:
  bool failure_enabled_ = true;
  struct ActiveSustain {
    uint32_t mask = 0;
    int gem_count = 0;
    double start_time = 0.0;
    double end_time = 0.0;
    double beat_seconds = 0.5;
    bool star_power_tail = false;
    size_t source_index = static_cast<size_t>(-1);
    uint32_t source_tick = UINT32_MAX;
  };

  FoFiXHitWindow window_for_note(size_t index) const;
  size_t group_end(size_t start) const;
  uint32_t group_mask(size_t start, size_t end) const;
  int group_gem_count(size_t start, size_t end) const;
  bool group_star_power(size_t start, size_t end) const;
  bool group_final_star(size_t start, size_t end) const;
  FoFiXSessionEvent make_event(FoFiXSessionEventType type,
                               double time,
                               uint32_t mask,
                               int gem_count,
                               int score_delta,
                               size_t source_index,
                               uint32_t source_tick) const;
  void finish_star_phrase();
  void observe_star_phrase(size_t start, size_t end, bool hit);
  void award_sustain(const ActiveSustain& sustain, double held_until);
  void update_sustains(double song_time,
                       double dt_seconds,
                       uint32_t held_frets,
                       float whammy_axis);
  void start_sustain(size_t start, size_t end, double song_time);
  void clear_hopo_strict_state();
  void update_hopo_strict_state(size_t start, size_t end, bool hopo_input);
  bool should_ignore_hopo_strum(double song_time, uint32_t held_frets);
  void apply_hopo_strum_ignored(uint32_t held_frets);
  void apply_hit(size_t start, size_t end, double song_time,
                 bool hopo_input = false);
  void apply_miss(size_t start, size_t end);
  void apply_skip(size_t start, size_t end);
  void apply_overstrum(uint32_t held_frets);

  std::vector<FoFiXSessionNote> notes_;
  std::vector<uint8_t> consumed_;
  std::vector<ActiveSustain> active_sustains_;
  std::vector<FoFiXSessionEvent> last_events_;
  FoFiXHitWindow hit_window_;
  double beat_seconds_ = 0.5;
  double last_time_ = 0.0;
  float last_whammy_axis_ = 0.0f;
  double last_whammy_sample_time_ = 0.0;
  double last_fast_whammy_time_ = -1.0e30;
  bool has_whammy_sample_ = false;
  bool whammying_ = false;
  uint32_t prev_fret_mask_ = 0;
  uint32_t diagnostic_autoplay_last_strum_tick_ = UINT32_MAX;
  size_t next_note_ = 0;
  FoFiXScoreState score_;
  FoFiXRockState rock_;
  FoFiXStarPowerState star_power_;
  bool star_phrase_active_ = false;
  bool star_phrase_missed_ = false;
  size_t star_phrase_source_index_ = static_cast<size_t>(-1);
  uint32_t star_phrase_source_tick_ = UINT32_MAX;
  bool was_last_note_hopod_ = false;
  bool last_strum_was_chord_ = false;
  bool same_note_hopo_string_ = false;
  int hopo_last_lane_ = -1;
  int hopo_problem_lane_ = -1;
  double hopo_active_time_ = 0.0;
  double hopo_strict_late_margin_sec_ = 0.0;
  int hits_ = 0;
  int misses_ = 0;
  int overstrums_ = 0;
};

}  // namespace ghogx::game
