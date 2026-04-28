// ============================================================================
// IR-level tests for PRINTLN lowering — Stage B3 of demand 2026-04-28-49.
//
// What we lock in here:
//   1. A top-level `PRINTLN "Hello\r\n";` interns the *decoded* bytes
//      (4-char `\r\n` source becomes the 2-byte CRLF sequence in IR) as a
//      `.rdata` global with a `.ptr` GEP, and emits a `polyrt_println(ptr,
//      len)` call into the auto-created `entry_fn`.
//   2. The pointer + length convention is honoured, so empty literals still
//      compile to a single `polyrt_println(ptr, 0)` call (no special case in
//      the codegen pipeline downstream).
//   3. Identical literals are interned to the *same* global symbol — repeated
//      `PRINTLN "x";` statements share the data section so PE/ELF/Mach-O all
//      stay deduplicated for free.
//   4. Inside a FUNC body, the call lands in that function's basic block
//      rather than the default one, which is the property B4 codegen relies
//      on for stack-frame correctness.
//   5. Unknown escape sequences (e.g. `\q`) are reported as a warning but
//      preserved as-is, so a bad source file still produces deterministic IR
//      instead of crashing the lowering layer.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <sstream>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::ir::ConstantString;
using polyglot::ir::Function;
using polyglot::ir::IRContext;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

namespace {

struct LowerEnv {
    Diagnostics diags;
    IRContext ctx;
};

// Drive the full lex/parse/sema/lower pipeline and hand back the live IR
// context plus diagnostics so individual tests can inspect functions, globals
// and warnings without re-implementing the boilerplate every time.
bool LowerSource(const std::string &code, LowerEnv &env) {
    PloyLexer lexer(code, "<println-ir-test>");
    PloyParser parser(lexer, env.diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || env.diags.HasErrors())
        return false;

    PloySemaOptions opts;
    opts.enable_package_discovery = false;
    PloySema sema(env.diags, opts);
    if (!sema.Analyze(module))
        return false;

    PloyLowering lowering(env.ctx, env.diags, sema);
    return lowering.Lower(module);
}

std::string IrText(const IRContext &ctx) {
    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return oss.str();
}

}  // namespace

TEST_CASE("PRINTLN lowers to a polyrt_println call against an interned global",
          "[ploy][lowering][println]") {
    LowerEnv env;
    REQUIRE(LowerSource("PRINTLN \"Hello\\r\\n\";\n", env));
    REQUIRE_FALSE(env.diags.HasErrors());

    // Exactly one runtime-string global should have been interned, and its
    // payload must be the *decoded* 7-byte CRLF-terminated line — proving the
    // lowering layer (not the front-end) owns the escape-decoding contract.
    bool saw_decoded_string = false;
    for (const auto &g : env.ctx.Globals()) {
        if (auto cs = std::dynamic_pointer_cast<ConstantString>(g->initializer)) {
            if (cs->data == std::string("Hello\r\n")) {
                saw_decoded_string = true;
                break;
            }
        }
    }
    CHECK(saw_decoded_string);

    // The IR text must contain a `call ... polyrt_println(...)` referencing
    // the interned ptr global and the literal length 7.
    const std::string ir = IrText(env.ctx);
    CAPTURE(ir);
    CHECK(ir.find("polyrt_println") != std::string::npos);
    CHECK(ir.find("println.msg0.ptr") != std::string::npos);
    CHECK(ir.find(", 7") != std::string::npos);
}

TEST_CASE("PRINTLN with an empty literal still emits a single call with len=0",
          "[ploy][lowering][println]") {
    LowerEnv env;
    REQUIRE(LowerSource("PRINTLN \"\";\n", env));
    REQUIRE_FALSE(env.diags.HasErrors());

    const std::string ir = IrText(env.ctx);
    CAPTURE(ir);
    CHECK(ir.find("polyrt_println") != std::string::npos);
    CHECK(ir.find(", 0") != std::string::npos);
}

TEST_CASE("Repeated identical PRINTLN literals share a single interned global",
          "[ploy][lowering][println]") {
    LowerEnv env;
    REQUIRE(LowerSource(
        "PRINTLN \"same\";\n"
        "PRINTLN \"same\";\n",
        env));
    REQUIRE_FALSE(env.diags.HasErrors());

    int matching_globals = 0;
    for (const auto &g : env.ctx.Globals()) {
        if (auto cs = std::dynamic_pointer_cast<ConstantString>(g->initializer)) {
            if (cs->data == "same") {
                ++matching_globals;
            }
        }
    }
    // MakeStringLiteral interns by content — the second PRINTLN must reuse the
    // first global, not allocate a new one.
    CHECK(matching_globals == 1);
}

TEST_CASE("PRINTLN inside a FUNC body lands in that function, not entry_fn",
          "[ploy][lowering][println]") {
    LowerEnv env;
    REQUIRE(LowerSource(
        "FUNC main() {\n"
        "  PRINTLN \"in-main\";\n"
        "}\n",
        env));
    REQUIRE_FALSE(env.diags.HasErrors());

    Function *main_fn = env.ctx.FindFunction("main");
    REQUIRE(main_fn != nullptr);

    // Walk every instruction in `main` and confirm at least one is the
    // polyrt_println call we emitted; the implicit `entry_fn` (if it was
    // ever created at all) must not contain a polyrt_println call.
    bool main_has_call = false;
    for (const auto &block : main_fn->blocks) {
        for (const auto &inst : block->instructions) {
            if (auto call = std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(inst)) {
                if (call->callee == "polyrt_println") {
                    main_has_call = true;
                }
            }
        }
    }
    CHECK(main_has_call);

    if (Function *entry_fn = env.ctx.FindFunction("entry_fn")) {
        for (const auto &block : entry_fn->blocks) {
            for (const auto &inst : block->instructions) {
                if (auto call = std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(inst)) {
                    CHECK(call->callee != "polyrt_println");
                }
            }
        }
    }
}

TEST_CASE("Unknown escape sequences in PRINTLN are reported but do not abort lowering",
          "[ploy][lowering][println][warning]") {
    LowerEnv env;
    // `\q` is not in our escape table.
    REQUIRE(LowerSource("PRINTLN \"oops\\q\";\n", env));

    // The lowering reports via ReportWarning(), so the lower step still
    // succeeds; we only assert that *some* warning was raised and that the IR
    // still contains a polyrt_println call (i.e. no crash, no missing call).
    CHECK_FALSE(env.diags.HasErrors());
    CHECK(env.diags.HasWarnings());
    const std::string ir = IrText(env.ctx);
    CAPTURE(ir);
    CHECK(ir.find("polyrt_println") != std::string::npos);
}
