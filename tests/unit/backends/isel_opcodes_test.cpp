#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "backends/arm64/include/machine_ir.h"
#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_target.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"

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

std::shared_ptr<Function> AddConditionalFunction(IRContext &ctx, const std::string &name) {
	auto fn = ctx.CreateFunction(name, IRType::I64(),
	                             {{"a", IRType::I64()}, {"b", IRType::I64()}});
	auto *entry = fn->CreateBlock("entry");
	auto *then_bb = fn->CreateBlock("then");
	auto *else_bb = fn->CreateBlock("else");

	auto cmp = std::make_shared<BinaryInstruction>();
	cmp->op = BinaryInstruction::Op::kCmpSlt;
	cmp->name = "is_lt";
	cmp->type = IRType::I1();
	cmp->operands = {"a", "b"};
	entry->AddInstruction(cmp);

	auto branch = std::make_shared<CondBranchStatement>();
	branch->operands = {"is_lt"};
	branch->true_target = then_bb;
	branch->false_target = else_bb;
	entry->SetTerminator(branch);

	auto then_ret = std::make_shared<ReturnStatement>();
	then_ret->operands = {"1"};
	then_bb->SetTerminator(then_ret);
	auto else_ret = std::make_shared<ReturnStatement>();
	else_ret->operands = {"0"};
	else_bb->SetTerminator(else_ret);
	return fn;
}

std::shared_ptr<Function> AddSwitchFunction(IRContext &ctx, const std::string &name) {
	auto fn = ctx.CreateFunction(name, IRType::I64(), {{"value", IRType::I64()}});
	auto *entry = fn->CreateBlock("entry");
	auto *one = fn->CreateBlock("case");
	auto *two = fn->CreateBlock("case");
	auto *fallback = fn->CreateBlock("default");

	auto sw = std::make_shared<SwitchStatement>();
	sw->operands = {"value"};
	sw->cases.push_back({1, one});
	sw->cases.push_back({2, two});
	sw->default_target = fallback;
	entry->SetTerminator(sw);

	for (auto [bb, value] : std::array<std::pair<BasicBlock *, const char *>, 3>{
	         std::pair{one, "11"}, std::pair{two, "22"},
	         std::pair{fallback, "33"}}) {
		auto ret = std::make_shared<ReturnStatement>();
		ret->operands = {value};
		bb->SetTerminator(ret);
	}
	return fn;
}

bool ContainsBytes(const std::vector<std::uint8_t> &data,
	               std::initializer_list<std::uint8_t> needle) {
	return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
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

TEST_CASE("X86_64 preserves every integer comparison predicate in MachineIR",
          "[isel][x86][control-flow]") {
	using polyglot::backends::x86_64::CostModel;
	using polyglot::backends::x86_64::IntComparePredicate;
	using polyglot::backends::x86_64::Opcode;
	using polyglot::backends::x86_64::Operand;
	using polyglot::backends::x86_64::SelectInstructions;

	const std::array cases = {
	    std::pair{BinaryInstruction::Op::kCmpEq, IntComparePredicate::kEq},
	    std::pair{BinaryInstruction::Op::kCmpNe, IntComparePredicate::kNe},
	    std::pair{BinaryInstruction::Op::kCmpUlt, IntComparePredicate::kUlt},
	    std::pair{BinaryInstruction::Op::kCmpUle, IntComparePredicate::kUle},
	    std::pair{BinaryInstruction::Op::kCmpUgt, IntComparePredicate::kUgt},
	    std::pair{BinaryInstruction::Op::kCmpUge, IntComparePredicate::kUge},
	    std::pair{BinaryInstruction::Op::kCmpSlt, IntComparePredicate::kSlt},
	    std::pair{BinaryInstruction::Op::kCmpSle, IntComparePredicate::kSle},
	    std::pair{BinaryInstruction::Op::kCmpSgt, IntComparePredicate::kSgt},
	    std::pair{BinaryInstruction::Op::kCmpSge, IntComparePredicate::kSge},
	};

	for (const auto &[ir_predicate, machine_predicate] : cases) {
		auto fn = MakeBinFunction(ir_predicate);
		auto mf = SelectInstructions(fn, CostModel{});
		REQUIRE_FALSE(mf.blocks.empty());
		REQUIRE_FALSE(mf.blocks.front().instructions.empty());
		const auto &cmp = mf.blocks.front().instructions.front();
		REQUIRE(cmp.opcode == Opcode::kCmp);
		REQUIRE(cmp.def >= 0);
		REQUIRE(cmp.operands.size() == 3);
		REQUIRE(cmp.operands[2].kind == Operand::Kind::kImm);
		CHECK(cmp.operands[2].imm == static_cast<long long>(machine_predicate));
	}
}

TEST_CASE("X86_64 qualifies branch and switch labels by function and block index",
          "[isel][x86][control-flow]") {
	using namespace polyglot::backends::x86_64;
	IRContext ctx;
	auto branch_fn = AddConditionalFunction(ctx, "decide");
	auto other_fn = AddConditionalFunction(ctx, "decide_again");
	auto switch_fn = AddSwitchFunction(ctx, "route");

	auto branch_mf = SelectInstructions(*branch_fn, CostModel{});
	auto other_mf = SelectInstructions(*other_fn, CostModel{});
	REQUIRE(branch_mf.blocks.size() == 3);
	REQUIRE(other_mf.blocks.size() == 3);
	CHECK(branch_mf.blocks[0].name == "decide.__bb0");
	CHECK(branch_mf.blocks[1].name == "decide.__bb1");
	CHECK(branch_mf.blocks[2].name == "decide.__bb2");
	CHECK(other_mf.blocks[0].name == "decide_again.__bb0");
	CHECK(branch_mf.blocks[0].name != other_mf.blocks[0].name);

	const auto branch_it = std::find_if(
	    branch_mf.blocks[0].instructions.begin(), branch_mf.blocks[0].instructions.end(),
	    [](const MachineInstr &mi) { return mi.opcode == Opcode::kJcc; });
	REQUIRE(branch_it != branch_mf.blocks[0].instructions.end());
	REQUIRE(branch_it->operands.size() == 2);
	CHECK(branch_it->operands[0].label == branch_mf.blocks[1].name);
	CHECK(branch_it->operands[1].label == branch_mf.blocks[2].name);

	auto switch_mf = SelectInstructions(*switch_fn, CostModel{});
	REQUIRE(switch_mf.blocks.size() == 5); // four IR blocks + one comparison block
	std::set<std::string> labels;
	for (const auto &bb : switch_mf.blocks)
		REQUIRE(labels.insert(bb.name).second);
	CHECK(labels.count("route.__bb0") == 1);
	CHECK(labels.count("route.__bb0.__switch1") == 1);
	CHECK(labels.count("route.__bb1") == 1);
	CHECK(labels.count("route.__bb2") == 1);
	CHECK(labels.count("route.__bb3") == 1);
	for (const auto &bb : switch_mf.blocks) {
		for (const auto &mi : bb.instructions) {
			if (mi.opcode != Opcode::kJcc && mi.opcode != Opcode::kJmp)
				continue;
			for (const auto &operand : mi.operands) {
				if (operand.kind == Operand::Kind::kLabel)
					CHECK(labels.count(operand.label) == 1);
			}
		}
	}
}

TEST_CASE("X86_64 object emitter materializes compare bool and both branch edges",
          "[backend][x86][control-flow]") {
	using polyglot::backends::x86_64::X86Target;
	IRContext ctx;
	AddConditionalFunction(ctx, "branch_bytes");

	X86Target target(&ctx);
	auto mc = target.EmitObjectCode();
	const X86Target::MCSection *text = nullptr;
	for (const auto &section : mc.sections) {
		if (section.name == ".text")
			text = &section;
	}
	REQUIRE(text != nullptr);
	CHECK(ContainsBytes(text->data, {0x0F, 0x9C}));       // setl r/m8
	CHECK(ContainsBytes(text->data, {0x48, 0x0F, 0xB6})); // movzx r64, r/m8
	CHECK(ContainsBytes(text->data, {0x0F, 0x85}));       // jne true
	CHECK(std::find(text->data.begin(), text->data.end(), 0xE9) != text->data.end());

	bool saw_true_edge = false;
	bool saw_false_edge = false;
	for (const auto &reloc : mc.relocs) {
		if (reloc.symbol == "branch_bytes.__bb1")
			saw_true_edge = true;
		if (reloc.symbol == "branch_bytes.__bb2")
			saw_false_edge = true;
	}
	CHECK(saw_true_edge);
	CHECK(saw_false_edge);

	std::set<std::string> local_labels;
	for (const auto &symbol : mc.symbols) {
		if (!symbol.global && symbol.defined)
			REQUIRE(local_labels.insert(symbol.name).second);
	}
}

namespace {

std::shared_ptr<Function> AddAggregateStackFunction(IRContext &ctx,
                                                     const std::string &name) {
	const IRType i32 = IRType::I32(true);
	const IRType pair = IRType::Struct("FourFields", {i32, i32, i32, i32});
	auto fn = ctx.CreateFunction(name, i32,
	                             {{"first", i32}, {"second", i32},
	                              {"third", i32}, {"fourth", i32}});
	auto *entry = fn->CreateBlock("entry");

	auto object = std::make_shared<AllocaInstruction>();
	object->name = "object";
	object->type = IRType::Pointer(pair);
	entry->AddInstruction(object);

	auto first = std::make_shared<GetElementPtrInstruction>();
	first->name = "first.addr";
	first->operands = {"object"};
	first->source_type = pair;
	first->indices = {0, 0};
	first->type = IRType::Pointer(i32);
	entry->AddInstruction(first);

	auto first_store = std::make_shared<StoreInstruction>();
	first_store->operands = {"first.addr", "first"};
	entry->AddInstruction(first_store);

	auto second = std::make_shared<GetElementPtrInstruction>();
	second->name = "second.addr";
	second->operands = {"object"};
	second->source_type = pair;
	second->indices = {0, 1};
	second->type = IRType::Pointer(i32);
	entry->AddInstruction(second);

	auto second_store = std::make_shared<StoreInstruction>();
	second_store->operands = {"second.addr", "second"};
	entry->AddInstruction(second_store);

	auto third = std::make_shared<GetElementPtrInstruction>();
	third->name = "third.addr";
	third->operands = {"object"};
	third->source_type = pair;
	third->indices = {0, 2};
	third->type = IRType::Pointer(i32);
	entry->AddInstruction(third);

	auto third_store = std::make_shared<StoreInstruction>();
	third_store->operands = {"third.addr", "third"};
	entry->AddInstruction(third_store);

	auto fourth = std::make_shared<GetElementPtrInstruction>();
	fourth->name = "fourth.addr";
	fourth->operands = {"object"};
	fourth->source_type = pair;
	fourth->indices = {0, 3};
	fourth->type = IRType::Pointer(i32);
	entry->AddInstruction(fourth);

	auto fourth_store = std::make_shared<StoreInstruction>();
	fourth_store->operands = {"fourth.addr", "fourth"};
	entry->AddInstruction(fourth_store);

	auto first_load = std::make_shared<LoadInstruction>();
	first_load->name = "first.value";
	first_load->operands = {"first.addr"};
	first_load->type = i32;
	entry->AddInstruction(first_load);

	auto second_load = std::make_shared<LoadInstruction>();
	second_load->name = "second.value";
	second_load->operands = {"second.addr"};
	second_load->type = i32;
	entry->AddInstruction(second_load);

	auto third_load = std::make_shared<LoadInstruction>();
	third_load->name = "third.value";
	third_load->operands = {"third.addr"};
	third_load->type = i32;
	entry->AddInstruction(third_load);

	auto fourth_load = std::make_shared<LoadInstruction>();
	fourth_load->name = "fourth.value";
	fourth_load->operands = {"fourth.addr"};
	fourth_load->type = i32;
	entry->AddInstruction(fourth_load);

	auto first_sum = std::make_shared<BinaryInstruction>();
	first_sum->name = "first.sum";
	first_sum->op = BinaryInstruction::Op::kAdd;
	first_sum->operands = {"first.value", "second.value"};
	first_sum->type = i32;
	entry->AddInstruction(first_sum);

	auto second_sum = std::make_shared<BinaryInstruction>();
	second_sum->name = "second.sum";
	second_sum->op = BinaryInstruction::Op::kAdd;
	second_sum->operands = {"first.sum", "third.value"};
	second_sum->type = i32;
	entry->AddInstruction(second_sum);

	auto sum = std::make_shared<BinaryInstruction>();
	sum->name = "sum";
	sum->op = BinaryInstruction::Op::kAdd;
	sum->operands = {"second.sum", "fourth.value"};
	sum->type = i32;
	entry->AddInstruction(sum);

	auto ret = std::make_shared<ReturnStatement>();
	ret->operands = {"sum"};
	entry->SetTerminator(ret);
	return fn;
}

} // namespace

TEST_CASE("X86_64 lowers aggregate alloca and DataLayout field offsets",
          "[isel][x86][aggregate]") {
	using namespace polyglot::backends::x86_64;
	IRContext ctx;
	auto fn = AddAggregateStackFunction(ctx, "aggregate_isel");
	auto mf = SelectInstructions(*fn, CostModel{});

	REQUIRE(mf.stack_objects.size() == 1);
	CHECK(mf.stack_objects[0].size == 16);
	CHECK(mf.stack_objects[0].alignment == 4);
	REQUIRE_FALSE(mf.blocks.empty());

	bool saw_stack_address = false;
	bool saw_first_field = false;
	bool saw_second_field = false;
	bool saw_third_field = false;
	bool saw_fourth_field = false;
	int narrow_stores = 0;
	int signed_narrow_loads = 0;
	for (const auto &instruction : mf.blocks.front().instructions) {
		if (instruction.opcode == Opcode::kLea && !instruction.operands.empty()) {
			const auto &address = instruction.operands.front();
			if (address.kind == Operand::Kind::kStackObject)
				saw_stack_address = address.stack_slot == 0;
			if (address.kind == Operand::Kind::kMemVReg && address.displacement == 0)
				saw_first_field = true;
			if (address.kind == Operand::Kind::kMemVReg && address.displacement == 4)
				saw_second_field = true;
			if (address.kind == Operand::Kind::kMemVReg && address.displacement == 8)
				saw_third_field = true;
			if (address.kind == Operand::Kind::kMemVReg && address.displacement == 12)
				saw_fourth_field = true;
		}
		if (instruction.opcode == Opcode::kStore && instruction.memory_width == 4)
			++narrow_stores;
		if (instruction.opcode == Opcode::kLoad && instruction.memory_width == 4 &&
		    instruction.memory_signed)
			++signed_narrow_loads;
	}
	CHECK(saw_stack_address);
	CHECK(saw_first_field);
	CHECK(saw_second_field);
	CHECK(saw_third_field);
	CHECK(saw_fourth_field);
	CHECK(narrow_stores == 4);
	CHECK(signed_narrow_loads == 4);
}

TEST_CASE("X86_64 object emitter encodes stack and displaced object memory",
          "[backend][x86][aggregate]") {
	using polyglot::backends::x86_64::X86Target;
	IRContext ctx;
	AddAggregateStackFunction(ctx, "aggregate_object");
	X86Target target(&ctx);
	const auto mc = target.EmitObjectCode();

	const X86Target::MCSection *text = nullptr;
	for (const auto &section : mc.sections) {
		if (section.name == ".text")
			text = &section;
	}
	REQUIRE(text != nullptr);

	bool saw_rbp_stack_address = false;
	bool saw_four_byte_gep = false;
	bool saw_eight_byte_gep = false;
	bool saw_twelve_byte_gep = false;
	for (std::size_t i = 0; i + 3 < text->data.size(); ++i) {
		if (text->data[i] != 0x48 || text->data[i + 1] != 0x8D)
			continue;
		const std::uint8_t modrm = text->data[i + 2];
		const std::uint8_t mod = modrm >> 6;
		const std::uint8_t base = modrm & 7;
		if (mod != 0 && base == 5)
			saw_rbp_stack_address = true;
		if (mod == 1 && text->data[i + 3] == 4)
			saw_four_byte_gep = true;
		if (mod == 1 && text->data[i + 3] == 8)
			saw_eight_byte_gep = true;
		if (mod == 1 && text->data[i + 3] == 12)
			saw_twelve_byte_gep = true;
	}

	CHECK(saw_rbp_stack_address);
	CHECK(saw_four_byte_gep);
	CHECK(saw_eight_byte_gep);
	CHECK(saw_twelve_byte_gep);
	CHECK(ContainsBytes(text->data, {0x48, 0x63})); // signed i32 field load
}

TEST_CASE("X86_64 executes a four-field aggregate from emitted native code",
          "[backend][x86][aggregate][execute]") {
#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
	using polyglot::backends::x86_64::X86Target;
	IRContext ctx;
	AddAggregateStackFunction(ctx, "aggregate_execute");
	X86Target target(&ctx);
	const auto mc = target.EmitObjectCode();

	const X86Target::MCSection *text = nullptr;
	std::uint64_t function_offset = 0;
	for (const auto &section : mc.sections) {
		if (section.name == ".text")
			text = &section;
	}
	for (const auto &symbol : mc.symbols) {
		if (symbol.name == "aggregate_execute" && symbol.defined)
			function_offset = symbol.value;
	}
	REQUIRE(text != nullptr);
	REQUIRE(function_offset < text->data.size());

	const long page_size = ::sysconf(_SC_PAGESIZE);
	REQUIRE(page_size > 0);
	const std::size_t allocation_size =
	    (text->data.size() + static_cast<std::size_t>(page_size) - 1) /
	    static_cast<std::size_t>(page_size) * static_cast<std::size_t>(page_size);
	void *mapping = ::mmap(nullptr, allocation_size, PROT_READ | PROT_WRITE,
	                       MAP_PRIVATE | MAP_ANON, -1, 0);
	REQUIRE(mapping != MAP_FAILED);
	std::memcpy(mapping, text->data.data(), text->data.size());
	REQUIRE(::mprotect(mapping, allocation_size, PROT_READ | PROT_EXEC) == 0);

	using NativeFunction = std::int64_t (*)(std::int64_t, std::int64_t,
	                                        std::int64_t, std::int64_t);
	auto *entry = static_cast<std::uint8_t *>(mapping) + function_offset;
	auto function = reinterpret_cast<NativeFunction>(entry);
	CHECK(function(10, 20, 30, 40) == 100);
	CHECK(function(-7, 2, -3, 9) == 1);
	CHECK(::munmap(mapping, allocation_size) == 0);
#else
	SUCCEED("native execution is only applicable on x86_64 POSIX hosts");
#endif
}
