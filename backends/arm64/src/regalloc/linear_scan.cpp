/**
 * @file     linear_scan.cpp
 * @brief    AArch64 linear-scan register allocator entry point. Provides the
 *           one-and-only definitions of ComputeLiveIntervals and
 *           LinearScanAllocate for the arm64 target namespace; the algorithm
 *           bodies are the common templates in
 *           backends/common/include/machine_ir/machine_ir.h.
 *
 * @ingroup  Backend / ARM64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/arm64/include/machine_ir.h"

namespace polyglot::backends::arm64 {

std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction& fn) {
    return common::machine_ir::ComputeLiveIntervals<Arm64TargetTraits, Opcode>(fn);
}

AllocationResult LinearScanAllocate(const MachineFunction&        fn,
                                    const std::vector<Register>&  available) {
    return common::machine_ir::LinearScanAllocate<Arm64TargetTraits, Opcode>(fn, available);
}

}  // namespace polyglot::backends::arm64