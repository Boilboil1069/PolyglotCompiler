/**
 * @file     graph_coloring.cpp
 * @brief    x86-64 graph-coloring register allocator entry point. Provides
 *           the one-and-only definition of GraphColoringAllocate for the
 *           x86-64 target namespace; the algorithm body is the common
 *           template in backends/common/include/machine_ir/machine_ir.h.
 *
 * @ingroup  Backend / x86-64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {

AllocationResult GraphColoringAllocate(const MachineFunction&        fn,
                                       const std::vector<Register>&  available) {
    return common::machine_ir::GraphColoringAllocate<X86TargetTraits, Opcode>(fn, available);
}

}  // namespace polyglot::backends::x86_64