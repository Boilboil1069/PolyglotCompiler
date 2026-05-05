/**
 * @file     launch_config_test.cpp
 * @brief    Unit tests for `ParseLaunchJson` / `Substitute` /
 *           `DefaultLaunchConfigurations`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/dap/launch_config.h"

using namespace polyglot::tools::ui::dap;

TEST_CASE("ParseLaunchJson reads the VS Code envelope", "[polyui][launch]") {
  std::string text = R"({
    "version": "0.2.0",
    "configurations": [
      {"name": "run", "type": "ploy", "request": "launch",
       "program": "${workspaceFolder}/main.ploy",
       "args": ["--flag", "1"], "env": {"K": "V"}},
      {"name": "attach", "type": "lldb", "request": "attach",
       "program": "/usr/bin/foo"}
    ]
  })";
  auto cfgs = ParseLaunchJson(text);
  REQUIRE(cfgs.size() == 2);
  CHECK(cfgs[0].name == "run");
  CHECK(cfgs[0].type == "ploy");
  CHECK(cfgs[0].request == LaunchRequest::kLaunch);
  REQUIRE(cfgs[0].args.size() == 2);
  CHECK(cfgs[0].args[0] == "--flag");
  CHECK(cfgs[0].env.at("K") == "V");
  CHECK(cfgs[1].request == LaunchRequest::kAttach);
}

TEST_CASE("ParseLaunchJson tolerates a bare array", "[polyui][launch]") {
  auto cfgs = ParseLaunchJson(R"([
    {"name": "x", "type": "ploy"}
  ])");
  REQUIRE(cfgs.size() == 1);
  CHECK(cfgs[0].name == "x");
}

TEST_CASE("ParseLaunchJson skips invalid entries", "[polyui][launch]") {
  auto cfgs = ParseLaunchJson(R"([
    {"name": "ok", "type": "ploy"},
    {"name": "no-type"},
    {"type": "no-name"}
  ])");
  REQUIRE(cfgs.size() == 1);
  CHECK(cfgs[0].name == "ok");
}

TEST_CASE("Substitute resolves built-in variables", "[polyui][launch]") {
  SubstitutionContext ctx;
  ctx.workspace_folder = "/repo";
  ctx.file = "/repo/main.ploy";
  ctx.file_basename = "main.ploy";
  ctx.env["TOKEN"] = "abc";
  ctx.command_resolver = [](const std::string &n) {
    return n == "pickPort" ? "5555" : "";
  };
  CHECK(Substitute("${workspaceFolder}/x", ctx) == "/repo/x");
  CHECK(Substitute("${file}", ctx) == "/repo/main.ploy");
  CHECK(Substitute("${fileBasename}", ctx) == "main.ploy");
  CHECK(Substitute("${env:TOKEN}", ctx) == "abc");
  CHECK(Substitute("${command:pickPort}", ctx) == "5555");
  // Unknown variable preserved verbatim.
  CHECK(Substitute("${nope}", ctx) == "${nope}");
}

TEST_CASE("DefaultLaunchConfigurations covers the promised types",
          "[polyui][launch]") {
  auto cfgs = DefaultLaunchConfigurations();
  CHECK_FALSE(cfgs.empty());
  bool has_ploy = false, has_python = false, has_cpp = false, has_rust = false,
       has_java = false, has_dotnet = false;
  for (const auto &c : cfgs) {
    if (c.type == "ploy") has_ploy = true;
    if (c.type == "python") has_python = true;
    if (c.type == "lldb" || c.type == "gdb") has_cpp = true;
    if (c.type == "codelldb") has_rust = true;
    if (c.type == "java") has_java = true;
    if (c.type == "coreclr") has_dotnet = true;
  }
  CHECK(has_ploy);
  CHECK(has_python);
  CHECK(has_cpp);
  CHECK(has_rust);
  CHECK(has_java);
  CHECK(has_dotnet);
}
