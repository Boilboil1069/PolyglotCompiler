#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/source_loc.h"
#include "common/include/core/types.h"

namespace polyglot::core {

enum class SymbolKind { kVariable, kFunction, kTypeName, kModule, kParameter, kField };

enum class ScopeKind { kGlobal, kModule, kFunction, kClass, kBlock, kComprehension };

struct Symbol {
  std::string name;
  Type type{Type::Invalid()};
  SourceLoc loc{};
  SymbolKind kind{SymbolKind::kVariable};
  std::string language;
  int scope_id{-1};
  bool captured{false};
};

struct ScopeInfo {
  int id{-1};
  int parent{-1};
  ScopeKind kind{ScopeKind::kBlock};
  std::string name;
};

class SymbolTable {
 public:
  SymbolTable() { EnterScope("<global>", ScopeKind::kGlobal); }

  int EnterScope(const std::string &name, ScopeKind kind) {
    int id = static_cast<int>(scopes_.size());
    int parent = scope_stack_.empty() ? -1 : scope_stack_.back();
    scopes_.push_back(ScopeInfo{id, parent, kind, name});
    scope_stack_.push_back(id);
    return id;
  }

  void ExitScope() {
    if (!scope_stack_.empty()) {
      scope_stack_.pop_back();
    }
  }

  // Declare in current scope; returns nullptr if duplicate.
  const Symbol *Declare(const Symbol &symbol) {
    if (scope_stack_.empty()) return nullptr;
    int current = scope_stack_.back();
    auto &slot = symbols_by_scope_[current];
    if (slot.count(symbol.name) > 0) {
      return nullptr;
    }
    Symbol stored = symbol;
    stored.scope_id = current;
    symbols_.push_back(std::move(stored));
    const Symbol *ptr = &symbols_.back();
    slot[symbol.name] = ptr;
    return ptr;
  }

  // Declare directly into a given scope id (used for globals/nonlocals).
  const Symbol *DeclareInScope(int scope_id, const Symbol &symbol) {
    if (scope_id < 0 || scope_id >= static_cast<int>(scopes_.size())) return nullptr;
    auto &slot = symbols_by_scope_[scope_id];
    if (slot.count(symbol.name) > 0) return nullptr;
    Symbol stored = symbol;
    stored.scope_id = scope_id;
    symbols_.push_back(std::move(stored));
    const Symbol *ptr = &symbols_.back();
    slot[symbol.name] = ptr;
    return ptr;
  }

  struct ResolveResult {
    const Symbol *symbol{nullptr};
    int scope_distance{-1};  // 0 = same scope, 1 = parent, ...
  };

  std::optional<ResolveResult> Lookup(const std::string &name) const {
    int distance = 0;
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it, ++distance) {
      int scope_id = *it;
      auto found_scope = symbols_by_scope_.find(scope_id);
      if (found_scope == symbols_by_scope_.end()) continue;
      auto found_symbol = found_scope->second.find(name);
      if (found_symbol != found_scope->second.end()) {
        return ResolveResult{found_symbol->second, distance};
      }
    }
    return std::nullopt;
  }

  // Non-stack-aware search across all scopes (primarily for testing/introspection).
  const Symbol *FindInAnyScope(const std::string &name) const {
    for (auto it = symbols_.rbegin(); it != symbols_.rend(); ++it) {
      if (it->name == name) return &*it;
    }
    return nullptr;
  }

  const ScopeInfo *CurrentScope() const {
    if (scope_stack_.empty()) return nullptr;
    return &scopes_[scope_stack_.back()];
  }

  int CurrentScopeId() const { return scope_stack_.empty() ? -1 : scope_stack_.back(); }

  int GlobalScopeId() const { return scopes_.empty() ? -1 : 0; }

  const ScopeInfo *ParentScope(int scope_id) const {
    if (scope_id < 0 || scope_id >= static_cast<int>(scopes_.size())) return nullptr;
    int parent = scopes_[scope_id].parent;
    if (parent < 0) return nullptr;
    return &scopes_[parent];
  }

  // Mark symbol as captured when resolved from an outer function scope.
  void MarkCaptured(const Symbol *sym) {
    if (!sym) return;
    const_cast<Symbol *>(sym)->captured = true;
  }

 private:
  std::vector<ScopeInfo> scopes_{};
  std::vector<int> scope_stack_{};
  std::unordered_map<int, std::unordered_map<std::string, const Symbol *>> symbols_by_scope_{};
  std::vector<Symbol> symbols_{};  // owns storage
};

}  // namespace polyglot::core
