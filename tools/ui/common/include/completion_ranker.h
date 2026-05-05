/**
 * @file     completion_ranker.h
 * @brief    Strategy-driven scoring for IDE completion candidates
 *
 * Provides a Qt-free matching API that the editor's CompletionPopup uses
 * to filter and order LSP / in-process completion items.  Implements
 * demand 2026-04-28-21 §1.4 — three strategies, switchable from the
 * settings dialog:
 *
 *   • kPrefix       — strict case-insensitive prefix match.
 *   • kSubsequence  — every prefix character must occur in the label
 *                     in order; bonus for adjacency and word starts.
 *   • kFuzzy        — subsequence match plus typo tolerance and a
 *                     softer penalty for inter-match gaps.
 *
 * The scorer returns a non-negative integer (higher is better) or a
 * negative value to indicate "no match"; callers should drop entries
 * with a negative score and stable-sort the remainder by descending
 * score.
 *
 * @ingroup  Tool / polyui / Completion
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <string>
#include <string_view>

namespace polyglot::tools::ui {

/// Match strategy negotiated through the settings page.
enum class CompletionMatchStrategy {
  kPrefix = 0,
  kSubsequence = 1,
  kFuzzy = 2,
};

/// Parse a strategy id from its lowercase string form.  Unknown values
/// fall back to @ref CompletionMatchStrategy::kFuzzy because that mode
/// is the strict superset of the other two.
CompletionMatchStrategy ParseMatchStrategy(std::string_view name);

/// Stringify a strategy id for round-tripping through settings JSON.
std::string MatchStrategyName(CompletionMatchStrategy s);

/// Score @p label against @p needle under @p strategy.  Returns a
/// non-negative score on a successful match (higher = better) or -1
/// when @p needle does not match @p label.  An empty needle always
/// scores 0 (no preference).
int ScoreCompletion(std::string_view label, std::string_view needle,
                    CompletionMatchStrategy strategy);

}  // namespace polyglot::tools::ui
