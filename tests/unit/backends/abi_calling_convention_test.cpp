/**
 * @file     abi_calling_convention_test.cpp
 * @brief    Unit tests for `common::abi::CallingConvention` and the
 *           target-independent `ComputeStackFrame` algorithm.
 *
 * The tests exercise both X86 and ARM64 traits to guarantee the templated
 * facade behaves identically across instantiations.
 *
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "backends/arm64/include/machine_ir.h"
#include "backends/common/include/abi/abi.h"
#include "backends/x86_64/include/machine_ir.h"

namespace abi = polyglot::backends::common::abi;

using polyglot::backends::arm64::Arm64TargetTraits;
using polyglot::backends::x86_64::X86TargetTraits;

namespace {

template <typename Traits, typename Opc>
typename polyglot::backends::common::machine_ir::MachineInstr<Traits, Opc>
MakeCallWithArgs(Opc opc, int arg_count) {
    using Instr = polyglot::backends::common::machine_ir::MachineInstr<Traits, Opc>;
    using Op    = polyglot::backends::common::machine_ir::Operand<Traits>;
    Instr mi;
    mi.opcode = opc;
    mi.def    = -1;
    for (int i = 0; i < arg_count; ++i) {
        mi.operands.push_back(Op::VReg(100 + i));
    }
    mi.operands.push_back(Op::Label("callee"));  // trailing callee operand
    return mi;
}

}  // namespace

TEST_CASE("CallingConvention exposes the canonical x86_64 SysV register tables",
          "[backends][abi][calling_convention]") {
    abi::CallingConvention<X86TargetTraits> cc;

    REQUIRE(cc.IntegerArgRegs().size()  == 6);
    REQUIRE(cc.FloatArgRegs().size()    == 8);
    REQUIRE(cc.CalleeSavedRegs().size() == 6);
    REQUIRE(cc.VolatileRegs().size()    == 9);
    REQUIRE(cc.StackAlignment()         == 16);
    REQUIRE(cc.PointerSize()            == 8);
    REQUIRE(cc.RedZoneSize()            == 128);

    // AvailableRegisters() == volatile + callee_saved (concatenation order).
    auto available = cc.AvailableRegisters();
    REQUIRE(available.size() == cc.VolatileRegs().size() + cc.CalleeSavedRegs().size());
    for (std::size_t i = 0; i < cc.VolatileRegs().size(); ++i) {
        REQUIRE(available[i] == cc.VolatileRegs()[i]);
    }
    for (std::size_t i = 0; i < cc.CalleeSavedRegs().size(); ++i) {
        REQUIRE(available[cc.VolatileRegs().size() + i] == cc.CalleeSavedRegs()[i]);
    }
}

TEST_CASE("ComputeStackFrame returns an empty frame for a trivial leaf function",
          "[backends][abi][stack_frame]") {
    using Traits = X86TargetTraits;
    using Opc    = polyglot::backends::x86_64::Opcode;
    using Fn     = polyglot::backends::common::machine_ir::MachineFunction<Traits, Opc>;
    using BB     = polyglot::backends::common::machine_ir::MachineBasicBlock<Traits, Opc>;
    using Instr  = polyglot::backends::common::machine_ir::MachineInstr<Traits, Opc>;
    using Alloc  = polyglot::backends::common::machine_ir::AllocationResult<Traits>;

    Fn fn;
    fn.name = "leaf";
    BB bb;
    bb.name = "entry";
    Instr ret;
    ret.opcode     = Opc::kRet;
    ret.def        = -1;
    ret.terminator = true;
    bb.instructions.push_back(ret);
    fn.blocks.push_back(bb);

    Alloc alloc;
    abi::CallingConvention<Traits> cc;
    auto frame = abi::ComputeStackFrame<Traits, Opc>(cc, fn, alloc, Opc::kCall);

    REQUIRE(frame.total_size      == 0);
    REQUIRE(frame.spill_area_size == 0);
    REQUIRE(frame.arg_area_size   == 0);
    REQUIRE(frame.saved_regs.empty());
}

TEST_CASE("ComputeStackFrame aligns the total to the platform stack alignment",
          "[backends][abi][stack_frame]") {
    using Traits = X86TargetTraits;
    using Opc    = polyglot::backends::x86_64::Opcode;
    using Fn     = polyglot::backends::common::machine_ir::MachineFunction<Traits, Opc>;
    using BB     = polyglot::backends::common::machine_ir::MachineBasicBlock<Traits, Opc>;
    using Instr  = polyglot::backends::common::machine_ir::MachineInstr<Traits, Opc>;
    using Alloc  = polyglot::backends::common::machine_ir::AllocationResult<Traits>;

    Fn fn;
    fn.name = "uses_callee_saved";
    BB bb;
    bb.name = "entry";
    Instr ret;
    ret.opcode     = Opc::kRet;
    ret.def        = -1;
    ret.terminator = true;
    bb.instructions.push_back(ret);
    fn.blocks.push_back(bb);

    // Use one callee-saved register (Rbx) for an arbitrary vreg.
    Alloc alloc;
    alloc.vreg_to_phys[42]  = polyglot::backends::x86_64::Register::kRbx;
    alloc.stack_slots       = 3;  // 3 spill slots * 8 bytes = 24 bytes.

    abi::CallingConvention<Traits> cc;
    auto frame = abi::ComputeStackFrame<Traits, Opc>(cc, fn, alloc, Opc::kCall);

    REQUIRE(frame.spill_area_size == 24);
    REQUIRE(frame.saved_regs.size() == 1);
    REQUIRE(frame.saved_regs.front() == polyglot::backends::x86_64::Register::kRbx);
    // 24 (spill) + 8 (one saved reg) = 32, already 16-aligned.
    REQUIRE(frame.total_size == 32);
    REQUIRE(frame.total_size % cc.StackAlignment() == 0);
}

TEST_CASE("ComputeStackFrame reserves outgoing arg slots when call exceeds reg count",
          "[backends][abi][stack_frame]") {
    using Traits = X86TargetTraits;
    using Opc    = polyglot::backends::x86_64::Opcode;
    using Fn     = polyglot::backends::common::machine_ir::MachineFunction<Traits, Opc>;
    using BB     = polyglot::backends::common::machine_ir::MachineBasicBlock<Traits, Opc>;
    using Instr  = polyglot::backends::common::machine_ir::MachineInstr<Traits, Opc>;
    using Alloc  = polyglot::backends::common::machine_ir::AllocationResult<Traits>;

    Fn fn;
    fn.name = "wide_caller";
    BB bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeCallWithArgs<Traits, Opc>(Opc::kCall, /*args=*/9));
    Instr ret;
    ret.opcode     = Opc::kRet;
    ret.def        = -1;
    ret.terminator = true;
    bb.instructions.push_back(ret);
    fn.blocks.push_back(bb);

    Alloc alloc;
    abi::CallingConvention<Traits> cc;
    auto frame = abi::ComputeStackFrame<Traits, Opc>(cc, fn, alloc, Opc::kCall);

    // 9 integer args, 6 fit in regs; (9 - 6) * 8 = 24 bytes outgoing arg area.
    REQUIRE(frame.arg_area_size == 24);
    // total = align16(0 + 0 + 24) == 32.
    REQUIRE(frame.total_size == 32);
}

TEST_CASE("CallingConvention works for AAPCS64 traits with the same algorithm",
          "[backends][abi][calling_convention][arm64]") {
    abi::CallingConvention<Arm64TargetTraits> cc;

    REQUIRE(cc.IntegerArgRegs().size()  == 8);
    REQUIRE(cc.FloatArgRegs().size()    == 8);
    REQUIRE(cc.CalleeSavedRegs().size() == 12);  // X19..X28, FP, LR
    REQUIRE(cc.VolatileRegs().size()    == 18);  // X0..X17
    REQUIRE(cc.StackAlignment()         == 16);
    REQUIRE(cc.PointerSize()            == 8);
    REQUIRE(cc.RedZoneSize()            == 0);

    auto available = cc.AvailableRegisters();
    REQUIRE(available.size() == 18 + 12);
}
