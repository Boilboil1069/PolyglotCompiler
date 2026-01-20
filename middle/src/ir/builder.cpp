#include "common/include/ir/ir_builder.h"

#include <utility>

namespace polyglot::ir {

std::shared_ptr<BasicBlock> IRBuilder::GetOrCreateEntryBlock() {
  auto fn = context_.DefaultFunction();
  auto bb = context_.DefaultBlock();
  if (!fn->entry) fn->entry = bb.get();
  return bb;
}

std::string IRBuilder::NextTempName(const std::string &hint) {
  return (hint.empty() ? std::string("tmp") : hint) + std::to_string(temp_index_++);
}

std::shared_ptr<LiteralExpression> IRBuilder::MakeLiteral(long long value, const std::string &name) {
  auto lit = std::make_shared<LiteralExpression>(value);
  lit->name = name.empty() ? NextTempName("c") : name;
  // Literals are values; no instruction stream needed.
  return lit;
}

std::shared_ptr<BinaryInstruction> IRBuilder::MakeBinary(BinaryInstruction::Op op,
                                                        const std::string &lhs,
                                                        const std::string &rhs,
                                                        const std::string &result) {
  auto bb = GetOrCreateEntryBlock();
  auto inst = std::make_shared<BinaryInstruction>();
  inst->op = op;
  inst->operands = {lhs, rhs};
  inst->name = result.empty() ? NextTempName("v") : result;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<Statement> IRBuilder::MakeReturn(const std::string &value_name) {
  auto bb = GetOrCreateEntryBlock();
  auto ret = std::make_shared<ReturnStatement>();
  if (!value_name.empty()) ret->operands.push_back(value_name);
  ret->parent = bb.get();
  bb->SetTerminator(ret);
  return ret;
}

std::shared_ptr<BranchStatement> IRBuilder::MakeBranch(BasicBlock *target) {
  auto bb = GetOrCreateEntryBlock();
  auto br = std::make_shared<BranchStatement>();
  br->target = target;
  br->parent = bb.get();
  bb->SetTerminator(br);
  return br;
}

std::shared_ptr<CondBranchStatement> IRBuilder::MakeCondBranch(const std::string &cond,
                                                              BasicBlock *true_bb,
                                                              BasicBlock *false_bb) {
  auto bb = GetOrCreateEntryBlock();
  auto br = std::make_shared<CondBranchStatement>();
  br->operands = {cond};
  br->true_target = true_bb;
  br->false_target = false_bb;
  br->parent = bb.get();
  bb->SetTerminator(br);
  return br;
}

}  // namespace polyglot::ir
