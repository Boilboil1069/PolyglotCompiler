#include <catch2/catch_test_macros.hpp>

#include "frontends/python/include/python_lexer.h"

using polyglot::frontends::TokenKind;
using polyglot::python::PythonLexer;

TEST_CASE("Python f-string is segmented", "[python][lexer]") {
  PythonLexer lexer("f\"a{b}c\"", "<mem>");

  auto t1 = lexer.NextToken();
  REQUIRE(t1.kind == TokenKind::kString);
  REQUIRE(t1.lexeme == "a");

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
