/**
 * @file     backend_registry.cpp
 * @brief    BackendRegistry singleton implementation
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/common/include/backend_registry.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace polyglot::backends {

namespace {

// JSON-escape an ASCII string.  We only ever serialise short identifiers
// (triples, descriptions) so the limited subset is sufficient.
std::string JsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
        out += buf;
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}

std::string JsonBool(bool b) { return b ? "true" : "false"; }

} // namespace

// ============================================================================
// Singleton access
// ============================================================================

BackendRegistry &BackendRegistry::Instance() {
  static BackendRegistry instance;
  return instance;
}

// ============================================================================
// Register
// ============================================================================

RegisterStatus BackendRegistry::Register(std::shared_ptr<ITargetBackend> backend) {
  if (!backend) {
    return RegisterStatus::kNullBackend;
  }

  const std::string canonical = AsciiToLower(backend->TargetTriple());

  std::lock_guard<std::mutex> lock(mutex_);

  if (backends_.find(canonical) != backends_.end()) {
    return RegisterStatus::kDuplicateTriple;
  }

  // Pre-validate aliases before mutating state.
  std::vector<std::string> normalised_aliases;
  normalised_aliases.reserve(backend->Aliases().size());
  for (const auto &alias : backend->Aliases()) {
    auto key = AsciiToLower(alias);
    if (key.empty()) {
      continue;
    }
    if (alias_map_.find(key) != alias_map_.end() && alias_map_.at(key) != canonical) {
      return RegisterStatus::kAliasConflict;
    }
    if (backends_.find(key) != backends_.end() && key != canonical) {
      return RegisterStatus::kAliasConflict;
    }
    normalised_aliases.push_back(std::move(key));
  }

  backends_.emplace(canonical, std::move(backend));
  for (auto &alias : normalised_aliases) {
    alias_map_.emplace(std::move(alias), canonical);
  }
  return RegisterStatus::kOk;
}

// ============================================================================
// Find
// ============================================================================

ITargetBackend *BackendRegistry::Find(const std::string &triple_or_alias) const {
  if (triple_or_alias.empty()) {
    return nullptr;
  }
  const std::string key = AsciiToLower(triple_or_alias);

  std::lock_guard<std::mutex> lock(mutex_);

  auto direct = backends_.find(key);
  if (direct != backends_.end()) {
    return direct->second.get();
  }

  auto alias = alias_map_.find(key);
  if (alias != alias_map_.end()) {
    auto canonical = backends_.find(alias->second);
    if (canonical != backends_.end()) {
      return canonical->second.get();
    }
  }
  return nullptr;
}

ITargetBackend *BackendRegistry::FindOrDiagnose(const std::string &triple_or_alias,
                                                std::string *out_diagnostic) const {
  ITargetBackend *backend = Find(triple_or_alias);
  if (backend) {
    return backend;
  }
  if (out_diagnostic) {
    std::ostringstream oss;
    oss << "no backend registered for target '" << triple_or_alias << "'. ";
    auto infos = List();
    if (infos.empty()) {
      oss << "No backends are currently registered.";
    } else {
      oss << "Available backends:";
      for (const auto &info : infos) {
        oss << "\n  - " << info.triple;
        if (!info.aliases.empty()) {
          oss << " (aliases:";
          for (const auto &a : info.aliases) {
            oss << ' ' << a;
          }
          oss << ')';
        }
      }
    }
    *out_diagnostic = oss.str();
  }
  return nullptr;
}

// ============================================================================
// Listing
// ============================================================================

std::vector<BackendInfo> BackendRegistry::List() const {
  std::vector<BackendInfo> result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(backends_.size());
    for (const auto &entry : backends_) {
      result.push_back(MakeBackendInfo(*entry.second));
    }
  }
  std::sort(result.begin(), result.end(),
            [](const BackendInfo &a, const BackendInfo &b) { return a.triple < b.triple; });
  return result;
}

std::size_t BackendRegistry::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return backends_.size();
}

void BackendRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  backends_.clear();
  alias_map_.clear();
}

// ============================================================================
// BackendRegistrar
// ============================================================================

BackendRegistrar::BackendRegistrar(std::shared_ptr<ITargetBackend> backend) {
  // We deliberately ignore the registration status here: collisions during
  // static initialisation indicate a programming error in a backend
  // translation unit and would otherwise be invisible at startup.  In a
  // debug build the registry's behaviour can be inspected directly via
  // BackendRegistry::Instance().Size() / List().
  (void)BackendRegistry::Instance().Register(std::move(backend));
}

// ============================================================================
// JSON / human-readable serialisation
// ============================================================================

std::string ToJson(const BackendInfo &info) {
  std::ostringstream oss;
  oss << "{\"triple\":\"" << JsonEscape(info.triple) << "\",";
  oss << "\"description\":\"" << JsonEscape(info.description) << "\",";
  oss << "\"available\":" << JsonBool(info.available) << ',';
  oss << "\"aliases\":[";
  for (std::size_t i = 0; i < info.aliases.size(); ++i) {
    if (i)
      oss << ',';
    oss << '"' << JsonEscape(info.aliases[i]) << '"';
  }
  oss << "],";
  oss << "\"capabilities\":{";
  oss << "\"emits_object\":" << JsonBool(info.capabilities.emits_object) << ',';
  oss << "\"emits_assembly\":" << JsonBool(info.capabilities.emits_assembly) << ',';
  oss << "\"emits_bitcode\":" << JsonBool(info.capabilities.emits_bitcode) << ',';
  oss << "\"supports_debug_info\":" << JsonBool(info.capabilities.supports_debug_info) << ',';
  oss << "\"supports_position_independent\":"
      << JsonBool(info.capabilities.supports_position_independent) << ',';
  oss << "\"supports_jit\":" << JsonBool(info.capabilities.supports_jit) << ',';
  oss << "\"supports_linear_scan\":" << JsonBool(info.capabilities.supports_linear_scan) << ',';
  oss << "\"supports_graph_coloring\":" << JsonBool(info.capabilities.supports_graph_coloring);
  oss << "}}";
  return oss.str();
}

std::string ToJson(const std::vector<BackendInfo> &infos) {
  std::ostringstream oss;
  oss << '[';
  for (std::size_t i = 0; i < infos.size(); ++i) {
    if (i)
      oss << ',';
    oss << ToJson(infos[i]);
  }
  oss << ']';
  return oss.str();
}

std::string ToHumanReadable(const BackendInfo &info) {
  std::ostringstream oss;
  oss << "  triple      : " << info.triple << '\n';
  if (!info.description.empty()) {
    oss << "  description : " << info.description << '\n';
  }
  oss << "  available   : " << (info.available ? "yes" : "no") << '\n';
  if (!info.aliases.empty()) {
    oss << "  aliases     :";
    for (const auto &a : info.aliases) {
      oss << ' ' << a;
    }
    oss << '\n';
  }
  oss << "  capabilities:\n";
  auto cap_line = [&oss](const char *name, bool v) {
    oss << "    " << name << " : " << (v ? "yes" : "no") << '\n';
  };
  cap_line("emits_object        ", info.capabilities.emits_object);
  cap_line("emits_assembly      ", info.capabilities.emits_assembly);
  cap_line("emits_bitcode       ", info.capabilities.emits_bitcode);
  cap_line("supports_debug_info ", info.capabilities.supports_debug_info);
  cap_line("position_independent", info.capabilities.supports_position_independent);
  cap_line("jit                 ", info.capabilities.supports_jit);
  cap_line("regalloc:linear-scan", info.capabilities.supports_linear_scan);
  cap_line("regalloc:graph-color", info.capabilities.supports_graph_coloring);
  return oss.str();
}

} // namespace polyglot::backends
