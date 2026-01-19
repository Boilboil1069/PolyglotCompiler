#pragma once

#include <memory>

#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

struct Expression {
  virtual ~Expression() = default;
  IRType type{IRType::Void()};
};

struct LiteralExpression : Expression {
  int value{0};
};

}  // namespace polyglot::ir
