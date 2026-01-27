#include "backends/x86_64/include/x86_target.h"

#include <sstream>
#include <string>
#include <vector>

#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"

namespace polyglot::backends::x86_64 {
namespace {

int StackOffsetBytes(int slot) { return static_cast<int>((slot + 1) * 8); }

std::string FormatOperand(const Operand &op, const AllocationResult &alloc, int stack_bytes) {
  switch (op.kind) {
    case Operand::Kind::kImm:
      return std::to_string(op.imm);
    case Operand::Kind::kLabel:
      return op.label;
    case Operand::Kind::kPhysReg:
      return RegisterName(op.phys);
    case Operand::Kind::kStackSlot: {
      int offset = StackOffsetBytes(op.stack_slot);
      return "[rbp - " + std::to_string(offset) + "]";
    }
    case Operand::Kind::kVReg: {
      auto phys_it = alloc.vreg_to_phys.find(op.vreg);
      if (phys_it != alloc.vreg_to_phys.end()) return RegisterName(phys_it->second);
      auto slot_it = alloc.vreg_to_slot.find(op.vreg);
      if (slot_it != alloc.vreg_to_slot.end()) {
        int offset = StackOffsetBytes(slot_it->second);
        return "[rbp - " + std::to_string(offset) + "]";
      }
      return "v" + std::to_string(op.vreg);
    }
    case Operand::Kind::kMemVReg: {
      auto phys_it = alloc.vreg_to_phys.find(op.vreg);
      if (phys_it != alloc.vreg_to_phys.end()) return "[" + RegisterName(phys_it->second) + "]";
      auto slot_it = alloc.vreg_to_slot.find(op.vreg);
      if (slot_it != alloc.vreg_to_slot.end()) {
        int offset = StackOffsetBytes(slot_it->second);
        return "[rbp - " + std::to_string(offset) + "]";
      }
      return "[v" + std::to_string(op.vreg) + "]";
    }
    case Operand::Kind::kMemLabel:
      return "[" + op.label + "]";
  }
  return "";
}

void EmitBinary(const MachineInstr &mi, const AllocationResult &alloc, int stack_bytes,
                std::ostream &os, const std::string &mnemonic) {
  std::string dst = mi.def >= 0 ? FormatOperand(Operand::VReg(mi.def), alloc, stack_bytes)
                                : FormatOperand(mi.operands[0], alloc, stack_bytes);
  std::string lhs = FormatOperand(mi.operands[0], alloc, stack_bytes);
  std::string rhs = mi.operands.size() > 1 ? FormatOperand(mi.operands[1], alloc, stack_bytes) : lhs;
  if (dst != lhs) os << "  mov " << dst << ", " << lhs << "\n";
  os << "  " << mnemonic << " " << dst << ", " << rhs << "\n";
}

void EmitInstruction(const MachineInstr &mi, const AllocationResult &alloc, int stack_bytes,
                     std::ostream &os) {
  switch (mi.opcode) {
    case Opcode::kMov: {
      std::string dst = mi.def >= 0 ? FormatOperand(Operand::VReg(mi.def), alloc, stack_bytes)
                                    : FormatOperand(mi.operands[0], alloc, stack_bytes);
      std::string src = FormatOperand(mi.operands[0], alloc, stack_bytes);
      if (dst != src) os << "  mov " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kAdd:
      EmitBinary(mi, alloc, stack_bytes, os, "add");
      break;
    case Opcode::kSub:
      EmitBinary(mi, alloc, stack_bytes, os, "sub");
      break;
    case Opcode::kMul:
      EmitBinary(mi, alloc, stack_bytes, os, "imul");
      break;
    case Opcode::kDiv:
      EmitBinary(mi, alloc, stack_bytes, os, "idiv");
      break;
    case Opcode::kAnd:
      EmitBinary(mi, alloc, stack_bytes, os, "and");
      break;
    case Opcode::kOr:
      EmitBinary(mi, alloc, stack_bytes, os, "or");
      break;
    case Opcode::kCmp: {
      if (mi.operands.size() < 2) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, stack_bytes);
      std::string rhs = FormatOperand(mi.operands[1], alloc, stack_bytes);
      os << "  cmp " << lhs << ", " << rhs << "\n";
      break;
    }
    case Opcode::kLoad: {
      std::string dst = FormatOperand(Operand::VReg(mi.def), alloc, stack_bytes);
      std::string mem = FormatOperand(mi.operands[0], alloc, stack_bytes);
      os << "  mov " << dst << ", " << mem << "\n";
      break;
    }
    case Opcode::kStore: {
      if (mi.operands.size() < 2) break;
      std::string mem = FormatOperand(mi.operands[0], alloc, stack_bytes);
      std::string src = FormatOperand(mi.operands[1], alloc, stack_bytes);
      os << "  mov " << mem << ", " << src << "\n";
      break;
    }
    case Opcode::kLea: {
      std::string dst = FormatOperand(Operand::VReg(mi.def), alloc, stack_bytes);
      std::string mem = FormatOperand(mi.operands[0], alloc, stack_bytes);
      os << "  lea " << dst << ", " << mem << "\n";
      break;
    }
    case Opcode::kCall: {
      // Basic ABI: assume arguments already placed; just issue call and move return to def.
      if (!mi.operands.empty()) {
        const auto &label = mi.operands.back();
        os << "  call " << FormatOperand(label, alloc, stack_bytes) << "\n";
      }
      if (mi.def >= 0) {
        std::string dst = FormatOperand(Operand::VReg(mi.def), alloc, stack_bytes);
        if (dst != "rax") os << "  mov " << dst << ", rax\n";
      }
      break;
    }
    case Opcode::kJmp: {
      if (!mi.operands.empty()) os << "  jmp " << FormatOperand(mi.operands[0], alloc, stack_bytes) << "\n";
      break;
    }
    case Opcode::kJcc: {
      if (mi.operands.size() >= 1) os << "  jne " << FormatOperand(mi.operands[0], alloc, stack_bytes) << "\n";
      if (mi.operands.size() >= 2) os << "  jmp " << FormatOperand(mi.operands[1], alloc, stack_bytes) << "\n";
      break;
    }
    case Opcode::kRet: {
      if (!mi.operands.empty()) {
        std::string src = FormatOperand(mi.operands[0], alloc, stack_bytes);
        if (src != "rax") os << "  mov rax, " << src << "\n";
      }
      os << "  leave\n";
      os << "  ret\n";
      break;
    }
  }
}

void EmitFunction(const MachineFunction &mf, const AllocationResult &alloc, std::ostream &os) {
  int stack_bytes = alloc.stack_slots * 8;
  if (stack_bytes % 16 != 0) stack_bytes += 8;  // keep simple alignment

  os << ".globl " << mf.name << "\n";
  os << mf.name << ":\n";
  os << "  push rbp\n";
  os << "  mov rbp, rsp\n";
  if (stack_bytes > 0) os << "  sub rsp, " << stack_bytes << "\n";

  for (const auto &bb : mf.blocks) {
    os << bb.name << ":\n";
    for (const auto &mi : bb.instructions) {
      EmitInstruction(mi, alloc, stack_bytes, os);
    }
  }
}

}  // namespace

std::string X86Target::EmitAssembly() {
  std::ostringstream os;
  if (!module_ || module_->Functions().empty()) {
    os << "; no IR module available for target " << TargetTriple() << "\n";
    return os.str();
  }

  CostModel cost_model;
  std::vector<Register> available = {Register::kRax, Register::kRbx, Register::kRcx, Register::kRdx};

  for (auto &fn : module_->Functions()) {
    auto mf = SelectInstructions(*fn, cost_model);
    ScheduleFunction(mf);
    AllocationResult alloc;
    switch (regalloc_strategy_) {
      case RegAllocStrategy::kGraphColoring:
        alloc = GraphColoringAllocate(mf, available);
        break;
      case RegAllocStrategy::kLinearScan:
      default:
        alloc = LinearScanAllocate(mf, available);
        break;
    }
    EmitFunction(mf, alloc, os);
  }

  return os.str();
}

}  // namespace polyglot::backends::x86_64
