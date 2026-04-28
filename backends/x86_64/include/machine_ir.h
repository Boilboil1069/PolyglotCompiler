/**
 * @file     machine_ir.h
 * @brief    x86-64 MachineIR public surface: the per-target Opcode enum, the
 *           X86TargetTraits hook used to instantiate the common MachineIR
 *           templates, the x86-64 cost model, and declarations of the four
 *           target-namespace free functions whose definitions live in
 *           `backends/x86_64/src/regalloc/*.cpp` and `asm_printer/scheduler.cpp`.
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <vector>

#include "middle/include/ir/cfg.h"

#include "backends/common/include/abi/abi.h"
#include "backends/common/include/machine_ir/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"

namespace polyglot::backends::x86_64 {

/// @brief x86-64 opcode enumeration.
enum class Opcode {
    kMov, kAdd, kSub, kMul, kDiv, kSDiv, kUDiv, kRem, kSRem, kURem,
    kAnd, kOr, kXor, kShl, kLShr, kAShr, kCmp,
    // Floating-point operations
    kMovsd, kMovss, kAddsd, kSubsd, kMulsd, kDivsd, kCmpsd,
    // SIMD operations
    kAddps, kSubps, kMulps, kDivps, kShufps, kMovaps, kMovups,
    // General
    kLoad, kStore, kLea, kCall, kRet, kJmp, kJcc
};

/// @brief Traits hook required by the common MachineIR templates and the
///        common ABI calling-convention facade.
///
/// In addition to the MachineIR contract (`Register` + `kDefaultRegister`),
/// this struct exposes the four ABI register tables and the three sizing
/// constants consumed by `common::abi::CallingConvention`.
struct X86TargetTraits {
    using Register                              = ::polyglot::backends::x86_64::Register;
    static constexpr Register kDefaultRegister  = Register::kRax;

    // ---- ABI sizing constants (System V AMD64) -----------------------------
    static constexpr int kStackAlignment = 16;
    static constexpr int kPointerSize    = 8;
    static constexpr int kRedZoneSize    = 128;

    // ---- ABI register tables (System V AMD64) ------------------------------
    inline static const std::vector<Register> kIntegerArgRegs = {
        Register::kRdi, Register::kRsi, Register::kRdx,
        Register::kRcx, Register::kR8,  Register::kR9
    };
    inline static const std::vector<Register> kFloatArgRegs = {
        Register::kXmm0, Register::kXmm1, Register::kXmm2, Register::kXmm3,
        Register::kXmm4, Register::kXmm5, Register::kXmm6, Register::kXmm7
    };
    inline static const std::vector<Register> kCalleeSavedRegs = {
        Register::kRbx, Register::kRbp, Register::kR12,
        Register::kR13, Register::kR14, Register::kR15
    };
    inline static const std::vector<Register> kVolatileRegs = {
        Register::kRax, Register::kRdi, Register::kRsi,
        Register::kRdx, Register::kRcx, Register::kR8,
        Register::kR9,  Register::kR10, Register::kR11
    };
};

// ---- Aliases over the common templates -------------------------------------

using Operand            = common::machine_ir::Operand<X86TargetTraits>;
using MachineInstr       = common::machine_ir::MachineInstr<X86TargetTraits, Opcode>;
using MachineBasicBlock  = common::machine_ir::MachineBasicBlock<X86TargetTraits, Opcode>;
using MachineFunction    = common::machine_ir::MachineFunction<X86TargetTraits, Opcode>;
using LiveInterval       = common::machine_ir::LiveInterval<X86TargetTraits>;
using AllocationResult   = common::machine_ir::AllocationResult<X86TargetTraits>;
using common::machine_ir::RegAllocStrategy;

/// @brief x86-64 cost model (target-specific Cost / Latency tables).
struct CostModel {
    int Cost(Opcode op) const;
    int Latency(Opcode op) const;
};

// ---- Free-function entry points (definitions in src/regalloc, asm_printer) -

std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction& fn);

AllocationResult LinearScanAllocate(const MachineFunction&        fn,
                                    const std::vector<Register>&  available);

AllocationResult GraphColoringAllocate(const MachineFunction&        fn,
                                       const std::vector<Register>&  available);

void ScheduleFunction(MachineFunction& fn);

// ---- Target-specific entry point implemented in isel/isel.cpp --------------

MachineFunction SelectInstructions(const ir::Function& fn, const CostModel& cost_model);

}  // namespace polyglot::backends::x86_64
