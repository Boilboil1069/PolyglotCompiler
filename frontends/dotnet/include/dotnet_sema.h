#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/dotnet/include/dotnet_ast.h"

namespace polyglot::dotnet {

// Perform semantic analysis on a parsed C# compilation unit.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::dotnet
