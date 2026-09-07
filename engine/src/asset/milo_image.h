// engine/src/asset/milo_image.h
//
// Load a decoded RGBA image from a Tex inside a PS2 MILO held in a PS2 ARK.
//
// Chains the existing readers (all PS2-native, under tools/): gh::ark to pull
// the milo bytes, gh::milo to inflate + walk the object directory, the Tex
// wrapper parser, and gh::tex to decode the embedded HMXBitmap to RGBA. This
// is the bridge that puts real PS2 game art on screen via the D3D9 blit.

#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace ghogx::asset {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // width*height*4, RGBA8 row-major
  bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Load an ordinary uncompressed Windows BMP from the loose filesystem. This
// is used for release-owned UI artwork that is intentionally kept outside the
// source game ARK (for example approved venue-select photographs).
Image load_bmp_file(const std::string& path);

// CPU reconstruction of RB2's instrument two-color material. Diffuse alpha
// interpolates primary to secondary; a valid mask preserves fixed-color
// diffuse channels. Without a mask, the primary color modulates the diffuse.
Image compose_rb2_paint(
    const Image& diffuse, const Image& mask,
    const std::array<uint8_t, 3>& primary,
    const std::array<uint8_t, 3>& secondary);
// PS2 Custom Paint uses one diffuse body color. The source diffuse retains
// authored wear/shading; the nearest-resampled RGB mask protects large fixed
// regions, and diffuse alpha protects instrument-specific detail islands such
// as Telecaster's white pickguard.
Image compose_rb2_body_paint(
    const Image& diffuse, const Image& mask,
    const std::array<uint8_t, 3>& color);
int rb2_paint_color_count();
std::array<uint8_t, 3> rb2_paint_color(int index);
const char* rb2_paint_color_name(int index);

// Load the largest decodable embedded Tex from `milo_path` inside the PS2 ARK
// (hdr_path + ark_path). "Largest" by pixel area, which reliably picks the
// dominant image (e.g. the splash poster among a panel's smaller textures).
// Returns an invalid Image (and logs a reason) on any failure.
Image load_milo_texture(const std::string& hdr_path,
                        const std::string& ark_path,
                        const std::string& milo_path);

// Load a SPECIFIC named Tex entry from a MILO. `entry_name` is the exact name
// of the Tex entry (e.g. "splash_poster.tex", "mm_brick03.tex"). Returns an
// invalid Image if the entry is not found or cannot be decoded. Case-sensitive.
Image load_milo_texture_named(const std::string& hdr_path,
                              const std::string& ark_path,
                              const std::string& milo_path,
                              const std::string& entry_name);

// Load MULTIPLE named Tex entries from one MILO in a single parse (efficient
// when many textures live in the same container, e.g. the track gem set).
// Returns a map keyed by entry name; entries that fail to decode are omitted.
std::map<std::string, Image> load_milo_textures(
    const std::string& hdr_path, const std::string& ark_path,
    const std::string& milo_path, const std::vector<std::string>& entry_names);

// Load MULTIPLE named Tex entries from the first MILO that contains each entry.
// This mirrors PS2 venue composition where overlay MILOs may reference textures
// stored in their paired geometry MILO instead of duplicating bitmap payloads.
std::map<std::string, Image> load_milo_textures_from_sources(
    const std::string& hdr_path, const std::string& ark_path,
    const std::vector<std::string>& milo_paths,
    const std::vector<std::string>& entry_names);

// Load a raw PS2 HMX bitmap entry directly from the ARK, e.g.
// track/surfaces/gen/<character>_keep.bmp_ps2 (GH2) or
// track/surfaces/gen/<character>.bmp_ps2 (GH1).
Image load_ps2_bitmap_from_ark(const std::string& hdr_path,
                               const std::string& ark_path,
                               const std::string& entry_path);

// GH2's endgame UIPicture selects loose portraits named
// ui/image/og/gen/photo_<outfit>0_keep.bmp_ps2.  The trailing zero is the
// first authored portrait variant, not part of the outfit symbol.
std::string endgame_photo_bitmap_path_for_outfit(std::string outfit_key);

// Resolve the guitarist-specific track surface selected by a character load.
// GH2 stores the playable highway art as loose PS2 bitmap entries under
// track/surfaces/gen/. The resolver first looks for an authored track/surfaces
// reference in the character MILO, then falls back to the selected outfit key.
std::string resolve_track_surface_bitmap_path(
    const std::string& hdr_path, const std::string& ark_path,
    const std::string& character_milo_path, const std::string& outfit_key);

// Convert a selected outfit/model key to the stock GH2 loose bitmap path.
std::string track_surface_bitmap_path_for_outfit(std::string outfit_key);

// Load a resolved surface reference. A full track/surfaces/... path is used
// directly; a bare outfit/model key is accepted for compatibility.
Image load_track_surface_bitmap(
    const std::string& hdr_path, const std::string& ark_path,
    const std::string& surface_ref,
    std::string* resolved_entry_path = nullptr);

Image load_track_surface_bitmap_for_outfit(
    const std::string& hdr_path, const std::string& ark_path,
    const std::string& outfit_key,
    std::string* resolved_entry_path = nullptr);

}  // namespace ghogx::asset
