#pragma once

#include <memory>
#include <optional>
#include <utility>

namespace ghogx::game {

// GH2 PS2 AnimTask::Poll 0x1AC9B8. Each task owns the preceding task for its
// AnimTarget. The source TaskMgr polls that older task before the newer one;
// walking the ownership chain oldest-first preserves the same publication order.
template <class Payload>
class SourceAnimTask {
 public:
  using Ptr = std::shared_ptr<SourceAnimTask>;

  SourceAnimTask(Payload data, double start, float period, Ptr preceding = {})
      : payload(std::make_shared<const Payload>(std::move(data))),
        start_time_(start), blend_period_(period), outgoing_(std::move(preceding)) {
    if (blend_period_ != 0.0f && outgoing_) outgoing_->blending_ = true;
  }

  // publish(payload, taskStart, elapsed, blend, mayFinish) returns whether the
  // source frame bounds have been passed. The payload decides frame/rate units.
  template <class Publish>
  void poll(double now, Publish&& publish) {
    poll_impl(now, publish, false);
  }

  // Runtime events can re-enter the venue updater at the same presentation
  // clock. Schedule a task only once at that clock, including an outgoing task
  // adopted by a new event. Keep raw Poll above available for source-math tests.
  template <class Publish>
  void poll_once(double now, Publish&& publish) {
    poll_impl(now, publish, true);
  }

  void cancel() { finished_ = true; outgoing_.reset(); }
  bool finished() const { return finished_; }
  bool has_outgoing() const { return static_cast<bool>(outgoing_); }
  double start_time() const { return start_time_; }
  void rebase(double elapsed_intro) {
    start_time_ -= elapsed_intro;
    if (last_scheduled_time_) *last_scheduled_time_ -= elapsed_intro;
    if (outgoing_) outgoing_->rebase(elapsed_intro);
  }

  const std::shared_ptr<const Payload> payload;

 private:
  template <class Publish>
  void poll_impl(double now, Publish& publish, bool scheduled_once) {
    if (finished_) return;
    if (scheduled_once) {
      if (last_scheduled_time_ && *last_scheduled_time_ == now) return;
      last_scheduled_time_ = now;
    }
    if (outgoing_) {
      outgoing_->poll_impl(now, publish, scheduled_once);
      if (outgoing_->finished()) outgoing_.reset();
    }
    const float elapsed = static_cast<float>(now - start_time_);
    if (elapsed < 0.0f) return;  // TaskMgr delay: older task keeps running.
    float blend = 1.0f;
    if (blend_period_ != 0.0f) {
      blend = elapsed / blend_period_;
      if (blend >= 1.0f) {
        blend = 1.0f;
        outgoing_.reset();
        blend_period_ = 0.0f;
      } else if (!outgoing_) {
        const float previous = blend_time_;
        blend_time_ = elapsed;
        blend = (elapsed - previous) / (blend_period_ - previous);
      }
    } else {
      outgoing_.reset();
    }
    const bool may_finish = !blending_ && blend_period_ == 0.0f;
    const bool passed_end = publish(payload, start_time_, elapsed, blend, may_finish);
    if (may_finish && passed_end) finished_ = true;
  }

  double start_time_ = 0.0;
  std::optional<double> last_scheduled_time_;
  float blend_time_ = 0.0f;
  float blend_period_ = 0.0f;
  bool blending_ = false;
  bool finished_ = false;
  Ptr outgoing_;
};

}  // namespace ghogx::game
