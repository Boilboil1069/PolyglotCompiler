/**
 * @file     java_lowering.h
 * @brief    Java language frontend
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/java/include/java_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::java {

// Lower a parsed+analyzed Java module into IR.
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

} // namespace polyglot::java
