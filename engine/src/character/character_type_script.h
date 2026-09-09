// Source-driven Character/BandCharacter type-script execution.
//
// Harmonix stores character-type defaults and handlers in
// char/gen/char_objects.dtb.  CharClipSamples enter/exit event strings call
// those handlers through the character object (for example, WorldCrowd hand
// selection).  This bridge deliberately executes the authored DTB graph
// instead of inferring behavior from clip, mesh, character, or venue names.

#pragma once

#include "core/data_node.h"
#include "dtb.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ghogx::character {

struct Character;

// Retail Waypoint selection is construction-order based. `waypoint_find`
// returns the first row whose flags overlap the requested mask, then
// `waypoint_last` moves that selected row to the back of the shared registry.
struct CharacterTypeScriptWaypoint {
  std::size_t source_index = 0;
  std::string name;
  uint32_t flags = 0;
};

class CharacterTypeScriptWaypointRegistry {
 public:
  explicit CharacterTypeScriptWaypointRegistry(
      std::vector<CharacterTypeScriptWaypoint> waypoints);
  ~CharacterTypeScriptWaypointRegistry();

  const std::vector<CharacterTypeScriptWaypoint>& waypoints() const;
  std::vector<std::size_t> ordered_source_indices() const;
  std::optional<CharacterTypeScriptWaypoint> find(uint32_t flags) const;
  bool mark_last(std::size_t source_index);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CharacterTypeScriptProgram {
 public:
  ~CharacterTypeScriptProgram();

  const std::string& class_name() const;
  const std::string& type_name() const;
  const gh::dtb::NodeList* handler(std::string_view name) const;
  std::shared_ptr<const gh::dtb::Node> member(
      std::string_view name) const;
  std::vector<std::pair<std::string, std::shared_ptr<gh::dtb::Node>>>
  default_rows() const;

 private:
  struct Impl;
  explicit CharacterTypeScriptProgram(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;

  friend std::shared_ptr<const CharacterTypeScriptProgram>
  compile_character_type_script_program(
      const gh::dtb::NodeList&, std::string_view, std::string_view,
      std::string*);
  friend class CharacterTypeScriptInstance;
};

// Compile an already-preprocessed char_objects root.  Exposed separately so
// conversion/runtime tests consume the same compiler without requiring an ARK.
std::shared_ptr<const CharacterTypeScriptProgram>
compile_character_type_script_program(
    const gh::dtb::NodeList& roots, std::string_view class_name,
    std::string_view type_name, std::string* error = nullptr);

// Load and preprocess char/gen/char_objects.dtb, including its authored
// archive-relative includes, then compile the requested class/type.
std::shared_ptr<const CharacterTypeScriptProgram>
load_character_type_script_program(
    const std::string& hdr_path, const std::string& ark_path,
    std::string_view class_name, std::string_view type_name,
    std::string* error = nullptr);

// Typed GH2 walking properties authored by BandCharacter/guitarist and
// CharWalk/guitarist in char/gen/char_objects.dtb.
struct CharacterTypeScriptWalkConfig {
  std::array<bool, 5> delay_enabled = {};
  std::array<float, 5> delay_min = {};
  std::array<float, 5> delay_max = {};
  uint32_t waypoint_flags = 0;
  float max_walk_wait = 0.0f;
  float path_radius = 0.0f;
};

std::optional<CharacterTypeScriptWalkConfig>
character_type_script_walk_config(
    const CharacterTypeScriptProgram& character_program,
    const CharacterTypeScriptProgram& walk_program,
    std::string* error = nullptr);

std::optional<CharacterTypeScriptWalkConfig>
load_character_type_script_walk_config(
    const std::string& hdr_path, const std::string& ark_path,
    std::string_view character_class, std::string_view character_type,
    std::string* error = nullptr);

// One evaluated message from an authored character type to a decoded
// CharDriver object. Gameplay supplies the live driver response; the bridge
// preserves the object name, message, and arguments without interpreting
// asset names.
struct CharacterTypeScriptDriverMessage {
  std::string driver;
  std::string message;
  DataArray args;
};

class CharacterTypeScriptInstance {
 public:
  struct Impl;
  using DriverMessageHandler =
      std::function<DataNode(const CharacterTypeScriptDriverMessage&)>;

  ~CharacterTypeScriptInstance();

  CharacterTypeScriptInstance(const CharacterTypeScriptInstance&) = delete;
  CharacterTypeScriptInstance& operator=(
      const CharacterTypeScriptInstance&) = delete;

  static std::unique_ptr<CharacterTypeScriptInstance> create(
      std::shared_ptr<const CharacterTypeScriptProgram> program,
      Character& character, std::string* error = nullptr);
  static std::unique_ptr<CharacterTypeScriptInstance> create(
      std::shared_ptr<const CharacterTypeScriptProgram> program,
      Character& character,
      std::shared_ptr<CharacterTypeScriptWaypointRegistry> waypoints,
      std::string* error = nullptr);
  static std::unique_ptr<CharacterTypeScriptInstance> create(
      std::shared_ptr<const CharacterTypeScriptProgram> program,
      Character& character,
      std::shared_ptr<CharacterTypeScriptWaypointRegistry> waypoints,
      std::string_view instance_name, std::string* error = nullptr);

  // Run a named authored type handler, normally enter/exit.
  bool run_handler(std::string_view handler, std::string* error = nullptr);
  bool run_handler(std::string_view handler, const DataArray& args,
                   std::string* error = nullptr);

  // Parse and execute one serialized CharClip enter/exit/beat event fragment.
  // An empty event is a successful no-op.
  bool run_clip_event(std::string_view event, std::string* error = nullptr);
  void set_timeline_beats(float task_beat, float next_event_beat);
  void set_win_campaign_song(bool won);

  // Messages emitted before a live driver is bound are retained in source
  // order. Query messages are not queued because replaying a query would
  // invent a second script evaluation.
  void set_driver_message_handler(DriverMessageHandler handler);
  std::vector<CharacterTypeScriptDriverMessage> take_driver_messages();

  // Runtime inspection for tests/audits.  Opaque named objects retain start /
  // stop state even when their renderer class is not implemented yet.
  bool has_handler(std::string_view name) const;
  bool named_object_active(std::string_view name) const;
  std::optional<uint32_t> last_waypoint_find_flags() const;
  std::optional<CharacterTypeScriptWaypoint> last_teleport() const;
  const std::vector<std::string>& unhandled_messages() const;

 private:
  explicit CharacterTypeScriptInstance(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace ghogx::character
