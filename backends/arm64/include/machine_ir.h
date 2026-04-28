/**
 * @file     machine_ir.h
 * @brief    AArch64 MachineIR public surface: the per-target Opcode enum, the
 *           Arm64TargetTraits hook used to instantiate the common MachineIR
 *           templates, the AArch64 cost model, and declarations of the four
 *           target-namespace free functions whose definitions live in
 *           `backends/arm64/src/regalloc/*.cpp` and `asm_printer/scheduler.cpp`.
 *
 * @ingroup  Backend / ARM64
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <vector>

#include "middle/include/ir/cfg.h"

#include "backends/arm64/include/arm64_register.h"
#include "backends/common/include/abi/abi.h"
#include "backends/common/include/machine_ir/machine_ir.h"

namespace polyglot::backends::arm64 {

/// @brief AArch64 opcode enumeration.
enum class Opcode {
    kMov, kAdd, kSub, kMul, kDiv, kSDiv, kUDiv, kRem, kSRem, kURem,
    kAnd, kOr, kXor, kShl, kLShr, kAShr, kCmp,
    kLoad, kStore, kLea, kCall, kRet, kJmp, kJcc
};

/// @brief Traits hook required by the common MachineIR templates and the
///        common ABI calling-convention facade.
///
/// In addition to the MachineIR contract (`Register` + `kDefaultRegister`),
/// this struct exposes the four AAPCS64 register tables and the three
/// sizing constants consumed by `common::abi::CallingConvention`.
struct Arm64TargetTraits {
    using Register                              = ::polyglot::backends::arm64::Register;
    static constexpr Register kDefaultRegister  = Register::kX0;

    // ---- ABI sizing constants (AAPCS64) ------------------------------------
    static constexpr int kStackAlignment = 16;
    static constexpr int kPointerSize    = 8;
    static constexpr int kRedZoneSize    = 0;

    // ---- ABI register tables (AAPCS64) -------------------------------------
    inline static const std::vector<Register> kIntegerArgRegs = {
        Register::kX0, Register::kX1, Register::kX2, Register::kX3,
        Register::kX4, Register::kX5, Register::kX6, Register::kX7
    };
    inline static const std::vector<Register> kFloatArgRegs = {
        Register::kV0, Register::kV1, Register::kV2, Register::kV3,
        Register::kV4, Register::kV5, Register::kV6, Register::kV7
    };
    inline static const std::vector<Register> kCalleeSavedRegs = {
        Register::kX19, Register::kX20, Register::kX21, Register::kX22,
        Register::kX23, Register::kX24, Register::kX25, Register::kX26,
        Register::kX27, Register::kX28, Register::kFp,  Register::kLr
    };
    inline static const std::vector<Register> kVolatileRegs = {
        Register::kX0,  Register::kX1,  Register::kX2,  Register::kX3,
        Register::kX4,  Register::kX5,  Register::kX6,  Register::kX7,
        Register::kX8,  Register::kX9,  Register::kX10, Register::kX11,
        Register::kX12, Register::kX13, Register::kX14, Register::kX15,
        Register::kX16, Register::kX17
    };
};

// ---- Aliases over the common templates -------------------------------------

using Operand            = common::machine_ir::Operand<Arm64TargetTraits>;
using MachineInstr       = common::machine_ir::MachineInstr<Arm64TargetTraits, Opcode>;
using MachineBasicBlock  = common::machine_ir::MachineBasicBlock<Arm64TargetTraits, Opcode>;
using MachineFunction    = common::machine_ir::MachineFunction<Arm64TargetTraits, Opcode>;
using LiveInterval       = common::machine_ir::LiveInterval<Arm64TargetTraits>;
using AllocationResult   = common::machine_ir::AllocationResult<Arm64TargetTraits>;
using common::machine_ir::RegAllocStrategy;

/// @brief AArch64 cost model (target-specific Cost / Latency tables).
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

}  // namespace polyglot::backends::arm64
