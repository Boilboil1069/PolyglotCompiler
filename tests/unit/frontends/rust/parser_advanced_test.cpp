#include <catch2/catch_test_macros.hpp>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

TEST_CASE("Rust parser parses loops/match/assignments", "[rust][parser]") {
  const char *src = R"(
fn main() {
  let mut x = 0;
  loop {
    if x > 3 { break x; }
    x += 1;
  }
  for i in 0..3 {
    continue;
  }
  let v = match x {
    0 => 1,
    _ => 2,
  };
}
)";
  Diagnostics diag;
  RustLexer lexer(src, "<mem>");
  RustParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->items.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
  REQUIRE(fn);
  REQUIRE(fn->body.size() >= 3);
  // match arm checks
  auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body.back());
  REQUIRE(let_stmt);
  auto match_expr = std::dynamic_pointer_cast<MatchExpression>(let_stmt->init);
  REQUIRE(match_expr);
  REQUIRE(match_expr->arms.size() == 2);
}

TEST_CASE("Rust parser parses struct/enum/use paths", "[rust][parser]") {
  const char *src = R"(
use crate::foo::bar;
struct S { a, b, c }
enum E { A, B, C }
fn f() {}
)";
  Diagnostics diag;
  RustLexer lexer(src, "<mem>");
  RustParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->items.size() == 4);
  auto use_decl = std::dynamic_pointer_cast<UseDeclaration>(mod->items[0]);
  REQUIRE(use_decl);
  REQUIRE(use_decl->path == "crate::foo::bar");
  auto s = std::dynamic_pointer_cast<StructItem>(mod->items[1]);
  REQUIRE(s);
  REQUIRE(s->fields.size() == 3);
  auto e = std::dynamic_pointer_cast<EnumItem>(mod->items[2]);
  REQUIRE(e);
  REQUIRE(e->variants.size() == 3);
}
