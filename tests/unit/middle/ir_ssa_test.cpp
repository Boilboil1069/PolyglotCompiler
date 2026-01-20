#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ssa.h"

using namespace polyglot::ir;

namespace {
std::shared_ptr<AssignInstruction> MakeAssign(BasicBlock *bb, const std::string &name,
                                             const std::vector<std::string> &operands,
                                             IRType type = IRType::Invalid()) {
  auto inst = std::make_shared<AssignInstruction>();
  inst->name = name;
  inst->operands = operands;
  inst->type = type;
  inst->parent = bb;
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<CondBranchStatement> MakeCond(BasicBlock *bb, const std::string &cond,
                                             BasicBlock *t, BasicBlock *f) {
  auto br = std::make_shared<CondBranchStatement>();
  br->operands = {cond};
  br->true_target = t;
  br->false_target = f;
  br->parent = bb;
  bb->SetTerminator(br);
  return br;
}

std::shared_ptr<BranchStatement> MakeJump(BasicBlock *bb, BasicBlock *target) {
  auto br = std::make_shared<BranchStatement>();
  br->target = target;
  br->parent = bb;
  bb->SetTerminator(br);
  return br;
}
}  // namespace

TEST_CASE("SSA inserts phi for multi-def variable", "[ir][ssa]") {
  IRContext ctx;
  auto fn = ctx.CreateFunction("ssa_test");
  auto *entry = fn->CreateBlock("entry");
  auto *then_bb = fn->CreateBlock("then");
  auto *else_bb = fn->CreateBlock("else");
  auto *merge = fn->CreateBlock("merge");

  // Pre-SSA definitions of x in then/else paths.
  MakeAssign(entry, "cond", {}, IRType::I1());
  MakeCond(entry, "cond", then_bb, else_bb);
  MakeAssign(then_bb, "x", {"cond"}, IRType::I64());
  MakeJump(then_bb, merge);
  MakeAssign(else_bb, "x", {}, IRType::I64());
  MakeJump(else_bb, merge);

  auto ret = std::make_shared<ReturnStatement>();
  ret->operands = {"x"};
  ret->parent = merge;
  merge->SetTerminator(ret);

  ConvertToSSA(*fn);

  REQUIRE(merge->phis.size() == 1);
  auto phi = merge->phis.front();
  REQUIRE(phi->incomings.size() == 2);
  // Names are versioned (x_0, x_1, phi result x_2, etc.)
  REQUIRE(phi->name.find("x_") == 0);
  for (auto &inc : phi->incomings) {
    REQUIRE(inc.second.find("x_") == 0);
  }
}

TEST_CASE("Dominance handles unreachable blocks", "[ir][cfg][dom]") {
  IRContext ctx;
  auto fn = ctx.CreateFunction("unreach");
  auto *entry = fn->CreateBlock("entry");
  auto *dead = fn->CreateBlock("dead");

  // Entry just returns; dead block is unreachable and empty.
  auto ret = std::make_shared<ReturnStatement>();
  entry->SetTerminator(ret);

  auto cfg = BuildCFG(*fn);
  auto dom = ComputeDominators(cfg);
  auto df = ComputeDominanceFrontier(cfg, dom);

  REQUIRE(cfg.entry == entry);
  // Entry should dominate itself.
  REQUIRE(dom.idom[entry] == entry);
  // No dominance frontier for single-block graph; unreachable block absent.
  REQUIRE(df.find(entry) == df.end());
  REQUIRE(dom.idom.find(dead) == dom.idom.end());
}
