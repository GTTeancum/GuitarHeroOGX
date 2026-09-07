#include "character/char_clip_binding.h"
#include "character/char_bones_output.h"
#include "character/char_clip.h"
#include "milo.h"
#include "milo_object.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace ghogx::character;
namespace {
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
template<class F> void rejects(F action, const char* message) {
  try { action(); } catch (const std::runtime_error&) { return; }
  check(false, message);
}

gh::milo::Directory fixture(bool moves) {
  gh::milo::Directory dir;
  dir.dir_type = "CharClipSet";
  dir.dir_name = "arbitrary_addon";
  gh::milo_object::CharClipSet14 root;
  root.move_self = moves;
  root.clips = {{"idle.clip", 0, 123}, {"playing.clip", 0, 456}};
  dir.dir_body_bytes = gh::milo_object::serialize_char_clip_set14(root);
  gh::milo::Entry idle, playing;
  idle.type = "CharClipSamples"; idle.name = "idle.clip";
  playing.type = "CharClipSamples"; playing.name = "playing.clip";
  dir.entries = {idle, playing};
  gh::milo::Entry filter;
  filter.type = "CharClipFilter"; filter.name = "clip_filter.ccf";
  dir.entries.push_back(filter);  // Must not inflate the root's clip count.
  gh::milo_object::CharBone2 bone;
  bone.position_context = true;
  bone.scale_context = true;
  bone.rotation = 2;
  bone.legacy_rotation = 8;
  gh::milo::Entry bone_entry;
  bone_entry.type = "CharBone";
  bone_entry.name = "bone_pelvis.trans";
  bone_entry.body_bytes = gh::milo_object::serialize_char_bone2(bone);
  dir.entries.push_back(bone_entry);
  return dir;
}

void unit_tests() {
  auto dir = fixture(true);
  const auto binding = decode_gh2_clip_set_binding(dir);
  check(binding.revision == 14 && binding.move_self, "authored root flag");
  check(binding.channels.size() == 8, "four facing plus four CharBone channels");
  const std::array<const char*, 8> names{
      "bone_facing.pos", "bone_facing.rotz", "bone_facing_delta.pos",
      "bone_facing_delta.rotz", "bone_pelvis.pos", "bone_pelvis.scale",
      "bone_pelvis.quat", "bone_pelvis.drotz"};
  const std::array<int, 8> types{0, 5, 0, 5, 0, 1, 2, 8};
  for (size_t i = 0; i < names.size(); ++i) {
    check(binding.channels[i].name == names[i], "original ListBones order");
    check(static_cast<int>(binding.channels[i].type) == types[i], "GH2 channel type");
  }
  // A pelvis and playable clip do not imply facing allocation.
  const auto stationary = decode_gh2_clip_set_binding(fixture(false));
  check(!stationary.move_self && stationary.channels.size() == 4,
        "do not infer move_self from skeleton or selected animation");
  // Verify the complete nine-type table, both selectors, and END=9.
  for (int type = 0; type <= 9; ++type) {
    auto single = fixture(false);
    gh::milo_object::CharBone2 bone;
    bone.rotation = type;
    single.entries.back().body_bytes = gh::milo_object::serialize_char_bone2(bone);
    const auto output = decode_gh2_clip_set_binding(single);
    check(output.channels.size() == (type == 9 ? 0u : 1u), "GH2 selector/END");
    if (type != 9)
      check(static_cast<int>(output.channels.front().type) == type,
            "delta rotation is not clamped to later TYPE_END");
    bone.rotation = 9; bone.legacy_rotation = type;
    single.entries.back().body_bytes = gh::milo_object::serialize_char_bone2(bone);
    const auto secondary = decode_gh2_clip_set_binding(single);
    check(secondary.channels.size() == output.channels.size(), "second selector retained");
    if (type != 9)
      check(secondary.channels.front().name == output.channels.front().name,
            "second selector uses same original suffix table");
  }
  auto unknown = fixture(false);
  unknown.dir_body_bytes[0] = 15;
  rejects([&]{ decode_gh2_clip_set_binding(unknown); }, "unsupported root must reject");
  unknown = fixture(false);
  unknown.entries.back().body_bytes[0] = 7;
  rejects([&]{ decode_gh2_clip_set_binding(unknown); }, "unsupported bone must reject");
  unknown = fixture(false);
  unknown.entries[0].name = "not_the_serialized_clip.clip";
  rejects([&]{ decode_gh2_clip_set_binding(unknown); }, "root identity mismatch must reject");
  unknown = fixture(false);
  unknown.dir_body_bytes.pop_back();
  rejects([&]{ decode_gh2_clip_set_binding(unknown); }, "truncated root must reject");
  unknown = fixture(false);
  gh::milo_object::CharBone2 bad;
  bad.rotation = 10;
  unknown.entries.back().body_bytes = gh::milo_object::serialize_char_bone2(bad);
  rejects([&]{ decode_gh2_clip_set_binding(unknown); }, "invalid channel selector must reject");
  unknown = fixture(false);
  auto root = gh::milo_object::parse_char_clip_set14(unknown.dir_body_bytes, 2);
  root.object_directory.subdirectories.push_back("external_bones.milo");
  unknown.dir_body_bytes = gh::milo_object::serialize_char_clip_set14(root);
  const auto with_child = decode_gh2_clip_set_binding(unknown);
  check(with_child.subdirectories == root.object_directory.subdirectories,
        "preserve child references as metadata");
  check(with_child.channels.size() == stationary.channels.size(),
        "original ListBones iterates this directory only, not child resources");
  std::puts("PASS: original GH2 clip-set binding, all nine types, both selectors, strict decoding");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    unit_tests();
    if (argc > 1 && std::string(argv[1]) == "--facing") {
      if (argc != 6) throw std::runtime_error("usage: test --facing hdr ark milo clip");
      const auto clip = load_clip(argv[2], argv[3], argv[4], argv[5]);
      check(clip.loaded && clip.gh2_facing_samples.has_value(), "native GH2 clip facing metadata loaded");
      const auto& facing = *clip.gh2_facing_samples;
      std::printf("FACING\t%s\t%.9g\t%.9g\t%zu\t%zu\n", clip.name.c_str(),
          clip.start_beat, clip.end_beat, facing.positions.size(), facing.rotations.size());
      for (float fraction : {-.25f, 0.0f, .25f, .5f, .75f, 1.0f, 1.25f}) {
        const float beat = clip.start_beat + fraction * (clip.end_beat - clip.start_beat);
        const auto sample = source_gh2_char_clip_facing_sample_at_beat(clip, beat);
        check(sample.has_value(), "source clip has both facing channels");
        std::printf("SAMPLE\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n", beat,
            sample->facing_pos[0], sample->facing_pos[1], sample->facing_pos[2], sample->facing_rot);
      }
      return 0;
    }
    const bool dump_channels = argc > 1 && std::string(argv[1]) == "--channels";
    const bool all_pages = argc > 1 && std::string(argv[1]) == "--all-pages";
    if (dump_channels || all_pages) { --argc; ++argv; }
    if (argc != 1 && argc < 4)
      throw std::runtime_error("usage: test [hdr ark milo ...]; empty hdr reads loose files");
    for (int i = 3; i < argc; ++i) {
      const auto binding = load_gh2_clip_set_binding(argv[1], argv[2], argv[i]);
      const auto cached = load_gh2_clip_set_binding(argv[1], argv[2], argv[i]);
      check(binding == cached, "cache retains directory binding identity");
      const auto clips = load_clip_catalog(argv[1], argv[2], {argv[i]});
      check(!clips.empty(), "asset has a decoded clip catalog");
      if (all_pages) {
        std::size_t tested_clips=0, nonempty_pages=0, tested_samples=0, bytes=0, clip_phases=0;
        ghogx::milo_scene::Xfm dummy;
        SourceCharBonesMeshesOutput output;
        output.reallocate({binding.get()}, {}, &dummy);
        for (const auto& entry : clips) {
          const auto source = load_clip(argv[1], argv[2], argv[i], entry.name);
          if (!source.loaded || !source.gh2_pose_samples)
            throw std::runtime_error("raw GH2 sample pages missing: " + entry.name);
          ++tested_clips;
          for (float phase : {-0.125f,0.0f,0.333f,0.999f,1.0f,1.125f}) {
            output.scale_down_clip(*source.gh2_pose_samples,0.25f);
            output.scale_add_clip(*source.gh2_pose_samples,phase-0.05f,phase,0.75f);
            ++clip_phases;
          }
          for (const auto& page : source.gh2_pose_samples->pages) {
            page.validate();
            bytes += page.disk_samples.size();
            if (page.channels.empty()) continue;
            ++nonempty_pages;
            // Every source sample, all named writes into actual bound output.
            // This is sample transport coverage, not time/pose visual parity.
            for (std::size_t sample=0; sample<page.sample_count; ++sample) {
              output.scale_down(page.channels,0);
              output.scale_add(page,sample,0.625f);
              for (std::size_t cell=0; cell<output.allocated_bytes()/sizeof(float); ++cell)
                if (!std::isfinite(output.data()[cell]))
                  throw std::runtime_error("nonfinite GH2 accumulated sample: " + entry.name);
              ++tested_samples;
            }
            for (float phase : {-0.125f,0.0f,0.333f,0.999f,1.0f,1.125f}) {
              output.scale_down(page.channels,0);
              output.scale_add_at_phase(page,phase,1);
            }
          }
        }
        std::printf("PAGES\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\n", argv[i],tested_clips,nonempty_pages,tested_samples,bytes,clip_phases);
        continue;
      }
      const auto clip = load_clip(argv[1], argv[2], argv[i], clips.front().name);
      check(clip.loaded, "real source clip must decode");
      check(clip.gh2_facing_samples.has_value(), "production sample loader retains GH2 full/one facing inventory");
      check(clip.gh2_binding == binding && clip.gh2_binding_error.empty(),
            "production clip loader propagates shared allocation metadata");
      std::array<size_t, 9> counts{};
      for (const auto& channel : binding->channels)
        ++counts.at(static_cast<size_t>(channel.type));
      std::printf("ASSET %s move_self=%d channels=%zu types=", argv[i],
                  binding->move_self ? 1 : 0, binding->channels.size());
      for (const auto count : counts) std::printf("%zu,", count);
      std::printf(" children=%zu clip_loaded=1 facing_samples=%zu/%zu\n", binding->subdirectories.size(),
                  clip.gh2_facing_samples->positions.size(), clip.gh2_facing_samples->rotations.size());
      if (dump_channels) {
        std::printf("BINDING\t%s\t%d\n", binding->directory_name.c_str(),
                    binding->move_self ? 1 : 0);
        for (const auto& channel : binding->channels)
          std::printf("CHANNEL\t%s\t%d\t%s\n", binding->directory_name.c_str(),
                      static_cast<int>(channel.type), channel.name.c_str());
      }
    }
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}
