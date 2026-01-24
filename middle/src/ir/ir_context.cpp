#include "middle/include/ir/ir_context.h"

#include <utility>

namespace polyglot::ir {

std::shared_ptr<Function> IRContext::CreateFunction(const std::string &name) {
  auto fn = std::make_shared<Function>();
  fn->name = name;
  functions_.push_back(fn);
  return fn;
}

std::shared_ptr<GlobalValue> IRContext::CreateGlobal(const std::string &name, const IRType &type,
                                                    bool is_const, const std::string &init,
                                                    std::shared_ptr<Value> initializer) {
  auto g = std::make_shared<GlobalValue>();
  g->name = name;
  g->type = type;
  g->is_const = is_const;
  g->init = init;
  g->initializer = std::move(initializer);
  globals_.push_back(g);
  return g;
}

std::shared_ptr<Function> IRContext::DefaultFunction() {
  if (!default_function_) {
    default_function_ = CreateFunction("entry_fn");
  }
  return default_function_;
}

std::shared_ptr<BasicBlock> IRContext::DefaultBlock() {
  if (!default_block_) {
    default_block_ = std::make_shared<BasicBlock>();
    default_block_->name = "entry";
    auto fn = DefaultFunction();
    fn->blocks.push_back(default_block_);
    fn->entry = default_block_.get();
  }
  return default_block_;
}

void IRContext::AddStatement(const std::shared_ptr<Statement> &stmt) {
  auto bb = DefaultBlock();
  if (stmt->IsTerminator()) {
    bb->SetTerminator(stmt);
  } else {
    bb->AddInstruction(stmt);
  }
}

}  // namespace polyglot::ir
