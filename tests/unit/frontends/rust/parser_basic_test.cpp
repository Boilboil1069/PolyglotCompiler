#include <catch2/catch_test_macros.hpp>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

TEST_CASE("Rust parser parses fn/let/control flow", "[rust][parser]") {
  const char *src = R"(
fn main(a, b) {
  let x = a + b * 2;
  if x {
    x = x + 1;
  } else {
    x = 0;
  }
  while x {
    x = x - 1;
  }
  return x;
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
  REQUIRE(fn->params.size() == 2);
  REQUIRE(fn->body.size() >= 4);
}

TEST_CASE("Rust parser handles call/member/index chains", "[rust][parser]") {
  const char *src = R"(
fn main() {
  let y = foo.bar(1, 2)[i];
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
  REQUIRE(fn->body.size() == 1);
  auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
  REQUIRE(let_stmt);
  auto idx = std::dynamic_pointer_cast<IndexExpression>(let_stmt->init);
  REQUIRE(idx);
  auto call = std::dynamic_pointer_cast<CallExpression>(idx->object);
  REQUIRE(call);
  auto mem = std::dynamic_pointer_cast<MemberExpression>(call->callee);
  REQUIRE(mem);
  REQUIRE(mem->member == "bar");
  REQUIRE(call->args.size() == 2);
}
