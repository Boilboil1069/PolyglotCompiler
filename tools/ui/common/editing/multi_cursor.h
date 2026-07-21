/**
 * @file     multi_cursor.h
 * @brief    Multi-cursor + column-selection model
 *           (demand 2026-04-28-26 §1).
 *
 * Pure data model — Qt-free so Catch2 can drive it directly.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

/// A cursor with optional selection (anchor != active).
struct Cursor {
  std::uint32_t anchor_line{0};
  std::uint32_t anchor_col{0};
  std::uint32_t active_line{0};
  std::uint32_t active_col{0};

  bool HasSelection() const {
    return anchor_line != active_line || anchor_col != active_col;
  }
};

/// Multi-cursor controller.  Cursors are held in document-order
/// (sorted by active position) and de-duplicated on every mutation.
class MultiCursor {
 public:
  /// Constructs with a single cursor at (0,0).
  MultiCursor() : cursors_{Cursor{}} {}

  const std::vector<Cursor> &Cursors() const { return cursors_; }
  std::size_t Size() const { return cursors_.size(); }

  /// Replaces the cursor list with a single primary cursor.
  void Reset(std::uint32_t line, std::uint32_t col);

  /// Adds a secondary cursor at `(line, col)`; merges with an
  /// existing cursor at the same position.
  void AddCursorAt(std::uint32_t line, std::uint32_t col);

  /// Adds a cursor one line below the current bottom cursor at the
  /// same column (clamped to that line's length).
  void AddCursorBelow(const std::vector<std::string> &lines);

  /// Adds a cursor one line above the current top cursor.
  void AddCursorAbove(const std::vector<std::string> &lines);

  /// Selects the next occurrence of the primary cursor's selection
  /// (or the word under it) and adds it as a new cursor.  Returns
  /// `true` if a new occurrence was added.
  bool SelectNextOccurrence(const std::vector<std::string> &lines);

  /// Selects every occurrence of the primary cursor's word in the
  /// document.  Returns the resulting cursor count.
  std::size_t SelectAllOccurrences(const std::vector<std::string> &lines);

  /// Replaces the cursor list with column-selection cursors that
  /// span every line between `(start_line, start_col)` and
  /// `(end_line, end_col)`.  The selection is rectangular: each line
  /// contributes one cursor whose selection is the [min,max] column
  /// pair (clamped to that line's length).
  void MakeColumnSelection(std::uint32_t start_line,
                           std::uint32_t start_col,
                           std::uint32_t end_line,
                           std::uint32_t end_col,
                           const std::vector<std::string> &lines);

 private:
  std::vector<Cursor> cursors_;

  void Normalize();
};

/// Splits `text` into lines (no trailing empty line synthesised when
/// `text` ends with `\n`).
std::vector<std::string> SplitLines(std::string_view text);

/// Returns the word boundaries `[start, end)` around `col` on
/// `line`, treating `[A-Za-z0-9_]` as word characters.  When `col`
/// is on a non-word character the empty range `(col, col)` is
/// returned.
std::pair<std::uint32_t, std::uint32_t> WordRangeAt(std::string_view line,
                                                    std::uint32_t col);

}  // namespace polyglot::tools::ui
