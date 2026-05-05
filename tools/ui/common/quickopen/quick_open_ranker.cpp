/**
 * @file     quick_open_ranker.cpp
 * @brief    Implementation of the Quick Open fuzzy ranker
 *           (demand 2026-04-28-25 §2).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/quickopen/quick_open_ranker.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui {

namespace {

bool IsSep(char c) { return c == '/' || c == '\\'; }

char ToLower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}  // namespace

int ScoreQuickOpenPath(std::string_view path, std::string_view needle) {
  if (needle.empty()) return 0;
  // Track segment start (file-name portion) so we can give it the
  // strongest bonus.
  std::size_t basename_start = 0;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (IsSep(path[i])) basename_start = i + 1;
  }

  int score = 0;
  std::size_t ni = 0;
  bool prev_matched = false;
  for (std::size_t i = 0; i < path.size() && ni < needle.size(); ++i) {
    if (ToLower(path[i]) == ToLower(needle[ni])) {
      int gain = 1;
      if (i == basename_start) gain += 16;          // file-name start
      else if (i > 0 && IsSep(path[i - 1])) gain += 8;  // path segment start
      else if (i >= basename_start &&
               i > basename_start &&
               !std::isalnum(static_cast<unsigned char>(path[i - 1])))
        gain += 4;                                   // word boundary
      if (prev_matched) gain += 2;                   // adjacency bonus
      if (path[i] == needle[ni]) gain += 1;          // case-exact
      score += gain;
      ++ni;
      prev_matched = true;
    } else {
      prev_matched = false;
    }
  }
  if (ni < needle.size()) return -1;
  // Slight penalty for very long paths so shorter matches win on tie.
  score -= static_cast<int>(path.size()) / 64;
  return score;
}

std::vector<QuickOpenScored> RankQuickOpen(
    const std::vector<QuickOpenCandidate> &candidates,
    std::string_view needle, std::size_t limit) {
  std::vector<QuickOpenScored> out;
  out.reserve(candidates.size());
  for (const auto &c : candidates) {
    const int s = ScoreQuickOpenPath(c.path, needle);
    if (s < 0) continue;
    out.push_back({c.path, s, c.recency});
  }
  // Stable sort: score desc, recency desc, path asc.
  std::stable_sort(out.begin(), out.end(),
                   [](const QuickOpenScored &a, const QuickOpenScored &b) {
                     if (a.score != b.score) return a.score > b.score;
                     if (a.recency != b.recency) return a.recency > b.recency;
                     return a.path < b.path;
                   });
  if (limit > 0 && out.size() > limit) out.resize(limit);
  return out;
}

}  // namespace polyglot::tools::ui
