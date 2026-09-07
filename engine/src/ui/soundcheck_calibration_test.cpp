#include "ui/soundcheck_panel.h"
#include "ui/screen_manager.h"
#include "ui/ui_classes.h"

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr)                                                          \
  do {                                                                       \
    if (!(expr)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

using ghogx::DataArray;
using ghogx::DataNode;
using ghogx::Object;
using ghogx::Symbol;

void send_button(ghogx::ui::ScreenManager& manager, Object* panel,
                 const char* button) {
  manager.set_global(Symbol("button"), DataNode::Sym(Symbol(button)));
  panel->handle_property(Symbol("BUTTON_DOWN_MSG"), DataArray());
}

void debug_stage(Object* panel, const char* stage) {
  DataArray args;
  args.push(DataNode::Sym(Symbol(stage)));
  panel->handle_property(Symbol("debug_set_stage"), args);
}

void debug_samples(Object* panel, std::initializer_list<int> samples) {
  for (const int sample : samples) {
    DataArray args;
    args.push(DataNode::Int(sample));
    panel->handle_property(Symbol("debug_record_sample_ms"), args);
  }
}

int property_int(Object* object, const char* property) {
  return object->get_property(Symbol(property)).as_int().value_or(0);
}

Symbol property_symbol(Object* object, const char* property) {
  return object->get_property(Symbol(property)).as_symbol().value_or(Symbol());
}
}  // namespace

int main() {
  using ghogx::ui::CalibrationTimingBand;
  using ghogx::ui::calibration_timing_band;
  using ghogx::ui::robust_calibration_estimate;

  const auto clean = robust_calibration_estimate(
      std::vector<int>{41, 39, 42, 40, 38, 41, 39, 40});
  CHECK(clean.valid);
  CHECK(clean.center_ms == 40);
  CHECK(clean.spread_ms <= 2);
  CHECK(clean.accepted == 8);
  CHECK(clean.rejected == 0);

  const auto outlier = robust_calibration_estimate(
      std::vector<int>{41, 39, 42, 40, 38, 260, 41, 39});
  CHECK(outlier.valid);
  CHECK(outlier.center_ms >= 39 && outlier.center_ms <= 41);
  CHECK(outlier.rejected == 1);

  CHECK(!robust_calibration_estimate(std::vector<int>{1, 2, 3}).valid);
  CHECK(calibration_timing_band(-23) == CalibrationTimingBand::Early);
  CHECK(calibration_timing_band(-22) == CalibrationTimingBand::Centered);
  CHECK(calibration_timing_band(22) == CalibrationTimingBand::Centered);
  CHECK(calibration_timing_band(23) == CalibrationTimingBand::Late);

  // Headless process-local walkthrough of every Soundcheck stage. This uses
  // the same panel handlers as live controller input without driving the host
  // desktop or depending on the retail ARK.
  ghogx::ui::register_ui_classes();
  ghogx::ui::ScreenManager manager;
  ghogx::ui::install_default_singletons(manager);
  ghogx::ui::install_soundcheck_screen(manager);
  Object* panel = manager.find_object(Symbol("soundcheck_panel"));
  Object* options = manager.resolve_object(Symbol("options"));
  CHECK(panel != nullptr);
  CHECK(options != nullptr);
  if (panel && options) {
    DataArray value;
    value.push(DataNode::Int(9));
    options->handle_property(Symbol("set_audio_offset"), value);
    value.at(0) = DataNode::Int(-8);
    options->handle_property(Symbol("set_video_input_offset"), value);

    manager.goto_screen(Symbol("lag_screen"));
    CHECK(manager.current_screen() != nullptr);
    CHECK(manager.current_screen() &&
          manager.current_screen()->name() == Symbol("soundcheck_screen"));
    CHECK(property_symbol(panel, "stage") == Symbol("main"));
    CHECK(property_symbol(panel, "backdrop_file") ==
          Symbol("practice_panel.milo"));
    CHECK(property_int(panel, "audio_offset_ms") == 9);
    CHECK(property_int(panel, "video_input_offset_ms") == -8);

    debug_stage(panel, "audio_measure");
    const int audio_cues_before = property_int(panel, "cue_count");
    manager.update(0.01f);
    manager.update(0.75f);
    manager.update(2.25f);
    CHECK(property_int(panel, "cue_count") == audio_cues_before + 5);
    CHECK(property_symbol(panel, "last_cue") == Symbol("sync_click.cue"));
    send_button(manager, panel, "kPad_X");
    CHECK(property_int(panel, "samples_collected") == 1);

    // Restart the pass before the deterministic robust-estimator fixture.
    debug_stage(panel, "audio_measure");
    debug_samples(panel, {41, 39, 42, 40, 38, 260, 41, 39});
    CHECK(property_symbol(panel, "stage") == Symbol("audio_result"));
    CHECK(property_int(panel, "audio_offset_ms") == 40);

    send_button(manager, panel, "kPad_X");
    CHECK(property_symbol(panel, "stage") == Symbol("video_measure"));
    const int video_cues_before = property_int(panel, "cue_count");
    manager.update(3.1f);
    CHECK(property_int(panel, "cue_count") == video_cues_before);
    CHECK(!ghogx::ui::route_soundcheck_guitar_edge(panel, 1u << 0));
    CHECK(property_int(panel, "samples_collected") == 0);
    CHECK(ghogx::ui::route_soundcheck_guitar_edge(panel, 1u << 5));
    CHECK(property_int(panel, "samples_collected") == 1);
    // Restart before the deterministic estimator fixture below.
    debug_stage(panel, "video_measure");
    debug_samples(panel, {64, 66, 65, 67, 63, -210, 65, 64});
    CHECK(property_symbol(panel, "stage") == Symbol("results"));
    CHECK(property_int(panel, "video_input_offset_ms") == -65);

    send_button(manager, panel, "kPad_X");
    CHECK(property_symbol(panel, "stage") == Symbol("combined_test"));
    CHECK(property_int(panel, "samples_required") == 16);
    const int combined_cues_before = property_int(panel, "cue_count");
    manager.update(3.1f);
    CHECK(property_int(panel, "cue_count") == combined_cues_before + 4);
    CHECK(property_symbol(panel, "last_cue") == Symbol("sync_click.cue"));
    debug_samples(panel, {65});
    CHECK(panel->get_property(Symbol("feedback")).as_string().value_or("") ==
          "CENTERED");
    send_button(manager, panel, "kPad_Tri");
    CHECK(property_symbol(panel, "stage") == Symbol("main"));

    send_button(manager, panel, "kPad_DDown");
    send_button(manager, panel, "kPad_X");
    CHECK(property_symbol(panel, "stage") == Symbol("fine_tune"));
    send_button(manager, panel, "kPad_X");
    send_button(manager, panel, "kPad_DUp");
    CHECK(property_int(panel, "audio_offset_ms") == 41);
    send_button(manager, panel, "kPad_X");
    send_button(manager, panel, "kPad_DDown");
    send_button(manager, panel, "kPad_X");
    send_button(manager, panel, "kPad_DDown");
    CHECK(property_int(panel, "video_input_offset_ms") == -66);
    send_button(manager, panel, "kPad_X");
    send_button(manager, panel, "kPad_Tri");
    CHECK(property_symbol(panel, "stage") == Symbol("main"));

    send_button(manager, panel, "kPad_DDown");
    send_button(manager, panel, "kPad_DDown");
    send_button(manager, panel, "kPad_X");
    CHECK(options->handle_property(Symbol("get_audio_offset"), DataArray())
              .as_int().value_or(0) == 41);
    CHECK(options->handle_property(Symbol("get_video_input_offset"), DataArray())
              .as_int().value_or(0) == -66);
    CHECK(options->handle_property(Symbol("get_sync_offset"), DataArray())
              .as_int().value_or(0) == -66);

    debug_stage(panel, "combined_test");
    CHECK(property_int(panel, "samples_required") == 16);
    debug_samples(panel, {72, 70, 73, 71, 72, 74, 71, 72,
                          73, 70, 72, 71, 74, 72, 73, 71});
    CHECK(property_symbol(panel, "stage") == Symbol("adaptive_result"));
    CHECK(property_int(panel, "video_input_offset_ms") == -72);
    CHECK(property_int(panel, "adaptive_adjustment_ms") == -6);
    send_button(manager, panel, "kPad_X");
    CHECK(property_symbol(panel, "stage") == Symbol("main"));

    panel->handle_property(Symbol("enter"), DataArray());
    DataArray offsets;
    offsets.push(DataNode::Int(111));
    offsets.push(DataNode::Int(222));
    panel->handle_property(Symbol("debug_set_offsets"), offsets);
    send_button(manager, panel, "kPad_Tri");
    CHECK(property_int(panel, "audio_offset_ms") == 41);
    CHECK(property_int(panel, "video_input_offset_ms") == -66);
  }

  std::printf("soundcheck calibration: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
