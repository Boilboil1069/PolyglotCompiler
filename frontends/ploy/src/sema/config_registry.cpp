// =====================================================================
// config_registry.cpp — implementation of the CONFIG package-manager
// registry described in `ploy_config_registry.h`.
// =====================================================================

#include "frontends/ploy/include/ploy_config_registry.h"

#include <algorithm>
#include <cctype>

namespace polyglot::ploy {

namespace {

std::string ToLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// Build the immutable registry table once at first use.  Entries are
// matched in insertion order, which also defines the canonical
// language / manager spelling reported by diagnostics.
const std::vector<ConfigManagerEntry> &Table() {
  static const std::vector<ConfigManagerEntry> kEntries = {
      // Python managers — historically keyword-driven, now stringified.
      {VenvConfigDecl::ManagerKind::kVenv,    "python",     "venv"},
      {VenvConfigDecl::ManagerKind::kConda,   "python",     "conda"},
      {VenvConfigDecl::ManagerKind::kUv,      "python",     "uv"},
      {VenvConfigDecl::ManagerKind::kPipenv,  "python",     "pipenv"},
      {VenvConfigDecl::ManagerKind::kPoetry,  "python",     "poetry"},
      // Other languages.
      {VenvConfigDecl::ManagerKind::kCargo,   "rust",       "cargo"},
      {VenvConfigDecl::ManagerKind::kNpm,     "javascript", "npm"},
      {VenvConfigDecl::ManagerKind::kNpm,     "typescript", "npm"},
      {VenvConfigDecl::ManagerKind::kMaven,   "java",       "maven"},
      {VenvConfigDecl::ManagerKind::kNuget,   "dotnet",     "nuget"},
      {VenvConfigDecl::ManagerKind::kNuget,   "csharp",     "nuget"},
      {VenvConfigDecl::ManagerKind::kBundler, "ruby",       "bundler"},
      {VenvConfigDecl::ManagerKind::kGoMod,   "go",         "gomod"},
  };
  return kEntries;
}

} // namespace

std::optional<ConfigManagerEntry> ResolveConfigManager(std::string_view language,
                                                       std::string_view manager_name) {
  const std::string lang = ToLower(language);
  const std::string mgr = ToLower(manager_name);
  for (const auto &entry : Table()) {
    if (entry.language == lang && entry.manager_name == mgr) {
      return entry;
    }
  }
  return std::nullopt;
}

std::optional<std::string> LegacyConfigKeywordToManagerName(std::string_view keyword) {
  const std::string up = [&] {
    std::string s;
    s.reserve(keyword.size());
    for (char c : keyword) {
      s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return s;
  }();
  if (up == "VENV")   return std::string{"venv"};
  if (up == "CONDA")  return std::string{"conda"};
  if (up == "UV")     return std::string{"uv"};
  if (up == "PIPENV") return std::string{"pipenv"};
  if (up == "POETRY") return std::string{"poetry"};
  return std::nullopt;
}

std::string_view ConfigManagerKindName(VenvConfigDecl::ManagerKind kind) {
  switch (kind) {
  case VenvConfigDecl::ManagerKind::kVenv:    return "venv";
  case VenvConfigDecl::ManagerKind::kConda:   return "conda";
  case VenvConfigDecl::ManagerKind::kUv:      return "uv";
  case VenvConfigDecl::ManagerKind::kPipenv:  return "pipenv";
  case VenvConfigDecl::ManagerKind::kPoetry:  return "poetry";
  case VenvConfigDecl::ManagerKind::kNpm:     return "npm";
  case VenvConfigDecl::ManagerKind::kCargo:   return "cargo";
  case VenvConfigDecl::ManagerKind::kMaven:   return "maven";
  case VenvConfigDecl::ManagerKind::kNuget:   return "nuget";
  case VenvConfigDecl::ManagerKind::kBundler: return "bundler";
  case VenvConfigDecl::ManagerKind::kGoMod:   return "gomod";
  case VenvConfigDecl::ManagerKind::kUnknown: return "unknown";
  }
  return "unknown";
}

const std::vector<ConfigManagerEntry> &AllConfigManagerEntries() { return Table(); }

} // namespace polyglot::ploy
