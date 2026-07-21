/**
 * @file     merge_resolver.cpp
 * @brief    Implementation of the merge-conflict parser/resolver.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/scm/merge_resolver.h"

#include <sstream>

namespace polyglot::tools::ui::scm {

namespace {

std::vector<std::string> SplitLines(std::string_view text, bool *trailing_nl) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      out.emplace_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < text.size()) {
    out.emplace_back(text.substr(start));
    if (trailing_nl) *trailing_nl = false;
  } else if (trailing_nl) {
    *trailing_nl = !text.empty();
  }
  return out;
}

bool StartsWith(const std::string &s, const char *p) {
  std::size_t n = 0;
  while (p[n]) ++n;
  return s.size() >= n && s.compare(0, n, p) == 0;
}

std::string Tail(const std::string &s, std::size_t off) {
  if (off >= s.size()) return {};
  std::size_t i = off;
  while (i < s.size() && s[i] == ' ') ++i;
  return s.substr(i);
}

}  // namespace

std::vector<MergeConflict> ParseMergeConflicts(std::string_view text) {
  std::vector<MergeConflict> out;
  auto lines = SplitLines(text, nullptr);
  std::size_t i = 0;
  while (i < lines.size()) {
    const std::string &ln = lines[i];
    if (StartsWith(ln, "<<<<<<<")) {
      MergeConflict mc;
      mc.start_line = static_cast<std::uint32_t>(i);
      mc.current_label = Tail(ln, 7);
      ++i;
      enum { kCurrent, kBase, kIncoming } sec = kCurrent;
      while (i < lines.size()) {
        const std::string &row = lines[i];
        if (StartsWith(row, "|||||||")) { sec = kBase; ++i; continue; }
        if (StartsWith(row, "=======")) { sec = kIncoming; ++i; continue; }
        if (StartsWith(row, ">>>>>>>")) {
          mc.end_line = static_cast<std::uint32_t>(i);
          mc.incoming_label = Tail(row, 7);
          ++i;
          out.push_back(std::move(mc));
          break;
        }
        switch (sec) {
          case kCurrent:  mc.current.push_back(row);  break;
          case kBase:     mc.base.push_back(row);     break;
          case kIncoming: mc.incoming.push_back(row); break;
        }
        ++i;
      }
    } else {
      ++i;
    }
  }
  return out;
}

bool HasMergeConflicts(std::string_view text) {
  return text.find("<<<<<<<") != std::string_view::npos &&
         text.find(">>>>>>>") != std::string_view::npos;
}

std::string ResolveConflicts(std::string_view text, MergeChoice default_choice,
                             const std::vector<std::vector<std::string>>
                                 &custom_resolutions) {
  bool trailing = false;
  auto lines = SplitLines(text, &trailing);
  std::vector<std::string> out;
  std::size_t i = 0;
  std::size_t conflict_idx = 0;
  while (i < lines.size()) {
    const std::string &ln = lines[i];
    if (!StartsWith(ln, "<<<<<<<")) {
      out.push_back(ln);
      ++i;
      continue;
    }
    // Collect the conflict.
    std::vector<std::string> current, base, incoming;
    enum { kCurrent, kBase, kIncoming } sec = kCurrent;
    ++i;
    while (i < lines.size()) {
      const std::string &row = lines[i];
      if (StartsWith(row, "|||||||")) { sec = kBase; ++i; continue; }
      if (StartsWith(row, "=======")) { sec = kIncoming; ++i; continue; }
      if (StartsWith(row, ">>>>>>>")) { ++i; break; }
      switch (sec) {
        case kCurrent:  current.push_back(row);  break;
        case kBase:     base.push_back(row);     break;
        case kIncoming: incoming.push_back(row); break;
      }
      ++i;
    }
    const std::vector<std::string> *replacement = nullptr;
    if (conflict_idx < custom_resolutions.size() &&
        !custom_resolutions[conflict_idx].empty()) {
      replacement = &custom_resolutions[conflict_idx];
    }
    if (replacement) {
      for (const auto &row : *replacement) out.push_back(row);
    } else {
      switch (default_choice) {
        case MergeChoice::kCurrent:
          for (const auto &row : current) out.push_back(row);
          break;
        case MergeChoice::kIncoming:
          for (const auto &row : incoming) out.push_back(row);
          break;
        case MergeChoice::kBoth:
          for (const auto &row : current) out.push_back(row);
          for (const auto &row : incoming) out.push_back(row);
          break;
        case MergeChoice::kBase:
          for (const auto &row : base) out.push_back(row);
          break;
      }
    }
    ++conflict_idx;
  }
  std::string result;
  for (std::size_t k = 0; k < out.size(); ++k) {
    result += out[k];
    if (k + 1 < out.size()) result += '\n';
  }
  if (trailing) result += '\n';
  return result;
}

}  // namespace polyglot::tools::ui::scm
