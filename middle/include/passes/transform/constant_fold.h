/**
 * @file     constant_fold.h
 * @brief    Transformation passes
 *
 * @ingroup  Middle / Transform
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

void RunConstantFold(ir::IRContext &context);

}  // namespace polyglot::passes::transform
