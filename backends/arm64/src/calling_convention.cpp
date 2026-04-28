/**
 * @file     calling_convention.cpp
 * @brief    AArch64 ABI glue: instantiates the common
 *           `CallingConvention<Arm64TargetTraits>` facade and provides the
 *           target-specific assembly emitters (prologue, epilogue, call
 *           setup) that cannot live in the architecture-neutral header.
 *
 * @ingroup  Backend / ARM64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <ostream>
#include <string>
#include <vector>

#include "backends/arm64/include/arm64_target.h"
#include "backends/arm64/include/machine_ir.h"
#include "backends/common/include/abi/abi.h"

namespace polyglot::backends::arm64 {

using CommonCC    = ::polyglot::backends::common::abi::CallingConvention<Arm64TargetTraits>;
using StackFrame  = ::polyglot::backends::common::abi::StackFrame<Arm64TargetTraits>;

namespace {

// Helper: render a vreg / immediate / label operand into an AArch64 source
// expression usable on the right-hand side of `mov` / `ldr`.
std::string RenderOperand(const Operand& arg, const AllocationResult& alloc) {
    switch (arg.kind) {
    case Operand::Kind::kImm:
        return "#" + std::to_string(arg.imm);
    case Operand::Kind::kVReg: {
        auto phys_it = alloc.vreg_to_phys.find(arg.vreg);
        if (phys_it != alloc.vreg_to_phys.end()) {
            return RegisterName(phys_it->second);
        }
        auto slot_it = alloc.vreg_to_slot.find(arg.vreg);
        if (slot_it != alloc.vreg_to_slot.end()) {
            const int offset = (slot_it->second + 1) * 8;
            return "[sp, #" + std::to_string(offset) + "]";
        }
        return "x0";
    }
    default:
        return "x0";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Per-target wrapper around the common ABI facade.
// ---------------------------------------------------------------------------
struct CallingConvention {
    CommonCC cc;

    StackFrame ComputeStackFrame(const MachineFunction& fn,
                                 const AllocationResult& alloc) const {
        return ::polyglot::backends::common::abi::ComputeStackFrame<Arm64TargetTraits, Opcode>(
            cc, fn, alloc, Opcode::kCall);
    }

    void EmitPrologue(std::ostream& os, const StackFrame& frame) const {
        // Save FP and LR via the canonical AAPCS64 pre-indexed pair store.
        os << "  stp fp, lr, [sp, #-16]!\n";
        os << "  mov fp, sp\n";

        if (frame.total_size > 0) {
            if (frame.total_size < 4096) {
                os << "  sub sp, sp, #" << frame.total_size << "\n";
            } else {
                // Large stack frames need MOVZ / MOVK then a register-form sub.
                os << "  mov x16, #" << (frame.total_size & 0xFFFF) << "\n";
                if (frame.total_size > 0xFFFF) {
                    os << "  movk x16, #" << ((frame.total_size >> 16) & 0xFFFF)
                       << ", lsl #16\n";
                }
                os << "  sub sp, sp, x16\n";
            }
        }

        // Save callee-saved registers in pairs where possible.
        int offset = frame.total_size;
        for (std::size_t i = 0; i < frame.saved_regs.size(); i += 2) {
            offset -= 16;
            if (i + 1 < frame.saved_regs.size()) {
                os << "  stp " << RegisterName(frame.saved_regs[i]) << ", "
                   << RegisterName(frame.saved_regs[i + 1]) << ", [sp, #" << offset << "]\n";
            } else {
                os << "  str " << RegisterName(frame.saved_regs[i]) << ", [sp, #" << offset
                   << "]\n";
            }
        }
    }

    void EmitEpilogue(std::ostream& os, const StackFrame& frame) const {
        int offset = frame.total_size;
        for (std::size_t i = 0; i < frame.saved_regs.size(); i += 2) {
            offset -= 16;
            if (i + 1 < frame.saved_regs.size()) {
                os << "  ldp " << RegisterName(frame.saved_regs[i]) << ", "
                   << RegisterName(frame.saved_regs[i + 1]) << ", [sp, #" << offset << "]\n";
            } else {
                os << "  ldr " << RegisterName(frame.saved_regs[i]) << ", [sp, #" << offset
                   << "]\n";
            }
        }
        os << "  mov sp, fp\n";
        os << "  ldp fp, lr, [sp], #16\n";
        os << "  ret\n";
    }

    void EmitCallSetup(std::ostream& os, const MachineInstr& call_instr,
                       const AllocationResult& alloc) const {
        const auto& int_regs   = cc.IntegerArgRegs();
        const auto& float_regs = cc.FloatArgRegs();

        const int num_args  = static_cast<int>(call_instr.operands.size()) - 1;
        int       int_idx   = 0;
        int       float_idx = 0;
        int       stack_off = 0;

        for (int i = 0; i < num_args; ++i) {
            const auto&       arg = call_instr.operands[i];
            const std::string s   = RenderOperand(arg, alloc);
            const bool        is_mem = !s.empty() && s.front() == '[';

            if (arg.is_float) {
                if (float_idx < static_cast<int>(float_regs.size())) {
                    if (arg.kind == Operand::Kind::kImm || is_mem) {
                        os << "  ldr " << RegisterName(float_regs[float_idx]) << ", " << s
                           << "\n";
                    } else {
                        os << "  fmov " << RegisterName(float_regs[float_idx]) << ", " << s
                           << "\n";
                    }
                    ++float_idx;
                } else {
                    os << "  str " << s << ", [sp, #" << stack_off << "]\n";
                    stack_off += 8;
                }
            } else {
                if (int_idx < static_cast<int>(int_regs.size())) {
                    if (is_mem) {
                        os << "  ldr " << RegisterName(int_regs[int_idx]) << ", " << s << "\n";
                    } else {
                        os << "  mov " << RegisterName(int_regs[int_idx]) << ", " << s << "\n";
                    }
                    ++int_idx;
                } else {
                    os << "  str " << s << ", [sp, #" << stack_off << "]\n";
                    stack_off += 8;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
//  Public symbols (kept for ABI surface stability).
// ---------------------------------------------------------------------------

CallingConvention& GetAAPCS64CallingConvention() {
    static CallingConvention instance;
    return instance;
}

std::vector<Register> GetAvailableRegisters() {
    return CommonCC{}.AvailableRegisters();
}

}  // namespace polyglot::backends::arm64