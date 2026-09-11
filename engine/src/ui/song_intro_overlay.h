#pragma once

#include "ui/menu_font.h"

#include <memory>
#include <string>

namespace ghogx::render {
class MiloSceneRenderer;
class Window;
}

namespace ghogx::ui {

// Data-driven implementation of the retail game.dtb MTV overlay route.
// GH2 owns its layout/font/camera; song metadata may come from an independently
// mounted content archive.
class SongIntroOverlay {
 public:
  explicit SongIntroOverlay(ghogx::render::Window& window);
  ~SongIntroOverlay();

  SongIntroOverlay(const SongIntroOverlay&) = delete;
  SongIntroOverlay& operator=(const SongIntroOverlay&) = delete;

  void reset(std::string hdr_path, std::string ark_path,
             std::string song_shortname);
  void reset(std::string visual_hdr_path, std::string visual_ark_path,
             std::string content_hdr_path, std::string content_ark_path,
             std::string song_shortname,
             std::string title_override = {},
             std::string artist_override = {},
             std::string caption_override = {});
  bool prepare();
  void draw(double song_time_seconds);

 private:
  bool load_text(std::string& title, std::string& caption,
                 std::string& artist) const;
  void ensure_loaded();

  ghogx::render::Window& window_;
  MenuFont font_;
  std::unique_ptr<ghogx::render::MiloSceneRenderer> renderer_;
  std::string hdr_path_;
  std::string ark_path_;
  std::string content_hdr_path_;
  std::string content_ark_path_;
  std::string song_shortname_;
  std::string title_override_;
  std::string artist_override_;
  std::string caption_override_;
  bool attempted_ = false;
  bool ready_ = false;
  bool shown_logged_ = false;
};

}  // namespace ghogx::ui
