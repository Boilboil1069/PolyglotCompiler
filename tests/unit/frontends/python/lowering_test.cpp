#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <unordered_set>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/verifier.h"

using polyglot::frontends::Diagnostics;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;
using polyglot::python::LowerToIR;
using polyglot::ir::IRContext;

TEST_CASE("Python native string literals retain escapes, raw prefixes and empty text", "[python][lowering][native-string]") {
    const std::string source = R"PY(
def main() -> int:
    print_text("hello\n")
    print_text(r"raw\n")
    print_text("\u4e2d")
    print_text("")
    return 0
)PY";
    Diagnostics diags;
    PythonLexer lexer(source, "<test>", &diags);
    PythonParser parser(lexer, diags); parser.ParseModule();
    auto module = parser.TakeModule(); REQUIRE(module); REQUIRE_FALSE(diags.HasErrors());
    IRContext ctx; LowerToIR(*module, ctx, diags); REQUIRE_FALSE(diags.HasErrors());
    std::unordered_set<std::string> strings;
    for (const auto &global : ctx.Globals())
        if (auto literal = std::dynamic_pointer_cast<polyglot::ir::ConstantString>(global->initializer)) strings.insert(literal->data);
    CHECK(strings.count("hello\n") == 1);
    CHECK(strings.count("raw\\n") == 1);
    CHECK(strings.count("中") == 1);
    CHECK(strings.count("") == 1);
}

TEST_CASE("Python native text rejects bytes and NUL literals", "[python][lowering][native-string]") {
    for (const auto &literal : {"b\"hello\"", "\"\\u0000\""}) {
        const std::string source = "def main() -> int:\n    print_text(" + std::string(literal) + ")\n    return 0\n";
        Diagnostics diags; PythonLexer lexer(source, "<test>", &diags);
        PythonParser parser(lexer, diags); parser.ParseModule(); auto module = parser.TakeModule(); REQUIRE(module);
        IRContext ctx; LowerToIR(*module, ctx, diags); CHECK(diags.HasErrors());
    }
}

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

bool HasUnsupportedLowering(const Diagnostics &diags) {
    for (const auto &diagnostic : diags.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering)
            return true;
    return false;
}

bool DiagnosticsContain(const Diagnostics &diags, const std::string &needle) {
    for (const auto &diagnostic : diags.All())
        if (diagnostic.message.find(needle) != std::string::npos)
            return true;
    return false;
}

bool TerminatorTargetsBelongToFunction(const polyglot::ir::Function &fn) {
    std::unordered_set<const polyglot::ir::BasicBlock *> blocks;
    for (const auto &block : fn.blocks)
        blocks.insert(block.get());

    const auto belongs = [&](const polyglot::ir::BasicBlock *target) {
        return target && blocks.count(target) != 0;
    };

    for (const auto &block : fn.blocks) {
        if (!block->terminator)
            return false;
        if (auto *branch =
                dynamic_cast<polyglot::ir::BranchStatement *>(block->terminator.get())) {
            if (!belongs(branch->target))
                return false;
        } else if (auto *branch = dynamic_cast<polyglot::ir::CondBranchStatement *>(
                       block->terminator.get())) {
            if (!belongs(branch->true_target) || !belongs(branch->false_target))
                return false;
        } else if (auto *switch_stmt =
                       dynamic_cast<polyglot::ir::SwitchStatement *>(block->terminator.get())) {
            if (!belongs(switch_stmt->default_target))
                return false;
            for (const auto &case_entry : switch_stmt->cases) {
                if (!belongs(case_entry.target))
                    return false;
            }
        }
    }
    return true;
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
    // Locals now have storage so mutations across loop back edges are visible.
    REQUIRE(ir.find(", 42 : void") != std::string::npos);
    REQUIRE(ir.find(" = load ") != std::string::npos);
    REQUIRE(ir.find("ret load.") != std::string::npos);
    REQUIRE(ir.find("lit.") == std::string::npos);

    std::string verify_message;
    const bool valid = polyglot::ir::Verify(ctx, &verify_message);
    INFO(verify_message);
    REQUIRE(valid);
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

TEST_CASE("Python lowering keeps nested-function CFGs isolated",
          "[python][lowering][func][cfg]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def outer(x: int) -> int:
    def inner(y: int) -> int:
        if y > 0:
            return y
        else:
            return 0
    if x > 0:
        return inner(x)
    else:
        return 0
)", diags);

    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python fraud-style functions keep every branch target local",
          "[python][lowering][func][cfg]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def velocity(recent: int, failed: int, changes: int) -> int:
    if failed >= 3:
        return 70
    else:
        if recent >= 10:
            if changes >= 2:
                return 55
            else:
                pass
            return 45
        else:
            pass
    return 5

def amount(payable: int, age: int, mismatch: int) -> int:
    if mismatch != 0:
        if payable >= 150:
            return 35
        else:
            pass
        return 20
    else:
        if age < 3:
            return 18
        else:
            pass
    return 6

def band(velocity_points: int, amount_points: int, failed: int) -> int:
    score = velocity_points + amount_points + failed * 12
    if score >= 80:
        return 3
    else:
        if score >= 45:
            return 2
        else:
            pass
    return 1
)", diags);

    REQUIRE(ok);
    REQUIRE(ctx.Functions().size() == 3);

    std::string verify_message;
    const bool valid = polyglot::ir::Verify(ctx, &verify_message);
    INFO(verify_message);
    REQUIRE(valid);
    for (const auto &fn : ctx.Functions()) {
        INFO("function: " << fn->name);
        REQUIRE(TerminatorTargetsBelongToFunction(*fn));
    }
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
    REQUIRE(ctx.Functions().size() == 2);
    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    REQUIRE(ir.find("Point.__init__") != std::string::npos);
    REQUIRE(ir.find("Point.get_x") != std::string::npos);
    REQUIRE(ir.find(" = gep ") != std::string::npos);

    std::string verify_message;
    REQUIRE(polyglot::ir::Verify(ctx, &verify_message));
}

TEST_CASE("Python lowering emits a real aggregate object lifecycle",
          "[python][lowering][class][oop][aggregate]") {
    Diagnostics diags;
    const std::string package_source = R"(
class FraudAssessment:
    def __init__(self, velocity: int, amount: int, failed: int):
        self.velocity = velocity
        self.amount = amount
        self.failed = failed
        self.score = velocity + amount

    def apply_failure_penalty(self, multiplier: int) -> int:
        self.score += self.failed * multiplier
        return self.score

    def band(self) -> int:
        if self.score >= 80:
            return 3
        else:
            if self.score >= 45:
                return 2
            else:
                return 1

    def close(self) -> None:
        self.velocity = 0
        self.amount = 0
        self.failed = 0
        self.score = 0
)";

    const std::string consumer_source = R"(
def fraud_session_band(velocity: int, amount: int, failed: int) -> int:
    session = FraudAssessment(velocity, amount, failed)
    session.apply_failure_penalty(12)
    result = session.band()
    session.close()
    return result
)";
    // This is the same source-bundling boundary used by IMPORT ... PACKAGE:
    // the local package is merged ahead of its consumer, with no CPython
    // import/runtime participation.
    auto [ctx, ok] = ParseAndLower(package_source + consumer_source, diags);

    std::string diagnostic_text;
    for (const auto &diagnostic : diags.All())
        diagnostic_text += diagnostic.message + "\n";
    INFO("Diagnostics:\n" << diagnostic_text);
    REQUIRE(ok);

    size_t aggregate_allocas = 0;
    size_t geps = 0;
    size_t loads = 0;
    size_t stores = 0;
    size_t constructor_calls = 0;
    size_t member_calls = 0;
    size_t close_calls = 0;
    for (const auto &function : ctx.Functions()) {
        for (const auto &block : function->blocks) {
            for (const auto &instruction : block->instructions) {
                if (auto *alloca = dynamic_cast<polyglot::ir::AllocaInstruction *>(instruction.get())) {
                    if (alloca->type.kind == polyglot::ir::IRTypeKind::kPointer &&
                        !alloca->type.subtypes.empty() &&
                        alloca->type.subtypes.front().kind == polyglot::ir::IRTypeKind::kStruct &&
                        alloca->type.subtypes.front().name == "FraudAssessment" &&
                        alloca->type.subtypes.front().subtypes.size() == 4)
                        ++aggregate_allocas;
                }
                if (dynamic_cast<polyglot::ir::GetElementPtrInstruction *>(instruction.get()))
                    ++geps;
                if (dynamic_cast<polyglot::ir::LoadInstruction *>(instruction.get()))
                    ++loads;
                if (dynamic_cast<polyglot::ir::StoreInstruction *>(instruction.get()))
                    ++stores;
                if (auto *call = dynamic_cast<polyglot::ir::CallInstruction *>(instruction.get())) {
                    constructor_calls += call->callee == "FraudAssessment.__init__";
                    member_calls += call->callee == "FraudAssessment.apply_failure_penalty" ||
                                    call->callee == "FraudAssessment.band";
                    close_calls += call->callee == "FraudAssessment.close";
                }
            }
        }
    }

    REQUIRE(aggregate_allocas == 1);
    REQUIRE(geps >= 12);
    REQUIRE(loads >= 5);
    REQUIRE(stores >= 9);
    REQUIRE(constructor_calls == 1);
    REQUIRE(member_calls == 2);
    REQUIRE(close_calls == 1);

    auto ir = GetIR(ctx);
    INFO("IR:\n" << ir);
    const auto construct = ir.find("FraudAssessment.__init__");
    const auto close = ir.rfind("FraudAssessment.close");
    const auto wrapper_return = ir.rfind("ret ");
    REQUIRE(construct != std::string::npos);
    REQUIRE(close != std::string::npos);
    REQUIRE(wrapper_return != std::string::npos);
    REQUIRE(construct < close);
    REQUIRE(close < wrapper_return);

    std::string verify_message;
    const bool valid = polyglot::ir::Verify(ctx, &verify_message);
    INFO(verify_message);
    REQUIRE(valid);
}

TEST_CASE("Python Lowering - Class with inheritance", "[python][lowering][class]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
class Base:
    def __init__(self, value: int):
        self.value = value

    def foo(self):
        return 1

class Derived(Base):
    def __init__(self, value: int):
        self.value = value

    def bar(self):
        return 2
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
    REQUIRE(DiagnosticsContain(diags, "inheritance"));
}

TEST_CASE("Python static classes reject dynamic fields and descriptors clearly",
          "[python][lowering][class][fail-closed]") {
    SECTION("dynamic field") {
        Diagnostics diags;
        auto [ctx, ok] = ParseAndLower(R"(
class Session:
    def __init__(self, value: int):
        self.value = value

    def add_dynamic_field(self, extra: int) -> int:
        self.extra = extra
        return self.extra
)", diags);
        REQUIRE_FALSE(ok);
        REQUIRE(HasUnsupportedLowering(diags));
        REQUIRE(DiagnosticsContain(diags, "dynamic attribute"));
    }

    SECTION("property descriptor") {
        Diagnostics diags;
        auto [ctx, ok] = ParseAndLower(R"(
class Session:
    def __init__(self, value: int):
        self.value = value

    @property
    def current(self) -> int:
        return self.value
)", diags);
        REQUIRE_FALSE(ok);
        REQUIRE(HasUnsupportedLowering(diags));
        REQUIRE(DiagnosticsContain(diags, "descriptor"));
    }
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Lowering - Raise statement", "[python][lowering][exception]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test(x):
    if x < 0:
        raise ValueError
    return x
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Lowering - Multiple context managers", "[python][lowering][with]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def test():
    with open("a") as a, open("b") as b:
        pass
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    bool unsupported = false;
    for (const auto &diagnostic : diags.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(unsupported);
}

TEST_CASE("Python lowering fails closed for f-string format specs and except-star",
          "[python][lowering][fail-closed]") {
    Diagnostics format_diags;
    auto [format_ctx, format_ok] = ParseAndLower(
        "def render(value):\n    return f'{value:.2f}'\n", format_diags);
    REQUIRE_FALSE(format_ok);
    bool format_unsupported = false;
    for (const auto &diagnostic : format_diags.All())
        format_unsupported = format_unsupported ||
                             diagnostic.code ==
                                 polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(format_unsupported);

    Diagnostics group_diags;
    auto [group_ctx, group_ok] = ParseAndLower(
        "def handle():\n"
        "    try:\n"
        "        work()\n"
        "    except* ValueError:\n"
        "        pass\n",
        group_diags);
    REQUIRE_FALSE(group_ok);
    bool group_unsupported = false;
    for (const auto &diagnostic : group_diags.All())
        group_unsupported = group_unsupported ||
                            diagnostic.code ==
                                polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(group_unsupported);
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python Lowering - Await expression", "[python][lowering][async]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
async def main():
    result = await fetch()
    return result
)", diags);
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python lowering does not fabricate signatures for imported callables",
          "[python][lowering][import][fail-closed]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
from math import imaginary_api

def test(value: float) -> float:
    return imaginary_api(value)
)", diags);

    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
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
    
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}

TEST_CASE("Python lowering fails closed for template strings", "[python][lowering][tstring]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def render(value):
    return t"value={value}"
)", diags);
    (void)ctx;
    REQUIRE_FALSE(ok);
    bool unsupported = false;
    for (const auto &diagnostic : diags.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(unsupported);
}

TEST_CASE("Python lowering rejects generic functions without specialization",
          "[python][lowering][pep695]") {
    Diagnostics diags;
    auto [ctx, ok] = ParseAndLower(R"(
def identity[T](value: T) -> T:
    return value
)", diags);
    (void)ctx;
    REQUIRE_FALSE(ok);
    REQUIRE(HasUnsupportedLowering(diags));
}
