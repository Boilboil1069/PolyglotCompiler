// ============================================================================
// Ploy Frontend Loop Optimisation Tests
//
// These tests verify that ploy loop constructs (WHILE, FOR..IN, recursive
// functions) produce IR with the correct structural properties — block counts,
// tail-call eligibility markers, and LICM-friendly instruction placement —
// as observed through the ploy→IR lowering pipeline.
//
// Unlike the IR-level loop optimisation tests (tests/unit/loop_optimization_test.cpp,
// which applies optimisation passes directly to handcrafted IR), these tests
// work at the ploy source language level and confirm observable IR structure
// changes when the ploy lowering pipeline processes loop constructs.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <sstream>
#include <algorithm>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::PloyLowering;
using polyglot::ir::IRContext;

// ============================================================================
// Helpers
// ============================================================================

namespace {

struct LoopCompileResult {
    std::string ir_text;
    size_t function_count;
    bool success;
};

LoopCompileResult Compile(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<loop_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return {"", 0, false};

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) return {"", 0, false};

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) return {"", 0, false};

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return {oss.str(), ctx.Functions().size(), true};
}

// Count how many times a substring appears in a string
int CountOccurrences(const std::string &text, const std::string &sub) {
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(sub, pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    return count;
}

// Get the number of basic blocks inside a function's IR text.
// Each block typically starts with a label line "blockN:" or "entry:".
// We count label markers as a proxy for block count.
int CountBasicBlocks(const std::string &ir_text, const std::string &fn_name) {
    // Find the function in IR text and count its blocks
    size_t fn_pos = ir_text.find(fn_name);
    if (fn_pos == std::string::npos) return -1;

    // Find function end (next function or end of string)
    size_t fn_end = ir_text.find("\ndefine ", fn_pos + 1);
    if (fn_end == std::string::npos) fn_end = ir_text.size();

    std::string fn_body = ir_text.substr(fn_pos, fn_end - fn_pos);

    // Count lines that look like block labels: "  <name>:"
    int count = 0;
    std::istringstream ss(fn_body);
    std::string line;
    while (std::getline(ss, line)) {
        // A block label is a line that ends with ':' and is not an instruction
        auto trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (!trimmed.empty() && trimmed.back() == ':' &&
            trimmed.find(' ') == std::string::npos) {
            ++count;
        }
    }
    return count;
}

} // namespace

// ============================================================================
// 1. WHILE loop: must produce multiple basic blocks (header, body, exit)
// ============================================================================

TEST_CASE("Ploy loop: WHILE generates at least 3 basic blocks", "[ploy][loop]") {
    Diagnostics diags;
    auto result = Compile(R"(
FUNC count_up() -> INT {
    VAR x = 0;
    WHILE x < 10 {
        x = x + 1;
    }
    RETURN x;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());
    REQUIRE(!result.ir_text.empty());

    // WHILE must produce: entry, loop-header, loop-body, exit (≥ 3 blocks)
    int blocks = CountBasicBlocks(result.ir_text, "count_up");
    CHECK(blocks >= 3);
}

// ============================================================================
// 2. FOR..IN loop: must also produce multiple basic blocks
// ============================================================================

TEST_CASE("Ploy loop: FOR..IN generates at least 3 basic blocks", "[ploy][loop]") {
    Diagnostics diags;
    auto result = Compile(R"(
FUNC sum_range() -> INT {
    VAR total = 0;
    FOR i IN 0..10 {
        total = total + i;
    }
    RETURN total;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());
    REQUIRE(!result.ir_text.empty());

    int blocks = CountBasicBlocks(result.ir_text, "sum_range");
    CHECK(blocks >= 3);
}

// ============================================================================
// 3. FOR..IN vs WHILE: both loops over the same range must produce the same
//    number of basic blocks (structural equivalence check)
// ============================================================================

TEST_CASE("Ploy loop: FOR..IN and equivalent WHILE have matching block counts", "[ploy][loop]") {
    Diagnostics for_diags;
    auto for_result = Compile(R"(
FUNC sum_for() -> INT {
    VAR total = 0;
    FOR i IN 0..5 {
        total = total + i;
    }
    RETURN total;
}
)", for_diags);
    REQUIRE(for_result.success);

    Diagnostics while_diags;
    auto while_result = Compile(R"(
FUNC sum_while() -> INT {
    VAR total = 0;
    VAR i = 0;
    WHILE i < 5 {
        total = total + i;
        i = i + 1;
    }
    RETURN total;
}
)", while_diags);
    REQUIRE(while_result.success);

    int for_blocks = CountBasicBlocks(for_result.ir_text, "sum_for");
    int while_blocks = CountBasicBlocks(while_result.ir_text, "sum_while");

    // Both should produce the same structural block count (loop header + body + exit)
    CHECK(for_blocks >= 3);
    CHECK(while_blocks >= 3);
    CHECK(for_blocks == while_blocks);
}

// ============================================================================
// 4. Nested loops: must produce more blocks than a single loop
// ============================================================================

TEST_CASE("Ploy loop: nested loops produce more blocks than a single loop", "[ploy][loop]") {
    Diagnostics single_diags;
    auto single = Compile(R"(
FUNC single_loop() -> INT {
    VAR s = 0;
    FOR i IN 0..5 {
        s = s + i;
    }
    RETURN s;
}
)", single_diags);
    REQUIRE(single.success);

    Diagnostics nested_diags;
    auto nested = Compile(R"(
FUNC nested_loop() -> INT {
    VAR s = 0;
    FOR i IN 0..5 {
        FOR j IN 0..5 {
            s = s + i + j;
        }
    }
    RETURN s;
}
)", nested_diags);
    REQUIRE(nested.success);

    int single_blocks = CountBasicBlocks(single.ir_text, "single_loop");
    int nested_blocks = CountBasicBlocks(nested.ir_text, "nested_loop");

    // Nested loop must have more basic blocks
    CHECK(nested_blocks > single_blocks);
}

// ============================================================================
// 5. Tail-recursive function: tail position call must appear in IR
//    This validates that ploy's recursive CALL in tail position is emitted,
//    which is the precondition for tail-call optimisation by middle-end passes.
// ============================================================================

TEST_CASE("Ploy loop: tail-recursive function emits call in tail position", "[ploy][loop]") {
    Diagnostics diags;
    auto result = Compile(R"(
FUNC factorial(n: INT, acc: INT) -> INT {
    IF n <= 1 {
        RETURN acc;
    }
    RETURN factorial(n - 1, n * acc);
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());
    REQUIRE(!result.ir_text.empty());

    // The function must appear in IR
    CHECK(result.ir_text.find("factorial") != std::string::npos);

    // There must be a recursive call to factorial (self-call = tail-call candidate)
    int call_count = CountOccurrences(result.ir_text, "factorial");
    // At least the definition + one self-call
    CHECK(call_count >= 2);
}

// ============================================================================
// 6. WHILE with BREAK: must generate an additional exit edge in IR
//    (loop body → exit-block path via the break)
// ============================================================================

TEST_CASE("Ploy loop: WHILE with BREAK generates conditional exit edge", "[ploy][loop]") {
    Diagnostics with_break_diags;
    auto with_break = Compile(R"(
FUNC find_first() -> INT {
    VAR i = 0;
    WHILE i < 100 {
        IF i == 42 {
            BREAK;
        }
        i = i + 1;
    }
    RETURN i;
}
)", with_break_diags);
    REQUIRE(with_break.success);

    Diagnostics no_break_diags;
    auto no_break = Compile(R"(
FUNC count_to() -> INT {
    VAR i = 0;
    WHILE i < 100 {
        i = i + 1;
    }
    RETURN i;
}
)", no_break_diags);
    REQUIRE(no_break.success);

    int with_break_blocks = CountBasicBlocks(with_break.ir_text, "find_first");
    int no_break_blocks = CountBasicBlocks(no_break.ir_text, "count_to");

    // BREAK creates an additional early-exit block
    CHECK(with_break_blocks > no_break_blocks);
}

// ============================================================================
// 7. WHILE with CONTINUE: must generate a backward edge that bypasses body
// ============================================================================

TEST_CASE("Ploy loop: WHILE with CONTINUE generates extra branch block", "[ploy][loop]") {
    Diagnostics with_cont_diags;
    auto with_cont = Compile(R"(
FUNC skip_evens() -> INT {
    VAR sum = 0;
    VAR i = 0;
    WHILE i < 10 {
        i = i + 1;
        IF i == 2 {
            CONTINUE;
        }
        sum = sum + i;
    }
    RETURN sum;
}
)", with_cont_diags);
    REQUIRE(with_cont.success);

    Diagnostics plain_diags;
    auto plain = Compile(R"(
FUNC sum_all() -> INT {
    VAR sum = 0;
    VAR i = 0;
    WHILE i < 10 {
        i = i + 1;
        sum = sum + i;
    }
    RETURN sum;
}
)", plain_diags);
    REQUIRE(plain.success);

    int with_cont_blocks = CountBasicBlocks(with_cont.ir_text, "skip_evens");
    int plain_blocks = CountBasicBlocks(plain.ir_text, "sum_all");

    // CONTINUE creates an additional branch block
    CHECK(with_cont_blocks > plain_blocks);
}

// ============================================================================
// 8. LICM precondition: loop-invariant cross-lang CALL must appear in IR
//    (the call is emitted inside the loop body — middle-end LICM can hoist it)
// ============================================================================

TEST_CASE("Ploy loop: loop-invariant CALL appears in loop body in IR", "[ploy][loop]") {
    Diagnostics diags;
    auto result = Compile(R"(
LINK(cpp, python, math::get_pi, pymath::pi);

FUNC sum_with_pi() -> INT {
    VAR total = 0;
    FOR i IN 0..10 {
        // math::get_pi() is loop-invariant; LICM should hoist it.
        // Here we verify it is EMITTED somewhere in the function (lowering
        // does not pre-hoist it — that is the job of the middle-end pass).
        LET pi_call = CALL(cpp, math::get_pi);
        total = total + i;
    }
    RETURN total;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // The bridge stub for get_pi must be in the IR
    CHECK(result.ir_text.find("__ploy_bridge") != std::string::npos);
    // The function body must be present
    CHECK(result.ir_text.find("sum_with_pi") != std::string::npos);
}

// ============================================================================
// 9. FOR..IN with conditional inside: verifies correct block nesting
// ============================================================================

TEST_CASE("Ploy loop: FOR with nested IF produces more blocks than plain FOR", "[ploy][loop]") {
    Diagnostics plain_diags;
    auto plain = Compile(R"(
FUNC plain_sum() -> INT {
    VAR s = 0;
    FOR i IN 0..10 { s = s + i; }
    RETURN s;
}
)", plain_diags);
    REQUIRE(plain.success);

    Diagnostics cond_diags;
    auto cond = Compile(R"(
FUNC cond_sum() -> INT {
    VAR s = 0;
    FOR i IN 0..10 {
        IF i > 5 {
            s = s + i;
        } ELSE {
            s = s + 1;
        }
    }
    RETURN s;
}
)", cond_diags);
    REQUIRE(cond.success);

    int plain_blocks = CountBasicBlocks(plain.ir_text, "plain_sum");
    int cond_blocks  = CountBasicBlocks(cond.ir_text, "cond_sum");

    // IF inside FOR adds then/else/merge blocks
    CHECK(cond_blocks > plain_blocks);
}

// ============================================================================
// 10. Infinite WHILE (no termination condition): must still produce valid IR
//     (block structure is well-formed even without an exit edge from the loop)
// ============================================================================

TEST_CASE("Ploy loop: infinite WHILE produces valid IR structure", "[ploy][loop]") {
    Diagnostics diags;
    // Use 1 == 1 as a numeric comparison that the ploy sema will accept
    auto result = Compile(R"(
FUNC infinite_work() -> INT {
    VAR x = 0;
    WHILE 1 == 1 {
        x = x + 1;
        IF x > 1000000 {
            RETURN x;
        }
    }
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());
    REQUIRE(!result.ir_text.empty());

    // The function must be well-formed
    CHECK(result.ir_text.find("infinite_work") != std::string::npos);
    int blocks = CountBasicBlocks(result.ir_text, "infinite_work");
    // Entry + loop-header + loop-body (with IF) + exit-from-IF ≥ 3
    CHECK(blocks >= 3);
}

// ============================================================================
// 11. Loop in PIPELINE: block structure is preserved inside pipeline functions
// ============================================================================

TEST_CASE("Ploy loop: PIPELINE with loop produces multi-block IR", "[ploy][loop]") {
    Diagnostics diags;
    auto result = Compile(R"(
PIPELINE aggregate {
    VAR sum = 0;
    FOR i IN 0..100 {
        sum = sum + i;
    }
    RETURN sum;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    CHECK(result.ir_text.find("__ploy_pipeline_aggregate") != std::string::npos);

    int blocks = CountBasicBlocks(result.ir_text, "__ploy_pipeline_aggregate");
    CHECK(blocks >= 3);
}

// ============================================================================
// 12. Multiple loops in one function: total block count grows additively
// ============================================================================

TEST_CASE("Ploy loop: function with two sequential loops has more blocks than one loop",
          "[ploy][loop]") {
    Diagnostics one_diags;
    auto one = Compile(R"(
FUNC one_loop() -> INT {
    VAR s = 0;
    FOR i IN 0..5 { s = s + i; }
    RETURN s;
}
)", one_diags);
    REQUIRE(one.success);

    Diagnostics two_diags;
    auto two = Compile(R"(
FUNC two_loops() -> INT {
    VAR s = 0;
    FOR i IN 0..5 { s = s + i; }
    FOR j IN 0..5 { s = s + j; }
    RETURN s;
}
)", two_diags);
    REQUIRE(two.success);

    int one_blocks = CountBasicBlocks(one.ir_text, "one_loop");
    int two_blocks  = CountBasicBlocks(two.ir_text, "two_loops");

    // Two sequential loops must have strictly more blocks
    CHECK(two_blocks > one_blocks);
}
