#include "middle/include/ir/ssa.h"

#include <stack>
#include <vector>

namespace polyglot::ir {

static std::unordered_map<std::string, std::unordered_set<BasicBlock *>>
CollectDefSites(Function &func) {
  std::unordered_map<std::string, std::unordered_set<BasicBlock *>> defsites;
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      if (!phi->name.empty()) defsites[phi->name].insert(bb);
    }
    for (auto &inst : bb->instructions) {
      if (inst->HasResult()) defsites[inst->name].insert(bb);
      if (auto *assign = dynamic_cast<AssignInstruction *>(inst.get())) {
        if (assign->HasResult()) defsites[assign->name].insert(bb);
      }
    }
  }
  return defsites;
}

void InsertPhiNodes(Function &func, const DominanceFrontier &df,
                    const std::unordered_map<std::string, std::unordered_set<BasicBlock *>> &defsites) {
  for (const auto &[var, blocks] : defsites) {
    std::unordered_set<BasicBlock *> has_already;
    std::vector<BasicBlock *> worklist(blocks.begin(), blocks.end());
    while (!worklist.empty()) {
      auto *x = worklist.back();
      worklist.pop_back();
      auto it = df.find(x);
      if (it == df.end()) continue;
      for (auto *y : it->second) {
        if (has_already.insert(y).second) {
          auto phi = std::make_shared<PhiInstruction>();
          phi->name = var;  // will be versioned during renaming
          // placeholder incomings for all preds
          for (auto *pred : y->predecessors) {
            phi->incomings.push_back({pred, var});
          }
          y->AddPhi(phi);
          if (blocks.find(y) == blocks.end()) {
            worklist.push_back(y);
          }
        }
      }
    }
  }
}

static void RenameBlock(BasicBlock *bb, const DominatorTree &dom_tree,
                        std::unordered_map<std::string, std::vector<int>> &stacks,
                        std::unordered_map<std::string, int> &counters) {
  auto new_name = [&](const std::string &base) {
    int &ctr = counters[base];
    int v = ctr++;
    stacks[base].push_back(v);
    return base + "_" + std::to_string(v);
  };

  auto current_top = [&](const std::string &base) -> std::string {
    auto it = stacks.find(base);
    if (it == stacks.end() || it->second.empty()) return base;  // fallback
    int v = it->second.back();
    return base + "_" + std::to_string(v);
  };

  // rename phi results first
  for (auto &phi : bb->phis) {
    if (!phi->name.empty()) {
      phi->name = new_name(phi->name);
    }
    for (auto &inc : phi->incomings) {
      inc.second = current_top(inc.second);
    }
  }

  for (auto &inst : bb->instructions) {
    // rename operands
    for (auto &op : inst->operands) {
      op = current_top(op);
    }
    if (auto *phi = dynamic_cast<PhiInstruction *>(inst.get())) {
      for (auto &inc : phi->incomings) {
        inc.second = current_top(inc.second);
      }
    }
    if (inst->HasResult()) {
      inst->name = new_name(inst->name);
    }
  }

  // rename operands in terminator (if any)
  if (bb->terminator) {
    for (auto &op : bb->terminator->operands) {
      op = current_top(op);
    }
  }

  // update phi operands in successors
  for (auto *succ : bb->successors) {
    for (auto &phi : succ->phis) {
      for (auto &inc : phi->incomings) {
        if (inc.first == bb) {
          inc.second = current_top(inc.second);
        }
      }
    }
  }

  // recurse on dom-tree children
  auto child_it = dom_tree.children.find(bb);
  if (child_it != dom_tree.children.end()) {
    for (auto *child : child_it->second) {
      RenameBlock(child, dom_tree, stacks, counters);
    }
  }

  // pop definitions from this block
  // Re-scan block to pop in reverse order for each definition.
  for (auto it = bb->instructions.rbegin(); it != bb->instructions.rend(); ++it) {
    if ((*it)->HasResult()) {
      auto base_end = (*it)->name.find_last_of('_');
      if (base_end != std::string::npos) {
        std::string base = (*it)->name.substr(0, base_end);
        if (!stacks[base].empty()) stacks[base].pop_back();
      }
    }
  }
  for (auto it = bb->phis.rbegin(); it != bb->phis.rend(); ++it) {
    if (!(*it)->name.empty()) {
      auto base_end = (*it)->name.find_last_of('_');
      if (base_end != std::string::npos) {
        std::string base = (*it)->name.substr(0, base_end);
        if (!stacks[base].empty()) stacks[base].pop_back();
      }
    }
  }
}

void RenameToSSA(Function &func, const DominatorTree &dom_tree) {
  if (!func.entry) return;
  std::unordered_map<std::string, std::vector<int>> stacks;
  std::unordered_map<std::string, int> counters;
  RenameBlock(func.entry, dom_tree, stacks, counters);
}

void ConvertToSSA(Function &func) {
  auto cfg = BuildCFG(func);
  auto dom = ComputeDominators(cfg);
  auto df = ComputeDominanceFrontier(cfg, dom);
  auto defs = CollectDefSites(func);
  InsertPhiNodes(func, df, defs);
  RenameToSSA(func, dom);
}

}  // namespace polyglot::ir
