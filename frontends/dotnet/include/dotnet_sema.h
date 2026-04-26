/**
 * @file     dotnet_sema.h
 * @brief    .NET/C# language frontend
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/dotnet/include/dotnet_ast.h"

namespace polyglot::dotnet {

class AssemblyLoader;  // metadata_reader.h

// Optional configuration passed by the language frontend.
struct DotNetSemaOptions {
    // Loader used to resolve `using` directives against real ECMA-335
    // metadata.  When null, using directives are recorded as opaque module
    // symbols (legacy behaviour, kept for unit tests).
    AssemblyLoader *loader{nullptr};
};

// Perform semantic analysis on a parsed C# compilation unit.
// 2-arg overload retained for backwards compatibility with existing tests.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);
void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const DotNetSemaOptions &options);

} // namespace polyglot::dotnet
