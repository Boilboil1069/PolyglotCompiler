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

#include "tools/polyld/include/linker.h"
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

TEST_CASE("BuildPrintlnSequencePE: multi-message PE prints all lines in order",
          "[pe_writer][integration][windows][println][b4]") {
  // Stage B4 — demand 2026-04-28-49.  Build a PE that issues three WriteFile
  // calls (with one duplicated payload) then ExitProcess(0).  Capture stdout
  // and assert both the exit code AND the byte sequence match the
  // concatenation of the message vector — including the duplicate.
  const std::vector<std::string> msgs = {
      "println alpha\r\n",
      "println beta\r\n",
      "println alpha\r\n", // dup of #0 — must dedupe in .rdata, still print
  };
  BuildResult r = BuildPrintlnSequencePE(msgs);
  REQUIRE_FALSE(r.image.empty());

  fs::path exe = UniqueTempExePath("println_seq");
  fs::path captured = exe;
  captured.replace_extension(".out");
  {
    std::ofstream out(exe, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(r.image.data()),
              static_cast<std::streamsize>(r.image.size()));
    REQUIRE(out.good());
  }

  // Same double-outer-quote redirect trick as the hello-world test above —
  // see the long-form comment there for the rationale.
  std::string cmd = "\"\"" + exe.string() + "\" > \"" + captured.string() + "\"\"";
  int rc = std::system(cmd.c_str());

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

  std::string expected;
  for (const auto &m : msgs) expected += m;

  REQUIRE(rc == 0);
  REQUIRE(captured_text == expected);
}

// ---------------------------------------------------------------------------
// Stage B5 — demand 2026-04-28-49.
//
// Smoke-test the full polyld stdout pipeline: hand-craft the kind of
// ObjectFile state that polyc would emit for a `.ploy` program of the form
//
//     PRINTLN "alpha\r\n";
//     PRINTLN "beta\r\n";
//     PRINTLN "alpha\r\n";
//
// run it through the public `CollectPolyrtPrintlnSequence` analysis pass
// (the same one `Linker::GeneratePEExecutable` now invokes), feed the
// recovered messages into `BuildPrintlnSequencePE`, write the image to a
// temp file, execute it, and assert both the exit code and the captured
// stdout match the source-order concatenation — duplicates included.
// ---------------------------------------------------------------------------
TEST_CASE("Stage B5: ObjectFile relocs round-trip through CollectPolyrtPrintlnSequence "
          "and BuildPrintlnSequencePE",
          "[pe_writer][integration][windows][println][b5]") {
  using polyglot::linker::CollectPolyrtPrintlnSequence;
  using polyglot::linker::InputSection;
  using polyglot::linker::ObjectFile;
  using polyglot::linker::ObjectFormat;
  using polyglot::linker::Relocation;
  using polyglot::linker::SectionFlags;
  using polyglot::linker::Symbol;
  using polyglot::linker::SymbolBinding;
  using polyglot::linker::SymbolType;

  const std::vector<std::string> source_calls = {
      "alpha\r\n",
      "beta\r\n",
      "alpha\r\n", // duplicate of #0 — interner shares the data global
  };

  // Build the ObjectFile that polyc would emit:
  //   .text  — three (lea-message, call-polyrt_println) reloc pairs
  //   .rdata — two interned message globals (`println.msg0`, `println.msg1`)
  ObjectFile obj;
  obj.path = "<b5_smoke>";
  obj.format = ObjectFormat::kPOBJ;
  obj.is_64bit = true;
  obj.is_little_endian = true;

  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  InputSection rdata;
  rdata.name = ".rdata";
  rdata.flags = SectionFlags::kAlloc;
  obj.sections.push_back(std::move(text));
  obj.sections.push_back(std::move(rdata));
  constexpr int kRdataIdx = 1;

  auto append_msg = [&](int idx, const std::string &payload) {
    auto &sec = obj.sections[kRdataIdx];
    Symbol sym;
    sym.name = "println.msg" + std::to_string(idx);
    sym.section_index = kRdataIdx;
    sym.section = sec.name;
    sym.offset = sec.data.size();
    sym.size = payload.size();
    sym.is_defined = true;
    sym.binding = SymbolBinding::kGlobal;
    sym.type = SymbolType::kObject;
    sec.data.insert(sec.data.end(), payload.begin(), payload.end());
    obj.symbols.push_back(sym);
  };
  append_msg(0, "alpha\r\n");
  append_msg(1, "beta\r\n");

  auto append_call = [&](std::uint64_t cursor, int msg_idx) {
    auto &sec = obj.sections[0];
    Relocation lea;
    lea.offset = cursor;
    // Reference via the GEP-alias spelling — that's what the IR layer hands
    // to the codegen for `polyrt_println`'s pointer argument.
    lea.symbol = "println.msg" + std::to_string(msg_idx) + ".ptr";
    lea.is_pc_relative = true;
    lea.size = 4;
    sec.relocations.push_back(lea);

    Relocation call;
    call.offset = cursor + 0x07;
    call.symbol = "polyrt_println";
    call.is_pc_relative = true;
    call.size = 4;
    sec.relocations.push_back(call);
  };
  append_call(0x10, 0);
  append_call(0x20, 1);
  append_call(0x30, 0); // duplicate

  // Step 1: recover the call-site sequence — must mirror source order.
  const auto recovered = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(recovered == source_calls);

  // Step 2: feed it to the PE writer used by `Linker::GeneratePEExecutable`.
  using namespace polyglot::linker::pe;
  BuildResult r = BuildPrintlnSequencePE(recovered);
  REQUIRE_FALSE(r.image.empty());

  // Step 3: write, execute, capture stdout, compare.
  fs::path exe = UniqueTempExePath("b5_pipeline");
  fs::path captured = exe;
  captured.replace_extension(".out");
  {
    std::ofstream out(exe, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(r.image.data()),
              static_cast<std::streamsize>(r.image.size()));
    REQUIRE(out.good());
  }

  // Same double-outer-quote redirect trick as the hello-world test above.
  std::string cmd = "\"\"" + exe.string() + "\" > \"" + captured.string() + "\"\"";
  int rc = std::system(cmd.c_str());

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

  std::string expected;
  for (const auto &m : source_calls) expected += m;

  REQUIRE(rc == 0);
  REQUIRE(captured_text == expected);
}

#else // !_WIN32

TEST_CASE("PE runtime smoke test (Windows-only)", "[pe_writer][integration][windows][.]") {
  SUCCEED("PE runtime smoke test is intentionally skipped on non-Windows hosts");
}

#endif // _WIN32
