/**
 * @file     folding_model.cpp
 * @brief    Implementation of `ComputeFolds`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editing/folding_model.h"

#include <algorithm>
#include <cctype>
#include <stack>
#include <string>

namespace polyglot::tools::ui {

namespace {

bool LineStartsWith(std::string_view line, std::string_view marker) {
  std::size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  return line.size() - i >= marker.size() &&
         line.compare(i, marker.size(), marker) == 0;
}

}  // namespace

std::vector<FoldRange> ComputeFolds(std::string_view text) {
  // Split into line views with absolute offsets.
  std::vector<std::string_view> lines;
  {
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\n') {
        lines.emplace_back(text.substr(start, i - start));
        start = i + 1;
      }
    }
    if (start <= text.size()) lines.emplace_back(text.substr(start));
  }

  std::vector<FoldRange> folds;
  std::stack<std::uint32_t> brace_open_lines;
  std::stack<std::uint32_t> region_open_lines;
  bool in_block_comment = false;
  std::uint32_t comment_open_line = 0;

  for (std::uint32_t lineno = 0; lineno < lines.size(); ++lineno) {
    std::string_view ln = lines[lineno];

    // Region markers — handled before character scanning so they win
    // over a brace happening on the same line.
    if (LineStartsWith(ln, "// region") || LineStartsWith(ln, "//region")) {
      region_open_lines.push(lineno);
    } else if (LineStartsWith(ln, "// endregion") ||
               LineStartsWith(ln, "//endregion")) {
      if (!region_open_lines.empty()) {
        std::uint32_t s = region_open_lines.top();
        region_open_lines.pop();
        if (lineno > s) {
          folds.push_back({FoldKind::kRegion, s, lineno});
        }
      }
    }

    // Walk the line looking at braces and comment markers.  Skip
    // // line comments and string literals to avoid false positives.
    bool in_string = false;
    char string_quote = '\0';
    for (std::size_t i = 0; i < ln.size(); ++i) {
      char c = ln[i];
      if (in_block_comment) {
        if (c == '*' && i + 1 < ln.size() && ln[i + 1] == '/') {
          in_block_comment = false;
          if (lineno > comment_open_line) {
            folds.push_back({FoldKind::kComment, comment_open_line, lineno});
          }
          ++i;
        }
        continue;
      }
      if (in_string) {
        if (c == '\\' && i + 1 < ln.size()) { ++i; continue; }
        if (c == string_quote) in_string = false;
        continue;
      }
      if (c == '/' && i + 1 < ln.size() && ln[i + 1] == '/') break;  // line comment
      if (c == '/' && i + 1 < ln.size() && ln[i + 1] == '*') {
        in_block_comment = true;
        comment_open_line = lineno;
        ++i;
        continue;
      }
      if (c == '"' || c == '\'') {
        in_string = true;
        string_quote = c;
        continue;
      }
      if (c == '{') {
        brace_open_lines.push(lineno);
      } else if (c == '}') {
        if (!brace_open_lines.empty()) {
          std::uint32_t s = brace_open_lines.top();
          brace_open_lines.pop();
          if (lineno > s) {
            folds.push_back({FoldKind::kBlock, s, lineno});
          }
        }
      }
    }
  }

  std::sort(folds.begin(), folds.end(), [](const FoldRange &a, const FoldRange &b) {
    if (a.start_line != b.start_line) return a.start_line < b.start_line;
    return a.end_line > b.end_line;
  });
  return folds;
}

}  // namespace polyglot::tools::ui
