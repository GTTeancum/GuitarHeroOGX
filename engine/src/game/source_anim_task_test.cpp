#include "game/source_anim_task.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
struct Motion { int id; float origin; float speed; float end = 1000; bool loop = true; };
using Task = ghogx::game::SourceAnimTask<Motion>;
void check(bool condition, const char* label) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
void near(float got, float expected, const char* label) {
  check(std::isfinite(got) && std::fabs(got - expected) < .00001f, label);
}
struct State {
  float value = 0;
  std::vector<int> order;
  std::vector<float> weights;
  bool operator()(const std::shared_ptr<const Motion>& motion, double, float elapsed,
                  float blend, bool) {
    order.push_back(motion->id);
    weights.push_back(blend);
    const float frame = motion->loop ? elapsed : std::min(elapsed, motion->end);
    const float sample = motion->origin + frame * motion->speed;
    value += (sample - value) * blend;
    return !motion->loop && elapsed > motion->end;
  }
  void clear_trace() { order.clear(); weights.clear(); }
};
}  // namespace

int main() {
  State state;
  Task lone({1, 10, 0}, 0, 1);
  lone.poll(0, state); near(state.value, 0, "lone blend starts at current value");
  lone.poll(.25, state); near(state.value, 2.5f, "lone first quarter");
  lone.poll(.5, state); near(state.weights.back(), 1.0f / 3, "incremental source weight");
  near(state.value, 5, "lone halfway remains linear in time");
  lone.poll(.5, state); near(state.weights.back(), 0, "same-time no-outgoing poll has zero weight");
  near(state.value, 5, "same-time repeat does not advance blend");
  lone.poll(1, state); near(state.value, 10, "lone blend completion");

  auto old = std::make_shared<Task>(Motion{2, 1, 2}, 0, 0);
  Task moving({3, 10, 0}, 1, 1, old);
  state = {};
  moving.poll(1, state);
  check(state.order == std::vector<int>({2, 3}), "older task publishes first");
  near(state.value, 3, "zero-weight new task keeps moving outgoing sample");
  state.clear_trace(); moving.poll(1.5, state);
  near(state.weights.back(), .5f, "outgoing task uses absolute blend weight");
  near(state.value, 7, "blend from moving old value 4 to new value 10");
  moving.poll(2, state); near(state.value, 10, "moving handoff completion");
  check(!moving.has_outgoing(), "outgoing reference released on completion");

  auto before_delay = std::make_shared<Task>(Motion{4, 1, 2}, 0, 0);
  Task delayed({5, 20, 0}, 2, 1, before_delay);
  state = {}; delayed.poll(1, state);
  check(state.order == std::vector<int>({4}), "delay does not stop outgoing task");
  near(state.value, 3, "outgoing advances during delay");
  delayed.poll(2, state); near(state.value, 5, "incoming starts with zero blend after delay");
  delayed.poll(2.5, state); near(state.value, 13, "delayed transition uses its own clock");

  auto finite = std::make_shared<Task>(Motion{6, 2, 4, .25f, false}, 0, 0);
  std::weak_ptr<Task> held = finite;
  Task hold({7, 10, 0}, 0, 1, finite);
  finite.reset(); state = {}; hold.poll(.5, state);
  check(!held.expired() && !held.lock()->finished(), "outgoing finite task held past its end");
  near(state.value, 6.5f, "held old task publishes its clamped endpoint");
  hold.poll(1, state); check(held.expired(), "held old task deleted when incoming blend finishes");

  auto a = std::make_shared<Task>(Motion{8, 0, 1}, 0, 0);
  auto b = std::make_shared<Task>(Motion{9, 5, 0}, 0, 2, a);
  std::weak_ptr<Task> weak_a = a, weak_b = b;
  Task c({10, 10, 0}, .25, 1, b);
  a.reset(); b.reset(); state = {}; c.poll(.5, state);
  check(state.order == std::vector<int>({8, 9, 10}), "chained replacement preserves poll order");
  c.poll(1.25, state);
  check(weak_a.expired() && weak_b.expired(), "completion releases entire outgoing ownership chain");

  auto cancel_old = std::make_shared<Task>(Motion{11, 2, 0}, 0, 0);
  Task cancel_new({12, 6, 0}, 0, 1, cancel_old);
  cancel_old->cancel(); state = {}; cancel_new.poll(.25, state);
  check(state.order == std::vector<int>({12}), "canceled outgoing task is never published");
  near(state.value, 1.5f, "canceled predecessor switches to no-outgoing weight branch");
  cancel_new.cancel(); state.clear_trace(); cancel_new.poll(.5, state);
  check(state.order.empty(), "canceled incoming task does not publish");

  auto r0 = std::make_shared<Task>(Motion{13, 2, 1}, 8, 0);
  auto r1 = std::make_shared<Task>(Motion{13, 2, 1}, 8, 0);
  Task original({14, 20, 0}, 12, 1, r0);
  Task rebased({14, 20, 0}, 12, 1, r1);
  rebased.rebase(10);
  State original_state, rebased_state;
  original.poll(12.5, original_state); rebased.poll(2.5, rebased_state);
  near(original_state.value, rebased_state.value, "clock rebase preserves all outgoing elapsed times");
  check(original_state.weights == rebased_state.weights, "clock rebase preserves blend weights");

  Task unrelated({15, -50, 0}, 0, 0);
  State other; unrelated.poll(.5, other);
  near(other.value, -50, "separate target state stays independent");
  near(original_state.value, rebased_state.value, "unrelated target cannot clobber previous state");
  auto scheduled_old = std::make_shared<Task>(Motion{16, 10, 0}, 0, 1);
  Task scheduled_new({17, 20, 0}, 0, 1, scheduled_old);
  state = {}; scheduled_new.poll_once(.5, state);
  near(state.value, 12.5f, "nested half-blended task state");
  state.clear_trace(); scheduled_new.poll_once(.5, state);
  check(state.order.empty(), "runtime repeat cannot republish a task chain");
  near(state.value, 12.5f, "same-clock reentry cannot accumulate nested blend");

  Task new_event({18, 40, 0}, .5, 1, scheduled_old);
  state.clear_trace(); new_event.poll_once(.5, state);
  check(state.order == std::vector<int>({18}), "new event runs without repolling adopted task");
  near(state.value, 12.5f, "same-clock zero-weight event preserves published state");
  new_event.rebase(.5);
  state.clear_trace(); new_event.poll_once(0, state);
  check(state.order.empty(), "rebase preserves scheduled-task identity");
  new_event.poll_once(.25, state);
  check(state.order == std::vector<int>({16, 18}), "rebased tasks resume oldest-first next tick");

  std::puts("PASS: both PS2 weight branches, moving outgoing pose, delay, endpoint holding, chained replacement, cancellation, rebase, target isolation and same-clock scheduling");
}
