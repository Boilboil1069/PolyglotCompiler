#include <catch2/catch_test_macros.hpp>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

TEST_CASE("Rust lexer keeps numeric type suffixes in one token", "[rust][lexer][literal]") {
  RustLexer lexer("0_i64 12u32 3.5f64", "<mem>");

  const auto zero = lexer.NextToken();
  REQUIRE(zero.kind == polyglot::frontends::TokenKind::kNumber);
  REQUIRE(zero.lexeme == "0_i64");

  const auto twelve = lexer.NextToken();
  REQUIRE(twelve.kind == polyglot::frontends::TokenKind::kNumber);
  REQUIRE(twelve.lexeme == "12u32");

  const auto decimal = lexer.NextToken();
  REQUIRE(decimal.kind == polyglot::frontends::TokenKind::kNumber);
  REQUIRE(decimal.lexeme == "3.5f64");
}

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

TEST_CASE("Rust parser preserves named-field struct construction", "[rust][parser][struct]") {
  const char *src = R"(
struct Session { requested: i64, reserved: i64 }
fn build(requested: i64, reserved: i64) {
  let session = Session { requested, reserved: reserved };
}
)";
  Diagnostics diag;
  RustLexer lexer(src, "<mem>");
  RustParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();

  REQUIRE_FALSE(diag.HasErrors());
  REQUIRE(mod);
  REQUIRE(mod->items.size() == 2);
  auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[1]);
  REQUIRE(fn);
  REQUIRE(fn->body.size() == 1);
  auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
  REQUIRE(let_stmt);
  auto value = std::dynamic_pointer_cast<StructExpression>(let_stmt->init);
  REQUIRE(value);
  REQUIRE(value->path.segments == std::vector<std::string>{"Session"});
  REQUIRE(value->fields.size() == 2);
  REQUIRE(value->fields[0].name == "requested");
  REQUIRE(std::dynamic_pointer_cast<Identifier>(value->fields[0].value));
  REQUIRE(value->fields[1].name == "reserved");
}
