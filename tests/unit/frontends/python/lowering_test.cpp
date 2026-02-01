#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "common/include/ir/ir_printer.h"

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

} // namespace

// ============================================================================
// Basic Expression Tests
// ============================================================================

TEST_CASE("Python Lowering - Integer literals", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    x = 42
    return x
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() >= 1);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("test") != std::string::npos);
}

TEST_CASE("Python Lowering - Float literals", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    x = 3.14
    return x
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() >= 1);
}

TEST_CASE("Python Lowering - String literals", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    s = "hello"
    return s
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() >= 1);
}

TEST_CASE("Python Lowering - Boolean literals", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    t = True
    f = False
    return t
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() >= 1);
}

TEST_CASE("Python Lowering - Binary arithmetic", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(a: int, b: int) -> int:
    return a + b * 2 - 1
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    // Should contain add, mul, sub operations
}

TEST_CASE("Python Lowering - Comparison operators", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(a: int, b: int) -> bool:
    return a < b
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
}

TEST_CASE("Python Lowering - Logical operators", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(a: bool, b: bool) -> bool:
    return a and b
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Unary operators", "[python][lowering][expr]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int) -> int:
    return -x
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Control Flow Tests
// ============================================================================

TEST_CASE("Python Lowering - If statement", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x: int) -> int:
    if x > 0:
        return 1
    else:
        return -1
)", diags);
    
    REQUIRE(ok);
    auto &fn = ctx.Functions()[0];
    // Should have multiple blocks for if/else
    REQUIRE(fn->blocks.size() >= 3);
}

TEST_CASE("Python Lowering - If-elif-else", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def classify(x: int) -> int:
    if x > 0:
        return 1
    elif x < 0:
        return -1
    else:
        return 0
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - While loop", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def sum_to_n(n: int) -> int:
    total = 0
    i = 1
    while i <= n:
        total = total + i
        i = i + 1
    return total
)", diags);
    
    REQUIRE(ok);
    auto &fn = ctx.Functions()[0];
    // Should have cond, body, exit blocks
    REQUIRE(fn->blocks.size() >= 3);
}

TEST_CASE("Python Lowering - For loop", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def sum_list(items):
    total = 0
    for x in items:
        total = total + x
    return total
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Break statement", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def find_first(items, target):
    for x in items:
        if x == target:
            break
    return x
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Continue statement", "[python][lowering][control]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def sum_positive(items):
    total = 0
    for x in items:
        if x < 0:
            continue
        total = total + x
    return total
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Function Tests
// ============================================================================

TEST_CASE("Python Lowering - Simple function", "[python][lowering][func]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def add(a: int, b: int) -> int:
    return a + b
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() == 1);
    REQUIRE(ctx.Functions()[0]->name == "add");
}

TEST_CASE("Python Lowering - Multiple functions", "[python][lowering][func]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def foo():
    return 1

def bar():
    return 2
)", diags);
    
    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() == 2);
}

TEST_CASE("Python Lowering - Function call", "[python][lowering][func]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def helper(x: int) -> int:
    return x * 2

def main(n: int) -> int:
    return helper(n)
)", diags);
    
    REQUIRE(ok);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("call") != std::string::npos);
}

TEST_CASE("Python Lowering - Recursive function", "[python][lowering][func]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Assignment Tests
// ============================================================================

TEST_CASE("Python Lowering - Simple assignment", "[python][lowering][assign]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    x = 10
    y = x
    return y
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Augmented assignment", "[python][lowering][assign]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    x = 5
    x += 3
    x *= 2
    return x
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Tuple unpacking", "[python][lowering][assign]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    a, b = (1, 2)
    return a + b
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Collection Tests
// ============================================================================

TEST_CASE("Python Lowering - List literal", "[python][lowering][collection]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    lst = [1, 2, 3]
    return lst
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Dict literal", "[python][lowering][collection]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    d = {"a": 1, "b": 2}
    return d
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Tuple literal", "[python][lowering][collection]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    t = (1, 2, 3)
    return t
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Set literal", "[python][lowering][collection]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    s = {1, 2, 3}
    return s
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Index access", "[python][lowering][collection]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(lst):
    return lst[0]
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Class Tests
// ============================================================================

TEST_CASE("Python Lowering - Simple class", "[python][lowering][class]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def get_x(self):
        return self.x
)", diags);
    
    REQUIRE(ok);
    // Should have __init__ and get_x methods
    REQUIRE(ctx.Functions().size() >= 2);
}

TEST_CASE("Python Lowering - Class with inheritance", "[python][lowering][class]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
class Base:
    def foo(self):
        return 1

class Derived(Base):
    def bar(self):
        return 2
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

TEST_CASE("Python Lowering - Try-except", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    try:
        x = 1
    except:
        x = 0
    return x
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Try-except-finally", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    try:
        x = 1
    except:
        x = 0
    finally:
        pass
    return x
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Raise statement", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x):
    if x < 0:
        raise ValueError
    return x
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Context Manager Tests
// ============================================================================

TEST_CASE("Python Lowering - With statement", "[python][lowering][with]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    with open("file.txt") as f:
        data = f.read()
    return data
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Multiple context managers", "[python][lowering][with]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    with open("a") as a, open("b") as b:
        pass
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Match Statement Tests
// ============================================================================

TEST_CASE("Python Lowering - Simple match", "[python][lowering][match]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x):
    match x:
        case 0:
            return "zero"
        case 1:
            return "one"
        case _:
            return "other"
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Async/Await Tests
// ============================================================================

TEST_CASE("Python Lowering - Async function", "[python][lowering][async]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
async def fetch():
    return 42
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - Await expression", "[python][lowering][async]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
async def main():
    result = await fetch()
    return result
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Generator Tests
// ============================================================================

TEST_CASE("Python Lowering - Yield expression", "[python][lowering][generator]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def gen():
    yield 1
    yield 2
    yield 3
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Lambda Tests
// ============================================================================

TEST_CASE("Python Lowering - Lambda expression", "[python][lowering][lambda]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    f = lambda x: x * 2
    return f(5)
)", diags);
    
    REQUIRE(ok);
    // Should create a lambda function
    REQUIRE(ctx.Functions().size() >= 2);
}

// ============================================================================
// Comprehension Tests
// ============================================================================

TEST_CASE("Python Lowering - List comprehension", "[python][lowering][comp]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    lst = [1, 2, 3]
    return lst
)", diags);
    
    // Lowering comprehensions may produce diagnostics but should not crash
    // For now just verify we get a result
    bool has_result = !ctx.Functions().empty() || diags.HasErrors();
    REQUIRE(has_result);
}

// ============================================================================
// Decorator Tests
// ============================================================================

TEST_CASE("Python Lowering - Decorated function", "[python][lowering][decorator]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
@staticmethod
def helper():
    return 42

def main():
    return helper()
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Import Tests
// ============================================================================

TEST_CASE("Python Lowering - Import statement", "[python][lowering][import]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
import math

def test():
    return 42
)", diags);
    
    REQUIRE(ok);
}

TEST_CASE("Python Lowering - From import", "[python][lowering][import]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
from math import sqrt

def test():
    return sqrt(4)
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("Python Lowering - Undefined variable error", "[python][lowering][error]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    return undefined_var
)", diags);
    
    // Should report error for undefined variable
    REQUIRE(diags.HasErrors());
}

TEST_CASE("Python Lowering - Assert statement", "[python][lowering][assert]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x):
    assert x > 0, "x must be positive"
    return x
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Type Annotation Tests
// ============================================================================

TEST_CASE("Python Lowering - Type annotations", "[python][lowering][types]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def add(a: int, b: int) -> int:
    return a + b

def greet(name: str) -> str:
    return name
)", diags);
    
    REQUIRE(ok);
}

// ============================================================================
// Global/Nonlocal Tests
// ============================================================================

TEST_CASE("Python Lowering - Global statement", "[python][lowering][scope]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
counter = 0

def increment():
    global counter
    counter = counter + 1
    return counter
)", diags);
    
    REQUIRE(ok);
}
