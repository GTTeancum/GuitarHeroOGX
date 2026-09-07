#include "ui/config_db.h"
#include "ui/menu_app.h"
#include "ui/meta_objects.h"
#include "ui/screen_manager.h"
#include "ui/ui_classes.h"

#include "ark_v3.h"
#include "character/char_clip.h"
#include "character/char_mesh.h"
#include "asset/milo_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace ghogx;
namespace fs = std::filesystem;

namespace {
constexpr std::size_t kMidoriMainClipCount = 113;
constexpr std::size_t kMidoriUiClipCount = 2;
constexpr std::size_t kMidoriStrumClipCount = 17;
constexpr std::size_t kMidoriFretClipCount = 25;
constexpr std::size_t kMidoriTotalClipCount =
    kMidoriMainClipCount + kMidoriUiClipCount + kMidoriStrumClipCount +
    kMidoriFretClipCount;
std::string first_existing(const std::string& dir,
                           std::vector<std::string> names) {
  for (auto& name : names) {
    std::string path = dir + "/" + name;
    if (fs::exists(path)) return path;
  }
  return {};
}

int source_rank(Symbol source) {
  if (source == Symbol("gh2")) return 1;
  if (source == Symbol("gh1")) return 2;
  if (source == Symbol("gh80")) return 3;
  if (source == Symbol("addon")) return 4;
  return 99;
}

std::string json_escape(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    if (ch == '"' || ch == '\\') out.push_back('\\');
    out.push_back(ch);
  }
  return out;
}

struct TempTree {
  explicit TempTree(fs::path root) : path(std::move(root)) {
    std::error_code error;
    fs::remove_all(path, error);
    fs::create_directories(path, error);
  }
  ~TempTree() {
    std::error_code error;
    fs::remove_all(path, error);
  }
  fs::path path;
};

std::string transform_base_name(std::string name) {
  for (const std::string suffix : {".trans", ".mesh"}) {
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      name.resize(name.size() - suffix.size());
      break;
    }
  }
  return name;
}

bool clip_drives_transform(const character::CharClip& clip,
                           const std::string& transform) {
  for (const auto& frame : clip.frames) {
    for (const auto& channel : frame) {
      if (transform_base_name(channel.bone_name) == transform) return true;
    }
  }
  return false;
}

bool image_has_visible_rgb(const asset::Image& image) {
  if (!image.valid() ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4u) {
    return false;
  }
  std::set<std::uint32_t> colors;
  std::size_t visible = 0;
  for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
    if (image.rgba[i + 3] == 0) continue;
    ++visible;
    colors.insert((static_cast<std::uint32_t>(image.rgba[i]) << 16) |
                  (static_cast<std::uint32_t>(image.rgba[i + 1]) << 8) |
                  static_cast<std::uint32_t>(image.rgba[i + 2]));
    if (visible > 1024 && colors.size() > 16) return true;
  }
  return visible > 1024 && colors.size() > 16;
}

bool expect_midori_texture(const std::string& hdr, const std::string& ark0,
                           const std::string& model_path,
                           const std::string& texture_name,
                           const char* label) {
  const auto texture =
      ghogx::asset::load_milo_texture_named(hdr, ark0, model_path,
                                            texture_name);
  if (texture.width != 256 || texture.height != 256 ||
      !image_has_visible_rgb(texture)) {
    std::fprintf(stderr,
                 "FAIL GH3 Midori %s texture decode %s from %s\n",
                 label, texture_name.c_str(), model_path.c_str());
    return false;
  }
  return true;
}

bool clip_payload_sane(const ghogx::character::CharClip& clip,
                       bool expect_static_noop) {
  if (!clip.loaded || clip.frames.empty() || clip.output_bones.empty())
    return false;
  std::size_t channels = 0;
  for (const auto& frame : clip.frames) {
    for (const auto& channel : frame) {
      ++channels;
      if (channel.bone_name.empty()) return false;
      switch (channel.type) {
        case ghogx::character::ClipChannel::kPos:
        case ghogx::character::ClipChannel::kScale:
          for (float value : channel.pos)
            if (!std::isfinite(value)) return false;
          break;
        case ghogx::character::ClipChannel::kQuat:
          for (float value : channel.quat)
            if (!std::isfinite(value)) return false;
          break;
        case ghogx::character::ClipChannel::kRotX:
        case ghogx::character::ClipChannel::kRotY:
        case ghogx::character::ClipChannel::kRotZ:
        case ghogx::character::ClipChannel::kDeltaX:
        case ghogx::character::ClipChannel::kDeltaY:
        case ghogx::character::ClipChannel::kDeltaZ:
          if (!std::isfinite(channel.angle)) return false;
          break;
      }
    }
  }
  if (expect_static_noop) return clip.frames.size() == 1 && channels == 0;
  return channels > 0;
}

bool is_static_face_noop(const std::string& clip_name) {
  return clip_name.rfind("gh2_face_call_", 0) == 0;
}

bool transform_exists_in_any(
    const std::vector<const ghogx::character::Character*>& characters,
    const std::string& name) {
  const std::string base = transform_base_name(name);
  // GH2 character clips carry root-motion through CharBones' virtual facing
  // channels. Retail Casey has no corresponding model transform either.
  if (base == "bone_facing" || base == "bone_facing_delta") return true;
  for (const auto* character : characters) {
    if (character &&
        (character->has_transform(base + ".trans") ||
         character->has_transform(base + ".mesh") ||
         character->has_transform(base))) {
      return true;
    }
  }
  return false;
}

bool clip_targets_bind_to_midori_models(
    const ghogx::character::CharClip& clip,
    const std::vector<const ghogx::character::Character*>& characters,
    const char* label, const std::string& clip_name) {
  std::set<std::string> missing;
  std::set<std::string> checked;
  // Retail Casey hand banks retain a broad output declaration that includes
  // unused Casey hair transforms. Validate the channels that are actually
  // serialized and applied; Midori must not fabricate Casey's hair hierarchy.
  for (const auto& frame : clip.frames) {
    for (const auto& channel : frame) {
      const std::string base = transform_base_name(channel.bone_name);
      if (!checked.insert(base).second) continue;
      if (!transform_exists_in_any(characters, base)) missing.insert(base);
    }
  }
  if (!missing.empty()) {
    std::fprintf(stderr,
                 "FAIL GH3 Midori %s clip/model missing targets in %s:",
                 label, clip_name.c_str());
    std::size_t printed = 0;
    for (const std::string& name : missing) {
      if (printed++ >= 12) break;
      std::fprintf(stderr, " %s", name.c_str());
    }
    std::fprintf(stderr, "\n");
    return false;
  }
  return !checked.empty();
}

bool expect_clip_bank(const std::string& hdr, const std::string& ark0,
                      const std::string& path, std::size_t expected_count,
                      std::size_t expected_static_noops,
                      const char* label,
                      const std::vector<const ghogx::character::Character*>&
                          characters) {
  const auto catalog = ghogx::character::load_clip_catalog(hdr, ark0, {path});
  if (catalog.size() != expected_count) {
    std::fprintf(stderr,
                 "FAIL GH3 Midori %s clip catalog count %zu != %zu\n",
                 label, catalog.size(), expected_count);
    return false;
  }
  if (catalog.empty()) return true;
  std::size_t static_noops = 0;
  for (const auto& entry : catalog) {
    const bool static_noop = is_static_face_noop(entry.name);
    if (static_noop) ++static_noops;
    const auto clip =
        ghogx::character::load_clip(hdr, ark0, entry.milo_path, entry.name);
    if (!clip_payload_sane(clip, static_noop)) {
      std::fprintf(stderr,
                   "FAIL GH3 Midori %s clip payload %s from %s\n",
                   label, entry.name.c_str(), path.c_str());
      return false;
    }
    if (!clip_targets_bind_to_midori_models(clip, characters, label,
                                            entry.name))
      return false;
  }
  if (static_noops != expected_static_noops) {
    std::fprintf(stderr,
                 "FAIL GH3 Midori %s static face no-op count %zu != %zu\n",
                 label, static_noops, expected_static_noops);
    return false;
  }
  return true;
}

bool expect_midori_catalog_row(const ui::ConfigDb& db) {
  const auto rows = db.character_variants(Symbol("gh3_midori"));
  if (rows.size() != 1 || rows[0].selection != Symbol("gh3_midori_1") ||
      rows[0].source_game != Symbol("addon") ||
      rows[0].model_path !=
          "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2" ||
      rows[0].ui_anim_path !=
          "char/gh3_midori/anims/gen/gh3_midori_ui.milo_ps2" ||
      rows[0].main_anim_path !=
          "char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2" ||
      rows[0].strum_anim_path !=
          "char/gh3_midori/anims/gen/gh3_midori_strum.milo_ps2" ||
      rows[0].fret_anim_path !=
          "char/gh3_midori/anims/gen/gh3_midori_fret.milo_ps2" ||
      rows[0].animation_source_model_path !=
          "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2" ||
      rows[0].retarget_animation) {
    std::fprintf(stderr,
                 "FAIL GH3 Midori outfit 1 catalog row is not mounted\n");
    return false;
  }
  return true;
}

bool expect_midori_external_assets(const std::string& hdr,
                                   const std::string& ark0) {
  if (!expect_midori_texture(
          hdr, ark0, "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
          "midori_1_539357ac.tex", "outfit 1")) {
    return false;
  }

  ghogx::character::Character midori_1;
  if (!ghogx::character::load_character(
          hdr, ark0, "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
          midori_1)) {
    std::fprintf(stderr, "FAIL GH3 Midori model reload for binding\n");
    return false;
  }
  const std::vector<const ghogx::character::Character*> models = {
      &midori_1};
  return expect_clip_bank(
             hdr, ark0,
             "char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2",
             kMidoriMainClipCount,
             0, "main", models) &&
         expect_clip_bank(
             hdr, ark0,
             "char/gh3_midori/anims/gen/gh3_midori_ui.milo_ps2",
             kMidoriUiClipCount, 0,
             "ui", models) &&
         expect_clip_bank(
             hdr, ark0,
             "char/gh3_midori/anims/gen/gh3_midori_strum.milo_ps2",
             kMidoriStrumClipCount,
             0, "strum", models) &&
         expect_clip_bank(
             hdr, ark0,
             "char/gh3_midori/anims/gen/gh3_midori_fret.milo_ps2",
             kMidoriFretClipCount,
             0, "fret", models);
}
}  // namespace

int main(int argc, char** argv) {
  const bool midori_assets_only =
      (argc == 4 || argc == 5) &&
      std::string(argv[1]) == "--midori-assets-only";
  if (argc != 1 && argc != 3 && !midori_assets_only) {
    std::fprintf(stderr,
                 "usage: ghogx_character_variant_catalog_test "
                 "[<main.hdr> <main_0.ark>]\n"
                 "       ghogx_character_variant_catalog_test "
                 "--midori-assets-only <main.hdr> <main_0.ark> "
                 "[<addons-dir>]\n");
    return 2;
  }
#ifdef _WIN32
  _putenv_s("GHOGX_DISABLE_PROFILE_PERSISTENCE", "1");
#else
  setenv("GHOGX_DISABLE_PROFILE_PERSISTENCE", "1", 1);
#endif
  if (midori_assets_only) {
    const fs::path addons = argc == 5 ? fs::path(argv[4])
                                      : fs::path(GHOGX_SOURCE_ROOT) / "DLC";
    const std::string addons_string = addons.string();
#ifdef _WIN32
    _putenv_s("GHOGX_ADDONS_DIR", addons_string.c_str());
#else
    setenv("GHOGX_ADDONS_DIR", addons_string.c_str(), 1);
#endif
    auto ark = gh::ark::ArkV3Reader::load(argv[2]);
    ui::ConfigDb db;
    db.load_addon_manifests(addons / "community.gh3.midori", &ark);
    if (!expect_midori_catalog_row(db)) return 1;
    if (!expect_midori_external_assets(argv[2], argv[3])) return 1;
    std::printf(
        "ghogx_character_variant_catalog_test: PASS "
        "(Midori external assets: 1 model, 1 texture, %zu clips)\n",
        kMidoriTotalClipCount);
    return 0;
  }
  const std::string default_ark_dir =
      "C:/Programming/GitHub/Guitar Hero II/gh2_ps2_hybrid_assets/gen";
  const std::string hdr =
      argc == 3 ? argv[1]
                : first_existing(default_ark_dir, {"MAIN.HDR", "main.hdr"});
  const std::string ark0 =
      argc == 3 ? argv[2]
                : first_existing(default_ark_dir, {"MAIN_0.ARK", "main_0.ark"});
  if (hdr.empty() || ark0.empty()) {
    std::printf(
        "ghogx_character_variant_catalog_test: SKIP (no merged ARK at %s)\n",
        default_ark_dir.c_str());
    return 0;
  }
  const auto ark = gh::ark::ArkV3Reader::load(hdr);
  TempTree boot_addon_root(fs::temp_directory_path() /
                           "ghogx-empty-dlc-boot-test");
#ifdef _WIN32
  _putenv_s("GHOGX_ADDONS_DIR", boot_addon_root.path.string().c_str());
#else
  setenv("GHOGX_ADDONS_DIR", boot_addon_root.path.string().c_str(), 1);
#endif
  ui::ConfigDb db;
  db.load(ark, {ark0});
  db.load_addon_manifests(
      fs::path(GHOGX_SOURCE_ROOT) / "DLC" / "core.singers", &ark);
  const auto seed_characters = db.characters();
  if (seed_characters.empty()) {
    std::fprintf(stderr, "FAIL built-in character catalog is empty\n");
    return 1;
  }
  const auto seed_variants = db.character_variants(seed_characters.front());
  if (seed_variants.empty()) {
    std::fprintf(stderr, "FAIL built-in character has no seed variant\n");
    return 1;
  }
  const ui::CharacterVariant& seed = seed_variants.front();
  TempTree addon_root(fs::temp_directory_path() /
                      "ghogx-character-addon-manifest-test");
  const fs::path addon_dir = addon_root.path / "community.addon_test";
  fs::create_directories(addon_dir / "content" / "portraits");
  {
    std::ofstream portrait(addon_dir / "content" / "portraits" /
                               "addon.bmp_ps2",
                           std::ios::binary);
    portrait << "loose-portrait-test";
  }
  {
    std::ofstream ignored(addon_dir / "content" / "portraits" /
                              "not_indexed.bmp_ps2",
                          std::ios::binary);
    ignored << "must-not-mount";
  }
  {
    std::ofstream manifest(addon_dir / "manifest.json");
    manifest
        << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"id\": \"community.addon_test\",\n"
        << "  \"files\": [\"portraits/addon.bmp_ps2\"],\n"
        << "  \"characters\": [{\n"
        << "    \"id\": \"addon_test\",\n"
        << "    \"label\": \"Addon Test\",\n"
        << "    \"portrait\": \"portraits/addon.bmp_ps2\",\n"
        << "    \"preferred_guitar\": \"addon_test_guitar\",\n"
        << "    \"preferred_guitar_finish\": \"addon_test_finish\",\n"
        << "    \"preferred_guitar_paint_primary\": 7,\n"
        << "    \"preferred_guitar_paint_secondary\": 8,\n"
        << "    \"outfits\": [{\n"
        << "      \"selection\": \"addon_test_default\",\n"
        << "      \"label\": \"Standard\",\n"
        << "      \"model\": \"" << json_escape(seed.model_path) << "\",\n"
        << "      \"ui_model\": \"" << json_escape(seed.ui_model_path)
        << "\",\n"
        << "      \"ui_anim\": \"" << json_escape(seed.ui_anim_path)
        << "\",\n"
        << "      \"main_anim\": \"" << json_escape(seed.main_anim_path)
        << "\",\n"
        << "      \"strum_anim\": \"" << json_escape(seed.strum_anim_path)
        << "\",\n"
        << "      \"fret_anim\": \"" << json_escape(seed.fret_anim_path)
        << "\",\n"
        << "      \"unlock\": \"won_campaign\"\n"
        << "    }]\n"
        << "  }],\n"
        << "  \"guitars\": [{\n"
        << "    \"id\": \"addon_test_guitar\",\n"
        << "    \"type\": \"guitar\",\n"
        << "    \"name\": \"Addon Test Guitar\",\n"
        << "    \"skins\": [{\"id\": \"addon_test_finish\", "
           "\"name\": \"Black\"}]\n"
        << "  }],\n"
        << "  \"finishes\": [{\"guitar\": \"addon_test_guitar\", "
           "\"id\": \"addon_test_finish_2\", \"name\": \"White\"}],\n"
        << "  \"venues\": [{\"id\": \"addon_test_venue\"}],\n"
        << "  \"songs\": [{\n"
        << "    \"id\": \"addon_test_song\",\n"
        << "    \"title\": \"Addon Test Song\",\n"
        << "    \"artist\": \"Addon Artist\",\n"
        << "    \"song\": {\"name\": \"songs/addon_test/song\", "
           "\"midi_file\": \"songs/addon_test/song.mid\"}\n"
        << "  }],\n"
        << "  \"setlists\": [{\"id\": \"addon_test_setlist\", "
           "\"label\": \"Addon Setlist\", "
           "\"songs\": [\"addon_test_song\"], "
           "\"include_in_quickplay\": true}]\n"
        << "}\n";
  }
  const fs::path rejected_dir = addon_root.path / "community.z_rejected";
  fs::create_directories(rejected_dir);
  {
    std::ofstream manifest(rejected_dir / "manifest.json");
    manifest
        << "{\n"
        << "  \"schema_version\": 1e0,\n"
        << "  \"id\": \"community.z_rejected\",\n"
        << "  \"characters\": [{\"id\": \"rollback_\\u00e9\", "
           "\"label\": \"Rollback\", \"outfits\": [{"
           "\"selection\": \"rollback_variant\", \"label\": "
           "\"Standard\", \"model\": \""
        << json_escape(seed.model_path) << "\"}]}],\n"
        << "  \"songs\": [{\"id\": \"rollback_song\", "
           "\"title\": \"Rollback Song\"}],\n"
        << "  \"guitars\": [{\"id\": \"rollback_guitar\", "
           "\"type\": \"guitar\"}],\n"
        << "  \"venues\": [{\"id\": \"rollback_venue\"}],\n"
        << "  \"setlists\": [{\"id\": \"rollback_setlist\", "
           "\"songs\": [\"missing_song\"]}]\n"
        << "}\n";
  }
  db.load_addon_manifests(addon_root.path, &ark);
  const fs::path midori_dir =
      fs::path(GHOGX_SOURCE_ROOT) / "DLC" / "community.gh3.midori";
  const bool midori_package_present =
      fs::is_regular_file(midori_dir / "manifest.json");
  if (midori_package_present)
    db.load_addon_manifests(midori_dir, &ark);
  const std::vector<Symbol> characters = db.characters();
  if (characters.empty()) {
    std::fprintf(stderr, "FAIL character catalog is empty\n");
    return 1;
  }

  ui::ScreenManager mgr;
  ui::install_default_singletons(mgr);
  ui::install_meta_singletons(mgr, db);
  Object* provider = mgr.resolve_object(Symbol("character_provider"));
  if (!provider) {
    std::fprintf(stderr, "FAIL character_provider is missing\n");
    return 1;
  }
  const auto playable_singer_rows =
      db.character_variants(Symbol("female_singer"));
  if (playable_singer_rows.size() != 2 ||
      playable_singer_rows[0].selection != Symbol("gh2_female_singer") ||
      playable_singer_rows[1].selection != Symbol("gh1_female_singer") ||
      playable_singer_rows[0].model_path !=
          "char/gh2_female_singer/og/gen/female_singer.milo_ps2" ||
      playable_singer_rows.front().main_anim_path.find(
          "char/alterna/anims/gen/") != 0 ||
      !playable_singer_rows.front().retarget_animation ||
      playable_singer_rows.front().guitarist_hidden_roots !=
          std::vector<std::string>{"bone_pos_mic.mesh"} ||
      !playable_singer_rows.front().addon_defined ||
      !playable_singer_rows.front().character_blurb.empty() ||
      !playable_singer_rows.front().outfit_blurb.empty() ||
      playable_singer_rows.front().unlock_requirement !=
          Symbol("won_campaign") ||
      db.character_label(Symbol("female_singer")) != "Female Singer") {
    std::fprintf(stderr,
                 "FAIL data-driven female singer/Judy unlock route\n");
    return 1;
  }
  const auto male_singer_rows = db.character_variants(Symbol("male_singer"));
  if (male_singer_rows.size() != 2 ||
      male_singer_rows[0].selection != Symbol("gh2_male_singer") ||
      male_singer_rows[1].selection != Symbol("gh1_male_singer") ||
      male_singer_rows.front().animation_source_model_path !=
          "char/classic/og/gen/classic.milo_ps2") {
    std::fprintf(stderr, "FAIL data-driven male singer/Clive route\n");
    return 1;
  }
  for (const char* portrait_path : {
           "ui/image/dlc/core_singers/female_singer.bmp_ps2",
           "ui/image/dlc/core_singers/male_singer.bmp_ps2"}) {
    const auto portrait =
        ghogx::asset::load_ps2_bitmap_from_ark(hdr, ark0, portrait_path);
    if (!portrait.valid() || portrait.width != 64 || portrait.height != 128) {
      std::fprintf(stderr, "FAIL singer portrait %s\n", portrait_path);
      return 1;
    }
  }
  const auto addon_rows = db.character_variants(Symbol("addon_test"));
  if (addon_rows.size() != 1 ||
      addon_rows.front().selection != Symbol("addon_test_default") ||
      addon_rows.front().source_game != Symbol("addon") ||
      addon_rows.front().character_label != "Addon Test" ||
      addon_rows.front().portrait_path != "portraits/addon.bmp_ps2" ||
      addon_rows.front().preferred_guitar != Symbol("addon_test_guitar") ||
      addon_rows.front().preferred_guitar_skin !=
          Symbol("addon_test_finish") ||
      addon_rows.front().preferred_guitar_paint_primary != 7 ||
      addon_rows.front().preferred_guitar_paint_secondary != 8) {
    std::fprintf(stderr, "FAIL per-addon manifest character merge\n");
    return 1;
  }
  const auto loose_portrait = ark.find("portraits/addon.bmp_ps2");
  if (!loose_portrait || loose_portrait->loose_path.empty() ||
      ark.read_entry(*loose_portrait, {ark0}) !=
          std::vector<uint8_t>({'l', 'o', 'o', 's', 'e', '-', 'p', 'o',
                                'r', 't', 'r', 'a', 'i', 't', '-', 't',
                                'e', 's', 't'})) {
    std::fprintf(stderr, "FAIL loose ARK-path mount\n");
    return 1;
  }
  if (ark.find("portraits/not_indexed.bmp_ps2")) {
    std::fprintf(stderr, "FAIL indexed package mounted an unlisted file\n");
    return 1;
  }
  const auto quickplay = db.quickplay_songs();
  if (!db.character_variants(Symbol("rollback_\xc3\xa9")).empty() ||
      db.song_index(Symbol("rollback_song")) >= 0 ||
      db.guitar(Symbol("rollback_guitar")) ||
      db.is_venue(Symbol("rollback_venue")) ||
      !db.setlist_songs(Symbol("rollback_setlist")).empty()) {
    std::fprintf(stderr, "FAIL rejected package was not transactional\n");
    return 1;
  }
  if (!db.guitar(Symbol("addon_test_guitar")) ||
      db.guitar_skin_count(Symbol("addon_test_guitar")) != 2 ||
      !db.is_venue(Symbol("addon_test_venue")) ||
      db.song_index(Symbol("addon_test_song")) < 0 ||
      db.song_audio_path(Symbol("addon_test_song")) !=
          "songs/addon_test/song" ||
      db.setlist_songs(Symbol("addon_test_setlist")) !=
          std::vector<Symbol>{Symbol("addon_test_song")} ||
      std::find(quickplay.begin(), quickplay.end(),
                Symbol("addon_test_song")) == quickplay.end()) {
    std::fprintf(stderr,
                 "FAIL package guitar/finish/venue/song/setlist merge\n");
    return 1;
  }
  Object* campaign = mgr.resolve_object(Symbol("campaign"));
  if (!campaign) {
    std::fprintf(stderr, "FAIL campaign singleton is missing\n");
    return 1;
  }
  const int locked_character_count =
      provider->handle_property(Symbol("list_length"), DataArray())
          .as_int()
          .value_or(-1);
  if (locked_character_count != static_cast<int>(characters.size() - 3)) {
    std::fprintf(stderr,
                 "FAIL locked DLC characters are visible before campaign unlock\n");
    return 1;
  }
  campaign->set_property(Symbol("won_campaign"), DataNode::Int(1));
  const int unlocked_character_count =
      provider->handle_property(Symbol("list_length"), DataArray())
          .as_int()
          .value_or(-1);
  if (unlocked_character_count != static_cast<int>(characters.size())) {
    std::fprintf(stderr,
                 "FAIL female singer does not appear after campaign unlock\n");
    return 1;
  }
  DataArray singer_index_args;
  singer_index_args.push(DataNode::Sym(Symbol("female_singer")));
  const int singer_index =
      provider->handle_property(Symbol("get_index"), singer_index_args)
          .as_int()
          .value_or(-1);
  DataArray singer_text_args;
  singer_text_args.push(DataNode::Int(singer_index));
  if (provider->handle_property(Symbol("get_text"), singer_text_args)
          .as_string()
          .value_or("") != "Female Singer") {
    std::fprintf(stderr,
                 "FAIL project playable character label is not presented\n");
    return 1;
  }
  if (!provider
           ->handle_property(Symbol("get_character_blurb"), singer_text_args)
           .as_string()
           .value_or("")
           .empty()) {
    std::fprintf(stderr,
                 "FAIL project playable character blurb is not blank\n");
    return 1;
  }
  DataArray addon_index_args;
  addon_index_args.push(DataNode::Sym(Symbol("addon_test")));
  const int addon_index =
      provider->handle_property(Symbol("get_index"), addon_index_args)
          .as_int()
          .value_or(-1);
  DataArray addon_portrait_args;
  addon_portrait_args.push(DataNode::Int(addon_index));
  if (provider->handle_property(Symbol("get_portrait"), addon_portrait_args)
          .as_string()
          .value_or("") != addon_rows.front().portrait_path) {
    std::fprintf(stderr,
                 "FAIL provider does not expose addon portrait path\n");
    return 1;
  }
  if (midori_package_present) {
    if (!expect_midori_catalog_row(db)) return 1;
    const auto midori_main =
        ark.find("char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2");
    if (!midori_main || midori_main->loose_path.empty() ||
        ark.read_entry(*midori_main, {ark0}).empty()) {
      std::fprintf(stderr,
                   "FAIL GH3 Midori main animation loose mount\n");
      return 1;
    }
    if (!expect_midori_texture(
            hdr, ark0, "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
            "midori_1_539357ac.tex", "outfit 1")) {
      return 1;
    }
    ghogx::character::Character midori_1;
    if (!ghogx::character::load_character(
            hdr, ark0, "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
            midori_1)) {
      std::fprintf(stderr, "FAIL GH3 Midori model reload for binding\n");
      return 1;
    }
    const std::vector<const ghogx::character::Character*> midori_models = {
        &midori_1};
    if (!expect_clip_bank(hdr, ark0,
                          "char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2",
                          kMidoriMainClipCount, 0, "main", midori_models) ||
        !expect_clip_bank(hdr, ark0,
                          "char/gh3_midori/anims/gen/gh3_midori_ui.milo_ps2",
                          kMidoriUiClipCount, 0, "ui", midori_models) ||
        !expect_clip_bank(
            hdr, ark0,
            "char/gh3_midori/anims/gen/gh3_midori_strum.milo_ps2",
            kMidoriStrumClipCount,
            0, "strum", midori_models) ||
        !expect_clip_bank(hdr, ark0,
                           "char/gh3_midori/anims/gen/gh3_midori_fret.milo_ps2",
                           kMidoriFretClipCount, 0, "fret", midori_models)) {
      return 1;
    }
  }

  {
    const auto closed = ui::source_charsys_external_door_rotation(0.0f);
    const float authored_open_z = 2.52563f;
    const auto open =
        ui::source_charsys_external_door_rotation(authored_open_z);
    const auto near = [](float a, float b) {
      return std::fabs(a - b) <= 1.0e-6f;
    };
    if (!near(closed[0], 1.0f) || !near(closed[4], 0.0f) ||
        !near(closed[5], 1.0f) || !near(closed[7], -1.0f) ||
        !near(open[0], std::cos(authored_open_z)) ||
        !near(open[1], std::sin(authored_open_z))) {
      std::fprintf(
          stderr,
          "FAIL CharsysPanel external-door Euler(pi/2,0,z) bridge\n");
      return 1;
    }
  }

  std::set<const void*> selections;
  std::size_t variants = 0;
  std::size_t gh1 = 0;
  std::size_t gh2 = 0;
  std::size_t gh80 = 0;
  std::size_t direct_door_variants = 0;
  std::size_t open_pose_fallback_variants = 0;
  for (Symbol character : characters) {
    const auto rows = db.character_variants(character);
    if (rows.empty()) {
      std::fprintf(stderr, "FAIL %s has no variants\n", character.c_str());
      return 1;
    }
    int previous_rank = 0;
    DataArray count_args;
    count_args.push(DataNode::Sym(character));
    const int provider_count =
        provider->handle_property(Symbol("num_outfits"), count_args)
            .as_int()
            .value_or(-1);
    if (provider_count != static_cast<int>(rows.size())) {
      std::fprintf(stderr, "FAIL %s provider=%d catalog=%zu\n",
                   character.c_str(), provider_count, rows.size());
      return 1;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto& row = rows[index];
      const int rank = source_rank(row.source_game);
      if (rank < previous_rank || rank == 99 || row.label.empty() ||
          !selections.insert(row.selection.id()).second) {
        std::fprintf(stderr, "FAIL invalid row %s/%s\n",
                     character.c_str(), row.selection.c_str());
        return 1;
      }
      previous_rank = rank;
      for (const std::string* path :
           {&row.model_path, &row.ui_model_path, &row.ui_anim_path,
            &row.main_anim_path, &row.strum_anim_path,
            &row.fret_anim_path, &row.highway_surface_path}) {
        if (!path->empty() && !ark.find(*path)) {
          std::fprintf(stderr, "FAIL missing %s for %s\n", path->c_str(),
                       row.selection.c_str());
          return 1;
        }
      }
      ghogx::character::Character preview_character;
      if (!ghogx::character::load_character(
              hdr, ark0,
              row.ui_model_path.empty() ? row.model_path
                                        : row.ui_model_path,
              preview_character)) {
        std::fprintf(stderr, "FAIL preview model %s\n",
                     row.selection.c_str());
        return 1;
      }
      const auto clips = ghogx::character::load_clip_catalog(
          hdr, ark0, {row.ui_anim_path});
      const auto named_loop = std::find_if(
          clips.begin(), clips.end(),
          [](const auto& clip) { return clip.name == "ui_loop"; });
      const auto authored_idle = std::find_if(
          clips.begin(), clips.end(), [](const auto& clip) {
            const std::string suffix = "_idle_ui";
            return clip.name.size() >= suffix.size() &&
                   clip.name.compare(clip.name.size() - suffix.size(),
                                     suffix.size(), suffix) == 0;
          });
      const auto selected_clip =
          named_loop != clips.end()
              ? named_loop
              : (authored_idle != clips.end() ? authored_idle
                                              : clips.begin());
      const auto ui_loop =
          selected_clip == clips.end()
              ? ghogx::character::CharClip{}
              : ghogx::character::load_clip(
                    hdr, ark0, selected_clip->milo_path,
                    selected_clip->name);
      if (!ui_loop.loaded) {
        std::fprintf(stderr, "FAIL ui_loop %s from %s\n",
                     row.selection.c_str(), row.ui_anim_path.c_str());
        return 1;
      }
      const bool has_authored_door_pose =
          selected_clip->name == "ui_loop" &&
          clip_drives_transform(ui_loop, "bone_door");
      direct_door_variants += has_authored_door_pose ? 1 : 0;
      open_pose_fallback_variants += has_authored_door_pose ? 0 : 1;
      DataArray get_args;
      get_args.push(DataNode::Sym(character));
      get_args.push(DataNode::Int(static_cast<int>(index)));
      const Symbol provider_outfit =
          provider->handle_property(Symbol("get_outfit"), get_args)
              .as_symbol()
              .value_or(Symbol());
      if (provider_outfit != row.selection) {
        std::fprintf(stderr, "FAIL provider order %s index=%zu\n",
                     character.c_str(), index);
        return 1;
      }
      const DataNode provider_blurb =
          provider->handle_property(Symbol("get_outfit_blurb"), get_args);
      if (row.addon_defined) {
        if (!provider_blurb.as_string().value_or("").empty()) {
          std::fprintf(stderr, "FAIL addon outfit blurb %s index=%zu\n",
                       character.c_str(), index);
          return 1;
        }
      } else if (row.source_game == Symbol("gh2")) {
        const std::string expected =
            std::string(character.c_str()) + "_outfit_blurb";
        if (provider_blurb.as_symbol().value_or(Symbol()) !=
            Symbol(expected.c_str())) {
          std::fprintf(stderr, "FAIL native outfit blurb %s index=%zu\n",
                       character.c_str(), index);
          return 1;
        }
      } else if (!provider_blurb.as_string().value_or("").empty()) {
        std::fprintf(stderr, "FAIL imported outfit blurb %s index=%zu\n",
                     character.c_str(), index);
        return 1;
      }
      ++variants;
      gh1 += row.source_game == Symbol("gh1") ? 1 : 0;
      gh2 += row.source_game == Symbol("gh2") ? 1 : 0;
      gh80 += row.source_game == Symbol("gh80") ? 1 : 0;
    }
    // The cyclic selector's two-row viewport is [selected, selected+1].
    // These endpoint checks cover both forward and reverse wrap.
    const std::size_t last = rows.size() - 1;
    if (rows[(last + 1) % rows.size()].selection != rows[0].selection ||
        rows[(0 + rows.size() - 1) % rows.size()].selection !=
            rows[last].selection) {
      std::fprintf(stderr, "FAIL wrap %s\n", character.c_str());
      return 1;
    }
  }
  if (direct_door_variants == 0) {
    std::fprintf(stderr,
                 "FAIL no authored ui_loop bone_door pose is available\n");
    return 1;
  }

  std::printf(
      "PASS character catalog characters=%zu variants=%zu "
      "gh1=%zu gh2=%zu gh80=%zu order=chronological wrap=both "
      "viewport=2 door_bridge=euler_pi_over_2_0_z "
      "door_direct=%zu door_open_pose_fallback=%zu\n",
      characters.size(), variants, gh1, gh2, gh80, direct_door_variants,
      open_pose_fallback_variants);
  return 0;
}
