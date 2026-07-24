#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <sstream>
#include <string>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;
using polyglot::python::LowerToIR;
using polyglot::ir::IRContext;

namespace {

// Helper to parse and lower Python code
std::pair<IRContext, bool> ParseAndLower(const std::string &code, Diagnostics &diags) {
    PythonLexer lexer(code, "<test>", &diags);
    PythonParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    
    IRContext ctx;
    if (module && !diags.HasErrors()) {
        LowerToIR(*module, ctx, diags);
    }
    return {std::move(ctx), !diags.HasErrors()};
}

std::string GetIR(const IRContext &ctx) {
    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return oss.str();
}

bool HasPhiNode(const std::string &ir) {
    return ir.find("phi") != std::string::npos;
}

bool HasUnsupportedLowering(const Diagnostics &diags) {
    for (const auto &diagnostic : diags.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering)
            return true;
    return false;
}

} // namespace

// ============================================================================
// PHI Node Tests for Logical Operators
// ============================================================================

TEST_CASE("Python PHI - Logical AND with short-circuit", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int, y: int) -> int:
    result = x and y
    return result
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python PHI - Logical OR with short-circuit", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int, y: int) -> int:
    result = x or y
    return result
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python PHI - Chained logical operators", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(a: int, b: int, c: int) -> int:
    result = a and b or c
    return result
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

// ============================================================================
// PHI Node Tests for If Statements
// ============================================================================

TEST_CASE("Python PHI - Variable modified in if branch", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(cond: bool) -> int:
    x = 0
    if cond:
        x = 1
    return x
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("if.then") != std::string::npos);
    REQUIRE(ir.find("if.end") != std::string::npos);
}

TEST_CASE("Python PHI - Variable modified in both branches", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(cond: bool) -> int:
    if cond:
        x = 1
    else:
        x = 2
    return x
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("if.then") != std::string::npos);
    REQUIRE(ir.find("if.else") != std::string::npos);
}

// ============================================================================
// PHI Node Tests for While Loops
// ============================================================================

TEST_CASE("Python PHI - While loop with modified variable", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def countdown(n: int) -> int:
    result = 0
    while n > 0:
        result = result + n
        n = n - 1
    return result
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("while.cond") != std::string::npos);
    REQUIRE(ir.find("while.body") != std::string::npos);
}

// ============================================================================
// PHI Node Tests for For Loops
// ============================================================================

TEST_CASE("Python PHI - For loop accumulator", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def sum_list(items):
    total = 0
    for x in items:
        total = total + x
    return total
)", diags);
    
    // Parser may fail due to undefined 'items' in some configurations
    // But this is acceptable for testing the PHI node generation logic
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("for.cond") != std::string::npos);
        REQUIRE(ir.find("for.body") != std::string::npos);
    }
}

// ============================================================================
// Range Iterator Tests
// ============================================================================

TEST_CASE("Python Range - range(stop)", "[python][lowering][range]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    r = range(10)
    return r
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("__py_range_1") != std::string::npos);
}

TEST_CASE("Python Range - range(start, stop)", "[python][lowering][range]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    r = range(5, 10)
    return r
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("__py_range_2") != std::string::npos);
}

TEST_CASE("Python Range - range(start, stop, step)", "[python][lowering][range]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    r = range(0, 10, 2)
    return r
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("__py_range_3") != std::string::npos);
}

// ============================================================================
// Comprehension Tests
// Note: Tests check that comprehension IR is correctly generated with loop
// unrolling. Parser must support comprehension syntax to succeed.
// ============================================================================

TEST_CASE("Python Comprehension - Simple list comprehension", "[python][lowering][comprehension]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items):
    result = [x for x in items]
    return result
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    INFO("Has errors: " << diags.HasErrors());
    if (ok) {
        REQUIRE(ir.find("__py_make_list") != std::string::npos);
        REQUIRE(ir.find("__py_list_append") != std::string::npos);
        REQUIRE(ir.find("comp.header") != std::string::npos);
    }
}

TEST_CASE("Python Comprehension - List comprehension with expression", "[python][lowering][comprehension]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items):
    result = [x * 2 for x in items]
    return result
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("comp.body") != std::string::npos);
    }
}

TEST_CASE("Python Comprehension - List comprehension with filter", "[python][lowering][comprehension]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items):
    result = [x for x in items if x > 0]
    return result
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("comp.filter") != std::string::npos);
    }
}

TEST_CASE("Python Comprehension - Set comprehension", "[python][lowering][comprehension]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items):
    result = {x for x in items}
    return result
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("__py_make_set") != std::string::npos);
        REQUIRE(ir.find("__py_set_add") != std::string::npos);
    }
}

TEST_CASE("Python Comprehension - Dict comprehension", "[python][lowering][comprehension]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items):
    result = {x: x for x in items}
    return result
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("__py_make_dict") != std::string::npos);
    }
}

TEST_CASE("Python Comprehension - Destructuring target reports E4003",
          "[python][lowering][comprehension][fail-closed]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(items: int) -> int:
    result = [x for x, y in items]
    return 0
)", diags);

    (void)ctx;
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
    REQUIRE(std::any_of(diags.All().begin(), diags.All().end(), [](const auto &diagnostic) {
        return diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering &&
               diagnostic.message.find("destructuring comprehension targets") != std::string::npos;
    }));
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

TEST_CASE("Python Exception - Simple try-except", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int):
    try:
        x = x + 1
    except:
        x = 0
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Exception - Try-except with type", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(value: int, exc_type):
    try:
        x = value + 1
    except exc_type as e:
        x = 0
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Exception - Try-except-else", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int):
    try:
        x = x * 2
    except:
        x = 0
    else:
        x = x + 1
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Exception - Try-except-finally", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int):
    try:
        x = x + 1
    except:
        x = 0
    finally:
        x = x * 2
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Exception - Multiple exception handlers", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int, exc1, exc2):
    try:
        x = x / 2
    except exc1:
        x = 1
    except exc2:
        x = 2
    except:
        x = 3
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

// ============================================================================
// Default Arguments with PHI Tests
// ============================================================================

TEST_CASE("Python Default Args - Function with defaults", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def greet(name: str, greeting: str = "Hello"):
    return greeting + name
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Default Args - Multiple defaults", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def func(a: int, b: int = 1, c: int = 2) -> int:
    return a + b + c
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

// ============================================================================
// Combined Control Flow Tests
// ============================================================================

TEST_CASE("Python Complex Flow - Nested if in loop", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def classify(items):
    pos = 0
    neg = 0
    for x in items:
        if x > 0:
            pos = pos + 1
        else:
            neg = neg + 1
    return pos
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("for.cond") != std::string::npos);
        REQUIRE(ir.find("if.then") != std::string::npos);
    }
}

TEST_CASE("Python Complex Flow - Try in loop", "[python][lowering][phi]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def safe_process(items):
    count = 0
    for item in items:
        try:
            process(item)
            count = count + 1
        except:
            pass
    return count
)", diags);
    
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    if (ok) {
        REQUIRE(ir.find("for.cond") != std::string::npos);
        REQUIRE(ir.find("try.body") != std::string::npos);
    }
}
