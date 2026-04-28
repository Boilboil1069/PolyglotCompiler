/**
 * @file     calling_convention.cpp
 * @brief    x86-64 ABI glue: instantiates the common
 *           `CallingConvention<X86TargetTraits>` facade and provides the
 *           target-specific assembly emitters (prologue, epilogue, call
 *           setup) that cannot live in the architecture-neutral header.
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "backends/common/include/abi/abi.h"
#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"

namespace polyglot::backends::x86_64 {

using CommonCC    = ::polyglot::backends::common::abi::CallingConvention<X86TargetTraits>;
using StackFrame  = ::polyglot::backends::common::abi::StackFrame<X86TargetTraits>;

namespace {

// Helper: render a vreg / immediate / label operand into an x86-64 source
// expression usable on the right-hand side of a `mov`.
std::string RenderOperand(const Operand& arg, const AllocationResult& alloc) {
    switch (arg.kind) {
    case Operand::Kind::kImm:
        return std::to_string(arg.imm);
    case Operand::Kind::kVReg: {
        auto phys_it = alloc.vreg_to_phys.find(arg.vreg);
        if (phys_it != alloc.vreg_to_phys.end()) {
            return RegisterName(phys_it->second);
        }
        auto slot_it = alloc.vreg_to_slot.find(arg.vreg);
        if (slot_it != alloc.vreg_to_slot.end()) {
            const int offset = (slot_it->second + 1) * 8;
            return "[rbp - " + std::to_string(offset) + "]";
        }
        return "rax";
    }
    default:
        return "rax";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Per-target wrapper around the common ABI facade.  Holds the target-
//  specific assembly emitters; everything else is delegated to `CommonCC`.
// ---------------------------------------------------------------------------
struct CallingConvention {
    CommonCC cc;

    StackFrame ComputeStackFrame(const MachineFunction& fn,
                                 const AllocationResult& alloc) const {
        return ::polyglot::backends::common::abi::ComputeStackFrame<X86TargetTraits, Opcode>(
            cc, fn, alloc, Opcode::kCall);
    }

    void EmitPrologue(std::ostream& os, const StackFrame& frame) const {
        os << "  push rbp\n";
        os << "  mov rbp, rsp\n";
        if (frame.total_size > 0) {
            os << "  sub rsp, " << frame.total_size << "\n";
        }
        // Save callee-saved registers (slot offset matches the legacy emitter).
        int offset = frame.total_size;
        for (Register reg : frame.saved_regs) {
            offset -= 8;
            os << "  mov [rbp - " << (frame.total_size - offset) << "], "
               << RegisterName(reg) << "\n";
        }
    }

    void EmitEpilogue(std::ostream& os, const StackFrame& frame) const {
        int offset = frame.total_size;
        for (Register reg : frame.saved_regs) {
            offset -= 8;
            os << "  mov " << RegisterName(reg) << ", [rbp - "
               << (frame.total_size - offset) << "]\n";
        }
        os << "  mov rsp, rbp\n";
        os << "  pop rbp\n";
        os << "  ret\n";
    }

    void EmitCallSetup(std::ostream& os, const MachineInstr& call_instr,
                       const AllocationResult& alloc) const {
        const auto& int_regs   = cc.IntegerArgRegs();
        const auto& float_regs = cc.FloatArgRegs();

        const int num_args   = static_cast<int>(call_instr.operands.size()) - 1;  // last is callee
        int       int_idx    = 0;
        int       float_idx  = 0;
        int       stack_off  = 0;

        for (int i = 0; i < num_args; ++i) {
            const auto& arg     = call_instr.operands[i];
            const std::string s = RenderOperand(arg, alloc);
            if (arg.is_float) {
                if (float_idx < static_cast<int>(float_regs.size())) {
                    os << "  movsd " << RegisterName(float_regs[float_idx]) << ", " << s << "\n";
                    ++float_idx;
                } else {
                    os << "  movsd [rsp + " << stack_off << "], " << s << "\n";
                    stack_off += 8;
                }
            } else {
                if (int_idx < static_cast<int>(int_regs.size())) {
                    os << "  mov " << RegisterName(int_regs[int_idx]) << ", " << s << "\n";
                    ++int_idx;
                } else {
                    os << "  mov [rsp + " << stack_off << "], " << s << "\n";
                    stack_off += 8;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
//  Public symbols (kept for ABI surface stability even though no current
//  caller outside this translation unit references them).
// ---------------------------------------------------------------------------

CallingConvention& GetSysVCallingConvention() {
    static CallingConvention instance;
    return instance;
}

std::vector<Register> GetAvailableRegisters() {
    return CommonCC{}.AvailableRegisters();
}

}  // namespace polyglot::backends::x86_64