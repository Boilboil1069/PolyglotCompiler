/**
 * @file     multi_cursor_test.cpp
 * @brief    Boundary tests for the multi-cursor model
 *           (demand 2026-04-28-26 §1).
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editing/multi_cursor.h"

using polyglot::tools::ui::Cursor;
using polyglot::tools::ui::MultiCursor;
using polyglot::tools::ui::SplitLines;
using polyglot::tools::ui::WordRangeAt;

TEST_CASE("AddCursorAt deduplicates and sorts", "[polyui][multi_cursor]") {
  MultiCursor mc;
  mc.Reset(2, 4);
  mc.AddCursorAt(0, 0);
  mc.AddCursorAt(2, 4);  // duplicate
  mc.AddCursorAt(1, 7);
  REQUIRE(mc.Size() == 3);
  REQUIRE(mc.Cursors().front().active_line == 0);
  REQUIRE(mc.Cursors().back().active_line == 2);
}

TEST_CASE("AddCursorBelow clamps column", "[polyui][multi_cursor]") {
  auto lines = SplitLines("hello world\nhi\nfoo bar baz");
  MultiCursor mc;
  mc.Reset(0, 8);
  mc.AddCursorBelow(lines);
  REQUIRE(mc.Size() == 2);
  // Line 1 ("hi") only has 2 chars — column should clamp to 2.
  REQUIRE(mc.Cursors().back().active_line == 1);
  REQUIRE(mc.Cursors().back().active_col == 2);
}

TEST_CASE("SelectNextOccurrence selects word then next match",
          "[polyui][multi_cursor]") {
  auto lines = SplitLines("foo bar foo baz foo");
  MultiCursor mc;
  mc.Reset(0, 1);  // inside first "foo"
  REQUIRE(mc.SelectNextOccurrence(lines));  // promotes to selection
  REQUIRE(mc.Size() == 1);
  REQUIRE(mc.Cursors().front().HasSelection());
  REQUIRE(mc.SelectNextOccurrence(lines));  // adds second occurrence
  REQUIRE(mc.Size() == 2);
}

TEST_CASE("SelectAllOccurrences finds every match", "[polyui][multi_cursor]") {
  auto lines = SplitLines("x y x y x");
  MultiCursor mc;
  mc.Reset(0, 0);
  auto n = mc.SelectAllOccurrences(lines);
  REQUIRE(n == 3);
}

TEST_CASE("MakeColumnSelection produces one cursor per line",
          "[polyui][multi_cursor]") {
  auto lines = SplitLines("AAAA\nBB\nCCCCCC");
  MultiCursor mc;
  mc.MakeColumnSelection(0, 1, 2, 3, lines);
  REQUIRE(mc.Size() == 3);
  // Line 1 only has 2 chars, so column [1,3] clamps to [1,2].
  REQUIRE(mc.Cursors()[1].anchor_col == 1);
  REQUIRE(mc.Cursors()[1].active_col == 2);
}

TEST_CASE("WordRangeAt at end of line returns trailing word",
          "[polyui][multi_cursor]") {
  auto wr = WordRangeAt("hello world", 11);
  REQUIRE(wr.first == 6);
  REQUIRE(wr.second == 11);
}
