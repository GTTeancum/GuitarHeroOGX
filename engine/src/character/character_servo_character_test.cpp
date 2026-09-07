#include "character/char_servo_character.h"
#include "milo_object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace ghogx::character;
using ghogx::milo_scene::Xfm;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
  check(std::isfinite(actual) && std::fabs(actual - expected) < 0.0001f, message);
}
template<class F> void rejects(F action, const char* message) {
  try { action(); } catch (const std::runtime_error&) { return; }
  throw std::runtime_error(message);
}
void finite(const Xfm& row) {
  for (int i=0; i<3; ++i) {
    check(std::isfinite(row.pos[i]), "nonfinite published position");
    for (int j=0; j<3; ++j)
      check(std::isfinite(row.rot[i][j]), "nonfinite published basis");
  }
}
void unit_tests() {
  Character character;
  character.root_decoded = true;
  character.root_transform.local.pos[0] = 11;
  character.root_transform.world_stored.pos[0] = 999;
  character.root_transform.parent = "stage_anchor.trans";
  ghogx::milo_scene::TransObj pelvis;
  pelvis.name = "bone_pelvis.trans";
  pelvis.local.pos[2] = 5;
  character.bones.push_back(pelvis);
  SkinnedMesh mesh;
  mesh.name = "bone_extra.mesh";
  character.meshes.push_back(mesh);
  Gh2ClipSetBinding inventory;
  inventory.move_self = true;
  inventory.channels = {{"bone_facing.pos", Gh2BoneChannelType::Pos},
      {"bone_facing.rotz", Gh2BoneChannelType::RotZ},
      {"bone_facing_delta.pos", Gh2BoneChannelType::Pos},
      {"bone_facing_delta.rotz", Gh2BoneChannelType::RotZ},
      {"bone_pelvis.pos", Gh2BoneChannelType::Pos},
      {"bone_extra.pos", Gh2BoneChannelType::Pos},
      {"attached.pos", Gh2BoneChannelType::Pos}};
  SourceCharServoCharacter binding;
  binding.rebind(character, {&inventory});
  near(binding.output().channel("bone_pelvis.pos")[2], 5, "seed current target local");
  bool cleared = false;
  binding.enter([&] { cleared = true; });
  check(cleared && binding.state().move_self, "Enter uses actual allocated facing channel");
  auto& output = binding.output();
  output.channel("bone_facing_delta.pos")[0] = 2;
  *output.channel("bone_facing_delta.rotz") = .25f;
  output.channel("bone_extra.pos")[1] = 7;
  bool correct_owner = false;
  binding.poll([&](Xfm& owner) { correct_owner = &owner == &character.root_transform.local; });
  check(correct_owner, "regulate receives actual Character LOCAL owner");
  near(character.root_transform.local.pos[0], 13, "movement updates Character local once");
  near(character.root_transform.local.rot[0][0], std::cos(.25f), "owner delta angle applied once");
  near(character.root_transform.world_stored.pos[0], 999, "never overwrite stored/composed world");
  check(character.root_transform.parent == "stage_anchor.trans", "keep root parent metadata");
  near(character.meshes[0].local.pos[1], 7, "publish exact mesh target");
  near(output.channel("bone_facing_delta.pos")[0], 0, "clear same buffer after servo");
  binding.poll([](Xfm&) {});
  near(character.root_transform.local.pos[0], 13, "no duplicate locomotion without driver delta");
  // Reallocate is not Enter: do not reset a pending SetMoveSelf transition.
  binding.set_move_self(false);
  character.bones.reserve(character.bones.capacity() + 20);
  rejects([&] { binding.poll([](Xfm&) {}); }, "stale borrowed bone storage was accepted");
  binding.rebind(character, {&inventory});
  check(!binding.state().move_self && binding.state().delta_changed,
        "rebind preserves lifecycle state");
  character.attached_prop_transform_proxies["attached.mesh"].name = "attached.mesh";
  rejects([&] { binding.output(); }, "new prop silently retained old dummy binding");
  binding.rebind(character, {&inventory});
  binding.output().channel("attached.pos")[2] = 9;
  binding.output().pose_meshes();
  near(character.attached_prop_transform_proxies.at("attached.mesh").local.pos[2], 9,
       "bind attached prop transform without copying it");
  Character missing;
  SourceCharServoCharacter invalid;
  rejects([&] { invalid.rebind(missing, {&inventory}); }, "missing root accepted");
  missing.root_decoded = true;
  rejects([&] { invalid.rebind(missing, {&inventory}); }, "facing with missing pelvis accepted");
  Gh2ClipSetBinding stationary;
  invalid.rebind(missing, {&stationary});
  invalid.enter([] {});
  invalid.poll([](Xfm&) { throw std::runtime_error("empty output must not regulate"); });
  check(!invalid.state().move_self, "stationary allocation does not invent movement");
  std::puts("PASS: live Character root/bone/mesh/prop ownership, stale binding rejection, Enter and rebind lifecycle");
}

void asset_test(const char* hdr, const char* ark, const char* model, const char* bank) {
  Character character;
  check(load_character(hdr, ark, model, character), "source character did not load");
  check(character.root_decoded, "source Character root did not decode");
  const auto root = character.dir_type == "BandCharacter"
      ? gh::milo_object::parse_band_character1(character.dir_entry_bytes).character
      : gh::milo_object::parse_character9(character.dir_entry_bytes);
  const auto& serialized = root.render_directory.transformable;
  const auto compare_root = [&] {
    for (int i=0; i<3; ++i) {
      near(character.root_transform.local.pos[i], serialized.local[9+i], "root local position lost");
      near(character.root_transform.world_stored.pos[i], serialized.world[9+i], "root world position lost");
      for (int j=0; j<3; ++j) {
        near(character.root_transform.local.rot[i][j], serialized.local[3*i+j], "root local basis lost");
        near(character.root_transform.world_stored.rot[i][j], serialized.world[3*i+j], "root world basis lost");
      }
    }
    check(character.root_transform.parent == serialized.parent &&
          character.root_transform.target == serialized.target &&
          character.root_transform.constraint == serialized.constraint &&
          character.root_transform.preserve_scale == serialized.preserve_scale,
          "root transform metadata lost");
  };
  compare_root();
  const auto inventory = load_gh2_clip_set_binding(hdr, ark, bank);
  const auto catalog = load_clip_catalog(hdr, ark, {bank});
  check(!catalog.empty(), "source animation catalog empty");
  const auto initial = character;
  size_t clips=0, frames=0, target_rows=0, dummy_rows=0;
  double max_owner_step=0;
  for (const auto& entry : catalog) {
    const auto clip = load_clip(hdr, ark, bank, entry.name);
    check(clip.loaded && clip.gh2_pose_samples, "clip original samples unavailable");
    character = initial;
    SourceCharServoCharacter binding;
    binding.rebind(character, {inventory.get()});
    binding.enter([] {});
    if (clips == 0) {
      target_rows = binding.output().rows().size();
      for (const auto& row : binding.output().rows()) dummy_rows += row.fallback;
    }
    CharClipPlayer player;
    player.play(clip, kCharPlayNoBlend | kCharPlayNoLoop, 0);
    // Actual driver clock and publication, not expanded-pose sampling. All
    // active phases of each bank clip, bounded to 240 steps per clip. No
    // IK/walk/controller or retail-animation parity claim from this test.
    const float span = std::max(.01f, clip.end_beat - clip.start_beat);
    const float step = span / 240.0f;
    auto previous = character.root_transform.local;
    for (int frame=0; frame<=240; ++frame) {
      player.advance_source(frame*step, frame == 0 ? 0.0f : step, frame == 0 ? 0.0f : step*.5f);
      player.accumulate_source_pose(binding.output());
      binding.poll([](Xfm&) {});
      finite(character.root_transform.local);
      for (const auto& bone : character.bones) finite(bone.local);
      for (const auto& mesh : character.meshes) finite(mesh.local);
      const auto& current = character.root_transform.local;
      double distance=0;
      for (int axis=0; axis<3; ++axis) {
        const double d=current.pos[axis]-previous.pos[axis]; distance+=d*d;
      }
      max_owner_step=std::max(max_owner_step, std::sqrt(distance));
      previous=current;
      if (const float* delta=binding.output().channel("bone_facing_delta.pos")) {
        for (int axis=0; axis<3; ++axis) near(delta[axis], 0, "facing delta not consumed");
        near(*binding.output().channel("bone_facing_delta.rotz"), 0, "angular delta not consumed");
      }
      ++frames;
    }
    ++clips;
  }
  std::printf("ASSET\t%s\t%s\t%zu\t%zu\t%zu\t%zu\t%.9g\n",
              model, bank, clips, frames, target_rows, dummy_rows, max_owner_step);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    unit_tests();
    if (argc == 6 && std::string(argv[1]) == "--asset")
      asset_test(argv[2], argv[3], argv[4], argv[5]);
    else if (argc != 1) throw std::runtime_error("usage: test [--asset hdr ark model bank]");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
  }
}
