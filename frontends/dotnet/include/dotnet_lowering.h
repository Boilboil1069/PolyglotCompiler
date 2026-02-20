#pragma once

#include "frontends/dotnet/include/dotnet_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::dotnet {

// Lower a parsed+analyzed C# module into IR.
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

} // namespace polyglot::dotnet
