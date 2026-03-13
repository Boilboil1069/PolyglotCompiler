#include "middle/include/ir/passes/opt.h"

#include "middle/include/ir/analysis.h"
#include "middle/include/ir/verifier.h"

#include <algorithm>
#include <functional>
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

bool TryParseFloat(const std::string &name, double &out) {
  if (name.empty()) return false;
  try {
    size_t idx = 0;
    double v = std::stod(name, &idx);
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

bool IsPromotableAlloca(const AllocaInstruction &alloca, const AliasInfo &alias) {
  if (alloca.type.kind != IRTypeKind::kPointer || alloca.type.subtypes.empty()) return false;
  const IRType &pointee = alloca.type.subtypes[0];
  if (!pointee.IsScalar()) return false;
  if (alias.ClassOf(alloca.name) != AliasClass::kLocalStack) return false;
  if (alias.IsAddrTaken(alloca.name)) return false;
  return true;
}

void RemoveInstructions(Function &func, const std::unordered_set<Instruction *> &erase) {
  for (auto &bb_ptr : func.blocks) {
    auto &insts = bb_ptr->instructions;
    insts.erase(std::remove_if(insts.begin(), insts.end(), [&](const std::shared_ptr<Instruction> &inst) {
                   return erase.count(inst.get()) > 0;
                 }),
                insts.end());
    bb_ptr->phis.erase(std::remove_if(bb_ptr->phis.begin(), bb_ptr->phis.end(), [&](const std::shared_ptr<PhiInstruction> &phi) {
                              return erase.count(phi.get()) > 0;
                            }),
                       bb_ptr->phis.end());
  }
}
}

void ConstantFold(Function &func) {
  std::unordered_map<std::string, long long> consts;
  std::unordered_map<std::string, double> consts_f;

  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      long long a, b;
      double fa, fb;
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() >= 2) {
          // commutative reordering
          auto is_comm = bin->op == BinaryInstruction::Op::kAdd || bin->op == BinaryInstruction::Op::kMul ||
                         bin->op == BinaryInstruction::Op::kAnd || bin->op == BinaryInstruction::Op::kOr ||
                         bin->op == BinaryInstruction::Op::kCmpEq;
          bool lhs_const = consts.count(bin->operands[0]) || TryParseConst(bin->operands[0], a) ||
                           consts_f.count(bin->operands[0]) || TryParseFloat(bin->operands[0], fa);
          bool rhs_const = consts.count(bin->operands[1]) || TryParseConst(bin->operands[1], b) ||
                           consts_f.count(bin->operands[1]) || TryParseFloat(bin->operands[1], fb);
          if (is_comm && !lhs_const && rhs_const) {
            std::swap(bin->operands[0], bin->operands[1]);
          }

          bool ca = consts.count(bin->operands[0]) ? true : TryParseConst(bin->operands[0], a);
          if (consts.count(bin->operands[0])) a = consts[bin->operands[0]];
          bool cb = consts.count(bin->operands[1]) ? true : TryParseConst(bin->operands[1], b);
          if (consts.count(bin->operands[1])) b = consts[bin->operands[1]];
          bool cfa = consts_f.count(bin->operands[0]) ? true : TryParseFloat(bin->operands[0], fa);
          if (consts_f.count(bin->operands[0])) fa = consts_f[bin->operands[0]];
          bool cfb = consts_f.count(bin->operands[1]) ? true : TryParseFloat(bin->operands[1], fb);
          if (consts_f.count(bin->operands[1])) fb = consts_f[bin->operands[1]];
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
          } else if (cfa && cfb) {
            double res = 0.0;
            switch (bin->op) {
              case BinaryInstruction::Op::kAdd: res = fa + fb; break;
              case BinaryInstruction::Op::kSub: res = fa - fb; break;
              case BinaryInstruction::Op::kMul: res = fa * fb; break;
              case BinaryInstruction::Op::kDiv: res = fb != 0.0 ? fa / fb : fa; break;
              case BinaryInstruction::Op::kCmpEq: consts[bin->name] = (fa == fb); continue;
              case BinaryInstruction::Op::kCmpLt: consts[bin->name] = (fa < fb); continue;
              default: break;
            }
            consts_f[bin->name] = res;
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

  std::unordered_map<std::string, std::string> subst;
  std::unordered_set<Instruction *> erase;
  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      if (!inst->HasResult()) continue;
      auto it_i = consts.find(inst->name);
      auto it_f = consts_f.find(inst->name);
      if (it_i != consts.end()) {
        subst[inst->name] = std::to_string(it_i->second);
        erase.insert(inst.get());
      } else if (it_f != consts_f.end()) {
        subst[inst->name] = std::to_string(it_f->second);
        erase.insert(inst.get());
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
  if (!erase.empty()) RemoveInstructions(func, erase);
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

void RepairPhis(Function &func) {
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    if (bb->predecessors.empty()) {
      bb->phis.clear();
      continue;
    }
    for (auto &phi : bb->phis) {
      std::vector<std::pair<BasicBlock *, std::string>> filtered;
      filtered.reserve(bb->predecessors.size());
      for (auto *pred : bb->predecessors) {
        auto it = std::find_if(phi->incomings.begin(), phi->incomings.end(), [&](auto &inc) { return inc.first == pred; });
        if (it != phi->incomings.end()) {
          filtered.push_back(*it);
        } else {
          filtered.push_back({pred, ""});
        }
      }
      phi->incomings.swap(filtered);
    }
  }
}

void CanonicalizeCFG(Function &func) {
  bool changed = true;
  while (changed) {
    changed = false;

    // remove unreachable blocks
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
    auto before = func.blocks.size();
    func.blocks.erase(std::remove_if(func.blocks.begin(), func.blocks.end(), [&](const std::shared_ptr<BasicBlock> &bb) {
                          return reachable.count(bb.get()) == 0;
                        }),
                      func.blocks.end());
    if (func.blocks.size() != before) changed = true;

    // rebuild preds
    for (auto &bb_ptr : func.blocks) bb_ptr->predecessors.clear();
    for (auto &bb_ptr : func.blocks) {
      auto *bb = bb_ptr.get();
      for (auto *succ : bb->successors) {
        succ->predecessors.push_back(bb);
      }
    }

    RepairPhis(func);

    // simplify branches on constants and trivial cond branches
    for (auto &bb_ptr : func.blocks) {
      auto *bb = bb_ptr.get();
      if (!bb->terminator) continue;
      if (auto *cbr = dynamic_cast<CondBranchStatement *>(bb->terminator.get())) {
        long long val;
        bool const_cond = TryParseConst(cbr->operands.empty() ? std::string() : cbr->operands[0], val);
        if (const_cond) {
          bb->terminator = std::make_shared<BranchStatement>();
          auto *br = dynamic_cast<BranchStatement *>(bb->terminator.get());
          br->target = val ? cbr->true_target : cbr->false_target;
          changed = true;
        } else if (cbr->true_target == cbr->false_target) {
          bb->terminator = std::make_shared<BranchStatement>();
          auto *br = dynamic_cast<BranchStatement *>(bb->terminator.get());
          br->target = cbr->true_target;
          changed = true;
        }
      }
    }

    // merge linear blocks: bb -> succ where bb has single succ and succ single pred
    for (auto it = func.blocks.begin(); it != func.blocks.end();) {
      auto *bb = it->get();
      if (bb == func.entry) {
        ++it;
        continue;
      }
      if (bb->successors.size() == 1) {
        BasicBlock *succ = bb->successors[0];
        if (succ && succ->predecessors.size() == 1 && succ != bb) {
          // fold succ's phis
          std::unordered_map<std::string, std::string> subst;
          for (auto &phi : succ->phis) {
            if (phi->incomings.size() == 1) {
              subst[phi->name] = phi->incomings[0].second;
            }
          }
          if (!subst.empty()) ApplySubstitutions(func, subst);

          // move instructions
          bb->instructions.insert(bb->instructions.end(), succ->instructions.begin(), succ->instructions.end());
          bb->terminator = succ->terminator;
          bb->successors = succ->successors;

          // update succ successors' predecessors
          for (auto *ss : succ->successors) {
            if (!ss) continue;
            for (auto &p : ss->predecessors) {
              if (p == succ) p = bb;
            }
          }

          // erase succ from function
          it = func.blocks.erase(std::remove_if(func.blocks.begin(), func.blocks.end(), [&](const std::shared_ptr<BasicBlock> &ptr) {
                                      return ptr.get() == succ;
                                    }),
                                  func.blocks.end());
          changed = true;
          continue;
        }
      }
      ++it;
    }

    // remove empty forwarding blocks (no phis/instructions) with single branch
    for (auto it = func.blocks.begin(); it != func.blocks.end();) {
      auto *bb = it->get();
      if (bb == func.entry) {
        ++it;
        continue;
      }
      if (!bb->phis.empty() || !bb->instructions.empty()) {
        ++it;
        continue;
      }
      if (auto *br = dynamic_cast<BranchStatement *>(bb->terminator.get())) {
        BasicBlock *target = br->target;
        if (target && target != bb) {
          // redirect predecessors
          for (auto *pred : bb->predecessors) {
            if (auto *pred_br = dynamic_cast<BranchStatement *>(pred->terminator.get())) {
              if (pred_br->target == bb) pred_br->target = target;
            } else if (auto *pred_cbr = dynamic_cast<CondBranchStatement *>(pred->terminator.get())) {
              if (pred_cbr->true_target == bb) pred_cbr->true_target = target;
              if (pred_cbr->false_target == bb) pred_cbr->false_target = target;
            } else if (auto *pred_sw = dynamic_cast<SwitchStatement *>(pred->terminator.get())) {
              for (auto &c : pred_sw->cases) if (c.target == bb) c.target = target;
              if (pred_sw->default_target == bb) pred_sw->default_target = target;
            }
          }
          // fix phi incomings in target
          for (auto &phi : target->phis) {
            for (auto &inc : phi->incomings) {
              if (inc.first == bb) inc.first = nullptr;  // will be repaired
            }
          }

          it = func.blocks.erase(it);
          changed = true;
          RepairPhis(func);
          continue;
        }
      }
      ++it;
    }
  }

  // final rebuild preds and repair phis
  for (auto &bb_ptr : func.blocks) bb_ptr->predecessors.clear();
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto *succ : bb->successors) {
      succ->predecessors.push_back(bb);
    }
  }
  RepairPhis(func);
}

void SimplifyCFG(Function &func) { CanonicalizeCFG(func); }

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
  AnalysisCache analysis(func);
  const auto &alias = analysis.GetAliasInfo();

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
             (std::hash<std::string>()(k.b) << 1) ^ std::hash<int>()(static_cast<int>(k.type.kind));
    }
  };
  std::unordered_map<Key, std::string, KeyHash> table;

  for (auto &bb_ptr : func.blocks) {
    std::unordered_map<std::string, std::string> avail_loads;
    for (auto &inst : bb_ptr->instructions) {
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() < 2 || !bin->HasResult()) continue;
        bool comm = bin->op == BinaryInstruction::Op::kAdd || bin->op == BinaryInstruction::Op::kMul ||
                     bin->op == BinaryInstruction::Op::kAnd || bin->op == BinaryInstruction::Op::kOr ||
                     bin->op == BinaryInstruction::Op::kCmpEq;
        std::string a = bin->operands[0];
        std::string b = bin->operands[1];
        if (comm && b < a) std::swap(a, b);
        Key k{bin->op, a, b, bin->type};
        auto it = table.find(k);
        if (it != table.end()) {
          subst[bin->name] = it->second;
        } else {
          table[k] = bin->name;
        }
      } else if (auto *load = dynamic_cast<LoadInstruction *>(inst.get())) {
        if (!load->HasResult() || load->operands.empty()) continue;
        const std::string &addr = load->operands[0];
        if (alias.ClassOf(addr) == AliasClass::kLocalStack && !alias.IsAddrTaken(addr)) {
          auto it = avail_loads.find(addr);
          if (it != avail_loads.end()) {
            subst[load->name] = it->second;
          } else {
            avail_loads[addr] = load->name;
          }
        } else {
          avail_loads.clear();
        }
      } else if (auto *store = dynamic_cast<StoreInstruction *>(inst.get())) {
        if (store->operands.size() >= 1) {
          const std::string &addr = store->operands[0];
          if (alias.ClassOf(addr) == AliasClass::kLocalStack && !alias.IsAddrTaken(addr)) {
            avail_loads.erase(addr);
          } else {
            avail_loads.clear();
          }
        }
      } else {
        // unknown side effects invalidate simple load cache
        avail_loads.clear();
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
  DeadCodeEliminate(func);
}

void CopyProp(Function &func) {
  std::unordered_map<std::string, std::string> subst;
  std::unordered_set<Instruction *> erase;
  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      if (auto *assign = dynamic_cast<AssignInstruction *>(inst.get())) {
        if (assign->operands.size() == 1 && assign->HasResult()) {
          subst[assign->name] = assign->operands[0];
          erase.insert(assign);
        }
      }
    }
  }
  if (!subst.empty()) ApplySubstitutions(func, subst);
  if (!erase.empty()) RemoveInstructions(func, erase);
}

struct Lattice {
  enum class State { kUnknown, kConstInt, kConstFloat, kOverdefined } state{State::kUnknown};
  long long i{0};
  double f{0.0};
};

static bool MergeConst(Lattice &dst, const Lattice &src) {
  if (dst.state == src.state) {
    if (dst.state == Lattice::State::kConstInt && dst.i != src.i) {
      dst.state = Lattice::State::kOverdefined;
      return true;
    }
    if (dst.state == Lattice::State::kConstFloat && dst.f != src.f) {
      dst.state = Lattice::State::kOverdefined;
      return true;
    }
    return false;
  }
  if (dst.state == Lattice::State::kUnknown) {
    dst = src;
    return true;
  }
  if (src.state == Lattice::State::kOverdefined || dst.state == Lattice::State::kOverdefined) {
    bool changed = dst.state != Lattice::State::kOverdefined;
    dst.state = Lattice::State::kOverdefined;
    return changed;
  }
  // unknown + const handled above; const + unknown handled by caller
  return false;
}

void SCCP(Function &func) {
  std::unordered_map<std::string, Lattice> value;
  std::unordered_set<BasicBlock *> executable;
  std::vector<BasicBlock *> worklist;
  if (func.entry) {
    executable.insert(func.entry);
    worklist.push_back(func.entry);
  }

  auto mark_exec = [&](BasicBlock *bb) {
    if (executable.insert(bb).second) worklist.push_back(bb);
  };

  auto get = [&](const std::string &name) -> Lattice {
    auto it = value.find(name);
    if (it != value.end()) return it->second;
    Lattice l;
    l.state = Lattice::State::kUnknown;
    return l;
  };

  auto set_const_int = [&](const std::string &name, long long v) {
    Lattice l;
    l.state = Lattice::State::kConstInt;
    l.i = v;
    MergeConst(value[name], l);
  };

  auto set_const_float = [&](const std::string &name, double v) {
    Lattice l;
    l.state = Lattice::State::kConstFloat;
    l.f = v;
    MergeConst(value[name], l);
  };

  auto mark_over = [&](const std::string &name) {
    Lattice l;
    l.state = Lattice::State::kOverdefined;
    MergeConst(value[name], l);
  };

  while (!worklist.empty()) {
    BasicBlock *bb = worklist.back();
    worklist.pop_back();
    if (!executable.count(bb)) continue;

    for (auto &phi : bb->phis) {
      if (!phi->HasResult()) continue;
      Lattice acc;
      acc.state = Lattice::State::kUnknown;
      for (auto &inc : phi->incomings) {
        if (!inc.first || executable.count(inc.first) == 0) continue;
        acc = acc.state == Lattice::State::kUnknown ? get(inc.second) : acc;
        MergeConst(acc, get(inc.second));
        if (acc.state == Lattice::State::kOverdefined) break;
      }
      MergeConst(value[phi->name], acc);
    }

    for (auto &inst : bb->instructions) {
      if (!inst->HasResult()) continue;
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() < 2) {
          mark_over(bin->name);
          continue;
        }
        auto a = get(bin->operands[0]);
        auto b = get(bin->operands[1]);
        if ((a.state == Lattice::State::kConstInt || a.state == Lattice::State::kConstFloat) &&
            (b.state == Lattice::State::kConstInt || b.state == Lattice::State::kConstFloat)) {
          if (bin->type.IsFloat()) {
            double av = (a.state == Lattice::State::kConstFloat) ? a.f : static_cast<double>(a.i);
            double bv = (b.state == Lattice::State::kConstFloat) ? b.f : static_cast<double>(b.i);
            double res = 0.0;
            switch (bin->op) {
              case BinaryInstruction::Op::kAdd: res = av + bv; break;
              case BinaryInstruction::Op::kSub: res = av - bv; break;
              case BinaryInstruction::Op::kMul: res = av * bv; break;
              case BinaryInstruction::Op::kDiv: res = bv != 0.0 ? av / bv : av; break;
              case BinaryInstruction::Op::kCmpEq: set_const_int(bin->name, av == bv); continue;
              case BinaryInstruction::Op::kCmpLt: set_const_int(bin->name, av < bv); continue;
              default: break;
            }
            set_const_float(bin->name, res);
          } else {
            long long av = (a.state == Lattice::State::kConstInt) ? a.i : static_cast<long long>(a.f);
            long long bv = (b.state == Lattice::State::kConstInt) ? b.i : static_cast<long long>(b.f);
            long long res = 0;
            switch (bin->op) {
              case BinaryInstruction::Op::kAdd: res = av + bv; break;
              case BinaryInstruction::Op::kSub: res = av - bv; break;
              case BinaryInstruction::Op::kMul: res = av * bv; break;
              case BinaryInstruction::Op::kDiv: res = bv != 0 ? av / bv : av; break;
              case BinaryInstruction::Op::kCmpEq: res = (av == bv); break;
              case BinaryInstruction::Op::kCmpLt: res = (av < bv); break;
              default: break;
            }
            set_const_int(bin->name, res);
          }
        } else {
          mark_over(bin->name);
        }
      } else if ([[maybe_unused]] auto *phi = dynamic_cast<PhiInstruction *>(inst.get())) {
        // already handled above
      } else {
        mark_over(inst->name);
      }
    }

    if (auto *term = bb->terminator.get()) {
      if (auto *cbr = dynamic_cast<CondBranchStatement *>(term)) {
        if (!cbr->operands.empty()) {
          auto cond = get(cbr->operands[0]);
          if (cond.state == Lattice::State::kConstInt) {
            if (cond.i) mark_exec(cbr->true_target);
            else mark_exec(cbr->false_target);
            continue;
          }
        }
      } else if (auto *sw = dynamic_cast<SwitchStatement *>(term)) {
        if (!sw->operands.empty()) {
          auto val = get(sw->operands[0]);
          if (val.state == Lattice::State::kConstInt) {
            bool hit = false;
            for (auto &c : sw->cases) {
              if (c.value == val.i) {
                mark_exec(c.target);
                hit = true;
                break;
              }
            }
            if (!hit) mark_exec(sw->default_target);
            continue;
          }
        }
      }
      for (auto *succ : bb->successors) mark_exec(succ);
    }
  }

  std::unordered_map<std::string, std::string> subst;
  std::unordered_set<Instruction *> erase;
  for (auto &bb_ptr : func.blocks) {
    for (auto &phi : bb_ptr->phis) {
      if (!phi->HasResult()) continue;
      auto it = value.find(phi->name);
      if (it != value.end()) {
        if (it->second.state == Lattice::State::kConstInt) {
          subst[phi->name] = std::to_string(it->second.i);
        } else if (it->second.state == Lattice::State::kConstFloat) {
          subst[phi->name] = std::to_string(it->second.f);
        }
      }
    }
    for (auto &inst : bb_ptr->instructions) {
      if (!inst->HasResult()) continue;
      auto it = value.find(inst->name);
      if (it == value.end()) continue;
      if (it->second.state == Lattice::State::kConstInt) {
        subst[inst->name] = std::to_string(it->second.i);
        erase.insert(inst.get());
      } else if (it->second.state == Lattice::State::kConstFloat) {
        subst[inst->name] = std::to_string(it->second.f);
        erase.insert(inst.get());
      }
    }
  }

  if (!subst.empty()) ApplySubstitutions(func, subst);
  if (!erase.empty()) RemoveInstructions(func, erase);
}

void Mem2Reg(Function &func) {
  AnalysisCache analysis(func);
  const auto &alias = analysis.GetAliasInfo();
  std::unordered_map<std::string, IRType> slot_type;
  std::vector<AllocaInstruction *> promotable;

  for (auto &bb_ptr : func.blocks) {
    for (auto &inst : bb_ptr->instructions) {
      if (auto *alloca = dynamic_cast<AllocaInstruction *>(inst.get())) {
        if (IsPromotableAlloca(*alloca, alias)) {
          promotable.push_back(alloca);
          slot_type[alloca->name] = alloca->type.subtypes[0];
        }
      }
    }
  }
  if (promotable.empty()) return;

  const auto &cfg = analysis.GetCFG();
  const auto &df = analysis.GetDomFrontier();
  const auto &dom = analysis.GetDomTree();

  // Per-slot phi insertion and renaming
  std::unordered_set<Instruction *> erase;
  std::unordered_map<std::string, std::string> subst;
  std::unordered_map<PhiInstruction *, std::string> phi_slot;

  struct SlotState {
    std::string slot;
    IRType type;
    int version{0};
    std::vector<std::string> stack;
  };

  std::unordered_map<std::string, SlotState> states;
  for (auto *alloca : promotable) {
    states[alloca->name] = SlotState{alloca->name, slot_type[alloca->name], 0, {}};
  }

  std::unordered_map<std::string, std::unordered_set<BasicBlock *>> defsites;
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &inst : bb->instructions) {
      if (auto *store = dynamic_cast<StoreInstruction *>(inst.get())) {
        if (store->operands.size() >= 2 && states.count(store->operands[0])) {
          defsites[store->operands[0]].insert(bb);
        }
      }
    }
  }

  // Insert phi nodes per slot using dominance frontier
  for (const auto &[slot, blocks] : defsites) {
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
          phi->name = slot;
          phi->type = slot_type[slot];
          for (auto *pred : y->predecessors) {
            phi->incomings.push_back({pred, slot});
          }
          y->AddPhi(phi);
            phi_slot[phi.get()] = slot;
          if (blocks.find(y) == blocks.end()) {
            worklist.push_back(y);
          }
        }
      }
    }
  }

  std::function<void(BasicBlock *)> rename_block = [&](BasicBlock *bb) {
    // Define phis
    for (auto &phi : bb->phis) {
      auto it_slot = phi_slot.find(phi.get());
      if (it_slot == phi_slot.end()) continue;
      auto &state = states[it_slot->second];
      std::string new_name = state.slot + "_ssa" + std::to_string(state.version++);
      subst[phi->name] = new_name;
      phi->name = new_name;
      state.stack.push_back(new_name);
    }

    for (auto &inst : bb->instructions) {
      if (auto *load = dynamic_cast<LoadInstruction *>(inst.get())) {
        if (load->operands.size() >= 1 && states.count(load->operands[0])) {
          auto &state = states[load->operands[0]];
          if (!state.stack.empty()) {
            subst[load->name] = state.stack.back();
            erase.insert(load);
          }
        }
      } else if (auto *store = dynamic_cast<StoreInstruction *>(inst.get())) {
        if (store->operands.size() >= 2 && states.count(store->operands[0])) {
          auto &state = states[store->operands[0]];
          const std::string &val = store->operands[1];
          state.stack.push_back(val);
          erase.insert(store);
        }
      } else if (auto *alloca = dynamic_cast<AllocaInstruction *>(inst.get())) {
        if (states.count(alloca->name)) {
          erase.insert(alloca);
        }
      }
    }

    // Update successor phi incoming
    for (auto *succ : bb->successors) {
      for (auto &phi : succ->phis) {
        auto it_slot = phi_slot.find(phi.get());
        if (it_slot == phi_slot.end()) continue;
        auto &state = states[it_slot->second];
        std::string current = state.stack.empty() ? state.slot : state.stack.back();
        for (auto &inc : phi->incomings) {
          if (inc.first == bb) inc.second = current;
        }
      }
    }

    auto child_it = dom.children.find(bb);
    if (child_it != dom.children.end()) {
      for (auto *child : child_it->second) rename_block(child);
    }

    // pop definitions from this block in reverse
    for (auto it = bb->instructions.rbegin(); it != bb->instructions.rend(); ++it) {
      if (auto *store = dynamic_cast<StoreInstruction *>(it->get())) {
        if (store->operands.size() >= 2 && states.count(store->operands[0])) {
          auto &state = states[store->operands[0]];
          if (!state.stack.empty()) state.stack.pop_back();
        }
      }
    }
    for (auto it = bb->phis.rbegin(); it != bb->phis.rend(); ++it) {
      auto it_slot = phi_slot.find(it->get());
      if (it_slot != phi_slot.end()) {
        auto &state = states[it_slot->second];
        if (!state.stack.empty()) state.stack.pop_back();
      }
    }
  };

  if (cfg.entry) rename_block(cfg.entry);

  if (!subst.empty()) ApplySubstitutions(func, subst);
  if (!erase.empty()) RemoveInstructions(func, erase);
  DeadCodeEliminate(func);
}

void RunDefaultOptimizations(Function &func) {
  std::string verify_msg;
  if (!Verify(func, &verify_msg)) {
    return;  // refuse to optimize ill-formed IR
  }
  ConstantFold(func);
  SCCP(func);
  CopyProp(func);
  CSE(func);
  EliminateRedundantPhis(func);
  Mem2Reg(func);
  SimplifyCFG(func);
  DeadCodeEliminate(func);
}

}  // namespace polyglot::ir::passes
