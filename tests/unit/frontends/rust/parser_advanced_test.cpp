#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_frontend.h"
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
struct S { a: i32, b: i32, c: i32 }
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

TEST_CASE("Rust parser preserves ranges, let-else blocks, and tail expressions",
          "[rust][parser][modern]") {
  const char *src = R"(
fn choose(x: i32) -> i32 {
  for i in 0..3 { let _ = i; }
  let 1 = x else { return 0; };
  x
}
)";
  Diagnostics diag;
  RustLexer lexer(src, "<rust2018>");
  RustParser parser(lexer, diag);
  parser.SetRustEdition(polyglot::frontends::RustEdition::kE2018);
  parser.ParseModule();
  auto mod = parser.TakeModule();

  REQUIRE_FALSE(diag.HasErrors());
  REQUIRE(mod->items.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
  REQUIRE(fn);
  REQUIRE(fn->body.size() == 3);

  auto loop = std::dynamic_pointer_cast<ForStatement>(fn->body[0]);
  REQUIRE(loop);
  auto range = std::dynamic_pointer_cast<RangeExpression>(loop->iterable);
  REQUIRE(range);
  REQUIRE(range->kind == RangeExpression::RangeKind::kExclusive);
  REQUIRE(std::dynamic_pointer_cast<Literal>(range->start)->value == "0");
  REQUIRE(std::dynamic_pointer_cast<Literal>(range->end)->value == "3");

  auto let_else = std::dynamic_pointer_cast<LetStatement>(fn->body[1]);
  REQUIRE(let_else);
  REQUIRE(let_else->has_else);
  REQUIRE(let_else->else_body.size() == 1);
  REQUIRE(std::dynamic_pointer_cast<ReturnStatement>(let_else->else_body[0]));

  auto tail = std::dynamic_pointer_cast<ExprStatement>(fn->body[2]);
  REQUIRE(tail);
  REQUIRE_FALSE(tail->has_semicolon);
}

TEST_CASE("Rust parser gates async syntax by edition and retains async AST",
          "[rust][parser][async][version]") {
  const char *src = R"(
async fn compute() -> i32 {
  let pending = async { 1 };
  pending.await
}
)";

  Diagnostics modern_diag;
  RustLexer modern_lexer(src, "<rust2018>");
  RustParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2018);
  modern_parser.ParseModule();
  auto mod = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
  REQUIRE(fn);
  REQUIRE(fn->is_async);
  auto pending = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
  REQUIRE(pending);
  REQUIRE(std::dynamic_pointer_cast<AsyncBlock>(pending->init));
  auto tail = std::dynamic_pointer_cast<ExprStatement>(fn->body[1]);
  REQUIRE(tail);
  REQUIRE(std::dynamic_pointer_cast<AwaitExpression>(tail->expr));

  Diagnostics old_diag;
  RustLexer old_lexer(src, "<rust2015>");
  RustParser old_parser(old_lexer, old_diag);
  old_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2015);
  old_parser.ParseModule();
  REQUIRE(std::count_if(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
            return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
          }) >= 2);
}

TEST_CASE("Rust parser preserves async closures and gates them by edition",
          "[rust][parser][async-closure][version]") {
  const char *src = "fn make() { let callback = async move |value: i32| value; }";

  Diagnostics modern_diag;
  RustLexer modern_lexer(src, "<rust2018>");
  RustParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2018);
  modern_parser.ParseModule();
  auto module = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  auto fn = std::dynamic_pointer_cast<FunctionItem>(module->items[0]);
  REQUIRE(fn);
  auto binding = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
  REQUIRE(binding);
  auto closure = std::dynamic_pointer_cast<ClosureExpression>(binding->init);
  REQUIRE(closure);
  REQUIRE(closure->is_async);
  REQUIRE(closure->is_move);
  REQUIRE(closure->params.size() == 1);

  Diagnostics old_diag;
  RustLexer old_lexer(src, "<rust2015>");
  RustParser old_parser(old_lexer, old_diag);
  old_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2015);
  old_parser.ParseModule();
  REQUIRE(std::any_of(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
  }));
}

TEST_CASE("Rust 2024 reserves gen but accepts its raw identifier form",
          "[rust][parser][edition2024][gen]") {
  Diagnostics old_diag;
  RustLexer old_lexer("fn gen() {}", "<rust2021>");
  RustParser old_parser(old_lexer, old_diag);
  old_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2021);
  old_parser.ParseModule();
  REQUIRE_FALSE(old_diag.HasErrors());

  Diagnostics modern_diag;
  RustLexer modern_lexer("fn gen() {}", "<rust2024>");
  RustParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2024);
  modern_parser.ParseModule();
  REQUIRE(std::any_of(modern_diag.All().begin(), modern_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
  }));

  Diagnostics raw_diag;
  RustLexer raw_lexer("fn r#gen() {}", "<rust2024>");
  RustParser raw_parser(raw_lexer, raw_diag);
  raw_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2024);
  raw_parser.ParseModule();
  REQUIRE_FALSE(raw_diag.HasErrors());
}

TEST_CASE("Rust 2024 rejects reserved guarded strings and unwrapped unsafe attributes",
          "[rust][parser][edition2024][guarded-string][attribute]") {
  const char *guarded = R"(macro_rules! tokens { (#"text"# ##) => {}; })";
  Diagnostics old_guard_diag;
  RustLexer old_guard_lexer(guarded, "<rust2021>");
  RustParser old_guard_parser(old_guard_lexer, old_guard_diag);
  old_guard_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2021);
  old_guard_parser.ParseModule();
  REQUIRE_FALSE(old_guard_diag.HasErrors());

  Diagnostics guard_diag;
  RustLexer guard_lexer(guarded, "<rust2024>");
  RustParser guard_parser(guard_lexer, guard_diag);
  guard_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2024);
  guard_parser.ParseModule();
  REQUIRE(std::any_of(guard_diag.All().begin(), guard_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
  }));

  Diagnostics unsafe_diag;
  RustLexer unsafe_lexer("#[no_mangle] fn exported() {}", "<rust2024>");
  RustParser unsafe_parser(unsafe_lexer, unsafe_diag);
  unsafe_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2024);
  unsafe_parser.ParseModule();
  REQUIRE(std::any_of(unsafe_diag.All().begin(), unsafe_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
  }));

  Diagnostics wrapped_diag;
  RustLexer wrapped_lexer("#[unsafe(no_mangle)] fn exported() {}", "<rust2024>");
  RustParser wrapped_parser(wrapped_lexer, wrapped_diag);
  wrapped_parser.SetRustEdition(polyglot::frontends::RustEdition::kE2024);
  wrapped_parser.ParseModule();
  REQUIRE_FALSE(wrapped_diag.HasErrors());
}

TEST_CASE("Rust 2015 contextual keywords survive version-aware signature extraction",
          "[rust][parser][edition2015][contextual-keyword][signatures]") {
  const char *src = "fn async() -> i32 { 1 }";
  Diagnostics parse_diag;
  RustLexer lexer(src, "<rust2015>");
  RustParser parser(lexer, parse_diag);
  parser.SetRustEdition(polyglot::frontends::RustEdition::kE2015);
  parser.ParseModule();
  REQUIRE_FALSE(parse_diag.HasErrors());

  RustLanguageFrontend frontend;
  Diagnostics extraction_diag;
  polyglot::frontends::FrontendOptions options;
  options.rust_edition = polyglot::frontends::RustEdition::kE2015;
  const auto signatures =
      frontend.ExtractSignatures(src, "<rust2015>", "legacy", extraction_diag, options);
  REQUIRE_FALSE(extraction_diag.HasErrors());
  REQUIRE(signatures.size() == 1);
  REQUIRE(signatures[0].name == "async");
}
