#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/python/include/python_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::SemaContext;
using polyglot::python::AnalyzeModule;
using polyglot::python::Assignment;
using polyglot::python::AttributeExpression;
using polyglot::python::CallArg;
using polyglot::python::CallExpression;
using polyglot::python::ExprStatement;
using polyglot::python::Identifier;
using polyglot::python::ListExpression;
using polyglot::python::Literal;
using polyglot::python::Module;

TEST_CASE("Python sema resolves list attribute/index", "[python][sema]") {
  Module mod;

  // a = [1, 2]
  auto assign = std::make_shared<Assignment>();
  auto target = std::make_shared<Identifier>();
  target->name = "a";
  assign->targets.push_back(target);
  auto list = std::make_shared<ListExpression>();
  auto lit1 = std::make_shared<Literal>();
  lit1->value = "1";
  auto lit2 = std::make_shared<Literal>();
  lit2->value = "2";
  list->elements.push_back(lit1);
  list->elements.push_back(lit2);
  assign->value = list;
  mod.body.push_back(assign);

  // a.append(3)
  auto call = std::make_shared<CallExpression>();
  auto attr = std::make_shared<AttributeExpression>();
  attr->object = target;
  attr->attribute = "append";
  call->callee = attr;
  CallArg arg;
  arg.value = lit1; // reuse literal
  call->args.push_back(arg);
  auto expr_stmt = std::make_shared<ExprStatement>();
  expr_stmt->expr = call;
  mod.body.push_back(expr_stmt);

  // a[0]
  auto idx_expr = std::make_shared<polyglot::python::IndexExpression>();
  idx_expr->object = target;
  idx_expr->index = lit1;
  auto expr_stmt2 = std::make_shared<ExprStatement>();
  expr_stmt2->expr = idx_expr;
  mod.body.push_back(expr_stmt2);

  Diagnostics diags;
  SemaContext ctx(diags);
  AnalyzeModule(mod, ctx);

  REQUIRE(diags.All().empty());
}
