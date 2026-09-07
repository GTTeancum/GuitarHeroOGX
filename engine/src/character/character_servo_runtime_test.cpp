#include "character/char_servo.h"
#include "character/char_clip.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using ghogx::character::SourceCharServoRuntime;
using ghogx::milo_scene::Xfm;
using namespace ghogx::character;

namespace {
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void near(float actual, float expected, const char* message) {
  check(std::isfinite(actual) && std::fabs(actual - expected) < 0.0003f, message);
}
void same(const Xfm& a, const Xfm& b, const char* message) {
  for (int i = 0; i < 3; ++i) {
    near(a.pos[i], b.pos[i], message);
    for (int j = 0; j < 3; ++j) near(a.rot[i][j], b.rot[i][j], message);
  }
}
// Independent homogeneous 4x4 reference, rather than calling production compose.
Xfm world(const Xfm& local, const Xfm& owner) {
  float a[4][4]{}, b[4][4]{}, product[4][4]{};
  for (int i = 0; i < 3; ++i) {
    a[3][i] = local.pos[i]; b[3][i] = owner.pos[i];
    for (int j = 0; j < 3; ++j) { a[i][j] = local.rot[i][j]; b[i][j] = owner.rot[i][j]; }
  }
  a[3][3] = b[3][3] = 1;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k) product[i][j] += a[i][k] * b[k][j];
  Xfm result;
  for (int i = 0; i < 3; ++i) {
    result.pos[i] = product[3][i];
    for (int j = 0; j < 3; ++j) result.rot[i][j] = product[i][j];
  }
  return result;
}
Xfm owner_fixture() {
  Xfm result;
  const float c = std::cos(.71f), s = std::sin(.71f);
  result.rot[0][0] = c; result.rot[0][1] = s;
  result.rot[1][0] = -s; result.rot[1][1] = c;
  result.pos[0] = 11; result.pos[1] = -7; result.pos[2] = 3;
  return result;
}
Xfm pelvis_fixture() {
  Xfm result;
  const float c = std::cos(.39f), s = std::sin(.39f);
  // Orthogonal but nonuniformly scaled and reflected: catches transpose-only inverse.
  result.rot[0][0] = -1.5f;
  result.rot[1][1] = 2 * c; result.rot[1][2] = 2 * s;
  result.rot[2][1] = -.7f * s; result.rot[2][2] = .7f * c;
  result.pos[0] = 2; result.pos[1] = -3; result.pos[2] = 4;
  return result;
}
SourceCharServoRuntime fixture() {
  SourceCharServoRuntime result;
  result.has_typed_outputs = result.has_facing_position_delta = true;
  result.facing_position = {1, 2, -3}; result.facing_rotation = -.47f;
  result.delta_position = {.2f, -.1f, .3f}; result.delta_rotation = .13f;
  return result;
}

void test_regulation() {
  constexpr float pi = 3.14159265358979323846f;
  CharClip clip, other;
  clip.name = other.name = "same_name_different_object";
  SourceCharServoTransition transition{&clip, 3.0f, 1.0f};
  SourceCharServoWaypoint waypoint;
  waypoint.radius = 0.0f;
  waypoint.world.pos[0] = 12.0f;
  SourceCharServoRegulation regulator;
  int samples = 0;
  std::vector<float> sample_beats;
  auto moving_sample = [&](const CharClip& selected, float beat)
      -> std::optional<SourceCharUtlClipPredictFrame> {
    check(&selected == &clip, "Regulate samples oldest outgoing clip identity");
    ++samples; sample_beats.push_back(beat);
    SourceCharUtlClipPredictFrame result;
    result.facing_pos = {beat * 2.0f, 0.0f, beat};
    return result;
  };
  auto still_sample = [](const CharClip&, float)
      -> std::optional<SourceCharUtlClipPredictFrame> { return SourceCharUtlClipPredictFrame{}; };
  Xfm owner;
  auto result = regulator.apply(owner, transition, .5f, moving_sample);
  check(!result.applied && samples == 0, "no waypoint skips prediction");
  regulator.waypoint = &waypoint;
  result = regulator.apply(owner, {}, .5f, moving_sample);
  check(!result.applied && samples == 0, "no outgoing pair skips prediction");
  for (float ramp : {0.0f, -2.0f}) {
    result = regulator.apply(owner, {&clip, 3, ramp}, .5f, moving_sample);
    check(!result.applied && samples == 0, "nonpositive scheduled ramp skips regulation");
  }

  result = regulator.apply(owner, transition, .5f, moving_sample);
  check(result.applied && !result.used_override && samples == 2, "ordinary regulation executes");
  check(sample_beats == std::vector<float>{3, 4}, "predict raw ramp, not smoothing denominator");
  near(result.gain, .25f, "ordinary denominator floors at two");
  near(result.prediction.pos[0], 2, "lookahead uses outgoing motion");
  near(result.prediction.pos[2], 1, "prediction retains vertical motion");
  near(owner.pos[0], 2.5f, "correction uses predicted endpoint, not current origin");
  near(owner.pos[2], 0, "radial waypoint does not correct vertical position");
  owner = {};
  result = regulator.apply(owner, {&clip, 3, 6}, .5f, moving_sample);
  near(result.gain, .125f, "long ramp denominator uses ramp divided by 1.5");
  near(result.prediction_end_beat, 9, "long ramp predicts to outgoing beat plus ramp");
  near(owner.pos[0], 0, "predicted arrival inside waypoint requires no correction");

  SourceCharServoWaypoint override_waypoint = waypoint;
  override_waypoint.world.pos[0] = 30;
  regulator.override_waypoint = &override_waypoint;
  regulator.override_clip = &clip;
  regulator.override_beat = 3.5f;
  owner = {};
  result = regulator.apply(owner, transition, 1, moving_sample);
  check(result.used_override, "override requires outgoing clip identity");
  near(result.gain, 3, "override has neither denominator floor nor gain clamp");
  near(result.prediction_end_beat, 3.5f, "override replaces lookahead endpoint");
  near(owner.pos[0], 87, "override uses its own waypoint");
  regulator.override_beat = 3;
  const auto before = owner; const int before_samples = samples;
  result = regulator.apply(owner, transition, .5f, moving_sample);
  check(!result.applied && samples == before_samples, "expired matching override returns without base fallback");
  same(owner, before, "expired override leaves transform untouched");
  regulator.override_clip = &other;
  regulator.prediction_offset = {2, 0, 0};
  owner = {};
  result = regulator.apply(owner, transition, .5f, moving_sample);
  check(!result.used_override, "same name is not same override clip");
  near(owner.pos[0], 2, "mismatched override still applies stored prediction offset to base waypoint");
  regulator.waypoint = nullptr; owner = {};
  const int mismatch_samples = samples;
  result = regulator.apply(owner, transition, .5f, moving_sample);
  check(!result.applied && samples == mismatch_samples, "override-only mismatch does not invent a base waypoint");

  regulator = {}; regulator.waypoint = &waypoint;
  waypoint.world = {}; waypoint.radius = 100; waypoint.angle_radius = .1f;
  owner = {}; owner.pos[0] = 2; owner.pos[1] = -3;
  // Tilted/sheared/scaled basis: row0 would report +0.4, row1 reports +0.8.
  owner.rot[0][0] = 2 * std::cos(.4f); owner.rot[0][1] = 2 * std::sin(.4f);
  owner.rot[1][0] = -3 * std::sin(.8f); owner.rot[1][1] = 3 * std::cos(.8f);
  owner.rot[1][2] = .6f; owner.rot[2][0] = .2f;
  const auto tilted = owner;
  result = regulator.apply(owner, transition, 1, still_sample);
  near(result.prediction.ang, .8f, "GH2 heading uses negative atan2 of row1 x,y");
  near(result.angle_correction, -.35f, "angular radius removed before gain");
  for (int row = 0; row < 3; ++row) {
    near(owner.rot[row][0], tilted.rot[row][0] * std::cos(-.35f) - tilted.rot[row][1] * std::sin(-.35f), "RotateAboutZ keeps source basis scale x");
    near(owner.rot[row][1], tilted.rot[row][0] * std::sin(-.35f) + tilted.rot[row][1] * std::cos(-.35f), "RotateAboutZ keeps source basis scale y");
    near(owner.rot[row][2], tilted.rot[row][2], "RotateAboutZ preserves vertical basis");
    near(owner.pos[row], tilted.pos[row], "rotation correction must not orbit owner position");
  }

  owner = {}; waypoint.world = {};
  owner.rot[1][0] = -std::sin(pi - .05f); owner.rot[1][1] = std::cos(pi - .05f);
  waypoint.world.rot[1][0] = -std::sin(-pi + .05f);
  waypoint.world.rot[1][1] = std::cos(-pi + .05f);
  waypoint.angle_radius = .02f;
  result = regulator.apply(owner, transition, 1, still_sample);
  near(result.angle_correction, .04f, "waypoint angle wraps across pi along short direction");

  // Prediction offset rotates by FUTURE facing, not current owner yaw.
  waypoint.world = {}; waypoint.radius = 0; waypoint.y_radius = 0;
  waypoint.angle_radius = pi; regulator.prediction_offset = {2, 0, 5}; owner = {};
  result = regulator.apply(owner, transition, 1,
      [&](const CharClip&, float beat) -> std::optional<SourceCharUtlClipPredictFrame> {
        SourceCharUtlClipPredictFrame frame;
        frame.facing_rot = (beat - 3) * pi * .5f;
        return frame;
      });
  near(owner.pos[0], 0, "future heading offset x");
  near(owner.pos[1], -1, "future heading offset y");
  near(owner.pos[2], 0, "radial shape ignores offset height");

  // Box projection uses raw waypoint rows, including vertical components.
  regulator.prediction_offset = {}; waypoint.world = {};
  waypoint.world.rot[0][2] = 1; waypoint.radius = 1; waypoint.y_radius = 2;
  owner = {}; owner.pos[0] = 4; owner.pos[1] = 5; owner.pos[2] = 3;
  result = regulator.apply(owner, transition, 1, still_sample);
  near(owner.pos[0], 1, "box raw row0 projection");
  near(owner.pos[1], 3.5f, "box independent y radius");
  near(owner.pos[2], 0, "tilted box may correct z");

  bool missing = false;
  const auto before_missing = owner;
  try {
    regulator.apply(owner, transition, 1,
        [](const CharClip&, float) -> std::optional<SourceCharUtlClipPredictFrame> { return std::nullopt; });
  } catch (const std::runtime_error&) { missing = true; }
  check(missing, "missing facing channel is an explicit error");
  same(owner, before_missing, "missing facing cannot partially change owner");
}

void test_regulation_stack() {
  CharClip a, b, c;
  for (auto* clip : {&a, &b, &c}) {
    clip->loaded = true; clip->frames.resize(2);
    clip->fps = 30; clip->start_beat = 0; clip->end_beat = 100;
    clip->beats_per_second = 2;
  }
  a.name = "a"; b.name = "b"; c.name = "c";
  CharClipPlayer player;
  check(!player.source_regulation_transition().outgoing_clip, "empty stack has no regulation pair");
  player.play(a, kCharPlayLoop, 4);
  check(!player.source_regulation_transition().outgoing_clip, "single node has no regulation pair");
  // Dirty mode seeds a nonzero node fraction; constructor must retain it
  // when adding the third node, while its scheduled ramp remains pending.
  player.play_source(b, kCharPlayLoop | kCharPlayDirty, 8, 5, 4);
  player.play_source(c, kCharPlayLoop | kCharPlayDirty, 12, 9, 4);
  check(player.source_stack_depth() == 3, "three genuine queued native layers");
  const auto pair = player.source_regulation_transition();
  check(pair.outgoing_clip == &a, "Last/Before uses oldest outgoing clip, not newest");
  near(pair.outgoing_beat, 0, "oldest outgoing beat");
  near(pair.next_ramp_in, 5, "immediate next oldest ramp, not head ramp");
  player.clear();
  check(!player.source_regulation_transition().outgoing_clip, "cleared player cannot retain stale pair");
}

void test_gh2_prediction_samples() {
  CharClip clip;
  clip.loaded = true; clip.fps = 30; clip.beats_per_second = 17;
  clip.start_beat = 10; clip.end_beat = 18;
  clip.frames.resize(101); // Deliberately unrelated expanded-pose frame count.
  clip.gh2_facing_samples.emplace();
  auto& source = *clip.gh2_facing_samples;
  source.positions = {{0, 1, 2}, {10, 3, 4}, {30, 5, 6}};
  source.rotations = {3, -3}; // Tests scalar interpolation, not shortest angle.
  auto sample = source_char_clip_facing_sample_at_beat(clip, 12);
  check(sample.has_value(), "normal facing sampler dispatches decoded GH2 track");
  near(sample->facing_pos[0], 5, "normalize beat interval, not fps/bps or expanded pose count");
  near(sample->facing_rot, 1.5f, "position and rotation use their own sample counts");
  sample = source_gh2_char_clip_facing_sample_at_beat(clip, 14);
  near(sample->facing_pos[0], 10, "middle position sample");
  near(sample->facing_rot, 0, "scalar interpolation crosses zero, not pi");
  sample = source_gh2_char_clip_facing_sample_at_beat(clip, -20);
  near(sample->facing_pos[0], 0, "early beat clamps first sample");
  sample = source_gh2_char_clip_facing_sample_at_beat(clip, 100);
  near(sample->facing_pos[0], 30, "late beat clamps last sample");
  SourceCharServoWaypoint waypoint;
  SourceCharServoRegulation regulator; regulator.waypoint = &waypoint;
  Xfm owner;
  const auto prediction = regulator.apply(owner, {&clip, 10, 4}, 1);
  check(prediction.applied, "Regulate default sampler uses production GH2 EvaluateChannel");
  near(prediction.prediction.last_pos[0], 10, "production prediction endpoint position");
  near(prediction.prediction.last_ang, 0, "production prediction endpoint rotation");
  source.interpolate_position = false;
  sample = source_gh2_char_clip_facing_sample_at_beat(clip, 12.04f);
  near(sample->facing_pos[0], 10, "disabled interpolation rounds to nearest position sample");
  near(sample->facing_rot, 1.47f, "position mode does not clobber rotation interpolation");
  source.positions = {{7, 8, 9}};
  sample = source_gh2_char_clip_facing_sample_at_beat(clip, 15);
  near(sample->facing_pos[0], 7, "one-sample channel remains constant");
  clip.end_beat = clip.start_beat;
  bool invalid = false;
  try { source_gh2_char_clip_facing_sample_at_beat(clip, 10); }
  catch (const std::runtime_error&) { invalid = true; }
  check(invalid, "invalid prediction span is diagnosed, not invented");
  clip.end_beat = 18; source.rotations.clear();
  check(!source_gh2_char_clip_facing_sample_at_beat(clip, 10), "missing facing rotation is reported");
}
}  // namespace

int main() {
  test_regulation();
  test_regulation_stack();
  test_gh2_prediction_samples();
  int cleared = 0;
  auto state = fixture(); state.delta_changed = true;
  float extra_scalars[] = {3, -2, 8};
  state.extra_delta_scalars = extra_scalars; state.extra_delta_scalar_count = 3;
  state.enter([&] { ++cleared; });
  for (float value : extra_scalars) near(value, 0, "Enter clears original PS2 scalar tail");
  check(state.move_self && !state.delta_changed && cleared == 1, "Enter uses allocation and clears regulator");
  near(state.delta_position[0], 0, "Enter clears translation delta");
  near(state.delta_rotation, 0, "Enter clears rotation delta");
  state.set_move_self(true); check(!state.delta_changed, "same movement mode does not mark changed");
  state.set_move_self(false); check(state.delta_changed && !state.move_self, "movement switch marks changed");
  state.set_move_self(false); check(state.delta_changed, "repeated request does not erase pending switch");
  state.has_facing_position_delta = false;
  state.enter([&] { ++cleared; }); check(!state.move_self, "Enter does not invent missing allocation");

  for (bool self : {false, true}) {
    state = fixture(); state.move_self = self;
    auto owner = owner_fixture(), pelvis = pelvis_fixture();
    const auto initial_owner = owner, initial_pelvis = pelvis;
    auto expected_owner = owner, expected_pelvis = pelvis;
    if (self) source_char_servo_bone_move_to_delta_facing(expected_owner, state.delta_position, state.delta_rotation);
    else source_char_servo_bone_move_to_facing(expected_pelvis, state.facing_position, state.facing_rotation);
    std::vector<int> order;
    state.poll(owner, pelvis, [&] { order.push_back(1); same(owner, initial_owner, "PoseMeshes occurs before movement"); },
        [&](Xfm& regulated) {
          order.push_back(2); same(regulated, expected_owner, "Regulate sees moved owner");
          near(state.delta_rotation, .13f, "deltas remain available during Regulate");
          regulated.pos[2] += .4f;
        });
    if (self) expected_owner.pos[2] += .4f;
    same(owner, expected_owner, "stable owner result"); same(pelvis, expected_pelvis, "stable pelvis result");
    check(order == (self ? std::vector<int>{1, 2} : std::vector<int>{1}), "source PoseMeshes/Regulate branch order");
    near(state.delta_rotation, 0, "stable poll zeros deltas");
    if (self) same(pelvis, initial_pelvis, "self movement does not rewrite pelvis");
  }

  for (bool incoming_self : {false, true}) {
    state = fixture(); state.move_self = !incoming_self; state.set_move_self(incoming_self);
    auto owner = owner_fixture(), pelvis = Xfm{};
    auto expected_owner = owner, expected_pelvis = pelvis_fixture();
    // The continuity equation uses the NEW pose from PoseMeshes, not last frame's pelvis.
    if (incoming_self) source_char_servo_bone_move_to_facing(expected_pelvis, state.facing_position, state.facing_rotation);
    else source_char_servo_bone_move_to_delta_facing(expected_owner, state.delta_position, state.delta_rotation);
    const auto expected_world = world(expected_pelvis, expected_owner);
    int regulated = 0;
    state.poll(owner, pelvis, [&] { pelvis = pelvis_fixture(); }, [&](Xfm&) { ++regulated; });
    same(world(pelvis, owner), expected_world, "movement switch preserves world pelvis with scaled/reflected bases");
    check(!state.delta_changed && regulated == (incoming_self ? 1 : 0), "switch consumes flag and regulates only self branch");
    near(state.delta_rotation, 0, "switch zeros rotation delta");
    if (incoming_self) same(pelvis, pelvis_fixture(), "switch to self keeps raw new pelvis pose");
    const auto settled_owner = owner;
    state.poll(owner, pelvis, [&] { pelvis = pelvis_fixture(); }, [&](Xfm&) {});
    same(owner, settled_owner, "second zero-delta frame cannot repeat ownership transfer");
  }

  state = fixture(); state.has_typed_outputs = false; state.delta_changed = true;
  extra_scalars[0] = 7;
  state.extra_delta_scalars = extra_scalars; state.extra_delta_scalar_count = 3;
  auto owner = owner_fixture(), pelvis = pelvis_fixture();
  const auto initial_owner = owner, initial_pelvis = pelvis;
  state.poll(owner, pelvis, [] { check(false, "empty typed output must not pose"); }, [](Xfm&) { check(false, "empty typed output must not regulate"); });
  same(owner, initial_owner, "empty owner untouched"); same(pelvis, initial_pelvis, "empty pelvis untouched");
  near(state.delta_rotation, .13f, "empty typed output skips even ZeroDeltas");
  check(state.delta_changed, "empty poll preserves changed flag");
  near(extra_scalars[0], 7, "empty typed output does not clear scalar tail");

  state.has_typed_outputs = true; state.has_facing_position_delta = false;
  int posed = 0;
  state.poll(owner, pelvis, [&] { ++posed; }, [](Xfm&) { check(false, "unallocated facing must not regulate"); });
  check(posed == 1 && state.delta_changed, "missing facing still poses but does not consume movement switch");
  near(state.delta_rotation, 0, "missing facing reaches ZeroDeltas");
  near(extra_scalars[0], 0, "missing facing still clears PS2 scalar tail");

  state = fixture(); state.move_self = true; state.delta_changed = true;
  pelvis = {}; for (auto& row : pelvis.rot) for (float& component : row) component = 0;
  bool invalid_reported = false;
  try { state.poll(owner, pelvis, [] {}, [](Xfm&) {}); }
  catch (const std::runtime_error&) { invalid_reported = true; }
  check(invalid_reported, "invalid source basis must not silently become identity");
  std::puts("PASS: PS2 ServoBone Poll branches, pose/regulate order, both continuity switches, scaled/reflected inverse, no double delta, Enter, reset and allocation gates");
  std::puts("PASS: PS2 Regulate oldest-pair selection, outgoing prediction, default/override timing, shape bounds, offset, heading wrap, scale preservation and missing-channel diagnostics");
  std::puts("PASS: PS2 EvaluateChannel beat normalization, independent full/one facing counts, endpoints, scalar interpolation and explicit invalid inputs");
}
