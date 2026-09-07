#include "character/char_clip_binding.h"

#include "milo.h"
#include "milo_object.h"

#include <array>
#include <stdexcept>
#include <unordered_set>

namespace ghogx::character {
namespace {
constexpr std::array<const char*, 9> suffixes{
    "pos", "scale", "quat", "rotx", "roty", "rotz",
    "drotx", "droty", "drotz"};

bool is_clip(const std::string& type) {
  // CharClipFilter is an editor/config object, NOT a CharClip subclass.
  return type == "CharClip" || type == "CharClipSamples";
}

void append_channel(Gh2ClipSetBinding& out, const std::string& bone, int type) {
  if (type == 9) return;  // GH2 TYPE_END, not RB3's value 6.
  if (type < 0 || type >= 9)
    throw std::runtime_error("GH2 CharBone channel type out of range: " + bone);
  const auto dot = bone.find('.');
  if (dot == std::string::npos || dot == 0)
    throw std::runtime_error("GH2 CharBone requires a transform suffix: " + bone);
  // CharBone::GetBoneName 0x167080 -> CharBones::GetBoneName 0x167970.
  out.channels.push_back({bone.substr(0, dot + 1) + suffixes[type],
                          static_cast<Gh2BoneChannelType>(type)});
}
}  // namespace

Gh2ClipSetBinding decode_gh2_clip_set_binding(const gh::milo::Directory& dir) {
  if (dir.dir_type != "CharClipSet")
    throw std::runtime_error("GH2 binding requires a CharClipSet directory");
  std::unordered_set<std::string> clip_names;
  for (const auto& entry : dir.entries) {
    if (is_clip(entry.type) && !clip_names.insert(entry.name).second)
      throw std::runtime_error("duplicate GH2 clip entry: " + entry.name);
  }
  const auto root = gh::milo_object::parse_char_clip_set14(
      dir.dir_body_bytes, static_cast<uint32_t>(clip_names.size()));
  // The root list is serialized per resident CharClip, not an unrelated
  // guessed count. Match its identity as well as exhausting the body reader.
  for (const auto& clip : root.clips) {
    if (clip_names.erase(clip.clip) != 1)
      throw std::runtime_error("unresolved/duplicate GH2 clip pointer: " + clip.clip);
  }
  if (!clip_names.empty())
    throw std::runtime_error("incomplete GH2 clip pointer list");

  Gh2ClipSetBinding out;
  out.directory_name = dir.dir_name;
  out.revision = root.revision;
  out.move_self = root.move_self;
  out.subdirectories = root.object_directory.subdirectories;
  // SLUS_214.47: CharDriver::SyncBones 0x171080 -> StuffBones 0x16E1B8
  // -> ListBones 0x16E230. All FOUR facing rows precede CharBone iteration.
  if (out.move_self) {
    append_channel(out, "bone_facing.trans", 0);
    append_channel(out, "bone_facing.trans", 5);
    append_channel(out, "bone_facing_delta.trans", 0);
    append_channel(out, "bone_facing_delta.trans", 5);
  }
  for (const auto& entry : dir.entries) {
    if (entry.type != "CharBone") continue;
    const auto bone = gh::milo_object::parse_char_bone2(entry.body_bytes);
    // Original CharBone::ListBones 0x1670A0: bools +C0/+C4, then BOTH
    // channel selectors +C8/+CC. The second is not discardable padding.
    if (bone.position_context) append_channel(out, entry.name, 0);
    if (bone.scale_context) append_channel(out, entry.name, 1);
    append_channel(out, entry.name, bone.rotation);
    append_channel(out, entry.name, bone.legacy_rotation);
  }
  return out;
}
}  // namespace ghogx::character
