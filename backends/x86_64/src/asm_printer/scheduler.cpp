/**
 * @file     scheduler.cpp
 * @brief    x86-64 list-scheduler entry point. Provides the one-and-only
 *           definition of ScheduleFunction for the x86-64 target namespace;
 *           the algorithm body is the common template in
 *           backends/common/include/machine_ir/machine_ir.h.
 *
 * @ingroup  Backend / x86-64 / Asm Printer
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {

void ScheduleFunction(MachineFunction& fn) {
    common::machine_ir::ScheduleFunction<X86TargetTraits, Opcode>(fn);
}

}  // namespace polyglot::backends::x86_64