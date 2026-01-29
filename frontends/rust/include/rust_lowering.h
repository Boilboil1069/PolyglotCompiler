#pragma once

#include <memory>

#include "frontends/rust/include/rust_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::rust {

// Lower Rust AST to IR
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags);

} // namespace polyglot::rust
