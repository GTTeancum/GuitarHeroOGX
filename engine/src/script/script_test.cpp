// engine/src/script/script_test.cpp
//
// Headless tests for the DataArray script interpreter. The trees are built to
// mirror the EXACT shapes found in stock GH2 ui/gen/main.dtb + init.dtb (the
// SELECT_START_MSG switch, the {if_else {> {campaign num_profiles} 0} ...}
// career branch, reset_player_settings' {do ($p {game get_player_config 1})...},
// init.dtb's {foreach $p (...) {$p load}}), so passing means the interpreter
// runs the real menu scripts. Plus a preprocessor #ifdef/#define check.

#include "core/object.h"
#include "core/object_dir.h"
#include "script/interp.h"
#include "script/preprocess.h"

#include "dtb.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ghogx;
using namespace ghogx::script;
using Node = gh::dtb::Node;
using NodePtr = std::shared_ptr<Node>;

static int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, \
                   #cond);                                                \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

// --- node builders (match dtb tags) ----------------------------------------
static NodePtr mkint(int v) { auto n = std::make_shared<Node>(); n->tag = 0x00; n->value = (int32_t)v; return n; }
static NodePtr mkflt(float v){ auto n = std::make_shared<Node>(); n->tag = 0x01; n->value = v; return n; }
static NodePtr mkvar(const char* s){ auto n = std::make_shared<Node>(); n->tag = 0x02; n->value = std::string(s); return n; }
static NodePtr mksym(const char* s){ auto n = std::make_shared<Node>(); n->tag = 0x05; n->value = std::string(s); return n; }
static NodePtr mkstr(const char* s){ auto n = std::make_shared<Node>(); n->tag = 0x12; n->value = std::string(s); return n; }
static NodePtr mkarr(std::vector<NodePtr> k){ auto n = std::make_shared<Node>(); n->tag = 0x10; n->value = std::move(k); return n; }
static NodePtr mkcmd(std::vector<NodePtr> k){ auto n = std::make_shared<Node>(); n->tag = 0x11; n->value = std::move(k); return n; }
static NodePtr mkprop(const char* name){ auto n = std::make_shared<Node>(); n->tag = 0x13; n->value = std::vector<NodePtr>{mksym(name)}; return n; }
static NodePtr mkdir(uint32_t tag, const char* payload){ auto n = std::make_shared<Node>(); n->tag = tag; n->value = std::string(payload ? payload : ""); return n; }

static std::string to_str(const DataNode& v) {
  switch (v.type()) {
    case DataType::kInt: return std::to_string(v.as_int().value_or(0));
    case DataType::kFloat: { char b[32]; std::snprintf(b, sizeof b, "%g", v.as_float().value_or(0)); return b; }
    case DataType::kSymbol:
    case DataType::kString: return std::string(v.as_string().value_or(""));
    case DataType::kObject: return "<obj>";
    default: return "<>";
  }
}

// --- mocks -----------------------------------------------------------------
class MockObject : public Object {
 public:
  explicit MockObject(const char* cls) : cls_(cls) {}
  Symbol class_name() const override { return cls_; }
  void ret(const char* msg, DataNode v) { returns_[Symbol(msg).id()] = std::move(v); }

  std::vector<std::string> calls;  // "msg:arg0,arg1"
  DataNode handle_property(Symbol msg, const DataArray& args) override {
    std::string rec = msg.c_str();
    rec += ':';
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i) rec += ',';
      rec += to_str(args.at(i));
    }
    calls.push_back(rec);
    auto it = returns_.find(msg.id());
    if (it != returns_.end()) return it->second;
    return Object::handle_property(msg, args);  // universal get/set/has/...
  }
  bool called(const std::string& rec) const {
    for (auto& c : calls) if (c == rec) return true;
    return false;
  }

 private:
  Symbol cls_;
  std::map<const void*, DataNode> returns_;
};

class MockHost : public Host {
 public:
  std::map<const void*, Object*> objs;
  std::map<const void*, DataNode> globals;
  std::map<const void*, NodeList> funcs;
  std::map<const void*, std::string> options;
  std::vector<std::string> unhandled;
  std::vector<std::pair<float, std::size_t>> scheduled;
  std::vector<std::string> commands;
  std::vector<std::unique_ptr<MockObject>> created;

  void bind(const char* name, Object* o) { objs[Symbol(name).id()] = o; }
  void bind_func(const char* name, NodeList body) { funcs[Symbol(name).id()] = std::move(body); }
  Object* resolve_object(Symbol n) override { auto it = objs.find(n.id()); return it == objs.end() ? nullptr : it->second; }
  DataNode get_global(Symbol n) override { auto it = globals.find(n.id()); return it == globals.end() ? DataNode() : it->second; }
  void set_global(Symbol n, DataNode v) override { globals[n.id()] = std::move(v); }
  void on_unhandled(const std::string& w) override { unhandled.push_back(w); }
  const NodeList* resolve_function(Symbol n) override {
    auto it = funcs.find(n.id());
    return it == funcs.end() ? nullptr : &it->second;
  }
  bool handle_command(Symbol n, const DataArray& args, DataNode& out) override {
    if (n != Symbol("game_restart_fast")) return false;
    std::string rec = n.c_str();
    rec += ':';
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i) rec += ',';
      rec += to_str(args.at(i));
    }
    commands.push_back(rec);
    out = DataNode::Sym(n);
    return true;
  }
  Object* create_object(Symbol cls, Symbol name) override {
    auto object = std::make_unique<MockObject>(cls.c_str());
    object->set_name(name);
    Object* raw = object.get();
    created.push_back(std::move(object));
    objs[name.id()] = raw;
    return raw;
  }
  std::optional<std::string> consume_option_str(Symbol n) override {
    auto it = options.find(n.id());
    if (it == options.end()) return std::nullopt;
    std::string value = it->second;
    options.erase(it);
    return value;
  }
  void schedule_script_task(const NodeList& body, Object* self,
                            float delay_seconds) override {
    (void)self;
    scheduled.push_back({delay_seconds, body.size()});
  }
};

// --- tests -----------------------------------------------------------------
static void test_operators() {
  Interp ip; MockHost host; Scope root; Env env; env.host = &host; env.scope = &root;
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(2), mkint(2)}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(2), mkint(3)}), env).as_int().value() == 0);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(0), mksym("kNormal")}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(1), mksym("kFocused")}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(2), mksym("kDisabled")}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mksym("kSelecting"), mkint(3)}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mksym("kSelected"), mkint(4)}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkint(2), mksym("kFocused")}), env).as_int().value() == 0);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkflt(2.5f), mksym("kDisabled")}), env).as_int().value() == 0);
  CHECK(ip.eval(*mkcmd({mksym(">"), mkint(5), mkint(0)}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("!"), mkcmd({mksym("=="), mkint(1), mkint(2)})}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("&&"), mkint(1), mkint(1)}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("&&"), mkint(1), mkint(0)}), env).as_int().value() == 0);
  CHECK(ip.eval(*mkcmd({mksym("+"), mkint(2), mkint(3)}), env).as_int().value() == 5);
  CHECK(ip.eval(*mkcmd({mksym("+"), mkflt(1.5f), mkint(2)}), env).as_float().value() == 3.5f);
  // truthiness of the FALSE symbol (initial value of e.g. [already_entered])
  CHECK(ip.eval(*mkcmd({mksym("!"), mksym("FALSE")}), env).as_int().value() == 1);
  CHECK(ip.eval(*mkcmd({mksym("!"), mksym("TRUE")}), env).as_int().value() == 0);
  // if_else returns the taken branch value
  CHECK(ip.eval(*mkcmd({mksym("if_else"), mkint(1), mkint(10), mkint(20)}), env).as_int().value() == 10);
  CHECK(ip.eval(*mkcmd({mksym("if_else"), mkint(0), mkint(10), mkint(20)}), env).as_int().value() == 20);
}

static void test_vars_and_props() {
  Interp ip; MockHost host; Scope root; MockObject self("GHPanel");
  Env env; env.host = &host; env.scope = &root; env.self = &self;
  // global var round-trip: {set $x 7}; {== $x 7}
  ip.eval(*mkcmd({mksym("set"), mkvar("x"), mkint(7)}), env);
  CHECK(ip.eval(*mkcmd({mksym("=="), mkvar("x"), mkint(7)}), env).as_int().value() == 1);
  // self property via {set [foo] 42} then [foo] and {$this get foo}
  ip.eval(*mkcmd({mksym("set"), mkprop("foo"), mkint(42)}), env);
  CHECK(ip.eval(*mkprop("foo"), env).as_int().value() == 42);
  CHECK(ip.eval(*mkcmd({mkvar("this"), mksym("get"), mksym("foo")}), env).as_int().value() == 42);
  // {$this set already_entered TRUE} then {! [already_entered]} == 0
  ip.eval(*mkcmd({mkvar("this"), mksym("set"), mksym("already_entered"), mksym("TRUE")}), env);
  CHECK(ip.eval(*mkcmd({mksym("!"), mkprop("already_entered")}), env).as_int().value() == 0);
}

static void test_handler_prefers_local_object_dir_child() {
  Interp ip;
  MockHost host;
  Scope root;
  ObjectDir panel;
  panel.set_name(Symbol("endgame_stats_panel"));
  auto local_owned = std::make_unique<MockObject>("BandLabel");
  MockObject* local = local_owned.get();
  panel.add(Symbol("song_name.lbl"), std::move(local_owned));
  MockObject global("BandLabel");
  global.set_name(Symbol("song_name.lbl"));
  host.bind("song_name.lbl", &global);
  Env env;
  env.host = &host;
  env.scope = &root;
  env.self = &panel;

  ip.eval(*mkcmd({mksym("song_name.lbl"), mksym("set"), mksym("text"),
                  mkstr("LOCAL SONG")} ), env);
  CHECK(local->get_property(Symbol("text")).as_string().value_or("") ==
        "LOCAL SONG");
  CHECK(global.get_property(Symbol("text")).empty());
}

static void test_runtime_new_and_message_output_refs() {
  Interp ip;
  MockHost host;
  Scope root;
  Env env;
  env.host = &host;
  env.scope = &root;

  DataNode created = ip.eval(
      *mkcmd({mksym("new"), mksym("StatsProvider"),
              mksym("stats_provider")}),
      env);
  CHECK(created.as_object() != nullptr);
  CHECK(host.resolve_object(Symbol("stats_provider")) == created.as_object());
  CHECK(created.as_object() &&
        created.as_object()->class_name() == Symbol("StatsProvider"));

  MockObject highscores("Highscores");
  auto values = std::make_shared<DataArray>();
  values->push(DataNode::Int(2));
  values->push(DataNode::Str("ACE"));
  values->push(DataNode::Int(123456));
  highscores.ret("get_highscore", DataNode::Array(values));
  host.bind("highscores", &highscores);
  ip.eval(*mkcmd({mksym("highscores"), mksym("get_highscore"),
                  mkvar("slot"), mkvar("name"), mkvar("score")}),
          env);
  CHECK(host.get_global(Symbol("slot")).as_int().value_or(-1) == 2);
  CHECK(host.get_global(Symbol("name")).as_string().value_or("") == "ACE");
  CHECK(host.get_global(Symbol("score")).as_int().value_or(-1) == 123456);
}

// Exact shape of stock main.dtb (SELECT_START_MSG ...): pick quickplay.
static void test_main_select_switch() {
  Interp ip; MockHost host; Scope root;
  MockObject ui("UIManager"), gamecfg("GameCfg");
  host.bind("ui", &ui); host.bind("gamecfg", &gamecfg);
  host.set_global(Symbol("component"), DataNode::Sym(Symbol("main_quickspin.btn")));
  Env env; env.host = &host; env.scope = &root;

  NodePtr sw = mkcmd({
    mksym("switch"), mkvar("component"),
    mkarr({mksym("main_career.btn"),
           mkcmd({mksym("gamecfg"), mksym("set"), mksym("mode"), mksym("career")}),
           mkcmd({mksym("ui"), mksym("goto_screen"), mksym("chooseprof_screen")})}),
    mkarr({mksym("main_quickspin.btn"),
           mkcmd({mksym("gamecfg"), mksym("set"), mksym("mode"), mksym("quickplay")}),
           mkcmd({mksym("ui"), mksym("goto_screen"), mksym("qp_selsong_screen")})}),
    mkarr({mksym("main_options.btn"),
           mkcmd({mksym("ui"), mksym("goto_screen"), mksym("options_screen")})}),
  });
  ip.eval(*sw, env);
  CHECK(gamecfg.called("set:mode,quickplay"));
  CHECK(ui.called("goto_screen:qp_selsong_screen"));
  CHECK(!ui.called("goto_screen:chooseprof_screen"));
  CHECK(!ui.called("goto_screen:options_screen"));
}

// Exact shape of main_career.btn: {if_else {> {campaign num_profiles} 0} A B}.
static void test_career_branch() {
  // num_profiles == 0 -> else branch (nameprof flow)
  {
    Interp ip; MockHost host; Scope root;
    MockObject ui("UIManager"), campaign("Campaign"), nameprof("GHScreen");
    campaign.ret("num_profiles", DataNode::Int(0));
    host.bind("ui", &ui); host.bind("campaign", &campaign); host.bind("nameprof_screen", &nameprof);
    Env env; env.host = &host; env.scope = &root;
    NodePtr e = mkcmd({mksym("if_else"),
        mkcmd({mksym(">"), mkcmd({mksym("campaign"), mksym("num_profiles")}), mkint(0)}),
        mkcmd({mksym("ui"), mksym("goto_screen"), mksym("chooseprof_screen")}),
        mkcmd({mksym("do"),
               mkcmd({mksym("nameprof_screen"), mksym("set"), mksym("back_screen"), mksym("main_screen")}),
               mkcmd({mksym("ui"), mksym("goto_screen"), mksym("nameprof_screen")})})});
    ip.eval(*e, env);
    CHECK(ui.called("goto_screen:nameprof_screen"));
    CHECK(!ui.called("goto_screen:chooseprof_screen"));
    CHECK(nameprof.called("set:back_screen,main_screen"));
  }
  // num_profiles == 2 -> then branch (chooseprof)
  {
    Interp ip; MockHost host; Scope root;
    MockObject ui("UIManager"), campaign("Campaign");
    campaign.ret("num_profiles", DataNode::Int(2));
    host.bind("ui", &ui); host.bind("campaign", &campaign);
    Env env; env.host = &host; env.scope = &root;
    NodePtr e = mkcmd({mksym("if_else"),
        mkcmd({mksym(">"), mkcmd({mksym("campaign"), mksym("num_profiles")}), mkint(0)}),
        mkcmd({mksym("ui"), mksym("goto_screen"), mksym("chooseprof_screen")}),
        mkint(0)});
    ip.eval(*e, env);
    CHECK(ui.called("goto_screen:chooseprof_screen"));
  }
}

// reset_player_settings shape: {do ($p2 {game get_player_config 1}) {$p2 set_difficulty ...}}
static void test_do_local_object() {
  Interp ip; MockHost host; Scope root;
  MockObject game("Game"), p2("PlayerCfg");
  game.ret("get_player_config", DataNode::Obj(&p2));
  host.bind("game", &game);
  Env env; env.host = &host; env.scope = &root;
  NodePtr e = mkcmd({mksym("do"),
      mkarr({mkvar("p2"), mkcmd({mksym("game"), mksym("get_player_config"), mkint(1)})}),
      mkcmd({mkvar("p2"), mksym("set_character"), mksym("rockabill1"), mksym("TRUE")}),
      mkcmd({mkvar("p2"), mksym("set_difficulty"), mksym("kDifficultyMedium")})});
  ip.eval(*e, env);
  CHECK(p2.called("set_character:rockabill1,TRUE"));
  CHECK(p2.called("set_difficulty:kDifficultyMedium"));
}

// init.dtb shape: {foreach $p (a.panel b.panel) {$p load}}
static void test_foreach_load() {
  Interp ip; MockHost host; Scope root;
  MockObject a("GHPanel"), b("GHPanel");
  host.bind("a.panel", &a); host.bind("b.panel", &b);
  Env env; env.host = &host; env.scope = &root;
  NodePtr e = mkcmd({mksym("foreach"), mkvar("p"), mkarr({mksym("a.panel"), mksym("b.panel")}),
                     mkcmd({mkvar("p"), mksym("load")})});
  ip.eval(*e, env);
  CHECK(a.called("load:"));
  CHECK(b.called("load:"));
}

// career.dta shape: {campaign foreach_venue $venue {...$venue...}}.
// Object foreach callbacks must bind the variable before evaluating the body.
static void test_object_foreach_callback() {
  Interp ip; MockHost host; Scope root;
  MockObject campaign("Campaign"), screen("GHScreen");
  auto venues = std::make_shared<DataArray>();
  venues->push(DataNode::Sym(Symbol("battle")));
  venues->push(DataNode::Sym(Symbol("small2")));
  campaign.ret("foreach_venue_values", DataNode::Array(venues));
  host.bind("campaign", &campaign);
  host.bind("screen", &screen);
  Env env; env.host = &host; env.scope = &root;

  NodePtr e = mkcmd({
      mksym("campaign"), mksym("foreach_venue"), mkvar("venue"),
      mkcmd({mksym("screen"), mksym("set_venue_seen"), mkvar("venue")})});
  ip.eval(*e, env);
  CHECK(campaign.called("foreach_venue_values:"));
  CHECK(screen.called("set_venue_seen:battle"));
  CHECK(screen.called("set_venue_seen:small2"));
  CHECK(!campaign.called("foreach_venue:"));
}

// manage_bands.dta shape:
// {foreach_int $idx 0 MAX_NUM_PROFILES ...} and {$btn set_text new_band}, where
// $btn is a sprintf-created object name string.
static void test_foreach_int_and_string_target() {
  Interp ip; MockHost host; Scope root;
  MockObject band0("BandButton"), band1("BandButton"), band2("BandButton");
  host.bind("cp_band0.btn", &band0);
  host.bind("cp_band1.btn", &band1);
  host.bind("cp_band2.btn", &band2);
  Env env; env.host = &host; env.scope = &root;

  NodePtr e = mkcmd({
      mksym("foreach_int"), mkvar("idx"), mkint(0), mkint(3),
      mkcmd({
          mksym("do"),
          mkarr({mkvar("btn"),
                 mkcmd({mksym("sprintf"), mkstr("cp_band%d.btn"), mkvar("idx")})}),
          mkcmd({mkvar("btn"), mksym("set_text"), mksym("new_band")})})});
  ip.eval(*e, env);
  CHECK(band0.called("set_text:new_band"));
  CHECK(band1.called("set_text:new_band"));
  CHECK(band2.called("set_text:new_band"));
}

// run_handler with a (params) list, display_cheat_msg shape.
static void test_run_handler_params() {
  Interp ip; MockHost host; MockObject self("GHPanel"), lbl("UILabel");
  host.bind("mm.lbl", &lbl);
  Scope root; Env env; env.host = &host; env.scope = &root;
  // (display_cheat_msg ($cheat $enable) {mm.lbl set_text $cheat})
  NodeList handler = {mksym("display_cheat_msg"), mkarr({mkvar("cheat"), mkvar("enable")}),
                      mkcmd({mksym("mm.lbl"), mksym("set_text"), mkvar("cheat")})};
  DataArray args; args.push(DataNode::Sym(Symbol("my_cheat"))); args.push(DataNode::Int(1));
  ip.run_handler(handler, &self, args, env);
  CHECK(lbl.called("set_text:my_cheat"));
}

// endgame.dtb dispatches the selected headline generator as a command in the
// message slot: {$this {cond (...) gen_headline_perfect_coop ...}}.  It also
// passes evaluated keyed arrays such as (adj {localize ...}) to the native
// EndGamePanel generator.
static void test_dynamic_message_and_evaluated_array_args() {
  Interp ip;
  MockHost host;
  MockObject self("EndGamePanel");
  Scope root;
  Env env;
  env.host = &host;
  env.scope = &root;
  env.self = &self;

  ip.eval(*mkcmd({
              mkvar("this"),
              mkcmd({mksym("if_else"), mksym("TRUE"),
                     mksym("gen_headline_perfect_coop"),
                     mksym("gen_headline_qc_1")})}),
          env);
  CHECK(self.called("gen_headline_perfect_coop:"));

  root.declare(Symbol("word"), DataNode::Str("Mind-blowing"));
  ip.eval(*mkcmd({mkvar("this"), mksym("generate_headline"),
                  mksym("headline_quick_coop1"),
                  mkarr({mksym("adj"), mkvar("word")}),
                  mkarr({mksym("noun"),
                         mkcmd({mksym("sprint"), mkstr("concert")})})}),
          env);
  CHECK(self.called("generate_headline:headline_quick_coop1,<>,<>"));
  CHECK(self.calls.size() >= 2);
  if (self.calls.size() >= 2) {
    // MockObject's compact call string cannot flatten arrays, so inspect the
    // evaluated value directly as a separate script-time array contract.
    DataNode keyed = ip.eval(
        *mkarr({mksym("adj"), mkvar("word"),
                mkcmd({mksym("sprint"), mkstr("concert")})}),
        env);
    auto values = keyed.as_array();
    CHECK(values && values->size() == 3);
    if (values && values->size() == 3) {
      CHECK(values->at(0).as_symbol().value_or(Symbol()) == Symbol("adj"));
      CHECK(values->at(1).as_string().value_or("") == "Mind-blowing");
      CHECK(values->at(2).as_string().value_or("") == "concert");
    }
  }
}

static void test_top_level_function_call() {
  Interp ip; MockHost host; Scope root; MockObject ui("UIManager");
  host.bind("ui", &ui);
  host.bind_func("route_to", {
      mksym("func"), mksym("route_to"), mkarr({mkvar("screen")}),
      mkcmd({mksym("ui"), mksym("goto_screen"), mkvar("screen")})});
  Env env; env.host = &host; env.scope = &root;

  ip.eval(*mkcmd({mksym("route_to"), mksym("complete_screen")}), env);
  CHECK(ui.called("goto_screen:complete_screen"));
}

// sprintf + localize (display_cheat_msg uses {sprintf {localize ...} {localize $cheat}}).
static void test_sprintf() {
  Interp ip; MockHost host; Scope root; Env env; env.host = &host; env.scope = &root;
  DataNode r = ip.eval(*mkcmd({mksym("sprintf"), mkstr("%s=%d"), mksym("foo"), mkint(5)}), env);
  CHECK(to_str(r) == "foo=5");
  CHECK(to_str(ip.eval(*mkcmd({mksym("sprintf"), mkstr("guitar%02d.env"), mkint(1)}), env)) ==
        "guitar01.env");
  CHECK(to_str(ip.eval(*mkcmd({mksym("sprintf"), mkstr("CASH: $%/D"), mkint(12345)}), env)) ==
        "CASH: $12,345");
  CHECK(to_str(ip.eval(*mkcmd({mksym("sprintf"), mkstr("%d%% (%d/%d)"),
                               mkint(100), mkint(8), mkint(8)}), env)) ==
        "100% (8/8)");
}

static void test_stock_collection_helpers() {
  Interp ip; MockHost host; Scope root; MockObject ui("UIManager");
  host.bind("ui", &ui);
  Env env; env.host = &host; env.scope = &root;

  CHECK(ip.eval(*mkcmd({mksym("mod"), mkint(-1), mkint(5)}), env)
            .as_int()
            .value_or(-1) == 4);
  CHECK(ip.eval(*mkcmd({mksym("min"), mkint(21), mkint(20)}), env)
            .as_int()
            .value_or(-1) == 20);
  CHECK(ip.eval(*mkcmd({mksym("max"), mkint(-1), mkint(0)}), env)
            .as_int()
            .value_or(-1) == 0);
  CHECK(ip.eval(*mkcmd({mksym("int"), mkflt(12.75f)}), env)
            .as_int()
            .value_or(-1) == 12);
  DataNode empty_array = ip.eval(*mkcmd({mksym("array"), mkint(0)}), env);
  CHECK(empty_array.as_array() && empty_array.as_array()->empty());
  DataNode sized_array = ip.eval(*mkcmd({mksym("array"), mkint(2)}), env);
  CHECK(sized_array.as_array() && sized_array.as_array()->size() == 2);
  CHECK(to_str(ip.eval(*mkcmd({mksym("sprint"), mksym("help_"), mkstr("back")}), env)) ==
        "help_back");

  NodePtr helpbar = mkcmd({
      mksym("do"),
      mkarr({mkvar("array")}),
      mkcmd({mksym("set"), mkvar("array"), mkarr({mksym("stale")})}),
      mkcmd({mksym("resize"), mkvar("array"), mkint(0)}),
      mkcmd({mksym("push_back"), mkvar("array"),
             mkarr({mksym("fret1"), mksym("help_select")})}),
      mkcmd({mksym("push_back"), mkvar("array"),
             mkarr({mksym("fret2"), mksym("help_back")})}),
      mkvar("array")});
  DataNode help = ip.eval(*helpbar, env);
  auto arr = help.as_array();
  CHECK(arr && arr->size() == 2);
  if (arr && arr->size() == 2) {
    auto row0 = arr->at(0).as_array();
    auto row1 = arr->at(1).as_array();
    CHECK(row0 && row0->size() == 2 &&
          row0->at(0).as_symbol().value_or(Symbol()) == Symbol("fret1") &&
          row0->at(1).as_symbol().value_or(Symbol()) == Symbol("help_select"));
    CHECK(row1 && row1->size() == 2 &&
          row1->at(0).as_symbol().value_or(Symbol()) == Symbol("fret2") &&
          row1->at(1).as_symbol().value_or(Symbol()) == Symbol("help_back"));
  }

  NodePtr conditional = mkcmd({
      mksym("cond"),
      mkarr({mkcmd({mksym("=="), mkint(0), mkint(1)}),
             mkcmd({mksym("ui"), mksym("goto_screen"), mksym("wrong_screen")})}),
      mkarr({mkcmd({mksym("=="), mkint(2), mkint(2)}),
             mkcmd({mksym("ui"), mksym("goto_screen"), mksym("right_screen")})}),
      mkarr({mksym("TRUE"),
             mkcmd({mksym("ui"), mksym("goto_screen"), mksym("fallback_screen")})})});
  ip.eval(*conditional, env);
  CHECK(ui.called("goto_screen:right_screen"));
  CHECK(!ui.called("goto_screen:wrong_screen"));
  CHECK(!ui.called("goto_screen:fallback_screen"));

  ip.eval(*mkcmd({mksym("autosave_goto"), mksym("nameprof_screen")}), env);
  CHECK(ui.called("goto_screen:nameprof_screen"));

  MockObject ten("BandTextEntry");
  ten.ret("user_can_scroll", DataNode::Sym(Symbol("TRUE")));
  ten.ret("no_text_entered", DataNode::Int(0));
  host.bind("profile.ten", &ten);
  DataNode text_help = ip.eval(*mkcmd({mksym("get_text_entry_help_text"),
                                       mkarr({}), mksym("profile.ten"),
                                       mksym("TRUE")}), env);
  auto text_help_rows = text_help.as_array();
  CHECK(text_help_rows && text_help_rows->size() == 3);
  if (text_help_rows && text_help_rows->size() == 3) {
    auto row0 = text_help_rows->at(0).as_array();
    auto row1 = text_help_rows->at(1).as_array();
    auto row2 = text_help_rows->at(2).as_array();
    CHECK(row0 && row0->at(1).as_symbol().value_or(Symbol()) ==
                      Symbol("help_nextletter"));
    CHECK(row1 && row1->at(1).as_symbol().value_or(Symbol()) ==
                      Symbol("help_deleteletter"));
    CHECK(row2 && row2->at(1).as_symbol().value_or(Symbol()) ==
                      Symbol("help_updown"));
  }

  NodePtr award_helpers = mkcmd({
      mksym("do"),
      mkarr({mkvar("awards"),
             mkarr({mkarr({mksym("ca_blurb1"), mkint(10)}),
                    mkarr({mksym("ca_blurb2"), mkint(20)})})}),
      mkarr({mkvar("total"), mkint(5)}),
      mkarr({mkvar("slot"), mkint(0)}),
      mkarr({mkvar("data"), mkcmd({mksym("random_elem"), mkvar("awards")})}),
      mkcmd({mksym("remove_elem"), mkvar("awards"), mkvar("data")}),
      mkcmd({mksym("+="), mkvar("total"),
             mkcmd({mksym("elem"), mkvar("data"), mkint(1)})}),
      mkcmd({mksym("++"), mkvar("slot")}),
      mkcmd({mksym("sprintf"), mkstr("%d:%d:%d"), mkvar("total"), mkvar("slot"),
             mkcmd({mksym("find_elem"), mkvar("awards"), mkvar("data")})})});
  CHECK(to_str(ip.eval(*award_helpers, env)) == "15:1:-1");
}

static void test_script_task_boot_shape() {
  Interp ip; MockHost host; Scope root; MockObject self("GHScreen");
  Env env; env.host = &host; env.scope = &root; env.self = &self;
  NodePtr task = mkcmd({
      mksym("script_task"),
      mkarr({mksym("delay"), mkint(1)}),
      mkarr({mksym("units"), mksym("kTaskUISeconds")}),
      mkarr({mksym("script"),
             mkcmd({mksym("dialog"), mksym("set_message"), mksym("load_loading")}),
             mkcmd({mksym("memcard"), mksym("load_data"), mksym("bootup_load")})})});
  ip.eval(*task, env);
  CHECK(host.scheduled.size() == 1);
  if (!host.scheduled.empty()) {
    CHECK(host.scheduled[0].first == 1.0f);
    CHECK(host.scheduled[0].second == 2);
  }
}

static void test_thread_task_sleep_segments() {
  Interp ip; MockHost host; Scope root; MockObject self("LagPanel");
  Env env; env.host = &host; env.scope = &root; env.self = &self;
  NodePtr task = mkcmd({
      mksym("thread_task"),
      mkarr({mksym("units"), mksym("kTaskUISeconds")}),
      mkarr({mksym("script"),
             mkcmd({mksym("countdown.lbl"), mksym("set"), mksym("text_token"),
                    mksym("lag_3")}),
             mkcmd({mkvar("task"), mksym("sleep"), mkflt(0.133f)}),
             mkcmd({mksym("practice_hat"), mksym("play")}),
             mkcmd({mkvar("task"), mksym("sleep"), mkflt(0.6f)}),
             mkcmd({mksym("sync_click.cue"), mksym("play")})})});
  ip.eval(*task, env);
  CHECK(host.scheduled.size() == 3);
  if (host.scheduled.size() == 3) {
    CHECK(host.scheduled[0].first == 0.0f);
    CHECK(host.scheduled[0].second == 1);
    CHECK(host.scheduled[1].first > 0.132f &&
          host.scheduled[1].first < 0.134f);
    CHECK(host.scheduled[1].second == 1);
    CHECK(host.scheduled[2].first > 0.732f &&
          host.scheduled[2].first < 0.734f);
    CHECK(host.scheduled[2].second == 1);
  }
}

static void test_switch_matches_object_component_names() {
  Interp ip; MockHost host; Scope root; MockObject reset("UIButton");
  reset.set_name(Symbol("reset_to_zero.btn"));
  host.set_global(Symbol("component"), DataNode::Obj(&reset));
  MockObject panel("LagPanel");
  panel.set_property(Symbol("lag"), DataNode::Int(44));
  Env env; env.host = &host; env.scope = &root; env.self = &panel;
  NodePtr sw = mkcmd({
      mksym("switch"),
      mkvar("component"),
      mkarr({mksym("autocalibrate.btn"),
             mkcmd({mksym("set"), mkvar("matched"), mksym("auto")})}),
      mkarr({mksym("reset_to_zero.btn"),
             mkcmd({mksym("set"), mkprop("lag"), mkint(0)}),
             mkcmd({mksym("set"), mkvar("matched"), mksym("reset")})})});
  ip.eval(*sw, env);
  CHECK(panel.get_property(Symbol("lag")).as_int().value_or(-1) == 0);
  CHECK(host.get_global(Symbol("matched")).as_symbol().value_or(Symbol()) ==
        Symbol("reset"));
}

static void test_host_global_command() {
  Interp ip;
  MockHost host;
  Scope root;
  Env env;
  env.host = &host;
  env.scope = &root;

  DataNode out =
      ip.eval(*mkcmd({mksym("game_restart_fast"), mksym("fast_intro")}), env);
  CHECK(out.as_symbol().value_or(Symbol()) == Symbol("game_restart_fast"));
  CHECK(host.commands.size() == 1);
  if (!host.commands.empty())
    CHECK(host.commands[0] == "game_restart_fast:fast_intro");
  CHECK(host.unhandled.empty());
}

static void test_option_str_boot_shape() {
  Interp ip;
  MockHost host;
  Scope root;
  Env env;
  env.host = &host;
  env.scope = &root;

  NodePtr option =
      mkcmd({mksym("option_str"), mksym("budget_config"), mkvar("cfg")});
  CHECK(ip.eval(*option, env).as_int().value_or(-1) == 0);
  CHECK(!host.get_global(Symbol("cfg")).as_string().has_value());

  host.options[Symbol("budget_config").id()] = "track_budget.dtb";
  CHECK(ip.eval(*option, env).as_int().value_or(0) == 1);
  CHECK(host.get_global(Symbol("cfg")).as_string().value_or("") ==
        "track_budget.dtb");
  CHECK(host.options.empty());
  CHECK(ip.eval(*option, env).as_int().value_or(-1) == 0);
  CHECK(host.get_global(Symbol("cfg")).as_string().value_or("") ==
        "track_budget.dtb");
  CHECK(host.unhandled.empty());
}

static void test_exists_data_func_shape() {
  Interp ip;
  MockHost host;
  Scope root;
  Env env;
  env.host = &host;
  env.scope = &root;

  MockObject anim("Group");
  anim.set_name(Symbol("unlock_anim"));
  host.bind("unlock_anim", &anim);
  host.bind_func("route_to", NodeList{mksym("func"), mksym("route_to")});

  CHECK(ip.eval(*mkcmd({mksym("exists"), mksym("unlock_anim")}), env)
            .as_int()
            .value_or(0) == 1);
  CHECK(ip.eval(*mkcmd({mksym("exists"), mksym("route_to")}), env)
            .as_int()
            .value_or(0) == 1);
  CHECK(ip.eval(*mkcmd({mksym("exists"), mksym("sprintf")}), env)
            .as_int()
            .value_or(0) == 1);
  CHECK(ip.eval(*mkcmd({mksym("exists"), mksym("missing_anim")}), env)
            .as_int()
            .value_or(-1) == 0);
  CHECK(host.unhandled.empty());
}

static void test_preprocess() {
  // [ #ifdef HX_XBOX (a 1) #else (a 2) #endif ]
  NodeList roots = {mkdir(0x07, "HX_XBOX"), mkarr({mksym("a"), mkint(1)}),
                    mkdir(0x08, ""), mkarr({mksym("a"), mkint(2)}), mkdir(0x09, "")};
  {
    PreprocessOptions o;  // HX_XBOX undefined -> else branch
    NodeList out = preprocess(roots, o);
    CHECK(out.size() == 1);
    CHECK(out.size() == 1 && gh::dtb::children(*out[0]).size() == 2 &&
          gh::dtb::as_int(*gh::dtb::children(*out[0])[1]).value_or(-1) == 2);
  }
  {
    PreprocessOptions o; o.defines.insert("HX_XBOX");  // -> then branch
    NodeList out = preprocess(roots, o);
    CHECK(out.size() == 1 && gh::dtb::children(*out[0]).size() == 2 &&
          gh::dtb::as_int(*gh::dtb::children(*out[0])[1]).value_or(-1) == 1);
  }
  // #define FOO (1 2 3) ; {x FOO}  -> FOO substituted by the array
  {
    NodeList r2 = {mkdir(0x20, "FOO"), mkarr({mkint(1), mkint(2), mkint(3)}),
                   mkcmd({mksym("x"), mksym("FOO")})};
    PreprocessOptions o;
    NodeList out = preprocess(r2, o);
    CHECK(out.size() == 1);  // the #define + body are consumed; only {x FOO} remains
    if (out.size() == 1) {
      const auto& cmd = gh::dtb::children(*out[0]);  // x , <array>
      CHECK(cmd.size() == 2 && gh::dtb::is_array(*cmd[1]) &&
            gh::dtb::children(*cmd[1]).size() == 3);
    }
  }
  // #define DELAY (60) ; {> $now DELAY} -> DELAY substituted by the scalar.
  {
    NodeList r3 = {mkdir(0x20, "DELAY"), mkarr({mkint(60)}),
                   mkcmd({mksym(">"), mkvar("now"), mksym("DELAY")})};
    PreprocessOptions o;
    NodeList out = preprocess(r3, o);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
      const auto& cmd = gh::dtb::children(*out[0]);
      CHECK(cmd.size() == 3 &&
            gh::dtb::as_int(*cmd[2]).value_or(-1) == 60);
    }
  }
}

int main() {
  test_operators();
  test_vars_and_props();
  test_handler_prefers_local_object_dir_child();
  test_runtime_new_and_message_output_refs();
  test_main_select_switch();
  test_career_branch();
  test_do_local_object();
  test_foreach_load();
  test_object_foreach_callback();
  test_foreach_int_and_string_target();
  test_run_handler_params();
  test_dynamic_message_and_evaluated_array_args();
  test_top_level_function_call();
  test_sprintf();
  test_stock_collection_helpers();
  test_script_task_boot_shape();
  test_thread_task_sleep_segments();
  test_switch_matches_object_component_names();
  test_host_global_command();
  test_option_str_boot_shape();
  test_exists_data_func_shape();
  test_preprocess();
  if (g_failures == 0) {
    std::printf("ghogx_script_test: interpreter runs real main.dtb script shapes -- all checks passed\n");
    return 0;
  }
  std::fprintf(stderr, "ghogx_script_test: %d check(s) failed\n", g_failures);
  return 1;
}
