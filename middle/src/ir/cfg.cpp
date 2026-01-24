#include "middle/include/ir/cfg.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace polyglot::ir {

void BasicBlock::AddInstruction(const std::shared_ptr<Instruction> &inst) {
  instructions.push_back(inst);
  inst->parent = this;
}

void BasicBlock::AddPhi(const std::shared_ptr<PhiInstruction> &phi) {
  phis.push_back(phi);
  phi->parent = this;
}

void BasicBlock::SetTerminator(const std::shared_ptr<Instruction> &term) {
  terminator = term;
  term->parent = this;
}

BasicBlock *Function::CreateBlock(const std::string &block_name) {
  auto bb = std::make_shared<BasicBlock>();
  bb->name = block_name;
  blocks.push_back(bb);
  if (!entry) entry = bb.get();
  return bb.get();
}

static void ClearCFG(Function &func) {
  for (auto &bb_ptr : func.blocks) {
    bb_ptr->successors.clear();
    bb_ptr->predecessors.clear();
  }
}

ControlFlowGraph BuildCFG(Function &func) {
  ClearCFG(func);
  ControlFlowGraph cfg;
  cfg.entry = func.entry;
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    cfg.blocks.push_back(bb);
    if (!bb->terminator) continue;
    if (auto *br = dynamic_cast<BranchStatement *>(bb->terminator.get())) {
      if (br->target) {
        bb->successors.push_back(br->target);
        br->target->predecessors.push_back(bb);
      }
    } else if (auto *cbr = dynamic_cast<CondBranchStatement *>(bb->terminator.get())) {
      if (cbr->true_target) {
        bb->successors.push_back(cbr->true_target);
        cbr->true_target->predecessors.push_back(bb);
      }
      if (cbr->false_target) {
        bb->successors.push_back(cbr->false_target);
        cbr->false_target->predecessors.push_back(bb);
      }
    } else if (auto *sw = dynamic_cast<SwitchStatement *>(bb->terminator.get())) {
      for (auto &c : sw->cases) {
        if (c.target) {
          bb->successors.push_back(c.target);
          c.target->predecessors.push_back(bb);
        }
      }
      if (sw->default_target) {
        bb->successors.push_back(sw->default_target);
        sw->default_target->predecessors.push_back(bb);
      }
    }
  }
  return cfg;
}

static BasicBlock *Intersect(BasicBlock *b1, BasicBlock *b2,
                             const std::unordered_map<BasicBlock *, int> &rpo_index,
                             const std::unordered_map<BasicBlock *, BasicBlock *> &idom) {
  auto i1 = b1;
  auto i2 = b2;
  while (i1 != i2) {
    while (rpo_index.at(i1) < rpo_index.at(i2)) {
      i1 = idom.at(i1);
    }
    while (rpo_index.at(i2) < rpo_index.at(i1)) {
      i2 = idom.at(i2);
    }
  }
  return i1;
}

DominatorTree ComputeDominators(const ControlFlowGraph &cfg) {
  DominatorTree dom;
  if (!cfg.entry) return dom;

  // Reverse post-order for intersect algorithm.
  std::vector<BasicBlock *> rpo = cfg.blocks;
  // Simple DFS to order blocks.
  std::unordered_map<BasicBlock *, bool> visited;
  rpo.clear();
  std::vector<BasicBlock *> stack{cfg.entry};
  while (!stack.empty()) {
    BasicBlock *n = stack.back();
    stack.pop_back();
    if (visited[n]) continue;
    visited[n] = true;
    rpo.push_back(n);
    for (auto *succ : n->successors) {
      if (!visited[succ]) stack.push_back(succ);
    }
  }
  // reverse to get post-order then reverse => RPO
  std::reverse(rpo.begin(), rpo.end());

  std::unordered_map<BasicBlock *, int> rpo_index;
  for (size_t i = 0; i < rpo.size(); ++i) rpo_index[rpo[i]] = static_cast<int>(i);

  dom.idom[cfg.entry] = cfg.entry;
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto *b : rpo) {
      if (b == cfg.entry) continue;
      BasicBlock *new_idom = nullptr;
      for (auto *p : b->predecessors) {
        if (dom.idom.find(p) == dom.idom.end()) continue;
        if (!new_idom) {
          new_idom = p;
        } else {
          new_idom = Intersect(p, new_idom, rpo_index, dom.idom);
        }
      }
      if (!new_idom) continue;
      if (dom.idom[b] != new_idom) {
        dom.idom[b] = new_idom;
        changed = true;
      }
    }
  }

  for (auto &[node, id] : dom.idom) {
    if (node == cfg.entry) continue;
    dom.children[id].push_back(node);
  }
  return dom;
}

DominanceFrontier ComputeDominanceFrontier(const ControlFlowGraph &cfg, const DominatorTree &dom_tree) {
  DominanceFrontier df;
  for (auto *b : cfg.blocks) {
    if (b->predecessors.size() < 2) continue;
    auto idom_it = dom_tree.idom.find(b);
    if (idom_it == dom_tree.idom.end()) continue;
    BasicBlock *idom_b = idom_it->second;
    for (auto *p : b->predecessors) {
      auto runner = p;
      while (runner && runner != idom_b) {
        df[runner].insert(b);
        auto it = dom_tree.idom.find(runner);
        if (it == dom_tree.idom.end()) break;
        runner = it->second;
      }
    }
  }
  return df;
}

}  // namespace polyglot::ir
