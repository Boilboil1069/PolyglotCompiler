#include <catch2/catch_test_macros.hpp>

#include "backends/arm64/include/machine_ir.h"
#include "backends/x86_64/include/machine_ir.h"
#include "middle/include/ir/cfg.h"

using namespace polyglot::ir;

namespace {

Function MakeBinFunction(BinaryInstruction::Op op) {
	Function fn;
	fn.name = "f";
	auto *bb = fn.CreateBlock("entry");

	auto bin = std::make_shared<BinaryInstruction>();
	bin->op = op;
	bin->name = "v0";
	bin->operands = {"a", "b"};
	switch (op) {
		case BinaryInstruction::Op::kCmpEq:
		case BinaryInstruction::Op::kCmpNe:
		case BinaryInstruction::Op::kCmpUlt:
		case BinaryInstruction::Op::kCmpUle:
		case BinaryInstruction::Op::kCmpUgt:
		case BinaryInstruction::Op::kCmpUge:
		case BinaryInstruction::Op::kCmpSlt:
		case BinaryInstruction::Op::kCmpSle:
		case BinaryInstruction::Op::kCmpSgt:
		case BinaryInstruction::Op::kCmpSge:
		case BinaryInstruction::Op::kCmpFoe:
		case BinaryInstruction::Op::kCmpFne:
		case BinaryInstruction::Op::kCmpFlt:
		case BinaryInstruction::Op::kCmpFle:
		case BinaryInstruction::Op::kCmpFgt:
		case BinaryInstruction::Op::kCmpFge:
		case BinaryInstruction::Op::kCmpLt:
			bin->type = IRType::I1();
			break;
		default:
			bin->type = IRType::I64();
			break;
	}
	bb->AddInstruction(bin);

	auto ret = std::make_shared<ReturnStatement>();
	ret->operands = {"v0"};
	bb->SetTerminator(ret);
	return fn;
}

template <typename OpcodeT>
OpcodeT GetFirstOpcode(const Function &fn, OpcodeT fallback);

template <>
polyglot::backends::arm64::Opcode GetFirstOpcode(const Function &fn, polyglot::backends::arm64::Opcode fallback) {
	polyglot::backends::arm64::CostModel cost;
	auto mf = polyglot::backends::arm64::SelectInstructions(fn, cost);
	if (!mf.blocks.empty() && !mf.blocks[0].instructions.empty()) return mf.blocks[0].instructions[0].opcode;
	return fallback;
}

template <>
polyglot::backends::x86_64::Opcode GetFirstOpcode(const Function &fn, polyglot::backends::x86_64::Opcode fallback) {
	polyglot::backends::x86_64::CostModel cost;
	auto mf = polyglot::backends::x86_64::SelectInstructions(fn, cost);
	if (!mf.blocks.empty() && !mf.blocks[0].instructions.empty()) return mf.blocks[0].instructions[0].opcode;
	return fallback;
}

}  // namespace

TEST_CASE("ARM64 selects extended binary ops", "[isel][arm64]") {
	using polyglot::backends::arm64::Opcode;
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kXor), Opcode::kAdd) == Opcode::kXor);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kShl), Opcode::kAdd) == Opcode::kShl);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kLShr), Opcode::kAdd) == Opcode::kLShr);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kAShr), Opcode::kAdd) == Opcode::kAShr);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kSDiv), Opcode::kAdd) == Opcode::kSDiv);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kUDiv), Opcode::kAdd) == Opcode::kUDiv);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kSRem), Opcode::kAdd) == Opcode::kSRem);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kURem), Opcode::kAdd) == Opcode::kURem);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kCmpSlt), Opcode::kAdd) == Opcode::kCmp);
}

TEST_CASE("X86_64 selects extended binary ops", "[isel][x86]") {
	using polyglot::backends::x86_64::Opcode;
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kXor), Opcode::kAdd) == Opcode::kXor);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kShl), Opcode::kAdd) == Opcode::kShl);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kLShr), Opcode::kAdd) == Opcode::kLShr);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kAShr), Opcode::kAdd) == Opcode::kAShr);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kSDiv), Opcode::kAdd) == Opcode::kSDiv);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kUDiv), Opcode::kAdd) == Opcode::kUDiv);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kSRem), Opcode::kAdd) == Opcode::kSRem);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kURem), Opcode::kAdd) == Opcode::kURem);
	REQUIRE(GetFirstOpcode(MakeBinFunction(BinaryInstruction::Op::kCmpSlt), Opcode::kAdd) == Opcode::kCmp);
}
