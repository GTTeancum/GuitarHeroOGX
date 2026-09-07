// engine/src/ui/config_db.h
//
// ConfigDb -- loads the stock config/gen/*.dtb data tables (songs, guitars,
// store, campaign, gh2, credits, tips) the menu's game-side objects answer queries from. This
// is the DATA the original menus read, loaded verbatim via dtb_bridge -- so
// game-side answers (song titles, artists, guitar descriptions, ...) come from
// the real data, never canned constants.
//
// songs.dtb shape (verified): a list of records
//   (badreputation (name "Bad Reputation") (artist "Thin Lizzy") (song ...)
//                  (quickplay (character_outfit funk1)(guitar flying_v)(venue fest)) ...)
// so song N's title = songs[N] find_keyed "name" -> value.

#pragma once

#include "core/data_node.h"
#include "core/symbol.h"

#include "ark_v3.h"

#include <map>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace ghogx::ui {

struct CharacterVariant {
  Symbol character;
  Symbol selection;
  Symbol source_game;
  std::string label;
  std::string model_path;
  std::string ui_model_path;
  std::string ui_anim_path;
  std::string main_anim_path;
  std::string strum_anim_path;
  std::string fret_anim_path;
  std::string highway_surface_path;
  std::string portrait_path;
  // Optional source-authored instrument fallback used only when the player
  // has not selected an instrument. Add-on packages may also name its finish
  // and authored paint indices; unavailable DLC instruments fall back to the
  // normal first-guitar behavior.
  Symbol preferred_guitar;
  Symbol preferred_guitar_skin;
  int preferred_guitar_paint_primary = -1;
  int preferred_guitar_paint_secondary = -1;
  // Optional model whose animation/controller graph drives this outfit.
  // This is the general cross-skeleton retarget contract used by external
  // characters as well as the two singer-as-guitarist variants.
  std::string animation_source_model_path;
  bool retarget_animation = false;
  // Object roots hidden only while this variant fills a guitarist role.
  // Descendants are resolved from the loaded model hierarchy at runtime.
  std::vector<std::string> guitarist_hidden_roots;
  Symbol unlock_requirement;
  std::string character_label;
  std::string character_blurb;
  // Add-on outfits own their copy. An empty value intentionally renders an
  // empty description instead of synthesizing a stock localization token.
  bool addon_defined = false;
  std::string outfit_blurb;
};

struct DlcPackageSummary {
  std::string id;
  std::string name;
  std::string version;
  std::filesystem::path directory;
  std::size_t mounted_files = 0;
};

struct DlcSetlist {
  Symbol id;
  std::string label;
  std::vector<Symbol> songs;
  bool include_in_quickplay = false;
};

// Complete runtime-facing projection of one authored song record. This keeps
// disc-imported DTB records authoritative while allowing a release package to
// namespace source-game character, venue, instrument, and band identities.
struct SongRuntimeConfig {
  Symbol source_game;
  std::string midi_path;
  std::string audio_path;
  std::string character_outfit;
  std::string guitar;
  std::string venue;
  std::string anim_tempo;
  std::vector<std::string> band;
};

class ConfigDb {
 public:
  // Load the named config/gen DTBs from the ARK. Missing/unparsable files are
  // skipped (logged), so this never aborts.
  void load(const gh::ark::ArkV3Reader& ark, const std::vector<std::string>& ark_paths);
  // Merge self-contained external addon manifests after built-in DTB data.
  // Each direct child of addon_root may provide one manifest.json.
  void load_addon_manifests(
      const std::filesystem::path& addon_root,
      const gh::ark::ArkV3Reader* base_ark = nullptr);
  // Replace only the song catalog from a mounted content archive while
  // retaining the front-end's guitars/store/campaign/UI configuration.
  void load_songs(const gh::ark::ArkV3Reader& ark,
                  const std::vector<std::string>& ark_paths);

  // A loaded table by name ("songs"/"guitars"/"store"/"campaign"/"gh2"/...), or null.
  const DataArray* table(Symbol name) const;

  // --- song table convenience (the most-queried) ---
  std::size_t song_count() const;
  const DataArray* song(std::size_t index) const;      // the Nth (key (...)...)
  Symbol song_key(std::size_t index) const;            // the Nth song's symbol key
  int song_index(Symbol song) const;                   // zero-based, -1 when absent
  DataNode song_field(std::size_t index, Symbol field) const;  // name/artist/...
  // Stock campaign order followed by the authored store-song order. This is
  // the one quickplay identity sequence used by both presentation and loading.
  std::vector<Symbol> quickplay_songs() const;
  // Authored `(song (name ...))` / `(song (midi_file ...))` paths. The audio
  // path has no extension in songs.dtb.
  std::string song_audio_path(Symbol song) const;
  std::string song_midi_path(Symbol song) const;
  SongRuntimeConfig song_runtime_config(Symbol song) const;
  DataNode store_field(Symbol category, Symbol item, Symbol field) const;
  std::size_t store_item_count(Symbol category) const;
  Symbol store_item(Symbol category, std::size_t index) const;
  std::vector<Symbol> store_items(Symbol category) const;
  const std::vector<Symbol>& practice_sections() const { return practice_sections_; }

  // --- venue list (config/gen/gh2.dtb: (venues ...)) ---
  std::vector<Symbol> venues() const;
  std::size_t venue_count() const;
  bool is_venue(Symbol venue) const;
  std::string venue_label(Symbol venue) const;
  int venue_index(Symbol venue) const;  // zero-based, -1 when absent
  Symbol default_venue() const;
  std::vector<Symbol> campaign_songs(Symbol venue) const;
  Symbol campaign_venue(Symbol song) const;
  Symbol campaign_venue_at(std::size_t tier_index) const;

  // --- guitar table convenience (config/gen/guitars.dtb) ---
  const DataArray* guitar(Symbol guitar) const;
  std::vector<Symbol> guitars(Symbol type = Symbol()) const;
  Symbol first_guitar(Symbol type = Symbol("guitar")) const;
  std::size_t guitar_skin_count(Symbol guitar) const;
  Symbol guitar_skin_at(Symbol guitar, std::size_t index) const;
  Symbol first_guitar_skin(Symbol guitar) const;
  const DataArray* guitar_skin(Symbol guitar, Symbol skin) const;
  DataNode guitar_skin_field(Symbol guitar, Symbol skin, Symbol field) const;
  Symbol guitar_for_skin(Symbol skin) const;

  // Generated from each game's authored roster, locale names, and asset
  // inventory. Canonical identity and exact per-game setup stay separate.
  std::vector<Symbol> characters() const;
  // Retail ui/gen/ui.dtb macro membership, retained separately from add-on
  // manifests so DLC can extend (never replace) the native outfit roster.
  std::vector<Symbol> native_character_outfits(Symbol character) const;
  // One canonical selection order shared by menus and player configuration:
  // native GH2 first, then GH1, GH80s, and later external sources.
  std::vector<Symbol> character_outfits(Symbol character) const;
  std::vector<CharacterVariant> character_variants(Symbol character) const;
  const CharacterVariant* character_variant(Symbol selection) const;
  Symbol character_for_variant(Symbol selection) const;
  std::string character_label(Symbol character) const;
  std::string character_portrait(Symbol character) const;

  const std::vector<DlcPackageSummary>& dlc_packages() const {
    return dlc_packages_;
  }
  std::vector<Symbol> setlists() const;
  std::string setlist_label(Symbol setlist) const;
  std::vector<Symbol> setlist_songs(Symbol setlist) const;

  // Generic keyed-record field: record's `find_keyed(field)` value (at(1)).
  static DataNode field(const DataArray* record, Symbol key);

 private:
  void load_practice_sections(const gh::ark::ArkV3Reader& ark,
                              const std::vector<std::string>& ark_paths);

  std::map<const void*, std::shared_ptr<DataArray>> tables_;
  std::vector<Symbol> practice_sections_;
  std::vector<Symbol> native_characters_;
  std::vector<Symbol> native_character_outfits_;
  std::vector<CharacterVariant> character_variants_;
  std::vector<Symbol> addon_venues_;
  std::vector<std::pair<Symbol, std::string>> addon_venue_labels_;
  std::vector<Symbol> addon_quickplay_songs_;
  std::vector<DlcSetlist> addon_setlists_;
  std::vector<DlcPackageSummary> dlc_packages_;
  std::map<const void*, Symbol> addon_song_sources_;
  std::map<std::string, std::string> source_routes_;
  std::map<std::string, std::vector<std::string>> source_default_bands_;
};

}  // namespace ghogx::ui
