#pragma once

#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

class IRVisitor {
 public:
  virtual ~IRVisitor() = default;

  virtual void Visit(const LiteralExpression &expr) = 0;
  virtual void Visit(const ReturnStatement &stmt) = 0;
};

}  // namespace polyglot::ir
