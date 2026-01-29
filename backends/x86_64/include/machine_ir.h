#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "backends/x86_64/include/x86_register.h"
#include "middle/include/ir/cfg.h"

namespace polyglot::backends::x86_64 {

enum class Opcode {
  kMov,
  kAdd,
  kSub,
  kMul,
  kDiv,
  kSDiv,
  kUDiv,
  kRem,
  kSRem,
  kURem,
  kAnd,
  kOr,
  kXor,
  kShl,
  kLShr,
  kAShr,
  kCmp,
  // Floating-point operations
  kMovsd,     // movsd (scalar double)
  kMovss,     // movss (scalar single)
  kAddsd,     // addsd
  kSubsd,     // subsd
  kMulsd,     // mulsd
  kDivsd,     // divsd
  kCmpsd,     // comisd (compare scalar double)
  // SIMD operations
  kAddps,     // addps (packed single)
  kSubps,     // subps
  kMulps,     // mulps
  kDivps,     // divps
  kShufps,    // shufps
  kMovaps,    // movaps (aligned packed single)
  kMovups,    // movups (unaligned packed single)
  // General
  kLoad,
  kStore,
  kLea,
  kCall,
  kRet,
  kJmp,
  kJcc
};

struct Operand {
  enum class Kind { kVReg, kPhysReg, kImm, kLabel, kStackSlot, kMemVReg, kMemLabel };
  Kind kind{Kind::kImm};
  int vreg{-1};
  Register phys{Register::kRax};
  long long imm{0};
  std::string label;
  int stack_slot{-1};
  bool is_float{false};  // used for call arg placement

  static Operand VReg(int v, bool is_float = false) { return Operand{Kind::kVReg, v, Register::kRax, 0, "", -1, is_float}; }
  static Operand Phys(Register r, bool is_float = false) {
    Operand op;
    op.kind = Kind::kPhysReg;
    op.phys = r;
    op.is_float = is_float;
    return op;
  }
  static Operand Imm(long long v) {
    Operand op;
    op.kind = Kind::kImm;
    op.imm = v;
    return op;
  }
  static Operand Label(const std::string &name) {
    Operand op;
    op.kind = Kind::kLabel;
    op.label = name;
    return op;
  }
  static Operand Stack(int slot) {
    Operand op;
    op.kind = Kind::kStackSlot;
    op.stack_slot = slot;
    return op;
  }
  static Operand MemVReg(int v, bool is_float = false) {
    Operand op;
    op.kind = Kind::kMemVReg;
    op.vreg = v;
    op.is_float = is_float;
    return op;
  }
  static Operand MemLabel(const std::string &name) {
    Operand op;
    op.kind = Kind::kMemLabel;
    op.label = name;
    return op;
  }
};

struct MachineInstr {
  Opcode opcode{Opcode::kMov};
  int def{-1};
  std::vector<int> uses;
  std::vector<Operand> operands;
  int cost{1};
  int latency{1};
  bool terminator{false};
};

struct MachineBasicBlock {
  std::string name;
  std::vector<MachineInstr> instructions;
};

struct MachineFunction {
  std::string name;
  std::vector<MachineBasicBlock> blocks;
};

struct CostModel {
  int Cost(Opcode op) const;
  int Latency(Opcode op) const;
};

struct LiveInterval {
  int vreg{-1};
  int start{0};
  int end{0};
  bool spilled{false};
  Register phys{Register::kRax};
  int stack_slot{-1};
};

struct AllocationResult {
  std::unordered_map<int, Register> vreg_to_phys;
  std::unordered_map<int, int> vreg_to_slot;
  int stack_slots{0};
};

enum class RegAllocStrategy { kLinearScan, kGraphColoring };

MachineFunction SelectInstructions(const ir::Function &fn, const CostModel &cost_model);
void ScheduleFunction(MachineFunction &fn);
std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction &fn);
AllocationResult LinearScanAllocate(const MachineFunction &fn, const std::vector<Register> &available);
AllocationResult GraphColoringAllocate(const MachineFunction &fn, const std::vector<Register> &available);

}  // namespace polyglot::backends::x86_64
