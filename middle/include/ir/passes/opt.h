#pragma once

#include "middle/include/ir/cfg.h"

namespace polyglot::ir::passes {

void ConstantFold(Function &func);
void DeadCodeEliminate(Function &func);
void SimplifyCFG(Function &func);
void EliminateRedundantPhis(Function &func);
void CSE(Function &func);
void Mem2Reg(Function &func);  // best-effort scalar promotion

void RunDefaultOptimizations(Function &func);

}  // namespace polyglot::ir::passes
