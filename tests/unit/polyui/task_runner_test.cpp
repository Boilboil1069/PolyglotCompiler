/**
 * @file     task_runner_test.cpp
 * @brief    Unit tests for `TaskRunner` (DAG planning + state).
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/tasks/task_runner.h"

using namespace polyglot::tools::ui::tasks;

namespace {
TaskDefinition Make(const std::string &label,
                    std::vector<std::string> deps = {},
                    DependsOrder order = DependsOrder::kParallel) {
  TaskDefinition t;
  t.label = label;
  t.depends_on = std::move(deps);
  t.depends_order = order;
  return t;
}
}  // namespace

TEST_CASE("TaskRunner schedules a diamond graph in batches",
          "[polyui][tasks][runner]") {
  TaskRunner r;
  r.Register(Make("a"));
  r.Register(Make("b", {"a"}));
  r.Register(Make("c", {"a"}));
  r.Register(Make("d", {"b", "c"}));

  auto plan = r.Plan("d");
  REQUIRE(plan.size() == 3);
  CHECK(plan[0].labels == std::vector<std::string>{"a"});
  // b and c land in the same batch (parallel).
  REQUIRE(plan[1].labels.size() == 2);
  CHECK(plan[2].labels == std::vector<std::string>{"d"});
}

TEST_CASE("TaskRunner expands sequence-ordered batches",
          "[polyui][tasks][runner]") {
  TaskRunner r;
  r.Register(Make("a"));
  r.Register(Make("b", {"a"}, DependsOrder::kSequence));
  r.Register(Make("c", {"a"}, DependsOrder::kSequence));
  r.Register(Make("d", {"b", "c"}));

  auto plan = r.Plan("d");
  // a · b · c · d  (sequence forces b and c into separate batches).
  REQUIRE(plan.size() == 4);
}

TEST_CASE("TaskRunner detects dependency cycles",
          "[polyui][tasks][runner]") {
  TaskRunner r;
  r.Register(Make("a", {"b"}));
  r.Register(Make("b", {"a"}));
  CHECK_THROWS(r.Plan("a"));
}

TEST_CASE("TaskRunner records status transitions",
          "[polyui][tasks][runner]") {
  TaskRunner r;
  r.Register(Make("x"));
  CHECK(r.StatusOf("x") == TaskStatus::kPending);
  r.MarkStarted("x");   CHECK(r.StatusOf("x") == TaskStatus::kRunning);
  r.MarkSucceeded("x"); CHECK(r.StatusOf("x") == TaskStatus::kSucceeded);
  r.Reset("x");         CHECK(r.StatusOf("x") == TaskStatus::kPending);
  r.MarkFailed("x");    CHECK(r.StatusOf("x") == TaskStatus::kFailed);
  r.MarkCancelled("x"); CHECK(r.StatusOf("x") == TaskStatus::kCancelled);
}

TEST_CASE("TaskRunner exposes a stable output channel name",
          "[polyui][tasks][runner]") {
  CHECK(TaskRunner::OutputChannel("build") == "task:build");
}
