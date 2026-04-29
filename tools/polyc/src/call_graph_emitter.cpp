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
// canonical name; native ploy functions are tagged "ploy".
std::string DetectLanguage(const ir::Function &fn) {
  if (fn.is_bridge_stub) {
    return "bridge";
  }
  const auto pos = fn.name.find("::");
  if (pos != std::string::npos) {
    return fn.name.substr(0, pos);
  }
  return "ploy";
}

} // namespace

std::string EmitCallGraphJson(const ir::IRContext &context, const std::string &source_path) {
  std::ostringstream os;
  os << "{\"schema\":\"polyglot.callgraph.v1\",\"source\":";
  EscapeJson(os, source_path);

  // Build a stable id for every defined function in encounter order so
  // edges can reference numeric identifiers rather than string names
  // (cuts the IDE-side parsing cost in half on large graphs).
  std::unordered_map<std::string, std::size_t> name_to_id;
  std::vector<const ir::Function *> ordered;
  for (const auto &fn : context.Functions()) {
    if (!fn) {
      continue;
    }
    if (name_to_id.count(fn->name)) {
      continue;
    }
    name_to_id[fn->name] = ordered.size();
    ordered.push_back(fn.get());
  }

  os << ",\"nodes\":[";
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    const auto *fn = ordered[i];
    if (i)
      os << ',';
    os << "{\"id\":" << i << ",\"name\":";
    EscapeJson(os, fn->name);
    os << ",\"language\":";
    EscapeJson(os, DetectLanguage(*fn));
    os << ",\"is_external\":" << (fn->is_external ? "true" : "false");
    os << ",\"is_bridge_stub\":" << (fn->is_bridge_stub ? "true" : "false");
    os << ",\"block_count\":" << fn->blocks.size() << '}';
  }
  os << "],\"edges\":[";

  bool first_edge = true;
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    const auto *fn = ordered[i];
    std::set<std::string> seen_callees; // dedupe parallel edges per caller
    for (const auto &bb : fn->blocks) {
      if (!bb) {
        continue;
      }
      for (const auto &inst : bb->instructions) {
        const ir::CallInstruction *call = dynamic_cast<const ir::CallInstruction *>(inst.get());
        if (!call) {
          continue;
        }
        if (call->callee.empty() || call->is_indirect) {
          continue;
        }
        if (!seen_callees.insert(call->callee).second) {
          continue;
        }
        std::size_t target_id;
        auto it = name_to_id.find(call->callee);
        if (it == name_to_id.end()) {
          // Synthesise an external node id on the fly so the edge list
          // remains self-contained.
          target_id = ordered.size() + name_to_id.size();
          name_to_id[call->callee] = target_id;
        } else {
          target_id = it->second;
        }
        if (!first_edge) {
          os << ',';
        }
        first_edge = false;
        os << "{\"from\":" << i << ",\"to\":" << target_id << ",\"callee\":";
        EscapeJson(os, call->callee);
        os << '}';
      }
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
    EscapeJson(os, DetectLanguage(*fn));
    os << ",\"block_count\":" << fn->blocks.size() << '}';
  }
  os << "]}";
  return os.str();
}

} // namespace polyglot::tools::polyc
