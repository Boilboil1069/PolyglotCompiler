#pragma once

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

void RunConstantFold(ir::IRContext &context);

}  // namespace polyglot::passes::transform
