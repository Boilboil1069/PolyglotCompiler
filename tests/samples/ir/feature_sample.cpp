#include <iostream>

#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/ir/verifier.h"

using namespace polyglot::ir;

int main() {
  IRContext ctx;
  IRBuilder b(ctx);
  auto fn = ctx.DefaultFunction();
  fn->name = "feature_demo";

  // Globals
  ctx.CreateGlobal("G0", IRType::I64(), true, "42");

  // Build a small program: alloca, store, load, call, cast, switch
  auto p = b.MakeAlloca(IRType::I64(), "p0");
  b.MakeStore(p->name, "1");
  auto v = b.MakeLoad(p->name, IRType::I64(), "v0");
  b.MakeCast(CastInstruction::CastKind::kZExt, v->name, IRType::I64(), "v1");
  b.MakeCall("foo", {v->name}, IRType::Void());

  // Switch terminator
  auto default_bb = b.CreateBlock("default");
  auto case1 = b.CreateBlock("case1");
  SwitchStatement::Case c1{1, case1.get()};
  b.MakeSwitch(v->name, {c1}, default_bb.get());

  // simple returns
  auto ret1 = std::make_shared<ReturnStatement>();
  ret1->operands = {v->name};
  case1->SetTerminator(ret1);

  auto ret0 = std::make_shared<ReturnStatement>();
  ret0->operands = {v->name};
  default_bb->SetTerminator(ret0);

  // run passes
  polyglot::ir::passes::RunDefaultOptimizations(*fn);

  // verify
  std::string err;
  if (!Verify(*fn, &err)) {
    std::cerr << "Verification failed: " << err << "\n";
    return 1;
  }

  PrintModule(ctx, std::cout);
  return 0;
}
