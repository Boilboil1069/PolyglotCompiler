#pragma once

#include <string>

#include "middle/include/ir/ir_context.h"

namespace polyglot::ir {

// Returns true if IR is well-formed. Optional message will contain first error.
bool Verify(const Function &func, std::string *msg = nullptr);
bool Verify(const IRContext &ctx, std::string *msg = nullptr);

}  // namespace polyglot::ir
