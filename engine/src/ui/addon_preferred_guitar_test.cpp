#include "ui/config_db.h"

#include "ark_v3.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ghogx;
namespace fs = std::filesystem;

namespace {

struct TempTree {
  explicit TempTree(fs::path value) : path(std::move(value)) {
    std::error_code error;
    fs::remove_all(path, error);
    fs::create_directories(path, error);
  }
  ~TempTree() {
    gh::ark::ArkV3Reader::clear_loose_file_mounts();
    std::error_code error;
    fs::remove_all(path, error);
  }
  fs::path path;
};

void touch(const fs::path& path) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << "preferred-guitar-test";
}

}  // namespace

int main() {
  TempTree tree(fs::temp_directory_path() /
                "ghogx-addon-preferred-guitar-test");
  const fs::path package = tree.path / "community.preferred_guitar";
  const fs::path content = package / "content";
  for (const char* path : {
           "char/example/model.milo_ps2",
           "char/example/ui.milo_ps2",
           "char/example/main.milo_ps2",
           "char/example/strum.milo_ps2",
           "char/example/fret.milo_ps2",
           "ui/example/portrait.bmp_ps2"}) {
    touch(content / path);
  }
  fs::create_directories(package);
  {
    std::ofstream manifest(package / "manifest.json");
    manifest
        << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"id\": \"community.preferred_guitar\",\n"
        << "  \"content_root\": \"content\",\n"
        << "  \"characters\": [{\n"
        << "    \"id\": \"example\",\n"
        << "    \"label\": \"Example\",\n"
        << "    \"portrait\": \"ui/example/portrait.bmp_ps2\",\n"
        << "    \"preferred_guitar\": \"source_guitar\",\n"
        << "    \"preferred_guitar_finish\": \"source_finish\",\n"
        << "    \"preferred_guitar_paint_primary\": 4,\n"
        << "    \"preferred_guitar_paint_secondary\": 9,\n"
        << "    \"outfits\": [{\n"
        << "      \"selection\": \"example_default\",\n"
        << "      \"model\": \"char/example/model.milo_ps2\",\n"
        << "      \"ui_model\": \"char/example/model.milo_ps2\",\n"
        << "      \"ui_anim\": \"char/example/ui.milo_ps2\",\n"
        << "      \"main_anim\": \"char/example/main.milo_ps2\",\n"
        << "      \"strum_anim\": \"char/example/strum.milo_ps2\",\n"
        << "      \"fret_anim\": \"char/example/fret.milo_ps2\"\n"
        << "    }, {\n"
        << "      \"selection\": \"example_override\",\n"
        << "      \"model\": \"char/example/model.milo_ps2\",\n"
        << "      \"preferred_guitar\": \"outfit_guitar\",\n"
        << "      \"preferred_guitar_finish\": \"outfit_finish\",\n"
        << "      \"preferred_guitar_paint_primary\": 2,\n"
        << "      \"preferred_guitar_paint_secondary\": 3\n"
        << "    }]\n"
        << "  }]\n"
        << "}\n";
  }

  gh::ark::ArkV3Reader empty_ark;
  ui::ConfigDb db;
  db.load_addon_manifests(tree.path, &empty_ark);
  const ui::CharacterVariant* inherited =
      db.character_variant(Symbol("example_default"));
  const ui::CharacterVariant* overridden =
      db.character_variant(Symbol("example_override"));
  if (!inherited || !overridden ||
      inherited->preferred_guitar != Symbol("source_guitar") ||
      inherited->preferred_guitar_skin != Symbol("source_finish") ||
      inherited->preferred_guitar_paint_primary != 4 ||
      inherited->preferred_guitar_paint_secondary != 9 ||
      overridden->preferred_guitar != Symbol("outfit_guitar") ||
      overridden->preferred_guitar_skin != Symbol("outfit_finish") ||
      overridden->preferred_guitar_paint_primary != 2 ||
      overridden->preferred_guitar_paint_secondary != 3) {
    std::fprintf(stderr,
                 "FAIL preferred-guitar inheritance/override contract\n");
    return 1;
  }
  std::printf("PASS preferred-guitar inheritance and outfit override\n");
  return 0;
}
