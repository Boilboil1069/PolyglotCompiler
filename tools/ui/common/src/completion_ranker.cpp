/**
 * @file     completion_ranker.cpp
 * @brief    Scoring implementations for the three completion match
 *           strategies declared in completion_ranker.h.
 *
 * @ingroup  Tool / polyui / Completion
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/completion_ranker.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui {

namespace {

inline char ToLower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool IsWordBoundary(char prev, char curr) {
  if (prev == '\0') return true;
  if (prev == '_' || prev == ':' || prev == '.' || prev == '-' || prev == ' ')
    return true;
  // CamelCase boundary: lowercase -> uppercase.
  if (std::islower(static_cast<unsigned char>(prev)) &&
      std::isupper(static_cast<unsigned char>(curr)))
    return true;
  return false;
}

int ScorePrefix(std::string_view label, std::string_view needle) {
  if (needle.empty()) return 0;
  if (label.size() < needle.size()) return -1;
  for (std::size_t i = 0; i < needle.size(); ++i) {
    if (ToLower(label[i]) != ToLower(needle[i])) return -1;
  }
  // Reward exact prefix length match: longer needle = stronger match.
  // Bonus when the casing matches verbatim.
  int score = 1000 + static_cast<int>(needle.size()) * 10;
  bool exact_case = true;
  for (std::size_t i = 0; i < needle.size(); ++i) {
    if (label[i] != needle[i]) {
      exact_case = false;
      break;
    }
  }
  if (exact_case) score += 50;
  if (label.size() == needle.size()) score += 100;  // exact label hit
  return score;
}

/// Subsequence + adjacency / boundary bonuses.  Returns -1 when @p
/// needle is not a subsequence of @p label.
int ScoreSubsequence(std::string_view label, std::string_view needle) {
  if (needle.empty()) return 0;
  std::size_t li = 0, ni = 0;
  int score = 0;
  bool last_matched = false;
  char prev = '\0';
  for (; li < label.size() && ni < needle.size(); ++li) {
    char l = label[li];
    char n = needle[ni];
    if (ToLower(l) == ToLower(n)) {
      // Base reward.
      score += 10;
      // Adjacency bonus.
      if (last_matched) score += 8;
      // Word-boundary bonus.
      if (IsWordBoundary(prev, l)) score += 12;
      // Position bonus (earlier matches favoured).
      if (li < 4) score += (4 - static_cast<int>(li)) * 2;
      // Case-match bonus.
      if (l == n) score += 1;
      ++ni;
      last_matched = true;
    } else {
      last_matched = false;
    }
    prev = l;
  }
  if (ni != needle.size()) return -1;  // not a subsequence
  // Penalise length difference modestly.
  score -= static_cast<int>(label.size() - needle.size()) / 4;
  return std::max(score, 0);
}

/// Fuzzy: subsequence with a one-character typo tolerated per needle.
int ScoreFuzzy(std::string_view label, std::string_view needle) {
  // Try clean subsequence first.
  int direct = ScoreSubsequence(label, needle);
  if (direct >= 0) return direct + 5;  // tiny bonus for clean match
  if (needle.size() < 3) return -1;    // too short to typo-correct safely

  // Allow exactly one needle character to be skipped.
  int best = -1;
  for (std::size_t skip = 0; skip < needle.size(); ++skip) {
    std::string trimmed;
    trimmed.reserve(needle.size() - 1);
    for (std::size_t i = 0; i < needle.size(); ++i) {
      if (i == skip) continue;
      trimmed.push_back(needle[i]);
    }
    int s = ScoreSubsequence(label, trimmed);
    if (s >= 0) {
      // Heavy penalty for the typo.
      s -= 50;
      if (s > best) best = s;
    }
  }
  return best;
}

}  // namespace

CompletionMatchStrategy ParseMatchStrategy(std::string_view name) {
  if (name == "prefix") return CompletionMatchStrategy::kPrefix;
  if (name == "subsequence") return CompletionMatchStrategy::kSubsequence;
  if (name == "fuzzy") return CompletionMatchStrategy::kFuzzy;
  return CompletionMatchStrategy::kFuzzy;
}

std::string MatchStrategyName(CompletionMatchStrategy s) {
  switch (s) {
    case CompletionMatchStrategy::kPrefix:
      return "prefix";
    case CompletionMatchStrategy::kSubsequence:
      return "subsequence";
    case CompletionMatchStrategy::kFuzzy:
      return "fuzzy";
  }
  return "fuzzy";
}

int ScoreCompletion(std::string_view label, std::string_view needle,
                    CompletionMatchStrategy strategy) {
  switch (strategy) {
    case CompletionMatchStrategy::kPrefix:
      return ScorePrefix(label, needle);
    case CompletionMatchStrategy::kSubsequence:
      return ScoreSubsequence(label, needle);
    case CompletionMatchStrategy::kFuzzy:
      return ScoreFuzzy(label, needle);
  }
  return -1;
}

}  // namespace polyglot::tools::ui
