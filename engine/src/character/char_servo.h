#pragma once

#include "milo_scene/milo_scene.h"
#include "character/char_clip.h"

#include <array>
#include <cstddef>
#include <functional>

namespace ghogx::character {

class SourceCharBonesMeshesOutput;

// Snapshot of an actual Waypoint's resolved world transform and GH2 limits.
// Do not substitute world_stored when the waypoint has a live parent.
struct SourceCharServoWaypoint {
  milo_scene::Xfm world;
  float radius = 12.0f;
  float y_radius = 0.0f;
  float angle_radius = 0.0f;
};

struct SourceCharServoRegulationResult {
  bool applied = false;
  bool used_override = false;
  float prediction_end_beat = 0.0f;
  float gain = 0.0f;
  SourceCharUtlClipPredictState prediction;
  std::array<float, 3> position_correction{};
  float angle_correction = 0.0f;
};

struct SourceCharServoRegulation {
  const SourceCharServoWaypoint* waypoint = nullptr;          // servo+E4
  const SourceCharServoWaypoint* override_waypoint = nullptr; // servo+100
  const CharClip* override_clip = nullptr;                   // servo+F0
  float override_beat = 0.0f;                                // servo+F4
  std::array<float, 3> prediction_offset{};                   // servo+110

  // Original Regulate 0x181230. character_delta is Character+23C in its
  // native clock units; it is NOT automatically render dt_seconds.
  // The sampler must implement GH2 EvaluateChannel at the supplied beat,
  // including the clip's own full/one channel rates and interpolation mode.
  // Missing channels must be reported, not silently treated as zero motion.
  SourceCharServoRegulationResult apply(
      milo_scene::Xfm& owner_local, const SourceCharServoTransition& transition,
      float character_delta,
      const std::function<std::optional<SourceCharUtlClipPredictFrame>(
          const CharClip&, float)>& sample = source_gh2_char_clip_facing_sample_at_beat) const;
};

// Persistent state for the original GH2 CharServoBone::Poll (0x180A48).
// GH2 allocation comes from bound CharClipSet::StuffBones inventories, NOT role,
// a currently playing clip, or whether a model happens to have a pelvis.
// The scene adapter owns real PoseMeshes and waypoint regulation callbacks.
struct SourceCharServoRuntime {
  bool has_typed_outputs = false;
  bool has_facing_position_delta = false;
  bool move_self = false;
  bool delta_changed = false;
  std::array<float, 3> facing_position{};
  float facing_rotation = 0;
  std::array<float, 3> delta_position{};
  float delta_rotation = 0;
  // Original GH2 ZeroDeltas also zeros the float range at servo+8C..+98.
  // This is the contiguous GH2 drotx/droty/drotz bucket tail (types 6..8,
  // original suffix table 0x3DA3C0). It is separate from named facing deltas.
  // The scene adapter must supply the actual allocated range, not fake rows.
  float* extra_delta_scalars = nullptr;
  std::size_t extra_delta_scalar_count = 0;

  void zero_deltas();
  void enter(const std::function<void()>& clear_regulate);
  void set_move_self(bool enabled);
  void poll(milo_scene::Xfm& owner, milo_scene::Xfm& pelvis,
            const std::function<void()>& pose_meshes,
            const std::function<void(milo_scene::Xfm&)>& regulate);

  // Typed-buffer adapter: read actual allocated facing channels, publish
  // PoseMeshes, correct the owner's LOCAL transform, then write ZeroDeltas
  // back to that same buffer. No transient per-clip allocation/second pose.
  // Re-resolves pointers each call, so reallocating between frames is safe.
  void enter(SourceCharBonesMeshesOutput& output, const std::function<void()>& clear_regulate);
  void poll(SourceCharBonesMeshesOutput& output, milo_scene::Xfm& owner_local,
            milo_scene::Xfm& pelvis_local,
            const std::function<void(milo_scene::Xfm&)>& regulate,
            const std::function<void(milo_scene::Xfm&)>& dirty = {});
};

}  // namespace ghogx::character
