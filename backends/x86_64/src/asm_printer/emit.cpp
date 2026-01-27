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

namespace {

std::uint8_t ModRM(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
  return static_cast<std::uint8_t>((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

std::uint8_t RegCode(Register r) {
  switch (r) {
    case Register::kRax: return 0;
    case Register::kRcx: return 1;
    case Register::kRdx: return 2;
    case Register::kRbx: return 3;
    case Register::kRsp: return 4;
    case Register::kRbp: return 5;
    case Register::kRsi: return 6;
    case Register::kRdi: return 7;
    default: return 0;
  }
}

Register Resolve(const Operand &op, const AllocationResult &alloc) {
  if (op.kind == Operand::Kind::kPhysReg) return op.phys;
  if (op.kind == Operand::Kind::kVReg) {
    auto it = alloc.vreg_to_phys.find(op.vreg);
    if (it != alloc.vreg_to_phys.end()) return it->second;
  }
  return Register::kRax;
}

}  // namespace

X86Target::MCResult X86Target::EmitObjectCode() {
  MCResult result;
  if (!module_ || module_->Functions().empty()) return result;

  CostModel cost_model;
  std::vector<Register> available = {Register::kRax, Register::kRbx, Register::kRcx, Register::kRdx};

  // Gather text section data and symbols across all functions.
  MCSection text_sec;
  text_sec.name = ".text";

  std::unordered_map<std::string, std::uint32_t> label_offsets;

  for (auto &fn_ptr : module_->Functions()) {
    auto mf = SelectInstructions(*fn_ptr, cost_model);
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

    std::size_t func_start = text_sec.data.size();
    label_offsets[mf.name] = static_cast<std::uint32_t>(func_start);
    result.symbols.push_back({mf.name, ".text", static_cast<std::uint64_t>(func_start), 0, true, true});

    // Prologue
    text_sec.data.insert(text_sec.data.end(), {0x55});              // push rbp
    text_sec.data.insert(text_sec.data.end(), {0x48, 0x89, 0xE5});  // mov rbp, rsp

    // Track block labels
    for (const auto &bb : mf.blocks) {
      label_offsets[bb.name] = static_cast<std::uint32_t>(text_sec.data.size());
      result.symbols.push_back({bb.name, ".text", static_cast<std::uint64_t>(text_sec.data.size()), 0, false, true});

      for (const auto &mi : bb.instructions) {
        switch (mi.opcode) {
          case Opcode::kMov: {
            if (mi.operands.empty()) break;
            const auto &src = mi.operands[0];
            Register dst_reg = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : src, alloc);
            if (src.kind == Operand::Kind::kImm) {
              text_sec.data.push_back(0xB8 + RegCode(dst_reg));
              std::int32_t imm = static_cast<std::int32_t>(src.imm);
              for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
            } else if (src.kind == Operand::Kind::kVReg || src.kind == Operand::Kind::kPhysReg) {
              Register src_reg = Resolve(src, alloc);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x89);
              text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(dst_reg)));
            }
            break;
          }
          case Opcode::kAdd: {
            if (mi.operands.size() < 2) break;
            Register dst_reg = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            const auto &rhs = mi.operands[1];
            if (rhs.kind == Operand::Kind::kImm) {
              text_sec.data.push_back(0x83);
              text_sec.data.push_back(ModRM(0b11, 0b000, RegCode(dst_reg)));  // add r/m64, imm8
              text_sec.data.push_back(static_cast<std::uint8_t>(rhs.imm));
            } else if (rhs.kind == Operand::Kind::kVReg || rhs.kind == Operand::Kind::kPhysReg) {
              Register src_reg = Resolve(rhs, alloc);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x01);
              text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(dst_reg)));
            }
            break;
          }
          case Opcode::kSub: {
            if (mi.operands.size() < 2) break;
            Register dst_reg = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            const auto &rhs = mi.operands[1];
            if (rhs.kind == Operand::Kind::kImm) {
              text_sec.data.push_back(0x83);
              text_sec.data.push_back(ModRM(0b11, 0b101, RegCode(dst_reg)));  // sub r/m64, imm8
              text_sec.data.push_back(static_cast<std::uint8_t>(rhs.imm));
            } else if (rhs.kind == Operand::Kind::kVReg || rhs.kind == Operand::Kind::kPhysReg) {
              Register src_reg = Resolve(rhs, alloc);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x29);
              text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(dst_reg)));
            }
            break;
          }
          case Opcode::kMul: {
            if (mi.operands.empty()) break;
            Register src_reg = Resolve(mi.operands[0], alloc);
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x0F);
            text_sec.data.push_back(0xAF);
            text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(Register::kRax)));
            break;
          }
          case Opcode::kLoad: {
            if (mi.operands.empty()) break;
            Register dst_reg = Resolve(Operand::VReg(mi.def), alloc);
            const auto &mem = mi.operands[0];
            if (mem.kind == Operand::Kind::kStackSlot) {
              int offset = StackOffsetBytes(mem.stack_slot);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x8B);
              if (offset <= 127) {
                text_sec.data.push_back(ModRM(0b01, RegCode(dst_reg), RegCode(Register::kRbp)));
                text_sec.data.push_back(static_cast<std::uint8_t>(-offset));
              } else {
                text_sec.data.push_back(ModRM(0b10, RegCode(dst_reg), RegCode(Register::kRbp)));
                std::int32_t disp = -offset;
                for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
              }
            }
            break;
          }
          case Opcode::kStore: {
            if (mi.operands.size() < 2) break;
            const auto &mem = mi.operands[0];
            Register src_reg = Resolve(mi.operands[1], alloc);
            if (mem.kind == Operand::Kind::kStackSlot) {
              int offset = StackOffsetBytes(mem.stack_slot);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x89);
              if (offset <= 127) {
                text_sec.data.push_back(ModRM(0b01, RegCode(src_reg), RegCode(Register::kRbp)));
                text_sec.data.push_back(static_cast<std::uint8_t>(-offset));
              } else {
                text_sec.data.push_back(ModRM(0b10, RegCode(src_reg), RegCode(Register::kRbp)));
                std::int32_t disp = -offset;
                for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
              }
            }
            break;
          }
          case Opcode::kJmp: {
            if (mi.operands.empty()) break;
            const auto &target = mi.operands[0];
            std::size_t reloc_offset = text_sec.data.size() + 1;
            text_sec.data.push_back(0xE9);
            for (int i = 0; i < 4; ++i) text_sec.data.push_back(0x00);
            MCReloc r;
            r.section = ".text";
            r.offset = static_cast<std::uint32_t>(reloc_offset);
            r.type = 1;
            r.symbol = target.label;
            r.addend = -4;
            result.relocs.push_back(r);
            break;
          }
          case Opcode::kJcc: {
            if (mi.operands.size() < 1) break;
            const auto &target = mi.operands[0];
            std::size_t reloc_offset = text_sec.data.size() + 2;
            text_sec.data.push_back(0x0F);
            text_sec.data.push_back(0x85);  // jne rel32
            for (int i = 0; i < 4; ++i) text_sec.data.push_back(0x00);
            MCReloc r;
            r.section = ".text";
            r.offset = static_cast<std::uint32_t>(reloc_offset);
            r.type = 1;
            r.symbol = target.label;
            r.addend = -4;
            result.relocs.push_back(r);
            break;
          }
          case Opcode::kCall: {
            if (mi.operands.empty()) break;
            const auto &target = mi.operands.back();
            std::size_t reloc_offset = text_sec.data.size() + 1;
            text_sec.data.push_back(0xE8);
            for (int i = 0; i < 4; ++i) text_sec.data.push_back(0x00);
            MCReloc r;
            r.section = ".text";
            r.offset = static_cast<std::uint32_t>(reloc_offset);
            r.type = 1;
            r.symbol = target.label;
            r.addend = -4;
            result.relocs.push_back(r);
            result.symbols.push_back({target.label, "", 0, 0, true, false});
            break;
          }
          case Opcode::kRet: {
            text_sec.data.push_back(0xC9);  // leave
            text_sec.data.push_back(0xC3);  // ret
            break;
          }
          default:
            text_sec.data.push_back(0x90);
            break;
        }
      }
    }

    std::size_t func_end = text_sec.data.size();
    if (!result.symbols.empty()) result.symbols.back().size = func_end - func_start;

    // Append an unreachable lea to data symbol to exercise data relocations.
    std::size_t reloc_disp_off = text_sec.data.size() + 3;
    text_sec.data.insert(text_sec.data.end(), {0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00});
    MCReloc dr;
    dr.section = ".text";
    dr.offset = static_cast<std::uint32_t>(reloc_disp_off);
    dr.type = 1;
    dr.symbol = "msg";
    dr.addend = 0;
    result.relocs.push_back(dr);
  }

  // Emit a tiny rodata section for demonstration
  MCSection rodata;
  rodata.name = ".data";
  const char msg[] = "hello\0";
  rodata.data.insert(rodata.data.end(), msg, msg + sizeof(msg));
  result.symbols.push_back({"msg", rodata.name, 0, sizeof(msg), true, true});

  result.sections.push_back(std::move(text_sec));
  result.sections.push_back(std::move(rodata));
  return result;
}

}  // namespace polyglot::backends::x86_64
