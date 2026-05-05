/**
 * @file     run_debug_picker.h
 * @brief    Status-bar quick-pick model fusing tasks + launches.
 *
 * Combines the task list (`tasks.json` → `TaskDefinition`) with the
 * launch list (`launch.json` → `LaunchConfig`) into a single,
 * sorted, status-bar-friendly menu.  Group-default tasks float to
 * the top so the IDE can wire one-keystroke "Run / Debug current
 * config" actions.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tools/ui/common/dap/launch_config.h"
#include "tools/ui/common/tasks/task_config.h"

namespace polyglot::tools::ui::runtime {

enum class PickKind {
  kTask,
  kLaunch,
};

struct PickItem {
  PickKind kind;
  std::string label;
  std::string detail;     ///< e.g. "shell · build" or "ploy · launch".
  std::string language;   ///< Launch type (empty for tasks).
  bool is_default{false};
};

class RunDebugPicker {
 public:
  void SetTasks(std::vector<polyglot::tools::ui::tasks::TaskDefinition> tasks);
  void SetLaunches(
      std::vector<polyglot::tools::ui::dap::LaunchConfig> launches);

  /// Items in display order: build-default tasks first, then test
  /// defaults, then everything else alphabetically.
  std::vector<PickItem> Items() const;

  /// Set the active selection by label.  Returns false if missing.
  bool Select(const std::string &label);
  std::optional<PickItem> Active() const;

  const std::vector<polyglot::tools::ui::tasks::TaskDefinition> &tasks()
      const noexcept {
    return tasks_;
  }
  const std::vector<polyglot::tools::ui::dap::LaunchConfig> &launches()
      const noexcept {
    return launches_;
  }

 private:
  std::vector<polyglot::tools::ui::tasks::TaskDefinition> tasks_;
  std::vector<polyglot::tools::ui::dap::LaunchConfig> launches_;
  std::optional<std::string> active_label_;
};

}  // namespace polyglot::tools::ui::runtime
