#include "character/char_servo_character.h"

#include <stdexcept>

namespace ghogx::character {

milo_scene::Xfm* SourceCharServoCharacter::find(std::string_view name) const {
  // Exact ObjectDir names. Suffix substitution is owned by the original
  // CharUtl::FindBoneMesh lookup in SourceCharBonesMeshesOutput, not a fuzzy
  // match against bone names or their parents.
  for (auto& bone : character_->bones)
    if (bone.name == name) return &bone.local;
  for (auto& mesh : character_->meshes)
    if (mesh.name == name) return &mesh.local;
  const auto prop = character_->attached_prop_transform_proxies.find(std::string(name));
  return prop == character_->attached_prop_transform_proxies.end() ? nullptr : &prop->second.local;
}

milo_scene::Xfm* SourceCharServoCharacter::pelvis() const {
  // Reallocate 180D30 calls FindBoneMesh("bone_pelvis", the owner ObjectDir).
  if (auto* transform = find("bone_pelvis.trans")) return transform;
  return find("bone_pelvis.mesh");
}

void SourceCharServoCharacter::rebind(Character& character,
    const std::vector<const Gh2ClipSetBinding*>& inventories) {
  if (!character.root_decoded)
    throw std::runtime_error("CharServoBone owner has no decoded Character root transform");
  character_ = &character;
  output_.reallocate(inventories, [this](std::string_view name) { return find(name); }, &dummy_);
  if (output_.channel("bone_facing_delta.pos") && !pelvis())
    throw std::runtime_error("CharServoBone facing allocation has no source pelvis target");
}

void SourceCharServoCharacter::validate_binding() const {
  if (!character_ || !character_->root_decoded)
    throw std::runtime_error("CharServoBone Character owner is not bound");
  // Detect storage/lookup changes BEFORE dereferencing borrowed output rows.
  // This also catches a newly attached prop replacing a formerly dummy row.
  for (const auto& row : output_.rows()) {
    const auto stem = row.channel.name.substr(0, row.channel.name.find('.'));
    const auto* current = find(stem + ".trans");
    if (!current) current = find(stem + ".mesh");
    if (!current) current = &dummy_;
    if (current != row.local)
      throw std::runtime_error("CharServoBone transform storage changed; rebind before publication");
  }
}

SourceCharBonesMeshesOutput& SourceCharServoCharacter::output() {
  validate_binding();
  return output_;
}

void SourceCharServoCharacter::enter(const std::function<void()>& clear_regulate) {
  validate_binding();
  servo_.enter(output_, clear_regulate);
}

void SourceCharServoCharacter::poll(
    const std::function<void(milo_scene::Xfm&)>& regulate,
    const SourceCharBonesMeshesOutput::DirtyTransform& dirty) {
  validate_binding();
  auto* pelvis_local = pelvis();
  if (output_.channel("bone_facing_delta.pos") && !pelvis_local)
    throw std::runtime_error("CharServoBone pelvis target disappeared");
  // Without facing channels Poll never reads a pelvis (180A80/180CB0).
  // The unused reference below does NOT create a skeleton/output row.
  servo_.poll(output_, character_->root_transform.local,
              pelvis_local ? *pelvis_local : dummy_, regulate, dirty);
}

}  // namespace ghogx::character
