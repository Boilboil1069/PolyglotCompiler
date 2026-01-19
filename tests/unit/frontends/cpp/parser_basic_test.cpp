#include <catch2/catch_test_macros.hpp>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::cpp::CppLexer;
using polyglot::cpp::CppParser;
using namespace polyglot::cpp;

TEST_CASE("C++ parser parses function and return", "[cpp][parser]") {
  const char *src = "int add(int a, int b) { return a + b; }";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionDecl>(mod->declarations[0]);
  REQUIRE(fn);
  REQUIRE(fn->params.size() == 2);
}

TEST_CASE("C++ parser parses if/while/for", "[cpp][parser]") {
  const char *src = R"(
int main() {
  int x = 0;
  if (x) { x = 1; } else { x = 2; }
  while (x) { x = x - 1; }
  for (x = 0; x < 3; x = x + 1) { x = x + 2; }
}
)";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
}
