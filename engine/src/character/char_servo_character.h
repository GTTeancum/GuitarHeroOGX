#pragma once

#include "character/char_bones_output.h"
#include "character/char_servo.h"

namespace ghogx::character {

// Runtime ownership bridge for one original CharServoBone. No role/venue
// selection, frame clock, renderer world transform, or controller ordering
// lives here. Drivers feed output(); the Character owner polls this once in
// its authored schedule, before the downstream IK/twist/look-at controllers.
class SourceCharServoCharacter {
 public:
  SourceCharServoCharacter() = default;
  // The output borrows our dummy and Character locals. Moving/copying this
  // object would silently leave those pointers referring to the old object.
  SourceCharServoCharacter(const SourceCharServoCharacter&) = delete;
  SourceCharServoCharacter& operator=(const SourceCharServoCharacter&) = delete;
  SourceCharServoCharacter(SourceCharServoCharacter&&) = delete;
  SourceCharServoCharacter& operator=(SourceCharServoCharacter&&) = delete;

  // All clip-set inventories bound to this servo, not only the active clip.
  // Rebind after replacing/reallocating the Character's transform storage.
  // This is allocation, NOT Enter: preserve lifecycle and move_self state.
  void rebind(Character& character,
              const std::vector<const Gh2ClipSetBinding*>& inventories);
  void enter(const std::function<void()>& clear_regulate);
  void set_move_self(bool enabled) { servo_.set_move_self(enabled); }
  void poll(const std::function<void(milo_scene::Xfm&)>& regulate,
            const SourceCharBonesMeshesOutput::DirtyTransform& dirty = {});
  SourceCharBonesMeshesOutput& output();
  const SourceCharServoRuntime& state() const { return servo_; }
  const milo_scene::Xfm& dummy_local() const { return dummy_; }

 private:
  milo_scene::Xfm* find(std::string_view name) const;
  milo_scene::Xfm* pelvis() const;
  void validate_binding() const;
  Character* character_ = nullptr;
  milo_scene::Xfm dummy_;
  SourceCharBonesMeshesOutput output_;
  SourceCharServoRuntime servo_;
};

}  // namespace ghogx::character
