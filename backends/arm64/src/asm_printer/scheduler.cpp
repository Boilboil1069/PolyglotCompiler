/**
 * @file     scheduler.cpp
 * @brief    AArch64 list-scheduler entry point. Provides the one-and-only
 *           definition of ScheduleFunction for the arm64 target namespace;
 *           the algorithm body is the common template in
 *           backends/common/include/machine_ir/machine_ir.h.
 *
 * @ingroup  Backend / ARM64 / Asm Printer
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/arm64/include/machine_ir.h"

namespace polyglot::backends::arm64 {

void ScheduleFunction(MachineFunction& fn) {
    common::machine_ir::ScheduleFunction<Arm64TargetTraits, Opcode>(fn);
}

}  // namespace polyglot::backends::arm64