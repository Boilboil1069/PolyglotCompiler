#include "frontends/common/include/sema_context.h"

#include "frontends/python/include/python_ast.h"

namespace polyglot::python {

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
  for (const auto &node : module.body) {
    auto ident = std::dynamic_pointer_cast<Identifier>(node);
    if (!ident) {
      continue;
    }
    core::Symbol symbol{ident->name, context.Types().String(), ident->loc};
    if (!context.Symbols().Declare(symbol)) {
      context.Diags().Report(ident->loc, "Duplicate symbol: " + ident->name);
    }
  }
}

}  // namespace polyglot::python
