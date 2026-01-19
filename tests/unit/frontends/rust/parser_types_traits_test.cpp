#include <catch2/catch_test_macros.hpp>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

TEST_CASE("Rust parser parses typed fn and generics", "[rust][parser]") {
  const char *src = R"(
fn map<T, U>(x: T) -> U { return x; }
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
  REQUIRE(fn->type_params.size() == 2);
  REQUIRE(fn->params.size() == 1);
  REQUIRE(fn->return_type);
}

TEST_CASE("Rust parser parses impl/trait/mod", "[rust][parser]") {
  const char *src = R"(
trait T {
  fn f();
}
impl T {
  fn f() {}
}
mod m {
  fn g() {}
}
)";
  Diagnostics diag;
  RustLexer lexer(src, "<mem>");
  RustParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->items.size() == 3);
  auto tr = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
  REQUIRE(tr);
  auto im = std::dynamic_pointer_cast<ImplItem>(mod->items[1]);
  REQUIRE(im);
  auto md = std::dynamic_pointer_cast<ModItem>(mod->items[2]);
  REQUIRE(md);
}

TEST_CASE("Rust parser parses match guard and struct pattern", "[rust][parser]") {
  const char *src = R"(
fn f(p: Foo) {
  match p {
    Foo { a, .. } if a > 0 => a,
    _ => 0,
  }
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
  auto blk_stmt = std::dynamic_pointer_cast<ExprStatement>(fn->body.back());
  REQUIRE(blk_stmt);
  auto me = std::dynamic_pointer_cast<MatchExpression>(blk_stmt->expr);
  REQUIRE(me);
  REQUIRE(me->arms.size() == 2);
  REQUIRE(me->arms[0]->guard);
  auto sp = std::dynamic_pointer_cast<StructPattern>(me->arms[0]->pattern);
  REQUIRE(sp);
  REQUIRE(sp->has_rest == true);
}
