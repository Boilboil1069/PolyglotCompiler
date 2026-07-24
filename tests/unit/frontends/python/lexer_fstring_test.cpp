#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/diagnostics.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"

using polyglot::frontends::TokenKind;
using polyglot::python::PythonLexer;

TEST_CASE("Python f-string is segmented", "[python][lexer]") {
  PythonLexer lexer("f\"a{b}c\"", "<mem>");

  auto t1 = lexer.NextToken();
  REQUIRE(t1.kind == TokenKind::kString);
  REQUIRE(t1.lexeme == "a");
  REQUIRE(t1.raw_lexeme == "__polyglot_python_fstring__");

  auto t2 = lexer.NextToken();
  REQUIRE(t2.kind == TokenKind::kSymbol);
  REQUIRE(t2.lexeme == "{");

  auto t3 = lexer.NextToken();
  REQUIRE(t3.kind == TokenKind::kIdentifier);
  REQUIRE(t3.lexeme == "b");

  auto t4 = lexer.NextToken();
  REQUIRE(t4.kind == TokenKind::kSymbol);
  REQUIRE(t4.lexeme == "}");

  auto t5 = lexer.NextToken();
  REQUIRE(t5.kind == TokenKind::kString);
  REQUIRE(t5.lexeme == "c");
}

TEST_CASE("Python parser retains f-string interpolation expressions", "[python][parser][fstring]") {
  polyglot::frontends::Diagnostics diagnostics;
  PythonLexer lexer("result = f\"a{value}c\"", "<mem>", &diagnostics);
  polyglot::python::PythonParser parser(lexer, diagnostics);
  parser.ParseModule();
  auto module = parser.TakeModule();

  REQUIRE_FALSE(diagnostics.HasErrors());
  REQUIRE(module->body.size() == 1);
  auto assignment = std::dynamic_pointer_cast<polyglot::python::Assignment>(module->body.front());
  REQUIRE(assignment);
  auto formatted = std::dynamic_pointer_cast<polyglot::python::FormattedString>(assignment->value);
  REQUIRE(formatted);
  REQUIRE(formatted->parts.size() == 3);
  REQUIRE(formatted->parts[0].literal == "a");
  auto interpolation =
      std::dynamic_pointer_cast<polyglot::python::Identifier>(formatted->parts[1].expr);
  REQUIRE(interpolation);
  REQUIRE(interpolation->name == "value");
  REQUIRE(formatted->parts[2].literal == "c");
}

TEST_CASE("Python f-string can start with interpolation", "[python][parser][fstring]") {
  polyglot::frontends::Diagnostics diagnostics;
  PythonLexer lexer("result = f\"{value}\"", "<mem>", &diagnostics);
  polyglot::python::PythonParser parser(lexer, diagnostics);
  parser.ParseModule();
  auto module = parser.TakeModule();
  REQUIRE_FALSE(diagnostics.HasErrors());
  auto assignment = std::dynamic_pointer_cast<polyglot::python::Assignment>(module->body.front());
  REQUIRE(assignment);
  auto formatted = std::dynamic_pointer_cast<polyglot::python::FormattedString>(assignment->value);
  REQUIRE(formatted);
  REQUIRE(formatted->parts.size() == 2);
  REQUIRE(formatted->parts[0].literal.empty());
  REQUIRE(formatted->parts[1].expr);
}

TEST_CASE("Python 3.14 t-string retains a distinct template AST",
          "[python][parser][tstring]") {
  polyglot::frontends::Diagnostics diagnostics;
  PythonLexer lexer("result = t\"hello {name}\"", "<mem>", &diagnostics);
  polyglot::python::PythonParser parser(lexer, diagnostics);
  parser.SetPythonVersion(polyglot::frontends::PythonVersion::kPy3_14);
  parser.ParseModule();
  auto module = parser.TakeModule();
  REQUIRE_FALSE(diagnostics.HasErrors());
  auto assignment = std::dynamic_pointer_cast<polyglot::python::Assignment>(module->body.front());
  REQUIRE(assignment);
  auto templated = std::dynamic_pointer_cast<polyglot::python::TemplateString>(assignment->value);
  REQUIRE(templated);
  REQUIRE(templated->parts.size() == 2);
  REQUIRE(templated->parts[0].literal == "hello ");
  REQUIRE(std::dynamic_pointer_cast<polyglot::python::Identifier>(templated->parts[1].expr));
}
