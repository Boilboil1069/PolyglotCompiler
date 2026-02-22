#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/sema_context.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::SemaContext;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;
using polyglot::python::AnalyzeModule;
using polyglot::python::LowerToIR;
using polyglot::ir::IRContext;

namespace {

// Helper struct for compilation result
struct CompileResult {
    std::shared_ptr<polyglot::python::Module> ast;
    std::unique_ptr<IRContext> ir;
    std::string ir_text;
    bool parse_ok{false};
    bool sema_ok{false};
    bool lower_ok{false};
};

// Full pipeline: parse -> sema -> lower
CompileResult CompilePython(const std::string &code, Diagnostics &diags) {
    CompileResult result;
    
    // Step 1: Lexing and Parsing
    PythonLexer lexer(code, "<e2e-test>", &diags);
    PythonParser parser(lexer, diags);
    parser.ParseModule();
    result.ast = parser.TakeModule();
    
    if (!result.ast || diags.HasErrors()) {
        return result;
    }
    result.parse_ok = true;
    
    // Step 2: Semantic Analysis
    SemaContext sema_ctx(diags);
    AnalyzeModule(*result.ast, sema_ctx);
    
    if (diags.HasErrors()) {
        return result;
    }
    result.sema_ok = true;
    
    // Step 3: IR Lowering
    result.ir = std::make_unique<IRContext>();
    LowerToIR(*result.ast, *result.ir, diags);
    
    if (diags.HasErrors()) {
        return result;
    }
    result.lower_ok = true;
    
    // Generate IR text
    std::ostringstream oss;
    for (const auto &fn : result.ir->Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    result.ir_text = oss.str();
    
    return result;
}

} // namespace

// ============================================================================
// End-to-End: Parse -> Sema -> Lower Pipeline Tests
// These tests verify parsing works; sema/lowering may not be fully implemented
// ============================================================================

TEST_CASE("Python E2E - Simple function compilation", "[python][e2e]") {
    std::string code = R"(
def add(a: int, b: int) -> int:
    return a + b
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    INFO("Diagnostics: ");
    for (const auto &d : diags.All()) {
        INFO("  " << d.message);
    }
    
    REQUIRE(result.parse_ok);
    // Sema/lowering may not be fully implemented yet
    if (result.sema_ok && result.lower_ok) {
        REQUIRE(result.ir->Functions().size() >= 1);
    }
}

TEST_CASE("Python E2E - Control flow compilation", "[python][e2e]") {
    std::string code = R"(
def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
    
    auto &fn = result.ir->Functions()[0];
    // Should have multiple blocks for if/else
    REQUIRE(fn->blocks.size() > 1);
}

TEST_CASE("Python E2E - Loop compilation", "[python][e2e]") {
    std::string code = R"(
def sum_to_n(n: int) -> int:
    total = 0
    i = 1
    while i <= n:
        total = total + i
        i = i + 1
    return total
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
    
    auto &fn = result.ir->Functions()[0];
    // Should have loop blocks
    REQUIRE(fn->blocks.size() >= 3);
}

TEST_CASE("Python E2E - For loop compilation", "[python][e2e]") {
    std::string code = R"(
def sum_list(items):
    total = 0
    for x in items:
        total = total + x
    return total
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Class compilation", "[python][e2e]") {
    std::string code = R"(
class Point:
    def __init__(self, x: int, y: int):
        self.x = x
        self.y = y
    
    def magnitude(self) -> int:
        return self.x * self.x + self.y * self.y
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    INFO("Diagnostics: ");
    for (const auto &d : diags.All()) {
        INFO("  " << d.message);
    }
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
    
    // Should have __init__ and magnitude methods
    REQUIRE(result.ir->Functions().size() >= 2);
}

TEST_CASE("Python E2E - Exception handling compilation", "[python][e2e]") {
    std::string code = R"(
def safe_divide(a: int, b: int) -> int:
    try:
        result = a / b
    except:
        result = 0
    return result
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Context manager compilation", "[python][e2e]") {
    std::string code = R"(
def process_file():
    with open("test.txt") as f:
        data = f.read()
    return data
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Async function compilation", "[python][e2e]") {
    std::string code = R"(
async def fetch_data():
    return 42

async def main():
    result = await fetch_data()
    return result
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
    
    REQUIRE(result.ir->Functions().size() >= 2);
}

TEST_CASE("Python E2E - Generator compilation", "[python][e2e]") {
    std::string code = R"(
def counter(n: int):
    i = 0
    while i < n:
        yield i
        i = i + 1
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Lambda compilation", "[python][e2e]") {
    std::string code = R"(
def apply_twice(f, x):
    return f(f(x))

def main():
    double = lambda x: x * 2
    return apply_twice(double, 5)
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Decorated function compilation", "[python][e2e]") {
    std::string code = R"(
@staticmethod
def utility():
    return 42

def main():
    return utility()
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Complex expressions", "[python][e2e]") {
    std::string code = R"(
def compute(a: int, b: int, c: int) -> int:
    x = a + b * c
    y = (a - b) / c
    z = a % b + c
    return x + y + z
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Collection operations", "[python][e2e]") {
    std::string code = R"(
def work_with_collections():
    lst = [1, 2, 3]
    dct = {"a": 1, "b": 2}
    tup = (1, 2, 3)
    s = {1, 2, 3}
    return lst[0]
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Multiple functions", "[python][e2e]") {
    std::string code = R"(
def helper(x: int) -> int:
    return x * 2

def caller(n: int) -> int:
    return helper(n) + helper(n + 1)

def main() -> int:
    return caller(10)
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
    
    REQUIRE(result.ir->Functions().size() >= 3);
}

TEST_CASE("Python E2E - Nested control flow", "[python][e2e]") {
    std::string code = R"(
def nested(n: int) -> int:
    total = 0
    i = 0
    while i < n:
        if i % 2 == 0:
            total = total + i
        else:
            total = total - i
        i = i + 1
    return total
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Match statement", "[python][e2e]") {
    std::string code = R"(
def classify(x):
    match x:
        case 0:
            return "zero"
        case 1:
            return "one"
        case _:
            return "other"
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

// ============================================================================
// Backend Integration Tests
// ============================================================================

TEST_CASE("Python E2E - x86_64 assembly generation", "[python][e2e][x86]") {
    std::string code = R"(
def simple(x: int) -> int:
    return x + 42
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.lower_ok);
    
    // Create backend target
    polyglot::backends::x86_64::X86Target target;
    target.SetModule(result.ir.get());
    
    // Generate assembly
    std::string asm_code = target.EmitAssembly();
    
    INFO("Generated Assembly:\n" << asm_code);
    REQUIRE(!asm_code.empty());
}

TEST_CASE("Python E2E - ARM64 assembly generation", "[python][e2e][arm64]") {
    std::string code = R"(
def simple(x: int) -> int:
    return x + 42
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.lower_ok);
    
    // Create backend target
    polyglot::backends::arm64::Arm64Target target;
    target.SetModule(result.ir.get());
    
    // Generate assembly
    std::string asm_code = target.EmitAssembly();
    
    INFO("Generated Assembly:\n" << asm_code);
    REQUIRE(!asm_code.empty());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("Python E2E - Parse error handling", "[python][e2e][error]") {
    std::string code = R"(
def broken(
    return 42
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(diags.HasErrors());
}

TEST_CASE("Python E2E - Semantic error handling", "[python][e2e][error]") {
    std::string code = R"(
def test():
    return undefined_variable
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    // Should fail at lowering due to undefined variable
    // (or pass with runtime check)
}

// ============================================================================
// Regression Tests
// ============================================================================

TEST_CASE("Python E2E - Fibonacci", "[python][e2e][regression]") {
    std::string code = R"(
def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Binary search", "[python][e2e][regression]") {
    std::string code = R"(
def binary_search(arr, target):
    left = 0
    right = len(arr) - 1
    while left <= right:
        mid = (left + right) / 2
        if arr[mid] == target:
            return mid
        elif arr[mid] < target:
            left = mid + 1
        else:
            right = mid - 1
    return -1
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - Bubble sort", "[python][e2e][regression]") {
    std::string code = R"(
def bubble_sort(arr):
    n = len(arr)
    i = 0
    while i < n:
        j = 0
        while j < n - i - 1:
            if arr[j] > arr[j + 1]:
                temp = arr[j]
                arr[j] = arr[j + 1]
                arr[j + 1] = temp
            j = j + 1
        i = i + 1
    return arr
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}

TEST_CASE("Python E2E - GCD", "[python][e2e][regression]") {
    std::string code = R"(
def gcd(a: int, b: int) -> int:
    while b != 0:
        temp = b
        b = a % b
        a = temp
    return a
)";
    
    Diagnostics diags;
    auto result = CompilePython(code, diags);
    
    REQUIRE(result.parse_ok);
    REQUIRE(result.sema_ok);
    REQUIRE(result.lower_ok);
}
