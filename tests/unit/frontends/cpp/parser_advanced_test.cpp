#include <catch2/catch_test_macros.hpp>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::cpp::CppLexer;
using polyglot::cpp::CppParser;
using namespace polyglot::cpp;

TEST_CASE("C++ parser parses lambda with captures and return type", "[cpp][parser][lambda]") {
  const char *src = "[x](int y)->int { return x + y; }(1);";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);

  auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(mod->declarations[0]);
  REQUIRE(expr_stmt);
  auto call = std::dynamic_pointer_cast<CallExpression>(expr_stmt->expr);
  REQUIRE(call);
  auto lam = std::dynamic_pointer_cast<LambdaExpression>(call->callee);
  REQUIRE(lam);
  REQUIRE(lam->captures.size() == 1);
  REQUIRE(lam->params.size() == 1);
  REQUIRE(lam->return_type != nullptr);
  REQUIRE(lam->body.size() == 1);
}

TEST_CASE("C++ parser parses templates, namespaces, records, enums, and using", "[cpp][parser][advanced]") {
  const char *src = R"(
  template <typename T> T id(T v) { return v; }
  using i32 = int;
  struct Point { int x; int y; };
  Point origin;
  enum Color { Red, Green, Blue };
  namespace ns { int value = 5; }
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 6);

  auto tmpl = std::dynamic_pointer_cast<TemplateDecl>(mod->declarations[0]);
  REQUIRE(tmpl);
  REQUIRE(tmpl->params.size() == 2); // "typename", "T"
  auto tmpl_fn = std::dynamic_pointer_cast<FunctionDecl>(tmpl->inner);
  REQUIRE(tmpl_fn);
  REQUIRE(tmpl_fn->name == "id");
  REQUIRE(tmpl_fn->params.size() == 1);

  auto using_decl = std::dynamic_pointer_cast<UsingDeclaration>(mod->declarations[1]);
  REQUIRE(using_decl);
  REQUIRE(using_decl->name == "i32");
  REQUIRE(using_decl->aliased == "int");

  auto rec = std::dynamic_pointer_cast<RecordDecl>(mod->declarations[2]);
  REQUIRE(rec);
  REQUIRE(rec->name == "Point");
  REQUIRE(rec->fields.size() == 2);
  REQUIRE(rec->fields[0].name == "x");

  auto origin = std::dynamic_pointer_cast<VarDecl>(mod->declarations[3]);
  REQUIRE(origin);
  REQUIRE(origin->name == "origin");
  auto origin_type = std::dynamic_pointer_cast<SimpleType>(origin->type);
  REQUIRE(origin_type);
  REQUIRE(origin_type->name == "Point");

  auto en = std::dynamic_pointer_cast<EnumDecl>(mod->declarations[4]);
  REQUIRE(en);
  REQUIRE(en->enumerators.size() == 3);

  auto ns = std::dynamic_pointer_cast<NamespaceDecl>(mod->declarations[5]);
  REQUIRE(ns);
  REQUIRE(ns->name == "ns");
  REQUIRE(ns->members.size() == 1);
  auto ns_var = std::dynamic_pointer_cast<VarDecl>(ns->members[0]);
  REQUIRE(ns_var);
  REQUIRE(ns_var->name == "value");
}
