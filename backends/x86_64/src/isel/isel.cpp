#include <stdexcept>
/**
 * @file     isel.cpp
 * @brief    x86-64 code generation implementation
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <cstdlib>
#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/data_layout.h"

#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {
namespace {

bool ParseImmediate(const std::string &text, long long *value) {
  char *end = nullptr;
  long long v = std::strtoll(text.c_str(), &end, 0);
  if (end == text.c_str() || *end != '\0')
    return false;
  if (value)
    *value = v;
  return true;
}

void SetCost(MachineInstr &mi, const CostModel &model) {
  mi.cost = model.Cost(mi.opcode);
  mi.latency = model.Latency(mi.opcode);
}

Opcode ToOpcode(ir::BinaryInstruction::Op op) {
  using Op = ir::BinaryInstruction::Op;
  switch (op) {
  case Op::kAdd:
    return Opcode::kAdd;
  case Op::kSub:
    return Opcode::kSub;
  case Op::kMul:
    return Opcode::kMul;
  case Op::kDiv:
  case Op::kSDiv:
    return Opcode::kSDiv;
  case Op::kUDiv:
    return Opcode::kUDiv;
  case Op::kRem:
  case Op::kSRem:
    return Opcode::kSRem;
  case Op::kURem:
    return Opcode::kURem;
  // Floating-point operations
  case Op::kFAdd:
    return Opcode::kAddsd;
  case Op::kFSub:
    return Opcode::kSubsd;
  case Op::kFMul:
    return Opcode::kMulsd;
  case Op::kFDiv:
    return Opcode::kDivsd;
  case Op::kFRem:
    return Opcode::kDivsd; // No direct frem, use divsd
  // Logical
  case Op::kAnd:
    return Opcode::kAnd;
  case Op::kOr:
    return Opcode::kOr;
  case Op::kXor:
    return Opcode::kXor;
  case Op::kShl:
    return Opcode::kShl;
  case Op::kLShr:
    return Opcode::kLShr;
  case Op::kAShr:
    return Opcode::kAShr;
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
  case Op::kCmpLt:
    return Opcode::kCmp;
  case Op::kCmpFoe:
  case Op::kCmpFne:
  case Op::kCmpFlt:
  case Op::kCmpFle:
  case Op::kCmpFgt:
  case Op::kCmpFge:
    return Opcode::kCmpsd;
  }
  return Opcode::kAdd;
}

IntComparePredicate ToIntComparePredicate(ir::BinaryInstruction::Op op) {
  using Op = ir::BinaryInstruction::Op;
  switch (op) {
  case Op::kCmpEq:
    return IntComparePredicate::kEq;
  case Op::kCmpNe:
    return IntComparePredicate::kNe;
  case Op::kCmpUlt:
    return IntComparePredicate::kUlt;
  case Op::kCmpUle:
    return IntComparePredicate::kUle;
  case Op::kCmpUgt:
    return IntComparePredicate::kUgt;
  case Op::kCmpUge:
    return IntComparePredicate::kUge;
  case Op::kCmpSle:
    return IntComparePredicate::kSle;
  case Op::kCmpSgt:
    return IntComparePredicate::kSgt;
  case Op::kCmpSge:
    return IntComparePredicate::kSge;
  case Op::kCmpSlt:
  case Op::kCmpLt:
  default:
    return IntComparePredicate::kSlt;
  }
}

std::size_t AlignTo(std::size_t value, std::size_t alignment) {
  if (alignment <= 1)
    return value;
  const std::size_t remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

/// Compute the byte displacement represented by the IR's constant-index GEP.
/// This deliberately follows ResolveGEPResultType's pointer-index convention:
/// a pointer/reference index selects its pointee, while array and struct
/// indices contribute storage offsets.
std::optional<long long> ComputeGEPByteOffset(
    ir::IRType source_type, const std::vector<std::size_t> &indices,
    const ir::DataLayout &layout) {
  ir::IRType current =
      (source_type.kind == ir::IRTypeKind::kPointer ||
       source_type.kind == ir::IRTypeKind::kReference)
          ? source_type
          : ir::IRType::Pointer(source_type);
  std::size_t offset = 0;

  for (std::size_t index : indices) {
    switch (current.kind) {
    case ir::IRTypeKind::kPointer:
    case ir::IRTypeKind::kReference:
      if (current.subtypes.empty())
        return std::nullopt;
      current = ir::IRType(current.subtypes.front());
      break;
    case ir::IRTypeKind::kArray:
    case ir::IRTypeKind::kVector: {
      if (current.subtypes.empty() || index >= current.count)
        return std::nullopt;
      const ir::IRType element = current.subtypes.front();
      const std::size_t stride = current.kind == ir::IRTypeKind::kArray
                                     ? AlignTo(layout.SizeOf(element),
                                               layout.AlignOf(element))
                                     : layout.SizeOf(element);
      offset += index * stride;
      current = element;
      break;
    }
    case ir::IRTypeKind::kStruct: {
      if (index >= current.subtypes.size())
        return std::nullopt;
      std::size_t field_offset = 0;
      for (std::size_t field = 0; field < index; ++field) {
        field_offset = AlignTo(field_offset, layout.AlignOf(current.subtypes[field]));
        field_offset += layout.SizeOf(current.subtypes[field]);
      }
      field_offset = AlignTo(field_offset, layout.AlignOf(current.subtypes[index]));
      offset += field_offset;
      current = ir::IRType(current.subtypes[index]);
      break;
    }
    default:
      return std::nullopt;
    }
  }
  return static_cast<long long>(offset);
}

ir::IRType PointeeType(const ir::IRType &type) {
  if ((type.kind == ir::IRTypeKind::kPointer ||
       type.kind == ir::IRTypeKind::kReference) &&
      !type.subtypes.empty())
    return type.subtypes.front();
  return ir::IRType::Invalid();
}

} // namespace

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
  const ir::DataLayout data_layout(ir::DataLayout::Arch::kX86_64);
  std::unordered_map<std::string, int> vreg_for_name;
  int next_vreg = 0;

  // IR block names are intentionally human-readable and repeat in every
  // function ("entry", "if.then", ...).  Machine/object labels must be
  // unique across the whole translation unit, otherwise a relocation in one
  // function can bind to a same-named block in another function.
  std::unordered_map<const ir::BasicBlock *, std::string> block_labels;
  for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
    block_labels.emplace(fn.blocks[i].get(),
                         fn.name + ".__bb" + std::to_string(i));
  }
  auto block_label = [&](const ir::BasicBlock *bb) -> std::string {
    auto it = block_labels.find(bb);
    return it == block_labels.end() ? std::string{} : it->second;
  };

  // Map SSA name -> IRType for float detection.
  std::unordered_map<std::string, ir::IRType> value_types;
  for (size_t i = 0; i < fn.params.size(); ++i) {
    value_types[fn.params[i]] =
        (i < fn.param_types.size()) ? fn.param_types[i] : ir::IRType::Invalid();
  }
  for (auto &bb_ptr : fn.blocks) {
    for (auto &phi : bb_ptr->phis) {
      if (phi->HasResult())
        value_types[phi->name] = phi->type;
    }
    for (auto &inst : bb_ptr->instructions) {
      if (inst->HasResult())
        value_types[inst->name] = inst->type;
    }
  }

  auto get_vreg = [&](const std::string &name) -> int {
    auto it = vreg_for_name.find(name);
    if (it != vreg_for_name.end())
      return it->second;
    int id = next_vreg++;
    vreg_for_name[name] = id;
    return id;
  };

  auto make_operand = [&](const std::string &name) -> Operand {
    long long imm{};
    if (ParseImmediate(name, &imm))
      return Operand::Imm(imm);
    bool is_float = false;
    auto t_it = value_types.find(name);
    if (t_it != value_types.end())
      is_float = t_it->second.IsFloat();
    auto it = vreg_for_name.find(name);
    if (it != vreg_for_name.end())
      return Operand::VReg(it->second, is_float);
    int id = get_vreg(name);
    return Operand::VReg(id, is_float);
  };

  auto add_use_if_vreg = [&](MachineInstr &mi, const std::string &name) {
    long long imm{};
    if (ParseImmediate(name, &imm))
      return;
    mi.uses.push_back(get_vreg(name));
  };

  for (std::size_t i = 0; i < fn.params.size(); ++i) {
    mf.param_vregs.push_back(get_vreg(fn.params[i]));
    mf.param_is_float.push_back(i < fn.param_types.size() && fn.param_types[i].IsFloat());
  }

  for (auto &bb_ptr : fn.blocks) {
    MachineBasicBlock mbb;
    mbb.name = block_label(bb_ptr.get());
    std::vector<MachineBasicBlock> extra_blocks;

    for (auto &inst_ptr : bb_ptr->instructions) {
      if (auto *constant = dynamic_cast<ir::ConstantInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kMov;
        mi.operands = {Operand::Imm(static_cast<long long>(constant->bits))};
        mi.def = get_vreg(constant->name);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }
      if (auto *alloca = dynamic_cast<ir::AllocaInstruction *>(inst_ptr.get())) {
        const ir::IRType allocated_type = PointeeType(alloca->type);
        // Keep every alloca as real storage at O0.  A previous scalar
        // forwarding shortcut remembered only the last store seen in layout
        // order; loads in a loop header therefore kept seeing the initializer
        // and source-level WHILE/FOR loops never advanced.  mem2reg may remove
        // these slots at optimized levels, but instruction selection must
        // preserve control-flow semantics on its own.
        const std::size_t size = data_layout.SizeOf(allocated_type);
        const std::size_t alignment = data_layout.AlignOf(allocated_type);
        const int object_index = static_cast<int>(mf.stack_objects.size());
        mf.stack_objects.push_back({std::max<std::size_t>(size, 1),
                                    std::max<std::size_t>(alignment, 1)});

        MachineInstr address;
        address.opcode = Opcode::kLea;
        address.def = get_vreg(alloca->name);
        address.operands = {Operand::StackObject(object_index)};
        SetCost(address, cost_model);
        mbb.instructions.push_back(std::move(address));
        continue;
      }

      if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = ToOpcode(bin->op);
        add_use_if_vreg(mi, bin->operands[0]);
        add_use_if_vreg(mi, bin->operands[1]);
        mi.operands = {make_operand(bin->operands[0]), make_operand(bin->operands[1])};
        if (mi.opcode == Opcode::kCmp) {
          mi.operands.push_back(
              Operand::Imm(static_cast<long long>(ToIntComparePredicate(bin->op))));
        }
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
        if (!imm_base)
          add_use_if_vreg(mi, load->operands[0]);
        int base = imm_base ? 0 : get_vreg(load->operands[0]);
        mi.operands = {Operand::MemVReg(base)};
        mi.memory_width = std::max<std::size_t>(data_layout.SizeOf(load->type), 1);
        mi.memory_signed = load->type.IsSigned();
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *store = dynamic_cast<ir::StoreInstruction *>(inst_ptr.get())) {
        MachineInstr mi;
        mi.opcode = Opcode::kStore;
        bool imm_base = ParseImmediate(store->operands[0], nullptr);
        if (!imm_base)
          add_use_if_vreg(mi, store->operands[0]);
        add_use_if_vreg(mi, store->operands[1]);
        int base = imm_base ? 0 : get_vreg(store->operands[0]);
        mi.operands = {Operand::MemVReg(base), make_operand(store->operands[1])};
        ir::IRType stored_type = ir::IRType::Invalid();
        const auto value_type = value_types.find(store->operands[1]);
        if (value_type != value_types.end())
          stored_type = value_type->second;
        if (stored_type.kind == ir::IRTypeKind::kInvalid) {
          const auto address_type = value_types.find(store->operands[0]);
          if (address_type != value_types.end())
            stored_type = PointeeType(address_type->second);
        }
        mi.memory_width =
            std::max<std::size_t>(data_layout.SizeOf(stored_type), 1);
        SetCost(mi, cost_model);
        mbb.instructions.push_back(std::move(mi));
        continue;
      }

      if (auto *cast = dynamic_cast<ir::CastInstruction *>(inst_ptr.get())) {
        if (cast->cast == ir::CastInstruction::CastKind::kSiToFp ||
            cast->cast == ir::CastInstruction::CastKind::kUiToFp)
          throw std::runtime_error("integer-to-float conversion requires the ARM64 native emitter");
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
        // PE-7-C: An argument name that is neither an immediate nor an SSA
        // value defined inside this function must be an external/global
        // symbol (e.g. the `.ptr` alias materialised by
        // `IRBuilder::MakeStringLiteral` for `polyrt_println`). For each
        // such reference we synthesise a `lea reg, [rip+0]` defining
        // instruction so the register allocator sees a producer for the
        // call's vreg, and so the asm emitter can attach a REL32
        // relocation that polyld will resolve against the corresponding
        // `.rdata` symbol.
        for (const auto &arg : call->operands) {
          long long imm{};
          if (ParseImmediate(arg, &imm))
            continue;
          if (value_types.count(arg))
            continue;
          if (vreg_for_name.count(arg))
            continue;
          MachineInstr lea_mi;
          lea_mi.opcode = Opcode::kLea;
          lea_mi.def = get_vreg(arg);
          lea_mi.operands = {Operand::Label(arg)};
          SetCost(lea_mi, cost_model);
          mbb.instructions.push_back(std::move(lea_mi));
        }
        MachineInstr mi;
        mi.opcode = Opcode::kCall;
        for (auto &arg : call->operands) {
          add_use_if_vreg(mi, arg);
          mi.operands.push_back(make_operand(arg));
        }
        mi.operands.push_back(Operand::Label(call->callee));
        if (call->HasResult())
          mi.def = get_vreg(call->name);
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
        const long long displacement =
            ComputeGEPByteOffset(gep->source_type, gep->indices, data_layout)
                .value_or(0);
        mi.operands = {Operand::MemVReg(mi.uses[0], displacement)};
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
          mi.opcode = Opcode::kMovaps; // Default fallback
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

        if (vec->HasResult())
          mi.def = get_vreg(vec->name);
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
      mi.operands.push_back(Operand::Label(block_label(br->target)));
      SetCost(mi, cost_model);
      mbb.instructions.push_back(std::move(mi));
    } else if (auto *cbr = dynamic_cast<ir::CondBranchStatement *>(bb_ptr->terminator.get())) {
      MachineInstr cmp;
      cmp.opcode = Opcode::kCmp;
      cmp.uses = {get_vreg(cbr->operands[0])};
      cmp.operands = {make_operand(cbr->operands[0]), Operand::Imm(0)};
      // Keep this flags-producing compare adjacent to its Jcc after the
      // list scheduler moves ordinary instructions around.
      cmp.terminator = true;
      SetCost(cmp, cost_model);
      mbb.instructions.push_back(std::move(cmp));

      MachineInstr mi;
      mi.opcode = Opcode::kJcc;
      mi.terminator = true;
      mi.operands.push_back(Operand::Label(block_label(cbr->true_target)));
      mi.operands.push_back(Operand::Label(block_label(cbr->false_target)));
      SetCost(mi, cost_model);
      mbb.instructions.push_back(std::move(mi));
    } else if (auto *sw = dynamic_cast<ir::SwitchStatement *>(bb_ptr->terminator.get())) {
      if (sw->cases.empty()) {
        MachineInstr jm;
        jm.opcode = Opcode::kJmp;
        jm.terminator = true;
        jm.operands.push_back(Operand::Label(block_label(sw->default_target)));
        SetCost(jm, cost_model);
        mbb.instructions.push_back(std::move(jm));
      } else {
        // Lower a switch to a chain of equality-test blocks.  Every kJcc has
        // both a true and a false target, so a failed case advances to the
        // next comparison instead of jumping straight to default.
        std::vector<std::string> chain_labels(sw->cases.size());
        chain_labels[0] = mbb.name;
        for (std::size_t i = 1; i < sw->cases.size(); ++i) {
          chain_labels[i] = mbb.name + ".__switch" + std::to_string(i);
          MachineBasicBlock next;
          next.name = chain_labels[i];
          extra_blocks.push_back(std::move(next));
        }

        for (std::size_t i = 0; i < sw->cases.size(); ++i) {
          MachineBasicBlock &case_block = i == 0 ? mbb : extra_blocks[i - 1];
          const auto &c = sw->cases[i];
          const std::string cmp_name =
              mbb.name + ".__switch_eq" + std::to_string(i);
          const int cmp_vreg = get_vreg(cmp_name);

          MachineInstr value_cmp;
          value_cmp.opcode = Opcode::kCmp;
          add_use_if_vreg(value_cmp, sw->operands[0]);
          value_cmp.operands = {
              make_operand(sw->operands[0]), Operand::Imm(c.value),
              Operand::Imm(static_cast<long long>(IntComparePredicate::kEq))};
          value_cmp.def = cmp_vreg;
          SetCost(value_cmp, cost_model);
          case_block.instructions.push_back(std::move(value_cmp));

          MachineInstr flags_cmp;
          flags_cmp.opcode = Opcode::kCmp;
          flags_cmp.uses = {cmp_vreg};
          flags_cmp.operands = {Operand::VReg(cmp_vreg), Operand::Imm(0)};
          flags_cmp.terminator = true;
          SetCost(flags_cmp, cost_model);
          case_block.instructions.push_back(std::move(flags_cmp));

          MachineInstr jcc;
          jcc.opcode = Opcode::kJcc;
          jcc.terminator = true;
          jcc.operands.push_back(Operand::Label(block_label(c.target)));
          jcc.operands.push_back(Operand::Label(
              i + 1 < sw->cases.size() ? chain_labels[i + 1]
                                       : block_label(sw->default_target)));
          SetCost(jcc, cost_model);
          case_block.instructions.push_back(std::move(jcc));
        }
      }
    }

    mf.blocks.push_back(std::move(mbb));
    for (auto &extra : extra_blocks)
      mf.blocks.push_back(std::move(extra));
  }

  return mf;
}

} // namespace polyglot::backends::x86_64
