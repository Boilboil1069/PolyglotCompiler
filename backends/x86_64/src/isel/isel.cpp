/**
 * @file     isel.cpp
 * @brief    x86-64 code generation implementation
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "backends/x86_64/include/machine_ir.h"

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

#include "middle/include/ir/nodes/statements.h"

namespace polyglot::backends::x86_64 {
namespace {

bool ParseImmediate(const std::string &text, long long *value) {
  char *end = nullptr;
  long long v = std::strtoll(text.c_str(), &end, 0);
  if (end == text.c_str() || *end != '\0') return false;
  if (value) *value = v;
  return true;
}

void SetCost(MachineInstr &mi, const CostModel &model) {
  mi.cost = model.Cost(mi.opcode);
  mi.latency = model.Latency(mi.opcode);
}

Opcode ToOpcode(ir::BinaryInstruction::Op op) {
  using Op = ir::BinaryInstruction::Op;
  switch (op) {
    case Op::kAdd: return Opcode::kAdd;
    case Op::kSub: return Opcode::kSub;
    case Op::kMul: return Opcode::kMul;
    case Op::kDiv:
    case Op::kSDiv: return Opcode::kSDiv;
    case Op::kUDiv: return Opcode::kUDiv;
    case Op::kRem:
    case Op::kSRem: return Opcode::kSRem;
    case Op::kURem: return Opcode::kURem;
    // Floating-point operations
    case Op::kFAdd: return Opcode::kAddsd;
    case Op::kFSub: return Opcode::kSubsd;
    case Op::kFMul: return Opcode::kMulsd;
    case Op::kFDiv: return Opcode::kDivsd;
    case Op::kFRem: return Opcode::kDivsd;  // No direct frem, use divsd
    // Logical
    case Op::kAnd: return Opcode::kAnd;
    case Op::kOr: return Opcode::kOr;
    case Op::kXor: return Opcode::kXor;
    case Op::kShl: return Opcode::kShl;
    case Op::kLShr: return Opcode::kLShr;
    case Op::kAShr: return Opcode::kAShr;
    // Comparisons
    case Op::kCmpEq:
    case Op::kCmpNe:
    case Op::kCmpUlt:
    case Op::kCmpUle:
    case Op::kCmpUgt:
    case Op::kCmpUge:
    case Op::kCmpSlt:
    case Op::kCmpSle:
    case Op::kCmpSgt:
    case Op::kCmpSge:
    case Op::kCmpLt: return Opcode::kCmp;
    case Op::kCmpFoe:
    case Op::kCmpFne:
    case Op::kCmpFlt:
    case Op::kCmpFle:
    case Op::kCmpFgt:
    case Op::kCmpFge: return Opcode::kCmpsd;
  }
  return Opcode::kAdd;
}

}  // namespace

int CostModel::Cost(Opcode op) const {
  switch (op) {
    case Opcode::kAdd:
    case Opcode::kSub:
    case Opcode::kMov:
    case Opcode::kShl:
    case Opcode::kLShr:
    case Opcode::kAShr:
      return 1;
    case Opcode::kAnd:
    case Opcode::kOr:
    case Opcode::kXor:
    case Opcode::kCmp:
    case Opcode::kLoad:
    case Opcode::kStore:
    case Opcode::kLea:
      return 2;
    case Opcode::kMul:
      return 3;
    case Opcode::kDiv:
    case Opcode::kSDiv:
    case Opcode::kUDiv:
    case Opcode::kRem:
    case Opcode::kSRem:
    case Opcode::kURem:
      return 8;
    // Floating-point operations (typically same as integer)
    case Opcode::kMovsd:
    case Opcode::kMovss:
    case Opcode::kAddsd:
    case Opcode::kSubsd:
      return 1;
    case Opcode::kMulsd:
      return 3;
    case Opcode::kDivsd:
      return 8;
    case Opcode::kCmpsd:
      return 2;
    // SIMD operations
    case Opcode::kMovaps:
    case Opcode::kMovups:
    case Opcode::kAddps:
    case Opcode::kSubps:
      return 1;
    case Opcode::kMulps:
      return 3;
    case Opcode::kDivps:
      return 8;
    case Opcode::kShufps:
      return 1;
    case Opcode::kCall:
      return 12;
    case Opcode::kRet:
    case Opcode::kJmp:
    case Opcode::kJcc:
      return 1;
  }
  return 1;
}

int CostModel::Latency(Opcode op) const {
  switch (op) {
    case Opcode::kMul:
      return 4;
    case Opcode::kDiv:
    case Opcode::kSDiv:
    case Opcode::kUDiv:
    case Opcode::kRem:
    case Opcode::kSRem:
    case Opcode::kURem:
      return 10;
    case Opcode::kLoad:
    case Opcode::kStore:
      return 3;
    case Opcode::kCall:
      return 6;
    default:
      return 1;
  }
}

MachineFunction SelectInstructions(const ir::Function &fn, const CostModel &cost_model) {
  MachineFunction mf;
  mf.name = fn.name;
  std::unordered_map<std::string, int> vreg_for_name;
  int next_vreg = 0;

  // Map SSA name -> IRType for float detection.
  std::unordered_map<std::string, ir::IRType> value_types;
  for (size_t i = 0; i < fn.params.size(); ++i) {
    value_types[fn.params[i]] = (i < fn.param_types.size()) ? fn.param_types[i] : ir::IRType::Invalid();
  }
  for (auto &bb_ptr : fn.blocks) {
    for (auto &phi : bb_ptr->phis) {
      if (phi->HasResult()) value_types[phi->name] = phi->type;
    }
    for (auto &inst : bb_ptr->instructions) {
      if (inst->HasResult()) value_types[inst->name] = inst->type;
    }
  }

  auto get_vreg = [&](const std::string &name) -> int {
    auto it = vreg_for_name.find(name);
    if (it != vreg_for_name.end()) return it->second;
    int id = next_vreg++;
    vreg_for_name[name] = id;
    return id;
  };

  auto make_operand = [&](const std::string &name) -> Operand {
    long long imm{};
    if (ParseImmediate(name, &imm)) return Operand::Imm(imm);
    bool is_float = false;
    auto t_it = value_types.find(name);
    if (t_it != value_types.end()) is_float = t_it->second.IsFloat();
    auto it = vreg_for_name.find(name);
    if (it != vreg_for_name.end()) return Operand::VReg(it->second, is_float);
    int id = get_vreg(name);
    return Operand::VReg(id, is_float);
  };

  auto add_use_if_vreg = [&](MachineInstr &mi, const std::string &name) {
    long long imm{};
    if (ParseImmediate(name, &imm)) return;
    mi.uses.push_back(get_vreg(name));
  };

  for (auto &bb_ptr : fn.blocks) {
    MachineBasicBlock mbb;
    mbb.name = bb_ptr->name;

    for (auto &inst_ptr : bb_ptr->instructions) {
      if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = ToOpcode(bin->op);
        add_use_if_vreg(mi, bin->operands[0]);
        add_use_if_vreg(mi, bin->operands[1]);
        mi.operands = {make_operand(bin->operands[0]), make_operand(bin->operands[1])};
        mi.def = get_vreg(bin->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *load = dynamic_cast<ir::LoadInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kLoad;
        mi.def = get_vreg(load->name);
        bool imm_base = ParseImmediate(load->operands[0], nullptr);
        if (!imm_base) add_use_if_vreg(mi, load->operands[0]);
        int base = imm_base ? 0 : get_vreg(load->operands[0]);
        mi.operands = {Operand::MemVReg(base)};
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *store = dynamic_cast<ir::StoreInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kStore;
        bool imm_base = ParseImmediate(store->operands[0], nullptr);
        if (!imm_base) add_use_if_vreg(mi, store->operands[0]);
        add_use_if_vreg(mi, store->operands[1]);
        int base = imm_base ? 0 : get_vreg(store->operands[0]);
        mi.operands = {Operand::MemVReg(base), make_operand(store->operands[1])};
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *cast = dynamic_cast<ir::CastInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kMov;
        add_use_if_vreg(mi, cast->operands[0]);
        mi.operands = {make_operand(cast->operands[0])};
        mi.def = get_vreg(cast->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *call = dynamic_cast<ir::CallInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kCall;
        for (auto &arg : call->operands) {
          add_use_if_vreg(mi, arg);
          mi.operands.push_back(make_operand(arg));
        }
        mi.operands.push_back(Operand::Label(call->callee));
        if (call->HasResult()) mi.def = get_vreg(call->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *mc = dynamic_cast<ir::MemcpyInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kCall;
        for (auto &op : mc->operands) {
          add_use_if_vreg(mi, op);
          mi.operands.push_back(make_operand(op));
        }
        mi.operands.push_back(Operand::Label("memcpy"));
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *ms = dynamic_cast<ir::MemsetInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kCall;
        for (auto &op : ms->operands) {
          add_use_if_vreg(mi, op);
          mi.operands.push_back(make_operand(op));
        }
        mi.operands.push_back(Operand::Label("memset"));
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *gep = dynamic_cast<ir::GetElementPtrInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kLea;
        mi.uses = {get_vreg(gep->operands[0])};
        mi.operands = {Operand::MemVReg(mi.uses[0])};
        mi.def = get_vreg(gep->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      // SIMD vector instructions
      if (auto *vec = dynamic_cast<ir::VectorInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        using VecOp = ir::VectorInstruction::VecOp;
        
        switch (vec->op) {
          case VecOp::kVecAdd:
          case VecOp::kVecFAdd:
            mi.opcode = Opcode::kAddps;
            break;
          case VecOp::kVecSub:
          case VecOp::kVecFSub:
            mi.opcode = Opcode::kSubps;
            break;
          case VecOp::kVecMul:
          case VecOp::kVecFMul:
            mi.opcode = Opcode::kMulps;
            break;
          case VecOp::kVecDiv:
          case VecOp::kVecFDiv:
            mi.opcode = Opcode::kDivps;
            break;
          case VecOp::kVecShuffle:
            mi.opcode = Opcode::kShufps;
            break;
          default:
            mi.opcode = Opcode::kMovaps;  // Default fallback
            break;
        }
        
        for (auto &op : vec->operands) {
          add_use_if_vreg(mi, op);
          mi.operands.push_back(make_operand(op));
        }
        
        // Add shuffle mask if present
        if (vec->op == VecOp::kVecShuffle && !vec->shuffle_mask.empty()) {
          // Encode shuffle mask as immediate (simplified)
          int mask = 0;
          for (size_t i = 0; i < vec->shuffle_mask.size() && i < 4; ++i) {
            mask |= (vec->shuffle_mask[i] & 0x3) << (i * 2);
          }
          mi.operands.push_back(Operand::Imm(mask));
        }
        
        if (vec->HasResult()) mi.def = get_vreg(vec->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }
    }

    if (auto *ret = dynamic_cast<ir::ReturnStatement *>(bb_ptr->terminator.get())) {
      MachineInstr mi;
      mi.opcode = Opcode::kRet;
      mi.terminator = true;
      if (!ret->operands.empty()) {
        mi.uses.push_back(get_vreg(ret->operands[0]));
        mi.operands.push_back(make_operand(ret->operands[0]));
      }
      SetCost(mi, cost_model);
      mbb.instructions.push_back(std::move(mi));
    } else if (auto *br = dynamic_cast<ir::BranchStatement *>(bb_ptr->terminator.get())) {
      MachineInstr mi;
      mi.opcode = Opcode::kJmp;
      mi.terminator = true;
      mi.operands.push_back(Operand::Label(br->target ? br->target->name : ""));
      SetCost(mi, cost_model);
      mbb.instructions.push_back(std::move(mi));
    } else if (auto *cbr = dynamic_cast<ir::CondBranchStatement *>(bb_ptr->terminator.get())) {
      MachineInstr cmp;
      cmp.opcode = Opcode::kCmp;
      cmp.uses = {get_vreg(cbr->operands[0])};
      cmp.operands = {make_operand(cbr->operands[0]), Operand::Imm(0)};
      SetCost(cmp, cost_model);
      mbb.instructions.push_back(std::move(cmp));

      MachineInstr mi;
      mi.opcode = Opcode::kJcc;
      mi.terminator = true;
      mi.operands.push_back(Operand::Label(cbr->true_target ? cbr->true_target->name : ""));
      mi.operands.push_back(Operand::Label(cbr->false_target ? cbr->false_target->name : ""));
      SetCost(mi, cost_model);
      mbb.instructions.push_back(std::move(mi));
    } else if (auto *sw = dynamic_cast<ir::SwitchStatement *>(bb_ptr->terminator.get())) {
      for (auto &c : sw->cases) {
        MachineInstr cmp;
        cmp.opcode = Opcode::kCmp;
        cmp.uses = {get_vreg(sw->operands[0])};
        cmp.operands = {make_operand(sw->operands[0]), Operand::Imm(c.value)};
        SetCost(cmp, cost_model);
        mbb.instructions.push_back(std::move(cmp));

        MachineInstr jcc;
        jcc.opcode = Opcode::kJcc;
        jcc.terminator = false;
        jcc.operands.push_back(Operand::Label(c.target ? c.target->name : ""));
        jcc.operands.push_back(Operand::Label(sw->default_target ? sw->default_target->name : ""));
        SetCost(jcc, cost_model);
        mbb.instructions.push_back(std::move(jcc));
      }
      MachineInstr jm;
      jm.opcode = Opcode::kJmp;
      jm.terminator = true;
      jm.operands.push_back(Operand::Label(sw->default_target ? sw->default_target->name : ""));
      SetCost(jm, cost_model);
      mbb.instructions.push_back(std::move(jm));
    }

    mf.blocks.push_back(std::move(mbb));
  }

  return mf;
}

}  // namespace polyglot::backends::x86_64
