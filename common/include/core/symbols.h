#pragma once

#include <deque>
#include <limits>
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
  std::string name{};
  Type type{Type::Invalid()};
  SourceLoc loc{};
  SymbolKind kind{SymbolKind::kVariable};
  std::string language{};
  int scope_id{-1};
  bool captured{false};
  std::string access{};  // "public"/"protected"/"private" (empty = default)
};

struct ScopeInfo {
  int id{-1};
  int parent{-1};
  ScopeKind kind{ScopeKind::kBlock};
  std::string name{};
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
    // Functions may be overloaded; keep a vector but also preserve first-declared for simple lookup.
    if (symbol.kind != SymbolKind::kFunction) {
      if (slot.count(symbol.name) > 0) {
        return nullptr;
      }
    }
    Symbol stored = symbol;
    stored.scope_id = current;
    symbols_.push_back(std::move(stored));
    const Symbol *ptr = &symbols_.back();
    slot[symbol.name] = ptr;
    if (symbol.kind == SymbolKind::kFunction) {
      functions_by_scope_[current][symbol.name].push_back(ptr);
    }
    return ptr;
  }

  // Declare directly into a given scope id (used for globals/nonlocals).
  const Symbol *DeclareInScope(int scope_id, const Symbol &symbol) {
    if (scope_id < 0 || scope_id >= static_cast<int>(scopes_.size())) return nullptr;
    auto &slot = symbols_by_scope_[scope_id];
    if (symbol.kind != SymbolKind::kFunction) {
      if (slot.count(symbol.name) > 0) return nullptr;
    }
    Symbol stored = symbol;
    stored.scope_id = scope_id;
    symbols_.push_back(std::move(stored));
    const Symbol *ptr = &symbols_.back();
    slot[symbol.name] = ptr;
    if (symbol.kind == SymbolKind::kFunction) {
      functions_by_scope_[scope_id][symbol.name].push_back(ptr);
    }
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

  // Resolve an overloaded function by argument types using a basic compatibility score.
  std::optional<ResolveResult> ResolveFunction(const std::string &name,
                                               const std::vector<Type> &arg_types,
                                               const TypeSystem &types) const {
    int distance = 0;
    auto conv_score = [&](const Type &from, const Type &to) -> int {
      // Lowest is best. Large sentinel means not convertible.
      if (types.IsCompatible(from, to)) return 0;
      // Reference binding preferences
      if (from.kind == TypeKind::kReference && to.kind == TypeKind::kReference) {
        if (types.IsCompatible(from.type_args.empty() ? Type::Any() : from.type_args[0],
                               to.type_args.empty() ? Type::Any() : to.type_args[0])) {
          // allow adding const when binding
          if (!from.is_const && to.is_const) return 1;
        }
      }
      // Bind T to const T& (temporary/lvalue binding) has a small penalty
      if (to.kind == TypeKind::kReference && to.is_const) {
        if (types.IsCompatible(from, to.type_args.empty() ? Type::Any() : to.type_args[0])) {
          return 2;
        }
      }
      // Prefer cv-qualification / widening implicit conversions over user-defined
      if (types.CanImplicitlyConvert(from, to)) return 3;

      // Very coarse user-defined conversion heuristic: allow class/struct to other class/struct in
      // the same language with a high penalty to keep it lowest priority.
      auto is_object = [](const Type &t) {
        return t.kind == TypeKind::kClass || t.kind == TypeKind::kStruct || t.kind == TypeKind::kGenericInstance;
      };
      if (is_object(from) && is_object(to) && (from.language == to.language)) return 10;

      return std::numeric_limits<int>::max() / 4;
    };

    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it, ++distance) {
      int scope_id = *it;
      auto scope_it = functions_by_scope_.find(scope_id);
      if (scope_it == functions_by_scope_.end()) continue;
      auto fn_it = scope_it->second.find(name);
      if (fn_it == scope_it->second.end()) continue;
      const Symbol *best = nullptr;
      int best_score = std::numeric_limits<int>::max();
      for (const Symbol *cand : fn_it->second) {
        if (!cand || cand->type.type_args.size() < 1) continue;
        size_t param_count = cand->type.type_args.size() - 1;
        if (param_count != arg_types.size()) continue;
        int score = 0;
        bool compatible = true;
        for (size_t i = 0; i < arg_types.size(); ++i) {
          const auto &expected = cand->type.type_args[i + 1];
          int s = conv_score(arg_types[i], expected);
          if (s >= (std::numeric_limits<int>::max() / 8)) {
            compatible = false;
            break;
          }
          score += s;
        }
        if (!compatible) continue;
        if (score < best_score) {
          best_score = score;
          best = cand;
        }
      }
      if (best) return ResolveResult{best, distance};
    }
    return std::nullopt;
  }

  void RegisterTypeScope(const std::string &name, int scope_id) {
    if (scope_id >= 0) {
      type_scopes_[name] = scope_id;
      scope_to_type_[scope_id] = name;
    }
  }

  void RegisterTypeBases(const std::string &name, std::vector<std::string> bases) {
    type_bases_[name] = std::move(bases);
  }

  std::optional<ResolveResult> LookupMember(const std::string &type_name,
                                            const std::string &member) const {
    auto it = type_scopes_.find(type_name);
    if (it == type_scopes_.end()) return std::nullopt;
    int scope_id = it->second;
    auto res = LookupMemberInScope(scope_id, member);
    if (res) return res;
    // search bases recursively
    auto b = type_bases_.find(type_name);
    if (b != type_bases_.end()) {
      for (const auto &base : b->second) {
        auto base_res = LookupMember(base, member);
        if (base_res) return base_res;
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
  std::optional<ResolveResult> LookupMemberInScope(int scope_id, const std::string &member) const {
    auto scope_symbols = symbols_by_scope_.find(scope_id);
    if (scope_symbols == symbols_by_scope_.end()) return std::nullopt;
    auto found = scope_symbols->second.find(member);
    if (found == scope_symbols->second.end()) return std::nullopt;
    // access control: allow if current scope is same type or member is not private
    const Symbol *sym = found->second;
    if (!IsAccessible(sym, scope_id)) return std::nullopt;
    return ResolveResult{sym, 0};
  }

  bool IsAccessible(const Symbol *sym, int declaring_scope) const {
    if (!sym) return false;
    if (sym->access.empty() || sym->access == "public") return true;
    // Get current type name if inside a type scope
    std::string current_type;
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
      auto s = *it;
      auto t = scope_to_type_.find(s);
      if (t != scope_to_type_.end()) { current_type = t->second; break; }
    }
    std::string declaring_type;
    auto itype = scope_to_type_.find(declaring_scope);
    if (itype != scope_to_type_.end()) declaring_type = itype->second;

    if (sym->access == "private") {
      return !current_type.empty() && current_type == declaring_type;
    }
    if (sym->access == "protected") {
      if (!current_type.empty() && current_type == declaring_type) return true;
      if (!current_type.empty() && !declaring_type.empty()) {
        return IsDerivedFrom(current_type, declaring_type);
      }
    }
    return false;
  }

  bool IsDerivedFrom(const std::string &type, const std::string &base) const {
    if (type == base) return true;
    auto it = type_bases_.find(type);
    if (it == type_bases_.end()) return false;
    for (const auto &b : it->second) {
      if (b == base) return true;
      if (IsDerivedFrom(b, base)) return true;
    }
    return false;
  }

  std::vector<ScopeInfo> scopes_{};
  std::vector<int> scope_stack_{};
  std::unordered_map<int, std::unordered_map<std::string, const Symbol *>> symbols_by_scope_{};
  std::unordered_map<int, std::unordered_map<std::string, std::vector<const Symbol *>>> functions_by_scope_{};
  std::unordered_map<std::string, int> type_scopes_{};
  std::unordered_map<std::string, std::vector<std::string>> type_bases_{};
  std::unordered_map<int, std::string> scope_to_type_{};
  std::deque<Symbol> symbols_{};  // owns storage; deque guarantees pointer stability
};

// ============================================================================
// Free-standing utility functions (implemented in symbol_table.cpp)
// ============================================================================

/// Convert a SymbolKind enum to its human-readable string name.
std::string SymbolKindToString(SymbolKind kind);

/// Convert a ScopeKind enum to its human-readable string name.
std::string ScopeKindToString(ScopeKind kind);

/// Format a Symbol for diagnostic output (includes type, language, access, location).
std::string FormatSymbol(const Symbol &sym);

/// Format a ScopeInfo for diagnostic output.
std::string FormatScope(const ScopeInfo &scope);

}  // namespace polyglot::core
