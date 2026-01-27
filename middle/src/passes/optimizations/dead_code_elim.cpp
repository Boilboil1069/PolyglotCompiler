#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/ir/passes/opt.h"

namespace polyglot::passes::transform {

void RunDeadCodeElimination(ir::IRContext &context) {
  for (auto &fn : context.Functions()) {
    polyglot::ir::passes::DeadCodeEliminate(*fn);
  }
}

}  // namespace polyglot::passes::transform
