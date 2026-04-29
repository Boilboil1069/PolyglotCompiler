// ============================================================================
// Synthetic-entry tests for the ploy lowering layer.
//
// What we lock in here:
//   1. A .ploy source that contains only top-level executable statements
//      (e.g. a lone PRINTLN) is wrapped in a synthesised function whose name
//      is `__ploy_main`, whose return type is i32, and whose entry block is
//      properly terminated by `RETURN <i32 0>`. Without that wrapper the IR
//      verifier would reject the module with "block missing terminator:
//      entry" and the whole polyc → polyld → exe pipeline would die before
//      the backend ever got the chance to emit the literal bytes.
//   2. When the source already declares its own `FUNC main(...)` the
//      lowering layer must not invent a competing `__ploy_main`; a
//      hand-written entry point always wins.
//   3. A pure "definitions only" .ploy (e.g. just `LINK` directives or a
//      single `STRUCT`) produces no synthetic wrapper either — the wrapper
//      exists solely to give orphan executable statements a home.
//   4. The synthetic entry interleaves correctly with user-defined helper
//      functions: `FUNC helper() {...}` plus a top-level `PRINTLN` ends up
//      with both `helper` and `__ploy_main` in `ctx.Functions()`, and the
//      PRINTLN call lives inside `__ploy_main`'s entry block.
// ============================================================================

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/verifier.h"

using polyglot::frontends::Diagnostics;
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

bool LowerSource(const std::string &code, LowerEnv &env) {
    PloyLexer lexer(code, "<synthetic-main-test>");
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

// True iff every block in `fn` ends in a terminator instruction. The IR
// verifier enforces this rule globally; we re-check it locally here to
// produce a more focused failure message.
bool AllBlocksTerminated(const Function &fn) {
    for (const auto &block : fn.blocks) {
        if (!block->terminator)
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("Top-level PRINTLN is wrapped in a synthesised __ploy_main entry",
          "[ploy][lowering][synthetic_main]") {
    LowerEnv env;
    REQUIRE(LowerSource("PRINTLN \"x\";\n", env));
    REQUIRE_FALSE(env.diags.HasErrors());

    Function *synth = env.ctx.FindFunction("__ploy_main");
    REQUIRE(synth != nullptr);
    CHECK(synth->ret_type.kind == polyglot::ir::IRTypeKind::kI32);
    CHECK(synth->params.empty());
    REQUIRE_FALSE(synth->blocks.empty());
    CHECK(synth->blocks.front()->name == "entry");
    REQUIRE(AllBlocksTerminated(*synth));

    // The terminator must be a Return statement carrying a single operand
    // (the SSA name of the synthesised `i32 0`). Every other terminator
    // shape would leave the program with an undefined exit code.
    auto term = synth->blocks.front()->terminator;
    REQUIRE(term != nullptr);
    auto ret = std::dynamic_pointer_cast<polyglot::ir::ReturnStatement>(term);
    REQUIRE(ret != nullptr);
    REQUIRE(ret->operands.size() == 1);
    CHECK_FALSE(ret->operands.front().empty());

    // The IR module as a whole must satisfy the verifier — the very rule
    // ("block missing terminator: entry") that motivated this lowering
    // change has to come back green now.
    std::string verify_msg;
    bool ok = polyglot::ir::Verify(env.ctx, &verify_msg);
    CAPTURE(verify_msg);
    CHECK(ok);
}

TEST_CASE("User-supplied FUNC main is preserved verbatim and no wrapper is invented",
          "[ploy][lowering][synthetic_main]") {
    LowerEnv env;
    REQUIRE(LowerSource(
        "FUNC main() -> i32 {\n"
        "  PRINTLN \"hi\";\n"
        "  RETURN 0;\n"
        "}\n",
        env));
    REQUIRE_FALSE(env.diags.HasErrors());

    REQUIRE(env.ctx.FindFunction("main") != nullptr);
    CHECK(env.ctx.FindFunction("__ploy_main") == nullptr);
}

TEST_CASE("Definition-only modules do not get a synthetic __ploy_main",
          "[ploy][lowering][synthetic_main]") {
    LowerEnv env;
    REQUIRE(LowerSource(
        "STRUCT Point { x: i32, y: i32 }\n",
        env));
    REQUIRE_FALSE(env.diags.HasErrors());

    CHECK(env.ctx.FindFunction("__ploy_main") == nullptr);
}

TEST_CASE("Synthetic __ploy_main coexists with user-defined helper functions",
          "[ploy][lowering][synthetic_main]") {
    LowerEnv env;
    REQUIRE(LowerSource(
        "FUNC helper() -> i32 {\n"
        "  RETURN 7;\n"
        "}\n"
        "PRINTLN \"top-level\";\n",
        env));
    REQUIRE_FALSE(env.diags.HasErrors());

    Function *helper = env.ctx.FindFunction("helper");
    REQUIRE(helper != nullptr);
    Function *synth = env.ctx.FindFunction("__ploy_main");
    REQUIRE(synth != nullptr);

    // The PRINTLN call must live inside the synthetic wrapper, not the
    // helper — the partitioning logic must classify FuncDecl as
    // definitional and PrintlnStmt as executable.
    bool synth_has_println = false;
    for (const auto &block : synth->blocks) {
        for (const auto &inst : block->instructions) {
            if (auto call = std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(inst)) {
                if (call->callee == "polyrt_println") {
                    synth_has_println = true;
                }
            }
        }
    }
    CHECK(synth_has_println);

    bool helper_has_println = false;
    for (const auto &block : helper->blocks) {
        for (const auto &inst : block->instructions) {
            if (auto call = std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(inst)) {
                if (call->callee == "polyrt_println") {
                    helper_has_println = true;
                }
            }
        }
    }
    CHECK_FALSE(helper_has_println);
}
