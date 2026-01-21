#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/sema_context.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::SemaContext;
using polyglot::rust::AnalyzeModule;
using namespace polyglot::rust;

TEST_CASE("Rust sema flags non-exhaustive match", "[rust][sema]") {
  Module mod;

  auto match_expr = std::make_shared<MatchExpression>();
  auto scrutinee = std::make_shared<Literal>();
  scrutinee->value = "1";
  match_expr->scrutinee = scrutinee;

  auto arm1 = std::make_shared<MatchArm>();
  arm1->pattern = std::make_shared<LiteralPattern>();
  std::static_pointer_cast<LiteralPattern>(arm1->pattern)->value = "1";
  arm1->body = std::make_shared<Literal>();
  std::static_pointer_cast<Literal>(arm1->body)->value = "1";

  auto arm2 = std::make_shared<MatchArm>();
  arm2->pattern = std::make_shared<LiteralPattern>();
  std::static_pointer_cast<LiteralPattern>(arm2->pattern)->value = "2";
  arm2->body = std::make_shared<Literal>();
  std::static_pointer_cast<Literal>(arm2->body)->value = "2";

  match_expr->arms.push_back(arm1);
  match_expr->arms.push_back(arm2);

  auto expr_stmt = std::make_shared<ExprStatement>();
  expr_stmt->expr = match_expr;
  mod.items.push_back(expr_stmt);

  Diagnostics diags;
  SemaContext ctx(diags);
  AnalyzeModule(mod, ctx);

  REQUIRE_FALSE(diags.All().empty());
}
