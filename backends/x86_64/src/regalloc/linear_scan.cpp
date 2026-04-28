/**
 * @file     linear_scan.cpp
 * @brief    x86-64 linear-scan register allocator entry point. The
 *           algorithmic body lives in
 *           backends/common/include/machine_ir/machine_ir.h as a function
 *           template parameterised on the target traits; this translation
 *           unit provides the one-and-only definition of the x86-64 free
 *           functions ComputeLiveIntervals and LinearScanAllocate so the
 *           linker can resolve calls from isel.cpp / optimizations.cpp /
 *           x86_target_backend.cpp.
 *
 * @ingroup  Backend / x86-64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {

std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction& fn) {
    return common::machine_ir::ComputeLiveIntervals<X86TargetTraits, Opcode>(fn);
}

AllocationResult LinearScanAllocate(const MachineFunction&        fn,
                                    const std::vector<Register>&  available) {
    return common::machine_ir::LinearScanAllocate<X86TargetTraits, Opcode>(fn, available);
}

}  // namespace polyglot::backends::x86_64