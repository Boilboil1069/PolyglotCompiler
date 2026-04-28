/**
 * @file     theme_system_test.cpp
 * @brief    Integration test for the polyui external theme system.
 *           Runs polyui as a child process and asserts on the CLI surface
 *           (--list-themes, --validate-theme).  No GUI is created.
 *
 * @ingroup  Tests / integration
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Locate polyui.exe relative to the test binary location.  The build layout
// puts both test_executables and polyui.exe at <build>/.
std::string FindPolyuiBinary() {
#ifdef _WIN32
  const char *name = "polyui.exe";
#else
  const char *name = "polyui";
#endif
  // Try the working directory first (CTest sets it to the build dir), then
  // walk up a few levels looking for the binary.
  fs::path here = fs::current_path();
  for (int i = 0; i < 4; ++i) {
    if (fs::exists(here / name)) return (here / name).string();
    here = here.parent_path();
  }
  return name;  // fall back, may fail to launch
}

// Run a command, capture stdout, return exit code.
int RunAndCapture(const std::string &cmd, std::string *out) {
#ifdef _WIN32
  FILE *p = _popen(cmd.c_str(), "r");
#else
  FILE *p = popen(cmd.c_str(), "r");
#endif
  if (!p) return -1;
  char buf[1024];
  std::ostringstream oss;
  while (std::fgets(buf, sizeof(buf), p)) oss << buf;
  *out = oss.str();
#ifdef _WIN32
  return _pclose(p);
#else
  return pclose(p);
#endif
}

}  // namespace

TEST_CASE("polyui --list-themes lists the 5 built-in themes", "[theme_system]") {
  const std::string bin = FindPolyuiBinary();
  std::string out;
  const std::string cmd = "\"" + bin + "\" --list-themes";
  const int rc = RunAndCapture(cmd, &out);
  // Exit code is implementation-defined on Windows when a GUI subsystem
  // app prints to a redirected pipe, so accept any value but require all
  // five built-in ids to appear in the output.
  (void)rc;
  REQUIRE(out.find("polyglot.dark")            != std::string::npos);
  REQUIRE(out.find("polyglot.light")           != std::string::npos);
  REQUIRE(out.find("polyglot.high-contrast")   != std::string::npos);
  REQUIRE(out.find("polyglot.solarized-dark")  != std::string::npos);
  REQUIRE(out.find("polyglot.solarized-light") != std::string::npos);
  // Header column names should be present too.
  REQUIRE(out.find("layer") != std::string::npos);
}

TEST_CASE("polyui --validate-theme accepts a built-in qrc theme",
          "[theme_system]") {
  const std::string bin = FindPolyuiBinary();
  std::string out;
  const std::string cmd =
      "\"" + bin + "\" --validate-theme :/polyglot/themes/polyglot.dark.polytheme.json";
  const int rc = RunAndCapture(cmd, &out);
  REQUIRE(rc == 0);
}

TEST_CASE("polyui --validate-theme rejects a malformed theme file",
          "[theme_system]") {
  const std::string bin = FindPolyuiBinary();
  // Write a garbage .polytheme.json into a temp dir and validate it.
  const fs::path tmp = fs::temp_directory_path() / "polyglot_theme_bad.polytheme.json";
  {
    std::ofstream f(tmp);
    f << "{ this is not valid json";
  }
  std::string out;
  const std::string cmd = "\"" + bin + "\" --validate-theme \"" + tmp.string() + "\"";
  const int rc = RunAndCapture(cmd, &out);
  REQUIRE(rc != 0);
  std::error_code ec;
  fs::remove(tmp, ec);
}