/**
 * @file     tree_sitter_runtime_test.cpp
 * @brief    Unit tests for the tree-sitter-shaped parsing runtime
 *           (demand 2026-04-28-24).
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "tools/polyls/grammar/grammar_descriptor.h"
#include "tools/ui/common/syntax/tree_sitter_runtime.h"

namespace gr = polyglot::polyls::grammar;
namespace tsr = polyglot::polyls::ts;

TEST_CASE("grammar table covers ploy and the five host languages",
          "[polyls][semantic]") {
  REQUIRE(gr::FindGrammar("ploy") != nullptr);
  REQUIRE(gr::FindGrammar("cpp") != nullptr);
  REQUIRE(gr::FindGrammar("c++") != nullptr);  // alias
  REQUIRE(gr::FindGrammar("python") != nullptr);
  REQUIRE(gr::FindGrammar("rust") != nullptr);
  REQUIRE(gr::FindGrammar("java") != nullptr);
  REQUIRE(gr::FindGrammar("csharp") != nullptr);
  REQUIRE(gr::FindGrammar("dotnet") != nullptr);  // alias
  REQUIRE(gr::FindGrammar("javascript") == nullptr);
}

TEST_CASE("parser produces semantic tokens for a Ploy module",
          "[polyls][semantic]") {
  const std::string src =
      "FUNC compute() -> INT {\n"
      "    LET answer = 42;\n"
      "    RETURN answer;\n"
      "}\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  const auto &toks = tree->Tokens();
  REQUIRE(!toks.empty());
  // "FUNC" must be classified as keyword (type index 6).
  bool saw_func_kw = false;
  bool saw_int_type = false;
  bool saw_number = false;
  for (const auto &t : toks) {
    if (t.line == 0 && t.start_char == 0) {
      saw_func_kw = (t.type_index == 6);
    }
    if (t.line == 0 && t.length == 3 && t.type_index == 1) {
      saw_int_type = true;  // INT primitive
    }
    if (t.type_index == 9) saw_number = true;  // 42
  }
  REQUIRE(saw_func_kw);
  REQUIRE(saw_int_type);
  REQUIRE(saw_number);
}

TEST_CASE("LINK / IMPORT directives are emitted as keyword+definition",
          "[polyls][semantic]") {
  const std::string src = "LINK cpp \"foo.cpp\";\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  bool saw_link = false;
  for (const auto &t : tree->Tokens()) {
    if (t.line == 0 && t.start_char == 0 && t.length == 4) {
      // declaration | definition
      REQUIRE((t.modifier_mask & gr::kModDeclaration) != 0);
      REQUIRE((t.modifier_mask & gr::kModDefinition) != 0);
      saw_link = true;
    }
  }
  REQUIRE(saw_link);
}

TEST_CASE("string literals do not leak into identifier tokens",
          "[polyls][semantic]") {
  const std::string src = "LET msg = \"FUNC inside string\";\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  // No keyword-typed token may overlap the string range.
  for (const auto &t : tree->Tokens()) {
    if (t.type_index == 6 /*keyword*/ && t.line == 0) {
      REQUIRE(t.start_char < src.find('"'));
    }
  }
}

TEST_CASE("brace-style folding emits one region per nested block",
          "[polyls][semantic]") {
  const std::string src =
      "FUNC outer() -> VOID {\n"
      "    IF (x) {\n"
      "        RETURN;\n"
      "    }\n"
      "}\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  REQUIRE(tree->Folds().size() == 2);
}

TEST_CASE("python folding follows indentation",
          "[polyls][semantic]") {
  const std::string src =
      "def hello():\n"
      "    print(\"hi\")\n"
      "    return 1\n";
  auto tree = tsr::Parse("python", src);
  REQUIRE(tree);
  REQUIRE(tree->Folds().size() == 1);
  REQUIRE(tree->Folds()[0].start_line == 0);
  REQUIRE(tree->Folds()[0].end_line == 2);
}

TEST_CASE("outline surfaces top-level FUNC/STRUCT in Ploy",
          "[polyls][semantic]") {
  const std::string src =
      "STRUCT Point { x: INT; y: INT; }\n"
      "FUNC area() -> INT { RETURN 0; }\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  REQUIRE(tree->Outline().size() == 2);
  REQUIRE(tree->Outline()[0].name == "Point");
  REQUIRE(tree->Outline()[0].kind == "struct");
  REQUIRE(tree->Outline()[1].name == "area");
  REQUIRE(tree->Outline()[1].kind == "function");
}

TEST_CASE("smart-select widens token → line → block → document",
          "[polyls][semantic]") {
  const std::string src =
      "FUNC compute() -> INT {\n"
      "    LET x = 7;\n"
      "}\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  // Inside identifier `x` on line 1 (column 9).
  auto ranges = tree->SmartSelect(1, 9);
  REQUIRE(ranges.size() >= 3);
  // First range covers just the token.
  REQUIRE(ranges[0].start_line == 1);
  REQUIRE(ranges[0].end_line == 1);
  // Final range spans the whole document.
  REQUIRE(ranges.back().start_line == 0);
  REQUIRE(ranges.back().end_line >= 2);
}

TEST_CASE("incremental edit reparses to a consistent tree",
          "[polyls][semantic]") {
  const std::string src = "FUNC foo() -> VOID {}\n";
  auto tree = tsr::Parse("ploy", src);
  REQUIRE(tree);
  const auto before = tree->Outline();
  REQUIRE(before.size() == 1);
  REQUIRE(before[0].name == "foo");
  // Replace the function name in place.
  const auto pos = src.find("foo");
  tree->Edit(pos, pos + 3, "bar");
  const auto after = tree->Outline();
  REQUIRE(after.size() == 1);
  REQUIRE(after[0].name == "bar");
}

TEST_CASE("delta-encoding is monotone and round-trip stable",
          "[polyls][semantic]") {
  std::vector<tsr::SemanticToken> toks = {
      {0, 0, 4, 6, 0},
      {0, 5, 7, 3, 0},
      {2, 4, 3, 4, 0},
  };
  auto data = tsr::EncodeSemanticTokens(toks);
  REQUIRE(data.size() == toks.size() * 5);
  // First five values are the absolute coords of the first token.
  REQUIRE(data[0] == 0);
  REQUIRE(data[1] == 0);
  REQUIRE(data[2] == 4);
  // Second token is on the same line: deltaLine=0, deltaChar=5.
  REQUIRE(data[5] == 0);
  REQUIRE(data[6] == 5);
  // Third token is two lines below, absolute char=4.
  REQUIRE(data[10] == 2);
  REQUIRE(data[11] == 4);
}

TEST_CASE("range encoding clips out-of-range tokens",
          "[polyls][semantic]") {
  std::vector<tsr::SemanticToken> toks = {
      {0, 0, 3, 6, 0},
      {5, 0, 3, 6, 0},
      {10, 0, 3, 6, 0},
  };
  auto data = tsr::EncodeSemanticTokensRange(toks, 4, 9);
  REQUIRE(data.size() == 5);  // exactly one token survived
  REQUIRE(data[0] == 5);      // first delta is absolute line 5
}

TEST_CASE("decode is the exact inverse of encode",
          "[polyls][semantic]") {
  std::vector<tsr::SemanticToken> toks = {
      {0, 0, 4, 6, 1},
      {0, 5, 7, 3, 0},
      {2, 4, 3, 4, 2},
      {5, 8, 2, 9, 0},
  };
  const auto data = tsr::EncodeSemanticTokens(toks);
  const auto round = tsr::DecodeSemanticTokens(data);
  REQUIRE(round.size() == toks.size());
  for (std::size_t i = 0; i < toks.size(); ++i) {
    REQUIRE(round[i].line == toks[i].line);
    REQUIRE(round[i].start_char == toks[i].start_char);
    REQUIRE(round[i].length == toks[i].length);
    REQUIRE(round[i].type_index == toks[i].type_index);
    REQUIRE(round[i].modifier_mask == toks[i].modifier_mask);
  }
}
