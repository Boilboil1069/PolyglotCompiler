/**
 * @file     diff_engine_test.cpp
 * @brief    Unit tests for the line diff engine and hunk operations.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/scm/diff_engine.h"

using namespace polyglot::tools::ui::scm;

TEST_CASE("DiffLines detects insertions and deletions", "[polyui][diff]") {
  auto ops = DiffLines("a\nb\nc\n", "a\nB\nc\nD\n");
  // Expect: =a -b +B =c +D
  REQUIRE(ops.size() == 5);
  CHECK(ops[0].kind == DiffOpKind::kEqual);
  CHECK(ops[0].text == "a");
  CHECK(ops[1].kind == DiffOpKind::kDelete);
  CHECK(ops[1].text == "b");
  CHECK(ops[2].kind == DiffOpKind::kInsert);
  CHECK(ops[2].text == "B");
  CHECK(ops[3].kind == DiffOpKind::kEqual);
  CHECK(ops[3].text == "c");
  CHECK(ops[4].kind == DiffOpKind::kInsert);
  CHECK(ops[4].text == "D");
}

TEST_CASE("BuildHunks groups changes with surrounding context",
          "[polyui][diff]") {
  std::string a = "1\n2\n3\n4\n5\n6\n7\n8\n9\n";
  std::string b = "1\n2\n3\nFOUR\n5\n6\n7\n8\n9\n";
  auto ops = DiffLines(a, b);
  auto hunks = BuildHunks(ops, /*context=*/2);
  REQUIRE(hunks.size() == 1);
  CHECK(hunks[0].old_start == 2);
  CHECK(hunks[0].new_start == 2);
  // 2 context + 1 deletion + 1 insertion + 2 context.
  bool has_minus = false;
  bool has_plus = false;
  for (const auto &line : hunks[0].lines) {
    if (line == "-4") has_minus = true;
    if (line == "+FOUR") has_plus = true;
  }
  CHECK(has_minus);
  CHECK(has_plus);
}

TEST_CASE("ApplyHunk and RevertHunk are inverses", "[polyui][diff]") {
  std::string a = "alpha\nbeta\ngamma\n";
  std::string b = "alpha\nBETA\ngamma\n";
  auto ops = DiffLines(a, b);
  auto hunks = BuildHunks(ops, /*context=*/1);
  REQUIRE(hunks.size() == 1);
  std::string applied = ApplyHunk(a, hunks[0]);
  CHECK(applied == b);
  std::string reverted = RevertHunk(b, hunks[0]);
  CHECK(reverted == a);
}

TEST_CASE("DiffLines handles identical input", "[polyui][diff]") {
  auto ops = DiffLines("x\ny\n", "x\ny\n");
  REQUIRE(ops.size() == 2);
  CHECK(ops[0].kind == DiffOpKind::kEqual);
  CHECK(ops[1].kind == DiffOpKind::kEqual);
  auto hunks = BuildHunks(ops, /*context=*/3);
  CHECK(hunks.empty());
}
