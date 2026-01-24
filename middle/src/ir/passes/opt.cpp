#include "middle/include/ir/passes/opt.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace polyglot::ir::passes {

namespace {
bool TryParseConst(const std::string &name, long long &out) {
  if (name.empty()) return false;
  try {
    size_t idx = 0;
    long long v = std::stoll(name, &idx, 0);
    if (idx == name.size()) {
      out = v;
      return true;
    }
  } catch (...) {
  }
  return false;
}

void ApplySubstitutions(Function &func, const std::unordered_map<std::string, std::string> &subst) {
  auto replace = [&](std::string &s) {
    auto it = subst.find(s);
    if (it != subst.end()) s = it->second;
  };
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      replace(phi->name);
      for (auto &inc : phi->incomings) replace(inc.second);
    }
    for (auto &inst : bb->instructions) {
      replace(inst->name);
      for (auto &op : inst->operands) replace(op);
    }
    if (bb->terminator) {
      replace(bb->terminator->name);
      for (auto &op : bb->terminator->operands) replace(op);
    }
  }
}
}

void ConstantFold(Function &func) {
  std::unordered_map<std::string, long long> consts;
  std::unordered_map<std::string, double> consts_f;

  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      long long a, b;
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() >= 2) {
          bool ca = consts.count(bin->operands[0]) ? true : TryParseConst(bin->operands[0], a);
          if (consts.count(bin->operands[0])) a = consts[bin->operands[0]];
          bool cb = consts.count(bin->operands[1]) ? true : TryParseConst(bin->operands[1], b);
          if (consts.count(bin->operands[1])) b = consts[bin->operands[1]];
          if (ca && cb) {
            long long res = 0;
            switch (bin->op) {
              case BinaryInstruction::Op::kAdd: res = a + b; break;
              case BinaryInstruction::Op::kSub: res = a - b; break;
              case BinaryInstruction::Op::kMul: res = a * b; break;
              case BinaryInstruction::Op::kDiv: res = b != 0 ? a / b : a; break;
              case BinaryInstruction::Op::kCmpEq: res = (a == b); break;
              case BinaryInstruction::Op::kCmpLt: res = (a < b); break;
              default: break;
            }
            consts[bin->name] = res;
          }
        }
      } else if (auto *cast = dynamic_cast<CastInstruction *>(inst.get())) {
        if (!cast->operands.empty()) {
          long long v;
          double vf;
          bool has_i = consts.count(cast->operands[0]) ? true : TryParseConst(cast->operands[0], v);
          if (consts.count(cast->operands[0])) v = consts[cast->operands[0]];
          bool has_f = consts_f.count(cast->operands[0]);
          if (has_f) vf = consts_f[cast->operands[0]];
          if (has_i || has_f) {
            if (cast->cast == CastInstruction::CastKind::kTrunc) {
              if (has_i) v = static_cast<int32_t>(v);
            }
            if (cast->type.IsFloat()) {
              double res = has_f ? vf : static_cast<double>(v);
              consts_f[cast->name] = res;
            } else {
              long long res = has_i ? v : static_cast<long long>(vf);
              consts[cast->name] = res;
            }
          }
        }
      }
    }
  }
}

void DeadCodeEliminate(Function &func) {
  std::unordered_set<std::string> live;
  // collect uses
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      for (auto &inc : phi->incomings) live.insert(inc.second);
    }
    for (auto &inst : bb->instructions) {
      for (auto &op : inst->operands) live.insert(op);
    }
    if (bb->terminator) {
      for (auto &op : bb->terminator->operands) live.insert(op);
    }
  }

  for (auto &bb_ptr : func.blocks) {
    auto &insts = bb_ptr->instructions;
    insts.erase(std::remove_if(insts.begin(), insts.end(), [&](const std::shared_ptr<Instruction> &inst) {
                   if (!inst->HasResult()) return false;
                   return live.count(inst->name) == 0;
                 }),
                insts.end());
    bb_ptr->phis.erase(std::remove_if(bb_ptr->phis.begin(), bb_ptr->phis.end(), [&](const std::shared_ptr<PhiInstruction> &phi) {
                              return phi->HasResult() && live.count(phi->name) == 0;
                            }),
                       bb_ptr->phis.end());
  }
}

void SimplifyCFG(Function &func) {
  // remove unreachable blocks and fix predecessors/successors
  auto cfg = BuildCFG(func);
  std::unordered_set<BasicBlock *> reachable;
  std::vector<BasicBlock *> stack;
  if (cfg.entry) stack.push_back(cfg.entry);
  while (!stack.empty()) {
    auto *b = stack.back();
    stack.pop_back();
    if (!reachable.insert(b).second) continue;
    for (auto *s : b->successors) stack.push_back(s);
  }
  func.blocks.erase(std::remove_if(func.blocks.begin(), func.blocks.end(), [&](const std::shared_ptr<BasicBlock> &bb) {
                        return reachable.count(bb.get()) == 0;
                      }),
                    func.blocks.end());

  // fix predecessor lists to match successors
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    bb->predecessors.clear();
  }
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto *succ : bb->successors) {
      succ->predecessors.push_back(bb);
    }
  }
}

void EliminateRedundantPhis(Function &func) {
  std::unordered_map<std::string, std::string> subst;
  for (auto &bb_ptr : func.blocks) {
    auto &phis = bb_ptr->phis;
    for (auto it = phis.begin(); it != phis.end();) {
      auto &phi = *it;
      if (phi->incomings.empty()) {
        ++it;
        continue;
      }
      const std::string first = phi->incomings.front().second;
      bool all_same = std::all_of(phi->incomings.begin(), phi->incomings.end(), [&](auto &inc) { return inc.second == first; });
      if (all_same && phi->HasResult()) {
        subst[phi->name] = first;
        it = phis.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
}

void CSE(Function &func) {
  std::unordered_map<std::string, std::string> subst;
  struct Key {
    BinaryInstruction::Op op;
    std::string a;
    std::string b;
    IRType type;
    bool operator==(const Key &o) const { return op == o.op && a == o.a && b == o.b && type == o.type; }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const {
      return std::hash<int>()(static_cast<int>(k.op)) ^ std::hash<std::string>()(k.a) ^
             (std::hash<std::string>()(k.b) << 1);
    }
  };
  std::unordered_map<Key, std::string, KeyHash> table;

  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() < 2 || !bin->HasResult()) continue;
        Key k{bin->op, bin->operands[0], bin->operands[1], bin->type};
        auto it = table.find(k);
        if (it != table.end()) {
          subst[bin->name] = it->second;
        } else {
          table[k] = bin->name;
        }
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
  DeadCodeEliminate(func);
}

void Mem2Reg(Function &func) {
  // Very small local promotion: if an alloca is only used by loads/stores in the same block, replace loads with last stored value.
  std::unordered_map<std::string, std::string> subst;
  for (auto &bb_ptr : func.blocks) {
    std::unordered_map<std::string, std::string> slot_value;
    for (auto &inst : bb_ptr->instructions) {
      if (auto *alloca = dynamic_cast<AllocaInstruction *>(inst.get())) {
        slot_value[alloca->name] = "";
      } else if (auto *store = dynamic_cast<StoreInstruction *>(inst.get())) {
        if (store->operands.size() >= 2 && slot_value.count(store->operands[0])) {
          slot_value[store->operands[0]] = store->operands[1];
        }
      } else if (auto *load = dynamic_cast<LoadInstruction *>(inst.get())) {
        if (!load->HasResult()) continue;
        if (load->operands.size() >= 1 && slot_value.count(load->operands[0])) {
          const auto &val = slot_value[load->operands[0]];
          if (!val.empty()) subst[load->name] = val;
        }
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
  DeadCodeEliminate(func);
}

void RunDefaultOptimizations(Function &func) {
  ConstantFold(func);
  CSE(func);
  EliminateRedundantPhis(func);
  Mem2Reg(func);
  SimplifyCFG(func);
  DeadCodeEliminate(func);
}

}  // namespace polyglot::ir::passes
