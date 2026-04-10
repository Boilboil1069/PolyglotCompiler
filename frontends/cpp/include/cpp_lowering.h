/**
 * @file     cpp_lowering.h
 * @brief    C++ language frontend
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/cpp/include/cpp_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::cpp {

// Lower a parsed+analyzed C++ module into IR. Supports a minimal subset
// (functions with integer params/returns, literals, binary ops, simple vars).
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

}  // namespace polyglot::cpp
