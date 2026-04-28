/**
 * @file     pe_runtime_smoke_test.cpp
 * @brief    Integration test that builds a PE32+ image in-process, writes it
 *           to a temp file, executes it via the host loader, and asserts the
 *           process terminates cleanly with exit code 0.
 *
 * Skipped on non-Windows hosts since the host OS is the one being tested.
 *
 * @ingroup  Tests / integration
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "tools/polyld/include/pe_writer.h"

#ifdef _WIN32

#include <process.h> // _getpid

namespace fs = std::filesystem;
using namespace polyglot::linker::pe;

namespace {

// Resolve a unique temp .exe path for this test process.
fs::path UniqueTempExePath(const char *stem) {
  fs::path base = fs::temp_directory_path();
  // PID + a counter keep parallel CTest runs from colliding.
  static int counter = 0;
  char name[128];
  std::snprintf(name, sizeof(name), "polyglot_%s_%d_%d.exe", stem,
                static_cast<int>(::_getpid()), ++counter);
  return base / name;
}

} // namespace

TEST_CASE("Minimal PE32+ image runs on the host Windows loader and exits 0",
          "[pe_writer][integration][windows]") {
  BuildResult r = BuildMinimalExitZeroImage();
  REQUIRE_FALSE(r.image.empty());

  fs::path exe = UniqueTempExePath("min_exit_zero");
  {
    std::ofstream out(exe, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(r.image.data()),
              static_cast<std::streamsize>(r.image.size()));
    REQUIRE(out.good());
  }

  // std::system returns the child's exit code (already converted from
  // Windows' DWORD by the CRT wrapper).
  std::string cmd = "\"" + exe.string() + "\"";
  int rc = std::system(cmd.c_str());

  std::error_code ec;
  fs::remove(exe, ec); // best-effort cleanup; ignore failure

  REQUIRE(rc == 0);
}

TEST_CASE("PE32+ image wrapping arbitrary user .text bytes still exits 0",
          "[pe_writer][integration][windows]") {
  // 256 NOPs (0x90) as fake "user code".  The writer appends a Win32 entry
  // shim AFTER these bytes and points AddressOfEntryPoint at the shim, so
  // the NOPs never execute.  The image must still load cleanly and exit 0.
  std::vector<std::uint8_t> user_text(256, 0x90);
  BuildResult r = BuildExitZeroPE(user_text);
  REQUIRE_FALSE(r.image.empty());

  fs::path exe = UniqueTempExePath("wrapped_user_text");
  {
    std::ofstream out(exe, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(r.image.data()),
              static_cast<std::streamsize>(r.image.size()));
    REQUIRE(out.good());
  }

  std::string cmd = "\"" + exe.string() + "\"";
  int rc = std::system(cmd.c_str());

  std::error_code ec;
  fs::remove(exe, ec);

  REQUIRE(rc == 0);
}

TEST_CASE("Hello-world PE32+ writes its message to stdout and exits 0",
          "[pe_writer][integration][windows][hello]") {
  // The image emitted by BuildHelloWorldPE imports GetStdHandle, WriteFile
  // and ExitProcess from kernel32.dll, embeds the message in `.rdata`, and
  // executes a 68-byte AMD64 entry shim.  We redirect stdout to a temp
  // file and assert both the exit code AND the captured bytes match.
  const std::string message = "PolyglotCompiler hello PE\r\n";
  BuildResult r = BuildHelloWorldPE(message);
  REQUIRE_FALSE(r.image.empty());

  fs::path exe = UniqueTempExePath("hello_world");
  fs::path captured = exe;
  captured.replace_extension(".out");
  {
    std::ofstream out(exe, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(r.image.data()),
              static_cast<std::streamsize>(r.image.size()));
    REQUIRE(out.good());
  }

  // Wrap the full command in an extra outer pair of quotes: when std::system
  // forwards to `cmd /c <string>`, cmd strips one layer of outer quotes
  // before re-parsing.  Without the outer pair the inner quote pairs break
  // its tokenisation and it returns "filename syntax incorrect".
  std::string cmd = "\"\"" + exe.string() + "\" > \"" + captured.string() + "\"\"";
  int rc = std::system(cmd.c_str());

  // Read the captured stdout before cleanup so we still report on read
  // failure if the assertion fails.
  std::string captured_text;
  {
    std::ifstream in(captured, std::ios::binary);
    if (in.good()) {
      captured_text.assign((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }
  }

  std::error_code ec;
  fs::remove(exe, ec);
  fs::remove(captured, ec);

  REQUIRE(rc == 0);
  REQUIRE(captured_text == message);
}

#else // !_WIN32

TEST_CASE("PE runtime smoke test (Windows-only)", "[pe_writer][integration][windows][.]") {
  SUCCEED("PE runtime smoke test is intentionally skipped on non-Windows hosts");
}

#endif // _WIN32
