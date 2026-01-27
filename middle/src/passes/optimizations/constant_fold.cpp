#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/ir/passes/opt.h"

namespace polyglot::passes::transform {

void RunConstantFold(ir::IRContext &context) {
  for (auto &fn : context.Functions()) {
    polyglot::ir::passes::ConstantFold(*fn);
  }
}

}  // namespace polyglot::passes::transform
