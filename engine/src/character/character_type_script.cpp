#include "character/character_type_script.h"

#include "character/char_clip.h"
#include "character/char_mesh.h"
#include "core/object.h"
#include "script/interp.h"

#include "ark_v3.h"
#include "dtb_preprocess.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace ghogx::character {
namespace {

std::string node_text(const gh::dtb::Node& node) {
  return gh::dtb::as_string(node).value_or("");
}

std::shared_ptr<gh::dtb::Node> find_direct_keyed(
    const gh::dtb::NodeList& nodes, std::string_view key) {
  for (const auto& node : nodes) {
    if (!node || !gh::dtb::is_array(*node)) continue;
    const auto& children = gh::dtb::children(*node);
    if (!children.empty() && children.front() &&
        node_text(*children.front()) == key) {
      return node;
    }
  }
  return {};
}

void collect_type_through_superclasses(
    const gh::dtb::NodeList& roots,
    const std::shared_ptr<gh::dtb::Node>& class_node,
    std::string_view type_name, std::set<std::string>& visited_classes,
    std::vector<std::shared_ptr<gh::dtb::Node>>& out) {
  if (!class_node) return;
  const auto& class_fields = gh::dtb::children(*class_node);
  const auto superclasses = find_direct_keyed(class_fields, "superclasses");
  if (superclasses) {
    const auto& super_fields = gh::dtb::children(*superclasses);
    for (size_t i = 1; i < super_fields.size(); ++i) {
      if (!super_fields[i]) continue;
      const std::string superclass = node_text(*super_fields[i]);
      if (superclass.empty() ||
          !visited_classes.insert(superclass).second) {
        continue;
      }
      collect_type_through_superclasses(
          roots, find_direct_keyed(roots, superclass), type_name,
          visited_classes, out);
    }
  }
  if (const auto types = find_direct_keyed(class_fields, "types")) {
    if (const auto type =
            find_direct_keyed(gh::dtb::children(*types), type_name)) {
      out.push_back(type);
    }
  }
}

void collect_clip_flag_constants(
    const gh::dtb::NodeList& nodes,
    std::map<std::string, int32_t>& constants) {
  for (const auto& node : nodes) {
    if (!node || !gh::dtb::is_array(*node)) continue;
    const auto& fields = gh::dtb::children(*node);
    if (fields.size() >= 2 && fields.front() &&
        node_text(*fields.front()) == "clip_flags" && fields[1] &&
        gh::dtb::is_array(*fields[1])) {
      const auto& names = gh::dtb::children(*fields[1]);
      for (size_t index = 0; index < names.size() && index < 31; ++index) {
        if (!names[index]) continue;
        const std::string name = node_text(*names[index]);
        if (name.empty()) continue;
        const int32_t value =
            static_cast<int32_t>(uint32_t{1} << index);
        const auto [found, inserted] = constants.emplace(name, value);
        if (!inserted && found->second != value) {
          // A symbol with conflicting bit positions is not a global constant;
          // keep it literal rather than assigning either schema's meaning.
          constants.erase(found);
        }
      }
    }
    collect_clip_flag_constants(fields, constants);
  }
}

bool has_command_body(const gh::dtb::NodeList& nodes) {
  return std::any_of(nodes.begin() + std::min<size_t>(1, nodes.size()),
                     nodes.end(), [](const auto& node) {
                       return node && node->tag == 0x11;
                     });
}

void collect_type_members(
    const std::shared_ptr<gh::dtb::Node>& candidate,
    std::vector<std::shared_ptr<gh::dtb::Node>>& out) {
  if (!candidate || !gh::dtb::is_array(*candidate)) return;
  const auto& fields = gh::dtb::children(*candidate);
  if (fields.empty()) return;
  if (fields.front() && !node_text(*fields.front()).empty()) {
    out.push_back(candidate);
    return;
  }
  // Bare list-valued DTA macros (for example CHAR_COMMON and BAND_COMMON in
  // stock char_objects.dtb) are substituted as one array containing several
  // member arrays. They are structural splices at a type-member site, not a
  // handler whose name is another array. Flatten only this nameless wrapper;
  // named handlers and their command bodies remain intact.
  for (const auto& child : fields) collect_type_members(child, out);
}

}  // namespace

struct CharacterTypeScriptWaypointRegistry::Impl {
  std::vector<CharacterTypeScriptWaypoint> waypoints;
  std::deque<std::size_t> order;
};

CharacterTypeScriptWaypointRegistry::CharacterTypeScriptWaypointRegistry(
    std::vector<CharacterTypeScriptWaypoint> waypoints)
    : impl_(std::make_unique<Impl>()) {
  impl_->waypoints = std::move(waypoints);
  for (std::size_t index = 0; index < impl_->waypoints.size(); ++index) {
    impl_->order.push_back(index);
  }
}

CharacterTypeScriptWaypointRegistry::~CharacterTypeScriptWaypointRegistry() =
    default;

const std::vector<CharacterTypeScriptWaypoint>&
CharacterTypeScriptWaypointRegistry::waypoints() const {
  return impl_->waypoints;
}

std::vector<std::size_t>
CharacterTypeScriptWaypointRegistry::ordered_source_indices() const {
  std::vector<std::size_t> result;
  result.reserve(impl_->order.size());
  for (const std::size_t index : impl_->order) {
    if (index < impl_->waypoints.size()) {
      result.push_back(impl_->waypoints[index].source_index);
    }
  }
  return result;
}

std::optional<CharacterTypeScriptWaypoint>
CharacterTypeScriptWaypointRegistry::find(uint32_t flags) const {
  for (const std::size_t index : impl_->order) {
    if (index < impl_->waypoints.size() &&
        (impl_->waypoints[index].flags & flags) != 0) {
      return impl_->waypoints[index];
    }
  }
  return std::nullopt;
}

bool CharacterTypeScriptWaypointRegistry::mark_last(
    std::size_t source_index) {
  const auto found =
      std::find_if(impl_->order.begin(), impl_->order.end(),
                   [&](std::size_t index) {
                     return index < impl_->waypoints.size() &&
                            impl_->waypoints[index].source_index ==
                                source_index;
                   });
  if (found == impl_->order.end()) return false;
  const std::size_t index = *found;
  impl_->order.erase(found);
  impl_->order.push_back(index);
  return true;
}

struct CharacterTypeScriptProgram::Impl {
  std::string class_name;
  std::string type_name;
  gh::dtb::NodeList roots;
  std::map<std::string, std::shared_ptr<gh::dtb::Node>> members;
  std::map<std::string, std::shared_ptr<gh::dtb::Node>> defaults;
  std::map<std::string, std::shared_ptr<gh::dtb::Node>> handlers;
  std::map<std::string, int32_t> clip_flag_constants;
};

CharacterTypeScriptProgram::CharacterTypeScriptProgram(
    std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CharacterTypeScriptProgram::~CharacterTypeScriptProgram() = default;

const std::string& CharacterTypeScriptProgram::class_name() const {
  return impl_->class_name;
}

const std::string& CharacterTypeScriptProgram::type_name() const {
  return impl_->type_name;
}

const gh::dtb::NodeList* CharacterTypeScriptProgram::handler(
    std::string_view name) const {
  const auto found = impl_->handlers.find(std::string(name));
  return found == impl_->handlers.end()
             ? nullptr
             : &gh::dtb::children(*found->second);
}

std::shared_ptr<const gh::dtb::Node>
CharacterTypeScriptProgram::member(std::string_view name) const {
  const auto found = impl_->members.find(std::string(name));
  return found == impl_->members.end() ? nullptr : found->second;
}

std::vector<std::pair<std::string, std::shared_ptr<gh::dtb::Node>>>
CharacterTypeScriptProgram::default_rows() const {
  return {impl_->defaults.begin(), impl_->defaults.end()};
}

std::shared_ptr<const CharacterTypeScriptProgram>
compile_character_type_script_program(
    const gh::dtb::NodeList& roots, std::string_view class_name,
    std::string_view type_name, std::string* error) {
  auto fail = [&](std::string message)
      -> std::shared_ptr<const CharacterTypeScriptProgram> {
    if (error) *error = std::move(message);
    return {};
  };

  const auto class_node = find_direct_keyed(roots, class_name);
  if (!class_node) {
    return fail("character class not found in char_objects: " +
                std::string(class_name));
  }
  std::set<std::string> visited_classes = {std::string(class_name)};
  std::vector<std::shared_ptr<gh::dtb::Node>> type_nodes;
  collect_type_through_superclasses(
      roots, class_node, type_name, visited_classes, type_nodes);
  if (type_nodes.empty()) {
    return fail("character type not found in char_objects: " +
                std::string(class_name) + "/" + std::string(type_name) +
                " (including source superclasses)");
  }

  auto impl = std::make_unique<CharacterTypeScriptProgram::Impl>();
  impl->class_name = class_name;
  impl->type_name = type_name;
  impl->roots = roots;
  collect_clip_flag_constants(roots, impl->clip_flag_constants);
  // Source type lookup walks superclasses and applies the derived row last.
  // Merge in that order so BandCharacter/guitarist can call Character's
  // gtr_solo_on handler while overriding any inherited member by name.
  for (const auto& type_node : type_nodes) {
    const auto& members = gh::dtb::children(*type_node);
    std::vector<std::shared_ptr<gh::dtb::Node>> flattened_members;
    for (size_t i = 1; i < members.size(); ++i) {
      collect_type_members(members[i], flattened_members);
    }
    for (const auto& member : flattened_members) {
      const auto& fields = gh::dtb::children(*member);
      if (fields.empty() || !fields.front()) continue;
      const std::string name = node_text(*fields.front());
      if (name.empty()) continue;
      impl->members[name] = member;
      if (has_command_body(fields)) {
        impl->handlers[name] = member;
      } else if (fields.size() == 2 && fields[1] &&
                 !gh::dtb::is_array(*fields[1])) {
        impl->defaults[name] = member;
      }
    }
  }
  if (error) error->clear();
  return std::shared_ptr<const CharacterTypeScriptProgram>(
      new CharacterTypeScriptProgram(std::move(impl)));
}

std::shared_ptr<const CharacterTypeScriptProgram>
load_character_type_script_program(
    const std::string& hdr_path, const std::string& ark_path,
    std::string_view class_name, std::string_view type_name,
    std::string* error) {
  constexpr const char* kCharObjectsPath = "char/gen/char_objects.dtb";
  try {
    auto ark = gh::ark::ArkV3Reader::load(hdr_path);
    const auto root_entry = ark.find(kCharObjectsPath);
    if (!root_entry) {
      if (error) *error = "archive entry not found: " + std::string(kCharObjectsPath);
      return {};
    }
    const auto root_tree =
        gh::dtb::parse(ark.read_entry(*root_entry, {ark_path}));
    gh::dtb::MacroTable macros;
    gh::dtb::PreprocessOptions options;
    options.source_path = kCharObjectsPath;
    options.macro_table = &macros;
    options.contextual_include_resolver =
        [&](const std::string& including_path,
            const std::string& authored_include)
        -> gh::dtb::PreprocessOptions::IncludedFile {
      const std::string compiled_path =
          gh::dtb::resolve_compiled_include_path(including_path,
                                                 authored_include);
      const auto entry = ark.find(compiled_path);
      if (!entry) {
        throw std::runtime_error("included archive entry not found: " +
                                 compiled_path);
      }
      auto included =
          gh::dtb::parse(ark.read_entry(*entry, {ark_path}));
      return {compiled_path, std::move(included.root)};
    };
    const auto roots = gh::dtb::preprocess(root_tree.root, options);
    return compile_character_type_script_program(
        roots, class_name, type_name, error);
  } catch (const std::exception& ex) {
    if (error) *error = ex.what();
    return {};
  }
}

namespace {

std::optional<float> type_member_number(
    const CharacterTypeScriptProgram& program, std::string_view name) {
  const auto row = program.member(name);
  if (!row || !gh::dtb::is_array(*row)) return std::nullopt;
  const auto& fields = gh::dtb::children(*row);
  if (fields.size() != 2 || !fields[1]) return std::nullopt;
  if (const auto value = gh::dtb::as_float(*fields[1])) return value;
  if (const auto value = gh::dtb::as_int(*fields[1])) {
    return static_cast<float>(*value);
  }
  return std::nullopt;
}

std::optional<uint32_t> literal_bit_or(const gh::dtb::Node& node) {
  if (const auto value = gh::dtb::as_int(node)) {
    return static_cast<uint32_t>(*value);
  }
  if (!gh::dtb::is_array(node)) return std::nullopt;
  const auto& fields = gh::dtb::children(node);
  if (fields.empty() || !fields.front() ||
      node_text(*fields.front()) != "|") {
    return std::nullopt;
  }
  uint32_t result = 0;
  for (size_t index = 1; index < fields.size(); ++index) {
    if (!fields[index]) return std::nullopt;
    const auto value = literal_bit_or(*fields[index]);
    if (!value) return std::nullopt;
    result |= *value;
  }
  return result;
}

}  // namespace

std::optional<CharacterTypeScriptWalkConfig>
character_type_script_walk_config(
    const CharacterTypeScriptProgram& character_program,
    const CharacterTypeScriptProgram& walk_program,
    std::string* error) {
  CharacterTypeScriptWalkConfig result;
  const auto delays = character_program.member("walk_delays");
  if (!delays || !gh::dtb::is_array(*delays)) {
    if (error) *error = "walking character type has no walk_delays row";
    return std::nullopt;
  }
  const auto& delay_fields = gh::dtb::children(*delays);
  for (size_t level = 0;
       level < result.delay_enabled.size() &&
       level + 1 < delay_fields.size();
       ++level) {
    if (!delay_fields[level + 1] ||
        !gh::dtb::is_array(*delay_fields[level + 1])) {
      continue;
    }
    const auto& range =
        gh::dtb::children(*delay_fields[level + 1]);
    if (range.size() != 2 || !range[0] || !range[1]) continue;
    const auto minimum = gh::dtb::as_float(*range[0]);
    const auto maximum = gh::dtb::as_float(*range[1]);
    if (!minimum || !maximum) continue;
    result.delay_enabled[level] = true;
    result.delay_min[level] = *minimum;
    result.delay_max[level] = *maximum;
  }

  const auto walkspot = character_program.member("walkspot");
  if (!walkspot || !gh::dtb::is_array(*walkspot)) {
    if (error) *error = "walking character type has no walkspot row";
    return std::nullopt;
  }
  const auto& walkspot_fields = gh::dtb::children(*walkspot);
  if (walkspot_fields.size() != 2 || !walkspot_fields[1]) {
    if (error) *error = "walking character type has malformed walkspot row";
    return std::nullopt;
  }
  const auto waypoint_flags = literal_bit_or(*walkspot_fields[1]);
  const auto max_walk_wait =
      type_member_number(character_program, "max_walk_wait");
  const auto path_radius =
      type_member_number(walk_program, "path_radius");
  if (!waypoint_flags || !max_walk_wait || !path_radius) {
    if (error) {
      *error =
          "walking type is missing literal walkspot/max_walk_wait/"
          "path_radius facts";
    }
    return std::nullopt;
  }
  result.waypoint_flags = *waypoint_flags;
  result.max_walk_wait = *max_walk_wait;
  result.path_radius = *path_radius;
  if (error) error->clear();
  return result;
}

std::optional<CharacterTypeScriptWalkConfig>
load_character_type_script_walk_config(
    const std::string& hdr_path, const std::string& ark_path,
    std::string_view character_class, std::string_view character_type,
    std::string* error) {
  std::string local_error;
  const auto character_program =
      load_character_type_script_program(
          hdr_path, ark_path, character_class, character_type,
          &local_error);
  if (!character_program) {
    if (error) *error = std::move(local_error);
    return std::nullopt;
  }
  const auto walk_program =
      load_character_type_script_program(
          hdr_path, ark_path, "CharWalk", character_type,
          &local_error);
  if (!walk_program) {
    if (error) *error = std::move(local_error);
    return std::nullopt;
  }
  return character_type_script_walk_config(
      *character_program, *walk_program, error);
}

struct CharacterTypeScriptInstance::Impl;

class ScriptCharacterObject final : public Object {
 public:
  ScriptCharacterObject(CharacterTypeScriptInstance::Impl* owner,
                        std::string class_name)
      : owner_(owner), class_name_(std::move(class_name)) {}

  Symbol class_name() const override { return Symbol(class_name_); }
  DataNode handle_property(Symbol msg, const DataArray& args) override;

 private:
  CharacterTypeScriptInstance::Impl* owner_ = nullptr;
  std::string class_name_;
};

class ScriptDirectoryObject final : public Object {
 public:
  explicit ScriptDirectoryObject(CharacterTypeScriptInstance::Impl* owner)
      : owner_(owner) {
    set_name(Symbol("dir"));
  }

  Symbol class_name() const override { return Symbol("ObjectDir"); }
  DataNode handle_property(Symbol msg, const DataArray& args) override;

 private:
  CharacterTypeScriptInstance::Impl* owner_ = nullptr;
};

class ScriptCharDriverObject final : public Object {
 public:
  ScriptCharDriverObject(CharacterTypeScriptInstance::Impl* owner,
                         std::string name)
      : owner_(owner) {
    set_name(Symbol(name));
  }

  Symbol class_name() const override { return Symbol("CharDriver"); }
  DataNode handle_property(Symbol msg, const DataArray& args) override;

 private:
  CharacterTypeScriptInstance::Impl* owner_ = nullptr;
};

class ScriptMeshObject final : public Object {
 public:
  explicit ScriptMeshObject(SkinnedMesh* mesh) : mesh_(mesh) {
    set_name(Symbol(mesh ? mesh->name : std::string{}));
  }

  Symbol class_name() const override { return Symbol("RndMesh"); }
  DataNode handle_property(Symbol msg, const DataArray& args) override {
    if (msg == Symbol("set_showing")) {
      mesh_->showing =
          !args.empty() && ghogx::script::truthy(args.at(0));
      return DataNode::Int(mesh_->showing ? 1 : 0);
    }
    return Object::handle_property(msg, args);
  }

 private:
  SkinnedMesh* mesh_ = nullptr;
};

class ScriptNamedObject final : public Object {
 public:
  ScriptNamedObject(std::string name, std::string type, bool* active)
      : type_(std::move(type)), active_(active) {
    set_name(Symbol(name));
  }

  Symbol class_name() const override {
    return Symbol(type_.empty() ? "Object" : type_);
  }
  DataNode handle_property(Symbol msg, const DataArray& args) override {
    if (msg == Symbol("start")) {
      *active_ = true;
      return DataNode::Int(1);
    }
    if (msg == Symbol("stop")) {
      *active_ = false;
      return DataNode::Int(0);
    }
    // Stock character scripts use WorldFx::Find to retarget child particle
    // systems and use global event-source objects only for AddSink wiring.
    // Both operations retain object identity; their live rendering/event
    // effects are owned by the surrounding runtime.
    if (msg == Symbol("find")) return DataNode::Obj(this);
    if (msg == Symbol("add_sink") ||
        msg == Symbol("remove_sink") ||
        msg == Symbol("set_relative_parent") ||
        msg == Symbol("set_mesh") ||
        msg == Symbol("iterate")) {
      return DataNode::Int(1);
    }
    if (msg == Symbol("set_showing")) {
      *active_ =
          !args.empty() && ghogx::script::truthy(args.at(0));
      return DataNode::Int(*active_ ? 1 : 0);
    }
    return Object::handle_property(msg, args);
  }

 private:
  std::string type_;
  bool* active_ = nullptr;
};

class ScriptTimelineObject final : public Object {
 public:
  ScriptTimelineObject(std::string name, std::string message, float* value)
      : message_(std::move(message)), value_(value) {
    set_name(Symbol(name));
  }

  Symbol class_name() const override { return Symbol("Object"); }
  DataNode handle_property(Symbol msg, const DataArray& args) override {
    (void)args;
    if (msg == Symbol(message_) && value_) return DataNode::Float(*value_);
    return Object::handle_property(msg, args);
  }

 private:
  std::string message_;
  float* value_ = nullptr;
};

class ScriptGameConfigObject final : public Object {
 public:
  explicit ScriptGameConfigObject(bool* won) : won_(won) { set_name(Symbol("gamecfg")); }
  Symbol class_name() const override { return Symbol("Object"); }
  DataNode handle_property(Symbol message, const DataArray& args) override {
    if (message == Symbol("win_campaign_song")) return DataNode::Int(*won_ ? 1 : 0);
    return Object::handle_property(message, args);
  }
 private:
  bool* won_;
};

class ScriptWaypointObject final : public Object {
 public:
  explicit ScriptWaypointObject(CharacterTypeScriptWaypoint waypoint)
      : waypoint_(std::move(waypoint)) {
    set_name(Symbol(waypoint_.name));
  }

  Symbol class_name() const override { return Symbol("Waypoint"); }
  const CharacterTypeScriptWaypoint& waypoint() const { return waypoint_; }

 private:
  CharacterTypeScriptWaypoint waypoint_;
};

struct CharacterTypeScriptInstance::Impl final : script::Host {
  std::shared_ptr<const CharacterTypeScriptProgram> program;
  std::shared_ptr<CharacterTypeScriptWaypointRegistry> waypoints;
  Character* character = nullptr;
  script::Interp interp;
  script::Scope root_scope;
  std::unique_ptr<ScriptCharacterObject> self;
  std::unique_ptr<ScriptDirectoryObject> directory;
  std::vector<std::unique_ptr<Object>> objects;
  std::map<std::string, Object*> objects_by_name;
  std::map<std::string, DataNode> globals;
  std::map<std::string, bool> named_active;
  std::map<std::size_t, ScriptWaypointObject*> waypoint_objects;
  std::optional<uint32_t> last_waypoint_find_flags;
  std::optional<CharacterTypeScriptWaypoint> last_teleport;
  std::vector<std::string> unhandled;
  CharacterTypeScriptInstance::DriverMessageHandler driver_message_handler;
  std::vector<CharacterTypeScriptDriverMessage> pending_driver_messages;
  float task_beat = 0.0f;
  float next_event_beat = 0.0f;
  bool win_campaign_song = false;

  Object* resolve_object(Symbol name) override {
    const std::string object_name(name.c_str());
    const auto it = objects_by_name.find(object_name);
    if (it != objects_by_name.end()) return it->second;
    constexpr std::string_view kParserSuffix = "_parser";
    if (object_name.size() >= kParserSuffix.size() &&
        object_name.compare(object_name.size() - kParserSuffix.size(),
                            kParserSuffix.size(), kParserSuffix) == 0) {
      auto parser = std::make_unique<ScriptTimelineObject>(
          object_name, "next_event_beat", &next_event_beat);
      Object* result = parser.get();
      objects_by_name[object_name] = result;
      objects.push_back(std::move(parser));
      return result;
    }
    const auto has_suffix = [&](std::string_view suffix) {
      return object_name.size() >= suffix.size() &&
             object_name.compare(object_name.size() - suffix.size(),
                                 suffix.size(), suffix) == 0;
    };
    if (has_suffix("_strum") || has_suffix("_fret") ||
        has_suffix("_fret_pos")) {
      auto [active, inserted] =
          named_active.emplace(object_name, false);
      (void)inserted;
      auto event_source = std::make_unique<ScriptNamedObject>(
          object_name, "EventSource", &active->second);
      Object* result = event_source.get();
      objects_by_name[object_name] = result;
      objects.push_back(std::move(event_source));
      return result;
    }
    return nullptr;
  }

  DataNode get_global(Symbol name) override {
    const auto it = globals.find(name.c_str());
    return it == globals.end() ? DataNode() : it->second;
  }

  void set_global(Symbol name, DataNode value) override {
    globals[std::string(name.c_str())] = std::move(value);
  }

  bool handle_command(
      Symbol name, const DataArray& args, DataNode& out) override {
    if (name == Symbol("waypoint_find")) {
      const auto flags =
          args.empty() ? std::optional<int32_t>{} : args.at(0).as_int();
      if (!flags || !waypoints) {
        out = DataNode();
        return true;
      }
      last_waypoint_find_flags = static_cast<uint32_t>(*flags);
      const auto selected = waypoints->find(static_cast<uint32_t>(*flags));
      if (!selected) {
        out = DataNode();
        return true;
      }
      const auto object = waypoint_objects.find(selected->source_index);
      out = object == waypoint_objects.end()
                ? DataNode()
                : DataNode::Obj(object->second);
      return true;
    }
    if (name == Symbol("waypoint_last")) {
      const auto* waypoint =
          args.empty()
              ? nullptr
              : dynamic_cast<ScriptWaypointObject*>(args.at(0).as_object());
      if (waypoint && waypoints) {
        waypoints->mark_last(waypoint->waypoint().source_index);
      }
      out = DataNode();
      return true;
    }
    if (name != Symbol("handle")) return false;
    DataNode last;
    for (size_t index = 0; index < args.size(); ++index) {
      const auto message = args.at(index).as_array();
      if (!message || message->size() < 2) continue;
      Object* target = message->at(0).as_object();
      if (!target) {
        if (const auto symbol = message->at(0).as_symbol()) {
          target = resolve_object(*symbol);
        } else if (const auto text = message->at(0).as_string()) {
          target = resolve_object(Symbol(*text));
        }
      }
      Symbol message_name;
      if (const auto symbol = message->at(1).as_symbol()) {
        message_name = *symbol;
      } else if (const auto text = message->at(1).as_string()) {
        message_name = Symbol(*text);
      }
      if (!target || !message_name.valid()) {
        // `handle` is the source language's optional-message form. Stock
        // Character handlers deliberately list outfit-specific meshes that
        // are absent from most instances, so a missing target is a no-op.
        continue;
      }
      DataArray message_args;
      for (size_t arg = 2; arg < message->size(); ++arg) {
        message_args.push(message->at(arg));
      }
      last = target->handle_property(message_name, message_args);
    }
    out = std::move(last);
    return true;
  }

  void on_unhandled(const std::string& message) override {
    if (std::find(unhandled.begin(), unhandled.end(), message) ==
        unhandled.end()) {
      unhandled.push_back(message);
    }
  }

  DataNode run_named_handler(Symbol handler, const DataArray& args) {
    const auto* body = program->handler(handler.c_str());
    if (!body) {
      if (program->class_name() == "BandCharacter") {
        const auto play_group =
            [&](const char* group) {
              DataArray group_args;
              group_args.push(DataNode::Sym(Symbol(group)));
              group_args.push(DataNode::Int(
                  static_cast<int32_t>(kCharPlayNow)));
              return dispatch_driver(
                  Symbol("main.drv"), Symbol("play_group"), group_args);
            };
        // GH2 PS2 BandCharacter::Handle at 0x0010C9E0..0x0010CB7C
        // owns these messages outside char_objects.dtb. Play/idle/wail-on
        // call the group helper at 0x0010B7F8 with normal/idle/extreme;
        // gtr_solo_on selects solo. Wail/solo off call 0x0010C5B8,
        // which restores the normal group.
        if (handler == Symbol("play")) return play_group("normal");
        if (handler == Symbol("idle")) return play_group("idle");
        if (handler == Symbol("wail_on")) return play_group("extreme");
        if (handler == Symbol("wail_off")) return play_group("normal");
        if (handler == Symbol("gtr_solo_on")) return play_group("solo");
        if (handler == Symbol("gtr_solo_off")) return play_group("normal");
        if (handler == Symbol("band_jump")) return play_group("sync_jump");
        if (handler == Symbol("sync_wag")) return play_group("sync_wag");
        if (handler == Symbol("sync_head_bang")) return play_group("sync_head_bang");
        // GUITAR_COMMON's i_won/i_lost handlers delegate their selected group
        // to this native BandCharacter message rather than to a DTA handler.
        if (handler == Symbol("set_game_over") && !args.empty()) {
          const auto group = args.at(0).as_string();
          if (group && !group->empty()) {
            DataArray group_args;
            group_args.push(DataNode::Sym(Symbol(std::string(*group))));
            group_args.push(DataNode::Int(kCharPlayNow | kCharPlayNoLoop));
            return dispatch_driver(Symbol("main.drv"), Symbol("play_group"), group_args);
          }
        }
      }
      on_unhandled(std::string("character-handler?:") + handler.c_str());
      return DataNode();
    }
    script::Env env{self.get(), &root_scope, this};
    return interp.run_handler(*body, self.get(), args, env);
  }

  DataNode teleport(const DataArray& args) {
    const auto* waypoint =
        args.empty()
            ? nullptr
            : dynamic_cast<ScriptWaypointObject*>(args.at(0).as_object());
    if (!waypoint) return DataNode();
    last_teleport = waypoint->waypoint();
    return DataNode::Int(0);
  }

  DataNode directory_exists(const DataArray& args) {
    if (args.empty()) return DataNode::Int(0);
    const auto name = args.at(0).as_string();
    if (!name) return DataNode::Int(0);
    return DataNode::Int(
        objects_by_name.find(std::string(*name)) != objects_by_name.end()
            ? 1
            : 0);
  }

  DataNode dispatch_driver(Symbol driver, Symbol message,
                           const DataArray& args) {
    CharacterTypeScriptDriverMessage request;
    request.driver = driver.c_str();
    request.message = message.c_str();
    for (size_t index = 0; index < args.size(); ++index) {
      const DataNode& arg = args.at(index);
      if (const auto symbol = arg.as_symbol()) {
        const auto constant = globals.find(symbol->c_str());
        request.args.push(
            constant == globals.end() ? arg : constant->second);
      } else if (const auto array = arg.as_array()) {
        auto resolved = std::make_shared<DataArray>();
        for (size_t child = 0; child < array->size(); ++child) {
          const DataNode& value = array->at(child);
          if (const auto symbol = value.as_symbol()) {
            const auto constant = globals.find(symbol->c_str());
            resolved->push(
                constant == globals.end() ? value : constant->second);
          } else {
            resolved->push(value);
          }
        }
        request.args.push(DataNode::Array(std::move(resolved)));
      } else {
        request.args.push(arg);
      }
    }
    if (driver_message_handler) return driver_message_handler(request);

    const std::string_view message_name = request.message;
    const bool query =
        message_name == "get_first_flags" ||
        message_name == "get_first_playing_flags" ||
        message_name == "get_first_playing" ||
        message_name == "get_most_playing";
    if (!query) pending_driver_messages.push_back(std::move(request));
    return query ? DataNode::Int(0) : DataNode();
  }
};

DataNode ScriptCharacterObject::handle_property(
    Symbol msg, const DataArray& args) {
  if (!owner_) return Object::handle_property(msg, args);
  if (msg == Symbol("dir")) return DataNode::Obj(owner_->directory.get());
  if (msg == Symbol("teleport")) return owner_->teleport(args);
  // GH2 Character::OnPlayClip forwards the evaluated DataNode and optional
  // play flags to main.drv::Play.
  if (msg == Symbol("play_clip")) {
    return owner_->dispatch_driver(Symbol("main.drv"), Symbol("play"), args);
  }
  return owner_->run_named_handler(msg, args);
}

DataNode ScriptCharDriverObject::handle_property(
    Symbol msg, const DataArray& args) {
  if (!owner_) return Object::handle_property(msg, args);
  return owner_->dispatch_driver(name(), msg, args);
}

DataNode ScriptDirectoryObject::handle_property(
    Symbol msg, const DataArray& args) {
  if (!owner_) return Object::handle_property(msg, args);
  if (msg == Symbol("exists")) return owner_->directory_exists(args);
  return Object::handle_property(msg, args);
}

CharacterTypeScriptInstance::CharacterTypeScriptInstance(
    std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CharacterTypeScriptInstance::~CharacterTypeScriptInstance() = default;

std::unique_ptr<CharacterTypeScriptInstance>
CharacterTypeScriptInstance::create(
    std::shared_ptr<const CharacterTypeScriptProgram> program,
    Character& character, std::string* error) {
  return create(std::move(program), character, {}, error);
}

std::unique_ptr<CharacterTypeScriptInstance>
CharacterTypeScriptInstance::create(
    std::shared_ptr<const CharacterTypeScriptProgram> program,
    Character& character,
    std::shared_ptr<CharacterTypeScriptWaypointRegistry> waypoints,
    std::string* error) {
  return create(std::move(program), character, std::move(waypoints),
                character.dir_name, error);
}

std::unique_ptr<CharacterTypeScriptInstance>
CharacterTypeScriptInstance::create(
    std::shared_ptr<const CharacterTypeScriptProgram> program,
    Character& character,
    std::shared_ptr<CharacterTypeScriptWaypointRegistry> waypoints,
    std::string_view instance_name, std::string* error) {
  if (!program) {
    if (error) *error = "null character type-script program";
    return {};
  }
  auto impl = std::make_unique<Impl>();
  impl->program = std::move(program);
  impl->waypoints = std::move(waypoints);
  impl->character = &character;
  for (const auto& [name, value] :
       impl->program->impl_->clip_flag_constants) {
    impl->globals[name] = DataNode::Int(value);
  }
  const std::pair<const char*, uint32_t> play_constants[] = {
      {"kPlayNoDefault", kCharPlayNoDefault},
      {"kPlayNow", kCharPlayNow},
      {"kPlayNoBlend", kCharPlayNoBlend},
      {"kPlayFirst", kCharPlayFirst},
      {"kPlayLast", kCharPlayLast},
      {"kPlayDirty", kCharPlayDirty},
      {"kPlayNoLoop", kCharPlayNoLoop},
      {"kPlayLoop", kCharPlayLoop},
      {"kPlayGraphLoop", kCharPlayGraphLoop},
      {"kPlayNodeLoop", kCharPlayNodeLoop},
      {"kPlayRealTime", kCharPlayRealTime},
      {"kPlayUserTime", kCharPlayUserTime},
  };
  for (const auto& [name, value] : play_constants) {
    impl->globals[name] =
        DataNode::Int(static_cast<int32_t>(value));
  }
  impl->directory =
      std::make_unique<ScriptDirectoryObject>(impl.get());
  impl->self = std::make_unique<ScriptCharacterObject>(
      impl.get(), impl->program->class_name());
  const std::string live_name =
      instance_name.empty() ? character.dir_name : std::string(instance_name);
  impl->self->set_name(Symbol(live_name));
  impl->objects_by_name[live_name] = impl->self.get();
  if (live_name != character.dir_name && !character.dir_name.empty()) {
    impl->objects_by_name[character.dir_name] = impl->self.get();
  }

  for (auto& mesh : character.meshes) {
    auto object = std::make_unique<ScriptMeshObject>(&mesh);
    impl->objects_by_name[mesh.name] = object.get();
    impl->objects.push_back(std::move(object));
  }
  for (const auto& driver : character.drivers) {
    auto object =
        std::make_unique<ScriptCharDriverObject>(impl.get(), driver.name);
    impl->objects_by_name[driver.name] = object.get();
    impl->objects.push_back(std::move(object));
  }
  if (impl->waypoints) {
    for (const auto& waypoint : impl->waypoints->waypoints()) {
      auto object = std::make_unique<ScriptWaypointObject>(waypoint);
      impl->waypoint_objects[waypoint.source_index] = object.get();
      impl->objects.push_back(std::move(object));
    }
  }
  const auto add_named_object =
      [&](const std::string& name, const std::string& type) {
        auto [active, inserted] =
            impl->named_active.emplace(name, false);
        (void)inserted;
        auto object =
            std::make_unique<ScriptNamedObject>(name, type, &active->second);
        impl->objects_by_name[name] = object.get();
        impl->objects.push_back(std::move(object));
      };
  for (const auto& loader : character.outfit_loaders) {
    add_named_object(loader.name, "OutfitLoader");
  }
  for (const auto& walk : character.char_walks) {
    add_named_object(walk.name, "CharWalk");
  }
  for (const auto& opaque : character.opaque_rows) {
    add_named_object(opaque.name, opaque.type);
  }
  for (const auto& world_fx : character.world_fxes) {
    // GH2 retail WorldFx runtime state is distinct from its serialized
    // RndDrawable::showing field. SLUS_214.47 WorldFx::Start
    // (0x002726b8) writes the live gate at object+0x98 to one and
    // WorldFx::Stop (0x002728e0) clears it; the class handler at
    // 0x00272bc8 dispatches the exact `start`/`stop` symbols. Type scripts
    // establish that live state after directory entry, so do not promote the
    // authored container visibility bit into a running effect.
    add_named_object(world_fx.name, "WorldFx");
  }
  // Gameplay-owned Character instances always enter beneath the live `game`
  // singleton. Stock BAND_COMMON guards its sink registration and initial
  // main.drv clips with `{exists game}`; omitting this object suppresses the
  // authored singer, bassist, and guitarist driver startup entirely.
  add_named_object("game", "Game");
  {
    auto taskmgr = std::make_unique<ScriptTimelineObject>(
        "taskmgr", "beat", &impl->task_beat);
    impl->objects_by_name["taskmgr"] = taskmgr.get();
    impl->objects.push_back(std::move(taskmgr));
  }

  script::Env env{impl->self.get(), &impl->root_scope, impl.get()};
  for (const auto& [name, row] : impl->program->default_rows()) {
    const auto& fields = gh::dtb::children(*row);
    if (fields.size() == 2 && fields[1]) {
      impl->self->set_property(Symbol(name),
                               impl->interp.eval(*fields[1], env));
    }
  }
  if (const auto parser =
          impl->self->get_property(Symbol("parser")).as_string();
      parser && !parser->empty()) {
    const std::string parser_name(*parser);
    auto parser_object = std::make_unique<ScriptTimelineObject>(
        parser_name, "next_event_beat", &impl->next_event_beat);
    impl->objects_by_name[parser_name] = parser_object.get();
    impl->objects.push_back(std::move(parser_object));
  }
  auto gamecfg = std::make_unique<ScriptGameConfigObject>(&impl->win_campaign_song);
  impl->objects_by_name["gamecfg"] = gamecfg.get();
  impl->objects.push_back(std::move(gamecfg));
  if (error) error->clear();
  return std::unique_ptr<CharacterTypeScriptInstance>(
      new CharacterTypeScriptInstance(std::move(impl)));
}

bool CharacterTypeScriptInstance::run_handler(
  std::string_view handler, std::string* error) {
  DataArray args;
  return run_handler(handler, args, error);
}

bool CharacterTypeScriptInstance::run_handler(
    std::string_view handler, const DataArray& args,
    std::string* error) {
  if (!has_handler(handler)) {
    if (error) *error = "character type handler not found: " +
                        std::string(handler);
    return false;
  }
  impl_->run_named_handler(Symbol(handler), args);
  if (error) error->clear();
  return true;
}

bool CharacterTypeScriptInstance::run_clip_event(
    std::string_view event, std::string* error) {
  if (event.empty()) {
    if (error) error->clear();
    return true;
  }
  try {
    const auto event_tree = gh::dtb::parse_dta(event);
    script::Scope event_scope(&impl_->root_scope);
    event_scope.declare(Symbol("dude"), DataNode::Obj(impl_->self.get()));
    script::Env env{impl_->self.get(), &event_scope, impl_.get()};
    for (const auto& root : event_tree.root) {
      if (root) impl_->interp.eval(*root, env);
    }
    if (error) error->clear();
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = ex.what();
    return false;
  }
}

void CharacterTypeScriptInstance::set_timeline_beats(
    float task_beat, float next_event_beat) {
  impl_->task_beat = task_beat;
  impl_->next_event_beat = next_event_beat;
}

void CharacterTypeScriptInstance::set_win_campaign_song(bool won) {
  impl_->win_campaign_song = won;
}

void CharacterTypeScriptInstance::set_driver_message_handler(
    DriverMessageHandler handler) {
  impl_->driver_message_handler = std::move(handler);
}

std::vector<CharacterTypeScriptDriverMessage>
CharacterTypeScriptInstance::take_driver_messages() {
  std::vector<CharacterTypeScriptDriverMessage> result;
  result.swap(impl_->pending_driver_messages);
  return result;
}

bool CharacterTypeScriptInstance::has_handler(
    std::string_view name) const {
  if (!impl_ || !impl_->program) return false;
  if (impl_->program->handler(name)) return true;
  if (impl_->program->class_name() != "BandCharacter") return false;
  return name == "play" || name == "idle" ||
         name == "wail_on" || name == "wail_off" ||
         name == "gtr_solo_on" || name == "gtr_solo_off" ||
         name == "band_jump" || name == "sync_wag" ||
         name == "sync_head_bang" || name == "set_game_over";
}

bool CharacterTypeScriptInstance::named_object_active(
    std::string_view name) const {
  const auto found = impl_->named_active.find(std::string(name));
  return found != impl_->named_active.end() && found->second;
}

std::optional<uint32_t>
CharacterTypeScriptInstance::last_waypoint_find_flags() const {
  return impl_->last_waypoint_find_flags;
}

std::optional<CharacterTypeScriptWaypoint>
CharacterTypeScriptInstance::last_teleport() const {
  return impl_->last_teleport;
}

const std::vector<std::string>&
CharacterTypeScriptInstance::unhandled_messages() const {
  return impl_->unhandled;
}

}  // namespace ghogx::character
