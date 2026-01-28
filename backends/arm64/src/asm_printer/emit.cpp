#include "backends/arm64/include/arm64_target.h"

#include <sstream>
#include <unordered_map>
#include <vector>

#include "backends/arm64/include/machine_ir.h"

namespace polyglot::backends::arm64 {
namespace {

// Emit a 32-bit instruction (little endian).
void Emit32(std::vector<std::uint8_t> &out, std::uint32_t word) {
  out.push_back(static_cast<std::uint8_t>(word & 0xFF));
  out.push_back(static_cast<std::uint8_t>((word >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((word >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((word >> 24) & 0xFF));
}

std::uint8_t RegCode(Register r) {
  switch (r) {
    case Register::kX0: return 0;
    case Register::kX1: return 1;
    case Register::kX2: return 2;
    case Register::kX3: return 3;
    case Register::kX4: return 4;
    case Register::kX5: return 5;
    case Register::kX6: return 6;
    case Register::kX7: return 7;
    case Register::kX8: return 8;
    case Register::kX9: return 9;
    case Register::kX10: return 10;
    case Register::kX11: return 11;
    case Register::kX12: return 12;
    case Register::kX13: return 13;
    case Register::kX14: return 14;
    case Register::kX15: return 15;
    case Register::kX16: return 16;
    case Register::kX17: return 17;
    case Register::kX18: return 18;
    case Register::kX19: return 19;
    case Register::kX20: return 20;
    case Register::kX21: return 21;
    case Register::kX22: return 22;
    case Register::kX23: return 23;
    case Register::kX24: return 24;
    case Register::kX25: return 25;
    case Register::kX26: return 26;
    case Register::kX27: return 27;
    case Register::kX28: return 28;
    case Register::kFp: return 29;
    case Register::kLr: return 30;
    case Register::kSp: return 31;
  }
  return 0;
}

Register Resolve(const Operand &op, const AllocationResult &alloc) {
  if (op.kind == Operand::Kind::kPhysReg) return op.phys;
  if (op.kind == Operand::Kind::kVReg) {
    auto it = alloc.vreg_to_phys.find(op.vreg);
    if (it != alloc.vreg_to_phys.end()) return it->second;
  }
  return Register::kX0;
}

int StackOffsetBytes(int slot) { return (slot + 1) * 16; }

std::string FormatOperand(const Operand &op, const AllocationResult &alloc) {
  switch (op.kind) {
    case Operand::Kind::kPhysReg:
      return RegisterName(op.phys);
    case Operand::Kind::kVReg:
      return RegisterName(Resolve(op, alloc));
    case Operand::Kind::kImm:
      return std::to_string(op.imm);
    case Operand::Kind::kLabel:
      return op.label;
    case Operand::Kind::kMemVReg:
      return "[" + RegisterName(Resolve(Operand::VReg(op.vreg), alloc)) + "]";
    case Operand::Kind::kMemLabel:
      return "[" + op.label + "]";
    case Operand::Kind::kStackSlot: {
      int offset = StackOffsetBytes(op.stack_slot);
      return "[x29, -" + std::to_string(offset) + "]";
    }
  }
  return "";
}

void EmitMovImm(std::vector<std::uint8_t> &out, Register dst, std::uint64_t imm) {
  // Use MOVZ, only handles 16-bit immediates for now.
  if (imm <= 0xFFFF) {
    std::uint32_t insn = 0xD2800000;  // MOVZ
    insn |= (static_cast<std::uint32_t>(imm) << 5);
    insn |= RegCode(dst);
    Emit32(out, insn);
  } else {
    // Fallback: zero then ORR with itself (clears register)
    Emit32(out, 0xAA0003E0 | (RegCode(dst)));  // mov dst, xzr
  }
}

}  // namespace

std::string Arm64Target::EmitAssembly() {
  std::ostringstream os;
  if (!module_ || module_->Functions().empty()) {
    os << "; no IR module available for target " << TargetTriple() << "\n";
    return os.str();
  }

  CostModel cost_model;
  std::vector<Register> available = {Register::kX0, Register::kX1, Register::kX2, Register::kX3};

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

    int stack_bytes = alloc.stack_slots * 16;
    if (stack_bytes % 16 != 0) stack_bytes = ((stack_bytes + 15) / 16) * 16;

    os << ".globl " << mf.name << "\n";
    os << mf.name << ":\n";
    os << "  stp x29, x30, [sp, #-16]!\n";
    os << "  mov x29, sp\n";
    if (stack_bytes > 0) os << "  sub sp, sp, " << stack_bytes << "\n";

    for (const auto &bb : mf.blocks) {
      os << bb.name << ":\n";
      for (const auto &mi : bb.instructions) {
        switch (mi.opcode) {
          case Opcode::kMov:
            if (mi.operands.empty()) break;
            os << "  mov "
               << RegisterName(Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc)) << ", "
               << FormatOperand(mi.operands[0], alloc) << "\n";
            break;
          case Opcode::kAdd:
            os << "  add " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kSub:
            os << "  sub " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kMul:
            os << "  mul " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kDiv:
          case Opcode::kSDiv:
            os << "  sdiv " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kUDiv:
            os << "  udiv " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kRem:
          case Opcode::kSRem: {
            // dst = lhs - (lhs / rhs) * rhs
            std::string dst = FormatOperand(Operand::VReg(mi.def), alloc);
            std::string lhs = FormatOperand(mi.operands[0], alloc);
            std::string rhs = FormatOperand(mi.operands[1], alloc);
            os << "  sdiv x9, " << lhs << ", " << rhs << "\n";
            os << "  msub " << dst << ", x9, " << rhs << ", " << lhs << "\n";
            break;
          }
          case Opcode::kURem: {
            std::string dst = FormatOperand(Operand::VReg(mi.def), alloc);
            std::string lhs = FormatOperand(mi.operands[0], alloc);
            std::string rhs = FormatOperand(mi.operands[1], alloc);
            os << "  udiv x9, " << lhs << ", " << rhs << "\n";
            os << "  msub " << dst << ", x9, " << rhs << ", " << lhs << "\n";
            break;
          }
          case Opcode::kAnd:
            os << "  and " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kOr:
            os << "  orr " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kXor:
            os << "  eor " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kShl:
            os << "  lsl " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kLShr:
            os << "  lsr " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kAShr:
            os << "  asr " << FormatOperand(Operand::VReg(mi.def), alloc) << ", "
               << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kCmp:
            os << "  cmp " << FormatOperand(mi.operands[0], alloc) << ", " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          case Opcode::kLoad:
            os << "  ldr " << FormatOperand(Operand::VReg(mi.def), alloc) << ", " << FormatOperand(mi.operands[0], alloc) << "\n";
            break;
          case Opcode::kStore:
            os << "  str " << FormatOperand(mi.operands[1], alloc) << ", " << FormatOperand(mi.operands[0], alloc) << "\n";
            break;
          case Opcode::kLea:
            os << "  add " << FormatOperand(Operand::VReg(mi.def), alloc) << ", " << FormatOperand(mi.operands[0], alloc) << ", #0\n";
            break;
          case Opcode::kCall:
            if (!mi.operands.empty()) os << "  bl " << FormatOperand(mi.operands.back(), alloc) << "\n";
            break;
          case Opcode::kRet:
            os << "  ret\n";
            break;
          case Opcode::kJmp:
            if (!mi.operands.empty()) os << "  b " << FormatOperand(mi.operands[0], alloc) << "\n";
            break;
          case Opcode::kJcc:
            if (mi.operands.size() >= 1) os << "  b.ne " << FormatOperand(mi.operands[0], alloc) << "\n";
            if (mi.operands.size() >= 2) os << "  b " << FormatOperand(mi.operands[1], alloc) << "\n";
            break;
          default:
            os << "  ; unhandled opcode\n";
            break;
        }
      }
    }
    os << "  mov sp, x29\n";
    os << "  ldp x29, x30, [sp], #16\n";
    os << "  ret\n";
  }

  return os.str();
}

Arm64Target::MCResult Arm64Target::EmitObjectCode() {
  MCResult result;
  if (!module_ || module_->Functions().empty()) return result;

  CostModel cost_model;
  std::vector<Register> available = {Register::kX0, Register::kX1, Register::kX2, Register::kX3};

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

    int stack_bytes = alloc.stack_slots * 16;
    if (stack_bytes % 16 != 0) stack_bytes = ((stack_bytes + 15) / 16) * 16;

    std::size_t func_start = text_sec.data.size();
    label_offsets[mf.name] = static_cast<std::uint32_t>(func_start);
    result.symbols.push_back({mf.name, ".text", static_cast<std::uint64_t>(func_start), 0, true, true});

    // Prologue
    Emit32(text_sec.data, 0xA9BF7BFD);  // stp x29, x30, [sp, #-16]!
    Emit32(text_sec.data, 0x910003FD);  // mov x29, sp
    if (stack_bytes > 0) {
      std::uint32_t insn = 0xD10003FF;  // sub sp, sp, #0 placeholder
      std::uint32_t imm12 = static_cast<std::uint32_t>(stack_bytes) & 0xFFF;
      insn |= (imm12 << 10);
      Emit32(text_sec.data, insn);
    }

    for (const auto &bb : mf.blocks) {
      label_offsets[bb.name] = static_cast<std::uint32_t>(text_sec.data.size());
      result.symbols.push_back({bb.name, ".text", static_cast<std::uint64_t>(text_sec.data.size()), 0, false, true});

      for (const auto &mi : bb.instructions) {
        switch (mi.opcode) {
          case Opcode::kMov: {
            if (mi.operands.empty()) break;
            Register dst = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            const auto &src = mi.operands[0];
            if (src.kind == Operand::Kind::kImm) {
              EmitMovImm(text_sec.data, dst, static_cast<std::uint64_t>(src.imm));
            } else {
              Register s = Resolve(src, alloc);
              std::uint32_t insn = 0xAA0003E0;  // orr dst, xzr, s (mov)
              insn |= (RegCode(s) << 16);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            }
            break;
          }
          case Opcode::kAdd: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            const auto &rhs = mi.operands[1];
            if (rhs.kind == Operand::Kind::kImm && rhs.imm >= 0 && rhs.imm <= 0xFFF) {
              std::uint32_t insn = 0x91000000;
              insn |= (static_cast<std::uint32_t>(rhs.imm) << 10);
              insn |= (RegCode(dst) << 5);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            } else {
              Register src = Resolve(rhs, alloc);
              std::uint32_t insn = 0x8B000000;
              insn |= (RegCode(src) << 16);
              insn |= (RegCode(dst) << 5);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            }
            break;
          }
          case Opcode::kSub: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            const auto &rhs = mi.operands[1];
            if (rhs.kind == Operand::Kind::kImm && rhs.imm >= 0 && rhs.imm <= 0xFFF) {
              std::uint32_t insn = 0xD1000000;
              insn |= (static_cast<std::uint32_t>(rhs.imm) << 10);
              insn |= (RegCode(dst) << 5);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            } else {
              Register src = Resolve(rhs, alloc);
              std::uint32_t insn = 0xCB000000;
              insn |= (RegCode(src) << 16);
              insn |= (RegCode(dst) << 5);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            }
            break;
          }
          case Opcode::kMul: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(Operand::VReg(mi.def), alloc);
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            std::uint32_t insn = 0x9B007C00;  // MUL dst, lhs, rhs
            insn |= (RegCode(rhs) << 16);
            insn |= (RegCode(lhs) << 5);
            insn |= RegCode(dst);
            Emit32(text_sec.data, insn);
            break;
          }
          case Opcode::kAnd:
          case Opcode::kOr:
          case Opcode::kXor: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            std::uint32_t insn = (mi.opcode == Opcode::kAnd) ? 0x8A000000 : (mi.opcode == Opcode::kOr ? 0xAA000000 : 0xCA000000);
            insn |= (RegCode(rhs) << 16);
            insn |= (RegCode(lhs) << 5);
            insn |= RegCode(dst);
            Emit32(text_sec.data, insn);
            break;
          }
          case Opcode::kShl:
          case Opcode::kLShr:
          case Opcode::kAShr: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(mi.def >= 0 ? Operand::VReg(mi.def) : mi.operands[0], alloc);
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            std::uint32_t insn = 0x9AC02000;  // LSLV
            if (mi.opcode == Opcode::kLShr) insn = 0x9AC02400;  // LSRV
            if (mi.opcode == Opcode::kAShr) insn = 0x9AC02800;  // ASRV
            insn |= (RegCode(rhs) << 16);
            insn |= (RegCode(lhs) << 5);
            insn |= RegCode(dst);
            Emit32(text_sec.data, insn);
            break;
          }
          case Opcode::kDiv:
          case Opcode::kSDiv:
          case Opcode::kUDiv: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(Operand::VReg(mi.def), alloc);
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            std::uint32_t insn = (mi.opcode == Opcode::kUDiv) ? 0x9AC00800 : 0x9AC00C00;  // UDIV / SDIV
            insn |= (RegCode(rhs) << 16);
            insn |= (RegCode(lhs) << 5);
            insn |= RegCode(dst);
            Emit32(text_sec.data, insn);
            break;
          }
          case Opcode::kRem:
          case Opcode::kSRem:
          case Opcode::kURem: {
            if (mi.operands.size() < 2) break;
            Register dst = Resolve(Operand::VReg(mi.def), alloc);
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            Register q = Register::kX9;  // scratch quotient
            std::uint32_t div_insn = (mi.opcode == Opcode::kURem) ? 0x9AC00800 : 0x9AC00C00;  // UDIV/SDIV q, lhs, rhs
            div_insn |= (RegCode(rhs) << 16);
            div_insn |= (RegCode(lhs) << 5);
            div_insn |= RegCode(q);
            Emit32(text_sec.data, div_insn);

            // MSUB dst, q, rhs, lhs => dst = lhs - q*rhs
            std::uint32_t msub = 0x9B008000;  // MSUB x0, x0, x0, x0 base
            msub |= (RegCode(rhs) << 16);
            msub |= (RegCode(q) << 10);
            msub |= (RegCode(lhs) << 5);
            msub |= RegCode(dst);
            Emit32(text_sec.data, msub);
            break;
          }
          case Opcode::kCmp: {
            if (mi.operands.size() < 2) break;
            Register lhs = Resolve(mi.operands[0], alloc);
            Register rhs = Resolve(mi.operands[1], alloc);
            std::uint32_t insn = 0xEB00001F;  // CMP (SUBS xzr, lhs, rhs)
            insn |= (RegCode(rhs) << 16);
            insn |= (RegCode(lhs) << 5);
            Emit32(text_sec.data, insn);
            break;
          }
          case Opcode::kLoad: {
            if (mi.operands.empty()) break;
            Register dst = Resolve(Operand::VReg(mi.def), alloc);
            const auto &mem = mi.operands[0];
            if (mem.kind == Operand::Kind::kStackSlot) {
              int offset = StackOffsetBytes(mem.stack_slot);
              std::int32_t disp = -offset;
              std::uint32_t insn = 0xF8400000;  // LDUR
              std::uint32_t imm9 = static_cast<std::uint32_t>(disp & 0x1FF);
              insn |= (imm9 << 12);
              insn |= (RegCode(Register::kFp) << 5);
              insn |= RegCode(dst);
              Emit32(text_sec.data, insn);
            }
            break;
          }
          case Opcode::kStore: {
            if (mi.operands.size() < 2) break;
            const auto &mem = mi.operands[0];
            Register src = Resolve(mi.operands[1], alloc);
            if (mem.kind == Operand::Kind::kStackSlot) {
              int offset = StackOffsetBytes(mem.stack_slot);
              std::int32_t disp = -offset;
              std::uint32_t insn = 0xF8000000;  // STUR
              std::uint32_t imm9 = static_cast<std::uint32_t>(disp & 0x1FF);
              insn |= (imm9 << 12);
              insn |= (RegCode(Register::kFp) << 5);
              insn |= RegCode(src);
              Emit32(text_sec.data, insn);
            }
            break;
          }
          case Opcode::kJmp: {
            if (mi.operands.empty()) break;
            std::size_t reloc_offset = text_sec.data.size();
            Emit32(text_sec.data, 0x14000000);  // B with imm26=0
            MCReloc r{.section = ".text", .offset = static_cast<std::uint32_t>(reloc_offset), .type = 1,
                      .symbol = mi.operands[0].label, .addend = 0};
            result.relocs.push_back(r);
            break;
          }
          case Opcode::kCall: {
            if (mi.operands.empty()) break;
            const auto &target = mi.operands.back();
            std::size_t reloc_offset = text_sec.data.size();
            Emit32(text_sec.data, 0x94000000);  // BL imm26
            MCReloc r{.section = ".text", .offset = static_cast<std::uint32_t>(reloc_offset), .type = 1,
                      .symbol = target.label, .addend = 0};
            result.relocs.push_back(r);
            result.symbols.push_back({target.label, "", 0, 0, true, false});
            break;
          }
          case Opcode::kJcc: {
            if (mi.operands.empty()) break;
            std::size_t reloc_offset = text_sec.data.size();
            Emit32(text_sec.data, 0x14000000);  // use B for now
            MCReloc r{.section = ".text", .offset = static_cast<std::uint32_t>(reloc_offset), .type = 1,
                      .symbol = mi.operands[0].label, .addend = 0};
            result.relocs.push_back(r);
            break;
          }
          case Opcode::kRet: {
            Emit32(text_sec.data, 0xD65F03C0);  // RET
            break;
          }
          default: {
            Emit32(text_sec.data, 0xD503201F);  // NOP
            break;
          }
        }
      }
    }

    // Epilogue
    if (stack_bytes > 0) {
      std::uint32_t insn = 0x91000000;  // add sp, sp, imm
      insn |= (static_cast<std::uint32_t>(stack_bytes & 0xFFF) << 10);
      insn |= (RegCode(Register::kSp) << 5);
      insn |= RegCode(Register::kSp);
      Emit32(text_sec.data, insn);
    }
    Emit32(text_sec.data, 0xA8C17BFD);  // ldp x29, x30, [sp], #16
    Emit32(text_sec.data, 0xD65F03C0);  // ret

    std::size_t func_end = text_sec.data.size();
    if (!result.symbols.empty()) result.symbols.back().size = func_end - func_start;
  }

  result.sections.push_back(std::move(text_sec));
  return result;
}

}  // namespace polyglot::backends::arm64
