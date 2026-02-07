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
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

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
  std::ostringstream os;
  os << SymbolKindToString(sym.kind) << " '" << sym.name << "'";
  os << " : " << sym.type.ToString();
  if (!sym.language.empty()) os << " [" << sym.language << "]";
  if (!sym.access.empty()) os << " (" << sym.access << ")";
  if (sym.captured) os << " [captured]";
  if (sym.scope_id >= 0) os << " @scope=" << sym.scope_id;
  if (!sym.loc.file.empty()) {
    os << " at " << sym.loc.file << ":" << sym.loc.line << ":" << sym.loc.column;
  }
  return os.str();
}

std::string FormatScope(const ScopeInfo &scope) {
  std::ostringstream os;
  os << ScopeKindToString(scope.kind) << " '" << scope.name << "'";
  os << " (id=" << scope.id << ", parent=" << scope.parent << ")";
  return os.str();
}

}  // namespace polyglot::core
