/**
 * @file     completion_ranker_test.cpp
 * @brief    Unit tests for the editor-side fuzzy match ranker
 *           (demand 2026-04-28-21 §1.4)
 *
 * @ingroup  Tests / unit / polyui
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/include/completion_ranker.h"

using polyglot::tools::ui::CompletionMatchStrategy;
using polyglot::tools::ui::ScoreCompletion;

TEST_CASE("Prefix strategy rejects non-prefix matches", "[ranker][prefix]") {
  REQUIRE(ScoreCompletion("compute_total", "comp",
                          CompletionMatchStrategy::kPrefix) >= 0);
  REQUIRE(ScoreCompletion("compute_total", "Comp",
                          CompletionMatchStrategy::kPrefix) >= 0);
  REQUIRE(ScoreCompletion("compute_total", "total",
                          CompletionMatchStrategy::kPrefix) < 0);
}

TEST_CASE("Subsequence strategy ranks adjacent matches higher",
          "[ranker][subsequence]") {
  const int adj = ScoreCompletion("computeTotal", "compu",
                                  CompletionMatchStrategy::kSubsequence);
  const int scattered = ScoreCompletion("c_o_m_p_u_t_eTotal", "compu",
                                        CompletionMatchStrategy::kSubsequence);
  REQUIRE(adj > 0);
  REQUIRE(scattered > 0);
  REQUIRE(adj > scattered);
}

TEST_CASE("Fuzzy strategy tolerates one-character typos for needle >= 3",
          "[ranker][fuzzy]") {
  // No typo — should match.
  REQUIRE(ScoreCompletion("compute", "comp",
                          CompletionMatchStrategy::kFuzzy) >= 0);
  // Single inserted typo character ("comZp" is "comp" with an inserted
  // 'Z' the label does not contain — fuzzy mode skips it).
  REQUIRE(ScoreCompletion("compute", "comZp",
                          CompletionMatchStrategy::kFuzzy) >= 0);
  // Two extra characters — must be rejected.
  REQUIRE(ScoreCompletion("compute", "coXmYp",
                          CompletionMatchStrategy::kFuzzy) < 0);
  // Needle shorter than 3 — typo path disabled.
  REQUIRE(ScoreCompletion("compute", "Zc", CompletionMatchStrategy::kFuzzy) <
          0);
}

TEST_CASE("Empty needle scores to zero", "[ranker]") {
  REQUIRE(ScoreCompletion("anything", "", CompletionMatchStrategy::kPrefix) ==
          0);
  REQUIRE(ScoreCompletion("anything", "",
                          CompletionMatchStrategy::kSubsequence) == 0);
  REQUIRE(ScoreCompletion("anything", "", CompletionMatchStrategy::kFuzzy) ==
          0);
}
