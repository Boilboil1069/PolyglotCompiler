#pragma once

#include <cstddef>
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
  void SetInsertPoint(const std::shared_ptr<BasicBlock> &block) { insert_block_ = block; }
  std::shared_ptr<BasicBlock> GetInsertPoint() const { return insert_block_; }

  std::shared_ptr<LiteralExpression> MakeLiteral(long long value, const std::string &name = "");
  std::shared_ptr<BinaryInstruction> MakeBinary(BinaryInstruction::Op op, const std::string &lhs,
                                                const std::string &rhs, const std::string &result);
  std::shared_ptr<AllocaInstruction> MakeAlloca(const IRType &type, const std::string &name = "");
  std::shared_ptr<LoadInstruction> MakeLoad(const std::string &addr, const IRType &type,
                                            const std::string &name = "", size_t align = 0);
  std::shared_ptr<StoreInstruction> MakeStore(const std::string &addr, const std::string &value,
                                              size_t align = 0);
  std::shared_ptr<CastInstruction> MakeCast(CastInstruction::CastKind kind, const std::string &value,
                                            const IRType &dest_type, const std::string &name = "");
  std::shared_ptr<CallInstruction> MakeCall(const std::string &callee,
const std::vector<std::string> &args,
                                            const IRType &ret_type,
                                            const std::string &name = "",
                                            const IRType &fn_type = IRType::Invalid(),
                                            bool is_vararg = false);
  std::shared_ptr<GetElementPtrInstruction> MakeGEP(const std::string &base, const IRType &base_type, const std::vector<size_t> &indices, const std::string &name = "");
  std::shared_ptr<MemcpyInstruction> MakeMemcpy(const std::string &dst, const std::string &src, const std::string &size_name, size_t align = 0);
  std::shared_ptr<MemsetInstruction> MakeMemset(const std::string &dst, const std::string &value, const std::string &size_name, size_t align = 0);
  std::shared_ptr<UnreachableStatement> MakeUnreachable();
  std::shared_ptr<Function> CreateFunction(const std::string &name, const IRType &ret,
                                           const std::vector<std::pair<std::string, IRType>> &params);
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
  std::shared_ptr<BasicBlock> CurrentBlock();
  std::string NextTempName(const std::string &hint);
  IRContext &context_;
  size_t temp_index_{0};
  std::shared_ptr<BasicBlock> insert_block_{};
};

}  // namespace polyglot::ir
