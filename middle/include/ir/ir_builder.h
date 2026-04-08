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
  std::shared_ptr<LiteralExpression> MakeLiteral(double value, const std::string &name = "");
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
  
  // Dynamic GEP with runtime-computed index for array access
  // Returns a pointer to the element at the given dynamic index
  std::shared_ptr<BinaryInstruction> MakeDynamicGEP(const std::string &base,
                                                    const IRType &elem_type,
                                                    const std::string &index,
                                                    const std::string &name = "");
  
  std::shared_ptr<MemcpyInstruction> MakeMemcpy(const std::string &dst, const std::string &src, const std::string &size_name, size_t align = 0);
  std::shared_ptr<MemsetInstruction> MakeMemset(const std::string &dst, const std::string &value, const std::string &size_name, size_t align = 0);
  std::shared_ptr<UnreachableStatement> MakeUnreachable();

  // Exception handling: invoke a callee with normal and unwind destinations.
  // Unlike MakeCall, this terminates the current block.
  std::shared_ptr<InvokeInstruction> MakeInvoke(const std::string &callee,
                                                 const std::vector<std::string> &args,
                                                 const IRType &ret_type,
                                                 BasicBlock *normal_dest,
                                                 BasicBlock *unwind_dest,
                                                 const std::string &name = "");

  // Create a landing pad instruction at the start of an unwind destination block.
  std::shared_ptr<LandingPadInstruction> MakeLandingPad(bool is_cleanup,
                                                         const std::vector<IRType> &catch_types = {},
                                                         const std::string &name = "");

  // Resume unwinding after a cleanup landing pad.
  std::shared_ptr<ResumeInstruction> MakeResume(const std::string &value = "");

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
  
  // Create a PHI instruction for SSA form
  // The incomings vector contains pairs of (predecessor block, value name)
  std::shared_ptr<PhiInstruction> MakePhi(const IRType &type, 
                                          const std::vector<std::pair<BasicBlock*, std::string>> &incomings,
                                          const std::string &name = "");
  
  // Add an incoming edge to an existing PHI instruction
  void AddPhiIncoming(PhiInstruction *phi, BasicBlock *pred, const std::string &value);

  std::shared_ptr<BasicBlock> CreateBlock(const std::string &name);

  // Set the active function that CreateBlock will add blocks to.
  // When set, CreateBlock targets this function instead of the default one.
  void SetCurrentFunction(const std::shared_ptr<Function> &fn) { active_function_ = fn; }
  void ClearCurrentFunction() { active_function_.reset(); }
  std::shared_ptr<Function> CurrentFunction() {
      return active_function_ ? active_function_ : context_.DefaultFunction();
  }

 private:
  std::shared_ptr<BasicBlock> CurrentBlock();
  std::string NextTempName(const std::string &hint);
  IRContext &context_;
  size_t temp_index_{0};
  std::shared_ptr<BasicBlock> insert_block_{};
  std::shared_ptr<Function> active_function_{};
};

}  // namespace polyglot::ir
