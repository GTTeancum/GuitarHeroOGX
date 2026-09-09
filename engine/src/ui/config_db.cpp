// engine/src/ui/config_db.cpp -- see config_db.h.

#include "ui/config_db.h"

#include "chart/midi_reader.h"
#include "dtb_bridge/dtb_bridge.h"

#include "dtb.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>
#include <unordered_set>

namespace ghogx::ui {

namespace {
namespace fs = std::filesystem;

bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::string lower_ascii(std::string_view value) {
  std::string out(value);
  for (char& ch : out)
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

std::string source_route_key(std::string_view source, std::string_view kind,
                             std::string_view value) {
  return lower_ascii(source) + "\n" + lower_ascii(kind) + "\n" +
         lower_ascii(value);
}

void flatten_dtb_symbols(const gh::dtb::Node& node,
                         std::vector<Symbol>& out) {
  if (gh::dtb::is_array(node)) {
    for (const auto& child : gh::dtb::children(node))
      if (child) flatten_dtb_symbols(*child, out);
    return;
  }
  // Macro bodies are authored as bare symbols. Do not turn quoted strings or
  // preprocessor directives into roster identities.
  if (node.tag != 0x05) return;
  if (auto value = gh::dtb::as_string(node); value && !value->empty())
    out.emplace_back(*value);
}

std::vector<Symbol> dtb_macro_symbols(const gh::dtb::Tree& tree,
                                      std::string_view macro) {
  std::vector<Symbol> out;
  for (std::size_t i = 0; i + 1 < tree.root.size(); ++i) {
    const auto& directive = tree.root[i];
    if (!directive || directive->tag != 0x20) continue;
    const auto name = gh::dtb::as_string(*directive);
    if (!name || *name != macro || !tree.root[i + 1]) continue;
    flatten_dtb_symbols(*tree.root[i + 1], out);
    break;
  }
  return out;
}

Symbol first_campaign_song(const ConfigDb& db) {
  const DataArray* campaign = db.table(Symbol("campaign"));
  auto order = campaign ? campaign->find_keyed(Symbol("order")) : nullptr;
  if (!order) return Symbol("shoutatthedevil");
  for (std::size_t i = 1; i < order->size(); ++i) {
    auto tier = order->at(i).as_array();
    if (!tier || tier->size() < 2) continue;
    Symbol song = tier->at(1).as_symbol().value_or(Symbol());
    if (song.valid()) return song;
  }
  return Symbol("shoutatthedevil");
}

struct JsonValue {
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };
  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  JsonValue parse() {
    JsonValue value = parse_value();
    skip_ws();
    if (pos_ != input_.size()) fail("trailing data");
    return value;
  }

 private:
  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string("json: ") + message);
  }

  void skip_ws() {
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_])))
      ++pos_;
  }

  char peek() {
    skip_ws();
    return pos_ < input_.size() ? input_[pos_] : '\0';
  }

  bool consume(char ch) {
    skip_ws();
    if (pos_ >= input_.size() || input_[pos_] != ch) return false;
    ++pos_;
    return true;
  }

  void expect(char ch) {
    if (!consume(ch)) fail("unexpected token");
  }

  JsonValue parse_value() {
    skip_ws();
    if (pos_ >= input_.size()) fail("unexpected end");
    const char ch = input_[pos_];
    if (ch == '"') return parse_string_value();
    if (ch == '{') return parse_object();
    if (ch == '[') return parse_array();
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
      return parse_number();
    if (match_literal("true")) {
      JsonValue value;
      value.type = JsonValue::Type::kBool;
      value.boolean = true;
      return value;
    }
    if (match_literal("false")) {
      JsonValue value;
      value.type = JsonValue::Type::kBool;
      return value;
    }
    if (match_literal("null")) return JsonValue();
    fail("invalid value");
  }

  bool match_literal(std::string_view literal) {
    skip_ws();
    if (input_.substr(pos_, literal.size()) != literal) return false;
    pos_ += literal.size();
    return true;
  }

  JsonValue parse_string_value() {
    JsonValue value;
    value.type = JsonValue::Type::kString;
    value.string = parse_string();
    return value;
  }

  unsigned parse_hex4() {
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
      if (pos_ >= input_.size()) fail("short unicode escape");
      const char ch = input_[pos_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9')
        value |= static_cast<unsigned>(ch - '0');
      else if (ch >= 'a' && ch <= 'f')
        value |= static_cast<unsigned>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F')
        value |= static_cast<unsigned>(ch - 'A' + 10);
      else
        fail("invalid unicode escape");
    }
    return value;
  }

  static void append_utf8(std::string& out, unsigned codepoint) {
    if (codepoint <= 0x7f) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    while (pos_ < input_.size()) {
      const char ch = input_[pos_++];
      if (ch == '"') return out;
      if (ch != '\\') {
        if (static_cast<unsigned char>(ch) < 0x20)
          fail("unescaped control character");
        out.push_back(ch);
        continue;
      }
      if (pos_ >= input_.size()) fail("bad escape");
      const char esc = input_[pos_++];
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          out.push_back(esc);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          unsigned codepoint = parse_hex4();
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (pos_ + 2 > input_.size() || input_[pos_] != '\\' ||
                input_[pos_ + 1] != 'u')
              fail("missing low surrogate");
            pos_ += 2;
            const unsigned low = parse_hex4();
            if (low < 0xdc00 || low > 0xdfff)
              fail("invalid low surrogate");
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (low - 0xdc00);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            fail("unexpected low surrogate");
          }
          append_utf8(out, codepoint);
          break;
        }
        default:
          fail("unsupported escape");
      }
    }
    fail("unterminated string");
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    if (input_[pos_] == '-') ++pos_;
    if (pos_ >= input_.size()) fail("invalid number");
    if (input_[pos_] == '0') {
      ++pos_;
      if (pos_ < input_.size() &&
          std::isdigit(static_cast<unsigned char>(input_[pos_])))
        fail("leading zero in number");
    } else {
      if (!std::isdigit(static_cast<unsigned char>(input_[pos_])))
        fail("invalid number");
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      if (pos_ >= input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[pos_])))
        fail("invalid fraction");
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
    }
    if (pos_ < input_.size() &&
        (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() &&
          (input_[pos_] == '+' || input_[pos_] == '-'))
        ++pos_;
      if (pos_ >= input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[pos_])))
        fail("invalid exponent");
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
    }
    JsonValue value;
    value.type = JsonValue::Type::kNumber;
    value.number = std::stod(std::string(input_.substr(start, pos_ - start)));
    return value;
  }

  JsonValue parse_array() {
    JsonValue value;
    value.type = JsonValue::Type::kArray;
    expect('[');
    if (consume(']')) return value;
    for (;;) {
      value.array.push_back(parse_value());
      if (consume(']')) return value;
      expect(',');
    }
  }

  JsonValue parse_object() {
    JsonValue value;
    value.type = JsonValue::Type::kObject;
    expect('{');
    if (consume('}')) return value;
    for (;;) {
      skip_ws();
      if (peek() != '"') fail("object key must be a string");
      std::string key = parse_string();
      expect(':');
      value.object.emplace(std::move(key), parse_value());
      if (consume('}')) return value;
      expect(',');
    }
  }

  std::string_view input_;
  std::size_t pos_ = 0;
};

const JsonValue* json_member(const JsonValue& value, const char* key) {
  if (value.type != JsonValue::Type::kObject) return nullptr;
  const auto found = value.object.find(key);
  return found == value.object.end() ? nullptr : &found->second;
}

std::string json_string(const JsonValue& value, const char* key) {
  const JsonValue* member = json_member(value, key);
  return member && member->type == JsonValue::Type::kString ? member->string
                                                            : std::string();
}

const std::vector<JsonValue>* json_array(const JsonValue& value,
                                         const char* key) {
  const JsonValue* member = json_member(value, key);
  return member && member->type == JsonValue::Type::kArray ? &member->array
                                                           : nullptr;
}

bool json_bool(const JsonValue& value, const char* key,
               bool fallback = false) {
  const JsonValue* member = json_member(value, key);
  return member && member->type == JsonValue::Type::kBool
             ? member->boolean
             : fallback;
}

int json_int(const JsonValue& value, const char* key, int fallback = 0) {
  const JsonValue* member = json_member(value, key);
  return member && member->type == JsonValue::Type::kNumber
             ? static_cast<int>(member->number)
             : fallback;
}

std::vector<std::string> json_strings(const JsonValue& value,
                                      const char* key) {
  std::vector<std::string> out;
  const auto* values = json_array(value, key);
  if (!values) return out;
  for (const JsonValue& item : *values) {
    if (item.type != JsonValue::Type::kString)
      throw std::runtime_error(std::string(key) + " must contain strings");
    out.push_back(item.string);
  }
  return out;
}

std::string normalized_virtual_path(const std::string& value) {
  if (value.empty()) return {};
  std::string path = value;
  std::replace(path.begin(), path.end(), '\\', '/');
  while (starts_with(path, "./")) path.erase(0, 2);
  const fs::path parsed(path);
  if (parsed.is_absolute() || parsed.has_root_name())
    throw std::runtime_error("asset path must be ARK-relative: " + value);
  const std::string normalized = parsed.lexically_normal().generic_string();
  if (normalized.empty() || normalized == "." || normalized == ".." ||
      starts_with(normalized, "../"))
    throw std::runtime_error("asset path escapes package content: " + value);
  return normalized;
}

std::string normalized_manifest_path(const fs::path& addon_dir,
                                     const std::string& value) {
  (void)addon_dir;
  if (value.empty()) return {};
  return normalized_virtual_path(value);
}

std::shared_ptr<DataArray> keyed_node(Symbol key, DataNode value) {
  auto row = std::make_shared<DataArray>();
  row->push(DataNode::Sym(key));
  row->push(std::move(value));
  return row;
}

void push_keyed(DataArray& record, Symbol key, DataNode value) {
  record.push(DataNode::Array(keyed_node(key, std::move(value))));
}

std::shared_ptr<DataArray> json_record(Symbol id, const JsonValue& object,
                                       const std::set<std::string>& skip = {}) {
  if (object.type != JsonValue::Type::kObject)
    throw std::runtime_error("catalog row must be an object");
  auto record = std::make_shared<DataArray>();
  record->push(DataNode::Sym(id));
  const std::set<std::string> symbol_fields = {
      "type", "character", "character_outfit", "guitar", "outfit",
      "mat", "venue", "require", "difficulty", "source"};
  for (const auto& [key, value] : object.object) {
    if (key == "id" || skip.count(key)) continue;
    if (value.type == JsonValue::Type::kString) {
      push_keyed(*record, Symbol(key),
                 symbol_fields.count(key) ? DataNode::Sym(Symbol(value.string))
                                          : DataNode::Str(value.string));
    } else if (value.type == JsonValue::Type::kBool) {
      push_keyed(*record, Symbol(key), DataNode::Int(value.boolean ? 1 : 0));
    } else if (value.type == JsonValue::Type::kNumber) {
      const int integer = static_cast<int>(value.number);
      push_keyed(*record, Symbol(key),
                 value.number == static_cast<double>(integer)
                     ? DataNode::Int(integer)
                     : DataNode::Float(static_cast<float>(value.number)));
    } else if (value.type == JsonValue::Type::kObject) {
      record->push(DataNode::Array(json_record(Symbol(key), value)));
    } else if (value.type == JsonValue::Type::kArray) {
      auto array = std::make_shared<DataArray>();
      array->push(DataNode::Sym(Symbol(key)));
      for (const JsonValue& item : value.array) {
        if (item.type == JsonValue::Type::kString) {
          array->push(DataNode::Sym(Symbol(item.string)));
        } else if (item.type == JsonValue::Type::kObject) {
          const std::string item_id = json_string(item, "id");
          if (item_id.empty())
            throw std::runtime_error(key + " object requires id");
          array->push(DataNode::Array(json_record(Symbol(item_id), item)));
        } else {
          throw std::runtime_error(key + " array has unsupported item");
        }
      }
      record->push(DataNode::Array(array));
    }
  }
  return record;
}

bool has_selection(const std::vector<CharacterVariant>& variants,
                   Symbol selection) {
  return std::any_of(variants.begin(), variants.end(),
                     [&](const CharacterVariant& variant) {
                       return variant.selection == selection;
                     });
}

}  // namespace

void ConfigDb::load(const gh::ark::ArkV3Reader& ark, const std::vector<std::string>& ark_paths) {
  // A new front-end boot owns a new deterministic DLC mount set.  Base DTBs
  // are intentionally parsed before any loose content becomes visible.
  gh::ark::ArkV3Reader::clear_loose_file_mounts();
  addon_venues_.clear();
  addon_venue_labels_.clear();
  addon_quickplay_songs_.clear();
  addon_setlists_.clear();
  dlc_packages_.clear();
  addon_song_sources_.clear();
  source_routes_.clear();
  source_default_bands_.clear();
  native_characters_.clear();
  native_character_outfits_.clear();
  static const struct { const char* name; const char* path; } kFiles[] = {
      {"songs", "config/gen/songs.dtb"},     {"guitars", "config/gen/guitars.dtb"},
      {"store", "config/gen/store.dtb"},     {"campaign", "config/gen/campaign.dtb"},
      {"gh2", "config/gen/gh2.dtb"},         {"credits", "config/gen/credits.dtb"},
      {"tips", "config/gen/tips.dtb"},
      {"ui", "ui/gen/ui.dtb"},
      {"character_variants", "config/gen/character_variants.dtb"}};
  for (const auto& f : kFiles) {
    try {
      auto entry = ark.find(f.path);
      if (!entry) continue;
      std::vector<uint8_t> bytes = ark.read_entry(*entry, ark_paths);
      gh::dtb::Tree tree = gh::dtb::parse(bytes);
      if (std::string_view(f.name) == "ui") {
        native_characters_ = dtb_macro_symbols(tree, "CHARACTERS");
        native_character_outfits_ =
            dtb_macro_symbols(tree, "LOAD_CHARACTERS");
        std::fprintf(stderr,
                     "[configdb] native character macros: characters=%zu "
                     "outfits=%zu\n",
                     native_characters_.size(),
                     native_character_outfits_.size());
      }
      tables_[Symbol(f.name).id()] = dtb_bridge::from_tree(tree);
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "[configdb] %s: %s\n", f.path, ex.what());
    }
  }
  character_variants_.clear();
  if (const DataArray* catalog = table(Symbol("character_variants"))) {
    auto text_field = [](const DataArray* record, Symbol key) {
      DataNode value = ConfigDb::field(record, key);
      if (auto text = value.as_string()) return std::string(*text);
      if (auto symbol = value.as_symbol())
        return std::string(symbol->c_str());
      return std::string();
    };
    for (std::size_t character_index = 0;
         character_index < catalog->size(); ++character_index) {
      auto character_record = catalog->at(character_index).as_array();
      if (!character_record || character_record->empty()) continue;
      const Symbol character =
          character_record->at(0).as_symbol().value_or(Symbol());
      if (!character.valid()) continue;
      for (std::size_t variant_index = 1;
           variant_index < character_record->size(); ++variant_index) {
        auto variant_record =
            character_record->at(variant_index).as_array();
        if (!variant_record || variant_record->empty()) continue;
        CharacterVariant variant;
        variant.character = character;
        variant.selection =
            variant_record->at(0).as_symbol().value_or(Symbol());
        variant.source_game =
            field(variant_record.get(), Symbol("source"))
                .as_symbol()
                .value_or(Symbol());
        variant.label = text_field(variant_record.get(), Symbol("label"));
        variant.model_path =
            text_field(variant_record.get(), Symbol("model"));
        variant.ui_model_path =
            text_field(variant_record.get(), Symbol("ui_model"));
        variant.ui_anim_path =
            text_field(variant_record.get(), Symbol("ui_anim"));
        variant.main_anim_path =
            text_field(variant_record.get(), Symbol("main_anim"));
        variant.strum_anim_path =
            text_field(variant_record.get(), Symbol("strum_anim"));
        variant.fret_anim_path =
            text_field(variant_record.get(), Symbol("fret_anim"));
        variant.highway_surface_path =
            text_field(variant_record.get(), Symbol("highway_surface"));
        variant.portrait_path =
            text_field(variant_record.get(), Symbol("portrait"));
        variant.animation_source_model_path = text_field(
            variant_record.get(), Symbol("animation_source_model"));
        variant.retarget_animation =
            field(variant_record.get(), Symbol("retarget_animation"))
                    .as_int()
                    .value_or(0) != 0;
        if (auto roots = variant_record->find_keyed(
                Symbol("guitarist_hidden_roots"))) {
          for (std::size_t root_index = 1; root_index < roots->size();
               ++root_index) {
            if (auto root = roots->at(root_index).as_string())
              variant.guitarist_hidden_roots.emplace_back(*root);
          }
        }
        const std::string unlock =
            text_field(variant_record.get(), Symbol("unlock"));
        variant.unlock_requirement =
            unlock.empty() ? Symbol() : Symbol(unlock);
        variant.character_label =
            text_field(variant_record.get(), Symbol("character_label"));
        if (variant.selection.valid() && !variant.model_path.empty())
          character_variants_.push_back(std::move(variant));
      }
    }
  }
  const char* addon_root = std::getenv("GHOGX_ADDONS_DIR");
  load_addon_manifests(addon_root && *addon_root ? fs::path(addon_root)
                                                 : fs::path("DLC"),
                       &ark);
  if (!character_variants_.empty()) {
    std::fprintf(stderr,
                 "[configdb] character catalog: characters=%zu variants=%zu\n",
                 characters().size(), character_variants_.size());
  }
  load_practice_sections(ark, ark_paths);
}

void ConfigDb::load_addon_manifests(
    const fs::path& addon_root,
    const gh::ark::ArkV3Reader* base_ark) {
  std::vector<fs::path> manifests;
  std::vector<gh::ark::LooseFileMount> mounts =
      gh::ark::ArkV3Reader::loose_file_mounts();
  std::map<std::string, std::string> mounted_by_package;
  for (const auto& mount : mounts)
    mounted_by_package.emplace(mount.virtual_path, mount.package_id);
  std::unordered_set<std::string> base_paths;
  if (base_ark) {
    for (const auto& entry : base_ark->entries())
      base_paths.insert(entry.full_path);
  }
  std::error_code error;
  if (fs::is_regular_file(addon_root / "manifest.json", error))
    manifests.push_back(addon_root / "manifest.json");
  error.clear();
  if (fs::is_directory(addon_root, error)) {
    error.clear();
    for (const auto& entry : fs::directory_iterator(addon_root, error)) {
      if (error) break;
      std::error_code entry_error;
      if (!entry.is_directory(entry_error) || entry_error) continue;
      const fs::path manifest = entry.path() / "manifest.json";
      entry_error.clear();
      if (fs::is_regular_file(manifest, entry_error) && !entry_error)
        manifests.push_back(manifest);
    }
  }
  std::sort(manifests.begin(), manifests.end());
  for (const fs::path& manifest : manifests) {
    const std::size_t variants_before = character_variants_.size();
    const std::size_t venues_before = addon_venues_.size();
    const std::size_t venue_labels_before = addon_venue_labels_.size();
    const std::size_t quickplay_before = addon_quickplay_songs_.size();
    const std::size_t setlists_before = addon_setlists_.size();
    std::vector<std::pair<std::size_t, CharacterVariant>>
        modified_variants;
    const auto song_sources_before = addon_song_sources_;
    const auto source_routes_before = source_routes_;
    const auto source_default_bands_before = source_default_bands_;
    std::vector<std::pair<DataArray*, std::size_t>> modified_arrays;
    auto record_array_before_mutation = [&](DataArray* array) {
      if (!array) return;
      if (std::none_of(modified_arrays.begin(), modified_arrays.end(),
                       [&](const auto& row) { return row.first == array; }))
        modified_arrays.emplace_back(array, array->size());
    };
    try {
      std::ifstream stream(manifest);
      std::string text((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
      if (text.empty()) continue;
      const JsonValue root = JsonParser(text).parse();
      if (root.type != JsonValue::Type::kObject)
        throw std::runtime_error("manifest root must be an object");
      if (json_int(root, "schema_version", 0) != 1)
        throw std::runtime_error("schema_version must be 1");
      const std::string package_id = json_string(root, "id");
      if (package_id.empty())
        throw std::runtime_error("manifest id is required");
      if (std::any_of(dlc_packages_.begin(), dlc_packages_.end(),
                      [&](const DlcPackageSummary& package) {
                        return package.id == package_id;
                      }))
        throw std::runtime_error("duplicate package id " + package_id);
      const fs::path addon_dir = manifest.parent_path();
      const std::string source_game = json_string(root, "source_game");
      std::string content_root_name = json_string(root, "content_root");
      if (content_root_name.empty()) content_root_name = "content";
      const std::string normalized_content_root =
          normalized_virtual_path(content_root_name);
      const fs::path content_root =
          (addon_dir / fs::path(normalized_content_root)).lexically_normal();
      std::unordered_set<std::string> replacements;
      for (const std::string& replacement : json_strings(root, "replaces"))
        replacements.insert(normalized_virtual_path(replacement));

      std::vector<gh::ark::LooseFileMount> package_mounts;
      if (fs::is_directory(content_root, error)) {
        std::vector<std::pair<std::string, fs::path>> package_files;
        const auto indexed_files = json_strings(root, "files");
        if (!indexed_files.empty()) {
          std::unordered_set<std::string> unique_files;
          for (const std::string& indexed_file : indexed_files) {
            const std::string virtual_path =
                normalized_virtual_path(indexed_file);
            if (!unique_files.insert(virtual_path).second)
              throw std::runtime_error("duplicate indexed content path: " +
                                       virtual_path);
            const fs::path loose_path =
                (content_root / fs::path(virtual_path)).lexically_normal();
            if (!fs::is_regular_file(loose_path, error) || error)
              throw std::runtime_error("indexed content file is missing: " +
                                       virtual_path);
            package_files.emplace_back(virtual_path, loose_path);
          }
        } else {
          for (fs::recursive_directory_iterator it(content_root, error), end;
               it != end && !error; it.increment(error)) {
            if (!it->is_regular_file(error)) continue;
            const std::string virtual_path = normalized_virtual_path(
                fs::relative(it->path(), content_root, error)
                    .generic_string());
            if (error) break;
            package_files.emplace_back(virtual_path, it->path());
          }
          if (error)
            throw std::runtime_error("cannot enumerate content directory");
          std::sort(package_files.begin(), package_files.end(),
                    [](const auto& left, const auto& right) {
                      return left.first < right.first;
                    });
        }
        for (const auto& [virtual_path, loose_path] : package_files) {
          if (base_paths.count(virtual_path) &&
              !replacements.count(virtual_path)) {
            throw std::runtime_error(
                "content path already exists in base ARK; declare it in "
                "replaces to override: " + virtual_path);
          }
          const auto duplicate = mounted_by_package.find(virtual_path);
          if (duplicate != mounted_by_package.end()) {
            throw std::runtime_error(
                "content path conflicts with package " + duplicate->second +
                ": " + virtual_path);
          }
          package_mounts.push_back(
              {virtual_path, fs::absolute(loose_path).lexically_normal(),
               package_id});
        }
      }
      const auto asset_exists = [&](const std::string& virtual_path) {
        if (virtual_path.empty()) return true;
        if (std::any_of(package_mounts.begin(), package_mounts.end(),
                        [&](const gh::ark::LooseFileMount& mount) {
                          return mount.virtual_path == virtual_path;
                        }))
          return true;
        return base_ark && base_ark->find(virtual_path).has_value();
      };

      // A preconverted release package can namespace identities that were
      // authored before GH2. Routes are data, not character/venue-specific
      // runtime exceptions, and apply equally to future external packages.
      if (const auto* routes = json_array(root, "source_routes")) {
        for (const JsonValue& route : *routes) {
          const std::string source = json_string(route, "source");
          const std::string kind = json_string(route, "kind");
          const std::string from = json_string(route, "from");
          const std::string to = json_string(route, "to");
          if (source.empty() || kind.empty() || from.empty() || to.empty())
            throw std::runtime_error(
                "source route requires source, kind, from, and to");
          const std::string key = source_route_key(source, kind, from);
          if (!source_routes_.emplace(key, to).second)
            throw std::runtime_error("duplicate source route " + key);
        }
      }
      if (const auto* bands = json_array(root, "source_default_bands")) {
        for (const JsonValue& band : *bands) {
          const std::string source = json_string(band, "source");
          const auto members = json_strings(band, "members");
          if (source.empty() || members.empty())
            throw std::runtime_error(
                "source default band requires source and members");
          if (!source_default_bands_.emplace(lower_ascii(source), members).second)
            throw std::runtime_error("duplicate source default band " +
                                     source);
        }
      }

      // A disc-import package can preserve the source game's complete song
      // records instead of projecting them through a reduced JSON schema.
      // Catalogs live inside the package and are appended transactionally to
      // the already-loaded GH2 songs table; the GH2 catalog itself is never
      // replaced or rewritten.
      if (const auto song_catalogs = json_strings(root, "song_catalogs");
          !song_catalogs.empty()) {
        DataArray* songs_table = nullptr;
        auto found = tables_.find(Symbol("songs").id());
        if (found != tables_.end()) songs_table = found->second.get();
        if (!songs_table)
          throw std::runtime_error("base songs table is unavailable");
        record_array_before_mutation(songs_table);
        for (const std::string& catalog_value : song_catalogs) {
          const std::string catalog_path =
              normalized_virtual_path(catalog_value);
          const auto mount = std::find_if(
              package_mounts.begin(), package_mounts.end(),
              [&](const gh::ark::LooseFileMount& row) {
                return row.virtual_path == catalog_path;
              });
          if (mount == package_mounts.end())
            throw std::runtime_error(
                "song catalog is not indexed package content: " +
                catalog_path);
          std::ifstream catalog_stream(mount->file_path, std::ios::binary);
          if (!catalog_stream)
            throw std::runtime_error("cannot open song catalog: " +
                                     catalog_path);
          std::vector<std::uint8_t> catalog_bytes(
              (std::istreambuf_iterator<char>(catalog_stream)),
              std::istreambuf_iterator<char>());
          const auto imported =
              dtb_bridge::from_tree(gh::dtb::parse(catalog_bytes));
          for (std::size_t row_index = 0; row_index < imported->size();
               ++row_index) {
            const auto record = imported->at(row_index).as_array();
            if (!record || record->empty()) continue;
            const Symbol song_id =
                record->at(0).as_symbol().value_or(Symbol());
            if (!song_id.valid()) continue;
            if (song_index(song_id) >= 0)
              throw std::runtime_error("duplicate song " +
                                       std::string(song_id.c_str()));
            songs_table->push(imported->at(row_index));
            addon_quickplay_songs_.push_back(song_id);
            if (!source_game.empty())
              addon_song_sources_[song_id.id()] = Symbol(source_game);
          }
        }
      }

      auto append_variant = [&](const JsonValue& row,
                                const std::string& inherited_character,
                                const std::string& inherited_label,
                                const std::string& inherited_blurb,
                                const std::string& inherited_portrait,
                                const std::string& inherited_preferred_guitar,
                                const std::string& inherited_preferred_finish,
                                int inherited_preferred_primary,
                                int inherited_preferred_secondary) {
        CharacterVariant variant;
        const std::string character =
            json_string(row, "character").empty()
                ? inherited_character
                : json_string(row, "character");
        const std::string selection = json_string(row, "selection");
        if (character.empty() || selection.empty())
          throw std::runtime_error("character and selection are required");
        variant.character = Symbol(character);
        variant.selection = Symbol(selection);
        const std::string replaces_selection =
            json_string(row, "replaces_selection");
        auto existing_variant = std::find_if(
            character_variants_.begin(), character_variants_.end(),
            [&](const CharacterVariant& existing) {
              return existing.selection == variant.selection;
            });
        const bool replaces_native_outfit =
            existing_variant == character_variants_.end() &&
            !replaces_selection.empty() &&
            std::find(native_character_outfits_.begin(),
                      native_character_outfits_.end(),
                      variant.selection) != native_character_outfits_.end();
        if (existing_variant != character_variants_.end() &&
            replaces_selection != selection)
          throw std::runtime_error("duplicate selection " + selection);
        if (existing_variant == character_variants_.end() &&
            !replaces_selection.empty() && !replaces_native_outfit)
          throw std::runtime_error(
              "replaces_selection names no existing selection " +
              replaces_selection);
        if (!replaces_selection.empty() &&
            replaces_selection != selection)
          throw std::runtime_error(
              "replaces_selection must equal selection " + selection);
        std::string source = json_string(row, "source");
        variant.source_game = Symbol(source.empty() ? "addon" : source);
        variant.label = json_string(row, "label");
        variant.model_path = normalized_manifest_path(
            addon_dir, json_string(row, "model"));
        variant.ui_model_path = normalized_manifest_path(
            addon_dir, json_string(row, "ui_model"));
        variant.ui_anim_path = normalized_manifest_path(
            addon_dir, json_string(row, "ui_anim"));
        const std::string ui_guitar = json_string(row, "ui_guitar");
        variant.ui_guitar = ui_guitar.empty() ? Symbol() : Symbol(ui_guitar);
        variant.main_anim_path = normalized_manifest_path(
            addon_dir, json_string(row, "main_anim"));
        variant.strum_anim_path = normalized_manifest_path(
            addon_dir, json_string(row, "strum_anim"));
        variant.fret_anim_path = normalized_manifest_path(
            addon_dir, json_string(row, "fret_anim"));
        variant.highway_surface_path = normalized_manifest_path(
            addon_dir, json_string(row, "highway_surface"));
        std::string portrait = json_string(row, "portrait");
        if (portrait.empty()) portrait = inherited_portrait;
        variant.portrait_path = normalized_manifest_path(addon_dir, portrait);
        std::string preferred_guitar = json_string(row, "preferred_guitar");
        if (preferred_guitar.empty())
          preferred_guitar = inherited_preferred_guitar;
        variant.preferred_guitar = preferred_guitar.empty()
                                       ? Symbol()
                                       : Symbol(preferred_guitar);
        std::string preferred_finish =
            json_string(row, "preferred_guitar_finish");
        if (preferred_finish.empty())
          preferred_finish = inherited_preferred_finish;
        variant.preferred_guitar_skin = preferred_finish.empty()
                                            ? Symbol()
                                            : Symbol(preferred_finish);
        variant.preferred_guitar_paint_primary = json_int(
            row, "preferred_guitar_paint_primary",
            inherited_preferred_primary);
        variant.preferred_guitar_paint_secondary = json_int(
            row, "preferred_guitar_paint_secondary",
            inherited_preferred_secondary);
        variant.animation_source_model_path = normalized_manifest_path(
            addon_dir, json_string(row, "animation_source_model"));
        variant.retarget_animation =
            json_bool(row, "retarget_animation", false);
        variant.guitarist_hidden_roots =
            json_strings(row, "guitarist_hidden_roots");
        if (variant.retarget_animation &&
            variant.animation_source_model_path.empty()) {
          throw std::runtime_error(
              "animation_source_model is required when retarget_animation "
              "is true for " + selection);
        }
        std::string unlock = json_string(row, "unlock");
        variant.unlock_requirement = unlock.empty() ? Symbol() : Symbol(unlock);
        variant.character_label = json_string(row, "character_label");
        if (variant.character_label.empty())
          variant.character_label = inherited_label;
        variant.character_blurb = inherited_blurb;
        variant.addon_defined = true;
        variant.outfit_blurb = json_string(row, "description");
        if (variant.label.empty()) variant.label = variant.character_label;
        if (variant.model_path.empty())
          throw std::runtime_error("model is required for " + selection);
        for (const std::string* path :
             {&variant.model_path, &variant.ui_model_path,
              &variant.ui_anim_path, &variant.main_anim_path,
              &variant.strum_anim_path, &variant.fret_anim_path,
              &variant.highway_surface_path, &variant.portrait_path,
              &variant.animation_source_model_path}) {
          if (!path->empty() && !asset_exists(*path))
            throw std::runtime_error("missing asset for " + selection +
                                     ": " + *path);
        }
        if (existing_variant != character_variants_.end()) {
          if (existing_variant->addon_defined)
            throw std::runtime_error(
                "cannot replace addon-defined selection " + selection);
          if (existing_variant->character != variant.character)
            throw std::runtime_error(
                "replacement changes character owner for " + selection);
          const std::string replaced_model =
              normalized_virtual_path(existing_variant->model_path);
          if (!replacements.count(replaced_model))
            throw std::runtime_error(
                "selection replacement must explicitly replace its base "
                "model path: " + replaced_model);
          const std::size_t index = static_cast<std::size_t>(
              std::distance(character_variants_.begin(), existing_variant));
          modified_variants.emplace_back(index, *existing_variant);
          *existing_variant = std::move(variant);
        } else if (replaces_native_outfit) {
          const auto owner_outfits = native_character_outfits(variant.character);
          if (std::find(owner_outfits.begin(), owner_outfits.end(),
                        variant.selection) == owner_outfits.end())
            throw std::runtime_error(
                "replacement changes native character owner for " +
                selection);
          const std::string replaced_model =
              "char/" + selection + "/og/gen/" + selection +
              ".milo_ps2";
          if (!replacements.count(replaced_model))
            throw std::runtime_error(
                "native selection replacement must explicitly replace its "
                "base model path: " + replaced_model);
          character_variants_.push_back(std::move(variant));
        } else {
          character_variants_.push_back(std::move(variant));
        }
      };

      if (const auto* characters = json_array(root, "characters")) {
        for (const JsonValue& character : *characters) {
          const std::string id = json_string(character, "id");
          if (id.empty()) throw std::runtime_error("character id is required");
          const std::string label = json_string(character, "label");
          const std::string description =
              json_string(character, "description");
          const std::string portrait = json_string(character, "portrait");
          const std::string preferred_guitar =
              json_string(character, "preferred_guitar");
          const std::string preferred_finish =
              json_string(character, "preferred_guitar_finish");
          const int preferred_primary = json_int(
              character, "preferred_guitar_paint_primary", -1);
          const int preferred_secondary = json_int(
              character, "preferred_guitar_paint_secondary", -1);
          const auto* outfits = json_array(character, "outfits");
          if (!outfits || outfits->empty())
            throw std::runtime_error("character " + id +
                                     " has no outfits");
          for (const JsonValue& outfit : *outfits)
            append_variant(outfit, id, label, description, portrait,
                           preferred_guitar, preferred_finish,
                           preferred_primary, preferred_secondary);
        }
      }
      if (const auto* outfits = json_array(root, "outfits")) {
        for (const JsonValue& outfit : *outfits)
          append_variant(outfit, {}, {}, {}, {}, {}, {}, -1, -1);
      }

      if (const auto* songs = json_array(root, "songs")) {
        DataArray* songs_table = nullptr;
        auto found = tables_.find(Symbol("songs").id());
        if (found != tables_.end()) songs_table = found->second.get();
        if (!songs_table)
          throw std::runtime_error("base songs table is unavailable");
        record_array_before_mutation(songs_table);
        for (JsonValue song_row : *songs) {
          const std::string id = json_string(song_row, "id");
          if (id.empty()) throw std::runtime_error("song id is required");
          if (song_index(Symbol(id)) >= 0)
            throw std::runtime_error("duplicate song " + id);
          if (json_string(song_row, "name").empty()) {
            const std::string title = json_string(song_row, "title");
            if (!title.empty()) {
              JsonValue title_value;
              title_value.type = JsonValue::Type::kString;
              title_value.string = title;
              song_row.object["name"] = std::move(title_value);
            }
          }
          songs_table->push(DataNode::Array(
              json_record(Symbol(id), song_row, {"title", "quickplay"})));
          if (json_bool(song_row, "quickplay", true))
            addon_quickplay_songs_.push_back(Symbol(id));
        }
      }

      if (const auto* guitars = json_array(root, "guitars")) {
        DataArray* guitar_table = nullptr;
        auto found = tables_.find(Symbol("guitars").id());
        if (found != tables_.end()) guitar_table = found->second.get();
        if (!guitar_table)
          throw std::runtime_error("base guitars table is unavailable");
        record_array_before_mutation(guitar_table);
        for (const JsonValue& guitar_row : *guitars) {
          const std::string id = json_string(guitar_row, "id");
          if (id.empty()) throw std::runtime_error("guitar id is required");
          if (guitar(Symbol(id)))
            throw std::runtime_error("duplicate guitar " + id);
          guitar_table->push(
              DataNode::Array(json_record(Symbol(id), guitar_row)));
        }
      }

      if (const auto* finishes = json_array(root, "finishes")) {
        for (const JsonValue& finish_row : *finishes) {
          const std::string guitar_id = json_string(finish_row, "guitar");
          const std::string finish_id = json_string(finish_row, "id");
          if (guitar_id.empty() || finish_id.empty())
            throw std::runtime_error("finish guitar and id are required");
          const DataArray* guitar_record = guitar(Symbol(guitar_id));
          if (!guitar_record)
            throw std::runtime_error("finish targets unknown guitar " +
                                     guitar_id);
          auto skins = guitar_record->find_keyed(Symbol("skins"));
          if (!skins) {
            auto mutable_record = const_cast<DataArray*>(guitar_record);
            record_array_before_mutation(mutable_record);
            skins = std::make_shared<DataArray>();
            skins->push(DataNode::Sym(Symbol("skins")));
            mutable_record->push(DataNode::Array(skins));
          }
          if (skins->find_keyed(Symbol(finish_id)))
            throw std::runtime_error("duplicate finish " + finish_id);
          record_array_before_mutation(skins.get());
          skins->push(DataNode::Array(json_record(
              Symbol(finish_id), finish_row, {"guitar"})));
        }
      }

      if (const auto* venues = json_array(root, "venues")) {
        for (const JsonValue& venue_row : *venues) {
          const std::string id = json_string(venue_row, "id");
          if (id.empty()) throw std::runtime_error("venue id is required");
          const Symbol venue(id);
          if (is_venue(venue))
            throw std::runtime_error("duplicate venue " + id);
          addon_venues_.push_back(venue);
          addon_venue_labels_.emplace_back(venue,
                                           json_string(venue_row, "name"));
        }
      }

      if (const auto* setlists = json_array(root, "setlists")) {
        for (const JsonValue& setlist_row : *setlists) {
          const std::string id = json_string(setlist_row, "id");
          if (id.empty()) throw std::runtime_error("setlist id is required");
          if (std::any_of(addon_setlists_.begin(), addon_setlists_.end(),
                          [&](const DlcSetlist& row) {
                            return row.id == Symbol(id);
                          }))
            throw std::runtime_error("duplicate setlist " + id);
          DlcSetlist setlist;
          setlist.id = Symbol(id);
          setlist.label = json_string(setlist_row, "label");
          setlist.include_in_quickplay =
              json_bool(setlist_row, "include_in_quickplay", false);
          for (const std::string& song :
               json_strings(setlist_row, "songs")) {
            if (song_index(Symbol(song)) < 0)
              throw std::runtime_error("setlist " + id +
                                       " references unknown song " + song);
            setlist.songs.push_back(Symbol(song));
            if (setlist.include_in_quickplay &&
                std::find(addon_quickplay_songs_.begin(),
                          addon_quickplay_songs_.end(), Symbol(song)) ==
                    addon_quickplay_songs_.end()) {
              addon_quickplay_songs_.push_back(Symbol(song));
            }
          }
          addon_setlists_.push_back(std::move(setlist));
        }
      }

      for (const auto& mount : package_mounts) {
        mounted_by_package.emplace(mount.virtual_path, package_id);
        mounts.push_back(mount);
      }
      dlc_packages_.push_back(
          {package_id, json_string(root, "name"),
           json_string(root, "version"), addon_dir,
           package_mounts.size()});
      std::fprintf(stderr, "[configdb] addon manifest: %s\n",
                   manifest.generic_string().c_str());
    } catch (const std::exception& ex) {
      for (auto it = modified_arrays.rbegin(); it != modified_arrays.rend();
           ++it)
        it->first->resize(it->second);
      for (auto it = modified_variants.rbegin();
           it != modified_variants.rend(); ++it)
        character_variants_[it->first] = std::move(it->second);
      character_variants_.resize(variants_before);
      addon_venues_.resize(venues_before);
      addon_venue_labels_.resize(venue_labels_before);
      addon_quickplay_songs_.resize(quickplay_before);
      addon_setlists_.resize(setlists_before);
      addon_song_sources_ = song_sources_before;
      source_routes_ = source_routes_before;
      source_default_bands_ = source_default_bands_before;
      std::fprintf(stderr, "[configdb] addon %s: %s\n",
                   manifest.generic_string().c_str(), ex.what());
    }
  }
  gh::ark::ArkV3Reader::set_loose_file_mounts(std::move(mounts));
  if (!dlc_packages_.empty()) {
    std::fprintf(stderr,
                 "[configdb] DLC catalog: packages=%zu files=%zu "
                 "setlists=%zu\n",
                 dlc_packages_.size(),
                 gh::ark::ArkV3Reader::loose_file_mounts().size(),
                 addon_setlists_.size());
  }
}

void ConfigDb::load_songs(
    const gh::ark::ArkV3Reader& ark,
    const std::vector<std::string>& ark_paths) {
  constexpr const char* path = "config/gen/songs.dtb";
  try {
    const auto entry = ark.find(path);
    if (!entry) {
      std::fprintf(stderr, "[configdb] content archive lacks %s\n", path);
      return;
    }
    const auto bytes = ark.read_entry(*entry, ark_paths);
    tables_[Symbol("songs").id()] =
        dtb_bridge::from_tree(gh::dtb::parse(bytes));
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[configdb] content %s: %s\n", path, ex.what());
  }
}

const DataArray* ConfigDb::table(Symbol name) const {
  auto it = tables_.find(name.id());
  return it == tables_.end() ? nullptr : it->second.get();
}

std::size_t ConfigDb::song_count() const {
  const DataArray* t = table(Symbol("songs"));
  return t ? t->size() : 0;
}

const DataArray* ConfigDb::song(std::size_t index) const {
  const DataArray* t = table(Symbol("songs"));
  if (!t || index >= t->size()) return nullptr;
  // The record's DataArray is owned by the table; .get() stays valid while this
  // ConfigDb is alive.
  return t->at(index).as_array().get();
}

Symbol ConfigDb::song_key(std::size_t index) const {
  const DataArray* rec = song(index);
  if (!rec || rec->size() == 0) return Symbol();
  return rec->at(0).as_symbol().value_or(Symbol());
}

int ConfigDb::song_index(Symbol song_name) const {
  if (!song_name.valid()) return -1;
  for (std::size_t i = 0; i < song_count(); ++i)
    if (song_key(i) == song_name) return static_cast<int>(i);
  return -1;
}

DataNode ConfigDb::song_field(std::size_t index, Symbol field_name) const {
  return field(song(index), field_name);
}

std::vector<Symbol> ConfigDb::quickplay_songs() const {
  std::vector<Symbol> out;
  const DataArray* campaign = table(Symbol("campaign"));
  auto order = campaign ? campaign->find_keyed(Symbol("order")) : nullptr;
  if (order) {
    for (std::size_t i = 1; i < order->size(); ++i) {
      auto tier = order->at(i).as_array();
      if (!tier) continue;
      for (std::size_t j = 1; j < tier->size(); ++j) {
        const Symbol key = tier->at(j).as_symbol().value_or(Symbol());
        if (key.valid() && song_index(key) >= 0 &&
            std::find(out.begin(), out.end(), key) == out.end())
          out.push_back(key);
      }
    }
  }
  for (const Symbol key : store_items(Symbol("song"))) {
    if (key.valid() && song_index(key) >= 0 &&
        std::find(out.begin(), out.end(), key) == out.end())
      out.push_back(key);
  }
  if (out.empty()) {
    out.reserve(song_count());
    for (std::size_t i = 0; i < song_count(); ++i) {
      const Symbol key = song_key(i);
      if (key.valid()) out.push_back(key);
    }
  }
  for (Symbol song : addon_quickplay_songs_) {
    if (song.valid() && song_index(song) >= 0 &&
        std::find(out.begin(), out.end(), song) == out.end())
      out.push_back(song);
  }
  return out;
}

std::string ConfigDb::song_audio_path(Symbol song_name) const {
  const int index = song_index(song_name);
  const DataArray* record = index >= 0 ? song(static_cast<std::size_t>(index))
                                      : nullptr;
  auto source = record ? record->find_keyed(Symbol("song")) : nullptr;
  const DataNode value = field(source.get(), Symbol("name"));
  if (auto text = value.as_string()) return std::string(*text);
  if (auto symbol = value.as_symbol()) return std::string(symbol->c_str());
  return {};
}

std::string ConfigDb::song_midi_path(Symbol song_name) const {
  const int index = song_index(song_name);
  const DataArray* record = index >= 0 ? song(static_cast<std::size_t>(index))
                                      : nullptr;
  auto source = record ? record->find_keyed(Symbol("song")) : nullptr;
  DataNode value = field(source.get(), Symbol("midi_file"));
  // GH1 authors midi_file at record scope; GH2/GH80s author it inside song.
  if (value.empty())
    value = field(record, Symbol("midi_file"));
  if (auto text = value.as_string()) return std::string(*text);
  if (auto symbol = value.as_symbol()) return std::string(symbol->c_str());
  return {};
}

SongRuntimeConfig ConfigDb::song_runtime_config(Symbol song_name) const {
  SongRuntimeConfig out;
  const int index = song_index(song_name);
  const DataArray* record =
      index >= 0 ? song(static_cast<std::size_t>(index)) : nullptr;
  if (!record) return out;
  if (const auto source = addon_song_sources_.find(song_name.id());
      source != addon_song_sources_.end()) {
    out.source_game = source->second;
  }
  out.midi_path = song_midi_path(song_name);
  out.audio_path = song_audio_path(song_name);
  auto text_value = [](const DataNode& value) {
    if (auto text = value.as_string()) return std::string(*text);
    if (auto symbol = value.as_symbol()) return std::string(symbol->c_str());
    return std::string();
  };
  out.anim_tempo = text_value(field(record, Symbol("anim_tempo")));
  if (auto quickplay = record->find_keyed(Symbol("quickplay"))) {
    out.character_outfit =
        text_value(field(quickplay.get(), Symbol("character_outfit")));
    if (out.character_outfit.empty())
      out.character_outfit =
          text_value(field(quickplay.get(), Symbol("character")));
    out.guitar = text_value(field(quickplay.get(), Symbol("guitar")));
    out.venue = text_value(field(quickplay.get(), Symbol("venue")));
  }
  if (auto band = record->find_keyed(Symbol("band"))) {
    for (std::size_t member = 1; member < band->size(); ++member) {
      const std::string value = text_value(band->at(member));
      if (!value.empty()) out.band.push_back(value);
    }
  }
  if (!out.source_game.valid()) return out;
  const std::string source = lower_ascii(out.source_game.c_str());
  auto route = [&](std::string_view kind, std::string value) {
    if (value.empty()) return value;
    const auto found =
        source_routes_.find(source_route_key(source, kind, value));
    return found == source_routes_.end() ? value : found->second;
  };
  out.character_outfit =
      route("character", std::move(out.character_outfit));
  out.guitar = route("guitar", std::move(out.guitar));
  out.venue = route("venue", std::move(out.venue));
  if (out.band.empty()) {
    if (const auto fallback = source_default_bands_.find(source);
        fallback != source_default_bands_.end()) {
      out.band = fallback->second;
    }
  }
  for (std::string& member : out.band)
    member = route("band_member", std::move(member));
  return out;
}

DataNode ConfigDb::store_field(Symbol category, Symbol item, Symbol field_name) const {
  const DataArray* store = table(Symbol("store"));
  if (!store) return DataNode();
  auto category_record = store->find_keyed(category);
  if (!category_record || category_record->size() <= 1) return DataNode();
  auto item_record = category_record->find_keyed(item);
  return field(item_record.get(), field_name);
}

std::vector<Symbol> ConfigDb::store_items(Symbol category) const {
  std::vector<Symbol> out;
  const DataArray* store = table(Symbol("store"));
  if (!store) return out;
  auto category_record = store->find_keyed(category);
  if (!category_record || category_record->size() <= 1) return out;
  for (std::size_t i = 1; i < category_record->size(); ++i) {
    auto item = category_record->at(i).as_array();
    if (!item || item->empty()) continue;
    Symbol key = item->at(0).as_symbol().value_or(Symbol());
    if (key.valid()) out.push_back(key);
  }
  return out;
}

std::size_t ConfigDb::store_item_count(Symbol category) const {
  return store_items(category).size();
}

Symbol ConfigDb::store_item(Symbol category, std::size_t index) const {
  const auto items = store_items(category);
  return index < items.size() ? items[index] : Symbol();
}

std::vector<Symbol> ConfigDb::venues() const {
  std::vector<Symbol> out;
  const DataArray* gh2 = table(Symbol("gh2"));
  if (gh2) {
    auto row = gh2->find_keyed(Symbol("venues"));
    if (row) {
      for (std::size_t i = 1; i < row->size(); ++i) {
        Symbol key = row->at(i).as_symbol().value_or(Symbol());
        if (key.valid()) out.push_back(key);
      }
    }
  }
  for (Symbol venue : addon_venues_)
    if (venue.valid() &&
        std::find(out.begin(), out.end(), venue) == out.end())
      out.push_back(venue);
  return out;
}

std::size_t ConfigDb::venue_count() const {
  return venues().size();
}

bool ConfigDb::is_venue(Symbol venue) const {
  if (!venue.valid()) return false;
  const auto list = venues();
  return std::find(list.begin(), list.end(), venue) != list.end();
}

int ConfigDb::venue_index(Symbol venue) const {
  const auto list = venues();
  for (std::size_t i = 0; i < list.size(); ++i)
    if (list[i] == venue) return static_cast<int>(i);
  return -1;
}

Symbol ConfigDb::default_venue() const {
  const Symbol stock_main_default("small2");
  if (is_venue(stock_main_default)) return stock_main_default;
  const auto list = venues();
  return list.empty() ? Symbol() : list.front();
}

std::string ConfigDb::venue_label(Symbol venue) const {
  const auto found = std::find_if(
      addon_venue_labels_.begin(), addon_venue_labels_.end(),
      [&](const auto& row) { return row.first == venue; });
  if (found != addon_venue_labels_.end() && !found->second.empty())
    return found->second;

  // The bundled converted GH1 package predates the optional manifest `name`
  // field. These are the source game's player-facing venue identities, not
  // synthesized internal-id formatting.
  static const std::pair<const char*, const char*> gh1_names[] = {
      {"gh1_basement", "The Basement"},
      {"gh1_small_club", "Freak Pit"},
      {"gh1_small_club_multi", "Freak Pit (Multiplayer)"},
      {"gh1_big_club", "Red Octane"},
      {"gh1_theatre", "Republik Theater"},
      {"gh1_fest", "Toxic Summer Tour"},
      {"gh1_arena", "The Garden"},
  };
  for (const auto& [id, label] : gh1_names)
    if (venue == Symbol(id)) return label;
  return {};
}

std::vector<Symbol> ConfigDb::campaign_songs(Symbol venue) const {
  std::vector<Symbol> out;
  const DataArray* campaign = table(Symbol("campaign"));
  if (!campaign || !venue.valid()) return out;
  auto order = campaign->find_keyed(Symbol("order"));
  if (!order) return out;
  for (std::size_t i = 1; i < order->size(); ++i) {
    auto tier = order->at(i).as_array();
    if (!tier || tier->empty() ||
        tier->at(0).as_symbol().value_or(Symbol()) != venue)
      continue;
    for (std::size_t j = 1; j < tier->size(); ++j) {
      Symbol song = tier->at(j).as_symbol().value_or(Symbol());
      if (song.valid()) out.push_back(song);
    }
    break;
  }
  return out;
}

Symbol ConfigDb::campaign_venue(Symbol song_name) const {
  if (!song_name.valid()) return Symbol();
  const DataArray* campaign = table(Symbol("campaign"));
  if (!campaign) return Symbol();
  auto order = campaign->find_keyed(Symbol("order"));
  if (!order) return Symbol();
  for (std::size_t i = 1; i < order->size(); ++i) {
    auto tier = order->at(i).as_array();
    if (!tier || tier->size() < 2) continue;
    for (std::size_t j = 1; j < tier->size(); ++j) {
      if (tier->at(j).as_symbol().value_or(Symbol()) == song_name)
        return tier->at(0).as_symbol().value_or(Symbol());
    }
  }
  return Symbol();
}

Symbol ConfigDb::campaign_venue_at(std::size_t tier_index) const {
  const DataArray* campaign = table(Symbol("campaign"));
  if (!campaign) return Symbol();
  auto order = campaign->find_keyed(Symbol("order"));
  if (!order || tier_index + 1 >= order->size()) return Symbol();
  auto tier = order->at(tier_index + 1).as_array();
  return tier && !tier->empty()
             ? tier->at(0).as_symbol().value_or(Symbol())
             : Symbol();
}

const DataArray* ConfigDb::guitar(Symbol guitar_name) const {
  const DataArray* guitars = table(Symbol("guitars"));
  if (!guitars) return nullptr;
  auto record = guitars->find_keyed(guitar_name);
  return record.get();
}

std::vector<Symbol> ConfigDb::guitars(Symbol type) const {
  std::vector<Symbol> output;
  const DataArray* table_data = table(Symbol("guitars"));
  if (!table_data) return output;
  for (std::size_t i = 0; i < table_data->size(); ++i) {
    auto record = table_data->at(i).as_array();
    if (!record || record->empty()) continue;
    const Symbol key = record->at(0).as_symbol().value_or(Symbol());
    if (!key.valid()) continue;
    if (type.valid() &&
        field(record.get(), Symbol("type")).as_symbol().value_or(Symbol()) !=
            type)
      continue;
    output.push_back(key);
  }
  return output;
}

Symbol ConfigDb::first_guitar(Symbol type) const {
  const std::vector<Symbol> matches = guitars(type);
  return matches.empty() ? Symbol() : matches.front();
}

std::size_t ConfigDb::guitar_skin_count(Symbol guitar_name) const {
  const DataArray* record = guitar(guitar_name);
  if (!record) return 0;
  auto skins = record->find_keyed(Symbol("skins"));
  if (!skins || skins->size() <= 1) return 0;
  std::size_t count = 0;
  for (std::size_t i = 1; i < skins->size(); ++i) {
    auto skin = skins->at(i).as_array();
    if (skin && !skin->empty() && skin->at(0).as_symbol()) ++count;
  }
  return count;
}

Symbol ConfigDb::first_guitar_skin(Symbol guitar_name) const {
  return guitar_skin_at(guitar_name, 0);
}

Symbol ConfigDb::guitar_skin_at(Symbol guitar_name, std::size_t index) const {
  const DataArray* record = guitar(guitar_name);
  if (!record) return Symbol();
  auto skins = record->find_keyed(Symbol("skins"));
  if (!skins) return Symbol();
  std::size_t found = 0;
  for (std::size_t i = 1; i < skins->size(); ++i) {
    auto skin = skins->at(i).as_array();
    if (skin && !skin->empty()) {
      Symbol key = skin->at(0).as_symbol().value_or(Symbol());
      if (!key.valid()) continue;
      if (found++ == index) return key;
    }
  }
  return Symbol();
}

const DataArray* ConfigDb::guitar_skin(Symbol guitar_name, Symbol skin_name) const {
  const DataArray* record = guitar(guitar_name);
  if (!record) return nullptr;
  auto skins = record->find_keyed(Symbol("skins"));
  if (!skins) return nullptr;
  auto skin = skins->find_keyed(skin_name);
  return skin.get();
}

DataNode ConfigDb::guitar_skin_field(Symbol guitar_name, Symbol skin_name,
                                     Symbol field_name) const {
  return field(guitar_skin(guitar_name, skin_name), field_name);
}

Symbol ConfigDb::guitar_for_skin(Symbol skin_name) const {
  const DataArray* guitars = table(Symbol("guitars"));
  if (!guitars || !skin_name.valid()) return Symbol();
  for (std::size_t i = 0; i < guitars->size(); ++i) {
    auto record = guitars->at(i).as_array();
    if (!record || record->empty()) continue;
    Symbol guitar_key = record->at(0).as_symbol().value_or(Symbol());
    if (!guitar_key.valid()) continue;
    if (guitar_skin(guitar_key, skin_name)) return guitar_key;
  }
  return Symbol();
}

std::vector<Symbol> ConfigDb::characters() const {
  std::vector<Symbol> out = native_characters_;
  for (const CharacterVariant& variant : character_variants_) {
    if (std::find(out.begin(), out.end(), variant.character) == out.end())
      out.push_back(variant.character);
  }
  return out;
}

std::vector<Symbol> ConfigDb::native_character_outfits(
    Symbol character) const {
  std::vector<Symbol> out;
  const std::string base = character.c_str();
  for (Symbol outfit : native_character_outfits_) {
    const std::string name = outfit.c_str();
    if (name == base ||
        (name.rfind(base, 0) == 0 && name.size() == base.size() + 1 &&
         name.back() >= '0' && name.back() <= '9')) {
      out.push_back(outfit);
    }
  }
  return out;
}

std::vector<Symbol> ConfigDb::character_outfits(Symbol character) const {
  std::vector<Symbol> out = native_character_outfits(character);
  auto variants = character_variants(character);
  const auto source_rank = [](Symbol source) {
    if (source == Symbol("gh2")) return 0;
    if (source == Symbol("gh1")) return 1;
    if (source == Symbol("gh80")) return 2;
    return 3;
  };
  std::stable_sort(
      variants.begin(), variants.end(),
      [&](const CharacterVariant& a, const CharacterVariant& b) {
        return source_rank(a.source_game) < source_rank(b.source_game);
      });
  for (const CharacterVariant& variant : variants) {
    if (std::find(out.begin(), out.end(), variant.selection) == out.end())
      out.push_back(variant.selection);
  }
  if (out.empty() && character.valid()) out.push_back(character);
  return out;
}

std::vector<CharacterVariant> ConfigDb::character_variants(
    Symbol character) const {
  std::vector<CharacterVariant> out;
  for (const CharacterVariant& variant : character_variants_) {
    if (variant.character == character) out.push_back(variant);
  }
  return out;
}

const CharacterVariant* ConfigDb::character_variant(Symbol selection) const {
  const auto found = std::find_if(
      character_variants_.begin(), character_variants_.end(),
      [&](const CharacterVariant& variant) {
        return variant.selection == selection;
      });
  return found == character_variants_.end() ? nullptr : &*found;
}

Symbol ConfigDb::character_for_variant(Symbol selection) const {
  const CharacterVariant* variant = character_variant(selection);
  return variant ? variant->character : Symbol();
}

std::string ConfigDb::character_label(Symbol character) const {
  for (const CharacterVariant& variant : character_variants_) {
    if (variant.character == character && !variant.character_label.empty())
      return variant.character_label;
  }
  return {};
}

std::string ConfigDb::character_portrait(Symbol character) const {
  for (const CharacterVariant& variant : character_variants_) {
    if (variant.character == character && !variant.portrait_path.empty())
      return variant.portrait_path;
  }
  return {};
}

std::vector<Symbol> ConfigDb::setlists() const {
  std::vector<Symbol> out;
  out.reserve(addon_setlists_.size());
  for (const DlcSetlist& setlist : addon_setlists_) out.push_back(setlist.id);
  return out;
}

std::string ConfigDb::setlist_label(Symbol setlist) const {
  const auto found = std::find_if(
      addon_setlists_.begin(), addon_setlists_.end(),
      [&](const DlcSetlist& row) { return row.id == setlist; });
  return found == addon_setlists_.end() ? std::string() : found->label;
}

std::vector<Symbol> ConfigDb::setlist_songs(Symbol setlist) const {
  const auto found = std::find_if(
      addon_setlists_.begin(), addon_setlists_.end(),
      [&](const DlcSetlist& row) { return row.id == setlist; });
  return found == addon_setlists_.end() ? std::vector<Symbol>{}
                                        : found->songs;
}

DataNode ConfigDb::field(const DataArray* record, Symbol key) {
  if (!record) return DataNode();
  auto kv = record->find_keyed(key);
  return (kv && kv->size() > 1) ? kv->at(1) : DataNode();
}

void ConfigDb::load_practice_sections(
    const gh::ark::ArkV3Reader& ark,
    const std::vector<std::string>& ark_paths) {
  practice_sections_.clear();
  practice_sections_.push_back(Symbol("full_song"));

  try {
    const Symbol song = first_campaign_song(*this);
    const std::string key = song.c_str();
    const std::string midi_path = "songs/" + key + "/" + key + ".mid";
    auto entry = ark.find(midi_path);
    if (!entry) return;

    const std::vector<uint8_t> bytes = ark.read_entry(*entry, ark_paths);
    const ghogx::chart::Chart chart = ghogx::chart::parse_midi(bytes);
    for (const auto& ev : chart.text_events) {
      constexpr std::string_view kPrefix = "[section ";
      if (!starts_with(ev.text, kPrefix) || ev.text.empty() ||
          ev.text.back() != ']') {
        continue;
      }
      std::string name = ev.text.substr(kPrefix.size());
      name.pop_back();
      if (name.empty()) continue;
      Symbol section(name.c_str());
      if (std::find(practice_sections_.begin(), practice_sections_.end(),
                    section) == practice_sections_.end()) {
        practice_sections_.push_back(section);
      }
    }
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[configdb] practice sections: %s\n", ex.what());
  }
}

}  // namespace ghogx::ui
