#include <catch2/catch_test_macros.hpp>
#include <algorithm>

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
  REQUIRE(tmpl->params.size() == 1);
  REQUIRE(tmpl->params[0].find("typename") != std::string::npos);
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

TEST_CASE("C++ parser parses richer class members and access", "[cpp][parser][class]") {
  const char *src = R"(
  struct S {
    [[nodiscard]] S();
  public:
    S(int v);
    ~S() noexcept;
    int value;
    int get() const noexcept;
    S operator+(const S& other);
    friend int helper(S s);
  private:
    static int count;
  };
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
  auto rec = std::dynamic_pointer_cast<RecordDecl>(mod->declarations[0]);
  REQUIRE(rec);
  REQUIRE(rec->fields.size() == 1);
  REQUIRE(rec->methods.size() >= 4);
  auto ctor = std::dynamic_pointer_cast<FunctionDecl>(rec->methods[0]);
  REQUIRE(ctor);
  REQUIRE(ctor->is_constructor);
  REQUIRE(!ctor->body.empty() || ctor->is_defaulted || ctor->is_deleted);
  auto dtor = std::find_if(rec->methods.begin(), rec->methods.end(), [](const auto &m) {
    auto fn = std::dynamic_pointer_cast<FunctionDecl>(m);
    return fn && fn->is_destructor;
  });
  REQUIRE(dtor != rec->methods.end());
}

TEST_CASE("C++ parser parses range-for and switch/try", "[cpp][parser][stmts]") {
  const char *src = R"(
  void foo(int n) {
    for (int x : n) { }
    for (i = 0; i < 3; i = i + 1) { }
    switch (n) { case 1: break; default: break; }
    try { throw n; } catch (int e) { n = e; }
  }
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionDecl>(mod->declarations[0]);
  REQUIRE(fn);
  REQUIRE(fn->body.size() >= 4);
}

TEST_CASE("C++ parser parses using namespace, alias, typedef", "[cpp][parser][using]") {
  const char *src = R"(
  namespace ns { int v; }
  namespace alias = ns;
  using namespace alias;
  typedef int i32;
  using Vec = i32;
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 5);
}

TEST_CASE("C++ parser parses fold and initializer list", "[cpp][parser][expr]") {
  const char *src = R"(
  auto x = (a + ... + b);
  auto y = {1, 2, 3};
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 2);
  auto fold_decl = std::dynamic_pointer_cast<VarDecl>(mod->declarations[0]);
  REQUIRE(fold_decl);
  auto init_fold = std::dynamic_pointer_cast<BinaryExpression>(fold_decl->init);
  REQUIRE(init_fold); // fallback binary if fold not produced
  auto list_decl = std::dynamic_pointer_cast<VarDecl>(mod->declarations[1]);
  REQUIRE(list_decl);
  auto list_init = std::dynamic_pointer_cast<InitializerListExpression>(list_decl->init);
  REQUIRE(list_init);
  REQUIRE(list_init->elements.size() == 3);
}
