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
#include <algorithm>

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

TEST_CASE("Function parameters interfere from the ABI entry point",
          "[backends][machineir][template][abi]") {
    using namespace polyglot::backends::x86_64;
    MachineFunction fn;
    fn.name = "two_args";
    fn.param_vregs = {1, 2};

    MachineBasicBlock bb;
    bb.name = "entry";
    MachineInstr first;
    first.opcode = Opcode::kMov;
    first.def = 3;
    first.uses = {1};
    bb.instructions.push_back(first);
    MachineInstr second;
    second.opcode = Opcode::kAdd;
    second.def = 4;
    second.uses = {3, 2};
    bb.instructions.push_back(second);
    fn.blocks.push_back(std::move(bb));

    const auto intervals = ComputeLiveIntervals(fn);
    auto find_interval = [&](int vreg) -> const LiveInterval & {
        const auto it = std::find_if(intervals.begin(), intervals.end(),
                                     [=](const LiveInterval &li) { return li.vreg == vreg; });
        REQUIRE(it != intervals.end());
        return *it;
    };
    REQUIRE(find_interval(1).start == 0);
    REQUIRE(find_interval(2).start == 0);

    const std::vector<Register> available = {
        Register::kRax, Register::kRbx, Register::kRcx, Register::kRdx};
    const auto allocation = GraphColoringAllocate(fn, available);
    REQUIRE(allocation.vreg_to_phys.at(1) != allocation.vreg_to_phys.at(2));
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

namespace {

polyglot::backends::x86_64::MachineInstr X86Instruction(
    polyglot::backends::x86_64::Opcode opcode,
    int def = -1,
    std::vector<int> uses = {},
    std::vector<polyglot::backends::x86_64::Operand> operands = {},
    bool terminator = false) {
    polyglot::backends::x86_64::MachineInstr instruction;
    instruction.opcode = opcode;
    instruction.def = def;
    instruction.uses = std::move(uses);
    instruction.operands = std::move(operands);
    instruction.terminator = terminator;
    return instruction;
}

polyglot::backends::x86_64::MachineFunction BuildBackedgeFunction(bool from_load) {
    using namespace polyglot::backends::x86_64;

    MachineFunction fn;
    fn.name = from_load ? "load_backedge" : "argument_backedge";
    if (!from_load) {
        fn.param_vregs = {1};
    }

    MachineBasicBlock entry;
    entry.name = "entry";
    if (from_load) {
        entry.instructions.push_back(
            X86Instruction(Opcode::kLoad, 1, {}, {Operand::MemVReg(99)}));
    }
    entry.instructions.push_back(
        X86Instruction(Opcode::kJmp, -1, {}, {Operand::Label("header")}, true));

    MachineBasicBlock header;
    header.name = "header";
    header.instructions.push_back(
        X86Instruction(Opcode::kCmp, -1, {1}, {Operand::VReg(1), Operand::Imm(0)}));
    header.instructions.push_back(X86Instruction(
        Opcode::kJcc, -1, {},
        {Operand::Label("exit"), Operand::Label("body")}, true));

    // Keep exit before body in layout. The body -> header edge is backwards,
    // which is exactly where a lexical-only interval incorrectly ends before
    // the body's definition of vreg 2.
    MachineBasicBlock exit;
    exit.name = "exit";
    exit.instructions.push_back(
        X86Instruction(Opcode::kRet, -1, {1}, {Operand::VReg(1)}, true));

    MachineBasicBlock body;
    body.name = "body";
    body.instructions.push_back(
        X86Instruction(Opcode::kMov, 2, {}, {Operand::Imm(42)}));
    body.instructions.push_back(
        X86Instruction(Opcode::kJmp, -1, {}, {Operand::Label("header")}, true));

    fn.blocks = {std::move(entry), std::move(header), std::move(exit), std::move(body)};
    return fn;
}

polyglot::backends::x86_64::MachineFunction BuildOutOfOrderDiamond() {
    using namespace polyglot::backends::x86_64;

    MachineFunction fn;
    fn.name = "out_of_order_diamond";
    fn.param_vregs = {1};

    MachineBasicBlock entry;
    entry.name = "entry";
    entry.instructions.push_back(X86Instruction(
        Opcode::kJcc, -1, {},
        {Operand::Label("true_arm"), Operand::Label("false_arm")}, true));

    // This is an acyclic diamond even though the join is stored before its
    // predecessors. A lexical interval ends vreg 1 here and then incorrectly
    // reuses its register in both arms before they jump back to the join.
    MachineBasicBlock join;
    join.name = "join";
    join.instructions.push_back(
        X86Instruction(Opcode::kRet, -1, {1}, {Operand::VReg(1)}, true));

    MachineBasicBlock true_arm;
    true_arm.name = "true_arm";
    true_arm.instructions.push_back(
        X86Instruction(Opcode::kMov, 2, {}, {Operand::Imm(2)}));
    true_arm.instructions.push_back(
        X86Instruction(Opcode::kJmp, -1, {}, {Operand::Label("join")}, true));

    MachineBasicBlock false_arm;
    false_arm.name = "false_arm";
    false_arm.instructions.push_back(
        X86Instruction(Opcode::kMov, 3, {}, {Operand::Imm(3)}));
    false_arm.instructions.push_back(
        X86Instruction(Opcode::kJmp, -1, {}, {Operand::Label("join")}, true));

    fn.blocks = {
        std::move(entry), std::move(join), std::move(true_arm), std::move(false_arm)};
    return fn;
}

void RequireBackedgeValueDoesNotShareRegister(bool from_load) {
    using namespace polyglot::backends::x86_64;

    const auto fn = BuildBackedgeFunction(from_load);
    const auto intervals = ComputeLiveIntervals(fn);
    const auto find_interval = [&](int vreg) -> const LiveInterval& {
        const auto found = std::find_if(intervals.begin(), intervals.end(),
                                        [=](const LiveInterval& interval) {
                                            return interval.vreg == vreg;
                                        });
        REQUIRE(found != intervals.end());
        return *found;
    };

    const auto& carried = find_interval(1);
    const auto& body_temporary = find_interval(2);
    REQUIRE(carried.end > body_temporary.start);

    const std::vector<Register> available = {Register::kRax, Register::kRbx};
    const auto linear = LinearScanAllocate(fn, available);
    REQUIRE(linear.vreg_to_phys.count(1) == 1);
    REQUIRE(linear.vreg_to_phys.count(2) == 1);
    REQUIRE(linear.vreg_to_phys.at(1) != linear.vreg_to_phys.at(2));

    const auto coloring = GraphColoringAllocate(fn, available);
    REQUIRE(coloring.vreg_to_phys.count(1) == 1);
    REQUIRE(coloring.vreg_to_phys.count(2) == 1);
    REQUIRE(coloring.vreg_to_phys.at(1) != coloring.vreg_to_phys.at(2));
}

}  // namespace

TEST_CASE("X86 CFG liveness preserves parameters across a conditional loop backedge",
          "[backends][machineir][x86][regalloc][cfg]") {
    RequireBackedgeValueDoesNotShareRegister(false);
}

TEST_CASE("X86 CFG liveness preserves load results across a conditional loop backedge",
          "[backends][machineir][x86][regalloc][cfg]") {
    RequireBackedgeValueDoesNotShareRegister(true);
}

TEST_CASE("X86 CFG liveness is independent of diamond block layout order",
          "[backends][machineir][x86][regalloc][cfg]") {
    using namespace polyglot::backends::x86_64;

    const auto fn = BuildOutOfOrderDiamond();
    const std::vector<Register> available = {Register::kRax, Register::kRbx};

    const auto linear = LinearScanAllocate(fn, available);
    REQUIRE(linear.vreg_to_phys.at(1) != linear.vreg_to_phys.at(2));
    REQUIRE(linear.vreg_to_phys.at(1) != linear.vreg_to_phys.at(3));

    const auto coloring = GraphColoringAllocate(fn, available);
    REQUIRE(coloring.vreg_to_phys.at(1) != coloring.vreg_to_phys.at(2));
    REQUIRE(coloring.vreg_to_phys.at(1) != coloring.vreg_to_phys.at(3));
}
