#pragma once

#include "character/char_clip_binding.h"
#include "character/char_bones_samples.h"
#include "milo_scene/milo_scene.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace ghogx::character {

// Original GH2 CharBonesMeshes allocation + pose publisher, not a sampled
// clip's transient set of channels. The owner supplies live local transforms
// from its typed ObjectDir lookup. All pointers are borrowed: reallocate after
// moving/replacing the character's bone/mesh storage, before the next publish.
class SourceCharBonesMeshesOutput {
 public:
  struct Row {
    Gh2BoneChannel channel;
    std::size_t byte_offset = 0;
    milo_scene::Xfm* local = nullptr;
    bool fallback = false;
  };
  using FindTransform = std::function<milo_scene::Xfm*(std::string_view)>;
  using DirtyTransform = std::function<void(milo_scene::Xfm&)>;

  // Union all bound drivers, in type/name order. Resolve FIRST-dot suffix to
  // .trans, then .mesh (CharUtl::FindBoneMesh 0x184140). Unresolved rows share
  // the supplied servo dummy; never invent a different transform per row.
  // Seed persistent channel storage from current target locals (0x192AA8).
  void reallocate(const std::vector<const Gh2ClipSetBinding*>& inventories,
                  const FindTransform& find, milo_scene::Xfm* fallback);

  // Original 0x192E20: pos, quat, absolute XYZ, delta XYZ, scale LAST.
  // Does not clear channels or facing deltas: the driver/servo owns that.
  void pose_meshes(const DirtyTransform& dirty = {});

  // Original GH2 167FD8/168320: modify only named source channels in this
  // persistent destination. These are NOT a whole-buffer clear or slerp.
  void scale_down(const std::vector<Gh2BoneChannel>& channels, float weight);
  void scale_add(const Gh2BoneSamplesPage& page, std::size_t sample, float weight);
  void scale_add_at_phase(const Gh2BoneSamplesPage& page, float phase, float weight);
  void scale_add_delta(const Gh2BoneSamplesPage& page, float previous_phase,
                       float current_phase, float weight);
  void scale_down_clip(const Gh2ClipPoseSamples& clip, float weight);
  void scale_add_clip(const Gh2ClipPoseSamples& clip, float previous_phase,
                      float current_phase, float weight);

  const std::vector<Row>& rows() const { return rows_; }
  const std::array<std::size_t, 10>& counts() const { return counts_; }
  const std::array<std::size_t, 10>& offsets() const { return offsets_; }
  std::size_t allocated_bytes() const { return allocated_bytes_; }
  std::size_t normalization_cursor() const { return normalization_cursor_; }
  float* data() { return data_.get(); }
  const float* data() const { return data_.get(); }
  float* channel(std::string_view name);
  const float* channel(std::string_view name) const;
  float* delta_scalars();
  std::size_t delta_scalar_count() const { return counts_[9] - counts_[6]; }

 private:
  float* require_channel(const Gh2BoneChannel& channel);
  struct AlignedDelete { void operator()(float*) const noexcept; };
  std::unique_ptr<float[], AlignedDelete> data_;
  std::vector<Row> rows_;
  std::array<std::size_t, 10> counts_{};
  std::array<std::size_t, 10> offsets_{};
  std::size_t allocated_bytes_ = 0;
  // Constructor initializes this, Reallocate does not reset it.
  std::size_t normalization_cursor_ = 0;
};

}  // namespace ghogx::character
