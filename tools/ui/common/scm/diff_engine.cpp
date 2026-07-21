/**
 * @file     diff_engine.cpp
 * @brief    Implementation of the line-level diff engine.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/scm/diff_engine.h"

#include <algorithm>
#include <sstream>

namespace polyglot::tools::ui::scm {

namespace {

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

}  // namespace

std::vector<DiffOp> DiffLines(std::string_view old_text,
                              std::string_view new_text) {
  auto a = SplitLines(old_text);
  auto b = SplitLines(new_text);
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  // LCS DP.
  std::vector<std::vector<std::uint32_t>> dp(
      n + 1, std::vector<std::uint32_t>(m + 1, 0));
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < m; ++j) {
      if (a[i] == b[j]) dp[i + 1][j + 1] = dp[i][j] + 1;
      else dp[i + 1][j + 1] = std::max(dp[i + 1][j], dp[i][j + 1]);
    }
  }
  std::vector<DiffOp> rev;
  std::size_t i = n, j = m;
  while (i > 0 && j > 0) {
    if (a[i - 1] == b[j - 1]) {
      rev.push_back({DiffOpKind::kEqual,
                     static_cast<std::uint32_t>(i - 1),
                     static_cast<std::uint32_t>(j - 1), a[i - 1]});
      --i; --j;
    } else if (dp[i][j - 1] >= dp[i - 1][j]) {
      rev.push_back({DiffOpKind::kInsert,
                     static_cast<std::uint32_t>(i),
                     static_cast<std::uint32_t>(j - 1), b[j - 1]});
      --j;
    } else {
      rev.push_back({DiffOpKind::kDelete,
                     static_cast<std::uint32_t>(i - 1),
                     static_cast<std::uint32_t>(j), a[i - 1]});
      --i;
    }
  }
  while (i > 0) {
    rev.push_back({DiffOpKind::kDelete,
                   static_cast<std::uint32_t>(i - 1),
                   static_cast<std::uint32_t>(j), a[i - 1]});
    --i;
  }
  while (j > 0) {
    rev.push_back({DiffOpKind::kInsert,
                   static_cast<std::uint32_t>(i),
                   static_cast<std::uint32_t>(j - 1), b[j - 1]});
    --j;
  }
  std::reverse(rev.begin(), rev.end());
  return rev;
}

std::vector<DiffHunk> BuildHunks(const std::vector<DiffOp> &ops,
                                 std::uint32_t context) {
  std::vector<DiffHunk> hunks;
  std::size_t i = 0;
  while (i < ops.size()) {
    if (ops[i].kind == DiffOpKind::kEqual) { ++i; continue; }
    // Walk back `context` equal lines.
    std::size_t start = i;
    std::uint32_t back = 0;
    while (start > 0 && ops[start - 1].kind == DiffOpKind::kEqual && back < context) {
      --start; ++back;
    }
    // Walk forward including changes and trailing context.
    std::size_t end = i;
    while (end < ops.size()) {
      if (ops[end].kind != DiffOpKind::kEqual) { ++end; continue; }
      // Count consecutive equal block.
      std::size_t k = end;
      while (k < ops.size() && ops[k].kind == DiffOpKind::kEqual) ++k;
      std::size_t equal_run = k - end;
      if (equal_run >= 2 * context && k < ops.size()) {
        end = end + context;
        break;
      }
      // Either the equal run is short (absorb) or we're at EOF
      // (absorb up to `context` lines).
      if (k >= ops.size()) {
        end = std::min<std::size_t>(end + context, ops.size());
        break;
      }
      end = k;
    }
    DiffHunk h;
    h.old_start = ops[start].old_index + 1;  // 1-based per unified diff
    h.new_start = ops[start].new_index + 1;
    h.old_count = 0;
    h.new_count = 0;
    for (std::size_t k = start; k < end; ++k) {
      const DiffOp &op = ops[k];
      switch (op.kind) {
        case DiffOpKind::kEqual:
          h.lines.push_back(" " + op.text);
          ++h.old_count; ++h.new_count;
          break;
        case DiffOpKind::kInsert:
          h.lines.push_back("+" + op.text);
          ++h.new_count;
          break;
        case DiffOpKind::kDelete:
          h.lines.push_back("-" + op.text);
          ++h.old_count;
          break;
      }
    }
    std::ostringstream os;
    os << "@@ -" << h.old_start << "," << h.old_count << " +" << h.new_start
       << "," << h.new_count << " @@";
    h.header = os.str();
    hunks.push_back(std::move(h));
    i = end;
  }
  return hunks;
}

namespace {

std::string Join(const std::vector<std::string> &lines, bool trailing_nl) {
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if (i + 1 < lines.size()) out += '\n';
  }
  if (trailing_nl) out += '\n';
  return out;
}

}  // namespace

std::string ApplyHunk(std::string_view target_text, const DiffHunk &hunk) {
  auto lines = SplitLines(target_text);
  bool trailing = !target_text.empty() && target_text.back() == '\n';
  if (hunk.old_start == 0) return Join(lines, trailing);
  std::vector<std::string> out;
  out.reserve(lines.size() + hunk.new_count);
  std::uint32_t cursor = 0;  // 1-based cursor into `lines`
  // Copy unchanged head.
  while (cursor + 1 < hunk.old_start && cursor < lines.size()) {
    out.push_back(lines[cursor]);
    ++cursor;
  }
  // Walk the hunk.
  for (const auto &raw : hunk.lines) {
    if (raw.empty()) continue;
    char tag = raw[0];
    std::string body = raw.substr(1);
    if (tag == ' ') {
      if (cursor < lines.size()) out.push_back(lines[cursor]);
      ++cursor;
    } else if (tag == '-') {
      ++cursor;  // skip the deleted line in the source
    } else if (tag == '+') {
      out.push_back(body);
    }
  }
  // Tail.
  while (cursor < lines.size()) {
    out.push_back(lines[cursor]);
    ++cursor;
  }
  return Join(out, trailing);
}

std::string RevertHunk(std::string_view target_text, const DiffHunk &hunk) {
  // Reverse: treat `+` as `-` and vice versa.
  DiffHunk inv = hunk;
  std::swap(inv.old_start, inv.new_start);
  std::swap(inv.old_count, inv.new_count);
  for (auto &raw : inv.lines) {
    if (raw.empty()) continue;
    if (raw[0] == '+') raw[0] = '-';
    else if (raw[0] == '-') raw[0] = '+';
  }
  return ApplyHunk(target_text, inv);
}

}  // namespace polyglot::tools::ui::scm
