/**
 * @file     machine_ir_verifier_test.cpp
 * @brief    Unit tests for the target-independent MachineIR verifier.
 *
 * The verifier is exercised through the x86_64 type aliases because every
 * backend re-exports the same template instantiations — using the x86 view
 * here keeps the test free from cross-backend setup while still exercising
 * the generic code path.
 *
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <catch2/catch_test_macros.hpp>

#include "backends/common/include/machine_ir/verifier.h"
#include "backends/x86_64/include/machine_ir.h"

using polyglot::backends::common::machine_ir::MachineIRVerifier;
using polyglot::backends::common::machine_ir::VerifierDiagnostic;
using polyglot::backends::x86_64::MachineBasicBlock;
using polyglot::backends::x86_64::MachineFunction;
using polyglot::backends::x86_64::MachineInstr;
using polyglot::backends::x86_64::Opcode;
using polyglot::backends::x86_64::X86TargetTraits;

namespace {

MachineInstr MakeInstr(Opcode op, int def, std::initializer_list<int> uses, bool term) {
    MachineInstr mi;
    mi.opcode     = op;
    mi.def        = def;
    mi.uses       = std::vector<int>(uses);
    mi.terminator = term;
    return mi;
}

MachineFunction LegalFunction() {
    MachineFunction fn;
    fn.name = "legal";
    MachineBasicBlock bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeInstr(Opcode::kMov, 1, {}, false));
    bb.instructions.push_back(MakeInstr(Opcode::kAdd, 2, {1}, false));
    bb.instructions.push_back(MakeInstr(Opcode::kRet, -1, {2}, true));
    fn.blocks.push_back(bb);
    return fn;
}

}  // namespace

TEST_CASE("MachineIR verifier accepts a legal function", "[backends][machineir][verifier]") {
    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(LegalFunction());
    REQUIRE(result.ok());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("MachineIR verifier flags a basic block missing a terminator",
          "[backends][machineir][verifier]") {
    MachineFunction fn = LegalFunction();
    fn.blocks[0].instructions.back().terminator = false;  // strip the kRet flag

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.diagnostics.size() >= 1);

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("terminator") != std::string::npos) {
            found = true;
            REQUIRE(d.severity == VerifierDiagnostic::Severity::kError);
            REQUIRE(d.function_name == "legal");
            REQUIRE(d.block_name == "entry");
            REQUIRE_FALSE(d.snapshot.empty());
            REQUIRE(d.snapshot.find("function legal") != std::string::npos);
        }
    }
    REQUIRE(found);
}

TEST_CASE("MachineIR verifier flags a use with no definition anywhere",
          "[backends][machineir][verifier]") {
    MachineFunction fn;
    fn.name = "use_undef";
    MachineBasicBlock bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeInstr(Opcode::kAdd, 1, {99}, false));   // use vreg 99 (undef)
    bb.instructions.push_back(MakeInstr(Opcode::kRet, -1, {1}, true));
    fn.blocks.push_back(bb);

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE_FALSE(result.ok());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("no definition anywhere") != std::string::npos) {
            found = true;
            REQUIRE(d.instruction_index == 0);
            REQUIRE(d.block_index == 0);
        }
    }
    REQUIRE(found);
}

TEST_CASE("MachineIR verifier accepts a use of a vreg defined in another block",
          "[backends][machineir][verifier]") {
    MachineFunction fn;
    fn.name = "cross_block";
    MachineBasicBlock entry;
    entry.name = "entry";
    entry.instructions.push_back(MakeInstr(Opcode::kMov, 7, {}, false));
    entry.instructions.push_back(MakeInstr(Opcode::kJmp, -1, {}, true));
    fn.blocks.push_back(entry);

    MachineBasicBlock exit;
    exit.name = "exit";
    exit.instructions.push_back(MakeInstr(Opcode::kRet, -1, {7}, true));   // use of 7 from entry
    fn.blocks.push_back(exit);

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE(result.ok());
}

TEST_CASE("MachineIR verifier flags duplicate definitions inside the same block",
          "[backends][machineir][verifier]") {
    MachineFunction fn;
    fn.name = "dup_def";
    MachineBasicBlock bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeInstr(Opcode::kMov, 5, {}, false));
    bb.instructions.push_back(MakeInstr(Opcode::kAdd, 5, {5}, false));    // redefine 5
    bb.instructions.push_back(MakeInstr(Opcode::kRet, -1, {5}, true));
    fn.blocks.push_back(bb);

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE_FALSE(result.ok());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("duplicate definition") != std::string::npos) {
            found = true;
            REQUIRE(d.instruction_index == 1);
        }
    }
    REQUIRE(found);
}

TEST_CASE("MachineIR verifier flags an empty basic block", "[backends][machineir][verifier]") {
    MachineFunction fn;
    fn.name = "empty_bb";
    MachineBasicBlock bb;
    bb.name = "entry";  // no instructions
    fn.blocks.push_back(bb);

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE_FALSE(result.ok());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("empty") != std::string::npos) {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("MachineIR verifier produces a printable snapshot on failure",
          "[backends][machineir][verifier]") {
    MachineFunction fn;
    fn.name = "snap";
    MachineBasicBlock bb;
    bb.name = "entry";
    bb.instructions.push_back(MakeInstr(Opcode::kAdd, 1, {7}, false));   // 7 undef
    bb.instructions.push_back(MakeInstr(Opcode::kRet, -1, {1}, true));
    fn.blocks.push_back(bb);

    MachineIRVerifier<X86TargetTraits, Opcode> verifier;
    auto                                       result = verifier.Verify(fn);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    const auto& d = result.diagnostics.front();
    REQUIRE_FALSE(d.snapshot.empty());
    REQUIRE(d.snapshot.find("function snap") != std::string::npos);
    REQUIRE(d.snapshot.find("entry") != std::string::npos);
}
