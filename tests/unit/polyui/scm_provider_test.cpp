/**
 * @file     scm_provider_test.cpp
 * @brief    Unit tests for the SCM provider abstraction (in-memory).
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/scm/scm_provider.h"

using namespace polyglot::tools::ui::scm;

TEST_CASE("InMemoryScmProvider returns fixture data and records mutations",
          "[polyui][scm]") {
  InMemoryScmFixture fx;
  fx.status.push_back({"src/main.cc", FileStatus::kModified,
                       FileStatus::kUnchanged, std::nullopt});
  fx.branches.push_back({"main", true, std::string{"origin/main"}});
  fx.log.push_back({"abc1234", "abc1234", "Alice", "alice@example.com",
                    "2026-05-05", "init", "init\n"});

  InMemoryScmProvider p(fx);
  CHECK(p.Name() == "in-memory");
  REQUIRE(p.Status().size() == 1);
  CHECK(p.Status().front().path == "src/main.cc");
  CHECK(p.Branches().front().current);

  CHECK(p.Stage("a.cc"));
  CHECK(p.Unstage("b.cc"));
  CHECK(p.Commit("msg") == "deadbeef");
  CHECK(p.Checkout("dev"));

  REQUIRE(p.staged().size() == 1);
  CHECK(p.staged().front() == "a.cc");
  REQUIRE(p.unstaged().size() == 1);
  CHECK(p.unstaged().front() == "b.cc");
  REQUIRE(p.commits().size() == 1);
  CHECK(p.commits().front() == "msg");
  CHECK(p.checked_out() == "dev");

  auto log = p.Log(10);
  REQUIRE(log.size() == 1);
  CHECK(log.front().author == "Alice");
}
