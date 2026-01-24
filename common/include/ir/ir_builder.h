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
  std::shared_ptr<AllocaInstruction> MakeAlloca(const IRType &type, const std::string &name = "");
  std::shared_ptr<LoadInstruction> MakeLoad(const std::string &addr, const IRType &type,
                                            const std::string &name = "");
  std::shared_ptr<StoreInstruction> MakeStore(const std::string &addr, const std::string &value);
  std::shared_ptr<CastInstruction> MakeCast(CastInstruction::CastKind kind, const std::string &value,
                                            const IRType &dest_type, const std::string &name = "");
  std::shared_ptr<CallInstruction> MakeCall(const std::string &callee,
                                            const std::vector<std::string> &args,
                                            const IRType &ret_type,
                                            const std::string &name = "");
  std::shared_ptr<GetElementPtrInstruction> MakeGEP(const std::string &base,
                                                    const IRType &base_type,
                                                    const std::vector<size_t> &indices,
                                                    const std::string &name = "");
  // Intern a string literal as a global and return the pointer-valued symbol name.
  std::string MakeStringLiteral(const std::string &text, const std::string &hint = "str");
  std::shared_ptr<Statement> MakeReturn(const std::string &value_name = "");
  std::shared_ptr<BranchStatement> MakeBranch(BasicBlock *target);
  std::shared_ptr<CondBranchStatement> MakeCondBranch(const std::string &cond,
                                                      BasicBlock *true_bb,
                                                      BasicBlock *false_bb);
  std::shared_ptr<SwitchStatement> MakeSwitch(const std::string &value,
                                              const std::vector<SwitchStatement::Case> &cases,
                                              BasicBlock *default_bb);

  std::shared_ptr<BasicBlock> CreateBlock(const std::string &name);
  std::shared_ptr<Function> CurrentFunction() { return context_.DefaultFunction(); }

 private:
  std::string NextTempName(const std::string &hint);
  IRContext &context_;
  size_t temp_index_{0};
};

}  // namespace polyglot::ir
