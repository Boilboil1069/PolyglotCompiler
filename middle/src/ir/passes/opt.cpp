/**
 * @file     opt.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "middle/include/ir/analysis.h"
#include <limits>
#include "middle/include/ir/passes/opt.h"
#include "middle/include/ir/verifier.h"

namespace polyglot::ir::passes {

namespace {
bool TryParseConst(const std::string &name, long long &out) {
  if (name.empty())
    return false;
  try {
    size_t idx = 0;
    long long v = std::stoll(name, &idx, 0);
    if (idx == name.size()) {
      out = v;
      return true;
    }
  } catch (...) {}
  return false;
}

void ApplySubstitutions(Function &func, const std::unordered_map<std::string, std::string> &subst) {
  auto replace = [&](std::string &s) {
    std::unordered_set<std::string> seen;
    while (seen.insert(s).second) {
      auto it = subst.find(s);
      if (it == subst.end()) break;
      s = it->second;
    }
  };
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      for (auto &inc : phi->incomings)
        replace(inc.second);
    }
    for (auto &inst : bb->instructions) {
      for (auto &op : inst->operands)
        replace(op);
      if (auto *call = dynamic_cast<CallInstruction *>(inst.get()); call && call->is_indirect)
        replace(call->callee);
    }
    if (bb->terminator) {
      for (auto &op : bb->terminator->operands)
        replace(op);
    }
  }
}

bool IsPromotableAlloca(const AllocaInstruction &alloca, const AliasInfo &alias) {
  if (alloca.type.kind != IRTypeKind::kPointer || alloca.type.subtypes.empty())
    return false;
  const IRType &pointee = alloca.type.subtypes[0];
  if (!pointee.IsScalar())
    return false;
  if (alias.ClassOf(alloca.name) != AliasClass::kLocalStack)
    return false;
  if (alias.IsAddrTaken(alloca.name))
    return false;
  return true;
}

void RemoveInstructions(Function &func, const std::unordered_set<Instruction *> &erase) {
  for (auto &bb_ptr : func.blocks) {
    auto &insts = bb_ptr->instructions;
    insts.erase(std::remove_if(insts.begin(), insts.end(),
                               [&](const std::shared_ptr<Instruction> &inst) {
                                 return erase.count(inst.get()) > 0;
                               }),
                insts.end());
    bb_ptr->phis.erase(std::remove_if(bb_ptr->phis.begin(), bb_ptr->phis.end(),
                                      [&](const std::shared_ptr<PhiInstruction> &phi) {
                                        return erase.count(phi.get()) > 0;
                                      }),
                       bb_ptr->phis.end());
  }
}
} // namespace

void ConstantFold(Function &func) {
  struct Constant { IRType type; std::uint64_t bits; };
  std::unordered_map<std::string, Constant> constants;
  auto width = [](const IRType &type) -> unsigned {
    switch (type.kind) {
    case IRTypeKind::kI1: return 1;
    case IRTypeKind::kI8: return 8;
    case IRTypeKind::kI16: return 16;
    case IRTypeKind::kI32: case IRTypeKind::kF32: return 32;
    default: return 64;
    }
  };
  auto mask = [&](const IRType &type) {
    return width(type) == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << width(type)) - 1;
  };
  auto signed_value = [&](const Constant &value) -> std::int64_t {
    const auto bits = value.bits & mask(value.type);
    const auto sign = std::uint64_t(1) << (width(value.type) - 1);
    return static_cast<std::int64_t>((bits ^ sign) - sign);
  };
  auto floating = [](const Constant &value) {
    if (value.type.kind == IRTypeKind::kF32) {
      auto bits = static_cast<std::uint32_t>(value.bits); float result;
      std::memcpy(&result, &bits, sizeof(result)); return static_cast<double>(result);
    }
    double result; std::memcpy(&result, &value.bits, sizeof(result)); return result;
  };
  auto float_bits = [](double value, const IRType &type) {
    std::uint64_t result = 0;
    if (type.kind == IRTypeKind::kF32) { float f = static_cast<float>(value); std::memcpy(&result, &f, sizeof(f)); }
    else std::memcpy(&result, &value, sizeof(value));
    return result;
  };
  auto get = [&](const std::string &name, IRType expected, Constant &out) {
    if (const auto found = constants.find(name); found != constants.end()) { out = found->second; return true; }
    try {
      std::size_t consumed = 0;
      if (expected.IsFloat()) {
        double value = std::stod(name, &consumed);
        out = {expected, float_bits(value, expected)};
      } else out = {expected, std::stoull(name, &consumed, 0) & mask(expected)};
      return consumed == name.size();
    } catch (...) { return false; }
  };
  for (auto &block : func.blocks) {
    for (auto &instruction : block->instructions) {
      if (auto constant = dynamic_cast<ConstantInstruction *>(instruction.get())) {
        constants[constant->name] = {constant->type, constant->bits & mask(constant->type)};
        continue;
      }
      auto binary = dynamic_cast<BinaryInstruction *>(instruction.get());
      if (!binary || binary->operands.size() != 2) continue;
      using Op = BinaryInstruction::Op;
      auto operand_type = binary->type.kind == IRTypeKind::kI1 ? IRType::I64() : binary->type;
      for (const auto &name : binary->operands)
        if (auto found = constants.find(name); found != constants.end()) { operand_type = found->second.type; break; }
      Constant a, b;
      if (!get(binary->operands[0], operand_type, a) || !get(binary->operands[1], operand_type, b)) continue;
      std::uint64_t result = 0;
      if (a.type.IsFloat() || b.type.IsFloat()) {
        if (!a.type.IsFloat() || !b.type.IsFloat()) continue;
        const double x = floating(a), y = floating(b);
        const bool ordered = !std::isnan(x) && !std::isnan(y);
        double value = 0; bool comparison = false;
        switch (binary->op) {
        case Op::kFAdd: value = x + y; break;
        case Op::kFSub: value = x - y; break;
        case Op::kFMul: value = x * y; break;
        case Op::kFDiv: value = x / y; break;
        case Op::kFRem: value = std::fmod(x, y); break;
        case Op::kCmpEq: case Op::kCmpFoe: result = ordered && x == y; comparison = true; break;
        case Op::kCmpNe: result = x != y; comparison = true; break;
        case Op::kCmpFne: result = ordered && x != y; comparison = true; break;
        case Op::kCmpLt: case Op::kCmpFlt: result = ordered && x < y; comparison = true; break;
        case Op::kCmpFle: result = ordered && x <= y; comparison = true; break;
        case Op::kCmpFgt: result = ordered && x > y; comparison = true; break;
        case Op::kCmpFge: result = ordered && x >= y; comparison = true; break;
        default: continue;
        }
        if (!comparison) result = float_bits(value, binary->type);
      } else {
        const auto x = a.bits & mask(a.type), y = b.bits & mask(b.type);
        const auto sx = signed_value(a), sy = signed_value(b);
        const auto signed_min = width(a.type) == 64 ? std::numeric_limits<std::int64_t>::min()
                                                   : -(std::int64_t(1) << (width(a.type) - 1));
        switch (binary->op) {
        case Op::kAdd: result = x + y; break;
        case Op::kSub: result = x - y; break;
        case Op::kMul: result = x * y; break;
        case Op::kAnd: result = x & y; break;
        case Op::kOr: result = x | y; break;
        case Op::kXor: result = x ^ y; break;
        case Op::kShl: if (y >= width(a.type)) continue; result = x << y; break;
        case Op::kLShr: if (y >= width(a.type)) continue; result = x >> y; break;
        case Op::kAShr: if (y >= width(a.type)) continue; result = sx >> y; break;
        case Op::kDiv: case Op::kSDiv:
          if (!sy || (sx == signed_min && sy == -1)) continue;
          result = sx / sy; break;
        case Op::kRem: case Op::kSRem:
          if (!sy || (sx == signed_min && sy == -1)) continue;
          result = sx % sy; break;
        case Op::kUDiv: if (!y) continue; result = x / y; break;
        case Op::kURem: if (!y) continue; result = x % y; break;
        case Op::kCmpEq: result = x == y; break;
        case Op::kCmpNe: result = x != y; break;
        case Op::kCmpUlt: result = x < y; break;
        case Op::kCmpUle: result = x <= y; break;
        case Op::kCmpUgt: result = x > y; break;
        case Op::kCmpUge: result = x >= y; break;
        case Op::kCmpLt: case Op::kCmpSlt: result = sx < sy; break;
        case Op::kCmpSle: result = sx <= sy; break;
        case Op::kCmpSgt: result = sx > sy; break;
        case Op::kCmpSge: result = sx >= sy; break;
        default: continue;
        }
      }
      auto folded = std::make_shared<ConstantInstruction>();
      folded->name = binary->name; folded->type = binary->type; folded->parent = block.get();
      folded->bits = result & mask(folded->type);
      constants[folded->name] = {folded->type, folded->bits};
      instruction = std::move(folded);
    }
    // CFG canonicalisation understands literal conditions; substitute only
    // here so arithmetic operands retain their original width and float bits.
    if (auto branch = dynamic_cast<CondBranchStatement *>(block->terminator.get());
        branch && !branch->operands.empty()) {
      const auto found = constants.find(branch->operands[0]);
      if (found != constants.end() && found->second.type.IsInteger())
        branch->operands[0] = found->second.bits ? "1" : "0";
    }
  }
}

void DeadCodeEliminate(Function &func) {
  std::unordered_set<std::string> live;
  // collect uses
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    for (auto &phi : bb->phis) {
      for (auto &inc : phi->incomings)
        live.insert(inc.second);
    }
    for (auto &inst : bb->instructions) {
      for (auto &op : inst->operands)
        live.insert(op);
    }
    if (bb->terminator) {
      for (auto &op : bb->terminator->operands)
        live.insert(op);
    }
  }

  for (auto &bb_ptr : func.blocks) {
    auto &insts = bb_ptr->instructions;
    insts.erase(std::remove_if(insts.begin(), insts.end(),
                               [&](const std::shared_ptr<Instruction> &inst) {
                                 if (!inst->HasResult() || dynamic_cast<CallInstruction *>(inst.get()) ||
                                     dynamic_cast<InvokeInstruction *>(inst.get()))
                                   return false;
                                 return live.count(inst->name) == 0;
                               }),
                insts.end());
    bb_ptr->phis.erase(std::remove_if(bb_ptr->phis.begin(), bb_ptr->phis.end(),
                                      [&](const std::shared_ptr<PhiInstruction> &phi) {
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
        auto it = std::find_if(phi->incomings.begin(), phi->incomings.end(),
                               [&](auto &inc) { return inc.first == pred; });
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
  // Rebuild after each structural change. Cached predecessors/successors must
  // never outlive a removed block or a replaced terminator.
  for (;;) {
    auto cfg = BuildCFG(func);
    std::unordered_set<BasicBlock *> reachable;
    std::vector<BasicBlock *> work;
    if (cfg.entry)
      work.push_back(cfg.entry);
    while (!work.empty()) {
      auto *block = work.back();
      work.pop_back();
      if (!reachable.insert(block).second)
        continue;
      for (auto *next : block->successors)
        work.push_back(next);
    }
    const auto old_size = func.blocks.size();
    std::erase_if(func.blocks, [&](const auto &block) { return !reachable.count(block.get()); });
    if (func.blocks.size() != old_size)
      BuildCFG(func);
    RepairPhis(func);

    bool changed = false;
    for (const auto &block : func.blocks) {
      auto *branch = dynamic_cast<CondBranchStatement *>(block->terminator.get());
      if (!branch)
        continue;
      long long condition = 0;
      const bool constant =
          TryParseConst(branch->operands.empty() ? "" : branch->operands[0], condition);
      if (!constant && branch->true_target != branch->false_target)
        continue;
      // Read the old targets before replacing their owning shared_ptr.
      auto *target =
          constant ? (condition ? branch->true_target : branch->false_target) : branch->true_target;
      auto replacement = std::make_shared<BranchStatement>();
      replacement->target = target;
      block->SetTerminator(replacement);
      changed = true;
    }
    if (changed)
      continue;

    for (const auto &block : func.blocks) {
      auto *branch = dynamic_cast<BranchStatement *>(block->terminator.get());
      auto *next = branch ? branch->target : nullptr;
      if (!next || next == block.get() || next == func.entry || next->predecessors.size() != 1)
        continue;
      std::unordered_map<std::string, std::string> replacements;
      bool complete_phis = true;
      for (const auto &phi : next->phis) {
        if (phi->incomings.size() != 1 || phi->incomings[0].first != block.get()) {
          complete_phis = false;
          break;
        }
        replacements[phi->name] = phi->incomings[0].second;
      }
      if (!complete_phis)
        continue;
      ApplySubstitutions(func, replacements);
      for (const auto &instruction : next->instructions)
        block->AddInstruction(instruction);
      block->SetTerminator(next->terminator);
      for (const auto &destination : func.blocks)
        for (const auto &phi : destination->phis)
          for (auto &incoming : phi->incomings)
            if (incoming.first == next)
              incoming.first = block.get();
      std::erase_if(func.blocks, [&](const auto &candidate) { return candidate.get() == next; });
      changed = true;
      break;
    }
    if (changed)
      continue;

    for (const auto &block : func.blocks) {
      if (block.get() == func.entry || !block->phis.empty() || !block->instructions.empty())
        continue;
      auto *branch = dynamic_cast<BranchStatement *>(block->terminator.get());
      auto *target = branch ? branch->target : nullptr;
      if (!target || target == block.get())
        continue;
      // A predecessor already reaching the target may carry a different phi
      // value on that edge. Such a forwarding block cannot be removed.
      bool safe = true;
      for (const auto &phi : target->phis) {
        const auto edge =
            std::find_if(phi->incomings.begin(), phi->incomings.end(),
                         [&](const auto &incoming) { return incoming.first == block.get(); });
        if (edge == phi->incomings.end()) {
          safe = false;
          break;
        }
        for (auto *pred : block->predecessors)
          for (const auto &incoming : phi->incomings)
            if (incoming.first == pred && incoming.second != edge->second)
              safe = false;
      }
      if (!safe)
        continue;
      for (const auto &phi : target->phis) {
        const auto edge =
            std::find_if(phi->incomings.begin(), phi->incomings.end(),
                         [&](const auto &incoming) { return incoming.first == block.get(); });
        const auto value = edge->second;
        std::erase_if(phi->incomings,
                      [&](const auto &incoming) { return incoming.first == block.get(); });
        for (auto *pred : block->predecessors)
          if (std::none_of(phi->incomings.begin(), phi->incomings.end(),
                           [&](const auto &incoming) { return incoming.first == pred; }))
            phi->incomings.push_back({pred, value});
      }
      for (auto *pred : block->predecessors) {
        if (auto *br = dynamic_cast<BranchStatement *>(pred->terminator.get())) {
          if (br->target == block.get())
            br->target = target;
        } else if (auto *br = dynamic_cast<CondBranchStatement *>(pred->terminator.get())) {
          if (br->true_target == block.get())
            br->true_target = target;
          if (br->false_target == block.get())
            br->false_target = target;
        } else if (auto *sw = dynamic_cast<SwitchStatement *>(pred->terminator.get())) {
          for (auto &item : sw->cases)
            if (item.target == block.get())
              item.target = target;
          if (sw->default_target == block.get())
            sw->default_target = target;
        }
      }
      auto *removed = block.get();
      std::erase_if(func.blocks, [&](const auto &candidate) { return candidate.get() == removed; });
      changed = true;
      break;
    }
    if (!changed)
      break;
  }
  BuildCFG(func);
  RepairPhis(func);
}

void SimplifyCFG(Function &func) {
  CanonicalizeCFG(func);
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
      bool all_same = std::all_of(phi->incomings.begin(), phi->incomings.end(),
                                  [&](auto &inc) { return inc.second == first; });
      if (all_same && phi->HasResult()) {
        subst[phi->name] = first;
        it = phis.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (!subst.empty())
    ApplySubstitutions(func, subst);
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
    bool operator==(const Key &o) const {
      return op == o.op && a == o.a && b == o.b && type == o.type;
    }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const {
      return std::hash<int>()(static_cast<int>(k.op)) ^ std::hash<std::string>()(k.a) ^
             (std::hash<std::string>()(k.b) << 1) ^ std::hash<int>()(static_cast<int>(k.type.kind));
    }
  };
  std::unordered_map<Key, std::string, KeyHash> table;

  for (auto &bb_ptr : func.blocks) {
    table.clear(); // availability is local until dominance is proven
    std::unordered_map<std::string, std::string> avail_loads;
    for (auto &inst : bb_ptr->instructions) {
      if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
        if (bin->operands.size() < 2 || !bin->HasResult())
          continue;
        bool comm =
            bin->op == BinaryInstruction::Op::kAdd || bin->op == BinaryInstruction::Op::kMul ||
            bin->op == BinaryInstruction::Op::kAnd || bin->op == BinaryInstruction::Op::kOr ||
            bin->op == BinaryInstruction::Op::kCmpEq;
        std::string a = bin->operands[0];
        std::string b = bin->operands[1];
        if (comm && b < a)
          std::swap(a, b);
        Key k{bin->op, a, b, bin->type};
        auto it = table.find(k);
        if (it != table.end()) {
          subst[bin->name] = it->second;
        } else {
          table[k] = bin->name;
        }
      } else if (auto *load = dynamic_cast<LoadInstruction *>(inst.get())) {
        if (!load->HasResult() || load->operands.empty())
          continue;
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
  if (!subst.empty())
    ApplySubstitutions(func, subst);
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
  if (!subst.empty())
    ApplySubstitutions(func, subst);
  if (!erase.empty())
    RemoveInstructions(func, erase);
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
    if (executable.insert(bb).second)
      worklist.push_back(bb);
  };

  auto get = [&](const std::string &name) -> Lattice {
    auto it = value.find(name);
    if (it != value.end())
      return it->second;
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
    if (!executable.count(bb))
      continue;

    for (auto &phi : bb->phis) {
      if (!phi->HasResult())
        continue;
      Lattice acc;
      acc.state = Lattice::State::kUnknown;
      for (auto &inc : phi->incomings) {
        if (!inc.first || executable.count(inc.first) == 0)
          continue;
        acc = acc.state == Lattice::State::kUnknown ? get(inc.second) : acc;
        MergeConst(acc, get(inc.second));
        if (acc.state == Lattice::State::kOverdefined)
          break;
      }
      MergeConst(value[phi->name], acc);
    }

    for (auto &inst : bb->instructions) {
      if (!inst->HasResult())
        continue;
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
            case BinaryInstruction::Op::kAdd:
              res = av + bv;
              break;
            case BinaryInstruction::Op::kSub:
              res = av - bv;
              break;
            case BinaryInstruction::Op::kMul:
              res = av * bv;
              break;
            case BinaryInstruction::Op::kDiv:
              res = bv != 0.0 ? av / bv : av;
              break;
            case BinaryInstruction::Op::kCmpEq:
              set_const_int(bin->name, av == bv);
              continue;
            case BinaryInstruction::Op::kCmpLt:
              set_const_int(bin->name, av < bv);
              continue;
            default:
              break;
            }
            set_const_float(bin->name, res);
          } else {
            long long av =
                (a.state == Lattice::State::kConstInt) ? a.i : static_cast<long long>(a.f);
            long long bv =
                (b.state == Lattice::State::kConstInt) ? b.i : static_cast<long long>(b.f);
            long long res = 0;
            switch (bin->op) {
            case BinaryInstruction::Op::kAdd:
              res = av + bv;
              break;
            case BinaryInstruction::Op::kSub:
              res = av - bv;
              break;
            case BinaryInstruction::Op::kMul:
              res = av * bv;
              break;
            case BinaryInstruction::Op::kDiv:
              res = bv != 0 ? av / bv : av;
              break;
            case BinaryInstruction::Op::kCmpEq:
              res = (av == bv);
              break;
            case BinaryInstruction::Op::kCmpLt:
              res = (av < bv);
              break;
            default:
              break;
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
            if (cond.i)
              mark_exec(cbr->true_target);
            else
              mark_exec(cbr->false_target);
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
            if (!hit)
              mark_exec(sw->default_target);
            continue;
          }
        }
      }
      for (auto *succ : bb->successors)
        mark_exec(succ);
    }
  }

  std::unordered_map<std::string, std::string> subst;
  std::unordered_set<Instruction *> erase;
  for (auto &bb_ptr : func.blocks) {
    for (auto &phi : bb_ptr->phis) {
      if (!phi->HasResult())
        continue;
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
      if (!inst->HasResult())
        continue;
      auto it = value.find(inst->name);
      if (it == value.end())
        continue;
      if (it->second.state == Lattice::State::kConstInt) {
        subst[inst->name] = std::to_string(it->second.i);
        erase.insert(inst.get());
      } else if (it->second.state == Lattice::State::kConstFloat) {
        subst[inst->name] = std::to_string(it->second.f);
        erase.insert(inst.get());
      }
    }
  }

  if (!subst.empty())
    ApplySubstitutions(func, subst);
  if (!erase.empty())
    RemoveInstructions(func, erase);
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
  if (promotable.empty())
    return;

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
      if (it == df.end())
        continue;
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
      if (it_slot == phi_slot.end())
        continue;
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
        if (it_slot == phi_slot.end())
          continue;
        auto &state = states[it_slot->second];
        std::string current = state.stack.empty() ? state.slot : state.stack.back();
        for (auto &inc : phi->incomings) {
          if (inc.first == bb)
            inc.second = current;
        }
      }
    }

    auto child_it = dom.children.find(bb);
    if (child_it != dom.children.end()) {
      for (auto *child : child_it->second)
        rename_block(child);
    }

    // pop definitions from this block in reverse
    for (auto it = bb->instructions.rbegin(); it != bb->instructions.rend(); ++it) {
      if (auto *store = dynamic_cast<StoreInstruction *>(it->get())) {
        if (store->operands.size() >= 2 && states.count(store->operands[0])) {
          auto &state = states[store->operands[0]];
          if (!state.stack.empty())
            state.stack.pop_back();
        }
      }
    }
    for (auto it = bb->phis.rbegin(); it != bb->phis.rend(); ++it) {
      auto it_slot = phi_slot.find(it->get());
      if (it_slot != phi_slot.end()) {
        auto &state = states[it_slot->second];
        if (!state.stack.empty())
          state.stack.pop_back();
      }
    }
  };

  if (cfg.entry)
    rename_block(cfg.entry);

  if (!subst.empty())
    ApplySubstitutions(func, subst);
  if (!erase.empty())
    RemoveInstructions(func, erase);
  DeadCodeEliminate(func);
}

void RunDefaultOptimizations(Function &func) {
  std::string verify_msg;
  if (!Verify(func, &verify_msg)) {
    return; // refuse to optimize ill-formed IR
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

} // namespace polyglot::ir::passes
