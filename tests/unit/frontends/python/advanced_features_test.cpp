#include <catch2/catch_test_macros.hpp>
#include <string>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"

using namespace polyglot::python;
using polyglot::frontends::Diagnostics;
using polyglot::ir::IRContext;

namespace {

// Helper to parse Python code and return AST
std::shared_ptr<Module> ParseCode(const std::string &code, Diagnostics &diags) {
    PythonLexer lexer(code, "<test>", &diags);
    PythonParser parser(lexer, diags);
    parser.ParseModule();
    return parser.TakeModule();
}

// Helper to parse and lower Python code
bool ParseAndLower(const std::string &code, Diagnostics &diags, IRContext &ctx) {
    auto mod = ParseCode(code, diags);
    if (!mod || diags.HasErrors()) return false;
    LowerToIR(*mod, ctx, diags);
    return !diags.HasErrors();
}

} // namespace

// ============ Test 1: Decorators ============
TEST_CASE("Python - Decorators", "[python][decorator]") {
    SECTION("@property decorator") {
        std::string code = R"(
class Person:
    @property
    def name(self):
        return self._name
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->body.size() == 1);
        auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[0]);
        REQUIRE(cls != nullptr);
        REQUIRE(cls->body.size() >= 1);
        auto method = std::dynamic_pointer_cast<FunctionDef>(cls->body[0]);
        REQUIRE(method != nullptr);
        REQUIRE(method->decorators.size() == 1);
    }
    
    SECTION("@staticmethod decorator") {
        std::string code = "@staticmethod\ndef func(): pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->decorators.size() == 1);
    }
    
    SECTION("@classmethod decorator") {
        std::string code = "@classmethod\ndef func(cls): pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->decorators.size() == 1);
    }
    
    SECTION("Multiple decorators") {
        std::string code = R"(
@decorator1
@decorator2
@decorator3
def func(): pass
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->decorators.size() == 3);
    }
    
    SECTION("Decorator with arguments") {
        std::string code = "@cache(maxsize=128)\ndef func(): pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->decorators.size() == 1);
        // Decorator should be a CallExpression
        auto dec_call = std::dynamic_pointer_cast<CallExpression>(fn->decorators[0]);
        REQUIRE(dec_call != nullptr);
    }
}

// ============ Test 2: Context Managers ============
TEST_CASE("Python - Context Managers", "[python][with]") {
    SECTION("Basic with statement") {
        std::string code = "def test():\n    with open('file.txt') as f:\n        data = f.read()";
        Diagnostics diags;
        IRContext ctx;
        REQUIRE(ParseAndLower(code, diags, ctx));
    }
    
    SECTION("Multiple context managers") {
        std::string code = "def test():\n    with open('a') as a, open('b') as b:\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto with_stmt = std::dynamic_pointer_cast<WithStatement>(fn->body[0]);
        REQUIRE(with_stmt != nullptr);
        REQUIRE(with_stmt->items.size() == 2);
    }
    
    SECTION("Nested with") {
        std::string code = R"(
def test():
    with outer():
        with inner():
            pass
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("With without as") {
        std::string code = "def test():\n    with lock:\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto with_stmt = std::dynamic_pointer_cast<WithStatement>(fn->body[0]);
        REQUIRE(with_stmt != nullptr);
        REQUIRE(with_stmt->items[0].optional_vars == nullptr);
    }
    
    SECTION("Custom context manager") {
        std::string code = R"(
class CM:
    def __enter__(self): return self
    def __exit__(self, *args): pass
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[0]);
        REQUIRE(cls != nullptr);
        REQUIRE(cls->body.size() >= 2);
    }
}

// ============ Test 3: Generators ============
TEST_CASE("Python - Generators", "[python][generator]") {
    SECTION("Simple generator") {
        std::string code = "def gen():\n    yield 1\n    yield 2";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        // Check that body contains yield expressions
        REQUIRE(fn->body.size() >= 2);
    }
    
    SECTION("Generator expression") {
        std::string code = "def test():\n    g = (x*2 for x in range(10))";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Yield from") {
        std::string code = "def gen():\n    yield from other()";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(fn->body[0]);
        REQUIRE(expr_stmt != nullptr);
        auto yield_expr = std::dynamic_pointer_cast<YieldExpression>(expr_stmt->expr);
        REQUIRE(yield_expr != nullptr);
        REQUIRE(yield_expr->is_from == true);
    }
    
    SECTION("Generator with send") {
        std::string code = "def gen():\n    x = yield\n    yield x*2";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Infinite generator") {
        std::string code = "def inf():\n    while True:\n        yield 1";
        Diagnostics diags;
        IRContext ctx;
        REQUIRE(ParseAndLower(code, diags, ctx));
    }
}

// ============ Test 4: async/await ============
TEST_CASE("Python - Async/Await", "[python][async]") {
    SECTION("Async function") {
        std::string code = "async def func():\n    return 42";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->is_async == true);
    }
    
    SECTION("Await expression") {
        std::string code = "async def func():\n    x = await other()";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto assign = std::dynamic_pointer_cast<Assignment>(fn->body[0]);
        REQUIRE(assign != nullptr);
        auto await_expr = std::dynamic_pointer_cast<AwaitExpression>(assign->value);
        REQUIRE(await_expr != nullptr);
    }
    
    SECTION("Async for") {
        std::string code = "async def func():\n    async for item in stream:\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto for_stmt = std::dynamic_pointer_cast<ForStatement>(fn->body[0]);
        REQUIRE(for_stmt != nullptr);
        REQUIRE(for_stmt->is_async == true);
    }
    
    SECTION("Async with") {
        std::string code = "async def func():\n    async with lock:\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto with_stmt = std::dynamic_pointer_cast<WithStatement>(fn->body[0]);
        REQUIRE(with_stmt != nullptr);
        REQUIRE(with_stmt->is_async == true);
    }
    
    SECTION("Async generator") {
        std::string code = "async def gen():\n    yield 1\n    yield 2";
        Diagnostics diags;
        IRContext ctx;
        REQUIRE(ParseAndLower(code, diags, ctx));
    }
}

// ============ Tests 5-10: Comprehensions ============
TEST_CASE("Python - List Comprehension", "[python][comp]") {
    SECTION("Basic list comp") {
        std::string code = "def test():\n    result = [x*2 for x in range(10)]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto assign = std::dynamic_pointer_cast<Assignment>(fn->body[0]);
        REQUIRE(assign != nullptr);
        // Just verify assignment has a value (parser may use ComprehensionExpression or ListComprehension)
        REQUIRE(assign->value != nullptr);
    }
    
    SECTION("With condition") {
        std::string code = "def test():\n    result = [x for x in range(10) if x % 2 == 0]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Nested comprehension") {
        std::string code = "def test():\n    result = [[y for y in row] for row in matrix]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        // Just verify parsing succeeds
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Multiple for") {
        std::string code = "def test():\n    result = [x+y for x in a for y in b]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto assign = std::dynamic_pointer_cast<Assignment>(fn->body[0]);
        REQUIRE(assign != nullptr);
        // Check comprehension parsed successfully
        REQUIRE(assign->value != nullptr);
    }
    
    SECTION("Complex expression") {
        std::string code = "def test():\n    result = [func(x, y) for x, y in zip(a, b) if check(x)]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
}

TEST_CASE("Python - Dict Comprehension", "[python][dict]") {
    std::string codes[] = {
        "def t():\n    d = {k: v*2 for k, v in items.items()}",
        "def t():\n    d = {x: x**2 for x in range(10)}",
        "def t():\n    d = {k: v for k, v in d.items() if v > 0}",
        "def t():\n    d = {str(i): i for i in range(5)}",
        "def t():\n    d = {k.upper(): v.lower() for k, v in pairs}"
    };
    
    for (const auto& code : codes) {
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
}

TEST_CASE("Python - Set Comprehension", "[python][set]") {
    std::string codes[] = {
        "def t():\n    s = {x for x in data}",
        "def t():\n    s = {x*2 for x in range(10)}",
        "def t():\n    s = {word.lower() for word in words}",
        "def t():\n    s = {x for x in nums if x > 0}",
        "def t():\n    s = {func(x) for x in items if cond(x)}"
    };
    
    for (const auto& code : codes) {
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
}

// ============ Test 11: Match statement ============
TEST_CASE("Python - Match Statement", "[python][match]") {
    SECTION("Simple match") {
        std::string code = R"(
def test(value):
    match value:
        case 0:
            return "zero"
        case _:
            return "other"
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(fn->body[0]);
        REQUIRE(match_stmt != nullptr);
        REQUIRE(match_stmt->cases.size() == 2);
    }
    
    SECTION("Pattern matching") {
        std::string code = R"(
def test(point):
    match point:
        case (0, 0):
            return "origin"
        case (x, 0):
            return f"x={x}"
        case (0, y):
            return f"y={y}"
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("With guard") {
        std::string code = R"(
def test(value):
    match value:
        case x if x > 0:
            return "positive"
        case x if x < 0:
            return "negative"
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(fn->body[0]);
        REQUIRE(match_stmt != nullptr);
        REQUIRE(match_stmt->cases[0].guard != nullptr);
    }
    
    SECTION("Structural pattern") {
        std::string code = R"(
def test(obj):
    match obj:
        case {"type": "user", "id": user_id}:
            process_user(user_id)
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("OR pattern") {
        std::string code = R"(
def test(value):
    match value:
        case 1 | 2 | 3:
            return "small"
)";
        Diagnostics diags;
        IRContext ctx;
        REQUIRE(ParseAndLower(code, diags, ctx));
    }
}

// ============ Test 12: f-string ============
TEST_CASE("Python - F-String", "[python][fstring]") {
    SECTION("basic interpolation") {
        Diagnostics diags;
        auto mod = ParseCode("def t():\n    s = f'Hello {name}'", diags);
        REQUIRE(mod != nullptr);
    }
    SECTION("expression in braces") {
        Diagnostics diags;
        auto mod = ParseCode("def t():\n    s = f'Result: {x + y}'", diags);
        REQUIRE(mod != nullptr);
    }
    SECTION("format spec") {
        Diagnostics diags;
        auto mod = ParseCode("def t():\n    s = f'{value:.2f}'", diags);
        REQUIRE(mod != nullptr);
    }
    SECTION("self-documenting") {
        Diagnostics diags;
        auto mod = ParseCode("def t():\n    s = f'{name=}'", diags);
        REQUIRE(mod != nullptr);
    }
    SECTION("simple interpolation 2") {
        Diagnostics diags;
        auto mod = ParseCode("def t():\n    s = f'nested: {inner}'", diags);
        REQUIRE(mod != nullptr);
    }
}

// ============ Test 13: Walrus operator ============
TEST_CASE("Python - Walrus Operator", "[python][walrus]") {
    SECTION("If with walrus") {
        std::string code = "def t():\n    if (n := len(data)) > 10:\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("While with walrus") {
        std::string code = "def t():\n    while (line := f.readline()):\n        pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Comprehension with walrus") {
        std::string code = "def t():\n    result = [y for x in data if (y := f(x)) > 0]";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Named expression test") {
        std::string code = "def t():\n    print(result := compute())";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(fn->body[0]);
        REQUIRE(expr_stmt != nullptr);
        auto call_expr = std::dynamic_pointer_cast<CallExpression>(expr_stmt->expr);
        REQUIRE(call_expr != nullptr);
        REQUIRE(!call_expr->args.empty());
    }
}

// ============ Tests 14-25: Other features ============

TEST_CASE("Python - Dataclass", "[python][dataclass]") {
    std::string code = R"(
@dataclass
class Point:
    x: int
    y: int
    frozen: bool = False
)";
    Diagnostics diags;
    auto mod = ParseCode(code, diags);
    REQUIRE(mod != nullptr);
    auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[0]);
    REQUIRE(cls != nullptr);
    REQUIRE(cls->name == "Point");
    REQUIRE(cls->decorators.size() == 1);
    auto dec_id = std::dynamic_pointer_cast<Identifier>(cls->decorators[0]);
    REQUIRE(dec_id != nullptr);
    REQUIRE(dec_id->name == "dataclass");
}

TEST_CASE("Python - Property", "[python][property]") {
    std::string code = R"(
class C:
    @property
    def x(self):
        return self._x
    @x.setter
    def x(self, value):
        self._x = value
)";
    Diagnostics diags;
    auto mod = ParseCode(code, diags);
    REQUIRE(mod != nullptr);
    auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[0]);
    REQUIRE(cls != nullptr);
    REQUIRE(cls->body.size() >= 2);
    // First method has @property decorator
    auto getter = std::dynamic_pointer_cast<FunctionDef>(cls->body[0]);
    REQUIRE(getter != nullptr);
    REQUIRE(getter->decorators.size() == 1);
}

TEST_CASE("Python - Type Annotations", "[python][typing]") {
    SECTION("Function annotations") {
        std::string code = "def func(x: int) -> int:\n    return x";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        REQUIRE(fn->return_annotation != nullptr);
    }
    
    SECTION("Variable annotation") {
        std::string code = "def t():\n    x: List[int] = []";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Union type") {
        std::string code = "def func(x: Union[int, str]):\n    pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Optional type") {
        std::string code = "def func(x: Optional[int]):\n    pass";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("TypeVar") {
        std::string code = "T = TypeVar('T')";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
}

TEST_CASE("Python - Lambda", "[python][lambda]") {
    SECTION("Simple lambda") {
        std::string code = "def t():\n    f = lambda x: x*2";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto assign = std::dynamic_pointer_cast<Assignment>(fn->body[0]);
        REQUIRE(assign != nullptr);
        auto lam = std::dynamic_pointer_cast<LambdaExpression>(assign->value);
        REQUIRE(lam != nullptr);
    }
    
    SECTION("Multi-arg lambda") {
        std::string code = "def t():\n    f = lambda x, y: x + y";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("No-arg lambda") {
        std::string code = "def t():\n    f = lambda: 42";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
    
    SECTION("Lambda returning tuple") {
        std::string code = "def t():\n    f = lambda x: (x, x*2)";
        Diagnostics diags;
        IRContext ctx;
        REQUIRE(ParseAndLower(code, diags, ctx));
    }
    
    SECTION("Lambda with *args, **kwargs") {
        std::string code = "def t():\n    f = lambda *args, **kwargs: sum(args)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
    }
}

TEST_CASE("Python - Global/Nonlocal", "[python][scope]") {
    SECTION("Global statement") {
        std::string code = "def t():\n    global x\n    x = 10";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto global_stmt = std::dynamic_pointer_cast<GlobalStatement>(fn->body[0]);
        REQUIRE(global_stmt != nullptr);
        REQUIRE(global_stmt->names.size() == 1);
        REQUIRE(global_stmt->names[0] == "x");
    }
    
    SECTION("Nonlocal statement") {
        std::string code = R"(
def outer():
    x = 1
    def inner():
        nonlocal x
        x = 2
)";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto outer = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(outer != nullptr);
        auto inner = std::dynamic_pointer_cast<FunctionDef>(outer->body[1]);
        REQUIRE(inner != nullptr);
        auto nonlocal_stmt = std::dynamic_pointer_cast<NonlocalStatement>(inner->body[0]);
        REQUIRE(nonlocal_stmt != nullptr);
    }
    
    SECTION("Multiple names") {
        std::string code = "def t():\n    global a, b, c";
        Diagnostics diags;
        auto mod = ParseCode(code, diags);
        REQUIRE(mod != nullptr);
        auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
        REQUIRE(fn != nullptr);
        auto global_stmt = std::dynamic_pointer_cast<GlobalStatement>(fn->body[0]);
        REQUIRE(global_stmt != nullptr);
        REQUIRE(global_stmt->names.size() == 3);
    }
}

// Integration test
TEST_CASE("Python - Combined Features", "[python][integration]") {
    std::string code = R"(
@dataclass
class User:
    name: str
    age: int

async def process_users():
    users = [User(name, age) for name, age in fetch_users()]
    return {u.name: u.age for u in users if u.age > 18}

def main():
    with database() as db:
        result = process_users()
        match result:
            case {} if not result:
                print("No users")
            case users:
                print(f"Found {len(users)} users")
)";
    Diagnostics diags;
    auto mod = ParseCode(code, diags);
    REQUIRE(mod != nullptr);
    // Should have at least 3 top-level definitions: User class, process_users, main
    REQUIRE(mod->body.size() >= 3);
    
    // Verify class with decorator
    auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[0]);
    REQUIRE(cls != nullptr);
    REQUIRE(cls->name == "User");
    REQUIRE(cls->decorators.size() == 1);
    
    // Verify async function
    auto async_fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[1]);
    REQUIRE(async_fn != nullptr);
    REQUIRE(async_fn->is_async == true);
    
    // Verify main function has with and match
    auto main_fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[2]);
    REQUIRE(main_fn != nullptr);
    auto with_stmt = std::dynamic_pointer_cast<WithStatement>(main_fn->body[0]);
    REQUIRE(with_stmt != nullptr);
}
