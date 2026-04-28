/**
 * @file     calling_convention.h
 * @brief    Target-independent ABI model: stack-frame layout, calling-
 *           convention register tables and the stack-frame computation
 *           algorithm shared by every code-generation backend.
 *
 * Per-target headers extend their `TargetTraits` struct (the same one used by
 * the common MachineIR templates) with four `inline static const
 * std::vector<Register>` register tables and three `static constexpr int`
 * sizing constants:
 *
 *   * `kIntegerArgRegs`    — argument registers used for integer / pointer
 *                            parameters, in passing order.
 *   * `kFloatArgRegs`      — argument registers used for floating-point
 *                            parameters, in passing order.
 *   * `kCalleeSavedRegs`   — registers the callee must preserve.
 *   * `kVolatileRegs`      — registers that may be clobbered across a call.
 *   * `kStackAlignment`    — required stack-pointer alignment in bytes.
 *   * `kPointerSize`       — pointer / general-purpose register width in
 *                            bytes (also the slot size of every spill).
 *   * `kRedZoneSize`       — bytes of unallocated stack guaranteed by the
 *                            ABI (0 when the platform has no red zone).
 *
 * `CallingConvention<TargetTraits>` exposes accessors over those traits and
 * the target-independent `ComputeStackFrame` algorithm derives a `StackFrame`
 * for a given function and register-allocation result.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "backends/common/include/machine_ir/machine_ir.h"

namespace polyglot::backends::common::abi {

/// @brief Stack-frame layout for a single function, derived from a register
///        allocation result and the function's outgoing-call shape.
///
/// All sizes are in bytes. `total_size` is the value the prologue subtracts
/// from the stack pointer (already aligned to `TargetTraits::kStackAlignment`).
template <typename TargetTraits>
struct StackFrame {
    using Register = typename TargetTraits::Register;

    int                   total_size{0};       ///< Aligned bytes to subtract from SP.
    int                   spill_area_size{0};  ///< Bytes reserved for register spills.
    int                   local_area_size{0};  ///< Bytes reserved for `alloca` / locals.
    int                   arg_area_size{0};    ///< Bytes for outgoing call arguments.
    std::vector<Register> saved_regs;          ///< Callee-saved regs actually used.
};

/// @brief Calling-convention facade over a `TargetTraits` instance.
///
/// All accessors are `const` and return references to the static traits
/// tables; the type itself is empty and trivially constructible so that
/// code-generation passes may keep an instance per target backend.
template <typename TargetTraits>
class CallingConvention {
 public:
    using Register = typename TargetTraits::Register;

    constexpr CallingConvention() noexcept = default;

    /// @brief Argument registers used for integer / pointer parameters.
    const std::vector<Register>& IntegerArgRegs() const noexcept {
        return TargetTraits::kIntegerArgRegs;
    }

    /// @brief Argument registers used for floating-point parameters.
    const std::vector<Register>& FloatArgRegs() const noexcept {
        return TargetTraits::kFloatArgRegs;
    }

    /// @brief Registers the callee is required to preserve.
    const std::vector<Register>& CalleeSavedRegs() const noexcept {
        return TargetTraits::kCalleeSavedRegs;
    }

    /// @brief Registers that may be clobbered across a call.
    const std::vector<Register>& VolatileRegs() const noexcept {
        return TargetTraits::kVolatileRegs;
    }

    /// @brief Required stack-pointer alignment in bytes.
    constexpr int StackAlignment() const noexcept {
        return TargetTraits::kStackAlignment;
    }

    /// @brief Pointer / general-purpose register width in bytes; also the
    ///        spill-slot size used by `ComputeStackFrame`.
    constexpr int PointerSize() const noexcept {
        return TargetTraits::kPointerSize;
    }

    /// @brief Red-zone size in bytes (0 when the platform has no red zone).
    constexpr int RedZoneSize() const noexcept {
        return TargetTraits::kRedZoneSize;
    }

    /// @brief Concatenation of `VolatileRegs() + CalleeSavedRegs()`.
    ///
    /// This is the order historically returned by each backend's
    /// `GetAvailableRegisters()` helper and the order the linear-scan and
    /// graph-coloring allocators consume.
    std::vector<Register> AvailableRegisters() const {
        const auto& vols    = VolatileRegs();
        const auto& callees = CalleeSavedRegs();
        std::vector<Register> available;
        available.reserve(vols.size() + callees.size());
        available.insert(available.end(), vols.begin(), vols.end());
        available.insert(available.end(), callees.begin(), callees.end());
        return available;
    }
};

/// @brief Compute the stack-frame layout for `fn` given the register
///        allocation `alloc` and the per-target opcode value that identifies
///        a call instruction (`call_opcode`).
///
/// The algorithm is the byte-equivalent uplift of the legacy x86-64 / AArch64
/// `CallingConvention::ComputeStackFrame` routine:
///
///   1. spill area  = `alloc.stack_slots * cc.PointerSize()`.
///   2. saved regs  = callee-saved registers actually present in
///                    `alloc.vreg_to_phys` (preserving callee-saved order).
///   3. arg area    = `(max_call_arg_count - integer_arg_reg_count) *
///                    cc.PointerSize()` clamped at zero, where
///                    `max_call_arg_count` is the largest non-callee operand
///                    count of any instruction whose opcode equals
///                    `call_opcode`.
///   4. total size  = align(spill + saved + arg, cc.StackAlignment()).
template <typename TargetTraits, typename OpcodeT>
StackFrame<TargetTraits>
ComputeStackFrame(const CallingConvention<TargetTraits>&                          cc,
                  const machine_ir::MachineFunction<TargetTraits, OpcodeT>&       fn,
                  const machine_ir::AllocationResult<TargetTraits>&               alloc,
                  OpcodeT                                                         call_opcode) {
    using Register = typename TargetTraits::Register;
    StackFrame<TargetTraits> frame;

    const int slot_bytes = cc.PointerSize();
    frame.spill_area_size = alloc.stack_slots * slot_bytes;

    // 2. Determine which callee-saved registers are actually used. Walk the
    //    callee-saved list to keep a deterministic, ABI-defined save order.
    for (Register reg : cc.CalleeSavedRegs()) {
        for (const auto& kv : alloc.vreg_to_phys) {
            if (kv.second == reg) {
                if (std::find(frame.saved_regs.begin(), frame.saved_regs.end(), reg) ==
                    frame.saved_regs.end()) {
                    frame.saved_regs.push_back(reg);
                }
                break;
            }
        }
    }
    const int saved_regs_size = static_cast<int>(frame.saved_regs.size()) * slot_bytes;

    // 3. Maximum outgoing argument count over all call sites. Each call
    //    instruction encodes its callee as the trailing operand, so subtract
    //    one to recover the actual argument count.
    int max_args = 0;
    for (const auto& bb : fn.blocks) {
        for (const auto& mi : bb.instructions) {
            if (mi.opcode == call_opcode) {
                const int arg_count = static_cast<int>(mi.operands.size()) - 1;
                if (arg_count > max_args) {
                    max_args = arg_count;
                }
            }
        }
    }
    const int reg_arg_count = static_cast<int>(cc.IntegerArgRegs().size());
    if (max_args > reg_arg_count) {
        frame.arg_area_size = (max_args - reg_arg_count) * slot_bytes;
    }

    // 4. Align the total to the platform stack alignment.
    const int align = cc.StackAlignment();
    int       total = frame.spill_area_size + saved_regs_size + frame.arg_area_size;
    if (align > 1) {
        const int mask = align - 1;
        total = (total + mask) & ~mask;
    }
    frame.total_size = total;
    return frame;
}

}  // namespace polyglot::backends::common::abi
