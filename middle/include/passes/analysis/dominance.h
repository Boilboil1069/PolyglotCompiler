#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/analysis.h"

namespace polyglot::passes::analysis {

// Public analysis API wrapping ir::DominatorTree / ir::DominanceFrontier.
// Provides a convenient interface for querying dominance relationships
// without direct dependency on the internal CFG builder.
struct DominanceInfo {
  // Populate from a function
  void ComputeFrom(ir::Function &func) {
    ir::AnalysisCache cache(func);
    dom_tree_ = cache.GetDomTree();
    dom_frontier_ = cache.GetDomFrontier();
    valid_ = true;
  }

  // Return the immediate dominator of a block (nullptr if none / entry)
  ir::BasicBlock *GetIDom(ir::BasicBlock *bb) const {
    if (!valid_) return nullptr;
    auto it = dom_tree_.idom.find(bb);
    return it != dom_tree_.idom.end() ? it->second : nullptr;
  }

  // Return the dominator-tree children of a block
  const std::vector<ir::BasicBlock *> &GetChildren(ir::BasicBlock *bb) const {
    static const std::vector<ir::BasicBlock *> empty;
    if (!valid_) return empty;
    auto it = dom_tree_.children.find(bb);
    return it != dom_tree_.children.end() ? it->second : empty;
  }

  // Check whether block a dominates block b (walking the idom chain)
  bool Dominates(ir::BasicBlock *a, ir::BasicBlock *b) const {
    if (!valid_) return false;
    if (a == b) return true;
    while (b) {
      auto it = dom_tree_.idom.find(b);
      if (it == dom_tree_.idom.end()) break;
      b = it->second;
      if (b == a) return true;
    }
    return false;
  }

  // Return the dominance frontier for a given block
  std::unordered_set<ir::BasicBlock *> GetFrontier(ir::BasicBlock *bb) const {
    if (!valid_) return {};
    auto it = dom_frontier_.find(bb);
    return it != dom_frontier_.end() ? it->second
                                     : std::unordered_set<ir::BasicBlock *>{};
  }

  // Access raw trees for advanced usage
  const ir::DominatorTree &GetDomTree() const { return dom_tree_; }
  const ir::DominanceFrontier &GetDomFrontier() const { return dom_frontier_; }

  bool IsValid() const { return valid_; }

private:
  ir::DominatorTree dom_tree_;
  ir::DominanceFrontier dom_frontier_;
  bool valid_{false};
};

}  // namespace polyglot::passes::analysis
