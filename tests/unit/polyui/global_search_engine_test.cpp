/**
 * @file     global_search_engine_test.cpp
 * @brief    Unit tests for the workspace search engine
 *           (demand 2026-04-28-25 §4).
 *
 * @ingroup  Tests / unit / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/search/global_search_engine.h"

using polyglot::tools::ui::GlobalSearchHit;
using polyglot::tools::ui::GlobalSearchOptions;
using polyglot::tools::ui::MatchesGlobFilter;
using polyglot::tools::ui::ReplaceInBuffer;
using polyglot::tools::ui::SearchInBuffer;

TEST_CASE("Glob include / exclude filters", "[polyui][search][glob]") {
  REQUIRE(MatchesGlobFilter("src/foo.cpp", {"src/*.cpp"}, {}));
  REQUIRE_FALSE(MatchesGlobFilter("src/foo.h", {"src/*.cpp"}, {}));
  REQUIRE_FALSE(MatchesGlobFilter("build/foo.cpp", {}, {"build/*"}));
  REQUIRE(MatchesGlobFilter("src/x/y.cpp", {"src/**"}, {}));
}

TEST_CASE("Literal search emits hits per occurrence",
          "[polyui][search][literal]") {
  GlobalSearchOptions opts;
  opts.pattern = "foo";
  std::vector<GlobalSearchHit> hits;
  SearchInBuffer("a.txt", "foo bar foo\nbaz foo\n", opts,
                 [&](const GlobalSearchHit &h) {
                   hits.push_back(h);
                   return true;
                 });
  REQUIRE(hits.size() == 3);
  REQUIRE(hits[0].line == 0);
  REQUIRE(hits[0].column == 0);
  REQUIRE(hits[1].line == 0);
  REQUIRE(hits[1].column == 8);
  REQUIRE(hits[2].line == 1);
}

TEST_CASE("Whole-word respects boundaries",
          "[polyui][search][whole_word]") {
  GlobalSearchOptions opts;
  opts.pattern = "foo";
  opts.whole_word = true;
  std::size_t n = 0;
  SearchInBuffer("a.txt", "foobar foo foozle\n", opts,
                 [&](const GlobalSearchHit &) {
                   ++n;
                   return true;
                 });
  REQUIRE(n == 1);  // only the standalone "foo" matches
}

TEST_CASE("Regex replace honours back-references",
          "[polyui][search][replace]") {
  GlobalSearchOptions opts;
  opts.pattern = R"(foo(\d+))";
  opts.regex = true;
  opts.case_sensitive = true;
  std::size_t count = 0;
  const std::string out =
      ReplaceInBuffer("foo12 foo34", opts, "bar$1", &count);
  REQUIRE(count == 2);
  REQUIRE(out == "bar12 bar34");
}

TEST_CASE("Sink can abort search early",
          "[polyui][search][stream]") {
  GlobalSearchOptions opts;
  opts.pattern = "x";
  std::size_t seen = 0;
  SearchInBuffer("a.txt", "x x x x x", opts,
                 [&](const GlobalSearchHit &) {
                   ++seen;
                   return seen < 2;  // stop after second hit
                 });
  REQUIRE(seen == 2);
}
