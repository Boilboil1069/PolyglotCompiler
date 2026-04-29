// ============================================================================
// PE-7-C: Backend `lea` / `ADRP+ADD` relocation emission for global refs
//         consumed by external runtime calls (e.g. polyrt_println).
//
// What we lock in here:
//   * When a CallInstruction's argument names a global symbol that has no
//     SSA producer inside the current function, the instruction selector
//     synthesises a `kLea` MachineInstr that materialises the symbol's
//     address into a vreg before the call. The asm emitter then encodes
//     the RIP-relative load (`lea reg,[rip+disp32]` on x86_64,
//     `ADRP Rd / ADD Rd,Rd,#0` on arm64) AND attaches the proper
//     PC-relative relocation(s) to `.text` so polyld can patch the final
//     displacement at link time.
//   * Both backends (x86_64 + arm64) must produce the parity-matched
//     reloc shape against the SAME global symbol name.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>

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

// Build a tiny module that interns the literal "hi\n" then issues
// `polyrt_println(<ptr_alias>, 3)` from a single-block function. This is
// exactly the IR shape the ploy lowering layer hands to the backends for
// every PRINTLN call.
void BuildPrintlnishModule(IRContext &ctx, std::string *ptr_name_out) {
    auto fn = ctx.CreateFunction("__ploy_main", IRType::I32(), {});
    auto bb = std::make_shared<polyglot::ir::BasicBlock>();
    bb->name = "entry";
    fn->blocks.push_back(bb);
    fn->entry = bb.get();

    IRBuilder builder(ctx);
    builder.SetInsertPoint(bb);
    const std::string ptr_name = builder.MakeStringLiteral("hi\n", "str");
    if (ptr_name_out) *ptr_name_out = ptr_name;
    builder.MakeCall("polyrt_println", {ptr_name, "3"}, IRType::Void());
    builder.MakeReturn("0");
}

}  // namespace

// ----- x86_64 ---------------------------------------------------------------

TEST_CASE("x86_64 emits REL32 lea relocation for polyrt_println string arg",
          "[backend][x86_64][printf_lea_reloc][pe7]") {
    IRContext ctx;
    std::string ptr_name;
    BuildPrintlnishModule(ctx, &ptr_name);
    REQUIRE(ptr_name == "str0.ptr");

    X86Target target(&ctx);
    auto mc = target.EmitObjectCode();

    // Exactly one REL32 relocation against `str0.ptr` must appear in `.text`,
    // anchored at the disp32 field of a `lea reg,[rip+disp32]` instruction.
    int rel32_lea = 0;
    int rel32_call = 0;
    for (const auto &r : mc.relocs) {
        if (r.section != ".text" || r.type != 1u) continue;
        if (r.symbol == "str0.ptr") {
            ++rel32_lea;
            CHECK(r.addend == -4);
        } else if (r.symbol == "polyrt_println") {
            ++rel32_call;
            CHECK(r.addend == -4);
        }
    }
    CHECK(rel32_lea == 1);
    CHECK(rel32_call == 1);

    // The `lea` opcode bytes (0x48 0x8D ModRM) must precede the disp32 we
    // relocated — sanity-check that the byte 4 positions before the reloc
    // offset is 0x8D.
    for (const auto &r : mc.relocs) {
        if (r.section == ".text" && r.symbol == "str0.ptr" && r.type == 1u) {
            const auto *text = [&]() -> const X86Target::MCSection * {
                for (const auto &s : mc.sections)
                    if (s.name == ".text") return &s;
                return nullptr;
            }();
            REQUIRE(text != nullptr);
            REQUIRE(r.offset >= 2u);
            // The byte right before disp32 is the ModR/M; the low 3 bits
            // (r/m) must be 0b101 to mean "disp32 with no base register"
            // — that's the canonical RIP-relative encoding on x86_64.
            const std::uint8_t modrm = text->data[r.offset - 1];
            CHECK(static_cast<int>(modrm & 0x07u) == 0x05);
            CHECK(text->data[r.offset - 2] == static_cast<std::uint8_t>(0x8D));
        }
    }
}

// ----- arm64 ----------------------------------------------------------------

TEST_CASE("arm64 emits ADRP+ADD relocation pair for polyrt_println string arg",
          "[backend][arm64][printf_lea_reloc][pe7]") {
    IRContext ctx;
    std::string ptr_name;
    BuildPrintlnishModule(ctx, &ptr_name);
    REQUIRE(ptr_name == "str0.ptr");

    Arm64Target target(&ctx);
    auto mc = target.EmitObjectCode();

    // ADRP -> reloc type 2 (PAGE21) AND ADD -> reloc type 3 (PAGEOFF12),
    // both in `.text`, both naming `str0.ptr`. Exactly one of each.
    int page21 = 0;
    int pageoff12 = 0;
    int bl_polyrt = 0;
    for (const auto &r : mc.relocs) {
        if (r.section != ".text") continue;
        if (r.symbol == "str0.ptr" && r.type == 2u) ++page21;
        if (r.symbol == "str0.ptr" && r.type == 3u) ++pageoff12;
        if (r.symbol == "polyrt_println" && r.type == 1u) ++bl_polyrt;
    }
    CHECK(page21 == 1);
    CHECK(pageoff12 == 1);
    CHECK(bl_polyrt == 1);
}
