#pragma once

#include <memory>
#include <vector>

#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

class IRContext {
 public:
  void AddStatement(std::shared_ptr<Statement> stmt) {
    statements_.push_back(std::move(stmt));
  }

  const std::vector<std::shared_ptr<Statement>> &Statements() const {
    return statements_;
  }

 private:
  std::vector<std::shared_ptr<Statement>> statements_{};
};

}  // namespace polyglot::ir
