/**
 * @file     merge_resolver_test.cpp
 * @brief    Unit tests for the three-way merge conflict resolver.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/scm/merge_resolver.h"

using namespace polyglot::tools::ui::scm;

namespace {

constexpr const char *kSample =
    "before\n"
    "<<<<<<< HEAD\n"
    "ours-1\n"
    "ours-2\n"
    "=======\n"
    "theirs-1\n"
    ">>>>>>> feature\n"
    "after\n";

constexpr const char *kSampleWithBase =
    "<<<<<<< HEAD\n"
    "ours\n"
    "|||||||\n"
    "base\n"
    "=======\n"
    "theirs\n"
    ">>>>>>> branch\n";

}  // namespace

TEST_CASE("ParseMergeConflicts extracts both sides", "[polyui][merge]") {
  auto conflicts = ParseMergeConflicts(kSample);
  REQUIRE(conflicts.size() == 1);
  CHECK(conflicts[0].current_label == "HEAD");
  CHECK(conflicts[0].incoming_label == "feature");
  REQUIRE(conflicts[0].current.size() == 2);
  CHECK(conflicts[0].current[0] == "ours-1");
  CHECK(conflicts[0].incoming.size() == 1);
  CHECK(conflicts[0].incoming[0] == "theirs-1");
}

TEST_CASE("ParseMergeConflicts extracts the base section",
          "[polyui][merge]") {
  auto conflicts = ParseMergeConflicts(kSampleWithBase);
  REQUIRE(conflicts.size() == 1);
  REQUIRE(conflicts[0].base.size() == 1);
  CHECK(conflicts[0].base[0] == "base");
}

TEST_CASE("ResolveConflicts accepts current/incoming/both",
          "[polyui][merge]") {
  CHECK(ResolveConflicts(kSample, MergeChoice::kCurrent) ==
        "before\nours-1\nours-2\nafter\n");
  CHECK(ResolveConflicts(kSample, MergeChoice::kIncoming) ==
        "before\ntheirs-1\nafter\n");
  CHECK(ResolveConflicts(kSample, MergeChoice::kBoth) ==
        "before\nours-1\nours-2\ntheirs-1\nafter\n");
}

TEST_CASE("ResolveConflicts honours custom replacements",
          "[polyui][merge]") {
  std::vector<std::vector<std::string>> custom{{"manual"}};
  CHECK(ResolveConflicts(kSample, MergeChoice::kCurrent, custom) ==
        "before\nmanual\nafter\n");
}

TEST_CASE("HasMergeConflicts detects markers", "[polyui][merge]") {
  CHECK(HasMergeConflicts(kSample));
  CHECK_FALSE(HasMergeConflicts("clean text\n"));
}
