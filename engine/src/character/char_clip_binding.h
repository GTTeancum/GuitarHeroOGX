#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gh::milo { struct Directory; }

namespace ghogx::character {

// Original PS2 GH2 CharBones suffix table at 0x3DA3C0. This is NOT the
// later six-type RB3 enum: GH2 has three additional delta-rotation buckets.
enum class Gh2BoneChannelType {
  Pos, Scale, Quat, RotX, RotY, RotZ, DeltaRotX, DeltaRotY, DeltaRotZ, End
};

struct Gh2BoneChannel {
  std::string name;
  Gh2BoneChannelType type = Gh2BoneChannelType::End;
};

// Inventory of a loaded clip DIRECTORY, independent of the selected clip.
// Multiple drivers can contribute inventories to the same servo. This is
// not yet the resolved CharBonesMeshes output table or its live pose buffer.
struct Gh2ClipSetBinding {
  std::string directory_name;
  uint32_t revision = 0;
  bool move_self = false;
  std::vector<Gh2BoneChannel> channels;
  // Retained metadata, NOT recursively added to this inventory. Original
  // ListBones 0x16E440 sets ObjDirItr.mDir=0 (recurse=false); its child branch
  // at 0x16E4C8 is therefore skipped. Other drivers bind their own clip sets.
  std::vector<std::string> subdirectories;
};

// Strict GH2 profile: CharClipSet14/ObjectDir16 and CharBone2. Fails on
// unsupported/malformed data rather than fabricating an allocation.
Gh2ClipSetBinding decode_gh2_clip_set_binding(const gh::milo::Directory& dir);

}  // namespace ghogx::character
