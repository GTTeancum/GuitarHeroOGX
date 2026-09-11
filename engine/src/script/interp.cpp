// engine/src/script/interp.cpp -- see interp.h.

#include "script/interp.h"

#include "core/object.h"
#include "core/object_dir.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstring>
#include <unordered_map>

namespace ghogx::script {

namespace {

// dtb payload string of any string-like node (symbol/var/quoted/directive).
std::string node_str(const Node& n) {
  auto s = gh::dtb::as_string(n);
  return s ? *s : std::string();
}
Symbol node_sym(const Node& n) { return Symbol(node_str(n)); }

// Convert a node to a *literal* DataNode without executing commands. Used when
// a (...) data array appears in value position (e.g. a message argument like
// the helpbar display list); preserves the tree shape as nested DataArrays.
DataNode to_literal(const Node& n) {
  switch (n.tag) {
    case 0x00: return DataNode::Int(gh::dtb::as_int(n).value_or(0));
    case 0x01: return DataNode::Float(gh::dtb::as_float(n).value_or(0.0f));
    case 0x12: return DataNode::Str(node_str(n));
    case 0x05:
    case 0x02: return DataNode::Sym(Symbol(node_str(n)));
    case 0x10:
    case 0x11:
    case 0x13: {
      auto arr = std::make_shared<DataArray>();
      for (const auto& k : gh::dtb::children(n)) arr->push(to_literal(*k));
      return DataNode::Array(arr);
    }
    default: return DataNode();
  }
}

Object* resolve_object(Symbol name, Env& env) {
  // Bare object names inside a Harmonix handler are relative to the handler's
  // ObjectDir before they fall back to the global registry. Stock panels
  // legitimately reuse child names such as song_name.lbl.
  if (env.self) {
    if (env.self->name() == name) return env.self;
    if (auto* dir = dynamic_cast<ObjectDir*>(env.self)) {
      if (Object* child = dir->find_path(name.c_str())) return child;
    }
  }
  return env.host ? env.host->resolve_object(name) : nullptr;
}

Object* node_to_object(const DataNode& v, Env& env) {
  if (Object* o = v.as_object()) return o;
  if (auto s = v.as_symbol()) return resolve_object(*s, env);
  if (auto text = v.as_string()) return resolve_object(Symbol(*text), env);
  return nullptr;
}

// --- builtin table ---------------------------------------------------------
enum Builtin {
  BI_NONE = 0,
  BI_IF, BI_IF_ELSE, BI_DO, BI_SWITCH, BI_FOREACH, BI_FOREACH_INT, BI_COND, BI_SET,
  BI_EQ, BI_NE, BI_LT, BI_GT, BI_LE, BI_GE,
  BI_NOT, BI_AND, BI_OR, BI_BIT_AND, BI_BIT_OR,
  BI_ADD, BI_SUB, BI_MUL, BI_DIV, BI_MOD, BI_MIN, BI_MAX,
  BI_INC, BI_ADD_ASSIGN, BI_INT, BI_ARRAY, BI_ELEM, BI_FIND_ELEM,
  BI_RANDOM_ELEM, BI_REMOVE_ELEM,
  BI_SPRINTF, BI_SPRINT, BI_RESIZE, BI_PUSH_BACK, BI_LOCALIZE, BI_PRINT,
  BI_EXISTS, BI_OPTION_STR, BI_TEXT_ENTRY_HELP, BI_AUTOSAVE_GOTO, BI_SCRIPT_TASK,
  BI_NEW,
};

int lookup_builtin(Symbol s) {
  static const std::unordered_map<const void*, int> kTable = [] {
    std::unordered_map<const void*, int> m;
    auto add = [&](const char* name, int id) { m[Symbol(name).id()] = id; };
    add("if", BI_IF);          add("if_else", BI_IF_ELSE);
    add("do", BI_DO);          add("switch", BI_SWITCH);
    add("foreach", BI_FOREACH);add("foreach_int", BI_FOREACH_INT);
    add("cond", BI_COND);
    add("set", BI_SET);
    add("==", BI_EQ);          add("!=", BI_NE);
    add("<", BI_LT);           add(">", BI_GT);
    add("<=", BI_LE);          add(">=", BI_GE);
    add("!", BI_NOT);          add("&&", BI_AND);   add("||", BI_OR);
    add("&", BI_BIT_AND);      add("|", BI_BIT_OR);
    add("+", BI_ADD);          add("-", BI_SUB);
    add("*", BI_MUL);          add("/", BI_DIV);     add("mod", BI_MOD);
    add("min", BI_MIN);        add("max", BI_MAX);
    add("++", BI_INC);         add("+=", BI_ADD_ASSIGN);
    add("int", BI_INT);        add("array", BI_ARRAY);
    add("elem", BI_ELEM);      add("find_elem", BI_FIND_ELEM);
    add("random_elem", BI_RANDOM_ELEM);
    add("remove_elem", BI_REMOVE_ELEM);
    add("sprintf", BI_SPRINTF);add("sprint", BI_SPRINT);
    add("resize", BI_RESIZE);  add("push_back", BI_PUSH_BACK);
    add("localize", BI_LOCALIZE);
    add("print", BI_PRINT);     add("printf", BI_PRINT);
    add("exists", BI_EXISTS);
    add("option_str", BI_OPTION_STR);
    add("get_text_entry_help_text", BI_TEXT_ENTRY_HELP);
    add("autosave_goto", BI_AUTOSAVE_GOTO);
    add("script_task", BI_SCRIPT_TASK);
    add("thread_task", BI_SCRIPT_TASK);
    add("new", BI_NEW);
    return m;
  }();
  auto it = kTable.find(s.id());
  return it == kTable.end() ? BI_NONE : it->second;
}

float node_float_value(const DataNode& node, float fallback = 0.0f) {
  if (auto f = node.as_float()) return *f;
  if (auto i = node.as_int()) return static_cast<float>(*i);
  return fallback;
}

bool task_sleep_seconds(const Node& node, float& seconds) {
  if (!is_command(node)) return false;
  const NodeList& kids = gh::dtb::children(node);
  if (kids.size() < 3) return false;
  if (!is_variable(*kids[0]) || node_str(*kids[0]) != "task")
    return false;
  if (node_sym(*kids[1]) != Symbol("sleep")) return false;
  if (auto f = gh::dtb::as_float(*kids[2]))
    seconds = *f;
  else if (auto i = gh::dtb::as_int(*kids[2]))
    seconds = static_cast<float>(*i);
  else
    seconds = node_float_value(to_literal(*kids[2]));
  seconds = std::max(0.0f, seconds);
  return true;
}

}  // namespace

// --- Scope -----------------------------------------------------------------
void Scope::declare(Symbol name, DataNode value) {
  for (auto& kv : vars_) {
    if (kv.first == name) { kv.second = std::move(value); return; }
  }
  vars_.emplace_back(name, std::move(value));
}
DataNode* Scope::find(Symbol name) {
  for (auto& kv : vars_)
    if (kv.first == name) return &kv.second;
  return parent_ ? parent_->find(name) : nullptr;
}
bool Scope::assign(Symbol name, DataNode value) {
  for (auto& kv : vars_) {
    if (kv.first == name) { kv.second = std::move(value); return true; }
  }
  return parent_ ? parent_->assign(name, std::move(value)) : false;
}

// --- truthiness / equality -------------------------------------------------
bool truthy(const DataNode& v) {
  switch (v.type()) {
    case DataType::kInt: return v.as_int().value_or(0) != 0;
    case DataType::kFloat: return v.as_float().value_or(0.0f) != 0.0f;
    case DataType::kSymbol: {
      auto s = v.as_string();
      if (!s) return false;
      return !(*s == "FALSE" || s->empty());  // TRUE and any other symbol -> true
    }
    case DataType::kString: return !v.as_string().value_or("").empty();
    case DataType::kObject: return v.as_object() != nullptr;
    case DataType::kArray: return v.as_array() && v.as_array()->size() > 0;
    case DataType::kEmpty: default: return false;
  }
}

bool node_equal(const DataNode& a, const DataNode& b) {
  // numbers compare by value (int/float coerced); symbols by interned identity;
  // strings by text. Cross-type number compare allowed.
  bool an = a.type() == DataType::kInt || a.type() == DataType::kFloat;
  bool bn = b.type() == DataType::kInt || b.type() == DataType::kFloat;
  if (an && bn) return a.as_float().value() == b.as_float().value();
  auto ui_state_code = [](const DataNode& n) -> std::optional<int32_t> {
    auto s = n.as_symbol();
    if (!s) return std::nullopt;
    if (*s == Symbol("kNormal")) return 0;
    if (*s == Symbol("kFocused")) return 1;
    if (*s == Symbol("kDisabled")) return 2;
    if (*s == Symbol("kSelecting")) return 3;
    if (*s == Symbol("kSelected")) return 4;
    return std::nullopt;
  };
  if (an) {
    if (auto code = ui_state_code(b))
      return a.as_float().value_or(0.0f) == static_cast<float>(*code);
  }
  if (bn) {
    if (auto code = ui_state_code(a))
      return b.as_float().value_or(0.0f) == static_cast<float>(*code);
  }
  if (a.type() == DataType::kArray || b.type() == DataType::kArray) {
    auto aa = a.as_array(), ba = b.as_array();
    if (!aa || !ba || aa->size() != ba->size()) return false;
    for (std::size_t i = 0; i < aa->size(); ++i)
      if (!node_equal(aa->at(i), ba->at(i))) return false;
    return true;
  }
  if (a.type() == DataType::kSymbol && b.type() == DataType::kSymbol)
    return a.as_symbol().value() == b.as_symbol().value();
  if (Object* ao = a.as_object()) {
    if (auto bs = b.as_symbol()) return ao->name() == *bs;
    if (auto bt = b.as_string()) return std::string(ao->name().c_str()) == *bt;
  }
  if (Object* bo = b.as_object()) {
    if (auto as = a.as_symbol()) return bo->name() == *as;
    if (auto at = a.as_string()) return std::string(bo->name().c_str()) == *at;
  }
  if (a.type() == DataType::kObject || b.type() == DataType::kObject)
    return a.as_object() == b.as_object();
  // fall back to text compare (symbol/string)
  auto as = a.as_string(), bs = b.as_string();
  if (as && bs) return *as == *bs;
  return a.type() == DataType::kEmpty && b.type() == DataType::kEmpty;
}

std::string value_text(const DataNode& v) {
  if (auto s = v.as_string()) return std::string(*s);
  if (auto i = v.as_int()) return std::to_string(*i);
  if (auto f = v.as_float()) {
    std::string out = std::to_string(*f);
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out;
  }
  if (Object* o = v.as_object()) return std::string(o->name().c_str());
  return {};
}

std::string format_grouped_int(int32_t value) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  std::string out;
  if (value < 0) out += '-';
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (digits.size() - i) % 3 == 0)
      out += ',';
    out += digits[i];
  }
  return out;
}

// --- Interp ----------------------------------------------------------------
DataNode Interp::eval(const Node& n, Env& env) {
  switch (n.tag) {
    case 0x00: return DataNode::Int(gh::dtb::as_int(n).value_or(0));
    case 0x01: return DataNode::Float(gh::dtb::as_float(n).value_or(0.0f));
    case 0x12: return DataNode::Str(node_str(n));
    case 0x05: return DataNode::Sym(Symbol(node_str(n)));
    case 0x02: {  // $variable
      std::string nm = node_str(n);
      if (nm == "this") return DataNode::Obj(env.self);
      Symbol s(nm);
      if (env.scope) {
        if (DataNode* v = env.scope->find(s)) return *v;
      }
      return env.host ? env.host->get_global(s) : DataNode();
    }
    case 0x13: {  // [prop] -> get_property on self
      const NodeList& k = gh::dtb::children(n);
      if (k.empty() || !env.self) return DataNode();
      return env.self->get_property(node_sym(*k[0]));
    }
    case 0x11: return eval_command(n, env);  // {command}
    case 0x10: return eval_data_array(n, env); // script-time (...) value
    default: return DataNode();
  }
}

DataNode Interp::eval_seq(const NodeList& body, std::size_t start, Env& env) {
  DataNode last;
  for (std::size_t i = start; i < body.size(); ++i) last = eval(*body[i], env);
  return last;
}

DataArray Interp::eval_args(const Node& cmd, std::size_t arg_start, Env& env) {
  DataArray args;
  const NodeList& k = gh::dtb::children(cmd);
  for (std::size_t i = arg_start; i < k.size(); ++i) args.push(eval(*k[i], env));
  return args;
}

DataNode Interp::eval_data_array(const Node& array, Env& env) {
  auto out = std::make_shared<DataArray>();
  for (const auto& child : gh::dtb::children(array)) {
    if (child && child->tag == 0x10)
      out->push(eval_data_array(*child, env));
    else
      out->push(child ? eval(*child, env) : DataNode());
  }
  return DataNode::Array(std::move(out));
}

Symbol Interp::eval_message_symbol(const Node& message, Env& env) {
  if (message.tag == 0x05 || message.tag == 0x12)
    return node_sym(message);
  const DataNode value = eval(message, env);
  if (auto symbol = value.as_symbol()) return *symbol;
  if (auto text = value.as_string()) return Symbol(*text);
  return Symbol();
}

Object* Interp::eval_object(const Node& head, Env& env) {
  return node_to_object(eval(head, env), env);
}

bool Interp::try_object_foreach(Object* target, Symbol msg, const Node& cmd,
                                Env& env, DataNode& out) {
  if (!target || std::strncmp(msg.c_str(), "foreach_", 8) != 0) return false;
  const NodeList& kids = gh::dtb::children(cmd);
  if (kids.size() < 4 || !is_variable(*kids[2])) return false;

  DataArray query;
  const std::string values_msg = std::string(msg.c_str()) + "_values";
  auto values = target->handle_property(Symbol(values_msg), query).as_array();
  if (!values) return false;

  Symbol var = node_sym(*kids[2]);
  Scope scope(env.scope);
  Env loop_env = env;
  loop_env.scope = &scope;
  scope.declare(var, DataNode());
  for (std::size_t i = 0; i < values->size(); ++i) {
    scope.assign(var, values->at(i));
    out = eval_seq(kids, 3, loop_env);
  }
  return true;
}

DataNode Interp::assign_target(const Node& target, DataNode value, Env& env) {
  if (is_prop_ref(target)) {
    const NodeList& pr = gh::dtb::children(target);
    if (!pr.empty() && env.self) env.self->set_property(node_sym(*pr[0]), value);
  } else if (is_variable(target)) {
    Symbol name = node_sym(target);
    if (!(env.scope && env.scope->assign(name, value))) {
      if (env.host) env.host->set_global(name, value);
    }
  }
  return value;
}

DataNode Interp::eval_command(const Node& cmd, Env& env) {
  const NodeList& kids = gh::dtb::children(cmd);
  if (kids.empty()) return DataNode();
  const Node& head = *kids[0];

  if (is_symbol(head)) {
    Symbol h = node_sym(head);
    switch (lookup_builtin(h)) {
      case BI_IF: return bi_if(cmd, env, false);
      case BI_IF_ELSE: return bi_if(cmd, env, true);
      case BI_DO: return bi_do(cmd, env);
      case BI_SWITCH: return bi_switch(cmd, env);
      case BI_FOREACH: return bi_foreach(cmd, env);
      case BI_FOREACH_INT: return bi_foreach_int(cmd, env);
      case BI_COND: return bi_cond(cmd, env);
      case BI_SET: return bi_set(cmd, env);
      case BI_EQ: return bi_compare(cmd, env, BI_EQ);
      case BI_NE: return bi_compare(cmd, env, BI_NE);
      case BI_LT: return bi_compare(cmd, env, BI_LT);
      case BI_GT: return bi_compare(cmd, env, BI_GT);
      case BI_LE: return bi_compare(cmd, env, BI_LE);
      case BI_GE: return bi_compare(cmd, env, BI_GE);
      case BI_NOT: return bi_logic(cmd, env, BI_NOT);
      case BI_AND: return bi_logic(cmd, env, BI_AND);
      case BI_OR: return bi_logic(cmd, env, BI_OR);
      case BI_BIT_AND: return bi_bitwise(cmd, env, false);
      case BI_BIT_OR: return bi_bitwise(cmd, env, true);
      case BI_ADD: return bi_arith(cmd, env, BI_ADD);
      case BI_SUB: return bi_arith(cmd, env, BI_SUB);
      case BI_MUL: return bi_arith(cmd, env, BI_MUL);
      case BI_DIV: return bi_arith(cmd, env, BI_DIV);
      case BI_MOD: return bi_mod(cmd, env);
      case BI_MIN: return bi_minmax(cmd, env, BI_MIN);
      case BI_MAX: return bi_minmax(cmd, env, BI_MAX);
      case BI_INC: return bi_inc_assign(cmd, env, BI_INC);
      case BI_ADD_ASSIGN: return bi_inc_assign(cmd, env, BI_ADD_ASSIGN);
      case BI_INT: return bi_int(cmd, env);
      case BI_ARRAY: return bi_array(cmd, env);
      case BI_ELEM: return bi_elem(cmd, env);
      case BI_FIND_ELEM: return bi_find_elem(cmd, env);
      case BI_RANDOM_ELEM: return bi_random_elem(cmd, env);
      case BI_REMOVE_ELEM: return bi_remove_elem(cmd, env);
      case BI_SPRINTF: return bi_sprintf(cmd, env);
      case BI_SPRINT: return bi_sprint(cmd, env);
      case BI_RESIZE: return bi_resize(cmd, env);
      case BI_PUSH_BACK: return bi_push_back(cmd, env);
      case BI_LOCALIZE: return bi_localize(cmd, env);
      case BI_PRINT: return bi_print(cmd, env);
      case BI_EXISTS: return bi_exists(cmd, env);
      case BI_OPTION_STR: return bi_option_str(cmd, env);
      case BI_TEXT_ENTRY_HELP: return bi_text_entry_help(cmd, env);
      case BI_AUTOSAVE_GOTO: return bi_autosave_goto(cmd, env);
      case BI_SCRIPT_TASK: return bi_script_task(cmd, env);
      case BI_NEW: return bi_new(cmd, env);
      default: break;  // not a builtin -> object message via symbol target
    }
    if (env.host) {
      if (const NodeList* fn = env.host->resolve_function(h)) {
        DataArray args = eval_args(cmd, 1, env);
        return run_function(*fn, args, env);
      }
    }

    Object* t = resolve_object(h, env);
    if (!t) {
      if (env.host) {
        DataArray args = eval_args(cmd, 1, env);
        DataNode out;
        if (env.host->handle_command(h, args, out)) return out;
        env.host->on_unhandled(std::string("target?:") + h.c_str());
      }
      return DataNode();
    }
    if (kids.size() < 2) return DataNode();
    Symbol msg = eval_message_symbol(*kids[1], env);
    DataNode foreach_out;
    if (try_object_foreach(t, msg, cmd, env, foreach_out)) return foreach_out;
    DataArray args = eval_args(cmd, 2, env);
    DataNode result = t->handle_property(msg, args);
    if (auto values = result.as_array()) {
      for (std::size_t i = 0; i < values->size() && 2 + i < kids.size(); ++i)
        assign_target(*kids[2 + i], values->at(i), env);
    }
    return result;
  }

  // head is $var / {command} / [prop] -> object target
  Object* t = eval_object(head, env);
  if (!t) {
    if (env.host) env.host->on_unhandled("target?:<expr>");
    return DataNode();
  }
  if (kids.size() < 2) return DataNode();
  Symbol msg = eval_message_symbol(*kids[1], env);
  DataNode foreach_out;
  if (try_object_foreach(t, msg, cmd, env, foreach_out)) return foreach_out;
  DataArray args = eval_args(cmd, 2, env);
  DataNode result = t->handle_property(msg, args);
  if (auto values = result.as_array()) {
    for (std::size_t i = 0; i < values->size() && 2 + i < kids.size(); ++i)
      assign_target(*kids[2 + i], values->at(i), env);
  }
  return result;
}

DataNode Interp::run_handler(const NodeList& handler_body, Object* self,
                             const DataArray& args, Env& parent_env) {
  Scope scope(parent_env.scope);
  Env env;
  env.self = self;
  env.scope = &scope;
  env.host = parent_env.host;

  std::size_t start = 1;  // [0] is the handler name symbol
  // Optional parameter list: (a $b ...) as the node right after the name.
  if (handler_body.size() > 1 && is_data_array(*handler_body[1])) {
    const NodeList& params = gh::dtb::children(*handler_body[1]);
    bool all_vars = !params.empty();
    for (const auto& p : params)
      if (!is_variable(*p)) { all_vars = false; break; }
    if (all_vars) {
      for (std::size_t i = 0; i < params.size(); ++i) {
        DataNode v = i < args.size() ? args.at(i) : DataNode();
        scope.declare(node_sym(*params[i]), v);
      }
      start = 2;
    }
  }
  return eval_seq(handler_body, start, env);
}

DataNode Interp::run_function(const NodeList& function_body,
                              const DataArray& args, Env& parent_env) {
  Scope scope(parent_env.scope);
  Env env;
  env.self = parent_env.self;
  env.scope = &scope;
  env.host = parent_env.host;

  std::size_t start = 2;  // [0] is `func`, [1] is the function name.
  if (function_body.size() > 2 && is_data_array(*function_body[2])) {
    const NodeList& params = gh::dtb::children(*function_body[2]);
    bool all_vars = !params.empty();
    for (const auto& p : params)
      if (!is_variable(*p)) { all_vars = false; break; }
    if (all_vars) {
      for (std::size_t i = 0; i < params.size(); ++i) {
        DataNode v = i < args.size() ? args.at(i) : DataNode();
        scope.declare(node_sym(*params[i]), v);
      }
      start = 3;
    }
  }
  return eval_seq(function_body, start, env);
}

// --- builtins --------------------------------------------------------------
DataNode Interp::bi_if(const Node& c, Env& env, bool has_else) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode();
  bool cond = truthy(eval(*k[1], env));
  if (has_else) {
    if (cond) return k.size() > 2 ? eval(*k[2], env) : DataNode();
    return k.size() > 3 ? eval(*k[3], env) : DataNode();
  }
  if (cond) return eval_seq(k, 2, env);
  return DataNode();
}

DataNode Interp::bi_do(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  Scope scope(env.scope);
  Env e = env;
  e.scope = &scope;
  std::size_t i = 1;
  // Leading (var [init]) arrays are local declarations.
  for (; i < k.size(); ++i) {
    if (!is_data_array(*k[i])) break;
    const NodeList& d = gh::dtb::children(*k[i]);
    if (d.empty() || !is_variable(*d[0])) break;
    DataNode init = d.size() > 1 ? eval(*d[1], e) : DataNode();
    scope.declare(node_sym(*d[0]), init);
  }
  return eval_seq(k, i, e);
}

DataNode Interp::bi_switch(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode();
  DataNode val = eval(*k[1], env);
  const Node* default_case = nullptr;
  for (std::size_t i = 2; i < k.size(); ++i) {
    if (!is_data_array(*k[i])) { default_case = k[i].get(); continue; }
    const NodeList& cse = gh::dtb::children(*k[i]);
    if (cse.empty()) continue;
    if (node_equal(val, eval(*cse[0], env))) return eval_seq(cse, 1, env);
  }
  if (default_case) return eval(*default_case, env);
  return DataNode();
}

DataNode Interp::bi_foreach(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3 || !is_variable(*k[1]) || !is_data_array(*k[2])) return DataNode();
  Symbol var = node_sym(*k[1]);
  Scope scope(env.scope);
  Env e = env;
  e.scope = &scope;
  scope.declare(var, DataNode());
  for (const auto& item : gh::dtb::children(*k[2])) {
    scope.assign(var, to_literal(*item));
    eval_seq(k, 3, e);
  }
  return DataNode();
}

DataNode Interp::bi_foreach_int(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 4 || !is_variable(*k[1])) return DataNode();
  const int32_t first = eval(*k[2], env).as_int().value_or(0);
  const int32_t stop = eval(*k[3], env).as_int().value_or(0);
  Symbol var = node_sym(*k[1]);
  Scope scope(env.scope);
  Env loop_env = env;
  loop_env.scope = &scope;
  scope.declare(var, DataNode());
  DataNode last;
  if (first <= stop) {
    for (int32_t i = first; i < stop; ++i) {
      scope.assign(var, DataNode::Int(i));
      last = eval_seq(k, 4, loop_env);
    }
  } else {
    for (int32_t i = first; i > stop; --i) {
      scope.assign(var, DataNode::Int(i));
      last = eval_seq(k, 4, loop_env);
    }
  }
  return last;
}

DataNode Interp::bi_cond(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  for (std::size_t i = 1; i < k.size(); ++i) {
    if (!is_data_array(*k[i])) continue;
    const NodeList& branch = gh::dtb::children(*k[i]);
    if (branch.empty()) continue;
    if (truthy(eval(*branch[0], env))) return eval_seq(branch, 1, env);
  }
  return DataNode();
}

DataNode Interp::bi_set(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode();
  DataNode value = eval(*k[2], env);
  return assign_target(*k[1], value, env);
}

DataNode Interp::bi_compare(const Node& c, Env& env, int op) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode::Int(0);
  DataNode a = eval(*k[1], env), b = eval(*k[2], env);
  bool r = false;
  if (op == BI_EQ) r = node_equal(a, b);
  else if (op == BI_NE) r = !node_equal(a, b);
  else {
    float af = a.as_float().value_or(0.0f), bf = b.as_float().value_or(0.0f);
    if (op == BI_LT) r = af < bf;
    else if (op == BI_GT) r = af > bf;
    else if (op == BI_LE) r = af <= bf;
    else if (op == BI_GE) r = af >= bf;
  }
  return DataNode::Int(r ? 1 : 0);
}

DataNode Interp::bi_logic(const Node& c, Env& env, int op) {
  const NodeList& k = gh::dtb::children(c);
  if (op == BI_NOT)
    return DataNode::Int((k.size() >= 2 && truthy(eval(*k[1], env))) ? 0 : 1);
  // && / ||  (short-circuit)
  for (std::size_t i = 1; i < k.size(); ++i) {
    bool t = truthy(eval(*k[i], env));
    if (op == BI_AND && !t) return DataNode::Int(0);
    if (op == BI_OR && t) return DataNode::Int(1);
  }
  return DataNode::Int(op == BI_AND ? 1 : 0);
}

DataNode Interp::bi_bitwise(const Node& c, Env& env, bool bit_or) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Int(0);
  auto integer_value = [&](const Node& source) {
    DataNode value = eval(source, env);
    if (const auto integer = value.as_int()) return *integer;
    if (const auto symbol = value.as_symbol()) {
      if (env.host) {
        return env.host->get_global(*symbol).as_int().value_or(0);
      }
    }
    return 0;
  };
  int32_t result = integer_value(*k[1]);
  for (std::size_t i = 2; i < k.size(); ++i) {
    const int32_t value = integer_value(*k[i]);
    result = bit_or ? (result | value) : (result & value);
  }
  return DataNode::Int(result);
}

DataNode Interp::bi_arith(const Node& c, Env& env, int op) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Int(0);
  std::vector<DataNode> v;
  bool any_float = false;
  for (std::size_t i = 1; i < k.size(); ++i) {
    v.push_back(eval(*k[i], env));
    if (v.back().type() == DataType::kFloat) any_float = true;
  }
  if (any_float) {
    float acc = v[0].as_float().value_or(0.0f);
    for (std::size_t i = 1; i < v.size(); ++i) {
      float x = v[i].as_float().value_or(0.0f);
      if (op == BI_ADD) acc += x; else if (op == BI_SUB) acc -= x;
      else if (op == BI_MUL) acc *= x; else if (op == BI_DIV) acc = x != 0 ? acc / x : 0;
    }
    return DataNode::Float(acc);
  }
  int32_t acc = v[0].as_int().value_or(0);
  for (std::size_t i = 1; i < v.size(); ++i) {
    int32_t x = v[i].as_int().value_or(0);
    if (op == BI_ADD) acc += x; else if (op == BI_SUB) acc -= x;
    else if (op == BI_MUL) acc *= x; else if (op == BI_DIV) acc = x != 0 ? acc / x : 0;
  }
  return DataNode::Int(acc);
}

DataNode Interp::bi_mod(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode::Int(0);
  int32_t a = eval(*k[1], env).as_int().value_or(0);
  int32_t b = eval(*k[2], env).as_int().value_or(0);
  if (b == 0) return DataNode::Int(0);
  int32_t r = a % b;
  int32_t m = b < 0 ? -b : b;
  if (r < 0) r += m;
  return DataNode::Int(r);
}

DataNode Interp::bi_minmax(const Node& c, Env& env, int op) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Int(0);
  DataNode best = eval(*k[1], env);
  bool any_float = best.type() == DataType::kFloat;
  float best_f = best.as_float().value_or(0.0f);
  int32_t best_i = best.as_int().value_or(static_cast<int32_t>(best_f));
  for (std::size_t i = 2; i < k.size(); ++i) {
    DataNode cur = eval(*k[i], env);
    any_float = any_float || cur.type() == DataType::kFloat;
    if (any_float) {
      const float value = cur.as_float().value_or(0.0f);
      best_f = (op == BI_MIN) ? std::min(best_f, value)
                              : std::max(best_f, value);
    } else {
      const int32_t value = cur.as_int().value_or(0);
      best_i = (op == BI_MIN) ? std::min(best_i, value)
                              : std::max(best_i, value);
      best_f = static_cast<float>(best_i);
    }
  }
  return any_float ? DataNode::Float(best_f) : DataNode::Int(best_i);
}

DataNode Interp::bi_inc_assign(const Node& c, Env& env, int op) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode();
  const int32_t cur = eval(*k[1], env).as_int().value_or(0);
  const int32_t delta =
      (op == BI_INC) ? 1 : (k.size() > 2 ? eval(*k[2], env).as_int().value_or(0) : 0);
  return assign_target(*k[1], DataNode::Int(cur + delta), env);
}

DataNode Interp::bi_int(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Int(0);
  DataNode value = eval(*k[1], env);
  if (auto i = value.as_int()) return DataNode::Int(*i);
  if (auto f = value.as_float()) return DataNode::Int(static_cast<int32_t>(*f));
  return DataNode::Int(0);
}

DataNode Interp::bi_array(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  auto out = std::make_shared<DataArray>();
  if (k.size() == 2) {
    DataNode maybe_size = eval(*k[1], env);
    if (auto count = maybe_size.as_int()) {
      out->resize(static_cast<std::size_t>(std::max<int32_t>(0, *count)));
      return DataNode::Array(out);
    }
  }
  for (std::size_t i = 1; i < k.size(); ++i) out->push(eval(*k[i], env));
  return DataNode::Array(out);
}

DataNode Interp::bi_elem(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode();
  auto arr = eval(*k[1], env).as_array();
  if (!arr) return DataNode();
  const int32_t index = eval(*k[2], env).as_int().value_or(-1);
  if (index < 0 || static_cast<std::size_t>(index) >= arr->size()) return DataNode();
  return arr->at(static_cast<std::size_t>(index));
}

DataNode Interp::bi_find_elem(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode::Int(-1);
  auto arr = eval(*k[1], env).as_array();
  if (!arr) return DataNode::Int(-1);
  DataNode needle = eval(*k[2], env);
  for (std::size_t i = 0; i < arr->size(); ++i)
    if (node_equal(arr->at(i), needle)) return DataNode::Int(static_cast<int32_t>(i));
  return DataNode::Int(-1);
}

DataNode Interp::bi_random_elem(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode();
  auto arr = eval(*k[1], env).as_array();
  if (!arr || arr->empty()) return DataNode();
  return arr->at(0);
}

DataNode Interp::bi_remove_elem(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode();
  auto arr = eval(*k[1], env).as_array();
  if (!arr) return DataNode();
  DataNode needle = eval(*k[2], env);
  auto out = std::make_shared<DataArray>();
  bool removed = false;
  for (std::size_t i = 0; i < arr->size(); ++i) {
    if (!removed && node_equal(arr->at(i), needle)) {
      removed = true;
      continue;
    }
    out->push(arr->at(i));
  }
  arr->resize(0);
  for (std::size_t i = 0; i < out->size(); ++i) arr->push(out->at(i));
  return DataNode::Int(removed ? 1 : 0);
}

DataNode Interp::bi_sprintf(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Str("");
  std::string fmt(eval(*k[1], env).as_string().value_or(""));
  std::string out;
  std::size_t argi = 2;
  for (std::size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '%' && i + 1 < fmt.size()) {
      char sp = fmt[++i];
      bool zero_pad = false;
      int width = 0;
      if (sp == '0' && i + 1 < fmt.size()) {
        zero_pad = true;
        sp = fmt[++i];
      }
      while (sp >= '0' && sp <= '9') {
        width = width * 10 + (sp - '0');
        if (i + 1 >= fmt.size()) break;
        sp = fmt[++i];
      }
      bool grouped_decimal = false;
      if (sp == '/' && i + 1 < fmt.size()) {
        char flagged = fmt[i + 1];
        if (flagged == 'D' || flagged == 'd' || flagged == 'i') {
          grouped_decimal = true;
          sp = flagged;
          ++i;
        }
      }
      if (sp == '%') {
        out += '%';
        continue;
      }
      DataNode a = argi < k.size() ? eval(*k[argi++], env) : DataNode();
      if (sp == 's') out += std::string(a.as_string().value_or(""));
      else if (sp == 'd' || sp == 'i' || sp == 'D') {
        int32_t value = a.as_int().value_or(0);
        std::string text =
            grouped_decimal ? format_grouped_int(value) : std::to_string(value);
        if (!grouped_decimal && zero_pad && width > 0 &&
            static_cast<int>(text.size()) < width) {
          if (!text.empty() && text[0] == '-') {
            text.insert(1, static_cast<std::size_t>(width - text.size()), '0');
          } else {
            text.insert(0, static_cast<std::size_t>(width - text.size()), '0');
          }
        }
        out += text;
      }
      else { out += '%'; out += sp; }
    } else {
      out += fmt[i];
    }
  }
  return DataNode::Str(out);
}

DataNode Interp::bi_sprint(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  std::string out;
  for (std::size_t i = 1; i < k.size(); ++i) out += value_text(eval(*k[i], env));
  return DataNode::Str(out);
}

DataNode Interp::bi_resize(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode();
  DataNode target = eval(*k[1], env);
  auto arr = target.as_array();
  if (!arr) return target;
  int32_t n = eval(*k[2], env).as_int().value_or(0);
  arr->resize(n > 0 ? static_cast<std::size_t>(n) : 0u);
  return target;
}

DataNode Interp::bi_push_back(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3) return DataNode();
  DataNode target = eval(*k[1], env);
  auto arr = target.as_array();
  if (!arr) return target;
  arr->push(eval(*k[2], env));
  return target;
}

DataNode Interp::bi_exists(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2) return DataNode::Int(0);
  Symbol name(node_str(*k[1]));
  if (!name.valid()) {
    if (auto s = eval(*k[1], env).as_string()) name = Symbol(*s);
  }
  if (!name.valid()) return DataNode::Int(0);
  const bool exists =
      (env.host && env.host->symbol_exists(name)) ||
      lookup_builtin(name) != BI_NONE;
  return DataNode::Int(exists ? 1 : 0);
}

DataNode Interp::bi_option_str(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3 || !env.host) return DataNode::Int(0);
  Symbol option(node_str(*k[1]));
  if (!option.valid()) {
    if (auto s = eval(*k[1], env).as_string()) option = Symbol(*s);
  }
  if (!option.valid()) return DataNode::Int(0);
  std::optional<std::string> value = env.host->consume_option_str(option);
  if (!value) return DataNode::Int(0);
  assign_target(*k[2], DataNode::Str(*value), env);
  return DataNode::Int(1);
}

namespace {
std::shared_ptr<DataArray> make_help_row(Symbol button, Symbol token) {
  auto row = std::make_shared<DataArray>();
  row->push(DataNode::Sym(button));
  row->push(DataNode::Sym(token));
  return row;
}
}  // namespace

DataNode Interp::bi_text_entry_help(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  auto out = k.size() > 1 ? eval(*k[1], env).as_array() : nullptr;
  if (!out) out = std::make_shared<DataArray>();
  out->resize(0);

  Object* ten = k.size() > 2 ? node_to_object(eval(*k[2], env), env) : nullptr;
  const bool back = k.size() > 3 && truthy(eval(*k[3], env));
  const bool can_scroll =
      ten && truthy(ten->handle_property(Symbol("user_can_scroll"), DataArray()));
  const bool no_text =
      !ten || truthy(ten->handle_property(Symbol("no_text_entered"), DataArray()));

  if (can_scroll)
    out->push(DataNode::Array(make_help_row(Symbol("fret1"),
                                            Symbol("help_nextletter"))));
  if (can_scroll) {
    if (no_text) {
      if (back)
        out->push(DataNode::Array(make_help_row(Symbol("fret2"),
                                                Symbol("help_back"))));
    } else {
      out->push(DataNode::Array(make_help_row(Symbol("fret2"),
                                              Symbol("help_deleteletter"))));
    }
  } else {
    out->push(DataNode::Array(make_help_row(Symbol("fret2"),
                                            Symbol("help_deleteletter"))));
  }
  if (can_scroll)
    out->push(DataNode::Array(make_help_row(Symbol("strum"),
                                            Symbol("help_updown"))));
  return DataNode::Array(out);
}

DataNode Interp::bi_autosave_goto(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2 || !env.host) return DataNode();
  DataNode target = eval(*k[1], env);
  Symbol screen;
  if (auto sym = target.as_symbol()) screen = *sym;
  else if (auto text = target.as_string()) screen = Symbol(*text);
  if (!screen.valid()) return DataNode();
  if (Object* ui = env.host->resolve_object(Symbol("ui"))) {
    DataArray args;
    args.push(DataNode::Sym(screen));
    ui->handle_property(Symbol("goto_screen"), args);
  }
  return DataNode();
}

DataNode Interp::bi_localize(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 2 || !env.host) return DataNode::Str("");
  DataNode tok = eval(*k[1], env);
  Symbol s = tok.as_symbol().value_or(Symbol(tok.as_string().value_or("")));
  return DataNode::Str(env.host->localize(s));
}

DataNode Interp::bi_print(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  for (std::size_t i = 1; i < k.size(); ++i) {
    std::string s(eval(*k[i], env).as_string().value_or(""));
    std::fprintf(stderr, "%s", s.c_str());
  }
  std::fprintf(stderr, "\n");
  return DataNode();
}

DataNode Interp::bi_script_task(const Node& c, Env& env) {
  if (!env.host) return DataNode();
  const NodeList& k = gh::dtb::children(c);
  const bool thread_task =
      !k.empty() && is_symbol(*k[0]) && node_sym(*k[0]) == Symbol("thread_task");
  float delay = 0.0f;
  NodeList script_body;
  for (std::size_t i = 1; i < k.size(); ++i) {
    if (!is_data_array(*k[i])) continue;
    const NodeList& row = gh::dtb::children(*k[i]);
    if (row.empty() || !is_symbol(*row[0])) continue;
    const Symbol key = node_sym(*row[0]);
    if (key == Symbol("delay") && row.size() >= 2) {
      delay = std::max(0.0f, eval(*row[1], env).as_float().value_or(0.0f));
    } else if (key == Symbol("script")) {
      script_body.assign(row.begin() + 1, row.end());
    }
  }
  if (script_body.empty()) return DataNode();

  if (!thread_task) {
    env.host->schedule_script_task(script_body, env.self, delay);
    return DataNode();
  }

  NodeList segment;
  float segment_delay = delay;
  auto schedule_segment = [&] {
    if (segment.empty()) return;
    env.host->schedule_script_task(segment, env.self, segment_delay);
    segment.clear();
  };

  for (const auto& node : script_body) {
    float sleep_seconds = 0.0f;
    if (node && task_sleep_seconds(*node, sleep_seconds)) {
      schedule_segment();
      segment_delay += sleep_seconds;
      continue;
    }
    segment.push_back(node);
  }
  schedule_segment();
  return DataNode();
}

DataNode Interp::bi_new(const Node& c, Env& env) {
  const NodeList& k = gh::dtb::children(c);
  if (k.size() < 3 || !env.host || !is_symbol(*k[1]) ||
      !is_symbol(*k[2])) {
    if (env.host) env.host->on_unhandled("new?:args");
    return DataNode();
  }
  const Symbol cls = node_sym(*k[1]);
  const Symbol name = node_sym(*k[2]);
  if (Object* object = env.host->create_object(cls, name))
    return DataNode::Obj(object);
  env.host->on_unhandled(std::string("new?:") + cls.c_str());
  return DataNode();
}

}  // namespace ghogx::script
