#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_ast.h"

namespace polyglot::python {

void AnalyzeModule(const Module &module, frontends::SemaContext &context);

}  // namespace polyglot::python
