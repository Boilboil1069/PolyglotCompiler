#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_ast.h"
#include "frontends/cpp/include/cpp_sema.h"

using polyglot::core::TypeKind;
using polyglot::frontends::Diagnostics;
using polyglot::frontends::SemaContext;
using polyglot::cpp::AnalyzeModule;
using namespace polyglot::cpp;

TEST_CASE("C++ sema resolves member and overload", "[cpp][sema]") {
  Module mod;

  // struct S { int x; };
  auto rec = std::make_shared<RecordDecl>();
  rec->kind = "struct";
  rec->name = "S";
  FieldDecl f;
  f.name = "x";
  f.type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(f.type)->name = "int";
  rec->fields.push_back(f);
  mod.declarations.push_back(rec);

  // int foo(int);
  auto foo_int = std::make_shared<FunctionDecl>();
  foo_int->name = "foo";
  foo_int->return_type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(foo_int->return_type)->name = "int";
  FunctionDecl::Param p1;
  p1.name = "a";
  p1.type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(p1.type)->name = "int";
  foo_int->params.push_back(p1);
  mod.declarations.push_back(foo_int);

  // double foo(double);
  auto foo_double = std::make_shared<FunctionDecl>();
  foo_double->name = "foo";
  foo_double->return_type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(foo_double->return_type)->name = "double";
  FunctionDecl::Param p2;
  p2.name = "a";
  p2.type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(p2.type)->name = "double";
  foo_double->params.push_back(p2);
  mod.declarations.push_back(foo_double);

  // int main() { S s; foo(1); return s.x; }
  auto main_fn = std::make_shared<FunctionDecl>();
  main_fn->name = "main";
  main_fn->return_type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(main_fn->return_type)->name = "int";

  auto body = std::make_shared<CompoundStatement>();

  auto decl_s = std::make_shared<VarDecl>();
  decl_s->name = "s";
  decl_s->type = std::make_shared<SimpleType>();
  std::static_pointer_cast<SimpleType>(decl_s->type)->name = "S";
  body->statements.push_back(decl_s);

  auto call_stmt = std::make_shared<ExprStatement>();
  auto call = std::make_shared<CallExpression>();
  auto callee = std::make_shared<Identifier>();
  callee->name = "foo";
  call->callee = callee;
  auto lit = std::make_shared<Literal>();
  lit->value = "1";
  call->args.push_back(lit);
  call_stmt->expr = call;
  body->statements.push_back(call_stmt);

  auto ret_stmt = std::make_shared<ReturnStatement>();
  auto mem = std::make_shared<MemberExpression>();
  auto obj = std::make_shared<Identifier>();
  obj->name = "s";
  mem->object = obj;
  mem->member = "x";
  ret_stmt->value = mem;
  body->statements.push_back(ret_stmt);

  main_fn->body.push_back(body);
  mod.declarations.push_back(main_fn);

  Diagnostics diags;
  SemaContext ctx(diags);
  AnalyzeModule(mod, ctx);

  REQUIRE(diags.All().empty());
  const auto *field = ctx.Symbols().FindInAnyScope("x");
  REQUIRE(field != nullptr);
  REQUIRE(field->type.kind == TypeKind::kInt);
}
