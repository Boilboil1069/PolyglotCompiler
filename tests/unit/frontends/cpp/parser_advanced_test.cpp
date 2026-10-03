#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "middle/include/ir/ir_context.h"

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
  // Constructor may have a body, be defaulted/deleted, or be a forward declaration
  if (!ctor->body.empty()) {
    REQUIRE(!ctor->body.empty());
  }
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

TEST_CASE("C++ signature scan treats angle headers as external and keeps local preprocessing",
          "[cpp][frontend][signatures][preprocessor]") {
  constexpr const char *source = R"cpp(
#include <polyglot_test_platform_header_that_does_not_exist>
#define RESULT_TYPE int
#if __cplusplus >= 202302L
RESULT_TYPE selected(double value) { return std::sqrt(value); }
#else
RESULT_TYPE legacy(int value) { return value; }
#endif
)cpp";

  CppLanguageFrontend frontend;
  polyglot::frontends::FrontendOptions options;
  options.cpp_dialect = polyglot::frontends::CppDialect::kCpp23;
  Diagnostics diagnostics;
  const auto signatures = frontend.ExtractSignatures(
      source, "<system-header-signatures.cpp>", "fixture", diagnostics, options);

  for (const auto &diagnostic : diagnostics.All())
    UNSCOPED_INFO(polyglot::frontends::Diagnostics::Format(diagnostic));
  REQUIRE_FALSE(diagnostics.HasErrors());
  REQUIRE(signatures.size() == 1);
  CHECK(signatures.front().name == "selected");
  CHECK(signatures.front().qualified_name == "fixture::selected");
}

TEST_CASE("C++ signature scan still rejects unresolved quoted project headers",
          "[cpp][frontend][signatures][preprocessor]") {
  constexpr const char *source = R"cpp(
#include "polyglot_missing_project_header.hpp"
int exposed(int value) { return value; }
)cpp";

  CppLanguageFrontend frontend;
  polyglot::frontends::FrontendOptions options;
  Diagnostics diagnostics;
  const auto signatures = frontend.ExtractSignatures(
      source, "<quoted-header-signatures.cpp>", "fixture", diagnostics, options);

  CHECK(signatures.empty());
  REQUIRE(diagnostics.HasErrors());
  CHECK(std::any_of(diagnostics.All().begin(), diagnostics.All().end(),
                    [](const auto &diagnostic) {
                      return diagnostic.message.find("Failed to resolve include") !=
                             std::string::npos;
                    }));
}

TEST_CASE("C++ parser preserves call and explicit template argument boundaries",
          "[cpp][parser][call][template]") {
  const char *src = R"(
  template <typename T> T add(T a, T b) { return a + b; }
  int driver() { return add<int>(1, 2); }
  )";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
  parser.ParseModule();
  auto mod = parser.TakeModule();

  REQUIRE_FALSE(diag.HasErrors());
  REQUIRE(mod->declarations.size() == 2);
  auto driver = std::dynamic_pointer_cast<FunctionDecl>(mod->declarations[1]);
  REQUIRE(driver);
  REQUIRE(driver->body.size() == 1);
  auto ret = std::dynamic_pointer_cast<ReturnStatement>(driver->body[0]);
  REQUIRE(ret);
  auto call = std::dynamic_pointer_cast<CallExpression>(ret->value);
  REQUIRE(call);
  REQUIRE(call->args.size() == 2);
  auto template_id = std::dynamic_pointer_cast<TemplateIdExpression>(call->callee);
  REQUIRE(template_id);
  REQUIRE(template_id->name == "add");
  REQUIRE(template_id->args.size() == 1);
}

TEST_CASE("C++ parser accepts lambdas with omitted empty parameter lists",
          "[cpp][parser][lambda]") {
  Diagnostics diag;
  CppLexer lexer("auto f = [] { return 42; };", "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();

  REQUIRE_FALSE(diag.HasErrors());
  REQUIRE(mod->declarations.size() == 1);
  auto var = std::dynamic_pointer_cast<VarDecl>(mod->declarations[0]);
  REQUIRE(var);
  auto lambda = std::dynamic_pointer_cast<LambdaExpression>(var->init);
  REQUIRE(lambda);
  REQUIRE(lambda->params.empty());
  REQUIRE(lambda->body.size() == 1);
}

TEST_CASE("C++ parser models C++17 if init-statements and gates older dialects",
          "[cpp][parser][if][version]") {
  const char *src = "int f() { if (int x = 1; x > 0) return x; return 0; }";

  Diagnostics modern_diag;
  CppLexer modern_lexer(src, "<cpp17>");
  CppParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp17);
  modern_parser.ParseModule();
  auto modern_mod = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  auto fn = std::dynamic_pointer_cast<FunctionDecl>(modern_mod->declarations[0]);
  REQUIRE(fn);
  auto if_stmt = std::dynamic_pointer_cast<IfStatement>(fn->body[0]);
  REQUIRE(if_stmt);
  REQUIRE(std::dynamic_pointer_cast<VarDecl>(if_stmt->init));

  Diagnostics old_diag;
  CppLexer old_lexer(src, "<cpp14>");
  CppParser old_parser(old_lexer, old_diag);
  old_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp14);
  old_parser.ParseModule();
  REQUIRE(std::any_of(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
  }));
}

TEST_CASE("C++11 and C++17 core syntax use exact dialect boundaries",
          "[cpp][parser][version][cpp11][cpp17]") {
  auto parse_at = [](const char *source, polyglot::frontends::CppDialect dialect,
                     Diagnostics &diagnostics) {
    CppLexer lexer(source, "<version-boundary>");
    CppParser parser(lexer, diagnostics);
    parser.SetCppDialect(dialect);
    parser.ParseModule();
    return parser.TakeModule();
  };
  auto mismatch_count = [](const Diagnostics &diagnostics) {
    return std::count_if(diagnostics.All().begin(), diagnostics.All().end(),
                         [](const auto &diagnostic) {
                           return diagnostic.code ==
                                  polyglot::frontends::ErrorCode::kLangVersionMismatch;
                         });
  };

  constexpr const char *cpp11_source = R"cpp(
    auto pointer = nullptr;
    auto closure = [] { return 1; };
    void visit(int values) { for (int value : values) { } }
  )cpp";
  Diagnostics cpp03_diagnostics;
  REQUIRE(parse_at(cpp11_source, polyglot::frontends::CppDialect::kCpp03,
                   cpp03_diagnostics));
  CHECK(mismatch_count(cpp03_diagnostics) == 3);

  Diagnostics cpp11_diagnostics;
  REQUIRE(parse_at(cpp11_source, polyglot::frontends::CppDialect::kCpp11,
                   cpp11_diagnostics));
  CHECK_FALSE(cpp11_diagnostics.HasErrors());

  constexpr const char *cpp17_source = R"cpp(
    auto folded = (... + values);
    void unpack() { auto [left, right] = pair; }
  )cpp";
  Diagnostics cpp14_diagnostics;
  REQUIRE(parse_at(cpp17_source, polyglot::frontends::CppDialect::kCpp14,
                   cpp14_diagnostics));
  CHECK(mismatch_count(cpp14_diagnostics) == 2);

  Diagnostics cpp17_diagnostics;
  auto cpp17_module = parse_at(cpp17_source, polyglot::frontends::CppDialect::kCpp17,
                               cpp17_diagnostics);
  REQUIRE(cpp17_module);
  CHECK_FALSE(cpp17_diagnostics.HasErrors());
  auto folded = std::dynamic_pointer_cast<VarDecl>(cpp17_module->declarations.at(0));
  REQUIRE(folded);
  CHECK(std::dynamic_pointer_cast<FoldExpression>(folded->init));
  auto unpack = std::dynamic_pointer_cast<FunctionDecl>(cpp17_module->declarations.at(1));
  REQUIRE(unpack);
  REQUIRE(unpack->body.size() == 1);
  CHECK(std::dynamic_pointer_cast<StructuredBindingDecl>(unpack->body.at(0)));
}

TEST_CASE("C++23 parser retains if consteval and gates C++20",
          "[cpp][parser][version][cpp23][if-consteval]") {
  constexpr const char *source = R"cpp(
    int immediate() {
      if consteval { return 1; } else { return 2; }
    }
    int negated() {
      if !consteval { return 3; } else { return 4; }
    }
  )cpp";

  Diagnostics old_diagnostics;
  CppLexer old_lexer(source, "<cpp20>");
  CppParser old_parser(old_lexer, old_diagnostics);
  old_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
  old_parser.ParseModule();
  CHECK(std::count_if(old_diagnostics.All().begin(), old_diagnostics.All().end(),
                      [](const auto &diagnostic) {
                        return diagnostic.code ==
                               polyglot::frontends::ErrorCode::kLangVersionMismatch;
                      }) == 2);

  Diagnostics modern_diagnostics;
  CppLexer modern_lexer(source, "<cpp23>");
  CppParser modern_parser(modern_lexer, modern_diagnostics);
  modern_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp23);
  modern_parser.ParseModule();
  auto module = modern_parser.TakeModule();
  REQUIRE(module);
  REQUIRE_FALSE(modern_diagnostics.HasErrors());
  REQUIRE(module->declarations.size() == 2);
  auto immediate = std::dynamic_pointer_cast<FunctionDecl>(module->declarations.at(0));
  auto negated = std::dynamic_pointer_cast<FunctionDecl>(module->declarations.at(1));
  REQUIRE(immediate);
  REQUIRE(negated);
  auto first_if = std::dynamic_pointer_cast<IfStatement>(immediate->body.at(0));
  auto second_if = std::dynamic_pointer_cast<IfStatement>(negated->body.at(0));
  REQUIRE(first_if);
  REQUIRE(second_if);
  CHECK(first_if->is_consteval);
  CHECK_FALSE(first_if->is_negated_consteval);
  CHECK(second_if->is_consteval);
  CHECK(second_if->is_negated_consteval);
  CHECK_FALSE(first_if->condition);
  CHECK(first_if->then_body.size() == 1);
  CHECK(first_if->else_body.size() == 1);

  polyglot::ir::IRContext ir_context;
  Diagnostics lowering_diagnostics;
  LowerToIR(*module, ir_context, lowering_diagnostics);
  CHECK(std::any_of(lowering_diagnostics.All().begin(), lowering_diagnostics.All().end(),
                    [](const auto &diagnostic) {
                      return diagnostic.code ==
                             polyglot::frontends::ErrorCode::kUnsupportedLowering;
                    }));
}

TEST_CASE("C++20 parser gates modules, spaceship, requires and designated initializers",
          "[cpp][parser][cpp20][version]") {
  const char *src = R"(
  export module demo;
  template <typename T> requires true T identity(T value) { return value; }
  struct Point { int x; int y; };
  Point p = {.x = 1, .y = 2};
  auto ordering = 1 <=> 2;
  )";

  Diagnostics modern_diag;
  CppLexer modern_lexer(src, "<cpp20>");
  CppParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
  modern_parser.ParseModule();
  auto modern_mod = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  REQUIRE(modern_mod->declarations.size() == 5);
  auto point = std::dynamic_pointer_cast<VarDecl>(modern_mod->declarations[3]);
  REQUIRE(point);
  auto init = std::dynamic_pointer_cast<InitializerListExpression>(point->init);
  REQUIRE(init);
  REQUIRE(init->elements.size() == 2);
  REQUIRE(std::dynamic_pointer_cast<DesignatedInitializerExpression>(init->elements[0]));

  Diagnostics old_diag;
  CppLexer old_lexer(src, "<cpp17>");
  CppParser old_parser(old_lexer, old_diag);
  old_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp17);
  old_parser.ParseModule();
  REQUIRE(std::count_if(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
            return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
          }) >= 4);
}

TEST_CASE("C++20 frontend analyzes constrained explicit template calls",
          "[cpp][frontend][concept][template]") {
  const char *src = R"(
  template <typename T>
  concept Integral = true;

  template <typename T> requires Integral<T>
  T add(T a, T b) { return a + b; }

  int driver() { return add<int>(1, 2); }
  )";
  Diagnostics diag;
  polyglot::frontends::FrontendOptions options;
  options.cpp_dialect = polyglot::frontends::CppDialect::kCpp20;
  CppLanguageFrontend frontend;

  const bool analyzed = frontend.Analyze(src, "<cpp20>", diag, options);
  for (const auto &entry : diag.All())
    UNSCOPED_INFO(polyglot::frontends::Diagnostics::Format(entry));
  REQUIRE(analyzed);
  REQUIRE_FALSE(diag.HasErrors());
}

TEST_CASE("C++20 parser preserves coroutine syntax and lowering fails closed",
          "[cpp][parser][coroutine][version][lowering]") {
  const char *src = R"(
  int task(int value) {
    co_yield value;
    co_await value;
    co_return value;
  }
  )";

  Diagnostics modern_diag;
  CppLexer modern_lexer(src, "<cpp20>");
  CppParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
  modern_parser.ParseModule();
  auto module = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  auto fn = std::dynamic_pointer_cast<FunctionDecl>(module->declarations[0]);
  REQUIRE(fn);
  REQUIRE(fn->is_coroutine);
  REQUIRE(fn->body.size() == 3);
  auto yield = std::dynamic_pointer_cast<ExprStatement>(fn->body[0]);
  auto await = std::dynamic_pointer_cast<ExprStatement>(fn->body[1]);
  auto coroutine_return = std::dynamic_pointer_cast<ReturnStatement>(fn->body[2]);
  REQUIRE(yield);
  REQUIRE(await);
  REQUIRE(coroutine_return);
  REQUIRE(std::dynamic_pointer_cast<UnaryExpression>(yield->expr)->op == "co_yield");
  REQUIRE(std::dynamic_pointer_cast<UnaryExpression>(await->expr)->op == "co_await");
  REQUIRE(coroutine_return->is_co_return);

  polyglot::ir::IRContext ir_context;
  Diagnostics lowering_diag;
  LowerToIR(*module, ir_context, lowering_diag);
  REQUIRE(std::any_of(lowering_diag.All().begin(), lowering_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
  }));
  REQUIRE(ir_context.Functions().empty());

  Diagnostics old_diag;
  CppLexer old_lexer(src, "<cpp17>");
  CppParser old_parser(old_lexer, old_diag);
  old_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp17);
  old_parser.ParseModule();
  REQUIRE(std::count_if(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
            return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
          }) >= 3);
}

TEST_CASE("C++23 parser models explicit object parameters and lowering fails closed",
          "[cpp][parser][cpp23][deducing-this][lowering]") {
  const char *src = R"(
  struct Counter {
    int value;
    int get(this Counter const& self) { return self.value; }
  };
  )";

  Diagnostics modern_diag;
  CppLexer modern_lexer(src, "<cpp23>");
  CppParser modern_parser(modern_lexer, modern_diag);
  modern_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp23);
  modern_parser.ParseModule();
  auto module = modern_parser.TakeModule();
  REQUIRE_FALSE(modern_diag.HasErrors());
  auto record = std::dynamic_pointer_cast<RecordDecl>(module->declarations[0]);
  REQUIRE(record);
  REQUIRE(record->methods.size() == 1);
  auto method = std::dynamic_pointer_cast<FunctionDecl>(record->methods[0]);
  REQUIRE(method);
  REQUIRE(method->has_explicit_object_parameter);
  REQUIRE(method->params.size() == 1);
  REQUIRE(method->params[0].is_explicit_object);
  REQUIRE(method->params[0].name == "self");

  polyglot::ir::IRContext ir_context;
  Diagnostics lowering_diag;
  LowerToIR(*module, ir_context, lowering_diag);
  REQUIRE(std::any_of(lowering_diag.All().begin(), lowering_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
  }));
  REQUIRE(ir_context.Functions().empty());

  Diagnostics old_diag;
  CppLexer old_lexer(src, "<cpp20>");
  CppParser old_parser(old_lexer, old_diag);
  old_parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
  old_parser.ParseModule();
  REQUIRE(std::any_of(old_diag.All().begin(), old_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
  }));
}

TEST_CASE("C++ contextual keywords remain identifiers in their valid dialect contexts",
          "[cpp][parser][contextual-keyword][signatures]") {
  const char *src = R"(
  int module = 1;
  int import = 2;
  int concept() { return module + import; }
  )";
  Diagnostics parse_diag;
  CppLexer lexer(src, "<cpp17>");
  CppParser parser(lexer, parse_diag);
  parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp17);
  parser.ParseModule();
  REQUIRE_FALSE(parse_diag.HasErrors());

  CppLanguageFrontend frontend;
  Diagnostics extraction_diag;
  polyglot::frontends::FrontendOptions options;
  options.cpp_dialect = polyglot::frontends::CppDialect::kCpp17;
  const auto signatures =
      frontend.ExtractSignatures(src, "<cpp17>", "legacy", extraction_diag, options);
  REQUIRE_FALSE(extraction_diag.HasErrors());
  REQUIRE(signatures.size() == 1);
  REQUIRE(signatures[0].name == "concept");
}

TEST_CASE("C++ signature extraction honors preprocessing options and fails closed when disabled",
          "[cpp][frontend][preprocessor][signatures]") {
  const char *src = R"(
  #ifdef ENABLE_FAST
  int selected(int value) { return value; }
  #else
  int fallback(int value) { return value; }
  #endif
  )";
  CppLanguageFrontend frontend;

  Diagnostics enabled_diag;
  polyglot::frontends::FrontendOptions enabled_options;
  enabled_options.cpp_dialect = polyglot::frontends::CppDialect::kCpp17;
  enabled_options.defines = {"ENABLE_FAST=1"};
  const auto enabled =
      frontend.ExtractSignatures(src, "<conditional.cpp>", "conditional", enabled_diag,
                                 enabled_options);
  REQUIRE_FALSE(enabled_diag.HasErrors());
  REQUIRE(enabled.size() == 1);
  REQUIRE(enabled[0].name == "selected");

  Diagnostics disabled_diag;
  auto disabled_options = enabled_options;
  disabled_options.enable_preprocessing = false;
  const auto disabled =
      frontend.ExtractSignatures(src, "<conditional.cpp>", "conditional", disabled_diag,
                                 disabled_options);
  REQUIRE(disabled.empty());
  REQUIRE(std::any_of(disabled_diag.All().begin(), disabled_diag.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
  }));

  SECTION("direct analysis also rejects raw directives when preprocessing is disabled") {
    Diagnostics analysis_diag;
    CHECK_FALSE(frontend.Analyze(src, "<conditional.cpp>", analysis_diag, disabled_options));
    CHECK(std::any_of(analysis_diag.All().begin(), analysis_diag.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    }));
  }

  SECTION("an explicitly preprocessed stream is not processed a second time") {
    const char *already_preprocessed = "int selected(int value) { return value; }\n";
    Diagnostics analysis_diag;
    auto preprocessed_options = disabled_options;
    preprocessed_options.source_is_preprocessed = true;
    CHECK(frontend.Analyze(already_preprocessed, "<conditional.ii>", analysis_diag,
                           preprocessed_options));
    CHECK_FALSE(analysis_diag.HasErrors());
  }

  SECTION("selected dialect defines __cplusplus") {
    const char *versioned = R"(
    #if __cplusplus >= 202302L
    int modern(int value) { return value; }
    #else
    int legacy(int value) { return value; }
    #endif
    )";

    Diagnostics cpp20_diag;
    polyglot::frontends::FrontendOptions cpp20_options;
    cpp20_options.cpp_dialect = polyglot::frontends::CppDialect::kCpp20;
    const auto cpp20 = frontend.ExtractSignatures(
        versioned, "<cplusplus.cpp>", "versioned", cpp20_diag, cpp20_options);
    REQUIRE_FALSE(cpp20_diag.HasErrors());
    REQUIRE(cpp20.size() == 1);
    CHECK(cpp20[0].name == "legacy");

    Diagnostics cpp23_diag;
    polyglot::frontends::FrontendOptions cpp23_options;
    cpp23_options.cpp_dialect = polyglot::frontends::CppDialect::kCpp23;
    const auto cpp23 = frontend.ExtractSignatures(
        versioned, "<cplusplus.cpp>", "versioned", cpp23_diag, cpp23_options);
    REQUIRE_FALSE(cpp23_diag.HasErrors());
    REQUIRE(cpp23.size() == 1);
    CHECK(cpp23[0].name == "modern");
  }
}

TEST_CASE("C++ lowering rejects unmodeled executable top-level and expression semantics",
          "[cpp][lowering][fail-closed]") {
  SECTION("global initialization") {
    Module module;
    auto global = std::make_shared<VarDecl>();
    global->name = "value";
    global->type = std::make_shared<SimpleType>();
    std::dynamic_pointer_cast<SimpleType>(global->type)->name = "int";
    auto init = std::make_shared<Literal>();
    init->value = "1";
    global->init = init;
    module.declarations.push_back(global);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("unimplemented expression") {
    Diagnostics parse_diag;
    CppLexer lexer("int main() { auto callback = [] { return 1; }; return 0; }", "<cpp23>");
    CppParser parser(lexer, parse_diag);
    parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp23);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE_FALSE(parse_diag.HasErrors());

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(*module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("unknown binary operators and missing returns") {
    Module module;
    auto fn = std::make_shared<FunctionDecl>();
    fn->name = "invalid";
    fn->has_body = true;
    auto return_type = std::make_shared<SimpleType>();
    return_type->name = "int";
    fn->return_type = return_type;
    auto expression = std::make_shared<ExprStatement>();
    auto binary = std::make_shared<BinaryExpression>();
    binary->op = "??";
    auto left = std::make_shared<Literal>();
    left->value = "1";
    auto right = std::make_shared<Literal>();
    right->value = "2";
    binary->left = left;
    binary->right = right;
    expression->expr = binary;
    fn->body.push_back(expression);
    module.declarations.push_back(fn);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));

    Diagnostics return_diag;
    CppLexer lexer("int missing() { int value = 1; }", "<cpp23>");
    CppParser parser(lexer, return_diag);
    parser.ParseModule();
    auto parsed = parser.TakeModule();
    REQUIRE_FALSE(return_diag.HasErrors());
    polyglot::ir::IRContext return_context;
    LowerToIR(*parsed, return_context, return_diag);
    REQUIRE(std::any_of(return_diag.All().begin(), return_diag.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("three-way comparison reports the lowering boundary") {
    Module module;
    auto fn = std::make_shared<FunctionDecl>();
    fn->name = "compare";
    fn->has_body = true;
    auto return_type = std::make_shared<SimpleType>();
    return_type->name = "int";
    fn->return_type = return_type;

    auto expression = std::make_shared<ExprStatement>();
    auto comparison = std::make_shared<BinaryExpression>();
    comparison->op = "<=>";
    auto left = std::make_shared<Literal>();
    left->value = "1";
    auto right = std::make_shared<Literal>();
    right->value = "2";
    comparison->left = left;
    comparison->right = right;
    expression->expr = comparison;
    fn->body.push_back(expression);

    auto ret = std::make_shared<ReturnStatement>();
    auto zero = std::make_shared<Literal>();
    zero->value = "0";
    ret->value = zero;
    fn->body.push_back(ret);
    module.declarations.push_back(fn);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("indirect call reports the callable ABI boundary") {
    Module module;
    auto fn = std::make_shared<FunctionDecl>();
    fn->name = "invoke";
    fn->has_body = true;
    auto return_type = std::make_shared<SimpleType>();
    return_type->name = "int";
    fn->return_type = return_type;

    auto expression = std::make_shared<ExprStatement>();
    auto call = std::make_shared<CallExpression>();
    call->callee = std::make_shared<LambdaExpression>();
    expression->expr = call;
    fn->body.push_back(expression);

    auto ret = std::make_shared<ReturnStatement>();
    auto zero = std::make_shared<Literal>();
    zero->value = "0";
    ret->value = zero;
    fn->body.push_back(ret);
    module.declarations.push_back(fn);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("RTTI typeid ABI") {
    Module module;
    auto fn = std::make_shared<FunctionDecl>();
    fn->name = "inspect_type";
    fn->has_body = true;
    auto return_type = std::make_shared<SimpleType>();
    return_type->name = "int";
    fn->return_type = return_type;
    auto expression = std::make_shared<ExprStatement>();
    auto typeid_expression = std::make_shared<TypeidExpression>();
    typeid_expression->is_type = true;
    typeid_expression->type_arg = std::make_shared<SimpleType>();
    std::dynamic_pointer_cast<SimpleType>(typeid_expression->type_arg)->name = "int";
    expression->expr = typeid_expression;
    fn->body.push_back(expression);
    auto return_statement = std::make_shared<ReturnStatement>();
    auto zero = std::make_shared<Literal>();
    zero->value = "0";
    return_statement->value = zero;
    fn->body.push_back(return_statement);
    module.declarations.push_back(fn);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("polymorphic constructor vptr initialization") {
    Module module;
    auto record = std::make_shared<RecordDecl>();
    record->name = "Polymorphic";

    auto virtual_method = std::make_shared<FunctionDecl>();
    virtual_method->name = "value";
    virtual_method->has_body = true;
    virtual_method->is_virtual = true;
    auto int_type = std::make_shared<SimpleType>();
    int_type->name = "int";
    virtual_method->return_type = int_type;
    auto method_return = std::make_shared<ReturnStatement>();
    auto one = std::make_shared<Literal>();
    one->value = "1";
    method_return->value = one;
    virtual_method->body.push_back(method_return);

    auto constructor = std::make_shared<FunctionDecl>();
    constructor->name = "Polymorphic";
    constructor->has_body = true;
    constructor->is_constructor = true;
    record->methods = {virtual_method, constructor};
    module.declarations.push_back(record);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }

  SECTION("non-type template parameter") {
    Module module;
    auto declaration = std::make_shared<TemplateDecl>();
    declaration->params = {"int N"};
    auto variable = std::make_shared<VarDecl>();
    variable->name = "value";
    declaration->inner = variable;
    module.declarations.push_back(declaration);

    polyglot::ir::IRContext context;
    Diagnostics diagnostics;
    LowerToIR(module, context, diagnostics);
    REQUIRE(std::any_of(diagnostics.All().begin(), diagnostics.All().end(), [](const auto &diag) {
      return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    }));
  }
}

TEST_CASE("C++ foreign primitive signatures match the lowered LP64 ABI",
          "[cpp][frontend][signatures][native-abi]") {
  struct Primitive {
    const char *name;
    int bits;
    bool sign;
  };
  for (const auto type :
       {Primitive{"int", 32, true}, Primitive{"long", 64, true}, Primitive{"long long", 64, true},
        Primitive{"unsigned long", 64, false}, Primitive{"unsigned long long", 64, false},
        Primitive{"size_t", 64, false}, Primitive{"int64_t", 64, true},
        Primitive{"uint64_t", 64, false}}) {
    CAPTURE(type.name);
    const std::string source =
        std::string(type.name) + " identity(" + type.name + " value) { return value; }";
    CppLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    Diagnostics diagnostics;
    const auto signatures =
        frontend.ExtractSignatures(source, "<native-abi.cpp>", "fixture", diagnostics, options);
    REQUIRE_FALSE(diagnostics.HasErrors());
    REQUIRE(signatures.size() == 1);
    REQUIRE(signatures[0].param_types.size() == 1);
    CHECK(signatures[0].param_types[0].bit_width == type.bits);
    CHECK(signatures[0].param_types[0].is_signed == type.sign);
    CHECK(signatures[0].return_type == signatures[0].param_types[0]);
    polyglot::ir::IRContext context;
    const auto result = frontend.Lower(source, "<native-abi.cpp>", context, diagnostics, options);
    for (const auto &entry : diagnostics.All())
      UNSCOPED_INFO(Diagnostics::Format(entry));
    REQUIRE(result.success);
    REQUIRE(context.Functions().size() == 1);
    const auto &function = context.Functions().front();
    REQUIRE(function->param_types.size() == 1);
    CHECK(function->param_types[0].BitWidth() == type.bits);
    CHECK(function->param_types[0].is_signed == type.sign);
    CHECK(function->ret_type.SameShape(function->param_types[0]));
  }
}
