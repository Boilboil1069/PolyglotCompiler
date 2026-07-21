/**
 * @file     quick_open_ranker.h
 * @brief    Fuzzy ranker for the Ctrl+P Quick Open palette
 *           (demand 2026-04-28-25 §2).
 *
 * The ranker scores file paths against a needle using a VS Code-style
 * algorithm: full-path subsequence match with bonuses for matches at
 * the start of a path segment, after a separator, or at the start of
 * the file name.  Recently-used files receive an additive recency
 * boost so they bubble to the top when scores are otherwise close.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

struct QuickOpenCandidate {
  std::string path;
  /// 0 = never opened.  Higher = more recently opened.  When two
  /// candidates have equal raw scores, the one with the larger
  /// `recency` wins.
  std::uint64_t recency{0};
};

struct QuickOpenScored {
  std::string path;
  int score{0};
  std::uint64_t recency{0};
};

/// Score a single file path.  Returns a value `< 0` when the needle
/// cannot be matched as a subsequence of @p path.
int ScoreQuickOpenPath(std::string_view path, std::string_view needle);

/// Rank @p candidates against @p needle, dropping non-matches and
/// returning best-score-first.  `recency` is folded into the final
/// ranking as a tie-breaker via a small additive bonus capped to
/// avoid drowning out genuine match-quality differences.
std::vector<QuickOpenScored> RankQuickOpen(
    const std::vector<QuickOpenCandidate> &candidates,
    std::string_view needle, std::size_t limit);

}  // namespace polyglot::tools::ui
