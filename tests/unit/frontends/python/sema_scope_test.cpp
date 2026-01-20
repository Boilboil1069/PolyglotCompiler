#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/python/include/python_sema.h"

using polyglot::frontends::SemaContext;
using polyglot::frontends::Diagnostics;
using polyglot::python::AnalyzeModule;
using polyglot::python::Assignment;
using polyglot::python::ExprStatement;
using polyglot::python::FunctionDef;
using polyglot::python::Identifier;
using polyglot::python::Literal;
using polyglot::python::Module;

TEST_CASE("Python sema captures outer variable", "[python][sema]") {
  Module mod;

  auto outer = std::make_shared<FunctionDef>();
  outer->name = "outer";

  auto assign = std::make_shared<Assignment>();
  auto target = std::make_shared<Identifier>();
  target->name = "x";
  assign->targets.push_back(target);
  auto val = std::make_shared<Literal>();
  val->value = "1";
  assign->value = val;
  outer->body.push_back(assign);

  auto inner = std::make_shared<FunctionDef>();
  inner->name = "inner";
  auto inner_expr = std::make_shared<ExprStatement>();
  auto ref = std::make_shared<Identifier>();
  ref->name = "x";
  inner_expr->expr = ref;
  inner->body.push_back(inner_expr);
  outer->body.push_back(inner);

  mod.body.push_back(outer);

  Diagnostics diags;
  SemaContext ctx(diags);
  AnalyzeModule(mod, ctx);

  const auto *sym_x = ctx.Symbols().FindInAnyScope("x");
  REQUIRE(sym_x != nullptr);
  REQUIRE(sym_x->captured);
  REQUIRE(diags.All().empty());
}
