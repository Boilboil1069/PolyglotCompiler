/**
 * @file     task_runner.h
 * @brief    Dependency-graph scheduler for `TaskDefinition` sets.
 *
 * Builds a topologically-sorted, batched execution plan from a set
 * of task definitions, honouring per-task `dependsOrder`
 * (`parallel` ⇒ a single batch, `sequence` ⇒ one batch per task).
 * The runner is a pure value-model: it never spawns processes
 * itself; the IDE shell drives execution and reports state changes
 * back via the `Mark*` methods.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/tasks/task_config.h"

namespace polyglot::tools::ui::tasks {

enum class TaskStatus {
  kPending,
  kRunning,
  kSucceeded,
  kFailed,
  kCancelled,
};

/// One scheduling tier.  Tasks within a `Batch` may run in
/// parallel; the runner advances to the next batch once every task
/// in the current batch reports success.
struct Batch {
  std::vector<std::string> labels;
};

class TaskRunner {
 public:
  /// Register or replace a task definition.
  void Register(TaskDefinition task);

  /// Build a batched execution plan for `label`, expanding
  /// dependencies recursively.  Throws `std::runtime_error` on
  /// missing labels or dependency cycles.
  std::vector<Batch> Plan(const std::string &label) const;

  /// Status transitions.  Storage-only; no side effects.
  void MarkStarted(const std::string &label);
  void MarkSucceeded(const std::string &label);
  void MarkFailed(const std::string &label);
  void MarkCancelled(const std::string &label);
  void Reset(const std::string &label);

  TaskStatus StatusOf(const std::string &label) const;
  const TaskDefinition *Find(const std::string &label) const;

  /// Stable subchannel name used by the output panel when streaming
  /// task stdout/stderr.
  static std::string OutputChannel(const std::string &label);

 private:
  void Visit(const std::string &label,
             std::unordered_map<std::string, int> &state,
             std::vector<std::string> &order) const;

  std::unordered_map<std::string, TaskDefinition> tasks_;
  std::unordered_map<std::string, TaskStatus> status_;
};

}  // namespace polyglot::tools::ui::tasks
