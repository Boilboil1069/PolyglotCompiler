#pragma once

#include <memory>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

class IRBuilder {
 public:
  explicit IRBuilder(IRContext &context) : context_(context) {}

  std::shared_ptr<Expression> MakeLiteral(int value);
  std::shared_ptr<Statement> MakeReturn(std::shared_ptr<Expression> value);

 private:
  IRContext &context_;
};

}  // namespace polyglot::ir
