#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/source_loc.h"
#include "common/include/core/types.h"

namespace polyglot::core {

struct Symbol {
  std::string name;
  Type type{Type::Invalid()};
  SourceLoc loc{};
};

class SymbolTable {
 public:
  SymbolTable() { EnterScope(); }

  void EnterScope() { scopes_.emplace_back(); }

  void ExitScope() {
    if (!scopes_.empty()) {
      scopes_.pop_back();
    }
  }

  bool Declare(const Symbol &symbol) {
    if (scopes_.empty()) {
      return false;
    }
    auto &current = scopes_.back();
    if (current.count(symbol.name) > 0) {
      return false;
    }
    current[symbol.name] = symbol;
    return true;
  }

  const Symbol *Lookup(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

 private:
  std::vector<std::unordered_map<std::string, Symbol>> scopes_{};
};

}  // namespace polyglot::core
