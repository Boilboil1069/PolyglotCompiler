/**
 * @file     git_provider_test.cpp
 * @brief    Unit tests for `GitProvider` and its parsers (using a
 *           stubbed git runner).
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "tools/ui/common/scm/git_provider.h"

using namespace polyglot::tools::ui::scm;

TEST_CASE("ParseGitStatus handles porcelain v1 lines", "[polyui][scm][git]") {
  std::string text =
      " M src/main.cc\n"
      "A  src/added.cc\n"
      "?? new.txt\n"
      "R  old.cc -> new.cc\n";
  auto entries = ParseGitStatus(text);
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].path == "src/main.cc");
  CHECK(entries[0].worktree_status == FileStatus::kModified);
  CHECK(entries[1].index_status == FileStatus::kAdded);
  CHECK(entries[2].index_status == FileStatus::kUntracked);
  CHECK(entries[3].path == "new.cc");
  REQUIRE(entries[3].rename_from.has_value());
  CHECK(*entries[3].rename_from == "old.cc");
}

TEST_CASE("ParseUnifiedDiff splits files and hunks", "[polyui][scm][git]") {
  std::string text =
      "diff --git a/x b/x\n"
      "--- a/x\n"
      "+++ b/x\n"
      "@@ -1,2 +1,3 @@\n"
      " keep\n"
      "+added\n"
      " trailer\n"
      "diff --git a/y b/y\n"
      "--- a/y\n"
      "+++ b/y\n"
      "@@ -10,1 +10,1 @@\n"
      "-old\n"
      "+new\n";
  auto files = ParseUnifiedDiff(text);
  REQUIRE(files.size() == 2);
  CHECK(files[0].new_path == "x");
  REQUIRE(files[0].hunks.size() == 1);
  CHECK(files[0].hunks[0].old_start == 1);
  CHECK(files[0].hunks[0].new_count == 3);
  REQUIRE(files[1].hunks.size() == 1);
  CHECK(files[1].hunks[0].old_start == 10);
}

TEST_CASE("ParseGitBlamePorcelain extracts author + summary",
          "[polyui][scm][git]") {
  std::string text =
      "abcdef1234567890 1 1 1\n"
      "author Alice\n"
      "author-time 1700000000\n"
      "summary first commit\n"
      "filename main.cc\n"
      "\tcode line one\n"
      "abcdef1234567890 2 2 1\n"
      "author Alice\n"
      "author-time 1700000000\n"
      "summary first commit\n"
      "filename main.cc\n"
      "\tcode line two\n";
  auto entries = ParseGitBlamePorcelain(text);
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].short_id == "abcdef1");
  CHECK(entries[0].author == "Alice");
  CHECK(entries[0].summary == "first commit");
  CHECK(entries[0].line == 0);
  CHECK(entries[1].line == 1);
}

TEST_CASE("ParseGitLog handles record-separated output",
          "[polyui][scm][git]") {
  std::string text =
      "abc1234deadbeef\x1f"
      "abc1234\x1f"
      "Alice\x1f"
      "alice@example.com\x1f"
      "2026-05-05 10:00:00 +0000\x1f"
      "first\x1f"
      "first\n\nlong message\x1e"
      "fed4321cafebabe\x1f"
      "fed4321\x1f"
      "Bob\x1f"
      "bob@example.com\x1f"
      "2026-05-04 09:00:00 +0000\x1f"
      "second\x1f"
      "second\x1e";
  auto commits = ParseGitLog(text);
  REQUIRE(commits.size() == 2);
  CHECK(commits[0].id == "abc1234deadbeef");
  CHECK(commits[0].short_id == "abc1234");
  CHECK(commits[0].author == "Alice");
  CHECK(commits[0].title == "first");
  CHECK(commits[1].author == "Bob");
}

TEST_CASE("GitProvider routes commands through the runner",
          "[polyui][scm][git]") {
  std::map<std::string, GitInvocationResult> table;
  table["status --porcelain=v1"] = {0, " M file.cc\n"};
  table["add -- file.cc"] = {0, ""};
  table["reset HEAD -- file.cc"] = {0, ""};
  table["commit -m hello"] = {0, ""};
  table["rev-parse HEAD"] = {0, "feedface\n"};
  table["checkout dev"] = {0, ""};
  table["branch --list --all -vv"] = {0, "* main 0123 [origin/main] msg\n"};

  GitRunner runner = [&](const std::vector<std::string> &args,
                         const std::string &) {
    std::string key;
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i) key += ' ';
      key += args[i];
    }
    auto it = table.find(key);
    if (it == table.end()) return GitInvocationResult{1, ""};
    return it->second;
  };

  GitProvider g("/tmp/repo", runner);
  CHECK(g.Status().size() == 1);
  CHECK(g.Stage("file.cc"));
  CHECK(g.Unstage("file.cc"));
  CHECK(g.Commit("hello") == "feedface");
  CHECK(g.Checkout("dev"));
  auto branches = g.Branches();
  REQUIRE(branches.size() == 1);
  CHECK(branches[0].current);
  CHECK(branches[0].name == "main");
}
