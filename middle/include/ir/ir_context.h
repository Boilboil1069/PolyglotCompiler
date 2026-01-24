#pragma once

#include <memory>
#include <string>
#include <vector>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

class IRContext {
 public:
  std::shared_ptr<Function> CreateFunction(const std::string &name);
  std::shared_ptr<GlobalValue> CreateGlobal(const std::string &name, const IRType &type,
                                           bool is_const = false, const std::string &init = "",
                                           std::shared_ptr<Value> initializer = nullptr);

  // Convenience: ensure a default function/block exist for simple builders.
  std::shared_ptr<Function> DefaultFunction();
  std::shared_ptr<BasicBlock> DefaultBlock();

  void AddStatement(const std::shared_ptr<Statement> &stmt);

  const std::vector<std::shared_ptr<Function>> &Functions() const { return functions_; }
  const std::vector<std::shared_ptr<GlobalValue>> &Globals() const { return globals_; }

 private:
  std::vector<std::shared_ptr<Function>> functions_{};
  std::shared_ptr<Function> default_function_{};
  std::shared_ptr<BasicBlock> default_block_{};
  std::vector<std::shared_ptr<GlobalValue>> globals_{};
};

}  // namespace polyglot::ir
