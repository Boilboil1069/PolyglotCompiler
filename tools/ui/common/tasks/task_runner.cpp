/**
 * @file     task_runner.cpp
 * @brief    Implementation of `task_runner.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/tasks/task_runner.h"

#include <algorithm>
#include <stdexcept>

namespace polyglot::tools::ui::tasks {

void TaskRunner::Register(TaskDefinition task) {
  const std::string label = task.label;
  tasks_[label] = std::move(task);
  status_.emplace(label, TaskStatus::kPending);
}

void TaskRunner::Visit(const std::string &label,
                       std::unordered_map<std::string, int> &state,
                       std::vector<std::string> &order) const {
  auto it = state.find(label);
  if (it != state.end() && it->second == 2) return;
  if (it != state.end() && it->second == 1)
    throw std::runtime_error("task dependency cycle at: " + label);
  state[label] = 1;
  auto found = tasks_.find(label);
  if (found == tasks_.end())
    throw std::runtime_error("unknown task: " + label);
  for (const auto &dep : found->second.depends_on) Visit(dep, state, order);
  state[label] = 2;
  order.push_back(label);
}

std::vector<Batch> TaskRunner::Plan(const std::string &label) const {
  std::unordered_map<std::string, int> state;
  std::vector<std::string> order;
  Visit(label, state, order);

  std::unordered_map<std::string, int> depth;
  for (const auto &name : order) {
    int d = 0;
    const auto *cfg = Find(name);
    for (const auto &dep : cfg->depends_on)
      d = std::max(d, depth[dep] + 1);
    depth[name] = d;
  }

  int max_depth = 0;
  for (const auto &kv : depth) max_depth = std::max(max_depth, kv.second);
  std::vector<Batch> batches(max_depth + 1);
  for (const auto &name : order) batches[depth[name]].labels.push_back(name);

  // If any task in a batch declares `kSequence`, expand the batch
  // into one task per batch to enforce strict ordering.
  std::vector<Batch> expanded;
  for (auto &b : batches) {
    bool needs_split = false;
    for (const auto &n : b.labels) {
      if (Find(n)->depends_order == DependsOrder::kSequence) {
        needs_split = true;
        break;
      }
    }
    if (!needs_split) {
      expanded.push_back(std::move(b));
    } else {
      for (auto &n : b.labels) {
        Batch single;
        single.labels.push_back(std::move(n));
        expanded.push_back(std::move(single));
      }
    }
  }
  return expanded;
}

void TaskRunner::MarkStarted(const std::string &l)   { status_[l] = TaskStatus::kRunning; }
void TaskRunner::MarkSucceeded(const std::string &l) { status_[l] = TaskStatus::kSucceeded; }
void TaskRunner::MarkFailed(const std::string &l)    { status_[l] = TaskStatus::kFailed; }
void TaskRunner::MarkCancelled(const std::string &l) { status_[l] = TaskStatus::kCancelled; }
void TaskRunner::Reset(const std::string &l)         { status_[l] = TaskStatus::kPending; }

TaskStatus TaskRunner::StatusOf(const std::string &l) const {
  auto it = status_.find(l);
  return it == status_.end() ? TaskStatus::kPending : it->second;
}

const TaskDefinition *TaskRunner::Find(const std::string &l) const {
  auto it = tasks_.find(l);
  return it == tasks_.end() ? nullptr : &it->second;
}

std::string TaskRunner::OutputChannel(const std::string &label) {
  return "task:" + label;
}

}  // namespace polyglot::tools::ui::tasks
