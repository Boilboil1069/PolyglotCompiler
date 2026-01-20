#pragma once

#include <memory>
#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

class IRBuilder {
 public:
  explicit IRBuilder(IRContext &context) : context_(context) {}

  std::shared_ptr<BasicBlock> GetOrCreateEntryBlock();

  std::shared_ptr<LiteralExpression> MakeLiteral(long long value, const std::string &name = "");
  std::shared_ptr<BinaryInstruction> MakeBinary(BinaryInstruction::Op op, const std::string &lhs,
                                                const std::string &rhs, const std::string &result);
  std::shared_ptr<Statement> MakeReturn(const std::string &value_name = "");
  std::shared_ptr<BranchStatement> MakeBranch(BasicBlock *target);
  std::shared_ptr<CondBranchStatement> MakeCondBranch(const std::string &cond,
                                                      BasicBlock *true_bb,
                                                      BasicBlock *false_bb);

 private:
  std::string NextTempName(const std::string &hint);
  IRContext &context_;
  size_t temp_index_{0};
};

}  // namespace polyglot::ir
