#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_ast.h"

namespace polyglot::cpp {

// Perform a lightweight semantic pass: build symbol scopes, basic type mapping, and mark captures.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::cpp
