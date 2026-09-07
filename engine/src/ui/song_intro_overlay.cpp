#include "ui/song_intro_overlay.h"

#include "catalog.h"
#include "milo_scene/milo_scene.h"
#include "render/milo_scene_renderer.h"
#include "render/window_d3d9.h"
#include "ui/menu_labels.h"

#include "ark_v3.h"
#include "dtb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

#include <d3d9.h>

namespace ghogx::ui {
namespace {

uint32_t pack_color(const std::array<float, 4>& rgba) {
  const auto channel = [](float value) {
    return static_cast<uint32_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
  };
  return D3DCOLOR_ARGB(channel(rgba[3]), channel(rgba[0]), channel(rgba[1]),
                       channel(rgba[2]));
}

}  // namespace

SongIntroOverlay::SongIntroOverlay(ghogx::render::Window& window)
    : window_(window) {}

SongIntroOverlay::~SongIntroOverlay() = default;

void SongIntroOverlay::reset(std::string hdr_path, std::string ark_path,
                             std::string song_shortname) {
  reset(hdr_path, ark_path, hdr_path, ark_path, std::move(song_shortname));
}

void SongIntroOverlay::reset(std::string visual_hdr_path,
                             std::string visual_ark_path,
                             std::string content_hdr_path,
                             std::string content_ark_path,
                             std::string song_shortname,
                             std::string title_override,
                             std::string artist_override) {
  hdr_path_ = std::move(visual_hdr_path);
  ark_path_ = std::move(visual_ark_path);
  content_hdr_path_ = std::move(content_hdr_path);
  content_ark_path_ = std::move(content_ark_path);
  song_shortname_ = std::move(song_shortname);
  title_override_ = std::move(title_override);
  artist_override_ = std::move(artist_override);
  attempted_ = false;
  ready_ = false;
  shown_logged_ = false;
  renderer_.reset();
  font_ = MenuFont{};
}

bool SongIntroOverlay::load_text(std::string& title, std::string& caption,
                                 std::string& artist) const {
  title = title_override_;
  artist = artist_override_;
  try {
    // The song card is a GH2 UI surface, so its localized caption belongs to
    // the visual/base archive even when the selected song comes from loose DLC
    // or a separately mounted GH1/GH80s archive.
    auto visual_archive = gh::ark::ArkV3Reader::load(hdr_path_);
    auto locale_entry = visual_archive.find("ui/eng/gen/locale.dtb");
    if (!locale_entry)
      locale_entry = visual_archive.find("ghui/eng/gen/locale.dtb");
    if (!locale_entry) return false;
    const auto locale =
        gh::dtb::parse(
            visual_archive.read_entry(*locale_entry, {ark_path_}));
    if (const auto row = gh::dtb::find_keyed(locale, "mtv_made_famous")) {
      const auto& children = gh::dtb::children(*row);
      if (children.size() >= 2 && children[1])
        caption = gh::dtb::as_string(*children[1]).value_or("");
    }

    if (title.empty() || artist.empty()) {
      auto content_archive = gh::ark::ArkV3Reader::load(content_hdr_path_);
      const auto songs_entry = content_archive.find("config/gen/songs.dtb");
      if (songs_entry) {
        const auto songs = ghogx::catalog::extract_songs(
            gh::dtb::parse(content_archive.read_entry(
                *songs_entry, {content_ark_path_})));
        for (const auto& song : songs) {
          if (song.shortname != song_shortname_) continue;
          if (title.empty()) title = song.display_name;
          if (artist.empty()) artist = song.artist;
          break;
        }
      }
    }
    return !title.empty() && !caption.empty() && !artist.empty();
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[ghogx] song intro text load failed: %s\n",
                 ex.what());
    return false;
  }
}

bool SongIntroOverlay::prepare() {
  ensure_loaded();
  return ready_;
}

void SongIntroOverlay::ensure_loaded() {
  if (attempted_) return;
  attempted_ = true;
  if (hdr_path_.empty() || ark_path_.empty() || song_shortname_.empty()) return;

  std::string title;
  std::string caption;
  std::string artist;
  if (!load_text(title, caption, artist)) return;

  ghogx::milo_scene::Scene scene;
  std::string overlay_path = "ui/gen/mtv_overlay.milo_ps2";
  std::string camera_path = "ui/gen/metacam.milo_ps2";
  std::string camera_name = "meta.cam";
  std::string font_path = "ui/gen/impactor_mtv.milo_ps2";
  if (!ghogx::milo_scene::load_scene(hdr_path_, ark_path_, camera_path,
                                      scene)) {
    overlay_path = "ghui/mtv_overlay.gh";
    camera_path = overlay_path;
    camera_name = "ui.cam";
    font_path = "ghui/gen/resources.rnd_ps2";
    if (!ghogx::milo_scene::load_scene(hdr_path_, ark_path_, camera_path,
                                        scene))
      return;
  }
  const auto labels =
      extract_menu_labels(hdr_path_, ark_path_, overlay_path);
  if (!font_.load(hdr_path_, ark_path_, font_path, "impactor_mtv.font"))
    return;

  std::vector<ghogx::render::MiloSceneRenderer::TextVertex> vertices;
  const float cap_height = font_.cap_height();
  const auto wrap_lines = [&](const std::string& text,
                              float max_native_width) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < text.size()) {
      while (pos < text.size() && text[pos] == ' ') ++pos;
      size_t end = text.find(' ', pos);
      if (end == std::string::npos) end = text.size();
      const std::string word = text.substr(pos, end - pos);
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && max_native_width > 0.0f &&
          font_.measure(candidate) > max_native_width) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
      pos = end;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) lines.push_back({});
    return lines;
  };

  // The packed view contains a `_shadow` Label for every face Label, but the
  // ObjectDir serialization order interleaves the six objects. Retail draws
  // the shadow layer first and the face layer second; preserving raw entry
  // order lets later coplanar shadows cut through earlier white faces.
  for (int shadow_pass = 1; shadow_pass >= 0; --shadow_pass) {
    for (const auto& label : labels) {
      if (label.name.rfind("mtv_campaign_line", 0) != 0 ||
          !label.has_world || !label.text_tail.valid)
        continue;
      const bool is_shadow = label.name.find("_shadow") != std::string::npos;
      if (is_shadow != (shadow_pass != 0)) continue;
    std::string text;
    if (label.name.find("line1") != std::string::npos)
      text = title;
    else if (label.name.find("line2") != std::string::npos)
      text = caption;
    else if (label.name.find("line3") != std::string::npos)
      text = artist;
    if (text.empty()) continue;

    const float scale = label.text_tail.text_size;
    const float max_native_width =
        label.text_tail.width_bound > 0.0f
            ? label.text_tail.width_bound * cap_height / scale
            : 0.0f;
    const auto lines = wrap_lines(text, max_native_width);
    const float leading =
        label.text_tail.leading > 0.0f ? label.text_tail.leading : 1.0f;
    const float block_height =
        scale * (1.0f + static_cast<float>(lines.size() - 1) * leading);
    float first_line_z = block_height * 0.5f - scale * 0.5f;
    if ((label.text_tail.alignment & 0x10) != 0)
      first_line_z = -scale * 0.5f;
    else if ((label.text_tail.alignment & 0x40) != 0)
      first_line_z = block_height - scale * 0.5f;
    const uint32_t color = pack_color(label.text_tail.color);

    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
      float native_width = 0.0f;
      const auto quads = font_.layout(lines[line_index], &native_width);
      float align_x = 0.0f;
      if ((label.text_tail.alignment & 4) != 0)
        align_x = -(native_width / cap_height) * scale;
      else if ((label.text_tail.alignment & 2) != 0)
        align_x = -(native_width / cap_height) * scale * 0.5f;
      const float line_z =
          first_line_z - static_cast<float>(line_index) * scale * leading;
      const auto vertex = [&](float qx, float qy, float u, float v) {
        const float local_x = (qx / cap_height) * scale + align_x;
        const float local_z =
            -((qy - cap_height * 0.5f) / cap_height) * scale + line_z;
        const auto world =
            transform_menu_text_point(label.world, local_x, local_z);
        return ghogx::render::MiloSceneRenderer::TextVertex{
            world[0], world[1], world[2], u, v, color};
      };
      for (const auto& quad : quads) {
        const auto a = vertex(quad.x0, quad.y0, quad.u0, quad.v0);
        const auto b = vertex(quad.x1, quad.y0, quad.u1, quad.v0);
        const auto c = vertex(quad.x1, quad.y1, quad.u1, quad.v1);
        const auto d = vertex(quad.x0, quad.y1, quad.u0, quad.v1);
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
      }
    }
  }
  }
  if (vertices.empty()) return;

  renderer_ =
      std::make_unique<ghogx::render::MiloSceneRenderer>(window_);
  renderer_->set_scene(std::move(scene), {});
  if (!renderer_->select_authored_camera(camera_name)) {
    renderer_.reset();
    return;
  }
  renderer_->set_text(std::move(vertices), font_.atlas());
  ready_ = true;
  std::fprintf(
      stderr,
      "[ghogx] MTV song overlay ready: title='%s' caption='%s' "
      "artist='%s' source=%s camera=%s show=1.0..6.0s\n",
      title.c_str(), caption.c_str(), artist.c_str(), overlay_path.c_str(),
      camera_name.c_str());
}

void SongIntroOverlay::draw(double song_time_seconds) {
  ensure_loaded();
  // Retail ui/gen/game.dtb::intro_start_msg hides the overlay, shows it after
  // one second, and hides it again at six seconds.
  if (!ready_ || !renderer_ || song_time_seconds < 1.0 ||
      song_time_seconds >= 6.0)
    return;
  renderer_->draw_text_over_scene();
  if (!shown_logged_) {
    shown_logged_ = true;
    std::fprintf(
        stderr,
        "[ghogx] MTV song overlay shown at t=%.3f "
        "source=ui/gen/game.dtb::intro_start_msg delay=1s hide=6s\n",
        song_time_seconds);
  }
}

}  // namespace ghogx::ui
