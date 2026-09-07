#pragma once

#include "character/char_clip_binding.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghogx::character {

// Original GH2 sample page, before frame expansion, quaternion normalization,
// channel-name aliases or pose retargeting. Disk vectors are 12 bytes; packed
// quaternion/scalar codes must survive until weighted accumulation (168320).
struct Gh2BoneSamplesPage {
  std::vector<Gh2BoneChannel> channels;
  std::size_t sample_count = 0;
  std::uint32_t compression = 0;
  bool interpolate = true; // original CharBonesSamples ctor 1935F0
  std::vector<std::uint8_t> disk_samples;

  std::size_t frame_bytes() const;
  std::size_t checked_frame_bytes() const;
  void validate() const;
};

struct Gh2ClipPoseSamples {
  // Original CharClipSamples members +84, +138, +1EC. Do not merge pages:
  // their independent counts, membership and publication order matter.
  enum Page { Full, One, Delta };
  std::array<Gh2BoneSamplesPage, 3> pages;
};

}  // namespace ghogx::character
