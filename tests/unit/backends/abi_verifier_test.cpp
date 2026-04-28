/**
 * @file     abi_verifier_test.cpp
 * @brief    Unit tests for the ABI-aware extension of the MachineIR
 *           verifier (`AbiContract`-driven rules d / e).
 *
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <catch2/catch_test_macros.hpp>

#include "backends/arm64/include/machine_ir.h"
#include "backends/common/include/abi/abi.h"
#include "backends/common/include/machine_ir/verifier.h"
#include "backends/x86_64/include/machine_ir.h"

using polyglot::backends::common::machine_ir::AbiContract;
using polyglot::backends::common::machine_ir::MachineIRVerifier;
using polyglot::backends::common::machine_ir::VerifierDiagnostic;

namespace x86 = polyglot::backends::x86_64;
namespace arm = polyglot::backends::arm64;
namespace abi = polyglot::backends::common::abi;

namespace {

x86::MachineInstr MakeX86Instr(x86::Opcode op, int def, std::initializer_list<int> uses,
                               bool term) {
    x86::MachineInstr mi;
    mi.opcode     = op;
    mi.def        = def;
    mi.uses       = std::vector<int>(uses);
    mi.terminator = term;
    return mi;
}

x86::MachineFunction LegalX86Function() {
    x86::MachineFunction fn;
    fn.name = "legal";
    x86::MachineBasicBlock bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeX86Instr(x86::Opcode::kMov, 1, {}, false));
    bb.instructions.push_back(MakeX86Instr(x86::Opcode::kAdd, 2, {1}, false));
    bb.instructions.push_back(MakeX86Instr(x86::Opcode::kRet, -1, {2}, true));
    fn.blocks.push_back(bb);
    return fn;
}

}  // namespace

TEST_CASE("Verifier behaviour is unchanged when no AbiContract is supplied",
          "[backends][abi][verifier]") {
    MachineIRVerifier<x86::X86TargetTraits, x86::Opcode> verifier;

    // Legal function still passes.
    auto ok = verifier.Verify(LegalX86Function());
    REQUIRE(ok.ok());
    REQUIRE(ok.diagnostics.empty());

    // Function missing a terminator still fails with the structural code.
    x86::MachineFunction broken = LegalX86Function();
    broken.blocks.front().instructions.back().terminator = false;
    auto bad = verifier.Verify(broken);
    REQUIRE_FALSE(bad.ok());
    REQUIRE(bad.diagnostics.size() == 1);
    REQUIRE(bad.diagnostics.front().code == VerifierDiagnostic::Code::kStructural);
}

TEST_CASE("Rule (d) flags a call instruction whose operand count exceeds the ABI capacity",
          "[backends][abi][verifier]") {
    abi::CallingConvention<x86::X86TargetTraits> cc;

    AbiContract<x86::X86TargetTraits, x86::Opcode> contract;
    contract.call_opcode       = x86::Opcode::kCall;
    contract.max_call_operands = cc.IntegerArgRegs().size() + cc.FloatArgRegs().size() + 16 + 1;
    contract.volatile_regs     = cc.VolatileRegs();

    x86::MachineFunction fn;
    fn.name = "wide_call";
    x86::MachineBasicBlock bb;
    bb.name = "entry";

    x86::MachineInstr call;
    call.opcode = x86::Opcode::kCall;
    call.def    = -1;
    for (int i = 0; i < 100; ++i) {
        call.operands.push_back(x86::Operand::VReg(200 + i));
    }
    call.operands.push_back(x86::Operand::Label("callee"));
    bb.instructions.push_back(call);
    bb.instructions.push_back(MakeX86Instr(x86::Opcode::kRet, -1, {}, true));
    fn.blocks.push_back(bb);

    MachineIRVerifier<x86::X86TargetTraits, x86::Opcode> verifier;
    auto result = verifier.Verify(fn, &contract);
    REQUIRE_FALSE(result.ok());
    bool found_arity = false;
    for (const auto& d : result.diagnostics) {
        if (d.code == VerifierDiagnostic::Code::kAbiCallArityExceeded) {
            found_arity = true;
            break;
        }
    }
    REQUIRE(found_arity);
}

TEST_CASE("Rule (d) accepts a normal four-argument AAPCS64 call",
          "[backends][abi][verifier][arm64]") {
    abi::CallingConvention<arm::Arm64TargetTraits> cc;

    AbiContract<arm::Arm64TargetTraits, arm::Opcode> contract;
    contract.call_opcode       = arm::Opcode::kCall;
    contract.max_call_operands = cc.IntegerArgRegs().size() + cc.FloatArgRegs().size() + 16 + 1;
    contract.volatile_regs     = cc.VolatileRegs();

    arm::MachineFunction fn;
    fn.name = "small_call";
    arm::MachineBasicBlock bb;
    bb.name = "entry";

    arm::MachineInstr call;
    call.opcode = arm::Opcode::kCall;
    call.def    = -1;
    for (int i = 0; i < 4; ++i) {
        call.operands.push_back(arm::Operand::VReg(10 + i));
    }
    call.operands.push_back(arm::Operand::Label("callee"));
    bb.instructions.push_back(call);

    arm::MachineInstr ret;
    ret.opcode     = arm::Opcode::kRet;
    ret.def        = -1;
    ret.terminator = true;
    bb.instructions.push_back(ret);
    fn.blocks.push_back(bb);

    MachineIRVerifier<arm::Arm64TargetTraits, arm::Opcode> verifier;
    auto result = verifier.Verify(fn, &contract);
    REQUIRE(result.ok());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Rule (e) flags a volatile physical register read in the call's continuation",
          "[backends][abi][verifier]") {
    abi::CallingConvention<x86::X86TargetTraits> cc;

    AbiContract<x86::X86TargetTraits, x86::Opcode> contract;
    contract.call_opcode       = x86::Opcode::kCall;
    contract.max_call_operands = cc.IntegerArgRegs().size() + cc.FloatArgRegs().size() + 16 + 1;
    contract.volatile_regs     = cc.VolatileRegs();

    x86::MachineFunction fn;
    fn.name = "leaky";
    x86::MachineBasicBlock bb;
    bb.name = "entry";

    x86::MachineInstr call;
    call.opcode = x86::Opcode::kCall;
    call.def    = -1;
    call.operands.push_back(x86::Operand::Label("callee"));
    bb.instructions.push_back(call);

    // Read of a physical volatile register (Rcx) without an intervening def.
    x86::MachineInstr leak;
    leak.opcode = x86::Opcode::kMov;
    leak.def    = -1;
    leak.operands.push_back(x86::Operand::Phys(x86::Register::kRcx));
    bb.instructions.push_back(leak);

    x86::MachineInstr ret;
    ret.opcode     = x86::Opcode::kRet;
    ret.def        = -1;
    ret.terminator = true;
    bb.instructions.push_back(ret);
    fn.blocks.push_back(bb);

    MachineIRVerifier<x86::X86TargetTraits, x86::Opcode> verifier;
    auto result = verifier.Verify(fn, &contract);
    REQUIRE_FALSE(result.ok());
    bool found_leak = false;
    for (const auto& d : result.diagnostics) {
        if (d.code == VerifierDiagnostic::Code::kAbiVolatileRegLeak) {
            found_leak = true;
            break;
        }
    }
    REQUIRE(found_leak);
}
