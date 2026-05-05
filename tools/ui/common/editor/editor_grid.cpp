/**
 * @file     editor_grid.cpp
 * @brief    Implementation of `EditorGrid` (demand 2026-04-28-25 §1).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editor/editor_grid.h"

#include <stdexcept>

namespace polyglot::tools::ui {

EditorGrid::EditorGrid() : cells_(1) {}

EditorGroup &EditorGrid::Cell(std::size_t row, std::size_t col) {
  if (row >= rows_ || col >= cols_) {
    throw std::out_of_range("EditorGrid::Cell");
  }
  return cells_[Index(row, col)];
}

const EditorGroup &EditorGrid::Cell(std::size_t row, std::size_t col) const {
  if (row >= rows_ || col >= cols_) {
    throw std::out_of_range("EditorGrid::Cell");
  }
  return cells_[Index(row, col)];
}

bool EditorGrid::Split(std::size_t row, std::size_t col,
                       SplitOrientation orientation) {
  if (row >= rows_ || col >= cols_) return false;
  if (orientation == SplitOrientation::kVertical) {
    if (cols_ >= kMaxCols) return false;
    std::vector<EditorGroup> next(rows_ * (cols_ + 1));
    for (std::size_t r = 0; r < rows_; ++r) {
      for (std::size_t c = 0; c < cols_; ++c) {
        const std::size_t target_col = (c <= col) ? c : c + 1;
        next[r * (cols_ + 1) + target_col] = std::move(cells_[r * cols_ + c]);
      }
    }
    cells_ = std::move(next);
    ++cols_;
    active_col_ = col + 1;  // focus the freshly created cell
    active_row_ = row;
  } else {
    if (rows_ >= kMaxRows) return false;
    std::vector<EditorGroup> next((rows_ + 1) * cols_);
    for (std::size_t r = 0; r < rows_; ++r) {
      const std::size_t target_row = (r <= row) ? r : r + 1;
      for (std::size_t c = 0; c < cols_; ++c) {
        next[target_row * cols_ + c] = std::move(cells_[r * cols_ + c]);
      }
    }
    cells_ = std::move(next);
    ++rows_;
    active_row_ = row + 1;
    active_col_ = col;
  }
  return true;
}

bool EditorGrid::Merge(std::size_t row, std::size_t col,
                       SplitOrientation orientation) {
  if (row >= rows_ || col >= cols_) return false;
  if (orientation == SplitOrientation::kVertical) {
    if (cols_ <= 1) return false;
    std::vector<EditorGroup> next(rows_ * (cols_ - 1));
    for (std::size_t r = 0; r < rows_; ++r) {
      std::size_t out_c = 0;
      for (std::size_t c = 0; c < cols_; ++c) {
        if (c == col) continue;
        next[r * (cols_ - 1) + out_c++] = std::move(cells_[r * cols_ + c]);
      }
    }
    cells_ = std::move(next);
    --cols_;
    if (active_col_ >= cols_) active_col_ = cols_ - 1;
  } else {
    if (rows_ <= 1) return false;
    std::vector<EditorGroup> next((rows_ - 1) * cols_);
    std::size_t out_r = 0;
    for (std::size_t r = 0; r < rows_; ++r) {
      if (r == row) continue;
      for (std::size_t c = 0; c < cols_; ++c) {
        next[out_r * cols_ + c] = std::move(cells_[r * cols_ + c]);
      }
      ++out_r;
    }
    cells_ = std::move(next);
    --rows_;
    if (active_row_ >= rows_) active_row_ = rows_ - 1;
  }
  return true;
}

bool EditorGrid::MoveTab(std::size_t from_row, std::size_t from_col,
                         std::size_t tab_index, std::size_t to_row,
                         std::size_t to_col) {
  if (from_row >= rows_ || from_col >= cols_) return false;
  if (to_row >= rows_ || to_col >= cols_) return false;
  EditorGroup &src = cells_[Index(from_row, from_col)];
  if (tab_index >= src.Tabs().size()) return false;
  if (from_row == to_row && from_col == to_col) return true;
  EditorTab moved = src.Tabs()[tab_index];
  src.Close(tab_index);
  cells_[Index(to_row, to_col)].Open(std::move(moved));
  active_row_ = to_row;
  active_col_ = to_col;
  return true;
}

void EditorGrid::Focus(std::size_t row, std::size_t col) {
  if (row >= rows_ || col >= cols_) return;
  active_row_ = row;
  active_col_ = col;
}

}  // namespace polyglot::tools::ui
