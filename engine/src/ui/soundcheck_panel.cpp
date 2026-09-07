#include "ui/soundcheck_panel.h"

#include "ui/screen_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>

namespace ghogx::ui {

namespace {

constexpr int kOffsetLimitMs = 500;
constexpr int kGuidedRequiredHits = 8;
constexpr int kGuidedAvailableTargets = 12;
constexpr int kAdaptiveRequiredHits = 16;
constexpr int kAdaptiveAvailableTargets = 24;
constexpr double kBeatSeconds = 0.75;
constexpr double kGuidedFirstTargetSeconds = 3.0;
constexpr double kAdaptiveFirstTargetSeconds = 0.75;

int median(std::vector<int> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if ((values.size() & 1u) != 0) return values[middle];
  return static_cast<int>(std::lround(
      (static_cast<double>(values[middle - 1]) + values[middle]) * 0.5));
}

int int_arg(const DataArray& args, std::size_t index, int fallback = 0) {
  if (index >= args.size()) return fallback;
  if (const auto value = args.at(index).as_int()) return *value;
  if (const auto value = args.at(index).as_float())
    return static_cast<int>(std::lround(*value));
  return fallback;
}

Symbol global_button(ScreenManager* manager) {
  return manager
             ? manager->get_global(Symbol("button"))
                   .as_symbol()
                   .value_or(Symbol())
             : Symbol();
}

DataNode call_with_int(Object* object, const char* message, int value) {
  if (!object) return DataNode();
  DataArray args;
  args.push(DataNode::Int(value));
  return object->handle_property(Symbol(message), args);
}

std::shared_ptr<DataArray> make_helpbar() {
  auto rows = std::make_shared<DataArray>();
  const auto add = [&](const char* control, const char* token) {
    auto row = std::make_shared<DataArray>();
    row->push(DataNode::Sym(Symbol(control)));
    row->push(DataNode::Sym(Symbol(token)));
    rows->push(DataNode::Array(row));
  };
  add("fret1", "help_select");
  add("fret2", "help_back");
  add("strum", "help_updown");
  return rows;
}

}  // namespace

CalibrationEstimate robust_calibration_estimate(
    const std::vector<int>& samples_ms) {
  CalibrationEstimate result;
  if (samples_ms.size() < 4) return result;

  const int sample_median = median(samples_ms);
  std::vector<int> deviations;
  deviations.reserve(samples_ms.size());
  for (const int sample : samples_ms)
    deviations.push_back(std::abs(sample - sample_median));
  const int mad = median(std::move(deviations));
  const int reject_distance = std::max(18, mad * 3);

  std::vector<int> accepted;
  accepted.reserve(samples_ms.size());
  for (const int sample : samples_ms) {
    if (std::abs(sample - sample_median) <= reject_distance)
      accepted.push_back(sample);
  }
  if (accepted.size() < 4) return result;

  std::sort(accepted.begin(), accepted.end());
  std::size_t first = 0;
  std::size_t last = accepted.size();
  if (accepted.size() >= 7) {
    ++first;
    --last;
  }
  const double sum = std::accumulate(
      accepted.begin() + static_cast<std::ptrdiff_t>(first),
      accepted.begin() + static_cast<std::ptrdiff_t>(last), 0.0);
  const double count = static_cast<double>(last - first);
  const int center = static_cast<int>(std::lround(sum / count));
  double squared = 0.0;
  for (std::size_t i = first; i < last; ++i) {
    const double delta = static_cast<double>(accepted[i] - center);
    squared += delta * delta;
  }

  result.valid = true;
  result.center_ms = center;
  result.spread_ms = static_cast<int>(
      std::lround(std::sqrt(squared / std::max(1.0, count))));
  result.accepted = accepted.size();
  result.rejected = samples_ms.size() - accepted.size();
  return result;
}

CalibrationTimingBand calibration_timing_band(int corrected_delta_ms,
                                               int centered_window_ms) {
  const int window = std::max(1, centered_window_ms);
  if (corrected_delta_ms < -window) return CalibrationTimingBand::Early;
  if (corrected_delta_ms > window) return CalibrationTimingBand::Late;
  return CalibrationTimingBand::Centered;
}

bool route_soundcheck_guitar_edge(Object* panel, std::uint32_t guitar_edge) {
  if (!panel || (guitar_edge & (1u << 5)) == 0) return false;
  const Symbol stage =
      panel->get_property(Symbol("stage")).as_symbol().value_or(Symbol());
  if (stage != Symbol("audio_measure") &&
      stage != Symbol("video_measure") &&
      stage != Symbol("combined_test"))
    return false;
  panel->handle_property(Symbol("STRUM_HIT_MSG"), DataArray());
  return true;
}

SoundcheckPanel::SoundcheckPanel(ScreenManager* manager)
    : UiObject(Symbol("SoundcheckPanel")) {
  set_manager(manager);
  // The calibration copy/layout comes from the authored Practice difficulty
  // panel, while its actual backdrop is GH2's authored Practice Room panel.
  // Keeping both sources explicit lets the menu renderer compose the screen
  // without recreating either asset.
  set_property(Symbol("file"),
               DataNode::Sym(Symbol("sel_diff_practice.milo")));
  set_property(Symbol("backdrop_file"),
               DataNode::Sym(Symbol("practice_panel.milo")));
  set_property(Symbol("showing"), DataNode::Int(1));
  update_properties();
}

const char* SoundcheckPanel::stage_name(Stage stage) {
  switch (stage) {
    case Stage::Main: return "main";
    case Stage::AudioMeasure: return "audio_measure";
    case Stage::AudioResult: return "audio_result";
    case Stage::VideoMeasure: return "video_measure";
    case Stage::Results: return "results";
    case Stage::FineTune: return "fine_tune";
    case Stage::CombinedTest: return "combined_test";
    case Stage::AdaptiveResult: return "adaptive_result";
  }
  return "main";
}

bool SoundcheckPanel::is_up(Symbol button) {
  return button == Symbol("kPad_DUp") || button == Symbol("kPad_DU");
}

bool SoundcheckPanel::is_down(Symbol button) {
  return button == Symbol("kPad_DDown") || button == Symbol("kPad_DD");
}

bool SoundcheckPanel::is_confirm(Symbol button) {
  return button == Symbol("kPad_X");
}

bool SoundcheckPanel::is_back(Symbol button) {
  return button == Symbol("kPad_Tri");
}

void SoundcheckPanel::enter_soundcheck() {
  Object* options = manager() ? manager()->resolve_object(Symbol("options"))
                              : nullptr;
  if (options) {
    audio_offset_ms_ = options
                           ->handle_property(Symbol("get_audio_offset"),
                                             DataArray())
                           .as_int()
                           .value_or(0);
    video_input_offset_ms_ =
        options
            ->handle_property(Symbol("get_video_input_offset"), DataArray())
            .as_int()
            .value_or(0);
  }
  original_audio_offset_ms_ = audio_offset_ms_;
  original_video_input_offset_ms_ = video_input_offset_ms_;
  main_selection_ = 0;
  fine_selection_ = 0;
  fine_adjusting_ = false;
  audio_estimate_ = {};
  video_estimate_ = {};
  feedback_.clear();
  if (manager()) manager()->emit_audio_event(Symbol("meta_music"), Symbol("stop"), false);
  set_stage(Stage::Main);
  std::fprintf(stderr,
               "[soundcheck] enter audio_ms=%d video_input_ms=%d\n",
               audio_offset_ms_, video_input_offset_ms_);
}

void SoundcheckPanel::leave_soundcheck(bool save) {
  if (save) {
    Object* options = manager() ? manager()->resolve_object(Symbol("options"))
                                : nullptr;
    call_with_int(options, "set_audio_offset", audio_offset_ms_);
    call_with_int(options, "set_video_input_offset", video_input_offset_ms_);
    original_audio_offset_ms_ = audio_offset_ms_;
    original_video_input_offset_ms_ = video_input_offset_ms_;
    std::fprintf(stderr,
                 "[soundcheck] saved audio_ms=%d video_input_ms=%d\n",
                 audio_offset_ms_, video_input_offset_ms_);
  } else {
    audio_offset_ms_ = original_audio_offset_ms_;
    video_input_offset_ms_ = original_video_input_offset_ms_;
    std::fprintf(stderr,
                 "[soundcheck] cancelled audio_ms=%d video_input_ms=%d\n",
                 audio_offset_ms_, video_input_offset_ms_);
  }
  update_properties();
  if (manager()) {
    manager()->emit_audio_event(Symbol("meta_music"), Symbol("start"), true);
    manager()->go_back();
  }
}

void SoundcheckPanel::set_stage(Stage stage) {
  stage_ = stage;
  stage_start_seconds_ = manager() ? manager()->ui_seconds() : 0.0;
  emitted_count_in_ = 0;
  emitted_measure_clicks_ = 0;
  last_recorded_target_ = -1;
  samples_ms_.clear();
  feedback_.clear();
  set_property(Symbol("measurement_failed"), DataNode::Int(0));
  if (stage_ != Stage::FineTune) fine_adjusting_ = false;
  update_properties();
  std::fprintf(stderr, "[soundcheck] stage=%s\n", stage_name(stage_));
}

void SoundcheckPanel::emit_click(Symbol cue) {
  if (manager()) manager()->emit_audio_event(Symbol("play_sfx"), cue);
  set_property(Symbol("last_cue"), DataNode::Sym(cue));
  set_property(Symbol("cue_count"),
               DataNode::Int(get_property(Symbol("cue_count"))
                                 .as_int()
                                 .value_or(0) + 1));
}

int SoundcheckPanel::required_hits() const {
  return stage_ == Stage::CombinedTest ? kAdaptiveRequiredHits
                                       : kGuidedRequiredHits;
}

int SoundcheckPanel::available_targets() const {
  return stage_ == Stage::CombinedTest ? kAdaptiveAvailableTargets
                                       : kGuidedAvailableTargets;
}

double SoundcheckPanel::first_target_seconds() const {
  return stage_ == Stage::CombinedTest ? kAdaptiveFirstTargetSeconds
                                       : kGuidedFirstTargetSeconds;
}

void SoundcheckPanel::poll_stage() {
  if (!manager()) return;
  const double local = manager()->ui_seconds() - stage_start_seconds_;
  const double first_target = first_target_seconds();
  const int target_count = available_targets();
  const int hit_goal = required_hits();
  set_property(Symbol("stage_elapsed_ms"),
               DataNode::Int(static_cast<int>(std::lround(local * 1000.0))));
  const double highway_time =
      stage_ == Stage::CombinedTest
          ? local + (kGuidedFirstTargetSeconds - kAdaptiveFirstTargetSeconds)
          : local;
  set_property(Symbol("highway_time_ms"), DataNode::Int(
      static_cast<int>(std::lround(highway_time * 1000.0))));

  const bool measuring = stage_ == Stage::AudioMeasure ||
                         stage_ == Stage::VideoMeasure;
  if (measuring || stage_ == Stage::CombinedTest) {
    const int count_in_beats = stage_ == Stage::CombinedTest ? 1 : 4;
    while (emitted_count_in_ < count_in_beats &&
           local + 0.0001 >= emitted_count_in_ * kBeatSeconds) {
      // The visual/input pass must stay silent or it measures the audio path
      // again. Only the Audio pass owns the stock hat count-in.
      if (stage_ == Stage::AudioMeasure)
        emit_click(Symbol("practice_hat"));
      ++emitted_count_in_;
    }
  }

  if (stage_ == Stage::AudioMeasure) {
    while (emitted_measure_clicks_ < target_count &&
           local + 0.0001 >=
               first_target +
                   emitted_measure_clicks_ * kBeatSeconds) {
      emit_click(Symbol("sync_click.cue"));
      ++emitted_measure_clicks_;
    }
  } else if (stage_ == Stage::CombinedTest) {
    const double adjusted_audio =
        static_cast<double>(audio_offset_ms_) / 1000.0;
    while (emitted_measure_clicks_ < target_count &&
           local + 0.0001 >=
               first_target +
                   emitted_measure_clicks_ * kBeatSeconds - adjusted_audio) {
      emit_click(Symbol("sync_click.cue"));
      ++emitted_measure_clicks_;
    }
  }

  if ((measuring || stage_ == Stage::CombinedTest) &&
      samples_ms_.size() < static_cast<std::size_t>(hit_goal) &&
      local > first_target + (target_count - 1) * kBeatSeconds + 0.5) {
    if (samples_ms_.size() >= 4) {
      if (stage_ == Stage::CombinedTest)
        finish_adaptive_tune();
      else
        finish_measurement();
    } else {
      feedback_ = "NOT ENOUGH HITS - PRESS GREEN TO RETRY";
      set_property(Symbol("measurement_failed"), DataNode::Int(1));
    }
  }
  update_properties();
}

void SoundcheckPanel::record_timed_hit(double now_seconds) {
  const double local = now_seconds - stage_start_seconds_;
  const double first_target = first_target_seconds();
  const int target = static_cast<int>(std::lround(
      (local - first_target) / kBeatSeconds));
  if (target < 0 || target >= available_targets() ||
      target == last_recorded_target_) return;
  const double expected =
      first_target + static_cast<double>(target) * kBeatSeconds;
  const int delta_ms =
      static_cast<int>(std::lround((local - expected) * 1000.0));
  if (std::abs(delta_ms) > 400) return;
  last_recorded_target_ = target;
  record_sample_ms(delta_ms);
}

void SoundcheckPanel::record_sample_ms(int delta_ms) {
  delta_ms = std::clamp(delta_ms, -400, 400);
  if (stage_ == Stage::CombinedTest) {
    const int corrected = delta_ms + video_input_offset_ms_;
    switch (calibration_timing_band(corrected)) {
      case CalibrationTimingBand::Early: feedback_ = "EARLY"; break;
      case CalibrationTimingBand::Centered: feedback_ = "CENTERED"; break;
      case CalibrationTimingBand::Late: feedback_ = "LATE"; break;
    }
    set_property(Symbol("last_test_delta_ms"), DataNode::Int(corrected));
    samples_ms_.push_back(delta_ms);
    set_property(Symbol("measurement_failed"), DataNode::Int(0));
    if (samples_ms_.size() >=
        static_cast<std::size_t>(kAdaptiveRequiredHits)) {
      finish_adaptive_tune();
    }
    update_properties();
    return;
  }
  if (stage_ != Stage::AudioMeasure && stage_ != Stage::VideoMeasure) return;
  samples_ms_.push_back(delta_ms);
  set_property(Symbol("measurement_failed"), DataNode::Int(0));
  if (samples_ms_.size() >= static_cast<std::size_t>(kGuidedRequiredHits))
    finish_measurement();
  update_properties();
}

void SoundcheckPanel::finish_measurement() {
  const Stage measured_stage = stage_;
  const CalibrationEstimate estimate =
      robust_calibration_estimate(samples_ms_);
  if (!estimate.valid) {
    feedback_ = "HITS WERE TOO UNEVEN - PRESS GREEN TO RETRY";
    set_property(Symbol("measurement_failed"), DataNode::Int(1));
    return;
  }
  if (stage_ == Stage::AudioMeasure) {
    audio_estimate_ = estimate;
    audio_offset_ms_ = std::clamp(estimate.center_ms, -kOffsetLimitMs,
                                  kOffsetLimitMs);
    set_stage(Stage::AudioResult);
  } else if (stage_ == Stage::VideoMeasure) {
    video_estimate_ = estimate;
    // The input judgement clock uses the correction, hence the negative of
    // the observed late/early press delta.  This is also GH2's legacy sign.
    video_input_offset_ms_ = std::clamp(-estimate.center_ms, -kOffsetLimitMs,
                                       kOffsetLimitMs);
    set_stage(Stage::Results);
  }
  std::fprintf(stderr,
               "[soundcheck] estimate stage=%s center_ms=%d spread_ms=%d "
               "accepted=%zu rejected=%zu audio_ms=%d video_input_ms=%d\n",
               stage_name(measured_stage), estimate.center_ms,
               estimate.spread_ms,
               estimate.accepted, estimate.rejected, audio_offset_ms_,
               video_input_offset_ms_);
}

void SoundcheckPanel::finish_adaptive_tune() {
  const CalibrationEstimate estimate =
      robust_calibration_estimate(samples_ms_);
  if (!estimate.valid) {
    feedback_ = "HITS WERE TOO UNEVEN - PRESS GREEN TO RETRY";
    set_property(Symbol("measurement_failed"), DataNode::Int(1));
    return;
  }

  const int previous_video_input_ms = video_input_offset_ms_;
  video_estimate_ = estimate;
  // The player is matching the visible green gems. The learned center is the
  // complete display + controller + personal timing delta, so the judgement
  // clock correction is its inverse rather than an increment layered on the
  // previous estimate.
  video_input_offset_ms_ = std::clamp(-estimate.center_ms, -kOffsetLimitMs,
                                      kOffsetLimitMs);
  const int adjustment = video_input_offset_ms_ - previous_video_input_ms;
  set_stage(Stage::AdaptiveResult);
  if (adjustment == 0) {
    feedback_ = "NO CHANGE NEEDED";
  } else {
    feedback_ = "ADJUSTED  " + std::to_string(std::abs(adjustment)) +
                " MS " + (adjustment < 0 ? "EARLIER" : "LATER");
  }
  set_property(Symbol("adaptive_adjustment_ms"), DataNode::Int(adjustment));
  update_properties();
  std::fprintf(stderr,
               "[soundcheck] adaptive estimate center_ms=%d spread_ms=%d "
               "accepted=%zu rejected=%zu previous_video_ms=%d "
               "video_input_ms=%d adjustment_ms=%d\n",
               estimate.center_ms, estimate.spread_ms, estimate.accepted,
               estimate.rejected, previous_video_input_ms,
               video_input_offset_ms_, adjustment);
}

void SoundcheckPanel::set_offsets(int audio_ms, int video_input_ms) {
  audio_offset_ms_ = std::clamp(audio_ms, -kOffsetLimitMs, kOffsetLimitMs);
  video_input_offset_ms_ =
      std::clamp(video_input_ms, -kOffsetLimitMs, kOffsetLimitMs);
  update_properties();
}

void SoundcheckPanel::handle_main_button(Symbol button) {
  if (is_up(button)) {
    main_selection_ = (main_selection_ + 3) % 4;
  } else if (is_down(button)) {
    main_selection_ = (main_selection_ + 1) % 4;
  } else if (is_confirm(button)) {
    if (main_selection_ == 0)
      set_stage(Stage::AudioMeasure);
    else if (main_selection_ == 1)
      set_stage(Stage::FineTune);
    else if (main_selection_ == 2)
      set_stage(Stage::CombinedTest);
    else
      leave_soundcheck(true);
  } else if (is_back(button)) {
    leave_soundcheck(false);
  }
}

void SoundcheckPanel::handle_fine_tune_button(Symbol button) {
  if (fine_adjusting_) {
    if (is_back(button) || is_confirm(button)) {
      fine_adjusting_ = false;
    } else if (is_up(button) || is_down(button)) {
      const double now = manager() ? manager()->ui_seconds() : 0.0;
      const double elapsed = now - last_adjust_seconds_;
      const int step = elapsed < 0.12 ? 10 : (elapsed < 0.3 ? 5 : 1);
      const int direction = is_up(button) ? 1 : -1;
      if (fine_selection_ == 0)
        audio_offset_ms_ = std::clamp(audio_offset_ms_ + direction * step,
                                      -kOffsetLimitMs, kOffsetLimitMs);
      else
        video_input_offset_ms_ =
            std::clamp(video_input_offset_ms_ + direction * step,
                       -kOffsetLimitMs, kOffsetLimitMs);
      last_adjust_seconds_ = now;
    }
  } else if (is_up(button)) {
    fine_selection_ = (fine_selection_ + 3) % 4;
  } else if (is_down(button)) {
    fine_selection_ = (fine_selection_ + 1) % 4;
  } else if (is_confirm(button)) {
    if (fine_selection_ <= 1) {
      fine_adjusting_ = true;
      last_adjust_seconds_ = -100.0;
    } else if (fine_selection_ == 2) {
      set_stage(Stage::CombinedTest);
    } else {
      set_offsets(0, 0);
    }
  } else if (is_back(button)) {
    set_stage(Stage::Main);
  }
}

void SoundcheckPanel::handle_button(Symbol button) {
  if (!button.valid()) return;
  if (stage_ == Stage::Main) {
    handle_main_button(button);
  } else if (stage_ == Stage::FineTune) {
    handle_fine_tune_button(button);
  } else if (stage_ == Stage::AudioResult) {
    if (is_confirm(button))
      set_stage(Stage::VideoMeasure);
    else if (is_back(button))
      set_stage(Stage::Main);
  } else if (stage_ == Stage::Results) {
    if (is_confirm(button))
      set_stage(Stage::CombinedTest);
    else if (is_back(button))
      set_stage(Stage::Main);
  } else if (stage_ == Stage::AdaptiveResult) {
    if (is_confirm(button) || is_back(button)) set_stage(Stage::Main);
  } else if (stage_ == Stage::CombinedTest) {
    if (is_back(button))
      set_stage(Stage::Main);
    else if (is_confirm(button)) {
      if (get_property(Symbol("measurement_failed"))
              .as_int()
              .value_or(0) != 0)
        set_stage(Stage::CombinedTest);
      else
        record_timed_hit(manager() ? manager()->ui_seconds() : 0.0);
    }
  } else {
    if (is_back(button)) {
      set_stage(Stage::Main);
    } else if (is_confirm(button) &&
               get_property(Symbol("measurement_failed"))
                       .as_int()
                       .value_or(0) != 0) {
      set_stage(stage_);
    } else {
      record_timed_hit(manager() ? manager()->ui_seconds() : 0.0);
    }
  }
  update_properties();
}

void SoundcheckPanel::update_properties() {
  set_property(Symbol("stage"), DataNode::Sym(Symbol(stage_name(stage_))));
  set_property(Symbol("main_selection"), DataNode::Int(main_selection_));
  set_property(Symbol("fine_selection"), DataNode::Int(fine_selection_));
  set_property(Symbol("fine_adjusting"), DataNode::Int(fine_adjusting_ ? 1 : 0));
  set_property(Symbol("audio_offset_ms"), DataNode::Int(audio_offset_ms_));
  set_property(Symbol("video_input_offset_ms"),
               DataNode::Int(video_input_offset_ms_));
  set_property(Symbol("samples_collected"),
               DataNode::Int(static_cast<int>(samples_ms_.size())));
  set_property(Symbol("samples_required"), DataNode::Int(required_hits()));
  set_property(Symbol("feedback"), DataNode::Str(feedback_));
  set_property(Symbol("show_highway"),
               DataNode::Int(stage_ == Stage::VideoMeasure ||
                                     stage_ == Stage::FineTune ||
                                     stage_ == Stage::CombinedTest ||
                                     stage_ == Stage::AdaptiveResult
                                 ? 1
                                 : 0));
  const double local = manager() ? manager()->ui_seconds() - stage_start_seconds_
                                 : 0.0;
  int countdown = 0;
  if ((stage_ == Stage::AudioMeasure || stage_ == Stage::VideoMeasure ||
       stage_ == Stage::CombinedTest) &&
      local >= 0.0 && local < first_target_seconds()) {
    const int count_in_beats = stage_ == Stage::CombinedTest ? 1 : 4;
    countdown = std::clamp(
        count_in_beats - static_cast<int>(local / kBeatSeconds), 1,
        count_in_beats);
  }
  set_property(Symbol("countdown"), DataNode::Int(countdown));
  set_property(Symbol("audio_spread_ms"),
               DataNode::Int(audio_estimate_.spread_ms));
  set_property(Symbol("video_spread_ms"),
               DataNode::Int(video_estimate_.spread_ms));
}

DataNode SoundcheckPanel::handle_property(Symbol msg, const DataArray& args) {
  if (msg == Symbol("enter")) {
    enter_soundcheck();
    return DataNode();
  }
  if (msg == Symbol("exit")) {
    if (manager()) manager()->emit_audio_event(Symbol("meta_music"), Symbol("start"), true);
    return DataNode();
  }
  if (msg == Symbol("poll")) {
    poll_stage();
    return DataNode();
  }
  if (msg == Symbol("BUTTON_DOWN_MSG")) {
    handle_button(global_button(manager()));
    return DataNode();
  }
  if (msg == Symbol("STRUM_HIT_MSG")) {
    // Live guitar strums arrive through the gameplay input bitfield, not the
    // menu Confirm action.  Keep the timing sample on the panel's UI clock so
    // audio, video, and process-local verification all share one timebase.
    record_timed_hit(manager() ? manager()->ui_seconds() : 0.0);
    return DataNode();
  }
  if (msg == Symbol("debug_set_stage")) {
    const Symbol stage = args.size()
                             ? args.at(0).as_symbol().value_or(Symbol())
                             : Symbol();
    if (stage == Symbol("audio_measure")) set_stage(Stage::AudioMeasure);
    else if (stage == Symbol("audio_result")) set_stage(Stage::AudioResult);
    else if (stage == Symbol("video_measure")) set_stage(Stage::VideoMeasure);
    else if (stage == Symbol("results")) set_stage(Stage::Results);
    else if (stage == Symbol("fine_tune")) set_stage(Stage::FineTune);
    else if (stage == Symbol("combined_test")) set_stage(Stage::CombinedTest);
    else if (stage == Symbol("adaptive_result"))
      set_stage(Stage::AdaptiveResult);
    else set_stage(Stage::Main);
    return DataNode();
  }
  if (msg == Symbol("debug_record_sample_ms")) {
    record_sample_ms(int_arg(args, 0));
    return DataNode();
  }
  if (msg == Symbol("debug_set_offsets")) {
    set_offsets(int_arg(args, 0), int_arg(args, 1));
    return DataNode();
  }
  return UiObject::handle_property(msg, args);
}

void install_soundcheck_screen(ScreenManager& manager) {
  if (manager.find_object(Symbol("soundcheck_screen"))) return;

  auto panel = std::make_unique<SoundcheckPanel>(&manager);
  panel->set_name(Symbol("soundcheck_panel"));
  manager.add_object(std::move(panel));

  auto screen = std::make_unique<UiObject>(Symbol("GHScreen"));
  screen->set_name(Symbol("soundcheck_screen"));
  screen->set_manager(&manager);
  auto panels = std::make_shared<DataArray>();
  panels->push(DataNode::Sym(Symbol("soundcheck_panel")));
  panels->push(DataNode::Sym(Symbol("helpbar")));
  screen->set_property(Symbol("panels"), DataNode::Array(panels));
  screen->set_property(Symbol("focus"),
                       DataNode::Sym(Symbol("soundcheck_panel")));
  screen->set_property(Symbol("allow_back"), DataNode::Int(0));
  screen->set_property(Symbol("helpbar"), DataNode::Array(make_helpbar()));
  manager.add_object(std::move(screen));
}

}  // namespace ghogx::ui
