#include "character/char_servo.h"
#include "character/char_bones_output.h"

#include "character/char_clip.h"

#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace ghogx::character {
namespace {

// Original GetZAngle 0x2DA1A8 reads row 1, NOT atan2(row0.y,row0.x).
// The distinction matters for tilted, scaled or sheared character bases.
float gh2_heading(const milo_scene::Xfm& transform) {
  return -std::atan2(transform.rot[1][0], transform.rot[1][1]);
}

// RotateAboutZ(Matrix3) 0x2DABD0 preserves scale and does not orbit position.
void gh2_rotate_basis(milo_scene::Xfm& transform, float angle) {
  const float c = std::cos(angle), s = std::sin(angle);
  for (auto& row : transform.rot) {
    const float x = row[0], y = row[1];
    row[0] = x * c - y * s;
    row[1] = x * s + y * c;
  }
}

// GH2 PS2 Multiply(Transform, Transform) 0x2DAF00: row-vector composition.
milo_scene::Xfm compose(const milo_scene::Xfm& local,
                        const milo_scene::Xfm& parent) {
  milo_scene::Xfm result;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      result.rot[row][col] = local.rot[row][0] * parent.rot[0][col] +
          local.rot[row][1] * parent.rot[1][col] +
          local.rot[row][2] * parent.rot[2][col];
    }
    result.pos[row] = local.pos[0] * parent.rot[0][row] +
        local.pos[1] * parent.rot[1][row] +
        local.pos[2] * parent.rot[2][row] + parent.pos[row];
  }
  return result;
}

// Poll calls the orthogonal/scaled inverse at 0x2DAF60, then multiplies
// -pelvis.pos by that basis using VU instructions at 0x180B48/0x180C3C.
// It is NOT an unscaled transpose, and NOT a general sheared-matrix inverse.
milo_scene::Xfm source_inverse(const milo_scene::Xfm& transform) {
  milo_scene::Xfm result;
  for (int row = 0; row < 3; ++row) {
    const float length_squared = transform.rot[row][0] * transform.rot[row][0] +
        transform.rot[row][1] * transform.rot[row][1] +
        transform.rot[row][2] * transform.rot[row][2];
    // Invalid source data must be surfaced, not replaced by an identity pose.
    if (!(length_squared > 0) || !std::isfinite(length_squared))
      throw std::runtime_error("CharServoBone: invalid pelvis basis in movement switch");
    const float reciprocal = 1.0f / length_squared;
    for (int col = 0; col < 3; ++col)
      result.rot[col][row] = transform.rot[row][col] * reciprocal;
  }
  for (int col = 0; col < 3; ++col)
    result.pos[col] = -transform.pos[0] * result.rot[0][col] -
        transform.pos[1] * result.rot[1][col] -
        transform.pos[2] * result.rot[2][col];
  return result;
}

}  // namespace

SourceCharServoRegulationResult SourceCharServoRegulation::apply(
    milo_scene::Xfm& owner, const SourceCharServoTransition& transition,
    float character_delta,
    const std::function<std::optional<SourceCharUtlClipPredictFrame>(
        const CharClip&, float)>& sample) const {
  SourceCharServoRegulationResult result;
  if ((!waypoint && !override_waypoint) || !transition.outgoing_clip ||
      transition.next_ramp_in <= 0.0f) return result;

  float lookahead = transition.next_ramp_in;
  float denominator = std::max(2.0f, lookahead / 1.5f);
  const auto* target = waypoint;
  if (override_waypoint && transition.outgoing_clip == override_clip) {
    lookahead = override_beat - transition.outgoing_beat;
    if (lookahead <= 0.0f) return result;
    denominator = lookahead / 1.5f; // The override has NO two-unit floor.
    target = override_waypoint;
    result.used_override = true;
  }
  if (!target) return result;

  result.prediction_end_beat = transition.outgoing_beat + lookahead;
  const auto first = sample(*transition.outgoing_clip, transition.outgoing_beat);
  const auto second = sample(*transition.outgoing_clip, result.prediction_end_beat);
  if (!first || !second)
    throw std::runtime_error("CharServoBone::Regulate: missing clip facing samples");
  for (int i = 0; i < 3; ++i) result.prediction.pos[i] = owner.pos[i];
  result.prediction.ang = gh2_heading(owner);
  source_char_utl_clip_predict(result.prediction, *first, *second);

  const float c = std::cos(result.prediction.ang);
  const float s = std::sin(result.prediction.ang);
  const std::array<float, 3> point = {
      result.prediction.pos[0] + prediction_offset[0] * c - prediction_offset[1] * s,
      result.prediction.pos[1] + prediction_offset[0] * s + prediction_offset[1] * c,
      result.prediction.pos[2] + prediction_offset[2]};
  // The offset is also used on the ordinary waypoint branch after a
  // mismatched override; do not clear or condition it on used_override.
  const auto delta = source_waypoint_shape_delta_box(
      target->world, point, target->radius, target->y_radius);
  const float angle_delta = source_waypoint_shape_delta_ang(
      gh2_heading(target->world), target->angle_radius, result.prediction.ang);
  result.gain = character_delta / denominator; // Source does not clamp gain.
  for (int i = 0; i < 3; ++i) {
    result.position_correction[i] = delta[i] * result.gain;
    owner.pos[i] += result.position_correction[i];
  }
  result.angle_correction = angle_delta * result.gain;
  gh2_rotate_basis(owner, result.angle_correction);
  result.applied = true;
  return result;
}

void SourceCharServoRuntime::zero_deltas() {
  source_char_servo_bone_zero_deltas(delta_position, delta_rotation);
  if (extra_delta_scalar_count) {
    if (!extra_delta_scalars)
      throw std::runtime_error("CharServoBone: missing allocated scalar delta range");
    std::fill_n(extra_delta_scalars, extra_delta_scalar_count, 0.0f);
  }
}

void SourceCharServoRuntime::enter(const std::function<void()>& clear_regulate) {
  zero_deltas();
  clear_regulate();
  delta_changed = false;
  move_self = has_facing_position_delta;
}

void SourceCharServoRuntime::set_move_self(bool enabled) {
  if (move_self == enabled) return;
  move_self = enabled;
  delta_changed = true;
}

namespace {
void read_output_channels(SourceCharServoRuntime& state, SourceCharBonesMeshesOutput& output) {
  const float* pos = output.channel("bone_facing.pos");
  const float* rot = output.channel("bone_facing.rotz");
  const float* delta_pos = output.channel("bone_facing_delta.pos");
  const float* delta_rot = output.channel("bone_facing_delta.rotz");
  if ((pos || rot || delta_pos || delta_rot) && !(pos && rot && delta_pos && delta_rot))
    throw std::runtime_error("CharServoBone: incomplete allocated GH2 facing channels");
  state.has_typed_outputs = !output.rows().empty();
  state.has_facing_position_delta = delta_pos != nullptr;
  if (pos) {
    std::copy_n(pos, 3, state.facing_position.begin()); state.facing_rotation = *rot;
    std::copy_n(delta_pos, 3, state.delta_position.begin()); state.delta_rotation = *delta_rot;
  }
  state.extra_delta_scalars = output.delta_scalars();
  state.extra_delta_scalar_count = output.delta_scalar_count();
}
void write_output_deltas(const SourceCharServoRuntime& state, SourceCharBonesMeshesOutput& output) {
  if (auto* pos = output.channel("bone_facing_delta.pos")) {
    std::copy(state.delta_position.begin(), state.delta_position.end(), pos);
    *output.channel("bone_facing_delta.rotz") = state.delta_rotation;
  }
}
}  // namespace

void SourceCharServoRuntime::enter(SourceCharBonesMeshesOutput& output,
                                  const std::function<void()>& clear_regulate) {
  read_output_channels(*this, output);
  enter([&] {
    write_output_deltas(*this, output);
    clear_regulate();
  });
}

void SourceCharServoRuntime::poll(SourceCharBonesMeshesOutput& output,
    milo_scene::Xfm& owner_local, milo_scene::Xfm& pelvis_local,
    const std::function<void(milo_scene::Xfm&)>& regulate,
    const std::function<void(milo_scene::Xfm&)>& dirty) {
  read_output_channels(*this, output);
  poll(owner_local, pelvis_local, [&] { output.pose_meshes(dirty); }, regulate);
  write_output_deltas(*this, output);
}

void SourceCharServoRuntime::poll(
    milo_scene::Xfm& owner, milo_scene::Xfm& pelvis,
    const std::function<void()>& pose_meshes,
    const std::function<void(milo_scene::Xfm&)>& regulate) {
  if (!has_typed_outputs) return;  // 0x180A64: even ZeroDeltas is skipped.
  pose_meshes();                  // 0x180A74: before any movement correction.
  if (has_facing_position_delta) {
    if (!move_self) {
      if (delta_changed) {
        // 0x180A9C: finish the outgoing self-motion step before changing the
        // pelvis to absolute facing. Solve a new owner preserving world pose.
        auto moved_owner = owner;
        source_char_servo_bone_move_to_delta_facing(
            moved_owner, delta_position, delta_rotation);
        const auto preserved_world = compose(pelvis, moved_owner);
        source_char_servo_bone_move_to_facing(pelvis, facing_position, facing_rotation);
        owner = compose(source_inverse(pelvis), preserved_world);
      } else {
        source_char_servo_bone_move_to_facing(pelvis, facing_position, facing_rotation);
      }
    } else {
      if (delta_changed) {
        // 0x180BB4: preserve the outgoing absolute-facing pose in the owner.
        // The incoming delta is NOT also applied on this transition frame.
        auto facing_pelvis = pelvis;
        source_char_servo_bone_move_to_facing(
            facing_pelvis, facing_position, facing_rotation);
        const auto preserved_world = compose(facing_pelvis, owner);
        owner = compose(source_inverse(pelvis), preserved_world);
      } else {
        source_char_servo_bone_move_to_delta_facing(owner, delta_position, delta_rotation);
      }
      regulate(owner);  // 0x180C98; caller must implement real waypoint semantics.
    }
    delta_changed = false;
  }
  zero_deltas();
}

}  // namespace ghogx::character
