// Practice Room Soundcheck -- themed, two-value calibration workflow.

#pragma once

#include "ui/ui_classes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ghogx::ui {

class ScreenManager;

struct CalibrationEstimate {
  bool valid = false;
  int center_ms = 0;
  int spread_ms = 0;
  std::size_t accepted = 0;
  std::size_t rejected = 0;
};

// Median/MAD rejection followed by a trimmed mean.  Human calibration taps
// occasionally contain a missed beat; one such hit must not move the saved
// result by tens or hundreds of milliseconds.
CalibrationEstimate robust_calibration_estimate(
    const std::vector<int>& samples_ms);

enum class CalibrationTimingBand { Early, Centered, Late };
CalibrationTimingBand calibration_timing_band(int corrected_delta_ms,
                                               int centered_window_ms = 22);

// Bridges the renderer's gameplay-format guitar edge mask into Soundcheck.
// Returns true only when a strum was consumed by an active timing stage.
bool route_soundcheck_guitar_edge(Object* panel, std::uint32_t guitar_edge);

class SoundcheckPanel final : public UiObject {
 public:
  explicit SoundcheckPanel(ScreenManager* manager);

  DataNode handle_property(Symbol msg, const DataArray& args) override;

 private:
  enum class Stage {
    Main,
    AudioMeasure,
    AudioResult,
    VideoMeasure,
    Results,
    FineTune,
    CombinedTest,
    AdaptiveResult,
  };

  void enter_soundcheck();
  void leave_soundcheck(bool save);
  void set_stage(Stage stage);
  void poll_stage();
  void handle_button(Symbol button);
  void handle_main_button(Symbol button);
  void handle_fine_tune_button(Symbol button);
  void record_timed_hit(double now_seconds);
  void record_sample_ms(int delta_ms);
  void finish_measurement();
  void finish_adaptive_tune();
  int required_hits() const;
  int available_targets() const;
  double first_target_seconds() const;
  void set_offsets(int audio_ms, int video_input_ms);
  void update_properties();
  void emit_click(Symbol cue);

  static const char* stage_name(Stage stage);
  static bool is_up(Symbol button);
  static bool is_down(Symbol button);
  static bool is_confirm(Symbol button);
  static bool is_back(Symbol button);

  Stage stage_ = Stage::Main;
  int main_selection_ = 0;
  int fine_selection_ = 0;
  bool fine_adjusting_ = false;
  int audio_offset_ms_ = 0;
  int video_input_offset_ms_ = 0;
  int original_audio_offset_ms_ = 0;
  int original_video_input_offset_ms_ = 0;
  double stage_start_seconds_ = 0.0;
  double last_adjust_seconds_ = -100.0;
  int emitted_count_in_ = 0;
  int emitted_measure_clicks_ = 0;
  int last_recorded_target_ = -1;
  std::vector<int> samples_ms_;
  CalibrationEstimate audio_estimate_;
  CalibrationEstimate video_estimate_;
  std::string feedback_;
};

// Installs a native screen while retaining the stock GH2 Practice Room art,
// Practice menu fonts/placement, helpbar, sounds, and navigation conventions.
void install_soundcheck_screen(ScreenManager& manager);

}  // namespace ghogx::ui
