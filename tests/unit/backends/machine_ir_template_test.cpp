/**
 * @file     machine_ir_template_test.cpp
 * @brief    Cross-target tests for the templated MachineIR algorithms.
 *
 * Validates that the common-template instantiations for both x86_64 and
 * arm64 produce identical-shape allocation results on a canonical small
 * function — guarding against drift that would have been impossible to
 * detect when each backend owned its own algorithm copy.
 *
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <catch2/catch_test_macros.hpp>

#include "backends/arm64/include/machine_ir.h"
#include "backends/x86_64/include/machine_ir.h"

namespace {

template <typename Mod>
typename Mod::MachineFunction Build() {
    using F  = typename Mod::MachineFunction;
    using BB = typename Mod::MachineBasicBlock;
    using I  = typename Mod::MachineInstr;
    F  fn;
    fn.name = "f";
    BB bb;
    bb.name = "entry";
    auto make = [](typename Mod::Opcode op, int def, std::vector<int> uses, bool term) {
        I mi;
        mi.opcode     = op;
        mi.def        = def;
        mi.uses       = std::move(uses);
        mi.terminator = term;
        return mi;
    };
    bb.instructions.push_back(make(Mod::Opcode::kMov, 1, {}, false));
    bb.instructions.push_back(make(Mod::Opcode::kMov, 2, {}, false));
    bb.instructions.push_back(make(Mod::Opcode::kAdd, 3, {1, 2}, false));
    bb.instructions.push_back(make(Mod::Opcode::kRet, -1, {3}, true));
    fn.blocks.push_back(std::move(bb));
    return fn;
}

struct X86Mod {
    using namespace_alias  = void;
    using MachineFunction  = polyglot::backends::x86_64::MachineFunction;
    using MachineBasicBlock = polyglot::backends::x86_64::MachineBasicBlock;
    using MachineInstr     = polyglot::backends::x86_64::MachineInstr;
    using Opcode           = polyglot::backends::x86_64::Opcode;
};

struct ArmMod {
    using namespace_alias  = void;
    using MachineFunction  = polyglot::backends::arm64::MachineFunction;
    using MachineBasicBlock = polyglot::backends::arm64::MachineBasicBlock;
    using MachineInstr     = polyglot::backends::arm64::MachineInstr;
    using Opcode           = polyglot::backends::arm64::Opcode;
};

}  // namespace

TEST_CASE("Common ComputeLiveIntervals produces identical-shape output for x86 and arm",
          "[backends][machineir][template]") {
    auto x86_fn = Build<X86Mod>();
    auto arm_fn = Build<ArmMod>();
    auto x86_li = polyglot::backends::x86_64::ComputeLiveIntervals(x86_fn);
    auto arm_li = polyglot::backends::arm64::ComputeLiveIntervals(arm_fn);
    REQUIRE(x86_li.size() == arm_li.size());
    REQUIRE(x86_li.size() == 3);
    for (std::size_t i = 0; i < x86_li.size(); ++i) {
        REQUIRE(x86_li[i].vreg  == arm_li[i].vreg);
        REQUIRE(x86_li[i].start == arm_li[i].start);
        REQUIRE(x86_li[i].end   == arm_li[i].end);
    }
}

TEST_CASE("Common LinearScanAllocate fills every vreg when registers are abundant",
          "[backends][machineir][template]") {
    auto x86_fn = Build<X86Mod>();
    std::vector<polyglot::backends::x86_64::Register> avail = {
        polyglot::backends::x86_64::Register::kRax,
        polyglot::backends::x86_64::Register::kRbx,
        polyglot::backends::x86_64::Register::kRcx,
        polyglot::backends::x86_64::Register::kRdx,
    };
    auto result = polyglot::backends::x86_64::LinearScanAllocate(x86_fn, avail);
    REQUIRE(result.vreg_to_phys.size() == 3);
    REQUIRE(result.vreg_to_slot.empty());
    REQUIRE(result.stack_slots == 0);
}

TEST_CASE("Common LinearScanAllocate spills when registers run out",
          "[backends][machineir][template]") {
    auto x86_fn = Build<X86Mod>();
    std::vector<polyglot::backends::x86_64::Register> avail = {
        polyglot::backends::x86_64::Register::kRax,
    };
    auto result = polyglot::backends::x86_64::LinearScanAllocate(x86_fn, avail);
    REQUIRE(result.stack_slots > 0);
    REQUIRE(result.vreg_to_phys.size() + result.vreg_to_slot.size() == 3);
}

TEST_CASE("Common GraphColoringAllocate spills entire function when no register is available",
          "[backends][machineir][template]") {
    auto                                                arm_fn = Build<ArmMod>();
    std::vector<polyglot::backends::arm64::Register>    avail;
    auto result = polyglot::backends::arm64::GraphColoringAllocate(arm_fn, avail);
    REQUIRE(result.vreg_to_phys.empty());
    REQUIRE(result.vreg_to_slot.size() == 3);
    REQUIRE(result.stack_slots == 3);
}

TEST_CASE("Common ScheduleFunction keeps the terminator at the end of every block",
          "[backends][machineir][template]") {
    auto x86_fn = Build<X86Mod>();
    polyglot::backends::x86_64::ScheduleFunction(x86_fn);
    REQUIRE(x86_fn.blocks.size() == 1);
    REQUIRE(x86_fn.blocks[0].instructions.back().terminator);

    auto arm_fn = Build<ArmMod>();
    polyglot::backends::arm64::ScheduleFunction(arm_fn);
    REQUIRE(arm_fn.blocks.size() == 1);
    REQUIRE(arm_fn.blocks[0].instructions.back().terminator);
}
