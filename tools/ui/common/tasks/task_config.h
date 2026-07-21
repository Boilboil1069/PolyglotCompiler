/**
 * @file     task_config.h
 * @brief    `.polyc/tasks.json` model + parser (demand 2026-04-28-29 §1).
 *
 * Mirrors the VS Code task schema (label / type / command / args /
 * group / dependsOn / dependsOrder / problemMatcher / isBackground /
 * presentation.panel) so existing user task files load unchanged.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::tasks {

using Json = nlohmann::json;

enum class TaskGroup { kNone, kBuild, kTest, kClean, kCustom };

enum class DependsOrder { kParallel, kSequence };

/// One task entry.  Compound tasks set `command` empty and rely on
/// `depends_on` to fan out into sub-tasks.
struct TaskDefinition {
  std::string label;
  std::string type;          ///< "shell", "process", "compound", custom.
  std::string command;       ///< Empty for compound tasks.
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;
  TaskGroup group{TaskGroup::kNone};
  std::vector<std::string> depends_on;
  DependsOrder depends_order{DependsOrder::kParallel};
  std::string problem_matcher;  ///< e.g. "$gcc", "$tsc", "$rustc".
  bool is_background{false};
  std::string output_channel;   ///< Routes output_panel sub-channel.
  Json extra;                   ///< Original JSON for round-tripping.
};

/// Parse `.polyc/tasks.json` (VS Code schema).  Accepts the
/// `{tasks: [...]}` envelope or a bare array.  Skips entries
/// missing a `label`.
std::vector<TaskDefinition> ParseTasksJson(const std::string &text);

/// Built-in starter tasks: cmake build / ctest / clang-format / lint.
std::vector<TaskDefinition> DefaultTasks();

}  // namespace polyglot::tools::ui::tasks
