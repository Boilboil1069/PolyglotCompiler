/**
 * @file     run_debug_picker_test.cpp
 * @brief    Unit tests for `RunDebugPicker`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/runtime/run_debug_picker.h"

using namespace polyglot::tools::ui::runtime;
namespace dap   = polyglot::tools::ui::dap;
namespace tasks = polyglot::tools::ui::tasks;

TEST_CASE("Picker fuses tasks and launches with build defaults first",
          "[polyui][runtime][picker]") {
  RunDebugPicker p;

  tasks::TaskDefinition build;  build.label = "cmake: build";
  build.type = "shell";  build.group = tasks::TaskGroup::kBuild;
  tasks::TaskDefinition fmt;    fmt.label = "format";
  fmt.type = "shell";  fmt.group = tasks::TaskGroup::kCustom;
  p.SetTasks({build, fmt});

  dap::LaunchConfig l;  l.name = "Run main"; l.type = "ploy";
  l.request = dap::LaunchRequest::kLaunch;
  p.SetLaunches({l});

  auto items = p.Items();
  REQUIRE(items.size() == 3);
  CHECK(items[0].label == "cmake: build");
  CHECK(items[0].is_default);
  // Remaining items alphabetised.
  CHECK(items[1].label < items[2].label);
}

TEST_CASE("Picker tracks the active selection",
          "[polyui][runtime][picker]") {
  RunDebugPicker p;
  dap::LaunchConfig l;  l.name = "x"; l.type = "ploy";
  p.SetLaunches({l});
  CHECK_FALSE(p.Active().has_value());
  CHECK_FALSE(p.Select("missing"));
  CHECK(p.Select("x"));
  REQUIRE(p.Active().has_value());
  CHECK(p.Active()->kind == PickKind::kLaunch);
  CHECK(p.Active()->language == "ploy");
}
