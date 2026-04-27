/**
 * @file     ruby_lowering.h
 * @brief    Ruby → Polyglot IR lowering
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "middle/include/ir/ir_context.h"

#include "frontends/common/include/diagnostics.h"
#include "frontends/ruby/include/ruby_ast.h"

namespace polyglot::ruby {
void LowerToIR(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag);
} // namespace polyglot::ruby
