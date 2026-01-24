#pragma once

#include <iosfwd>
#include <string>

#include "middle/include/ir/ir_context.h"

namespace polyglot::ir {

void PrintFunction(const Function &func, std::ostream &os);
void PrintModule(const IRContext &ctx, std::ostream &os);
std::string Dump(const Function &func);

}  // namespace polyglot::ir
