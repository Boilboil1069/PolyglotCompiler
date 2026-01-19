#pragma once

#include <memory>

#include "middle/include/ir/nodes/expressions.h"

namespace polyglot::ir {

struct Statement {
  virtual ~Statement() = default;
};

struct ReturnStatement : Statement {
  std::shared_ptr<Expression> value;
};

}  // namespace polyglot::ir
