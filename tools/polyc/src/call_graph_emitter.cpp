/**
 * @file     call_graph_emitter.cpp
 * @brief    Emit static call-graph + symbol id JSON
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/polyc/include/call_graph_emitter.h"

#include <set>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::tools::polyc {

namespace {

void EscapeJson(std::ostringstream &os, const std::string &s) {
  os << '"';
  for (char c : s) {
    switch (c) {
    case '"':
      os << "\\\"";
      break;
    case '\\':
      os << "\\\\";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\r':
      os << "\\r";
      break;
    case '\t':
      os << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
        os << buf;
      } else {
        os << c;
      }
    }
  }
  os << '"';
}

// Best-effort language detection from an IR function name.  Bridge
// stubs preserve the host language as a "::" prefix segment in their
// canonical name; native poly functions are tagged "poly".
std::string DetectLanguage(const ir::Function &fn, const std::string &source_path = {}) {
  if (fn.is_bridge_stub) {
    return "bridge";
  }
  const auto pos = fn.name.find("::");
  if (pos != std::string::npos) {
    const auto prefix = fn.name.substr(0, pos);
    static const std::set<std::string> languages{"poly", "cpp", "python", "rust", "go", "java", "dotnet", "javascript", "ruby"};
    if (languages.count(prefix)) return prefix;
  }
  static const std::unordered_map<std::string, std::string> extensions{
      {".cpp", "cpp"}, {".cc", "cpp"}, {".c", "cpp"}, {".py", "python"},
      {".rs", "rust"}, {".go", "go"}, {".java", "java"}, {".cs", "dotnet"},
      {".js", "javascript"}, {".rb", "ruby"}, {".poly", "poly"}, {".ploy", "poly"}};
  const auto ext = std::filesystem::path(source_path).extension().string();
  if (auto found = extensions.find(ext); found != extensions.end()) return found->second;
  return "unknown";
}

} // namespace

std::string EmitCallGraphJson(const ir::IRContext &context, const std::string &source_path) {
  // Resolve every direct callee before emitting nodes. An edge must never
  // reference a synthetic id absent from the node list.
  std::unordered_map<std::string, std::size_t> ids;
  std::vector<std::string> names;
  std::vector<const ir::Function *> functions;
  const auto add = [&](const std::string &name, const ir::Function *fn) {
    if (ids.count(name)) return;
    ids[name] = names.size(); names.push_back(name); functions.push_back(fn);
  };
  for (const auto &fn : context.Functions()) if (fn) add(fn->name, fn.get());
  for (const auto &fn : context.Functions()) if (fn)
    for (const auto &bb : fn->blocks) if (bb)
      for (const auto &inst : bb->instructions)
        if (const auto *call = dynamic_cast<const ir::CallInstruction *>(inst.get()))
          if (!call->callee.empty() && !call->is_indirect) add(call->callee, nullptr);
  const auto type_name = [](const ir::IRType &type) {
    return type.kind == ir::IRTypeKind::kInvalid || type.is_placeholder ? std::string("unknown") : type.name;
  };
  std::ostringstream os;
  os << "{\"schema\":\"polyglot.callgraph.v1\",\"source\":";
  EscapeJson(os, source_path);
  os << ",\"nodes\":[";
  for (size_t i = 0; i < names.size(); ++i) {
    if (i) os << ',';
    auto *fn = functions[i];
    const bool signature_known = fn && type_name(fn->ret_type) != "unknown" &&
        fn->param_types.size() == fn->params.size() &&
        std::all_of(fn->param_types.begin(), fn->param_types.end(),
                    [&](const auto &type) { return type_name(type) != "unknown"; });
    os << "{\"id\":" << i << ",\"name\":"; EscapeJson(os, names[i]);
    os << ",\"language\":"; EscapeJson(os, fn ? DetectLanguage(*fn, source_path) : "unknown");
    os << ",\"is_external\":" << (!fn || fn->is_external ? "true" : "false")
       << ",\"is_bridge_stub\":" << (fn && fn->is_bridge_stub ? "true" : "false")
       << ",\"block_count\":" << (fn ? fn->blocks.size() : 0)
       << ",\"signature_known\":" << (signature_known ? "true" : "false")
       << ",\"return_type\":"; EscapeJson(os, fn ? type_name(fn->ret_type) : "unknown");
    os << ",\"parameters\":[";
    if (fn) for (size_t p = 0; p < fn->params.size(); ++p) {
      if (p) os << ',';
      os << "{\"index\":" << p << ",\"name\":"; EscapeJson(os, fn->params[p]);
      os << ",\"type\":"; EscapeJson(os, p < fn->param_types.size() ? type_name(fn->param_types[p]) : "unknown");
      os << '}';
    }
    os << "]}";
  }
  os << "],\"edges\":[";
  bool first = true;
  size_t callsite = 0;
  for (size_t i = 0; i < functions.size(); ++i) {
    const auto *fn = functions[i]; if (!fn) continue;
    std::unordered_map<std::string, ir::IRType> values;
    for (size_t p = 0; p < fn->params.size(); ++p)
      if (p < fn->param_types.size()) values[fn->params[p]] = fn->param_types[p];
    for (const auto &bb : fn->blocks) if (bb)
      for (const auto &inst : bb->instructions) if (inst && !inst->name.empty()) values[inst->name] = inst->type;
    for (const auto &bb : fn->blocks) if (bb)
      for (const auto &inst : bb->instructions) {
        const auto *call = dynamic_cast<const ir::CallInstruction *>(inst.get());
        if (!call || call->callee.empty() || call->is_indirect) continue;
        if (!first) os << ','; first = false;
        const auto target = ids.at(call->callee);
        const auto *callee = functions[target];
        os << "{\"from\":" << i << ",\"to\":" << target << ",\"callsite_id\":" << callsite++
           << ",\"callee\":"; EscapeJson(os, call->callee);
        os << ",\"block\":"; EscapeJson(os, bb->name);
        os << ",\"result\":"; EscapeJson(os, call->name);
        os << ",\"result_type\":"; EscapeJson(os, type_name(call->type));
        os << ",\"arguments\":[";
        for (size_t a = 0; a < call->operands.size(); ++a) {
          if (a) os << ',';
          const auto found = values.find(call->operands[a]);
          const auto source = found == values.end() ? "unknown" : type_name(found->second);
          const auto expected = callee && a < callee->param_types.size() ? type_name(callee->param_types[a]) : "unknown";
          os << "{\"index\":" << a << ",\"value\":"; EscapeJson(os, call->operands[a]);
          os << ",\"type\":"; EscapeJson(os, source);
          os << ",\"parameter\":"; EscapeJson(os, callee && a < callee->params.size() ? callee->params[a] : "unknown");
          os << ",\"expected_type\":"; EscapeJson(os, expected);
          os << ",\"transfer\":";
          EscapeJson(os, source == "unknown" || expected == "unknown" ? "unresolved" :
                         (source == expected ? "identity" : "conversion_required"));
          os << '}';
        }
        os << "]}";
      }
  }
  os << "]}";
  return os.str();
}

std::string EmitProfileSymbolsJson(const ir::IRContext &context, const std::string &source_path) {
  std::ostringstream os;
  os << "{\"schema\":\"polyglot.profilesymbols.v1\",\"source\":";
  EscapeJson(os, source_path);
  os << ",\"symbols\":[";
  bool first = true;
  std::size_t id = 0;
  for (const auto &fn : context.Functions()) {
    if (!fn) {
      continue;
    }
    if (!first) {
      os << ',';
    }
    first = false;
    os << "{\"id\":" << id++ << ",\"qualified_name\":";
    EscapeJson(os, fn->name);
    os << ",\"language\":";
    EscapeJson(os, DetectLanguage(*fn, source_path));
    os << ",\"block_count\":" << fn->blocks.size() << '}';
  }
  os << "]}";
  return os.str();
}

} // namespace polyglot::tools::polyc
