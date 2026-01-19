#include <catch2/catch_test_macros.hpp>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

TEST_CASE("Rust parser parses closure with types and move", "[rust][parser]") {
    const char *src = R"(
fn main() {
  let c = move |x: &i32, y: i32| x;
  let s = | | 1;
}
)";
    Diagnostics diag;
    RustLexer lexer(src, "<mem>");
    RustParser parser(lexer, diag);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    REQUIRE(mod);
    auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
    REQUIRE(fn);
    REQUIRE(fn->body.size() == 2);
    auto let0 = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
    REQUIRE(let0);
    auto clos = std::dynamic_pointer_cast<ClosureExpression>(let0->init);
    REQUIRE(clos);
    REQUIRE(clos->is_move == true);
    REQUIRE(clos->params.size() == 2);
    REQUIRE(clos->params[0].type);
}

TEST_CASE("Rust parser parses macro call", "[rust][parser]") {
    const char *src = R"(
fn main() {
  println!("hi {}", 1 + 2);
}
)";
    Diagnostics diag;
    RustLexer lexer(src, "<mem>");
    RustParser parser(lexer, diag);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    REQUIRE(mod);
    auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
    REQUIRE(fn);
    auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(fn->body[0]);
    REQUIRE(expr_stmt);
    auto macro = std::dynamic_pointer_cast<MacroCallExpression>(expr_stmt->expr);
    REQUIRE(macro);
    REQUIRE(macro->path.segments.size() == 1);
    REQUIRE(macro->path.segments[0] == "println");
    REQUIRE(macro->body.find("hi") != std::string::npos);
}

TEST_CASE("Rust parser parses ref/slice/array/function types", "[rust][parser]") {
    const char *src = R"(
fn f(x: &mut 'a i32, y: [u8; 3], z: &[u8], w: fn(i32) -> i32) -> (i32, i32) { w(x[0] as i32) }
)";
    Diagnostics diag;
    RustLexer lexer(src, "<mem>");
    RustParser parser(lexer, diag);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    REQUIRE(mod);
    auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
    REQUIRE(fn);
    REQUIRE(fn->params.size() == 4);
    REQUIRE(fn->return_type);
}
