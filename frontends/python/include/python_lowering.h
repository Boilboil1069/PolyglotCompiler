/**
 * @file     python_lowering.h
 * @brief    Python language frontend
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>

#include "frontends/python/include/python_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::python {

// Lower Python AST to IR
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

} // namespace polyglot::python
