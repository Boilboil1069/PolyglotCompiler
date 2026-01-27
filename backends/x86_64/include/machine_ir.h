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
  kAnd,
  kOr,
  kCmp,
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

  static Operand VReg(int v) { return Operand{Kind::kVReg, v}; }
  static Operand Phys(Register r) {
    Operand op;
    op.kind = Kind::kPhysReg;
    op.phys = r;
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
  static Operand MemVReg(int v) {
    Operand op;
    op.kind = Kind::kMemVReg;
    op.vreg = v;
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

MachineFunction SelectInstructions(const ir::Function &fn, const CostModel &cost_model);
void ScheduleFunction(MachineFunction &fn);
AllocationResult LinearScanAllocate(const MachineFunction &fn, const std::vector<Register> &available);

}  // namespace polyglot::backends::x86_64
