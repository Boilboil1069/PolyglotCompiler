#include <catch2/catch_test_macros.hpp>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;
using namespace polyglot::python;

TEST_CASE("Python parser parses if/assign", "[python][parser]") {
  const char *src = R"(
if x:
    y = 1
else:
    y = 2
)";
  Diagnostics diag;
  PythonLexer lexer(src, "<mem>", &diag);
  PythonParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->body.size() == 1);
  auto ifs = std::dynamic_pointer_cast<IfStatement>(mod->body[0]);
  REQUIRE(ifs);
  REQUIRE(ifs->then_body.size() == 1);
  REQUIRE(ifs->else_body.size() == 1);
}

TEST_CASE("Python parser parses function with call", "[python][parser]") {
  const char *src = "def f(a, b):\n    return a + b\n";
  Diagnostics diag;
  PythonLexer lexer(src, "<mem>", &diag);
  PythonParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->body.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[0]);
  REQUIRE(fn);
  REQUIRE(fn->params.size() == 2);
  REQUIRE(fn->body.size() == 1);
}
