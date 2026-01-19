#pragma once

#include "common/include/core/symbols.h"
#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::frontends {

class SemaContext {
 public:
  explicit SemaContext(Diagnostics &diagnostics) : diagnostics_(diagnostics) {}

  core::SymbolTable &Symbols() { return symbols_; }
  core::TypeSystem &Types() { return types_; }
  Diagnostics &Diags() { return diagnostics_; }

 private:
  Diagnostics &diagnostics_;
  core::SymbolTable symbols_{};
  core::TypeSystem types_{};
};

}  // namespace polyglot::frontends
