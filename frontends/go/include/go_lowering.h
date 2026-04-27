/**
 * @file     go_lowering.h
 * @brief    Go → Polyglot IR lowering entry point
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "middle/include/ir/ir_context.h"

#include "frontends/common/include/diagnostics.h"
#include "frontends/go/include/go_ast.h"

namespace polyglot::go {

void LowerToIR(const File &file, ir::IRContext &ctx, frontends::Diagnostics &d);

} // namespace polyglot::go
