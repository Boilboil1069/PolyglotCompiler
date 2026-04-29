// ============================================================================
// Backend `.rdata` emission tests for both x86_64 and arm64 targets.
//
// What we lock in here:
//   1. After IR-level `MakeStringLiteral("hi\n")`, both backends produce
//      a `.rdata` section whose first bytes match the decoded payload
//      followed by a NUL terminator.
//   2. Both backends register the two symbols the lowering layer expects
//      polyld to see: `<hint><N>` for the raw bytes (e.g. `str0` /
//      `println.msg0`) and `<hint><N>.ptr` for the pointer alias.
//   3. The pointer-alias slot is an 8-byte zero placeholder followed by
//      an `ABS64` (type=0) relocation against the underlying bytes
//      symbol — that is the exact shape polyld's section-merging linker
//      pass uses to thread the runtime address through the final image.
//   4. Modules without any string globals must NOT emit a stray `.rdata`
//      section (we no longer ship the placeholder "msg" rodata that the
//      previous `EmitObjectCode()` always appended).
//
// Both targets are exercised in the same translation unit so the parity
// invariant (x86_64 and arm64 must produce identical symbol/reloc shapes
// for the same IR) is enforced by construction.
// ============================================================================

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/arm64/include/arm64_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"

using polyglot::backends::arm64::Arm64Target;
using polyglot::backends::x86_64::X86Target;
using polyglot::ir::IRBuilder;
using polyglot::ir::IRContext;
using polyglot::ir::IRType;

namespace {

// Build a tiny IR module containing one trivial function plus a single
// `MakeStringLiteral("hi\n")` global pair (the data global plus its `.ptr`
// alias). Returns the IR context by reference so the caller can hand it
// straight to a backend.
void BuildTinyModuleWithStringLiteral(IRContext &ctx) {
    auto fn = ctx.CreateFunction("nop_main", IRType::I64(), {});
    auto bb = std::make_shared<polyglot::ir::BasicBlock>();
    bb->name = "entry";
    fn->blocks.push_back(bb);
    fn->entry = bb.get();
    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("0");
    bb->SetTerminator(ret);

    IRBuilder builder(ctx);
    // The hint argument controls the symbol prefix; we use "str" here so
    // the assertions can pin the exact spelling without depending on the
    // PRINTLN-specific naming convention used by the lowering layer.
    builder.MakeStringLiteral("hi\n", "str");
}

// Find a section by name. Returns nullptr if no such section exists.
template <typename MCResult>
const typename MCResult::value_type *FindSection(const std::vector<typename MCResult::value_type> &secs,
                                                 const std::string &name) {
    for (const auto &s : secs) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

}  // namespace

// ----- x86_64 ---------------------------------------------------------------

TEST_CASE("x86_64 EmitObjectCode emits .rdata bytes for ConstantString globals",
          "[backend][x86_64][globals_emit][pe7]") {
    IRContext ctx;
    BuildTinyModuleWithStringLiteral(ctx);

    X86Target target(&ctx);
    auto mc = target.EmitObjectCode();

    const X86Target::MCSection *rdata = nullptr;
    for (const auto &s : mc.sections) {
        if (s.name == ".rdata") { rdata = &s; break; }
    }
    REQUIRE(rdata != nullptr);
    // First 4 bytes: 'h', 'i', '\n', '\0' (the decoded payload + NUL).
    REQUIRE(rdata->data.size() >= 4);
    CHECK(rdata->data[0] == static_cast<std::uint8_t>('h'));
    CHECK(rdata->data[1] == static_cast<std::uint8_t>('i'));
    CHECK(rdata->data[2] == static_cast<std::uint8_t>('\n'));
    CHECK(rdata->data[3] == 0u);

    // The two symbols MakeStringLiteral conjures are `str0` (the raw bytes)
    // and `str0.ptr` (the GEP alias). Both must land in `.rdata` and be
    // marked global+defined so the polyld symbol resolver picks them up.
    const X86Target::MCSymbol *bytes_sym = nullptr;
    const X86Target::MCSymbol *ptr_sym = nullptr;
    for (const auto &s : mc.symbols) {
        if (s.name == "str0") bytes_sym = &s;
        if (s.name == "str0.ptr") ptr_sym = &s;
    }
    REQUIRE(bytes_sym != nullptr);
    REQUIRE(ptr_sym != nullptr);
    CHECK(bytes_sym->section == ".rdata");
    CHECK(bytes_sym->global);
    CHECK(bytes_sym->defined);
    CHECK(bytes_sym->size == 4u);  // 3 bytes "hi\n" + NUL
    CHECK(ptr_sym->section == ".rdata");
    CHECK(ptr_sym->global);
    CHECK(ptr_sym->defined);
    CHECK(ptr_sym->size == 8u);  // 8-byte ABS64 slot

    // Exactly one ABS64 relocation against the underlying bytes symbol,
    // anchored at the `.rdata` offset of the alias slot.
    int abs64_count = 0;
    for (const auto &r : mc.relocs) {
        if (r.section == ".rdata" && r.symbol == "str0" && r.type == 0u) {
            ++abs64_count;
            CHECK(static_cast<std::uint64_t>(r.offset) == ptr_sym->value);
            CHECK(r.addend == 0);
        }
    }
    CHECK(abs64_count == 1);
}

TEST_CASE("x86_64 EmitObjectCode does NOT emit .rdata when no globals exist",
          "[backend][x86_64][globals_emit][pe7]") {
    IRContext ctx;
    auto fn = ctx.CreateFunction("only_text", IRType::I64(), {});
    auto bb = std::make_shared<polyglot::ir::BasicBlock>();
    bb->name = "entry";
    fn->blocks.push_back(bb);
    fn->entry = bb.get();
    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("0");
    bb->SetTerminator(ret);

    X86Target target(&ctx);
    auto mc = target.EmitObjectCode();

    bool saw_text = false;
    bool saw_rdata = false;
    bool saw_data = false;
    for (const auto &s : mc.sections) {
        if (s.name == ".text") saw_text = true;
        if (s.name == ".rdata") saw_rdata = true;
        if (s.name == ".data") saw_data = true;
    }
    CHECK(saw_text);
    CHECK_FALSE(saw_rdata);
    // The previous backend stub used to always append a placeholder
    // `.data` section with a `msg` symbol — that contract is dead now.
    CHECK_FALSE(saw_data);
    for (const auto &sym : mc.symbols) {
        CHECK(sym.name != "msg");
    }
}

// ----- arm64 ----------------------------------------------------------------

TEST_CASE("arm64 EmitObjectCode emits .rdata bytes for ConstantString globals",
          "[backend][arm64][globals_emit][pe7]") {
    IRContext ctx;
    BuildTinyModuleWithStringLiteral(ctx);

    Arm64Target target(&ctx);
    auto mc = target.EmitObjectCode();

    const Arm64Target::MCSection *rdata = nullptr;
    for (const auto &s : mc.sections) {
        if (s.name == ".rdata") { rdata = &s; break; }
    }
    REQUIRE(rdata != nullptr);
    REQUIRE(rdata->data.size() >= 4);
    CHECK(rdata->data[0] == static_cast<std::uint8_t>('h'));
    CHECK(rdata->data[1] == static_cast<std::uint8_t>('i'));
    CHECK(rdata->data[2] == static_cast<std::uint8_t>('\n'));
    CHECK(rdata->data[3] == 0u);

    const Arm64Target::MCSymbol *bytes_sym = nullptr;
    const Arm64Target::MCSymbol *ptr_sym = nullptr;
    for (const auto &s : mc.symbols) {
        if (s.name == "str0") bytes_sym = &s;
        if (s.name == "str0.ptr") ptr_sym = &s;
    }
    REQUIRE(bytes_sym != nullptr);
    REQUIRE(ptr_sym != nullptr);
    CHECK(bytes_sym->section == ".rdata");
    CHECK(bytes_sym->size == 4u);
    CHECK(ptr_sym->section == ".rdata");
    CHECK(ptr_sym->size == 8u);

    int abs64_count = 0;
    for (const auto &r : mc.relocs) {
        if (r.section == ".rdata" && r.symbol == "str0" && r.type == 0u) {
            ++abs64_count;
            CHECK(static_cast<std::uint64_t>(r.offset) == ptr_sym->value);
            CHECK(r.addend == 0);
        }
    }
    CHECK(abs64_count == 1);
}
