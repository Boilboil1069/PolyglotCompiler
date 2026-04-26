/**
 * @file     javascript_lowering.h
 * @brief    JavaScript IR lowering
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/diagnostics.h"
#include "frontends/javascript/include/javascript_ast.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::javascript {

/// Lower a parsed+analyzed JavaScript module to the unified IR.
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

}  // namespace polyglot::javascript
