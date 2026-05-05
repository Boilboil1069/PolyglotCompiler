/**
 * @file     task_config_test.cpp
 * @brief    Unit tests for `ParseTasksJson` / `DefaultTasks`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/tasks/task_config.h"

using namespace polyglot::tools::ui::tasks;

TEST_CASE("ParseTasksJson reads the VS Code envelope", "[polyui][tasks]") {
  std::string text = R"({
    "version": "2.0.0",
    "tasks": [
      {"label": "build", "type": "shell", "command": "cmake",
       "args": ["--build", "build"], "group": {"kind": "build"},
       "problemMatcher": "$gcc"},
      {"label": "test",  "type": "shell", "command": "ctest",
       "dependsOn": ["build"], "dependsOrder": "sequence"},
      {"label": "all", "dependsOn": ["build", "test"]}
    ]
  })";
  auto tasks = ParseTasksJson(text);
  REQUIRE(tasks.size() == 3);
  CHECK(tasks[0].command == "cmake");
  CHECK(tasks[0].group == TaskGroup::kBuild);
  CHECK(tasks[0].problem_matcher == "$gcc");
  CHECK(tasks[1].depends_order == DependsOrder::kSequence);
  CHECK(tasks[1].depends_on.size() == 1);
  CHECK(tasks[2].depends_on.size() == 2);
}

TEST_CASE("ParseTasksJson tolerates a bare array", "[polyui][tasks]") {
  auto tasks = ParseTasksJson(R"([{"label": "x", "command": "echo"}])");
  REQUIRE(tasks.size() == 1);
  CHECK(tasks[0].label == "x");
}

TEST_CASE("ParseTasksJson skips entries with no label", "[polyui][tasks]") {
  auto tasks = ParseTasksJson(R"([
    {"label": "ok"}, {"command": "no-label"}
  ])");
  REQUIRE(tasks.size() == 1);
  CHECK(tasks[0].label == "ok");
}

TEST_CASE("DefaultTasks ships build/test/format/lint/watch templates",
          "[polyui][tasks]") {
  auto tasks = DefaultTasks();
  CHECK_FALSE(tasks.empty());
  bool has_build = false, has_test = false, has_watch = false,
       has_format = false, has_lint = false;
  for (const auto &t : tasks) {
    if (t.label.find("build") != std::string::npos)  has_build = true;
    if (t.label.find("ctest") != std::string::npos)  has_test = true;
    if (t.label.find("watch") != std::string::npos)  has_watch = true;
    if (t.label.find("format") != std::string::npos) has_format = true;
    if (t.label.find("lint") != std::string::npos)   has_lint = true;
  }
  CHECK(has_build);
  CHECK(has_test);
  CHECK(has_watch);
  CHECK(has_format);
  CHECK(has_lint);
}
