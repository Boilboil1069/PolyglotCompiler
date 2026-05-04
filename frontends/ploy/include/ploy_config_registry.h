// =====================================================================
// ploy_config_registry.h
//
// Registry mapping `(language, package_manager_string)` pairs to the
// `VenvConfigDecl::ManagerKind` enumerator that drives package
// auto-discovery in `PloySema`.
//
// The registry is the single source of truth for the canonical form
// of `CONFIG` introduced in v1.12.0:
//
//     CONFIG <language> "<package_manager>" "<path_or_env>";
//
// Adding support for a new package manager only requires registering
// a new `(lang, manager_str) -> ManagerKind` entry — no lexer or
// parser changes are needed because the manager spelling is always a
// string literal.
//
// The legacy keyword-driven form (`CONFIG VENV "..."`, etc.) is
// translated to a registry lookup at parse time so sema sees a
// uniform representation.
// =====================================================================

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frontends/ploy/include/ploy_ast.h"

namespace polyglot::ploy {

/// Result of a registry lookup for a `(language, manager)` pair.
struct ConfigManagerEntry {
  VenvConfigDecl::ManagerKind kind{VenvConfigDecl::ManagerKind::kUnknown};
  std::string language;     // canonical language name
  std::string manager_name; // canonical manager string
};

/// Resolve a `(language, manager)` pair to its ManagerKind entry.
/// Returns `std::nullopt` if the pair is not registered.
std::optional<ConfigManagerEntry> ResolveConfigManager(std::string_view language,
                                                       std::string_view manager_name);

/// Translate a legacy `CONFIG <KEYWORD>` keyword (`"VENV"`, `"CONDA"`,
/// `"UV"`, `"PIPENV"`, `"POETRY"`) to its canonical manager-name
/// string (`"venv"`, `"conda"`, ...).  Returns an empty optional if
/// the keyword is not a known legacy alias.
std::optional<std::string> LegacyConfigKeywordToManagerName(std::string_view keyword);

/// Reverse map: a `ManagerKind` enumerator back to the canonical
/// manager-name string.  Used by sema and tooling when reconstructing
/// the canonical `CONFIG ...;` form for diagnostics.
std::string_view ConfigManagerKindName(VenvConfigDecl::ManagerKind kind);

/// Enumerate every `(language, manager)` pair registered at compile
/// time.  Useful for diagnostics ("did you mean ...") and tests.
const std::vector<ConfigManagerEntry> &AllConfigManagerEntries();

} // namespace polyglot::ploy
