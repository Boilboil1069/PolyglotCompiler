/**
 * @file     editor_config_test.cpp
 * @brief    Boundary tests for the EditorConfig parser
 *           (demand 2026-04-28-26 §5).
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editing/editor_config.h"

using polyglot::tools::ui::EditorConfigDocument;
using polyglot::tools::ui::EditorConfigMatch;

TEST_CASE("Parses sections and resolves per-file settings",
          "[polyui][editorconfig]") {
  EditorConfigDocument doc;
  REQUIRE(doc.Parse(
      "root = true\n"
      "\n"
      "[*]\n"
      "indent_style = space\n"
      "indent_size = 2\n"
      "end_of_line = lf\n"
      "insert_final_newline = true\n"
      "\n"
      "[*.ploy]\n"
      "indent_size = 4\n"
      "trim_trailing_whitespace = true\n"));
  REQUIRE(doc.IsRoot());
  auto s = doc.ResolveFor("src/main.ploy");
  REQUIRE(s.indent_style == "space");
  REQUIRE(s.indent_size == 4u);  // overridden by [*.ploy]
  REQUIRE(s.end_of_line == "lf");
  REQUIRE(s.insert_final_newline.value());
  REQUIRE(s.trim_trailing_whitespace.value());
}

TEST_CASE("Settings unrelated section leaves defaults unset",
          "[polyui][editorconfig]") {
  EditorConfigDocument doc;
  doc.Parse("[*.md]\nindent_size = 2\n");
  auto s = doc.ResolveFor("src/main.ploy");
  REQUIRE_FALSE(s.indent_size.has_value());
  REQUIRE(s.ToStatusString().empty());
}

TEST_CASE("Glob matcher handles `**` and brace alternation",
          "[polyui][editorconfig]") {
  REQUIRE(EditorConfigMatch("**/*.ploy", "src/sub/main.ploy"));
  // Unanchored patterns match against the basename per the spec.
  REQUIRE(EditorConfigMatch("*.ploy", "src/main.ploy"));
  REQUIRE_FALSE(EditorConfigMatch("*.ploy", "src/main.cpp"));
  REQUIRE(EditorConfigMatch("*.{cpp,hpp}", "code.cpp"));
  REQUIRE(EditorConfigMatch("*.{cpp,hpp}", "code.hpp"));
  REQUIRE_FALSE(EditorConfigMatch("*.{cpp,hpp}", "code.cc"));
}
