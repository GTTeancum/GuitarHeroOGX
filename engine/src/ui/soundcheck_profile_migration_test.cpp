#include "core/data_node.h"
#include "core/object.h"
#include "core/symbol.h"
#include "ui/config_db.h"
#include "ui/meta_objects.h"
#include "ui/screen_manager.h"
#include "ui/ui_classes.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int failures = 0;
#define CHECK(expr)                                                          \
  do {                                                                       \
    if (!(expr)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int call_int(ghogx::Object* object, const char* message) {
  return object->handle_property(ghogx::Symbol(message), ghogx::DataArray())
      .as_int().value_or(0);
}

void set_int(ghogx::Object* object, const char* message, int value) {
  ghogx::DataArray args;
  args.push(ghogx::DataNode::Int(value));
  object->handle_property(ghogx::Symbol(message), args);
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path profile_path =
      fs::temp_directory_path() / "ghogx_soundcheck_profile_migration.txt";
  std::error_code error;
  fs::remove(profile_path, error);
  {
    std::ofstream profile(profile_path, std::ios::binary | std::ios::trunc);
    profile << "GHOGX_PROFILE_V2\n"
               "active_slot=-1\n"
               "root.value.sync_offset=73\n";
  }
#ifdef _WIN32
  _putenv_s("GHOGX_DISABLE_PROFILE_PERSISTENCE", "0");
  _putenv_s("GHOGX_PROFILE_PATH", profile_path.string().c_str());
#else
  setenv("GHOGX_DISABLE_PROFILE_PERSISTENCE", "0", 1);
  setenv("GHOGX_PROFILE_PATH", profile_path.string().c_str(), 1);
#endif

  ghogx::ui::register_ui_classes();
  ghogx::ui::ScreenManager manager;
  ghogx::ui::install_default_singletons(manager);
  ghogx::ui::ConfigDb config;
  ghogx::ui::install_meta_singletons(manager, config);
  ghogx::Object* campaign = manager.resolve_object(ghogx::Symbol("campaign"));
  ghogx::Object* options = manager.resolve_object(ghogx::Symbol("options"));
  CHECK(campaign != nullptr);
  CHECK(options != nullptr);
  if (campaign && options) {
    CHECK(call_int(campaign, "get_audio_offset") == 0);
    CHECK(call_int(campaign, "get_video_input_offset") == 73);
    CHECK(call_int(campaign, "get_sync_offset") == 73);
    CHECK(call_int(options, "get_audio_offset") == 0);
    CHECK(call_int(options, "get_video_input_offset") == 73);

    set_int(options, "set_audio_offset", 21);
    set_int(options, "set_video_input_offset", -37);
    CHECK(call_int(campaign, "get_audio_offset") == 21);
    CHECK(call_int(campaign, "get_video_input_offset") == -37);
    CHECK(call_int(campaign, "get_sync_offset") == -37);
  }

  std::ifstream saved(profile_path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(saved)),
                         std::istreambuf_iterator<char>());
  CHECK(text.find("root.value.audio_offset_ms=21") != std::string::npos);
  CHECK(text.find("root.value.video_input_offset_ms=-37") !=
        std::string::npos);
  CHECK(text.find("root.value.sync_offset=-37") != std::string::npos);
  fs::remove(profile_path, error);

  std::printf("soundcheck profile migration: %s\n",
              failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
