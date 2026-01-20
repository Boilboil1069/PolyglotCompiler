#include <iostream>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ssa.h"

using namespace polyglot::ir;

int main() {
  IRContext ctx;
  auto fn = ctx.CreateFunction("sample");
  auto *entry = fn->CreateBlock("entry");
  auto *loop = fn->CreateBlock("loop");
  auto *exit = fn->CreateBlock("exit");

  auto init = std::make_shared<AssignInstruction>();
  init->name = "i";
  init->type = IRType::I64();
  init->operands = {};
  entry->AddInstruction(init);

  auto to_loop = std::make_shared<BranchStatement>();
  to_loop->target = loop;
  entry->SetTerminator(to_loop);

  auto inc = std::make_shared<BinaryInstruction>();
  inc->op = BinaryInstruction::Op::kAdd;
  inc->operands = {"i", "one"};
  inc->name = "i";  // redefines i pre-SSA
  inc->type = IRType::I64();
  loop->AddInstruction(inc);

  auto cond = std::make_shared<CondBranchStatement>();
  cond->operands = {"i"};
  cond->true_target = loop;
  cond->false_target = exit;
  loop->SetTerminator(cond);

  auto ret = std::make_shared<ReturnStatement>();
  ret->operands = {"i"};
  exit->SetTerminator(ret);

  ConvertToSSA(*fn);

  std::cout << "Blocks:\n";
  for (auto &bb_ptr : fn->blocks) {
    auto *bb = bb_ptr.get();
    std::cout << "  " << bb->name << "\n";
    for (auto &phi : bb->phis) {
      std::cout << "    phi " << phi->name << " =";
      for (auto &inc : phi->incomings) {
        std::cout << " [" << inc.first->name << ": " << inc.second << "]";
      }
      std::cout << "\n";
    }
    for (auto &inst : bb->instructions) {
      std::cout << "    inst " << inst->name << "\n";
    }
  }
  return 0;
}
