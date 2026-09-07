#include "ui/manage_band_panel.h"

#include "ui/config_db.h"
#include "ui/screen_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace ghogx::ui {
namespace {

constexpr int kCategoryCount = 12;
constexpr int kVisibleRows = 7;
constexpr int kSelectedRow = 2;

bool is_up(Symbol button) {
  return button == Symbol("kPad_DUp") || button == Symbol("kPad_DU");
}
bool is_down(Symbol button) {
  return button == Symbol("kPad_DDown") || button == Symbol("kPad_DD");
}
bool is_confirm(Symbol button) { return button == Symbol("kPad_X"); }
bool is_back(Symbol button) { return button == Symbol("kPad_Tri"); }

Symbol global_button(ScreenManager* manager) {
  return manager
             ? manager->get_global(Symbol("button"))
                   .as_symbol()
                   .value_or(Symbol())
             : Symbol();
}

std::string friendly(std::string value) {
  for (char& c : value)
    if (c == '_') c = ' ';
  bool cap = true;
  for (char& c : value) {
    if (cap && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    cap = c == ' ';
  }
  return value;
}

const char* canonical_venue_label(Symbol venue) {
  if (venue == Symbol("big")) return "Red Octane (GH2)";
  if (venue == Symbol("arena")) return "The Arena";
  if (venue == Symbol("fest")) return "Vans Warped Tour";
  if (venue == Symbol("theatre")) return "Rock City Theater";
  if (venue == Symbol("stone")) return "Stonehenge";
  if (venue == Symbol("small1")) return "Rat Cellar";
  if (venue == Symbol("small2")) return "Blackout Bar";
  if (venue == Symbol("battle")) return "High School";
  if (venue == Symbol("gh1_big_club")) return "Red Octane (GH1)";
  if (venue == Symbol("gh1_arena")) return "The Garden";
  if (venue == Symbol("gh1_theatre")) return "Republik Theater";
  if (venue == Symbol("gh1_fest")) return "Toxic Summer Tour";
  if (venue == Symbol("gh1_basement")) return "The Basement";
  if (venue == Symbol("gh1_small_club")) return "Freak Pit";
  return nullptr;
}

}  // namespace

ManageBandPanel::ManageBandPanel(ScreenManager* manager, ConfigDb* db)
    : UiObject(Symbol("ManageBandPanel")), db_(db) {
  set_manager(manager);
  set_property(Symbol("showing"), DataNode::Int(1));
  refresh();
}

Symbol ManageBandPanel::category_key(int category) const {
  static constexpr const char* keys[kCategoryCount] = {
      "favorite_character", "favorite_guitar", "preferred_bassist",
      "preferred_drummer", "preferred_keyboardist", "preferred_male_singer",
      "preferred_female_singer", "favorite_bass", "favorite_venue",
      "rename_band", "delete_band", "save_return"};
  return Symbol(keys[std::clamp(category, 0, kCategoryCount - 1)]);
}

std::string ManageBandPanel::category_label(int category) const {
  static constexpr const char* labels[kCategoryCount] = {
      "GUITARIST / OUTFIT", "GUITAR / FINISH", "BASSIST OUTFIT",
      "DRUMMER OUTFIT", "KEYBOARDIST OUTFIT", "MALE SINGER OUTFIT",
      "FEMALE SINGER OUTFIT", "SCRUFFY'S BASS", "FAVORITE VENUE",
      "RENAME BAND", "DELETE BAND", "SAVE & RETURN"};
  return labels[std::clamp(category, 0, kCategoryCount - 1)];
}

Symbol ManageBandPanel::preference(Symbol key) const {
  Object* campaign = manager() ? manager()->resolve_object(Symbol("campaign"))
                               : nullptr;
  if (!campaign) return Symbol();
  DataArray args;
  args.push(DataNode::Sym(key));
  return campaign->handle_property(Symbol("get_manage_preference"), args)
      .as_symbol()
      .value_or(Symbol());
}

void ManageBandPanel::set_preference(Symbol key, Symbol value) {
  Object* campaign = manager() ? manager()->resolve_object(Symbol("campaign"))
                               : nullptr;
  if (!campaign || !key.valid()) return;
  DataArray args;
  args.push(DataNode::Sym(key));
  args.push(DataNode::Sym(value));
  campaign->handle_property(Symbol("set_manage_preference"), args);
  std::fprintf(stderr, "[manage-band] saved key=%s value=%s\n", key.c_str(),
               value.valid() ? value.c_str() : "<game-default>");
}

std::string ManageBandPanel::display_label(Symbol value) const {
  if (!value.valid()) return "GAME DEFAULT";
  if (db_) {
    if (db_->is_venue(value)) {
      if (const char* canonical = canonical_venue_label(value))
        return canonical;
      const std::string authored = db_->venue_label(value);
      const std::string id = value.c_str();
      std::string label = authored.empty() ? localized_label(id)
                                           : localized_label(authored);
      return label;
    }
    const std::string character = db_->character_label(value);
    if (!character.empty()) return localized_label(character);
    if (const CharacterVariant* variant = db_->character_variant(value)) {
      if (!variant->label.empty()) return localized_label(variant->label);
    }
    // Stock outfit names live in locale.dtb as <selection>_outfit. This is
    // the same source contract used by CharacterProvider; showing the raw
    // selection id here (punk1, alterna2, etc.) is never player-facing.
    if (manager()) {
      const std::string token = std::string(value.c_str()) + "_outfit";
      const std::string localized = manager()->localize(Symbol(token.c_str()));
      if (localized != token) return localized;
    }
    if (const DataArray* guitar = db_->guitar(value)) {
      const DataNode name = ConfigDb::field(guitar, Symbol("name"));
      if (const auto text = name.as_string())
        return localized_label(std::string(*text));
      if (const auto token = name.as_symbol())
        return localized_label(token->c_str());
    }
    if (const Symbol guitar = db_->guitar_for_skin(value); guitar.valid()) {
      const DataNode name =
          db_->guitar_skin_field(guitar, value, Symbol("name"));
      if (const auto text = name.as_string())
        return localized_label(std::string(*text));
      if (const auto token = name.as_symbol())
        return localized_label(token->c_str());
    }
  }
  if (value == Symbol("metal_bass")) return "GH2";
  if (value == Symbol("gh1_metal_bass")) return "GH1";
  if (value == Symbol("metal_drummer")) return "GH2";
  if (value == Symbol("gh1_metal_drummer")) return "GH1";
  if (value == Symbol("metal_keyboard")) return "GH2";
  if (value == Symbol("gh1_metal_keyboard")) return "GH1";
  if (value == Symbol("metal_singer")) return "GH2";
  if (value == Symbol("gh1_metal_singer")) return "GH1";
  if (value == Symbol("female_singer")) return "GH2";
  if (value == Symbol("gh1_female_singer")) return "GH1";
  return friendly(value.c_str());
}

std::string ManageBandPanel::bass_display_label(Symbol value) const {
  std::string label = display_label(value);
  std::string upper = label;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
  });
  const std::size_t prefix = upper.rfind("BASS: ", 0) == 0
                                 ? 6
                                 : (upper.rfind("BASS ", 0) == 0 ? 5 : 0);
  if (prefix) label.erase(0, prefix);
  return label;
}

std::string ManageBandPanel::localized_label(std::string value) const {
  if (value.empty() || !manager()) return value;
  const std::string localized = manager()->localize(Symbol(value.c_str()));
  // Locale tables return the token unchanged when it is absent. Preserve
  // authored literal DLC labels, but make bare internal identifiers readable.
  return localized == value ? friendly(std::move(value)) : localized;
}

std::string ManageBandPanel::character_display_label(Symbol value) const {
  if (!value.valid()) return "GAME DEFAULT";
  if (manager()) {
    const std::string localized = manager()->localize(value);
    if (localized != value.c_str()) return localized;
  }
  if (db_) {
    const std::string catalog = db_->character_label(value);
    if (!catalog.empty()) return localized_label(catalog);
  }
  return friendly(value.c_str());
}

std::vector<ManageBandPanel::Choice> ManageBandPanel::choices_for_category(
    int category) const {
  std::vector<Choice> out;
  const auto add = [&](Symbol id, std::string model = {}, std::string anim = {}) {
    out.push_back({id, display_label(id), std::move(model), std::move(anim)});
  };
  if (!db_) return out;
  switch (category) {
    case 0: {
      if (stage_ == Stage::Categories || flow_step_ == 0) {
        for (Symbol id : db_->characters())
          out.push_back({id, character_display_label(id), {}, {}});
        break;
      }
      Symbol character = pending_parent_;
      if (!character.valid()) character = preference(Symbol("favorite_character"));
      if (!character.valid() && !db_->characters().empty())
        character = db_->characters().front();
      for (Symbol id : db_->character_outfits(character)) add(id);
      break;
    }
    case 1: {
      if (stage_ == Stage::Categories || flow_step_ == 0) {
        for (Symbol id : db_->guitars(Symbol("guitar"))) add(id);
        break;
      }
      Symbol guitar = pending_parent_;
      if (!guitar.valid()) guitar = preference(Symbol("favorite_guitar"));
      if (!guitar.valid()) guitar = db_->first_guitar(Symbol("guitar"));
      for (std::size_t i = 0; i < db_->guitar_skin_count(guitar); ++i)
        add(db_->guitar_skin_at(guitar, i));
      break;
    }
    case 2:
      add(Symbol("metal_bass"), "char/metal_bass/og/gen/metal_bass.milo_ps2",
          "char/metal_bass/anims/gen/bass_main.milo_ps2");
      add(Symbol("gh1_metal_bass"),
          "char/gh1_metal_bass/og/gen/gh1_metal_bass.milo_ps2",
          "char/gh1_metal_bass/anims/gen/metal_bass_main.milo_ps2");
      break;
    case 3:
      add(Symbol("metal_drummer"),
          "char/metal_drummer/og/gen/metal_drummer.milo_ps2",
          "char/metal_drummer/anims/gen/drummer_main.milo_ps2");
      add(Symbol("gh1_metal_drummer"),
          "char/gh1_metal_drummer/og/gen/gh1_metal_drummer.milo_ps2",
          "char/gh1_metal_drummer/anims/gen/metal_drummer_main.milo_ps2");
      break;
    case 4:
      add(Symbol("metal_keyboard"),
          "char/metal_keyboard/og/gen/metal_keyboard.milo_ps2",
          "char/metal_keyboard/anims/gen/keyboard_main.milo_ps2");
      add(Symbol("gh1_metal_keyboard"),
          "char/gh1_metal_keyboard/og/gen/gh1_metal_keyboard.milo_ps2",
          "char/gh1_metal_keyboard/anims/gen/metal_keyboard_main.milo_ps2");
      break;
    case 5:
      add(Symbol("metal_singer"),
          "char/metal_singer/og/gen/metal_singer.milo_ps2",
          "char/metal_singer/anims/gen/singer_main.milo_ps2");
      add(Symbol("gh1_metal_singer"),
          "char/gh1_metal_singer/og/gen/gh1_metal_singer.milo_ps2",
          "char/gh1_metal_singer/anims/gen/metal_singer_main.milo_ps2");
      break;
    case 6:
      add(Symbol("female_singer"),
          "char/female_singer/og/gen/female_singer.milo_ps2",
          "char/female_singer/anims/gen/singer_main.milo_ps2");
      add(Symbol("gh1_female_singer"),
          "char/gh1_female_singer/og/gen/gh1_female_singer.milo_ps2",
          "char/gh1_female_singer/anims/gen/female_singer_main.milo_ps2");
      break;
    case 7:
      for (Symbol id : db_->guitars(Symbol("bass")))
        out.push_back({id, bass_display_label(id), {}, {}});
      break;
    case 8:
      for (Symbol id : db_->venues()) {
        // GH1's small_club_multi is an internal multiplayer scene variant,
        // not a distinct player-facing venue.
        if (id == Symbol("gh1_small_club_multi")) continue;
        add(id);
      }
      break;
    default:
      break;
  }
  return out;
}

int ManageBandPanel::choice_index(const std::vector<Choice>& choices,
                                  Symbol selected) const {
  const auto it = std::find_if(choices.begin(), choices.end(),
                               [&](const Choice& row) {
                                 return row.id == selected;
                               });
  return it == choices.end() ? 0 : static_cast<int>(it - choices.begin());
}

void ManageBandPanel::enter() {
  stage_ = Stage::Categories;
  category_ = 9;
  flow_step_ = 0;
  pending_parent_ = Symbol();
  refresh();
  std::fprintf(stderr, "[manage-band] enter categories=%d\n", kCategoryCount);
}

void ManageBandPanel::move(int direction) {
  if (stage_ == Stage::Categories) {
    category_ = (category_ + direction + kCategoryCount) % kCategoryCount;
  } else {
    const auto choices = choices_for_category(category_);
    if (!choices.empty())
      value_index_ = (value_index_ + direction +
                      static_cast<int>(choices.size())) %
                     static_cast<int>(choices.size());
  }
  refresh();
  std::fprintf(stderr,
               "[manage-band] selection stage=%s category=%d key=%s "
               "value_index=%d\n",
               stage_ == Stage::Categories ? "categories" : "values",
               category_, category_key(category_).c_str(), value_index_);
}

void ManageBandPanel::confirm() {
  if (stage_ == Stage::Values) {
    const auto choices = choices_for_category(category_);
    if (choices.empty()) return;
    const Symbol selected =
        choices[static_cast<std::size_t>(value_index_)].id;
    if ((category_ == 0 || category_ == 1) && flow_step_ == 0) {
      pending_parent_ = selected;
      flow_step_ = 1;
      const Symbol stored = preference(
          Symbol(category_ == 0 ? "favorite_outfit" : "favorite_guitar_skin"));
      value_index_ = choice_index(choices_for_category(category_), stored);
      refresh();
      std::fprintf(stderr,
                   "[manage-band] guided-flow category=%d parent=%s step=1\n",
                   category_, pending_parent_.c_str());
      return;
    }
    if (category_ == 0) {
      set_preference(Symbol("favorite_character"), pending_parent_);
      set_preference(Symbol("favorite_outfit"), selected);
    } else if (category_ == 1) {
      set_preference(Symbol("favorite_guitar"), pending_parent_);
      set_preference(Symbol("favorite_guitar_skin"), selected);
    } else {
      set_preference(category_key(category_), selected);
    }
    stage_ = Stage::Categories;
    flow_step_ = 0;
    pending_parent_ = Symbol();
    refresh();
    return;
  }
  if (category_ < 9) {
    flow_step_ = 0;
    pending_parent_ = Symbol();
    const auto choices = choices_for_category(category_);
    Symbol stored = preference(category_key(category_));
    value_index_ = choice_index(choices, stored);
    stage_ = Stage::Values;
    refresh();
    std::fprintf(stderr,
                 "[manage-band] opened key=%s choices=%zu value_index=%d\n",
                 category_key(category_).c_str(), choices.size(),
                 value_index_);
    return;
  }
  Object* screen = manager() ? manager()->find_object(Symbol("manage_band_screen"))
                             : nullptr;
  const int slot = screen
                       ? screen->get_property(Symbol("profile_slot"))
                             .as_int()
                             .value_or(-1)
                       : -1;
  if (category_ == 9) {
    if (Object* target = manager()->find_object(Symbol("nameprof_screen"))) {
      target->set_property(Symbol("profile_slot"), DataNode::Int(slot));
      target->set_property(Symbol("back_screen"),
                           DataNode::Sym(Symbol("manage_band_screen")));
      target->set_property(Symbol("next_screen"),
                           DataNode::Sym(Symbol("options_screen")));
      target->set_property(Symbol("is_editing"), DataNode::Int(1));
      manager()->goto_screen(Symbol("nameprof_screen"));
    }
  } else if (category_ == 10) {
    if (Object* target = manager()->resolve_object(Symbol("delete_confirm")))
      target->set_property(Symbol("selected_slot"), DataNode::Int(slot));
    manager()->goto_screen(Symbol("delete_confirm"));
  } else {
    manager()->go_back();
  }
}

void ManageBandPanel::back() {
  if (stage_ == Stage::Values) {
    if ((category_ == 0 || category_ == 1) && flow_step_ == 1) {
      flow_step_ = 0;
      const auto choices = choices_for_category(category_);
      value_index_ = choice_index(choices, pending_parent_);
      refresh();
      return;
    }
    stage_ = Stage::Categories;
    flow_step_ = 0;
    pending_parent_ = Symbol();
    refresh();
  } else if (manager()) {
    manager()->go_back();
  }
}

void ManageBandPanel::update_preview() {
  if (!manager()) return;
  set_property(Symbol("preview_venue"), DataNode());
  Object* chars = manager()->find_object(Symbol("manage_band_char_preview"));
  Object* guitar = manager()->find_object(Symbol("manage_band_guitar_preview"));
  if (chars) chars->set_property(Symbol("showing"), DataNode::Int(0));
  if (guitar) guitar->set_property(Symbol("showing"), DataNode::Int(0));

  int preview_category = category_;
  Symbol selected = stage_ == Stage::Values
                        ? Symbol()
                        : preference(category_key(category_));
  const auto choices = choices_for_category(category_);
  const Choice* choice = nullptr;
  if (!choices.empty()) {
    int index = stage_ == Stage::Values
                    ? std::clamp(value_index_, 0,
                                 static_cast<int>(choices.size() - 1))
                    : choice_index(choices, selected);
    choice = &choices[static_cast<std::size_t>(index)];
    selected = choice->id;
  }
  if (preview_category == 0) {
    if (stage_ == Stage::Values && flow_step_ == 1) {
      // `selected` is already the highlighted outfit.
    } else {
      const Symbol character = selected.valid() ? selected : pending_parent_;
      Symbol outfit = preference(Symbol("favorite_outfit"));
      const auto outfits = db_->character_outfits(character);
      if (std::find(outfits.begin(), outfits.end(), outfit) == outfits.end())
        outfit = outfits.empty() ? Symbol() : outfits.front();
      selected = outfit;
    }
  }
  if (preview_category == 0 ||
      (preview_category >= 2 && preview_category <= 6)) {
    if (chars && selected.valid()) {
      const bool lead_guitarist = preview_category == 0;
      chars->set_property(Symbol("showing"), DataNode::Int(1));
      chars->set_property(Symbol("num_placers"), DataNode::Int(1));
      chars->set_property(Symbol("char_outfit_0"), DataNode::Sym(selected));
      chars->set_property(Symbol("char_loaded_0"), DataNode::Int(0));
      chars->set_property(Symbol("char_event_0"),
                          DataNode::Sym(Symbol("animate")));
      // Lead guitarists have a compact, instrument-free UI pose and need to
      // move into the visual centre of the 40% bay. Backing performers retain
      // their wider instrument/microphone silhouette; lift those only slightly.
      chars->set_property(Symbol("preview_offset_x"),
                          DataNode::Float(lead_guitarist ? 14.0f : 0.0f));
      chars->set_property(Symbol("preview_offset_z"),
                          DataNode::Float(lead_guitarist ? 9.5f : 3.0f));
      if (choice && !choice->model_path.empty()) {
        chars->set_property(Symbol("preview_model_path"),
                            DataNode::Str(choice->model_path));
        chars->set_property(Symbol("preview_anim_path"),
                            DataNode::Str(choice->animation_path));
      } else {
        chars->set_property(Symbol("preview_model_path"), DataNode());
        chars->set_property(Symbol("preview_anim_path"), DataNode());
      }
    }
  } else if (preview_category == 1 || preview_category == 7) {
    Symbol instrument = selected;
    if (preview_category == 1 && stage_ == Stage::Values && flow_step_ == 1)
      instrument = pending_parent_;
    else if (preview_category == 1 && stage_ != Stage::Values)
      instrument = preference(Symbol("favorite_guitar"));
    if (guitar && instrument.valid()) {
      guitar->set_property(Symbol("showing"), DataNode::Int(1));
      guitar->set_property(Symbol("guitar"), DataNode::Sym(instrument));
      Symbol skin;
      if (preview_category == 1 && stage_ == Stage::Values && flow_step_ == 1)
        skin = selected;
      else skin = db_->first_guitar_skin(instrument);
      guitar->set_property(Symbol("guitar_skin"), DataNode::Sym(skin));
    }
  } else if (preview_category == 8 && selected.valid()) {
    // Venue selection uses the approved static establishing shots in the
    // screen's left preview bay. Keep the stable runtime ID on the panel so
    // the renderer can resolve release-owned artwork without hard-coding the
    // visible label or depending on list position.
    set_property(Symbol("preview_venue"), DataNode::Sym(selected));
  }
}

void ManageBandPanel::refresh() {
  set_property(Symbol("stage"),
               DataNode::Sym(Symbol(stage_ == Stage::Categories ? "categories"
                                                               : "values")));
  set_property(Symbol("category"), DataNode::Int(category_));
  set_property(Symbol("value_index"), DataNode::Int(value_index_));
  set_property(Symbol("title"), DataNode::Str("MANAGE BAND"));
  const int selected = stage_ == Stage::Categories ? category_ : value_index_;
  const int count = stage_ == Stage::Categories
                        ? kCategoryCount
                        : static_cast<int>(choices_for_category(category_).size());
  set_property(Symbol("selected"), DataNode::Int(selected));
  set_property(Symbol("row_count"), DataNode::Int(count));
  for (int row = 0; row < kVisibleRows; ++row) {
    std::string text;
    if (count > 0) {
      int index = 0;
      bool show_row = false;
      if (count >= kVisibleRows) {
        index = (selected - kSelectedRow + row + count * 2) % count;
        show_row = true;
      } else {
        // Small lists appear once rather than repeating to fill the reel. Keep
        // the active choice on the third line so category and value screens
        // share one stable focus position.
        const int first_row =
            std::clamp(kSelectedRow - selected, 0, kVisibleRows - count);
        show_row = row >= first_row && row < first_row + count;
        if (show_row) index = row - first_row;
      }
      if (!show_row) {
        set_property(Symbol(("row_text_" + std::to_string(row)).c_str()),
                     DataNode::Str(""));
        continue;
      }
      if (stage_ == Stage::Categories) {
        text = category_label(index);
      } else {
        const auto choices = choices_for_category(category_);
        text = choices[static_cast<std::size_t>(index)].label;
      }
      set_property(Symbol(("row_index_" + std::to_string(row)).c_str()),
                   DataNode::Int(index));
    }
    set_property(Symbol(("row_text_" + std::to_string(row)).c_str()),
                 DataNode::Str(text));
  }
  update_preview();
}

void ManageBandPanel::handle_button(Symbol button) {
  if (is_up(button)) move(-1);
  else if (is_down(button)) move(1);
  else if (is_confirm(button)) confirm();
  else if (is_back(button)) back();
}

DataNode ManageBandPanel::handle_property(Symbol msg, const DataArray& args) {
  if (msg == Symbol("enter")) {
    enter();
    return DataNode();
  }
  if (msg == Symbol("BUTTON_DOWN_MSG")) {
    handle_button(global_button(manager()));
    return DataNode();
  }
  if (msg == Symbol("debug_select_category")) {
    category_ = std::clamp(args.size() ? args.at(0).as_int().value_or(0) : 0,
                           0, kCategoryCount - 1);
    stage_ = Stage::Categories;
    refresh();
    return DataNode();
  }
  if (msg == Symbol("debug_open_category")) {
    category_ = std::clamp(args.size() ? args.at(0).as_int().value_or(0) : 0,
                           0, kCategoryCount - 1);
    stage_ = Stage::Values;
    flow_step_ = 0;
    pending_parent_ = Symbol();
    const auto choices = choices_for_category(category_);
    const int requested =
        args.size() > 1 ? args.at(1).as_int().value_or(0) : 0;
    value_index_ = choices.empty()
                       ? 0
                       : std::clamp(requested, 0,
                                    static_cast<int>(choices.size() - 1));
    refresh();
    return DataNode();
  }
  if (msg == Symbol("debug_open_venue")) {
    category_ = 8;
    stage_ = Stage::Values;
    flow_step_ = 0;
    pending_parent_ = Symbol();
    const Symbol requested =
        args.size() ? args.at(0).as_symbol().value_or(Symbol()) : Symbol();
    value_index_ = choice_index(choices_for_category(category_), requested);
    refresh();
    return DataNode();
  }
  return UiObject::handle_property(msg, args);
}

void install_manage_band_screen(ScreenManager& manager, ConfigDb& db) {
  Object* screen = manager.find_object(Symbol("manage_band_screen"));
  if (!screen || manager.find_object(Symbol("manage_band_preferences_panel")))
    return;

  auto panel = std::make_unique<ManageBandPanel>(&manager, &db);
  panel->set_name(Symbol("manage_band_preferences_panel"));
  manager.add_object(std::move(panel));

  auto chars = std::make_unique<UiObject>(Symbol("CharsysPanel"));
  chars->set_name(Symbol("manage_band_char_preview"));
  chars->set_manager(&manager);
  chars->set_property(Symbol("showing"), DataNode::Int(0));
  chars->set_property(Symbol("num_placers"), DataNode::Int(1));
  chars->set_property(Symbol("char_placer_0"),
                      DataNode::Sym(Symbol("char_multi0.placer")));
  chars->set_property(Symbol("char_env_0"),
                      DataNode::Sym(Symbol("character01.env")));
  manager.add_object(std::move(chars));

  auto guitar = std::make_unique<UiObject>(Symbol("GuitarDisplayPanel"));
  guitar->set_name(Symbol("manage_band_guitar_preview"));
  guitar->set_manager(&manager);
  guitar->set_property(Symbol("showing"), DataNode::Int(0));
  guitar->set_property(Symbol("guitar_proxy"),
                       DataNode::Sym(Symbol("guitar_multi0.pxy")));
  guitar->set_property(Symbol("guitar_filter"),
                       DataNode::Sym(Symbol("guitar_multi0.filt")));
  guitar->set_property(Symbol("guitar_display_env"),
                       DataNode::Sym(Symbol("guitar01.env")));
  manager.add_object(std::move(guitar));

  auto panels = std::make_shared<DataArray>();
  if (auto existing = screen->get_property(Symbol("panels")).as_array())
    for (std::size_t i = 0; i < existing->size(); ++i)
      panels->push(existing->at(i));
  panels->push(DataNode::Sym(Symbol("manage_band_char_preview")));
  panels->push(DataNode::Sym(Symbol("manage_band_guitar_preview")));
  panels->push(DataNode::Sym(Symbol("manage_band_preferences_panel")));
  screen->set_property(Symbol("panels"), DataNode::Array(panels));
  screen->set_property(Symbol("focus"),
                       DataNode::Sym(Symbol("manage_band_preferences_panel")));
  // Reuse the source-authored two-player character-select room and camera.
  // Its portrait rack and P2 selector are presentation-only and are masked by
  // the renderer; P1's authored placer becomes Manage Band's live preview bay.
  if (Object* backdrop = manager.find_object(Symbol("manage_band_panel")))
    backdrop->set_property(Symbol("file"),
                           DataNode::Sym(Symbol("multi_sel_character.milo")));
  std::fprintf(stderr,
               "[manage-band] installed stock-screen extension categories=%d\n",
               kCategoryCount);
}

}  // namespace ghogx::ui
