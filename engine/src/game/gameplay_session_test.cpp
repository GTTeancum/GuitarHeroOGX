#include "game/gameplay_session.h"

#include "chart/midi_reader.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr, msg)                                                     \
  do {                                                                       \
    if (!(expr)) {                                                           \
      std::fprintf(stderr, "gameplay_session_test: FAIL: %s\n", (msg));     \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

constexpr uint32_t kGreen = 1u << 0;
constexpr uint32_t kRed = 1u << 1;
constexpr uint32_t kYellow = 1u << 2;
constexpr uint32_t kStrum = 1u << 5;
constexpr uint32_t kStar = 1u << 6;
constexpr uint32_t kWhammy = 1u << 7;

ghogx::game::FoFiXSessionNote make_note(double time,
                                        double end_time,
                                        uint32_t mask,
                                        bool hopo,
                                        bool star_power,
                                        bool final_star = false) {
  ghogx::game::FoFiXSessionNote note;
  note.time = time;
  note.end_time = end_time;
  note.mask = mask;
  note.hopo = hopo;
  note.star_power = star_power;
  note.final_star = final_star;
  return note;
}

}  // namespace

int main() {
  using namespace ghogx::game;

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    CHECK(session.score() == 50 && session.streak() == 1 &&
              session.hits() == 1,
          "strummed note hit scores and starts streak");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].mask == kGreen &&
              session.last_events()[0].gem_count == 1 &&
              session.last_events()[0].score_delta == 50 &&
              session.last_events()[0].score == 50 &&
              session.last_events()[0].streak == 1 &&
              session.last_events()[0].multiplier == 1 &&
              session.last_events()[0].rock_fill > 0.5001 &&
              session.last_events()[0].star_power_fill == 0.0 &&
              !session.last_events()[0].failed,
          "FoFiX session reports hit delta for native presentation");
    session.tick(1.1, kGreen);
    session.tick(1.3, kRed | kStrum);
    CHECK(session.overstrums() == 1 && session.streak() == 0,
          "wrong extra strum resets streak as overstrum");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::Overstrum &&
              session.last_events()[0].mask == kRed,
          "FoFiX session reports overstrum delta for native presentation");
    CHECK(session.last_events()[0].streak == 0 &&
              session.last_events()[0].multiplier == 1,
          "FoFiX overstrum event snapshots reset score state");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.01, kGreen | kStrum);
    CHECK(session.score() == 50 && session.streak() == 1 &&
              session.overstrums() == 0,
          "held strum does not create repeated FoFiX overstrums");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.3, kStrum);
    CHECK(session.overstrums() == 1 && session.streak() == 0,
          "empty extra strum resets streak as FoFiX overstrum");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::Overstrum &&
              session.last_events()[0].mask == 0,
          "empty FoFiX overstrum reports no held fret mask");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.tick(1.2, kStrum);
    CHECK(session.misses() == 1 && session.overstrums() == 1,
          "late bad strum applies both FoFiX missed-note and bad-pick penalties");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type == FoFiXSessionEventType::Miss &&
              session.last_events()[1].type ==
                  FoFiXSessionEventType::Overstrum &&
              session.last_events()[1].mask == 0 &&
              session.last_events()[1].rock_fill < 0.5,
          "FoFiX session reports miss and bad-pick deltas on the same tick");
  }

  {
    FoFiXGameplaySession session({{100.0, 100.0, kGreen, false, false}});
    double t = 1.0;
    for (int i = 0; i < 450 && !session.failed(); ++i, t += 0.1) {
      session.tick(t, (i & 1) ? 0 : kStrum);
    }
    CHECK(session.failed(), "repeated bad picks can fail the song");
    const int overstrums = session.overstrums();
    const int score = session.score();
    session.tick(t + 0.1, 0);
    session.tick(t + 0.2, kGreen | kStrum);
    CHECK(session.overstrums() == overstrums && session.score() == score,
          "failed FoFiX session ignores later picks and scoring");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.set_rock_fill_for_diagnostic(0.25);
    CHECK(session.rock_fill() > 0.249 && session.rock_fill() < 0.251,
          "diagnostic rock fill sets the FoFiX rock meter exactly");
    session.set_rock_fill_for_diagnostic(0.0);
    CHECK(session.failed(),
          "diagnostic rock fill can start a FoFiX session failed");
    session.tick(1.0, kGreen | kStrum);
    CHECK(session.score() == 0 && session.hits() == 0 &&
              session.last_events().empty(),
          "diagnostic failed FoFiX session ignores later picks and scoring");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.set_star_power_active_for_diagnostic(true);
    CHECK(session.star_power_active() &&
              session.star_power_fill() > 0.499 &&
              session.star_power_fill() < 0.501,
          "diagnostic active star power starts at the activation threshold when empty");
    session.tick(1.0, kGreen | kStrum);
    CHECK(session.score() == 100,
          "diagnostic active star power drives the FoFiX doubled score path");
    session.set_star_power_active_for_diagnostic(false);
    CHECK(!session.star_power_active(),
          "diagnostic active star power can be cleared without draining the meter");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {1.1, 1.1, kRed, true, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kRed);
    CHECK(session.score() == 100 && session.streak() == 2 &&
              session.hits() == 2,
          "HOPO note hits on fret edge after prior streak");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {1.1, 1.1, kRed, true, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kRed);
    session.tick(1.15, kRed | kStrum);
    CHECK(session.score() == 100 && session.streak() == 2 &&
              session.overstrums() == 0,
          "GH2 strict ignores one nearby strum while the last HOPO fret remains held");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::HopoStrumIgnored &&
              session.last_events()[0].mask == kRed,
          "ignored GH2-strict HOPO strums are surfaced as neutral session events");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {1.1, 1.1, kRed, true, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kRed);
    session.tick(1.15, kRed | kYellow | kStrum);
    CHECK(session.overstrums() == 1 && session.streak() == 0,
          "higher held frets still turn the extra HOPO strum into an overstrum");
  }

  {
    FoFiXSessionNote starter;
    starter.time = 1.0;
    starter.end_time = 1.0;
    starter.mask = kGreen;
    starter.hopo_tappable = 1;
    FoFiXSessionNote ending;
    ending.time = 1.1;
    ending.end_time = 1.1;
    ending.mask = kRed;
    ending.hopo_tappable = 3;
    FoFiXGameplaySession session({starter, ending});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kRed);
    CHECK(session.score() == 100 && session.streak() == 2 &&
              session.hits() == 2,
          "FoFiX tappable end-class note is playable as a HOPO without the legacy bool");
    session.tick(1.15, kRed | kStrum);
    CHECK(session.overstrums() == 0 && session.streak() == 2 &&
              session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::HopoStrumIgnored,
          "FoFiX class-3 HOPO end notes accept the same neutral GH2-strict extra strum");
  }

  {
    FoFiXSessionNote chord_green;
    chord_green.time = 1.0;
    chord_green.end_time = 1.0;
    chord_green.mask = kGreen;
    chord_green.hopo_tappable = 1;
    FoFiXSessionNote chord_yellow;
    chord_yellow.time = 1.0;
    chord_yellow.end_time = 1.0;
    chord_yellow.mask = kYellow;
    chord_yellow.hopo_tappable = 1;
    FoFiXSessionNote after_chord_red;
    after_chord_red.time = 1.1;
    after_chord_red.end_time = 1.1;
    after_chord_red.mask = kRed;
    after_chord_red.hopo_tappable = 3;
    FoFiXGameplaySession session({
        chord_green,
        chord_yellow,
        after_chord_red,
    });
    session.tick(1.0, kGreen | kYellow | kStrum);
    session.tick(1.05, 0);
    session.tick(1.1, kRed);
    CHECK(session.score() == 150 && session.streak() == 2 &&
              session.hits() == 2 && session.overstrums() == 0,
          "FoFiX after-chord tappable run is playable by strumming the chord then hammering on the following single");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].mask == kRed,
          "after-chord HOPO reports the following single as a normal native hit event");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kRed, false, false},
        {1.1, 1.1, kGreen, true, false},
    });
    session.tick(1.0, kGreen | kRed | kStrum);
    session.tick(1.1, kGreen);
    CHECK(session.score() == 100 && session.streak() == 2 &&
              session.hits() == 2 && session.overstrums() == 0,
          "HOPO pull-off hits when releasing a higher fret leaves the required lower fret held");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].mask == kGreen,
          "FoFiX session reports pull-off HOPO hits as normal hit events");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {1.05, 1.05, kRed, false, false},
    });
    session.tick(1.05, kRed | kStrum);
    session.tick(1.3, 0);
    CHECK(session.score() == 50 && session.streak() == 1 &&
              session.hits() == 1 && session.misses() == 0,
          "strumming a later matched note skips earlier in-window candidates without a miss");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false, 0.5, 0.1, 0.1, 0, 100},
        {1.2, 1.2, kRed, false, false, 0.5, 0.1, 0.1, 1, 120},
    });
    session.tick(1.2, kRed | kStrum);
    CHECK(session.score() == 50 && session.streak() == 1 &&
              session.hits() == 1 && session.misses() == 1 &&
              session.overstrums() == 0,
          "strumming a later matched note after an earlier note expired reports FoFiX catch-up miss before hit");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type == FoFiXSessionEventType::Miss &&
              session.last_events()[0].mask == kGreen &&
              session.last_events()[0].source_index == 0 &&
              session.last_events()[0].source_tick == 100 &&
              session.last_events()[1].type == FoFiXSessionEventType::Hit &&
              session.last_events()[1].mask == kRed &&
              session.last_events()[1].source_index == 1 &&
              session.last_events()[1].source_tick == 120,
          "FoFiX catch-up miss is surfaced before the matched hit event");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false, 0.5, 0.0, 0.0, 0, 100},
        {1.05, 1.05, kRed, false, false, 0.5, 0.0, 0.0, 1, 105},
    });
    session.tick(1.05, kRed | kStrum);
    std::vector<uint8_t> consumed(2, 0);
    session.copy_source_consumed(consumed);
    CHECK(consumed[0] == 1 && consumed[1] == 1,
          "FoFiX session exposes skipped and hit chart notes as consumed for highway rendering");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {1.05, 1.05, kGreen, false, false},
    });
    session.tick(1.03, kGreen | kStrum);
    CHECK(session.score() == 50 && session.hits() == 1,
          "single FoFiX strum consumes only one note group");
    session.tick(1.3, kGreen);
    CHECK(session.misses() == 1,
          "second in-window group remains live after the first strum hit");
  }

  {
    FoFiXGameplaySession session({
        {0.8, 0.8, kYellow, false, false},
        {1.0, 1.0, kGreen, false, false},
        {1.05, 1.05, kRed, true, false},
    });
    session.tick(0.8, kYellow | kStrum);
    session.tick(1.05, kRed);
    CHECK(session.score() == 50 && session.streak() == 1 &&
              session.hits() == 1,
          "HOPO cannot jump over an earlier unmatched in-window candidate");
    session.tick(1.3, 0);
    CHECK(session.misses() == 2 && session.streak() == 0,
          "blocked HOPO candidates still miss later through the normal miss path");
  }

  {
    FoFiXGameplaySession session({{1.0, 2.0, kGreen, false, false}});
    CHECK(session.diagnostic_autoplay_mask(0.95) == (kGreen | kStrum),
          "FoFiX diagnostic autoplay strums the next in-window note group");
    session.tick(1.0, kGreen | kStrum);
    CHECK(session.diagnostic_autoplay_mask(1.2) == kGreen,
          "FoFiX diagnostic autoplay holds active sustain tails without restrumming");
    session.tick(2.0, kGreen);
    CHECK(session.diagnostic_autoplay_mask(2.1) == 0,
          "FoFiX diagnostic autoplay releases after consumed sustain tails");
    CHECK(session.score() == 150,
          "one-second held sustain adds FoFiX sustain score");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::Sustain &&
              session.last_events()[0].mask == kGreen &&
              session.last_events()[0].score_delta == 100,
          "FoFiX session reports sustain delta for native presentation");
  }

  {
    FoFiXGameplaySession session({{1.0, 2.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.5, kGreen | kRed);
    std::vector<FoFiXSessionSustain> sustains;
    session.copy_active_sustains(sustains);
    CHECK(session.score() == 50 && session.last_events().empty() &&
              sustains.size() == 1 && sustains[0].mask == kGreen,
          "extra frets do not cut a FoFiX sustain while the played fret remains held");
    session.tick(2.0, kGreen | kRed);
    CHECK(session.score() == 150 && session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Sustain &&
              session.last_events()[0].score_delta == 100,
          "extra-fret sustain still awards the full held-tail score at release/end");
  }

  {
    FoFiXGameplaySession session({{1.0, 2.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.5, kRed);
    std::vector<FoFiXSessionSustain> sustains;
    session.copy_active_sustains(sustains);
    CHECK(session.score() == 100 && sustains.empty() &&
              session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Sustain &&
              session.last_events()[0].score_delta == 50,
          "releasing the played sustain fret still ends the FoFiX tail immediately");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen, false, true},
        {3.0, 3.0, kRed, false, true},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.25, kGreen | kRed | kWhammy, -0.25f);
    std::vector<FoFiXSessionSustain> sustains;
    session.copy_active_sustains(sustains);
    CHECK(session.star_power_fill() > 0.008 &&
              session.star_power_fill() < 0.009 && sustains.size() == 1,
          "extra frets do not block FoFiX whammy gain on a held star sustain");
  }

  {
    FoFiXGameplaySession session({{
        1.0, 2.0, kGreen, false, false, 0.5, 0.1, 0.1, 0, 480,
    }});
    const uint32_t hit_mask = session.diagnostic_autoplay_mask(0.95);
    CHECK(hit_mask == (kGreen | kStrum),
          "FoFiX diagnostic autoplay can hit a sustain early");
    session.tick(0.95, hit_mask);
    CHECK(session.diagnostic_autoplay_mask(0.96) == kGreen,
          "FoFiX diagnostic autoplay holds early-hit sustains before their scoring start");
    session.tick(0.96, kGreen);
    std::vector<FoFiXSessionSustain> sustains;
    session.copy_active_sustains(sustains);
    CHECK(sustains.size() == 1 && sustains[0].mask == kGreen,
          "early-hit active sustains remain live for native tail presentation");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false, 0.5, 0.0, 0.0, 0, 480},
        {1.05, 1.05, kRed, false, false, 0.5, 0.0, 0.0, 1, 504},
    });
    const uint32_t first = session.diagnostic_autoplay_mask(1.0);
    CHECK(first == (kGreen | kStrum),
          "FoFiX diagnostic autoplay pulses strum on the first close note");
    session.tick(1.0, first);
    const uint32_t second = session.diagnostic_autoplay_mask(1.05);
    CHECK(second == (kRed | kStrum),
          "FoFiX diagnostic autoplay pulses strum again for a new close note tick");
    session.tick(1.05, second);
    CHECK(session.score() == 100 && session.hits() == 2 && session.misses() == 0,
          "close diagnostic-autoplay notes do not miss from a stuck strum bit");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false, 0.5, 0.05, 0.05, 0, 480},
        {1.1, 1.1, kRed, false, false, 0.5, 0.05, 0.05, 1, 528},
    });
    const uint32_t final_mask = session.tick_diagnostic_autoplay(1.3);
    CHECK(final_mask == 0,
          "diagnostic autoplay releases transient strum after frame-skip catch-up");
    CHECK(session.score() == 100 && session.hits() == 2 &&
              session.misses() == 0 && session.overstrums() == 0,
          "diagnostic autoplay hits crossed notes at chart time during a slow frame");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].source_tick == 480 &&
              session.last_events()[1].type == FoFiXSessionEventType::Hit &&
              session.last_events()[1].source_tick == 528,
          "diagnostic autoplay preserves all substep hit events for native presentation");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen | kRed, false, false, 0.5, 0.05, 0.05, 0, 480},
        {1.1, 1.1, kYellow, false, false, 0.5, 0.05, 0.05, 1, 528},
    });
    session.tick_diagnostic_autoplay(1.3);
    CHECK(session.hits() == 2 && session.overstrums() == 0 &&
              session.misses() == 0,
          "diagnostic autoplay releases non-sustain frets between caught-up strums");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen | kRed, false, false, 0.5, 0.05, 0.05, 0, 480},
        {1.5, 1.5, kYellow, false, false, 0.5, 0.05, 0.05, 1, 720},
    });
    session.tick(1.0, kGreen | kRed | kStrum);
    const uint32_t next = session.diagnostic_autoplay_mask(1.5);
    CHECK(next == (kYellow | kStrum),
          "diagnostic autoplay releases active sustain frets before a different strummed note");
    session.tick(1.5, next);
    CHECK(session.hits() == 2 && session.overstrums() == 0 &&
              session.streak() == 2,
          "released sustain frets do not contaminate the next autoplay hit");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen | kRed, false, false, 0.5, 0.05, 0.05, 0, 480},
        {1.5, 1.5, kYellow, false, false, 0.5, 0.05, 0.05, 1, 720},
    });
    session.tick_diagnostic_autoplay(1.6);
    CHECK(session.hits() == 2 && session.overstrums() == 0 &&
              session.misses() == 0 && session.streak() == 2,
          "frame-skip autoplay also releases sustained frets before a different strummed note");
  }

  {
    FoFiXGameplaySession session({
        {1.00, 1.00, kGreen, false, false},
        {1.03, 1.03, kRed, false, false},
    });
    uint32_t mask = session.diagnostic_autoplay_mask(0.95);
    CHECK(mask == (kGreen | kStrum),
          "FoFiX diagnostic autoplay starts close-note strum pulse");
    session.tick(0.95, mask);
    mask = session.diagnostic_autoplay_mask(0.96);
    CHECK(mask == kRed,
          "FoFiX diagnostic autoplay releases strum before next close group");
    session.tick(0.96, mask);
    mask = session.diagnostic_autoplay_mask(0.98);
    CHECK(mask == (kRed | kStrum),
          "FoFiX diagnostic autoplay restrums close next group");
    session.tick(0.98, mask);
    CHECK(session.hits() == 2 && session.misses() == 0 &&
              session.overstrums() == 0,
          "FoFiX diagnostic autoplay hits close note groups cleanly");
  }

  {
    FoFiXGameplaySession session({
        {0.50, 0.50, kGreen, false, false},
        {0.75, 0.75, kRed, false, false},
        {1.00, 1.00, kYellow, false, false},
    });
    session.tick_diagnostic_autoplay(2.0);
    CHECK(session.hits() == 3 && session.misses() == 0 &&
              session.overstrums() == 0 && !session.failed(),
          "FoFiX diagnostic autoplay catch-up consumes due notes as hits");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen, false, true, 0.5, 0.0, 0.0, 7, 480},
    });
    std::vector<FoFiXSessionSustain> sustains;
    session.copy_active_sustains(sustains);
    CHECK(sustains.empty(), "FoFiX active sustain export starts empty");
    session.tick(1.0, kGreen | kStrum);
    session.copy_active_sustains(sustains);
    CHECK(sustains.size() == 1 && sustains[0].mask == kGreen &&
              sustains[0].star_power_tail && sustains[0].source_index == 7 &&
              sustains[0].source_tick == 480 &&
              sustains[0].start_time >= 1.0 &&
              sustains[0].end_time == 2.0,
          "FoFiX session exports held sustain tails for native highway rendering");
    session.tick(2.0, kGreen);
    session.copy_active_sustains(sustains);
    CHECK(sustains.empty(),
          "FoFiX active sustain export clears when the held tail ends");
  }

  {
    FoFiXGameplaySession session({{1.0, 2.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kGreen | kStrum);
    session.tick(2.0, kGreen);
    CHECK(session.score() == 50 && session.overstrums() == 1,
          "manual strum during sustain clears FoFiX tail bonus");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen, false, true},
        {3.0, 3.0, kRed, false, true},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.05, kGreen | kWhammy, -0.1f);
    CHECK(session.star_power_fill() > 0.0016 &&
              session.star_power_fill() < 0.0018,
          "moving whammy begins GH2 star-power gain immediately on a held star sustain");
    session.tick(1.25, kGreen | kWhammy, -0.1f);
    CHECK(session.star_power_fill() > 0.008 &&
              session.star_power_fill() < 0.009,
          "GH2 whammy timeout keeps fixed-rate star-power gain alive after bar motion");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPowerWhammy &&
              session.last_events()[0].mask == kGreen &&
              session.last_events()[0].star_power_fill > 0.008,
          "GH2 moving-bar whammy gain is surfaced for native presentation");
    session.tick(1.8, kGreen | kWhammy, -0.1f);
    CHECK(session.star_power_fill() > 0.008 &&
              session.star_power_fill() < 0.009 &&
              session.last_events().empty(),
          "stationary whammy stops earning after the decoded GH2 timeout");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen, false, true},
        {3.0, 3.0, kRed, false, true},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.25, kGreen | kWhammy, 0.0f);
    CHECK(session.star_power_fill() == 0.0 && session.last_events().empty(),
          "holding a digital whammy flag without bar motion does not earn star power");
  }

  {
    FoFiXGameplaySession session({{1.0, 2.0, kGreen, false, false}});
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.25, kGreen | kWhammy);
    CHECK(session.star_power_fill() == 0.0 && session.last_events().empty(),
          "FoFiX whammy does not award meter on non-star sustain tails");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 2.0, kGreen, false, false},
        {1.5, 1.5, kRed, false, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kStrum);
    session.tick(2.0, kRed);
    CHECK(session.score() == 100 && session.hits() == 2,
          "new hit during sustain does not award a repick tail bonus");
  }

  {
    FoFiXGameplaySession session({
        make_note(1.0, 1.0, kGreen, false, true),
        make_note(1.5, 1.5, kRed, false, true, true),
        make_note(2.0, 2.0, kYellow, false, false),
    });
    session.tick(1.0, kGreen | kStrum);
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].phrase_state == 2,
          "star phrase hit reports retail kPhraseHitting state");
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kStrum);
    CHECK(session.star_power_fill() > 0.249 &&
              session.star_power_fill() < 0.251,
          "completed star phrase awards quarter meter on final star hit");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].phrase_state == 3 &&
              session.last_events()[1].type ==
                  FoFiXSessionEventType::StarPhraseComplete &&
              session.last_events()[1].star_power_fill > 0.249 &&
              session.last_events()[1].star_power_fill < 0.251,
          "FoFiX session reports star phrase completion after final star hit");
    session.tick(1.6, kRed);
    session.tick(2.0, kYellow | kStrum);
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit,
          "next non-star boundary does not re-award a completed final-star phrase");
  }

  {
    FoFiXGameplaySession session({
        make_note(1.0, 1.0, kGreen, false, true),
        make_note(1.5, 1.5, kRed | kYellow, false, true, true),
        make_note(2.0, 2.0, kYellow, false, false),
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kYellow | kStrum);
    CHECK(session.star_power_fill() > 0.249 &&
              session.star_power_fill() < 0.251 &&
              session.last_events().size() == 2 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit &&
              session.last_events()[0].gem_count == 2 &&
              session.last_events()[1].type ==
                  FoFiXSessionEventType::StarPhraseComplete,
          "all gems in a final-star chord complete the FoFiX phrase once");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, true},
        {1.5, 1.5, kRed, false, false},
    });
    session.tick(1.0, kRed | kStrum);
    session.tick(1.005, kRed);
    session.tick(1.01, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kStrum);
    CHECK(session.overstrums() == 1 && session.hits() == 2 &&
              session.star_power_fill() == 0.0,
          "wrong strum inside a star note window breaks FoFiX star phrase");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPhraseMiss &&
              session.last_events()[1].type == FoFiXSessionEventType::Hit,
          "FoFiX session reports missed star phrase before boundary hit");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, true},
        {1.5, 1.5, kRed, false, true},
        {2.0, 2.0, kYellow, false, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.25, kYellow | kStrum);
    session.tick(1.3, 0);
    session.tick(1.5, kRed | kStrum);
    session.tick(1.6, kRed);
    session.tick(2.0, kYellow | kStrum);
    CHECK(session.overstrums() == 1 && session.hits() == 3 &&
              session.star_power_fill() == 0.0,
          "bad pick between star phrase notes breaks the active FoFiX phrase");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPhraseMiss &&
              session.last_events()[1].type == FoFiXSessionEventType::Hit,
          "inter-phrase overstrum reports missed star phrase before boundary hit");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, true},
        {1.5, 1.5, kRed, false, false},
        {2.0, 2.0, kGreen, false, true},
        {2.5, 2.5, kRed, false, false},
        {3.0, 3.0, kGreen, false, true},
        {3.5, 3.5, kRed, false, false},
        {4.0, 4.0, kGreen, false, true},
        {4.5, 4.5, kRed, false, false},
        {5.2, 5.2, kGreen, false, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kStrum);
    session.tick(1.6, kRed);
    session.tick(2.0, kGreen | kStrum);
    session.tick(2.1, kGreen);
    session.tick(2.5, kRed | kStrum);
    session.tick(2.6, kRed);
    session.tick(3.0, kGreen | kStrum);
    session.tick(3.1, kGreen);
    session.tick(3.5, kRed | kStrum);
    session.tick(3.6, kRed);
    session.tick(4.0, kGreen | kStrum);
    session.tick(4.1, kGreen);
    session.tick(4.5, kRed | kStrum);
    session.tick(4.6, kRed);
    CHECK(session.star_power_fill() > 0.999, "four phrases fill star meter");
    CHECK(!session.star_power_state().active &&
              session.star_power_state().value > 99.9,
          "session exposes raw full star meter for live gameplay adoption");
    session.tick(5.1, kStar);
    CHECK(session.star_power_active(), "star power activates at half or more");
    CHECK(session.star_power_state().active,
          "session exposes raw active star power state for live gameplay adoption");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPowerActivate &&
              session.last_events()[0].mask == kStar,
          "FoFiX session reports star power activation for native presentation");
    const int before_powered_note = session.score();
    session.tick(5.2, kGreen | kStrum);
    CHECK(session.score() - before_powered_note == 100,
          "active star power doubles subsequent note score");
    CHECK(session.rock_state().value > 15000.0,
          "session exposes raw FoFiX rock state for live gameplay adoption");
    session.tick(25.2, 0);
    CHECK(!session.star_power_active() && session.star_power_fill() == 0.0,
          "star power drains out over time");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPowerDeactivate &&
              session.last_events()[0].star_power_fill == 0.0,
          "FoFiX session reports star power deactivation for native presentation");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, true},
        {1.5, 1.5, kRed, false, false},
        {2.0, 2.0, kGreen, false, true},
        {2.5, 2.5, kRed, false, false},
    });
    session.tick(0.5, kStar);
    session.tick(1.0, kStar | kGreen | kStrum);
    session.tick(1.1, kStar | kGreen);
    session.tick(1.5, kStar | kRed | kStrum);
    session.tick(1.6, kStar | kRed);
    session.tick(2.0, kStar | kGreen | kStrum);
    session.tick(2.1, kStar | kGreen);
    session.tick(2.5, kStar | kRed | kStrum);
    session.tick(2.6, kStar | kRed);
    CHECK(!session.star_power_active() &&
              session.star_power_fill() > 0.499 &&
              session.star_power_fill() < 0.501,
          "held star power does not auto-activate when phrases reach half meter");
    CHECK(session.last_events().empty(),
          "held star power produces no activation event after crossing threshold");
    session.tick(2.7, 0);
    session.tick(2.8, kStar);
    CHECK(session.star_power_active(),
          "fresh star-power press activates after threshold is available");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPowerActivate,
          "fresh star-power edge reports one activation event");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, true},
        {1.5, 1.5, kRed, false, false},
        {2.0, 2.0, kGreen, false, true},
        {2.5, 2.5, kRed, false, false},
    });
    session.tick(1.0, kGreen | kStrum);
    session.tick(1.1, kGreen);
    session.tick(1.5, kRed | kStrum);
    session.tick(1.6, kRed);
    session.tick(2.0, kGreen | kStrum);
    session.tick(2.1, kGreen);
    session.tick(2.5, kRed | kStrum);
    session.tick(2.6, kRed);
    CHECK((session.diagnostic_autoplay_mask(2.7) & kStar) == 0,
          "default diagnostic autoplay does not inject star power activation");
    const uint32_t powered_mask =
        session.diagnostic_autoplay_mask(2.7, true);
    CHECK((powered_mask & kStar) != 0,
          "diagnostic autoplay can request star power once FoFiX meter reaches half");
    session.tick(2.7, powered_mask);
    CHECK(session.star_power_active(),
          "diagnostic autoplay star-power edge activates the FoFiX session");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPowerActivate,
          "diagnostic autoplay activation emits the native session event");
    CHECK((session.diagnostic_autoplay_mask(2.8, true) & kStar) == 0,
          "diagnostic autoplay releases star power after activation");
  }

  {
    FoFiXGameplaySession session({{1.0, 1.0, kGreen, false, false}});
    session.set_star_power_fill_for_diagnostic(0.5);
    CHECK(!session.star_power_active() && session.star_power_fill() == 0.5,
          "diagnostic star power seed does not force active state");
    const uint32_t powered_mask =
        session.diagnostic_autoplay_mask(0.1, true);
    CHECK((powered_mask & kStar) != 0,
          "diagnostic star power seed can drive a real activation edge");
    session.tick(0.1, powered_mask);
    CHECK(session.star_power_active(),
          "seeded diagnostic star power activates through the FoFiX edge path");
  }

  {
    ghogx::chart::Chart chart;
    chart.ticks_per_beat = 480;
    chart.tempo_map = {
        {0, 500000},
        {960, 1000000},
    };
    chart.notes[3] = {
        {0, 0, 0, false, false},
        {240, 240, 1, true, false},
        {960, 1440, 2, false, false},
    };

    FoFiXGameplaySession session =
        FoFiXGameplaySession::FromChart(chart, 3);
    session.tick(0.0, kGreen | kStrum);
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].source_index == 0 &&
              session.last_events()[0].source_tick == 0,
          "chart-backed session reports source note index and tick for hit presentation");
    session.tick(0.25, kRed);
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].source_index == 1 &&
              session.last_events()[0].source_tick == 240,
          "chart-backed session reports source tick for HOPO presentation");
    session.tick(0.9, kYellow | kStrum);
    session.tick(2.0, kYellow);
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::Sustain &&
              session.last_events()[0].source_index == 2 &&
              session.last_events()[0].source_tick == 960,
          "chart-backed session reports source tick for sustain presentation");
    CHECK(session.score() == 250 && session.hits() == 3,
          "chart-backed session uses MIDI ticks, HOPOs, tempo hit windows, and sustain beat scale");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {2.0, 2.0, kRed, false, false},
    });
    session.seek_without_scoring(1.3);
    CHECK(session.score() == 0 && session.misses() == 0,
          "diagnostic seek consumes earlier notes without scoring or miss penalties");
    session.tick(2.0, kRed | kStrum);
    CHECK(session.score() == 50 && session.hits() == 1,
          "session remains playable after no-score seek");
  }

  {
    FoFiXGameplaySession session({
        {1.0, 1.0, kGreen, false, false},
        {2.0, 2.0, kRed, false, false},
    });
    session.seek_without_scoring(1.05);
    session.tick(1.2, 0);
    CHECK(session.score() == 0 && session.misses() == 0,
          "diagnostic seek consumes pre-start notes even while their late window is open");
    session.tick(2.0, kRed | kStrum);
    CHECK(session.score() == 50 && session.hits() == 1,
          "session remains playable after in-window no-score seek");
  }

  {
    FoFiXGameplaySession session({
        make_note(1.0, 1.0, kGreen, false, true),
        make_note(1.5, 1.5, kRed, false, true, true),
        make_note(2.0, 2.0, kYellow, false, false),
    });
    session.seek_without_scoring(1.25);
    CHECK(session.score() == 0 && session.misses() == 0 &&
              session.star_power_fill() == 0.0,
          "diagnostic seek into a star phrase has no scoring or miss penalty");
    session.tick(1.5, kRed | kStrum);
    CHECK(session.hits() == 1 && session.star_power_fill() == 0.0 &&
              session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit,
          "partial phrase after diagnostic seek does not award on final star");
    session.tick(1.6, kRed);
    session.tick(2.0, kYellow | kStrum);
    CHECK(session.hits() == 2 && session.star_power_fill() == 0.0,
          "boundary hit after a partial diagnostic phrase stays unpowered");
    CHECK(session.last_events().size() == 2 &&
              session.last_events()[0].type ==
                  FoFiXSessionEventType::StarPhraseMiss &&
              session.last_events()[1].type == FoFiXSessionEventType::Hit,
          "partial diagnostic phrase reports a missed phrase at the boundary");
  }

  {
    FoFiXGameplaySession session({
        make_note(1.0, 1.0, kGreen, false, true),
        make_note(1.5, 1.5, kRed, false, true, true),
        make_note(2.0, 2.0, kYellow, false, false),
    });
    session.seek_without_scoring(1.75);
    session.tick(2.0, kYellow | kStrum);
    CHECK(session.hits() == 1 && session.star_power_fill() == 0.0,
          "diagnostic seek past a whole star phrase does not award meter");
    CHECK(session.last_events().size() == 1 &&
              session.last_events()[0].type == FoFiXSessionEventType::Hit,
          "diagnostic seek past a whole star phrase leaves no stale phrase miss");
  }

  {
    FoFiXGameplaySession practice({
        make_note(1.0, 1.0, kGreen, false, false),
        make_note(2.0, 2.0, kRed, false, false),
    });
    practice.set_failure_enabled(false);
    practice.set_rock_fill_for_diagnostic(0.0);
    practice.tick(1.3, 0);
    practice.tick(2.0, kRed | kStrum);
    CHECK(!practice.failed() && practice.misses() == 1 && practice.hits() == 1,
          "Practice keeps judging misses and later hits with an empty rock meter");
    practice.set_rock_fill_for_diagnostic(0.0);
    practice.set_failure_enabled(true);
    CHECK(practice.failed(), "normal play retains failure at an empty rock meter");
  }

  if (failures == 0) {
    std::fprintf(stderr, "gameplay_session_test: PASS\n");
  }
  return failures == 0 ? 0 : 1;
}
