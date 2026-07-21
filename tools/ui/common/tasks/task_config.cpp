/**
 * @file     task_config.cpp
 * @brief    Implementation of `task_config.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/tasks/task_config.h"

namespace polyglot::tools::ui::tasks {

namespace {

TaskGroup ParseGroup(const Json &v) {
  std::string s;
  if (v.is_string())
    s = v.get<std::string>();
  else if (v.is_object() && v.contains("kind"))
    s = v.value("kind", std::string{});
  if (s == "build") return TaskGroup::kBuild;
  if (s == "test") return TaskGroup::kTest;
  if (s == "clean") return TaskGroup::kClean;
  if (!s.empty()) return TaskGroup::kCustom;
  return TaskGroup::kNone;
}

DependsOrder ParseDependsOrder(const std::string &s) {
  return s == "sequence" ? DependsOrder::kSequence : DependsOrder::kParallel;
}

TaskDefinition ParseOne(const Json &j) {
  TaskDefinition t;
  t.label = j.value("label", std::string{});
  t.type = j.value("type", std::string{"shell"});
  t.command = j.value("command", std::string{});
  if (j.contains("args") && j["args"].is_array()) {
    for (const auto &a : j["args"]) {
      if (a.is_string()) t.args.push_back(a.get<std::string>());
    }
  }
  t.cwd = j.value("cwd", std::string{});
  if (j.contains("options") && j["options"].is_object()) {
    const auto &o = j["options"];
    if (o.contains("cwd") && o["cwd"].is_string())
      t.cwd = o["cwd"].get<std::string>();
    if (o.contains("env") && o["env"].is_object()) {
      for (auto it = o["env"].begin(); it != o["env"].end(); ++it) {
        if (it.value().is_string())
          t.env[it.key()] = it.value().get<std::string>();
      }
    }
  }
  if (j.contains("group")) t.group = ParseGroup(j["group"]);
  if (j.contains("dependsOn")) {
    const auto &d = j["dependsOn"];
    if (d.is_string())
      t.depends_on.push_back(d.get<std::string>());
    else if (d.is_array())
      for (const auto &x : d)
        if (x.is_string()) t.depends_on.push_back(x.get<std::string>());
  }
  t.depends_order =
      ParseDependsOrder(j.value("dependsOrder", std::string{"parallel"}));
  if (j.contains("problemMatcher") && j["problemMatcher"].is_string())
    t.problem_matcher = j["problemMatcher"].get<std::string>();
  t.is_background = j.value("isBackground", false);
  if (j.contains("presentation") && j["presentation"].is_object()) {
    t.output_channel =
        j["presentation"].value("panel", std::string{"shared"});
  }
  t.extra = j;
  return t;
}

}  // namespace

std::vector<TaskDefinition> ParseTasksJson(const std::string &text) {
  std::vector<TaskDefinition> out;
  Json root;
  try {
    root = Json::parse(text);
  } catch (...) {
    return out;
  }
  const Json *arr = nullptr;
  if (root.is_array()) {
    arr = &root;
  } else if (root.is_object() && root.contains("tasks") &&
             root["tasks"].is_array()) {
    arr = &root["tasks"];
  } else {
    return out;
  }
  for (const auto &e : *arr) {
    if (!e.is_object()) continue;
    if (!e.contains("label") || !e["label"].is_string()) continue;
    out.push_back(ParseOne(e));
  }
  return out;
}

std::vector<TaskDefinition> DefaultTasks() {
  std::vector<TaskDefinition> v;
  TaskDefinition build;
  build.label = "cmake: build";
  build.type = "shell";
  build.command = "cmake";
  build.args = {"--build", "build", "-j"};
  build.group = TaskGroup::kBuild;
  build.problem_matcher = "$gcc";
  v.push_back(build);

  TaskDefinition test;
  test.label = "ctest: all";
  test.type = "shell";
  test.command = "ctest";
  test.args = {"--test-dir", "build", "--output-on-failure"};
  test.group = TaskGroup::kTest;
  test.depends_on = {"cmake: build"};
  test.depends_order = DependsOrder::kSequence;
  v.push_back(test);

  TaskDefinition fmt;
  fmt.label = "format: clang-format";
  fmt.type = "shell";
  fmt.command = "clang-format";
  fmt.args = {"-i", "${file}"};
  fmt.group = TaskGroup::kCustom;
  v.push_back(fmt);

  TaskDefinition lint;
  lint.label = "lint: clang-tidy";
  lint.type = "shell";
  lint.command = "clang-tidy";
  lint.args = {"${file}"};
  lint.group = TaskGroup::kCustom;
  v.push_back(lint);

  TaskDefinition watch;
  watch.label = "watch: build";
  watch.type = "shell";
  watch.command = "cmake";
  watch.args = {"--build", "build", "-j"};
  watch.is_background = true;
  watch.problem_matcher = "$gcc-watch";
  v.push_back(watch);
  return v;
}

}  // namespace polyglot::tools::ui::tasks
