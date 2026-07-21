/**
 * @file     multi_cursor.cpp
 * @brief    Implementation of the multi-cursor model.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editing/multi_cursor.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui {

namespace {

bool IsWordChar(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) || c == '_';
}

std::string SelectedText(const Cursor &c, const std::vector<std::string> &lines) {
  if (c.anchor_line != c.active_line) return {};
  if (c.anchor_line >= lines.size()) return {};
  std::uint32_t lo = std::min(c.anchor_col, c.active_col);
  std::uint32_t hi = std::max(c.anchor_col, c.active_col);
  const std::string &row = lines[c.anchor_line];
  if (lo >= row.size()) return {};
  hi = std::min<std::uint32_t>(hi, row.size());
  return row.substr(lo, hi - lo);
}

}  // namespace

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      out.emplace_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < text.size()) out.emplace_back(text.substr(start));
  return out;
}

std::pair<std::uint32_t, std::uint32_t> WordRangeAt(std::string_view line,
                                                    std::uint32_t col) {
  if (col > line.size()) col = static_cast<std::uint32_t>(line.size());
  // Pick the word at `col`; if `col` sits at the right edge of a
  // word, treat the previous char as the anchor.
  std::uint32_t probe = col;
  if (probe == line.size() && probe > 0 && IsWordChar(line[probe - 1])) --probe;
  if (probe >= line.size() || !IsWordChar(line[probe])) return {col, col};
  std::uint32_t s = probe;
  while (s > 0 && IsWordChar(line[s - 1])) --s;
  std::uint32_t e = probe;
  while (e < line.size() && IsWordChar(line[e])) ++e;
  return {s, e};
}

void MultiCursor::Reset(std::uint32_t line, std::uint32_t col) {
  cursors_.assign(1, Cursor{line, col, line, col});
}

void MultiCursor::AddCursorAt(std::uint32_t line, std::uint32_t col) {
  cursors_.push_back(Cursor{line, col, line, col});
  Normalize();
}

void MultiCursor::AddCursorBelow(const std::vector<std::string> &lines) {
  if (cursors_.empty()) return;
  const Cursor &bottom = cursors_.back();
  if (bottom.active_line + 1 >= lines.size()) return;
  std::uint32_t next = bottom.active_line + 1;
  std::uint32_t col =
      std::min<std::uint32_t>(bottom.active_col, lines[next].size());
  cursors_.push_back(Cursor{next, col, next, col});
  Normalize();
}

void MultiCursor::AddCursorAbove(const std::vector<std::string> &lines) {
  if (cursors_.empty()) return;
  const Cursor &top = cursors_.front();
  if (top.active_line == 0) return;
  std::uint32_t prev = top.active_line - 1;
  std::uint32_t col =
      std::min<std::uint32_t>(top.active_col, lines[prev].size());
  cursors_.push_back(Cursor{prev, col, prev, col});
  Normalize();
}

bool MultiCursor::SelectNextOccurrence(const std::vector<std::string> &lines) {
  if (cursors_.empty() || lines.empty()) return false;

  // Use the last cursor as the search seed; promote the word under
  // it to a selection if it isn't one already.
  Cursor &seed = cursors_.back();
  std::string needle = SelectedText(seed, lines);
  if (needle.empty()) {
    auto wr = WordRangeAt(lines[seed.active_line], seed.active_col);
    if (wr.first == wr.second) return false;
    seed.anchor_col = wr.first;
    seed.active_col = wr.second;
    needle = lines[seed.active_line].substr(wr.first, wr.second - wr.first);
    return true;
  }

  // Search forward from seed end.
  std::uint32_t row = seed.active_line;
  std::uint32_t col = std::max(seed.anchor_col, seed.active_col);
  for (std::size_t step = 0; step <= lines.size(); ++step) {
    if (row >= lines.size()) {
      row = 0;
      col = 0;
    }
    const std::string &r = lines[row];
    if (col <= r.size()) {
      auto pos = r.find(needle, col);
      if (pos != std::string::npos) {
        Cursor n{row, static_cast<std::uint32_t>(pos), row,
                 static_cast<std::uint32_t>(pos + needle.size())};
        cursors_.push_back(n);
        Normalize();
        return true;
      }
    }
    ++row;
    col = 0;
  }
  return false;
}

std::size_t MultiCursor::SelectAllOccurrences(
    const std::vector<std::string> &lines) {
  if (cursors_.empty() || lines.empty()) return cursors_.size();
  Cursor seed = cursors_.front();
  std::string needle = SelectedText(seed, lines);
  if (needle.empty()) {
    auto wr = WordRangeAt(lines[seed.active_line], seed.active_col);
    if (wr.first == wr.second) return cursors_.size();
    needle = lines[seed.active_line].substr(wr.first, wr.second - wr.first);
  }
  cursors_.clear();
  for (std::uint32_t r = 0; r < lines.size(); ++r) {
    const std::string &row = lines[r];
    std::size_t pos = 0;
    while ((pos = row.find(needle, pos)) != std::string::npos) {
      cursors_.push_back(
          Cursor{r, static_cast<std::uint32_t>(pos), r,
                 static_cast<std::uint32_t>(pos + needle.size())});
      pos += needle.size();
    }
  }
  if (cursors_.empty()) cursors_.push_back(seed);
  Normalize();
  return cursors_.size();
}

void MultiCursor::MakeColumnSelection(std::uint32_t start_line,
                                      std::uint32_t start_col,
                                      std::uint32_t end_line,
                                      std::uint32_t end_col,
                                      const std::vector<std::string> &lines) {
  cursors_.clear();
  std::uint32_t lo_line = std::min(start_line, end_line);
  std::uint32_t hi_line = std::max(start_line, end_line);
  std::uint32_t lo_col = std::min(start_col, end_col);
  std::uint32_t hi_col = std::max(start_col, end_col);
  for (std::uint32_t r = lo_line; r <= hi_line && r < lines.size(); ++r) {
    std::uint32_t row_len = static_cast<std::uint32_t>(lines[r].size());
    std::uint32_t a = std::min(lo_col, row_len);
    std::uint32_t b = std::min(hi_col, row_len);
    cursors_.push_back(Cursor{r, a, r, b});
  }
  if (cursors_.empty()) cursors_.push_back(Cursor{lo_line, lo_col, lo_line, lo_col});
}

void MultiCursor::Normalize() {
  std::sort(cursors_.begin(), cursors_.end(), [](const Cursor &a, const Cursor &b) {
    if (a.active_line != b.active_line) return a.active_line < b.active_line;
    return a.active_col < b.active_col;
  });
  cursors_.erase(std::unique(cursors_.begin(), cursors_.end(),
                             [](const Cursor &a, const Cursor &b) {
                               return a.active_line == b.active_line &&
                                      a.active_col == b.active_col &&
                                      a.anchor_line == b.anchor_line &&
                                      a.anchor_col == b.anchor_col;
                             }),
                 cursors_.end());
}

}  // namespace polyglot::tools::ui
