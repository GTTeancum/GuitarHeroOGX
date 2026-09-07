// engine/src/ui/screen_manager.cpp -- see screen_manager.h.

#include "ui/screen_manager.h"

#include "core/class_reg.h"
#include "ui/ui_classes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ghogx::ui {

namespace {
bool node_falsey(const DataNode& node) {
  if (node.empty()) return false;
  if (auto i = node.as_int()) return *i == 0;
  if (auto f = node.as_float()) return *f == 0.0f;
  if (auto s = node.as_string())
    return s->empty() || *s == "FALSE" || *s == "false" || *s == "0";
  return false;
}

bool loading_completion_is_external(Object* screen) {
  if (!screen) return false;
  const char* screen_name = screen->name().c_str();
  return std::strcmp(screen_name, "loading_screen") == 0 ||
         std::strcmp(screen_name, "practice_loading_screen") == 0;
}

Object* focused_panel(ScreenManager& mgr) {
  Object* screen = mgr.current_screen();
  if (!screen) return nullptr;
  Symbol panel_name =
      screen->get_property(Symbol("focus")).as_symbol().value_or(Symbol());
  return panel_name.valid() ? mgr.find_object(panel_name) : nullptr;
}

bool sfx_truthy(const DataNode& node) {
  if (auto i = node.as_int()) return *i != 0;
  if (auto f = node.as_float()) return *f != 0.0f;
  if (auto s = node.as_symbol())
    return !(*s == Symbol("FALSE") || *s == Symbol("false") ||
             *s == Symbol("0"));
  if (auto text = node.as_string())
    return !(*text == "FALSE" || *text == "false" || *text == "0" ||
             text->empty());
  return node.as_object() != nullptr;
}

Symbol node_name_symbol(const DataNode& node) {
  if (Object* obj = node.as_object()) return obj->name();
  if (auto sym = node.as_symbol()) return *sym;
  if (auto text = node.as_string()) return Symbol(*text);
  return Symbol();
}

Symbol current_screen_name(ScreenManager& mgr) {
  Object* screen = mgr.current_screen();
  return screen ? screen->name() : Symbol();
}

Symbol focused_panel_name(ScreenManager& mgr) {
  Object* panel = focused_panel(mgr);
  return panel ? panel->name() : Symbol();
}

Symbol current_component_name(ScreenManager& mgr) {
  return node_name_symbol(mgr.get_global(Symbol("component")));
}

Object* resolve_animation_target(ScreenManager& mgr, const DataArray& args) {
  if (args.empty()) return nullptr;
  if (args.size() == 1) {
    if (Object* obj = args.at(0).as_object()) return obj;
    Symbol name = node_name_symbol(args.at(0));
    return name.valid() ? mgr.resolve_object(name) : nullptr;
  }

  Object* owner = args.at(0).as_object();
  Symbol target_name = node_name_symbol(args.at(1));
  if (owner && target_name.valid()) {
    if (auto* dir = dynamic_cast<ObjectDir*>(owner)) {
      if (Object* child = dir->find_path(target_name.c_str())) return child;
      if (Object* child = dir->find(target_name)) return child;
    }
  }
  if (Object* target = args.at(1).as_object()) return target;
  return target_name.valid() ? mgr.resolve_object(target_name) : nullptr;
}

const DataArray* keyed_arg(const DataArray& args, Symbol key) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    auto arr = args.at(i).as_array();
    if (!arr || arr->empty()) continue;
    if (arr->at(0).as_symbol().value_or(Symbol()) == key) return arr.get();
  }
  return nullptr;
}

float node_float_value(const DataNode& node, float fallback = 0.0f) {
  if (auto f = node.as_float()) return *f;
  if (auto i = node.as_int()) return static_cast<float>(*i);
  return fallback;
}

float keyed_float(const DataArray& args, Symbol key, float fallback = 0.0f) {
  const DataArray* arr = keyed_arg(args, key);
  return arr && arr->size() > 1 ? node_float_value(arr->at(1), fallback)
                                : fallback;
}

Symbol keyed_symbol(const DataArray& args, Symbol key) {
  const DataArray* arr = keyed_arg(args, key);
  if (!arr || arr->size() < 2) return Symbol();
  return node_name_symbol(arr->at(1));
}

bool keyed_range(const DataArray& args, Symbol key, float& start, float& end) {
  const DataArray* arr = keyed_arg(args, key);
  if (!arr || arr->size() < 3) return false;
  start = node_float_value(arr->at(1), start);
  end = node_float_value(arr->at(2), end);
  return true;
}

float clamp_frame(float value, float a, float b) {
  const float lo = std::min(a, b);
  const float hi = std::max(a, b);
  return std::clamp(value, lo, hi);
}

void play_synth_sequence(ScreenManager& mgr, Symbol sequence) {
  if (!sequence.valid()) return;
  if (Object* synth = mgr.resolve_object(Symbol("synth"))) {
    DataArray args;
    args.push(DataNode::Sym(sequence));
    synth->handle_property(Symbol("play_sequence"), args);
  }
}

void dispatch_shared_menu_sfx(ScreenManager& mgr, Symbol msg) {
  // Mirrors ui/gen/sfx.dta, whose top-level handlers are not ordinary screen
  // objects in this harness.
  if (msg == Symbol("SELECT_START_MSG")) {
    const Symbol screen = current_screen_name(mgr);
    const Symbol component = current_component_name(mgr);
    const bool play =
        screen == Symbol("sel_song_screen") ||
        screen == Symbol("multi_seldiff_screen") ||
        screen == Symbol("seldiff_screen") ||
        component == Symbol("pause_restart.btn") ||
        component == Symbol("pause_controller_resume.btn") ||
        component == Symbol("lose_restart.btn") ||
        component == Symbol("comp_restart.btn");
    play_synth_sequence(mgr, play ? Symbol("button_play")
                                  : Symbol("button_select"));
    return;
  }

  if (msg == Symbol("SCROLL_MSG")) {
    const Symbol panel = focused_panel_name(mgr);
    if (panel != Symbol("credits_panel") &&
        panel != Symbol("sel_character_panel"))
      play_synth_sequence(mgr, Symbol("button_toggle"));
    return;
  }

  if (msg == Symbol("FOCUS_MSG")) {
    const Symbol screen = current_screen_name(mgr);
    if (!mgr.in_transition() &&
        screen != Symbol("sel_character_new_screen") &&
        screen != Symbol("sel_character_edit_screen"))
      play_synth_sequence(mgr, Symbol("button_toggle"));
    return;
  }

  if (msg == Symbol("SCREEN_BACK_MSG")) {
    Object* meta = mgr.resolve_object(Symbol("meta"));
    if (!meta || sfx_truthy(meta->handle_property(Symbol("is_up"), DataArray())))
      play_synth_sequence(mgr, Symbol("button_back.cue"));
  }
}

void dispatch_bad_select(ScreenManager& mgr) {
  Object* screen = mgr.current_screen();
  Object* panel = focused_panel(mgr);
  if (panel) panel->handle_property(Symbol("BAD_SELECT_MSG"), DataArray());
  if (mgr.current_screen() == screen && screen)
    screen->handle_property(Symbol("BAD_SELECT_MSG"), DataArray());

  // Stock sfx.dta: BAD_SELECT_MSG plays button_error except nameprof_panel.
  if (panel && panel->name() == Symbol("nameprof_panel")) return;
  if (Object* play_sfx = mgr.resolve_object(Symbol("play_sfx")))
    play_sfx->handle_property(Symbol("button_error"), DataArray());
}
}  // namespace

ScreenManager::ScreenManager() = default;

// --- registry --------------------------------------------------------------
void ScreenManager::add_object(std::unique_ptr<Object> obj) {
  if (auto* ui = dynamic_cast<UiObject*>(obj.get())) ui->set_manager(this);
  registry_.add(std::move(obj));
}
Object* ScreenManager::find_object(Symbol name) { return registry_.find(name); }

void ScreenManager::register_runtime_class(Symbol cls, RuntimeCreator creator) {
  if (cls.valid() && creator) runtime_creators_[cls.id()] = std::move(creator);
}

Object* ScreenManager::create_object(Symbol cls, Symbol name) {
  if (!cls.valid() || !name.valid()) return nullptr;
  std::unique_ptr<Object> object;
  if (auto it = runtime_creators_.find(cls.id()); it != runtime_creators_.end())
    object = it->second(name);
  else
    object = ClassReg::instance().create(cls);
  if (!object) return nullptr;
  object->set_name(name);
  if (auto* ui = dynamic_cast<UiObject*>(object.get())) ui->set_manager(this);
  return registry_.add(std::move(object));
}

void ScreenManager::add_singleton(Symbol name, std::unique_ptr<Object> obj) {
  singletons_[name.id()] = obj.get();
  singletons_owned_.push_back(std::move(obj));
}

// --- handler firing --------------------------------------------------------
DataNode ScreenManager::run_object_handler(const std::shared_ptr<gh::dtb::Node>& block,
                                           Object* self, const DataArray& args) {
  script::Env env;
  env.host = this;
  env.self = nullptr;
  env.scope = nullptr;
  return interp_.run_handler(gh::dtb::children(*block), self, args, env);
}

void ScreenManager::add_function(Symbol name, std::shared_ptr<gh::dtb::Node> block) {
  if (name.valid() && block) functions_[name.id()] = std::move(block);
}

// --- screen navigation -----------------------------------------------------
std::vector<Symbol> ScreenManager::screen_panels(Object* screen) {
  std::vector<Symbol> out;
  if (!screen) return out;
  DataNode p = screen->get_property(Symbol("panels"));
  if (auto arr = p.as_array()) {
    for (std::size_t i = 0; i < arr->size(); ++i)
      if (auto s = arr->at(i).as_symbol()) out.push_back(*s);
  } else if (auto s = p.as_symbol()) {
    out.push_back(*s);
  }
  return out;
}

void ScreenManager::send_screen_panels(Object* screen, Symbol msg, bool screen_first) {
  auto panels = [&] {
    for (Symbol pn : screen_panels(screen))
      if (Object* p = find_object(pn)) p->handle_property(msg, DataArray());
  };
  if (screen_first) { screen->handle_property(msg, DataArray()); panels(); }
  else { panels(); screen->handle_property(msg, DataArray()); }
}

void ScreenManager::exit_sequence(Object* screen, bool back, bool defer_unload) {
  if (!screen) return;
  // screen_change for both directions, EXCEPT screens that define a screen_back
  // handler fire it on back (chooseprof/mem_card/bonus_material/credits) -- the
  // rule "run screen_back only if defined" reproduces those exceptions without a
  // name list (menus.md).
  auto* u = dynamic_cast<UiObject*>(screen);
  bool has_back = u && u->has_handler(Symbol("screen_back"));
  screen->handle_property(back && has_back ? Symbol("screen_back") : Symbol("screen_change"),
                          DataArray());
  send_screen_panels(screen, Symbol("exit"), /*screen_first=*/true);
  screen->handle_property(back ? Symbol("ui_exit_back") : Symbol("ui_exit"), DataArray());
  for (Symbol panel_name : screen_panels(screen)) {
    if (Object* panel = find_object(panel_name)) {
      dispatch_panel_transition(
          panel, Symbol("ui_exit"),
          back ? Symbol("ui_exit_back") : Symbol("ui_exit_forward"));
    }
  }
  if (!defer_unload) {
    send_screen_panels(screen, Symbol("exit_complete"), /*screen_first=*/true);
    send_screen_panels(screen, Symbol("unload"), /*screen_first=*/true);
  }
}

float ScreenManager::animatable_duration_seconds(ObjectDir* panel,
                                                 Object* anim) const {
  if (!panel || !anim) return 0.0f;
  float start_frame = 0.0f;
  float end_frame = 0.0f;
  if (anim->class_name() == Symbol("AnimFilter")) {
    const Symbol source =
        anim->get_property(Symbol("anim_ref")).as_symbol().value_or(Symbol());
    if (!source.valid() || !panel->find(source)) return 0.0f;
    float scale = node_float_value(anim->get_property(Symbol("scale")), 1.0f);
    if (!std::isfinite(scale) || std::fabs(scale) <= 0.0001f) scale = 1.0f;
    const float offset =
        node_float_value(anim->get_property(Symbol("offset")), 0.0f);
    const float authored_start =
        node_float_value(anim->get_property(Symbol("start")), 0.0f);
    const float authored_end =
        node_float_value(anim->get_property(Symbol("end")), 0.0f);
    start_frame = (authored_start - offset) / scale;
    const float end_offset =
        offset + (authored_end >= authored_start ? 0.0f
                                                 : authored_start - authored_end);
    end_frame = (authored_end - end_offset) / scale;
    if (anim->get_property(Symbol("filter_type")).as_int().value_or(0) == 2)
      end_frame *= 2.0f;
  } else if (anim->class_name() == Symbol("TransAnim")) {
    start_frame =
        node_float_value(anim->get_property(Symbol("start_frame")), 0.0f);
    end_frame =
        node_float_value(anim->get_property(Symbol("end_frame")), 0.0f);
  } else {
    return 0.0f;
  }
  const float seconds = std::fabs(end_frame - start_frame) / 30.0f;
  return std::isfinite(seconds) ? seconds : 0.0f;
}

float ScreenManager::dispatch_panel_transition(Object* panel_object,
                                               Symbol event,
                                               Symbol directional_event) {
  auto* panel = dynamic_cast<ObjectDir*>(panel_object);
  if (!panel) return 0.0f;
  float blocking_seconds = 0.0f;
  for (std::size_t i = 0; i < panel->size(); ++i) {
    Object* trigger = panel->at(i);
    if (!trigger || trigger->class_name() != Symbol("UITrigger")) continue;
    const Symbol trigger_event =
        trigger->get_property(Symbol("event")).as_symbol().value_or(Symbol());
    if (trigger_event != event && trigger_event != directional_event) continue;

    trigger->handle_property(trigger_event, DataArray());
    trigger->set_property(Symbol("triggered_at"), DataNode::Float(ui_seconds_));
    const Symbol anim_ref =
        trigger->get_property(Symbol("anim_ref")).as_symbol().value_or(Symbol());
    Object* anim = anim_ref.valid() ? panel->find(anim_ref) : nullptr;
    const float duration = animatable_duration_seconds(panel, anim);
    trigger->set_property(Symbol("end_time"),
                          DataNode::Float(duration > 0.0f
                                              ? ui_seconds_ + duration
                                              : 0.0f));
    if (anim) {
      anim->set_property(Symbol("triggered_at"), DataNode::Float(ui_seconds_));
      anim->set_property(Symbol("trigger_event"),
                         DataNode::Sym(trigger_event));
      anim->set_property(Symbol("trigger_duration"),
                         DataNode::Float(duration));
      // UITrigger owns an RndAnimatable reference and starts it when its
      // authored event fires. Drive that same live object here so TransAnim,
      // MatAnim, Group, and AnimFilter frames advance through the ordinary
      // panel animation graph instead of leaving the renderer to infer a
      // separate one-shot from the event name.
      start_animation_task(anim, DataArray());
    }
    const bool blocks =
        !node_falsey(trigger->get_property(Symbol("block_transition")));
    if (blocks && duration > 0.0f)
      blocking_seconds = std::max(blocking_seconds, duration);
  }
  return blocking_seconds;
}

float ScreenManager::enter_sequence(Object* screen, bool back) {
  if (!screen) return 0.0f;
  bool has_helpbar_panel = false;
  for (Symbol panel : screen_panels(screen)) {
    if (panel == Symbol("helpbar")) {
      has_helpbar_panel = true;
      break;
    }
  }
  if (has_helpbar_panel) {
    if (Object* helpbar = resolve_object(Symbol("helpbar"))) {
      helpbar->set_property(Symbol("display"),
                            screen->get_property(Symbol("helpbar")));
    }
  }
  send_screen_panels(screen, Symbol("change_proxies"), /*screen_first=*/false);
  send_screen_panels(screen, Symbol("load"), /*screen_first=*/false);
  send_screen_panels(screen, Symbol("finish_load"), /*screen_first=*/false);
  screen->handle_property(back ? Symbol("ui_enter_back") : Symbol("ui_enter"), DataArray());
  send_screen_panels(screen, Symbol("enter"), /*screen_first=*/false);  // sub-objects then screen
  float blocking_seconds = 0.0f;
  for (Symbol panel_name : screen_panels(screen)) {
    if (Object* panel = find_object(panel_name)) {
      blocking_seconds = std::max(
          blocking_seconds,
          dispatch_panel_transition(
              panel, Symbol("ui_enter"),
              back ? Symbol("ui_enter_back") : Symbol("ui_enter_forward")));
    }
  }
  // Scene-state ID is refined per-screen (harmonix_symbols.h:904) when the
  // gameplay handoff is wired; menus are NORMAL/MENU here.
  scene_state_ = 1;
  return blocking_seconds;
}

ScreenManager::TransitionSnapshot ScreenManager::transition_snapshot() const {
  TransitionSnapshot out;
  out.active = transition_.active;
  out.back = transition_.back;
  out.remaining = transition_.remaining;
  out.duration = transition_.duration;
  out.exiting_screen = transition_.exiting_screen;
  out.entering_screen = transition_.entering_screen;
  if (transition_.active && transition_.duration > 0.0001f) {
    out.progress = std::clamp(
        1.0f - transition_.remaining / transition_.duration, 0.0f, 1.0f);
  } else {
    out.progress = transition_.active ? 0.0f : 1.0f;
  }
  return out;
}

void ScreenManager::request_backwards_anim() {
  pending_backwards_anim_ = true;
}

bool ScreenManager::consume_backwards_anim() {
  const bool requested = pending_backwards_anim_;
  pending_backwards_anim_ = false;
  return requested;
}

void ScreenManager::finish_transition() {
  if (!transition_.active) return;
  Object* entering = transition_.entering_screen;
  Object* exiting = transition_.exiting_screen;
  transition_ = TransitionState{};
  if (entering && !loading_completion_is_external(entering)) {
    send_screen_panels(entering, Symbol("TRANSITION_COMPLETE_MSG"),
                       /*screen_first=*/false);
  }
  if (exiting) {
    send_screen_panels(exiting, Symbol("exit_complete"),
                       /*screen_first=*/true);
    send_screen_panels(exiting, Symbol("unload"), /*screen_first=*/true);
  }
}

void ScreenManager::goto_screen(Symbol name) {
  // Both stock video-settings routes still target GH2's one-value LagPanel.
  // Preserve those authored routes and transparently substitute the native
  // two-value Practice Room Soundcheck when it is installed.
  if ((name == Symbol("lag_screen") || name == Symbol("pause_lag_screen")) &&
      find_object(Symbol("soundcheck_screen")))
    name = Symbol("soundcheck_screen");
  const bool back = consume_backwards_anim();
  Object* target = find_object(name);
  if (!target) { on_unhandled(std::string("goto_screen?:") + name.c_str()); return; }
  if (target == current_) return;
  if (transition_.active) finish_transition();
  Object* exiting = current_;
  if (current_) {
    // Authored reverse routes mark the transition before calling goto_screen.
    // They consume history instead of creating a new forward visit.
    if (!back) history_.push_back(current_);
    exit_sequence(current_, back, /*defer_unload=*/true);
  }
  current_ = target;
  if (back) {
    const auto target_it =
        std::find(history_.rbegin(), history_.rend(), current_);
    if (target_it != history_.rend())
      history_.erase(std::next(target_it).base());
  } else {
    history_.erase(std::remove(history_.begin(), history_.end(), current_),
                   history_.end());
  }
  const float blocking_seconds = enter_sequence(current_, back);
  transition_.active = true;
  transition_.back = back;
  transition_.remaining = blocking_seconds;
  transition_.duration = blocking_seconds;
  transition_.exiting_screen = exiting;
  transition_.entering_screen = current_;
  if (blocking_seconds <= 0.0f) finish_transition();
}

void ScreenManager::push_screen(Symbol name) {
  // Overlay: the underlying screen stays loaded (and unpolled); it is NOT exited.
  Object* target = find_object(name);
  if (!target) { on_unhandled(std::string("push_screen?:") + name.c_str()); return; }
  if (transition_.active) finish_transition();
  if (current_) stack_.push_back(current_);
  current_ = target;
  const float blocking_seconds = enter_sequence(current_, /*back=*/false);
  transition_.active = true;
  transition_.back = false;
  transition_.remaining = blocking_seconds;
  transition_.duration = blocking_seconds;
  transition_.entering_screen = current_;
  if (blocking_seconds <= 0.0f) finish_transition();
}

void ScreenManager::pop_screen() {
  // Exit the overlay; the underlying screen (still loaded) resumes as current.
  if (transition_.active) finish_transition();
  if (current_) exit_sequence(current_, /*back=*/true, /*defer_unload=*/false);
  if (!stack_.empty()) {
    current_ = stack_.back();
    stack_.pop_back();
  } else {
    current_ = nullptr;
  }
}

void ScreenManager::pop_screen(Symbol next_overlay) {
  Object* target = next_overlay.valid() ? find_object(next_overlay) : nullptr;
  if (next_overlay.valid() && !target) {
    on_unhandled(std::string("pop_screen?:") + next_overlay.c_str());
    return;
  }
  pop_screen();
  if (!target) return;
  if (transition_.active) finish_transition();
  if (current_) stack_.push_back(current_);
  current_ = target;
  const float blocking_seconds = enter_sequence(current_, /*back=*/false);
  transition_.active = true;
  transition_.back = false;
  transition_.remaining = blocking_seconds;
  transition_.duration = blocking_seconds;
  transition_.entering_screen = current_;
  if (blocking_seconds <= 0.0f) finish_transition();
}

void ScreenManager::go_back() {
  if (!current_) return;

  Object* target = nullptr;
  Symbol back =
      current_->get_property(Symbol("back_screen")).as_symbol().value_or(Symbol());
  if (back.valid()) target = find_object(back);

  if (!target && !history_.empty()) {
    target = history_.back();
    history_.pop_back();
  } else if (target && !history_.empty() && history_.back() == target) {
    history_.pop_back();
  }

  if (!target && current_->name() != Symbol("main_screen"))
    target = find_object(Symbol("main_screen"));
  if (!target || target == current_) return;

  if (transition_.active) finish_transition();
  Object* exiting = current_;
  exit_sequence(current_, /*back=*/true, /*defer_unload=*/true);
  current_ = target;
  const float blocking_seconds = enter_sequence(current_, /*back=*/true);
  transition_.active = true;
  transition_.back = true;
  transition_.remaining = blocking_seconds;
  transition_.duration = blocking_seconds;
  transition_.exiting_screen = exiting;
  transition_.entering_screen = current_;
  if (blocking_seconds <= 0.0f) finish_transition();
}

void ScreenManager::update(float dt) {
  ui_seconds_ += dt;
  run_due_script_tasks();
  poll_active_animations();
  if (!current_) return;
  for (Symbol pn : screen_panels(current_)) {
    if (Object* panel = find_object(pn)) panel->handle_property(Symbol("poll"), DataArray());
  }
  current_->handle_property(Symbol("poll"), DataArray());
  if (transition_.active) {
    transition_.remaining -= std::max(0.0f, dt);
    if (transition_.remaining <= 0.0f) finish_transition();
  }
  run_due_script_tasks();
  poll_active_animations();
}

DataNode ScreenManager::run_script(const gh::dtb::NodeList& roots) {
  script::Scope root;
  script::Env env;
  env.host = this;
  env.self = nullptr;
  env.scope = &root;
  DataNode last;
  for (const auto& n : roots) last = interp_.eval(*n, env);
  return last;
}

void ScreenManager::schedule_script_task(const gh::dtb::NodeList& body,
                                         Object* self,
                                         float delay_seconds) {
  script_tasks_.push_back({body, self,
                           ui_seconds_ + std::max(0.0f, delay_seconds)});
}

const gh::dtb::NodeList* ScreenManager::resolve_function(Symbol name) {
  auto it = functions_.find(name.id());
  if (it == functions_.end() || !it->second) return nullptr;
  return &gh::dtb::children(*it->second);
}

void ScreenManager::clear_script_tasks() {
  script_tasks_.clear();
}

void ScreenManager::run_due_script_tasks() {
  for (;;) {
    auto it = std::find_if(script_tasks_.begin(), script_tasks_.end(),
                           [&](const ScheduledScriptTask& task) {
                             return task.due_seconds <= ui_seconds_;
                           });
    if (it == script_tasks_.end()) return;
    ScheduledScriptTask task = std::move(*it);
    script_tasks_.erase(it);

    script::Scope root;
    script::Env env;
    env.host = this;
    env.self = task.self;
    env.scope = &root;
    interp_.eval_seq(task.body, 0, env);
  }
}

void ScreenManager::poll_active_animations() {
  if (active_animations_.empty()) return;
  auto out = active_animations_.begin();
  for (ActiveAnimation& anim : active_animations_) {
    if (!anim.target) continue;
    if (ui_seconds_ < anim.start_seconds) {
      *out++ = anim;
      continue;
    }
    float frame = 0.0f;
    bool done = false;
    if (anim.forever) {
      frame = std::max(0.0f, anim.frames_per_second) * ui_seconds_;
    } else {
      const float elapsed = std::max(0.0f, ui_seconds_ - anim.start_seconds);
      frame = anim.start_frame + anim.frames_per_second * elapsed;
      if (anim.loop) {
        const float lo = std::min(anim.start_frame, anim.end_frame);
        const float hi = std::max(anim.start_frame, anim.end_frame);
        const float span = hi - lo;
        if (span > 0.0f) {
          frame = std::fmod(frame - lo, span);
          if (frame < 0.0f) frame += span;
          frame += lo;
        }
      } else {
        frame = clamp_frame(frame, anim.start_frame, anim.end_frame);
        done = anim.duration_seconds <= 0.0f ||
               elapsed + 0.000001f >= anim.duration_seconds;
      }
    }
    set_animation_frame(anim.target, frame);
    if (done) {
      anim.target->set_property(Symbol("anim_task_active"), DataNode::Int(0));
      anim.target->set_property(Symbol("animating"), DataNode::Int(0));
      if (anim.task_name.valid())
        anim.target->set_property(Symbol("finished_anim_task"),
                                  DataNode::Sym(anim.task_name));
      continue;
    }
    *out++ = anim;
  }
  active_animations_.erase(out, active_animations_.end());
}

// --- Object (the `ui` object messages) -------------------------------------
DataNode ScreenManager::handle_property(Symbol msg, const DataArray& args) {
  const char* m = msg.c_str();
  if (std::strcmp(m, "goto_screen") == 0) {
    if (args.size()) if (auto s = args.at(0).as_symbol()) goto_screen(*s);
    return DataNode();
  }
  if (std::strcmp(m, "push_screen") == 0) {
    if (args.size()) if (auto s = args.at(0).as_symbol()) push_screen(*s);
    return DataNode();
  }
  if (std::strcmp(m, "pop_screen") == 0) {
    if (args.size()) {
      if (auto s = args.at(0).as_symbol()) pop_screen(*s);
      else if (auto text = args.at(0).as_string()) pop_screen(Symbol(*text));
      else pop_screen();
    } else {
      pop_screen();
    }
    return DataNode();
  }
  if (std::strcmp(m, "go_back") == 0) { go_back(); return DataNode(); }
  if (std::strcmp(m, "current_screen") == 0)
    return current_ ? DataNode::Sym(current_->name()) : DataNode();
  if (std::strcmp(m, "focus_panel") == 0) {
    Object* panel = focused_panel(*this);
    return panel ? DataNode::Obj(panel) : DataNode();
  }
  if (std::strcmp(m, "BAD_SELECT_START_MSG") == 0) {
    dispatch_bad_select(*this);
    return DataNode();
  }
  if (std::strcmp(m, "SELECT_START_MSG") == 0 ||
      std::strcmp(m, "SCROLL_MSG") == 0 ||
      std::strcmp(m, "FOCUS_MSG") == 0 ||
      std::strcmp(m, "SCREEN_BACK_MSG") == 0) {
    dispatch_shared_menu_sfx(*this, msg);
    return DataNode();
  }
  if (std::strcmp(m, "in_transition") == 0) return DataNode::Int(in_transition() ? 1 : 0);
  if (std::strcmp(m, "my_init") == 0) return DataNode();
  return Object::handle_property(msg, args);
}

// --- script::Host ----------------------------------------------------------
Object* ScreenManager::resolve_object(Symbol name) {
  const char* n = name.c_str();
  if (std::strcmp(n, "ui") == 0) return this;
  auto it = singletons_.find(name.id());
  if (it != singletons_.end()) return it->second;
  if (Object* o = registry_.find(name)) return o;
  // Dotted child path (e.g. main_msg.view): search the current screen's panels.
  if (current_) {
    if (Object* o = registry_.find_path(n)) return o;
    for (Symbol pn : screen_panels(current_)) {
      if (Object* panel = find_object(pn)) {
        if (auto* dir = dynamic_cast<ObjectDir*>(panel)) {
          if (Object* c = dir->find_path(n)) return c;
        }
      }
    }
  }
  return nullptr;
}

DataNode ScreenManager::get_global(Symbol name) {
  auto it = globals_.find(name.id());
  return it == globals_.end() ? DataNode() : it->second;
}
void ScreenManager::set_global(Symbol name, DataNode value) {
  globals_[name.id()] = std::move(value);
}
void ScreenManager::set_locale_string(Symbol token, std::string text) {
  locale_[token.id()] = std::move(text);
}
std::string ScreenManager::localize(Symbol token) {
  auto it = locale_.find(token.id());
  if (it != locale_.end()) return it->second;
  const std::string ps2_key = std::string(token.c_str()) + "_ps2";
  it = locale_.find(Symbol(ps2_key.c_str()).id());
  if (it != locale_.end()) return it->second;

  // GH2's originally single-outfit characters have no *_outfit_blurb entry
  // because retail never opened the outfit picker for them. Preserve that
  // source absence as an empty description; a character biography is not
  // canonical outfit copy.
  const std::string key = token.c_str();
  constexpr std::string_view suffix = "_outfit_blurb";
  if (key.size() > suffix.size() &&
      key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return {};
  }
  return key;
}
void ScreenManager::on_unhandled(const std::string& what) {
  if (unhandled_seen_.emplace(what, true).second) unhandled_.push_back(what);
}

void ScreenManager::start_animation_task(Object* target, const DataArray& args) {
  if (!target) return;
  float start = target->handle_property(Symbol("start_frame"), DataArray())
                    .as_float().value_or(
                        target->get_property(Symbol("start_frame"))
                            .as_float().value_or(0.0f));
  float end = target->handle_property(Symbol("end_frame"), DataArray())
                  .as_float().value_or(
                      target->get_property(Symbol("end_frame"))
                          .as_float().value_or(start));
  keyed_range(args, Symbol("range"), start, end);
  bool has_loop = keyed_range(args, Symbol("loop"), start, end);
  if (const DataArray* loop = keyed_arg(args, Symbol("loop"));
      loop && loop->size() < 3) {
    has_loop = true;
  }
  if (const DataArray* dest = keyed_arg(args, Symbol("dest"));
      dest && dest->size() > 1) {
    start = target->get_property(Symbol("frame")).as_float().value_or(start);
    end = node_float_value(dest->at(1), end);
    has_loop = false;
  }
  const float period = keyed_float(args, Symbol("period"), 0.0f);
  const float delay = std::max(0.0f, keyed_float(args, Symbol("delay"), 0.0f));
  const float speed_scale = keyed_float(args, Symbol("scale"), 1.0f);
  const Symbol task_name = keyed_symbol(args, Symbol("name"));

  float frames_per_second = 30.0f * speed_scale;
  const float span = std::fabs(end - start);
  if (period > 0.0f && span > 0.0f) frames_per_second = span / period;
  if (end < start) frames_per_second = -frames_per_second;
  const float duration =
      period > 0.0f ? period
                    : (std::fabs(frames_per_second) > 0.0f
                           ? span / std::fabs(frames_per_second)
                           : 0.0f);

  active_animations_.erase(
      std::remove_if(active_animations_.begin(), active_animations_.end(),
                     [&](const ActiveAnimation& anim) {
                       return anim.target == target &&
                              (!task_name.valid() ||
                               anim.task_name == task_name ||
                               !anim.task_name.valid());
                     }),
      active_animations_.end());

  target->set_property(Symbol("animating"), DataNode::Int(1));
  target->set_property(Symbol("anim_task_active"), DataNode::Int(1));
  if (task_name.valid())
    target->set_property(Symbol("anim_task_name"), DataNode::Sym(task_name));

  if (delay <= 0.0f) set_animation_frame(target, start);
  active_animations_.push_back(
      {target, task_name, start, end, frames_per_second, ui_seconds_ + delay,
       duration, false, has_loop});
}

void ScreenManager::stop_animation_task(Object* target) {
  if (!target) return;
  active_animations_.erase(
      std::remove_if(active_animations_.begin(), active_animations_.end(),
                     [&](const ActiveAnimation& anim) {
                       return anim.target == target;
                     }),
      active_animations_.end());
  target->set_property(Symbol("anim_task_active"), DataNode::Int(0));
  target->set_property(Symbol("animating"), DataNode::Int(0));
}

bool ScreenManager::is_animation_task_active(Object* target) const {
  return target &&
         std::any_of(active_animations_.begin(), active_animations_.end(),
                     [&](const ActiveAnimation& anim) {
                       return anim.target == target;
                     });
}

void ScreenManager::set_animation_frame(Object* target, float frame) {
  if (!target || !std::isfinite(frame)) return;
  target->set_property(Symbol("frame"), DataNode::Float(frame));

  ObjectDir* container = nullptr;
  for (std::size_t i = 0; i < registry_.size() && !container; ++i) {
    auto* candidate = dynamic_cast<ObjectDir*>(registry_.at(i));
    if (!candidate) continue;
    for (std::size_t child_i = 0; child_i < candidate->size(); ++child_i) {
      if (candidate->at(child_i) == target) {
        container = candidate;
        break;
      }
    }
  }

  if (target->class_name() == Symbol("AnimFilter") && container) {
    const Symbol source =
        target->get_property(Symbol("anim_ref")).as_symbol().value_or(Symbol());
    Object* child = source.valid() ? container->find(source) : nullptr;
    if (child) {
      float scale = node_float_value(target->get_property(Symbol("scale")), 1.0f);
      const float start =
          node_float_value(target->get_property(Symbol("start")), 0.0f);
      const float end =
          node_float_value(target->get_property(Symbol("end")), 0.0f);
      if (end < start) scale = -std::fabs(scale);
      const float offset =
          node_float_value(target->get_property(Symbol("offset")), 0.0f) +
          (end < start ? start - end : 0.0f);
      float source_frame = frame * scale + offset;
      const float lo = std::min(start, end);
      const float hi = std::max(start, end);
      const float span = hi - lo;
      const int filter_type =
          target->get_property(Symbol("filter_type")).as_int().value_or(0);
      if (span > 0.0001f) {
        if (filter_type == 1) {
          source_frame = std::fmod(source_frame - lo, span);
          if (source_frame < 0.0f) source_frame += span;
          source_frame += lo;
        } else if (filter_type == 2) {
          const float cycle = span * 2.0f;
          source_frame = std::fmod(source_frame - lo, cycle);
          if (source_frame < 0.0f) source_frame += cycle;
          if (source_frame > span) source_frame = cycle - source_frame;
          source_frame += lo;
        } else {
          source_frame = std::clamp(source_frame, lo, hi);
        }
      }
      set_animation_frame(child, source_frame);
    }
    return;
  }

  auto propagate_children = [&](ObjectDir* dir, const DataNode& value) {
    auto children = value.as_array();
    if (!dir || !children) return;
    for (std::size_t i = 0; i < children->size(); ++i) {
      const Symbol name = children->at(i).as_symbol().value_or(Symbol());
      Object* child = name.valid() ? dir->find(name) : nullptr;
      if (!child) continue;
      const Symbol type = child->class_name();
      if (type == Symbol("TransAnim") || type == Symbol("MatAnim") ||
          type == Symbol("AnimFilter") || type == Symbol("Group"))
        set_animation_frame(child, frame);
    }
  };
  if (target->class_name() == Symbol("Group")) {
    propagate_children(container, target->get_property(Symbol("anim_children")));
  } else if (auto* dir = dynamic_cast<ObjectDir*>(target)) {
    for (std::size_t i = 0; i < dir->size(); ++i) {
      Object* child = dir->at(i);
      if (!child) continue;
      const Symbol type = child->class_name();
      if (type == Symbol("TransAnim") || type == Symbol("MatAnim") ||
          type == Symbol("AnimFilter") || type == Symbol("Group"))
        set_animation_frame(child, frame);
    }
  }
}

bool ScreenManager::symbol_exists(Symbol name) {
  if (resolve_object(name)) return true;
  if (resolve_function(name)) return true;
  return std::any_of(active_animations_.begin(), active_animations_.end(),
                     [&](const ActiveAnimation& anim) {
                       return anim.task_name.valid() && anim.task_name == name;
                     });
}

bool ScreenManager::handle_command(Symbol name, const DataArray& args,
                                   DataNode& out) {
  if (name == Symbol("set_loader_period")) {
    const float period =
        args.size() ? args.at(0).as_float().value_or(0.0f) : 0.0f;
    DataNode previous = get_global(Symbol("loader_period"));
    if (previous.empty()) previous = DataNode::Float(10.0f);
    const DataNode current = DataNode::Float(period);
    set_global(Symbol("loader_period"), current);
    if (Object* taskmgr = resolve_object(Symbol("taskmgr"))) {
      taskmgr->set_property(Symbol("loader_period"), current);
      taskmgr->set_property(Symbol("previous_loader_period"), previous);
    }
    out = previous;
    return true;
  }

  if (name == Symbol("animate_forever_30fps")) {
    Object* target = resolve_animation_target(*this, args);
    if (!target) return false;
    const float fps = 30.0f;
    const float frame = fps * ui_seconds_;
    target->set_property(Symbol("animating"), DataNode::Int(1));
    target->set_property(Symbol("animate_forever_30fps"), DataNode::Int(1));
    target->set_property(Symbol("anim_rate"), DataNode::Sym(Symbol("k30_fps_ui")));
    target->set_property(Symbol("anim_loop"), DataNode::Int(1));
    target->set_property(Symbol("frame"), DataNode::Float(frame));
    DataArray set_frame_args;
    set_frame_args.push(DataNode::Float(frame));
    target->handle_property(Symbol("set_frame"), set_frame_args);
    if (std::none_of(active_animations_.begin(), active_animations_.end(),
                     [&](const ActiveAnimation& anim) {
                       return anim.target == target;
                     })) {
      active_animations_.push_back({target, Symbol(), 0.0f, 0.0f, fps,
                                    ui_seconds_, 0.0f, true, true});
    }
    out = DataNode::Obj(target);
    return true;
  }

  const bool restart = name == Symbol("game_restart");
  const bool restart_fast = name == Symbol("game_restart_fast");
  if (!restart && !restart_fast) return false;

  Object* game = resolve_object(Symbol("game"));
  if (game) {
    game->set_property(Symbol("last_restart_command"), DataNode::Sym(name));
    game->set_property(Symbol("restart_fast"),
                       DataNode::Int(restart_fast ? 1 : 0));
    game->set_property(
        Symbol("restart_count"),
        DataNode::Int(game->get_property(Symbol("restart_count"))
                          .as_int()
                          .value_or(0) +
                      1));
    game->set_property(Symbol("intro_complete"), DataNode::Sym(Symbol("FALSE")));
    game->set_property(Symbol("paused"), DataNode::Sym(Symbol("FALSE")));
  }

  Symbol target = Symbol("game_screen");
  if (Object* gamecfg = resolve_object(Symbol("gamecfg"))) {
    DataArray get_game_screen;
    get_game_screen.push(DataNode::Sym(Symbol("game_screen")));
    Symbol configured =
        node_name_symbol(gamecfg->handle_property(Symbol("get"), get_game_screen));
    if (configured.valid()) target = configured;
  }
  if (find_object(target)) goto_screen(target);
  out = DataNode::Sym(name);
  return true;
}

// --- singleton stubs -------------------------------------------------------
namespace {

int arg_int(const DataArray& args, std::size_t index, int fallback = 0) {
  if (index >= args.size()) return fallback;
  if (auto i = args.at(index).as_int()) return *i;
  if (auto f = args.at(index).as_float()) return static_cast<int>(*f);
  if (auto arr = args.at(index).as_array(); arr && arr->size() == 1) {
    if (auto i = arr->at(0).as_int()) return *i;
    if (auto f = arr->at(0).as_float()) return static_cast<int>(*f);
  }
  return fallback;
}

Symbol arg_symbol(const DataArray& args, std::size_t index,
                  Symbol fallback = Symbol()) {
  if (index >= args.size()) return fallback;
  if (auto s = args.at(index).as_symbol()) return *s;
  if (auto text = args.at(index).as_string()) return Symbol(*text);
  return fallback;
}

bool node_bool(const DataNode& node) {
  if (auto i = node.as_int()) return *i != 0;
  if (auto f = node.as_float()) return *f != 0.0f;
  if (auto s = node.as_symbol())
    return !(*s == Symbol("FALSE") || *s == Symbol("false") ||
             *s == Symbol("0"));
  if (auto s = node.as_string())
    return !(*s == "FALSE" || *s == "false" || *s == "0" || s->empty());
  return node.as_object() != nullptr;
}

bool arg_bool(const DataArray& args, std::size_t index, bool fallback = false) {
  if (index >= args.size()) return fallback;
  return node_bool(args.at(index));
}

DataNode int_bool(bool value) { return DataNode::Int(value ? 1 : 0); }

// A placeholder for the non-UI game objects the menu scripts message
// (game/gamecfg/campaign/synth/...). Answers the few queries the menus read so
// boot doesn't fault; everything else falls to the universal Object messages
// (so set/get on it still works) and is logged to the worklist.
class StubObject : public Object {
 public:
  StubObject(Symbol cls, ScreenManager* mgr) : cls_(cls), mgr_(mgr) {
    if (cls_ == Symbol("options")) {
      // options.dtb defines SLIDER_LEVELS as 12 and the audio panel enter
      // handler reads these indices into gs_{band,guitar,sfx}.sld.
      constexpr int kDefaultVolumeIdx = 11;
      set_property(Symbol("band_volume_idx"), DataNode::Int(kDefaultVolumeIdx));
      set_property(Symbol("guitar_volume_idx"), DataNode::Int(kDefaultVolumeIdx));
      set_property(Symbol("fx_volume_idx"), DataNode::Int(kDefaultVolumeIdx));
    }
  }
  Symbol class_name() const override { return cls_; }

  DataNode handle_property(Symbol msg, const DataArray& args) override {
    const char* c = cls_.c_str();
    const char* m = msg.c_str();
    if (std::strcmp(c, "taskmgr") == 0 && std::strcmp(m, "ui_seconds") == 0)
      return DataNode::Float(mgr_->ui_seconds());
    if (std::strcmp(c, "taskmgr") == 0 && std::strcmp(m, "seconds") == 0)
      return DataNode::Float(mgr_->ui_seconds());
    if (std::strcmp(c, "taskmgr") == 0 && std::strcmp(m, "clear_tasks") == 0) {
      mgr_->clear_script_tasks();
      set_property(Symbol("clear_tasks_count"),
                   DataNode::Int(get_property(Symbol("clear_tasks_count"))
                                         .as_int()
                                         .value_or(0) +
                                 1));
      return DataNode();
    }
    if (std::strcmp(c, "beatmatch") == 0) {
      if (std::strcmp(m, "paused") == 0)
        return get_property(Symbol("paused"));
      if (std::strcmp(m, "set_paused") == 0) {
        set_property(Symbol("paused"), int_bool(arg_bool(args, 0)));
        return DataNode();
      }
      if (std::strcmp(m, "set_volume") == 0) {
        set_property(Symbol("volume"), args.size() ? args.at(0) : DataNode());
        return DataNode();
      }
      if (std::strcmp(m, "set_music_speed") == 0) {
        set_property(Symbol("music_speed"),
                     args.size() ? args.at(0) : DataNode());
        return DataNode();
      }
    }
    if (std::strcmp(m, "num_profiles") == 0) return DataNode::Int(0);
    if (std::strcmp(m, "tutorials_done") == 0) return DataNode::Int(1);
    if (std::strcmp(m, "is_missing_multi_controller") == 0) return DataNode::Sym(Symbol("TRUE"));
    if (std::strcmp(m, "is_multiple_controllers") == 0) return DataNode::Sym(Symbol("FALSE"));
    if (std::strcmp(c, "game") == 0 &&
        std::strcmp(m, "set_missing_controller") == 0) {
      set_property(Symbol("missing_controller"),
                   args.size() ? args.at(0) : DataNode::Int(0));
      return DataNode();
    }
    if (std::strcmp(c, "game") == 0 &&
        std::strcmp(m, "is_missing_controller") == 0) {
      return DataNode::Sym(node_bool(get_property(Symbol("missing_controller")))
                               ? Symbol("TRUE")
                               : Symbol("FALSE"));
    }
    if (std::strcmp(m, "get_controller") == 0) return DataNode::Sym(Symbol("guitar"));
    if (std::strcmp(m, "get_player_config") == 0) return DataNode::Obj(this);  // chainable stub
    if (std::strcmp(c, "helpbar") == 0) {
      if (std::strcmp(m, "set_display") == 0) {
        set_property(Symbol("display"), args.size() ? args.at(0) : DataNode());
        return DataNode();
      }
      if (std::strcmp(m, "display") == 0 ||
          std::strcmp(m, "get_display") == 0) {
        return get_property(Symbol("display"));
      }
    }
    if (std::strcmp(c, "options") == 0) {
      const auto get_bool = [&](const char* key, bool fallback = false) {
        DataNode stored = get_property(Symbol(key));
        return stored.empty() ? int_bool(fallback) : int_bool(node_bool(stored));
      };
      const auto set_bool = [&](const char* key, bool value) {
        set_property(Symbol(key), int_bool(value));
        return DataNode();
      };
      const auto get_int = [&](const char* key, int fallback = 0) {
        DataNode stored = get_property(Symbol(key));
        if (auto i = stored.as_int()) return DataNode::Int(*i);
        if (auto f = stored.as_float()) return DataNode::Int(static_cast<int>(*f));
        return DataNode::Int(fallback);
      };
      if (std::strcmp(m, "get_widescreen") == 0) return get_bool("widescreen");
      if (std::strcmp(m, "set_widescreen") == 0)
        return set_bool("widescreen", arg_bool(args, 0));
      if (std::strcmp(m, "get_pscan") == 0) return get_bool("pscan");
      if (std::strcmp(m, "set_pscan") == 0) return set_bool("pscan", arg_bool(args, 0));
      if (std::strcmp(m, "get_stereo") == 0) return get_bool("stereo", true);
      if (std::strcmp(m, "set_stereo") == 0) return set_bool("stereo", arg_bool(args, 0));
      if (std::strcmp(m, "get_band_volume_idx") == 0)
        return get_int("band_volume_idx", 11);
      if (std::strcmp(m, "get_guitar_volume_idx") == 0)
        return get_int("guitar_volume_idx", 11);
      if (std::strcmp(m, "get_fx_volume_idx") == 0)
        return get_int("fx_volume_idx", 11);
      if (std::strcmp(m, "get_sync_offset") == 0) {
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataNode persisted = campaign->handle_property(
              Symbol("get_sync_offset"), DataArray());
          if (persisted.as_int()) return persisted;
        }
        return get_int("sync_offset", 0);
      }
      if (std::strcmp(m, "set_sync_offset") == 0) {
        const int offset = std::clamp(arg_int(args, 0, 0), -500, 500);
        set_property(Symbol("sync_offset"),
                     DataNode::Int(offset));
        set_property(Symbol("video_input_offset_ms"), DataNode::Int(offset));
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataArray persist_args;
          persist_args.push(DataNode::Int(offset));
          campaign->handle_property(Symbol("set_sync_offset"), persist_args);
        }
        return DataNode();
      }
      if (std::strcmp(m, "get_audio_offset") == 0) {
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataNode persisted = campaign->handle_property(
              Symbol("get_audio_offset"), DataArray());
          if (persisted.as_int()) return persisted;
        }
        return get_int("audio_offset_ms", 0);
      }
      if (std::strcmp(m, "set_audio_offset") == 0) {
        const int offset = std::clamp(arg_int(args, 0, 0), -500, 500);
        set_property(Symbol("audio_offset_ms"), DataNode::Int(offset));
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataArray persist_args;
          persist_args.push(DataNode::Int(offset));
          campaign->handle_property(Symbol("set_audio_offset"), persist_args);
        }
        return DataNode();
      }
      if (std::strcmp(m, "get_video_input_offset") == 0) {
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataNode persisted = campaign->handle_property(
              Symbol("get_video_input_offset"), DataArray());
          if (persisted.as_int()) return persisted;
        }
        const int legacy = get_int("sync_offset", 0).as_int().value_or(0);
        return get_int("video_input_offset_ms", legacy);
      }
      if (std::strcmp(m, "set_video_input_offset") == 0) {
        const int offset = std::clamp(arg_int(args, 0, 0), -500, 500);
        set_property(Symbol("video_input_offset_ms"), DataNode::Int(offset));
        set_property(Symbol("sync_offset"), DataNode::Int(offset));
        if (Object* campaign = mgr_->resolve_object(Symbol("campaign"));
            campaign && campaign != this) {
          DataArray persist_args;
          persist_args.push(DataNode::Int(offset));
          campaign->handle_property(Symbol("set_video_input_offset"),
                                    persist_args);
        }
        return DataNode();
      }
      if (std::strcmp(m, "get_volume_from_idx") == 0) {
        constexpr int kMaxVolumeIdx = 11;
        const int idx = std::clamp(arg_int(args, 0, kMaxVolumeIdx), 0,
                                   kMaxVolumeIdx);
        return DataNode::Float(static_cast<float>(idx) /
                               static_cast<float>(kMaxVolumeIdx));
      }
      if (std::strcmp(m, "get_lefty") == 0) {
        const int player = arg_int(args, 0, 0);
        const std::string key = std::string("lefty") + std::to_string(player);
        return get_bool(key.c_str());
      }
      if (std::strcmp(m, "set_lefty") == 0) {
        // Harmonix scripts call: options set_lefty <player> <checked>.
        const int player = args.size() > 1 ? arg_int(args, 0, 0) : 0;
        const std::size_t value_index = args.size() > 1 ? 1u : 0u;
        const std::string key = std::string("lefty") + std::to_string(player);
        return set_bool(key.c_str(), arg_bool(args, value_index));
      }
    }
    if (std::strcmp(c, "meta") == 0) {
      if (std::strcmp(m, "play_movie") == 0) {
        set_property(Symbol("movie"), args.size() ? args.at(0) : DataNode());
        return DataNode();
      }
      if (std::strcmp(m, "is_up") == 0) {
        return DataNode::Sym(Symbol("TRUE"));
      }
      if (std::strcmp(m, "is_any_button") == 0) {
        return int_bool(args.size() > 0);
      }
    }
    if (std::strcmp(c, "synth") == 0) {
      if (std::strcmp(m, "play_sequence") == 0) {
        const Symbol sequence = arg_symbol(args, 0);
        if (sequence.valid())
          set_property(Symbol("last_sequence"), DataNode::Sym(sequence));
        set_property(Symbol("sequence_count"),
                     DataNode::Int(get_property(Symbol("sequence_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        if (sequence.valid())
          mgr_->emit_audio_event(Symbol("play_sequence"), sequence);
        return DataNode();
      }
      if (std::strcmp(m, "stop_all_sfx") == 0) {
        set_property(Symbol("last_control"), DataNode::Sym(msg));
        set_property(Symbol("stop_all_count"),
                     DataNode::Int(get_property(Symbol("stop_all_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        mgr_->emit_audio_event(Symbol("stop_all_sfx"));
        return DataNode();
      }
      if (std::strcmp(m, "pause_all_sfx") == 0) {
        set_property(Symbol("last_control"), DataNode::Sym(msg));
        set_property(Symbol("paused"), int_bool(arg_bool(args, 0)));
        set_property(Symbol("pause_all_count"),
                     DataNode::Int(get_property(Symbol("pause_all_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        mgr_->emit_audio_event(Symbol("pause_all_sfx"), Symbol(),
                               arg_bool(args, 0));
        return DataNode();
      }
    }
    if (std::strcmp(c, "world") == 0) {
      if (std::strcmp(m, "play_sfx") == 0) {
        const Symbol cue = arg_symbol(args, 0);
        if (cue.valid())
          set_property(Symbol("last_played"), DataNode::Sym(cue));
        set_property(Symbol("play_count"),
                     DataNode::Int(get_property(Symbol("play_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        if (cue.valid()) mgr_->emit_audio_event(Symbol("play_sfx"), cue);
        return DataNode();
      }
      if (std::strcmp(m, "play_meta_sfx") == 0) {
        const Symbol cue = arg_symbol(args, 0);
        if (cue.valid())
          set_property(Symbol("last_meta_played"), DataNode::Sym(cue));
        set_property(Symbol("meta_play_count"),
                     DataNode::Int(get_property(Symbol("meta_play_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        if (cue.valid()) mgr_->emit_audio_event(Symbol("play_sfx"), cue);
        return DataNode();
      }
    }
    if (std::strcmp(c, "sync_click.cue") == 0) {
      if (std::strcmp(m, "play") == 0) {
        set_property(Symbol("last_control"), DataNode::Sym(msg));
        set_property(Symbol("play_count"),
                     DataNode::Int(get_property(Symbol("play_count"))
                                           .as_int()
                                           .value_or(0) +
                                   1));
        mgr_->emit_audio_event(Symbol("play_sfx"), Symbol("sync_click.cue"));
        return DataNode();
      }
    }
    if (std::strcmp(c, "practice_hat") == 0 &&
        std::strcmp(m, "play") == 0) {
      set_property(Symbol("last_control"), DataNode::Sym(msg));
      set_property(Symbol("play_count"),
                   DataNode::Int(get_property(Symbol("play_count"))
                                         .as_int()
                                         .value_or(0) +
                                 1));
      mgr_->emit_audio_event(Symbol("play_sfx"), Symbol("practice_hat"));
      return DataNode();
    }
    if (std::strcmp(c, "play_sfx") == 0) {
      if (msg == Symbol("get") || msg == Symbol("set") || msg == Symbol("has"))
        return Object::handle_property(msg, args);
      set_property(Symbol("last_played"), DataNode::Sym(msg));
      set_property(Symbol("play_count"),
                   DataNode::Int(get_property(Symbol("play_count"))
                                         .as_int()
                                         .value_or(0) +
                                 1));
      mgr_->emit_audio_event(Symbol("play_sfx"), msg);
      return DataNode();
    }
    if (std::strcmp(c, "stop_sfx") == 0) {
      if (msg == Symbol("get") || msg == Symbol("set") || msg == Symbol("has"))
        return Object::handle_property(msg, args);
      set_property(Symbol("last_stopped"), DataNode::Sym(msg));
      set_property(Symbol("stop_count"),
                   DataNode::Int(get_property(Symbol("stop_count"))
                                         .as_int()
                                         .value_or(0) +
                                 1));
      mgr_->emit_audio_event(Symbol("stop_sfx"), msg);
      return DataNode();
    }
    if (std::strcmp(c, "meta_music") == 0) {
      if (std::strcmp(m, "start") == 0 || std::strcmp(m, "stop") == 0) {
        set_property(Symbol("last_control"), DataNode::Sym(msg));
        set_property(Symbol("active"),
                     DataNode::Int(std::strcmp(m, "start") == 0 ? 1 : 0));
        mgr_->emit_audio_event(Symbol("meta_music"), msg,
                               std::strcmp(m, "start") == 0);
        return DataNode();
      }
    }
    if (std::strcmp(c, "memcard") == 0) {
      if (std::strcmp(m, "get_info") == 0)
        return DataNode::Sym(Symbol("kMCNoError"));
      if (std::strcmp(m, "space_available") == 0)
        return DataNode::Int(8 * 1024 * 1024);
      if (std::strcmp(m, "space_needed") == 0)
        return DataNode::Int(0);
      if (std::strcmp(m, "localize") == 0) {
        Symbol token = arg_symbol(args, 0);
        return token.valid() ? DataNode::Str(mgr_->localize(token)) : DataNode::Str("");
      }
      if (std::strcmp(m, "load_data") == 0 ||
          std::strcmp(m, "save_data") == 0 ||
          std::strcmp(m, "format") == 0) {
        Symbol target = arg_symbol(args, 0);
        if (!target.valid() && mgr_->current_screen())
          target = mgr_->current_screen()->name();
        mgr_->set_global(Symbol("result"),
                         DataNode::Sym(Symbol("kMCNoError")));
        mgr_->set_global(Symbol("space_free"),
                         DataNode::Int(8 * 1024 * 1024));
        if (Object* screen = target.valid() ? mgr_->find_object(target) : nullptr)
          screen->handle_property(Symbol("MEMCARD_RESULT_MSG"), DataArray());
        return DataNode::Sym(Symbol("kMCNoError"));
      }
    }
    // Universal get/set/has still work (menus stash state on gamecfg etc.).
    Symbol gs("get"), ss("set"), hs("has");
    if (msg == gs || msg == ss || msg == hs) return Object::handle_property(msg, args);
    mgr_->on_unhandled(std::string(c) + "::" + m);
    return DataNode();
  }

 private:
  Symbol cls_;
  ScreenManager* mgr_;
};

}  // namespace

void install_default_singletons(ScreenManager& mgr) {
  static const char* kNames[] = {"taskmgr",      "game",    "gamecfg",   "campaign",
                                 "synth",        "profilemgr", "meta",   "song_provider",
                                 "content_mgr",  "helpbar", "options",   "leaderboards",
                                 "memcard",      "play_sfx", "stop_sfx", "meta_music",
                                 "world",        "sync_click.cue", "practice_hat",
                                 "beatmatch"};
  for (const char* n : kNames)
    mgr.add_singleton(Symbol(n), std::make_unique<StubObject>(Symbol(n), &mgr));
}

}  // namespace ghogx::ui
