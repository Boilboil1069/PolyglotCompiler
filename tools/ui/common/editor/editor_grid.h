/**
 * @file     editor_grid.h
 * @brief    Grid model for editor split panes
 *           (demand 2026-04-28-25 §1).
 *
 * Up to a 4×4 grid of @ref EditorGroup instances. Splits are created
 * by inserting a new column or row; merging is the inverse.  The
 * grid keeps track of the currently focused cell so keyboard
 * navigation and tab drops can target it deterministically.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstddef>
#include <vector>

#include "tools/ui/common/editor/editor_group.h"

namespace polyglot::tools::ui {

enum class SplitOrientation { kHorizontal, kVertical };

class EditorGrid {
 public:
  static constexpr std::size_t kMaxRows = 4;
  static constexpr std::size_t kMaxCols = 4;

  EditorGrid();

  std::size_t Rows() const { return rows_; }
  std::size_t Cols() const { return cols_; }

  /// Active cell coordinates.  Always within `[0, Rows) × [0, Cols)`.
  std::size_t ActiveRow() const { return active_row_; }
  std::size_t ActiveCol() const { return active_col_; }

  EditorGroup &Cell(std::size_t row, std::size_t col);
  const EditorGroup &Cell(std::size_t row, std::size_t col) const;

  EditorGroup &ActiveCell() { return Cell(active_row_, active_col_); }
  const EditorGroup &ActiveCell() const {
    return Cell(active_row_, active_col_);
  }

  /// Split the cell at `(row, col)` along @p orientation, inserting a
  /// new empty cell adjacent to it.  Returns `false` when the grid is
  /// already at its maximum dimension along the requested axis.
  bool Split(std::size_t row, std::size_t col, SplitOrientation orientation);

  /// Drop the row or column containing `(row, col)`.  No-op when the
  /// grid is already 1×1.  Tabs in the dropped cells are discarded.
  bool Merge(std::size_t row, std::size_t col, SplitOrientation orientation);

  /// Move a tab from one cell to another.  When `to` is empty the tab
  /// becomes that cell's only tab; otherwise it is appended.  Returns
  /// false on out-of-range coordinates or when the source has no tab
  /// at `tab_index`.
  bool MoveTab(std::size_t from_row, std::size_t from_col,
               std::size_t tab_index, std::size_t to_row, std::size_t to_col);

  void Focus(std::size_t row, std::size_t col);

 private:
  std::size_t rows_{1};
  std::size_t cols_{1};
  std::size_t active_row_{0};
  std::size_t active_col_{0};
  std::vector<EditorGroup> cells_;  ///< row-major, size = rows_ * cols_

  std::size_t Index(std::size_t row, std::size_t col) const {
    return row * cols_ + col;
  }
};

}  // namespace polyglot::tools::ui
