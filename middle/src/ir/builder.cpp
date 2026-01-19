#include "common/include/ir/ir_builder.h"

namespace polyglot::ir {

std::shared_ptr<Expression> IRBuilder::MakeLiteral(int value) {
  auto literal = std::make_shared<LiteralExpression>();
  literal->value = value;
  literal->type = IRType::I32();
  return literal;
}

std::shared_ptr<Statement> IRBuilder::MakeReturn(std::shared_ptr<Expression> value) {
  auto stmt = std::make_shared<ReturnStatement>();
  stmt->value = std::move(value);
  context_.AddStatement(stmt);
  return stmt;
}

}  // namespace polyglot::ir
