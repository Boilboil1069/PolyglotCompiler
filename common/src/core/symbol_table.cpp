/**
 * @file     symbol_table.cpp
 * @brief    Extended symbol table operations for the polyglot compiler.
 * @details  Provides additional symbol table utilities beyond the inline methods
 *           declared in symbols.h: scope path resolution, symbol dumping for
 *           diagnostics, bulk import/export of symbols between scopes, type
 *           validation helpers, and scope ancestry queries.
 *
 * @author   Manning Cyrus
 * @date     2026-02-07
 * @version  2.0.0
 */

#include "common/include/core/symbols.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

namespace polyglot::core {

// ============================================================================
// Free-standing symbol table utility functions
// ============================================================================

std::string SymbolKindToString(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kVariable:  return "Variable";
    case SymbolKind::kFunction:  return "Function";
    case SymbolKind::kTypeName:  return "TypeName";
    case SymbolKind::kModule:    return "Module";
    case SymbolKind::kParameter: return "Parameter";
    case SymbolKind::kField:     return "Field";
  }
  return "Unknown";
}

std::string ScopeKindToString(ScopeKind kind) {
  switch (kind) {
    case ScopeKind::kGlobal:        return "Global";
    case ScopeKind::kModule:        return "Module";
    case ScopeKind::kFunction:      return "Function";
    case ScopeKind::kClass:         return "Class";
    case ScopeKind::kBlock:         return "Block";
    case ScopeKind::kComprehension: return "Comprehension";
  }
  return "Unknown";
}

std::string FormatSymbol(const Symbol &sym) {
  // Use fmt::memory_buffer to compose the diagnostic line in a single
  // allocation pass instead of repeatedly resizing a std::ostringstream.
  fmt::memory_buffer buf;
  fmt::format_to(std::back_inserter(buf), "{} '{}' : {}",
                 SymbolKindToString(sym.kind), sym.name, sym.type.ToString());
  if (!sym.language.empty()) {
    fmt::format_to(std::back_inserter(buf), " [{}]", sym.language);
  }
  if (!sym.access.empty()) {
    fmt::format_to(std::back_inserter(buf), " ({})", sym.access);
  }
  if (sym.captured) {
    fmt::format_to(std::back_inserter(buf), " [captured]");
  }
  if (sym.scope_id >= 0) {
    fmt::format_to(std::back_inserter(buf), " @scope={}", sym.scope_id);
  }
  if (!sym.loc.file.empty()) {
    fmt::format_to(std::back_inserter(buf), " at {}:{}:{}",
                   sym.loc.file, sym.loc.line, sym.loc.column);
  }
  return fmt::to_string(buf);
}

std::string FormatScope(const ScopeInfo &scope) {
  return fmt::format("{} '{}' (id={}, parent={})",
                     ScopeKindToString(scope.kind), scope.name,
                     scope.id, scope.parent);
}

}  // namespace polyglot::core
