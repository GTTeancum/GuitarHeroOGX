#include "game/gameplay_session.h"

#include "chart/midi_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ghogx::game {

namespace {

constexpr double kGh2WhammyStarPowerPerSecond = 0.034 * 100.0;
constexpr double kGh2WhammySpeed = 0.05;
constexpr double kGh2WhammyTimeoutSeconds = 0.5;

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

bool sustain_frets_held(uint32_t held_frets, uint32_t sustain_mask) {
  held_frets &= 0x1fu;
  sustain_mask &= 0x1fu;
  return sustain_mask != 0 && (held_frets & sustain_mask) == sustain_mask;
}

double tempo_bpm_at_tick(const ghogx::chart::Chart& chart, uint32_t tick) {
  uint32_t us_per_beat = 500000;
  for (const auto& tempo : chart.tempo_map) {
    if (tempo.tick > tick) break;
    if (tempo.us_per_beat != 0) us_per_beat = tempo.us_per_beat;
  }
  return 60000000.0 / static_cast<double>(us_per_beat);
}

double beat_seconds_at_tick(const ghogx::chart::Chart& chart, uint32_t tick) {
  const double bpm = tempo_bpm_at_tick(chart, tick);
  return bpm > 0.0 ? 60.0 / bpm : 0.5;
}

}  // namespace

FoFiXGameplaySession::FoFiXGameplaySession(std::vector<FoFiXSessionNote> notes,
                                           double bpm)
    : notes_(std::move(notes)), hit_window_(fofix_hit_window_for_bpm(bpm)) {
  beat_seconds_ = bpm > 0.0 ? 60.0 / bpm : 0.5;
  std::sort(notes_.begin(), notes_.end(),
            [](const FoFiXSessionNote& a, const FoFiXSessionNote& b) {
              if (a.time != b.time) return a.time < b.time;
              return a.mask < b.mask;
            });
  for (FoFiXSessionNote& note : notes_) {
    if (note.end_time < note.time) note.end_time = note.time;
    note.hopo_tappable = std::clamp(note.hopo_tappable, 0, 3);
    if (note.hopo && note.hopo_tappable == 0) note.hopo_tappable = 2;
    if (note.hopo_tappable >= 2) note.hopo = true;
    if (note.beat_seconds <= 0.0 || !std::isfinite(note.beat_seconds)) {
      note.beat_seconds = beat_seconds_;
    }
    if (note.hit_early_sec <= 0.0 || !std::isfinite(note.hit_early_sec)) {
      note.hit_early_sec = hit_window_.early_sec;
    }
    if (note.hit_late_sec <= 0.0 || !std::isfinite(note.hit_late_sec)) {
      note.hit_late_sec = hit_window_.late_sec;
    }
  }
  consumed_.assign(notes_.size(), 0);
}

FoFiXGameplaySession FoFiXGameplaySession::FromChart(
    const ghogx::chart::Chart& chart, int difficulty) {
  const int diff = std::clamp(difficulty, 0, 3);
  std::vector<FoFiXSessionNote> notes;
  notes.reserve(chart.notes[diff].size());
  for (const auto& note : chart.notes[diff]) {
    const double bpm = tempo_bpm_at_tick(chart, note.tick_on);
    const FoFiXHitWindow window = fofix_hit_window_for_bpm(bpm);
    FoFiXSessionNote session_note{
        chart.tick_to_sec(note.tick_on),
        std::max(chart.tick_to_sec(note.tick_on),
                 chart.tick_to_sec(note.tick_off)),
        1u << std::clamp(note.lane, 0, 4),
        note.is_hopo,
        note.star_power,
        beat_seconds_at_tick(chart, note.tick_on),
        window.early_sec,
        window.late_sec,
        notes.size(),
        note.tick_on,
    };
    session_note.hopo_tappable = note.hopo_tappable;
    session_note.final_star = note.final_star;
    notes.push_back(session_note);
  }
  return FoFiXGameplaySession(std::move(notes));
}

FoFiXHitWindow FoFiXGameplaySession::window_for_note(size_t index) const {
  if (index >= notes_.size()) return hit_window_;
  return FoFiXHitWindow{notes_[index].hit_early_sec,
                        notes_[index].hit_late_sec};
}

size_t FoFiXGameplaySession::group_end(size_t start) const {
  const double t = notes_[start].time;
  size_t end = start + 1;
  while (end < notes_.size() && notes_[end].time == t) ++end;
  return end;
}

uint32_t FoFiXGameplaySession::group_mask(size_t start, size_t end) const {
  uint32_t mask = 0;
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    mask |= notes_[i].mask & 0x1fu;
  }
  return mask;
}

int FoFiXGameplaySession::group_gem_count(size_t start, size_t end) const {
  int count = 0;
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    count += std::max(1, popcount5(notes_[i].mask));
  }
  return count;
}

bool FoFiXGameplaySession::group_star_power(size_t start, size_t end) const {
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    if (notes_[i].star_power) return true;
  }
  return false;
}

bool FoFiXGameplaySession::group_final_star(size_t start, size_t end) const {
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    if (notes_[i].final_star) return true;
  }
  return false;
}

FoFiXSessionEvent FoFiXGameplaySession::make_event(
    FoFiXSessionEventType type,
    double time,
    uint32_t mask,
    int gem_count,
    int score_delta,
    size_t source_index,
    uint32_t source_tick) const {
  FoFiXSessionEvent event;
  event.type = type;
  event.time = time;
  event.mask = mask;
  event.gem_count = gem_count;
  event.score_delta = score_delta;
  event.score = score_.score;
  event.streak = score_.streak;
  event.multiplier = score_.multiplier;
  event.source_index = source_index;
  event.source_tick = source_tick;
  event.rock_fill = fofix_rock_fill(rock_);
  event.star_power_fill = fofix_star_power_fill(star_power_);
  event.failed = failed();
  event.phrase_state = star_phrase_active_
                           ? static_cast<uint8_t>(star_phrase_missed_ ? 1 : 2)
                           : 0;
  return event;
}

void FoFiXGameplaySession::finish_star_phrase() {
  if (!star_phrase_active_) return;
  if (!star_phrase_missed_) {
    fofix_award_star_phrase(star_power_);
    last_events_.push_back(make_event(FoFiXSessionEventType::StarPhraseComplete,
                                      last_time_, 0, 0, 0,
                                      star_phrase_source_index_,
                                      star_phrase_source_tick_));
  } else {
    last_events_.push_back(make_event(FoFiXSessionEventType::StarPhraseMiss,
                                      last_time_, 0, 0, 0,
                                      star_phrase_source_index_,
                                      star_phrase_source_tick_));
  }
  star_phrase_active_ = false;
  star_phrase_missed_ = false;
  star_phrase_source_index_ = static_cast<size_t>(-1);
  star_phrase_source_tick_ = UINT32_MAX;
}

void FoFiXGameplaySession::observe_star_phrase(size_t start,
                                               size_t end,
                                               bool hit) {
  if (!group_star_power(start, end)) {
    finish_star_phrase();
    return;
  }
  if (!star_phrase_active_) {
    star_phrase_active_ = true;
    star_phrase_missed_ = false;
    star_phrase_source_index_ = notes_[start].source_index;
    star_phrase_source_tick_ = notes_[start].source_tick;
  }
  if (!hit) star_phrase_missed_ = true;
}

void FoFiXGameplaySession::award_sustain(const ActiveSustain& sustain,
                                         double held_until) {
  const double held_seconds =
      std::max(0.0, std::min(held_until, sustain.end_time) -
                        sustain.start_time);
  const int points = fofix_sustain_score(
      held_seconds, sustain.gem_count, sustain.beat_seconds,
      score_.multiplier * fofix_star_power_score_multiplier(star_power_));
  score_.score += points;
  if (points > 0) {
    last_events_.push_back(make_event(FoFiXSessionEventType::Sustain,
                                      held_until, sustain.mask,
                                      sustain.gem_count, points,
                                      sustain.source_index,
                                      sustain.source_tick));
  }
}

void FoFiXGameplaySession::update_sustains(double song_time,
                                           double dt_seconds,
                                           uint32_t held_frets,
                                           float whammy_axis) {
  std::vector<ActiveSustain> keep;
  keep.reserve(active_sustains_.size());
  const ActiveSustain* whammy_sustain = nullptr;
  for (const ActiveSustain& sustain : active_sustains_) {
    if (song_time >= sustain.end_time) {
      award_sustain(sustain, sustain.end_time);
      continue;
    }
    if (!sustain_frets_held(held_frets, sustain.mask)) {
      award_sustain(sustain, song_time);
      continue;
    }
    if (sustain.star_power_tail && !whammy_sustain) whammy_sustain = &sustain;
    keep.push_back(sustain);
  }

  // GemPlayer::FilteredWhammyBar (0x822C8438) does not treat the whammy bar
  // as a digital held input.  It measures filtered-axis velocity against the
  // loaded whammy_speed (0.05), then keeps Player::mWhammying alive for the
  // loaded whammy_timeout (0.5 seconds).  Player::Poll (0x822DBD60) awards the
  // fixed whammy_rate (0.034 meter/second) while that state is live.
  const float filtered_axis = std::clamp(whammy_axis, -1.0f, 1.0f);
  const double sample_dt = has_whammy_sample_
                               ? song_time - last_whammy_sample_time_
                               : 0.0;
  bool fast_whammy = false;
  if (whammy_sustain && sample_dt > 0.0) {
    const double speed =
        std::abs(static_cast<double>(filtered_axis - last_whammy_axis_)) /
        sample_dt;
    fast_whammy = speed > kGh2WhammySpeed;
    if (fast_whammy) last_fast_whammy_time_ = song_time;
    whammying_ = fast_whammy ||
                 song_time - last_fast_whammy_time_ <
                     kGh2WhammyTimeoutSeconds;
  } else {
    whammying_ = false;
    if (!whammy_sustain) last_fast_whammy_time_ = -1.0e30;
  }
  last_whammy_axis_ = filtered_axis;
  last_whammy_sample_time_ = song_time;
  has_whammy_sample_ = true;

  if (whammy_sustain && whammying_) {
    const double before = star_power_.value;
    star_power_.value =
        std::clamp(star_power_.value + std::max(0.0, dt_seconds) *
                                              kGh2WhammyStarPowerPerSecond,
                   0.0, 100.0);
    if (star_power_.value > before) {
      last_events_.push_back(make_event(
          FoFiXSessionEventType::StarPowerWhammy, song_time,
          whammy_sustain->mask, whammy_sustain->gem_count, 0,
          whammy_sustain->source_index, whammy_sustain->source_tick));
    }
  }
  active_sustains_ = std::move(keep);
}

void FoFiXGameplaySession::start_sustain(size_t start,
                                         size_t end,
                                         double song_time) {
  uint32_t mask = 0;
  int gems = 0;
  double end_time = 1.0e30;
  bool star_power_tail = false;
  for (size_t i = start; i < end; ++i) {
    if (notes_[i].end_time <= notes_[i].time + notes_[i].beat_seconds / 4.0)
      continue;
    mask |= notes_[i].mask & 0x1fu;
    gems += std::max(1, popcount5(notes_[i].mask));
    end_time = std::min(end_time, notes_[i].end_time);
    star_power_tail = star_power_tail || notes_[i].star_power;
  }
  if (mask == 0 || gems <= 0 || end_time == 1.0e30) return;
  active_sustains_.push_back(
      ActiveSustain{mask, gems, std::max(song_time, notes_[start].time),
                    end_time, notes_[start].beat_seconds, star_power_tail,
                    notes_[start].source_index,
                    notes_[start].source_tick});
}

void FoFiXGameplaySession::clear_hopo_strict_state() {
  was_last_note_hopod_ = false;
  last_strum_was_chord_ = false;
  same_note_hopo_string_ = false;
  hopo_last_lane_ = -1;
  hopo_problem_lane_ = -1;
  hopo_active_time_ = 0.0;
  hopo_strict_late_margin_sec_ = 0.0;
}

void FoFiXGameplaySession::update_hopo_strict_state(size_t start,
                                                    size_t end,
                                                    bool hopo_input) {
  const int gem_count = group_gem_count(start, end);
  const uint32_t mask = group_mask(start, end);
  if (gem_count != 1 || mask == 0 || start >= notes_.size()) {
    clear_hopo_strict_state();
    last_strum_was_chord_ = gem_count > 1;
    return;
  }

  const int tappable = notes_[start].hopo_tappable;
  if (tappable <= 0) {
    clear_hopo_strict_state();
    return;
  }

  hopo_last_lane_ = first_lane(mask);
  hopo_active_time_ =
      tappable == 3 ? -notes_[start].time : notes_[start].time;
  hopo_strict_late_margin_sec_ = window_for_note(start).late_sec;
  was_last_note_hopod_ = true;
  last_strum_was_chord_ = false;
  same_note_hopo_string_ = hopo_input && tappable == 3;
  hopo_problem_lane_ = same_note_hopo_string_ ? hopo_last_lane_ : -1;
}

bool FoFiXGameplaySession::should_ignore_hopo_strum(double song_time,
                                                    uint32_t held_frets) {
  if (!was_last_note_hopod_ || last_strum_was_chord_ ||
      hopo_last_lane_ < 0 || hopo_last_lane_ >= 5) {
    return false;
  }

  held_frets &= 0x1fu;
  const uint32_t last_bit = 1u << hopo_last_lane_;
  const bool last_hopo_fret_still_held = (held_frets & last_bit) != 0;
  const uint32_t higher_frets =
      0x1fu & ~((1u << (hopo_last_lane_ + 1)) - 1u);
  const bool higher_frets_held = (held_frets & higher_frets) != 0;

  if (same_note_hopo_string_) {
    const bool problem_note_still_held =
        hopo_problem_lane_ >= 0 && hopo_problem_lane_ < 5 &&
        (held_frets & (1u << hopo_problem_lane_)) != 0;
    if (!problem_note_still_held || higher_frets_held) {
      same_note_hopo_string_ = false;
      hopo_problem_lane_ = -1;
      return false;
    }
    return last_hopo_fret_still_held;
  }

  if (!last_hopo_fret_still_held || higher_frets_held) return false;
  const double active_time = std::abs(hopo_active_time_);
  if (active_time <= 0.0 || hopo_strict_late_margin_sec_ <= 0.0)
    return false;
  const double hopo_fudge = std::abs(active_time - song_time);
  return hopo_fudge >= 0.0 && hopo_fudge < hopo_strict_late_margin_sec_;
}

void FoFiXGameplaySession::apply_hopo_strum_ignored(uint32_t held_frets) {
  last_events_.push_back(make_event(FoFiXSessionEventType::HopoStrumIgnored,
                                    last_time_, held_frets & 0x1fu, 0, 0,
                                    static_cast<size_t>(-1), UINT32_MAX));
  clear_hopo_strict_state();
}

void FoFiXGameplaySession::apply_hit(size_t start,
                                     size_t end,
                                     double song_time,
                                     bool hopo_input) {
  const int gem_count = group_gem_count(start, end);
  if (gem_count <= 0) return;
  const uint32_t mask = group_mask(start, end);
  active_sustains_.clear();
  observe_star_phrase(start, end, true);
  const bool completes_clean_star_phrase =
      group_final_star(start, end) && star_phrase_active_ &&
      !star_phrase_missed_;
  const FoFiXScoreAward award = fofix_apply_hit(
      score_, gem_count, fofix_star_power_score_multiplier(star_power_));
  fofix_apply_rock_hit(
      rock_,
      static_cast<double>(fofix_star_power_score_multiplier(star_power_)));
  start_sustain(start, end, song_time);
  update_hopo_strict_state(start, end, hopo_input);
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size()) consumed_[i] = 1;
  }
  ++hits_;
  FoFiXSessionEvent hit_event = make_event(
      FoFiXSessionEventType::Hit, song_time, mask, gem_count, award.points,
      notes_[start].source_index, notes_[start].source_tick);
  if (completes_clean_star_phrase) hit_event.phrase_state = 3;
  last_events_.push_back(hit_event);
  if (completes_clean_star_phrase) finish_star_phrase();
}

void FoFiXGameplaySession::apply_miss(size_t start, size_t end) {
  const uint32_t mask = group_mask(start, end);
  const int gem_count = group_gem_count(start, end);
  observe_star_phrase(start, end, false);
  fofix_apply_miss(score_);
  fofix_apply_rock_miss(
      rock_,
      static_cast<double>(fofix_star_power_score_multiplier(star_power_)));
  clear_hopo_strict_state();
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size()) consumed_[i] = 1;
  }
  ++misses_;
  last_events_.push_back(make_event(FoFiXSessionEventType::Miss,
                                    notes_[start].time, mask, gem_count, 0,
                                    notes_[start].source_index,
                                    notes_[start].source_tick));
}

void FoFiXGameplaySession::apply_skip(size_t start, size_t end) {
  observe_star_phrase(start, end, false);
  for (size_t i = start; i < end; ++i) {
    if (i < consumed_.size()) consumed_[i] = 1;
  }
}

void FoFiXGameplaySession::apply_overstrum(uint32_t held_frets) {
  if (star_phrase_active_) star_phrase_missed_ = true;
  fofix_apply_miss(score_);
  fofix_apply_rock_overstrum(
      rock_,
      static_cast<double>(fofix_star_power_score_multiplier(star_power_)));
  clear_hopo_strict_state();
  ++overstrums_;
  last_events_.push_back(make_event(FoFiXSessionEventType::Overstrum,
                                    last_time_, held_frets & 0x1fu, 0, 0,
                                    static_cast<size_t>(-1), UINT32_MAX));
}

void FoFiXGameplaySession::seek_without_scoring(double song_time) {
  last_events_.clear();
  last_time_ = std::max(0.0, song_time);
  last_whammy_axis_ = 0.0f;
  last_whammy_sample_time_ = last_time_;
  last_fast_whammy_time_ = -1.0e30;
  has_whammy_sample_ = false;
  whammying_ = false;
  prev_fret_mask_ = 0;
  diagnostic_autoplay_last_strum_tick_ = UINT32_MAX;
  active_sustains_.clear();
  clear_hopo_strict_state();
  star_phrase_active_ = false;
  star_phrase_missed_ = false;
  star_phrase_source_index_ = static_cast<size_t>(-1);
  star_phrase_source_tick_ = UINT32_MAX;
  bool skipped_star_phrase_active = false;
  size_t skipped_star_phrase_source_index = static_cast<size_t>(-1);
  uint32_t skipped_star_phrase_source_tick = UINT32_MAX;
  while (next_note_ < notes_.size()) {
    if (next_note_ < consumed_.size() && consumed_[next_note_]) {
      ++next_note_;
      continue;
    }
    const bool pre_seek_note = notes_[next_note_].time < last_time_ - 1e-6;
    const bool late_missed =
        fofix_note_missed(last_time_, notes_[next_note_].time,
                          window_for_note(next_note_));
    if (!pre_seek_note && !late_missed) {
      break;
    }
    const size_t end = group_end(next_note_);
    const bool skipped_star_group = group_star_power(next_note_, end);
    const bool skipped_final_star_group = group_final_star(next_note_, end);
    if (!skipped_star_group) {
      skipped_star_phrase_active = false;
      skipped_star_phrase_source_index = static_cast<size_t>(-1);
      skipped_star_phrase_source_tick = UINT32_MAX;
    } else if (!skipped_star_phrase_active) {
      skipped_star_phrase_active = true;
      skipped_star_phrase_source_index = notes_[next_note_].source_index;
      skipped_star_phrase_source_tick = notes_[next_note_].source_tick;
    }
    for (size_t i = next_note_; i < end; ++i) {
      if (i < consumed_.size()) consumed_[i] = 1;
    }
    if (skipped_final_star_group) {
      skipped_star_phrase_active = false;
      skipped_star_phrase_source_index = static_cast<size_t>(-1);
      skipped_star_phrase_source_tick = UINT32_MAX;
    }
    next_note_ = end;
  }
  if (skipped_star_phrase_active) {
    star_phrase_active_ = true;
    star_phrase_missed_ = true;
    star_phrase_source_index_ = skipped_star_phrase_source_index;
    star_phrase_source_tick_ = skipped_star_phrase_source_tick;
  }
}

uint32_t FoFiXGameplaySession::diagnostic_autoplay_mask(
    double song_time,
    bool activate_star_power) {
  uint32_t sustain_mask = 0;
  for (const ActiveSustain& sustain : active_sustains_) {
    if (song_time <= sustain.end_time) {
      sustain_mask |= sustain.mask;
    }
  }
  const uint32_t star_power_mask =
      activate_star_power && !star_power_.active &&
              fofix_star_power_fill(star_power_) >= 0.5 &&
              (prev_fret_mask_ & (1u << 6)) == 0
          ? (1u << 6)
          : 0;

  size_t target = notes_.size();
  for (size_t i = next_note_; i < notes_.size(); ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    const FoFiXHitWindow window = window_for_note(i);
    if (fofix_note_missed(song_time, notes_[i].time, window)) continue;
    if (notes_[i].time > song_time + window.early_sec) break;
    if (!fofix_note_in_window(song_time, notes_[i].time, window)) continue;
    target = i;
    break;
  }
  if (target >= notes_.size()) return sustain_mask | star_power_mask;

  const size_t end = group_end(target);
  const uint32_t mask = group_mask(target, end);
  const uint32_t source_tick =
      notes_[target].source_tick != UINT32_MAX
          ? notes_[target].source_tick
          : static_cast<uint32_t>(
                std::min<size_t>(target, UINT32_MAX - 1u));
  uint32_t strum_mask = 0;
  if (source_tick != diagnostic_autoplay_last_strum_tick_) {
    const bool previous_strum_held = (prev_fret_mask_ & (1u << 5)) != 0;
    const bool target_is_still_early = song_time + 1e-6 < notes_[target].time;
    if (previous_strum_held && target_is_still_early) {
      return sustain_mask | mask | star_power_mask;
    }
    strum_mask = 1u << 5;
    diagnostic_autoplay_last_strum_tick_ = source_tick;
    prev_fret_mask_ &= ~(1u << 5);
  }
  if (strum_mask != 0) return mask | strum_mask | star_power_mask;
  return sustain_mask | mask | star_power_mask;
}

uint32_t FoFiXGameplaySession::tick_diagnostic_autoplay(
    double song_time,
    bool activate_star_power) {
  const double target_time = std::max(0.0, song_time);
  std::vector<FoFiXSessionEvent> emitted;

  auto collect_tick = [&](double tick_time, uint32_t mask) {
    tick(tick_time, mask);
    emitted.insert(emitted.end(), last_events_.begin(), last_events_.end());
  };
  auto sustain_mask_at = [&](double time) {
    uint32_t mask = 0;
    for (const ActiveSustain& sustain : active_sustains_) {
      if (time <= sustain.end_time) mask |= sustain.mask;
    }
    return mask;
  };
  auto star_power_mask = [&]() {
    return activate_star_power && !star_power_.active &&
                   fofix_star_power_fill(star_power_) >= 0.5 &&
                   (prev_fret_mask_ & (1u << 6)) == 0
               ? (1u << 6)
               : 0;
  };
  auto release_to_sustains = [&](double tick_time, uint32_t mask) {
    const uint32_t released = sustain_mask_at(tick_time);
    if ((mask & ~((1u << 5) | (1u << 6))) != released ||
        (mask & ((1u << 5) | (1u << 6))) != 0) {
      collect_tick(tick_time, released);
    }
    return released;
  };

  uint32_t final_mask = sustain_mask_at(target_time);
  for (size_t guard = 0; guard < notes_.size() + 16; ++guard) {
    size_t target = notes_.size();
    for (size_t i = next_note_; i < notes_.size(); ++i) {
      if (i < consumed_.size() && consumed_[i]) continue;
      const FoFiXHitWindow window = window_for_note(i);
      if (notes_[i].time > target_time + window.early_sec) break;
      target = i;
      break;
    }
    if (target >= notes_.size()) break;

    const size_t end = group_end(target);
    const uint32_t required = group_mask(target, end);
    if (required == 0) {
      for (size_t i = target; i < end; ++i) {
        if (i < consumed_.size()) consumed_[i] = 1;
      }
      next_note_ = end;
      continue;
    }

    const double note_time = notes_[target].time;
    const double hit_time =
        note_time <= target_time
            ? std::max(last_time_, note_time)
            : target_time;
    const uint32_t source_tick =
        notes_[target].source_tick != UINT32_MAX
            ? notes_[target].source_tick
            : static_cast<uint32_t>(
                  std::min<size_t>(target, UINT32_MAX - 1u));
    diagnostic_autoplay_last_strum_tick_ = source_tick;

    uint32_t hit_mask = required | (1u << 5) | star_power_mask();
    collect_tick(hit_time, hit_mask);
    final_mask = release_to_sustains(hit_time, hit_mask);
    if (failed()) break;
  }

  final_mask = sustain_mask_at(target_time) | star_power_mask();
  collect_tick(target_time, final_mask);
  last_events_ = std::move(emitted);
  return final_mask & ~((1u << 5) | (1u << 6));
}

void FoFiXGameplaySession::set_rock_fill_for_diagnostic(double fill) {
  fofix_set_rock_fill(rock_, fill);
}

void FoFiXGameplaySession::set_star_power_fill_for_diagnostic(double fill) {
  fofix_set_star_power_fill(star_power_, fill);
}

void FoFiXGameplaySession::set_star_power_active_for_diagnostic(bool active) {
  if (!active) {
    star_power_.active = false;
    return;
  }
  if (star_power_.value < 50.0) {
    fofix_set_star_power_fill(star_power_, 0.5);
  }
  star_power_.active = true;
}

void FoFiXGameplaySession::copy_source_consumed(
    std::vector<uint8_t>& out) const {
  std::fill(out.begin(), out.end(), 0);
  for (size_t i = 0; i < notes_.size() && i < consumed_.size(); ++i) {
    if (!consumed_[i]) continue;
    const size_t source = notes_[i].source_index;
    if (source == static_cast<size_t>(-1) || source >= out.size()) continue;
    out[source] = 1;
  }
}

void FoFiXGameplaySession::copy_active_sustains(
    std::vector<FoFiXSessionSustain>& out) const {
  out.clear();
  out.reserve(active_sustains_.size());
  for (const ActiveSustain& sustain : active_sustains_) {
    out.push_back(FoFiXSessionSustain{
        sustain.mask,
        sustain.start_time,
        sustain.end_time,
        sustain.star_power_tail,
        sustain.source_index,
        sustain.source_tick,
    });
  }
}

void FoFiXGameplaySession::tick(double song_time, uint32_t fret_mask,
                                float whammy_axis) {
  last_events_.clear();
  const double dt = std::max(0.0, song_time - last_time_);
  last_time_ = std::max(last_time_, song_time);
  if ((fret_mask & (1u << 6)) != 0 &&
      (prev_fret_mask_ & (1u << 6)) == 0) {
    if (fofix_activate_star_power(star_power_)) {
      last_events_.push_back(make_event(FoFiXSessionEventType::StarPowerActivate,
                                        song_time, 1u << 6, 0, 0,
                                        static_cast<size_t>(-1), UINT32_MAX));
    }
  }
  const bool star_power_was_active = star_power_.active;
  fofix_update_star_power(star_power_, dt);
  if (star_power_was_active && !star_power_.active) {
    last_events_.push_back(
        make_event(FoFiXSessionEventType::StarPowerDeactivate,
                   song_time, 0, 0, 0,
                   static_cast<size_t>(-1), UINT32_MAX));
  }
  if (failed()) {
    active_sustains_.clear();
    prev_fret_mask_ = fret_mask;
    return;
  }

  const bool strummed =
      (fret_mask & (1u << 5)) != 0 && (prev_fret_mask_ & (1u << 5)) == 0;
  const uint32_t previous_held_frets = prev_fret_mask_ & 0x1fu;
  const uint32_t held_frets = fret_mask & 0x1fu;
  const uint32_t pressed_frets = held_frets & ~previous_held_frets;
  const uint32_t released_frets = previous_held_frets & ~held_frets;
  const float filtered_whammy_axis =
      (fret_mask & (1u << 7)) != 0
          ? std::clamp(whammy_axis, -1.0f, 1.0f)
          : 0.0f;
  if (strummed) active_sustains_.clear();
  update_sustains(song_time, dt, held_frets, filtered_whammy_axis);
  bool hit_this_frame = false;
  bool overstrum_candidate_seen = false;
  size_t overstrum_candidate_start = 0;
  size_t overstrum_candidate_end = 0;

  while (next_note_ < notes_.size()) {
    if (next_note_ < consumed_.size() && consumed_[next_note_]) {
      ++next_note_;
      continue;
    }
    const size_t end = group_end(next_note_);
    if (!fofix_note_missed(song_time, notes_[next_note_].time,
                           window_for_note(next_note_)))
      break;
    apply_miss(next_note_, end);
    next_note_ = end;
  }

  for (size_t i = next_note_; i < notes_.size(); ++i) {
    if (i < consumed_.size() && consumed_[i]) continue;
    const size_t end = group_end(i);
    const double note_time = notes_[i].time;
    const FoFiXHitWindow window = window_for_note(i);
    if (note_time > song_time + window.early_sec) break;
    if (!fofix_note_in_window(song_time, note_time, window)) {
      i = end - 1;
      continue;
    }

    const uint32_t required = group_mask(i, end);
    const int gem_count = group_gem_count(i, end);
    if (gem_count <= 0 || required == 0) {
      i = end - 1;
      continue;
    }
    if (strummed && !overstrum_candidate_seen) {
      overstrum_candidate_seen = true;
      overstrum_candidate_start = i;
      overstrum_candidate_end = end;
    }

    const bool is_hopo =
        gem_count == 1 &&
        (notes_[i].hopo || notes_[i].hopo_tappable >= 2) &&
        score_.streak > 0;
    const int lane = first_lane(required);
    bool can_hit = false;
    if (is_hopo && (held_frets & (1u << lane)) != 0) {
      const uint32_t lane_bit = 1u << lane;
      const bool fret_pressed = (pressed_frets & lane_bit) != 0;
      const bool pull_off = released_frets != 0 && held_frets != 0;
      if (fret_pressed || pull_off) {
        can_hit = fofix_match_frets(held_frets, required);
      }
    }
    if (!can_hit && strummed) {
      can_hit = fofix_match_frets(held_frets, required);
    }
    if (!strummed && !can_hit) {
      break;
    }
    if (can_hit) {
      if (strummed) {
        for (size_t skipped = next_note_; skipped < i;) {
          if (skipped < consumed_.size() && consumed_[skipped]) {
            ++skipped;
            continue;
          }
          const size_t skipped_end = group_end(skipped);
          apply_skip(skipped, skipped_end);
          skipped = skipped_end;
        }
      }
      apply_hit(i, end, song_time, is_hopo && !strummed);
      hit_this_frame = true;
      break;
    }
    i = end - 1;
  }

  while (next_note_ < notes_.size() && next_note_ < consumed_.size() &&
         consumed_[next_note_]) {
    ++next_note_;
  }
  if (next_note_ >= notes_.size()) finish_star_phrase();

  if (strummed && !hit_this_frame) {
    if (should_ignore_hopo_strum(song_time, held_frets)) {
      apply_hopo_strum_ignored(held_frets);
      prev_fret_mask_ = fret_mask;
      return;
    }
    if (overstrum_candidate_seen &&
        group_star_power(overstrum_candidate_start, overstrum_candidate_end)) {
      observe_star_phrase(overstrum_candidate_start, overstrum_candidate_end,
                          false);
    }
    apply_overstrum(held_frets);
  }
  prev_fret_mask_ = fret_mask;
}

}  // namespace ghogx::game
