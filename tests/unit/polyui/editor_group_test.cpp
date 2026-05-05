/**
 * @file     editor_group_test.cpp
 * @brief    Unit tests for EditorGroup + EditorGrid
 *           (demand 2026-04-28-25 §1).
 *
 * @ingroup  Tests / unit / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editor/editor_grid.h"
#include "tools/ui/common/editor/editor_group.h"

using polyglot::tools::ui::EditorGrid;
using polyglot::tools::ui::EditorGroup;
using polyglot::tools::ui::EditorTab;
using polyglot::tools::ui::SplitOrientation;

TEST_CASE("EditorGroup opens, activates and re-uses tabs",
          "[polyui][editor_group]") {
  EditorGroup g;
  REQUIRE_FALSE(g.ActiveIndex().has_value());
  g.Open({"/a", "a", false, false});
  g.Open({"/b", "b", false, false});
  REQUIRE(g.Tabs().size() == 2);
  REQUIRE(g.ActiveIndex().value() == 1);
  // Re-opening focuses the existing tab without duplicating.
  g.Open({"/a", "a", false, false});
  REQUIRE(g.Tabs().size() == 2);
  REQUIRE(g.ActiveIndex().value() == 0);
}

TEST_CASE("EditorGroup pinning keeps pinned tabs at the front",
          "[polyui][editor_group][pin]") {
  EditorGroup g;
  g.Open({"/a", "a", false, false});
  g.Open({"/b", "b", false, false});
  g.Open({"/c", "c", false, false});
  // Pin the middle tab → should hop to front.
  g.TogglePin(1);
  REQUIRE(g.Tabs()[0].path == "/b");
  REQUIRE(g.Tabs()[0].pinned);
  REQUIRE_FALSE(g.Tabs()[1].pinned);
}

TEST_CASE("EditorGroup CloseOthers preserves pinned + chosen tab",
          "[polyui][editor_group][close]") {
  EditorGroup g;
  g.Open({"/a", "a", false, false});
  g.Open({"/b", "b", true, false});      // pinned
  g.Open({"/c", "c", false, false});
  g.Open({"/d", "d", false, false});
  g.CloseOthers(g.Find("/c"));
  REQUIRE(g.Tabs().size() == 2);
  REQUIRE(g.Tabs()[0].path == "/b");      // pinned survived
  REQUIRE(g.Tabs()[1].path == "/c");      // kept
}

TEST_CASE("EditorGroup CloseToRight keeps left + pinned tabs",
          "[polyui][editor_group][close]") {
  EditorGroup g;
  g.Open({"/a", "a", false, false});
  g.Open({"/b", "b", false, false});
  g.Open({"/c", "c", false, false});
  g.Open({"/d", "d", true, false});      // pinned (front-shifted)
  // After pin, order is: d, a, b, c.  Close-to-right of /a leaves d,a only.
  g.CloseToRight(g.Find("/a"));
  REQUIRE(g.Tabs().size() == 2);
  REQUIRE(g.Tabs()[0].path == "/d");
  REQUIRE(g.Tabs()[1].path == "/a");
}

TEST_CASE("EditorGrid splits and merges within 4x4 limit",
          "[polyui][editor_grid][split]") {
  EditorGrid grid;
  REQUIRE(grid.Rows() == 1);
  REQUIRE(grid.Cols() == 1);
  REQUIRE(grid.Split(0, 0, SplitOrientation::kVertical));
  REQUIRE(grid.Cols() == 2);
  REQUIRE(grid.Split(0, 0, SplitOrientation::kHorizontal));
  REQUIRE(grid.Rows() == 2);
  // Saturate and reject further splits.
  for (int i = 0; i < 6; ++i) (void)grid.Split(0, 0, SplitOrientation::kVertical);
  for (int i = 0; i < 6; ++i) (void)grid.Split(0, 0, SplitOrientation::kHorizontal);
  REQUIRE(grid.Rows() <= EditorGrid::kMaxRows);
  REQUIRE(grid.Cols() <= EditorGrid::kMaxCols);
  // Merge back.
  REQUIRE(grid.Merge(0, 0, SplitOrientation::kHorizontal));
  REQUIRE(grid.Rows() <= EditorGrid::kMaxRows);
}

TEST_CASE("EditorGrid moves tabs across cells",
          "[polyui][editor_grid][move]") {
  EditorGrid grid;
  grid.Cell(0, 0).Open({"/file", "file", false, false});
  REQUIRE(grid.Split(0, 0, SplitOrientation::kVertical));
  REQUIRE(grid.Cell(0, 0).Tabs().size() == 1);
  REQUIRE(grid.Cell(0, 1).Tabs().empty());
  REQUIRE(grid.MoveTab(0, 0, 0, 0, 1));
  REQUIRE(grid.Cell(0, 0).Tabs().empty());
  REQUIRE(grid.Cell(0, 1).Tabs().size() == 1);
  REQUIRE(grid.Cell(0, 1).Tabs()[0].path == "/file");
}
