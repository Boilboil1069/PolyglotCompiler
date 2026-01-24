#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"

// Minimal toy "frontend" lowering demo: lower a+b into IR with a call and return.
// This is not a full parser, just a smoke test for the IR builder and SSA/passes pipeline.

using namespace polyglot::ir;

int main() {
  IRContext ctx;
  IRBuilder b(ctx);
  auto fn = ctx.DefaultFunction();
  fn->name = "cpp_lowering_stub";

  // Pretend we parsed: int add(int a, int b) { int c = a + b; return foo(c); }
  fn->params = {"a", "b"};

  auto sum = b.MakeBinary(BinaryInstruction::Op::kAdd, "a", "b", "c");
  b.MakeCall("foo", {sum->name}, IRType::I32(), "retv");
  b.MakeReturn("retv");

  // SSA & verify
  ConvertToSSA(*fn);
  std::string err;
  if (!Verify(*fn, &err)) {
    std::cerr << "verify failed: " << err << "\n";
    return 1;
  }
  PrintFunction(*fn, std::cout);
  return 0;
}
