/**
 * @file     graph_coloring.cpp
 * @brief    AArch64 graph-coloring register allocator entry point. Provides
 *           the one-and-only definition of GraphColoringAllocate for the
 *           arm64 target namespace; the algorithm body is the common
 *           template in backends/common/include/machine_ir/machine_ir.h.
 *
 * @ingroup  Backend / ARM64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/arm64/include/machine_ir.h"

namespace polyglot::backends::arm64 {

AllocationResult GraphColoringAllocate(const MachineFunction&        fn,
                                       const std::vector<Register>&  available) {
    return common::machine_ir::GraphColoringAllocate<Arm64TargetTraits, Opcode>(fn, available);
}

}  // namespace polyglot::backends::arm64