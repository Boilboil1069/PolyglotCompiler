/**
 * @file     refactor_diff_test.cpp
 * @brief    Unit tests for `RefactorReviewSession`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/ai/refactor_diff.h"

using namespace polyglot::tools::ui::ai;

namespace {

RefactorSuggestResponse MakeResponse() {
  RefactorSuggestResponse r;
  r.summary = "extract helper";
  RefactorHunk a;
  a.file_path = "src/a.ploy";
  a.start_line = 10;
  a.end_line = 12;
  a.original = "x=1\ny=2\nz=3\n";
  a.replacement = "x,y,z = 1,2,3\n";
  a.rationale = "tuple";
  r.hunks.push_back(a);
  RefactorHunk b;
  b.file_path = "src/b.ploy";
  b.start_line = 4;
  b.end_line = 4;
  b.original = "old\n";
  b.replacement = "new\n";
  b.rationale = "rename";
  r.hunks.push_back(b);
  return r;
}

}  // namespace

TEST_CASE("Hunk decision names cover every variant",
          "[polyui][ai][refactor]") {
  CHECK(HunkDecisionName(HunkDecision::kPending)  == "pending");
  CHECK(HunkDecisionName(HunkDecision::kAccepted) == "accepted");
  CHECK(HunkDecisionName(HunkDecision::kRejected) == "rejected");
}

TEST_CASE("Per-hunk accept / reject tracking",
          "[polyui][ai][refactor]") {
  RefactorReviewSession s;
  s.Load(MakeResponse());
  REQUIRE(s.size() == 2);
  CHECK(s.summary() == "extract helper");
  CHECK(s.pending_count() == 2);
  CHECK(s.Accept(0));
  CHECK(s.Reject(1));
  CHECK_FALSE(s.Accept(99));
  CHECK(s.accepted_count() == 1);
  CHECK(s.rejected_count() == 1);
  CHECK(s.pending_count() == 0);

  auto accepted = s.AcceptedHunks();
  REQUIRE(accepted.size() == 1);
  CHECK(accepted.front().file_path == "src/a.ploy");
}

TEST_CASE("Bulk accept / reject", "[polyui][ai][refactor]") {
  RefactorReviewSession s;
  s.Load(MakeResponse());
  s.AcceptAll();
  CHECK(s.accepted_count() == 2);
  s.RejectAll();
  CHECK(s.rejected_count() == 2);
  CHECK(s.accepted_count() == 0);
}

TEST_CASE("Unified diff emits accepted hunks only",
          "[polyui][ai][refactor]") {
  RefactorReviewSession s;
  s.Load(MakeResponse());
  s.Accept(0);
  auto diff = s.RenderUnifiedDiff();
  CHECK(diff.find("--- src/a.ploy") != std::string::npos);
  CHECK(diff.find("@@ -10,3 +10,1 @@") != std::string::npos);
  CHECK(diff.find("-x=1") != std::string::npos);
  CHECK(diff.find("+x,y,z = 1,2,3") != std::string::npos);
  CHECK(diff.find("src/b.ploy") == std::string::npos);
}
