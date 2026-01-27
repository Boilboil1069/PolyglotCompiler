#include <iostream>

#include "middle/include/ir/ir_context.h"
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/passes/transform/inlining.h"

namespace polyglot::tools {

void Optimize(ir::IRContext &context) {
  passes::transform::RunConstantFold(context);
  passes::transform::RunDeadCodeElimination(context);
  passes::transform::RunCommonSubexpressionElimination(context);
  passes::transform::RunInlining(context);
}

}  // namespace polyglot::tools

int main() {
  polyglot::ir::IRContext ctx;
  polyglot::tools::Optimize(ctx);
  std::cout << "optimized\n";
  return 0;
}
