/**
 * @file     snippet_engine_test.cpp
 * @brief    Boundary tests for snippet expansion + library loading
 *           (demand 2026-04-28-26 §4).
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editing/snippet_engine.h"

using polyglot::tools::ui::ExpandSnippet;
using polyglot::tools::ui::SnippetEntry;
using polyglot::tools::ui::SnippetLibrary;

TEST_CASE("Tabstops and placeholders expand", "[polyui][snippet]") {
  std::map<std::string, std::string> vars;
  auto e = ExpandSnippet("FUNC ${1:name}() { $0 }", vars);
  REQUIRE(e.text == "FUNC name() {  }");
  REQUIRE(e.tabstops.size() == 2);
  REQUIRE(e.tabstops[0].index == 1);
  REQUIRE(e.tabstops[0].length == 4);  // "name"
  REQUIRE(e.tabstops[1].index == 0);
}

TEST_CASE("Choices use first option as default", "[polyui][snippet]") {
  std::map<std::string, std::string> vars;
  auto e = ExpandSnippet("kind=${1|let,var,const|}", vars);
  REQUIRE(e.text == "kind=let");
  REQUIRE(e.tabstops.size() == 1);
  REQUIRE(e.tabstops[0].choices.size() == 3);
}

TEST_CASE("Variables substitute from map", "[polyui][snippet]") {
  std::map<std::string, std::string> vars{
      {"CURRENT_DATE", "2026-05-05"},
      {"TM_FILENAME", "main.ploy"},
  };
  auto e = ExpandSnippet("// $CURRENT_DATE — ${TM_FILENAME}", vars);
  REQUIRE(e.text == "// 2026-05-05 — main.ploy");
}

TEST_CASE("Library loads JSON object map", "[polyui][snippet]") {
  SnippetLibrary lib;
  std::size_t n = lib.LoadJson(R"({
    "Function": {
      "prefix": "fn",
      "body": ["FUNC ${1:name}() {", "    $0", "}"],
      "description": "Function template"
    },
    "Print": { "prefix": "pr", "body": "PRINT $1" }
  })");
  REQUIRE(n == 2);
  auto matches = lib.Match("f");
  REQUIRE(matches.size() == 1);
  REQUIRE(matches.front().name == "Function");
}
