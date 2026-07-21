/**
 * @file     run_debug_picker.cpp
 * @brief    Implementation of `run_debug_picker.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/runtime/run_debug_picker.h"

#include <algorithm>

namespace polyglot::tools::ui::runtime {
namespace {

const char *GroupName(polyglot::tools::ui::tasks::TaskGroup g) {
  using polyglot::tools::ui::tasks::TaskGroup;
  switch (g) {
    case TaskGroup::kBuild:  return "build";
    case TaskGroup::kTest:   return "test";
    case TaskGroup::kClean:  return "clean";
    case TaskGroup::kCustom: return "custom";
    case TaskGroup::kNone:   break;
  }
  return "task";
}

bool IsBuildDefault(const polyglot::tools::ui::tasks::TaskDefinition &t) {
  return t.group == polyglot::tools::ui::tasks::TaskGroup::kBuild;
}

bool IsTestDefault(const polyglot::tools::ui::tasks::TaskDefinition &t) {
  return t.group == polyglot::tools::ui::tasks::TaskGroup::kTest;
}

}  // namespace

void RunDebugPicker::SetTasks(
    std::vector<polyglot::tools::ui::tasks::TaskDefinition> tasks) {
  tasks_ = std::move(tasks);
}
void RunDebugPicker::SetLaunches(
    std::vector<polyglot::tools::ui::dap::LaunchConfig> launches) {
  launches_ = std::move(launches);
}

std::vector<PickItem> RunDebugPicker::Items() const {
  std::vector<PickItem> items;
  items.reserve(tasks_.size() + launches_.size());

  for (const auto &t : tasks_) {
    PickItem it;
    it.kind   = PickKind::kTask;
    it.label  = t.label;
    it.detail = t.type + " · " + GroupName(t.group);
    it.is_default = IsBuildDefault(t) || IsTestDefault(t);
    items.push_back(std::move(it));
  }
  for (const auto &l : launches_) {
    PickItem it;
    it.kind   = PickKind::kLaunch;
    it.label  = l.name;
    it.detail = l.type + " · " +
                (l.request == polyglot::tools::ui::dap::LaunchRequest::kAttach
                     ? "attach" : "launch");
    it.language = l.type;
    items.push_back(std::move(it));
  }

  std::sort(items.begin(), items.end(),
            [](const PickItem &a, const PickItem &b) {
              if (a.is_default != b.is_default) return a.is_default;
              return a.label < b.label;
            });
  return items;
}

bool RunDebugPicker::Select(const std::string &label) {
  for (const auto &t : tasks_)
    if (t.label == label) { active_label_ = label; return true; }
  for (const auto &l : launches_)
    if (l.name == label) { active_label_ = label; return true; }
  return false;
}

std::optional<PickItem> RunDebugPicker::Active() const {
  if (!active_label_) return std::nullopt;
  for (const auto &it : Items())
    if (it.label == *active_label_) return it;
  return std::nullopt;
}

}  // namespace polyglot::tools::ui::runtime
