/**
 * @file     ploy_e2e_real_exit_code_test.cpp
 * @brief    End-to-end smoke: compile a `.ploy` source through `polyc` to a
 *           native COFF object, link it through `polyld` to a Win32 PE32+
 *           executable, spawn the produced `.exe` and assert that its
 *           process exit code matches the literal returned by the .ploy
 *           `main`.
 *
 * This test pins the entire `.ploy` source -> object -> executable -> live
 * process exit code chain. It is what proves that the compiler+linker
 * pipeline actually emits machine code that runs and produces a
 * user-observable result, as opposed to merely producing a structurally
 * valid PE that always exits with code 0 (the previous milestone).
 *
 * Skipped on non-Windows hosts since the Win32 PE writer + loader is what
 * is being exercised end-to-end.
 *
 * @ingroup  Tests / integration
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32

#include <process.h> // _getpid
#include <windows.h> // GetModuleFileNameA

namespace fs = std::filesystem;

namespace {

// Locate the directory containing the currently-running test executable.
// polyc.exe and polyld.exe live in the same build output directory, so we
// derive their paths by appending to this directory.
fs::path TestBinaryDirectory() {
  char buf[MAX_PATH];
  DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
  REQUIRE(n > 0);
  REQUIRE(n < MAX_PATH);
  return fs::path(std::string(buf, n)).parent_path();
}

// Per-process unique scratch directory to keep parallel CTest runs isolated.
fs::path ScratchDir() {
  static int counter = 0;
  static const fs::path base = [] {
    fs::path p = fs::temp_directory_path() /
                 ("polyglot_ploy_e2e_" + std::to_string(::_getpid()));
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
  }();
  ++counter;
  return base;
}

// Write `content` to `path` (binary-safe, preserves source bytes verbatim).
void WriteFile(const fs::path &path, const std::string &content) {
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.good());
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  REQUIRE(f.good());
}

// Run `cmd` via std::system and return its exit code (already converted
// from the Win32 DWORD by the CRT wrapper).
int RunCommand(const std::string &cmd) {
  // std::system on Windows forwards the string to `cmd /c <string>` which
  // strips ONE outer quote pair before re-parsing.  Wrap with an extra
  // pair so embedded quotes around individual paths survive.
  return std::system(("\"" + cmd + "\"").c_str());
}

// Compile, link and execute one .ploy program, returning the live process
// exit code observed via std::system.  Aborts the test (via REQUIRE) if any
// stage fails to produce its output artefact.
int CompileLinkAndRun(const std::string &ploy_source, const std::string &stem) {
  const fs::path bin_dir = TestBinaryDirectory();
  const fs::path polyc = bin_dir / "polyc.exe";
  const fs::path polyld = bin_dir / "polyld.exe";
  REQUIRE(fs::exists(polyc));
  REQUIRE(fs::exists(polyld));

  const fs::path scratch = ScratchDir();
  static int n = 0;
  const std::string tag = stem + "_" + std::to_string(++n);
  const fs::path src = scratch / (tag + ".ploy");
  const fs::path obj = scratch / (tag + ".obj");
  const fs::path exe = scratch / (tag + ".exe");

  WriteFile(src, ploy_source);

  // polyc: .ploy -> COFF .obj
  const std::string compile_cmd =
      "\"" + polyc.string() + "\" \"" + src.string() + "\" --emit-obj=\"" +
      obj.string() + "\" --obj-format=coff";
  const int compile_rc = RunCommand(compile_cmd);
  INFO("polyc exit code: " << compile_rc);
  REQUIRE(fs::exists(obj));

  // polyld: COFF .obj -> PE32+ .exe
  const std::string link_cmd =
      "\"" + polyld.string() + "\" \"" + obj.string() + "\" -o \"" + exe.string() + "\"";
  const int link_rc = RunCommand(link_cmd);
  INFO("polyld exit code: " << link_rc);
  REQUIRE(fs::exists(exe));

  // Run the produced image and report its exit code.
  return RunCommand("\"" + exe.string() + "\"");
}

} // namespace

TEST_CASE("Ploy main returning literal 42 yields process exit code 42",
          "[ploy_e2e][exit_code][windows]") {
  const std::string src = "FUNC main() -> i32 { RETURN 42; }\n";
  REQUIRE(CompileLinkAndRun(src, "exit42") == 42);
}

TEST_CASE("Ploy main returning literal 0 yields process exit code 0",
          "[ploy_e2e][exit_code][windows]") {
  const std::string src = "FUNC main() -> i32 { RETURN 0; }\n";
  REQUIRE(CompileLinkAndRun(src, "exit0") == 0);
}

TEST_CASE("Ploy main returning literal 7 yields process exit code 7",
          "[ploy_e2e][exit_code][windows]") {
  const std::string src = "FUNC main() -> i32 { RETURN 7; }\n";
  REQUIRE(CompileLinkAndRun(src, "exit7") == 7);
}

#else // !_WIN32

TEST_CASE("Ploy end-to-end exit-code smoke is Windows-only",
          "[ploy_e2e][exit_code][skip]") {
  SUCCEED("PE32+ exit-code smoke is Windows-host specific; skipped.");
}

#endif // _WIN32
