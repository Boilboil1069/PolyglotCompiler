/**
 * @file     quick_open_ranker_test.cpp
 * @brief    Unit tests for the Quick Open fuzzy ranker
 *           (demand 2026-04-28-25 §2).
 *
 * @ingroup  Tests / unit / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/quickopen/quick_open_ranker.h"

using polyglot::tools::ui::QuickOpenCandidate;
using polyglot::tools::ui::RankQuickOpen;
using polyglot::tools::ui::ScoreQuickOpenPath;

TEST_CASE("Empty needle scores zero and matches everything",
          "[polyui][quickopen]") {
  REQUIRE(ScoreQuickOpenPath("src/foo.cpp", "") == 0);
}

TEST_CASE("Non-subsequence needles are rejected",
          "[polyui][quickopen]") {
  REQUIRE(ScoreQuickOpenPath("foo.cpp", "zxy") < 0);
}

TEST_CASE("Filename-start matches outscore mid-word matches",
          "[polyui][quickopen]") {
  const int basename = ScoreQuickOpenPath("a/very/long/path/foo.cpp", "foo");
  const int midword = ScoreQuickOpenPath("a/very/loongfoo.cpp", "foo");
  REQUIRE(basename > midword);
}

TEST_CASE("Recent files win on score ties", "[polyui][quickopen]") {
  std::vector<QuickOpenCandidate> cs = {
      {"src/alpha.cpp", 0},
      {"src/alpha.cpp.bak", 100},  // higher recency
  };
  // Both share an identical "alpha" prefix score; recency tie-break
  // sends the recently-opened file to the top.
  auto out = RankQuickOpen(cs, "alpha", 10);
  REQUIRE(out.size() == 2);
  REQUIRE(out.front().path == "src/alpha.cpp.bak");
}

TEST_CASE("Limit truncates the result list", "[polyui][quickopen]") {
  std::vector<QuickOpenCandidate> cs = {
      {"a.cpp", 0}, {"ab.cpp", 0}, {"abc.cpp", 0}, {"abcd.cpp", 0},
  };
  auto out = RankQuickOpen(cs, "a", 2);
  REQUIRE(out.size() == 2);
}
