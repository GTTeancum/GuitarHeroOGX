// engine/src/character/char_mesh.cpp — see char_mesh.h for the byte layouts.

#include "character/char_mesh.h"

#include "ark_v3.h"
#include "milo.h"
#include "milo_object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ghogx::character {

namespace {

std::vector<uint8_t> read_binary_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("cannot open MILO " + path);
  const std::streamoff size = input.tellg();
  if (size < 0) throw std::runtime_error("cannot size MILO " + path);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  input.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!input) throw std::runtime_error("cannot read MILO " + path);
  return bytes;
}

ghogx::milo_scene::Xfm xfm_from_serialized_4x3(
    const std::array<float, 12>& values) {
  ghogx::milo_scene::Xfm out;
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) {
      out.rot[row][column] = values[row * 3 + column];
    }
  }
  out.pos[0] = values[9];
  out.pos[1] = values[10];
  out.pos[2] = values[11];
  return out;
}

void decode_character_root(Character& out) {
  out.root_decoded = false;
  out.root_decode_error.clear();
  out.root_object_type.clear();
  out.root_lods.clear();
  out.root_shadow.clear();
  out.root_self_shadow = false;
  out.root_sphere_base.clear();
  out.root_environment.clear();
  out.root_transform = {};
  out.root_transform.name = out.dir_name;
  if (out.dir_entry_bytes.empty()) {
    out.root_decode_error = "empty Character directory root";
    return;
  }

  try {
    gh::milo_object::Character9 root;
    if (out.dir_type == "BandCharacter") {
      root = gh::milo_object::parse_band_character1(out.dir_entry_bytes).character;
    } else if (out.dir_type == "Character") {
      root = gh::milo_object::parse_character9(out.dir_entry_bytes);
    } else {
      out.root_decode_error =
          "unsupported character directory type: " + out.dir_type;
      return;
    }
    out.root_object_type =
        root.render_directory.object_directory.object_fields.type;
    out.root_lods.reserve(root.lods.size());
    for (const auto& lod : root.lods) {
      out.root_lods.push_back({lod.screen_size, lod.group});
    }
    out.root_shadow = root.shadow;
    out.root_self_shadow = root.self_shadow;
    out.root_sphere_base = root.sphere_base;
    out.root_environment = root.render_directory.environment;
    const auto& transform = root.render_directory.transformable;
    out.root_transform.local = xfm_from_serialized_4x3(transform.local);
    out.root_transform.world_stored = xfm_from_serialized_4x3(transform.world);
    out.root_transform.constraint = transform.constraint;
    out.root_transform.target = transform.target;
    out.root_transform.preserve_scale = transform.preserve_scale;
    out.root_transform.parent = transform.parent;
    out.root_decoded = true;
  } catch (const std::exception& ex) {
    out.root_decode_error = ex.what();
  }
}

}  // namespace

OutfitLoader decode_outfit_loader(
    const std::string& entry_name,
    const std::vector<uint8_t>& body) {
  OutfitLoader out;
  out.name = entry_name;
  try {
    const auto source = gh::milo_object::parse_outfit_loader1(body);
    out.revision = static_cast<int32_t>(source.revision);
    out.object_type = source.object_fields.type;
    out.directory = source.directory;
    out.categories.reserve(source.categories.size());
    for (const auto& source_category : source.categories) {
      OutfitLoaderCategory category;
      category.selected = source_category.selected;
      category.shown = source_category.shown;
      category.outfits.reserve(source_category.outfits.size());
      for (const auto& source_outfit : source_category.outfits) {
        category.outfits.push_back(
            {source_outfit.hide, source_outfit.desire,
             source_outfit.exclude});
      }
      out.categories.push_back(std::move(category));
    }
    out.decoded = true;
  } catch (const std::exception& ex) {
    out.error = ex.what();
  }
  return out;
}

CharWalk decode_char_walk(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  CharWalk out;
  out.name = entry_name;
  try {
    const auto source = gh::milo_object::parse_char_walk1(body);
    out.revision = static_cast<int32_t>(source.revision);
    out.object_type = source.object_fields.type;
    out.decoded = true;
  } catch (const std::exception& ex) {
    out.error = ex.what();
  }
  return out;
}

WorldFx decode_world_fx(const std::string& entry_name,
                        const std::vector<uint8_t>& body) {
  WorldFx out;
  out.name = entry_name;
  try {
    const auto source = gh::milo_object::parse_world_fx1(body);
    out.revision = static_cast<int32_t>(source.revision);
    const auto& directory = source.render_directory;
    const auto& object_directory = directory.object_directory;
    const auto& transformable = directory.transformable;
    out.render_directory_revision =
        static_cast<int32_t>(directory.revision);
    out.object_directory_revision =
        static_cast<int32_t>(object_directory.revision);
    out.object_type = object_directory.object_fields.type;
    out.proxy_path = object_directory.proxy_path;
    out.subdirectories = object_directory.subdirectories;
    out.environment = directory.environment;
    out.test_event = directory.test_event;
    out.legacy_symbol_1 = directory.legacy_symbol_1;
    out.legacy_symbol_2 = directory.legacy_symbol_2;
    out.parent = transformable.parent;
    out.local = xfm_from_serialized_4x3(transformable.local);
    out.world = xfm_from_serialized_4x3(transformable.world);
    out.constraint = static_cast<int32_t>(transformable.constraint);
    out.target = transformable.target;
    out.preserve_scale = transformable.preserve_scale;
    out.showing = directory.drawable.showing;
    out.draw_order = directory.drawable.draw_order;
    out.frame = directory.animatable.frame;
    out.anim_rate = directory.animatable.rate;
    out.decoded = true;
  } catch (const std::exception& ex) {
    out.error = ex.what();
  }
  return out;
}

std::optional<std::string> source_character_active_lod_view(
    const Character& character, int min_lod) {
  if (!character.root_decoded || character.root_lods.empty()) {
    return std::nullopt;
  }
  const size_t requested =
      min_lod <= 0 ? 0u : static_cast<size_t>(min_lod);
  const size_t index =
      std::min(requested, character.root_lods.size() - 1u);
  const std::string& group_name = character.root_lods[index].group;
  if (group_name.empty()) return std::nullopt;
  const bool resident =
      std::any_of(character.groups.begin(), character.groups.end(),
                  [&](const milo_scene::GroupObj& group) {
                    return group.name == group_name;
                  });
  return resident ? std::optional<std::string>(group_name) : std::nullopt;
}

SourceCharacterDrawClosure source_character_draw_closure(
    const Character& character, int min_lod) {
  SourceCharacterDrawClosure result;
  const auto active =
      source_character_active_lod_view(character, min_lod);
  if (!active) return result;

  std::unordered_map<std::string, const milo_scene::GroupObj*> groups;
  groups.reserve(character.groups.size());
  for (const auto& group : character.groups) {
    groups.emplace(group.name, &group);
  }
  std::unordered_set<std::string> resident_meshes;
  resident_meshes.reserve(character.meshes.size());
  for (const auto& mesh : character.meshes) {
    resident_meshes.insert(mesh.name);
  }
  std::unordered_set<std::string> lod_groups;
  lod_groups.reserve(character.root_lods.size());
  for (const auto& lod : character.root_lods) {
    if (!lod.group.empty()) lod_groups.insert(lod.group);
  }

  // Every decoded ancestor of the active LOD is an authored draw container.
  // Walk to the outermost ancestors so their direct accessory/prop children
  // remain part of the closure without admitting unrelated ungrouped objects.
  std::unordered_set<std::string> ancestors{*active};
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& group : character.groups) {
      const bool owns_ancestor =
          std::any_of(
              group.children.begin(), group.children.end(),
              [&](const std::string& child) {
                return ancestors.find(child) != ancestors.end();
              });
      if (owns_ancestor && ancestors.insert(group.name).second) {
        changed = true;
      }
    }
  }

  std::unordered_set<std::string> roots = ancestors;
  for (const auto& group_name : ancestors) {
    const auto found = groups.find(group_name);
    if (found == groups.end()) continue;
    for (const auto& child : found->second->children) {
      if (ancestors.find(child) != ancestors.end()) {
        roots.erase(child);
      }
    }
  }
  if (roots.empty()) roots.insert(*active);

  std::unordered_set<std::string> visited;
  std::function<void(const std::string&)> collect =
      [&](const std::string& group_name) {
        if (!visited.insert(group_name).second) return;
        const auto found = groups.find(group_name);
        if (found == groups.end()) return;
        for (const auto& child : found->second->children) {
          const auto child_group = groups.find(child);
          if (child_group != groups.end()) {
            if (lod_groups.find(child) != lod_groups.end() &&
                child != *active) {
              continue;
            }
            collect(child);
          } else if (resident_meshes.find(child) !=
                     resident_meshes.end()) {
            result.meshes.insert(child);
          }
        }
      };
  for (const auto& root : roots) collect(root);
  result.authoritative = true;
  return result;
}

namespace {

using milo_scene::Xfm;

const std::vector<std::string>& source_rb3_skeleton_bone_names() {
  // Direct mirror of glTFMilo Source/Shared/BoneNames.cs rb3SkeletonBones.
  static const std::vector<std::string> names = {
      "bone_L-ankle.mesh",
      "bone_L-breast.mesh",
      "bone_L-brow1.mesh",
      "bone_L-brow2.mesh",
      "bone_L-brow2-eyeshape.mesh",
      "bone_L-brow3.mesh",
      "bone_L-brow3-eyeshape.mesh",
      "bone_L-butt.mesh",
      "bone_L-cheek.mesh",
      "bone_L-cheek2.mesh",
      "bone_L-clavicle.mesh",
      "bone_L-collar.mesh",
      "bone_L-collar-base.mesh",
      "bone_L-crease.mesh",
      "bone_L-cymbal.mesh",
      "bone_L-cymbal_shake.mesh",
      "bone_L-cymbal_stand.mesh",
      "bone_L-deltoid.mesh",
      "bone_L-deltoid-base.mesh",
      "bone_L-deltoid-target.mesh",
      "bone_L-eye.mesh",
      "bone_L-eye-base.mesh",
      "bone_L-eyelid-low.mesh",
      "bone_L-eyelid-low-blink.mesh",
      "bone_L-foreArm.mesh",
      "bone_L-foreTwist1.mesh",
      "bone_L-foreTwist2.mesh",
      "bone_L-hand.mesh",
      "bone_L-hand_fret.mesh",
      "bone_L-hand_hip.mesh",
      "bone_L-hand_keys.mesh",
      "bone_L-hand_mic.mesh",
      "bone_L-hand_mic_stand.mesh",
      "bone_L-hand_pegs.mesh",
      "bone_L-index01.mesh",
      "bone_L-index02.mesh",
      "bone_L-index03.mesh",
      "bone_L-knee.mesh",
      "bone_L-lid.mesh",
      "bone_L-lid-blink.mesh",
      "bone_L-lipcorner.mesh",
      "bone_L-middlefinger01.mesh",
      "bone_L-middlefinger02.mesh",
      "bone_L-middlefinger03.mesh",
      "bone_L-nose.mesh",
      "bone_L-pinky-base.mesh",
      "bone_L-pinky01.mesh",
      "bone_L-pinky02.mesh",
      "bone_L-pinky03.mesh",
      "bone_L-ringfinger-base.mesh",
      "bone_L-ringfinger01.mesh",
      "bone_L-ringfinger02.mesh",
      "bone_L-ringfinger03.mesh",
      "bone_L-shoulderTwist1.mesh",
      "bone_L-shoulderTwist2.mesh",
      "bone_L-shoulderTwist3.mesh",
      "bone_L-shoulderTwist4.mesh",
      "bone_L-shoulderTwist5.mesh",
      "bone_L-stick.mesh",
      "bone_L-thigh.mesh",
      "bone_L-thumb01.mesh",
      "bone_L-thumb02.mesh",
      "bone_L-thumb03.mesh",
      "bone_L-tip.mesh",
      "bone_L-tip_L-cymbal.mesh",
      "bone_L-tip_L-tom.mesh",
      "bone_L-tip_R-cymbal.mesh",
      "bone_L-tip_R-tom.mesh",
      "bone_L-tip_floor_tom.mesh",
      "bone_L-tip_hihat.mesh",
      "bone_L-tip_keys.mesh",
      "bone_L-tip_snare.mesh",
      "bone_L-toe.mesh",
      "bone_L-toe_hihat.mesh",
      "bone_L-tom_shake.mesh",
      "bone_L-upperArm.mesh",
      "bone_L-upperTwist1.mesh",
      "bone_L-upperTwist2.mesh",
      "bone_R-ankle.mesh",
      "bone_R-breast.mesh",
      "bone_R-brow1.mesh",
      "bone_R-brow2.mesh",
      "bone_R-brow2-eyeshape.mesh",
      "bone_R-brow3.mesh",
      "bone_R-brow3-eyeshape.mesh",
      "bone_R-butt.mesh",
      "bone_R-cheek.mesh",
      "bone_R-cheek2.mesh",
      "bone_R-clavicle.mesh",
      "bone_R-collar.mesh",
      "bone_R-collar-base.mesh",
      "bone_R-crease.mesh",
      "bone_R-cymbal.mesh",
      "bone_R-cymbal_shake.mesh",
      "bone_R-cymbal_stand.mesh",
      "bone_R-deltoid.mesh",
      "bone_R-deltoid-base.mesh",
      "bone_R-deltoid-target.mesh",
      "bone_R-eye.mesh",
      "bone_R-eye-base.mesh",
      "bone_R-eyelid-low.mesh",
      "bone_R-eyelid-low-blink.mesh",
      "bone_R-foreArm.mesh",
      "bone_R-foreTwist1.mesh",
      "bone_R-foreTwist2.mesh",
      "bone_R-hand.mesh",
      "bone_R-hand_fret.mesh",
      "bone_R-hand_hip.mesh",
      "bone_R-hand_keys.mesh",
      "bone_R-hand_mic.mesh",
      "bone_R-hand_mic_stand.mesh",
      "bone_R-hand_pegs.mesh",
      "bone_R-index01.mesh",
      "bone_R-index02.mesh",
      "bone_R-index03.mesh",
      "bone_R-knee.mesh",
      "bone_R-lid.mesh",
      "bone_R-lid-blink.mesh",
      "bone_R-lipcorner.mesh",
      "bone_R-middlefinger01.mesh",
      "bone_R-middlefinger02.mesh",
      "bone_R-middlefinger03.mesh",
      "bone_R-nose.mesh",
      "bone_R-pinky-base.mesh",
      "bone_R-pinky01.mesh",
      "bone_R-pinky02.mesh",
      "bone_R-pinky03.mesh",
      "bone_R-ringfinger-base.mesh",
      "bone_R-ringfinger01.mesh",
      "bone_R-ringfinger02.mesh",
      "bone_R-ringfinger03.mesh",
      "bone_R-shoulderTwist1.mesh",
      "bone_R-shoulderTwist2.mesh",
      "bone_R-shoulderTwist3.mesh",
      "bone_R-shoulderTwist4.mesh",
      "bone_R-shoulderTwist5.mesh",
      "bone_R-stick.mesh",
      "bone_R-thigh.mesh",
      "bone_R-thumb01.mesh",
      "bone_R-thumb02.mesh",
      "bone_R-thumb03.mesh",
      "bone_R-tip.mesh",
      "bone_R-tip_L-cymbal.mesh",
      "bone_R-tip_L-tom.mesh",
      "bone_R-tip_R-cymbal.mesh",
      "bone_R-tip_R-tom.mesh",
      "bone_R-tip_cowbell.mesh",
      "bone_R-tip_floor_tom.mesh",
      "bone_R-tip_hihat.mesh",
      "bone_R-tip_keys.mesh",
      "bone_R-tip_ride.mesh",
      "bone_R-tip_snare.mesh",
      "bone_R-toe.mesh",
      "bone_R-toe_kick.mesh",
      "bone_R-tom_shake.mesh",
      "bone_R-upperArm.mesh",
      "bone_R-upperTwist1.mesh",
      "bone_R-upperTwist2.mesh",
      "bone_bend_string01.mesh",
      "bone_bend_string02.mesh",
      "bone_bend_string03.mesh",
      "bone_bend_string04.mesh",
      "bone_bend_string05.mesh",
      "bone_bend_string06.mesh",
      "bone_bridge.mesh",
      "bone_brow-low.mesh",
      "bone_brow-mid.mesh",
      "bone_chin.mesh",
      "bone_cowbell_shake.mesh",
      "bone_deform_drumkit.mesh",
      "bone_deform_seat.mesh",
      "bone_deform_seatbase.mesh",
      "bone_drumbase.mesh",
      "bone_earrings_L.mesh",
      "bone_earrings_R.mesh",
      "bone_eyes.mesh",
      "bone_floortom_shake.mesh",
      "bone_floortombase.mesh",
      "bone_forehead.mesh",
      "bone_glasses.mesh",
      "bone_glasses_L.mesh",
      "bone_glasses_R.mesh",
      "bone_guitar.mesh",
      "bone_guitar_lh_mod.mesh",
      "bone_guitar_rh_mod.mesh",
      "bone_hair.mesh",
      "bone_head.mesh",
      "bone_head_nod.mesh",
      "bone_hihat_bottom_rot.mesh",
      "bone_hihat_pedal.mesh",
      "bone_hihat_pos.mesh",
      "bone_hihat_rot.mesh",
      "bone_jaw.mesh",
      "bone_jaw_base.mesh",
      "bone_key_a1.mesh",
      "bone_key_a2.mesh",
      "bone_key_a3.mesh",
      "bone_key_asharp1.mesh",
      "bone_key_asharp2.mesh",
      "bone_key_asharp3.mesh",
      "bone_key_b1.mesh",
      "bone_key_b2.mesh",
      "bone_key_b3.mesh",
      "bone_key_c1.mesh",
      "bone_key_c2.mesh",
      "bone_key_c3.mesh",
      "bone_key_c4.mesh",
      "bone_key_csharp1.mesh",
      "bone_key_csharp2.mesh",
      "bone_key_csharp3.mesh",
      "bone_key_d1.mesh",
      "bone_key_d2.mesh",
      "bone_key_d3.mesh",
      "bone_key_dsharp1.mesh",
      "bone_key_dsharp2.mesh",
      "bone_key_dsharp3.mesh",
      "bone_key_e1.mesh",
      "bone_key_e2.mesh",
      "bone_key_e3.mesh",
      "bone_key_f1.mesh",
      "bone_key_f2.mesh",
      "bone_key_f3.mesh",
      "bone_key_fsharp1.mesh",
      "bone_key_fsharp2.mesh",
      "bone_key_fsharp3.mesh",
      "bone_key_g1.mesh",
      "bone_key_g2.mesh",
      "bone_key_g3.mesh",
      "bone_key_gsharp1.mesh",
      "bone_key_gsharp2.mesh",
      "bone_key_gsharp3.mesh",
      "bone_keyboard_base.mesh",
      "bone_keyboard_height.mesh",
      "bone_keyboard_shake.mesh",
      "bone_keys_lh.mesh",
      "bone_keys_rh.mesh",
      "bone_kick.mesh",
      "bone_kick-face.mesh",
      "bone_kick_head_center.mesh",
      "bone_kick_head_middle.mesh",
      "bone_kick_head_outer.mesh",
      "bone_kickanim.mesh",
      "bone_liptop_left.mesh",
      "bone_liptop_mid.mesh",
      "bone_liptop_right.mesh",
      "bone_lowlip_left.mesh",
      "bone_lowlip_mid.mesh",
      "bone_lowlip_right.mesh",
      "bone_lowerteeth-base.mesh",
      "bone_maskstrap.mesh",
      "bone_mic.mesh",
      "bone_mic_L-hip.mesh",
      "bone_mic_R-hip.mesh",
      "bone_mic_mic_stand.mesh",
      "bone_mic_mouth.mesh",
      "bone_mic_stand_bottom.mesh",
      "bone_mic_stand_mouth.mesh",
      "bone_mic_stand_top.mesh",
      "bone_neck.mesh",
      "bone_neckTwist.mesh",
      "bone_nose.mesh",
      "bone_nut.mesh",
      "bone_paddle1.mesh",
      "bone_paddle2.mesh",
      "bone_pelvis.mesh",
      "bone_pick.mesh",
      "bone_pick_strum.mesh",
      "bone_pos_string01.mesh",
      "bone_pos_string02.mesh",
      "bone_pos_string03.mesh",
      "bone_pos_string04.mesh",
      "bone_pos_string05.mesh",
      "bone_pos_string06.mesh",
      "bone_prop0.mesh",
      "bone_prop1.mesh",
      "bone_prop2.mesh",
      "bone_prop3.mesh",
      "bone_ride.mesh",
      "bone_ride_shake.mesh",
      "bone_ride_stand.mesh",
      "bone_snare_shake.mesh",
      "bone_snarebase_rot.mesh",
      "bone_spine1.mesh",
      "bone_spine2.mesh",
      "bone_spine3.mesh",
      "bone_target_L-cymbal.mesh",
      "bone_target_L-hand.mesh",
      "bone_target_L-hip.mesh",
      "bone_target_L-tom.mesh",
      "bone_target_R-cowbell.mesh",
      "bone_target_R-cymbal.mesh",
      "bone_target_R-hand.mesh",
      "bone_target_R-hip.mesh",
      "bone_target_R-tom.mesh",
      "bone_target_belly.mesh",
      "bone_target_floor_tom.mesh",
      "bone_target_fret.mesh",
      "bone_target_hihat.mesh",
      "bone_target_hihat_pedal.mesh",
      "bone_target_keyboard_lh.mesh",
      "bone_target_keyboard_rh.mesh",
      "bone_target_kick_pedal.mesh",
      "bone_target_mouth.mesh",
      "bone_target_pegs.mesh",
      "bone_target_ride.mesh",
      "bone_target_snare.mesh",
      "bone_target_strum.mesh",
      "bone_tip.mesh",
      "bone_tip_fret.mesh",
      "bone_tongue1.mesh",
      "bone_tongue2.mesh",
      "bone_tongue3.mesh",
      "bone_tongue4.mesh",
      "bone_upperteeth-base.mesh",
      "bone_vibrate_hi.mesh",
      "bone_vibrate_low.mesh",
      "exo_L-ankle.mesh",
      "exo_L-boot-size.mesh",
      "exo_L-breast.mesh",
      "exo_L-brow1.mesh",
      "exo_L-brow2.mesh",
      "exo_L-brow3.mesh",
      "exo_L-butt.mesh",
      "exo_L-calf.mesh",
      "exo_L-clavicle.mesh",
      "exo_L-collar.mesh",
      "exo_L-deltoid.mesh",
      "exo_L-foreTwist1.mesh",
      "exo_L-foreTwist2.mesh",
      "exo_L-forearm.mesh",
      "exo_L-hand.mesh",
      "exo_L-index01.mesh",
      "exo_L-index02.mesh",
      "exo_L-index03.mesh",
      "exo_L-knee.mesh",
      "exo_L-middlefinger01.mesh",
      "exo_L-middlefinger02.mesh",
      "exo_L-middlefinger03.mesh",
      "exo_L-pinky-base.mesh",
      "exo_L-pinky01.mesh",
      "exo_L-pinky02.mesh",
      "exo_L-pinky03.mesh",
      "exo_L-ringfinger-base.mesh",
      "exo_L-ringfinger01.mesh",
      "exo_L-ringfinger02.mesh",
      "exo_L-ringfinger03.mesh",
      "exo_L-shoulder.mesh",
      "exo_L-shoulderTwist1.mesh",
      "exo_L-shoulderTwist2.mesh",
      "exo_L-shoulderTwist3.mesh",
      "exo_L-shoulderTwist4.mesh",
      "exo_L-shoulderTwist5.mesh",
      "exo_L-thigh.mesh",
      "exo_L-thigh_muscle.mesh",
      "exo_L-thumb01.mesh",
      "exo_L-thumb02.mesh",
      "exo_L-thumb03.mesh",
      "exo_L-toe.mesh",
      "exo_L-upperArm.mesh",
      "exo_L-upperTwist1.mesh",
      "exo_L-upperTwist2.mesh",
      "exo_R-ankle.mesh",
      "exo_R-boot-size.mesh",
      "exo_R-breast.mesh",
      "exo_R-brow1.mesh",
      "exo_R-brow2.mesh",
      "exo_R-brow3.mesh",
      "exo_R-butt.mesh",
      "exo_R-calf.mesh",
      "exo_R-clavicle.mesh",
      "exo_R-collar.mesh",
      "exo_R-deltoid.mesh",
      "exo_R-foreTwist1.mesh",
      "exo_R-foreTwist2.mesh",
      "exo_R-forearm.mesh",
      "exo_R-hand.mesh",
      "exo_R-index01.mesh",
      "exo_R-index02.mesh",
      "exo_R-index03.mesh",
      "exo_R-knee.mesh",
      "exo_R-middlefinger01.mesh",
      "exo_R-middlefinger02.mesh",
      "exo_R-middlefinger03.mesh",
      "exo_R-pinky-base.mesh",
      "exo_R-pinky01.mesh",
      "exo_R-pinky02.mesh",
      "exo_R-pinky03.mesh",
      "exo_R-ringfinger-base.mesh",
      "exo_R-ringfinger01.mesh",
      "exo_R-ringfinger02.mesh",
      "exo_R-ringfinger03.mesh",
      "exo_R-shoulder.mesh",
      "exo_R-shoulderTwist1.mesh",
      "exo_R-shoulderTwist2.mesh",
      "exo_R-shoulderTwist3.mesh",
      "exo_R-shoulderTwist4.mesh",
      "exo_R-shoulderTwist5.mesh",
      "exo_R-thigh.mesh",
      "exo_R-thigh_muscle.mesh",
      "exo_R-thumb01.mesh",
      "exo_R-thumb02.mesh",
      "exo_R-thumb03.mesh",
      "exo_R-toe.mesh",
      "exo_R-upperArm.mesh",
      "exo_R-upperTwist1.mesh",
      "exo_R-upperTwist2.mesh",
      "exo_armpit.mesh",
      "exo_butt.mesh",
      "exo_chest-L1.mesh",
      "exo_chest-L2.mesh",
      "exo_chest-R1.mesh",
      "exo_chest-R2.mesh",
      "exo_chest-mid.mesh",
      "exo_head.mesh",
      "exo_lowerbelly-L.mesh",
      "exo_lowerbelly-R.mesh",
      "exo_lowerbelly-mid.mesh",
      "exo_lowerchest-L.mesh",
      "exo_lowerchest-R.mesh",
      "exo_lowerchest-mid.mesh",
      "exo_midbelly-L1.mesh",
      "exo_midbelly-L2.mesh",
      "exo_midbelly-L3.mesh",
      "exo_midbelly-R1.mesh",
      "exo_midbelly-R2.mesh",
      "exo_midbelly-R3.mesh",
      "exo_midbelly-mid.mesh",
      "exo_neck.mesh",
      "exo_neck-front.mesh",
      "exo_neckTwist.mesh",
      "exo_neckbase.mesh",
      "exo_pelvis.mesh",
      "exo_spine1.mesh",
      "exo_spine2.mesh",
      "exo_spine3.mesh",
      "exo_stomach.mesh",
      "exo_upperbelly-L.mesh",
      "exo_upperbelly-L1.mesh",
      "exo_upperbelly-L2.mesh",
      "exo_upperbelly-R.mesh",
      "exo_upperbelly-R1.mesh",
      "exo_upperbelly-R2.mesh",
      "exo_upperbelly-mid.mesh",
      "spot_L-index_tip.mesh",
      "spot_L-middlefinger_tip.mesh",
      "spot_L-pinky_tip.mesh",
      "spot_L-ringfinger_tip.mesh",
      "spot_L-thumb_tip.mesh",
      "spot_R-index_tip.mesh",
      "spot_R-middlefinger_tip.mesh",
      "spot_R-pinky_tip.mesh",
      "spot_R-ringfinger_tip.mesh",
      "spot_R-thumb_tip.mesh",
      "spot_bridge01.mesh",
      "spot_bridge02.mesh",
      "spot_bridge03.mesh",
      "spot_bridge04.mesh",
      "spot_bridge05.mesh",
      "spot_bridge06.mesh",
      "spot_keyboard_grab.mesh",
      "spot_keys_left.mesh",
      "spot_keys_left_lh.mesh",
      "spot_keys_right.mesh",
      "spot_keys_right_lh.mesh",
      "spot_navel.mesh",
      "spot_neck.mesh",
      "spot_neck_fret01.mesh",
      "spot_neck_fret02.mesh",
      "spot_neck_fret03.mesh",
      "spot_neck_fret04.mesh",
      "spot_neck_fret05.mesh",
      "spot_neck_fret06.mesh",
      "spot_neck_fret07.mesh",
      "spot_neck_fret08.mesh",
      "spot_neck_fret09.mesh",
      "spot_neck_fret10.mesh",
      "spot_neck_fret11.mesh",
      "spot_neck_fret12.mesh",
      "spot_neck_fret13.mesh",
      "spot_neck_fret14.mesh",
      "spot_neck_fret15.mesh",
      "spot_neck_fret16.mesh",
      "spot_neck_fret17.mesh",
      "spot_neck_fret18.mesh",
      "spot_neck_fret19.mesh",
      "spot_neck_fret20.mesh",
      "spot_nut01.mesh",
      "spot_nut02.mesh",
      "spot_nut03.mesh",
      "spot_nut04.mesh",
      "spot_nut05.mesh",
      "spot_nut06.mesh",
  };
  return names;
}

// Bounds-checked little-endian cursor (same shape as milo_scene's Reader).
struct Reader {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  Reader(const uint8_t* data, size_t len) : p(data), n(len) {}
  void need(size_t k) const {
    if (pos + k > n) throw std::runtime_error("char_mesh: read past end");
  }
  void skip(size_t k) { need(k); pos += k; }
  uint8_t u8() { need(1); return p[pos++]; }
  uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, p + pos, 2); pos += 2; return v; }
  uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  float f32() { need(4); float v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
  std::string str() {
    uint32_t len = u32();
    if (len > n - pos || len > (1u << 20))
      throw std::runtime_error("char_mesh: implausible string length");
    std::string s(reinterpret_cast<const char*>(p + pos), len);
    pos += len;
    return s;
  }
  std::string utf8_z() {
    const size_t start = pos;
    while (pos < n && p[pos] != 0) ++pos;
    if (pos >= n) {
      throw std::runtime_error("char_mesh: unterminated UTF-8 string");
    }
    if (pos - start > (1u << 20)) {
      throw std::runtime_error("char_mesh: implausible UTF-8 string length");
    }
    std::string s(reinterpret_cast<const char*>(p + start), pos - start);
    ++pos;
    return s;
  }
  Xfm matrix() {
    Xfm m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) m.rot[r][c] = f32();
    for (int c = 0; c < 3; ++c) m.pos[c] = f32();
    return m;
  }
};

void read_dtb_node(Reader& r);

void read_dtb_array_parent(Reader& r) {
  const uint16_t child_count = r.u16();
  (void)r.u32();  // id
  for (uint16_t i = 0; i < child_count; ++i) read_dtb_node(r);
}

void read_dtb_parent(Reader& r) {
  const bool has_tree = r.u8() != 0;
  if (!has_tree) return;
  read_dtb_array_parent(r);
}

void read_dtb_node(Reader& r) {
  const uint32_t type = r.u32();
  const milo_scene::SourceMiloEditorDtbNodePayloadPlan plan =
      milo_scene::source_milo_editor_dtb_node_payload_plan(
          static_cast<int32_t>(type));
  if (plan.reads_uint32) {
    (void)r.u32();
  } else if (plan.reads_float) {
    (void)r.f32();
  } else if (plan.reads_symbol) {
    (void)r.str();
  } else if (plan.reads_array_parent) {
    read_dtb_array_parent(r);
  }
}

void read_object_fields(Reader& r) {
  // MiloLib ObjectFields.Read for GH2+ directories: combined object revision,
  // subtype Symbol, root DTB parent, and optional note Symbol for revision > 0.
  const uint32_t combined_revision = r.u32();
  const uint16_t revision = static_cast<uint16_t>(combined_revision & 0xffffu);
  (void)r.str();
  read_dtb_parent(r);
  if (revision > 0) (void)r.str();
}

struct RndAnimatableFields {
  int32_t version = 0;
  float frame = 0.0f;
  int32_t rate = 0;
};

RndAnimatableFields read_rnd_animatable(Reader& r) {
  RndAnimatableFields out;
  out.version = r.i32();
  if (out.version > 1) out.frame = r.f32();
  if (out.version > 3) {
    out.rate = r.i32();
  } else if (out.version > 2) {
    const uint8_t legacy_rate = r.u8();
    out.rate = legacy_rate == 0 ? 1 : 0;
  }
  if (out.version < 1) {
    throw std::runtime_error(
        "char_mesh: RndAnimatable rev0 object-list branch not decoded");
  }
  return out;
}

RndMorph decode_rnd_morph_body(const std::string& entry_name,
                               const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  RndMorph morph;
  morph.name = entry_name;
  morph.revision = r.i32();
  if (morph.revision != 3 && morph.revision != 4) {
    throw std::runtime_error("char_mesh: unsupported RndMorph revision");
  }

  if (morph.revision == 3) {
    // GH1's RndAnimatable revision 0 stores legacy animation-entry and
    // animation-object vectors instead of the later frame/rate pair.
    morph.anim_revision = r.i32();
    if (morph.anim_revision != 0) {
      throw std::runtime_error(
          "char_mesh: unsupported GH1 RndMorph anim revision");
    }
    const uint32_t legacy_entries = r.u32();
    if (legacy_entries > 4096) {
      throw std::runtime_error(
          "char_mesh: implausible RndMorph legacy entry count");
    }
    for (uint32_t i = 0; i < legacy_entries; ++i) {
      (void)r.str();
      (void)r.f32();
      (void)r.f32();
    }
    const uint32_t anim_objects = r.u32();
    if (anim_objects > 4096) {
      throw std::runtime_error(
          "char_mesh: implausible RndMorph anim object count");
    }
    for (uint32_t i = 0; i < anim_objects; ++i) {
      morph.anim_objects.push_back(r.str());
    }
  } else {
    // GH2 revision 4 adds the native Hmx::Object base and upgrades the
    // embedded RndAnimatable to revision 4 (frame + rate). The morph payload
    // that follows is otherwise field-for-field compatible with revision 3.
    read_object_fields(r);
    const RndAnimatableFields animatable = read_rnd_animatable(r);
    if (animatable.version != 4) {
      throw std::runtime_error(
          "char_mesh: unsupported GH2 RndMorph anim revision");
    }
    morph.anim_revision = animatable.version;
  }

  const uint32_t pose_count = r.u32();
  if (pose_count > 4096) {
    throw std::runtime_error("char_mesh: implausible RndMorph pose count");
  }
  morph.poses.reserve(pose_count);
  for (uint32_t i = 0; i < pose_count; ++i) {
    RndMorphPose pose;
    pose.mesh = r.str();
    const uint32_t key_count = r.u32();
    if (key_count > 65536) {
      throw std::runtime_error("char_mesh: implausible RndMorph key count");
    }
    pose.keys.reserve(key_count);
    for (uint32_t k = 0; k < key_count; ++k) {
      // GH1 Key<Weight> revision 0 stores the scalar weight followed by frame.
      pose.keys.push_back({r.f32(), r.f32()});
    }
    morph.poses.push_back(std::move(pose));
  }
  morph.target = r.str();
  morph.normals = r.u8() != 0;
  morph.spline = r.u8() != 0;
  morph.intensity = r.f32();
  if (r.pos != r.n) {
    throw std::runtime_error("char_mesh: trailing RndMorph bytes");
  }
  morph.decoded = true;
  return morph;
}

struct TransFields {
  Xfm local;
  Xfm world;
  uint32_t constraint = 0;
  std::string target;
  bool preserve_scale = false;
  std::string parent;
  std::vector<std::string> legacy_children;
};

TransFields read_rnd_trans(Reader& r,
                           bool standalone,
                           int32_t parent_dir_revision) {
  TransFields out;
  const uint32_t combined_revision = r.u32();
  const uint16_t ver = static_cast<uint16_t>(combined_revision & 0xffffu);
  const milo_scene::SourceRndTransLoadPlan plan =
      milo_scene::source_rndtrans_load_plan(ver, parent_dir_revision,
                                            standalone);
  if (plan.reads_object_fields) {
    read_object_fields(r);
  }
  out.local = r.matrix();
  out.world = r.matrix();
  if (plan.reads_old_child_list) {
    const uint32_t trans_count = r.u32();
    for (uint32_t i = 0; i < trans_count; ++i) {
      if (plan.old_child_list_is_null_terminated_strings) {
        out.legacy_children.push_back(r.utf8_z());
      } else {
        out.legacy_children.push_back(r.str());
      }
    }
  }
  if (plan.reads_constraint) out.constraint = r.u32();
  if (plan.reads_target) out.target = r.str();
  if (plan.reads_preserve_scale) out.preserve_scale = r.u8() != 0;
  out.parent = r.str();
  return out;
}

std::vector<std::string> read_obj_ptr_list(Reader& r) {
  std::vector<std::string> out;
  const uint32_t count = r.u32();
  if (count > 256) throw std::runtime_error("char_mesh: implausible object list");
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) out.push_back(r.str());
  return out;
}

std::vector<std::string> read_symbol_vector(Reader& r) {
  std::vector<std::string> out;
  const uint32_t count = r.u32();
  if (count > 1024) {
    throw std::runtime_error("char_mesh: implausible symbol vector");
  }
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) out.push_back(r.str());
  return out;
}

std::string hex_bytes(const uint8_t* data, size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; ++i) {
    if (i != 0) out.push_back(':');
    out.push_back(kHex[(data[i] >> 4) & 0x0f]);
    out.push_back(kHex[data[i] & 0x0f]);
  }
  return out;
}

uint16_t source_hmx_rev(uint32_t packed) {
  return static_cast<uint16_t>(packed & 0xffffu);
}

uint16_t source_alt_rev(uint32_t packed) {
  return static_cast<uint16_t>(packed >> 16);
}

bool source_power_of_two_dim(int32_t dim) {
  if (dim < 0) return false;
  if (dim == 0) return true;
  return (dim & (dim - 1)) == 0;
}

bool source_power_of_two(int32_t width, int32_t height) {
  return source_power_of_two_dim(width) && source_power_of_two_dim(height);
}

void apply_source_rndmesh_active_bones(SkinnedMesh& mesh,
                                       const Character* character) {
  mesh.bone_palette.clear();
  mesh.bind.clear();
  const size_t slot_count =
      std::min(mesh.raw_bone_palette.size(), mesh.raw_bind.size());
  const size_t max_bones = std::min<size_t>(slot_count, 40);
  for (size_t i = 0; i < max_bones; ++i) {
    const std::string& bone_name = mesh.raw_bone_palette[i];
    if (bone_name.empty()) break;
    if (character && !character->has_transform(bone_name)) break;
    mesh.bone_palette.push_back(bone_name);
    mesh.bind.push_back(mesh.raw_bind[i]);
  }
}

void source_insert_tex_suffix(std::string& path, const char* suffix) {
  const size_t dot = path.find('.');
  if (dot == std::string::npos) return;
  path.insert(dot, suffix);
}

size_t source_bitmap_palette_bytes(int32_t bpp, uint32_t order) {
  if (bpp <= 0 || bpp > 30) return 0;
  if (bpp <= 8 && (order & 0x38u) == 0 && (order & 0x80u) == 0) {
    return static_cast<size_t>(1u << bpp) * 4u;
  }
  return 0;
}

size_t source_bitmap_row_bytes_for_width(int32_t width, int32_t bpp) {
  if (width <= 0 || bpp <= 0) return 0;
  return static_cast<size_t>(bpp) * static_cast<size_t>(width) / 8u;
}

size_t source_bitmap_mip_pixel_bytes(int32_t width, int32_t height,
                                     int32_t bpp, int32_t mip_count) {
  if (width <= 0 || height <= 0 || bpp <= 0 || mip_count <= 0) return 0;
  size_t bytes = 0;
  int32_t mip_width = width;
  int32_t mip_height = height;
  for (int32_t i = 0; i < mip_count; ++i) {
    mip_width >>= 1;
    mip_height >>= 1;
    if (mip_width <= 0 || mip_height <= 0) break;
    bytes += source_bitmap_row_bytes_for_width(mip_width, bpp) *
             static_cast<size_t>(mip_height);
  }
  return bytes;
}

}  // namespace

SourceRndMeshVertLoadPlan source_rndmesh_vert_load_plan(
    int32_t mesh_revision,
    bool is_skinned) {
  SourceRndMeshVertLoadPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.reads_legacy_weight_pair = mesh_revision != 10 && mesh_revision < 23;
  plan.reads_separate_weights = mesh_revision >= 0x25;
  plan.computes_legacy_pair_weights = plan.reads_legacy_weight_pair;
  plan.reads_legacy_extra_vec2 = mesh_revision < 0x0b;
  plan.reads_bone_indices = mesh_revision > 0x1c;
  plan.reads_post_indices_vec4 = mesh_revision > 0x1d;
  plan.quantizes_unskinned_color32 =
      mesh_revision < 0x25 && !is_skinned;
  plan.preserves_signed_skinned_float_weights =
      mesh_revision < 0x25 && is_skinned;
  plan.postload_color_to_weights = mesh_revision < 0x25 && is_skinned;
  plan.postload_clears_color = plan.postload_color_to_weights;
  plan.gh2_rev28_color_payload_is_skin_weights =
      mesh_revision == 28 && plan.postload_color_to_weights &&
      !plan.reads_separate_weights && !plan.reads_bone_indices;
  return plan;
}

SourceRndMeshBoneTailPlan source_rndmesh_bone_tail_plan(
    int32_t mesh_revision,
    const std::vector<bool>& resolved_slots) {
  SourceRndMeshBoneTailPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.reads_new_bone_vector = mesh_revision > 0x1c;
  plan.clamps_new_bone_vector_to_max = plan.reads_new_bone_vector;
  plan.reads_old_first_bone =
      mesh_revision > 0x0d && mesh_revision <= 0x1c;
  plan.calls_zero_weight_fixup = mesh_revision < 0x1f;
  if (plan.reads_new_bone_vector) {
    const size_t max_slots = std::min<size_t>(resolved_slots.size(), 40);
    for (size_t i = 0; i < max_slots; ++i) {
      if (resolved_slots[i]) ++plan.active_bone_count;
    }
    return plan;
  }
  if (!plan.reads_old_first_bone) return plan;
  if (resolved_slots.empty() || !resolved_slots[0]) {
    plan.clears_when_first_bone_null = true;
    return plan;
  }
  plan.resizes_old_bones_to_four = true;
  if (mesh_revision > 0x16) {
    plan.reads_old_slots_1_to_3 = true;
    plan.reads_four_old_offsets = true;
    plan.recomputes_pre25_legacy_weights = mesh_revision < 0x19;
  } else {
    plan.older_parent_or_self_slot0_path = true;
  }
  plan.trims_old_slots_at_first_null = true;
  const size_t old_slot_count = std::min<size_t>(resolved_slots.size(), 4);
  for (size_t i = 0; i < old_slot_count; ++i) {
    if (!resolved_slots[i]) break;
    ++plan.active_bone_count;
  }
  plan.gh2_rev28_old_four_slot_tail =
      mesh_revision == 28 && plan.reads_old_slots_1_to_3 &&
      plan.reads_four_old_offsets && plan.trims_old_slots_at_first_null;
  return plan;
}

SourceMiloEditorRndMeshRevisionWordPlan
source_milo_editor_rndmesh_revision_word_plan(
    uint32_t combined_word,
    uint16_t revision_to_write,
    uint16_t alt_revision_to_write,
    bool host_little_endian) {
  SourceMiloEditorRndMeshRevisionWordPlan plan;
  plan.combined_word = combined_word;
  plan.host_little_endian = host_little_endian;
  if (host_little_endian) {
    plan.revision = static_cast<uint16_t>(combined_word & 0xffffu);
    plan.alt_revision = static_cast<uint16_t>((combined_word >> 16) & 0xffffu);
    plan.read_low_word_as_revision = true;
    plan.write_alt_revision_high_word = true;
    plan.written_word =
        (static_cast<uint32_t>(alt_revision_to_write) << 16) |
        static_cast<uint32_t>(revision_to_write);
  } else {
    plan.alt_revision = static_cast<uint16_t>(combined_word & 0xffffu);
    plan.revision = static_cast<uint16_t>((combined_word >> 16) & 0xffffu);
    plan.read_low_word_as_alt_revision = true;
    plan.write_revision_high_word = true;
    plan.written_word =
        (static_cast<uint32_t>(revision_to_write) << 16) |
        static_cast<uint32_t>(alt_revision_to_write);
  }
  return plan;
}

SourceMiloEditorRndMeshNewPlan source_milo_editor_rndmesh_new_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    uint32_t requested_vertex_count,
    uint32_t requested_face_count) {
  SourceMiloEditorRndMeshNewPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.alt_revision = alt_revision;
  plan.requested_vertex_count = requested_vertex_count;
  plan.requested_face_count = requested_face_count;
  plan.gh2_rev28_factory_is_revision_only =
      mesh_revision == 28 && alt_revision == 0 &&
      plan.ignores_requested_vertex_count &&
      plan.ignores_requested_face_count && plan.factory_only_sets_revision_fields;
  return plan;
}

SourceMiloEditorRndMeshBoneTransformIoPlan
source_milo_editor_rndmesh_bone_transform_io_plan(
    int32_t mesh_revision,
    bool read_probe_positive,
    int32_t bone_transform_count) {
  SourceMiloEditorRndMeshBoneTransformIoPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.input_bone_transform_count = std::max(0, bone_transform_count);

  if (read_probe_positive) {
    plan.read_rewinds_probe_when_positive = true;
    if (mesh_revision >= 33) {
      plan.read_modern_counted_vector = true;
    } else {
      plan.read_legacy_four_names_then_four_transforms = true;
      plan.read_legacy_slot_count = 4;
    }
  } else {
    plan.read_skips_bone_block_when_probe_nonpositive = true;
  }

  if (mesh_revision >= 33) {
    plan.write_modern_counted_vector = true;
    plan.write_serialized_slot_count = plan.input_bone_transform_count;
  } else if (plan.input_bone_transform_count > 0) {
    plan.write_legacy_pads_to_four_when_nonempty = true;
    plan.write_legacy_four_names_then_four_transforms = true;
    plan.write_serialized_slot_count = 4;
  } else {
    plan.write_legacy_zero_sentinel_when_empty = true;
  }
  return plan;
}

SourceMiloEditorRndMeshFaceIoPlan source_milo_editor_rndmesh_face_io_plan(
    int32_t face_count) {
  SourceMiloEditorRndMeshFaceIoPlan plan;
  plan.face_count = std::max(0, face_count);
  plan.reads_faces_in_count_order = plan.face_count > 0;
  plan.writes_faces_in_vector_order = plan.face_count > 0;
  plan.read_face_rows = plan.face_count;
  plan.write_face_rows = plan.face_count;
  return plan;
}

SourceMiloEditorRndMeshVertexIoPlan
source_milo_editor_rndmesh_vertex_io_plan(
    int32_t mesh_version,
    bool is_next_gen,
    int32_t vertex_count) {
  SourceMiloEditorRndMeshVertexIoPlan plan;
  plan.mesh_version = mesh_version;
  plan.vertex_count = std::max(0, vertex_count);
  plan.read_vertex_rows = plan.vertex_count;
  plan.write_vertex_rows = plan.vertex_count;
  plan.reads_next_gen_header = mesh_version >= 36;
  plan.writes_next_gen_header = mesh_version >= 36;
  plan.next_gen_header_has_vertex_size_and_compression =
      mesh_version >= 36 && is_next_gen;
  plan.uses_last_gen_uncompressed_rows = mesh_version < 35 || !is_next_gen;

  if (!plan.uses_last_gen_uncompressed_rows) {
    plan.row_reads_packed_next_gen = true;
    return plan;
  }

  plan.row_float_count = 3;
  if (mesh_version == 34) {
    plan.row_reads_position_w = true;
    plan.row_float_count += 1;
  }

  if (mesh_version <= 10) {
    plan.row_layout_freq_le10 = true;
    plan.row_reads_normal_xyz = true;
    plan.row_reads_uv = true;
    plan.row_reads_weights = true;
    plan.row_reads_uv_before_weights = true;
    plan.row_reads_bone_indices = true;
    plan.row_float_count += 3 + 2 + 4;
    plan.row_uint16_count = 4;
  } else if (mesh_version <= 22) {
    plan.row_layout_bones_first_11_to_22 = true;
    plan.row_reads_bone_indices = true;
    plan.row_reads_bone_indices_before_normal = true;
    plan.row_reads_normal_xyz = true;
    plan.row_reads_weights = true;
    plan.row_reads_weights_before_uv = true;
    plan.row_reads_uv = true;
    plan.row_float_count += 3 + 4 + 2;
    plan.row_uint16_count = 4;
  } else {
    plan.row_layout_modern_uncompressed_23_plus = true;
    if (mesh_version >= 38) {
      plan.row_layout_packed_uncompressed_38_plus = true;
      plan.row_reads_pre_normal_packed_pairs = true;
      plan.row_reads_packed_next_gen = true;
      plan.row_float_count += 2;
      plan.row_uint32_count += 2;
    }
    plan.row_reads_normal_xyz = true;
    plan.row_float_count += 3;
    if (mesh_version == 34) {
      plan.row_reads_normal_w = true;
      plan.row_float_count += 1;
    }
    plan.row_reads_weights = true;
    plan.row_reads_uv = true;
    plan.row_reads_weights_before_uv = mesh_version < 38;
    plan.row_reads_uv_before_weights = mesh_version >= 38;
    plan.row_float_count += 4 + 2;
    if (mesh_version >= 33) {
      plan.row_reads_bone_indices = true;
      plan.row_uint16_count = 4;
      if (mesh_version >= 38) {
        plan.row_reads_post_bone_packed_pairs = true;
        plan.row_reads_packed_next_gen = true;
        plan.row_float_count += 2;
        plan.row_uint32_count += 2;
      } else {
        plan.row_reads_tangent = true;
        plan.row_float_count += 4;
        if (mesh_version > 34) {
          plan.row_reads_tangent_unknown_float_pair = true;
          plan.row_float_count += 2;
        }
      }
    }
  }

  plan.row_byte_size = plan.row_float_count * 4 + plan.row_uint32_count * 4 +
                       plan.row_uint16_count * 2;
  plan.gh2_rev28_row_is_skin_vertex_48 =
      mesh_version == 28 && !is_next_gen &&
      plan.uses_last_gen_uncompressed_rows && plan.row_reads_position_xyz &&
      plan.row_reads_normal_xyz && plan.row_reads_weights &&
      plan.row_reads_weights_before_uv && plan.row_reads_uv &&
      !plan.row_reads_bone_indices && !plan.row_reads_tangent &&
      plan.row_float_count == 12 && plan.row_uint32_count == 0 &&
      plan.row_uint16_count == 0 &&
      plan.row_byte_size == 48;
  return plan;
}

SourceMiloEditorRndMeshCompressedVertexIoPlan
source_milo_editor_rndmesh_compressed_vertex_io_plan(
    int32_t mesh_version,
    bool is_next_gen,
    int32_t compression_type) {
  SourceMiloEditorRndMeshCompressedVertexIoPlan plan;
  plan.mesh_version = mesh_version;
  plan.is_next_gen = is_next_gen;
  plan.compression_type = compression_type;
  plan.uses_next_gen_compressed_branch = mesh_version >= 35 && is_next_gen;
  plan.gh2_rev28_is_not_next_gen_compressed =
      mesh_version == 28 && !plan.uses_next_gen_compressed_branch;
  if (!plan.uses_next_gen_compressed_branch) return plan;

  plan.reads_half_uv = true;
  plan.writes_half_uv = true;
  if (compression_type == 1) {
    plan.compression1_xbox_layout = true;
    plan.reads_rgba_color_word = true;
    plan.writes_rgba_color_word = true;
    plan.reads_signed_compressed_vec4_normals = true;
    plan.writes_signed_compressed_vec4_normals = true;
    plan.reads_signed_compressed_vec4_tangents = true;
    plan.writes_signed_compressed_vec4_tangents = true;
    plan.reads_unsigned_compressed_vec4_weights = true;
    plan.writes_unsigned_compressed_vec4_weights = true;
    plan.reads_bone_indices_as_bytes = true;
    plan.writes_bone_indices_as_bytes = true;
    plan.bone_index_storage_bytes_per_slot = 1;
  } else if (compression_type == 2) {
    plan.compression2_ps3_layout = true;
    plan.reads_argb_color_word = true;
    plan.writes_argb_color_word = true;
    plan.reads_ps3_signed_compressed_vec3_normals = true;
    plan.writes_ps3_signed_compressed_vec3_normals = true;
    plan.reads_ps3_signed_compressed_vec3_tangents = true;
    plan.writes_ps3_signed_compressed_vec3_tangents = true;
    plan.reads_ps3_unsigned_compressed_vec3_weights = true;
    plan.writes_ps3_unsigned_compressed_vec3_weights = true;
    plan.reads_bone_indices_as_uint16 = true;
    plan.writes_bone_indices_as_uint16_with_byte_cast = true;
    plan.bone_index_storage_bytes_per_slot = 2;
  } else {
    plan.unsupported_compression_type = true;
  }
  return plan;
}

SourceMiloEditorCompressedVectorBoundary
source_milo_editor_compressed_vector_boundary() {
  SourceMiloEditorCompressedVectorBoundary boundary;
  boundary.call_sites = {
      "RndMesh.Vertices type1 normals SignedCompressedVec4",
      "RndMesh.Vertices type1 tangents SignedCompressedVec4",
      "RndMesh.Vertices type1 weights UnsignedCompressedVec4",
      "RndMesh.Vertices type2 normals PS3SignedCompressedVec3",
      "RndMesh.Vertices type2 tangents PS3SignedCompressedVec3",
      "RndMesh.Vertices type2 weights PS3UnsignedCompressedVec3",
  };
  boundary.missing_helpers = {
      "MiloLib.Classes.Vertex",
      "Vertex.SignedCompressedVec4",
      "Vertex.UnsignedCompressedVec4",
      "Vertex.PS3SignedCompressedVec3",
      "Vertex.PS3UnsignedCompressedVec3",
  };
  return boundary;
}

SourceRndMeshSkinIndexPlan source_rndmesh_skin_index_plan(
    int32_t mesh_revision) {
  SourceRndMeshSkinIndexPlan plan;
  // ihatecompvir rb3 Mesh.cpp operator>>(RndMesh::Vert&) reads explicit
  // per-vertex bone indices only after revision 0x1c.
  plan.rb3_stream_reads_bone_indices = mesh_revision > 0x1c;

  // MiloEditor's layout reader has older pre-GH2 indexed layouts, then later
  // indexed layouts. GH2 rev28 is in the in-between legacy slot-weight range.
  plan.milo_editor_reads_bone_indices =
      mesh_revision <= 22 || mesh_revision >= 33;

  // RB3 PostLoad calls SetZeroWeightBones for old mesh revisions, but this is
  // not a license to invent serialized indices for GH2 rev28 rows.
  plan.zero_weight_fixup_runs = mesh_revision > 0 && mesh_revision < 0x1f;
  plan.gh2_legacy_slots_without_serialized_indices =
      mesh_revision == 28 && !plan.rb3_stream_reads_bone_indices &&
      !plan.milo_editor_reads_bone_indices;
  return plan;
}

SourceRndMeshSkinRuntimeBoundary source_rndmesh_skin_runtime_boundary() {
  return SourceRndMeshSkinRuntimeBoundary{};
}

SourceRndMeshFieldGatePlan source_rndmesh_field_gate_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    int32_t parent_dir_revision,
    int32_t group_sizes_count,
    bool group_sizes_first_positive) {
  SourceRndMeshFieldGatePlan plan;
  plan.mesh_revision = mesh_revision;
  plan.alt_revision = alt_revision;
  plan.parent_dir_revision = parent_dir_revision;
  plan.reads_second_material = mesh_revision == 27;
  plan.reads_alt_geom_owner = mesh_revision < 13;
  plan.reads_trans_parent = mesh_revision < 15;
  plan.reads_unknown_trans_refs = mesh_revision < 14;
  plan.reads_unknown_vector3 = mesh_revision < 3;
  plan.reads_legacy_sphere = mesh_revision < 15;
  plan.reads_legacy_bool = mesh_revision < 8;
  plan.reads_unknown_symbol_float = mesh_revision < 15;
  plan.reads_legacy_bool1 = mesh_revision < 16 && mesh_revision > 11;
  plan.reads_mutable = mesh_revision >= 16;
  plan.reads_volume = mesh_revision > 17;
  plan.reads_bsp_node = mesh_revision > 18;
  plan.reads_rev7_bool = mesh_revision == 7;
  plan.reads_legacy_int = mesh_revision < 11;
  plan.reads_group_sizes_modern = mesh_revision > 0x17;
  plan.reads_patch_vector_loop_legacy =
      mesh_revision > 0x15 && mesh_revision <= 0x17;
  plan.reads_group_sizes_legacy =
      mesh_revision > 0x10 && mesh_revision <= 0x15;
  plan.reads_modern_bone_transform_vector = mesh_revision >= 33;
  plan.reads_old_four_bone_names_and_offsets = mesh_revision < 33;
  plan.striper_block_todo = alt_revision > 5;
  plan.legacy_usvec_todo = mesh_revision != 0 && mesh_revision < 4;
  plan.revision_zero_todo = mesh_revision == 0;
  plan.reads_keep_mesh_data = mesh_revision > 34;
  plan.reads_has_ao_calculation = mesh_revision > 0x25;
  plan.reads_no_quant = alt_revision > 1;
  plan.reads_alt_bool3 = alt_revision > 3;
  plan.reads_group_sections =
      group_sizes_count > 0 && group_sizes_first_positive &&
      parent_dir_revision < 25;
  return plan;
}

SourceMiloEditorRndMeshCoreFieldsIoPlan
source_milo_editor_rndmesh_core_fields_io_plan(int32_t mesh_revision) {
  SourceMiloEditorRndMeshCoreFieldsIoPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.reads_second_material_symbol = mesh_revision == 27;
  plan.writes_second_material_symbol = mesh_revision == 27;
  plan.reads_alt_geom_owner_symbol = mesh_revision < 13;
  plan.writes_alt_geom_owner_symbol = mesh_revision < 13;
  plan.reads_trans_parent_symbol = mesh_revision < 15;
  plan.writes_trans_parent_symbol = mesh_revision < 15;
  plan.reads_unknown_transform_refs = mesh_revision < 14;
  plan.writes_unknown_transform_refs = mesh_revision < 14;
  plan.reads_unknown_vector3 = mesh_revision < 3;
  plan.writes_unknown_vector3 = mesh_revision < 3;
  plan.reads_legacy_sphere = mesh_revision < 15;
  plan.writes_legacy_sphere = mesh_revision < 15;
  plan.reads_legacy_bool = mesh_revision < 8;
  plan.writes_legacy_bool = mesh_revision < 8;
  plan.reads_unknown_symbol_float = mesh_revision < 15;
  plan.writes_unknown_symbol_float = mesh_revision < 15;
  plan.reads_legacy_bool1 = mesh_revision < 16 && mesh_revision > 11;
  plan.writes_legacy_bool1 = mesh_revision < 16 && mesh_revision > 11;
  plan.reads_mutable_uint32 = mesh_revision >= 16;
  plan.writes_mutable_uint32 = mesh_revision >= 16;
  plan.reads_volume_uint32 = mesh_revision > 17;
  plan.writes_volume_uint32 = mesh_revision > 17;
  plan.reads_bsp_node = mesh_revision > 18;
  plan.writes_bsp_node = mesh_revision > 18;
  plan.reads_rev7_bool = mesh_revision == 7;
  plan.writes_rev7_bool = mesh_revision == 7;
  plan.reads_legacy_int = mesh_revision < 11;
  plan.writes_legacy_int = mesh_revision < 11;

  plan.read_symbol_count = 2 + (plan.reads_second_material_symbol ? 1 : 0) +
                           (plan.reads_alt_geom_owner_symbol ? 1 : 0) +
                           (plan.reads_trans_parent_symbol ? 1 : 0) +
                           (plan.reads_unknown_transform_refs ? 2 : 0) +
                           (plan.reads_unknown_symbol_float ? 1 : 0);
  plan.write_symbol_count = 2 + (plan.writes_second_material_symbol ? 1 : 0) +
                            (plan.writes_alt_geom_owner_symbol ? 1 : 0) +
                            (plan.writes_trans_parent_symbol ? 1 : 0) +
                            (plan.writes_unknown_transform_refs ? 2 : 0) +
                            (plan.writes_unknown_symbol_float ? 1 : 0);
  plan.read_bool_count = (plan.reads_legacy_bool ? 1 : 0) +
                         (plan.reads_legacy_bool1 ? 1 : 0) +
                         (plan.reads_rev7_bool ? 1 : 0);
  plan.write_bool_count = (plan.writes_legacy_bool ? 1 : 0) +
                          (plan.writes_legacy_bool1 ? 1 : 0) +
                          (plan.writes_rev7_bool ? 1 : 0);
  plan.read_uint32_count = (plan.reads_mutable_uint32 ? 1 : 0) +
                           (plan.reads_volume_uint32 ? 1 : 0) +
                           (plan.reads_legacy_int ? 1 : 0);
  plan.write_uint32_count = (plan.writes_mutable_uint32 ? 1 : 0) +
                            (plan.writes_volume_uint32 ? 1 : 0) +
                            (plan.writes_legacy_int ? 1 : 0);
  plan.gh2_rev28_core_is_mat_geom_mutable_volume_bsp =
      mesh_revision == 28 && plan.read_symbol_count == 2 &&
      plan.read_bool_count == 0 && plan.read_uint32_count == 2 &&
      plan.reads_material_symbol && plan.reads_geom_owner_symbol &&
      plan.reads_mutable_uint32 && plan.reads_volume_uint32 &&
      plan.reads_bsp_node && !plan.reads_second_material_symbol &&
      !plan.reads_alt_geom_owner_symbol && !plan.reads_trans_parent_symbol &&
      !plan.reads_unknown_transform_refs;
  return plan;
}

SourceMiloEditorRndMeshEnumPlan source_milo_editor_rndmesh_enum_plan(
    int32_t mesh_revision,
    uint32_t volume_value) {
  SourceMiloEditorRndMeshEnumPlan plan;
  plan.gh2_rev28_volume_value_is_triangles =
      mesh_revision == 28 && volume_value == plan.volume_triangles;
  return plan;
}

SourceMiloEditorRndMeshBspNodeIoPlan
source_milo_editor_rndmesh_bsp_node_io_plan(
    int32_t mesh_revision,
    bool has_value,
    bool left_present,
    bool right_present) {
  SourceMiloEditorRndMeshBspNodeIoPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.has_value = has_value;
  plan.left_present = left_present;
  plan.right_present = right_present;
  plan.reads_bsp_node = mesh_revision > 18;
  plan.writes_bsp_node = mesh_revision > 18;
  if (!plan.reads_bsp_node) return plan;

  if (!has_value) {
    plan.empty_node_is_bool_only = true;
    return plan;
  }

  plan.reads_vector4_when_has_value = true;
  plan.reads_left_right_children_when_has_value = true;
  plan.writes_vector4_when_has_value = true;
  plan.writes_left_child_only_if_present = left_present;
  plan.writes_right_child_only_if_present = right_present;
  plan.read_child_count = 2;
  plan.write_child_count = (left_present ? 1 : 0) + (right_present ? 1 : 0);
  plan.gh2_rev28_bsp_node_is_source_bool_tree =
      mesh_revision == 28 && plan.row_starts_with_has_value_bool &&
      plan.reads_vector4_when_has_value &&
      plan.reads_left_right_children_when_has_value &&
      plan.write_does_not_allocate_missing_children;
  return plan;
}

SourceMiloEditorRndMeshSectionOrderPlan
source_milo_editor_rndmesh_section_order_plan(
    int32_t mesh_revision,
    int32_t alt_revision,
    bool standalone) {
  SourceMiloEditorRndMeshSectionOrderPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.alt_revision = alt_revision;
  plan.read_sections = {"combined_revision", "base", "trans", "draw",
                        "core_fields",       "vertices", "faces",
                        "group_sizes",       "bone_transforms",
                        "tail_flags",        "group_sections"};
  plan.write_sections = plan.read_sections;
  if (standalone) {
    plan.read_sections.push_back("standalone_end_bytes");
    plan.write_sections.push_back("standalone_end_bytes");
  }
  plan.read_write_orders_match = plan.read_sections == plan.write_sections;
  plan.gh2_rev28_order_is_source_layout =
      mesh_revision == 28 && alt_revision == 0 && !standalone &&
      plan.read_write_orders_match && plan.vertices_before_faces &&
      plan.group_sizes_before_bone_transforms &&
      plan.tail_flags_before_group_sections;
  return plan;
}

SourceMiloEditorRndMeshGroupSizesIoPlan
source_milo_editor_rndmesh_group_sizes_io_plan(
    int32_t mesh_revision,
    int32_t group_sizes_count) {
  SourceMiloEditorRndMeshGroupSizesIoPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.input_group_sizes_count = std::max(0, group_sizes_count);
  plan.reads_modern_group_sizes = mesh_revision > 0x17;
  plan.writes_modern_group_sizes = mesh_revision > 0x17;
  plan.reads_legacy_group_sizes =
      mesh_revision > 0x10 && mesh_revision <= 0x15;
  plan.writes_legacy_group_sizes =
      mesh_revision > 0x10 && mesh_revision <= 0x15;
  plan.leaves_patch_vector_loop_todo =
      mesh_revision > 0x15 && mesh_revision <= 0x17;

  if (plan.reads_modern_group_sizes || plan.reads_legacy_group_sizes) {
    plan.reads_count_before_rows = true;
    plan.writes_count_from_group_sizes_vector = true;
    plan.read_group_size_rows = plan.input_group_sizes_count;
    plan.write_group_size_rows = plan.input_group_sizes_count;
  }

  plan.gh2_rev28_counted_byte_rows =
      mesh_revision == 28 && plan.reads_modern_group_sizes &&
      plan.writes_modern_group_sizes && plan.group_size_row_is_uint8 &&
      plan.reads_count_before_rows && plan.writes_count_from_group_sizes_vector;
  return plan;
}

SourceMiloEditorRndMeshUnsupportedTailPlan
source_milo_editor_rndmesh_unsupported_tail_plan(
    int32_t mesh_revision,
    int32_t alt_revision) {
  SourceMiloEditorRndMeshUnsupportedTailPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.alt_revision = alt_revision;
  plan.read_alt_revision_striper_todo = alt_revision > 5;
  plan.read_legacy_usvec_todo = mesh_revision != 0 && mesh_revision < 4;
  plan.read_revision_zero_comment_todo = mesh_revision == 0;
  plan.read_todo_block_count =
      (plan.read_alt_revision_striper_todo ? 1 : 0) +
      (plan.read_legacy_usvec_todo ? 1 : 0) +
      (plan.read_revision_zero_comment_todo ? 1 : 0);
  plan.gh2_rev28_has_no_unsupported_tail =
      mesh_revision == 28 && alt_revision == 0 &&
      plan.read_todo_block_count == 0 &&
      plan.write_has_no_alt_revision_striper_todo &&
      plan.write_has_no_legacy_usvec_todo &&
      plan.write_has_no_revision_zero_todo;
  return plan;
}

SourceMiloEditorRndMeshTailFlagsIoPlan
source_milo_editor_rndmesh_tail_flags_io_plan(
    int32_t mesh_revision,
    int32_t alt_revision) {
  SourceMiloEditorRndMeshTailFlagsIoPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.alt_revision = alt_revision;
  plan.reads_keep_mesh_data = mesh_revision > 34;
  plan.writes_keep_mesh_data = mesh_revision > 34;
  plan.reads_has_ao_calculation = mesh_revision > 0x25;
  plan.writes_has_ao_calculation = mesh_revision > 0x25;
  plan.reads_no_quant = alt_revision > 1;
  plan.writes_no_quant = alt_revision > 1;
  plan.reads_unk_bool3 = alt_revision > 3;
  plan.writes_unk_bool3 = alt_revision > 3;
  plan.read_bool_count =
      (plan.reads_keep_mesh_data ? 1 : 0) +
      (plan.reads_has_ao_calculation ? 1 : 0) +
      (plan.reads_no_quant ? 1 : 0) + (plan.reads_unk_bool3 ? 1 : 0);
  plan.write_bool_count =
      (plan.writes_keep_mesh_data ? 1 : 0) +
      (plan.writes_has_ao_calculation ? 1 : 0) +
      (plan.writes_no_quant ? 1 : 0) + (plan.writes_unk_bool3 ? 1 : 0);
  plan.gh2_rev28_has_no_tail_flags =
      mesh_revision == 28 && alt_revision == 0 && plan.read_bool_count == 0 &&
      plan.write_bool_count == 0;
  return plan;
}

SourceMiloEditorRndMeshGroupSectionIoPlan
source_milo_editor_rndmesh_group_section_io_plan(
    int32_t group_sizes_count,
    bool group_sizes_first_positive,
    int32_t parent_dir_revision,
    int32_t existing_group_section_count) {
  SourceMiloEditorRndMeshGroupSectionIoPlan plan;
  plan.group_sizes_count = std::max(0, group_sizes_count);
  plan.existing_group_section_count = std::max(0, existing_group_section_count);
  const bool gate = plan.group_sizes_count > 0 && group_sizes_first_positive &&
                    parent_dir_revision < 25;
  plan.reads_group_sections = gate;
  plan.writes_group_sections = gate;
  if (gate) {
    plan.read_group_section_count = plan.group_sizes_count;
    plan.write_group_section_count = plan.group_sizes_count;
    plan.write_pads_to_group_sizes_count =
        plan.existing_group_section_count < plan.group_sizes_count;
  }
  return plan;
}

SourceGltfMiloSkinAccessorSetPlan source_gltf_milo_validate_skin_accessor_set(
    bool has_joints,
    bool has_weights,
    int32_t joints_count,
    int32_t weights_count,
    int32_t expected_position_count) {
  SourceGltfMiloSkinAccessorSetPlan plan;
  if (!has_joints && !has_weights) {
    plan.ignored_empty_pair = true;
    return plan;
  }
  if (!has_joints || !has_weights) {
    plan.warned_missing_pair = true;
    plan.cleared_joints = true;
    plan.cleared_weights = true;
    return plan;
  }
  if (joints_count != weights_count) {
    plan.warned_mismatched_counts = true;
    plan.cleared_joints = true;
    plan.cleared_weights = true;
    return plan;
  }
  if (joints_count != expected_position_count) {
    plan.warned_position_count_mismatch = true;
    plan.cleared_joints = true;
    plan.cleared_weights = true;
    return plan;
  }
  plan.valid = true;
  return plan;
}

SourceGltfMiloPrimitiveReadPlan source_gltf_milo_primitive_read_plan(
    const SourceGltfMiloPrimitiveReadInput& input) {
  SourceGltfMiloPrimitiveReadPlan plan;
  const bool has_positions =
      input.position_accessor_present && !input.position_read_failed;
  plan.logs_position_read_error = input.position_read_failed;
  plan.logs_normal_read_error = input.normal_read_failed;
  plan.logs_bad_normals =
      input.normal_read_failed || input.normal_count == 0 ||
      input.normals_all_zero;
  plan.logs_uv_read_error = input.uv_read_failed;
  plan.logs_bad_uvs =
      input.uv_read_failed || input.uv_count == 0 || input.uvs_all_zero;

  if (input.indices_read_failed) {
    plan.logs_index_read_error = true;
    plan.logs_cannot_continue_mesh = true;
    plan.skips_primitive = true;
    plan.skip_reason = "indices_read_failed";
    return plan;
  }

  if (!has_positions) {
    plan.warns_missing_position = true;
    plan.skips_primitive = true;
    plan.skip_reason = "missing_position";
    return plan;
  }

  if (input.has_any_skin_accessors && !input.has_skin) {
    plan.warns_skin_accessors_without_skin = true;
    plan.clears_skin_accessors = true;
  }

  plan.validates_primary_skin_set = true;
  plan.validates_secondary_skin_set = true;

  const bool primary_valid = input.has_skin && input.primary_skin_set_valid;
  bool secondary_valid = input.has_skin && input.secondary_skin_set_valid;
  if (!primary_valid && secondary_valid) {
    plan.warns_secondary_without_primary = true;
    plan.clears_secondary_skin_set = true;
    secondary_valid = false;
  }

  plan.builds_vertex_skin_influences =
      input.has_skin && (primary_valid || secondary_valid);
  plan.builds_empty_vertex_skin_influences =
      !plan.builds_vertex_skin_influences;

  if (input.source_triangle_count <= 0) {
    plan.warns_no_valid_triangles = true;
    plan.skips_primitive = true;
    plan.skip_reason = "no_valid_triangles";
    return plan;
  }

  plan.reaches_chunking = true;
  return plan;
}

SourceGltfMiloPrimitiveFilenamePlan
source_gltf_milo_primitive_filename_plan(
    const std::string& node_name,
    int32_t primitive_index) {
  SourceGltfMiloPrimitiveFilenamePlan plan;
  plan.node_name = node_name;
  plan.primitive_index = primitive_index;
  if (primitive_index == 0) {
    plan.first_primitive_uses_plain_node_name = true;
    plan.base_filename = node_name + ".mesh";
  } else {
    plan.later_primitive_uses_index_suffix = true;
    plan.base_filename =
        node_name + "_" + std::to_string(primitive_index) + ".mesh";
  }
  return plan;
}

SourceGltfMiloSkinValidationResult source_gltf_milo_validate_skin_influences(
    const std::vector<SourceGltfMiloRawSkinInfluence>& raw_influences,
    int32_t skin_joint_count,
    const std::vector<int32_t>& excluded_joint_indices) {
  SourceGltfMiloSkinValidationResult result;
  auto excluded = [&](int32_t joint_index) {
    return std::find(excluded_joint_indices.begin(),
                     excluded_joint_indices.end(),
                     joint_index) != excluded_joint_indices.end();
  };

  for (const SourceGltfMiloRawSkinInfluence& raw : raw_influences) {
    if (!std::isfinite(raw.weight)) {
      result.logged_invalid_weights = true;
      ++result.ignored_invalid_weights;
      continue;
    }
    if (raw.weight <= 0.0f) continue;

    if (!std::isfinite(raw.joint_value)) {
      result.logged_invalid_joint_indices = true;
      ++result.ignored_invalid_joint_indices;
      continue;
    }

    const int32_t joint_index =
        static_cast<int32_t>(std::round(raw.joint_value));
    if (std::fabs(raw.joint_value - static_cast<float>(joint_index)) >
            0.001f ||
        joint_index < 0 || joint_index >= skin_joint_count) {
      result.logged_invalid_joint_indices = true;
      ++result.ignored_invalid_joint_indices;
      continue;
    }

    if (excluded(joint_index)) {
      result.logged_excluded_joint_influences = true;
      ++result.ignored_excluded_joint_influences;
      continue;
    }

    result.influences.push_back({joint_index, raw.weight});
  }

  std::stable_sort(
      result.influences.begin(), result.influences.end(),
      [](const SourceGltfMiloValidatedSkinInfluence& a,
         const SourceGltfMiloValidatedSkinInfluence& b) {
        return a.weight > b.weight;
      });

  if (result.influences.size() > 4) {
    result.logged_trimmed_influences = true;
    result.dropped_influence_count =
        static_cast<int32_t>(result.influences.size() - 4);
    for (size_t i = 4; i < result.influences.size(); ++i) {
      result.dropped_weight += result.influences[i].weight;
    }
    result.influences.resize(4);
  }

  float total_weight = 0.0f;
  for (const SourceGltfMiloValidatedSkinInfluence& influence :
       result.influences) {
    total_weight += influence.weight;
  }
  if (total_weight > 0.0f) {
    for (SourceGltfMiloValidatedSkinInfluence& influence : result.influences) {
      influence.weight /= total_weight;
    }
  }
  return result;
}

SourceGltfMiloVertexSkinInfluencePlan
source_gltf_milo_get_vertex_skin_influences_plan(
    const SourceGltfMiloSkinAccessorVertexRow& set0,
    const SourceGltfMiloSkinAccessorVertexRow& set1,
    int32_t skin_joint_count,
    const std::vector<int32_t>& excluded_joint_indices) {
  SourceGltfMiloVertexSkinInfluencePlan plan;

  auto append_set = [&](const SourceGltfMiloSkinAccessorVertexRow& set,
                        const char* accessor_name, bool& read_flag) {
    if (!set.present) return;
    read_flag = true;
    plan.accessor_order.push_back(accessor_name);
    for (size_t i = 0; i < 4; ++i) {
      plan.raw_influences.push_back({set.joints[i], set.weights[i]});
    }
  };

  append_set(set0, "JOINTS_0/WEIGHTS_0", plan.read_joints0_weights0);
  append_set(set1, "JOINTS_1/WEIGHTS_1", plan.read_joints1_weights1);
  plan.validation =
      source_gltf_milo_validate_skin_influences(plan.raw_influences,
                                                skin_joint_count,
                                                excluded_joint_indices);
  return plan;
}

SourceGltfMiloPackedSkinSlots source_gltf_milo_pack_skin_slots(
    const std::vector<SourceGltfMiloSkinInfluence>& influences,
    bool compressed_vertex_layout) {
  SourceGltfMiloPackedSkinSlots slots;
  const size_t count = std::min<size_t>(influences.size(), 4);
  for (size_t i = 0; i < count; ++i) {
    slots.weights[i] = influences[i].weight;
  }

  constexpr uint16_t kInvalidBone = 0xffff;
  auto remapped = [&](size_t index) -> uint16_t {
    if (index >= count || influences[index].remapped_bone < 0 ||
        influences[index].remapped_bone > 0xffff) {
      return kInvalidBone;
    }
    return static_cast<uint16_t>(influences[index].remapped_bone);
  };

  if (count > 0) {
    if (compressed_vertex_layout) {
      slots.bones[0] = count > 3 ? remapped(3) : 0;
      slots.bones[1] = count > 2 ? remapped(2) : slots.bones[0];
      slots.bones[2] = count > 1 ? remapped(1) : slots.bones[1];
      slots.bones[3] = remapped(0);
    } else {
      slots.bones[0] = remapped(0);
      slots.bones[1] = count > 1 ? remapped(1) : slots.bones[0];
      slots.bones[2] = count > 2 ? remapped(2) : slots.bones[1];
      slots.bones[3] = count > 3 ? remapped(3) : slots.bones[2];
    }
  }

  uint16_t last_valid_bone = 0;
  for (uint16_t& bone : slots.bones) {
    if (bone != kInvalidBone) {
      last_valid_bone = bone;
    } else {
      bone = last_valid_bone;
    }
  }
  return slots;
}

SourceGltfMiloAddVertexResult source_gltf_milo_add_vertex_to_chunk_mesh(
    uint32_t original_index,
    const std::vector<uint32_t>& existing_original_indices,
    const SourceGltfMiloVertexInput& input,
    bool mesh_has_skin,
    bool compressed_vertex_layout,
    bool mesh_has_ao_calculation,
    int32_t current_vertex_count) {
  SourceGltfMiloAddVertexResult result;
  result.vertex.original_index = original_index;
  const auto existing_it =
      std::find(existing_original_indices.begin(),
                existing_original_indices.end(), original_index);
  if (existing_it != existing_original_indices.end()) {
    result.skipped_existing = true;
    result.new_index = static_cast<uint16_t>(
        std::distance(existing_original_indices.begin(), existing_it));
    return result;
  }

  result.added_vertex = true;
  result.new_index =
      current_vertex_count >= 0
          ? static_cast<uint16_t>(current_vertex_count)
          : 0;
  result.vertex.position = input.position;
  if (input.has_uv) result.vertex.uv = input.uv;
  if (input.has_normal) result.vertex.normal = input.normal;
  if (input.has_tangent) result.vertex.tangent = input.tangent;

  SourceGltfMiloPackedSkinSlots skin =
      source_gltf_milo_pack_skin_slots(input.influences,
                                       compressed_vertex_layout);
  if (!mesh_has_skin || input.influences.empty()) {
    skin.bones = {0, 0, 0, 0};
  }
  result.vertex.skin = skin;

  if (input.has_color) result.vertex.color = input.color;
  if (mesh_has_ao_calculation) {
    result.applied_ao_color_override = true;
    result.vertex.color = {255.0f, 255.0f, 255.0f, 255.0f};
  }

  result.exceeded_max_vertices = current_vertex_count + 1 > 65535;
  return result;
}

SourceGltfMiloMaterialPlan source_gltf_milo_material_base_plan(
    const SourceGltfMiloMaterialInput& input) {
  SourceGltfMiloMaterialPlan plan;
  plan.mat_entry_name = input.name + ".mat";
  std::string platform_lower = input.platform;
  for (char& c : platform_lower) {
    c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
  }
  const bool xbox_platform = platform_lower == "xbox";

  if (input.has_base_color_texture) {
    plan.creates_diffuse_tex_entry = true;
    plan.diffuse_tex = input.name + ".tex";
    plan.diffuse_tex_entry_name = input.name + ".tex";
    plan.texture_external_path = input.name + ".png";
    plan.diffuse_compression_format = input.image_has_alpha ? "BC3" : "BC1";
    plan.diffuse_bitmap_encoding =
        input.image_has_alpha ? "DXT5_BC3" : "DXT1_BC1";
    plan.diffuse_mip_map_k = -8.0f;
    plan.diffuse_type_regular = true;
    plan.diffuse_optimize_for_ps3 = true;
    plan.diffuse_bitmap_mip_maps_zero = true;
    plan.diffuse_bpl_width_bpp_over_8 = true;
    plan.diffuse_xbox_byte_swap = xbox_platform;
    plan.stencil_ignore = true;
    plan.per_pixel_lit = true;
    plan.pre_lit = !input.prelit_option_equals_false;
    plan.point_lights = true;
    plan.projected_lights = true;
    plan.fog = false;
    plan.cull = !input.double_sided;

    if (input.name.find("_skin") != std::string::npos) {
      plan.shader_variation = 1;
    } else if (input.name.find("_hair") != std::string::npos) {
      plan.shader_variation = 2;
    } else {
      plan.shader_variation = 0;
    }

    if (input.sampler_present) {
      if (input.wrap_s == SourceGltfMiloTextureWrapMode::kClampToEdge ||
          input.wrap_t == SourceGltfMiloTextureWrapMode::kClampToEdge) {
        plan.tex_wrap = 0;
      } else if (input.wrap_s ==
                     SourceGltfMiloTextureWrapMode::kMirroredRepeat ||
                 input.wrap_t ==
                     SourceGltfMiloTextureWrapMode::kMirroredRepeat) {
        plan.tex_wrap = 4;
      } else {
        plan.tex_wrap = 1;
      }
    } else {
      plan.tex_wrap = 1;
    }

    plan.z_mode = 1;
    if (input.alpha_mode == SourceGltfMiloAlphaMode::kMask) {
      plan.alpha_cut = true;
      plan.alpha_threshold = static_cast<int>(input.alpha_cutoff * 255.0f);
      plan.blend = 1;
    } else if (input.image_has_alpha) {
      plan.alpha_cut = false;
      plan.alpha_write = true;
      plan.blend = 3;
    } else {
      plan.alpha_cut = false;
      plan.blend = 1;
    }

    plan.texture_compression = input.image_has_alpha ? 3 : 1;
    plan.emissive_multiplier = 1.0f;
    plan.normal_detail_tiling = 1.0f;
    plan.rim_power_zeroed_before_final = true;
    plan.rim_power = 0.0f;
    plan.specular2_power = 0.0f;
    plan.rim_rgb_zeroed = true;
    plan.rim_rgb = {0.0f, 0.0f, 0.0f, 0.0f};
    plan.rim_power_final_overrides_zero = true;
    plan.rim_power = 4.0f;
    plan.specular_power = 0.0f;
    plan.specular2_power = 0.0f;
    plan.obj_fields_revision2 = true;
  }

  if (input.has_normal_texture) {
    plan.creates_normal_tex_entry = true;
    plan.normal_map = input.name + "_norm.tex";
    plan.normal_tex_entry_name = input.name + "_norm.tex";
    plan.normal_texture_external_path = input.name + "_norm.png";
    plan.normal_compression_format = xbox_platform ? "BC5" : "BC1";
    plan.normal_bitmap_encoding =
        xbox_platform ? "ATI2_BC5" : "DXT1_BC1";
    plan.normal_mip_map_k = -8.0f;
    plan.normal_type_regular = true;
    plan.normal_optimize_for_ps3 = true;
    plan.normal_bitmap_mip_maps_zero = true;
    plan.normal_bpl_width_bpp_over_8 = true;
    plan.normal_xbox_byte_swap = xbox_platform;
  }

  if (input.has_emissive_texture) {
    plan.creates_emissive_tex_entry = true;
    plan.emissive_map = input.name + "_emissive.tex";
    plan.emissive_tex_entry_name = input.name + "_emissive.tex";
    plan.emissive_texture_external_path = input.name + "_emissive.png";
    plan.emissive_compression_format = "BC1";
    plan.emissive_bitmap_encoding = "DXT1_BC1";
    plan.emissive_mip_map_k = -8.0f;
    plan.emissive_type_regular = true;
    plan.emissive_optimize_for_ps3 = !xbox_platform;
    plan.emissive_bitmap_mip_maps_zero = true;
    plan.emissive_bpl_width_bpp_over_8 = true;
    plan.emissive_xbox_byte_swap = xbox_platform;
    plan.emissive_multiplier = 1.0f;
  }

  if (input.has_specular_color_texture) {
    plan.creates_specular_tex_entry = true;
    plan.specular_map = input.name + "_spec.tex";
    plan.specular_tex_entry_name = input.name + "_spec.tex";
    plan.specular_texture_external_path = input.name + "_spec.png";
    plan.specular_compression_format = "BC3";
    plan.specular_bitmap_encoding = "DXT5_BC3";
    plan.specular_mip_map_k = -8.0f;
    plan.specular_type_regular = true;
    plan.specular_optimize_for_ps3 = !xbox_platform;
    plan.specular_bitmap_mip_maps_zero = true;
    plan.specular_bpl_width_bpp_over_8 = true;
    plan.specular_xbox_byte_swap = xbox_platform;
  }

  if (input.has_specular_color) {
    plan.has_specular_rgb = true;
    plan.specular_rgb = input.specular_color;
  }

  if (input.has_specular_factor) {
    plan.specular_power = input.specular_factor;
  }

  if (input.extras.present) {
    plan.extras_applied = true;
    if (input.prelit_option_empty) plan.pre_lit = input.extras.prelit == 1;
    plan.alpha_cut = input.extras.alpha_cut == 1;
    plan.alpha_threshold = static_cast<int>(input.extras.alpha_threshold);
    plan.alpha_write = input.extras.alpha_write == 1;
    plan.z_mode = input.extras.z_mode;
    plan.blend = input.extras.blend_mode;
    plan.use_environment = input.extras.use_environment == 1;
    plan.emissive_multiplier = input.extras.emissive_multiplier;
    plan.cull = input.extras.cull == 1;
    plan.point_lights = input.extras.point_lights == 1;
    plan.extras_projected_lights_declared = true;
    plan.extras_projected_lights = input.extras.projected_lights;
    plan.extras_material_type_declared = true;
    plan.extras_material_type = input.extras.material_type;
    plan.normal_detail_map = input.extras.normal_detail_map;
    plan.shader_variation = input.extras.shader_variation;
  }

  return plan;
}

SourceGltfMiloPrelitOptionPlan source_gltf_milo_prelit_option_plan(
    const SourceGltfMiloPrelitOptionInput& input) {
  SourceGltfMiloPrelitOptionPlan plan;
  plan.raw_prelit = input.raw_prelit;
  plan.normalized_prelit = input.raw_prelit;
  std::transform(plan.normalized_prelit.begin(), plan.normalized_prelit.end(),
                 plan.normalized_prelit.begin(), [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });

  plan.base_color_branch_entered = input.has_base_color_texture;
  if (plan.base_color_branch_entered && input.raw_prelit != "false") {
    plan.base_branch_sets_pre_lit = true;
    plan.final_pre_lit = true;
  }

  plan.extras_branch_reads_prelit =
      input.extras_present && plan.normalized_prelit.empty();
  if (plan.extras_branch_reads_prelit) {
    plan.extras_branch_sets_pre_lit = input.extras_prelit == 1;
    plan.final_pre_lit = plan.extras_branch_sets_pre_lit;
  }

  return plan;
}

SourceGltfMiloTextureTempOutputPlan
source_gltf_milo_texture_temp_output_plan(
    const SourceGltfMiloTextureTempOutputInput& input) {
  SourceGltfMiloTextureTempOutputPlan plan;
  int32_t curmat = input.curmat;
  plan.curmat_start = curmat;

  auto record_temp_path = [&](const std::string& path) {
    plan.convert_temp_paths.push_back(path);
    plan.parse_temp_paths.push_back(path);
    plan.delete_temp_paths.push_back(path);
  };

  if (input.has_base_color_texture) {
    ++curmat;
    plan.base_texture_increments_curmat = true;
    record_temp_path("output_" + std::to_string(curmat) + ".dds");
  }

  plan.curmat_after_base = curmat;
  const std::string prefix = "output_" + std::to_string(curmat);
  if (input.has_normal_texture) record_temp_path(prefix + "_norm.dds");
  if (input.has_emissive_texture) record_temp_path(prefix + "_emissive.dds");
  if (input.has_specular_color_texture) record_temp_path(prefix + "_spec.dds");

  plan.curmat_final = curmat;
  plan.side_maps_reuse_current_curmat = plan.curmat_after_base == curmat;
  return plan;
}

SourceGltfMiloXboxTextureByteSwapResult
source_gltf_milo_xbox_texture_byte_swap(const std::vector<uint8_t>& pixels) {
  SourceGltfMiloXboxTextureByteSwapResult result;
  result.input_size_multiple_of_four = pixels.size() % 4 == 0;
  result.would_index_past_end = !result.input_size_multiple_of_four;
  if (result.would_index_past_end) return result;

  result.bytes.reserve(pixels.size());
  for (size_t i = 0; i < pixels.size(); i += 4) {
    result.bytes.push_back(pixels[i + 1]);
    result.bytes.push_back(pixels[i]);
    result.bytes.push_back(pixels[i + 3]);
    result.bytes.push_back(pixels[i + 2]);
  }
  return result;
}

SourceGltfMiloMaterialRuntimeBoundary
source_gltf_milo_material_runtime_boundary() {
  SourceGltfMiloMaterialRuntimeBoundary boundary;
  boundary.source_authorities = {
      "glTFMilo/Source/glTFMilo/Program.cs",
      "MiloEditor/MiloLib/Assets/Rnd/RndMat.cs",
      "rb3/src/system/rndobj/Mat.cpp"};
  boundary.forbidden_runtime_edits = {
      "depth_priority",
      "material_sort",
      "blend_or_z_rewrite",
      "synthesized_skin_indices"};
  return boundary;
}

SourceGltfMiloRunOptionsPlan source_gltf_milo_run_options_plan(
    const SourceGltfMiloRunOptionsInput& input) {
  SourceGltfMiloRunOptionsPlan plan;
  std::string platform = input.platform;
  std::transform(platform.begin(), platform.end(), platform.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (platform == "xbox" || platform == "ps3") {
    plan.normalized_platform = platform;
  } else {
    plan.normalized_platform = "xbox";
    plan.warns_invalid_platform = true;
  }

  std::string game_arg = input.game_arg;
  std::transform(game_arg.begin(), game_arg.end(), game_arg.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (game_arg == "tbrb") {
    plan.selected_game = SourceGltfMiloGame::kTheBeatlesRockBand;
  } else if (game_arg == "rb3" || game_arg.empty()) {
    plan.selected_game = SourceGltfMiloGame::kRockBand3;
  } else if (game_arg == "rb2") {
    plan.selected_game = SourceGltfMiloGame::kRockBand2;
  } else {
    plan.selected_game = SourceGltfMiloGame::kRockBand3;
    plan.warns_invalid_game = true;
  }

  plan.character_directory_type =
      input.type == SourceGltfMiloSceneType::kCharacter ||
      input.type == SourceGltfMiloSceneType::kInstrument ||
      input.type == SourceGltfMiloSceneType::kDancer;
  plan.convert_world_coordinates = !plan.character_directory_type;
  plan.meta_type = plan.character_directory_type ? "Character" : "RndDir";
  return plan;
}

SourceGltfMiloRunTypePlan source_gltf_milo_run_type_plan(
    const SourceGltfMiloRunTypeInput& input) {
  SourceGltfMiloRunTypePlan plan;
  plan.raw_type = input.raw_type;
  plan.normalized_type = input.raw_type;
  std::transform(plan.normalized_type.begin(), plan.normalized_type.end(),
                 plan.normalized_type.begin(), [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  const auto is_character_family = [](const std::string& value) {
    return value == "character" || value == "instrument" ||
           value == "dancer";
  };
  plan.normalized_character_family =
      is_character_family(plan.normalized_type);
  plan.convert_world_coordinates = !plan.normalized_character_family;
  plan.meta_type_character = is_character_family(input.raw_type);
  plan.meta_type = plan.meta_type_character ? "Character" : "RndDir";
  plan.calls_character_directory_builder = is_character_family(input.raw_type);
  plan.calls_rnd_directory_builder = !plan.calls_character_directory_builder;
  return plan;
}

SourceGltfMiloRunPreflightPlan source_gltf_milo_run_preflight_plan(
    const SourceGltfMiloRunPreflightInput& input) {
  SourceGltfMiloRunPreflightPlan plan;
  plan.normalized_outfit_config_path = input.outfit_config_path;
  std::transform(plan.normalized_outfit_config_path.begin(),
                 plan.normalized_outfit_config_path.end(),
                 plan.normalized_outfit_config_path.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (!input.input_file_exists) {
    plan.exits_missing_input = true;
    return plan;
  }

  auto ends_with = [](const std::string& value, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len &&
           value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
  };

  plan.accepts_gltf_extension = ends_with(input.input_path, ".gltf");
  plan.accepts_glb_extension = ends_with(input.input_path, ".glb");
  if (!plan.accepts_gltf_extension && !plan.accepts_glb_extension) {
    plan.exits_non_gltf_extension = true;
    return plan;
  }

  if (!plan.normalized_outfit_config_path.empty()) {
    plan.checks_outfit_config_exists = true;
    if (!input.outfit_config_exists) {
      plan.exits_missing_outfit_config = true;
      return plan;
    }
  }

  plan.reaches_model_load = true;
  return plan;
}

SourceGltfMiloBaseMeshPlan source_gltf_milo_create_base_mesh_plan(
    const SourceGltfMiloBaseMeshInput& input) {
  SourceGltfMiloBaseMeshPlan plan;
  plan.calls_milo_editor_rndmesh_new = true;
  plan.mesh_revision = input.model_revision;
  plan.mesh_alt_revision = 0;
  plan.factory_requested_zero_vertices = true;
  plan.factory_requested_zero_faces = true;
  plan.factory_ignores_requested_counts = true;
  plan.object_fields_revision = 2;
  plan.trans_revision = 9;
  plan.parent_name = input.parent_name;
  plan.copies_local_matrix = true;
  plan.copies_world_matrix = true;
  plan.drawable_revision = 3;
  plan.initializes_draw_sphere = true;
  plan.draw_sphere_radius = 0.0f;
  plan.volume_triangles = true;
  plan.keep_mesh_data = true;
  plan.has_ao_calculation = false;

  const bool has_next_gen_vertex_branch =
      input.game == SourceGltfMiloGame::kRockBand3 ||
      input.game == SourceGltfMiloGame::kDanceCentral1;
  if (has_next_gen_vertex_branch && input.platform == "xbox") {
    plan.vertices_is_next_gen = true;
    plan.vertex_compression_type = 1;
    plan.vertex_size = 36;
  } else if (has_next_gen_vertex_branch && input.platform == "ps3") {
    plan.vertices_is_next_gen = true;
    plan.vertex_compression_type = 2;
    plan.vertex_size = 40;
  }

  if (input.has_material) {
    plan.binds_material = true;
    plan.material_name = input.material_name + ".mat";
    plan.logs_missing_diffuse_or_maps =
        !input.material_has_diffuse ||
        (!input.material_has_diffuse && !input.material_has_normal &&
         !input.material_has_specular);
    if (input.material_has_normal) plan.has_ao_calculation = true;
  }
  return plan;
}

SourceGltfMiloSceneAssemblyPlan source_gltf_milo_scene_assembly_plan(
    const SourceGltfMiloSceneAssemblyInput& input) {
  SourceGltfMiloSceneAssemblyPlan plan;
  plan.band_configuration.entry_name = input.filename;

  if (input.type == SourceGltfMiloSceneType::kVenue) {
    SourceGltfMiloVenueAllGeomGroupPlan& group =
        plan.venue_all_geom_group;
    group.creates_group = true;
    group.entry_type = "Group";
    group.entry_name = input.filename + "_geom.grp";
    group.group_revision = input.group_revision;
    group.trans_revision = input.trans_revision;
    group.drawable_revision = input.drawable_revision;
    group.initializes_draw_sphere = true;
    group.draw_sphere_radius = 0.0f;
    group.animatable_revision = input.animatable_revision;
    group.object_fields_revision = 2;
    for (const SourceGltfMiloDirectoryEntryInput& entry :
         input.existing_entries) {
      if (entry.type == "Mesh") {
        group.objects.push_back(entry.name);
      }
    }
  }

  plan.calls_character_directory_builder =
      input.type == SourceGltfMiloSceneType::kCharacter ||
      input.type == SourceGltfMiloSceneType::kInstrument ||
      input.type == SourceGltfMiloSceneType::kDancer;
  plan.calls_rnd_directory_builder = !plan.calls_character_directory_builder;
  return plan;
}

SourceGltfMiloReportGeneratorPlan source_gltf_milo_report_generator_plan(
    const SourceGltfMiloReportGeneratorInput& input) {
  SourceGltfMiloReportGeneratorPlan plan;
  plan.raw_report = input.raw_report;
  plan.normalized_report = input.raw_report;
  std::transform(plan.normalized_report.begin(), plan.normalized_report.end(),
                 plan.normalized_report.begin(), [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  plan.report_type_arg = input.normalized_type;
  plan.calls_report_generator = plan.normalized_report == "true";
  plan.passes_meta = plan.calls_report_generator;
  plan.passes_selected_game = plan.calls_report_generator;
  return plan;
}

SourceGltfMiloNodeTraversalPlan source_gltf_milo_node_traversal_plan(
    const SourceGltfMiloNodeTraversalInput& input) {
  SourceGltfMiloNodeTraversalPlan plan;
  switch (input.kind) {
    case SourceGltfMiloNodeTraversalKind::kMesh:
      if (!input.mesh_present) {
        plan.aborts_meshless_mesh_node = true;
        return plan;
      }
      plan.calls_create_base_mesh = true;
      plan.calls_populate_mesh_chunk = true;
      for (const std::string& joint_name : input.chunk_joint_names) {
        if (source_gltf_milo_is_hair_bone_name(joint_name)) {
          plan.hair_strand_bones_added.push_back(joint_name);
        }
      }
      break;
    case SourceGltfMiloNodeTraversalKind::kBone:
      plan.calls_process_bone_node = true;
      if (source_gltf_milo_is_hair_bone_name(input.node_name) &&
          input.node_extras_present) {
        plan.tries_hair_settings_detection = true;
        plan.sets_detected_hair_settings =
            input.extras_contains_hair_marker && input.parsed_hair_settings &&
            !input.settings_already_detected;
      }
      break;
    case SourceGltfMiloNodeTraversalKind::kGroup:
      plan.calls_process_group_node = true;
      break;
    case SourceGltfMiloNodeTraversalKind::kLight:
      plan.calls_process_light_node = true;
      break;
    case SourceGltfMiloNodeTraversalKind::kOther:
      plan.ignores_node = true;
      break;
  }

  const bool has_hair_strand_bones =
      input.has_hair_strand_bones_before ||
      !plan.hair_strand_bones_added.empty();
  if (has_hair_strand_bones) {
    plan.calls_process_char_hair_after_traversal = true;
    plan.calls_process_empty_hair_collides_after_traversal = true;
    plan.split_strands_at_branches = !input.disable_splitting;
  }
  return plan;
}

SourceGltfMiloBoneNodePlan source_gltf_milo_process_bone_node_plan(
    const SourceGltfMiloBoneNodeInput& input) {
  SourceGltfMiloBoneNodePlan plan;
  if (input.name == "neutral_bone") {
    plan.skipped_neutral_bone = true;
    return plan;
  }
  const bool is_rb3_skeleton_bone =
      input.is_rb3_skeleton_bone ||
      source_gltf_milo_is_rb3_skeleton_bone_name(input.name);
  if (input.type == "character" && is_rb3_skeleton_bone) {
    plan.skipped_character_rb3_skeleton_bone = true;
    return plan;
  }

  plan.creates_trans_entry = true;
  plan.entry_type = "Trans";
  plan.entry_name = input.name;
  plan.trans_revision = 9;
  plan.object_fields_revision = 2;
  plan.copies_local_matrix = true;
  plan.copies_world_matrix = true;
  plan.parent_name =
      input.has_parent_bone ? input.parent_bone : input.fallback_parent;
  return plan;
}

std::vector<std::string> source_gltf_milo_rb3_skeleton_bone_names() {
  return source_rb3_skeleton_bone_names();
}

std::size_t source_gltf_milo_rb3_skeleton_bone_name_count() {
  return source_rb3_skeleton_bone_names().size();
}

bool source_gltf_milo_is_rb3_skeleton_bone_name(const std::string& name) {
  const auto& names = source_rb3_skeleton_bone_names();
  return std::find(names.begin(), names.end(), name) != names.end();
}

SourceGltfMiloGroupNodePlan source_gltf_milo_process_group_node_plan(
    const SourceGltfMiloGroupNodeInput& input) {
  SourceGltfMiloGroupNodePlan plan;
  if (input.name == "Armature") {
    plan.skipped_armature = true;
    return plan;
  }

  plan.creates_group_entry = true;
  plan.entry_type = "Group";
  plan.entry_name = input.name + ".grp";
  plan.group_revision = input.group_revision;
  plan.object_fields_revision = 2;
  plan.trans_revision = input.trans_revision;
  plan.drawable_revision = input.drawable_revision;
  plan.animatable_revision = input.animatable_revision;
  plan.copies_local_matrix = true;
  plan.copies_world_matrix = true;
  plan.calls_milo_extras_add_to_group = true;
  for (const std::string& child : input.descendant_names) {
    if (!child.empty()) plan.objects.push_back(child);
  }
  return plan;
}

SourceGltfMiloLightNodePlan source_gltf_milo_process_light_node_plan(
    const SourceGltfMiloLightNodeInput& input) {
  SourceGltfMiloLightNodePlan plan;
  plan.creates_light_entry = true;
  plan.entry_type = "Light";
  plan.entry_name = input.name + ".lit";
  plan.light_revision = input.light_revision;
  plan.object_fields_revision = 2;
  plan.range = input.range;
  plan.color_owner = input.name + ".lit";
  plan.color = {input.color[0], input.color[1], input.color[2], 1.0f};
  if (input.punctual_light_type == "Spot") {
    plan.light_type = "kSpot";
  } else if (input.punctual_light_type == "Directional") {
    plan.light_type = "kDirectional";
  } else {
    plan.light_type = "kPoint";
  }
  plan.trans_revision = input.trans_revision;
  plan.copies_local_matrix = true;
  plan.copies_world_matrix = true;
  plan.calls_milo_extras_add_to_object = true;
  return plan;
}

SourceRndLightDefaultState source_rndlight_default_state() {
  return SourceRndLightDefaultState{};
}

SourceRndLightSavePlan source_rndlight_save_plan() {
  return SourceRndLightSavePlan{};
}

SourceRndLightLoadPlan source_rndlight_load_plan(
    int32_t revision,
    int32_t alt_revision,
    int32_t serialized_type) {
  SourceRndLightLoadPlan plan;
  plan.revision = revision;
  plan.alt_revision = alt_revision;
  plan.accepted_revision = revision >= 0 && revision <= 16;
  plan.accepted_alt_revision = alt_revision >= 0 && alt_revision <= 1;
  if (!plan.accepted_revision || !plan.accepted_alt_revision) return plan;

  plan.reads_object_fields = revision > 3;
  plan.reads_legacy_colors = revision < 2;
  plan.reads_legacy_pre_range_ints = revision < 3;
  plan.reads_legacy_post_range_ints = revision < 3;
  plan.reads_type = revision != 0;
  plan.serialized_type = serialized_type;
  plan.effective_type = serialized_type;
  if (revision != 0 && revision < 0xE && plan.effective_type > 1) {
    --plan.effective_type;
    plan.legacy_type_decrements_above_one = true;
  }
  plan.reads_falloff_start = revision > 0xB;
  plan.reads_animate_color_position = revision > 5;
  plan.reads_top_bot_radius = revision > 6;
  plan.reads_legacy_radius_ints = revision > 6 && revision < 0xE;
  plan.reads_texture = revision > 7;
  plan.reads_rev9_shadow_draw_list = revision == 9;
  plan.reads_rev8_shadow_draw_ptr = revision == 8;
  plan.reads_color_owner = revision > 10;
  plan.null_color_owner_defaults_to_self = revision > 10;
  plan.reads_texture_xfm = revision > 0xC;
  plan.reads_legacy_texture_ptr = revision > 0xD;
  plan.reads_only_projection = alt_revision != 0;
  plan.reads_shadow_objects = revision > 0xE;
  plan.reads_projected_blend = revision > 0xE;
  plan.reads_animate_range = revision > 0xF;
  plan.animate_range_defaults_from_color = revision <= 0xF;
  return plan;
}

SourceRndLightCopyPlan source_rndlight_copy_plan(
    bool copy_type_shallow,
    bool copy_type_from_max,
    bool source_color_owner_self) {
  SourceRndLightCopyPlan plan;
  plan.superclasses = {"Hmx::Object", "RndTransformable"};
  plan.copied_members = {
      "mColor",       "mType",        "mAnimateColorFromPreset",
      "mAnimatePositionFromPreset",   "mAnimateRangeFromPreset",
      "mFalloffStart", "mTopRadius",  "mBotRadius",
      "mTexture",    "mShadowOverride",
      "mShadowObjects", "mProjectedBlend"};
  plan.copy_type_shallow = copy_type_shallow;
  plan.copy_type_from_max = copy_type_from_max;
  plan.source_color_owner_self = source_color_owner_self;
  plan.copies_range = !copy_type_from_max;
  plan.copies_color_owner =
      copy_type_shallow || (copy_type_from_max && !source_color_owner_self);
  if (!plan.copies_color_owner) {
    plan.resets_color_owner_to_self = true;
    plan.copies_color_in_owner_fallback = true;
  }
  return plan;
}

SourceRndLightReplacePlan source_rndlight_replace_plan(
    bool color_owner_matches_from,
    bool replacement_is_light) {
  SourceRndLightReplacePlan plan;
  plan.color_owner_matches_from = color_owner_matches_from;
  plan.replacement_is_light = replacement_is_light;
  if (color_owner_matches_from) {
    if (replacement_is_light) {
      plan.copies_replacement_color_owner = true;
    } else {
      plan.resets_color_owner_to_self = true;
    }
  }
  return plan;
}

SourceRndLightIntensityPlan source_rndlight_intensity_plan(
    std::array<float, 3> color) {
  SourceRndLightIntensityPlan plan;
  plan.color = color;
  plan.intensity = std::max(1.0f, std::max(color[0], std::max(color[1], color[2])));
  return plan;
}

SourceRndLightHandlerPlan source_rndlight_handler_plan() {
  SourceRndLightHandlerPlan plan;
  plan.actions = {"set_showing:SetShowing(_msg->Int(2))"};
  plan.superclasses = {"RndTransformable", "Hmx::Object"};
  return plan;
}

SourceRndLightPropSyncPlan source_rndlight_prop_sync_plan() {
  SourceRndLightPropSyncPlan plan;
  plan.props = {"animate_color_from_preset",
                "animate_position_from_preset",
                "animate_range_from_preset",
                "color_owner",
                "texture",
                "texture_xfm",
                "only_projection",
                "shadow_objects"};
  plan.set_props = {"type",       "range",     "falloff_start",
                    "color",      "intensity", "topradius",
                    "botradius",  "projected_blend"};
  plan.superclasses = {"RndTransformable"};
  return plan;
}

SourceRndFurSavePlan source_rndfur_save_plan() {
  return SourceRndFurSavePlan{};
}

SourceRndFurCopyPlan source_rndfur_copy_plan() {
  SourceRndFurCopyPlan plan;
  plan.superclasses = {"Hmx::Object"};
  return plan;
}

SourceRndFurLoadPlan source_rndfur_load_plan(
    int32_t revision,
    int32_t alt_revision) {
  SourceRndFurLoadPlan plan;
  plan.revision = revision;
  plan.alt_revision = alt_revision;
  plan.accepted_revision = revision >= 0 && revision <= 3;
  plan.accepted_alt_revision = alt_revision == 0;
  if (!plan.accepted_revision || !plan.accepted_alt_revision) return plan;

  plan.reads_object = true;
  plan.reads_base_filler_block = true;
  plan.reads_rev2_extra_fillers = revision > 1;
  plan.reads_second_filler_block = true;
  plan.reads_base_tint = true;
  plan.reads_end_tint = true;
  plan.reads_fur_detail_tex = true;
  plan.reads_fur_tiling = true;
  plan.reads_wind = revision > 2;

  plan.read_order = {"LOAD_REVS",
                     "Hmx::Object",
                     "filler2",
                     "filler1",
                     "filler1"};
  if (plan.reads_rev2_extra_fillers) {
    plan.read_order.push_back("rev2_filler1");
    plan.read_order.push_back("rev2_filler1");
  }
  plan.read_order.push_back("filler1");
  plan.read_order.push_back("filler1");
  plan.read_order.push_back("filler1");
  plan.read_order.push_back("filler1");
  plan.read_order.push_back("base_tint");
  plan.read_order.push_back("end_tint");
  plan.read_order.push_back("fur_detail_tex");
  plan.read_order.push_back("fur_tiling");
  if (plan.reads_wind) plan.read_order.push_back("wind");
  return plan;
}

SourceRndFurHandlerPlan source_rndfur_handler_plan() {
  SourceRndFurHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  return plan;
}

SourceRndFurPropSyncPlan source_rndfur_prop_sync_plan() {
  return SourceRndFurPropSyncPlan{};
}

SourceRndFurRuntimeBoundary source_rndfur_runtime_boundary() {
  return SourceRndFurRuntimeBoundary{};
}

SourceRndFurRb2DumpLayout source_rndfur_rb2_dump_layout() {
  SourceRndFurRb2DumpLayout layout;
  layout.members = {"mNumPasses",  "mThickness", "mCurvature",
                    "mShellOut",   "mAlphaFalloff",
                    "mStretch",    "mSlide",     "mGravity",
                    "mFluidity",   "mBaseTint",  "mEndTint",
                    "mFurDetail",  "mFurTiling"};
  return layout;
}

SourceGltfMiloTransAnimExportPlan
source_gltf_milo_export_trans_anim_plan(
    const std::string& anim_name,
    const std::vector<SourceGltfMiloTransAnimChannelInput>& channels,
    int animatable_revision,
    int drawable_revision,
    bool convert_world_coordinates) {
  SourceGltfMiloTransAnimExportPlan plan;
  plan.has_channels = !channels.empty();
  if (!plan.has_channels) return plan;

  auto is_transform_path = [](const std::string& path) {
    return path == "translation" || path == "rotation" || path == "scale";
  };
  plan.transform_only = std::all_of(
      channels.begin(), channels.end(),
      [&](const SourceGltfMiloTransAnimChannelInput& channel) {
        return is_transform_path(channel.target_path);
      });
  if (!plan.transform_only) return plan;

  const std::string target_node = channels.front().target_node;
  for (const SourceGltfMiloTransAnimChannelInput& channel : channels) {
    if (channel.target_node != target_node) {
      plan.logs_mismatched_target = true;
      plan.mismatched_target_nodes.push_back(channel.target_node);
    }
  }

  plan.creates_trans_anim = true;
  plan.uses_reflection_revision = true;
  plan.trans_anim_revision = 7;
  plan.animatable_revision = animatable_revision;
  plan.anim_rate_30_fps = true;
  plan.drawable_revision = drawable_revision;
  plan.draw_sphere_radius = 0.0f;
  plan.trans_target = target_node + ".mesh";
  plan.keys_owner = anim_name + ".tnm";
  plan.object_fields_revision = 2;
  plan.entry_type = "TransAnim";
  plan.entry_name = anim_name + ".tnm";

  for (const SourceGltfMiloTransAnimChannelInput& channel : channels) {
    const int32_t count = std::max(0, channel.linear_key_count);
    plan.processed_channel_paths.push_back(channel.target_path);
    if (channel.target_path == "translation") {
      plan.translation_key_count += count;
      plan.converts_translation_keys |= convert_world_coordinates && count > 0;
    } else if (channel.target_path == "rotation") {
      plan.rotation_key_count += count;
      plan.converts_rotation_keys |= convert_world_coordinates && count > 0;
    } else if (channel.target_path == "scale") {
      plan.scale_key_count += count;
      plan.converts_scale_keys |= convert_world_coordinates && count > 0;
    }
  }
  return plan;
}

SourceGltfMiloBuildTrianglesResult source_gltf_milo_build_source_triangles(
    const std::vector<uint32_t>& indices,
    int32_t position_count,
    bool has_index_buffer) {
  SourceGltfMiloBuildTrianglesResult result;
  if (position_count <= 0) return result;
  if (!has_index_buffer || indices.empty()) {
    if (position_count % 3 != 0) {
      result.warned_unindexed_trailing_vertices = true;
      result.ignored_trailing_vertices = position_count % 3;
    }
    for (int32_t i = 0; i + 2 < position_count; i += 3) {
      result.triangles.push_back(
          {static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1),
           static_cast<uint32_t>(i + 2)});
    }
    return result;
  }

  result.used_index_buffer = true;
  if (indices.size() % 3 != 0) {
    result.warned_index_count_not_multiple_of_three = true;
    result.ignored_trailing_indices = static_cast<int32_t>(indices.size() % 3);
  }

  const uint32_t max_position_index = static_cast<uint32_t>(position_count);
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const uint32_t idx0 = indices[i + 0];
    const uint32_t idx1 = indices[i + 1];
    const uint32_t idx2 = indices[i + 2];
    if (idx0 >= max_position_index || idx1 >= max_position_index ||
        idx2 >= max_position_index) {
      result.warned_invalid_index = true;
      ++result.ignored_invalid_triangles;
      continue;
    }
    result.triangles.push_back({idx0, idx1, idx2});
  }
  return result;
}

namespace {

struct SourceGltfMiloMeshChunkBuilder {
  std::vector<int32_t> triangle_indices;
  std::vector<int32_t> joint_indices;
  std::unordered_set<int32_t> joint_set;
  std::unordered_set<uint32_t> vertex_set;

  int32_t additional_joint_count(const std::vector<int32_t>& joints) const {
    int32_t count = 0;
    for (int32_t joint : joints) {
      if (joint_set.find(joint) == joint_set.end()) ++count;
    }
    return count;
  }

  int32_t additional_vertex_count(const SourceGltfMiloTriangle& tri) const {
    int32_t count = 0;
    if (vertex_set.find(tri.idx0) == vertex_set.end()) ++count;
    if (tri.idx1 != tri.idx0 &&
        vertex_set.find(tri.idx1) == vertex_set.end()) {
      ++count;
    }
    if (tri.idx2 != tri.idx0 && tri.idx2 != tri.idx1 &&
        vertex_set.find(tri.idx2) == vertex_set.end()) {
      ++count;
    }
    return count;
  }

  bool can_add_triangle(const SourceGltfMiloTriangle& tri,
                        const std::vector<int32_t>& joints,
                        int32_t max_joint_count,
                        int32_t max_vertex_count) const {
    return static_cast<int32_t>(joint_indices.size()) +
                   additional_joint_count(joints) <=
               max_joint_count &&
           static_cast<int32_t>(vertex_set.size()) +
                   additional_vertex_count(tri) <=
               max_vertex_count;
  }

  void add_triangle(int32_t triangle_index,
                    const SourceGltfMiloTriangle& tri,
                    const std::vector<int32_t>& joints) {
    triangle_indices.push_back(triangle_index);
    vertex_set.insert(tri.idx0);
    vertex_set.insert(tri.idx1);
    vertex_set.insert(tri.idx2);
    for (int32_t joint : joints) {
      if (joint_set.insert(joint).second) joint_indices.push_back(joint);
    }
  }

  SourceGltfMiloMeshChunk finish() const {
    SourceGltfMiloMeshChunk chunk;
    chunk.triangle_indices = triangle_indices;
    chunk.joint_indices = joint_indices;
    chunk.unique_vertex_count = static_cast<int32_t>(vertex_set.size());
    return chunk;
  }
};

std::vector<int32_t> source_gltf_milo_triangle_joint_indices(
    const SourceGltfMiloTriangle& tri,
    const std::vector<std::vector<int32_t>>& vertex_joint_indices) {
  std::vector<int32_t> joints;
  std::unordered_set<int32_t> seen;
  const uint32_t vertices[3] = {tri.idx0, tri.idx1, tri.idx2};
  for (uint32_t vertex : vertices) {
    if (vertex >= vertex_joint_indices.size()) continue;
    for (int32_t joint : vertex_joint_indices[vertex]) {
      if (seen.insert(joint).second) joints.push_back(joint);
    }
  }
  return joints;
}

}  // namespace

SourceGltfMiloMeshChunkBuilderPlan source_gltf_milo_mesh_chunk_builder_plan(
    const SourceGltfMiloMeshChunkBuilderInput& input) {
  SourceGltfMiloMeshChunkBuilder builder;
  builder.triangle_indices = input.existing_triangle_indices;
  for (int32_t joint : input.existing_joint_indices) {
    if (builder.joint_set.insert(joint).second) {
      builder.joint_indices.push_back(joint);
    }
  }
  for (uint32_t vertex : input.existing_vertex_indices) {
    builder.vertex_set.insert(vertex);
  }

  SourceGltfMiloMeshChunkBuilderPlan plan;
  plan.starting_joint_count =
      static_cast<int32_t>(builder.joint_indices.size());
  plan.starting_unique_vertex_count =
      static_cast<int32_t>(builder.vertex_set.size());
  plan.additional_joint_count =
      builder.additional_joint_count(input.triangle_joint_indices);
  plan.additional_vertex_count =
      builder.additional_vertex_count(input.triangle);
  plan.can_add_triangle =
      builder.can_add_triangle(input.triangle, input.triangle_joint_indices,
                               input.max_joint_count, input.max_vertex_count);

  if (input.add_triangle) {
    builder.add_triangle(input.triangle_index, input.triangle,
                         input.triangle_joint_indices);
  }

  const SourceGltfMiloMeshChunk chunk = builder.finish();
  plan.triangle_indices_after_add = chunk.triangle_indices;
  plan.joint_indices_after_add = chunk.joint_indices;
  plan.unique_vertex_count_after_add = chunk.unique_vertex_count;
  return plan;
}

SourceGltfMiloMeshChunkPlan source_gltf_milo_split_mesh_chunks(
    const std::vector<SourceGltfMiloTriangle>& triangles,
    const std::vector<std::vector<int32_t>>& vertex_joint_indices) {
  SourceGltfMiloMeshChunkPlan plan;
  constexpr int32_t kMaxMeshInfluencingBones = 40;
  constexpr int32_t kMaxMeshVertices = 65535;
  plan.max_influencing_bones = kMaxMeshInfluencingBones;
  plan.max_vertices = kMaxMeshVertices;

  std::vector<std::vector<int32_t>> triangle_joints;
  triangle_joints.reserve(triangles.size());
  for (size_t i = 0; i < triangles.size(); ++i) {
    triangle_joints.push_back(source_gltf_milo_triangle_joint_indices(
        triangles[i], vertex_joint_indices));
    if (triangle_joints.back().size() >
        static_cast<size_t>(kMaxMeshInfluencingBones)) {
      plan.source_limits_exceeded = true;
      plan.rejected_triangle_indices.push_back(static_cast<int32_t>(i));
    }
  }
  if (plan.source_limits_exceeded) return plan;

  SourceGltfMiloMeshChunkBuilder full_mesh_chunk;
  for (size_t i = 0; i < triangles.size(); ++i) {
    full_mesh_chunk.add_triangle(static_cast<int32_t>(i), triangles[i],
                                 triangle_joints[i]);
  }
  if (full_mesh_chunk.joint_indices.size() <=
          static_cast<size_t>(kMaxMeshInfluencingBones) &&
      full_mesh_chunk.vertex_set.size() <=
          static_cast<size_t>(kMaxMeshVertices)) {
    plan.chunks.push_back(full_mesh_chunk.finish());
    return plan;
  }

  std::map<std::pair<uint32_t, uint32_t>, std::vector<int32_t>>
      edge_to_triangle_indices;
  auto add_edge = [&](uint32_t a, uint32_t b, int32_t triangle_index) {
    if (b < a) std::swap(a, b);
    edge_to_triangle_indices[{a, b}].push_back(triangle_index);
  };
  for (size_t i = 0; i < triangles.size(); ++i) {
    const SourceGltfMiloTriangle& tri = triangles[i];
    const int32_t triangle_index = static_cast<int32_t>(i);
    add_edge(tri.idx0, tri.idx1, triangle_index);
    add_edge(tri.idx1, tri.idx2, triangle_index);
    add_edge(tri.idx2, tri.idx0, triangle_index);
  }

  std::vector<std::set<int32_t>> adjacency_sets(triangles.size());
  for (const auto& edge : edge_to_triangle_indices) {
    const std::vector<int32_t>& edge_triangles = edge.second;
    if (edge_triangles.size() <= 1) continue;
    for (size_t i = 0; i < edge_triangles.size(); ++i) {
      for (size_t j = 0; j < edge_triangles.size(); ++j) {
        if (i != j) {
          adjacency_sets[edge_triangles[i]].insert(edge_triangles[j]);
        }
      }
    }
  }

  std::vector<std::vector<int32_t>> triangle_adjacency;
  triangle_adjacency.reserve(adjacency_sets.size());
  for (const auto& adjacency : adjacency_sets) {
    triangle_adjacency.emplace_back(adjacency.begin(), adjacency.end());
  }

  std::vector<bool> assigned(triangles.size(), false);
  int32_t remaining_triangle_count = static_cast<int32_t>(triangles.size());
  while (remaining_triangle_count > 0) {
    int32_t seed_triangle_index = -1;
    int32_t best_seed_joint_count = -1;
    for (size_t i = 0; i < triangle_joints.size(); ++i) {
      if (assigned[i]) continue;
      const int32_t joint_count =
          static_cast<int32_t>(triangle_joints[i].size());
      if (joint_count > best_seed_joint_count) {
        seed_triangle_index = static_cast<int32_t>(i);
        best_seed_joint_count = joint_count;
      }
    }
    if (seed_triangle_index < 0) break;

    SourceGltfMiloMeshChunkBuilder chunk;
    chunk.add_triangle(seed_triangle_index, triangles[seed_triangle_index],
                       triangle_joints[seed_triangle_index]);
    assigned[seed_triangle_index] = true;
    --remaining_triangle_count;

    std::set<int32_t> frontier;
    for (int32_t adjacent : triangle_adjacency[seed_triangle_index]) {
      if (!assigned[adjacent]) frontier.insert(adjacent);
    }

    while (!frontier.empty()) {
      int32_t best_triangle_index = -1;
      int32_t best_additional_joint_count = INT32_MAX;
      int32_t best_additional_vertex_count = INT32_MAX;
      for (int32_t triangle_index : frontier) {
        if (!chunk.can_add_triangle(triangles[triangle_index],
                                    triangle_joints[triangle_index],
                                    kMaxMeshInfluencingBones,
                                    kMaxMeshVertices)) {
          continue;
        }
        const int32_t additional_joint_count =
            chunk.additional_joint_count(triangle_joints[triangle_index]);
        const int32_t additional_vertex_count =
            chunk.additional_vertex_count(triangles[triangle_index]);
        if (additional_joint_count < best_additional_joint_count ||
            (additional_joint_count == best_additional_joint_count &&
             additional_vertex_count < best_additional_vertex_count)) {
          best_triangle_index = triangle_index;
          best_additional_joint_count = additional_joint_count;
          best_additional_vertex_count = additional_vertex_count;
        }
      }
      if (best_triangle_index < 0) break;

      frontier.erase(best_triangle_index);
      chunk.add_triangle(best_triangle_index, triangles[best_triangle_index],
                         triangle_joints[best_triangle_index]);
      assigned[best_triangle_index] = true;
      --remaining_triangle_count;
      for (int32_t adjacent : triangle_adjacency[best_triangle_index]) {
        if (!assigned[adjacent]) frontier.insert(adjacent);
      }
    }

    int32_t global_search_start = 0;
    while (remaining_triangle_count > 0 &&
           chunk.vertex_set.size() < static_cast<size_t>(kMaxMeshVertices)) {
      int32_t best_triangle_index = -1;
      for (size_t offset = 0; offset < triangles.size(); ++offset) {
        const int32_t triangle_index = static_cast<int32_t>(
            (static_cast<size_t>(global_search_start) + offset) %
            triangles.size());
        if (assigned[triangle_index]) continue;
        if (!chunk.can_add_triangle(triangles[triangle_index],
                                    triangle_joints[triangle_index],
                                    kMaxMeshInfluencingBones,
                                    kMaxMeshVertices)) {
          continue;
        }
        best_triangle_index = triangle_index;
        global_search_start =
            (triangle_index + 1) % static_cast<int32_t>(triangles.size());
        break;
      }
      if (best_triangle_index < 0) break;

      chunk.add_triangle(best_triangle_index, triangles[best_triangle_index],
                         triangle_joints[best_triangle_index]);
      assigned[best_triangle_index] = true;
      --remaining_triangle_count;
    }

    plan.chunks.push_back(chunk.finish());
  }

  return plan;
}

SourceGltfMiloMeshSplitWarningPlan
source_gltf_milo_mesh_split_warning_plan(
    const std::vector<SourceGltfMiloMeshChunk>& chunks,
    int32_t source_vertex_count) {
  SourceGltfMiloMeshSplitWarningPlan plan;
  constexpr int32_t kMaxMeshInfluencingBones = 40;
  constexpr int32_t kMaxMeshVertices = 65535;
  plan.max_influencing_bones = kMaxMeshInfluencingBones;
  plan.max_vertices = kMaxMeshVertices;
  plan.chunk_count = static_cast<int32_t>(chunks.size());
  plan.source_vertex_count = source_vertex_count;
  if (chunks.size() <= 1) return plan;

  std::unordered_set<int32_t> distinct_joints;
  for (const SourceGltfMiloMeshChunk& chunk : chunks) {
    for (int32_t joint : chunk.joint_indices) distinct_joints.insert(joint);
  }
  plan.total_influencing_bone_count =
      static_cast<int32_t>(distinct_joints.size());

  if (plan.total_influencing_bone_count > kMaxMeshInfluencingBones) {
    plan.split_reasons.push_back("more than 40 bones");
  }
  if (source_vertex_count > kMaxMeshVertices) {
    plan.split_reasons.push_back("more than 65535 vertices");
  }

  if (plan.split_reasons.empty()) {
    plan.split_reason = "mesh export limits";
  } else {
    for (size_t i = 0; i < plan.split_reasons.size(); ++i) {
      if (i > 0) plan.split_reason += " and ";
      plan.split_reason += plan.split_reasons[i];
    }
  }

  plan.logs_warning = true;
  plan.exported_chunk_count = plan.chunk_count;
  return plan;
}

SourceGltfMiloPopulateMeshChunkPlan
source_gltf_milo_populate_mesh_chunk_plan(
    const std::vector<SourceGltfMiloTriangle>& triangles,
    const std::vector<int32_t>& chunk_joint_indices,
    bool mesh_has_skin) {
  SourceGltfMiloPopulateMeshChunkPlan plan;
  for (size_t i = 0; i < chunk_joint_indices.size(); ++i) {
    plan.joint_local_bones.push_back(
        {chunk_joint_indices[i], static_cast<uint16_t>(i)});
  }

  auto remap_original_index = [&](uint32_t original_index) -> uint16_t {
    const auto it = std::find(plan.original_indices_in_vertex_order.begin(),
                              plan.original_indices_in_vertex_order.end(),
                              original_index);
    if (it != plan.original_indices_in_vertex_order.end()) {
      return static_cast<uint16_t>(std::distance(
          plan.original_indices_in_vertex_order.begin(), it));
    }
    plan.original_indices_in_vertex_order.push_back(original_index);
    if (plan.original_indices_in_vertex_order.size() > 65535) {
      plan.exceeded_max_vertices = true;
    }
    return static_cast<uint16_t>(
        plan.original_indices_in_vertex_order.size() - 1);
  };

  for (const SourceGltfMiloTriangle& tri : triangles) {
    const uint16_t idx0 = remap_original_index(tri.idx0);
    const uint16_t idx1 = remap_original_index(tri.idx1);
    const uint16_t idx2 = remap_original_index(tri.idx2);
    plan.faces.push_back({idx0, idx1, idx2});
  }

  if (mesh_has_skin) {
    plan.builds_bone_transforms = true;
    plan.bone_transform_joint_indices = chunk_joint_indices;
  } else {
    plan.clears_bone_transforms = true;
  }
  return plan;
}

bool source_gltf_milo_is_hair_bone_name(const std::string& bone_name) {
  constexpr char kPrefix[] = "bone_hair_";
  if (bone_name.empty()) return false;
  if (bone_name.size() < sizeof(kPrefix) - 1) return false;
  for (size_t i = 0; i < sizeof(kPrefix) - 1; ++i) {
    const unsigned char c = static_cast<unsigned char>(bone_name[i]);
    if (std::tolower(c) != kPrefix[i]) return false;
  }
  return true;
}

namespace {

std::string source_gltf_milo_lower_ascii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool source_gltf_milo_ends_with_ascii(const std::string& value,
                                      const std::string& suffix) {
  if (suffix.size() > value.size()) return false;
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
         0;
}

std::string source_gltf_milo_chunk_suffix(int32_t chunk_index) {
  if (chunk_index >= 0 && chunk_index < 10) {
    return "0" + std::to_string(chunk_index);
  }
  return std::to_string(chunk_index);
}

std::string source_gltf_milo_insert_chunk_suffix(std::string filename,
                                                 int32_t chunk_index) {
  const size_t last_sep = filename.find_last_of("/\\");
  const size_t dot = filename.find_last_of('.');
  const std::string suffix =
      "." + source_gltf_milo_chunk_suffix(chunk_index);
  if (dot == std::string::npos ||
      (last_sep != std::string::npos && dot < last_sep) ||
      dot + 1 == filename.size()) {
    return filename + suffix;
  }
  filename.insert(dot, suffix);
  return filename;
}

}  // namespace

SourceGltfMiloHairCollisionMeshDecision
source_gltf_milo_hair_collision_mesh_decision(
    const std::string& entry_name,
    const std::string& node_name,
    const std::string& object_type_from_extras) {
  SourceGltfMiloHairCollisionMeshDecision decision;
  const std::string entry_lower = source_gltf_milo_lower_ascii(entry_name);
  const std::string node_lower = source_gltf_milo_lower_ascii(node_name);
  const std::string object_type_lower =
      source_gltf_milo_lower_ascii(object_type_from_extras);

  decision.object_type_char_collide = object_type_lower == "charcollide";
  decision.entry_suffix_coll =
      source_gltf_milo_ends_with_ascii(entry_lower, ".coll");
  decision.entry_suffix_collide =
      source_gltf_milo_ends_with_ascii(entry_lower, ".collide");
  decision.node_suffix_coll =
      source_gltf_milo_ends_with_ascii(node_lower, ".coll");
  decision.node_suffix_collide =
      source_gltf_milo_ends_with_ascii(node_lower, ".collide");
  decision.node_contains_hair_collide =
      node_lower.find("hair_collide") != std::string::npos;
  decision.records_hair_collision_mesh =
      decision.object_type_char_collide || decision.entry_suffix_coll ||
      decision.entry_suffix_collide || decision.node_suffix_coll ||
      decision.node_suffix_collide || decision.node_contains_hair_collide;
  return decision;
}

SourceGltfMiloMeshChunkFinalizePlan
source_gltf_milo_finalize_mesh_chunk_plan(
    const SourceGltfMiloMeshChunkFinalizeInput& input) {
  SourceGltfMiloMeshChunkFinalizePlan plan;
  for (int32_t remaining_faces = std::max(0, input.face_count);
       remaining_faces > 0;) {
    if (remaining_faces >= 255) {
      plan.group_sizes.push_back(255);
      remaining_faces -= 255;
    } else {
      plan.group_sizes.push_back(static_cast<uint8_t>(remaining_faces));
      remaining_faces = 0;
    }
  }

  for (const std::string& joint_name : input.chunk_joint_names) {
    if (source_gltf_milo_is_hair_bone_name(joint_name)) {
      plan.collected_hair_strand_bones.push_back(joint_name);
    }
  }

  std::string overridden_filename = input.filename_after_milo_extras.empty()
                                        ? input.base_filename
                                        : input.filename_after_milo_extras;
  if (input.mesh_chunk_count > 1 && input.chunk_index > 0) {
    overridden_filename =
        source_gltf_milo_insert_chunk_suffix(overridden_filename,
                                             input.chunk_index);
  }
  plan.entry_type = "Mesh";
  plan.entry_name = overridden_filename;
  plan.geom_owner = plan.entry_name;
  plan.hair_collision_decision =
      source_gltf_milo_hair_collision_mesh_decision(
          plan.entry_name, input.node_name, input.object_type_from_extras);
  plan.records_hair_collision_mesh =
      plan.hair_collision_decision.records_hair_collision_mesh;
  return plan;
}

SourceRndMeshZeroWeightPlan source_rndmesh_set_zero_weight_bones(
    int32_t bone_count,
    std::vector<SourceRndMeshZeroWeightVertex> vertices) {
  SourceRndMeshZeroWeightPlan plan;
  plan.vertices = vertices;
  if (bone_count < 2) return plan;
  plan.ran = true;
  for (SourceRndMeshZeroWeightVertex& vertex : plan.vertices) {
    if (vertex.weights[1] == 0.0f) {
      vertex.bone_indices[1] = vertex.bone_indices[0];
    }
    if (vertex.weights[2] == 0.0f) {
      vertex.bone_indices[2] = vertex.bone_indices[0];
    }
    if (vertex.weights[3] == 0.0f) {
      vertex.bone_indices[3] = vertex.bone_indices[0];
    }
  }
  return plan;
}

SourceRndMeshDefaultState source_rndmesh_default_state() {
  return SourceRndMeshDefaultState{};
}

SourceRndMeshSavePlan source_rndmesh_save_plan() {
  return SourceRndMeshSavePlan{};
}

SourceRndMeshDestructorPlan source_rndmesh_destructor_plan() {
  return SourceRndMeshDestructorPlan{};
}

SourceRndMeshSetMatPlan source_rndmesh_set_mat_plan(
    bool material_pointer_present) {
  SourceRndMeshSetMatPlan plan;
  plan.material_pointer_present = material_pointer_present;
  return plan;
}

SourceRndMeshDebugCountsPlan source_rndmesh_debug_counts_plan(
    int32_t face_count,
    int32_t vert_count) {
  SourceRndMeshDebugCountsPlan plan;
  plan.face_count = face_count;
  plan.vert_count = vert_count;
  plan.num_faces_result = face_count;
  plan.num_verts_result = vert_count;
  return plan;
}

SourceRndMeshVolumeTextPlan source_rndmesh_volume_text_plan(int32_t volume) {
  SourceRndMeshVolumeTextPlan plan;
  plan.volume = volume;
  switch (volume) {
    case 0:
      plan.known_volume = true;
      plan.label = "Empty";
      break;
    case 1:
      plan.known_volume = true;
      plan.label = "Triangles";
      break;
    case 2:
      plan.known_volume = true;
      plan.label = "BSP";
      break;
    case 3:
      plan.known_volume = true;
      plan.label = "Box";
      break;
    default:
      break;
  }
  return plan;
}

SourceRndMeshPrintPlan source_rndmesh_print_plan() {
  SourceRndMeshPrintPlan plan;
  plan.rows = {"mat", "geomOwner", "mutable", "volume", "bones:TODO",
               "geometry:TODO"};
  return plan;
}

int32_t source_rndmesh_max_bones() {
  return 40;
}

SourceRndMeshSyncPlan source_rndmesh_sync_plan(int32_t mask,
                                               bool keep_mesh_data) {
  SourceRndMeshSyncPlan plan;
  plan.input_mask = mask;
  plan.keep_mesh_data = keep_mesh_data;
  plan.on_sync_mask = keep_mesh_data ? (mask | 0x200) : mask;
  return plan;
}

SourceRndMeshClearCompressedVertsPlan
source_rndmesh_clear_compressed_verts_plan() {
  return SourceRndMeshClearCompressedVertsPlan{};
}

SourceRndMeshCountPlan source_rndmesh_set_num_verts_plan(
    int32_t count,
    bool keep_mesh_data) {
  SourceRndMeshCountPlan plan;
  plan.requested_count = count;
  plan.resize_verts = true;
  plan.on_sync_mask = source_rndmesh_sync_plan(plan.sync_input_mask,
                                               keep_mesh_data)
                          .on_sync_mask;
  return plan;
}

SourceRndMeshCountPlan source_rndmesh_set_num_faces_plan(
    int32_t count,
    bool keep_mesh_data) {
  SourceRndMeshCountPlan plan;
  plan.requested_count = count;
  plan.resize_faces = true;
  plan.on_sync_mask = source_rndmesh_sync_plan(plan.sync_input_mask,
                                               keep_mesh_data)
                          .on_sync_mask;
  return plan;
}

SourceRndMeshFaceLoadPlan source_rndmesh_face_load_plan(
    int32_t mesh_revision) {
  SourceRndMeshFaceLoadPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.reads_legacy_vector = mesh_revision < 1;
  return plan;
}

SourceRndMeshFaceCenterResult source_rndmesh_face_center(
    const std::vector<std::array<float, 3>>& vertices,
    const SourceRndMeshFace& face) {
  SourceRndMeshFaceCenterResult result;
  const std::array<int32_t, 3> indices = {face.idx0, face.idx1, face.idx2};
  for (int32_t index : indices) {
    if (index < 0 || static_cast<size_t>(index) >= vertices.size()) {
      result.invalid_index = true;
      return result;
    }
    result.center[0] += vertices[static_cast<size_t>(index)][0];
    result.center[1] += vertices[static_cast<size_t>(index)][1];
    result.center[2] += vertices[static_cast<size_t>(index)][2];
  }
  result.center[0] *= 0.33333333f;
  result.center[1] *= 0.33333333f;
  result.center[2] *= 0.33333333f;
  return result;
}

SourceRndMeshHandlerPlan source_rndmesh_handler_plan() {
  SourceRndMeshHandlerPlan plan;
  plan.handlers = {"compare_edge_verts", "attach_mesh",  "get_face",
                   "set_face",           "get_vert_pos", "set_vert_pos",
                   "get_vert_norm",      "set_vert_norm", "get_vert_uv",
                   "set_vert_uv",        "unitize_normals",
                   "point_collide",      "configure_mesh"};
  plan.expressions = {"num_bones", "estimated_size_kb:MILO_DEBUG"};
  plan.actions = {"clear_bones:CopyBones(NULL)",
                  "copy_geom_from_owner:CopyGeometryFromOwner()"};
  plan.superclasses = {"RndDrawable", "RndTransformable", "Hmx::Object"};
  return plan;
}

SourceRndMeshPropSyncPlan source_rndmesh_prop_sync_plan() {
  SourceRndMeshPropSyncPlan plan;
  plan.properties = {"mat",
                     "geom_owner:null->self",
                     "mutable",
                     "num_verts:SetNumVerts",
                     "num_faces:SetNumFaces",
                     "volume:SetVolume",
                     "has_valid_bones",
                     "bones",
                     "has_ao_calculation",
                     "force_no_quantize",
                     "keep_mesh_data:SetKeepMeshData"};
  plan.mutable_rows = {"int bit mask",
                       "BIT_* symbol macro",
                       "whole mutable PropSync fallback"};
  plan.flag_rows = {"has_ao_calculation:get/set",
                    "force_no_quantize:get/set"};
  plan.superclasses = {"RndTransformable", "RndDrawable"};
  return plan;
}

SourceRndMeshMutableBitPlan source_rndmesh_mutable_bit_plan(
    uint32_t current_flags,
    uint32_t bit_mask,
    bool has_bit_subproperty,
    bool prop_get,
    bool set_value) {
  SourceRndMeshMutableBitPlan plan;
  plan.has_bit_subproperty = has_bit_subproperty;
  plan.bit_mask = bit_mask;
  plan.result_flags = current_flags;
  if (!has_bit_subproperty) {
    plan.delegates_whole_mutable = true;
    return plan;
  }

  plan.resolves_int_or_bit_symbol = true;
  plan.asserts_prop_insert_or_less = true;
  if (prop_get) {
    plan.get_returns_bit_set = (current_flags & bit_mask) != 0;
    return plan;
  }

  plan.set_or_clear_bit = true;
  plan.result_flags = set_value ? (current_flags | bit_mask)
                                : (current_flags & ~bit_mask);
  return plan;
}

SourceRndMeshPointCollidePlan source_rndmesh_point_collide_plan(
    bool has_bsp_tree,
    bool intersected) {
  SourceRndMeshPointCollidePlan plan;
  plan.has_bsp_tree = has_bsp_tree;
  plan.calls_intersect = has_bsp_tree;
  plan.intersected = has_bsp_tree && intersected;
  plan.returns_hit = has_bsp_tree && intersected;
  return plan;
}

SourceRndMeshAttachMeshPlan source_rndmesh_attach_mesh_plan() {
  return SourceRndMeshAttachMeshPlan{};
}

SourceRndMeshConfigureMeshPlan source_rndmesh_configure_mesh_plan(
    bool type_is_configurable,
    float left,
    float right,
    float height) {
  SourceRndMeshConfigureMeshPlan plan;
  plan.type_is_configurable = type_is_configurable;
  if (!type_is_configurable) {
    plan.warns_nonconfigurable = true;
    return plan;
  }

  plan.reads_left_right_height = true;
  plan.assigns_four_vertex_positions = true;
  plan.positions[0] = {left, 0.0f, height};
  plan.positions[1] = {left, 0.0f, 0.0f};
  plan.positions[2] = {right, 0.0f, 0.0f};
  plan.positions[3] = {right, 0.0f, height};
  plan.syncs = true;
  plan.sync_mask = 0x3f;
  return plan;
}

SourceRndMeshIndexedEditPlan source_rndmesh_vertex_edit_plan(
    int32_t vertex_count,
    int32_t index,
    const std::string& row,
    bool write) {
  SourceRndMeshIndexedEditPlan plan;
  plan.row = row;
  plan.count = vertex_count;
  plan.index = index;
  plan.write = write;
  if (row == "norm") {
    plan.value_count = 3;
    plan.assert_line = write ? 2457 : 2446;
  } else if (row == "pos" || row == "xyz") {
    plan.row = "pos";
    plan.value_count = 3;
    plan.assert_line = write ? 2480 : 2469;
  } else if (row == "uv") {
    plan.value_count = 2;
    plan.assert_line = write ? 2502 : 2492;
  }
  plan.valid_index =
      plan.value_count != 0 && index >= 0 && index < vertex_count;
  if (write && plan.valid_index) plan.sync_mask = 31;
  return plan;
}

SourceRndMeshIndexedEditPlan source_rndmesh_face_edit_plan(
    int32_t face_count,
    int32_t index,
    bool write) {
  SourceRndMeshIndexedEditPlan plan;
  plan.row = "face";
  plan.count = face_count;
  plan.index = index;
  plan.value_count = 3;
  plan.write = write;
  plan.valid_index = index >= 0 && index < face_count;
  plan.assert_line = write ? 2524 : 2513;
  if (write && plan.valid_index) plan.sync_mask = 32;
  return plan;
}

SourceRndMeshUnitizeNormalsPlan source_rndmesh_unitize_normals_plan(
    int32_t vertex_count) {
  SourceRndMeshUnitizeNormalsPlan plan;
  plan.vertex_count = vertex_count;
  if (vertex_count > 0) plan.normalized_count = vertex_count;
  return plan;
}

SourceRndMeshVertVectorResizePlan source_rndmesh_vert_vector_resize_plan(
    int32_t current_capacity,
    int32_t current_count,
    int32_t requested_count,
    bool resize_bool) {
  SourceRndMeshVertVectorResizePlan plan;
  plan.requested_count = requested_count;
  plan.requested_unka = resize_bool;
  plan.resulting_count = current_count;
  if (current_capacity != 0) {
    plan.capacity_path = true;
    plan.assertion_would_fail = requested_count > current_capacity;
    if (!plan.assertion_would_fail) plan.resulting_count = requested_count;
    return plan;
  }

  plan.dynamic_path = true;
  if (requested_count == 0) {
    plan.releases_verts = true;
    plan.resulting_count = 0;
  } else if (requested_count != current_count) {
    plan.allocates_new_verts = true;
    plan.copied_vert_count = std::min(requested_count, current_count);
    plan.copies_old_verts = plan.copied_vert_count > 0;
    plan.deletes_old_verts = true;
    plan.resulting_count = requested_count;
  }
  return plan;
}

SourceRndMeshVertVectorReservePlan source_rndmesh_vert_vector_reserve_plan(
    int32_t current_capacity,
    int32_t current_count,
    int32_t requested_capacity,
    bool resize_bool) {
  SourceRndMeshVertVectorReservePlan plan;
  plan.requested_capacity = requested_capacity;
  plan.requested_unka = resize_bool;
  plan.assertion_would_fail = requested_capacity <= current_capacity ||
                              requested_capacity <= current_count;
  plan.overflow_fail = requested_capacity < 0 || requested_capacity > 0xffff;
  plan.resulting_capacity = current_capacity;
  plan.resulting_count = current_count;
  if (plan.assertion_would_fail) return plan;

  plan.clears_capacity_before_resize = true;
  plan.resulting_capacity = 0;
  if (plan.overflow_fail) return plan;

  plan.resize_step = source_rndmesh_vert_vector_resize_plan(
      0, current_count, requested_capacity, resize_bool);
  plan.resulting_capacity = requested_capacity;
  plan.resulting_count = current_count;
  return plan;
}

SourceRndMeshKeepMeshDataPlan source_rndmesh_set_keep_mesh_data_plan(
    bool current_keep_mesh_data,
    bool requested_keep_mesh_data) {
  SourceRndMeshKeepMeshDataPlan plan;
  plan.changed = current_keep_mesh_data != requested_keep_mesh_data;
  plan.keep_mesh_data = plan.changed ? requested_keep_mesh_data
                                     : current_keep_mesh_data;
  if (plan.changed && !requested_keep_mesh_data) {
    plan.clear_verts = true;
    plan.clear_faces = true;
    plan.clear_patches = true;
  }
  return plan;
}

SourceRndMeshCollideShowingPlan source_rndmesh_collide_showing_plan(
    bool is_skinned,
    bool raw_collide,
    bool has_bsp_tree,
    bool volume_is_triangles,
    bool hit) {
  SourceRndMeshCollideShowingPlan plan;
  plan.use_original_segment = is_skinned || raw_collide;
  plan.invert_world_for_segment = !plan.use_original_segment;
  plan.multiply_segment_start_end = plan.invert_world_for_segment;
  plan.checks_bsp_tree = has_bsp_tree;
  plan.checks_triangle_volume = !has_bsp_tree && volume_is_triangles;
  plan.skins_triangle_vertices =
      plan.checks_triangle_volume && is_skinned && !raw_collide;
  plan.uses_raw_vertex_positions =
      plan.checks_triangle_volume && !plan.skins_triangle_vertices;

  const bool can_hit = hit && (plan.checks_bsp_tree ||
                              plan.checks_triangle_volume);
  plan.returns_mesh = can_hit;
  plan.transforms_bsp_plane_to_world = hit && plan.checks_bsp_tree;
  if (hit && plan.checks_triangle_volume) {
    plan.interpolates_segment_end = true;
    plan.multiplies_hit_fraction = true;
    plan.sets_plane_from_triangle = true;
    plan.records_last_collide_face = true;
    plan.transforms_triangle_plane_to_world = !raw_collide;
  }
  return plan;
}

SourceRndMeshUpdateSpherePlan source_rndmesh_update_sphere_plan(
    bool has_bones) {
  SourceRndMeshUpdateSpherePlan plan;
  plan.has_bones = has_bones;
  if (has_bones) {
    plan.zero_sphere = true;
  } else {
    plan.make_world_sphere = true;
    plan.make_world_sphere_uses_showing = true;
    plan.invert_world = true;
    plan.multiply_sphere_to_local = true;
  }
  plan.set_drawable_sphere = true;
  return plan;
}

SourceRndMeshDistanceToPlaneResult source_rndmesh_get_distance_to_plane(
    const std::vector<float>& world_plane_dots) {
  SourceRndMeshDistanceToPlaneResult result;
  if (world_plane_dots.empty()) {
    result.empty_vertices = true;
    result.uses_world_xfm = false;
    return result;
  }

  result.starts_from_first_vertex = true;
  result.selected_vertex = 0;
  result.distance = world_plane_dots[0];
  for (size_t i = 0; i < world_plane_dots.size(); ++i) {
    const float dotted = world_plane_dots[i];
    if (std::fabs(dotted) < std::fabs(result.distance)) {
      result.distance = dotted;
      result.selected_vertex = i;
    }
  }
  return result;
}

SourceRndMeshSetVolumePlan source_rndmesh_set_volume_plan(
    int32_t requested_volume,
    bool owner_is_self,
    bool has_vertices,
    bool has_faces) {
  SourceRndMeshSetVolumePlan plan;
  plan.requested_volume = requested_volume;
  plan.owner_is_self = owner_is_self;
  if (!owner_is_self) {
    plan.forwards_to_geom_owner = true;
    return plan;
  }

  plan.assigns_volume = true;
  plan.releases_bsp_tree = true;
  plan.checks_nonempty_geometry = has_vertices && has_faces;
  if (!plan.checks_nonempty_geometry) return plan;

  if (requested_volume == 3) {
    plan.enters_volume_box_branch = true;
    plan.grows_box_from_vertices = true;
    plan.creates_bsp_tree = true;
    plan.volume_box_body_incomplete = true;
  } else if (requested_volume == 2) {
    plan.enters_volume_bsp_branch = true;
    plan.volume_bsp_body_incomplete = true;
  }
  return plan;
}

SourceRndMeshPreLoadVerticesPlan source_rndmesh_pre_load_vertices_plan(
    int32_t alt_revision) {
  SourceRndMeshPreLoadVerticesPlan plan;
  plan.alt_revision = alt_revision;
  plan.creates_file_loader = alt_revision > 4;
  return plan;
}

SourceRndMeshPostLoadVerticesPlan source_rndmesh_post_load_vertices_plan(
    int32_t mesh_revision,
    int32_t compressed_size,
    bool stream_compressed_flag,
    int32_t loaded_compressed_size,
    int32_t loaded_version,
    uint32_t mutable_flags,
    bool keep_mesh_data,
    bool has_file_loader) {
  SourceRndMeshPostLoadVerticesPlan plan;
  plan.mesh_revision = mesh_revision;
  plan.compressed_size = compressed_size;
  plan.had_file_loader = has_file_loader;
  plan.releases_file_loader = has_file_loader;
  plan.wraps_buffer_stream = has_file_loader;
  plan.frees_temp_buffer = has_file_loader;
  plan.reads_compressed_flag = mesh_revision > 0x22;
  plan.compressed_flag = plan.reads_compressed_flag && stream_compressed_flag;
  if (plan.compressed_flag) {
    plan.loaded_compressed_size = loaded_compressed_size;
    plan.loaded_version = loaded_version;
    plan.asserts_vertex_compression_supported = true;
    plan.unsupported_compression_fail = true;
    plan.compressed_metadata_zero =
        loaded_compressed_size == 0 && loaded_version == 0;
    plan.warns_stale_compressed_data = !plan.compressed_metadata_zero;
    if (plan.compressed_metadata_zero) {
      plan.stores_num_compressed_verts = true;
      plan.num_compressed_verts = compressed_size;
      plan.debug_fail_if_compressed_size_nonzero = compressed_size != 0;
      plan.allocates_compressed_verts = compressed_size != 0;
      plan.reads_compressed_chunks = compressed_size != 0;
    } else {
      plan.asserts_positive_seek = true;
      plan.seek_bytes = loaded_compressed_size * compressed_size;
    }
    return plan;
  }

  plan.uncompressed_path = true;
  plan.resize_verts = true;
  plan.resize_bool = (mutable_flags & 0x1fU) == 0 && !keep_mesh_data;
  plan.vertex_read_count = compressed_size;
  plan.temp_eof_poll_count = compressed_size > 0 ? compressed_size / 0x200 : 0;
  return plan;
}

SourceRndMeshCreateMultiMeshPlan source_rndmesh_create_multi_mesh_plan(
    bool owner_had_multimesh) {
  SourceRndMeshCreateMultiMeshPlan plan;
  plan.owner_had_multimesh = owner_had_multimesh;
  plan.creates_multimesh = !owner_had_multimesh;
  plan.sets_mesh_to_owner = !owner_had_multimesh;
  plan.clears_instances = true;
  plan.returns_owner_multimesh = true;
  return plan;
}

SourceRndMeshCacheStripsPlan source_rndmesh_cache_strips_plan(
    bool stream_cached,
    bool platform_wii,
    bool owner_is_self,
    int32_t face_count,
    int32_t vert_count,
    uint32_t mutable_flags) {
  SourceRndMeshCacheStripsPlan plan;
  plan.stream_cached = stream_cached;
  plan.platform_wii = platform_wii;
  plan.owner_is_self = owner_is_self;
  plan.has_faces = face_count != 0;
  plan.has_verts = vert_count != 0;
  plan.mutable_strip_disabled = (mutable_flags & 0x20U) != 0;
  plan.cache_strips = stream_cached && platform_wii && owner_is_self &&
                      plan.has_faces && plan.has_verts &&
                      !plan.mutable_strip_disabled;
  return plan;
}

SourceRndMeshStriperResultReadPlan source_rndmesh_striper_result_read_plan(
    int32_t nb_strips,
    int32_t runs) {
  SourceRndMeshStriperResultReadPlan plan;
  plan.nb_strips = nb_strips;
  plan.runs = runs;
  plan.allocates_lengths_and_runs = true;
  plan.strip_lengths_bytes = nb_strips * 4;
  plan.strip_runs_bytes = runs * 2;
  return plan;
}

SourceRndMeshCreateStripPlan source_rndmesh_create_strip_plan(
    int32_t face_start,
    int32_t face_count,
    int32_t nb_strips_after_compute,
    const std::vector<int32_t>& strip_lengths,
    bool one_sided) {
  SourceRndMeshCreateStripPlan plan;
  plan.face_start = face_start;
  plan.face_count = face_count;
  plan.one_sided = one_sided;
  plan.final_nb_strips = nb_strips_after_compute;
  for (int32_t i = plan.loop_start_index; i < plan.final_nb_strips; ++i) {
    if (i < 0 || static_cast<size_t>(i) >= strip_lengths.size()) {
      plan.missing_strip_length = true;
      break;
    }
    plan.final_nb_strips += strip_lengths[static_cast<size_t>(i)];
  }
  return plan;
}

SkinnedMesh decode_skinned_mesh(const std::string& entry_name,
                                const std::vector<uint8_t>& body,
                                int32_t parent_dir_revision) {
  SkinnedMesh mesh;
  mesh.name = entry_name;
  try {
    Reader r(body.data(), body.size());
    int32_t ver = r.i32();  // GH1=25, GH2=28.
    if (ver != 25 && ver != 28)
      mesh.error = "unexpected mesh version " + std::to_string(ver);

    // GH1 revision-10 directories predate the per-entry Hmx::Object block.
    if (parent_dir_revision >= 24) read_object_fields(r);
    const TransFields trans = read_rnd_trans(r, false, parent_dir_revision);
    mesh.local = trans.local;
    mesh.world_stored = trans.world;
    mesh.constraint = trans.constraint;
    mesh.target = trans.target;
    mesh.preserve_scale = trans.preserve_scale;
    mesh.parent = trans.parent;
    mesh.legacy_children = trans.legacy_children;

    // Draw base.
    const int32_t draw_ver = r.i32();  // GH1=1, GH2=3.
    if (draw_ver < 1 || draw_ver > 3) {
      mesh.error = "unexpected drawable version " +
                   std::to_string(draw_ver);
      return mesh;
    }
    mesh.showing = r.u8() != 0;
    if (draw_ver < 2) {
      const uint32_t drawable_count = r.u32();
      if (drawable_count > 4096) {
        mesh.error = "legacy drawable list count exceeds entry";
        return mesh;
      }
      for (uint32_t i = 0; i < drawable_count; ++i) {
        if (parent_dir_revision <= 6)
          (void)r.utf8_z();
        else
          (void)r.str();
      }
    }
    if (draw_ver > 0) r.skip(16);  // bounding sphere
    if (draw_ver > 2) mesh.draw_order = r.f32();

    // Mesh fields.
    mesh.material = r.str();
    if (ver == 27) r.str();  // legacy secondary mat name.
    mesh.geometry_owner = r.str();  // geometry-owner name (usually self)
    if (ver < 13) r.str();   // alt geom owner.
    if (ver < 15) r.str();   // trans parent reference.
    if (ver < 14) {
      r.str();
      r.str();
    }
    if (ver < 3) {
      (void)r.f32();
      (void)r.f32();
      (void)r.f32();
    }
    if (ver < 15) r.skip(16);
    if (ver < 8) (void)r.u8();
    if (ver < 15) {
      r.str();
      (void)r.f32();
    }
    if (ver < 16) {
      if (ver > 11) (void)r.u8();
    } else {
      mesh.mutable_flags = r.u32();
    }
    if (ver > 17) mesh.volume = r.u32();
    if (ver > 18) {
      const bool bsp_has_value = r.u8() != 0;
      if (bsp_has_value) {
        mesh.error = "unsupported non-empty BSP tree";
        return mesh;
      }
    }
    if (ver == 7) (void)r.u8();
    if (ver < 11) (void)r.u32();
    uint32_t vcount = r.u32();

    // Gate vertex count against remaining bytes (vcount*48 + 4 for face count).
    if (static_cast<uint64_t>(vcount) * sizeof(SkinVertex) + 4 > body.size() - r.pos) {
      mesh.error = "vertex_count " + std::to_string(vcount) + " exceeds entry";
      return mesh;
    }
    mesh.verts.resize(vcount);
    for (uint32_t i = 0; i < vcount; ++i) {
      SkinVertex& v = mesh.verts[i];
      v.px = r.f32(); v.py = r.f32(); v.pz = r.f32();
      v.nx = r.f32(); v.ny = r.f32(); v.nz = r.f32();
      v.r = r.f32();
      v.g = r.f32();
      v.b = r.f32();
      v.a = r.f32();
      v.u = r.f32(); v.v = r.f32();
    }

    uint32_t fcount = r.u32();
    if (static_cast<uint64_t>(fcount) * 6 > body.size() - r.pos) {
      mesh.error = "face_count " + std::to_string(fcount) + " exceeds entry";
      return mesh;
    }
    mesh.indices.resize(static_cast<size_t>(fcount) * 3);
    for (uint32_t i = 0; i < fcount; ++i) {
      mesh.indices[i * 3 + 0] = r.u16();
      mesh.indices[i * 3 + 1] = r.u16();
      mesh.indices[i * 3 + 2] = r.u16();
    }
    for (uint16_t idx : mesh.indices) {
      if (idx >= vcount) { mesh.error = "face index out of range"; return mesh; }
    }

    // --- skinning tail ---------------------------------------------------
    // MiloLib/RB3 source order for Mesh rev 28:
    //   mPatches/groupSizes: u32 count, then count bytes
    //   if first bone ref is present: four old-style bone refs, then four
    //   RndBone::mOffset transforms.
    if (ver > 0x17) {
      const uint32_t group_count = r.u32();
      if (group_count > 4096 || group_count > body.size() - r.pos) {
        mesh.error = "groupSizes count exceeds entry";
        return mesh;
      }
      mesh.group_sizes.resize(group_count);
      for (uint32_t gi = 0; gi < group_count; ++gi) {
        mesh.group_sizes[gi] = r.u8();
      }
    } else if (ver > 0x10) {
      const uint32_t group_count = r.u32();
      if (group_count > 4096 || group_count > body.size() - r.pos) {
        mesh.error = "legacy groupSizes count exceeds entry";
        return mesh;
      }
      mesh.group_sizes.resize(group_count);
      for (uint32_t gi = 0; gi < group_count; ++gi) {
        mesh.group_sizes[gi] = r.u8();
      }
    }

    if (r.pos + 4 <= body.size()) {
      const size_t bone_probe = r.pos;
      const int32_t first_bone_len = r.i32();
      if (first_bone_len > 0) {
        r.pos = bone_probe;
        if (ver >= 33) {
          const uint32_t bone_count = r.u32();
          if (bone_count > 256) {
            mesh.error = "bone count exceeds supported range";
            return mesh;
          }
          mesh.raw_bone_palette.reserve(bone_count);
          mesh.raw_bind.reserve(bone_count);
          for (uint32_t bi = 0; bi < bone_count; ++bi) {
            mesh.raw_bone_palette.push_back(r.str());
            mesh.raw_bind.push_back(r.matrix());
          }
        } else {
          mesh.raw_bone_palette.reserve(4);
          mesh.raw_bind.reserve(4);
          for (int bi = 0; bi < 4; ++bi) {
            mesh.raw_bone_palette.push_back(r.str());
          }
          for (int bi = 0; bi < 4; ++bi) {
            mesh.raw_bind.push_back(r.matrix());
          }
        }
        apply_source_rndmesh_active_bones(mesh, nullptr);
      }
    }

    if (mesh.bone_palette.empty()) {
      for (SkinVertex& vertex : mesh.verts) {
        vertex.r = milo_scene::source_hmx_color32_channel(vertex.r);
        vertex.g = milo_scene::source_hmx_color32_channel(vertex.g);
        vertex.b = milo_scene::source_hmx_color32_channel(vertex.b);
        vertex.a = milo_scene::source_hmx_color32_channel(vertex.a);
      }
    }

    // ihatecompvir MiloLib RndMesh.Read keeps this last-gen tail for parent
    // dirs before revision 25 when groupSizes[0] is non-zero.
    if (!mesh.group_sizes.empty() && mesh.group_sizes[0] > 0 &&
        parent_dir_revision < 25) {
      mesh.group_sections.reserve(mesh.group_sizes.size());
      for (size_t gi = 0; gi < mesh.group_sizes.size(); ++gi) {
        const uint32_t section_count = r.u32();
        const uint32_t vert_count = r.u32();
        const uint64_t payload_bytes =
            static_cast<uint64_t>(section_count) * 4u +
            static_cast<uint64_t>(vert_count) * 2u;
        if (section_count > 65536 || vert_count > 65536 ||
            payload_bytes > body.size() - r.pos) {
          mesh.error = "group section " + std::to_string(gi) +
                       " exceeds entry";
          return mesh;
        }
        RndMeshGroupSection group_section;
        group_section.sections.reserve(section_count);
        for (uint32_t si = 0; si < section_count; ++si) {
          group_section.sections.push_back(r.i32());
        }
        group_section.vert_offsets.reserve(vert_count);
        for (uint32_t vi = 0; vi < vert_count; ++vi) {
          group_section.vert_offsets.push_back(r.u16());
        }
        mesh.group_sections.push_back(std::move(group_section));
      }
    }

    // Bounding box (bind-pose model space).
    if (vcount > 0) {
      mesh.bb_min[0] = mesh.bb_max[0] = mesh.verts[0].px;
      mesh.bb_min[1] = mesh.bb_max[1] = mesh.verts[0].py;
      mesh.bb_min[2] = mesh.bb_max[2] = mesh.verts[0].pz;
      for (const SkinVertex& v : mesh.verts) {
        const float xyz[3] = {v.px, v.py, v.pz};
        for (int k = 0; k < 3; ++k) {
          if (!std::isfinite(xyz[k])) { mesh.error = "non-finite vertex"; return mesh; }
          if (xyz[k] < mesh.bb_min[k]) mesh.bb_min[k] = xyz[k];
          if (xyz[k] > mesh.bb_max[k]) mesh.bb_max[k] = xyz[k];
        }
      }
    }
    mesh.decoded = mesh.error.empty();
  } catch (const std::exception& ex) {
    mesh.error = ex.what();
  }
  return mesh;
}

namespace {

void read_object_row_dtb_node(Reader& r);

struct DtbParentInfo {
  bool has_tree = false;
  uint32_t id = 0;
  uint16_t child_count = 0;
};

struct ObjectFieldRows {
  int32_t version = 0;
  int32_t alt_version = 0;
  std::string subtype;
  DtbParentInfo root;
  std::string note;
};

DtbParentInfo read_object_row_dtb_array_parent_info(Reader& r) {
  DtbParentInfo info;
  info.has_tree = true;
  info.child_count = r.u16();
  info.id = r.u32();
  for (uint16_t i = 0; i < info.child_count; ++i) {
    read_object_row_dtb_node(r);
  }
  return info;
}

DtbParentInfo read_object_row_dtb_parent_info(Reader& r) {
  DtbParentInfo info;
  info.has_tree = r.u8() != 0;
  if (!info.has_tree) return info;
  info = read_object_row_dtb_array_parent_info(r);
  info.has_tree = true;
  return info;
}

void read_object_row_dtb_node(Reader& r) {
  const uint32_t type = r.u32();
  switch (type) {
    case 0x00:  // Int
      (void)r.u32();
      break;
    case 0x01:  // Float
      (void)r.f32();
      break;
    case 0x02:  // Variable
    case 0x04:  // Object
    case 0x05:  // Symbol
    case 0x06:  // Unhandled
    case 0x07:  // IfDef
    case 0x08:  // Else
    case 0x09:  // EndIf
    case 0x12:  // String
    case 0x20:  // Define
    case 0x21:  // Include
    case 0x22:  // Merge
    case 0x23:  // IfNDef
    case 0x24:  // Autorun
    case 0x25:  // Undef
      (void)r.str();
      break;
    case 0x10:  // Array
    case 0x11:  // Command
    case 0x13:  // Property
      (void)read_object_row_dtb_array_parent_info(r);
      break;
    default:
      break;
  }
}

ObjectFieldRows read_object_row_fields(Reader& r) {
  ObjectFieldRows out;
  const uint32_t combined_revision = r.u32();
  out.version = static_cast<uint16_t>(combined_revision & 0xffffu);
  out.alt_version = static_cast<uint16_t>(combined_revision >> 16);
  out.subtype = r.str();
  out.root = read_object_row_dtb_parent_info(r);
  if (out.version > 0) out.note = r.str();
  return out;
}

ObjectRow decode_object_row(const std::string& entry_name,
                            const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  ObjectRow row;
  row.name = entry_name;
  const ObjectFieldRows fields = read_object_row_fields(r);
  row.version = fields.version;
  row.alt_version = fields.alt_version;
  row.subtype = fields.subtype;
  row.root_has_tree = fields.root.has_tree;
  row.root_id = fields.root.id;
  row.root_child_count = fields.root.child_count;
  row.note = fields.note;
  row.unread_bytes = r.n - r.pos;
  if (row.unread_bytes > 0) {
    row.unread_tail_hex =
        hex_bytes(r.p + r.pos, std::min<size_t>(row.unread_bytes, 32));
  }
  return row;
}

CharUpperTwist decode_upper_twist(const std::string& entry_name,
                                  const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharUpperTwist t;
  t.name = entry_name;
  (void)r.i32();      // version 1
  read_object_fields(r);  // Hmx::Object metadata
  // ihatecompvir's CharUpperTwist source has misleading member names:
  // binary/properties are upper_arm, twist1, twist2, while Load stores them
  // into mTwist2, mUpperArm, mTwist1 respectively for Poll().
  t.upper_arm = r.str();
  t.twist1 = r.str();
  t.twist2 = r.str();
  return t;
}

CharForeTwist decode_fore_twist(const std::string& entry_name,
                                const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharForeTwist t;
  t.name = entry_name;
  t.version = r.i32();
  read_object_fields(r);  // Hmx::Object metadata
  t.offset_degrees = r.f32();
  t.hand = r.str();
  t.twist2 = r.str();
  if (t.version == 2 && r.pos + 4 <= r.n) (void)r.i32();
  if (t.version > 3 && r.pos + 4 <= r.n) t.bias_degrees = r.f32();
  return t;
}

CharNeckTwist decode_neck_twist(const std::string& entry_name,
                                const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharNeckTwist t;
  t.name = entry_name;
  t.version = r.i32();
  if (t.version < 0 || t.version > 1) {
    throw std::runtime_error(
        "char_mesh: CharNeckTwist revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata
  t.head = r.str();
  t.twist = r.str();
  t.unread_bytes = r.n - r.pos;
  return t;
}

CharIKRod decode_ik_rod(const std::string& entry_name,
                        const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharIKRod rod;
  rod.name = entry_name;
  rod.version = r.i32();
  read_object_fields(r);  // Hmx::Object metadata
  rod.left_end = r.str();
  rod.right_end = r.str();
  rod.dest_pos = r.f32();
  rod.side_axis = r.str();
  rod.vertical = r.u8() != 0;
  rod.dest = r.str();
  for (int v = 0; v < 4; ++v)
    for (int c = 0; c < 3; ++c)
      rod.xfm[v][c] = r.f32();
  return rod;
}

CharIKHand decode_ik_hand(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharIKHand hand;
  hand.name = entry_name;
  hand.version = r.i32();
  if (hand.version < 0 || hand.version > 0xC) {
    throw std::runtime_error(
        "char_mesh: CharIKHand revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata
  hand.unknown = r.i32();
  hand.weight = r.f32();
  hand.weight_prop = r.str();
  hand.hand = r.str();
  if (hand.version > 4) hand.finger = r.str();
  if (hand.version < 3) {
    hand.target = r.str();
    hand.targets.push_back({hand.target, 0.0f});
  } else if (hand.version < 0xB && r.pos + 4 <= r.n) {
    const uint32_t count = r.u32();
    if (count <= 64) {
      hand.targets.reserve(count);
      for (uint32_t i = 0; i < count && r.pos < r.n; ++i) {
        const std::string target = r.str();
        hand.targets.push_back({target, 0.0f});
        if (hand.target.empty()) hand.target = target;
      }
    }
  }
  hand.orientation = r.u8() != 0;
  hand.stretch = r.u8() != 0;
  if (hand.version > 1 && r.pos < r.n) hand.scalable = r.u8() != 0;
  if (hand.version > 3 && r.pos < r.n) hand.move_elbow = r.u8() != 0;
  if (hand.version > 5 && r.pos + 4 <= r.n) hand.elbow_swing = r.f32();
  if (hand.version > 6 && r.pos < r.n) hand.always_ik_elbow = r.u8() != 0;
  if (hand.version > 7 && r.pos + 5 <= r.n) {
    hand.constrain_wrist = r.u8() != 0;
    hand.wrist_radians = r.f32();
  }
  if (hand.version == 9 && r.pos < r.n) {
    (void)r.str();
    if (r.pos < r.n) (void)r.u8();
  }
  if (hand.version > 0xB && r.pos < r.n) {
    hand.elbow_collide = r.str();
    if (r.pos < r.n) hand.clockwise = r.u8() != 0;
  }
  hand.unread_bytes = r.n - r.pos;
  return hand;
}

CharIKMidi decode_ik_midi(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharIKMidi midi;
  midi.name = entry_name;
  midi.version = r.i32();
  if (midi.version < 0 || midi.version > 5) {
    throw std::runtime_error(
        "char_mesh: CharIKMidi revision outside source range");
  }
  read_object_fields(r);
  midi.bone = r.str();
  if (midi.version < 3) {
    midi.legacy_spots = read_obj_ptr_list(r);
  }
  if (midi.version == 2 || midi.version == 3) {
    midi.legacy_string = r.str();
  }
  if (midi.version > 4) {
    midi.anim_blender = r.str();
    midi.max_anim_blend = r.f32();
  }
  midi.unread_bytes = r.n - r.pos;
  return midi;
}

CharServoBone decode_servo_bone(const std::string& entry_name,
                                const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharServoBone servo;
  servo.name = entry_name;
  servo.version = r.i32();
  if (servo.version < 0 || servo.version > 2) {
    throw std::runtime_error(
        "char_mesh: CharServoBone revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata.
  if (servo.version > 1) servo.clip_type = r.str();
  servo.unread_bytes = r.n - r.pos;
  return servo;
}

CharHair decode_hair_body(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharHair hair;
  hair.name = entry_name;
  hair.version = r.i32();
  if (hair.version < 0 || hair.version > 11) {
    throw std::runtime_error(
        "char_mesh: CharHair revision outside source range");
  }
  read_object_fields(r);
  hair.stiffness = r.f32();
  hair.torsion = r.f32();
  hair.inertia = r.f32();
  hair.gravity = r.f32();
  hair.weight = r.f32();
  hair.friction = r.f32();
  if (hair.version >= 8) {
    hair.min_slack = r.f32();
    hair.max_slack = r.f32();
  }
  const uint32_t strand_count = r.u32();
  hair.strands.reserve(strand_count);
  for (uint32_t si = 0; si < strand_count; ++si) {
    CharHairStrand strand;
    strand.root = r.str();
    strand.angle = r.f32();
    const uint32_t point_count = r.u32();
    strand.points.reserve(point_count);
    for (uint32_t pi = 0; pi < point_count; ++pi) {
      CharHairPoint point;
      point.pos[0] = r.f32();
      point.pos[1] = r.f32();
      point.pos[2] = r.f32();
      point.bone = r.str();
      point.length = r.f32();
      if (hair.version < 3) {
        const uint32_t raw_collide_type = r.u32();
        point.collide_type = hair.version == 2
                                 ? source_grim_char_hair_collide_type(
                                       raw_collide_type)
                                 : raw_collide_type;
        point.collision = r.str();
      } else if (hair.version == 3) {
        point.collide_type = r.u32();
      }
      point.radius = r.f32();
      if (hair.version > 1) {
        point.outer_radius = r.f32();
      } else {
        point.outer_radius = 0.0f;
      }
      if (hair.version == 6 || hair.version == 7 || hair.version == 8) {
        const float add_to_radius = r.f32();
        point.radius += add_to_radius;
        point.outer_radius += add_to_radius;
      }
      if (hair.version == 6) {
        (void)r.str();
      }
      if (hair.version < 8) {
        point.side_length = -1.0f;
        if (hair.version > 5) {
          (void)r.i32();
          (void)r.i32();
        }
      } else {
        bool side_enabled = true;
        if (hair.version < 9) side_enabled = r.u8() != 0;
        point.side_length = r.f32();
        if (hair.version < 9 && !side_enabled) point.side_length = -1.0f;
      }
      if (hair.version > 9) {
        point.unk5c[0] = r.f32();
        point.unk5c[1] = r.f32();
        point.unk5c[2] = r.f32();
      }
      strand.points.push_back(std::move(point));
    }
    for (float& v : strand.base_mat) v = r.f32();
    for (float& v : strand.root_mat) v = r.f32();
    if (hair.version > 2) strand.hookup_flags = r.i32();
    hair.strands.push_back(std::move(strand));
  }
  hair.simulate = r.u8() != 0;
  if (hair.version > 10) hair.wind = r.str();
  hair.unread_bytes = r.n - r.pos;
  if (hair.unread_bytes > 0) {
    hair.unread_tail_hex =
        hex_bytes(r.p + r.pos, std::min<size_t>(hair.unread_bytes, 32));
  }
  return hair;
}

CharCollide decode_collide_body(const std::string& entry_name,
                                const std::vector<uint8_t>& body,
                                int32_t parent_dir_revision) {
  Reader r(body.data(), body.size());
  CharCollide collide;
  collide.name = entry_name;
  collide.version = r.i32();
  if (collide.version < 0 || collide.version > 7) {
    throw std::runtime_error(
        "char_mesh: CharCollide revision outside source range");
  }
  read_object_fields(r);
  const TransFields trans = read_rnd_trans(r, false, parent_dir_revision);
  collide.local = trans.local;
  collide.world_stored = trans.world;
  collide.constraint = trans.constraint;
  collide.target = trans.target;
  collide.preserve_scale = trans.preserve_scale;
  collide.parent = trans.parent;

  collide.shape = r.i32();
  collide.orig_radius[0] = r.f32();
  if (collide.version > 4) collide.orig_length[0] = r.f32();
  if (collide.version > 2) collide.orig_length[1] = r.f32();
  if (collide.version > 1) collide.flags = r.i32();
  if (collide.version > 3) {
    collide.cur_radius[0] = r.f32();
  } else {
    collide.cur_radius[0] = collide.orig_radius[0];
  }

  if (collide.version > 5) {
    collide.orig_radius[1] = r.f32();
    collide.cur_radius[1] = r.f32();
    collide.cur_length[0] = r.f32();
    collide.cur_length[1] = r.f32();
    collide.mesh_transform = r.matrix();
    collide.mesh = r.str();
    for (int i = 0; i < 8; ++i) {
      collide.mesh_spheres[i].vertex = r.i32();
      collide.mesh_spheres[i].vec[0] = r.f32();
      collide.mesh_spheres[i].vec[1] = r.f32();
      collide.mesh_spheres[i].vec[2] = r.f32();
    }
    for (uint8_t& byte : collide.digest) byte = r.u8();
    collide.mesh_y_bias = r.u8() != 0;
    if (collide.version < 7) source_char_collide_copy_original_to_cur(collide);
  } else {
    collide.orig_radius[1] = collide.orig_radius[0];
    source_char_collide_copy_original_to_cur(collide);
  }
  return collide;
}

CharPosConstraint decode_pos_constraint_body(const std::string& entry_name,
                                             const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharPosConstraint constraint;
  constraint.name = entry_name;
  constraint.version = r.i32();
  if (constraint.version < 0 || constraint.version > 2) {
    throw std::runtime_error(
        "char_mesh: CharPosConstraint revision outside source range");
  }
  read_object_fields(r);
  constraint.targets = read_obj_ptr_list(r);
  constraint.source = r.str();
  if (constraint.version > 1) {
    for (float& v : constraint.box_min) v = r.f32();
    for (float& v : constraint.box_max) v = r.f32();
  }
  return constraint;
}

CharBoneOffset decode_bone_offset(const std::string& entry_name,
                                  const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharBoneOffset offset;
  offset.name = entry_name;
  offset.version = r.i32();
  if (offset.version < 0 || offset.version > 1) {
    throw std::runtime_error(
        "char_mesh: CharBoneOffset revision outside source range");
  }
  read_object_fields(r);
  offset.dest = r.str();
  for (float& v : offset.offset) v = r.f32();
  offset.unread_bytes = r.n - r.pos;
  return offset;
}

CharBoneTwist decode_bone_twist(const std::string& entry_name,
                                const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharBoneTwist twist;
  twist.name = entry_name;
  twist.version = r.i32();
  if (twist.version != 0) {
    throw std::runtime_error(
        "char_mesh: CharBoneTwist revision outside source range");
  }
  read_object_fields(r);
  twist.weightable_version = r.i32();
  if (twist.weightable_version < 0 || twist.weightable_version > 2) {
    throw std::runtime_error(
        "char_mesh: CharBoneTwist CharWeightable revision outside source range");
  }
  twist.weight = r.f32();
  if (twist.weightable_version > 1) twist.weight_owner = r.str();
  twist.bone = r.str();
  twist.targets = read_obj_ptr_list(r);
  twist.unread_bytes = r.n - r.pos;
  return twist;
}

CharLookAt decode_lookat_body(const std::string& entry_name,
                              const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharLookAt la;
  la.name = entry_name;
  la.version = r.i32();
  if (la.version < 0 || la.version > 5) {
    throw std::runtime_error(
        "char_mesh: CharLookAt revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata
  la.weightable_version = r.i32();
  if (la.weightable_version < 0 || la.weightable_version > 2) {
    throw std::runtime_error(
        "char_mesh: CharLookAt CharWeightable revision outside source range");
  }
  la.weight = r.f32();
  if (la.weightable_version > 1) la.weight_owner = r.str();
  la.source = r.str();
  la.pivot = r.str();
  la.dest = r.str();
  la.half_time = r.f32();
  la.min_yaw = r.f32();
  la.max_yaw = r.f32();
  la.min_pitch = r.f32();
  la.max_pitch = r.f32();
  if (la.version > 1) {
    la.min_weight_yaw = r.f32();
    la.max_weight_yaw = r.f32();
    la.weight_yaw_speed = r.f32();
  }
  if (la.version < 3) {
    la.allow_roll = true;
  } else {
    la.allow_roll = r.u8() != 0;
  }
  if (la.version < 4) {
    la.enable_jitter = false;
    la.pitch_jitter_limit = 0.0f;
    la.yaw_jitter_limit = 0.0f;
  } else {
    la.enable_jitter = r.u8() != 0;
    la.pitch_jitter_limit = r.f32();
    la.yaw_jitter_limit = r.f32();
  }
  if (la.version > 4) la.source_radius = r.f32();
  la.unread_bytes = r.n - r.pos;
  return la;
}

CharEyes decode_eyes_body(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharEyes eyes;
  eyes.name = entry_name;
  eyes.version = r.i32();
  if (eyes.version < 0 || eyes.version > 0x12) {
    throw std::runtime_error(
        "char_mesh: CharEyes revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata
  if (eyes.version < 5) {
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && r.pos < r.n; ++i)
      eyes.lookats.push_back(r.str());
    if ((eyes.version == 3 || eyes.version == 4) && r.pos < r.n) {
      eyes.legacy_transform = r.str();
    }
  }
  eyes.unread_bytes = r.n - r.pos;
  return eyes;
}

FaceFxLipSyncServo decode_lip_sync_servo(const std::string& entry_name,
                                         const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  FaceFxLipSyncServo servo;
  servo.name = entry_name;
  (void)r.i32();      // version 5 in GH2
  (void)r.i32();
  (void)r.str();      // GH2 servo tag: "gh2" for guitarists, "singer" for vocalists.

  // GH2 PS2 FaceFxLipSyncServo compatibility, not a CharFaceServo source port:
  // ihatecompvir's checked sources expose CharFaceServo::Load, but no matching
  // FaceFxLipSyncServo::Load body. Keep this limited to the stock FAC/viseme
  // references and target rows instead of treating it as controller authority.
  // The row stores a single NUL terminator after the tag string, then the
  // Weightable block. The old decoder aligned to 4 bytes, which only worked
  // for the 3-byte "gh2" tag by accident and broke 6-byte "singer". Probe the
  // few possible post-tag starts and keep the one whose following fields match
  // the traced servo layout.
  std::string best_facefx;
  std::string best_viseme;
  std::vector<FaceFxServoTarget> best_targets;
  bool decoded = false;
  const size_t tag_end = r.pos;
  for (size_t start = tag_end; start <= tag_end + 4 && start < r.n; ++start) {
    try {
      Reader q = r;
      q.pos = start;
      const uint32_t weight_version = q.u32();
      const float weight = q.f32();
      const std::string self_name = q.str();
      const std::string facefx_path = q.str();
      const std::string viseme_milo = q.str();
      const uint32_t count = q.u32();
      if (weight_version != 2 || !std::isfinite(weight) ||
          self_name != entry_name || facefx_path.find(".fac") == std::string::npos ||
          viseme_milo.find(".milo") == std::string::npos || count > 64) {
        continue;
      }
      std::vector<FaceFxServoTarget> targets;
      for (uint32_t i = 0; i < count && q.pos < q.n; ++i) {
        FaceFxServoTarget t;
        t.object = q.str();
        t.prop_type = q.i32();
        t.property = q.str();
        targets.push_back(std::move(t));
      }
      if (targets.size() != count) continue;
      if (q.pos != q.n) continue;
      best_facefx = facefx_path;
      best_viseme = viseme_milo;
      best_targets = std::move(targets);
      decoded = true;
      break;
    } catch (const std::exception&) {
      continue;
    }
  }
  if (!decoded)
    throw std::runtime_error("char_mesh: FaceFxLipSyncServo layout not recognized");
  servo.facefx_path = std::move(best_facefx);
  servo.viseme_milo = std::move(best_viseme);
  servo.targets = std::move(best_targets);
  return servo;
}

RndAnimFilter decode_anim_filter(const std::string& entry_name,
                                 const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  RndAnimFilter filter;
  filter.name = entry_name;
  filter.version = r.i32();
  read_object_fields(r);  // Hmx::Object metadata.
  const RndAnimatableFields animatable = read_rnd_animatable(r);
  filter.animatable_version = animatable.version;
  filter.frame = animatable.frame;
  filter.rate = animatable.rate;
  filter.anim = r.str();
  filter.scale = r.f32();
  filter.offset = r.f32();
  filter.start = r.f32();
  filter.end = r.f32();
  if (filter.version != 0) {
    filter.type = r.i32();
    filter.period = r.f32();
  } else {
    const uint8_t legacy_loop = r.u8();
    filter.type = legacy_loop != 0 ? 1 : 0;
  }
  if (filter.version > 1) {
    filter.snap = r.f32();
    filter.jitter = r.f32();
  }
  filter.unread_bytes = r.n - r.pos;
  return filter;
}

EventTriggerAnim read_event_trigger_anim(Reader& r, int32_t version) {
  EventTriggerAnim anim;
  anim.anim = r.str();
  anim.blend = r.f32();
  anim.wait = r.u8() != 0;
  anim.delay = r.f32();
  if (version > 9) {
    anim.enable = r.u8() != 0;
    anim.rate = r.i32();
    anim.start = r.f32();
    anim.end = r.f32();
    anim.period = r.f32();
    anim.type = r.str();
    anim.scale = r.f32();
  }
  return anim;
}

std::vector<EventTriggerAnim> read_event_trigger_anims(Reader& r,
                                                       int32_t version) {
  std::vector<EventTriggerAnim> out;
  const uint32_t count = r.u32();
  if (count > 256) {
    throw std::runtime_error("char_mesh: implausible EventTrigger anim vector");
  }
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    out.push_back(read_event_trigger_anim(r, version));
  }
  return out;
}

EventTriggerProxyCall read_event_trigger_proxy_call(Reader& r,
                                                    int32_t version) {
  EventTriggerProxyCall call;
  call.proxy = r.str();
  call.call = r.str();
  if (version > 10) call.event = r.str();
  return call;
}

std::vector<EventTriggerProxyCall> read_event_trigger_proxy_calls(
    Reader& r, int32_t version) {
  std::vector<EventTriggerProxyCall> out;
  const uint32_t count = r.u32();
  if (count > 256) {
    throw std::runtime_error(
        "char_mesh: implausible EventTrigger proxy vector");
  }
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    out.push_back(read_event_trigger_proxy_call(r, version));
  }
  return out;
}

EventTriggerHideDelay read_event_trigger_hide_delay(Reader& r) {
  EventTriggerHideDelay delay;
  delay.hide = r.str();
  delay.delay = r.f32();
  delay.rate = r.i32();
  return delay;
}

std::vector<EventTriggerHideDelay> read_event_trigger_hide_delays(Reader& r) {
  std::vector<EventTriggerHideDelay> out;
  const uint32_t count = r.u32();
  if (count > 256) {
    throw std::runtime_error(
        "char_mesh: implausible EventTrigger hide-delay vector");
  }
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    out.push_back(read_event_trigger_hide_delay(r));
  }
  return out;
}

EventTrigger decode_event_trigger(const std::string& entry_name,
                                  const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  EventTrigger trigger;
  trigger.name = entry_name;
  const uint32_t packed_rev = r.u32();
  trigger.version = source_hmx_rev(packed_rev);
  trigger.alt_version = source_alt_rev(packed_rev);
  read_object_fields(r);
  if (trigger.version > 0x0f) {
    const RndAnimatableFields animatable = read_rnd_animatable(r);
    trigger.animatable_version = animatable.version;
    trigger.frame = animatable.frame;
    trigger.anim_rate = animatable.rate;
  }
  if (trigger.version > 9) {
    trigger.trigger_events = read_symbol_vector(r);
  } else if (trigger.version > 6) {
    const std::string event = r.str();
    if (!event.empty()) trigger.trigger_events.push_back(event);
  }
  if (trigger.version > 6) {
    trigger.anims = read_event_trigger_anims(r, trigger.version);
    trigger.sounds = read_obj_ptr_list(r);
    trigger.shows = read_obj_ptr_list(r);
  }
  if (trigger.version > 0x0c) {
    trigger.hide_delays = read_event_trigger_hide_delays(r);
  }
  if (trigger.version > 2) {
    trigger.enable_events = read_symbol_vector(r);
    trigger.disable_events = read_symbol_vector(r);
  }
  if (trigger.version > 5) trigger.wait_for_events = read_symbol_vector(r);
  if (trigger.version > 6) trigger.next_link = r.str();
  if (trigger.version > 7) {
    trigger.proxy_calls = read_event_trigger_proxy_calls(r, trigger.version);
  }
  if (trigger.version > 0x0b) trigger.trigger_order = r.i32();
  if (trigger.version > 0x0d) trigger.reset_triggers = read_obj_ptr_list(r);
  if (trigger.version > 0x0e) trigger.reset_self = r.u8() != 0;
  if (trigger.version > 0x0f) {
    trigger.anim_trigger = r.i32();
    trigger.anim_frame = r.f32();
  }
  if (trigger.version > 0x10) trigger.part_launchers = read_obj_ptr_list(r);
  trigger.unread_bytes = r.n - r.pos;
  if (trigger.unread_bytes > 0) {
    trigger.unread_tail_hex = hex_bytes(r.p + r.pos, trigger.unread_bytes);
  }
  return trigger;
}

}  // namespace

CharMeshHide decode_char_mesh_hide(const std::string& entry_name,
                                   const std::vector<uint8_t>& body) {
  CharMeshHide out;
  out.name = entry_name;
  try {
    Reader r(body.data(), body.size());
    const uint32_t combined_revision = r.u32();
    out.revision =
        static_cast<int32_t>(combined_revision & 0xffffu);
    out.alt_revision =
        static_cast<int32_t>(combined_revision >> 16);
    if (out.revision < 0 || out.revision > 2) {
      throw std::runtime_error(
          "char_mesh: unsupported CharMeshHide revision " +
          std::to_string(out.revision));
    }
    read_object_fields(r);
    out.flags = r.i32();
    const uint32_t hide_count = r.u32();
    if (hide_count > 65536u) {
      throw std::runtime_error(
          "char_mesh: implausible CharMeshHide row count");
    }
    out.hides.reserve(hide_count);
    for (uint32_t i = 0; i < hide_count; ++i) {
      CharMeshHideRow row;
      row.drawable = r.str();
      row.flags = r.i32();
      if (out.revision > 1) row.show = r.u8() != 0;
      out.hides.push_back(std::move(row));
    }
    out.unread_bytes = r.n - r.pos;
    out.decoded = true;
  } catch (const std::exception& ex) {
    out.error = ex.what();
  }
  return out;
}

SourceRndTexLoadPlan source_rndtex_load_plan(
    int32_t revision,
    int32_t alt_revision,
    bool stream_cached) {
  SourceRndTexLoadPlan plan;
  plan.revision = revision;
  plan.alt_revision = alt_revision;
  plan.stream_cached = stream_cached;
  plan.accepted_revision = revision >= 1 && revision <= 11;
  plan.reads_object_fields = revision > 8;
  plan.reads_short_dimensions = revision == 1;
  plan.reads_int_dimensions = !plan.reads_short_dimensions;
  plan.creates_uncached_loader = revision > 9 && !stream_cached;
  plan.creates_cached_loader = stream_cached && alt_revision != 0;
  plan.reads_legacy_cubemap_mask = revision < 5;
  plan.reads_legacy_bool = revision != 0 && revision < 3;
  plan.reads_float_mip_map_k = revision > 7;
  plan.reads_fixed_mip_map_k = revision > 3 && revision <= 7;
  plan.reads_direct_type = revision > 6;
  plan.reads_legacy_type_index = revision > 5 && revision <= 6;
  plan.reads_rendered_bool_type = revision > 4 && revision <= 5;
  plan.reads_post_flag = revision > 7;
  plan.reads_optimize_for_ps3 = revision > 10;
  plan.delegates_cached_payload_to_bitmap = stream_cached;
  return plan;
}

SourceRndTexSavePlan source_rndtex_save_plan() {
  return SourceRndTexSavePlan{};
}

SourceRndTexPowerOfTwoPlan source_rndtex_power_of_two_plan(
    int32_t width,
    int32_t height) {
  SourceRndTexPowerOfTwoPlan plan;
  plan.width = width;
  plan.height = height;
  plan.width_is_power_of_two = source_power_of_two_dim(width);
  plan.height_is_power_of_two = source_power_of_two_dim(height);
  plan.result = plan.width_is_power_of_two && plan.height_is_power_of_two;
  return plan;
}

SourceRndTexCheckDimPlan source_rndtex_check_dim_plan(
    int32_t dim,
    int32_t type,
    bool file,
    bool gfx_mode_zero) {
  SourceRndTexCheckDimPlan plan;
  plan.dim = dim;
  plan.type = type;
  plan.file = file;
  plan.gfx_mode_zero = gfx_mode_zero;
  if (dim == 0) {
    plan.zero_dimension_ok = true;
    return plan;
  }

  if (type == 4 && (dim % 16 != 0)) {
    plan.movie_multiple_of_16_required = true;
    plan.error = "%s: dimensions not multiple of 16";
  }
  if (gfx_mode_zero) {
    if (file && dim > 0x400) {
      plan.gfx_max_1024_required = true;
      plan.error = "%s: dimensions greater than 1024";
    } else if (dim > 0x800) {
      plan.gfx_max_2048_required = true;
      plan.error = "%s: dimensions greater than 2048";
    }
    if (dim % 8 != 0) {
      plan.gfx_multiple_of_8_required = true;
      plan.error = "%s: dimensions not multiple of 8";
    }
  }
  if (file) {
    plan.file_power_of_two_required = true;
    if (!source_power_of_two_dim(dim)) {
      plan.error = "%s: dimensions are not power-of-2";
    }
  }
  return plan;
}

SourceRndTexCheckSizePlan source_rndtex_check_size_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t num_mips,
    int32_t type,
    bool file,
    bool gfx_mode_zero) {
  SourceRndTexCheckSizePlan plan;
  plan.width = width;
  plan.height = height;
  plan.bpp = bpp;
  plan.num_mips = num_mips;
  plan.type = type;
  plan.file = file;
  plan.gfx_mode_zero = gfx_mode_zero;
  if (type == 0x0a2 || type == 0x122 || (type & 0x1000)) {
    plan.bypass_device_or_density = true;
    return plan;
  }

  plan.checked_width = true;
  const SourceRndTexCheckDimPlan width_check =
      source_rndtex_check_dim_plan(width, type, file, gfx_mode_zero);
  plan.error = width_check.error;
  if (plan.error.empty()) {
    plan.checked_height = true;
    const SourceRndTexCheckDimPlan height_check =
        source_rndtex_check_dim_plan(height, type, file, gfx_mode_zero);
    plan.error = height_check.error;
  }

  if (plan.error.empty()) {
    plan.checked_bpp = true;
    plan.bpp_valid =
        bpp == 4 || bpp == 8 || bpp == 0x10 || bpp == 0x18 || bpp == 0x20;
    if (!plan.bpp_valid) plan.error = "%s: invalid bpp";
  }

  plan.byte_size =
      (static_cast<int64_t>(width) * static_cast<int64_t>(height) *
       static_cast<int64_t>(bpp)) >>
      3;
  if (gfx_mode_zero) {
    plan.checked_total_size = true;
    if (plan.error.empty() && plan.byte_size > 0x7fff0) {
      plan.error = "%s: size over 524,272 bytes";
    }
    if (plan.error.empty() && (plan.byte_size & 0x0f) != 0) {
      plan.error = "%s: size not multiple of 16 bytes";
    }
    plan.checked_mip_count = true;
    if (plan.error.empty() && num_mips > 0) {
      plan.error = "%s: more than 0 mip levels";
    }
  }
  return plan;
}

SourceRndTexRenderedClampPlan source_rndtex_rendered_clamp_plan(
    const std::string& name,
    int32_t width,
    int32_t height,
    int32_t type,
    bool filepath_empty) {
  SourceRndTexRenderedClampPlan plan;
  plan.name = name;
  plan.initial_width = width;
  plan.initial_height = height;
  plan.result_width = width;
  plan.result_height = height;
  plan.type = type;
  plan.filepath_empty = filepath_empty;
  plan.rendered_type = (type & 2) != 0;
  plan.movie_exception = name == "movie.tex" || name == "movie_splash.tex";
  plan.clamped =
      filepath_empty && !plan.movie_exception && plan.rendered_type;
  if (plan.clamped) {
    while (plan.result_width > 0x100) plan.result_width /= 2;
    while (plan.result_height > 0x100) plan.result_height /= 2;
  }
  plan.result_power_of_two =
      source_power_of_two(plan.result_width, plan.result_height);
  return plan;
}

SourceRndTexCopyPlan source_rndtex_copy_plan(
    bool copy_from_max,
    int32_t source_type,
    int32_t destination_type) {
  SourceRndTexCopyPlan plan;
  plan.copy_from_max = copy_from_max;
  plan.source_type = source_type;
  plan.destination_type = destination_type;
  plan.copies_mip_map_k = !copy_from_max;
  plan.aborts_for_copy_from_max_type_mismatch =
      copy_from_max && source_type != destination_type;
  if (plan.aborts_for_copy_from_max_type_mismatch) return plan;

  plan.calls_presync_bitmap = true;
  plan.copies_type = true;
  plan.copies_dimensions = true;
  plan.recomputes_power_of_two = true;
  plan.copies_bpp = true;
  plan.copies_filepath = true;
  plan.copies_num_mips = true;
  plan.asserts_no_mips = true;
  plan.copies_optimize_for_ps3 = true;
  plan.creates_bitmap_from_source_bpp_order = true;
  plan.calls_sync_bitmap = true;
  return plan;
}

std::string source_rndtex_type_name(int32_t type) {
  if (type <= 0x22) {
    if (type <= 4) {
      if (type <= 2) {
        if (type < 2 && type > 0) return "Regular";
        if (type == 2) return "Rendered";
      } else if (type == 4) {
        return "Movie";
      }
    } else if (type <= 0x18) {
      if (type < 0x18 && type == 8) return "BackBuffer";
      if (type == 0x18) return "FrontBuffer";
    } else if (type == 0x22) {
      return "RenderedNoZ";
    }
  } else if (type <= 0x122) {
    if (type < 0x0a2 && type == 0x42) return "ShadowMap";
    if (type == 0x0a2) return "DepthVolumeMap";
    if (type == 0x122) return "DensityMap";
  } else if (type == 0x1000) {
    return "DeviceTexture";
  } else if (type < 0x1000 && type == 0x200) {
    return "Scratch";
  }
  return "";
}

SourceRndTexPrintPlan source_rndtex_print_plan() {
  SourceRndTexPrintPlan plan;
  plan.fields = {"width", "height", "bpp", "mipMapK", "file", "type"};
  return plan;
}

SourceRndTexHandlerPlan source_rndtex_handler_plan() {
  SourceRndTexHandlerPlan plan;
  plan.handlers = {"set_bitmap", "set_rendered", "file_path",
                   "set_file_path", "size_kb", "tex_type", "save_bmp",
                   "Hmx::Object"};
  return plan;
}

SourceRndTexOnSetBitmapPlan source_rndtex_on_set_bitmap_plan(
    int32_t data_array_size) {
  SourceRndTexOnSetBitmapPlan plan;
  plan.data_array_size = data_array_size;
  plan.uses_file_path_overload = data_array_size == 3;
  plan.uses_explicit_bitmap_overload = !plan.uses_file_path_overload;
  plan.explicit_argument_order = {"width", "height", "bpp", "type",
                                  "use_mips", "null_path"};
  return plan;
}

SourceRndTexOnSetRenderedPlan source_rndtex_on_set_rendered_plan(
    bool is_render_target,
    int32_t num_mips) {
  SourceRndTexOnSetRenderedPlan plan;
  plan.is_render_target = is_render_target;
  plan.num_mips = num_mips;
  plan.calls_set_bitmap = is_render_target;
  plan.use_mips = num_mips > 0;
  return plan;
}

SourceRndTexPropSyncPlan source_rndtex_prop_sync_plan() {
  SourceRndTexPropSyncPlan plan;
  plan.get_only_props = {"width", "height", "bpp"};
  plan.direct_props = {"mip_map_k", "optimize_for_ps3"};
  plan.modify_alt_props = {"file_path"};
  return plan;
}

SourceRndTexPlatformBppOrderPlan source_rndtex_platform_bpp_order_plan(
    const std::string& platform,
    const std::string& path_hint,
    int32_t input_bpp,
    bool has_alpha) {
  SourceRndTexPlatformBppOrderPlan plan;
  plan.platform = platform;
  plan.path_hint = path_hint;
  plan.has_alpha = has_alpha;
  plan.input_bpp = input_bpp;
  plan.result_bpp = input_bpp;
  plan.normal_texture = path_hint.find("_norm") != std::string::npos;

  if (platform == "wii") {
    plan.result_order = 8;
    if (has_alpha) {
      plan.result_order |= 0x100;
      plan.result_bpp = 8;
    } else {
      plan.result_bpp = 4;
    }
    plan.result_order |= 0x40;
  } else if (platform == "ps2") {
    plan.ps2_leaves_existing_values = true;
  } else if (platform == "xbox" || platform == "pc" ||
             platform == "ps3") {
    if (plan.normal_texture) {
      if (platform == "xbox") {
        plan.result_order = 0x20;
      } else if (platform == "ps3") {
        plan.result_order = 8;
      } else {
        plan.result_order = 0;
      }
    } else {
      plan.result_order = has_alpha ? 0x18 : 8;
    }

    if (plan.result_order == 8) {
      plan.result_bpp = 4;
    } else if ((plan.result_order & 0x38) != 0) {
      plan.result_bpp = 8;
    } else if (plan.normal_texture) {
      plan.result_bpp = 0x18;
    } else if (plan.result_bpp < 0x10) {
      plan.result_bpp = 0x10;
    }
  } else if (platform == "none") {
    plan.result_order = 0;
  }
  return plan;
}

SourceRndTexSetBitmapPlan source_rndtex_set_bitmap_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t type,
    bool use_mips,
    int32_t screen_width,
    int32_t screen_height,
    int32_t screen_bpp) {
  SourceRndTexSetBitmapPlan plan;
  plan.width = width;
  plan.height = height;
  plan.bpp = bpp;
  plan.type = type;
  plan.use_mips = use_mips;
  plan.result_width = width;
  plan.result_height = height;
  plan.result_bpp = bpp;

  if ((type & 8) != 0) {
    plan.back_buffer_uses_screen_values = true;
    plan.result_width = screen_width;
    plan.result_height = screen_height;
    plan.result_bpp = screen_bpp;
  } else if ((type & 2) != 0) {
    plan.rendered_counts_mips = use_mips;
    if (use_mips) {
      for (int32_t i = width, j = height; i > 0x10 && j > 0x10;
           i >>= 1, j >>= 1) {
        ++plan.rendered_mip_count;
      }
      plan.result_num_mips = plan.rendered_mip_count;
    }
  } else {
    plan.checks_size = true;
    plan.size_error =
        source_rndtex_check_size_plan(width, height, bpp, 0, type, false, true)
            .error;
    if (plan.size_error.empty()) {
      plan.skips_bitmap_for_special_type = (type & 0x204) != 0;
      plan.creates_bitmap = !plan.skips_bitmap_for_special_type;
      plan.asserts_before_generate_mips = plan.creates_bitmap && use_mips;
    }
  }
  return plan;
}

SourceRndTexSetBitmapFromBitmapPlan source_rndtex_set_bitmap_from_bitmap_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t order,
    int32_t num_mips,
    bool preserve_bitmap_format,
    const std::string& platform_name,
    const std::string& path_hint,
    bool has_alpha) {
  SourceRndTexSetBitmapFromBitmapPlan plan;
  plan.platform = platform_name;
  plan.bitmap_width = width;
  plan.bitmap_height = height;
  plan.bitmap_bpp = bpp;
  plan.bitmap_order = order;
  plan.bitmap_num_mips = num_mips;
  plan.preserve_bitmap_format = preserve_bitmap_format;
  plan.calls_platform_bpp_order = !preserve_bitmap_format;
  plan.create_bpp = bpp;
  plan.create_order = order;
  plan.size_error =
      source_rndtex_check_size_plan(width, height, bpp, num_mips, 1, false, true)
          .error;
  plan.resets_on_size_error = !plan.size_error.empty();
  if (!plan.resets_on_size_error && plan.calls_platform_bpp_order) {
    const SourceRndTexPlatformBppOrderPlan platform =
        source_rndtex_platform_bpp_order_plan(platform_name, path_hint, bpp,
                                              has_alpha);
    plan.create_bpp = platform.result_bpp;
    plan.create_order = platform.result_order;
  }
  return plan;
}

SourceRndTexSetBitmapFromLoaderPlan source_rndtex_set_bitmap_from_loader_plan(
    bool has_loader,
    bool has_buffer,
    bool loader_is_current,
    bool edit_mode,
    const std::string& filepath,
    bool use_bottom_mip,
    int32_t bitmap_width,
    int32_t bitmap_height,
    int32_t bitmap_bpp,
    int32_t bitmap_num_mips) {
  SourceRndTexSetBitmapFromLoaderPlan plan;
  plan.has_loader = has_loader;
  plan.has_buffer = has_loader && has_buffer;
  plan.loader_is_current = loader_is_current;
  plan.edit_mode = edit_mode;
  plan.filepath = has_loader ? filepath : "";
  plan.uses_bottom_mip = use_bottom_mip;
  plan.warns_disc_build_without_keep =
      has_loader && !edit_mode && !loader_is_current &&
      filepath.find("_keep") == std::string::npos;
  if (plan.has_buffer) {
    plan.creates_bitmap_from_buffer = !use_bottom_mip;
    plan.copies_bottom_mip = use_bottom_mip;
    plan.result_width = bitmap_width;
    plan.result_height = bitmap_height;
    plan.result_bpp = bitmap_bpp;
    plan.result_num_mips = bitmap_num_mips;
  } else {
    plan.resets_bitmap_and_dimensions = true;
    plan.result_width = 0;
    plan.result_height = 0;
    plan.result_bpp = 0x20;
    plan.result_num_mips = 0;
  }
  return plan;
}

SourceRndTexCopyBottomMipPlan source_rndtex_copy_bottom_mip_plan(
    int32_t source_mip_count) {
  SourceRndTexCopyBottomMipPlan plan;
  plan.source_mip_count = source_mip_count;
  plan.walks_to_last_mip = source_mip_count > 0;
  plan.selected_mip_index = source_mip_count > 0 ? source_mip_count : 0;
  return plan;
}

SourceRndTexLockBitmapPlan source_rndtex_lock_bitmap_plan(
    int32_t bitmap_order,
    int32_t bitmap_bpp) {
  SourceRndTexLockBitmapPlan plan;
  plan.bitmap_order = bitmap_order;
  plan.converts_ordered_bitmap_to_32bpp = (bitmap_order & 0x38) != 0;
  plan.creates_direct_bitmap_view = !plan.converts_ordered_bitmap_to_32bpp;
  plan.create_bpp = plan.converts_ordered_bitmap_to_32bpp ? 0x20 : bitmap_bpp;
  plan.create_order = plan.converts_ordered_bitmap_to_32bpp ? 0 : bitmap_order;
  return plan;
}

SourceRndBitmapResetPlan source_rndbitmap_reset_plan(
    bool has_buffer,
    bool has_mip) {
  SourceRndBitmapResetPlan plan;
  plan.frees_buffer_when_present = has_buffer;
  plan.resets_and_frees_mip_when_present = has_mip;
  return plan;
}

SourceRndBitmapCreatePlan source_rndbitmap_create_plan(
    int32_t width,
    int32_t height,
    int32_t row_bytes,
    int32_t bpp,
    int32_t order,
    bool has_palette,
    bool has_buffer) {
  SourceRndBitmapCreatePlan plan;
  plan.width = width;
  plan.height = height;
  plan.row_bytes = row_bytes;
  plan.bpp = bpp;
  plan.order = order;
  plan.valid_dimensions = width >= 0 && height >= 0;
  plan.valid_bpp =
      bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32;
  plan.frees_palette_argument_after_assignment = has_palette;
  plan.allocates_when_no_palette_and_no_buffer = !has_palette && !has_buffer;
  return plan;
}

SourceRndBitmapSetMipPlan source_rndbitmap_set_mip_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t order,
    bool has_mip,
    int32_t mip_width,
    int32_t mip_height,
    int32_t mip_bpp,
    int32_t mip_order) {
  SourceRndBitmapSetMipPlan plan;
  plan.width = width;
  plan.height = height;
  plan.bpp = bpp;
  plan.order = order;
  plan.has_mip = has_mip;
  plan.mip_width = mip_width;
  plan.mip_height = mip_height;
  plan.mip_bpp = mip_bpp;
  plan.mip_order = mip_order;
  plan.checks_half_dimensions = has_mip;
  plan.accepts_mip = has_mip && width / 2 == mip_width &&
                     height / 2 == mip_height && bpp == mip_bpp &&
                     order == mip_order;
  return plan;
}

SourceRndBitmapLoadSafelyPlan source_rndbitmap_load_safely_plan(
    int32_t width,
    int32_t height,
    int32_t bpp,
    int32_t row_bytes,
    int32_t max_width,
    int32_t max_height,
    int32_t mip_count) {
  SourceRndBitmapLoadSafelyPlan plan;
  plan.width = width;
  plan.height = height;
  plan.bpp = bpp;
  plan.row_bytes = row_bytes;
  plan.max_width = max_width;
  plan.max_height = max_height;
  plan.mip_count = mip_count;
  plan.dimension_fallback = width > max_width || height > max_height;
  plan.row_bytes_fallback =
      !plan.dimension_fallback && ((bpp * width) / 8 != row_bytes);
  plan.creates_8x8_32bpp_fallback =
      plan.dimension_fallback || plan.row_bytes_fallback;
  plan.reads_palette_and_pixels = !plan.creates_8x8_32bpp_fallback;
  plan.builds_mip_chain = plan.reads_palette_and_pixels && mip_count > 0;
  plan.result = plan.reads_palette_and_pixels;
  return plan;
}

SourceRndBitmapNumMipsPlan source_rndbitmap_num_mips_plan(
    int32_t linked_mip_count) {
  SourceRndBitmapNumMipsPlan plan;
  plan.linked_mip_count = std::max(0, linked_mip_count);
  plan.returned_mip_count = plan.linked_mip_count;
  return plan;
}

SourceRndBitmapPixelBytesPlan source_rndbitmap_pixel_bytes_plan(
    int32_t row_bytes,
    int32_t height) {
  SourceRndBitmapPixelBytesPlan plan;
  plan.row_bytes = row_bytes;
  plan.height = height;
  plan.result = row_bytes * height;
  return plan;
}

SourceReadChunksPlan source_read_chunks_plan(
    int32_t total_len,
    int32_t max_chunk_size) {
  SourceReadChunksPlan plan;
  plan.total_len = total_len;
  plan.max_chunk_size = max_chunk_size;
  if (total_len <= 0 || max_chunk_size <= 0) return plan;
  int32_t curr_size = 0;
  while (curr_size != total_len) {
    const int32_t len_left =
        std::min(total_len - curr_size, max_chunk_size);
    plan.chunk_sizes.push_back(len_left);
    curr_size += len_left;
  }
  return plan;
}

SourceRndBitmapSaveHeaderPlan source_rndbitmap_save_header_plan(
    int32_t bpp,
    int32_t order,
    int32_t num_mips,
    int32_t width,
    int32_t height,
    int32_t row_bytes) {
  SourceRndBitmapSaveHeaderPlan plan;
  plan.bpp = bpp;
  plan.order = order;
  plan.num_mips = num_mips;
  plan.width = width;
  plan.height = height;
  plan.row_bytes = row_bytes;
  plan.write_order = {"BITMAP_REV", "mBpp", "mOrder", "NumMips",
                      "mWidth", "mHeight", "mRowBytes", "pad[0x13]"};
  return plan;
}

SourceRndBitmapSavePlan source_rndbitmap_save_plan(
    int32_t bpp,
    int32_t order,
    bool has_palette,
    const std::vector<int32_t>& row_bytes,
    const std::vector<int32_t>& heights) {
  SourceRndBitmapSavePlan plan;
  plan.has_palette = has_palette;
  plan.palette_bytes =
      has_palette ? source_bitmap_palette_bytes(bpp, static_cast<uint32_t>(order))
                  : 0;
  plan.writes_palette = has_palette;
  const size_t count = std::min(row_bytes.size(), heights.size());
  for (size_t i = 0; i < count; ++i) {
    plan.pixel_write_bytes.push_back(row_bytes[i] * heights[i]);
  }
  return plan;
}

SourceRndBitmapDetachMipPlan source_rndbitmap_detach_mip_plan(bool had_mip) {
  SourceRndBitmapDetachMipPlan plan;
  plan.had_mip = had_mip;
  plan.returns_existing_mip = had_mip;
  return plan;
}

SourceRndBitmapSamePixelFormatPlan source_rndbitmap_same_pixel_format_plan(
    int32_t lhs_bpp,
    int32_t rhs_bpp,
    int32_t lhs_order,
    int32_t rhs_order,
    bool lhs_has_palette,
    bool rhs_has_palette,
    bool same_palette_colors) {
  SourceRndBitmapSamePixelFormatPlan plan;
  plan.lhs_bpp = lhs_bpp;
  plan.rhs_bpp = rhs_bpp;
  plan.lhs_order = lhs_order;
  plan.rhs_order = rhs_order;
  plan.lhs_has_palette = lhs_has_palette;
  plan.rhs_has_palette = rhs_has_palette;
  plan.same_palette_colors = same_palette_colors;
  if (lhs_bpp != rhs_bpp || lhs_order != rhs_order) {
    plan.result = false;
    return plan;
  }
  plan.calls_same_palette_colors = lhs_has_palette && rhs_has_palette;
  plan.result = plan.calls_same_palette_colors ? same_palette_colors : true;
  return plan;
}

SourceRndBitmapBltPlan source_rndbitmap_blt_plan(
    int32_t dest_width,
    int32_t dest_height,
    int32_t source_width,
    int32_t source_height,
    int32_t dest_x,
    int32_t dest_y,
    int32_t source_x,
    int32_t source_y,
    int32_t width,
    int32_t height,
    bool same_pixel_format) {
  SourceRndBitmapBltPlan plan;
  plan.dest_width = dest_width;
  plan.dest_height = dest_height;
  plan.source_width = source_width;
  plan.source_height = source_height;
  plan.dest_x = dest_x;
  plan.dest_y = dest_y;
  plan.source_x = source_x;
  plan.source_y = source_y;
  plan.width = width;
  plan.height = height;
  plan.dest_width_assert = dest_x + width <= dest_width;
  plan.dest_height_assert = dest_y + height <= dest_height;
  plan.source_width_assert = source_x + width <= source_width;
  plan.source_height_assert = source_y + height <= source_height;
  plan.same_pixel_format = same_pixel_format;
  plan.reaches_empty_mismatch_body = !same_pixel_format;
  return plan;
}

SourceBitmapFileHeaderStreamPlan source_bitmap_file_header_stream_plan() {
  SourceBitmapFileHeaderStreamPlan plan;
  plan.read_order = {"bfSize", "bfReserved1", "bfReserved2", "bfOffBits"};
  plan.write_order = plan.read_order;
  return plan;
}

SourceBitmapInfoHeaderStreamPlan source_bitmap_info_header_stream_plan() {
  SourceBitmapInfoHeaderStreamPlan plan;
  plan.read_order = {"biSize", "biWidth", "biHeight", "biPlanes",
                     "biBitCount", "biCompression", "biSizeImage",
                     "biXPelsPerMeter", "biYPelsPerMeter", "biClrUsed",
                     "biClrImportant"};
  plan.write_order = plan.read_order;
  return plan;
}

SourcePreMultiplyAlphaPlan source_premultiply_alpha_plan() {
  return SourcePreMultiplyAlphaPlan{};
}

SourceRndBitmapColumnNonTransparentPlan
source_rndbitmap_column_nontransparent_plan(
    int32_t x,
    int32_t y,
    const std::vector<uint8_t>& alpha_samples) {
  SourceRndBitmapColumnNonTransparentPlan plan;
  plan.x = x;
  plan.y = y;
  plan.height = static_cast<int32_t>(alpha_samples.size());
  plan.alpha_samples = alpha_samples;
  plan.last_transparent_y = y;
  for (uint8_t alpha : alpha_samples) {
    if (alpha != 0) {
      plan.returns_true = false;
      return plan;
    }
    plan.writes_last_transparent_y = true;
    plan.last_transparent_y = y;
  }
  plan.returns_true = false;
  return plan;
}

RndTex decode_rnd_tex(const std::string& entry_name,
                      const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  RndTex tex;
  tex.name = entry_name;
  const uint32_t packed_rev = r.u32();
  tex.version = source_hmx_rev(packed_rev);
  tex.alt_version = source_alt_rev(packed_rev);
  const SourceRndTexLoadPlan plan =
      source_rndtex_load_plan(tex.version, tex.alt_version, false);
  if (!plan.accepted_revision) {
    throw std::runtime_error("char_mesh: RndTex revision outside source range");
  }
  if (plan.reads_object_fields) read_object_fields(r);
  if (plan.reads_short_dimensions) {
    tex.width = static_cast<int16_t>(r.u16());
    tex.height = static_cast<int16_t>(r.u16());
  } else {
    tex.width = r.i32();
    tex.height = r.i32();
  }
  tex.power_of_two = source_power_of_two(tex.width, tex.height);
  tex.bpp = r.i32();
  tex.filepath = r.str();

  if (plan.reads_legacy_cubemap_mask) {
    tex.cubemap_mask = r.i32();
    if (tex.cubemap_mask != 0 && !tex.filepath.empty()) {
      if (tex.cubemap_mask & 2) {
        source_insert_tex_suffix(tex.filepath, "_tb");
      } else if (tex.cubemap_mask & 0x10) {
        source_insert_tex_suffix(tex.filepath, "_ga");
      } else if (tex.cubemap_mask & 0x20) {
        source_insert_tex_suffix(tex.filepath, "_gw");
      }
    }
  }
  if (plan.reads_legacy_bool) {
    tex.has_legacy_flag = true;
    tex.legacy_flag = r.u8() != 0;
  }
  if (plan.reads_float_mip_map_k) {
    tex.mip_map_k = r.f32();
  } else if (plan.reads_fixed_mip_map_k) {
    const int32_t mip = r.i32();
    tex.mip_map_k = mip / 16.0f;
  }

  if (plan.reads_direct_type) {
    tex.type = r.i32();
  } else if (plan.reads_legacy_type_index) {
    static constexpr int32_t kLegacyTypes[] = {1, 2, 4, 8, 0x18};
    const int32_t type_index = r.i32();
    if (type_index < 0 ||
        type_index >= static_cast<int32_t>(sizeof(kLegacyTypes) /
                                           sizeof(kLegacyTypes[0]))) {
      throw std::runtime_error("char_mesh: RndTex legacy type index out of range");
    }
    tex.type = kLegacyTypes[type_index];
  } else if (plan.reads_rendered_bool_type) {
    const bool rendered = r.u8() != 0;
    tex.type = rendered ? 2 : 1;
  }

  const SourceRndTexRenderedClampPlan clamp_plan =
      source_rndtex_rendered_clamp_plan(tex.name, tex.width, tex.height,
                                        tex.type, tex.filepath.empty());
  if (clamp_plan.clamped) {
    tex.width = clamp_plan.result_width;
    tex.height = clamp_plan.result_height;
    tex.power_of_two = clamp_plan.result_power_of_two;
  }
  if (plan.reads_post_flag) {
    tex.has_post_flag = true;
    tex.post_flag = r.u8() != 0;
  }
  if (plan.reads_optimize_for_ps3) {
    tex.optimize_for_ps3 = r.u8() != 0;
  }
  tex.cached_bitmap_bytes = r.n - r.pos;
  const SourceRndTexLoadPlan payload_plan = source_rndtex_load_plan(
      tex.version, tex.alt_version, tex.cached_bitmap_bytes > 0);
  if (payload_plan.delegates_cached_payload_to_bitmap) {
    try {
      Reader bitmap(r.p + r.pos, tex.cached_bitmap_bytes);
      tex.bitmap_version = bitmap.u8();
      tex.bitmap_bpp = bitmap.u8();
      if (tex.bitmap_version != 0) {
        tex.bitmap_order = bitmap.u32();
      } else {
        tex.bitmap_order = bitmap.u8();
      }
      tex.bitmap_mip_count = bitmap.u8();
      tex.bitmap_width = bitmap.u16();
      tex.bitmap_height = bitmap.u16();
      tex.bitmap_row_bytes = bitmap.u16();
      bitmap.skip(tex.bitmap_version != 0 ? 0x13 : 6);
      tex.bitmap_header_decoded = true;
      tex.cached_bitmap_payload_bytes = bitmap.n - bitmap.pos;
      tex.bitmap_palette_bytes =
          source_bitmap_palette_bytes(tex.bitmap_bpp, tex.bitmap_order);
      if (tex.bitmap_height >= 0 && tex.bitmap_row_bytes >= 0) {
        tex.bitmap_base_pixel_bytes =
            static_cast<size_t>(tex.bitmap_row_bytes) *
            static_cast<size_t>(tex.bitmap_height);
      }
      tex.bitmap_mip_pixel_bytes = source_bitmap_mip_pixel_bytes(
          tex.bitmap_width, tex.bitmap_height, tex.bitmap_bpp,
          tex.bitmap_mip_count);
      tex.bitmap_expected_payload_bytes =
          tex.bitmap_palette_bytes + tex.bitmap_base_pixel_bytes +
          tex.bitmap_mip_pixel_bytes;
      tex.bitmap_payload_size_matches =
          tex.bitmap_expected_payload_bytes == tex.cached_bitmap_payload_bytes;
      if (tex.cached_bitmap_payload_bytes > 0) {
        tex.cached_bitmap_payload_prefix_hex =
            hex_bytes(bitmap.p + bitmap.pos,
                      std::min<size_t>(tex.cached_bitmap_payload_bytes, 32));
      }
    } catch (const std::exception& ex) {
      tex.bitmap_header_error = ex.what();
      tex.cached_bitmap_payload_prefix_hex =
          hex_bytes(r.p + r.pos, std::min<size_t>(tex.cached_bitmap_bytes, 32));
    }
  }
  return tex;
}

namespace {

CharDriver decode_driver_body(const std::string& entry_name, Reader& r,
                              bool midi) {
  CharDriver driver;
  driver.name = entry_name;
  driver.version = r.i32();  // CharDriver version, observed 3 in GH2.
  read_object_fields(r);  // Hmx::Object metadata
  driver.weightable_version = r.i32();  // CharWeightable version.
  driver.weight = r.f32();
  if (driver.weightable_version > 1) driver.weight_owner = r.str();
  driver.weight_prop = driver.weight_owner;
  driver.target = r.str();
  driver.clip_milo = r.str();
  if (r.pos < r.n) driver.realign = r.u8() != 0;
  driver.midi = midi;
  return driver;
}

CharDriver decode_driver(const std::string& entry_name,
                         const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  return decode_driver_body(entry_name, r, false);
}

CharDriver decode_driver_midi(const std::string& entry_name,
                              const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  const int32_t midi_version = r.i32();  // CharDriverMidi version.
  if (midi_version < 0 || midi_version > 7) {
    throw std::runtime_error(
        "char_mesh: CharDriverMidi revision outside source range");
  }
  CharDriver driver = decode_driver_body(entry_name, r, true);
  driver.midi_version = midi_version;
  if (driver.midi_version < 7 && r.pos < r.n) driver.midi_default_clip = r.str();
  if (driver.midi_version == 2 && r.pos < r.n) driver.midi_legacy_string = r.str();
  if (driver.midi_version > 3 && r.pos < r.n) driver.midi_parser = r.str();
  if (driver.midi_version > 4 && r.pos < r.n) driver.midi_flag_parser = r.str();
  if (driver.midi_version > 5 && r.pos + 4 <= r.n)
    driver.midi_blend_override_pct = r.f32();
  driver.midi_unread_bytes = r.n - r.pos;
  return driver;
}

CharWeightSetter decode_weight_setter(const std::string& entry_name,
                                      const std::vector<uint8_t>& body) {
  Reader r(body.data(), body.size());
  CharWeightSetter setter;
  setter.name = entry_name;
  setter.version = r.i32();
  if (setter.version < 0 || setter.version > 9) {
    throw std::runtime_error(
        "char_mesh: CharWeightSetter revision outside source range");
  }
  read_object_fields(r);  // Hmx::Object metadata
  if (setter.version > 1) {
    setter.weightable_version = r.i32();
    setter.weight = r.f32();
    if (setter.weightable_version > 1) setter.weight_owner = r.str();
  }
  setter.weight_prop = setter.weight_owner;
  setter.driver = r.str();
  setter.flags = r.u32();
  setter.mask = setter.flags;
  if (setter.version < 3) {
    setter.scale = 1.0f;
    setter.offset = 0.0f;
  } else if (setter.version < 4) {
    const bool invert = r.u8() != 0;
    setter.scale = invert ? -1.0f : 1.0f;
    setter.offset = invert ? 1.0f : 0.0f;
  } else {
    setter.offset = r.f32();
    setter.scale = r.f32();
  }
  if (setter.version < 2 && r.pos + 4 <= r.n) {
    (void)read_obj_ptr_list(r);
  }
  if (setter.version > 4) {
    setter.base_weight = r.f32();
    setter.beats_per_weight = r.f32();
  } else {
    setter.base_weight = setter.weight;
    setter.beats_per_weight = 0.0f;
  }
  if (setter.version > 5) setter.base = r.str();
  if (setter.version > 8) {
    setter.min_weights = read_obj_ptr_list(r);
    setter.max_weights = read_obj_ptr_list(r);
  } else {
    if (setter.version > 6) {
      const std::string min_weight = r.str();
      if (!min_weight.empty()) setter.min_weights.push_back(min_weight);
    }
    if (setter.version > 7) {
      const std::string max_weight = r.str();
      if (!max_weight.empty()) setter.max_weights.push_back(max_weight);
    }
  }
  setter.unread_bytes = r.n - r.pos;
  return setter;
}

}  // namespace

SourceRndAnimFilterSetAnimPlan source_rnd_anim_filter_set_anim(
    RndAnimFilter& filter,
    const std::string& anim,
    const SourceRndAnimFilterAnimInfo& anim_info) {
  SourceRndAnimFilterSetAnimPlan plan;
  filter.anim = anim;
  plan.anim_present = anim_info.present;
  if (anim_info.present) {
    filter.rate = anim_info.rate;
    filter.start = anim_info.start;
    filter.end = anim_info.end;
    plan.copies_rate = true;
    plan.copies_start_end = true;
  }
  return plan;
}

bool source_rnd_anim_filter_loop(int32_t type) {
  return type >= 1;
}

SourceRndAnimFilterScaleResult source_rnd_anim_filter_scale(
    float start,
    float end,
    float scale,
    float period,
    float frames_per_unit) {
  SourceRndAnimFilterScaleResult result;
  if (period != 0.0f) {
    result.period_path = true;
    result.scale = (end - start) / (period * frames_per_unit);
  } else {
    result.reversed_range = end < start;
    result.scale = result.reversed_range ? -scale : scale;
  }
  return result;
}

SourceRndAnimFilterFrameOffsetResult source_rnd_anim_filter_frame_offset(
    float start,
    float end,
    float offset) {
  SourceRndAnimFilterFrameOffsetResult result;
  float range_offset = start;
  if (end >= start) {
    range_offset = 0.0f;
  } else {
    range_offset -= end;
    result.reversed_range = true;
  }
  result.frame_offset = offset + range_offset;
  return result;
}

SourceRndAnimFilterFrameBoundsResult source_rnd_anim_filter_frame_bounds(
    const RndAnimFilter& filter,
    float frames_per_unit) {
  SourceRndAnimFilterFrameBoundsResult result;
  result.has_anim = !filter.anim.empty();
  if (!result.has_anim) return result;

  result.scale = source_rnd_anim_filter_scale(
                     filter.start, filter.end, filter.scale, filter.period,
                     frames_per_unit)
                     .scale;
  if (result.scale == 0.0f) {
    result.scale_was_zero = true;
    result.scale = 1.0f;
  }
  result.frame_offset =
      source_rnd_anim_filter_frame_offset(filter.start, filter.end,
                                          filter.offset)
          .frame_offset;
  result.start_frame = (filter.start - result.frame_offset) / result.scale;
  result.end_frame = (filter.end - result.frame_offset) / result.scale;
  result.shuttle = filter.type == 2;
  if (result.shuttle) result.end_frame *= 2.0f;
  return result;
}

std::string source_rnd_anim_filter_anim_target(const RndAnimFilter& filter) {
  return filter.anim;
}

std::vector<std::string> source_rnd_anim_filter_list_anim_children(
    const RndAnimFilter& filter) {
  if (filter.anim.empty()) return {};
  return {filter.anim};
}

SourceRndAnimFilterCopyPlan source_rnd_anim_filter_copy_plan(
    bool copy_from_max) {
  SourceRndAnimFilterCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndAnimatable"};
  plan.copy_from_max = copy_from_max;
  if (!copy_from_max) {
    plan.copied_members = {"mScale", "mOffset", "mStart",  "mEnd",
                           "mType",  "mAnim",   "mPeriod", "mSnap",
                           "mJitter"};
  }
  return plan;
}

SourceRndAnimFilterSafeAnimsResult source_rnd_anim_filter_safe_anims(
    const std::vector<std::pair<std::string, bool>>& anim_contains_self) {
  SourceRndAnimFilterSafeAnimsResult result;
  for (const auto& [anim, contains_self] : anim_contains_self) {
    if (!contains_self) result.safe_anims.push_back(anim);
  }
  return result;
}

SourceRndAnimFilterHandlerPlan source_rnd_anim_filter_handler_plan() {
  SourceRndAnimFilterHandlerPlan plan;
  plan.handlers = {"safe_anims"};
  plan.superclasses = {"RndAnimatable", "Hmx::Object"};
  return plan;
}

SourceRndAnimFilterPropSyncPlan source_rnd_anim_filter_prop_sync_plan() {
  SourceRndAnimFilterPropSyncPlan plan;
  plan.set_properties = {"anim:SetAnim", "scale:abs"};
  plan.properties = {"offset", "period", "start", "end", "snap", "type"};
  plan.modify_properties = {"jitter:reset_jitter_frame"};
  plan.superclasses = {"RndAnimatable"};
  return plan;
}

SourceRndAnimFilterSavePlan source_rnd_anim_filter_save_plan() {
  return SourceRndAnimFilterSavePlan{};
}

SourceEventTriggerLoadPlan source_event_trigger_load_plan(int revision) {
  SourceEventTriggerLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 0x11;
  if (!plan.known_revision) return plan;

  plan.load_steps = {"LOAD_REVS", "Hmx::Object"};
  if (revision > 0x0f) plan.load_steps.push_back("RndAnimatable");
  plan.load_steps.push_back("UnregisterEvents");

  if (revision > 9) {
    plan.load_steps.push_back("mTriggerEvents");
  } else if (revision > 6) {
    plan.load_steps.push_back("legacyTriggerEvent");
  }

  if (revision > 6) {
    plan.load_steps.push_back("mAnims");
    plan.load_steps.push_back("mSounds");
    plan.load_steps.push_back("mShows");
  }
  if (revision > 0x0c) {
    plan.load_steps.push_back("mHideDelays");
  } else {
    plan.load_steps.push_back("legacyHideDelayGrossBranch");
  }
  if (revision > 2) {
    plan.load_steps.push_back("mEnableEvents");
    plan.load_steps.push_back("mDisableEvents");
  }
  if (revision > 5) plan.load_steps.push_back("mWaitForEvents");
  if (revision > 6) plan.load_steps.push_back("mNextLink");
  if (revision < 10) {
    plan.load_steps.push_back("RemoveNullEvents(mEnableEvents)");
    plan.load_steps.push_back("RemoveNullEvents(mDisableEvents)");
    plan.load_steps.push_back("RemoveNullEvents(mWaitForEvents)");
  }
  if (revision < 7) plan.load_steps.push_back("legacyIteratorJank");
  if (revision > 7) plan.load_steps.push_back("mProxyCalls");
  if (revision > 0x0b) plan.load_steps.push_back("mTriggerOrderInt");
  if (revision > 0x0d) plan.load_steps.push_back("mResetTriggers");
  if (revision > 0x0e) plan.load_steps.push_back("unkdfBitfield");
  if (revision > 0x0f) {
    plan.load_steps.push_back("mAnimTriggerInt");
    plan.load_steps.push_back("mAnimFrame");
  }
  if (revision > 0x10) plan.load_steps.push_back("mPartLaunchers");

  plan.load_steps.push_back("CleanupEventCase(mTriggerEvents)");
  plan.load_steps.push_back("CleanupEventCase(mEnableEvents)");
  plan.load_steps.push_back("CleanupEventCase(mDisableEvents)");
  plan.load_steps.push_back("CleanupEventCase(mWaitForEvents)");
  plan.load_steps.push_back("RegisterEvents");
  plan.load_steps.push_back("CleanupHideShow");
  plan.load_steps.push_back("ConvertParticleTriggerType");

  plan.anim.read_order = {"mAnim", "mBlend", "mWait", "mDelay"};
  if (revision > 9) {
    plan.anim.read_order.push_back("mEnable");
    plan.anim.read_order.push_back("mRateInt");
    plan.anim.read_order.push_back("mStart");
    plan.anim.read_order.push_back("mEnd");
    plan.anim.read_order.push_back("mPeriod");
    plan.anim.read_order.push_back("mType");
    plan.anim.read_order.push_back("mScale");
  } else {
    plan.anim.reset_anim_for_legacy = true;
  }

  plan.proxy_call.read_order = {"mProxy", "mCall"};
  if (revision > 10) plan.proxy_call.read_order.push_back("mEvent");
  plan.hide_delay_read_order = {"mHide", "mDelay", "mRate"};
  return plan;
}

SourceEventTriggerDefaultState source_event_trigger_default_state() {
  return {};
}

SourceEventTriggerSupportedEventsPlan source_event_trigger_supported_events_plan(
    bool type_is_endgame_action) {
  SourceEventTriggerSupportedEventsPlan plan;
  plan.uses_endgame_action_type_path = type_is_endgame_action;
  plan.config_path = type_is_endgame_action
                         ? std::vector<std::string>{"objects", "EventTrigger",
                                                    "types", "endgame_action",
                                                    "supported_events"}
                         : std::vector<std::string>{"objects", "EventTrigger",
                                                    "supported_events"};
  return plan;
}

namespace {

void append_event_trigger_sink_rows(std::vector<SourceEventTriggerSinkRow>& rows,
                                    const std::vector<std::string>& events,
                                    const std::string& message) {
  for (const std::string& event : events) {
    rows.push_back(SourceEventTriggerSinkRow{event, message, "kHandle"});
  }
}

}  // namespace

SourceEventTriggerEventRegistrationPlan source_event_trigger_register_events_plan(
    const EventTrigger& trigger,
    bool dir_is_msg_source) {
  SourceEventTriggerEventRegistrationPlan plan;
  plan.dir_is_msg_source = dir_is_msg_source;
  if (!dir_is_msg_source) return plan;
  append_event_trigger_sink_rows(plan.add_sinks, trigger.trigger_events,
                                 "trigger");
  append_event_trigger_sink_rows(plan.add_sinks, trigger.enable_events,
                                 "enable");
  append_event_trigger_sink_rows(plan.add_sinks, trigger.disable_events,
                                 "disable");
  append_event_trigger_sink_rows(plan.add_sinks, trigger.wait_for_events,
                                 "wait_for");
  plan.clears_enabled_at_start = true;
  return plan;
}

SourceEventTriggerEventRegistrationPlan
source_event_trigger_unregister_events_plan(const EventTrigger& trigger,
                                            bool dir_is_msg_source) {
  SourceEventTriggerEventRegistrationPlan plan;
  plan.dir_is_msg_source = dir_is_msg_source;
  if (!dir_is_msg_source) return plan;
  append_event_trigger_sink_rows(plan.remove_sinks, trigger.trigger_events,
                                 "trigger");
  append_event_trigger_sink_rows(plan.remove_sinks, trigger.enable_events,
                                 "enable");
  append_event_trigger_sink_rows(plan.remove_sinks, trigger.disable_events,
                                 "disable");
  append_event_trigger_sink_rows(plan.remove_sinks, trigger.wait_for_events,
                                 "wait_for");
  return plan;
}

SourceEventTriggerCopyPlan source_event_trigger_copy_plan() {
  SourceEventTriggerCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndAnimatable"};
  plan.pre_copy_steps = {"UnregisterEvents"};
  plan.copied_members = {"mTriggerEvents", "mAnims",         "mSounds",
                         "mProxyCalls",    "mShows",         "mHideDelays",
                         "mEnableEvents",  "mDisableEvents", "mWaitForEvents",
                         "mNextLink",      "mTriggerOrder",  "mResetTriggers",
                         "unkdf",          "mAnimTrigger",   "mAnimFrame",
                         "mPartLaunchers"};
  plan.post_copy_steps = {"RegisterEvents", "CleanupHideShow"};
  plan.not_copied_members = {"mSpawnedTasks", "unkbc", "unkcc", "unkde",
                             "mEnabled", "mEnabledAtStart"};
  return plan;
}

SourceEventTriggerHandlerPlan source_event_trigger_handler_plan() {
  SourceEventTriggerHandlerPlan plan;
  plan.handlers = {"trigger:OnTrigger", "proxy_calls:OnProxyCalls"};
  plan.action_handlers = {"enable:unkdf=true",
                          "disable:unkdf=false",
                          "wait_for:unkdf=true;Trigger()",
                          "basic_cleanup:BasicReset"};
  plan.direct_returns = {"supported_events:SupportedEvents"};
  plan.superclasses = {"RndAnimatable", "Hmx::Object"};
  plan.check = 0x3af;
  return plan;
}

SourceEventTriggerPropSyncPlan source_event_trigger_prop_sync_plan() {
  SourceEventTriggerPropSyncPlan plan;
  plan.anim_props = {"anim:ResetAnim", "blend", "wait",  "delay",
                     "enable",         "rate",  "start", "end",
                     "scale",          "period", "type"};
  plan.proxy_call_props = {"proxy:clear_call", "call", "event"};
  plan.hide_delay_props = {"hide", "delay", "rate"};
  plan.event_list_props = {"trigger_events", "enable_events",
                           "disable_events", "wait_for_events"};
  plan.event_lists_unregister_before_mutation = true;
  plan.event_lists_register_after_mutation = true;
  plan.properties = {"anims:CheckAnims",
                     "proxy_calls",
                     "sounds",
                     "shows",
                     "hide_delays",
                     "part_launchers",
                     "enabled",
                     "enabled_at_start",
                     "next_link:SetNextLink",
                     "trigger_order",
                     "triggers_to_reset",
                     "anim_trigger",
                     "anim_frame"};
  plan.superclasses = {"RndAnimatable"};
  return plan;
}

CharHair decode_hair(const std::string& entry_name,
                     const std::vector<uint8_t>& body) {
  return decode_hair_body(entry_name, body);
}

CharCollide decode_collide(const std::string& entry_name,
                           const std::vector<uint8_t>& body,
                           int32_t parent_dir_revision) {
  return decode_collide_body(entry_name, body, parent_dir_revision);
}

CharPosConstraint decode_pos_constraint(const std::string& entry_name,
                                        const std::vector<uint8_t>& body) {
  return decode_pos_constraint_body(entry_name, body);
}

CharLookAt decode_lookat(const std::string& entry_name,
                         const std::vector<uint8_t>& body) {
  return decode_lookat_body(entry_name, body);
}

CharEyes decode_eyes(const std::string& entry_name,
                     const std::vector<uint8_t>& body) {
  return decode_eyes_body(entry_name, body);
}

SourceCharPosConstraintLoadPlan source_char_pos_constraint_load_plan(
    int revision) {
  SourceCharPosConstraintLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 2;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "Hmx::Object", "mTargets", "mSrc"};
  if (revision > 1) {
    plan.read_order.push_back("mBox");
  } else {
    plan.branches.push_back("mBox.min=(1,1,0)");
    plan.branches.push_back("mBox.max=(-1,-1,1000)");
  }
  return plan;
}

SourceCharPosConstraintSavePlan source_char_pos_constraint_save_plan() {
  return SourceCharPosConstraintSavePlan{};
}

SourceCharPosConstraintCopyPlan source_char_pos_constraint_copy_plan() {
  SourceCharPosConstraintCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mTargets", "mSrc", "mBox"};
  return plan;
}

SourceCharPosConstraintPollDepsPlan source_char_pos_constraint_poll_deps_plan(
    const std::string& source,
    const std::vector<std::string>& targets) {
  SourceCharPosConstraintPollDepsPlan plan;
  plan.changed_by.push_back(source);
  for (const std::string& target : targets) {
    plan.change.push_back(target);
    plan.changed_by.push_back(target);
  }
  return plan;
}

std::array<float, 3> source_char_pos_constraint_target_position(
    const std::array<float, 3>& source_pos,
    const std::array<float, 3>& target_pos,
    const std::array<float, 3>& box_min,
    const std::array<float, 3>& box_max) {
  std::array<float, 3> out = target_pos;
  for (int axis = 0; axis < 3; ++axis) {
    if (box_min[axis] <= box_max[axis]) {
      const float delta = std::clamp(target_pos[axis] - source_pos[axis],
                                     box_min[axis], box_max[axis]);
      out[axis] = source_pos[axis] + delta;
    }
  }
  return out;
}

void source_char_collide_copy_original_to_cur(CharCollide& collide) {
  collide.cur_radius[0] = collide.orig_radius[0];
  collide.cur_radius[1] = collide.orig_radius[1];
  collide.cur_length[0] = collide.orig_length[0];
  collide.cur_length[1] = collide.orig_length[1];
}

void source_char_collide_clear_mesh(CharCollide& collide) {
  collide.mesh.clear();
}

SourceCharCollideDefaultState source_char_collide_default_state() {
  return {};
}

SourceCharCollideSavePlan source_char_collide_save_plan() {
  return {};
}

SourceCharCollideLoadPlan source_char_collide_load_plan(int revision) {
  SourceCharCollideLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 7;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "Hmx::Object", "RndTransformable",
                     "mShape",    "mOrigRadius[0]"};
  if (revision > 4) plan.read_order.push_back("mOrigLength[0]");
  if (revision > 2) plan.read_order.push_back("mOrigLength[1]");
  if (revision > 1) {
    plan.read_order.push_back("mFlags");
  } else {
    plan.branches.push_back("mFlags=0");
  }
  if (revision > 3) {
    plan.read_order.push_back("mCurRadius[0]");
  } else {
    plan.branches.push_back("mCurRadius[0]=mOrigRadius[0]");
  }

  if (revision > 5) {
    plan.read_order.push_back("mOrigRadius[1]");
    plan.read_order.push_back("mCurRadius[1]");
    plan.read_order.push_back("mCurLength[0]");
    plan.read_order.push_back("mCurLength[1]");
    plan.read_order.push_back("unk148");
    plan.read_order.push_back("mMesh");
    plan.read_order.push_back("unk_structs[8]");
    plan.mesh_sphere_rows = 8;
    plan.read_order.push_back("mDigest");
    plan.read_order.push_back("mMeshYBias");
    if (revision < 7) plan.branches.push_back("CopyOriginalToCur");
  } else {
    plan.branches.push_back("mOrigRadius[1]=mOrigRadius[0]");
    plan.branches.push_back("CopyOriginalToCur");
  }
  return plan;
}

SourceCharCollideCopyPlan source_char_collide_copy_plan() {
  SourceCharCollideCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndTransformable"};
  plan.copied_members = {"mShape",     "mFlags",     "mOrigRadius",
                         "mOrigLength", "mCurRadius", "mCurLength",
                         "unk148",     "mMeshYBias", "mMesh"};
  plan.not_in_source_copy_members = {"mDigest", "unk_structs"};
  return plan;
}

SourceCharCollideHandlerPlan source_char_collide_handler_plan() {
  SourceCharCollideHandlerPlan plan;
  plan.superclasses = {"RndTransformable", "Hmx::Object"};
  plan.check = 0x221;
  return plan;
}

SourceCharCollidePropSyncPlan source_char_collide_prop_sync_plan() {
  SourceCharCollidePropSyncPlan plan;
  plan.modify_properties = {"shape:SyncShape",   "radius0:SyncShape",
                            "radius1:SyncShape", "length0:SyncShape",
                            "length1:SyncShape", "mesh:SyncShape",
                            "mesh_y_bias:SyncShape"};
  plan.properties = {"flags"};
  plan.superclasses = {"RndTransformable"};
  return plan;
}

SourceCharCollideHighlightPlan source_char_collide_highlight_plan(
    const CharCollide& collide,
    bool has_mesh) {
  SourceCharCollideHighlightPlan plan;
  switch (collide.shape) {
    case 0:
      plan.draw_calls.push_back("UtilDrawPlane");
      break;
    case 1:
    case 2:
      plan.draw_calls.push_back("UtilDrawSphere:orig_radius0");
      plan.draw_calls.push_back("UtilDrawSphere:cur_radius0");
      break;
    case 3:
    case 4:
      plan.draw_calls.push_back("UtilDrawCigar:orig_radius_length");
      plan.draw_calls.push_back("UtilDrawCigar:cur_radius_length");
      break;
    default:
      break;
  }
  if (has_mesh) {
    plan.mesh_sphere_draws = source_char_collide_num_spheres(collide) * 2;
  }
  return plan;
}

SourceCharCollideDeformPlan source_char_collide_deform_plan() {
  return {};
}

SourceCharCollideRadiusRuntimeEvidence
source_char_collide_radius_runtime_evidence() {
  SourceCharCollideRadiusRuntimeEvidence evidence;
  evidence.compute_radius_range = "0x803473DC -> 0x803474E8";
  return evidence;
}

SourceCharCollideAccessorsPlan source_char_collide_accessors_plan(
    const CharCollide& collide) {
  SourceCharCollideAccessorsPlan plan;
  plan.shape = collide.shape;
  return plan;
}

void source_char_collide_sync_shape(CharCollide& collide) {
  const float t = collide.cur_length[1];
  if (collide.cur_length[0] > t) {
    collide.cur_length[0] = collide.cur_length[1];
  }
  source_char_collide_copy_original_to_cur(collide);
}

int source_char_collide_num_spheres(const CharCollide& collide) {
  if (collide.shape == 3 || collide.shape == 4) return 2;
  if (collide.shape == 1 || collide.shape == 2) return 1;
  return 0;
}

float source_char_collide_get_radius(
    const CharCollide& collide,
    const SourceCharCollideRadiusCache& cache,
    const std::array<float, 3>& point,
    std::array<float, 3>& out_delta) {
  out_delta = {point[0] - cache.origin[0], point[1] - cache.origin[1],
               point[2] - cache.origin[2]};
  float radius = collide.cur_radius[0];
  const auto dot_axis = [&]() {
    return out_delta[0] * cache.axis[0] + out_delta[1] * cache.axis[1] +
           out_delta[2] * cache.axis[2];
  };
  if (collide.shape >= 3) {
    const float clamped =
        std::clamp(cache.length_scale * dot_axis(), collide.cur_length[0],
                   collide.cur_length[1]);
    for (int i = 0; i < 3; ++i) out_delta[i] -= cache.axis[i] * clamped;
    const float t =
        cache.radius_lerp_scale * (clamped - collide.cur_length[0]);
    radius = radius + (collide.cur_radius[1] - radius) * t;
  } else if (collide.shape == 0) {
    radius = dot_axis();
    for (int i = 0; i < 3; ++i) out_delta[i] = cache.axis[i] * radius;
  }
  return radius;
}

void source_char_hair_set_cloth(CharHair& hair, bool enabled) {
  const size_t strand_count = hair.strands.size();
  if (strand_count == 0) return;
  for (size_t si = 0; si < strand_count; ++si) {
    CharHairStrand& strand = hair.strands[si];
    const CharHairStrand& next = hair.strands[(si + 1) % strand_count];
    for (size_t pi = 0; pi < strand.points.size(); ++pi) {
      CharHairPoint& point = strand.points[pi];
      if (!enabled || pi >= next.points.size()) {
        point.side_length = -1.0f;
        continue;
      }
      const CharHairPoint& next_point = next.points[pi];
      const float dx = point.pos[0] - next_point.pos[0];
      const float dy = point.pos[1] - next_point.pos[1];
      const float dz = point.pos[2] - next_point.pos[2];
      point.side_length = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
}

SourceCharHairDefaultState source_char_hair_default_state() {
  return SourceCharHairDefaultState{};
}

SourceCharHairPointDefaultState source_char_hair_point_default_state() {
  return SourceCharHairPointDefaultState{};
}

SourceCharHairStrandDefaultState source_char_hair_strand_default_state() {
  return SourceCharHairStrandDefaultState{};
}

SourceCharHairSavePlan source_char_hair_save_plan() {
  return {};
}

SourceCharHairDestructorPlan source_char_hair_destructor_plan() {
  return {};
}

SourceCharHairPointLoadPlan source_char_hair_point_load_plan(int revision) {
  SourceCharHairPointLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 11;
  if (!plan.known_revision) return plan;

  plan.read_order = {"pos", "bone", "length"};
  if (revision < 3) {
    plan.read_order.push_back("legacyCollideType");
    plan.read_order.push_back("legacyCollisionName");
  } else if (revision == 3) {
    plan.read_order.push_back("legacyCollideType");
  }

  plan.read_order.push_back("radius");
  if (revision > 1) {
    plan.read_order.push_back("outerRadius");
  } else {
    plan.branches.push_back("outerRadius=0");
  }
  if (revision == 6 || revision == 7 || revision == 8) {
    plan.read_order.push_back("addToRadius");
    plan.branches.push_back("addToRadiusAppliesToRadiusAndOuterRadius");
  }
  if (revision == 6) plan.read_order.push_back("legacyCollisionName");

  if (revision < 8) {
    plan.branches.push_back("sideLength=-1");
    if (revision > 5) {
      plan.read_order.push_back("legacySideLengthInt0");
      plan.read_order.push_back("legacySideLengthInt1");
    }
  } else {
    if (revision < 9) plan.read_order.push_back("sideLengthEnabled");
    plan.read_order.push_back("sideLength");
    if (revision < 9) {
      plan.branches.push_back("disabledSideLengthForcesMinusOne");
    }
  }

  if (revision > 9) plan.read_order.push_back("unk5c");
  plan.branches.push_back("clearCollides");
  plan.branches.push_back("zeroForce");
  plan.branches.push_back("zeroLastFriction");
  plan.branches.push_back("zeroLastZ");
  return plan;
}

SourceCharHairStrandLoadPlan source_char_hair_strand_load_plan(int revision) {
  SourceCharHairStrandLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 11;
  if (!plan.known_revision) return plan;

  plan.read_order = {"mRoot", "mAngle", "mPoints", "mBaseMat", "mRootMat"};
  if (revision > 2) {
    plan.read_order.push_back("mHookupFlags");
  } else {
    plan.branches.push_back("mHookupFlags=0");
  }
  return plan;
}

SourceCharHairLoadPlan source_char_hair_load_plan(int revision) {
  SourceCharHairLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 11;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "Hmx::Object", "mStiffness", "mTorsion",
                     "mInertia",  "mGravity",    "mWeight",    "mFriction"};
  if (revision < 8) {
    plan.branches.push_back("mMinSlack=0");
    plan.branches.push_back("mMaxSlack=0");
  } else {
    plan.read_order.push_back("mMinSlack");
    plan.read_order.push_back("mMaxSlack");
  }
  plan.read_order.push_back("mStrands");
  plan.read_order.push_back("mSimulate");
  if (revision > 10) plan.read_order.push_back("mWind");
  return plan;
}

SourceGrimCharHairLoadPlan source_grim_char_hair_load_plan(int version) {
  SourceGrimCharHairLoadPlan plan;
  if (version != 2) return plan;

  plan.known_version = true;
  plan.reads_object_meta = true;
  plan.reads_min_slack = false;
  plan.reads_wind = false;
  plan.read_order = {"version",     "Object::Load", "stiffness",
                     "torsion",     "inertia",      "gravity",
                     "weight",      "friction",     "strand_count",
                     "strand.root", "strand.angle", "point_count",
                     "point",       "strand.base_mat",
                     "strand.root_mat", "simulate"};
  plan.branches = {"version 2 only", "min_slack/max_slack omitted",
                   "wind omitted"};
  plan.point.known_version = true;
  plan.point.grim_read_order = {"unknown_floats", "bone",       "length",
                                "collide_type",   "collision",  "distance",
                                "align_dist"};
  plan.point.rb3_rev2_equivalents = {
      "unknown_floats->pos", "bone->bone", "length->length",
      "collide_type->legacyCollideType",
      "collision->legacyCollisionName", "distance->radius",
      "align_dist->outerRadius"};
  return plan;
}

uint32_t source_grim_char_hair_collide_type(uint32_t raw) {
  if (raw <= 4) return raw;
  return 3;
}

SourceCharHairSetNamePlan source_char_hair_set_name_plan(
    bool owner_is_character,
    bool owner_is_world_dir) {
  SourceCharHairSetNamePlan plan;
  plan.assigns_character_owner = owner_is_character;
  plan.use_post_proc = owner_is_character || owner_is_world_dir;
  return plan;
}

SourceCharHairHandlerPlan source_char_hair_handler_plan() {
  SourceCharHairHandlerPlan plan;
  plan.actions = {"reset:mReset=_msg->Int(2)", "hookup:Hookup()",
                  "set_cloth:SetCloth(_msg->Int(2))",
                  "freeze_pose:FreezePose()"};
  plan.superclasses = {"RndPollable", "Hmx::Object"};
  plan.check = 0x46f;
  return plan;
}

SourceCharHairPropSyncPlan source_char_hair_prop_sync_plan() {
  SourceCharHairPropSyncPlan plan;
  plan.point_properties = {"bone", "length", "collides",
                           "radius", "outer_radius", "side_length"};
  plan.strand_set_properties = {"root:SetRoot", "angle:SetAngle"};
  plan.strand_properties = {"points",       "hookup_flags", "show_spheres",
                            "show_collide", "show_pose"};
  plan.hair_properties = {"stiffness", "torsion",   "inertia", "gravity",
                          "weight",    "friction",  "min_slack",
                          "max_slack", "strands",   "simulate",
                          "wind"};
  return plan;
}

SourceCharHairDoResetPlan source_char_hair_do_reset_plan(int reset) {
  SourceCharHairDoResetPlan plan;
  plan.point_steps = {"Multiply(unk5c,parentWorld,pos)",
                      "Subtract(pos,previousPos,delta)",
                      "Cross(rootX,delta,lastZ)", "Normalize(lastZ)",
                      "Cross(delta,lastZ,rootX)", "zeroForce",
                      "zeroLastFriction"};
  plan.simulate_loop_count = reset;
  plan.next_reset = 0;
  return plan;
}

bool source_char_hair_set_name_use_post_proc(bool owner_is_character,
                                             bool owner_is_world_dir) {
  return source_char_hair_set_name_plan(owner_is_character, owner_is_world_dir)
      .use_post_proc;
}

void source_char_hair_set_managed_hookup(SourceCharHairDefaultState& state,
                                         bool managed_hookup) {
  state.managed_hookup = managed_hookup;
}

SourceCharHairGetFpsResult source_char_hair_get_fps_result(
    bool use_post_proc,
    float emulated_fps) {
  SourceCharHairGetFpsResult result;
  result.emulated_fps = emulated_fps;
  if (use_post_proc && emulated_fps > 0.0f) {
    result.used_post_proc = true;
    result.fps = emulated_fps;
    if (result.fps != 60.0f) {
      result.adjusted_non_sixty = true;
      result.fps = 60.0f - result.fps;
    }
    return result;
  }
  result.fps = 60.0f;
  return result;
}

float source_char_hair_get_fps(bool use_post_proc, float emulated_fps) {
  return source_char_hair_get_fps_result(use_post_proc, emulated_fps).fps;
}

SourceCharHairHookupDumpEvidence source_char_hair_hookup_dump_evidence() {
  SourceCharHairHookupDumpEvidence evidence;
  evidence.range = "0x80360284 -> 0x80360BE0";
  evidence.locals = {
      "vector collides r1+0x60",
      "ObjDirItr c r1+0x6C",
      "int i r28",
      "int j r27",
      "int k r27",
      "CharCollide* c r26",
      "Vector3 delta r1+0x50",
      "float rootDist f31",
      "Vector3 d r1+0x40",
      "float length f30",
      "int j r25",
      "float maxRadius f1",
  };
  evidence.references = {
      "Debug TheDebug",
      "const char * kAssertStr",
      "__RTTI__Q23Hmx6Object",
      "__RTTI__11CharCollide",
  };
  return evidence;
}

SourceCharHairSimulateZeroTimeDumpEvidence
source_char_hair_simulate_zero_time_dump_evidence() {
  SourceCharHairSimulateZeroTimeDumpEvidence evidence;
  evidence.range = "0x8035FC8C -> 0x80360144";
  evidence.locals = {
      "int i r31",
      "Transform t r1+0x70",
      "ObjVector& ps r0",
      "int j r27",
      "Matrix3 m r1+0x40",
  };
  return evidence;
}

SourceCharHairRb2MappedBodyEvidence
source_char_hair_rb2_mapped_body_evidence() {
  SourceCharHairRb2MappedBodyEvidence evidence;
  evidence.set_root_range = "0x8035D848 -> 0x8035DA2C";
  evidence.set_root_locals = {
      "int i r31",
      "RndTransformable* r r30",
      "RndTransformable* r r30",
      "Point* last r30",
      "int i r31",
      "RndTransformable* bone r29",
      "Point& p r29",
  };
  evidence.set_cloth_range = "0x8035DA2C -> 0x8035DB64";
  evidence.set_cloth_locals = {
      "int i r5",
      "Strand& s r11",
      "Strand& lastStrand r0",
      "int j r12",
  };
  evidence.set_angle_range = "0x8035DB64 -> 0x8035DDD8";
  evidence.set_angle_locals = {"Matrix3 m r1+0x40"};
  evidence.do_reset_range = "0x8035E3B0 -> 0x8035E618";
  evidence.do_reset_locals = {
      "unsigned char oldSimulate r31",
      "int i r30",
      "ObjVector& ps r0",
      "Transform t r1+0x10",
      "int j r29",
      "float oldInertia f31",
      "float oldFriction f30",
      "int i r25",
  };
  evidence.poll_range = "0x8035F448 -> 0x8035F510";
  evidence.poll_locals = {"float kFPS f1"};
  evidence.poll_references = {"TaskMgr TheTaskMgr"};
  evidence.simulate_range = "0x8035F510 -> 0x8035FC8C";
  evidence.simulate_locals = {
      "float kTimeDelta f2",
      "float kTimeCorrection f3",
      "float kGrav f31",
      "float kWeight f30",
      "int i r30",
      "Strand& lastStrand r29",
      "Strand& s r28",
      "Transform t r1+0xB0",
      "ObjVector& ps r28",
      "int j r31",
      "Point& p r27",
      "Vector3 lastPos r1+0x70",
      "Vector3 delta r1+0x60",
      "Point& lastP r26",
      "float l2 f0",
      "float min2 f3",
      "float v2 f1",
      "float max2 f2",
      "float v2 f1",
      "Matrix3 m r1+0x80",
      "float l f0",
      "float rat f28",
      "Vector3 ideal r1+0x50",
      "iterator it r1+0x18",
      "CharCollide* coll r26",
      "Vector3 delta r1+0x40",
      "float dot f0",
      "float radius f29",
      "float outerRadius f27",
      "float l2 f0",
      "float l f0",
      "float frac f1",
      "float l f0",
      "float dot f0",
      "float radius f27",
      "float innerRadius f28",
      "float l2 f0",
      "float l f0",
      "float frac f1",
      "Vector3 friction r1+0x30",
      "Vector3 delta r1+0x20",
  };
  evidence.simulate_references = {"DebugNotifier TheDebugNotifier"};
  evidence.poll_deps_range = "0x80360144 -> 0x80360284";
  evidence.poll_deps_locals = {"int i r31"};
  evidence.poll_deps_references = {
      "const char * gStlAllocName",
      "__RTTI__PQ211stlpmtx_std26_List_node<PQ23Hmx6Object>",
      "unsigned char gStlAllocNameLookup",
  };
  evidence.copy_range = "0x803616E8 -> 0x8036181C";
  evidence.copy_locals = {"const CharHair* h r0"};
  evidence.copy_references = {
      "__RTTI__Q23Hmx6Object",
      "__RTTI__8CharHair",
  };
  return evidence;
}

SourceCharHairHookupPlan source_char_hair_hookup_plan(
    bool managed_hookup,
    const std::vector<std::string>& dir_collides) {
  SourceCharHairHookupPlan plan;
  if (managed_hookup) {
    plan.returned_for_managed_hookup = true;
    return plan;
  }
  plan.collected_collides = dir_collides;
  plan.collected_from_object_dir = true;
  plan.has_overload_declaration = true;
  plan.overload_body_statement_visible = false;
  plan.called_overloaded_hookup = true;
  return plan;
}

SourceBandCharacterHairHookupPlan source_band_character_hair_hookup_plan(
    const std::vector<std::string>& hair_rows,
    const std::vector<std::string>& collide_rows,
    bool in_closet) {
  SourceBandCharacterHairHookupPlan plan;
  plan.in_closet = in_closet;
  plan.hair_rows = hair_rows;
  plan.collide_rows = collide_rows;
  plan.clears_collide_meshes_after_sync_when_not_in_closet = !in_closet;
  return plan;
}

SourceCharHairPointCollideResolution
source_char_hair_point_collide_resolution(const CharHairPoint& point) {
  SourceCharHairPointCollideResolution resolution;
  resolution.has_collision_name = !point.collision.empty();
  resolution.has_collide_type = point.collide_type != 0;
  resolution.has_positive_radius =
      point.radius > 0.0f || point.outer_radius > 0.0f;
  resolution.has_legacy_inline_rows =
      resolution.has_collision_name || resolution.has_collide_type ||
      resolution.has_positive_radius;
  return resolution;
}

SourceCharHairWritebackGate source_char_hair_writeback_gate(
    bool has_bone,
    int resolved_point_collide_count) {
  SourceCharHairWritebackGate gate;
  gate.has_bone = has_bone;
  gate.resolved_point_collide_count =
      std::max(0, resolved_point_collide_count);
  gate.enters_collision_branch = gate.resolved_point_collide_count != 0;
  gate.rebuilds_basis = gate.enters_collision_branch;
  gate.updates_force_state = gate.enters_collision_branch;
  gate.may_set_world_xfm = gate.enters_collision_branch && gate.has_bone;
  return gate;
}

SourceCharHairEnterPlan source_char_hair_enter_plan(
    bool managed_hookup,
    const std::vector<std::string>& dir_collides) {
  SourceCharHairEnterPlan plan;
  plan.next_reset = 1;
  plan.called_rnd_pollable_enter = true;
  plan.hookup = source_char_hair_hookup_plan(managed_hookup, dir_collides);
  return plan;
}

SourceCharHairSimulateLoopsPlan source_char_hair_simulate_loops_plan(
    bool simulate,
    int strand_count,
    int collide_count,
    int loop_count,
    float fps) {
  SourceCharHairSimulateLoopsPlan plan;
  plan.fps = fps;
  if (!simulate || strand_count == 0) return plan;
  plan.entered = true;
  plan.collide_maintenance_count = collide_count > 0 ? collide_count : 0;
  plan.simulate_internal_calls = loop_count > 0 ? loop_count : 0;
  return plan;
}

SourceCharHairSimulateInternalScalars
source_char_hair_simulate_internal_scalars(
    float fps,
    float stiffness,
    float gravity,
    bool has_wind,
    bool has_wind_root,
    std::array<float, 3> wind) {
  SourceCharHairSimulateInternalScalars scalars;
  scalars.sixty_over_fps = 60.0f / fps;
  scalars.f19 = (1.0f / fps) * scalars.sixty_over_fps;
  scalars.stiffness_pow =
      std::pow(1.0f - stiffness,
               scalars.sixty_over_fps * scalars.sixty_over_fps);
  if (has_wind && has_wind_root) {
    for (int i = 0; i < 3; ++i) {
      scalars.external_force[i] = wind[i] * scalars.f19 * 0.5f;
    }
  }
  scalars.external_force[2] += gravity * scalars.f19 * -3.858268f;
  return scalars;
}

SourceCharHairClothPairStep source_char_hair_simulate_internal_cloth_pair(
    std::array<float, 3> point_pos,
    std::array<float, 3> next_point_pos,
    float side_length,
    float min_slack,
    float max_slack) {
  SourceCharHairClothPairStep step;
  step.point_pos = point_pos;
  step.next_point_pos = next_point_pos;
  if (side_length < 0.0f) return step;

  step.entered = true;
  std::array<float, 3> v_res = {
      point_pos[0] - next_point_pos[0],
      point_pos[1] - next_point_pos[1],
      point_pos[2] - next_point_pos[2]};
  step.lensq = v_res[0] * v_res[0] + v_res[1] * v_res[1] +
               v_res[2] * v_res[2];
  step.min_slack_length = side_length - min_slack;
  const float side_len_sq = step.min_slack_length * step.min_slack_length;
  if (step.lensq < side_len_sq) {
    const float scale = side_len_sq / (side_len_sq + step.lensq) - 0.5f;
    for (int i = 0; i < 3; ++i) {
      const float delta = v_res[i] * scale;
      step.point_pos[i] += delta;
      step.next_point_pos[i] -= delta;
    }
    step.min_slack_applied = true;
    return step;
  }

  step.max_slack_length = side_length + max_slack;
  const float max_slack_len_sq =
      step.max_slack_length * step.max_slack_length;
  if (step.max_slack_length > max_slack_len_sq) {
    const float scale =
        max_slack_len_sq / (max_slack_len_sq + step.lensq) - 0.5f;
    for (int i = 0; i < 3; ++i) {
      const float delta = v_res[i] * scale;
      step.point_pos[i] += delta;
      step.next_point_pos[i] -= delta;
    }
    step.max_slack_applied = true;
  }
  return step;
}

SourceCharHairLengthStep source_char_hair_simulate_internal_length_step(
    std::array<float, 3> point_pos,
    std::array<float, 3> point_force,
    std::array<float, 3> external_force,
    std::array<float, 3> root_pos,
    std::array<float, 3> root_y_axis,
    float point_length,
    float sixty_over_fps,
    bool has_previous_point) {
  SourceCharHairLengthStep step;
  step.original_pos = point_pos;
  for (int i = 0; i < 3; ++i) {
    point_pos[i] += point_force[i] + external_force[i];
    step.root_to_point[i] = point_pos[i] - root_pos[i];
  }

  const float len_sq = step.root_to_point[0] * step.root_to_point[0] +
                       step.root_to_point[1] * step.root_to_point[1] +
                       step.root_to_point[2] * step.root_to_point[2];
  step.reciprocal_length = 1.0f / std::sqrt(len_sq);
  step.length_scale = point_length * step.reciprocal_length - 1.0f;
  if (has_previous_point) {
    const float prev_scale = -sixty_over_fps * 0.5f * step.length_scale;
    for (int i = 0; i < 3; ++i) {
      step.previous_force_delta[i] = step.root_to_point[i] * prev_scale;
    }
  }
  for (int i = 0; i < 3; ++i) {
    step.point_pos[i] = point_pos[i] +
                        step.root_to_point[i] * step.length_scale;
    step.target_pos[i] = root_pos[i] + root_y_axis[i] * point_length;
  }
  return step;
}

SourceCharHairForceStep source_char_hair_simulate_internal_force_step(
    std::array<float, 3> target_pos,
    std::array<float, 3> point_pos,
    std::array<float, 3> original_pos,
    std::array<float, 3> last_friction,
    float stiffness_pow,
    float friction,
    float inertia) {
  SourceCharHairForceStep step;
  for (int i = 0; i < 3; ++i) {
    step.force[i] = target_pos[i] - point_pos[i];
    step.friction_delta[i] = last_friction[i] - step.force[i];
    step.last_friction[i] = step.force[i];
    step.force[i] *= 1.0f - stiffness_pow;
    step.force[i] += step.friction_delta[i] * -friction;
    step.motion_delta[i] = point_pos[i] - original_pos[i];
    step.force[i] += step.motion_delta[i] * inertia;
  }
  return step;
}

SourceCharHairCollisionStep source_char_hair_simulate_internal_collision_step(
    std::array<float, 3> point_pos,
    std::array<float, 3> root_to_point,
    float reciprocal_length,
    std::array<float, 3> last_z,
    std::array<float, 3> root_z_axis,
    float torsion,
    float point_radius,
    float point_outer_radius,
    const std::vector<SourceCharHairCollisionInput>& collides) {
  SourceCharHairCollisionStep step;
  step.point_pos = point_pos;
  step.collide_count = static_cast<int>(collides.size());

  auto dot = [](const std::array<float, 3>& a,
                const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  auto cross = [](const std::array<float, 3>& a,
                  const std::array<float, 3>& b) {
    return std::array<float, 3>{
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
  };
  auto scale_add = [](std::array<float, 3>& dst,
                      const std::array<float, 3>& src, float scale) {
    for (int i = 0; i < 3; ++i) dst[i] += src[i] * scale;
  };
  auto interp = [](const std::array<float, 3>& a,
                   const std::array<float, 3>& b, float t) {
    return std::array<float, 3>{
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    };
  };
  auto normalize = [&](std::array<float, 3> v) {
    const float len_sq = dot(v, v);
    const float inv_len = 1.0f / std::sqrt(len_sq);
    for (int i = 0; i < 3; ++i) v[i] *= inv_len;
    return v;
  };

  std::array<float, 3> z_axis = interp(last_z, root_z_axis, torsion);
  step.pre_collision_z = z_axis;
  if (collides.empty()) return step;

  step.entered = true;
  const float diff_radius = point_outer_radius - point_radius;
  const float max_radius = std::max(point_radius, point_outer_radius);
  for (const SourceCharHairCollisionInput& collide : collides) {
    std::array<float, 3> delta = collide.delta;
    switch (collide.shape) {
      case 0:
        if (max_radius > collide.radius) {
          scale_add(point_pos, collide.axis, max_radius - collide.radius);
          step.adjusted_point = true;
        }
        break;
      case 1:
      case 3: {
        const float delta_sq = dot(delta, delta);
        const float sum_radius = collide.radius + max_radius;
        if (delta_sq < sum_radius * sum_radius) {
          if (diff_radius > 0.0f) {
            const float recip = 1.0f / std::sqrt(delta_sq);
            const float cluster = delta_sq * recip;
            const float other_sum_radius = collide.radius + point_radius;
            for (int i = 0; i < 3; ++i) delta[i] *= -recip;
            if (cluster < other_sum_radius) {
              z_axis = delta;
              step.z_overridden = true;
              scale_add(point_pos, delta, cluster - other_sum_radius);
            } else {
              z_axis = interp(z_axis, delta,
                              (sum_radius - cluster) / diff_radius);
              step.z_interpolated = true;
            }
          } else {
            scale_add(point_pos, delta,
                      sum_radius * (1.0f / std::sqrt(delta_sq)) - 1.0f);
          }
          step.adjusted_point = true;
        }
        break;
      }
      case 2:
      case 4: {
        const float delta_sq = dot(delta, delta);
        const float minus_radius = collide.radius - max_radius;
        if (delta_sq > minus_radius * minus_radius) {
          if (diff_radius > 0.0f) {
            const float recip = 1.0f / std::sqrt(delta_sq);
            const float cluster = delta_sq * recip;
            const float other_sum_radius = collide.radius - point_radius;
            for (int i = 0; i < 3; ++i) delta[i] *= -recip;
            if (cluster > other_sum_radius) {
              z_axis = delta;
              step.z_overridden = true;
              scale_add(point_pos, delta, cluster - other_sum_radius);
            } else {
              z_axis = interp(z_axis, delta,
                              (cluster - minus_radius) / diff_radius);
              step.z_interpolated = true;
            }
          } else {
            scale_add(point_pos, delta,
                      minus_radius * (1.0f / std::sqrt(delta_sq)) - 1.0f);
          }
          step.adjusted_point = true;
        }
        break;
      }
      default:
        break;
    }
  }

  for (int i = 0; i < 3; ++i) {
    step.basis_y[i] = root_to_point[i] * reciprocal_length;
  }
  step.basis_x = cross(step.basis_y, z_axis);
  step.basis_x = normalize(step.basis_x);
  step.basis_z = cross(step.basis_x, step.basis_y);
  step.last_z = step.basis_z;
  step.point_pos = point_pos;
  step.set_world_xfm = true;
  return step;
}

SourceCharHairFreezePosePlan source_char_hair_freeze_pose_plan(
    bool simulate,
    int strand_count,
    int collide_count) {
  SourceCharHairFreezePosePlan plan;
  plan.called_hookup = true;
  plan.simulate_loops =
      source_char_hair_simulate_loops_plan(simulate, strand_count,
                                           collide_count, 200, 60.0f);
  plan.restored_simulate = true;
  plan.restored_simulate_value = simulate;
  plan.called_freeze_pose_raw = true;
  return plan;
}

SourceCharHairPollDecision source_char_hair_poll_decision(
    bool owner_is_character,
    bool character_syncing,
    bool character_teleported,
    int character_min_lod,
    int current_reset,
    float delta_seconds) {
  SourceCharHairPollDecision decision;
  int reset = current_reset;
  if (owner_is_character) {
    decision.hookup = character_syncing;
    if (character_teleported) {
      reset = 1;
      decision.teleported_reset = true;
    }
    if (character_min_lod > 0) {
      decision.do_reset = true;
      decision.reset_count = 0;
      decision.return_after_reset = true;
      decision.next_reset = 0;
      return decision;
    }
  }
  if (reset > 0) {
    decision.do_reset = true;
    decision.reset_count = reset;
    reset = 0;
  }
  if (delta_seconds != 0.0f) {
    decision.simulate_loops = true;
  } else {
    decision.simulate_zero_time = true;
  }
  decision.next_reset = reset;
  return decision;
}

std::array<float, 9> source_char_hair_set_angle_root_mat(
    float angle_degrees, const float base_mat[9]) {
  constexpr float kPi = 3.14159265358979323846f;
  const float angle = angle_degrees * (kPi / 180.0f);
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  std::array<float, 9> out{};
  out[0] = base_mat[0];
  out[1] = base_mat[1];
  out[2] = base_mat[2];
  for (int col = 0; col < 3; ++col) {
    out[3 + col] = c * base_mat[3 + col] + s * base_mat[6 + col];
    out[6 + col] = -s * base_mat[3 + col] + c * base_mat[6 + col];
  }
  return out;
}

SourceCharFaceServoTryScaleDownResult source_char_face_servo_try_scale_down(
    SourceCharFaceServoBlinkState& state,
    bool has_base_clip,
    bool clip_type_valid) {
  SourceCharFaceServoTryScaleDownResult result;
  if (!state.need_scale_down) return result;
  state.need_scale_down = false;
  result.consumed_need_scale_down = true;
  result.invoked_base_scale_down = has_base_clip && clip_type_valid;
  state.left = 0.0f;
  state.right = 0.0f;
  result.reset_blink_weights = true;
  return result;
}

float source_char_face_servo_blink_weight_left(
    const SourceCharFaceServoBlinkState& state) {
  return state.left;
}

SourceCharFaceServoScaleAddResult source_char_face_servo_scale_add_blink(
    SourceCharFaceServoBlinkState& state,
    const SourceCharFaceServoBlinkClips& clips,
    const std::string& clip_name,
    bool clip_is_relative,
    float weight,
    float f2,
    float f3) {
  SourceCharFaceServoScaleAddResult result;
  if (!clip_is_relative || weight < 0.0f) return result;

  result.accepted = true;
  result.downstream_call = "clip->ScaleAdd";
  result.forwarded_weight = weight;
  result.forwarded_f2 = f2;
  result.forwarded_f3 = f3;
  result.scale_down =
      source_char_face_servo_try_scale_down(state, false, false)
          .consumed_need_scale_down;

  const bool left_match =
      clip_name == clips.left || (!clips.left2.empty() && clip_name == clips.left2);
  const bool right_match =
      clip_name == clips.right || (!clips.right2.empty() && clip_name == clips.right2);
  if (left_match) {
    state.left = std::clamp(state.left + weight, 0.0f, 1.0f);
    result.matched_left = true;
  } else if (right_match) {
    state.right = std::clamp(state.right + weight, 0.0f, 1.0f);
    result.matched_right = true;
  }
  return result;
}

SourceCharFaceServoLoadPlan source_char_face_servo_load_plan(int revision) {
  SourceCharFaceServoLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 4;
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "clipObjectDir"};
  if (revision > 3) {
    plan.read_order.push_back("clipTypeSymbol");
  } else {
    plan.branches.push_back("deriveClipTypeFromDirType");
    plan.branches.push_back("fallbackClipTypeFromFirstClip");
  }
  if (revision != 0) plan.read_order.push_back("mBlinkClipLeftName");
  if (revision > 1) plan.read_order.push_back("mBlinkClipRightName");
  if (revision > 2) {
    plan.read_order.push_back("mBlinkClipLeftName2");
    plan.read_order.push_back("mBlinkClipRightName2");
  }
  plan.read_order.push_back("SetClips");
  plan.read_order.push_back("SetClipType");
  return plan;
}

SourceCharFaceServoCopyPlan source_char_face_servo_copy_plan() {
  SourceCharFaceServoCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mBlinkWeightLeft",    "mBlinkWeightRight",
                         "mBlinkClipLeftName", "mBlinkClipRightName",
                         "mBlinkClipLeftName2", "mBlinkClipRightName2"};
  plan.post_copy_calls = {"SetClips(c->mClips)", "SetClipType(c->mClipType)"};
  return plan;
}

SourceCharFaceServoHandlerPlan source_char_face_servo_handler_plan() {
  SourceCharFaceServoHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x119;
  return plan;
}

SourceCharFaceServoPropSyncPlan source_char_face_servo_prop_sync_plan() {
  SourceCharFaceServoPropSyncPlan plan;
  plan.set_properties = {"clips", "clip_type"};
  plan.set_actions = {"SetClips", "SetClipType"};
  plan.properties = {"blink_clip_left", "blink_clip_left2",
                     "blink_clip_right", "blink_clip_right2"};
  plan.superclasses = {"CharBonesMeshes"};
  return plan;
}

SourceCharFaceServoSavePlan source_char_face_servo_save_plan() {
  return SourceCharFaceServoSavePlan{};
}

SourceCharFaceServoEnterPlan source_char_face_servo_enter_plan() {
  SourceCharFaceServoEnterPlan plan;
  plan.calls = {"RndPollable::Enter"};
  return plan;
}

SourceCharFaceServoSetClipsPlan source_char_face_servo_set_clips_plan() {
  SourceCharFaceServoSetClipsPlan plan;
  plan.clip_lookups = {"Base", "mBlinkClipLeftName", "mBlinkClipLeftName2",
                       "mBlinkClipRightName", "mBlinkClipRightName2"};
  return plan;
}

SourceCharFaceServoSetClipTypePlan
source_char_face_servo_set_clip_type_plan(bool changed) {
  SourceCharFaceServoSetClipTypePlan plan;
  if (changed) {
    plan.changed_calls = {"set mClipType", "ClearBones",
                          "CharBoneDir::StuffBones", "mNeedScaleDown=true"};
  }
  return plan;
}

SourceCharFaceServoPollPlan source_char_face_servo_poll_plan(
    bool has_base_clip) {
  SourceCharFaceServoPollPlan plan;
  if (has_base_clip) {
    plan.base_clip_calls = {"TryScaleDown", "ScaleAddIdentity",
                            "mBaseClip->RotateBy", "PoseMeshes"};
  }
  return plan;
}

SourceCharFaceServoProceduralWeightsPlan
source_char_face_servo_procedural_weights_plan(bool positive_weight,
                                               bool already_applied) {
  SourceCharFaceServoProceduralWeightsPlan plan;
  if (positive_weight && !already_applied) {
    plan.calls = {"TryScaleDown", "left blink ScaleAdd",
                  "right blink ScaleAdd", "mAppliedProceduralBlink=true"};
  }
  return plan;
}

SourceCharFaceServoProceduralWeightsResult
source_char_face_servo_apply_procedural_weights(
    SourceCharFaceServoBlinkState& state,
    float procedural_weight,
    bool already_applied,
    bool has_left_clip,
    bool has_right_clip,
    bool right_same_as_left) {
  SourceCharFaceServoProceduralWeightsResult result;
  if (procedural_weight <= 0.0f || already_applied) return result;

  result.accepted = true;
  result.scale_down =
      source_char_face_servo_try_scale_down(state, false, false)
          .consumed_need_scale_down;

  if (has_left_clip) {
    result.left_applied = true;
    result.left_weight = (1.0f - state.left) * procedural_weight;
  }
  if (has_right_clip && !right_same_as_left) {
    result.right_applied = true;
    result.right_weight = (1.0f - state.right) * procedural_weight;
  }
  result.applied_procedural_blink = true;
  return result;
}

SourceCharFaceServoPollDepsPlan source_char_face_servo_poll_deps_plan() {
  return SourceCharFaceServoPollDepsPlan{};
}

SourceCharMeshHideRowLoadPlan source_char_mesh_hide_row_load_plan(
    int32_t revision) {
  SourceCharMeshHideRowLoadPlan plan;
  if (revision < 0 || revision > 2) return plan;
  plan.known_revision = true;
  plan.read_order = {"mDraw", "mFlags"};
  if (revision > 1) plan.read_order.push_back("mShow");
  return plan;
}

SourceCharMeshHideLoadPlan source_char_mesh_hide_load_plan(int32_t revision) {
  SourceCharMeshHideLoadPlan plan;
  if (revision < 0 || revision > plan.max_revision) return plan;
  plan.known_revision = true;
  plan.read_order = {"LOAD_REVS", "Hmx::Object", "mFlags", "mHides"};
  return plan;
}

SourceCharMeshHideSavePlan source_char_mesh_hide_save_plan() {
  return SourceCharMeshHideSavePlan{};
}

SourceCharMeshHideCopyPlan source_char_mesh_hide_copy_plan() {
  SourceCharMeshHideCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mFlags", "mHides"};
  return plan;
}

SourceCharMeshHideHandlerPlan source_char_mesh_hide_handler_plan() {
  SourceCharMeshHideHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  return plan;
}

SourceCharMeshHidePropSyncPlan source_char_mesh_hide_prop_sync_plan() {
  SourceCharMeshHidePropSyncPlan plan;
  plan.hide_properties = {"drawable", "flags", "show"};
  plan.properties = {"flags", "hides"};
  return plan;
}

int32_t source_char_mesh_hide_combined_flags(
    const std::vector<SourceCharMeshHideObject>& objects,
    int32_t initial_flags) {
  int32_t flags = initial_flags;
  for (const SourceCharMeshHideObject& object : objects) {
    flags |= object.flags;
  }
  return flags;
}

void source_char_mesh_hide_draws(SourceCharMeshHideObject& object,
                                 int32_t flags) {
  for (SourceCharMeshHideRow& hide : object.hides) {
    if (hide.has_draw) {
      const bool draw_allowed = (flags & hide.flags) == 0;
      hide.show = draw_allowed & hide.draw_showing;
    }
  }
}

int32_t source_char_mesh_hide_all(
    std::vector<SourceCharMeshHideObject>& objects,
    int32_t initial_flags) {
  const int32_t flags =
      source_char_mesh_hide_combined_flags(objects, initial_flags);
  for (SourceCharMeshHideObject& object : objects) {
    source_char_mesh_hide_draws(object, flags);
  }
  return flags;
}

bool source_char_trans_copy_poll(const milo_scene::Xfm* src,
                                 milo_scene::Xfm* dest) {
  if (src == nullptr || dest == nullptr) return false;
  *dest = *src;
  return true;
}

SourceCharTransCopyLoadPlan source_char_trans_copy_load_plan(int revision) {
  SourceCharTransCopyLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 1;
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "mSrc", "mDest"};
  return plan;
}

SourceCharTransCopySavePlan source_char_trans_copy_save_plan() {
  return SourceCharTransCopySavePlan{};
}

SourceCharTransCopyCopyPlan source_char_trans_copy_copy_plan() {
  SourceCharTransCopyCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mSrc", "mDest"};
  return plan;
}

SourceCharTransCopyHandlerPlan source_char_trans_copy_handler_plan() {
  SourceCharTransCopyHandlerPlan plan;
  plan.superclasses = {"RndPollable", "Hmx::Object"};
  plan.check = 0x4C;
  return plan;
}

SourceCharTransCopyPropSyncPlan source_char_trans_copy_prop_sync_plan() {
  SourceCharTransCopyPropSyncPlan plan;
  plan.properties = {"src", "dest"};
  return plan;
}

void source_char_trans_copy_poll_deps(
    SourceCharTransCopyPollDeps& deps,
    const std::string& src,
    const std::string& dest) {
  deps.change.push_back(dest);
  deps.changed_by.push_back(src);
}

std::vector<std::string> source_char_poll_group_poll_order(
    float weight,
    const std::vector<std::string>& polls) {
  if (weight == 0.0f) return {};
  return polls;
}

std::vector<std::string> source_char_poll_group_enter_order(
    const std::vector<std::string>& polls) {
  return polls;
}

std::vector<std::string> source_char_poll_group_exit_order(
    const std::vector<std::string>& polls) {
  return polls;
}

std::vector<std::string> source_char_poll_group_list_children(
    const std::vector<std::string>& polls) {
  return polls;
}

void source_char_poll_group_poll_deps(
    SourceCharPollGroupPollDeps& deps,
    const std::vector<SourceCharPollGroupChildDeps>& child_deps,
    const std::string& changed_by_override,
    const std::string& change_override) {
  if (!changed_by_override.empty() || !change_override.empty()) {
    deps.changed_by.push_back(changed_by_override);
    deps.change.push_back(change_override);
    return;
  }
  for (const SourceCharPollGroupChildDeps& child : child_deps) {
    deps.changed_by.push_back(child.changed_by);
    deps.change.push_back(child.change);
  }
}

SourceCharPollGroupLoadPlan source_char_poll_group_load_plan(int revision) {
  SourceCharPollGroupLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 3;
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object"};
  if (revision > 2) plan.read_order.push_back("CharWeightable");
  plan.read_order.push_back("mPolls");
  if (revision > 1) {
    plan.read_order.push_back("mChangedBy");
    plan.read_order.push_back("mChanges");
  }
  return plan;
}

SourceCharPollGroupSavePlan source_char_poll_group_save_plan() {
  return SourceCharPollGroupSavePlan{};
}

SourceCharPollGroupCopyPlan source_char_poll_group_copy_plan() {
  SourceCharPollGroupCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "CharWeightable"};
  plan.copy_from_max_steps = {"iterate source mPolls",
                              "append missing poll refs only"};
  plan.copied_members = {"mPolls", "mChangedBy", "mChanges"};
  return plan;
}

SourceCharPollGroupHandlerPlan source_char_poll_group_handler_plan() {
  SourceCharPollGroupHandlerPlan plan;
  plan.action_handlers = {"sort_polls"};
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0xA2;
  return plan;
}

SourceCharPollGroupPropSyncPlan source_char_poll_group_prop_sync_plan() {
  SourceCharPollGroupPropSyncPlan plan;
  plan.properties = {"polls", "changed_by", "changes"};
  plan.superclasses = {"CharWeightable"};
  return plan;
}

SourceCharPollGroupSortPlan source_char_poll_group_sort_plan() {
  SourceCharPollGroupSortPlan plan;
  plan.steps = {"reserve mPolls size", "copy refs into RndPollable vector",
                "CharPollableSorter::Sort", "clear mPolls",
                "push sorted refs as CharPollable"};
  return plan;
}

SourceCharIKScaleDefaultState source_char_ik_scale_default_state() {
  return SourceCharIKScaleDefaultState{};
}

SourceCharIKScaleLoadPlan source_char_ik_scale_load_plan(int revision) {
  SourceCharIKScaleLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 3;
  if (!plan.known_revision) {
    return plan;
  }

  plan.read_order = {"Hmx::Object", "CharWeightable", "mDest", "mScale"};
  if (revision > 1) {
    plan.read_order.push_back("mSecondaryTargets");
  }
  if (revision > 2) {
    plan.read_order.push_back("mAutoWeight");
    plan.read_order.push_back("mBottomHeight");
    plan.read_order.push_back("mTopHeight");
  }
  return plan;
}

SourceCharIKScaleCopyPlan source_char_ik_scale_copy_plan() {
  SourceCharIKScaleCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "CharWeightable"};
  plan.copied_members = {"mDest",        "mScale",       "mSecondaryTargets",
                         "mAutoWeight",  "mBottomHeight", "mTopHeight"};
  return plan;
}

SourceCharIKScaleHandlerPlan source_char_ik_scale_handler_plan() {
  SourceCharIKScaleHandlerPlan plan;
  plan.superclasses = {"CharWeightable", "Hmx::Object"};
  plan.actions = {"capture_before", "capture_after"};
  plan.check = 0xCC;
  return plan;
}

SourceCharIKScalePropSyncPlan source_char_ik_scale_prop_sync_plan() {
  SourceCharIKScalePropSyncPlan plan;
  plan.properties = {"dest",          "scale",         "secondary_targets",
                     "auto_weight",   "bottom_height", "top_height"};
  plan.superclasses = {"CharWeightable"};
  return plan;
}

SourceCharIKScaleSavePlan source_char_ik_scale_save_plan() {
  return SourceCharIKScaleSavePlan{};
}

bool source_char_ik_scale_poll_enters(bool has_dest, float weight) {
  return has_dest && weight != 0.0f;
}

float source_char_ik_scale_capture_before(bool has_dest, float dest_local_z,
                                          float current_scale) {
  return has_dest ? dest_local_z : current_scale;
}

float source_char_ik_scale_capture_after(bool has_dest, float dest_local_z,
                                         float current_scale) {
  return has_dest ? dest_local_z / current_scale : current_scale;
}

void source_char_ik_scale_poll_deps(
    SourceCharIKScalePollDeps& deps,
    const std::string& dest,
    const std::vector<std::string>& secondary_targets) {
  deps.change.push_back(dest);
  for (const std::string& target : secondary_targets) {
    deps.change.push_back(target);
  }
  deps.changed_by.push_back(dest);
}

SourceCharacterState source_character_default_state() {
  return SourceCharacterState{};
}

SourceCharacterLodState source_character_lod_default_state() {
  return SourceCharacterLodState{};
}

SourceCharacterLodState source_character_lod_copy_state(
    const SourceCharacterLodState& lod) {
  return lod;
}

void source_character_lod_assign(SourceCharacterLodState& dest,
                                 const SourceCharacterLodState& src) {
  dest.screen_size = src.screen_size;
  dest.group = src.group;
  dest.trans_group = src.trans_group;
}

SourceCharacterLodCopyPlan source_character_lod_copy_plan() {
  SourceCharacterLodCopyPlan plan;
  plan.copied_members = {"mScreenSize", "mGroup", "mTransGroup"};
  return plan;
}

SourceCharacterLodPropSyncPlan source_character_lod_prop_sync_plan() {
  SourceCharacterLodPropSyncPlan plan;
  plan.properties = {"screen_size", "group", "trans_group"};
  return plan;
}

SourceObjectDirDefaultState source_object_dir_default_state() {
  return SourceObjectDirDefaultState{};
}

SourceObjectDirSavePlan source_object_dir_save_plan() {
  return SourceObjectDirSavePlan{};
}

SourceObjectDirPreLoadPlan source_object_dir_preload_plan(
    int revision,
    bool loading_proxy_from_disk,
    bool proxy_override) {
  SourceObjectDirPreLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 0x1b;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "ASSERT_REVS(0x1B,0)"};
  if (revision > 0x15) {
    plan.read_order.push_back("Hmx::Object::LoadType");
  } else if (revision >= 2 && revision <= 0x10) {
    plan.read_order.push_back("Hmx::Object::Load");
  }
  if (revision < 3) {
    plan.read_order.push_back("reserveHashCount");
    plan.read_order.push_back("reserveStringCount");
  }
  if (revision > 0x19) {
    plan.read_order.push_back("mAlwaysInlined");
    plan.read_order.push_back("mAlwaysInlineHashBytes");
  }
  if (revision > 1) {
    plan.read_order.push_back("viewports");
    plan.read_order.push_back("currentViewportIndex");
  }
  if (revision > 0x0c) {
    if (revision > 0x13) {
      plan.read_order.push_back(
          loading_proxy_from_disk ? "inlineProxyDummy" : "mInlineProxy");
    }
    plan.read_order.push_back("proxyFilePath");
    plan.branches.push_back(proxy_override ? "proxyOverridePath"
                                           : "storedProxyPath");
  }
  if (revision < 11) plan.read_order.push_back("legacyStringA");
  if (revision < 11) plan.read_order.push_back("legacyStringB");
  if (revision == 5) plan.read_order.push_back("legacyStringC");
  if (revision > 2) {
    plan.read_order.push_back("notInlinedSubDirs");
    if (revision == 0x17) plan.read_order.push_back("legacyIntVector");
    if (revision < 0x15) {
      plan.branches.push_back("clearInlinedSubDirs");
    } else {
      plan.read_order.push_back("mInlineSubDirType");
      plan.read_order.push_back("inlinedSubDirs");
    }
  }
  if (revision == 0x0c || revision == 0x0d) {
    plan.read_order.push_back("OldLoadProxies");
  }
  plan.branches.push_back("mIsSubDir=false");
  plan.pushes_revision = true;
  return plan;
}

SourceObjectDirPostLoadPlan source_object_dir_postload_plan(
    int revision,
    int inlined_dir_count,
    bool stream_cached,
    bool is_proxy,
    bool proxy_file_empty,
    bool proxy_override,
    bool edit_mode,
    bool allows_inline_proxy) {
  SourceObjectDirPostLoadPlan plan;
  plan.steps = {"PopRev", "restore gRev/gAltRev"};
  if (inlined_dir_count > 0) {
    plan.steps.push_back("postloadInlinedDirsReverse");
  }
  if (revision > 0x17) {
    plan.steps.push_back(stream_cached ? "useCachedRevs2" : "PopRev(revs2)");
    plan.steps.push_back("PopRev(subdirOffset)");
    plan.steps.push_back("postloadOffsetSubDirs");
  } else {
    plan.steps.push_back("postloadAllSubDirs");
  }
  if (revision > 10) plan.steps.push_back("readCurrentCameraStrings");
  if (revision > 0x15) {
    plan.steps.push_back("LoadRest");
  } else if (revision > 0x10) {
    plan.steps.push_back("Hmx::Object::Load");
  }
  plan.steps.push_back("HandleType(change_proxies_msg)");
  if (proxy_override) {
    plan.branches.push_back("clearProxyOverride");
    if (!edit_mode && (!is_proxy || allows_inline_proxy)) {
      plan.branches.push_back("failInlinedProxyOverride");
    }
  } else if (is_proxy && !proxy_file_empty) {
    plan.branches.push_back("DeleteObjects");
    plan.branches.push_back("DeleteSubDirs");
    plan.branches.push_back("createDirLoaderForProxyFile");
  }
  return plan;
}

SourceObjectDirFindObjectPlan source_object_dir_find_object_plan(
    bool entry_hit,
    bool subdir_hit,
    bool name_matches_self,
    bool parent_dirs,
    bool has_parent_dir,
    bool parent_is_self,
    bool is_main_dir) {
  SourceObjectDirFindObjectPlan plan;
  plan.search_order.push_back("FindEntry(local)");
  if (entry_hit) {
    plan.result = "entry";
    return plan;
  }
  plan.search_order.push_back("scanSubDirs");
  if (subdir_hit) {
    plan.result = "subdir";
    return plan;
  }
  plan.search_order.push_back("compareSelfName");
  if (name_matches_self) {
    plan.result = "self";
    return plan;
  }
  if (parent_dirs) {
    if (has_parent_dir && !parent_is_self) {
      plan.search_order.push_back("parentDir");
      plan.result = "parentDir";
      return plan;
    }
    if (!is_main_dir) {
      plan.search_order.push_back("mainDir");
      plan.result = "mainDir";
      return plan;
    }
  }
  plan.result = "null";
  return plan;
}

SourceObjectDirSubDirPlan source_object_dir_subdir_plan(bool add_subdir) {
  SourceObjectDirSubDirPlan plan;
  plan.set_subdir_true = add_subdir;
  plan.clears_name_and_type = add_subdir;
  plan.added_sets_subdir_true = add_subdir;
  plan.added_publishes_nested_objects = add_subdir;
  plan.removing_sets_subdir_false = !add_subdir;
  plan.removing_publishes_nested_objects = !add_subdir;
  return plan;
}

SourceRndDirDefaultState source_rnddir_default_state() {
  return SourceRndDirDefaultState{};
}

SourceRndDirSavePlan source_rnddir_save_plan() {
  return SourceRndDirSavePlan{};
}

SourceRndDirLoadPlan source_rnddir_load_plan(int revision,
                                             bool loading_proxy_from_disk) {
  SourceRndDirLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 0x0a;
  if (!plan.known_revision) return plan;
  plan.preload_steps = {"LOAD_REVS", "ASSERT_REVS(0xA,0)",
                        "PushRev(packRevs(gAltRev,gRev))",
                        "ObjectDir::PreLoad"};
  plan.postload_steps = {"ObjectDir::PostLoad", "PopRev",
                         "restore gRev/gAltRev", "RndAnimatable::Load",
                         "RndDrawable::Load"};
  if (revision != 0) plan.postload_steps.push_back("RndTransformable::Load");
  if (revision > 1) {
    plan.postload_reads.push_back(
        loading_proxy_from_disk ? "mEnvProxyDummy" : "mEnv");
  }
  if (revision > 2 && revision != 9) plan.postload_reads.push_back("mTestEvent");
  if (revision >= 4 && revision <= 8) {
    plan.postload_reads.push_back("legacySymbolA");
    plan.postload_reads.push_back("legacySymbolB");
  }
  if (revision >= 5 && revision <= 7) {
    plan.postload_reads.push_back("legacyRndPostProc");
    plan.branches.push_back("loadAndDeleteRndPostProc");
  }
  return plan;
}

SourceRndDirSyncObjectsPlan source_rnddir_sync_objects_plan(
    bool is_subdir,
    bool parent_dir_is_msg_source) {
  SourceRndDirSyncObjectsPlan plan;
  if (is_subdir) return plan;
  plan.calls_sync_drawables = true;
  plan.collects_animatables = true;
  plan.removes_anim_children = true;
  plan.collects_pollables = true;
  plan.removes_poll_children = true;
  plan.sorts_polls = true;
  plan.chains_source_subdir = parent_dir_is_msg_source;
  plan.calls_object_dir_sync = true;
  return plan;
}

SourceRndDirSyncDrawablesPlan source_rnddir_sync_drawables_plan(
    bool is_subdir) {
  SourceRndDirSyncDrawablesPlan plan;
  if (is_subdir) return plan;
  plan.collects_drawables = true;
  plan.updates_preclear_state = true;
  plan.removes_draw_children = true;
  plan.sorts_draws = true;
  return plan;
}

SourceRndDirCopyPlan source_rnddir_copy_plan() {
  SourceRndDirCopyPlan plan;
  plan.copied_superclasses = {"ObjectDir", "RndAnimatable", "RndDrawable",
                              "RndTransformable"};
  plan.member_gate = "ty != kCopyFromMax";
  plan.copied_members = {"mEnv", "mTestEvent"};
  return plan;
}

SourceRndDirHandlerPlan source_rnddir_handler_plan() {
  SourceRndDirHandlerPlan plan;
  plan.handlers = {"show_objects", "supported_events"};
  plan.superclasses = {"RndAnimatable", "RndDrawable", "RndTransformable",
                       "RndPollable",   "ObjectDir",   "MsgSource"};
  plan.check = 609;
  return plan;
}

SourceRndDirPropSyncPlan source_rnddir_prop_sync_plan() {
  SourceRndDirPropSyncPlan plan;
  plan.properties = {"environ", "polls", "draws", "test_event"};
  plan.superclasses = {"ObjectDir", "RndTransformable", "RndDrawable",
                       "RndAnimatable"};
  return plan;
}

SourceCharacterLoadPlan source_character_load_plan(int revision,
                                                   bool is_proxy,
                                                   int legacy_other_revision) {
  SourceCharacterLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 0x11;
  if (!plan.known_revision) return plan;

  plan.preload_steps = {"LOAD_REVS", "ASSERT_REVS(0x11,0)"};
  if (revision > 1) {
    plan.preload_steps.push_back("RndDir::PreLoad");
    if (revision < 7) plan.preload_steps.push_back("mRate=k1_fpb");
    plan.preload_steps.push_back("PushRev(packRevs(gAltRev,gRev))");
  } else {
    plan.preload_steps.push_back("somerev");
    if (legacy_other_revision > 3) {
      plan.preload_steps.push_back("RndTransformable::Load");
      plan.preload_steps.push_back("RndDrawable::Load");
    }
    plan.preload_steps.push_back("ObjectDir::PreLoad");
    plan.preload_steps.push_back("PushRev(somerev)");
    plan.preload_steps.push_back("PushRev(packRevs(gAltRev,gRev))");
  }

  plan.postload_steps = {"PopRev(packRevs)", "gRev=getHmxRev",
                         "gAltRev=getAltRev"};
  if (revision > 1) {
    plan.postload_steps.push_back("RndDir::PostLoad");
    plan.postload_steps.push_back("gRev=oldRev");
    if (revision < 4 || !is_proxy) {
      if (revision < 9) {
        plan.postload_reads.push_back("legacyNestedLods");
      } else {
        plan.postload_reads.push_back("mLods");
      }
      plan.postload_reads.push_back("mShadow");
      if (revision > 2) {
        plan.postload_reads.push_back("mSelfShadow");
      } else {
        plan.branches.push_back("mSelfShadow=false");
      }
      if (revision > 4) {
        plan.postload_reads.push_back("mSphereBase");
      } else {
        plan.branches.push_back("mSphereBase=this");
      }
      if (revision > 0x0a) {
        plan.postload_reads.push_back("mBounding");
      } else {
        plan.branches.push_back("mBounding.Zero");
      }
      if (revision < 0x0c) {
        plan.branches.push_back("legacyBoundingFromSphereWhenSelf");
      }
      if (revision > 0x0c) plan.postload_reads.push_back("mFrozen");
      if (revision > 0x0e) plan.postload_reads.push_back("mMinLod");
      if (revision > 0x10) plan.postload_reads.push_back("mTransGroup");
      if (revision > 9) plan.postload_reads.push_back("mTest");
    } else if (revision > 0x0f) {
      plan.postload_reads.push_back("mTest");
      plan.branches.push_back("proxyTestOnly");
    }
  } else {
    plan.postload_steps.push_back("PopRev(somerev)");
    plan.postload_steps.push_back("ObjectDir::PostLoad");
    plan.postload_steps.push_back("gRev=oldotherrev");
    if (legacy_other_revision > 4) plan.postload_reads.push_back("mEnv");
    if (legacy_other_revision > 3) {
      plan.postload_reads.push_back("legacyNestedLods");
      if (legacy_other_revision < 6) plan.branches.push_back("legacyRenameLods");
    } else {
      plan.branches.push_back("mLods.clear");
    }
    if (legacy_other_revision > 6) plan.postload_reads.push_back("mShadow");
  }

  if (revision < 8) plan.branches.push_back("scaleLodScreenSizeBySphereRadius");
  return plan;
}

SourceCharacterCopyPlan source_character_copy_plan() {
  SourceCharacterCopyPlan plan;
  plan.copied_superclasses = {"RndDir"};
  plan.member_gate = "ty != kCopyFromMax";
  plan.copied_members = {"mLods",       "mLastLod",    "mMinLod",
                         "mShadow",     "mDriver",     "mSelfShadow",
                         "mSphereBase", "mFrozen",     "mMinLod",
                         "mTransGroup"};
  return plan;
}

SourceCharacterHandlerPlan source_character_handler_plan() {
  SourceCharacterHandlerPlan plan;
  plan.handlers = {"teleport",           "play_clip",
                   "calc_bounding_sphere", "copy_bounding_sphere",
                   "find_interest_objects", "force_interest",
                   "force_interest_named",  "enable_blink"};
  plan.debug_handlers = {"list_interest_objects", "mTest"};
  plan.superclass = "RndDir";
  plan.check = "0x57B";
  return plan;
}

SourceCharacterPropSyncPlan source_character_prop_sync_plan() {
  SourceCharacterPropSyncPlan plan;
  plan.set_properties = {"sphere_base", "shadow", "driver"};
  plan.properties = {"lods",       "force_lod", "trans_group",
                     "self_shadow", "bounding",  "frozen"};
  plan.modify_properties = {"interest_to_force"};
  plan.debug_properties = {"debug_draw_interest_objects", "CharacterTesting"};
  plan.superclass = "RndDir";
  return plan;
}

SourceCharacterSavePlan source_character_save_plan() {
  return SourceCharacterSavePlan{};
}

SourceCharacterPlayClipDecision source_character_on_play_clip(
    bool has_driver,
    int32_t message_size,
    int32_t supplied_play_flags,
    bool driver_play_returned) {
  SourceCharacterPlayClipDecision decision;
  decision.has_driver = has_driver;
  if (!has_driver) return decision;

  decision.play_flags = message_size > 3 ? supplied_play_flags : 4;
  decision.would_assert_size = message_size > 4;
  if (decision.would_assert_size) return decision;

  decision.called_driver_play = true;
  decision.returns_true = driver_play_returned;
  return decision;
}

SourceCharacterCopyBoundingSphereHandlerResult
source_character_on_copy_bounding_sphere(bool has_source_character) {
  SourceCharacterCopyBoundingSphereHandlerResult result;
  result.copied = has_source_character;
  return result;
}

void source_character_enter(SourceCharacterState& state) {
  state.poll_state = SourceCharacterPollState::kEntered;
  state.min_lod = -1;
  state.frozen = false;
  state.last_lod = 0;
  state.teleported = true;
  state.interest_to_force.clear();
}

void source_character_exit(SourceCharacterState& state) {
  state.poll_state = SourceCharacterPollState::kExited;
}

SourceCharacterPollResult source_character_poll(SourceCharacterState& state) {
  SourceCharacterPollResult result;
  if (state.frozen) {
    result.skipped_for_frozen = true;
    return result;
  }
  result.called_rnd_dir_poll = true;
  state.teleported = false;
  state.poll_state = SourceCharacterPollState::kPolled;
  return result;
}

bool source_character_bone_servo_resolves(bool has_driver,
                                          bool driver_bones_is_servo) {
  return has_driver && driver_bones_is_servo;
}

SourceCharacterReplaceResult source_character_replace(
    SourceCharacterState& state,
    bool from_is_sphere_base,
    bool to_is_transformable) {
  SourceCharacterReplaceResult result;
  result.called_rnd_dir_replace = true;
  if (from_is_sphere_base) {
    result.repointed_sphere_base = true;
    state.sphere_base_is_self = !to_is_transformable;
    result.fell_back_to_self = !to_is_transformable;
  }
  return result;
}

SourceCharacterAddedObjectResult source_character_added_object(
    SourceCharacterState& state,
    bool is_char_pollable,
    bool is_char_driver,
    const std::string& object_name) {
  SourceCharacterAddedObjectResult result;
  result.accepted_pollable = is_char_pollable;
  if (is_char_pollable && is_char_driver && object_name == "main.drv") {
    state.has_driver = true;
    result.assigned_main_driver = true;
  }
  return result;
}

SourceCharacterRemoveObjectResult source_character_removing_object(
    SourceCharacterState& state,
    bool object_is_current_driver) {
  SourceCharacterRemoveObjectResult result;
  if (object_is_current_driver) {
    state.has_driver = false;
    result.cleared_driver = true;
  }
  result.called_rnd_dir_removing_object = true;
  return result;
}

SourceCharacterSyncObjectsResult source_character_sync_objects(
    SourceCharacterState& state,
    bool has_bone_pelvis_mesh,
    int32_t lod_count) {
  SourceCharacterSyncObjectsResult result;
  state.poll_state = SourceCharacterPollState::kSyncObject;
  result.converted_bones_to_transes = has_bone_pelvis_mesh;
  result.called_rnd_dir_sync_objects = true;
  result.removed_trans_group = true;
  result.removed_lod_draws = lod_count > 0 ? lod_count * 2 : 0;
  result.synced_shadow = true;
  result.sorted_polls = true;
  return result;
}

SourceCharacterRuntimeDumpEvidence source_character_runtime_dump_evidence() {
  SourceCharacterRuntimeDumpEvidence evidence;
  evidence.poll_range = "0x8030D360 -> 0x8030D434";
  evidence.bone_servo_range = "0x8030E15C -> 0x8030E190";
  evidence.convert_bones_to_transes_range = "0x8030EE9C -> 0x8030F7CC";
  evidence.sync_objects_range = "0x8030F7CC -> 0x8030FD5C";
  evidence.poll_locals = {"AutoTimer _at"};
  evidence.bone_servo_references = {"CharBonesObject RTTI",
                                    "CharServoBone RTTI"};
  evidence.convert_bones_to_transes_locals = {"list meshes", "ObjDirItr mesh",
                                              "ObjDirItr trans"};
  evidence.sync_objects_locals = {"CharPollableSorter sorter"};
  evidence.has_statement_bodies = false;
  evidence.safe_to_publish_pose = false;
  evidence.safe_to_replace_pose_publisher = false;
  return evidence;
}

SourceCharacterInterestResult source_character_force_blink(bool has_eyes) {
  return {has_eyes, has_eyes};
}

SourceCharacterInterestResult source_character_enable_blinks(bool has_eyes) {
  return {has_eyes, has_eyes};
}

SourceCharacterInterestResult source_character_set_focus_interest(
    bool has_eyes) {
  return {has_eyes, has_eyes};
}

SourceCharacterInterestResult source_character_set_interest_filter_flags(
    bool has_eyes) {
  return {has_eyes, has_eyes};
}

SourceCharacterInterestResult source_character_clear_interest_filter_flags(
    bool has_eyes) {
  return {has_eyes, has_eyes};
}

SourceCharacterCurrentInterestsResult source_character_on_get_current_interests(
    bool has_eyes,
    const std::vector<std::string>& interest_names) {
  SourceCharacterCurrentInterestsResult result;
  result.found_eyes = has_eyes;
  result.interest_count =
      has_eyes ? static_cast<int32_t>(interest_names.size()) : 0;
  result.data_array_symbols.reserve(
      static_cast<size_t>(result.interest_count) + 1u);
  result.data_array_symbols.push_back("");
  if (has_eyes) {
    for (const std::string& interest_name : interest_names) {
      result.data_array_symbols.push_back(interest_name);
    }
  }
  result.first_node_empty_symbol =
      !result.data_array_symbols.empty() && result.data_array_symbols[0].empty();
  return result;
}

SourceCharacterDebugDrawInterestResult
source_character_set_debug_draw_interest_objects(bool enabled) {
  SourceCharacterDebugDrawInterestResult result;
  result.assigned = true;
  result.debug_draw_interest_objects = enabled;
  return result;
}

SourceCharacterSetSphereBaseResult source_character_set_sphere_base(
    SourceCharacterState& state,
    bool has_transform) {
  SourceCharacterSetSphereBaseResult result;
  result.defaulted_to_self = !has_transform;
  result.made_world_sphere = true;
  result.multiplied_by_trans_world = true;
  result.set_sphere = true;
  state.sphere_base_is_self = !has_transform;
  state.sphere_base_is_null = false;
  return result;
}

SourceCharacterSetInterestObjectsResult source_character_set_interest_objects(
    bool has_eyes,
    const std::vector<bool>& validate_results,
    bool has_override_dir) {
  SourceCharacterSetInterestObjectsResult result;
  result.found_eyes = has_eyes;
  if (!has_eyes) return result;
  result.cleared_all = true;
  for (bool valid : validate_results) {
    ++result.validated_count;
    if (has_override_dir) {
      ++result.used_override_dir_count;
    } else {
      ++result.used_interest_dir_count;
    }
    if (valid) ++result.add_count;
  }
  return result;
}

SourceCharacterAddShadowBoneResult source_character_add_shadow_bone(
    int32_t current_shadow_bones,
    bool has_transform,
    bool already_hooked) {
  SourceCharacterAddShadowBoneResult result;
  result.final_shadow_bones = current_shadow_bones > 0 ? current_shadow_bones : 0;
  if (!has_transform) {
    result.returned_null = true;
    return result;
  }
  if (already_hooked) {
    result.returned_existing = true;
    return result;
  }
  result.created = true;
  ++result.final_shadow_bones;
  return result;
}

SourceCharacterUnhookShadowResult source_character_unhook_shadow(
    int32_t current_shadow_bones) {
  SourceCharacterUnhookShadowResult result;
  result.deleted_shadow_bones =
      current_shadow_bones > 0 ? current_shadow_bones : 0;
  result.deleted_all = true;
  return result;
}

SourceCharacterSyncShadowResult source_character_sync_shadow(
    bool has_shadow,
    bool old_gfx,
    const std::vector<int32_t>& mesh_bone_counts) {
  SourceCharacterSyncShadowResult result;
  result.unhooked_shadow = true;
  if (!has_shadow) return result;
  if (old_gfx) {
    for (int32_t bone_count : mesh_bone_counts) {
      if (bone_count > 0) {
        result.hooked_bone_count += bone_count;
      } else {
        ++result.hooked_mesh_parent_count;
      }
    }
  }
  result.removed_shadow_draw = true;
  return result;
}

SourceCharacterCopyBoundingSphereResult source_character_copy_bounding_sphere(
    SourceCharacterState& state,
    bool source_has_sphere_base) {
  SourceCharacterCopyBoundingSphereResult result;
  result.set_sphere = true;
  result.copied_bounding = true;
  result.copied_sphere_base = source_has_sphere_base;
  result.cleared_sphere_base = !source_has_sphere_base;
  state.sphere_base_is_null = !source_has_sphere_base;
  if (!source_has_sphere_base) state.sphere_base_is_self = false;
  return result;
}

SourceCharacterRepointSphereBaseResult source_character_repoint_sphere_base(
    SourceCharacterState& state,
    bool found_matching_transform) {
  SourceCharacterRepointSphereBaseResult result;
  result.had_sphere_base = !state.sphere_base_is_null;
  if (!result.had_sphere_base) return result;
  result.looked_up_by_name = true;
  result.repointed = found_matching_transform;
  if (found_matching_transform) {
    state.sphere_base_is_null = false;
    state.sphere_base_is_self = false;
  }
  return result;
}

SourceCharacterPreSaveResult source_character_pre_save() {
  return {true};
}

SourceBandCharacterDeformationPlan source_band_character_deformation_plan(
    bool has_deform_clip,
    bool edit_mode_bone_servo,
    bool in_closet) {
  SourceBandCharacterDeformationPlan plan;
  plan.has_deform_clip = has_deform_clip;
  plan.edit_mode_bone_servo = edit_mode_bone_servo;
  plan.in_closet = in_closet;
  if (!has_deform_clip) return plan;

  plan.poses_neutral_before_cache = true;
  plan.poses_weighted_after_cache = true;
  plan.captures_ik_scale_before = true;
  plan.captures_ik_scale_after = true;
  plan.measures_ik_hand_lengths_after_deform = true;
  plan.clears_dirty_bit = true;
  plan.steps = {
      "BandCharDesc::GetDeformClip(mGender)",
      "CharBonesMeshes tmp_bones",
      "meshes.SetName(tmp_bones,this)",
      "clip.StuffBones(meshes)",
      "clip.ScaleDown(meshes,0)",
      "clip.ScaleAdd(meshes,1,0,0)",
      "meshes.PoseMeshes(neutral)",
      edit_mode_bone_servo ? "BoneServo.AcquirePose" : "skip BoneServo.AcquirePose",
      "CharIKScale.CaptureBefore",
      in_closet ? "CharMeshCacheMgr.Disable(false)"
                : "CharMeshCacheMgr.Disable(true)",
      "CharMeshCacheMgr.SyncMesh(mask=0xBF)",
      "DeformHead",
      "CharCuff.Deform",
      "outfitMeshes.clear",
      "CharMeshCacheMgr.StuffMeshes(outfitMeshes)",
      "clip.ScaleDown(meshes,0)",
      "ComputeDeformWeights(weights[18])",
      "clip.ScaleAdd(meshes,weights[i],i,0)",
      "meshes.PoseMeshes(weighted)",
      "RndMeshDeform.Reskin",
      "CharCollide.DeformIfMeshCached",
      "CharIKScale.CaptureAfter",
      "CharIKHand.MeasureLengths",
      "SyncOutfitConfig",
      "OutfitConfig.ApplyAO",
      "delete CharMeshCacheMgr",
      "clear unk224 dirty bit 0x2",
  };
  return plan;
}

namespace {

bool source_char_pollable_sorter_changed_by_recurse(
    std::vector<SourceCharPollableSorterDep>& deps,
    int32_t target_index,
    int32_t query_index,
    int32_t search_id,
    std::vector<int32_t>& visited_indices) {
  if (query_index < 0 ||
      query_index >= static_cast<int32_t>(deps.size())) {
    return false;
  }
  if (query_index == target_index) return true;

  SourceCharPollableSorterDep& dep = deps[query_index];
  if (dep.search_id == search_id) return false;

  dep.search_id = search_id;
  visited_indices.push_back(query_index);
  for (const int32_t changed_by : dep.changed_by) {
    if (source_char_pollable_sorter_changed_by_recurse(
            deps, target_index, changed_by, search_id, visited_indices)) {
      return true;
    }
  }
  return false;
}

}  // namespace

SourceCharPollableSorterChangedByResult
source_char_pollable_sorter_changed_by(
    std::vector<SourceCharPollableSorterDep>& deps,
    int32_t target_index,
    int32_t query_index,
    int32_t current_search_id) {
  SourceCharPollableSorterChangedByResult result;
  result.search_id = current_search_id;
  if (target_index == query_index) {
    result.same_dep_short_circuit = true;
    return result;
  }

  result.search_id = current_search_id + 1;
  result.changed_by = source_char_pollable_sorter_changed_by_recurse(
      deps, target_index, query_index, result.search_id,
      result.visited_indices);
  return result;
}

SourceCharLifecyclePlan source_char_lifecycle_plan() {
  SourceCharLifecyclePlan plan;
  plan.init_steps = {"Character::Init",       "CharBonesObject::Init",
                     "CharBoneOffset::Init",  "PreloadSharedSubdirs(char)",
                     "CharBoneDir::Init",     "CharUtlInit",
                     "AddExitCallback(CharTerminate)"};
  plan.terminate_steps = {"RemoveExitCallback(CharTerminate)",
                          "Character::Terminate",
                          "CharBoneDir::Terminate"};
  return plan;
}

SourceCharacterTestState source_character_test_default_state() {
  return SourceCharacterTestState{};
}

SourceCharacterTestDestroyResult source_character_test_destroy(
    bool overlay_found,
    bool overlay_callback_is_this) {
  SourceCharacterTestDestroyResult result;
  if (overlay_found && overlay_callback_is_this) {
    result.cleared_callback = true;
    result.hid_overlay = true;
    result.restarted_timer = true;
  }
  return result;
}

SourceCharacterTestDrawResult source_character_test_draw(
    bool has_driver,
    bool has_clip1,
    bool has_clip2,
    bool has_bone_head,
    bool show_screen_size) {
  SourceCharacterTestDrawResult result;
  result.highlighted_driver = has_driver && (has_clip1 || has_clip2);
  result.draw_transform = has_bone_head ? "bone_head" : "self";
  result.drew_screen_size = show_screen_size;
  return result;
}

SourceCharacterTestPollResult source_character_test_poll(
    const SourceCharacterTestPollInput& input) {
  SourceCharacterTestPollResult result;
  const bool clip_branch =
      input.has_driver && input.has_clip_dir && input.has_clip1;
  if (!clip_branch) return result;

  result.entered_clip_branch = true;
  result.loaded_click_cue = !input.static_click_present;
  result.restored_click_static = true;
  result.metronome_edge =
      input.metronome &&
      (std::floor(input.beat - input.delta_beat) + 1.0f ==
       std::floor(input.beat));
  result.would_play_click = result.metronome_edge && input.static_click_present;

  if (!input.has_first_driver) {
    result.play_new = true;
  } else if (input.has_clip2) {
    const bool first_is_neither =
        !input.first_clip_is_clip1 && !input.first_clip_is_clip2;
    const bool first_is_clip2_after_transition =
        input.first_clip_is_clip2 &&
        input.transition_beat < input.first_driver_beat;
    result.play_new = first_is_neither || first_is_clip2_after_transition;
  } else {
    result.play_new = !input.first_clip_is_clip1;
  }

  if (input.zero_travel) {
    result.reset_bone_servo_regulate = input.has_bone_servo;
    result.recenter = true;
  }
  return result;
}

SourceCharacterTestAddDefaultsResult source_character_test_add_defaults(
    const SourceCharacterTestExisting& existing,
    const SourceCharacterTestBones& bones) {
  SourceCharacterTestAddDefaultsResult result;
  result.created_main_driver = !existing.has_main_driver;
  if (!existing.has_bone_servo) {
    result.created_bone_servo = !existing.has_bone_servo_object;
    result.set_driver_bones_to_bone_servo = true;
  }

  if (!existing.has_fore_twist_l && bones.bone_l_hand &&
      bones.bone_l_fore_twist2) {
    SourceCharacterTestControllerSetup setup;
    setup.name = "foreTwist_L.ik";
    setup.hand = "bone_L-hand";
    setup.twist2 = "bone_L-foreTwist2";
    setup.has_offset = true;
    setup.offset = 90.0f;
    result.controllers.push_back(setup);
  }
  if (!existing.has_fore_twist_r && bones.bone_r_hand &&
      bones.bone_r_fore_twist2) {
    SourceCharacterTestControllerSetup setup;
    setup.name = "foreTwist_R.ik";
    setup.hand = "bone_R-hand";
    setup.twist2 = "bone_R-foreTwist2";
    setup.has_offset = true;
    setup.offset = -90.0f;
    result.controllers.push_back(setup);
  }
  if (!existing.has_upper_twist_l && bones.bone_l_upper_twist1 &&
      bones.bone_l_upper_twist2 && bones.bone_l_upper_arm) {
    SourceCharacterTestControllerSetup setup;
    setup.name = "upperTwist_L.ik";
    setup.twist1 = "bone_L-upperTwist1";
    setup.twist2 = "bone_L-upperTwist2";
    setup.upper_arm = "bone_L-upperArm";
    result.controllers.push_back(setup);
  }
  if (!existing.has_upper_twist_r && bones.bone_r_upper_twist1 &&
      bones.bone_r_upper_twist2 && bones.bone_r_upper_arm) {
    SourceCharacterTestControllerSetup setup;
    setup.name = "upperTwist_R.ik";
    setup.twist1 = "bone_R-upperTwist1";
    setup.twist2 = "bone_R-upperTwist2";
    setup.upper_arm = "bone_R-upperArm";
    result.controllers.push_back(setup);
  }
  return result;
}

std::vector<std::string> source_character_test_walk(
    const std::vector<std::string>& walk_path) {
  std::vector<std::string> waypoints;
  if (!walk_path.empty()) {
    for (const std::string& waypoint : walk_path) {
      waypoints.push_back(waypoint);
    }
  }
  return waypoints;
}

std::string source_character_test_teleport_to(const std::string& waypoint) {
  return waypoint;
}

SourceCharacterTestStartEndBeatResult source_character_test_set_start_end_beat(
    bool milo_found,
    bool cur_anim_is_object,
    bool cur_anim_is_me,
    float start_beat,
    float end_beat,
    int32_t bpm) {
  SourceCharacterTestStartEndBeatResult result;
  result.found_milo = milo_found;
  if (!milo_found) return result;
  result.current_anim_is_object = cur_anim_is_object;
  if (!cur_anim_is_object) return result;
  result.current_anim_is_me = cur_anim_is_me;
  if (!cur_anim_is_me) return result;
  result.unfroze_character = true;
  result.set_bpm = true;
  result.sent_set_anim_frame = true;
  result.bpm = bpm;
  const float beats_per_second = static_cast<float>(bpm) / 60.0f;
  result.start_frame = (start_beat * 30.0f) / beats_per_second;
  result.end_frame = (end_beat * 30.0f) / beats_per_second;
  return result;
}

bool source_character_test_set_move_self(bool has_bone_servo) {
  return has_bone_servo;
}

SourceCharacterTestLoadResult source_character_test_load(
    int32_t revision,
    int32_t alt_revision) {
  SourceCharacterTestLoadResult result;
  result.fail_new_revision = revision > 0xF;
  result.fail_new_alt_revision = alt_revision != 0;
  result.loaded_driver = revision != 0xD;
  return result;
}

SourceCharTransDrawLoadPlan source_char_trans_draw_load_plan(int revision) {
  SourceCharTransDrawLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 1;
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "RndDrawable", "mChars"};
  plan.post_load_mode = SourceCharacterDrawMode::kOpaque;
  return plan;
}

SourceCharTransDrawSavePlan source_char_trans_draw_save_plan() {
  return SourceCharTransDrawSavePlan{};
}

SourceCharTransDrawCopyPlan source_char_trans_draw_copy_plan() {
  SourceCharTransDrawCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndDrawable"};
  plan.copied_members = {"mChars"};
  return plan;
}

SourceCharTransDrawHandlerPlan source_char_trans_draw_handler_plan() {
  SourceCharTransDrawHandlerPlan plan;
  plan.superclasses = {"RndDrawable", "Hmx::Object"};
  plan.check = 0x5e;
  return plan;
}

SourceCharTransDrawPropSyncPlan source_char_trans_draw_prop_sync_plan() {
  SourceCharTransDrawPropSyncPlan plan;
  plan.properties = {"chars"};
  plan.superclasses = {"RndDrawable"};
  return plan;
}

std::vector<SourceCharTransDrawStep> source_char_trans_draw_set_draw_modes(
    const std::vector<std::string>& chars,
    SourceCharacterDrawMode mode) {
  std::vector<SourceCharTransDrawStep> steps;
  steps.reserve(chars.size());
  for (const std::string& character : chars) {
    steps.push_back({character, mode, false});
  }
  return steps;
}

std::vector<SourceCharTransDrawStep> source_char_trans_draw_load_modes(
    const std::vector<std::string>& chars) {
  return source_char_trans_draw_set_draw_modes(
      chars, SourceCharacterDrawMode::kOpaque);
}

std::vector<SourceCharTransDrawStep> source_char_trans_draw_destruct_modes(
    const std::vector<std::string>& chars) {
  return source_char_trans_draw_set_draw_modes(
      chars, SourceCharacterDrawMode::kAll);
}

std::vector<SourceCharTransDrawStep> source_char_trans_draw_draw_showing(
    const std::vector<SourceCharTransDrawCharacter>& chars) {
  std::vector<SourceCharTransDrawStep> steps;
  for (const SourceCharTransDrawCharacter& character : chars) {
    if (!character.showing) continue;
    steps.push_back(
        {character.name, SourceCharacterDrawMode::kTranslucent, false});
    steps.push_back(
        {character.name, SourceCharacterDrawMode::kTranslucent, true});
    steps.push_back({character.name, SourceCharacterDrawMode::kOpaque, false});
  }
  return steps;
}

SourceCharCuffState source_char_cuff_default_state() {
  SourceCharCuffState cuff;
  cuff.shape[0].offset = -2.9f;
  cuff.shape[0].radius = 1.9f;
  cuff.shape[1].offset = 0.0f;
  cuff.shape[1].radius = 2.6f;
  cuff.shape[2].offset = 2.0f;
  cuff.shape[2].radius = 3.5f;
  cuff.outer_radius = cuff.shape[1].radius + 0.5f;
  return cuff;
}

SourceCharCuffLoadPlan source_char_cuff_load_plan(int revision) {
  SourceCharCuffLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 8;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "Hmx::Object", "RndTransformable",
                     "mShape[0].radius", "mShape[0].offset",
                     "mShape[1].radius", "mShape[1].offset",
                     "mShape[2].radius", "mShape[2].offset"};
  if (revision > 1) {
    plan.read_order.push_back("mOuterRadius");
  } else {
    plan.branches.push_back("mOuterRadius=mShape[1].radius+0.5");
  }
  if (revision > 2) {
    plan.read_order.push_back("mOpenEnd");
  } else {
    plan.branches.push_back("mOpenEnd=false");
  }
  if (revision > 3) {
    plan.read_order.push_back("mBone");
  } else {
    plan.branches.push_back("mBone=TransParent");
  }
  if (revision > 4) {
    plan.read_order.push_back("mEccentricity");
  } else {
    plan.branches.push_back("mEccentricity=1");
  }
  if (revision > 5) {
    plan.read_order.push_back("mCategory");
  } else {
    plan.branches.push_back("mCategory=empty");
  }
  if (revision > 7) {
    plan.read_order.push_back("mIgnore");
  }
  plan.warns_old_revision = revision < 7;
  if (plan.warns_old_revision) plan.branches.push_back("warnOldCharCuff");
  return plan;
}

SourceCharCuffSavePlan source_char_cuff_save_plan() {
  return SourceCharCuffSavePlan{};
}

SourceCharCuffCopyPlan source_char_cuff_copy_plan() {
  SourceCharCuffCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndTransformable"};
  plan.copied_members = {"mShape",        "mOuterRadius",  "mOpenEnd",
                         "mBone",         "mEccentricity", "mCategory",
                         "mIgnore"};
  return plan;
}

SourceCharCuffHandlerPlan source_char_cuff_handler_plan() {
  SourceCharCuffHandlerPlan plan;
  plan.superclasses = {"RndTransformable", "Hmx::Object"};
  plan.check = 0x1FE;
  return plan;
}

SourceCharCuffPropSyncPlan source_char_cuff_prop_sync_plan() {
  SourceCharCuffPropSyncPlan plan;
  plan.properties = {"offset0",      "radius0",      "offset1",
                     "radius1",      "offset2",      "radius2",
                     "outer_radius", "open_end",     "bone",
                     "eccentricity", "category",     "ignore"};
  plan.superclasses = {"RndTransformable"};
  return plan;
}

float source_char_cuff_eccentricity(float x, float y, float eccentricity) {
  const float f1 = y * y;
  const float f2 = x * x;
  return std::sqrt((f1 + f2) /
                   (f1 * (1.0f / (eccentricity * eccentricity)) + f2));
}

void source_char_cuff_apply_revision_defaults(SourceCharCuffState& cuff,
                                              int32_t revision,
                                              const std::string& trans_parent) {
  if (revision <= 1) cuff.outer_radius = cuff.shape[1].radius + 0.5f;
  if (revision <= 2) cuff.open_end = false;
  if (revision <= 3) cuff.bone = trans_parent;
  if (revision <= 4) cuff.eccentricity = 1.0f;
  if (revision <= 5) cuff.category.clear();
  if (revision <= 7) cuff.ignore.clear();
}

namespace {

void source_char_cuff_add_bone_children_impl(
    const SourceCharCuffTransformNode* trans,
    std::vector<std::string>& bones) {
  if (!trans) return;
  if (trans->name.rfind("bone_", 0) != 0) return;
  bones.push_back(trans->name);
  for (const SourceCharCuffTransformNode& child : trans->children) {
    source_char_cuff_add_bone_children_impl(&child, bones);
  }
}

}  // namespace

std::vector<std::string> source_char_cuff_add_bone_children(
    const SourceCharCuffTransformNode* trans) {
  std::vector<std::string> bones;
  source_char_cuff_add_bone_children_impl(trans, bones);
  return bones;
}

SourceCharCuffDeformRuntimeMap source_char_cuff_deform_runtime_map() {
  SourceCharCuffDeformRuntimeMap map;
  map.runtime_functions = {"BoneMask(list,RndMesh*)",
                           "CharCuff::DeformAll(SyncMeshCB*)",
                           "CharCuff::Deform(SyncMeshCB*)",
                           "CharCuff::DeformMesh(RndMesh*,int,SyncMeshCB*)"};
  map.deform_locals = {"meshes", "cuff", "cat",  "ol",
                       "bones",  "mesh", "mask"};
  map.deform_mesh_locals = {"changed", "verts", "w",             "inv",
                            "Plane",   "bottom", "center",        "delta",
                            "rad2",    "allowedRadius", "frac",   "end"};
  return map;
}

SourceCharBlendBoneState source_char_blend_bone_default_state() {
  return SourceCharBlendBoneState{};
}

SourceCharBlendBoneConstraintLoadPlan
source_char_blend_bone_constraint_load_plan() {
  SourceCharBlendBoneConstraintLoadPlan plan;
  plan.read_order = {"mTarget", "mWeight"};
  return plan;
}

SourceCharBlendBoneLoadPlan source_char_blend_bone_load_plan(int revision) {
  SourceCharBlendBoneLoadPlan plan;
  plan.known_revision = revision >= 0 && revision <= 3;
  if (!plan.known_revision) return plan;

  plan.read_order = {"LOAD_REVS", "Hmx::Object", "mTargets", "mSrc1",
                     "mSrc2",     "mTransX",     "mTransY",  "mTransZ",
                     "mRotation"};
  return plan;
}

SourceCharBlendBoneSavePlan source_char_blend_bone_save_plan() {
  return SourceCharBlendBoneSavePlan{};
}

SourceCharBlendBoneCopyPlan source_char_blend_bone_copy_plan() {
  SourceCharBlendBoneCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mTargets", "mSrc1",   "mSrc2", "mTransX",
                         "mTransY",  "mTransZ", "mRotation"};
  return plan;
}

SourceCharBlendBoneHandlerPlan source_char_blend_bone_handler_plan() {
  SourceCharBlendBoneHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x8F;
  return plan;
}

SourceCharBlendBoneConstraintPropSyncPlan
source_char_blend_bone_constraint_prop_sync_plan() {
  SourceCharBlendBoneConstraintPropSyncPlan plan;
  plan.properties = {"target", "weight"};
  return plan;
}

SourceCharBlendBonePropSyncPlan source_char_blend_bone_prop_sync_plan() {
  SourceCharBlendBonePropSyncPlan plan;
  plan.properties = {"targets", "src_one", "src_two", "trans_x",
                     "trans_y", "trans_z", "rotation"};
  return plan;
}

SourceCharBlendBoneRuntimeDumpEvidence
source_char_blend_bone_runtime_dump_evidence() {
  SourceCharBlendBoneRuntimeDumpEvidence evidence;
  evidence.replace_range = "0x8031774C -> 0x80317928";
  evidence.poll_range = "0x80317928 -> 0x80317C24";
  evidence.poll_deps_range = "0x80317C24 -> 0x80317D58";
  evidence.load_range = "0x80317E64 -> 0x80318208";
  evidence.sync_property_range = "0x80318AAC -> 0x80318CC0";
  evidence.replace_locals = {"int i"};
  evidence.poll_locals = {"Transform dst", "Quat q", "float weight", "int i",
                          "Quat tmp"};
  evidence.poll_deps_locals = {"int i"};
  evidence.load_locals = {"int size", "int i", "Transform t"};
  evidence.sync_property_symbols = {"targets", "src_one", "src_two"};
  return evidence;
}

void source_char_blend_bone_poll_deps(
    SourceCharBlendBonePollDeps& deps,
    const SourceCharBlendBoneState& blend) {
  deps.changed_by.push_back(blend.src1);
  deps.changed_by.push_back(blend.src2);
  for (const SourceCharBlendBoneConstraint& target : blend.targets) {
    deps.change.push_back(target.target);
  }
}

namespace {

using SourceVec3 = std::array<float, 3>;

SourceVec3 source_vec_add(SourceVec3 a, SourceVec3 b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

SourceVec3 source_vec_sub(SourceVec3 a, SourceVec3 b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

SourceVec3 source_vec_scale(SourceVec3 a, float scale) {
  return {a[0] * scale, a[1] * scale, a[2] * scale};
}

float source_vec_dot(SourceVec3 a, SourceVec3 b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

SourceVec3 source_vec_cross(SourceVec3 a, SourceVec3 b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

float source_vec_length(SourceVec3 a) {
  return std::sqrt(source_vec_dot(a, a));
}

SourceVec3 source_vec_normalize(SourceVec3 a) {
  const float len = source_vec_length(a);
  if (len == 0.0f) return {0.0f, 0.0f, 0.0f};
  return source_vec_scale(a, 1.0f / len);
}

SourceVec3 source_vec_scale_to_magnitude(SourceVec3 a, float magnitude) {
  const float len = source_vec_length(a);
  if (len == 0.0f) return {0.0f, 0.0f, 0.0f};
  return source_vec_scale(a, magnitude / len);
}

SourceVec3 source_xfm_pos(const milo_scene::Xfm& xfm) {
  return {xfm.pos[0], xfm.pos[1], xfm.pos[2]};
}

SourceVec3 source_xfm_row(const milo_scene::Xfm& xfm, int row) {
  return {xfm.rot[row][0], xfm.rot[row][1], xfm.rot[row][2]};
}

float source_xfm_z_angle(const milo_scene::Xfm& xfm) {
  return std::atan2(xfm.rot[0][1], xfm.rot[0][0]);
}

void source_set_xfm_pos(milo_scene::Xfm& xfm, SourceVec3 v) {
  xfm.pos[0] = v[0];
  xfm.pos[1] = v[1];
  xfm.pos[2] = v[2];
}

void source_set_xfm_row(milo_scene::Xfm& xfm, int row, SourceVec3 v) {
  xfm.rot[row][0] = v[0];
  xfm.rot[row][1] = v[1];
  xfm.rot[row][2] = v[2];
}

float source_limit_ang(float radians) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 6.28318530717958647692f;
  while (radians > kPi) radians -= kTwoPi;
  while (radians < -kPi) radians += kTwoPi;
  return radians;
}

void source_rotate_about_z(milo_scene::Xfm& xfm, float angle) {
  const float ca = std::cos(angle);
  const float sa = std::sin(angle);
  for (int r = 0; r < 3; ++r) {
    const float x = xfm.rot[r][0];
    const float y = xfm.rot[r][1];
    xfm.rot[r][0] = ca * x - sa * y;
    xfm.rot[r][1] = sa * x + ca * y;
  }
}

milo_scene::Xfm source_char_sleeve_make_world(SourceVec3 pos,
                                              SourceVec3 axis_x,
                                              SourceVec3 delta) {
  milo_scene::Xfm out;
  source_set_xfm_pos(out, pos);
  SourceVec3 z = source_vec_scale(delta, -1.0f);
  SourceVec3 y = source_vec_cross(z, axis_x);
  z = source_vec_normalize(z);
  y = source_vec_normalize(y);
  const SourceVec3 x = source_vec_cross(y, z);
  source_set_xfm_row(out, 0, x);
  source_set_xfm_row(out, 1, y);
  source_set_xfm_row(out, 2, z);
  return out;
}

}  // namespace

SourceWaypointState source_waypoint_default_state() {
  return SourceWaypointState{};
}

SourceWaypointRegistryState source_waypoint_init_registry() {
  SourceWaypointRegistryState registry;
  registry.allocated = true;
  registry.registered_functions = {"waypoint_find", "waypoint_nearest",
                                   "waypoint_last"};
  registry.exit_callback_registered = true;
  return registry;
}

void source_waypoint_terminate_registry(SourceWaypointRegistryState& registry) {
  registry.allocated = false;
  registry.registered_functions.clear();
  registry.exit_callback_registered = false;
  registry.waypoints.clear();
}

SourceWaypointConstructorStep source_waypoint_construct(
    SourceWaypointRegistryState& registry) {
  SourceWaypointConstructorStep step;
  step.waypoint = source_waypoint_default_state();
  if (registry.allocated) {
    registry.waypoints.push_back(step.waypoint);
    step.registry_push = true;
    step.registry_size = registry.waypoints.size();
  }
  return step;
}

SourceWaypointFindResult source_waypoint_find_by_flags(
    const SourceWaypointRegistryState& registry,
    int flags_mask) {
  SourceWaypointFindResult result;
  result.mask = flags_mask;
  if (!registry.allocated) return result;
  for (size_t i = 0; i < registry.waypoints.size(); ++i) {
    if ((registry.waypoints[i].flags & flags_mask) != 0) {
      result.index = static_cast<int>(i);
      result.found = true;
      return result;
    }
  }
  return result;
}

SourceWaypointRegisteredCommandDumpEvidence
source_waypoint_registered_command_dump_evidence() {
  SourceWaypointRegisteredCommandDumpEvidence evidence;
  evidence.rb2_ranges = {
      "FindNearest:0x803B4A24->0x803B4B58",
      "OnWaypointNearest:0x803B4BC0->0x803B4C7C",
      "OnWaypointLast:0x803B4C9C->0x803B4D90",
  };
  evidence.rb2_locals = {
      "FindNearest:dist,best,it",
      "OnWaypointLast:w,it",
  };
  return evidence;
}

bool source_waypoint_load_revision_known(int revision) {
  return revision >= 0 && revision <= 5;
}

SourceWaypointLoadPlan source_waypoint_load_plan(int revision) {
  SourceWaypointLoadPlan plan;
  plan.known_revision = source_waypoint_load_revision_known(revision);
  plan.read_order = {"Hmx::Object", "RndTransformable", "mFlags",
                     "mConnections"};
  if (revision < 5) {
    plan.revision_branches.push_back(
        "legacy RndMesh RndDrawable payload before RndTransformable");
  }
  if (revision > 1) {
    plan.read_order.push_back("mRadius");
  } else {
    plan.revision_branches.push_back("default mRadius=12");
  }
  if (revision > 2) {
    plan.read_order.push_back("mYRadius");
    plan.read_order.push_back("mAngRadius");
  }
  if (revision > 3) {
    plan.read_order.push_back("mStrictRadiusDelta");
    plan.read_order.push_back("mStrictAngDelta");
  }
  return plan;
}

SourceWaypointCopyPlan source_waypoint_copy_plan() {
  SourceWaypointCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndTransformable"};
  plan.copied_members = {"mFlags", "mConnections", "mRadius", "mYRadius",
                         "mAngRadius", "mStrictRadiusDelta",
                         "mStrictAngDelta"};
  return plan;
}

SourceWaypointHandlerPlan source_waypoint_handler_plan() {
  SourceWaypointHandlerPlan plan;
  plan.superclasses = {"RndTransformable", "Hmx::Object"};
  plan.check = 524;
  return plan;
}

SourceWaypointPropSyncPlan source_waypoint_prop_sync_plan() {
  SourceWaypointPropSyncPlan plan;
  plan.properties = {"flags", "radius", "y_radius", "strict_radius_delta",
                     "connections"};
  plan.set_properties = {"ang_radius", "strict_ang_delta"};
  plan.superclasses = {"RndTransformable"};
  return plan;
}

SourceWaypointSavePlan source_waypoint_save_plan() {
  return SourceWaypointSavePlan{};
}

std::array<float, 3> source_waypoint_shape_delta_box(
    const milo_scene::Xfm& waypoint_world,
    const std::array<float, 3>& point,
    float radius,
    float y_radius) {
  const SourceVec3 waypoint_pos = source_xfm_pos(waypoint_world);
  const SourceVec3 p = point;
  SourceVec3 delta{};
  if (y_radius > 0.0f) {
    const SourceVec3 from_waypoint = source_vec_sub(p, waypoint_pos);
    const float dot_x =
        source_vec_dot(from_waypoint, source_xfm_row(waypoint_world, 0));
    const float dot_y =
        source_vec_dot(from_waypoint, source_xfm_row(waypoint_world, 1));
    const float clamped_x = std::clamp(dot_x, -radius, radius);
    const float clamped_y = std::clamp(dot_y, -y_radius, y_radius);
    delta = source_vec_scale(source_xfm_row(waypoint_world, 0),
                             clamped_x - dot_x);
    delta = source_vec_add(
        delta,
        source_vec_scale(source_xfm_row(waypoint_world, 1),
                         clamped_y - dot_y));
  } else {
    delta = source_vec_sub(waypoint_pos, p);
    delta[2] = 0.0f;
    const float len_sq = source_vec_dot(delta, delta);
    if (len_sq <= radius * radius) {
      delta = {0.0f, 0.0f, 0.0f};
    } else {
      delta = source_vec_scale(delta, 1.0f - (radius / std::sqrt(len_sq)));
    }
  }
  return delta;
}

float source_waypoint_shape_delta_ang(float waypoint_z_angle,
                                      float radius,
                                      float subject_z_angle) {
  const float limited = source_limit_ang(waypoint_z_angle - subject_z_angle);
  const float clamped = std::clamp(limited, -radius, radius);
  return limited - clamped;
}

SourceWaypointConstrainResult source_waypoint_constrain(
    const SourceWaypointState& waypoint,
    const milo_scene::Xfm& waypoint_world,
    const milo_scene::Xfm& subject) {
  SourceWaypointConstrainResult result;
  result.constrained = subject;

  if (waypoint.strict_radius_delta > 0.0f) {
    float y_radius = 0.0f;
    if (waypoint.y_radius > 0.0f) {
      y_radius = waypoint.y_radius + waypoint.strict_radius_delta;
    }
    result.position_delta = source_waypoint_shape_delta_box(
        waypoint_world, source_xfm_pos(subject),
        waypoint.radius + waypoint.strict_radius_delta, y_radius);
    result.constrained.pos[0] += result.position_delta[0];
    result.constrained.pos[1] += result.position_delta[1];
    result.constrained.pos[2] += result.position_delta[2];
    result.applied_radius = true;
  }

  if (waypoint.strict_ang_delta > 0.0f) {
    result.angle_delta = source_waypoint_shape_delta_ang(
        source_xfm_z_angle(waypoint_world),
        waypoint.ang_radius + waypoint.strict_ang_delta,
        source_xfm_z_angle(subject));
    source_rotate_about_z(result.constrained, result.angle_delta);
    result.applied_angle = true;
  }

  return result;
}

SourceCharSleeveState source_char_sleeve_default_state() {
  return SourceCharSleeveState{};
}

SourceCharSleeveSetNameStep source_char_sleeve_set_name_step(
    bool dir_is_character) {
  SourceCharSleeveSetNameStep step;
  step.assigns_character_owner = dir_is_character;
  return step;
}

SourceCharMeshCacheState source_char_mesh_cache_default_state() {
  return SourceCharMeshCacheState{};
}

SourceCharMeshCacheDisableResult source_char_mesh_cache_disable(
    SourceCharMeshCacheState& state,
    bool disabled) {
  SourceCharMeshCacheDisableResult result;
  if (!state.cache.empty()) {
    result.asserted_non_empty_cache = true;
    return result;
  }
  state.disabled = disabled;
  result.accepted = true;
  return result;
}

bool source_char_mesh_cache_has_mesh(
    const SourceCharMeshCacheState& state,
    const std::string& mesh) {
  for (const SourceCharMeshCacher& cacher : state.cache) {
    if (mesh == cacher.mesh) return true;
  }
  return false;
}

SourceCharMeshCacheVertsResult source_char_mesh_cache_get_verts(
    const SourceCharMeshCacheState& state,
    const std::string& mesh) {
  SourceCharMeshCacheVertsResult result;
  for (const SourceCharMeshCacher& cacher : state.cache) {
    if (mesh == cacher.mesh) {
      result.found = true;
      result.verts = cacher.verts;
      return result;
    }
  }
  return result;
}

SourceCharMeshCacheSyncResult source_char_mesh_cache_sync_mesh(
    SourceCharMeshCacheState& state,
    const std::string& mesh,
    int32_t mask) {
  SourceCharMeshCacheSyncResult result;
  result.mask = mask;
  size_t idx = 0;
  for (size_t i = 0; i < state.cache.size(); ++i) {
    if (state.cache[idx++].mesh == mesh) break;
  }
  result.index_after_scan = idx;
  if (idx == state.cache.size()) {
    if (mesh.empty()) {
      result.asserted_null_mesh = true;
      return result;
    }
    SourceCharMeshCacher cacher;
    cacher.mesh = mesh;
    cacher.unk4 = 0;
    cacher.disabled = state.disabled;
    state.cache.push_back(cacher);
    result.added = true;
  }
  result.inline_cacher_body_visible = false;
  return result;
}

std::vector<std::string> source_char_mesh_cache_stuff_meshes(
    const SourceCharMeshCacheState& state) {
  std::vector<std::string> meshes;
  meshes.reserve(state.cache.size());
  for (const SourceCharMeshCacher& cacher : state.cache) {
    meshes.push_back(cacher.mesh);
  }
  return meshes;
}

SourceCharGuitarStringPollResult source_char_guitar_string_poll(
    bool has_nut,
    bool has_bridge,
    bool has_bend,
    bool has_target,
    bool open,
    const std::array<float, 3>& nut_pos,
    const std::array<float, 3>& bridge_pos,
    const std::array<float, 3>& bend_pos,
    const std::array<float, 3>& target_pos) {
  SourceCharGuitarStringPollResult result;
  result.bend_pos = bend_pos;
  if (!has_nut || !has_bridge || !has_bend || !has_target) return result;

  const SourceVec3 tmp = source_vec_sub(target_pos, nut_pos);
  const SourceVec3 tmp2 = source_vec_sub(bridge_pos, nut_pos);
  float clamped =
      std::clamp(source_vec_dot(tmp, tmp2) / source_vec_dot(tmp2, tmp2),
                 0.0f, 1.0f);
  if (open) clamped = 0.0f;
  result.bend_pos =
      source_vec_add(source_vec_scale(nut_pos, 1.0f - clamped),
                     source_vec_scale(bridge_pos, clamped));
  result.wrote_bend = true;
  return result;
}

void source_char_guitar_string_poll_deps(
    SourceCharGuitarStringPollDeps& deps,
    const std::string& nut,
    const std::string& bridge,
    const std::string& target,
    const std::string& bend) {
  deps.changed_by.push_back(nut);
  deps.changed_by.push_back(bridge);
  deps.changed_by.push_back(target);
  deps.change.push_back(bend);
}

SourceCharGuitarStringDefaultState source_char_guitar_string_default_state() {
  return SourceCharGuitarStringDefaultState{};
}

SourceCharGuitarStringLoadPlan source_char_guitar_string_load_plan(
    int revision) {
  SourceCharGuitarStringLoadPlan plan;
  plan.known_revision = revision == 0;
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "mNut", "mBridge", "mBend", "mTarget"};
  return plan;
}

SourceCharGuitarStringCopyPlan source_char_guitar_string_copy_plan() {
  SourceCharGuitarStringCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mTarget", "mNut", "mBridge", "mBend"};
  return plan;
}

SourceCharGuitarStringHandlerPlan source_char_guitar_string_handler_plan() {
  SourceCharGuitarStringHandlerPlan plan;
  plan.actions = {"set_open:mOpen=_msg->Int(2)!=0"};
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x70;
  return plan;
}

SourceCharGuitarStringPropSyncPlan
source_char_guitar_string_prop_sync_plan() {
  SourceCharGuitarStringPropSyncPlan plan;
  plan.properties = {"nut", "bridge", "bend", "target"};
  return plan;
}

SourceCharGuitarStringSavePlan source_char_guitar_string_save_plan() {
  return SourceCharGuitarStringSavePlan{};
}

std::vector<std::string> source_char_eyes_list_poll_children(
    const std::vector<std::string>& eye_lookats) {
  std::vector<std::string> children;
  for (const std::string& eye : eye_lookats) children.push_back(eye);
  return children;
}

bool source_char_eyes_either_eye_clamped(
    const std::vector<SourceCharEyesClampRow>& eyes) {
  for (const SourceCharEyesClampRow& eye : eyes) {
    if (eye.has_eye && eye.clamped) return true;
  }
  return false;
}

SourceCharEyesEyeDescLoadPlan source_char_eyes_eye_desc_load_plan(
    int32_t revision) {
  SourceCharEyesEyeDescLoadPlan plan;
  plan.read_order = {"mEye", "mUpperLid"};
  if (revision > 6) plan.read_order.push_back("mLowerLid");
  if (revision > 0xF) {
    plan.read_order.push_back("mUpperLidBlink");
    plan.read_order.push_back("mLowerLidBlink");
  }
  return plan;
}

SourceCharEyesLoadPlan source_char_eyes_load_plan(int32_t revision) {
  SourceCharEyesLoadPlan plan;
  plan.revision_supported = revision >= 0 && revision <= 0x12;
  if (!plan.revision_supported) return plan;

  plan.read_order.push_back("Hmx::Object");
  if (revision > 5) plan.read_order.push_back("CharWeightable");
  if (revision > 4) {
    plan.read_order.push_back("mEyes");
  } else {
    plan.read_order.push_back("legacyLookAtList");
    plan.branches.push_back("legacy lookats become EyeDesc with lid refs null");
  }
  if (revision >= 3 && revision <= 4) {
    plan.read_order.push_back("legacyTransformPtr");
  }
  plan.branches.push_back("mInterests.clear");
  if (revision >= 4 && revision <= 8) {
    plan.read_order.push_back("legacyInterestTransformCount");
    plan.read_order.push_back("legacyInterestTransformRows");
  } else if (revision > 8) {
    plan.read_order.push_back("mInterests");
  }
  if (revision > 4) {
    plan.read_order.push_back("mFaceServo");
  } else {
    plan.branches.push_back("mFaceServo=0");
  }
  if (revision > 7) plan.read_order.push_back("mCamWeight");
  if (revision > 9) plan.read_order.push_back("mDefaultFilterFlags");
  if (revision > 10) plan.read_order.push_back("mViewDirection");
  if (revision > 0xB) plan.read_order.push_back("mHeadLookAt");
  if (revision > 0xC) plan.read_order.push_back("mMaxExtrapolation");
  if (revision > 0xD) plan.read_order.push_back("mMinTargetDist");
  if (revision > 0xE) {
    plan.read_order.push_back("mUpperLidTrackUp");
    plan.read_order.push_back("mUpperLidTrackDown");
    plan.read_order.push_back("mLowerLidTrackUp");
    if (revision < 0x11) {
      plan.read_order.push_back("legacyLowerLidTrackDownPad0");
      plan.read_order.push_back("mLowerLidTrackDown");
      plan.read_order.push_back("legacyLowerLidTrackDownPad1");
    } else {
      plan.read_order.push_back("mLowerLidTrackDown");
    }
  }
  if (revision > 0x11) plan.read_order.push_back("mLowerLidTrackRotate");
  return plan;
}

SourceCharEyesCopyPlan source_char_eyes_copy_plan() {
  SourceCharEyesCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "CharWeightable"};
  plan.copied_members = {"mEyes",
                         "mInterests",
                         "mFaceServo",
                         "unka4",
                         "unkb4",
                         "mCamWeight",
                         "mDefaultFilterFlags",
                         "mViewDirection",
                         "mHeadLookAt",
                         "mMaxExtrapolation",
                         "mMinTargetDist",
                         "mUpperLidTrackUp",
                         "mUpperLidTrackDown",
                         "mLowerLidTrackUp",
                         "mLowerLidTrackDown",
                         "mLowerLidTrackRotate"};
  return plan;
}

SourceCharEyesHandlerPlan source_char_eyes_handler_plan() {
  SourceCharEyesHandlerPlan plan;
  plan.handlers = {"add_interest"};
  plan.action_handlers = {"force_blink"};
  plan.debug_handlers = {"toggle_force_focus", "toggle_interest_overlay"};
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x660;
  return plan;
}

SourceCharEyesPropSyncPlan source_char_eyes_prop_sync_plan() {
  SourceCharEyesPropSyncPlan plan;
  plan.eye_desc_properties = {"eye", "upper_lid", "lower_lid",
                              "upper_lid_blink", "lower_lid_blink"};
  plan.interest_state_properties = {"interest"};
  plan.properties = {"eyes",
                     "view_direction",
                     "interests",
                     "face_servo",
                     "camera_weight",
                     "head_lookat",
                     "max_extrapolation",
                     "min_target_dist",
                     "ulid_track_up",
                     "ulid_track_down",
                     "llid_track_up",
                     "llid_track_down",
                     "llid_track_rotate"};
  plan.bitfield_properties = {"default_interest_categories"};
  plan.debug_properties = {"disable_eye_dart",
                           "disable_eye_jitter",
                           "disable_interest_objects",
                           "disable_procedural_blink",
                           "disable_eye_clamping",
                           "interest_filter_testing"};
  plan.superclasses = {"CharWeightable"};
  return plan;
}

SourceCharEyesSavePlan source_char_eyes_save_plan() {
  return SourceCharEyesSavePlan{};
}

SourceCharEyesBitfieldPropResult
source_char_eyes_default_interest_categories_sync(
    int current_flags,
    int bit_mask,
    bool get_operation,
    bool requested_enabled) {
  SourceCharEyesBitfieldPropResult result;
  result.flags = current_flags;
  if (get_operation) {
    result.get_value = (current_flags & bit_mask) != 0;
    return result;
  }
  if (requested_enabled) {
    result.flags = current_flags | bit_mask;
  } else {
    result.flags = current_flags & ~bit_mask;
  }
  result.get_value = (result.flags & bit_mask) != 0;
  return result;
}

SourceCharEyesFilterFlagsResult source_char_eyes_set_interest_filter_flags(
    int requested_flags) {
  SourceCharEyesFilterFlagsResult result;
  result.flags = requested_flags;
  result.marked_changed = true;
  return result;
}

SourceCharEyesFilterFlagsResult source_char_eyes_clear_interest_filter_flags(
    int default_flags) {
  SourceCharEyesFilterFlagsResult result;
  result.flags = default_flags;
  return result;
}

SourceCharEyesDefaultState source_char_eyes_default_state() {
  SourceCharEyesDefaultState state;
  state.unkb8 = std::cos(0.52359879f);
  state.overlay_name = "eye_status";
  return state;
}

SourceCharEyesDefaultState source_char_eyes_copy_state(
    const SourceCharEyesDefaultState& source) {
  SourceCharEyesDefaultState dest = source_char_eyes_default_state();
  dest.eye_count = source.eye_count;
  dest.interest_count = source.interest_count;
  dest.has_face_servo = source.has_face_servo;
  dest.unka4 = source.unka4;
  dest.unkb4 = source.unkb4;
  dest.has_cam_weight = source.has_cam_weight;
  dest.default_filter_flags = source.default_filter_flags;
  dest.has_view_direction = source.has_view_direction;
  dest.has_head_lookat = source.has_head_lookat;
  dest.max_extrapolation = source.max_extrapolation;
  dest.min_target_dist = source.min_target_dist;
  dest.upper_lid_track_up = source.upper_lid_track_up;
  dest.upper_lid_track_down = source.upper_lid_track_down;
  dest.lower_lid_track_up = source.lower_lid_track_up;
  dest.lower_lid_track_down = source.lower_lid_track_down;
  dest.lower_lid_track_rotate = source.lower_lid_track_rotate;
  return dest;
}

SourceCharEyesEyeDesc source_char_eyes_eye_desc_default() {
  return SourceCharEyesEyeDesc{};
}

SourceCharEyesEyeDesc source_char_eyes_eye_desc_copy(
    const SourceCharEyesEyeDesc& source) {
  SourceCharEyesEyeDesc desc;
  desc.eye = source.eye;
  desc.upper_lid = source.upper_lid;
  desc.lower_lid = source.lower_lid;
  desc.lower_lid_blink = source.lower_lid_blink;
  desc.upper_lid_blink = source.upper_lid_blink;
  return desc;
}

void source_char_eyes_eye_desc_assign(
    SourceCharEyesEyeDesc& dest,
    const SourceCharEyesEyeDesc& source) {
  dest.eye = source.eye;
  dest.upper_lid = source.upper_lid;
  dest.lower_lid = source.lower_lid;
  dest.upper_lid_blink = source.upper_lid_blink;
  dest.lower_lid_blink = source.lower_lid_blink;
}

std::string source_char_eyes_get_head(
    const std::string& view_direction,
    const std::string& first_eye_source_parent) {
  if (!view_direction.empty()) return view_direction;
  if (!first_eye_source_parent.empty()) return first_eye_source_parent;
  return {};
}

std::string source_char_eyes_current_interest(
    const std::string& focus_interest,
    const std::string& current_interest) {
  if (!focus_interest.empty()) return focus_interest;
  if (!current_interest.empty()) return current_interest;
  return {};
}

SourceCharEyesFocusResult source_char_eyes_set_focus_interest(
    const std::string& current_focus,
    int current_priority,
    const std::string& requested_interest,
    int requested_priority) {
  SourceCharEyesFocusResult result;
  result.focus_interest = current_focus;
  result.focus_priority = current_focus.empty() ? -1 : current_priority;
  if (!current_focus.empty() && current_priority > requested_priority) {
    return result;
  }
  result.accepted = true;
  result.focus_interest = requested_interest;
  result.focus_priority = requested_interest.empty() ? -1 : requested_priority;
  return result;
}

SourceCharEyesFocusResult source_char_eyes_toggle_force_focus(
    const std::string& current_focus,
    int current_priority,
    const std::string& current_interest) {
  if (!current_focus.empty()) {
    return source_char_eyes_set_focus_interest(current_focus, current_priority,
                                               "", 0);
  }
  return source_char_eyes_set_focus_interest(current_focus, current_priority,
                                             current_interest, 0);
}

SourceCharEyesOverlayToggleResult source_char_eyes_toggle_interest_overlay(
    bool has_overlay,
    bool current_showing) {
  SourceCharEyesOverlayToggleResult result;
  result.has_overlay = has_overlay;
  result.showing = current_showing;
  if (!has_overlay) return result;
  result.showing = !current_showing;
  result.timer_restarted = true;
  return result;
}

SourceCharEyesForceBlinkState source_char_eyes_force_blink(
    float task_seconds) {
  SourceCharEyesForceBlinkState state;
  state.pending_blink = true;
  state.blink_time = task_seconds;
  state.blink_count_delta = 1;
  return state;
}

SourceCharEyesEnterState source_char_eyes_enter_state(
    int default_filter_flags,
    bool has_head,
    const std::array<float, 3>& head_world_y,
    size_t eye_count,
    size_t interest_count) {
  SourceCharEyesEnterState state;
  state.interest_filter_flags = default_filter_flags;
  state.eye_enter_count = eye_count;
  state.interest_reset_count = interest_count;
  if (has_head) {
    const float len_sq = head_world_y[0] * head_world_y[0] +
                         head_world_y[1] * head_world_y[1] +
                         head_world_y[2] * head_world_y[2];
    if (len_sq > 0.0f) {
      const float inv_len = 1.0f / std::sqrt(len_sq);
      state.unka4 = {head_world_y[0] * inv_len,
                     head_world_y[1] * inv_len,
                     head_world_y[2] * inv_len};
    }
  }
  return state;
}

SourceCharEyesExitState source_char_eyes_exit_state(size_t eye_count) {
  SourceCharEyesExitState state;
  state.focus_interest = {};
  state.focus_priority = -1;
  state.clear_interests = true;
  state.eye_exit_count = eye_count;
  state.pollable_exit = true;
  return state;
}

SourceCharEyesInterestRuntime source_char_eyes_interest_state(
    const std::string& interest) {
  SourceCharEyesInterestRuntime state;
  state.interest = interest;
  state.refractory_start = -1.0f;
  return state;
}

void source_char_eyes_interest_reset(
    SourceCharEyesInterestRuntime& state) {
  state.refractory_start = -1.0f;
}

void source_char_eyes_interest_begin_refractory(
    SourceCharEyesInterestRuntime& state,
    float task_seconds) {
  state.refractory_start = task_seconds;
}

bool source_char_eyes_interest_in_refractory(
    const SourceCharEyesInterestRuntime& state,
    float task_seconds,
    float refractory_period) {
  if (state.interest.empty() || state.refractory_start < 0.0f) return false;
  return task_seconds - state.refractory_start < refractory_period;
}

float source_char_eyes_interest_refractory_remaining(
    const SourceCharEyesInterestRuntime& state,
    float task_seconds,
    float refractory_period) {
  if (state.interest.empty() || state.refractory_start < 0.0f) return 0.0f;
  const float elapsed = task_seconds - state.refractory_start;
  if (elapsed < refractory_period) return refractory_period - elapsed;
  return 0.0f;
}

void source_char_eyes_clear_interest_objects(
    std::vector<SourceCharEyesInterestRuntime>& interests) {
  interests.clear();
}

bool source_char_eyes_add_interest_object(
    std::vector<SourceCharEyesInterestRuntime>& interests,
    const std::string& interest) {
  if (interest.empty()) return false;
  interests.push_back(source_char_eyes_interest_state(interest));
  return true;
}

void source_char_eyes_poll_deps(
    SourceCharEyesPollDeps& deps,
    const std::vector<SourceCharEyesInterest>& interests,
    bool has_eyes,
    const std::string& head,
    const std::string& target,
    const std::string& head_lookat,
    const std::string& face_servo) {
  for (const SourceCharEyesInterest& interest : interests) {
    if (interest.same_dir) deps.changed_by.push_back(interest.interest);
  }
  if (has_eyes) {
    deps.changed_by.push_back(head);
    deps.change.push_back(target);
  }
  if (!head_lookat.empty()) deps.changed_by.push_back(head_lookat);
  if (!face_servo.empty()) deps.changed_by.push_back(face_servo);
}

SourceCharEyesRuntimeDumpEvidence
source_char_eyes_runtime_dump_evidence() {
  SourceCharEyesRuntimeDumpEvidence evidence;
  evidence.poll_range = "0x80354D64->0x80355480";
  evidence.next_look_range = "0x8035559C->0x80355A74";
  evidence.replace_range = "0x80355A74->0x80355DCC";
  evidence.list_poll_children_range = "0x80355DCC->0x80355E84";
  evidence.poll_deps_range = "0x80355E84->0x80356030";
  evidence.gh2_xex_char_lookat_poll_range = "0x8216E4E8->0x8216EA40";
  evidence.gh2_xex_char_eyes_next_look_range =
      "0x82170130->0x821704B0";
  evidence.gh2_xex_char_eyes_poll_range = "0x82170BA8->0x82170E10";
  evidence.poll_locals = {"h",       "camWeight", "blinkWeight", "blink",
                          "delta",   "cang",      "sec",         "d",
                          "dest",    "weight",    "srcCam",      "t",
                          "t",       "it",        "height"};
  evidence.next_look_locals = {"facing", "delta", "d",    "tanang",
                               "h",      "scale", "delta", "it",
                               "b",      "dist",  "c",     "d2"};
  evidence.rb2_dump_has_statement_body = false;
  evidence.latest_source_has_poll_body = false;
  evidence.gh2_xex_owns_generated_target = true;
  evidence.gh2_xex_interest_can_replace_target = true;
  evidence.gh2_xex_assigns_target_to_every_lookat = true;
  evidence.safe_to_publish_destination_links = true;
  evidence.safe_to_publish_stock_v2_lookat_local = true;
  evidence.safe_to_publish_eye_runtime_rows = false;
  evidence.safe_to_infer_facefx_rows = false;
  return evidence;
}

SourceGh2CharEyesNextLookPublication
source_gh2_char_eyes_next_look_publication(
    const std::vector<std::string>& lookats,
    const std::string& generated_target,
    const std::string& qualifying_interest) {
  SourceGh2CharEyesNextLookPublication publication;
  publication.generated_target = generated_target;
  publication.generated_target_local_written = true;
  publication.used_interest = !qualifying_interest.empty();
  publication.chosen_target = publication.used_interest
                                  ? qualifying_interest
                                  : generated_target;
  publication.lookats = lookats;
  publication.destination_targets.assign(lookats.size(),
                                         publication.chosen_target);
  publication.reset_last_look = true;
  publication.reset_average_delta = true;
  publication.reset_last_cang = true;
  return publication;
}

SourceGh2CharEyesGeneratedTargetResult
source_gh2_char_eyes_generated_target(
    const std::array<float, 3>& current_facing,
    const std::array<float, 3>& last_facing,
    const std::array<float, 3>& source_world_position,
    float random_unit,
    bool object_dir_is_transformable,
    float object_dir_world_z) {
  SourceGh2CharEyesGeneratedTargetResult result;
  constexpr float kFacingGain = 45.0f;
  constexpr double kFacingLimitRadians = 0.45814892509952188;
  constexpr float kMinDistance = 20.0f;
  constexpr float kMaxDistance = 100.0f;
  constexpr float kDistanceScale = 12.0f;

  result.facing_delta_limit =
      static_cast<float>(std::tan(kFacingLimitRadians));
  float facing_delta_length_sq = 0.0f;
  for (size_t axis = 0; axis < 3; ++axis) {
    result.facing_delta[axis] =
        (current_facing[axis] - last_facing[axis]) * kFacingGain;
    facing_delta_length_sq +=
        result.facing_delta[axis] * result.facing_delta[axis];
  }
  const float facing_delta_length = std::sqrt(facing_delta_length_sq);
  if (facing_delta_length > result.facing_delta_limit) {
    const float scale = result.facing_delta_limit / facing_delta_length;
    for (float& value : result.facing_delta) value *= scale;
    result.facing_delta_clamped = true;
  }

  result.random_distance =
      (kMinDistance + (kMaxDistance - kMinDistance) * random_unit) *
      kDistanceScale;
  for (size_t axis = 0; axis < 3; ++axis) {
    result.projected_facing[axis] =
        current_facing[axis] + result.facing_delta[axis];
    result.target[axis] = source_world_position[axis] +
                          result.projected_facing[axis] *
                              result.random_distance;
  }

  if (object_dir_is_transformable && result.target[2] < object_dir_world_z) {
    result.floor_scale =
        (object_dir_world_z - source_world_position[2]) /
        (result.target[2] - source_world_position[2]);
    for (size_t axis = 0; axis < 3; ++axis) {
      result.target[axis] =
          source_world_position[axis] +
          (result.target[axis] - source_world_position[axis]) *
              result.floor_scale;
    }
    result.floor_clamped = true;
  }
  return result;
}

void source_gh2_random_seed(SourceGh2RandomState& state, uint32_t seed) {
  constexpr uint32_t kMultiplier = 1103515245u;
  constexpr uint32_t kAddend = 12345u;
  for (uint32_t& word : state.words) {
    seed = seed * kMultiplier + kAddend;
    const uint32_t low = (seed >> 16u) & 0xffffu;
    seed = seed * kMultiplier + kAddend;
    const uint32_t high = (seed >> 16u) & 0x7fffu;
    word = low | (high << 16u);
  }
  state.first_index = 0;
  state.second_index = 103;
}

uint32_t source_gh2_random_u32(SourceGh2RandomState& state) {
  constexpr uint32_t kActiveWords = 250;
  state.words[state.first_index] ^= state.words[state.second_index];
  const uint32_t value = state.words[state.first_index];
  if (++state.first_index == kActiveWords) state.first_index = 0;
  if (++state.second_index == kActiveWords) state.second_index = 0;
  return value;
}

float source_gh2_random_unit(SourceGh2RandomState& state) {
  constexpr float kLow16Scale = 1.0f / 65536.0f;
  return static_cast<float>(source_gh2_random_u32(state) & 0xffffu) *
         kLow16Scale;
}

SourceGh2CharEyesPollState source_gh2_char_eyes_enter(
    const std::array<float, 3>& first_eye_world_y,
    bool has_first_eye) {
  SourceGh2CharEyesPollState state;
  if (has_first_eye) state.last_facing = first_eye_world_y;
  state.last_cang = 1.0f;
  return state;
}

SourceGh2CharEyesPollResult source_gh2_char_eyes_poll(
    SourceGh2CharEyesPollState& state,
    const std::array<float, 3>& first_eye_world_y,
    const std::array<float, 3>& first_eye_world_position,
    const std::array<float, 3>& target_world_position,
    float delta_seconds,
    bool has_blink_weight,
    float blink_weight,
    float random_unit) {
  SourceGh2CharEyesPollResult result;
  constexpr float kBlinkWeightThreshold = -0.1f;
  constexpr float kBlinkChance = 0.33f;
  constexpr float kLookTimeoutSeconds = 8.0f;
  constexpr float kMaximumEyeAngleCos = 0.81915204700314759f;
  constexpr float kAverageDeltaCoefficient = 0.1f;
  constexpr float kUnsetCang = 1.0e30f;

  result.previous_facing = state.last_facing;
  state.seconds_since_look += delta_seconds;

  if (has_blink_weight) {
    result.blink_delta = blink_weight - state.previous_blink_weight;
    result.blink_trigger = blink_weight < kBlinkWeightThreshold &&
                           result.blink_delta > 0.0f &&
                           state.previous_blink_delta <= 0.0f &&
                           random_unit < kBlinkChance;
    state.previous_blink_weight = blink_weight;
    state.previous_blink_delta = result.blink_delta;
  }

  std::array<float, 3> target_dir = {};
  float target_length_sq = 0.0f;
  for (size_t axis = 0; axis < 3; ++axis) {
    target_dir[axis] =
        target_world_position[axis] - first_eye_world_position[axis];
    target_length_sq += target_dir[axis] * target_dir[axis];
  }
  if (target_length_sq > 0.0f) {
    const float inv_length = 1.0f / std::sqrt(target_length_sq);
    for (float& value : target_dir) value *= inv_length;
  }
  result.cang = std::clamp(first_eye_world_y[0] * target_dir[0] +
                               first_eye_world_y[1] * target_dir[1] +
                               first_eye_world_y[2] * target_dir[2],
                           -1.0f, 1.0f);

  if (state.last_cang != kUnsetCang) {
    const float delta = result.cang - state.last_cang;
    state.average_delta +=
        (delta - state.average_delta) * kAverageDeltaCoefficient;
  }

  result.timeout_trigger = state.seconds_since_look > kLookTimeoutSeconds;
  result.facing_trigger = state.seconds_since_look > 0.0f &&
                          result.cang > kMaximumEyeAngleCos &&
                          state.average_delta > 0.0f;
  result.called_next_look =
      result.timeout_trigger ||
      (state.seconds_since_look > 0.0f && result.blink_trigger) ||
      result.facing_trigger;
  if (result.called_next_look) {
    state.seconds_since_look = 0.0f;
    state.average_delta = 0.0f;
    state.last_cang = kUnsetCang;
  }

  // CharEyes::Poll stores these after NextLook returns, including on a frame
  // where NextLook temporarily wrote the sentinel above.
  state.last_cang = result.cang;
  state.last_facing = first_eye_world_y;
  return result;
}

SourceCharEyeDartRulesetData source_char_eye_dart_ruleset_defaults() {
  return SourceCharEyeDartRulesetData{};
}

bool source_char_eye_dart_ruleset_load_revision_known(int revision) {
  return revision >= 0 && revision <= 1;
}

SourceCharEyeDartRulesetLoadPlan source_char_eye_dart_ruleset_load_plan(
    int revision) {
  SourceCharEyeDartRulesetLoadPlan plan;
  plan.known_revision =
      source_char_eye_dart_ruleset_load_revision_known(revision);
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object",
                     "mData.mMinRadius",
                     "mData.mMaxRadius",
                     "mData.mOnTargetAngleThresh",
                     "mData.mMinDartsPerSequence",
                     "mData.mMaxDartsPerSequence",
                     "mData.mMinSecsBetweenDarts",
                     "mData.mMaxSecsBetweenDarts",
                     "mData.mMinSecsBetweenSequences",
                     "mData.mMaxSecsBetweenSequences",
                     "mData.mScaleWithDistance",
                     "mData.mReferenceDistance"};
  return plan;
}

SourceCharEyeDartRulesetData source_char_eye_dart_ruleset_copy(
    const SourceCharEyeDartRulesetData& src) {
  SourceCharEyeDartRulesetData dst;
  dst.min_radius = src.min_radius;
  dst.max_radius = src.min_radius;
  dst.on_target_angle_thresh = src.on_target_angle_thresh;
  dst.min_darts_per_sequence = src.min_darts_per_sequence;
  dst.max_darts_per_sequence = src.max_darts_per_sequence;
  dst.min_secs_between_darts = src.min_secs_between_darts;
  dst.max_secs_between_darts = src.max_secs_between_darts;
  dst.min_secs_between_sequences = src.min_secs_between_sequences;
  dst.max_secs_between_sequences = src.max_secs_between_sequences;
  dst.scale_with_distance = src.scale_with_distance;
  dst.reference_distance = src.reference_distance;
  return dst;
}

SourceCharEyeDartRulesetCopyPlan source_char_eye_dart_ruleset_copy_plan() {
  SourceCharEyeDartRulesetCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mData.mMinRadius",
                         "mData.mOnTargetAngleThresh",
                         "mData.mMinDartsPerSequence",
                         "mData.mMaxDartsPerSequence",
                         "mData.mMinSecsBetweenDarts",
                         "mData.mMaxSecsBetweenDarts",
                         "mData.mMinSecsBetweenSequences",
                         "mData.mMaxSecsBetweenSequences",
                         "mData.mScaleWithDistance",
                         "mData.mReferenceDistance"};
  return plan;
}

SourceCharEyeDartRulesetPropSyncPlan
source_char_eye_dart_ruleset_prop_sync_plan() {
  SourceCharEyeDartRulesetPropSyncPlan plan;
  plan.properties = {"min_radius",
                     "max_radius",
                     "on_target_angle_thresh",
                     "min_darts_per_sequence",
                     "max_darts_per_sequence",
                     "min_secs_between_darts",
                     "max_secs_between_darts",
                     "min_secs_between_sequences",
                     "max_secs_between_sequences",
                     "scale_with_distance",
                     "reference_distance"};
  return plan;
}

SourceCharEyeDartRulesetHandlerPlan
source_char_eye_dart_ruleset_handler_plan() {
  SourceCharEyeDartRulesetHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0xd4;
  return plan;
}

SourceCharEyeDartRulesetSavePlan
source_char_eye_dart_ruleset_save_plan() {
  return SourceCharEyeDartRulesetSavePlan{};
}

float source_char_interest_sync_max_view_angle(float max_view_angle_degrees) {
  return std::cos(max_view_angle_degrees * 0.017453292f);
}

SourceCharInterestState source_char_interest_defaults() {
  SourceCharInterestState state;
  state.max_view_angle_cos =
      source_char_interest_sync_max_view_angle(state.max_view_angle);
  return state;
}

bool source_char_interest_load_revision_known(int revision) {
  return revision >= 0 && revision <= 6;
}

SourceCharInterestLoadPlan source_char_interest_load_plan(int revision) {
  SourceCharInterestLoadPlan plan;
  plan.known_revision = source_char_interest_load_revision_known(revision);
  if (!plan.known_revision) return plan;

  plan.read_order = {"Hmx::Object",       "RndTransformable",
                     "mMaxViewAngle",     "mPriority",
                     "mMinLookTime",      "mMaxLookTime",
                     "mRefractoryPeriod"};

  const unsigned int temp = static_cast<unsigned int>(revision) + 0x10000u;
  const unsigned int temp_minus_two_16 = (temp - 2u) & 0xffffu;
  if (temp_minus_two_16 <= 3u) {
    plan.read_order.push_back("legacyObjectPtr");
    plan.branches.push_back("u16(temp - 2) <= 3");
  } else if (temp > 5u) {
    plan.read_order.push_back("mDartOverride");
    plan.branches.push_back("temp > 5");
  }

  if (revision > 2) {
    plan.read_order.push_back("mCategoryFlags");
    plan.branches.push_back("gRev > 2");
    if (revision == 3) {
      plan.read_order.push_back("legacyCategoryFlagsByte");
      plan.branches.push_back("gRev == 3");
    }
  }

  if (revision > 4) {
    plan.read_order.push_back("mOverrideMinTargetDistance");
    plan.read_order.push_back("mMinTargetDistanceOverride");
    plan.branches.push_back("gRev > 4");
  }

  plan.sync_max_view_angle = true;
  return plan;
}

bool source_char_interest_is_matching_filter_flags(int category_flags,
                                                   int mask) {
  return (category_flags & mask) != 0 && category_flags != 0;
}

bool source_char_interest_is_within_view_cone(
    const std::array<float, 3>& interest_world,
    const std::array<float, 3>& viewer_world,
    const std::array<float, 3>& view_direction,
    float max_view_angle_cos) {
  const float dx = interest_world[0] - viewer_world[0];
  const float dy = interest_world[1] - viewer_world[1];
  const float dz = interest_world[2] - viewer_world[2];
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len <= 0.0f) return false;
  const float dot =
      view_direction[0] * (dx / len) + view_direction[1] * (dy / len) +
      view_direction[2] * (dz / len);
  return dot >= max_view_angle_cos;
}

SourceCharInterestState source_char_interest_copy(
    const SourceCharInterestState& src) {
  SourceCharInterestState dst;
  dst.max_view_angle = src.max_view_angle;
  dst.priority = src.priority;
  dst.min_look_time = src.min_look_time;
  dst.max_look_time = src.max_look_time;
  dst.refractory_period = src.refractory_period;
  dst.dart_override = src.dart_override;
  dst.category_flags = src.category_flags;
  dst.override_min_target_distance = src.override_min_target_distance;
  dst.min_target_distance_override = src.min_target_distance_override;
  dst.max_view_angle_cos =
      source_char_interest_sync_max_view_angle(dst.max_view_angle);
  return dst;
}

SourceCharInterestCopyPlan source_char_interest_copy_plan() {
  SourceCharInterestCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "RndTransformable"};
  plan.copied_members = {"mMaxViewAngle",
                         "mPriority",
                         "mMinLookTime",
                         "mMaxLookTime",
                         "mRefractoryPeriod",
                         "mDartOverride",
                         "mCategoryFlags",
                         "mOverrideMinTargetDistance",
                         "mMinTargetDistanceOverride"};
  return plan;
}

SourceCharInterestPropSyncPlan source_char_interest_prop_sync_plan() {
  SourceCharInterestPropSyncPlan plan;
  plan.modify_properties = {"max_view_angle"};
  plan.modify_actions = {"SyncMaxViewAngle"};
  plan.properties = {"priority",
                     "min_look_time",
                     "max_look_time",
                     "refractory_period",
                     "dart_ruleset_override",
                     "overrides_min_target_dist",
                     "min_target_dist_override"};
  plan.custom_branches = {"category_flags"};
  plan.superclasses = {"RndTransformable"};
  return plan;
}

SourceCharInterestCategoryFlagsPropPlan
source_char_interest_category_flags_prop_plan() {
  SourceCharInterestCategoryFlagsPropPlan plan;
  plan.operations = {"raw PropSync when no bit operand",
                     "kPropGet returns mCategoryFlags & flags",
                     "nonzero set ORs mask",
                     "zero set clears mask"};
  return plan;
}

SourceCharInterestHandlerPlan source_char_interest_handler_plan() {
  SourceCharInterestHandlerPlan plan;
  plan.superclasses = {"RndTransformable", "Hmx::Object"};
  plan.check = 0x141;
  return plan;
}

SourceCharInterestSavePlan source_char_interest_save_plan() {
  return SourceCharInterestSavePlan{};
}

SourceCharInterestHighlightPlan source_char_interest_highlight_plan(
    bool world_to_screen_positive,
    bool has_dart_override,
    bool has_min_radius_property,
    bool has_max_radius_property) {
  SourceCharInterestHighlightPlan plan;
  plan.graph_calls = {"AddSphere interest radius 1 red"};
  if (world_to_screen_positive) {
    plan.projects_label = true;
    plan.graph_calls.push_back("AddString name screen offset");
  }
  if (has_dart_override) {
    plan.queries_dart_min_radius = true;
    plan.queries_dart_max_radius = true;
    if (has_min_radius_property && has_max_radius_property) {
      plan.graph_calls.push_back("AddSphere dart min radius gray");
      plan.graph_calls.push_back("AddSphere dart max radius white");
    }
  }
  return plan;
}

SourceCharInterestComputeScorePlan source_char_interest_compute_score_plan() {
  SourceCharInterestComputeScorePlan plan;
  plan.gates = {"IsMatchingFilterFlags(mask)",
                "default category allowed when fallback flag is true",
                "return -1.0 when no category gate matches"};
  plan.score_steps = {"direction from viewer to interest",
                      "distance squared",
                      "view dot against max angle cosine",
                      "interest dot against max angle cosine",
                      "-(distanceSquared * distanceScale - 1.0)",
                      "NaN distance contribution becomes 0.2",
                      "add two dot gates and -0.99",
                      "nonnegative score receives RandomFloat jitter",
                      "multiply by priority"};
  return plan;
}

SourceCharInterestScoreResult source_char_interest_compute_score_deterministic(
    const std::array<float, 3>& view_direction,
    const std::array<float, 3>& viewer_world,
    const std::array<float, 3>& interest_direction,
    const std::array<float, 3>& interest_world,
    float distance_scale,
    int mask,
    bool allow_default_category,
    int category_flags,
    float priority,
    float max_view_angle_cos,
    float random_jitter) {
  SourceCharInterestScoreResult result;
  result.category_gate =
      source_char_interest_is_matching_filter_flags(category_flags, mask);
  result.default_category_gate = allow_default_category && category_flags == 0;
  if (!result.category_gate && !result.default_category_gate) {
    result.returned_reject = true;
    result.score = -1.0f;
    return result;
  }

  const float dx = interest_world[0] - viewer_world[0];
  const float dy = interest_world[1] - viewer_world[1];
  const float dz = interest_world[2] - viewer_world[2];
  result.distance_squared = dx * dx + dy * dy + dz * dz;
  const float len = std::sqrt(result.distance_squared);
  const float nx = len > 0.0f ? dx / len : 0.0f;
  const float ny = len > 0.0f ? dy / len : 0.0f;
  const float nz = len > 0.0f ? dz / len : 0.0f;

  result.view_dot = view_direction[0] * nx + view_direction[1] * ny +
                    view_direction[2] * nz;
  result.view_dot_gate = result.view_dot >= max_view_angle_cos;
  result.interest_dot = interest_direction[0] * nx + interest_direction[1] * ny +
                        interest_direction[2] * nz;
  result.interest_dot_gate = result.interest_dot >= max_view_angle_cos;

  result.distance_score = -(result.distance_squared * distance_scale - 1.0f);
  if (std::isnan(result.distance_score)) {
    result.distance_score_was_nan = true;
    result.distance_score = 0.2f;
  }

  result.pre_jitter_score = result.distance_score +
                            (result.view_dot_gate ? 1.0f : 0.0f) +
                            (result.interest_dot_gate ? 1.0f : 0.0f) - 0.99f;
  result.score = result.pre_jitter_score;
  if (result.score >= 0.0f) {
    result.applied_random_jitter = true;
    result.score += random_jitter;
  }
  result.score *= priority;
  return result;
}

std::array<float, 9> source_char_neck_twist_multiply_matrix(
    const std::array<float, 9>& lhs,
    const std::array<float, 9>& rhs) {
  std::array<float, 9> out = {};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      out[static_cast<size_t>(row * 3 + col)] =
          lhs[static_cast<size_t>(row * 3 + 0)] *
              rhs[static_cast<size_t>(0 * 3 + col)] +
          lhs[static_cast<size_t>(row * 3 + 1)] *
              rhs[static_cast<size_t>(1 * 3 + col)] +
          lhs[static_cast<size_t>(row * 3 + 2)] *
              rhs[static_cast<size_t>(2 * 3 + col)];
    }
  }
  return out;
}

std::array<float, 3> source_char_neck_twist_matrix_row(
    const std::array<float, 9>& matrix,
    int row) {
  return {matrix[static_cast<size_t>(row * 3 + 0)],
          matrix[static_cast<size_t>(row * 3 + 1)],
          matrix[static_cast<size_t>(row * 3 + 2)]};
}

float source_char_neck_twist_dot(const std::array<float, 3>& a,
                                 const std::array<float, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> source_char_neck_twist_cross(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

std::array<float, 3> source_char_neck_twist_normalize(
    std::array<float, 3> v,
    const std::array<float, 3>& fallback = {1.0f, 0.0f, 0.0f}) {
  const float len_sq = source_char_neck_twist_dot(v, v);
  if (len_sq <= 1.0e-12f) return fallback;
  const float inv_len = 1.0f / std::sqrt(len_sq);
  v[0] *= inv_len;
  v[1] *= inv_len;
  v[2] *= inv_len;
  return v;
}

std::array<float, 4> source_char_neck_twist_quat_from_vec_to_vec(
    std::array<float, 3> from,
    std::array<float, 3> to) {
  from = source_char_neck_twist_normalize(from);
  to = source_char_neck_twist_normalize(to, from);
  std::array<float, 3> axis = source_char_neck_twist_cross(from, to);
  const float dot =
      std::clamp(source_char_neck_twist_dot(from, to), -1.0f, 1.0f);
  if (dot < -0.9999f) {
    axis = source_char_neck_twist_cross(from, {1.0f, 0.0f, 0.0f});
    if (source_char_neck_twist_dot(axis, axis) <= 1.0e-10f) {
      axis = source_char_neck_twist_cross(from, {0.0f, 1.0f, 0.0f});
    }
    axis = source_char_neck_twist_normalize(axis, {0.0f, 0.0f, 1.0f});
    return {axis[0], axis[1], axis[2], 0.0f};
  }

  const float scale = std::sqrt((1.0f + dot) * 2.0f);
  if (scale <= 1.0e-6f) return {0.0f, 0.0f, 0.0f, 1.0f};
  const float inv_scale = 1.0f / scale;
  return {axis[0] * inv_scale, axis[1] * inv_scale, axis[2] * inv_scale,
          0.5f * scale};
}

std::array<float, 3> source_char_neck_twist_rotate_vec_by_quat(
    const std::array<float, 3>& v,
    const std::array<float, 4>& q) {
  const float x = q[0];
  const float y = q[1];
  const float z = q[2];
  const float w = q[3];
  const float xx = x * x;
  const float yy = y * y;
  const float zz = z * z;
  const float xy = x * y;
  const float xz = x * z;
  const float yz = y * z;
  const float wx = w * x;
  const float wy = w * y;
  const float wz = w * z;

  const float m00 = 1.0f - 2.0f * (yy + zz);
  const float m01 = 2.0f * (xy - wz);
  const float m02 = 2.0f * (xz + wy);
  const float m10 = 2.0f * (xy + wz);
  const float m11 = 1.0f - 2.0f * (xx + zz);
  const float m12 = 2.0f * (yz - wx);
  const float m20 = 2.0f * (xz - wy);
  const float m21 = 2.0f * (yz + wx);
  const float m22 = 1.0f - 2.0f * (xx + yy);

  return {v[0] * m00 + v[1] * m10 + v[2] * m20,
          v[0] * m01 + v[1] * m11 + v[2] * m21,
          v[0] * m02 + v[1] * m12 + v[2] * m22};
}

std::array<float, 3> source_char_neck_twist_make_rot_quat_unit_x_y(
    const std::array<float, 3>& accumulated_x,
    const std::array<float, 3>& accumulated_y) {
  const std::array<float, 4> quat =
      source_char_neck_twist_quat_from_vec_to_vec({1.0f, 0.0f, 0.0f},
                                                  accumulated_x);
  return source_char_neck_twist_rotate_vec_by_quat(accumulated_y, quat);
}

SourceCharNeckTwistState source_char_neck_twist_defaults() {
  return SourceCharNeckTwistState{};
}

bool source_char_neck_twist_load_revision_known(int revision) {
  return revision >= 0 && revision <= 1;
}

SourceCharNeckTwistLoadPlan source_char_neck_twist_load_plan(int revision) {
  SourceCharNeckTwistLoadPlan plan;
  plan.known_revision = source_char_neck_twist_load_revision_known(revision);
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "mHead", "mTwist"};
  return plan;
}

SourceCharNeckTwistCopyPlan source_char_neck_twist_copy_plan() {
  SourceCharNeckTwistCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mHead", "mTwist"};
  return plan;
}

SourceCharNeckTwistHandlerPlan source_char_neck_twist_handler_plan() {
  SourceCharNeckTwistHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x65;
  return plan;
}

SourceCharNeckTwistPropSyncPlan source_char_neck_twist_prop_sync_plan() {
  SourceCharNeckTwistPropSyncPlan plan;
  plan.properties = {"head", "twist"};
  return plan;
}

SourceCharNeckTwistSavePlan source_char_neck_twist_save_plan() {
  return SourceCharNeckTwistSavePlan{};
}

void source_char_neck_twist_poll_deps(SourceCharNeckTwistPollDeps& deps,
                                      const std::string& head,
                                      const std::string& twist) {
  deps.changed_by.push_back(head);
  deps.change.push_back(twist);
}

float source_char_neck_twist_half_limited_angle(float rotated_y_y,
                                                float rotated_y_z) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = kPi * 2.0f;
  float angle = std::atan2(rotated_y_z, rotated_y_y);
  angle = std::fmod(angle + kPi, kTwoPi);
  if (angle < 0.0f) angle += kTwoPi;
  angle -= kPi;
  return angle * 0.5f;
}

SourceCharNeckTwistPollPlan source_char_neck_twist_poll_plan(
    bool has_head,
    bool has_twist,
    bool has_twist_parent,
    bool reaches_twist_parent,
    const std::array<float, 9>& head_local_matrix,
    const std::vector<std::array<float, 9>>& parent_local_matrices) {
  SourceCharNeckTwistPollPlan plan;
  if (!has_head || !has_twist) return plan;
  plan.entered_head_twist_gate = true;
  if (!has_twist_parent) return plan;
  plan.entered_twist_parent_gate = true;
  plan.accumulated_matrix = head_local_matrix;
  for (const std::array<float, 9>& parent_local : parent_local_matrices) {
    plan.accumulated_matrix =
        source_char_neck_twist_multiply_matrix(plan.accumulated_matrix,
                                               parent_local);
    ++plan.parent_multiply_count;
  }
  if (!reaches_twist_parent) return plan;

  plan.reached_twist_parent = true;
  plan.accumulated_x =
      source_char_neck_twist_matrix_row(plan.accumulated_matrix, 0);
  plan.accumulated_y =
      source_char_neck_twist_matrix_row(plan.accumulated_matrix, 1);
  plan.applied_make_rot_quat_unit_x = true;
  plan.rotated_y_after_make_rot_quat_unit_x =
      source_char_neck_twist_make_rot_quat_unit_x_y(plan.accumulated_x,
                                                    plan.accumulated_y);
  plan.rotate_about_x_radians = source_char_neck_twist_half_limited_angle(
      plan.rotated_y_after_make_rot_quat_unit_x[1],
      plan.rotated_y_after_make_rot_quat_unit_x[2]);
  plan.writes_twist_local_rotate_x = true;
  return plan;
}

SourceCharIKFingersState source_char_ik_fingers_defaults() {
  return SourceCharIKFingersState{};
}

bool source_char_ik_fingers_load_revision_known(int revision) {
  return revision >= 0 && revision <= 5;
}

SourceCharIKFingersSetupRefs source_char_ik_fingers_set_name_refs(
    bool is_right_hand) {
  SourceCharIKFingersSetupRefs refs;
  refs.is_right_hand = is_right_hand;
  const std::string side = is_right_hand ? "R" : "L";
  refs.hand = "bone_" + side + "-hand.mesh";
  refs.forearm = "bone_" + side + "-foreArm.mesh";
  refs.upperarm = "bone_" + side + "-upperArm.mesh";
  const std::array<std::string, 5> fingers = {
      "thumb", "index", "middlefinger", "ringfinger", "pinky"};
  for (size_t i = 0; i < fingers.size(); ++i) {
    refs.fingers[i].finger01 = "bone_" + side + "-" + fingers[i] + "01.mesh";
    refs.fingers[i].finger02 = "bone_" + side + "-" + fingers[i] + "02.mesh";
    refs.fingers[i].finger03 = "bone_" + side + "-" + fingers[i] + "03.mesh";
    refs.fingers[i].fingertip =
        "spot_" + side + "-" + fingers[i] + "_tip.mesh";
  }
  refs.raw_matrix =
      is_right_hand
          ? std::array<float, 9>{-0.023f, 0.97899997f, 0.201f,
                                 -0.228f, 0.191f, -0.95499998f,
                                 -0.972f, -0.068f, 0.21799999f}
          : std::array<float, 9>{-0.067f, 0.985f, 0.156f,
                                 0.224f, 0.167f, -0.95999998f,
                                 -0.972f, -0.028999999f, -0.23199999f};
  return refs;
}

bool source_char_ik_fingers_setup_complete(
    const SourceCharIKFingersSetupRefs& refs,
    const std::vector<std::string>& present_transforms) {
  const auto present = [&](const std::string& name) {
    return std::find(present_transforms.begin(), present_transforms.end(),
                     name) != present_transforms.end();
  };
  for (const SourceCharIKFingersFingerRefs& finger : refs.fingers) {
    if (!present(finger.finger01) || !present(finger.finger02) ||
        !present(finger.finger03) || !present(finger.fingertip)) {
      return false;
    }
  }
  return true;
}

SourceCharIKFingersSetFingerPlan source_char_ik_fingers_set_finger_plan(
    int finger) {
  SourceCharIKFingersSetFingerPlan plan;
  plan.finger = finger;
  plan.known_finger = finger >= 0 && finger < 5;
  if (!plan.known_finger) return plan;
  plan.assign_primary_vector = true;
  plan.assign_secondary_vector = true;
  plan.set_active = true;
  plan.mark_dirty = true;
  plan.multiply_finger01_by_current_hand = true;
  return plan;
}

SourceCharIKFingersReleaseFingerPlan
source_char_ik_fingers_release_finger_plan(int finger) {
  SourceCharIKFingersReleaseFingerPlan plan;
  plan.finger = finger;
  plan.known_finger = finger >= 0 && finger < 5;
  if (!plan.known_finger) return plan;
  plan.clear_active = true;
  plan.mark_dirty = true;
  return plan;
}

SourceCharIKFingersLoadPlan source_char_ik_fingers_load_plan(int revision) {
  SourceCharIKFingersLoadPlan plan;
  plan.known_revision = source_char_ik_fingers_load_revision_known(revision);
  if (!plan.known_revision) return plan;
  plan.read_order = {"Hmx::Object", "CharWeightable"};
  if (revision > 1) plan.read_order.push_back("mIsRightHand");
  if (revision > 2) plan.read_order.push_back("mOutputTrans");
  if (revision > 3) plan.read_order.push_back("mKeyboardRefBone");
  if (revision > 4) {
    plan.read_order.push_back("mHandKeyboardOffset");
    plan.read_order.push_back("mHandThumbRotation");
    plan.read_order.push_back("mHandPinkyRotation");
    plan.read_order.push_back("mHandMoveForward");
    plan.read_order.push_back("mHandDestOffset");
  }
  return plan;
}

SourceCharIKFingersCopyPlan source_char_ik_fingers_copy_plan() {
  SourceCharIKFingersCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object", "CharWeightable"};
  plan.copied_members = {"mIsRightHand",        "mOutputTrans",
                         "mKeyboardRefBone",   "mHandKeyboardOffset",
                         "mHandThumbRotation", "mHandPinkyRotation",
                         "mHandMoveForward",   "mHandDestOffset"};
  return plan;
}

SourceCharIKFingersHandlerPlan source_char_ik_fingers_handler_plan() {
  SourceCharIKFingersHandlerPlan plan;
  plan.superclasses = {"CharWeightable", "Hmx::Object"};
  plan.check = 0x3ab;
  return plan;
}

SourceCharIKFingersPropSyncPlan source_char_ik_fingers_prop_sync_plan() {
  SourceCharIKFingersPropSyncPlan plan;
  plan.properties = {"is_right_hand",
                     "output_trans",
                     "keyboard_ref_bone",
                     "hand_keyboard_offset",
                     "hand_thumb_rotation",
                     "hand_pinky_rotation",
                     "hand_move_forward",
                     "hand_dest_offset"};
  plan.superclasses = {"CharWeightable"};
  return plan;
}

SourceCharIKFingersRuntimeBoundary
source_char_ik_fingers_runtime_boundary() {
  SourceCharIKFingersRuntimeBoundary boundary;
  boundary.unresolved_bodies = {"MeasureLengths",
                                "PollDeps",
                                "SetFinger transform math",
                                "full Poll solve"};
  return boundary;
}

SourceCharIKFingersSavePlan source_char_ik_fingers_save_plan() {
  return SourceCharIKFingersSavePlan{};
}

SourceCharSleevePollResult source_char_sleeve_poll(
    SourceCharSleeveState& state,
    bool has_sleeve,
    bool has_parent,
    bool has_top_sleeve,
    bool character_teleported,
    float delta_seconds,
    float sleeve_local_z,
    const milo_scene::Xfm& sleeve_world,
    const milo_scene::Xfm& parent_world) {
  SourceCharSleevePollResult result;
  if (!has_sleeve || !has_parent) return result;

  const float dvar12 = delta_seconds * 60.0f;
  const float powed = std::pow(1.0f - state.stiffness, dvar12 * dvar12);
  const float absed = std::fabs(sleeve_local_z);
  const SourceVec3 parent_pos = source_xfm_pos(parent_world);
  const SourceVec3 parent_x = source_xfm_row(parent_world, 0);
  bool teleported_reset = false;

  if (character_teleported) {
    state.pos = source_xfm_pos(sleeve_world);
    SourceVec3 v9c = {0.0f, 0.0f, -(absed + state.pos_length)};
    float dotted = source_vec_dot(v9c, parent_x);
    dotted = std::clamp(dotted, -state.range, state.range);
    v9c = source_vec_add(v9c, source_vec_scale(parent_x, dotted));
    state.pos = source_vec_add(state.pos, v9c);
    const SourceVec3 va8 = source_vec_add(parent_pos,
                                          source_vec_scale(parent_x, dotted));
    v9c = source_vec_sub(state.pos, va8);
    v9c = source_vec_scale_to_magnitude(v9c, absed + state.pos_length);
    state.pos = source_vec_add(va8, v9c);
    state.last_pos = state.pos;
    teleported_reset = true;
    state.last_dt = 0.0f;
  }

  SourceVec3 vb4 = state.pos;
  if (state.last_dt > 0.0f && delta_seconds > 0.0f) {
    const SourceVec3 vc0 = source_vec_sub(state.pos, state.last_pos);
    vb4 = source_vec_add(
        vb4, source_vec_scale(vc0, (state.inertia * delta_seconds) /
                                       state.last_dt));
  }
  vb4[2] += state.gravity * delta_seconds * dvar12 * -3.858268f;

  SourceVec3 vcc = source_vec_sub(vb4, parent_pos);
  const float dotted2 = source_vec_dot(vcc, parent_x);
  (void)dotted2;
  float d4 = dvar12 * (1.0f - (1.0f - powed));
  d4 = std::clamp(d4, -state.range, state.range);
  vcc = source_vec_add(vcc, source_vec_scale(parent_x, d4 - dvar12));
  const float len = source_vec_length(vcc);
  float interped = len + (absed - len) * (1.0f - powed);
  interped = std::clamp(interped, absed - state.neg_length,
                        absed + state.pos_length);
  (void)interped;
  vcc = source_vec_scale_to_magnitude(vcc, len);
  vb4 = source_vec_add(parent_pos, vcc);

  result.sleeve_world =
      source_char_sleeve_make_world(vb4, parent_x, vcc);
  result.wrote_sleeve = true;

  state.last_pos = state.pos;
  state.last_dt = delta_seconds;
  state.pos = vb4;
  if (teleported_reset) state.last_pos = state.pos;

  if (has_top_sleeve) {
    const float dotcc = source_vec_dot(vcc, parent_x);
    SourceVec3 top_delta =
        source_vec_add(vcc, source_vec_scale(parent_x, -dotcc));
    const SourceVec3 top_pos = source_vec_add(parent_pos, top_delta);
    result.top_sleeve_world =
        source_char_sleeve_make_world(top_pos, parent_x, top_delta);
    result.wrote_top_sleeve = true;
  }

  return result;
}

SourceCharSleeveHighlightPlan source_char_sleeve_highlight_plan(
    bool has_sleeve,
    bool has_parent,
    bool has_top_sleeve) {
  SourceCharSleeveHighlightPlan plan;
  if (!has_sleeve || !has_parent) {
    plan.exits_without_sleeve_or_parent = true;
    return plan;
  }
  plan.draw_steps = {"UtilDrawAxes(mSleeve, green)",
                     "DrawLine(mSleeve,parent, green)"};
  if (has_top_sleeve) {
    plan.draw_steps.push_back("UtilDrawAxes(mTopSleeve, cyan)");
    plan.draw_steps.push_back("DrawLine(mTopSleeve,parent, cyan)");
  }
  return plan;
}

void source_char_sleeve_poll_deps(SourceCharSleevePollDeps& deps,
                                  const std::string& sleeve_parent,
                                  const std::string& sleeve,
                                  const std::string& top_sleeve,
                                  bool has_sleeve) {
  if (!has_sleeve) return;
  deps.changed_by.push_back(sleeve_parent);
  deps.change.push_back(sleeve);
  deps.change.push_back(top_sleeve);
}

SourceCharSleeveLoadPlan source_char_sleeve_load_plan(int32_t revision) {
  SourceCharSleeveLoadPlan plan;
  plan.revision_supported = revision == 0;
  if (!plan.revision_supported) return plan;
  plan.read_order = {"Hmx::Object", "mSleeve",    "mTopSleeve",
                     "mInertia",    "mGravity",  "mStiffness",
                     "mRange",      "mNegLength", "mPosLength"};
  return plan;
}

SourceCharSleeveSavePlan source_char_sleeve_save_plan() {
  return SourceCharSleeveSavePlan{};
}

SourceCharSleeveCopyPlan source_char_sleeve_copy_plan() {
  SourceCharSleeveCopyPlan plan;
  plan.copied_superclasses = {"Hmx::Object"};
  plan.copied_members = {"mSleeve",    "mTopSleeve", "mInertia",
                         "mGravity",   "mStiffness", "mRange",
                         "mNegLength", "mPosLength"};
  return plan;
}

SourceCharSleeveHandlerPlan source_char_sleeve_handler_plan() {
  SourceCharSleeveHandlerPlan plan;
  plan.superclasses = {"Hmx::Object"};
  plan.check = 0x112;
  return plan;
}

SourceCharSleevePropSyncPlan source_char_sleeve_prop_sync_plan() {
  SourceCharSleevePropSyncPlan plan;
  plan.properties = {"sleeve",    "top_sleeve", "inertia", "gravity",
                     "stiffness", "range",      "neg_length",
                     "pos_length"};
  return plan;
}

void source_char_hair_strand_set_angle(CharHairStrand& strand,
                                       float angle_degrees) {
  strand.angle = angle_degrees;
  const std::array<float, 9> root =
      source_char_hair_set_angle_root_mat(strand.angle, strand.base_mat);
  for (size_t i = 0; i < root.size(); ++i) strand.root_mat[i] = root[i];
}

void source_char_hair_strand_set_root(
    CharHairStrand& strand,
    const std::vector<SourceCharHairRootNode>& first_child_chain) {
  strand.root = first_child_chain.empty() ? "" : first_child_chain.front().bone;
  if (strand.root.empty()) {
    strand.points.clear();
    return;
  }

  float len = strand.points.empty() ? 0.0f : strand.points.back().length;
  for (size_t i = 0; i < first_child_chain.front().local_mat.size(); ++i) {
    strand.base_mat[i] = first_child_chain.front().local_mat[i];
  }
  source_char_hair_strand_set_angle(strand, strand.angle);

  strand.points.resize(first_child_chain.size());
  for (size_t i = 0; i < first_child_chain.size(); ++i) {
    strand.points[i].bone = first_child_chain[i].bone;
  }

  CharHairPoint* previous_point = nullptr;
  for (size_t i = 1; i < strand.points.size(); ++i) {
    previous_point = &strand.points[i - 1];
    const SourceCharHairRootNode& bone = first_child_chain[i];
    previous_point->length = bone.local_y;
    previous_point->pos[0] = bone.world_pos[0];
    previous_point->pos[1] = bone.world_pos[1];
    previous_point->pos[2] = bone.world_pos[2];
  }

  CharHairPoint& back_point = strand.points.back();
  if (len == 0.0f) {
    len = previous_point != nullptr ? previous_point->length : 5.0f;
  }
  const SourceCharHairRootNode& back_bone = first_child_chain.back();
  back_point.length = len;
  back_point.pos[0] = back_bone.world_pos[0] + back_bone.world_y_axis[0] * len;
  back_point.pos[1] = back_bone.world_pos[1] + back_bone.world_y_axis[1] * len;
  back_point.pos[2] = back_bone.world_pos[2] + back_bone.world_y_axis[2] * len;
}

static std::string source_ascii_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static bool source_case_equal(const std::string& a, const std::string& b) {
  return source_ascii_lower(a) == source_ascii_lower(b);
}

static bool source_case_ends_with(const std::string& s,
                                  const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  return source_case_equal(s.substr(s.size() - suffix.size()), suffix);
}

bool source_gltf_milo_is_hair_bone_node(
    const SourceGltfMiloHairNode& node) {
  if (!node.is_bone) return false;
  const std::string lower = source_ascii_lower(node.name);
  return lower.rfind("bone_hair_", 0) == 0;
}

SourceGltfMiloHairChildClassification
source_gltf_milo_classify_hair_children(
    const SourceGltfMiloHairNode& parent,
    const std::vector<SourceGltfMiloHairNode>& children) {
  SourceGltfMiloHairChildClassification result;
  for (const SourceGltfMiloHairNode& child : children) {
    if (source_gltf_milo_is_hair_bone_node(child)) {
      result.hair_children.push_back(child.name);
    } else if (child.is_bone) {
      result.non_hair_bone_children.push_back(child.name);
      result.warnings.push_back(
          "Non-hair bone '" + child.name + "' found under hair bone '" +
          parent.name +
          "'. It will not be included in CharHair strand generation.");
    }
  }
  return result;
}

static std::vector<SourceGltfMiloHairNode> source_gltf_milo_child_nodes(
    const std::vector<SourceGltfMiloHairNode>& nodes,
    const std::vector<int>& child_indices) {
  std::vector<SourceGltfMiloHairNode> child_nodes;
  child_nodes.reserve(child_indices.size());
  for (int child : child_indices) {
    if (child >= 0 && static_cast<size_t>(child) < nodes.size()) {
      child_nodes.push_back(nodes[static_cast<size_t>(child)]);
    }
  }
  return child_nodes;
}

struct SourceGltfMiloHairRootDiscoveryInternal {
  SourceGltfMiloHairRootDiscoveryResult result;
  std::vector<int> root_indices;
};

static SourceGltfMiloHairRootDiscoveryInternal
source_gltf_milo_discover_hair_roots_internal(
    const std::vector<SourceGltfMiloHairNode>& nodes) {
  SourceGltfMiloHairRootDiscoveryInternal discovered;
  std::unordered_set<std::string> seen_roots;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].weighted || !source_gltf_milo_is_hair_bone_node(nodes[i])) {
      continue;
    }
    discovered.result.has_weighted_hair_bones = true;
    int root = static_cast<int>(i);
    while (nodes[static_cast<size_t>(root)].parent >= 0) {
      const int parent = nodes[static_cast<size_t>(root)].parent;
      if (parent < 0 || static_cast<size_t>(parent) >= nodes.size() ||
          !source_gltf_milo_is_hair_bone_node(
              nodes[static_cast<size_t>(parent)])) {
        break;
      }
      root = parent;
    }

    const SourceGltfMiloHairNode& root_node =
        nodes[static_cast<size_t>(root)];
    const std::string key = source_ascii_lower(root_node.name);
    if (seen_roots.insert(key).second) {
      discovered.root_indices.push_back(root);
      discovered.result.roots.push_back(root_node.name);
    } else {
      discovered.result.skipped_duplicate_roots.push_back(root_node.name);
    }
  }
  return discovered;
}

SourceGltfMiloHairRootDiscoveryResult
source_gltf_milo_discover_hair_roots(
    const std::vector<SourceGltfMiloHairNode>& nodes) {
  return source_gltf_milo_discover_hair_roots_internal(nodes).result;
}

static bool source_gltf_milo_collect_hair_chains_split_at_branches_impl(
    const std::vector<SourceGltfMiloHairNode>& nodes,
    const std::vector<std::vector<int>>& children,
    int node_index,
    bool ancestor_weighted,
    std::vector<std::vector<std::string>>& chains,
    std::vector<std::string>& warnings) {
  std::vector<int> segment;
  int current = node_index;

  while (current >= 0 && static_cast<size_t>(current) < nodes.size()) {
    segment.push_back(current);

    std::vector<int> hair_children;
    const SourceGltfMiloHairChildClassification child_classification =
        source_gltf_milo_classify_hair_children(
            nodes[static_cast<size_t>(current)],
            source_gltf_milo_child_nodes(
                nodes, children[static_cast<size_t>(current)]));
    warnings.insert(warnings.end(), child_classification.warnings.begin(),
                    child_classification.warnings.end());
    for (int child : children[static_cast<size_t>(current)]) {
      const SourceGltfMiloHairNode& child_node =
          nodes[static_cast<size_t>(child)];
      if (source_gltf_milo_is_hair_bone_node(child_node)) {
        hair_children.push_back(child);
      }
    }

    if (hair_children.size() == 1) {
      current = hair_children.front();
      continue;
    }

    bool segment_weighted = false;
    for (int index : segment) {
      segment_weighted = segment_weighted ||
                         nodes[static_cast<size_t>(index)].weighted;
    }

    std::vector<std::vector<std::string>> child_chains;
    bool subtree_weighted = false;
    for (int child : hair_children) {
      subtree_weighted =
          source_gltf_milo_collect_hair_chains_split_at_branches_impl(
              nodes, children, child, ancestor_weighted || segment_weighted,
              child_chains, warnings) ||
          subtree_weighted;
    }

    if (segment_weighted || ancestor_weighted || subtree_weighted) {
      std::vector<std::string> chain;
      chain.reserve(segment.size());
      for (int index : segment) {
        chain.push_back(nodes[static_cast<size_t>(index)].name);
      }
      chains.push_back(std::move(chain));
    }
    chains.insert(chains.end(), child_chains.begin(), child_chains.end());
    return segment_weighted || subtree_weighted;
  }

  return false;
}

SourceGltfMiloHairChainsResult
source_gltf_milo_collect_hair_chains_split_at_branches(
    const std::vector<SourceGltfMiloHairNode>& nodes) {
  SourceGltfMiloHairChainsResult result;
  std::vector<std::vector<int>> children(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    const int parent = nodes[i].parent;
    if (parent >= 0 && static_cast<size_t>(parent) < nodes.size()) {
      children[static_cast<size_t>(parent)].push_back(static_cast<int>(i));
    }
  }

  const SourceGltfMiloHairRootDiscoveryInternal roots =
      source_gltf_milo_discover_hair_roots_internal(nodes);
  result.has_weighted_hair_bones =
      roots.result.has_weighted_hair_bones;
  result.roots = roots.result.roots;

  for (int root : roots.root_indices) {
    source_gltf_milo_collect_hair_chains_split_at_branches_impl(
        nodes, children, root, false, result.chains, result.warnings);
  }
  return result;
}

static void source_gltf_milo_collect_hair_chains_without_splitting_impl(
    const std::vector<SourceGltfMiloHairNode>& nodes,
    const std::vector<std::vector<int>>& children,
    int node_index,
    std::vector<int>& current_chain,
    std::vector<std::vector<std::string>>& chains,
    std::vector<std::string>& warnings) {
  current_chain.push_back(node_index);

  std::vector<int> hair_children;
  const SourceGltfMiloHairChildClassification child_classification =
      source_gltf_milo_classify_hair_children(
          nodes[static_cast<size_t>(node_index)],
          source_gltf_milo_child_nodes(
              nodes, children[static_cast<size_t>(node_index)]));
  warnings.insert(warnings.end(), child_classification.warnings.begin(),
                  child_classification.warnings.end());
  for (int child : children[static_cast<size_t>(node_index)]) {
    const SourceGltfMiloHairNode& child_node = nodes[static_cast<size_t>(child)];
    if (source_gltf_milo_is_hair_bone_node(child_node)) {
      hair_children.push_back(child);
    }
  }

  if (hair_children.size() > 1) {
    warnings.push_back("Hair bone '" +
                       nodes[static_cast<size_t>(node_index)].name +
                       "' branches into multiple hair chains and strand "
                       "splitting is disabled. Bones above the branch will be "
                       "simulated by multiple strands, which will likely "
                       "behave incorrectly in-game.");
  }

  if (hair_children.empty()) {
    bool chain_weighted = false;
    for (int index : current_chain) {
      chain_weighted = chain_weighted ||
                       nodes[static_cast<size_t>(index)].weighted;
    }
    if (chain_weighted) {
      std::vector<std::string> chain;
      chain.reserve(current_chain.size());
      for (int index : current_chain) {
        chain.push_back(nodes[static_cast<size_t>(index)].name);
      }
      chains.push_back(std::move(chain));
    }
  } else {
    for (int child : hair_children) {
      source_gltf_milo_collect_hair_chains_without_splitting_impl(
          nodes, children, child, current_chain, chains, warnings);
    }
  }

  current_chain.pop_back();
}

SourceGltfMiloHairChainsResult
source_gltf_milo_collect_hair_chains_without_splitting(
    const std::vector<SourceGltfMiloHairNode>& nodes) {
  SourceGltfMiloHairChainsResult result;
  std::vector<std::vector<int>> children(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    const int parent = nodes[i].parent;
    if (parent >= 0 && static_cast<size_t>(parent) < nodes.size()) {
      children[static_cast<size_t>(parent)].push_back(static_cast<int>(i));
    }
  }

  const SourceGltfMiloHairRootDiscoveryInternal roots =
      source_gltf_milo_discover_hair_roots_internal(nodes);
  result.has_weighted_hair_bones =
      roots.result.has_weighted_hair_bones;
  result.roots = roots.result.roots;

  std::vector<int> current_chain;
  for (int root : roots.root_indices) {
    source_gltf_milo_collect_hair_chains_without_splitting_impl(
        nodes, children, root, current_chain, result.chains, result.warnings);
  }
  return result;
}

SourceGltfMiloCharHairExportPlan source_gltf_milo_process_char_hair_plan(
    int weighted_hair_bone_count,
    int strand_count,
    const std::string& requested_wind,
    bool split_strands_at_branches) {
  SourceGltfMiloCharHairExportPlan plan;
  if (weighted_hair_bone_count <= 0) {
    plan.exits_for_empty_weighted_set = true;
    return plan;
  }

  plan.constructs_char_hair_object = true;
  plan.revision = 11;
  plan.object_revision = 2;
  plan.simulate = true;
  plan.physics_fields = {"stiffness", "torsion", "inertia",
                         "gravity", "weight",  "friction"};
  plan.uses_default_wind = requested_wind.empty();
  plan.wind_source =
      plan.uses_default_wind ? "CharHairExtras.DefaultWind"
                             : "physicsSettings.Wind";
  plan.wind_value = requested_wind;
  plan.strand_collector =
      split_strands_at_branches ? "CollectHairChainsSplitAtBranches"
                                : "CollectHairChains";

  if (strand_count <= 0) {
    plan.exits_for_empty_strands = true;
    return plan;
  }

  plan.creates_entry = true;
  plan.entry_type = "CharHair";
  plan.entry_name = "hair.hair";
  return plan;
}

SourceGltfMiloCharHairExtrasBoundary
source_gltf_milo_char_hair_extras_boundary() {
  SourceGltfMiloCharHairExtrasBoundary boundary;
  boundary.process_call_sites = {
      "Program detectedHairSettings CharHairExtras",
      "Program JsonSerializer.Deserialize<CharHairExtras>",
      "Program detectedHairSettings ?? new CharHairExtras()",
      "ProcessCharHair physicsSettings.Stiffness",
      "ProcessCharHair physicsSettings.Torsion",
      "ProcessCharHair physicsSettings.Inertia",
      "ProcessCharHair physicsSettings.Gravity",
      "ProcessCharHair physicsSettings.Weight",
      "ProcessCharHair physicsSettings.Friction",
      "ProcessCharHair CharHairExtras.DefaultWind"};
  return boundary;
}

SourceGltfMiloCharHairExtrasDefaults
source_gltf_milo_char_hair_extras_defaults() {
  return SourceGltfMiloCharHairExtrasDefaults{};
}

SourceGltfMiloHairSettingsDetectionPlan
source_gltf_milo_detect_hair_settings_plan(
    const std::string& bone_name,
    const std::string& extras_json,
    bool already_detected_settings,
    bool deserialize_succeeds) {
  SourceGltfMiloHairSettingsDetectionPlan plan;
  plan.is_hair_bone = source_gltf_milo_is_hair_bone_name(bone_name);
  if (!plan.is_hair_bone) return plan;

  plan.checks_extras = !extras_json.empty();
  if (!plan.checks_extras) return plan;

  plan.contains_milo_hair_marker =
      extras_json.find("milo_hair_") != std::string::npos;
  if (!plan.contains_milo_hair_marker) return plan;

  plan.attempts_deserialize = true;
  plan.bad_extras_nonfatal = true;
  if (already_detected_settings) {
    plan.preserves_existing_settings = true;
  } else if (deserialize_succeeds) {
    plan.assigns_detected_settings = true;
  }
  return plan;
}

static float source_gltf_milo_hair_point_length(
    const std::vector<SourceGltfMiloHairPointNode>& chain,
    size_t point_index,
    SourceGltfMiloHairPointExport& result) {
  if (point_index >= chain.size()) return 0.0f;
  if (point_index + 1 < chain.size()) {
    result.length_from_next_bone = true;
    return source_vec_length(
        source_vec_sub(chain[point_index].world_pos,
                       chain[point_index + 1].world_pos));
  }
  if (point_index > 0) {
    result.length_from_previous_point = true;
    SourceGltfMiloHairPointExport ignored;
    return source_gltf_milo_hair_point_length(chain, point_index - 1, ignored);
  }
  if (chain[point_index].has_parent_world_pos) {
    const float parent_distance = source_vec_length(
        source_vec_sub(chain[point_index].parent_world_pos,
                       chain[point_index].world_pos));
    if (parent_distance > 0.0f) {
      result.length_from_parent = true;
      return parent_distance;
    }
  }
  result.length_defaulted_to_five = true;
  return 5.0f;
}

static SourceVec3 source_gltf_milo_transform_point(
    SourceVec3 point,
    const std::array<float, 16>& matrix) {
  return {point[0] * matrix[0] + point[1] * matrix[4] +
              point[2] * matrix[8] + matrix[12],
          point[0] * matrix[1] + point[1] * matrix[5] +
              point[2] * matrix[9] + matrix[13],
          point[0] * matrix[2] + point[1] * matrix[6] +
              point[2] * matrix[10] + matrix[14]};
}

SourceGltfMiloHairStrandHeaderPlan
source_gltf_milo_export_hair_strand_header_plan(
    const std::vector<SourceGltfMiloHairStrandHeaderNode>& chain,
    bool convert_coordinates) {
  SourceGltfMiloHairStrandHeaderPlan plan;
  plan.convert_coordinates_arg = convert_coordinates;
  plan.uses_matrix_helper_when_converting = convert_coordinates;
  plan.requires_unvendored_matrix_helper_when_converting = convert_coordinates;
  if (chain.empty()) {
    plan.skipped_empty_chain = true;
    return plan;
  }

  plan.creates_strand = true;
  plan.root = chain.front().name;
  plan.copies_first_local_matrix_to_base_mat = true;
  plan.copies_first_local_matrix_to_root_mat = true;
  plan.base_mat = chain.front().local_mat;
  plan.root_mat = chain.front().local_mat;
  return plan;
}

SourceGltfMiloHairPointExport source_gltf_milo_export_hair_point(
    const std::vector<SourceGltfMiloHairPointNode>& chain,
    int point_index,
    std::array<float, 16> strand_root_parent_world_inverse) {
  SourceGltfMiloHairPointExport result;
  if (point_index < 0 || static_cast<size_t>(point_index) >= chain.size()) {
    return result;
  }

  const size_t index = static_cast<size_t>(point_index);
  const SourceGltfMiloHairPointNode& node = chain[index];
  result.bone = node.name;
  result.length = source_gltf_milo_hair_point_length(chain, index, result);

  if (index + 1 < chain.size()) {
    result.used_next_bone_position = true;
    result.pos = chain[index + 1].world_pos;
  } else {
    result.used_tip_direction = true;
    SourceVec3 direction = node.world_y_axis;
    const float direction_len_sq = source_vec_dot(direction, direction);
    if (direction_len_sq <= 1.40129846e-45f) {
      direction = {0.0f, 1.0f, 0.0f};
      result.used_unit_y_fallback = true;
    } else {
      direction = source_vec_scale(direction, 1.0f / std::sqrt(direction_len_sq));
      if (!std::isfinite(direction[0]) || !std::isfinite(direction[1]) ||
          !std::isfinite(direction[2])) {
        direction = {0.0f, 1.0f, 0.0f};
        result.used_unit_y_fallback = true;
      }
    }
    result.pos =
        source_vec_add(node.world_pos, source_vec_scale(direction, result.length));
  }

  if (index + 1 < chain.size() && chain.size() > 1) {
    const float t = static_cast<float>(index) /
                    static_cast<float>(chain.size() - 1);
    result.radius = std::max(0.0f, 0.75f * (1.0f - (t * 0.5f)));
    result.outer_radius = std::max(0.0f, 2.0f * (1.0f - t));
  }

  result.reset_pos =
      source_gltf_milo_transform_point(result.pos,
                                       strand_root_parent_world_inverse);
  return result;
}

std::string source_gltf_milo_hair_collide_name(
    const std::string& mesh_name) {
  if (source_case_ends_with(mesh_name, ".mesh")) {
    return mesh_name.substr(0, mesh_name.size() - 5) + ".coll";
  }
  return mesh_name + ".coll";
}

std::vector<SourceGltfMiloHairCollideExport>
source_gltf_milo_process_empty_hair_collides(
    const std::vector<std::string>& hair_mesh_names,
    const std::vector<std::string>& existing_collide_names,
    const std::string& parent_name) {
  std::unordered_set<std::string> seen_meshes;
  std::unordered_set<std::string> existing_collides;
  for (const std::string& collide : existing_collide_names) {
    existing_collides.insert(source_ascii_lower(collide));
  }

  std::vector<SourceGltfMiloHairCollideExport> exports;
  for (const std::string& mesh_name : hair_mesh_names) {
    if (!seen_meshes.insert(source_ascii_lower(mesh_name)).second) continue;
    const std::string collide_name =
        source_gltf_milo_hair_collide_name(mesh_name);
    if (existing_collides.find(source_ascii_lower(collide_name)) !=
        existing_collides.end()) {
      continue;
    }

    SourceGltfMiloHairCollideExport out;
    out.collide_name = collide_name;
    out.mesh_name = mesh_name;
    out.parent_name = parent_name;
    exports.push_back(std::move(out));
  }
  return exports;
}

// ---------------------------------------------------------------------------
// 4x4 helpers (row-vector convention, matching render::Mat4).
// ---------------------------------------------------------------------------
namespace {

std::array<float, 16> mat4_mul(const std::array<float, 16>& a,
                               const std::array<float, 16>& b) {
  std::array<float, 16> r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
      r[i * 4 + j] = s;
    }
  return r;
}

std::array<float, 16> xfm_to_mat4(const Xfm& x) {
  std::array<float, 16> m{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) m[r * 4 + c] = x.rot[r][c];
  m[3 * 4 + 0] = x.pos[0];
  m[3 * 4 + 1] = x.pos[1];
  m[3 * 4 + 2] = x.pos[2];
  m[3 * 4 + 3] = 1.0f;
  return m;
}

std::array<float, 16> identity_mat4() {
  return {1, 0, 0, 0, 0, 1, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 1};
}

std::array<float, 16> affine_inverse(const std::array<float, 16>& m) {
  const float a = m[0], b = m[1], c = m[2];
  const float d = m[4], e = m[5], f = m[6];
  const float g = m[8], h = m[9], i = m[10];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) +
                    c * (d * h - e * g);
  if (std::fabs(det) < 1.0e-8f) return identity_mat4();
  const float inv = 1.0f / det;
  std::array<float, 16> out{};
  out[0] = (e * i - f * h) * inv;
  out[1] = -(b * i - c * h) * inv;
  out[2] = (b * f - c * e) * inv;
  out[4] = -(d * i - f * g) * inv;
  out[5] = (a * i - c * g) * inv;
  out[6] = -(a * f - c * d) * inv;
  out[8] = (d * h - e * g) * inv;
  out[9] = -(a * h - b * g) * inv;
  out[10] = (a * e - b * d) * inv;
  out[15] = 1.0f;
  const float tx = m[12], ty = m[13], tz = m[14];
  out[12] = -(tx * out[0] + ty * out[4] + tz * out[8]);
  out[13] = -(tx * out[1] + ty * out[5] + tz * out[9]);
  out[14] = -(tx * out[2] + ty * out[6] + tz * out[10]);
  return out;
}

bool affine_invertible(const std::array<float, 16>& m) {
  const float a = m[0], b = m[1], c = m[2];
  const float d = m[4], e = m[5], f = m[6];
  const float g = m[8], h = m[9], i = m[10];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) +
                    c * (d * h - e * g);
  return std::fabs(det) >= 1.0e-8f;
}

void mat4_to_xfm(const std::array<float, 16>& m, Xfm& x) {
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) x.rot[r][c] = m[r * 4 + c];
  x.pos[0] = m[12];
  x.pos[1] = m[13];
  x.pos[2] = m[14];
}

struct SourceXfm {
  const Xfm* current = nullptr;
  const Xfm* bind = nullptr;
  const Xfm* stored_world = nullptr;
  uint32_t constraint = 0;
  std::string target;
  bool preserve_scale = false;
  std::string parent;
};

bool find_source_xfm(const Character& c, const std::string& name,
                     SourceXfm& out) {
  for (size_t i = 0; i < c.bones.size(); ++i) {
    if (c.bones[i].name != name) continue;
    out.current = &c.bones[i].local;
    out.bind = i < c.bind_bone_local.size() ? &c.bind_bone_local[i] : &c.bones[i].local;
    out.stored_world = &c.bones[i].world_stored;
    out.constraint = c.bones[i].constraint;
    out.target = c.bones[i].target;
    out.preserve_scale = c.bones[i].preserve_scale;
    out.parent = c.bones[i].parent;
    return true;
  }
  for (size_t i = 0; i < c.meshes.size(); ++i) {
    if (c.meshes[i].name != name) continue;
    out.current = &c.meshes[i].local;
    out.bind = i < c.bind_mesh_local.size() ? &c.bind_mesh_local[i] : &c.meshes[i].local;
    out.stored_world = &c.meshes[i].world_stored;
    out.constraint = c.meshes[i].constraint;
    out.target = c.meshes[i].target;
    out.preserve_scale = c.meshes[i].preserve_scale;
    out.parent = c.meshes[i].parent;
    return true;
  }
  for (const auto& [proxy_name, proxy] :
       c.attached_prop_transform_proxies) {
    if (proxy_name != name) continue;
    out.current = &proxy.local;
    out.bind = &proxy.bind_local;
    out.parent = proxy.parent;
    return true;
  }
  for (const auto& group : c.groups) {
    if (group.name != name || !group.has_transform) continue;
    out.current = &group.local;
    out.bind = &group.local;
    out.stored_world = &group.world_stored;
    out.constraint = group.constraint;
    out.target = group.target;
    out.preserve_scale = group.preserve_scale;
    out.parent = group.parent;
    return true;
  }
  return false;
}

bool find_runtime_world_override(const Character& c, const std::string& name,
                                 std::array<float, 16>& out) {
  const auto it = c.runtime_world_overrides.find(name);
  if (it == c.runtime_world_overrides.end()) return false;
  out = it->second;
  return true;
}

std::array<float, 3> transform_pos(const Xfm& local,
                                   const std::array<float, 16>& parent_world) {
  const float x = local.pos[0];
  const float y = local.pos[1];
  const float z = local.pos[2];
  return {x * parent_world[0] + y * parent_world[4] +
              z * parent_world[8] + parent_world[12],
          x * parent_world[1] + y * parent_world[5] +
              z * parent_world[9] + parent_world[13],
          x * parent_world[2] + y * parent_world[6] +
              z * parent_world[10] + parent_world[14]};
}

bool source_dynamic_constraint_needs_runtime(uint32_t constraint,
                                             const std::string& target) {
  if (constraint == 9) return target.empty();  // kTargetWorld without target.
  return constraint >= 3 && constraint <= 8;
}

void warn_source_dynamic_constraint_once(const std::string& name,
                                         uint32_t constraint,
                                         const std::string& target) {
  static std::unordered_set<std::string> warned;
  const std::string key = name + "#" + std::to_string(constraint) + "#" + target;
  if (!warned.insert(key).second) return;
  std::fprintf(stderr,
               "[source-xfm-unsupported] name=%s constraint=%u target=%s "
               "runtimeWriteback=0 reason=awaiting-source-dynamic-constraint-port\n",
               name.c_str(), constraint,
               target.empty() ? "<none>" : target.c_str());
}

std::array<float, 16> source_world_for(const Character& c,
                                       const std::string& name,
                                       bool bind_pose,
                                       bool include_runtime_overrides = true,
                                       int depth = 0) {
  if (depth > 128) return identity_mat4();

  std::array<float, 16> override_world{};
  if (!bind_pose && include_runtime_overrides &&
      find_runtime_world_override(c, name, override_world)) {
    return override_world;
  }

  SourceXfm xfm;
  if (!find_source_xfm(c, name, xfm)) return identity_mat4();

  const Xfm& local = bind_pose ? *xfm.bind : *xfm.current;
  std::array<float, 16> local_mat = xfm_to_mat4(local);
  if (xfm.parent.empty()) {
    return local_mat;
  }

  const auto parent_world =
      source_world_for(c, xfm.parent, bind_pose, include_runtime_overrides,
                       depth + 1);
  if (xfm.constraint == 2) {  // kParentWorld
    return parent_world;
  }
  if (xfm.constraint == 1) {  // kLocalRotate
    auto world = local_mat;
    const auto pos = transform_pos(local, parent_world);
    world[12] = pos[0];
    world[13] = pos[1];
    world[14] = pos[2];
    return world;
  }
  auto world = mat4_mul(local_mat, parent_world);
  if (xfm.constraint == 9 && !xfm.target.empty()) {  // kTargetWorld
    world = source_world_for(c, xfm.target, bind_pose, include_runtime_overrides,
                             depth + 1);
  } else if (source_dynamic_constraint_needs_runtime(xfm.constraint,
                                                    xfm.target)) {
    warn_source_dynamic_constraint_once(name, xfm.constraint, xfm.target);
  }
  return world;
}

}  // namespace

SourceRndMeshSetBonePlan source_rndmesh_set_bone_plan(
    const milo_scene::Xfm& mesh_world,
    const milo_scene::Xfm& bone_world,
    bool recompute_offset) {
  SourceRndMeshSetBonePlan plan;
  if (!recompute_offset) return plan;
  plan.recomputed_offset = true;
  mat4_to_xfm(mat4_mul(xfm_to_mat4(mesh_world),
                       affine_inverse(xfm_to_mat4(bone_world))),
              plan.offset);
  return plan;
}

SourceGltfMiloBoneTransformPlan source_gltf_milo_build_bone_transforms(
    const std::vector<SourceGltfMiloChunkJoint>& joints,
    const std::vector<int32_t>& chunk_joint_indices,
    std::array<float, 16> mesh_world_matrix) {
  SourceGltfMiloBoneTransformPlan plan;
  for (int32_t joint_index : chunk_joint_indices) {
    if (joint_index < 0 || static_cast<size_t>(joint_index) >= joints.size()) {
      continue;
    }
    const SourceGltfMiloChunkJoint& joint =
        joints[static_cast<size_t>(joint_index)];

    SourceGltfMiloBoneTransform transform;
    transform.name =
        joint.name.empty() ? "joint_" + std::to_string(joint_index) : joint.name;
    transform.used_identity_for_noninvertible_joint =
        !affine_invertible(joint.world_matrix);
    const std::array<float, 16> bone_world_inverse =
        transform.used_identity_for_noninvertible_joint
            ? identity_mat4()
            : affine_inverse(joint.world_matrix);
    transform.transform = mat4_mul(bone_world_inverse, mesh_world_matrix);
    plan.bone_transforms.push_back(transform);
  }
  return plan;
}

SourceGltfMiloMatrixHelpersBoundary
source_gltf_milo_matrix_helpers_boundary() {
  SourceGltfMiloMatrixHelpersBoundary boundary;
  boundary.copy_matrix_call_sites = {
      "CreateBaseMesh mesh.trans.localXfm",
      "CreateBaseMesh mesh.trans.worldXfm",
      "PopulateMeshChunk boneWorldInverse * node.WorldMatrix",
      "ProcessBoneNode trans.localXfm",
      "ProcessBoneNode trans.worldXfm",
      "ProcessGroupNode group.trans.localXfm",
      "ProcessGroupNode group.trans.worldXfm",
      "ProcessLightNode light.trans.localXfm",
      "ProcessLightNode light.trans.worldXfm",
      "ProcessCharHair strand.baseMat",
      "ProcessCharHair strand.rootMat",
      "ProcessEmptyHairCollides collide.trans.localXfm",
      "ProcessEmptyHairCollides collide.trans.worldXfm"};
  return boundary;
}

SourceGltfMiloCoordinateConversionPlan
source_gltf_milo_coordinate_conversion_plan(
    const std::array<float, 3>& vector,
    const std::array<float, 4>& quaternion,
    const std::array<float, 3>& scale) {
  SourceGltfMiloCoordinateConversionPlan plan;
  plan.input_vector = vector;
  plan.milo_vector = {vector[0], -vector[2], vector[1]};
  plan.input_quaternion = quaternion;
  plan.milo_quaternion = {quaternion[0], -quaternion[2], quaternion[1],
                          quaternion[3]};
  plan.input_scale = scale;
  plan.milo_scale = {scale[0], scale[2], scale[1]};
  return plan;
}

SourceGltfMiloNodeHelpersBoundary
source_gltf_milo_node_helpers_boundary() {
  SourceGltfMiloNodeHelpersBoundary boundary;
  boundary.traversal_call_sites = {
      "Program Run NodeHelpers.IsPrimitive",
      "Program Run NodeHelpers.IsBone",
      "Program Run NodeHelpers.IsGroupNode",
      "Program Run NodeHelpers.IsLightNode",
      "NodeProcessor IsHairBoneNode NodeHelpers.IsBone",
      "NodeProcessor WarnAboutNonHairChildBones NodeHelpers.IsBone"};
  boundary.parent_call_sites = {
      "ProcessBoneNode NodeHelpers.GetParentBoneName",
      "ProcessGroupNode NodeHelpers.GetAllDescendantNames",
      "ProcessCharHair root NodeHelpers.GetParentNode",
      "ProcessCharHair strand NodeHelpers.GetParentNode"};
  return boundary;
}

SourceGltfMiloMiloExtrasBoundary
source_gltf_milo_milo_extras_boundary() {
  SourceGltfMiloMiloExtrasBoundary boundary;
  boundary.call_sites = {
      "Program mesh MiloExtras.AddToMesh",
      "Program mesh JsonSerializer.Deserialize<MiloExtras>",
      "Program mesh MiloExtras.ObjectType",
      "ProcessGroupNode MiloExtras.AddToGroup",
      "ProcessLightNode MiloExtras.AddToObject"};
  return boundary;
}

SourceGltfMiloMiloExtrasApplyPlan source_gltf_milo_milo_extras_apply_plan(
    const SourceGltfMiloMiloExtrasInput& input,
    bool drawable_target) {
  SourceGltfMiloMiloExtrasApplyPlan plan;
  plan.reads_node_extras = input.node_extras_present;
  if (!plan.reads_node_extras) return plan;

  if (!input.deserialize_succeeds) {
    plan.warns_deserialize_failed = true;
    return plan;
  }

  if (!input.filename.empty()) {
    plan.overrides_filename = true;
    plan.filename = input.filename;
  }
  if (!input.object_type.empty()) {
    plan.writes_object_type = true;
    plan.object_type = input.object_type;
  }
  if (!input.note.empty()) {
    plan.writes_note = true;
    plan.note = input.note;
  }

  plan.writes_drawable_fields = drawable_target;
  if (drawable_target) {
    plan.showing = input.is_showing == 1;
    plan.draw_order = input.draw_order;
    plan.sphere_radius = input.sphere_radius;
    plan.sphere_center = input.sphere_center;
  }
  return plan;
}

SourceGltfMiloGameRevisionsBoundary
source_gltf_milo_game_revisions_boundary() {
  SourceGltfMiloGameRevisionsBoundary boundary;
  boundary.revision_call_sites = {
      "Program CreateBaseMesh ModelRevision",
      "Program Run MiloRevision",
      "Program TransAnim AnimatableRevision",
      "Program TransAnim DrawableRevision",
      "Program material MatRevision",
      "Program texture TextureRevision",
      "Program venue group GroupRevision",
      "Program venue group TransRevision",
      "Program venue group DrawableRevision",
      "Program venue group AnimatableRevision",
      "ProcessGroupNode GroupRevision",
      "ProcessGroupNode TransRevision",
      "ProcessGroupNode DrawableRevision",
      "ProcessGroupNode AnimatableRevision",
      "ProcessLightNode LightRevision",
      "ProcessLightNode TransRevision",
      "ProcessEmptyHairCollides TransRevision"};
  return boundary;
}

SourceGltfMiloDirectoryBuilderBoundary
source_gltf_milo_directory_builder_boundary() {
  SourceGltfMiloDirectoryBuilderBoundary boundary;
  boundary.finalizer_call_sites = {
      "Program finalizer OutfitConfigBuilder.BuildOutfitConfig",
      "Program finalizer DirBuilder.BuildCharacterDirectory",
      "Program finalizer DirBuilder.BuildRndDirectory",
      "Program finalizer MiloFile.Save uncompressed 0x810"};
  return boundary;
}

SourceGltfMiloCharacterDirectoryPlan
source_gltf_milo_character_directory_plan(
    const SourceGltfMiloCharacterDirectoryInput& input) {
  SourceGltfMiloCharacterDirectoryPlan plan;

  const auto apply_rb3_revisions = [&]() {
    plan.character_revision = 17;
    plan.character_testing_revision = 15;
    plan.animatable_revision = 4;
    plan.drawable_revision = 3;
    plan.trans_revision = 9;
    plan.rnd_dir_revision = 10;
    plan.object_dir_revision = 27;
  };

  switch (input.game) {
    case SourceGltfMiloGame::kRockBand3:
    case SourceGltfMiloGame::kDanceCentral1:
      apply_rb3_revisions();
      break;
    case SourceGltfMiloGame::kTheBeatlesRockBand:
      plan.character_revision = 15;
      plan.character_testing_revision = 10;
      plan.animatable_revision = 4;
      plan.drawable_revision = 3;
      plan.trans_revision = 9;
      plan.rnd_dir_revision = 10;
      plan.object_dir_revision = 22;
      break;
    case SourceGltfMiloGame::kRockBand2:
      plan.character_revision = 12;
      plan.character_testing_revision = 8;
      plan.animatable_revision = 4;
      plan.drawable_revision = 3;
      plan.trans_revision = 9;
      plan.rnd_dir_revision = 10;
      plan.object_dir_revision = 20;
      break;
    default:
      break;
  }

  plan.inline_proxy = input.raw_type == "instrument";
  if ((input.game == SourceGltfMiloGame::kRockBand3 ||
       input.game == SourceGltfMiloGame::kDanceCentral1) &&
      input.raw_type == "character") {
    plan.sub_dirs.push_back("../../shared/char_shared.milo");
  }
  plan.sphere_base = input.meta_name;
  return plan;
}

SourceRndMeshScaleBonesPlan source_rndmesh_scale_bones(
    std::vector<milo_scene::Xfm> offsets,
    float scale) {
  SourceRndMeshScaleBonesPlan plan;
  plan.scaled = true;
  plan.scale = scale;
  plan.offsets = offsets;
  for (milo_scene::Xfm& offset : plan.offsets) {
    offset.pos[0] *= scale;
    offset.pos[1] *= scale;
    offset.pos[2] *= scale;
  }
  return plan;
}

SourceRndMeshCopyBonesPlan source_rndmesh_copy_bones(
    const std::vector<std::string>* source_bones) {
  SourceRndMeshCopyBonesPlan plan;
  if (source_bones != nullptr) {
    plan.copied = true;
    plan.bones = *source_bones;
  } else {
    plan.cleared = true;
    plan.bones.clear();
  }
  return plan;
}

SourceRndMeshCopyGeometryFromOwnerPlan
source_rndmesh_copy_geometry_from_owner(bool owner_is_self) {
  SourceRndMeshCopyGeometryFromOwnerPlan plan;
  plan.owner_is_self = owner_is_self;
  if (!owner_is_self) {
    plan.copied_geometry = true;
    plan.copy_with_volume = true;
    plan.sync = true;
    plan.sync_mask = 0x3f;
  }
  return plan;
}

SourceRndMeshSetGeomOwnerPlan source_rndmesh_set_geom_owner_plan(
    bool owner_present) {
  SourceRndMeshSetGeomOwnerPlan plan;
  plan.owner_present = owner_present;
  if (owner_present) {
    plan.assigned_geom_owner = true;
  } else {
    plan.assertion_would_fail = true;
  }
  return plan;
}

SourceRndMeshCopyGeometryPlan source_rndmesh_copy_geometry_plan(
    int32_t owner_vert_count,
    int32_t owner_face_count,
    int32_t owner_patch_count,
    int32_t owner_volume,
    std::vector<std::string> mesh_bones,
    bool copy_volume) {
  SourceRndMeshCopyGeometryPlan plan;
  plan.copied_vert_count = owner_vert_count;
  plan.copied_face_count = owner_face_count;
  plan.copied_patch_count = owner_patch_count;
  plan.copied_bones = mesh_bones;
  plan.copied_volume = copy_volume;
  if (copy_volume) {
    plan.copied_volume_value = owner_volume;
  }
  return plan;
}

SourceRndMeshReplacePlan source_rndmesh_replace_plan(
    bool geom_owner_matches_from,
    bool to_is_mesh) {
  SourceRndMeshReplacePlan plan;
  plan.geom_owner_matches_from = geom_owner_matches_from;
  plan.to_is_mesh = to_is_mesh;
  if (geom_owner_matches_from) {
    plan.changed_geom_owner = true;
    if (to_is_mesh) {
      plan.new_owner_from_to_geom_owner = true;
    } else {
      plan.new_owner_is_self = true;
    }
  }
  return plan;
}

SourceRndMeshCopyPlan source_rndmesh_copy_plan(
    bool copy_shallow,
    bool copy_from_max,
    bool source_geom_owner_is_self) {
  SourceRndMeshCopyPlan plan;
  plan.copies_keep_mesh_data = !copy_from_max;
  plan.ors_mutable = copy_from_max;
  plan.copies_mutable = !copy_from_max;
  if (copy_shallow || (copy_from_max && !source_geom_owner_is_self)) {
    plan.copies_geom_owner = true;
    plan.copies_bones = true;
  } else {
    plan.copies_geometry = true;
    plan.copy_geometry_with_volume = !copy_from_max;
    plan.copies_has_ao_calc = !copy_from_max;
  }
  return plan;
}

static std::string source_milo_symbol_base(const std::string& symbol) {
  std::string base = source_ascii_lower(symbol);
  if (source_case_ends_with(base, ".mesh")) {
    base.resize(base.size() - 5);
  } else if (source_case_ends_with(base, ".trans")) {
    base.resize(base.size() - 6);
  }
  return base;
}

static bool source_milo_symbol_matches(const std::string& a,
                                       const std::string& b) {
  if (a.empty() || b.empty()) return false;
  return source_milo_symbol_base(a) == source_milo_symbol_base(b);
}

bool character_mesh_uses_char_hair_point_bone(const Character& character,
                                              const SkinnedMesh& mesh) {
  auto mesh_matches = [&](const std::string& point_bone) {
    if (source_milo_symbol_matches(point_bone, mesh.name) ||
        source_milo_symbol_matches(point_bone, mesh.parent)) {
      return true;
    }
    for (const std::string& palette_bone : mesh.bone_palette) {
      if (source_milo_symbol_matches(point_bone, palette_bone)) {
        return true;
      }
    }
    return false;
  };

  for (const CharHair& hair : character.hairs) {
    for (const CharHairStrand& strand : hair.strands) {
      for (const CharHairPoint& point : strand.points) {
        if (mesh_matches(point.bone)) return true;
      }
    }
  }
  return false;
}

std::vector<std::string> Character::texture_names() const {
  std::set<std::string> set;
  for (const milo_scene::MatObj& m : mats)
    if (!m.diffuse_tex.empty()) set.insert(m.diffuse_tex);
  return {set.begin(), set.end()};
}

const milo_scene::MatObj* Character::find_mat(const std::string& name) const {
  for (const milo_scene::MatObj& m : mats)
    if (m.name == name) return &m;
  return nullptr;
}

std::array<float, 16> Character::bone_world(const std::string& bone_name) const {
  return source_world_for(*this, bone_name, false);
}

std::array<float, 16> Character::bone_world_bind(const std::string& bone_name) const {
  return source_world_for(*this, bone_name, true, false);
}

std::array<float, 16> Character::bone_world_local_chain(const std::string& bone_name) const {
  return source_world_for(*this, bone_name, false);
}

std::array<float, 16> Character::bone_world_local_chain_authored(const std::string& bone_name) const {
  return source_world_for(*this, bone_name, false, false);
}

std::array<float, 16> Character::bone_world_bind_local_chain(const std::string& bone_name) const {
  return source_world_for(*this, bone_name, true, false);
}

std::array<float, 16> Character::mesh_world(const SkinnedMesh& m) const {
  return source_world_for(*this, m.name, false);
}

std::array<float, 16> Character::model_space_parent_delta(
    const std::string& parent) const {
  const auto bind = bone_world_bind_local_chain(parent);
  const auto current = bone_world_local_chain(parent);
  return mat4_mul(affine_inverse(bind), current);
}

std::array<float, 16> Character::attachment_parent_world(
    const std::string& parent) const {
  return mat4_mul(bone_world_bind(parent), model_space_parent_delta(parent));
}

std::array<float, 16> Character::mesh_attachment_world(
    const SkinnedMesh& m, bool bind_local) const {
  const Xfm* local = &m.local;
  if (bind_local) {
    for (size_t i = 0; i < meshes.size(); ++i) {
      if (meshes[i].name == m.name && i < bind_mesh_local.size()) {
        local = &bind_mesh_local[i];
        break;
      }
    }
  }
  return mat4_mul(xfm_to_mat4(*local), attachment_parent_world(m.parent));
}

bool has_mesh_local_bind_space(const Character& character,
                               const SkinnedMesh& mesh) {
  if (!mesh.decoded || mesh.bone_palette.empty() ||
      mesh.bind.size() < mesh.bone_palette.size()) {
    return false;
  }
  const auto mesh_bind = character.bone_world_bind_local_chain(mesh.name);
  const auto identity = identity_mat4();
  float model_error = 0.0f;
  float mesh_error = 0.0f;
  auto max_delta = [](const std::array<float, 16>& a,
                      const std::array<float, 16>& b) {
    float result = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
      result = std::max(result, std::fabs(a[i] - b[i]));
    }
    return result;
  };
  for (size_t i = 0; i < mesh.bone_palette.size(); ++i) {
    const auto product = mat4_mul(
        xfm_to_mat4(mesh.bind[i]),
        character.bone_world_bind_local_chain(mesh.bone_palette[i]));
    model_error = std::max(model_error, max_delta(product, identity));
    mesh_error = std::max(mesh_error, max_delta(product, mesh_bind));
  }
  constexpr float kMeshClose = 0.05f;
  constexpr float kNontrivialModelOffset = 1.0f;
  constexpr float kErrorRatio = 8.0f;
  return mesh_error <= kMeshClose && model_error > kNontrivialModelOffset &&
         model_error > mesh_error * kErrorRatio;
}

bool Character::has_transform(const std::string& name) const {
  SourceXfm xfm;
  return find_source_xfm(*this, name, xfm);
}

RndMorph decode_rnd_morph(const std::string& entry_name,
                          const std::vector<uint8_t>& body) {
  try {
    return decode_rnd_morph_body(entry_name, body);
  } catch (const std::exception& ex) {
    RndMorph morph;
    morph.name = entry_name;
    morph.error = ex.what();
    return morph;
  }
}

bool apply_rnd_morph_frame(Character& character,
                           std::string_view morph_name,
                           float frame) {
  const auto morph_it = std::find_if(
      character.morphs.begin(), character.morphs.end(),
      [&](const RndMorph& morph) {
        return source_ascii_lower(morph.name) ==
               source_ascii_lower(std::string(morph_name));
      });
  if (morph_it == character.morphs.end() || !morph_it->decoded ||
      morph_it->poses.empty()) {
    return false;
  }
  const RndMorph& morph = *morph_it;
  std::string target_name = morph.target;
  if (target_name.empty()) {
    const size_t dot = morph.name.find_last_of('.');
    target_name = morph.name.substr(0, dot) + ".mesh";
  }
  auto target = std::find_if(
      character.meshes.begin(), character.meshes.end(),
      [&](const SkinnedMesh& mesh) {
        return source_ascii_lower(mesh.name) ==
               source_ascii_lower(target_name);
      });
  if (target == character.meshes.end() || target->verts.empty()) return false;

  auto weight_at = [&](const RndMorphPose& pose) {
    if (pose.keys.empty()) return 0.0f;
    if (frame <= pose.keys.front().frame) return pose.keys.front().weight;
    for (size_t i = 1; i < pose.keys.size(); ++i) {
      if (frame > pose.keys[i].frame) continue;
      const RndMorphKey& a = pose.keys[i - 1];
      const RndMorphKey& b = pose.keys[i];
      const float span = b.frame - a.frame;
      const float t = std::abs(span) > 1.0e-6f
                          ? (frame - a.frame) / span
                          : 0.0f;
      return a.weight + (b.weight - a.weight) *
                            std::clamp(t, 0.0f, 1.0f);
    }
    return pose.keys.back().weight;
  };

  std::vector<SkinVertex> blended(target->verts.size());
  float total_weight = 0.0f;
  bool initialized = false;
  for (const RndMorphPose& pose : morph.poses) {
    auto source = std::find_if(
        character.meshes.begin(), character.meshes.end(),
        [&](const SkinnedMesh& mesh) {
          return source_ascii_lower(mesh.name) ==
                 source_ascii_lower(pose.mesh);
        });
    if (source == character.meshes.end()) continue;
    source->showing = false;
    if (source->verts.size() != target->verts.size()) continue;
    const float weight = weight_at(pose) * morph.intensity;
    if (weight <= 0.0f) continue;
    if (!initialized) {
      blended = source->verts;
      for (SkinVertex& vertex : blended) {
        vertex.px *= weight;
        vertex.py *= weight;
        vertex.pz *= weight;
        if (morph.normals) {
          vertex.nx *= weight;
          vertex.ny *= weight;
          vertex.nz *= weight;
        }
      }
      initialized = true;
    } else {
      for (size_t i = 0; i < blended.size(); ++i) {
        blended[i].px += source->verts[i].px * weight;
        blended[i].py += source->verts[i].py * weight;
        blended[i].pz += source->verts[i].pz * weight;
        if (morph.normals) {
          blended[i].nx += source->verts[i].nx * weight;
          blended[i].ny += source->verts[i].ny * weight;
          blended[i].nz += source->verts[i].nz * weight;
        }
      }
    }
    total_weight += weight;
  }
  if (!initialized || total_weight <= 1.0e-6f) return false;
  for (size_t i = 0; i < blended.size(); ++i) {
    target->verts[i].px = blended[i].px / total_weight;
    target->verts[i].py = blended[i].py / total_weight;
    target->verts[i].pz = blended[i].pz / total_weight;
    if (morph.normals) {
      target->verts[i].nx = blended[i].nx / total_weight;
      target->verts[i].ny = blended[i].ny / total_weight;
      target->verts[i].nz = blended[i].nz / total_weight;
    }
  }
  target->bb_min[0] = target->bb_max[0] = target->verts[0].px;
  target->bb_min[1] = target->bb_max[1] = target->verts[0].py;
  target->bb_min[2] = target->bb_max[2] = target->verts[0].pz;
  for (const SkinVertex& vertex : target->verts) {
    target->bb_min[0] = std::min(target->bb_min[0], vertex.px);
    target->bb_min[1] = std::min(target->bb_min[1], vertex.py);
    target->bb_min[2] = std::min(target->bb_min[2], vertex.pz);
    target->bb_max[0] = std::max(target->bb_max[0], vertex.px);
    target->bb_max[1] = std::max(target->bb_max[1], vertex.py);
    target->bb_max[2] = std::max(target->bb_max[2], vertex.pz);
  }
  target->showing = true;
  return true;
}

std::optional<float> rnd_morph_pose_peak_frame(
    const Character& character, std::string_view morph_name,
    std::string_view pose_name) {
  const std::string wanted_morph = source_ascii_lower(std::string(morph_name));
  const std::string wanted_pose = source_ascii_lower(std::string(pose_name));
  const auto morph_it = std::find_if(
      character.morphs.begin(), character.morphs.end(),
      [&](const RndMorph& morph) {
        return morph.decoded &&
               source_ascii_lower(morph.name) == wanted_morph;
      });
  if (morph_it == character.morphs.end()) return std::nullopt;
  for (const RndMorphPose& pose : morph_it->poses) {
    std::string mesh_name = source_ascii_lower(pose.mesh);
    const size_t slash = mesh_name.find_last_of("/\\");
    if (slash != std::string::npos) mesh_name.erase(0, slash + 1);
    const size_t dot = mesh_name.find_last_of('.');
    if (dot != std::string::npos) mesh_name.erase(dot);
    if (mesh_name != wanted_pose || pose.keys.empty()) continue;
    const auto peak = std::max_element(
        pose.keys.begin(), pose.keys.end(),
        [](const RndMorphKey& a, const RndMorphKey& b) {
          return a.weight < b.weight;
        });
    return peak->frame;
  }
  return std::nullopt;
}

bool load_character(const std::string& hdr_path, const std::string& ark_path,
                    const std::string& milo_path, Character& out) {
  try {
    std::vector<uint8_t> bytes;
    if (hdr_path.empty()) {
      bytes = read_binary_file(milo_path);
    } else {
      auto ark = gh::ark::ArkV3Reader::load(hdr_path);
      auto entry = ark.find(milo_path);
      if (!entry) entry = ark.find("../../system/run/" + milo_path);
      if (!entry) {
        std::fprintf(stderr, "[char] not in ARK: %s\n", milo_path.c_str());
        return false;
      }
      bytes = ark.read_entry(*entry, {ark_path});
    }
    auto hdr = gh::milo::parse_header(bytes);
    auto payload = gh::milo::inflate_payload(bytes, hdr);
    auto dir = gh::milo::parse_directory(payload);
    out.dir_name = dir.dir_name;
    out.dir_type = dir.dir_type;
    out.dir_version = dir.dir_version;
    out.dir_entry_offset = dir.dir_entry_offset;
    out.dir_entry_size = dir.dir_entry_size;
    out.dir_entry_bytes.clear();
    if (dir.dir_entry_offset <= payload.size() &&
        dir.dir_entry_size <= payload.size() - dir.dir_entry_offset) {
      const auto begin =
          payload.begin() + static_cast<std::ptrdiff_t>(dir.dir_entry_offset);
      out.dir_entry_bytes.assign(
          begin, begin + static_cast<std::ptrdiff_t>(dir.dir_entry_size));
    }
    decode_character_root(out);

    int mesh_ok = 0, mesh_fail = 0;
    for (const auto& de : dir.entries) {
      ++out.object_type_counts[de.type];
      std::vector<uint8_t> b(payload.data() + de.offset,
                             payload.data() + de.offset + de.size);
      try {
        bool handled = false;
        if (de.type == "Mesh") {
          handled = true;
          SkinnedMesh m = decode_skinned_mesh(de.name, b, dir.dir_version);
          if (m.decoded) ++mesh_ok; else ++mesh_fail;
          out.meshes.push_back(std::move(m));
        } else if (de.type == "Trans") {
          handled = true;
          out.bones.push_back(milo_scene::decode_trans(de.name, b,
                                                       dir.dir_version));
        } else if (de.type == "Mat") {
          handled = true;
          out.mats.push_back(milo_scene::decode_mat(de.name, b));
        } else if (de.type == "Group" ||
                   (dir.dir_version == 10 && de.type == "View")) {
          handled = true;
          milo_scene::GroupObj group =
              milo_scene::decode_group(de.name, b, dir.dir_version);
          if (!group.decoded) {
            std::fprintf(stderr, "[char]   Group '%s' decode: %s\n",
                         de.name.c_str(), group.error.c_str());
          }
          out.groups.push_back(std::move(group));
        } else if (de.type == "Morph") {
          handled = true;
          RndMorph morph = decode_rnd_morph(de.name, b);
          if (!morph.decoded) {
            std::fprintf(stderr, "[char]   Morph '%s' decode: %s\n",
                         de.name.c_str(), morph.error.c_str());
          }
          out.morphs.push_back(std::move(morph));
        } else if (de.type == "CharUpperTwist") {
          handled = true;
          out.upper_twists.push_back(decode_upper_twist(de.name, b));
        } else if (de.type == "CharForeTwist") {
          handled = true;
          out.fore_twists.push_back(decode_fore_twist(de.name, b));
        } else if (de.type == "CharNeckTwist") {
          handled = true;
          out.neck_twists.push_back(decode_neck_twist(de.name, b));
        } else if (de.type == "CharIKRod") {
          handled = true;
          out.ik_rods.push_back(decode_ik_rod(de.name, b));
        } else if (de.type == "CharIKHand") {
          handled = true;
          out.ik_hands.push_back(decode_ik_hand(de.name, b));
        } else if (de.type == "CharIKMidi") {
          handled = true;
          out.ik_midis.push_back(decode_ik_midi(de.name, b));
        } else if (de.type == "CharServoBone") {
          handled = true;
          out.servo_bones.push_back(decode_servo_bone(de.name, b));
        } else if (de.type == "CharLookAt") {
          handled = true;
          out.lookats.push_back(decode_lookat(de.name, b));
        } else if (de.type == "CharEyes") {
          handled = true;
          out.eyes.push_back(decode_eyes(de.name, b));
        } else if (de.type == "CharHair") {
          handled = true;
          out.hairs.push_back(decode_hair(de.name, b));
        } else if (de.type == "CharCollide") {
          handled = true;
          out.collides.push_back(decode_collide(de.name, b, dir.dir_version));
        } else if (de.type == "CharPosConstraint") {
          handled = true;
          out.pos_constraints.push_back(decode_pos_constraint(de.name, b));
        } else if (de.type == "CharBoneOffset") {
          handled = true;
          out.bone_offsets.push_back(decode_bone_offset(de.name, b));
        } else if (de.type == "CharBoneTwist") {
          handled = true;
          out.bone_twists.push_back(decode_bone_twist(de.name, b));
        } else if (de.type == "FaceFxLipSyncServo") {
          handled = true;
          out.lip_sync_servos.push_back(decode_lip_sync_servo(de.name, b));
        } else if (de.type == "AnimFilter") {
          handled = true;
          out.anim_filters.push_back(decode_anim_filter(de.name, b));
        } else if (de.type == "EventTrigger") {
          handled = true;
          out.event_triggers.push_back(decode_event_trigger(de.name, b));
        } else if (de.type == "Object") {
          handled = true;
          out.object_rows.push_back(decode_object_row(de.name, b));
        } else if (de.type == "OutfitLoader") {
          handled = true;
          OutfitLoader outfit_loader =
              decode_outfit_loader(de.name, b);
          if (!outfit_loader.decoded) {
            std::fprintf(
                stderr, "[char]   OutfitLoader '%s' decode: %s\n",
                de.name.c_str(), outfit_loader.error.c_str());
          }
          out.outfit_loaders.push_back(std::move(outfit_loader));
        } else if (de.type == "CharWalk") {
          handled = true;
          CharWalk char_walk = decode_char_walk(de.name, b);
          if (!char_walk.decoded) {
            std::fprintf(stderr, "[char]   CharWalk '%s' decode: %s\n",
                         de.name.c_str(), char_walk.error.c_str());
          }
          out.char_walks.push_back(std::move(char_walk));
        } else if (de.type == "WorldFx") {
          handled = true;
          WorldFx world_fx = decode_world_fx(de.name, b);
          if (!world_fx.decoded) {
            std::fprintf(stderr, "[char]   WorldFx '%s' decode: %s\n",
                         de.name.c_str(), world_fx.error.c_str());
          }
          out.world_fxes.push_back(std::move(world_fx));
        } else if (de.type == "CharMeshHide") {
          handled = true;
          out.mesh_hides.push_back(decode_char_mesh_hide(de.name, b));
        } else if (de.type == "Tex") {
          handled = true;
          out.tex_rows.push_back(decode_rnd_tex(de.name, b));
        } else if (de.type == "CharDriver") {
          handled = true;
          out.drivers.push_back(decode_driver(de.name, b));
        } else if (de.type == "CharDriverMidi") {
          handled = true;
          out.drivers.push_back(decode_driver_midi(de.name, b));
        } else if (de.type == "CharWeightSetter") {
          handled = true;
          out.weight_setters.push_back(decode_weight_setter(de.name, b));
        }
        if (!handled) {
          OpaqueObjectRow row;
          row.name = de.name;
          row.type = de.type;
          row.body_bytes = b.size();
          if (!b.empty()) {
            row.head_hex = hex_bytes(b.data(), std::min<size_t>(b.size(), 32));
            const size_t tail_start = b.size() > 32 ? b.size() - 32 : 0;
            row.tail_hex =
                hex_bytes(b.data() + tail_start, b.size() - tail_start);
          }
          out.opaque_rows.push_back(std::move(row));
        }
      } catch (const std::exception& ex) {
        std::fprintf(stderr, "[char]   %s '%s' decode: %s\n", de.type.c_str(),
                     de.name.c_str(), ex.what());
      }
    }
    if (dir.dir_version == 10) {
      // Trans rev8 serializes the old child list before a trailing parent
      // pointer. GH1 character bodies normally store `this` in that trailing
      // slot; SetTransParent ignores it. Rebuild the actual hierarchy from the
      // child lists exactly as the legacy loader does.
      std::map<std::string, size_t> mesh_indices;
      for (size_t i = 0; i < out.meshes.size(); ++i)
        mesh_indices[out.meshes[i].name] = i;
      for (auto& mesh : out.meshes) {
        if (mesh.parent == mesh.name) mesh.parent.clear();
      }
      for (const auto& parent : out.meshes) {
        for (const auto& child_name : parent.legacy_children) {
          const auto child = mesh_indices.find(child_name);
          if (child == mesh_indices.end() || child->second >= out.meshes.size())
            continue;
          out.meshes[child->second].parent = parent.name;
        }
      }
    }

    // RndMesh::Verts/Faces forward exactly one level to mGeomOwner's backing
    // members (they do not recursively call the owner's accessors). Preserve
    // each alias's own Trans, Mat, and bone rows while materializing that
    // direct geometry view for this value-based renderer.
    if (dir.dir_version == 10) {
      std::map<std::string, const SkinnedMesh*> owners;
      for (const auto& mesh : out.meshes) owners[mesh.name] = &mesh;
      struct SharedGeometry {
        size_t index = 0;
        std::vector<SkinVertex> verts;
        std::vector<uint16_t> indices;
        float bb_min[3] = {};
        float bb_max[3] = {};
      };
      std::vector<SharedGeometry> shared;
      for (size_t i = 0; i < out.meshes.size(); ++i) {
        const auto owner = owners.find(out.meshes[i].geometry_owner);
        if (owner == owners.end() || owner->second == &out.meshes[i]) continue;
        SharedGeometry row;
        row.index = i;
        row.verts = owner->second->verts;
        row.indices = owner->second->indices;
        for (int axis = 0; axis < 3; ++axis) {
          row.bb_min[axis] = owner->second->bb_min[axis];
          row.bb_max[axis] = owner->second->bb_max[axis];
        }
        shared.push_back(std::move(row));
      }
      for (auto& row : shared) {
        auto& mesh = out.meshes[row.index];
        mesh.verts = std::move(row.verts);
        mesh.indices = std::move(row.indices);
        for (int axis = 0; axis < 3; ++axis) {
          mesh.bb_min[axis] = row.bb_min[axis];
          mesh.bb_max[axis] = row.bb_max[axis];
        }
      }
    }

    for (SkinnedMesh& mesh : out.meshes) {
      apply_source_rndmesh_active_bones(mesh, &out);
    }
    const size_t showing_meshes = static_cast<size_t>(std::count_if(
        out.meshes.begin(), out.meshes.end(),
        [](const SkinnedMesh& mesh) { return mesh.decoded && mesh.showing; }));
    if (out.root_decoded) {
      std::fprintf(
          stderr,
          "[char-root] %s: type=%s object_type=%s lods=%zu shadow=%s self_shadow=%d sphere_base=%s environment=%s\n",
          milo_path.c_str(), out.dir_type.c_str(),
          out.root_object_type.c_str(), out.root_lods.size(),
          out.root_shadow.c_str(), out.root_self_shadow ? 1 : 0,
          out.root_sphere_base.c_str(), out.root_environment.c_str());
    } else {
      std::fprintf(stderr, "[char-root] %s: decode-failed: %s\n",
                   milo_path.c_str(), out.root_decode_error.c_str());
    }
    const char* mesh_hide_audit =
        std::getenv("GHOGX_AUDIT_CHAR_MESH_HIDE");
    if (mesh_hide_audit && *mesh_hide_audit &&
        std::strcmp(mesh_hide_audit, "0") != 0) {
      for (const auto& hide : out.mesh_hides) {
        std::fprintf(
            stderr,
            "[char-mesh-hide] %s:%s rev=%d alt=%d flags=0x%08x rows=%zu unread=%zu decoded=%d error=%s\n",
            milo_path.c_str(), hide.name.c_str(), hide.revision,
            hide.alt_revision, static_cast<uint32_t>(hide.flags),
            hide.hides.size(), hide.unread_bytes, hide.decoded ? 1 : 0,
            hide.error.c_str());
        for (const auto& row : hide.hides) {
          std::fprintf(
              stderr,
              "[char-mesh-hide-row] owner=%s drawable=%s flags=0x%08x show=%d\n",
              hide.name.c_str(), row.drawable.c_str(),
              static_cast<uint32_t>(row.flags), row.show ? 1 : 0);
        }
      }
    }
    std::fprintf(stderr,
                 "[char] %s: %zu meshes (%d ok / %d fail, %zu showing), %zu bones, %zu mat, "
                 "%zu group, %zu morph, %zu upperTwist, %zu foreTwist, %zu neckTwist, %zu ikRod, %zu ikHand, %zu ikMidi, "
                 "%zu servoBone, %zu lookAt, %zu eyes, %zu hair, %zu collide, "
                 "%zu posConstraint, %zu boneOffset, %zu boneTwist, %zu lipServo, %zu animFilter, "
                 "%zu eventTrigger, %zu object, %zu outfitLoader, %zu charWalk, %zu worldFx, %zu meshHide, %zu tex, %zu driver, "
                 "%zu weightSetter, %zu opaque\n",
                 milo_path.c_str(), out.meshes.size(), mesh_ok, mesh_fail,
                 showing_meshes,
                 out.bones.size(), out.mats.size(), out.groups.size(),
                 out.morphs.size(),
                 out.upper_twists.size(), out.fore_twists.size(),
                 out.neck_twists.size(), out.ik_rods.size(),
                 out.ik_hands.size(), out.ik_midis.size(),
                 out.servo_bones.size(), out.lookats.size(), out.eyes.size(),
                 out.hairs.size(), out.collides.size(),
                 out.pos_constraints.size(),
                 out.bone_offsets.size(),
                 out.bone_twists.size(),
                 out.lip_sync_servos.size(), out.anim_filters.size(),
                   out.event_triggers.size(),
                   out.object_rows.size(),
                   out.outfit_loaders.size(),
                   out.char_walks.size(),
                   out.world_fxes.size(),
                  out.mesh_hides.size(),
                  out.tex_rows.size(),
                 out.drivers.size(),
                 out.weight_setters.size(),
                 out.opaque_rows.size());
    out.bind_mesh_local.clear();
    out.bind_mesh_local.reserve(out.meshes.size());
    for (const auto& m : out.meshes) out.bind_mesh_local.push_back(m.local);
    out.bind_bone_local.clear();
    out.bind_bone_local.reserve(out.bones.size());
    for (const auto& b : out.bones) out.bind_bone_local.push_back(b.local);
    for (auto& mesh : out.meshes) {
      mesh.mesh_local_bind_space = has_mesh_local_bind_space(out, mesh);
    }
    return true;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[char] load_character(%s): %s\n", milo_path.c_str(),
                 ex.what());
    return false;
  }
}

}  // namespace ghogx::character
