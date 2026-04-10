/**
 * @file     common_subexpr.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/ir/passes/opt.h"

namespace polyglot::passes::transform {

void RunCommonSubexpressionElimination(ir::IRContext &context) {
  for (auto &fn : context.Functions()) {
    polyglot::ir::passes::CSE(*fn);
  }
}

}  // namespace polyglot::passes::transform
