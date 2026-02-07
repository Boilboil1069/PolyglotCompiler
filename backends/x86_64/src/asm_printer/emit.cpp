#include "backends/x86_64/include/x86_target.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"

namespace polyglot::backends::x86_64 {
namespace {

int StackOffsetBytes(int slot) { return static_cast<int>((slot + 1) * 8); }

bool IsSpilled(int vreg, const AllocationResult &alloc) {
  return alloc.vreg_to_phys.find(vreg) == alloc.vreg_to_phys.end() &&
         alloc.vreg_to_slot.find(vreg) != alloc.vreg_to_slot.end();
}

// format operand; inserts reloads into 'pre' when spilled.
std::string FormatOperand(const Operand &op, const AllocationResult &alloc,
                          std::ostringstream &pre) {
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
      if (IsSpilled(op.vreg, alloc)) {
        int offset = StackOffsetBytes(alloc.vreg_to_slot.at(op.vreg));
        pre << "  mov r10, [rbp - " << offset << "]\n";
        return "r10";
      }
      return "v" + std::to_string(op.vreg);
    }
    case Operand::Kind::kMemVReg: {
      auto phys_it = alloc.vreg_to_phys.find(op.vreg);
      if (phys_it != alloc.vreg_to_phys.end()) return "[" + RegisterName(phys_it->second) + "]";
      if (IsSpilled(op.vreg, alloc)) {
        int offset = StackOffsetBytes(alloc.vreg_to_slot.at(op.vreg));
        return "[rbp - " + std::to_string(offset) + "]";
      }
      return "[v" + std::to_string(op.vreg) + "]";
    }
    case Operand::Kind::kMemLabel:
      return "[" + op.label + "]";
  }
  return "";
}

void EmitBinary(const MachineInstr &mi, const AllocationResult &alloc,
                std::ostream &os, const std::string &mnemonic) {
  std::ostringstream pre;
  std::string dst = mi.def >= 0 ? FormatOperand(Operand::VReg(mi.def), alloc, pre)
                                : FormatOperand(mi.operands[0], alloc, pre);
  std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
  std::string rhs = mi.operands.size() > 1 ? FormatOperand(mi.operands[1], alloc, pre) : lhs;
  os << pre.str();
  if (dst != lhs) os << "  mov " << dst << ", " << lhs << "\n";
  os << "  " << mnemonic << " " << dst << ", " << rhs << "\n";
}

void EmitInstruction(const MachineInstr &mi, const AllocationResult &alloc,
                     std::ostream &os) {
  std::ostringstream pre;
  bool def_spilled = (mi.def >= 0 && IsSpilled(mi.def, alloc));
  Register def_reg = (mi.def >= 0 && alloc.vreg_to_phys.count(mi.def))
                         ? alloc.vreg_to_phys.at(mi.def)
                         : (def_spilled ? Register::kR10 : Register::kRax);

  switch (mi.opcode) {
    case Opcode::kMov: {
      std::string dst = mi.def >= 0 ? RegisterName(def_reg)
                                    : FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      if (dst != src) os << "  mov " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kAdd:
      EmitBinary(mi, alloc, os, "add");
      break;
    case Opcode::kSub:
      EmitBinary(mi, alloc, os, "sub");
      break;
    case Opcode::kMul:
      EmitBinary(mi, alloc, os, "imul");
      break;
    case Opcode::kDiv:
    case Opcode::kSDiv: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      std::string dst = RegisterName(def_reg);
      os << pre.str();
      os << "  mov rax, " << lhs << "\n";
      os << "  cqo\n";
      os << "  idiv " << rhs << "\n";
      if (dst != "rax") os << "  mov " << dst << ", rax\n";
      break;
    }
    case Opcode::kUDiv: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      std::string dst = RegisterName(def_reg);
      os << pre.str();
      os << "  mov rax, " << lhs << "\n";
      os << "  xor rdx, rdx\n";
      os << "  div " << rhs << "\n";
      if (dst != "rax") os << "  mov " << dst << ", rax\n";
      break;
    }
    case Opcode::kAnd:
      EmitBinary(mi, alloc, os, "and");
      break;
    case Opcode::kOr:
      EmitBinary(mi, alloc, os, "or");
      break;
    case Opcode::kXor:
      EmitBinary(mi, alloc, os, "xor");
      break;
    case Opcode::kShl:
      EmitBinary(mi, alloc, os, "shl");
      break;
    case Opcode::kLShr:
      EmitBinary(mi, alloc, os, "shr");
      break;
    case Opcode::kAShr:
      EmitBinary(mi, alloc, os, "sar");
      break;
    case Opcode::kRem:
    case Opcode::kSRem: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      std::string dst = RegisterName(def_reg);
      os << pre.str();
      os << "  mov rax, " << lhs << "\n";
      os << "  cqo\n";
      os << "  idiv " << rhs << "\n";
      if (dst != "rdx") os << "  mov " << dst << ", rdx\n";
      break;
    }
    case Opcode::kURem: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      std::string dst = RegisterName(def_reg);
      os << pre.str();
      os << "  mov rax, " << lhs << "\n";
      os << "  xor rdx, rdx\n";
      os << "  div " << rhs << "\n";
      if (dst != "rdx") os << "  mov " << dst << ", rdx\n";
      break;
    }
    case Opcode::kCmp: {
      if (mi.operands.size() < 2) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      os << "  cmp " << lhs << ", " << rhs << "\n";
      break;
    }
    // Floating-point instructions
    case Opcode::kMovsd: {
      std::string dst = mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      if (dst != src) os << "  movsd " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kMovss: {
      std::string dst = mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      if (dst != src) os << "  movss " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kAddsd: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movsd " << dst << ", " << lhs << "\n";
      os << "  addsd " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kSubsd: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movsd " << dst << ", " << lhs << "\n";
      os << "  subsd " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kMulsd: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movsd " << dst << ", " << lhs << "\n";
      os << "  mulsd " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kDivsd: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movsd " << dst << ", " << lhs << "\n";
      os << "  divsd " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kCmpsd: {
      if (mi.operands.size() < 2) break;
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      os << "  comisd " << lhs << ", " << rhs << "\n";
      break;
    }
    // SIMD instructions
    case Opcode::kMovaps: {
      std::string dst = mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      if (dst != src) os << "  movaps " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kMovups: {
      std::string dst = mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      if (dst != src) os << "  movups " << dst << ", " << src << "\n";
      break;
    }
    case Opcode::kAddps: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movaps " << dst << ", " << lhs << "\n";
      os << "  addps " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kSubps: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movaps " << dst << ", " << lhs << "\n";
      os << "  subps " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kMulps: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movaps " << dst << ", " << lhs << "\n";
      os << "  mulps " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kDivps: {
      if (mi.operands.size() < 2 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movaps " << dst << ", " << lhs << "\n";
      os << "  divps " << dst << ", " << rhs << "\n";
      break;
    }
    case Opcode::kShufps: {
      if (mi.operands.size() < 3 || mi.def < 0) break;
      std::string dst = RegisterName(def_reg);
      std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
      std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
      std::string imm = FormatOperand(mi.operands[2], alloc, pre);
      os << pre.str();
      if (dst != lhs) os << "  movaps " << dst << ", " << lhs << "\n";
      os << "  shufps " << dst << ", " << rhs << ", " << imm << "\n";
      break;
    }
    case Opcode::kLoad: {
      std::string dst = RegisterName(def_reg);
      std::string mem = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      os << "  mov " << dst << ", " << mem << "\n";
      break;
    }
    case Opcode::kStore: {
      if (mi.operands.size() < 2) break;
      std::string mem = FormatOperand(mi.operands[0], alloc, pre);
      std::string src = FormatOperand(mi.operands[1], alloc, pre);
      os << pre.str();
      os << "  mov " << mem << ", " << src << "\n";
      break;
    }
    case Opcode::kLea: {
      std::string dst = RegisterName(def_reg);
      std::string mem = FormatOperand(mi.operands[0], alloc, pre);
      os << pre.str();
      os << "  lea " << dst << ", " << mem << "\n";
      break;
    }
    case Opcode::kCall: {
      os << pre.str();
      static const Register kIArgRegs[] = {Register::kRdi, Register::kRsi, Register::kRdx,
                                           Register::kRcx, Register::kR8,  Register::kR9};
      size_t argn = mi.operands.size() > 0 ? mi.operands.size() - 1 : 0;
      for (size_t ai = 0; ai < argn && ai < 6; ++ai) {
        std::string src = FormatOperand(mi.operands[ai], alloc, pre);
        std::string dst = RegisterName(kIArgRegs[ai]);
        if (src != dst) os << "  mov " << dst << ", " << src << "\n";
      }
      if (!mi.operands.empty()) {
        const auto &label = mi.operands.back();
        os << "  call " << FormatOperand(label, alloc, pre) << "\n";
      }
      if (mi.def >= 0) {
        std::string dst = RegisterName(def_reg);
        if (dst != "rax") os << "  mov " << dst << ", rax\n";
      }
      break;
    }
    case Opcode::kJmp: {
      os << pre.str();
      if (!mi.operands.empty()) os << "  jmp " << FormatOperand(mi.operands[0], alloc, pre) << "\n";
      break;
    }
    case Opcode::kJcc: {
      os << pre.str();
      if (mi.operands.size() >= 1) os << "  jne " << FormatOperand(mi.operands[0], alloc, pre) << "\n";
      if (mi.operands.size() >= 2) os << "  jmp " << FormatOperand(mi.operands[1], alloc, pre) << "\n";
      break;
    }
    case Opcode::kRet: {
      os << pre.str();
      if (!mi.operands.empty()) {
        std::string src = FormatOperand(mi.operands[0], alloc, pre);
        if (src != "rax") os << "  mov rax, " << src << "\n";
      }
      os << "  leave\n";
      os << "  ret\n";
      break;
    }
  }

  if (def_spilled && mi.opcode != Opcode::kCall) {
    int offset = StackOffsetBytes(alloc.vreg_to_slot.at(mi.def));
    os << "  mov [rbp - " << offset << "], " << RegisterName(def_reg) << "\n";
  }
}

void EmitFunction(const MachineFunction &mf, const AllocationResult &alloc, std::ostream &os) {
  auto is_callee_saved = [](Register r) {
    return r == Register::kRbx || r == Register::kR12 || r == Register::kR13 || r == Register::kR14 || r == Register::kR15;
  };
  std::vector<Register> callee_used;
  for (auto &kv : alloc.vreg_to_phys) {
    if (is_callee_saved(kv.second)) callee_used.push_back(kv.second);
  }
  std::sort(callee_used.begin(), callee_used.end());
  callee_used.erase(std::unique(callee_used.begin(), callee_used.end()), callee_used.end());

  int stack_bytes = alloc.stack_slots * 8;
  if ((stack_bytes + 8) % 16 != 0) stack_bytes += 8;  // align after push rbp

  os << ".globl " << mf.name << "\n";
  os << mf.name << ":\n";
  os << "  push rbp\n";
  os << "  mov rbp, rsp\n";
  if (stack_bytes > 0) os << "  sub rsp, " << stack_bytes << "\n";
  for (auto r : callee_used) {
    os << "  push " << RegisterName(r) << "\n";
  }

  for (const auto &bb : mf.blocks) {
    os << bb.name << ":\n";
    for (const auto &mi : bb.instructions) {
      EmitInstruction(mi, alloc, os);
    }
  }

  for (auto it = callee_used.rbegin(); it != callee_used.rend(); ++it) {
    os << "  pop " << RegisterName(*it) << "\n";
  }
  os << "  leave\n";
  os << "  ret\n";
}

}  // namespace

std::string X86Target::EmitAssembly() {
  std::ostringstream os;
  if (!module_ || module_->Functions().empty()) {
    os << "; no IR module available for target " << TargetTriple() << "\n";
    return os.str();
  }

  CostModel cost_model;
  // Reserve RCX for literal materialization in div/rem emission; do not allocate it.
  std::vector<Register> available = {Register::kRax, Register::kRbx, Register::kRdx};

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
  // Reserve RCX for literal materialization in div/rem emission; do not allocate it.
  std::vector<Register> available = {Register::kRax, Register::kRbx, Register::kRdx};

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
          case Opcode::kDiv:
          case Opcode::kSDiv:
          case Opcode::kUDiv:
          case Opcode::kRem:
          case Opcode::kSRem:
          case Opcode::kURem: {
            if (mi.operands.size() < 2) break;
            const auto &lhs = mi.operands[0];
            const auto &rhs = mi.operands[1];

            auto emit_mov_reg_reg = [&](Register dst, Register src) {
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x89);
              text_sec.data.push_back(ModRM(0b11, RegCode(src), RegCode(dst)));
            };
            auto emit_mov_imm_reg = [&](Register dst, std::int32_t imm) {
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(static_cast<std::uint8_t>(0xB8 + RegCode(dst)));
              for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
            };
            auto emit_load_stack_to_reg = [&](Register dst, int slot) {
              int offset = StackOffsetBytes(slot);
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0x8B);
              if (offset <= 127) {
                text_sec.data.push_back(ModRM(0b01, RegCode(dst), RegCode(Register::kRbp)));
                text_sec.data.push_back(static_cast<std::uint8_t>(-offset));
              } else {
                text_sec.data.push_back(ModRM(0b10, RegCode(dst), RegCode(Register::kRbp)));
                std::int32_t disp = -offset;
                for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
              }
            };
            auto emit_div_rm = [&](std::uint8_t div_opcode, const Operand &op) {
              text_sec.data.push_back(0x48);
              text_sec.data.push_back(0xF7);
              if (op.kind == Operand::Kind::kVReg || op.kind == Operand::Kind::kPhysReg) {
                Register reg = Resolve(op, alloc);
                text_sec.data.push_back(ModRM(0b11, div_opcode, RegCode(reg)));
              } else if (op.kind == Operand::Kind::kStackSlot) {
                int offset = StackOffsetBytes(op.stack_slot);
                if (offset <= 127) {
                  text_sec.data.push_back(ModRM(0b01, div_opcode, RegCode(Register::kRbp)));
                  text_sec.data.push_back(static_cast<std::uint8_t>(-offset));
                } else {
                  text_sec.data.push_back(ModRM(0b10, div_opcode, RegCode(Register::kRbp)));
                  std::int32_t disp = -offset;
                  for (int i = 0; i < 4; ++i) text_sec.data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
                }
              }
            };

            // Move lhs into RAX
            if (lhs.kind == Operand::Kind::kImm) {
              emit_mov_imm_reg(Register::kRax, static_cast<std::int32_t>(lhs.imm));
            } else if (lhs.kind == Operand::Kind::kStackSlot) {
              emit_load_stack_to_reg(Register::kRax, lhs.stack_slot);
            } else {
              emit_mov_reg_reg(Register::kRax, Resolve(lhs, alloc));
            }

            bool signed_op = (mi.opcode == Opcode::kDiv || mi.opcode == Opcode::kSDiv || mi.opcode == Opcode::kRem || mi.opcode == Opcode::kSRem);

            // Prepare RDX
            if (signed_op) {
              text_sec.data.push_back(0x48);  // cqo
              text_sec.data.push_back(0x99);
            } else {
              text_sec.data.push_back(0x48);  // xor rdx, rdx
              text_sec.data.push_back(0x31);
              text_sec.data.push_back(0xD2);
            }

            // If rhs is immediate, move to RCX as scratch
            Operand rhs_op = rhs;
            if (rhs.kind == Operand::Kind::kImm) {
              emit_mov_imm_reg(Register::kRcx, static_cast<std::int32_t>(rhs.imm));
              rhs_op = Operand::Phys(Register::kRcx);
            }

            // Emit div/idiv r/m64
            std::uint8_t div_opcode = signed_op ? 0b111 : 0b110;  // /7 for idiv, /6 for div
            emit_div_rm(div_opcode, rhs_op);

            // Move result to def if needed
            if (mi.def >= 0) {
              Register dst = Resolve(Operand::VReg(mi.def), alloc);
              bool is_rem = (mi.opcode == Opcode::kRem || mi.opcode == Opcode::kSRem || mi.opcode == Opcode::kURem);
              Register src = is_rem ? Register::kRdx : Register::kRax;
              if (dst != src) emit_mov_reg_reg(dst, src);
            }
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
            if (!mi.operands.empty()) {
              const auto &src = mi.operands[0];
              if (src.kind == Operand::Kind::kImm) {
                text_sec.data.push_back(0x48);  // REX.W
                text_sec.data.push_back(static_cast<std::uint8_t>(0xB8 + RegCode(Register::kRax)));
                std::uint64_t imm = static_cast<std::uint64_t>(src.imm);
                for (int i = 0; i < 8; ++i) {
                  text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (8 * i)) & 0xFF));
                }
              } else {
                Register src_reg = Resolve(src, alloc);
                text_sec.data.push_back(0x48);
                text_sec.data.push_back(0x89);  // mov r/m64, r64
                text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(Register::kRax)));
              }
            }
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
