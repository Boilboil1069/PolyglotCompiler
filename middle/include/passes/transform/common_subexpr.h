/**
 * @file     common_subexpr.h
 * @brief    Transformation passes
 *
 * @ingroup  Middle / Transform
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

void RunCommonSubexpressionElimination(ir::IRContext &context);

}  // namespace polyglot::passes::transform
