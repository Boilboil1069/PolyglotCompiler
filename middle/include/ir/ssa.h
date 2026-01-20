#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "middle/include/ir/cfg.h"

namespace polyglot::ir {

// Convert a function into SSA form (phi insertion + renaming).
void ConvertToSSA(Function &func);

// Exposed helpers for testing or advanced pipelines.
void InsertPhiNodes(Function &func, const DominanceFrontier &df,
                    const std::unordered_map<std::string, std::unordered_set<BasicBlock *>> &defsites);
void RenameToSSA(Function &func, const DominatorTree &dom_tree);

}  // namespace polyglot::ir
