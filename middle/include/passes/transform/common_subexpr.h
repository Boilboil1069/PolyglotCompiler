#pragma once

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

void RunCommonSubexpressionElimination(ir::IRContext &context);

}  // namespace polyglot::passes::transform
