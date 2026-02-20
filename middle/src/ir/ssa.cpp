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
                        std::unordered_map<std::string, int> &counters,
                        const std::unordered_set<std::string> &pre_defined) {
  auto new_name = [&](const std::string &base) {
    int &ctr = counters[base];
    int v = ctr++;
    stacks[base].push_back(v);
    return base + "_" + std::to_string(v);
  };

  auto current_top = [&](const std::string &base) -> std::string {
    auto it = stacks.find(base);
    if (it == stacks.end() || it->second.empty()) return base;
    int v = it->second.back();
    return base + "_" + std::to_string(v);
  };

  // rename phi results first (do NOT rename phi incomings here —
  // they are set by each predecessor in the "update phi operands in
  // successors" step below)
  for (auto &phi : bb->phis) {
    if (!phi->name.empty()) {
      phi->name = new_name(phi->name);
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
          // Check if the base name has a version on the stack
          auto sit = stacks.find(inc.second);
          if ((sit == stacks.end() || sit->second.empty()) && !pre_defined.count(inc.second)) {
            // Value is undefined on this path — mark as undef
            inc.second = "undef";
          } else {
            inc.second = current_top(inc.second);
          }
        }
      }
    }
  }

  // recurse on dom-tree children
  auto child_it = dom_tree.children.find(bb);
  if (child_it != dom_tree.children.end()) {
    for (auto *child : child_it->second) {
      RenameBlock(child, dom_tree, stacks, counters, pre_defined);
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
  std::unordered_set<std::string> pre_defined(func.params.begin(), func.params.end());
  RenameBlock(func.entry, dom_tree, stacks, counters, pre_defined);
}

// Remove trivial phis: a phi is trivial if all incoming values (excluding
// self-references) resolve to the same single value.  Such phis can be
// replaced by that value throughout the function.
static void RemoveTrivialPhis(Function &func) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto &bb_ptr : func.blocks) {
      auto *bb = bb_ptr.get();
      for (auto it = bb->phis.begin(); it != bb->phis.end(); ) {
        auto &phi = *it;
        std::string unique_val;
        bool trivial = true;
        for (auto &inc : phi->incomings) {
          if (inc.second == phi->name || inc.second == "undef") continue;  // self-reference or undef
          if (unique_val.empty()) {
            unique_val = inc.second;
          } else if (inc.second != unique_val) {
            trivial = false;
            break;
          }
        }
        if (!trivial || unique_val.empty()) {
          ++it;
          continue;
        }
        // Replace all uses of phi->name with unique_val
        const std::string old_name = phi->name;
        auto replace_in = [&](std::vector<std::string> &ops) {
          for (auto &op : ops) {
            if (op == old_name) op = unique_val;
          }
        };
        for (auto &b : func.blocks) {
          for (auto &p : b->phis) {
            for (auto &inc : p->incomings) {
              if (inc.second == old_name) inc.second = unique_val;
            }
          }
          for (auto &inst : b->instructions) {
            replace_in(inst->operands);
          }
          if (b->terminator) {
            replace_in(b->terminator->operands);
          }
        }
        it = bb->phis.erase(it);
        changed = true;
      }
    }
  }
}

void ConvertToSSA(Function &func) {
  auto cfg = BuildCFG(func);
  auto dom = ComputeDominators(cfg);
  auto df = ComputeDominanceFrontier(cfg, dom);
  auto defs = CollectDefSites(func);
  InsertPhiNodes(func, df, defs);
  RenameToSSA(func, dom);
  RemoveTrivialPhis(func);
}

}  // namespace polyglot::ir
