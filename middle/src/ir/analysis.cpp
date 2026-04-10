/**
 * @file     analysis.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "middle/include/ir/analysis.h"

#include <algorithm>
#include <stack>
#include <unordered_set>

#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

AliasClass AliasInfo::ClassOf(const std::string &name) const {
  auto it = classes.find(name);
  return it == classes.end() ? AliasClass::kUnknown : it->second;
}

bool AliasInfo::IsLocalStack(const std::string &name) const { return ClassOf(name) == AliasClass::kLocalStack; }

bool AliasInfo::IsAddrTaken(const std::string &name) const { return addr_taken.count(name) > 0; }

namespace {

bool Dominates(BasicBlock *dom, BasicBlock *node, const DominatorTree &tree) {
  if (dom == node) return true;
  BasicBlock *runner = node;
  auto it = tree.idom.find(runner);
  while (it != tree.idom.end()) {
    BasicBlock *next = it->second;
    if (next == dom) return true;
    if (next == runner) break;  // reached root/self
    runner = next;
    it = tree.idom.find(runner);
  }
  return false;
}

void ComputeOrders(const ControlFlowGraph &cfg, std::vector<BasicBlock *> &post, std::vector<BasicBlock *> &rpo) {
  post.clear();
  rpo.clear();
  std::unordered_set<BasicBlock *> visited;
  std::stack<BasicBlock *> st;
  if (cfg.entry) st.push(cfg.entry);
  while (!st.empty()) {
    BasicBlock *b = st.top();
    st.pop();
    if (!visited.insert(b).second) continue;
    for (auto *succ : b->successors) {
      if (succ && !visited.count(succ)) st.push(succ);
    }
    post.push_back(b);
  }
  rpo = post;
  std::reverse(rpo.begin(), rpo.end());
}

std::vector<LoopInfo> ComputeLoops(const ControlFlowGraph &cfg, const DominatorTree &dom) {
  std::vector<LoopInfo> loops;
  for (auto *tail : cfg.blocks) {
    for (auto *head : tail->successors) {
      if (!head) continue;
      if (!Dominates(head, tail, dom)) continue;
      LoopInfo loop;
      loop.header = head;
      loop.backedges.push_back(tail);
      std::unordered_set<BasicBlock *> visited;
      std::vector<BasicBlock *> worklist;
      worklist.push_back(tail);
      visited.insert(head);
      while (!worklist.empty()) {
        BasicBlock *n = worklist.back();
        worklist.pop_back();
        if (!visited.insert(n).second) continue;
        loop.blocks.push_back(n);
        for (auto *pred : n->predecessors) {
          if (pred) worklist.push_back(pred);
        }
      }
      if (std::find(loop.blocks.begin(), loop.blocks.end(), head) == loop.blocks.end()) loop.blocks.push_back(head);
      loops.push_back(std::move(loop));
    }
  }
  return loops;
}

struct DefUseSets {
  std::unordered_map<BasicBlock *, std::unordered_set<std::string>> def;
  std::unordered_map<BasicBlock *, std::unordered_set<std::string>> use;
};

DefUseSets BuildDefUse(Function &func) {
  DefUseSets du;
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    auto &def = du.def[bb];
    auto &use = du.use[bb];

    for (auto &phi : bb->phis) {
      if (phi->HasResult()) def.insert(phi->name);
    }

    auto handle_operands = [&](Instruction *inst) {
      for (const auto &op : inst->operands) {
        if (op.empty()) continue;
        if (def.count(op) == 0) use.insert(op);
      }
    };

    for (auto &inst : bb->instructions) {
      handle_operands(inst.get());
      if (inst->HasResult()) def.insert(inst->name);
    }
    if (bb->terminator) {
      handle_operands(bb->terminator.get());
    }
  }
  return du;
}

LivenessInfo ComputeLiveness(Function &func, const ControlFlowGraph &cfg) {
  LivenessInfo lv;
  auto du = BuildDefUse(func);

  // initialize sets
  for (auto *b : cfg.blocks) {
    lv.live_in[b];
    lv.live_out[b];
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (auto *b : cfg.blocks) {
      auto old_in = lv.live_in[b];
      auto old_out = lv.live_out[b];

      std::unordered_set<std::string> new_out;
      for (auto *succ : b->successors) {
        if (!succ) continue;
        std::unordered_set<std::string> succ_live = lv.live_in[succ];
        for (auto &phi : succ->phis) {
          succ_live.erase(phi->name);
          for (auto &inc : phi->incomings) {
            if (inc.first == b && !inc.second.empty()) {
              succ_live.insert(inc.second);
            }
          }
        }
        new_out.insert(succ_live.begin(), succ_live.end());
      }

      std::unordered_set<std::string> new_in = du.use[b];
      for (const auto &v : new_out) {
        if (du.def[b].count(v) == 0) new_in.insert(v);
      }

      if (new_in != old_in || new_out != old_out) {
        lv.live_in[b] = std::move(new_in);
        lv.live_out[b] = std::move(new_out);
        changed = true;
      }
    }
  }
  return lv;
}

AliasInfo ComputeAlias(Function &func) {
  AliasInfo ai;
  for (const auto &p : func.params) {
    ai.classes[p] = AliasClass::kGlobalOrArg;
  }

  auto mark_addr_taken = [&](const std::string &name) {
    if (ai.ClassOf(name) == AliasClass::kLocalStack) ai.addr_taken.insert(name);
  };

  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      if (phi->HasResult() && ai.classes.find(phi->name) == ai.classes.end()) {
        ai.classes[phi->name] = AliasClass::kUnknown;
      }
      for (auto &inc : phi->incomings) {
        if (!inc.second.empty()) {
          mark_addr_taken(inc.second);
        }
      }
    }

    auto process_inst = [&](Instruction *inst) {
      if (!inst) return;
      if (auto *alloca = dynamic_cast<AllocaInstruction *>(inst)) {
        ai.classes[alloca->name] = AliasClass::kLocalStack;
      } else if (auto *gep = dynamic_cast<GetElementPtrInstruction *>(inst)) {
        if (!gep->operands.empty() && ai.ClassOf(gep->operands[0]) == AliasClass::kLocalStack) {
          ai.classes[gep->name] = AliasClass::kLocalStack;
        } else if (inst->HasResult()) {
          ai.classes[gep->name] = ai.ClassOf(gep->operands.empty() ? "" : gep->operands[0]);
        }
      } else if (auto *cast = dynamic_cast<CastInstruction *>(inst)) {
        if (!cast->operands.empty() && ai.ClassOf(cast->operands[0]) == AliasClass::kLocalStack) {
          ai.classes[cast->name] = AliasClass::kLocalStack;
        } else if (inst->HasResult()) {
          ai.classes[cast->name] = ai.ClassOf(cast->operands.empty() ? "" : cast->operands[0]);
        }
      } else if (inst->HasResult()) {
        ai.classes[inst->name] = AliasClass::kUnknown;
      }

      if (auto *st = dynamic_cast<StoreInstruction *>(inst)) {
        if (st->operands.size() >= 2) mark_addr_taken(st->operands[1]);
      }
      if (auto *call = dynamic_cast<CallInstruction *>(inst)) {
        for (auto &op : call->operands) mark_addr_taken(op);
      }
    };

    for (auto &inst : bb->instructions) process_inst(inst.get());
    process_inst(bb->terminator.get());
  }

  return ai;
}

}  // namespace

AnalysisCache::AnalysisCache(Function &func) : func_(func) {}

void AnalysisCache::InvalidateAll() {
  cfg_dirty_ = dom_dirty_ = df_dirty_ = order_dirty_ = loops_dirty_ = liveness_dirty_ = alias_dirty_ = true;
}

void AnalysisCache::InvalidateCFG() {
  cfg_dirty_ = true;
  dom_dirty_ = df_dirty_ = order_dirty_ = loops_dirty_ = liveness_dirty_ = true;
}

void AnalysisCache::EnsureCFG() {
  if (!cfg_dirty_) return;
  cfg_ = BuildCFG(func_);
  cfg_dirty_ = false;
  dom_dirty_ = df_dirty_ = order_dirty_ = loops_dirty_ = liveness_dirty_ = true;
}

void AnalysisCache::EnsureDom() {
  EnsureCFG();
  if (!dom_dirty_) return;
  dom_ = ComputeDominators(cfg_);
  dom_dirty_ = false;
  df_dirty_ = loops_dirty_ = true;
}

void AnalysisCache::EnsureDF() {
  EnsureDom();
  if (!df_dirty_) return;
  df_ = ComputeDominanceFrontier(cfg_, dom_);
  df_dirty_ = false;
}

void AnalysisCache::EnsureOrders() {
  EnsureCFG();
  if (!order_dirty_) return;
  ComputeOrders(cfg_, postorder_, rpo_);
  order_dirty_ = false;
}

void AnalysisCache::EnsureLoops() {
  EnsureDom();
  EnsureCFG();
  if (!loops_dirty_) return;
  loops_ = ComputeLoops(cfg_, dom_);
  loops_dirty_ = false;
}

void AnalysisCache::EnsureLiveness() {
  EnsureCFG();
  if (!liveness_dirty_) return;
  liveness_ = ComputeLiveness(func_, cfg_);
  liveness_dirty_ = false;
}

void AnalysisCache::EnsureAlias() {
  if (!alias_dirty_) return;
  alias_ = ComputeAlias(func_);
  alias_dirty_ = false;
}

const ControlFlowGraph &AnalysisCache::GetCFG() {
  EnsureCFG();
  return cfg_;
}

const DominatorTree &AnalysisCache::GetDomTree() {
  EnsureDom();
  return dom_;
}

const DominanceFrontier &AnalysisCache::GetDomFrontier() {
  EnsureDF();
  return df_;
}

const std::vector<BasicBlock *> &AnalysisCache::GetPostOrder() {
  EnsureOrders();
  return postorder_;
}

const std::vector<BasicBlock *> &AnalysisCache::GetReversePostOrder() {
  EnsureOrders();
  return rpo_;
}

const std::vector<LoopInfo> &AnalysisCache::GetLoops() {
  EnsureLoops();
  return loops_;
}

const LivenessInfo &AnalysisCache::GetLiveness() {
  EnsureLiveness();
  return liveness_;
}

const AliasInfo &AnalysisCache::GetAliasInfo() {
  EnsureAlias();
  return alias_;
}

}  // namespace polyglot::ir
