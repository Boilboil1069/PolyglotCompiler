/**
 * @file     emit.cpp
 * @brief    x86-64 code generation implementation
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"
#include "backends/x86_64/include/x86_target.h"

namespace polyglot::backends::x86_64 {
namespace {

long long PredicateCode(const MachineInstr &mi) {
  if (mi.operands.size() >= 3 && mi.operands[2].kind == Operand::Kind::kImm)
    return mi.operands[2].imm;
  return static_cast<long long>(IntComparePredicate::kEq);
}

const char *SetccMnemonic(long long predicate) {
  switch (static_cast<IntComparePredicate>(predicate)) {
  case IntComparePredicate::kEq:
    return "sete";
  case IntComparePredicate::kNe:
    return "setne";
  case IntComparePredicate::kUlt:
    return "setb";
  case IntComparePredicate::kUle:
    return "setbe";
  case IntComparePredicate::kUgt:
    return "seta";
  case IntComparePredicate::kUge:
    return "setae";
  case IntComparePredicate::kSlt:
    return "setl";
  case IntComparePredicate::kSle:
    return "setle";
  case IntComparePredicate::kSgt:
    return "setg";
  case IntComparePredicate::kSge:
    return "setge";
  }
  return "sete";
}

std::uint8_t SetccOpcode(long long predicate) {
  switch (static_cast<IntComparePredicate>(predicate)) {
  case IntComparePredicate::kEq:
    return 0x94;
  case IntComparePredicate::kNe:
    return 0x95;
  case IntComparePredicate::kUlt:
    return 0x92;
  case IntComparePredicate::kUle:
    return 0x96;
  case IntComparePredicate::kUgt:
    return 0x97;
  case IntComparePredicate::kUge:
    return 0x93;
  case IntComparePredicate::kSlt:
    return 0x9C;
  case IntComparePredicate::kSle:
    return 0x9E;
  case IntComparePredicate::kSgt:
    return 0x9F;
  case IntComparePredicate::kSge:
    return 0x9D;
  }
  return 0x94;
}

const char *ByteRegisterName(Register reg) {
  switch (reg) {
  case Register::kRax: return "al";
  case Register::kRbx: return "bl";
  case Register::kRcx: return "cl";
  case Register::kRdx: return "dl";
  case Register::kRsp: return "spl";
  case Register::kRbp: return "bpl";
  case Register::kRsi: return "sil";
  case Register::kRdi: return "dil";
  case Register::kR8: return "r8b";
  case Register::kR9: return "r9b";
  case Register::kR10: return "r10b";
  case Register::kR11: return "r11b";
  case Register::kR12: return "r12b";
  case Register::kR13: return "r13b";
  case Register::kR14: return "r14b";
  case Register::kR15: return "r15b";
  default: return "al";
  }
}

int StackOffsetBytes(int slot) {
  return static_cast<int>((slot + 1) * 8);
}

int AlignTo(int value, int alignment) {
  if (alignment <= 1)
    return value;
  const int remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

struct StackObjectLayout {
  std::vector<int> offsets;
  int extent{0};
};

StackObjectLayout LayoutStackObjects(const MachineFunction &mf, int initial_extent) {
  StackObjectLayout layout;
  layout.extent = initial_extent;
  layout.offsets.reserve(mf.stack_objects.size());
  for (const auto &object : mf.stack_objects) {
    const int size = static_cast<int>(std::max<std::size_t>(object.size, 1));
    const int alignment = static_cast<int>(std::max<std::size_t>(object.alignment, 1));
    // The object address is its lowest address.  With a downward-growing
    // stack that is the aligned end offset from RBP.
    layout.extent = AlignTo(layout.extent + size, alignment);
    layout.offsets.push_back(layout.extent);
  }
  return layout;
}

std::string FormatDisplacedAddress(const std::string &base, long long displacement) {
  if (displacement == 0)
    return "[" + base + "]";
  if (displacement > 0)
    return "[" + base + " + " + std::to_string(displacement) + "]";
  return "[" + base + " - " + std::to_string(-displacement) + "]";
}

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
  case Operand::Kind::kStackObject:
    return "[stack-object-" + std::to_string(op.stack_slot) + "]";
  case Operand::Kind::kVReg: {
    auto phys_it = alloc.vreg_to_phys.find(op.vreg);
    if (phys_it != alloc.vreg_to_phys.end())
      return RegisterName(phys_it->second);
    if (IsSpilled(op.vreg, alloc)) {
      int offset = StackOffsetBytes(alloc.vreg_to_slot.at(op.vreg));
      pre << "  mov r10, [rbp - " << offset << "]\n";
      return "r10";
    }
    return "v" + std::to_string(op.vreg);
  }
  case Operand::Kind::kMemVReg: {
    auto phys_it = alloc.vreg_to_phys.find(op.vreg);
    if (phys_it != alloc.vreg_to_phys.end())
      return FormatDisplacedAddress(RegisterName(phys_it->second), op.displacement);
    if (IsSpilled(op.vreg, alloc)) {
      int offset = StackOffsetBytes(alloc.vreg_to_slot.at(op.vreg));
      // A spilled MemVReg contains an address; reload that address before
      // dereferencing it.  Accessing the spill slot directly would load the
      // pointer value rather than the pointee.
      pre << "  mov r10, [rbp - " << offset << "]\n";
      return FormatDisplacedAddress("r10", op.displacement);
    }
    return "[v" + std::to_string(op.vreg) + "]";
  }
  case Operand::Kind::kMemLabel:
    return "[" + op.label + "]";
  }
  return "";
}

void EmitBinary(const MachineInstr &mi, const AllocationResult &alloc, std::ostream &os,
                const std::string &mnemonic) {
  std::ostringstream pre;
  std::string dst = mi.def >= 0 ? FormatOperand(Operand::VReg(mi.def), alloc, pre)
                                : FormatOperand(mi.operands[0], alloc, pre);
  std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
  std::string rhs = mi.operands.size() > 1 ? FormatOperand(mi.operands[1], alloc, pre) : lhs;
  os << pre.str();
  if (dst != lhs)
    os << "  mov " << dst << ", " << lhs << "\n";
  os << "  " << mnemonic << " " << dst << ", " << rhs << "\n";
}

void EmitInstruction(const MachineInstr &mi, const AllocationResult &alloc,
                     const std::vector<int> &stack_object_offsets,
                     std::ostream &os) {
  std::ostringstream pre;
  bool def_spilled = (mi.def >= 0 && IsSpilled(mi.def, alloc));
  Register def_reg = (mi.def >= 0 && alloc.vreg_to_phys.count(mi.def))
                         ? alloc.vreg_to_phys.at(mi.def)
                         : (def_spilled ? Register::kR10 : Register::kRax);

  switch (mi.opcode) {
  case Opcode::kMov: {
    std::string dst =
        mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (dst != src)
      os << "  mov " << dst << ", " << src << "\n";
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
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    std::string dst = RegisterName(def_reg);
    os << pre.str();
    os << "  mov rax, " << lhs << "\n";
    os << "  cqo\n";
    os << "  idiv " << rhs << "\n";
    if (dst != "rax")
      os << "  mov " << dst << ", rax\n";
    break;
  }
  case Opcode::kUDiv: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    std::string dst = RegisterName(def_reg);
    os << pre.str();
    os << "  mov rax, " << lhs << "\n";
    os << "  xor rdx, rdx\n";
    os << "  div " << rhs << "\n";
    if (dst != "rax")
      os << "  mov " << dst << ", rax\n";
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
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    std::string dst = RegisterName(def_reg);
    os << pre.str();
    os << "  mov rax, " << lhs << "\n";
    os << "  cqo\n";
    os << "  idiv " << rhs << "\n";
    if (dst != "rdx")
      os << "  mov " << dst << ", rdx\n";
    break;
  }
  case Opcode::kURem: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    std::string dst = RegisterName(def_reg);
    os << pre.str();
    os << "  mov rax, " << lhs << "\n";
    os << "  xor rdx, rdx\n";
    os << "  div " << rhs << "\n";
    if (dst != "rdx")
      os << "  mov " << dst << ", rdx\n";
    break;
  }
  case Opcode::kCmp: {
    if (mi.operands.size() < 2)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    os << "  cmp " << lhs << ", " << rhs << "\n";
    if (mi.def >= 0) {
      std::string dst = RegisterName(def_reg);
      os << "  " << SetccMnemonic(PredicateCode(mi)) << " "
         << ByteRegisterName(def_reg) << "\n";
      os << "  movzx " << dst << ", " << ByteRegisterName(def_reg) << "\n";
    }
    break;
  }
  // Floating-point instructions
  case Opcode::kMovsd: {
    std::string dst =
        mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (dst != src)
      os << "  movsd " << dst << ", " << src << "\n";
    break;
  }
  case Opcode::kMovss: {
    std::string dst =
        mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (dst != src)
      os << "  movss " << dst << ", " << src << "\n";
    break;
  }
  case Opcode::kAddsd: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movsd " << dst << ", " << lhs << "\n";
    os << "  addsd " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kSubsd: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movsd " << dst << ", " << lhs << "\n";
    os << "  subsd " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kMulsd: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movsd " << dst << ", " << lhs << "\n";
    os << "  mulsd " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kDivsd: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movsd " << dst << ", " << lhs << "\n";
    os << "  divsd " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kCmpsd: {
    if (mi.operands.size() < 2)
      break;
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    os << "  comisd " << lhs << ", " << rhs << "\n";
    break;
  }
  // SIMD instructions
  case Opcode::kMovaps: {
    std::string dst =
        mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (dst != src)
      os << "  movaps " << dst << ", " << src << "\n";
    break;
  }
  case Opcode::kMovups: {
    std::string dst =
        mi.def >= 0 ? RegisterName(def_reg) : FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (dst != src)
      os << "  movups " << dst << ", " << src << "\n";
    break;
  }
  case Opcode::kAddps: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movaps " << dst << ", " << lhs << "\n";
    os << "  addps " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kSubps: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movaps " << dst << ", " << lhs << "\n";
    os << "  subps " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kMulps: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movaps " << dst << ", " << lhs << "\n";
    os << "  mulps " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kDivps: {
    if (mi.operands.size() < 2 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movaps " << dst << ", " << lhs << "\n";
    os << "  divps " << dst << ", " << rhs << "\n";
    break;
  }
  case Opcode::kShufps: {
    if (mi.operands.size() < 3 || mi.def < 0)
      break;
    std::string dst = RegisterName(def_reg);
    std::string lhs = FormatOperand(mi.operands[0], alloc, pre);
    std::string rhs = FormatOperand(mi.operands[1], alloc, pre);
    std::string imm = FormatOperand(mi.operands[2], alloc, pre);
    os << pre.str();
    if (dst != lhs)
      os << "  movaps " << dst << ", " << lhs << "\n";
    os << "  shufps " << dst << ", " << rhs << ", " << imm << "\n";
    break;
  }
  case Opcode::kLoad: {
    std::string dst = RegisterName(def_reg);
    std::string mem = FormatOperand(mi.operands[0], alloc, pre);
    os << pre.str();
    if (mi.memory_width == 4 && mi.memory_signed)
      os << "  movsxd " << dst << ", dword ptr " << mem << "\n";
    else
      os << "  mov " << dst << ", " << mem << "\n";
    break;
  }
  case Opcode::kStore: {
    if (mi.operands.size() < 2)
      break;
    std::string mem = FormatOperand(mi.operands[0], alloc, pre);
    std::string src = FormatOperand(mi.operands[1], alloc, pre);
    os << pre.str();
    const char *size = mi.memory_width == 1 ? "byte ptr "
                       : mi.memory_width == 2 ? "word ptr "
                       : mi.memory_width == 4 ? "dword ptr "
                                              : "qword ptr ";
    os << "  mov " << size << mem << ", " << src << "\n";
    break;
  }
  case Opcode::kLea: {
    std::string dst = RegisterName(def_reg);
    std::string mem;
    if (mi.operands[0].kind == Operand::Kind::kStackObject &&
        mi.operands[0].stack_slot >= 0 &&
        static_cast<std::size_t>(mi.operands[0].stack_slot) < stack_object_offsets.size()) {
      const long long offset =
          stack_object_offsets[mi.operands[0].stack_slot] - mi.operands[0].displacement;
      mem = offset >= 0 ? "[rbp - " + std::to_string(offset) + "]"
                        : "[rbp + " + std::to_string(-offset) + "]";
    } else {
      mem = FormatOperand(mi.operands[0], alloc, pre);
    }
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
      if (src != dst)
        os << "  mov " << dst << ", " << src << "\n";
    }
    if (!mi.operands.empty()) {
      const auto &label = mi.operands.back();
      os << "  call " << FormatOperand(label, alloc, pre) << "\n";
    }
    if (mi.def >= 0) {
      std::string dst = RegisterName(def_reg);
      if (dst != "rax")
        os << "  mov " << dst << ", rax\n";
    }
    break;
  }
  case Opcode::kJmp: {
    os << pre.str();
    if (!mi.operands.empty())
      os << "  jmp " << FormatOperand(mi.operands[0], alloc, pre) << "\n";
    break;
  }
  case Opcode::kJcc: {
    os << pre.str();
    if (mi.operands.size() >= 1)
      os << "  jne " << FormatOperand(mi.operands[0], alloc, pre) << "\n";
    if (mi.operands.size() >= 2)
      os << "  jmp " << FormatOperand(mi.operands[1], alloc, pre) << "\n";
    break;
  }
  case Opcode::kRet: {
    os << pre.str();
    if (!mi.operands.empty()) {
      std::string src = FormatOperand(mi.operands[0], alloc, pre);
      if (src != "rax")
        os << "  mov rax, " << src << "\n";
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
    return r == Register::kRbx || r == Register::kR12 || r == Register::kR13 ||
           r == Register::kR14 || r == Register::kR15;
  };
  std::vector<Register> callee_used;
  for (auto &kv : alloc.vreg_to_phys) {
    if (is_callee_saved(kv.second))
      callee_used.push_back(kv.second);
  }
  std::sort(callee_used.begin(), callee_used.end());
  callee_used.erase(std::unique(callee_used.begin(), callee_used.end()), callee_used.end());

  const auto object_layout = LayoutStackObjects(mf, alloc.stack_slots * 8);
  int stack_bytes = object_layout.extent;
  if ((stack_bytes + 8) % 16 != 0)
    stack_bytes += 8; // align after push rbp

  os << ".globl " << mf.name << "\n";
  os << mf.name << ":\n";
  os << "  push rbp\n";
  os << "  mov rbp, rsp\n";
  if (stack_bytes > 0)
    os << "  sub rsp, " << stack_bytes << "\n";
  for (auto r : callee_used) {
    os << "  push " << RegisterName(r) << "\n";
  }

  for (const auto &bb : mf.blocks) {
    os << bb.name << ":\n";
    for (const auto &mi : bb.instructions) {
      EmitInstruction(mi, alloc, object_layout.offsets, os);
    }
  }

  for (auto it = callee_used.rbegin(); it != callee_used.rend(); ++it) {
    os << "  pop " << RegisterName(*it) << "\n";
  }
  os << "  leave\n";
  os << "  ret\n";
}

} // namespace

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
    // The generic scheduler currently models virtual-register dependencies,
    // but not aliases between stack loads and stores.  Keep memory-bearing
    // functions in source order until memory dependencies are represented in
    // MachineIR; otherwise mutable Poly locals can be read before their store.
    if (mf.stack_objects.empty())
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
  case Register::kRax:
    return 0;
  case Register::kRcx:
    return 1;
  case Register::kRdx:
    return 2;
  case Register::kRbx:
    return 3;
  case Register::kRsp:
    return 4;
  case Register::kRbp:
    return 5;
  case Register::kRsi:
    return 6;
  case Register::kRdi:
    return 7;
  case Register::kR8:
    return 0;
  case Register::kR9:
    return 1;
  case Register::kR10:
    return 2;
  case Register::kR11:
    return 3;
  case Register::kR12:
    return 4;
  case Register::kR13:
    return 5;
  case Register::kR14:
    return 6;
  case Register::kR15:
    return 7;
  default:
    return 0;
  }
}

bool IsExtendedRegister(Register r) {
  return r >= Register::kR8 && r <= Register::kR15;
}

void EmitRex(std::vector<std::uint8_t> &data, bool rex_r = false,
             bool rex_b = false) {
  data.push_back(static_cast<std::uint8_t>(0x48 | (rex_r ? 0x04 : 0x00) |
                                           (rex_b ? 0x01 : 0x00)));
}

void EmitMemoryRex(std::vector<std::uint8_t> &data, bool wide,
                   Register reg, Register base) {
  const std::uint8_t rex = static_cast<std::uint8_t>(
      0x40 | (wide ? 0x08 : 0x00) |
      (IsExtendedRegister(reg) ? 0x04 : 0x00) |
      (IsExtendedRegister(base) ? 0x01 : 0x00));
  if (rex != 0x40)
    data.push_back(rex);
}

void EmitMemoryModRM(std::vector<std::uint8_t> &data, Register reg,
                     Register base, long long displacement) {
  const std::uint8_t base_code = RegCode(base);
  std::uint8_t mod = 0b00;
  if (displacement != 0 || base_code == RegCode(Register::kRbp))
    mod = displacement >= -128 && displacement <= 127 ? 0b01 : 0b10;

  // RSP/R12 addressing requires a SIB byte even without an index.
  const bool needs_sib = base_code == RegCode(Register::kRsp);
  data.push_back(ModRM(mod, RegCode(reg), needs_sib ? 0b100 : base_code));
  if (needs_sib)
    data.push_back(static_cast<std::uint8_t>((0b00 << 6) | (0b100 << 3) | base_code));

  if (mod == 0b01) {
    data.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(displacement)));
  } else if (mod == 0b10) {
    const std::int32_t disp = static_cast<std::int32_t>(displacement);
    for (int i = 0; i < 4; ++i)
      data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
  }
}

void EmitLoadMemory(std::vector<std::uint8_t> &data, Register dst,
                    Register base, long long displacement,
                    std::size_t width, bool is_signed) {
  width = width == 0 ? 8 : width;
  if (width == 8) {
    EmitMemoryRex(data, true, dst, base);
    data.push_back(0x8B);
  } else if (width == 4 && is_signed) {
    EmitMemoryRex(data, true, dst, base);
    data.push_back(0x63); // movsxd r64, r/m32
  } else if (width == 4) {
    EmitMemoryRex(data, false, dst, base);
    data.push_back(0x8B); // writing r32 zero-extends into r64
  } else {
    EmitMemoryRex(data, true, dst, base);
    data.push_back(0x0F);
    if (width == 2)
      data.push_back(is_signed ? 0xBF : 0xB7); // movsx/movzx r64, r/m16
    else
      data.push_back(is_signed ? 0xBE : 0xB6); // movsx/movzx r64, r/m8
  }
  EmitMemoryModRM(data, dst, base, displacement);
}

void EmitStoreMemory(std::vector<std::uint8_t> &data, Register src,
                     Register base, long long displacement,
                     std::size_t width) {
  width = width == 0 ? 8 : width;
  if (width == 2)
    data.push_back(0x66);
  EmitMemoryRex(data, width == 8, src, base);
  data.push_back(width == 1 ? 0x88 : 0x89);
  EmitMemoryModRM(data, src, base, displacement);
}

void EmitLeaMemory(std::vector<std::uint8_t> &data, Register dst,
                   Register base, long long displacement) {
  EmitMemoryRex(data, true, dst, base);
  data.push_back(0x8D);
  EmitMemoryModRM(data, dst, base, displacement);
}

void EmitMovRegReg(std::vector<std::uint8_t> &data, Register dst, Register src) {
  EmitRex(data, IsExtendedRegister(src), IsExtendedRegister(dst));
  data.push_back(0x89); // mov r/m64, r64
  data.push_back(ModRM(0b11, RegCode(src), RegCode(dst)));
}

void EmitMovImmReg(std::vector<std::uint8_t> &data, Register dst, long long value) {
  EmitRex(data, false, IsExtendedRegister(dst));
  data.push_back(static_cast<std::uint8_t>(0xB8 + RegCode(dst)));
  const auto imm = static_cast<std::uint64_t>(value);
  for (int i = 0; i < 8; ++i)
    data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
}

void EmitFrameStore(std::vector<std::uint8_t> &data, Register src, int offset) {
  EmitRex(data, IsExtendedRegister(src), false);
  data.push_back(0x89); // mov [rbp-offset], src
  if (offset <= 127) {
    data.push_back(ModRM(0b01, RegCode(src), RegCode(Register::kRbp)));
    data.push_back(static_cast<std::uint8_t>(-offset));
  } else {
    data.push_back(ModRM(0b10, RegCode(src), RegCode(Register::kRbp)));
    const std::int32_t disp = -offset;
    for (int i = 0; i < 4; ++i)
      data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
  }
}

void EmitFrameLoad(std::vector<std::uint8_t> &data, Register dst, int offset) {
  EmitRex(data, IsExtendedRegister(dst), false);
  data.push_back(0x8B); // mov dst, [rbp-offset]
  if (offset <= 127) {
    data.push_back(ModRM(0b01, RegCode(dst), RegCode(Register::kRbp)));
    data.push_back(static_cast<std::uint8_t>(-offset));
  } else {
    data.push_back(ModRM(0b10, RegCode(dst), RegCode(Register::kRbp)));
    const std::int32_t disp = -offset;
    for (int i = 0; i < 4; ++i)
      data.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
  }
}

constexpr int kSavedRbxOffset = 8;
constexpr int kIncomingArgBaseOffset = 16;
constexpr int kCallSaveRaxOffset = 64;
constexpr int kCallSaveRdxOffset = 72;
constexpr int kNativeSpillBaseOffset = 80;

int IncomingArgOffset(std::size_t index) {
  return kIncomingArgBaseOffset + static_cast<int>(index) * 8;
}

int NativeSpillOffset(int slot) {
  return kNativeSpillBaseOffset + slot * 8;
}

} // namespace

X86Target::MCResult X86Target::EmitObjectCode() {
  MCResult result;
  if (!module_ || module_->Functions().empty())
    return result;

  CostModel cost_model;
  // Reserve RCX for literal materialization in div/rem emission; do not allocate it.
  std::vector<Register> available = {Register::kRax, Register::kRbx, Register::kRdx};

  // Gather text section data and symbols across all functions.
  MCSection text_sec;
  text_sec.name = ".text";

  std::unordered_map<std::string, std::uint32_t> label_offsets;

  for (auto &fn_ptr : module_->Functions()) {
    auto mf = SelectInstructions(*fn_ptr, cost_model);
    if (mf.stack_objects.empty())
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

    auto is_spilled_vreg = [&](int vreg) {
      return alloc.vreg_to_phys.find(vreg) == alloc.vreg_to_phys.end() &&
             alloc.vreg_to_slot.find(vreg) != alloc.vreg_to_slot.end();
    };
    auto spill_offset = [&](int vreg) {
      return NativeSpillOffset(alloc.vreg_to_slot.at(vreg));
    };
    auto operand_register = [&](const Operand &operand, Register scratch) {
      if (operand.kind == Operand::Kind::kPhysReg)
        return operand.phys;
      if (operand.kind == Operand::Kind::kVReg) {
        auto phys = alloc.vreg_to_phys.find(operand.vreg);
        if (phys != alloc.vreg_to_phys.end())
          return phys->second;
        auto slot = alloc.vreg_to_slot.find(operand.vreg);
        if (slot != alloc.vreg_to_slot.end()) {
          EmitFrameLoad(text_sec.data, scratch, NativeSpillOffset(slot->second));
          return scratch;
        }
      }
      return scratch;
    };
    auto def_register = [&](int vreg, Register scratch) {
      auto phys = alloc.vreg_to_phys.find(vreg);
      return phys != alloc.vreg_to_phys.end() ? phys->second : scratch;
    };
    auto commit_def = [&](int vreg, Register value) {
      if (vreg >= 0 && is_spilled_vreg(vreg))
        EmitFrameStore(text_sec.data, value, spill_offset(vreg));
    };

    const auto stack_object_layout = LayoutStackObjects(
        mf, kCallSaveRdxOffset + alloc.stack_slots * 8);

    std::size_t func_start = text_sec.data.size();
    label_offsets[mf.name] = static_cast<std::uint32_t>(func_start);
    result.symbols.push_back(
        {mf.name, ".text", static_cast<std::uint64_t>(func_start), 0, true, true});

    // Native frame layout reserves stable slots for RBX, the six incoming
    // integer arguments, caller-clobber snapshots, and allocator spills.
    const int raw_frame_bytes = stack_object_layout.extent;
    const int frame_bytes = (raw_frame_bytes + 15) & ~15;

    // Prologue
    text_sec.data.insert(text_sec.data.end(), {0x55});             // push rbp
    text_sec.data.insert(text_sec.data.end(), {0x48, 0x89, 0xE5}); // mov rbp, rsp
    if (frame_bytes > 0) {
      text_sec.data.insert(text_sec.data.end(), {0x48, 0x81, 0xEC}); // sub rsp, imm32
      const auto amount = static_cast<std::uint32_t>(frame_bytes);
      for (int i = 0; i < 4; ++i)
        text_sec.data.push_back(static_cast<std::uint8_t>((amount >> (i * 8)) & 0xFF));
    }
    EmitFrameStore(text_sec.data, Register::kRbx, kSavedRbxOffset);

    // Materialise incoming System V integer arguments into the virtual
    // registers assigned to source-level parameters.  The current native
    // frontend subset uses scalar integer parameters; float and stack-passed
    // parameters remain handled by the broader ABI work track.
    const std::vector<Register> integer_arg_regs = {
        Register::kRdi, Register::kRsi, Register::kRdx,
        Register::kRcx, Register::kR8, Register::kR9};
    const std::size_t incoming_count =
        std::min(mf.param_vregs.size(), integer_arg_regs.size());
    // Snapshot first, then materialise.  This is a parallel copy: assigning
    // parameter zero to RDX must not destroy the still-unread third argument.
    for (std::size_t i = 0; i < incoming_count; ++i) {
      if (i < mf.param_is_float.size() && mf.param_is_float[i])
        continue;
      EmitFrameStore(text_sec.data, integer_arg_regs[i], IncomingArgOffset(i));
    }
    for (std::size_t i = 0; i < incoming_count; ++i) {
      if (i < mf.param_is_float.size() && mf.param_is_float[i])
        continue;
      const int vreg = mf.param_vregs[i];
      if (is_spilled_vreg(vreg)) {
        EmitFrameLoad(text_sec.data, Register::kRcx, IncomingArgOffset(i));
        EmitFrameStore(text_sec.data, Register::kRcx, spill_offset(vreg));
      } else {
        EmitFrameLoad(text_sec.data, def_register(vreg, Register::kRcx),
                      IncomingArgOffset(i));
      }
    }

    // Track block labels
    for (const auto &bb : mf.blocks) {
      label_offsets[bb.name] = static_cast<std::uint32_t>(text_sec.data.size());
      result.symbols.push_back(
          {bb.name, ".text", static_cast<std::uint64_t>(text_sec.data.size()), 0, false, true});

      for (const auto &mi : bb.instructions) {
        switch (mi.opcode) {
        case Opcode::kMov: {
          if (mi.operands.empty())
            break;
          const auto &src = mi.operands[0];
          Register dst_reg =
              mi.def >= 0 ? def_register(mi.def, Register::kRcx)
                          : operand_register(src, Register::kRcx);
          if (src.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, dst_reg, src.imm);
          } else if (src.kind == Operand::Kind::kVReg || src.kind == Operand::Kind::kPhysReg) {
            Register src_reg = operand_register(src, Register::kRcx);
            if (src_reg != dst_reg)
              EmitMovRegReg(text_sec.data, dst_reg, src_reg);
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kAdd: {
          if (mi.operands.size() < 2)
            break;
          Register dst_reg = def_register(mi.def, Register::kRcx);
          const auto &lhs = mi.operands[0];
          const auto &rhs = mi.operands[1];
          if (lhs.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, dst_reg, lhs.imm);
          } else {
            Register lhs_reg = operand_register(lhs, Register::kRcx);
            if (lhs_reg != dst_reg)
              EmitMovRegReg(text_sec.data, dst_reg, lhs_reg);
          }
          if (rhs.kind == Operand::Kind::kImm) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x81);
            text_sec.data.push_back(ModRM(0b11, 0b000, RegCode(dst_reg))); // add r/m64, imm32
            std::int32_t imm = static_cast<std::int32_t>(rhs.imm);
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
          } else if (rhs.kind == Operand::Kind::kVReg || rhs.kind == Operand::Kind::kPhysReg) {
            Register src_reg = operand_register(rhs, Register::kRdi);
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x01);
            text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(dst_reg)));
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kSub: {
          if (mi.operands.size() < 2)
            break;
          Register dst_reg = def_register(mi.def, Register::kRcx);
          const auto &lhs = mi.operands[0];
          const auto &rhs = mi.operands[1];
          if (lhs.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, dst_reg, lhs.imm);
          } else {
            Register lhs_reg = operand_register(lhs, Register::kRcx);
            if (lhs_reg != dst_reg)
              EmitMovRegReg(text_sec.data, dst_reg, lhs_reg);
          }
          if (rhs.kind == Operand::Kind::kImm) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x81);
            text_sec.data.push_back(ModRM(0b11, 0b101, RegCode(dst_reg))); // sub r/m64, imm32
            std::int32_t imm = static_cast<std::int32_t>(rhs.imm);
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
          } else if (rhs.kind == Operand::Kind::kVReg || rhs.kind == Operand::Kind::kPhysReg) {
            Register src_reg = operand_register(rhs, Register::kRdi);
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x29);
            text_sec.data.push_back(ModRM(0b11, RegCode(src_reg), RegCode(dst_reg)));
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kMul: {
          if (mi.operands.size() < 2)
            break;
          Register dst_reg = def_register(mi.def, Register::kRcx);
          const auto &lhs = mi.operands[0];
          const auto &rhs = mi.operands[1];
          if (lhs.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, dst_reg, lhs.imm);
          } else {
            Register lhs_reg = operand_register(lhs, Register::kRcx);
            if (lhs_reg != dst_reg)
              EmitMovRegReg(text_sec.data, dst_reg, lhs_reg);
          }
          if (rhs.kind == Operand::Kind::kImm) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x69);
            text_sec.data.push_back(ModRM(0b11, RegCode(dst_reg), RegCode(dst_reg)));
            std::int32_t imm = static_cast<std::int32_t>(rhs.imm);
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
          } else {
            Register rhs_reg = operand_register(rhs, Register::kRdi);
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x0F);
            text_sec.data.push_back(0xAF);
            text_sec.data.push_back(ModRM(0b11, RegCode(dst_reg), RegCode(rhs_reg)));
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kDiv:
        case Opcode::kSDiv:
        case Opcode::kUDiv:
        case Opcode::kRem:
        case Opcode::kSRem:
        case Opcode::kURem: {
          if (mi.operands.size() < 2)
            break;
          const auto &lhs = mi.operands[0];
          const auto &rhs = mi.operands[1];

          // idiv/div implicitly overwrite RAX and RDX, which are allocator-
          // owned registers.  Preserve their live values just as the call
          // emitter does, and keep the result in reserved RCX while restoring
          // them.  This also gives spilled operands their native-frame reload
          // path instead of treating a spill slot as an old-style stack slot.
          EmitFrameStore(text_sec.data, Register::kRax, kCallSaveRaxOffset);
          EmitFrameStore(text_sec.data, Register::kRdx, kCallSaveRdxOffset);

          // Materialise the divisor in reserved RCX before touching RAX/RDX.
          if (rhs.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, Register::kRcx, rhs.imm);
          } else {
            const Register rhs_reg = operand_register(rhs, Register::kRcx);
            if (rhs_reg == Register::kRax) {
              EmitFrameLoad(text_sec.data, Register::kRcx, kCallSaveRaxOffset);
            } else if (rhs_reg == Register::kRdx) {
              EmitFrameLoad(text_sec.data, Register::kRcx, kCallSaveRdxOffset);
            } else if (rhs_reg != Register::kRcx) {
              EmitMovRegReg(text_sec.data, Register::kRcx, rhs_reg);
            }
          }

          // Move lhs into RAX.
          if (lhs.kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, Register::kRax, lhs.imm);
          } else if (lhs.kind == Operand::Kind::kVReg &&
                     is_spilled_vreg(lhs.vreg)) {
            // A spilled lhs is deliberately reloaded into RAX after the
            // allocator-owned RAX/RDX snapshots.  Do not mistake that scratch
            // register for a value that was originally resident in RAX and
            // overwrite it with the saved pre-division contents.
            EmitFrameLoad(text_sec.data, Register::kRax,
                          spill_offset(lhs.vreg));
          } else {
            const Register lhs_reg = operand_register(lhs, Register::kRax);
            if (lhs_reg == Register::kRax) {
              EmitFrameLoad(text_sec.data, Register::kRax, kCallSaveRaxOffset);
            } else if (lhs_reg == Register::kRdx) {
              EmitFrameLoad(text_sec.data, Register::kRax, kCallSaveRdxOffset);
            } else {
              EmitMovRegReg(text_sec.data, Register::kRax, lhs_reg);
            }
          }

          bool signed_op = (mi.opcode == Opcode::kDiv || mi.opcode == Opcode::kSDiv ||
                            mi.opcode == Opcode::kRem || mi.opcode == Opcode::kSRem);

          // Prepare RDX
          if (signed_op) {
            text_sec.data.push_back(0x48); // cqo
            text_sec.data.push_back(0x99);
          } else {
            text_sec.data.push_back(0x48); // xor rdx, rdx
            text_sec.data.push_back(0x31);
            text_sec.data.push_back(0xD2);
          }

          // Emit div/idiv rcx.
          std::uint8_t div_opcode = signed_op ? 0b111 : 0b110; // /7 for idiv, /6 for div
          text_sec.data.push_back(0x48);
          text_sec.data.push_back(0xF7);
          text_sec.data.push_back(
              ModRM(0b11, div_opcode, RegCode(Register::kRcx)));

          // Preserve the result while restoring allocator-owned registers,
          // then assign it to the definition (or its spill slot).
          if (mi.def >= 0) {
            bool is_rem = (mi.opcode == Opcode::kRem || mi.opcode == Opcode::kSRem ||
                           mi.opcode == Opcode::kURem);
            Register src = is_rem ? Register::kRdx : Register::kRax;
            EmitMovRegReg(text_sec.data, Register::kRcx, src);
          }
          EmitFrameLoad(text_sec.data, Register::kRax, kCallSaveRaxOffset);
          EmitFrameLoad(text_sec.data, Register::kRdx, kCallSaveRdxOffset);
          if (mi.def >= 0) {
            Register dst = def_register(mi.def, Register::kRcx);
            if (dst != Register::kRcx)
              EmitMovRegReg(text_sec.data, dst, Register::kRcx);
            commit_def(mi.def, dst);
          }
          break;
        }
        case Opcode::kCmp: {
          if (mi.operands.size() < 2)
            break;

          auto emit_mov_imm64 = [&](Register dst, std::uint64_t imm) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(
                static_cast<std::uint8_t>(0xB8 + RegCode(dst)));
            for (int i = 0; i < 8; ++i)
              text_sec.data.push_back(
                  static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
          };
          auto emit_cmp_reg_reg = [&](Register lhs, Register rhs) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x39); // cmp r/m64, r64
            text_sec.data.push_back(ModRM(0b11, RegCode(rhs), RegCode(lhs)));
          };
          auto emit_cmp_reg_imm32 = [&](Register lhs, std::int32_t imm) {
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x81);
            text_sec.data.push_back(ModRM(0b11, 0b111, RegCode(lhs)));
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(
                  static_cast<std::uint8_t>((imm >> (i * 8)) & 0xFF));
          };

          const auto &lhs = mi.operands[0];
          const auto &rhs = mi.operands[1];
          Register lhs_reg = Register::kRcx; // RCX is reserved from allocation.
          if (lhs.kind == Operand::Kind::kImm) {
            emit_mov_imm64(lhs_reg, static_cast<std::uint64_t>(lhs.imm));
          } else {
            lhs_reg = operand_register(lhs, Register::kRcx);
          }

          if (rhs.kind == Operand::Kind::kImm) {
            if (rhs.imm >= std::numeric_limits<std::int32_t>::min() &&
                rhs.imm <= std::numeric_limits<std::int32_t>::max()) {
              emit_cmp_reg_imm32(lhs_reg, static_cast<std::int32_t>(rhs.imm));
            } else {
              // x86-64 has no cmp r64, imm64 encoding: materialise a wide
              // model/checksum constant in non-allocated RDI first.
              EmitMovImmReg(text_sec.data, Register::kRdi, rhs.imm);
              emit_cmp_reg_reg(lhs_reg, Register::kRdi);
            }
          } else {
            emit_cmp_reg_reg(lhs_reg, operand_register(rhs, Register::kRdi));
          }

          // A comparison instruction originating from an IR binary compare
          // has a result vreg and a predicate operand.  Materialise a stable
          // 0/1 integer rather than leaving the value implicit in EFLAGS.
          if (mi.def >= 0) {
            Register dst = def_register(mi.def, Register::kRcx);
            text_sec.data.push_back(0x0F);
            text_sec.data.push_back(SetccOpcode(PredicateCode(mi)));
            text_sec.data.push_back(ModRM(0b11, 0, RegCode(dst)));
            text_sec.data.push_back(0x48);
            text_sec.data.push_back(0x0F);
            text_sec.data.push_back(0xB6); // movzx r64, r/m8
            text_sec.data.push_back(
                ModRM(0b11, RegCode(dst), RegCode(dst)));
            commit_def(mi.def, dst);
          }
          break;
        }
        case Opcode::kLoad: {
          if (mi.operands.empty())
            break;
          Register dst_reg = def_register(mi.def, Register::kRcx);
          const auto &mem = mi.operands[0];
          if (mem.kind == Operand::Kind::kStackSlot) {
            EmitLoadMemory(text_sec.data, dst_reg, Register::kRbp,
                           -StackOffsetBytes(mem.stack_slot),
                           mi.memory_width, mi.memory_signed);
          } else if (mem.kind == Operand::Kind::kMemVReg) {
            const Register base = operand_register(
                Operand::VReg(mem.vreg), Register::kRdi);
            EmitLoadMemory(text_sec.data, dst_reg, base, mem.displacement,
                           mi.memory_width, mi.memory_signed);
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kStore: {
          if (mi.operands.size() < 2)
            break;
          const auto &mem = mi.operands[0];
          Register src_reg = Register::kRcx;
          if (mi.operands[1].kind == Operand::Kind::kImm) {
            EmitMovImmReg(text_sec.data, src_reg, mi.operands[1].imm);
          } else {
            src_reg = operand_register(mi.operands[1], Register::kRcx);
          }
          if (mem.kind == Operand::Kind::kStackSlot) {
            EmitStoreMemory(text_sec.data, src_reg, Register::kRbp,
                            -StackOffsetBytes(mem.stack_slot),
                            mi.memory_width);
          } else if (mem.kind == Operand::Kind::kMemVReg) {
            const Register base = operand_register(
                Operand::VReg(mem.vreg), Register::kRdi);
            EmitStoreMemory(text_sec.data, src_reg, base, mem.displacement,
                            mi.memory_width);
          }
          break;
        }
        case Opcode::kJmp: {
          if (mi.operands.empty())
            break;
          const auto &target = mi.operands[0];
          std::size_t reloc_offset = text_sec.data.size() + 1;
          text_sec.data.push_back(0xE9);
          for (int i = 0; i < 4; ++i)
            text_sec.data.push_back(0x00);
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
          if (mi.operands.size() < 1)
            break;
          const auto &target = mi.operands[0];
          std::size_t reloc_offset = text_sec.data.size() + 2;
          text_sec.data.push_back(0x0F);
          text_sec.data.push_back(0x85); // jne rel32
          for (int i = 0; i < 4; ++i)
            text_sec.data.push_back(0x00);
          MCReloc r;
          r.section = ".text";
          r.offset = static_cast<std::uint32_t>(reloc_offset);
          r.type = 1;
          r.symbol = target.label;
          r.addend = -4;
          result.relocs.push_back(r);
          if (mi.operands.size() >= 2) {
            const auto &false_target = mi.operands[1];
            std::size_t false_reloc_offset = text_sec.data.size() + 1;
            text_sec.data.push_back(0xE9); // jmp rel32
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(0x00);
            MCReloc false_reloc;
            false_reloc.section = ".text";
            false_reloc.offset =
                static_cast<std::uint32_t>(false_reloc_offset);
            false_reloc.type = 1;
            false_reloc.symbol = false_target.label;
            false_reloc.addend = -4;
            result.relocs.push_back(false_reloc);
          }
          break;
        }
        case Opcode::kCall: {
          if (mi.operands.empty())
            break;
          // Preserve allocator-owned caller-clobbered registers.  Arguments
          // are then copied from this snapshot, which also makes overlapping
          // moves such as RDX -> RDI / arg3 -> RDX behave like a parallel copy.
          EmitFrameStore(text_sec.data, Register::kRax, kCallSaveRaxOffset);
          EmitFrameStore(text_sec.data, Register::kRdx, kCallSaveRdxOffset);

          // Move scalar arguments into their System V ABI registers.  The
          // final operand is the callee label.
          const std::size_t arg_count = mi.operands.size() - 1;
          for (std::size_t i = 0; i < arg_count && i < integer_arg_regs.size(); ++i) {
            const auto &arg = mi.operands[i];
            Register dst = integer_arg_regs[i];
            if (arg.kind == Operand::Kind::kImm) {
              EmitMovImmReg(text_sec.data, dst, arg.imm);
            } else if (arg.kind == Operand::Kind::kVReg) {
              auto phys = alloc.vreg_to_phys.find(arg.vreg);
              if (phys != alloc.vreg_to_phys.end()) {
                if (phys->second == Register::kRax) {
                  EmitFrameLoad(text_sec.data, dst, kCallSaveRaxOffset);
                } else if (phys->second == Register::kRdx) {
                  EmitFrameLoad(text_sec.data, dst, kCallSaveRdxOffset);
                } else if (phys->second != dst) {
                  EmitMovRegReg(text_sec.data, dst, phys->second);
                }
              } else {
                auto slot = alloc.vreg_to_slot.find(arg.vreg);
                if (slot != alloc.vreg_to_slot.end())
                  EmitFrameLoad(text_sec.data, dst, NativeSpillOffset(slot->second));
              }
            } else if (arg.kind == Operand::Kind::kPhysReg && arg.phys != dst) {
              EmitMovRegReg(text_sec.data, dst, arg.phys);
            }
          }
          const auto &target = mi.operands.back();
          std::size_t reloc_offset = text_sec.data.size() + 1;
          text_sec.data.push_back(0xE8);
          for (int i = 0; i < 4; ++i)
            text_sec.data.push_back(0x00);
          MCReloc r;
          r.section = ".text";
          r.offset = static_cast<std::uint32_t>(reloc_offset);
          r.type = 1;
          r.symbol = target.label;
          r.addend = -4;
          result.relocs.push_back(r);
          result.symbols.push_back({target.label, "", 0, 0, true, false});

          // Keep the return value in reserved RCX while restoring the caller's
          // live RAX/RDX values, then assign it to the call-result vreg.
          if (mi.def >= 0)
            EmitMovRegReg(text_sec.data, Register::kRcx, Register::kRax);
          EmitFrameLoad(text_sec.data, Register::kRax, kCallSaveRaxOffset);
          EmitFrameLoad(text_sec.data, Register::kRdx, kCallSaveRdxOffset);
          if (mi.def >= 0) {
            Register dst = def_register(mi.def, Register::kRcx);
            if (dst != Register::kRcx)
              EmitMovRegReg(text_sec.data, dst, Register::kRcx);
            commit_def(mi.def, dst);
          }
          break;
        }
        case Opcode::kLea: {
          // PE-7-C: Materialise the address of an external/global symbol
          // into a register via `lea reg, [rip + 0]` plus a REL32
          // relocation that polyld will patch with the correct
          // PC-relative displacement at link time. The 32-bit field uses
          // addend `-4` because the CPU evaluates `[rip+disp32]` after
          // the instruction has been fetched, so the displacement must be
          // measured from the byte right after the disp32 itself.
          if (mi.operands.empty() || mi.def < 0)
            break;
          const auto &src = mi.operands[0];
          Register dst_reg = def_register(mi.def, Register::kRcx);
          if (src.kind == Operand::Kind::kLabel) {
            EmitMemoryRex(text_sec.data, true, dst_reg, Register::kRbp);
            text_sec.data.push_back(0x8D); // lea reg, [rip+disp32]
            text_sec.data.push_back(ModRM(0b00, RegCode(dst_reg), 0b101));
            std::size_t reloc_offset = text_sec.data.size();
            for (int i = 0; i < 4; ++i)
              text_sec.data.push_back(0x00);
            MCReloc r;
            r.section = ".text";
            r.offset = static_cast<std::uint32_t>(reloc_offset);
            r.type = 1;
            r.symbol = src.label;
            r.addend = -4;
            result.relocs.push_back(r);
            result.symbols.push_back({src.label, "", 0, 0, true, false});
          } else if (src.kind == Operand::Kind::kMemVReg) {
            const Register base = operand_register(
                Operand::VReg(src.vreg), Register::kRdi);
            EmitLeaMemory(text_sec.data, dst_reg, base, src.displacement);
          } else if (src.kind == Operand::Kind::kStackObject &&
                     src.stack_slot >= 0 &&
                     static_cast<std::size_t>(src.stack_slot) <
                         stack_object_layout.offsets.size()) {
            const long long displacement =
                -stack_object_layout.offsets[src.stack_slot] + src.displacement;
            EmitLeaMemory(text_sec.data, dst_reg, Register::kRbp,
                          displacement);
          }
          commit_def(mi.def, dst_reg);
          break;
        }
        case Opcode::kRet: {
          if (!mi.operands.empty()) {
            const auto &src = mi.operands[0];
            if (src.kind == Operand::Kind::kImm) {
              EmitMovImmReg(text_sec.data, Register::kRax, src.imm);
            } else {
              Register src_reg = operand_register(src, Register::kRcx);
              if (src_reg != Register::kRax)
                EmitMovRegReg(text_sec.data, Register::kRax, src_reg);
            }
          }
          EmitFrameLoad(text_sec.data, Register::kRbx, kSavedRbxOffset);
          text_sec.data.push_back(0xC9); // leave
          text_sec.data.push_back(0xC3); // ret
          break;
        }
        default:
          text_sec.data.push_back(0x90);
          break;
        }
      }
    }

    std::size_t func_end = text_sec.data.size();
    if (!result.symbols.empty())
      result.symbols.back().size = func_end - func_start;
  }

  // Emit IR-level read-only globals into a real `.rdata` section. The two
  // shapes we recognise are:
  //   * `ConstantString` initializers — the bytes interned by
  //     `IRBuilder::MakeStringLiteral` plus a trailing NUL so legacy CRT
  //     consumers that still expect C-strings keep working alongside the
  //     pointer+length runtime contract.
  //   * `ConstantGEP` initializers whose base is one of the above strings —
  //     these are the `.ptr` aliases the lowering layer hands to
  //     `polyrt_println` calls. We materialise an 8-byte ABS64 slot and add
  //     a relocation so that polyld patches the slot with the runtime
  //     address of the underlying string at link time.
  // The two sub-passes are emitted in dependency order (strings first,
  // then aliases) so the alias relocations can name a symbol that already
  // exists in `result.symbols`.
  MCSection rodata;
  rodata.name = ".rdata";
  for (const auto &gv : module_->Globals()) {
    if (!gv || !gv->initializer)
      continue;
    auto cs = std::dynamic_pointer_cast<polyglot::ir::ConstantString>(gv->initializer);
    if (!cs)
      continue;
    std::uint64_t off = static_cast<std::uint64_t>(rodata.data.size());
    rodata.data.insert(rodata.data.end(), cs->data.begin(), cs->data.end());
    if (cs->null_terminated) {
      rodata.data.push_back(0x00);
    }
    std::uint64_t total = static_cast<std::uint64_t>(cs->data.size()) +
                          (cs->null_terminated ? 1u : 0u);
    result.symbols.push_back({gv->name, ".rdata", off, total, true, true});
  }
  for (const auto &gv : module_->Globals()) {
    if (!gv || !gv->initializer)
      continue;
    auto gep = std::dynamic_pointer_cast<polyglot::ir::ConstantGEP>(gv->initializer);
    if (!gep || !gep->base)
      continue;
    auto base_gv = std::dynamic_pointer_cast<polyglot::ir::GlobalValue>(gep->base);
    if (!base_gv)
      continue;
    std::uint64_t off = static_cast<std::uint64_t>(rodata.data.size());
    rodata.data.insert(rodata.data.end(), 8, 0x00);
    result.symbols.push_back({gv->name, ".rdata", off, 8u, true, true});
    MCReloc r;
    r.section = ".rdata";
    r.offset = static_cast<std::uint32_t>(off);
    r.type = 0; // ABS64 — polyld patches the 8-byte slot with the runtime
                // address of the underlying string symbol.
    r.symbol = base_gv->name;
    r.addend = 0;
    result.relocs.push_back(r);
  }

  result.sections.push_back(std::move(text_sec));
  if (!rodata.data.empty()) {
    result.sections.push_back(std::move(rodata));
  }
  return result;
}

} // namespace polyglot::backends::x86_64
