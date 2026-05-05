/**
 * @file     format_engine_test.cpp
 * @brief    Boundary tests for `FormatPloy`
 *           (demand 2026-04-28-26 §3).
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editing/format_engine.h"

using polyglot::tools::ui::FormatOptions;
using polyglot::tools::ui::FormatPloy;

TEST_CASE("Re-indents brace-nested blocks", "[polyui][format]") {
  FormatOptions o;
  std::string out = FormatPloy(
      "FUNC f() {\n"
      "RETURN 1\n"
      "}\n",
      o);
  REQUIRE(out ==
          "FUNC f() {\n"
          "    RETURN 1\n"
          "}\n");
}

TEST_CASE("Honours hard tabs when insert_spaces=false",
          "[polyui][format]") {
  FormatOptions o;
  o.insert_spaces = false;
  std::string out = FormatPloy("FUNC f() {\nRETURN 1\n}\n", o);
  REQUIRE(out.find("\tRETURN 1") != std::string::npos);
}

TEST_CASE("Trims trailing whitespace and ensures final newline",
          "[polyui][format]") {
  FormatOptions o;
  std::string out = FormatPloy("FUNC f() {   \n  body  \n}", o);
  // trailing spaces gone; final newline added
  REQUIRE(out.back() == '\n');
  REQUIRE(out.find("body  ") == std::string::npos);
}

TEST_CASE("Empty input yields empty (with optional final NL)",
          "[polyui][format]") {
  FormatOptions o;
  REQUIRE(FormatPloy("", o) == "\n");
  o.insert_final_newline = false;
  REQUIRE(FormatPloy("", o).empty());
}
