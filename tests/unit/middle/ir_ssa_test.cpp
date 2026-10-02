#include <catch2/catch_test_macros.hpp>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ssa.h"

using namespace polyglot::ir;

namespace {
std::shared_ptr<AssignInstruction> MakeAssign(BasicBlock *bb, const std::string &name, const std::vector<std::string> &operands, IRType type = IRType::Invalid()) {
  auto inst = std::make_shared<AssignInstruction>();
  inst->name = name;
  inst->operands = operands;
  inst->type = type;
  inst->parent = bb;
  bb->AddInstruction(inst);
  return inst;
}

std::shared_ptr<CondBranchStatement> MakeCond(BasicBlock *bb, const std::string &cond, BasicBlock *t, BasicBlock *f) {
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

#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/verifier.h"

TEST_CASE("IRBuilder phi uses the verified edge collection and preserves literals", "[ir][ssa][regression]") {
  IRContext context; IRBuilder builder(context);
  auto fn = builder.CreateFunction("choose", IRType::I1(), {{"condition", IRType::I1()}});
  builder.SetCurrentFunction(fn);
  auto entry = builder.CreateBlock("entry"), right = builder.CreateBlock("right"), merge = builder.CreateBlock("merge");
  builder.SetInsertPoint(entry); builder.MakeCondBranch("condition", right.get(), merge.get());
  builder.SetInsertPoint(right); builder.MakeBranch(merge.get());
  builder.SetInsertPoint(merge);
  auto phi = builder.MakePhi(IRType::I1(), {{entry.get(), "0"}, {right.get(), "1"}}, "result");
  builder.MakeReturn(phi->name);
  REQUIRE(merge->phis.size() == 1); CHECK(merge->instructions.empty());
  ConvertToSSA(*fn);
  REQUIRE(merge->phis.size() == 1);
  CHECK(merge->phis[0]->incomings[0].second == "0");
  CHECK(merge->phis[0]->incomings[1].second == "1");
  std::string error; CHECK(Verify(context, &error)); INFO(error);
}

TEST_CASE("Module verification retains writable global pointer types", "[ir][verifier][globals]") {
  IRContext context; IRBuilder builder(context);
  context.CreateGlobal("counter", IRType::I64(), false, "0", std::make_shared<LiteralExpression>(0LL));
  auto fn = builder.CreateFunction("update", IRType::I64(), {{"value", IRType::I64()}});
  builder.SetCurrentFunction(fn); builder.SetInsertPoint(builder.CreateBlock("entry"));
  builder.MakeStore("counter", "value");
  auto loaded = builder.MakeLoad("counter", IRType::I64()); builder.MakeReturn(loaded->name);
  std::string error;
  CHECK(Verify(context, &error)); INFO(error);
  CHECK_FALSE(Verify(*fn, &error)); // no global symbol table in standalone verification
}

TEST_CASE("Verifier rejects a literal-looking name without a definition", "[ir][verifier][constants]") {
  IRContext context; IRBuilder builder(context);
  auto fn = builder.CreateFunction("missing", IRType::I64(), {});
  builder.SetCurrentFunction(fn); builder.SetInsertPoint(builder.CreateBlock("entry"));
  builder.MakeReturn("c123");
  std::string error; CHECK_FALSE(Verify(context, &error));
  CHECK(error.find("undefined value") != std::string::npos);
}

TEST_CASE("Floating negation bitcasts preserve negative zero", "[ir][verifier][float]") {
  CHECK(IRType::F64().CanBitcastTo(IRType::I64(false)));
  CHECK(IRType::I32(false).CanBitcastTo(IRType::F32()));
  CHECK_FALSE(IRType::F64().CanBitcastTo(IRType::I32()));
  IRContext context; IRBuilder builder(context);
  auto fn = builder.CreateFunction("negative", IRType::F64(), {{"value", IRType::F64()}});
  builder.SetCurrentFunction(fn); builder.SetInsertPoint(builder.CreateBlock("entry"));
  auto result = builder.MakeFloatNegate("value", IRType::F64()); builder.MakeReturn(result->name);
  std::string error; INFO(error); CHECK(Verify(context, &error));
}

TEST_CASE("Generated IR names cannot shadow parameters or earlier function values", "[ir][builder][names]") {
  IRContext context; IRBuilder builder(context);
  auto fn = builder.CreateFunction("collision", IRType::I64(), {{"truth", IRType::I64()}, {"c0", IRType::I64()}});
  builder.SetCurrentFunction(fn); auto entry = builder.CreateBlock("entry"); builder.SetInsertPoint(entry);
  auto literal = builder.MakeLiteral(1LL);
  CHECK(literal->name != "c0");
  auto result = builder.MakeBinary(BinaryInstruction::Op::kAdd, "truth", literal->name, "truth");
  CHECK(result->name != "truth");
  auto other = builder.CreateFunction("other", IRType::Void(), {});
  builder.SetCurrentFunction(other); builder.SetInsertPoint(builder.CreateBlock("entry")); builder.MakeReturn();
  builder.SetCurrentFunction(fn); builder.SetInsertPoint(entry);
  auto extra = builder.MakeLiteral(2LL, result->name);
  CHECK(extra->name != result->name);
  builder.MakeReturn(result->name);
  std::string error; INFO(error); CHECK(Verify(context, &error));
}
