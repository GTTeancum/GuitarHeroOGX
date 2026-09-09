#include "character/character_type_script.h"

#include "character/char_clip.h"
#include "character/char_mesh.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <sstream>

namespace {

ghogx::character::SkinnedMesh mesh(std::string name, bool showing) {
  ghogx::character::SkinnedMesh result;
  result.name = std::move(name);
  result.showing = showing;
  return result;
}

bool showing(const ghogx::character::Character& character,
             const char* name) {
  for (const auto& candidate : character.meshes) {
    if (candidate.name == name) return candidate.showing;
  }
  return false;
}

bool driver_bridge_contract() {
  gh::dtb::Tree tree;
  try {
    tree = gh::dtb::parse_dta(R"DTA(
      (CharClipSet
        (types
          (band
            (clip_flags
              ("kBandIntro" "kBandIdle" "kBandIdleComplete")))))
      (Character
        (types
          (driver_test
            (parser singer_parser)
            (enter
              {$this play_clip kBandIntro
                {| kPlayNoBlend kPlayGraphLoop kPlayRealTime}}
              {main.drv set_starved starved}
              {main.drv set realign TRUE})
            (idle
              {main.drv play_if_safe
                kBandIdle kPlayGraphLoop kBandIdleComplete
                {- {[parser] next_event_beat} {taskmgr beat}}})
            (query
              {main.drv get_first_flags}))))
    )DTA");
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "driver bridge parse failed: %s\n", ex.what());
    return false;
  }
  std::string error;
  const auto program =
      ghogx::character::compile_character_type_script_program(
          tree.root, "Character", "driver_test", &error);
  ghogx::character::Character character;
  character.dir_name = "driver_test";
  character.dir_type = "Character";
  character.root_object_type = "driver_test";
  ghogx::character::CharDriver main_driver;
  main_driver.name = "main.drv";
  character.drivers.push_back(std::move(main_driver));
  auto instance =
      ghogx::character::CharacterTypeScriptInstance::create(
          program, character, &error);
  if (!instance || !instance->run_handler("enter", &error)) {
    std::fprintf(stderr, "driver bridge enter failed: %s\n", error.c_str());
    return false;
  }
  const auto pending = instance->take_driver_messages();
  if (pending.size() != 3 ||
      pending[0].driver != "main.drv" ||
      pending[0].message != "play" ||
      pending[0].args.size() != 2 ||
      pending[0].args.at(0).as_int().value_or(0) != 1 ||
      pending[0].args.at(1).as_int().value_or(0) !=
          (ghogx::character::kCharPlayNoBlend |
           ghogx::character::kCharPlayGraphLoop |
           ghogx::character::kCharPlayRealTime) ||
      pending[1].message != "set_starved" ||
      pending[1].args.size() != 1 ||
      pending[1].args.at(0).as_string().value_or("") != "starved" ||
      pending[2].message != "set" ||
      pending[2].args.size() != 2 ||
      pending[2].args.at(0).as_string().value_or("") != "realign" ||
      (pending[2].args.at(1).as_int().value_or(0) != 1 &&
       pending[2].args.at(1).as_string().value_or("") != "TRUE")) {
    std::fprintf(stderr,
                 "driver bridge pending-message contract mismatch count=%zu\n",
                 pending.size());
    for (const auto& message : pending) {
      std::fprintf(stderr, "pending=%s:%s args=%zu\n",
                   message.driver.c_str(), message.message.c_str(),
                   message.args.size());
      for (size_t i = 0; i < message.args.size(); ++i) {
        const auto& arg = message.args.at(i);
        std::fprintf(stderr,
                     "  arg%zu type=%u int=%d text=%.*s\n", i,
                     static_cast<unsigned>(arg.type()),
                     arg.as_int().value_or(-999),
                     static_cast<int>(arg.as_string().value_or("").size()),
                     arg.as_string().value_or("").data());
      }
    }
    return false;
  }
  std::vector<std::string> live_messages;
  ghogx::character::CharacterTypeScriptDriverMessage live_idle;
  instance->set_driver_message_handler(
      [&](const ghogx::character::CharacterTypeScriptDriverMessage& message) {
        live_messages.push_back(message.driver + ":" + message.message);
        if (message.message == "play_if_safe") live_idle = message;
        return ghogx::DataNode::Int(0x1234);
      });
  instance->set_timeline_beats(4.0f, 12.0f);
  if (!instance->run_handler("idle", &error) ||
      !instance->run_handler("query", &error) ||
      live_messages !=
          std::vector<std::string>{"main.drv:play_if_safe",
                                   "main.drv:get_first_flags"} ||
      live_idle.args.size() != 4 ||
      live_idle.args.at(0).as_int().value_or(0) != 2 ||
      live_idle.args.at(1).as_int().value_or(0) !=
          ghogx::character::kCharPlayGraphLoop ||
      live_idle.args.at(2).as_int().value_or(0) != 4 ||
      live_idle.args.at(3).as_float().value_or(-1.0f) != 8.0f ||
      !instance->take_driver_messages().empty() ||
      !instance->unhandled_messages().empty()) {
    std::fprintf(stderr, "driver bridge live dispatch mismatch: %s\n",
                 error.c_str());
    return false;
  }
  return true;
}

bool native_band_character_group_contract() {
  gh::dtb::Tree tree;
  try {
    tree = gh::dtb::parse_dta(R"DTA(
      (Character (types (guitarist)))
      (BandCharacter
        (superclasses Character)
        (types
          (guitarist
            (i_won {$this set_game_over {if_else {gamecfg win_campaign_song} win_finals win}})
            (solo_on {$this gtr_solo_on})
            (solo_off {$this gtr_solo_off}))))
    )DTA");
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "native band handler parse failed: %s\n", ex.what());
    return false;
  }
  std::string error;
  const auto program =
      ghogx::character::compile_character_type_script_program(
          tree.root, "BandCharacter", "guitarist", &error);
  ghogx::character::Character character;
  character.dir_name = "guitarist_fixture";
  character.dir_type = "BandCharacter";
  character.root_object_type = "guitarist";
  ghogx::character::CharDriver main_driver;
  main_driver.name = "main.drv";
  character.drivers.push_back(std::move(main_driver));
  auto instance =
      ghogx::character::CharacterTypeScriptInstance::create(
          program, character, &error);
  if (!instance) {
    std::fprintf(stderr, "native band handler create failed: %s\n",
                 error.c_str());
    return false;
  }
  const std::pair<const char*, const char*> requests[] = {
      {"play", "normal"},
      {"idle", "idle"},
      {"wail_on", "extreme"},
      {"wail_off", "normal"},
      {"solo_on", "solo"},
      {"solo_off", "normal"},
      {"band_jump", "sync_jump"},
      {"sync_wag", "sync_wag"},
      {"sync_head_bang", "sync_head_bang"},
  };
  for (const auto& [handler, expected_group] : requests) {
    if (!instance->has_handler(handler) ||
        !instance->run_handler(handler, &error)) {
      std::fprintf(stderr, "native band handler failed: %s: %s\n",
                   handler, error.c_str());
      return false;
    }
    const auto messages = instance->take_driver_messages();
    if (messages.size() != 1 ||
        messages[0].driver != "main.drv" ||
        messages[0].message != "play_group" ||
        messages[0].args.size() != 2 ||
        messages[0].args.at(0).as_string().value_or("") != expected_group ||
        messages[0].args.at(1).as_int().value_or(0) !=
            ghogx::character::kCharPlayNow) {
      std::fprintf(stderr,
                   "native band group mismatch handler=%s expected=%s\n",
                   handler, expected_group);
      return false;
    }
  }
  ghogx::DataArray outcome;
  outcome.push(ghogx::DataNode::Sym(ghogx::Symbol("lose")));
  if (!instance->has_handler("set_game_over") ||
      !instance->run_handler("set_game_over", outcome, &error)) return false;
  const auto ending = instance->take_driver_messages();
  if (ending.size() != 1 || ending[0].message != "play_group" ||
      ending[0].args.at(0).as_string().value_or("") != "lose" ||
      ending[0].args.at(1).as_int().value_or(0) !=
          (ghogx::character::kCharPlayNow | ghogx::character::kCharPlayNoLoop)) return false;
  for (bool campaign : {false, true}) {
    instance->set_win_campaign_song(campaign);
    if (!instance->run_handler("i_won", &error)) return false;
    const auto messages = instance->take_driver_messages();
    if (messages.size() != 1 || messages[0].args.at(0).as_string().value_or("") !=
        (campaign ? "win_finals" : "win")) return false;
  }
  if (!instance->unhandled_messages().empty()) {
    std::fprintf(stderr, "native band handlers emitted unhandled messages\n");
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (!driver_bridge_contract()) return 1;
  if (!native_band_character_group_contract()) return 1;
  if (argc == 4 || argc == 5) {
    ghogx::character::Character character;
    if (!ghogx::character::load_character(
            argv[1], argv[2], argv[3], character)) {
      std::fprintf(stderr, "character load failed: %s\n", argv[3]);
      return 1;
    }
    std::string error;
    const auto program =
        ghogx::character::load_character_type_script_program(
            argv[1], argv[2], character.dir_type,
            character.root_object_type, &error);
    if (!program) {
      std::fprintf(
          stderr, "program load failed: class=%s type=%s error=%s\n",
          character.dir_type.c_str(),
          character.root_object_type.c_str(), error.c_str());
      return 1;
    }
    auto waypoints = std::make_shared<
        ghogx::character::CharacterTypeScriptWaypointRegistry>(
        std::vector<ghogx::character::CharacterTypeScriptWaypoint>{
            {0, "audit_start", 0xFFFFFFFFu},
        });
    auto instance =
        ghogx::character::CharacterTypeScriptInstance::create(
            program, character, waypoints,
            program->type_name() == "guitarist"
                ? std::string_view("guitarist0")
                : std::string_view(character.dir_name),
            &error);
    if (!instance || !instance->run_handler("enter", &error)) {
      std::fprintf(stderr, "enter failed: %s\n", error.c_str());
      return 1;
    }
    if (argc == 5) {
      std::istringstream requested(argv[4]);
      std::string handler;
      instance->take_driver_messages();
      while (std::getline(requested, handler, ',')) {
        const auto before = instance->unhandled_messages().size();
        const bool available = instance->has_handler(handler);
        const bool handled = available && instance->run_handler(handler, &error);
        const auto messages = instance->take_driver_messages();
        std::printf("handler=%s available=%d handled=%d new_unhandled=%zu drivers=%zu\n",
                    handler.c_str(), available ? 1 : 0, handled ? 1 : 0,
                    instance->unhandled_messages().size() - before, messages.size());
        for (const auto& message : messages) {
          std::printf("  driver=%s message=%s", message.driver.c_str(), message.message.c_str());
          for (size_t i = 0; i < message.args.size(); ++i) {
            if (const auto value = message.args.at(i).as_string())
              std::printf(" arg%zu=%.*s", i, static_cast<int>(value->size()), value->data());
            else if (const auto value = message.args.at(i).as_int())
              std::printf(" arg%zu=0x%08x", i, static_cast<unsigned>(*value));
          }
          std::printf("\n");
        }
      }
    }
    const auto driver_messages = instance->take_driver_messages();
    for (const auto& message : driver_messages) {
      std::printf("driver_message=%s:%s args=%zu",
                  message.driver.c_str(), message.message.c_str(),
                  message.args.size());
      for (size_t index = 0; index < message.args.size(); ++index) {
        const auto& arg = message.args.at(index);
        if (const auto value = arg.as_int()) {
          std::printf(" arg%zu=0x%08x", index,
                      static_cast<unsigned>(*value));
        } else if (const auto value = arg.as_string()) {
          std::printf(" arg%zu=%.*s", index,
                      static_cast<int>(value->size()), value->data());
        }
      }
      std::printf("\n");
    }
    std::printf(
        "character_type_script_test: class=%s type=%s worldFx=%zu "
        "waypoint_flags=%u teleported=%d unhandled=%zu driverMessages=%zu\n",
        character.dir_type.c_str(),
        character.root_object_type.c_str(), character.world_fxes.size(),
        instance->last_waypoint_find_flags().value_or(0),
        instance->last_teleport().has_value() ? 1 : 0,
        instance->unhandled_messages().size(), driver_messages.size());
    for (const auto& fx : character.world_fxes) {
      std::printf(
          "worldFx=%s serialized_showing=%d running_after_enter=%d "
          "proxy=%s parent=%s\n",
          fx.name.c_str(), fx.showing ? 1 : 0,
          instance->named_object_active(fx.name) ? 1 : 0,
          fx.proxy_path.c_str(), fx.parent.c_str());
    }
    if (instance->has_handler("solo_on") &&
        instance->has_handler("peak_on") &&
        instance->has_handler("peak_off")) {
      if (!instance->run_handler("solo_on", &error) ||
          !instance->run_handler("peak_on", &error)) {
        std::fprintf(
            stderr, "guitarist peak transition failed: %s\n",
            error.c_str());
        return 1;
      }
      std::printf(
          "guitarist_peak hand_flames_L=%d hand_flames_R=%d\n",
          instance->named_object_active("hand_flames_L") ? 1 : 0,
          instance->named_object_active("hand_flames_R") ? 1 : 0);
      const auto owns_world_fx =
          [&](const char* name) {
            return std::any_of(
                character.world_fxes.begin(),
                character.world_fxes.end(),
                [&](const auto& fx) { return fx.name == name; });
          };
      const bool owns_both_hand_flames =
          owns_world_fx("hand_flames_L") &&
          owns_world_fx("hand_flames_R");
      if (owns_both_hand_flames &&
          (!instance->named_object_active("hand_flames_L") ||
           !instance->named_object_active("hand_flames_R"))) {
        std::fprintf(
            stderr,
            "guitarist peak did not start both authored hand flames\n");
        return 1;
      }
      if (!instance->run_handler("peak_off", &error)) {
        std::fprintf(
            stderr, "guitarist peak-off transition failed: %s\n",
            error.c_str());
        return 1;
      }
      std::printf(
          "guitarist_peak_off hand_flames_L=%d hand_flames_R=%d\n",
          instance->named_object_active("hand_flames_L") ? 1 : 0,
          instance->named_object_active("hand_flames_R") ? 1 : 0);
      if (owns_both_hand_flames &&
          (instance->named_object_active("hand_flames_L") ||
           instance->named_object_active("hand_flames_R"))) {
        std::fprintf(
            stderr,
            "guitarist peak-off did not stop both authored hand flames\n");
        return 1;
      }
    }
    for (const auto& message : instance->unhandled_messages()) {
      std::printf("unhandled=%s\n", message.c_str());
    }
    return 0;
  }
  gh::dtb::Tree tree;
  try {
    tree = gh::dtb::parse_dta(R"DTA(
    (BandCharacter
      (types
        (crowd
          (hand clap)
          (milo_hand clap)
          (enter
            {set [hand] clap}
            {set [milo_hand] clap}
            {hand_L-clap.mesh set_showing TRUE}
            {hand_R-clap.mesh set_showing TRUE}
            {hand_L-devil.mesh set_showing 0}
            {hand_R-devil.mesh set_showing 0}
            {hand_L-fist.mesh set_showing 0}
            {hand_R-fist.mesh set_showing 0}
            {hand_R-lighter.mesh set_showing 0}
            {if {exists lighter_flame} {lighter_flame stop}}
            {switch $cheat_crowd_heads
              (monkey_crowd_heads {monkey.mesh set_showing TRUE})})
          (set_hand ($a)
            {if {!= $a [hand]}
              {switch [hand]
                (clap
                  {hand_L-clap.mesh set_showing 0}
                  {hand_R-clap.mesh set_showing 0})
                (devil
                  {hand_L-devil.mesh set_showing 0}
                  {hand_R-devil.mesh set_showing 0})
                (fist
                  {hand_L-fist.mesh set_showing 0}
                  {hand_R-fist.mesh set_showing 0})
                (lighter
                  {hand_L-clap.mesh set_showing 0}
                  {hand_R-lighter.mesh set_showing 0}
                  {if {exists lighter_flame} {lighter_flame stop}})}
              {switch $a
                (clap
                  {hand_L-clap.mesh set_showing 1}
                  {hand_R-clap.mesh set_showing 1})
                (devil
                  {hand_L-devil.mesh set_showing 1}
                  {hand_R-devil.mesh set_showing 1})
                (fist
                  {hand_L-fist.mesh set_showing 1}
                  {hand_R-fist.mesh set_showing 1})
                (lighter
                  {hand_L-clap.mesh set_showing 1}
                  {hand_R-lighter.mesh set_showing 1}
                  {if {exists lighter_flame} {lighter_flame start}})}
              {set [hand] $a}})
          (start_at ($waypoint)
            {if {!= $waypoint }
              {$this teleport $waypoint}
              {waypoint_last $waypoint}})
          (place
            {$this teleport {waypoint_find 4}}
            {waypoint_last {waypoint_find 4}})
          (
            (macro_bundle_handler
              {set [milo_hand] macro_bundle_seen})))))
    (WalkingBandCharacter
      (types
        (guitarist
          (walk_delays FALSE FALSE (35 55) (20 40) FALSE)
          (walkspot {| 64 128})
          (max_walk_wait 6))))
    (CharWalk
      (types
        (guitarist
          (path_radius 12))))
  )DTA");
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "parse failed: %s\n", ex.what());
    return 1;
  }
  std::string error;
  const auto program =
      argc == 3
          ? ghogx::character::load_character_type_script_program(
                argv[1], argv[2], "BandCharacter", "crowd", &error)
          : ghogx::character::compile_character_type_script_program(
                tree.root, "BandCharacter", "crowd", &error);
  if (!program) {
    std::fprintf(stderr, "compile failed: %s\n", error.c_str());
    return 1;
  }
  const auto walking_character_program =
      ghogx::character::compile_character_type_script_program(
          tree.root, "WalkingBandCharacter", "guitarist", &error);
  const auto walk_program =
      ghogx::character::compile_character_type_script_program(
          tree.root, "CharWalk", "guitarist", &error);
  const auto walk_config =
      walking_character_program && walk_program
          ? ghogx::character::character_type_script_walk_config(
                *walking_character_program, *walk_program, &error)
          : std::nullopt;
  if (!walk_config ||
      walk_config->delay_enabled !=
          std::array<bool, 5>{false, false, true, true, false} ||
      walk_config->delay_min[2] != 35.0f ||
      walk_config->delay_max[2] != 55.0f ||
      walk_config->delay_min[3] != 20.0f ||
      walk_config->delay_max[3] != 40.0f ||
      walk_config->waypoint_flags != (64u | 128u) ||
      walk_config->max_walk_wait != 6.0f ||
      walk_config->path_radius != 12.0f) {
    std::fprintf(
        stderr, "typed CharWalk source config mismatch: %s\n",
        error.c_str());
    return 1;
  }

  ghogx::character::Character character;
  character.dir_name = "crowd_test";
  character.dir_type = "BandCharacter";
  character.root_object_type = "crowd";
  character.meshes = {
      mesh("hand_L-clap.mesh", false),
      mesh("hand_R-clap.mesh", false),
      mesh("hand_L-devil.mesh", true),
      mesh("hand_R-devil.mesh", true),
      mesh("hand_L-fist.mesh", true),
      mesh("hand_R-fist.mesh", true),
      mesh("hand_R-lighter.mesh", true),
      mesh("monkey.mesh", false),
  };
  ghogx::character::WorldFx lighter_flame;
  lighter_flame.name = "lighter_flame";
  lighter_flame.revision = 1;
  lighter_flame.showing = true;
  lighter_flame.decoded = true;
  character.world_fxes.push_back(std::move(lighter_flame));

  auto waypoints = std::make_shared<
      ghogx::character::CharacterTypeScriptWaypointRegistry>(
      std::vector<ghogx::character::CharacterTypeScriptWaypoint>{
          {7, "singer_start_a", 4},
          {8, "singer_start_b", 4},
          {9, "bassist_start", 16},
      });
  auto instance = ghogx::character::CharacterTypeScriptInstance::create(
      program, character, waypoints, &error);
  if (!instance || !instance->has_handler("macro_bundle_handler") ||
      !instance->run_handler("macro_bundle_handler", &error) ||
      !instance->run_handler("enter", &error)) {
    std::fprintf(stderr, "enter failed: %s\n", error.c_str());
    return 1;
  }
  bool ok =
      showing(character, "hand_L-clap.mesh") &&
      showing(character, "hand_R-clap.mesh") &&
      !showing(character, "hand_L-devil.mesh") &&
      !showing(character, "hand_R-devil.mesh") &&
      !showing(character, "hand_L-fist.mesh") &&
      !showing(character, "hand_R-fist.mesh") &&
      !showing(character, "hand_R-lighter.mesh") &&
      !showing(character, "monkey.mesh") &&
      !instance->named_object_active("lighter_flame");
  bool waypoint_ok = true;
  std::optional<ghogx::character::CharacterTypeScriptWaypoint>
      first_waypoint;
  std::optional<ghogx::character::CharacterTypeScriptWaypoint>
      second_waypoint;
  if (argc != 3) {
    waypoint_ok &= instance->run_handler("place", &error);
    first_waypoint = instance->last_teleport();
    waypoint_ok &= instance->last_waypoint_find_flags().value_or(0) == 4;
    waypoint_ok &= first_waypoint && first_waypoint->source_index == 7;
    waypoint_ok &= instance->run_handler("place", &error);
    second_waypoint = instance->last_teleport();
    waypoint_ok &= second_waypoint && second_waypoint->source_index == 8;
    waypoint_ok &=
        waypoints->ordered_source_indices() ==
        std::vector<std::size_t>({9, 7, 8});
    ok &= waypoint_ok;
  }

  ok &= instance->run_clip_event(
      "{ $dude 'set_hand' 'lighter' }", &error);
  ok &= showing(character, "hand_L-clap.mesh") &&
        !showing(character, "hand_R-clap.mesh") &&
        showing(character, "hand_R-lighter.mesh") &&
        instance->named_object_active("lighter_flame");

  ok &= instance->run_clip_event(
      "{ $dude 'set_hand' 'fist' }", &error);
  ok &= !showing(character, "hand_L-clap.mesh") &&
        !showing(character, "hand_R-lighter.mesh") &&
        showing(character, "hand_L-fist.mesh") &&
        showing(character, "hand_R-fist.mesh") &&
        !instance->named_object_active("lighter_flame") &&
        instance->unhandled_messages().empty();

  if (!ok) {
    std::fprintf(stderr, "authored crowd type-script state mismatch: %s\n",
                 error.c_str());
    std::fprintf(
        stderr, "waypoint_ok=%d first=%lld second=%lld unhandled=%zu\n",
        waypoint_ok ? 1 : 0,
        first_waypoint
            ? static_cast<long long>(first_waypoint->source_index)
            : -1LL,
        second_waypoint
            ? static_cast<long long>(second_waypoint->source_index)
            : -1LL,
        instance->unhandled_messages().size());
    for (const auto& message : instance->unhandled_messages()) {
      std::fprintf(stderr, "unhandled=%s\n", message.c_str());
    }
    return 1;
  }
  std::printf("character_type_script_test: all checks passed\n");
  return 0;
}
