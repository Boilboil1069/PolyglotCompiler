#include "common/include/ir/ir_builder.h"

#include <utility>
#include <unordered_map>

namespace polyglot::ir {

namespace {
IRType ComputeGEPType(const IRType &base_type, const std::vector<size_t> &indices) {
  IRType base_ptr =
      (base_type.kind == IRTypeKind::kPointer || base_type.kind == IRTypeKind::kReference) ? base_type
                                                                                            : IRType::Pointer(base_type);
  return ResolveGEPResultType(base_ptr, indices);
}
}

std::shared_ptr<BasicBlock> IRBuilder::GetOrCreateEntryBlock() {
  return CurrentBlock();
}

std::shared_ptr<BasicBlock> IRBuilder::CurrentBlock() {
  if (insert_block_) return insert_block_;
  auto fn = context_.DefaultFunction();
  auto bb = context_.DefaultBlock();
  if (!fn->entry) fn->entry = bb.get();
  insert_block_ = bb;
  return insert_block_;
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

std::shared_ptr<LiteralExpression> IRBuilder::MakeLiteral(double value, const std::string &name) {
  auto lit = std::make_shared<LiteralExpression>(value);
  lit->name = name.empty() ? NextTempName("cf") : name;
  // Literals are values; no instruction stream needed.
  return lit;
}

std::shared_ptr<AllocaInstruction> IRBuilder::MakeAlloca(const IRType &type, const std::string &name) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<AllocaInstruction>();
  inst->type = IRType::Pointer(type);
  inst->name = name.empty() ? NextTempName("p") : name;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<LoadInstruction> IRBuilder::MakeLoad(const std::string &addr, const IRType &type,
                                                    const std::string &name, size_t align) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<LoadInstruction>();
  inst->operands = {addr};
  inst->type = type;
  inst->name = name.empty() ? NextTempName("ld") : name;
  inst->align = align;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<StoreInstruction> IRBuilder::MakeStore(const std::string &addr, const std::string &value,
                                                      size_t align) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<StoreInstruction>();
  inst->operands = {addr, value};
  inst->align = align;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<CastInstruction> IRBuilder::MakeCast(CastInstruction::CastKind kind,
                                                    const std::string &value,
                                                    const IRType &dest_type,
                                                    const std::string &name) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<CastInstruction>();
  inst->cast = kind;
  inst->operands = {value};
  inst->type = dest_type;
  inst->name = name.empty() ? NextTempName("cast") : name;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<CallInstruction> IRBuilder::MakeCall(const std::string &callee,
                                                    const std::vector<std::string> &args,
                                                    const IRType &ret_type,
                                                    const std::string &name,
                                                    const IRType &fn_type,
                                                    bool is_vararg) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<CallInstruction>();
  inst->callee = callee;
  inst->operands = args;
  inst->type = ret_type;
  inst->callee_type = fn_type;
  inst->is_vararg = is_vararg;
  inst->name = ret_type.kind == IRTypeKind::kVoid ? "" : (name.empty() ? NextTempName("call") : name);
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<GetElementPtrInstruction> IRBuilder::MakeGEP(const std::string &base,
                                                            const IRType &base_type,
                                                            const std::vector<size_t> &indices,
                                                            const std::string &name) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<GetElementPtrInstruction>();
  inst->operands = {base};
  inst->source_type = base_type;
  inst->indices = indices;
  inst->type = ComputeGEPType(base_type, indices);
  inst->name = name.empty() ? NextTempName("gep") : name;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<MemcpyInstruction> IRBuilder::MakeMemcpy(const std::string &dst, const std::string &src, const std::string &size_name, size_t align) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<MemcpyInstruction>();
  inst->operands = {dst, src, size_name};
  inst->align = align;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<MemsetInstruction> IRBuilder::MakeMemset(const std::string &dst, const std::string &value, const std::string &size_name, size_t align) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<MemsetInstruction>();
  inst->operands = {dst, value, size_name};
  inst->align = align;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<UnreachableStatement> IRBuilder::MakeUnreachable() {
  auto bb = CurrentBlock();
  auto ur = std::make_shared<UnreachableStatement>();
  ur->parent = bb.get();
  bb->SetTerminator(ur);
  return ur;
}

std::shared_ptr<Function> IRBuilder::CreateFunction(const std::string &name, const IRType &ret,
                                                    const std::vector<std::pair<std::string, IRType>> &params) {
  insert_block_.reset();  // reset insertion point for new function
  return context_.CreateFunction(name, ret, params);
}

std::shared_ptr<BinaryInstruction> IRBuilder::MakeBinary(BinaryInstruction::Op op,
                                                        const std::string &lhs,
                                                        const std::string &rhs,
                                                        const std::string &result) {
  auto bb = CurrentBlock();
  auto inst = std::make_shared<BinaryInstruction>();
  inst->op = op;
  inst->operands = {lhs, rhs};
  inst->name = result.empty() ? NextTempName("v") : result;
  inst->parent = bb.get();
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<Statement> IRBuilder::MakeReturn(const std::string &value_name) {
  auto bb = CurrentBlock();
  auto ret = std::make_shared<ReturnStatement>();
  if (!value_name.empty()) ret->operands.push_back(value_name);
  ret->parent = bb.get();
  bb->SetTerminator(ret);
  return ret;
}

std::shared_ptr<BranchStatement> IRBuilder::MakeBranch(BasicBlock *target) {
  auto bb = CurrentBlock();
  auto br = std::make_shared<BranchStatement>();
  br->target = target;
  br->parent = bb.get();
  bb->SetTerminator(br);
  return br;
}

std::shared_ptr<CondBranchStatement> IRBuilder::MakeCondBranch(const std::string &cond,
                                                              BasicBlock *true_bb,
                                                              BasicBlock *false_bb) {
  auto bb = CurrentBlock();
  auto br = std::make_shared<CondBranchStatement>();
  br->operands = {cond};
  br->true_target = true_bb;
  br->false_target = false_bb;
  br->parent = bb.get();
  bb->SetTerminator(br);
  return br;
}

std::shared_ptr<SwitchStatement> IRBuilder::MakeSwitch(const std::string &value,
                                                      const std::vector<SwitchStatement::Case> &cases,
                                                      BasicBlock *default_bb) {
  auto bb = CurrentBlock();
  auto sw = std::make_shared<SwitchStatement>();
  sw->operands = {value};
  sw->cases = cases;
  sw->default_target = default_bb;
  sw->parent = bb.get();
  bb->SetTerminator(sw);
  return sw;
}

std::shared_ptr<BasicBlock> IRBuilder::CreateBlock(const std::string &name) {
  GetOrCreateEntryBlock();  // ensure default function exists
  auto target_fn = context_.DefaultFunction();
  auto bb = std::make_shared<BasicBlock>();
  bb->name = name;
  target_fn->blocks.push_back(bb);
  return bb;
}

std::string IRBuilder::MakeStringLiteral(const std::string &text, const std::string &hint) {
  struct Interned {
    std::string data_name;
    std::string addr_name;
  };
  static std::unordered_map<std::string, Interned> interned;
  auto it = interned.find(text);
  if (it != interned.end()) return it->second.addr_name;

  std::string data_name = hint + std::to_string(interned.size());
  auto cs = std::make_shared<ConstantString>(text);
  auto data_gv = context_.CreateGlobal(data_name, cs->type, true, text, cs);

  std::string addr_name = data_name + ".ptr";
  std::vector<size_t> idx = {0, 0};
  auto gep_init = std::make_shared<ConstantGEP>(data_gv, idx);
  context_.CreateGlobal(addr_name, gep_init->type, true, "", gep_init);

  interned[text] = {data_name, addr_name};
  return addr_name;
}

}  // namespace polyglot::ir
