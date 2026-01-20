#pragma once

#include <memory>
#include <string>

#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

// Base value in the unified IR. Every SSA value inherits from this.
struct Value {
  virtual ~Value() = default;
  IRType type{IRType::Invalid()};
  std::string name;  // SSA name (may be empty pre-SSA)
};

// Integer literal.
struct LiteralExpression : Value {
  long long value{0};

  explicit LiteralExpression(long long v = 0) : value(v) { type = IRType::I64(); }
};

// Placeholder for uninitialized/undefined values.
struct UndefValue : Value {
  UndefValue() { type = IRType::Invalid(); name = "undef"; }
};

}  // namespace polyglot::ir
