#pragma once

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

void RunDeadCodeElimination(ir::IRContext &context);

}  // namespace polyglot::passes::transform
