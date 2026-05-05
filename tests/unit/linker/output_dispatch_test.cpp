/**
 * @file     output_dispatch_test.cpp
 * @brief    Verify Linker::GenerateOutput() dispatches to the
 *           correct container writer based on the resolved triple +
 *           container override, and that the produced files carry
 *           the right magic bytes.
 *
 * @ingroup  Linker / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/include/binary_container.h"
#include "common/include/target_triple.h"
#include "tools/polyld/include/linker.h"

namespace fs = std::filesystem;
using polyglot::common::BinaryContainer;
using polyglot::common::ParseTargetTriple;
using polyglot::linker::Linker;
using polyglot::linker::LinkerConfig;
using polyglot::linker::OutputFormat;

namespace {
std::vector<unsigned char> Read(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in),
          std::istreambuf_iterator<char>()};
}

std::string TmpPath(const char *stem) {
  auto p = fs::temp_directory_path() /
           ("polyglot_dispatch_" + std::string(stem));
  return p.string();
}

LinkerConfig MakeConfig(const char *triple_spec,
                         BinaryContainer requested,
                         OutputFormat fmt,
                         const std::string &out) {
  LinkerConfig cfg;
  auto t = ParseTargetTriple(triple_spec);
  REQUIRE(t.ok());
  cfg.target_triple = *t.triple;
  cfg.container = requested;
  cfg.output_format = fmt;
  cfg.output_file = out;
  return cfg;
}

bool StartsWith(const std::vector<unsigned char> &b,
                std::initializer_list<unsigned char> sig) {
  if (b.size() < sig.size()) return false;
  size_t i = 0;
  for (auto v : sig) {
    if (b[i++] != v) return false;
  }
  return true;
}
}  // namespace

TEST_CASE("Wasm container produces a valid wasm preamble",
          "[linker][dispatch][wasm]") {
  auto out = TmpPath("module.wasm");
  Linker l(MakeConfig("wasm32-wasi", BinaryContainer::kAuto,
                       OutputFormat::kExecutable, out));
  REQUIRE(l.GenerateOutput());
  auto bytes = Read(out);
  REQUIRE(bytes.size() >= 8);
  CHECK(StartsWith(bytes, {0x00, 0x61, 0x73, 0x6D,
                           0x01, 0x00, 0x00, 0x00}));
  fs::remove(out);
}

TEST_CASE("Linux triple dispatches to ELF executable",
          "[linker][dispatch][elf]") {
  auto out = TmpPath("a.elf");
  Linker l(MakeConfig("x86_64-unknown-linux-gnu", BinaryContainer::kAuto,
                       OutputFormat::kExecutable, out));
  REQUIRE(l.GenerateOutput());
  auto bytes = Read(out);
  REQUIRE(bytes.size() >= 18);
  CHECK(StartsWith(bytes, {0x7F, 'E', 'L', 'F'}));
  // e_type at offset 16 (little-endian).  ET_EXEC = 2.
  CHECK(bytes[16] == 0x02);
  CHECK(bytes[17] == 0x00);
  fs::remove(out);
}

TEST_CASE("ELF shared library writes ET_DYN",
          "[linker][dispatch][elf][shared]") {
  auto out = TmpPath("libfoo.so");
  Linker l(MakeConfig("x86_64-unknown-linux-gnu", BinaryContainer::kAuto,
                       OutputFormat::kSharedLibrary, out));
  REQUIRE(l.GenerateOutput());
  auto bytes = Read(out);
  REQUIRE(bytes.size() >= 18);
  CHECK(StartsWith(bytes, {0x7F, 'E', 'L', 'F'}));
  // ET_DYN = 3.
  CHECK(bytes[16] == 0x03);
  CHECK(bytes[17] == 0x00);
  fs::remove(out);
}

TEST_CASE("Mach-O executable + dylib differ only in filetype",
          "[linker][dispatch][macho]") {
  auto exe = TmpPath("a.macho");
  auto dyl = TmpPath("libfoo.dylib");
  {
    Linker l(MakeConfig("x86_64-apple-darwin", BinaryContainer::kAuto,
                         OutputFormat::kExecutable, exe));
    REQUIRE(l.GenerateOutput());
  }
  {
    Linker l(MakeConfig("x86_64-apple-darwin", BinaryContainer::kAuto,
                         OutputFormat::kSharedLibrary, dyl));
    REQUIRE(l.GenerateOutput());
  }
  auto e = Read(exe);
  auto d = Read(dyl);
  REQUIRE(e.size() >= 16);
  REQUIRE(d.size() >= 16);
  // Mach-O 64-bit magic: 0xFEEDFACF (little-endian).
  CHECK(StartsWith(e, {0xCF, 0xFA, 0xED, 0xFE}));
  CHECK(StartsWith(d, {0xCF, 0xFA, 0xED, 0xFE}));
  // filetype lives at offset 12 (32-bit LE).  Executable=2, Dylib=6.
  CHECK(e[12] == 0x02);
  CHECK(d[12] == 0x06);
  fs::remove(exe);
  fs::remove(dyl);
}

TEST_CASE("Explicit container override wins over triple",
          "[linker][dispatch][override]") {
  auto out = TmpPath("override.bin");
  Linker l(MakeConfig("x86_64-unknown-linux-gnu", BinaryContainer::kWasm,
                       OutputFormat::kExecutable, out));
  REQUIRE(l.GenerateOutput());
  auto bytes = Read(out);
  CHECK(StartsWith(bytes, {0x00, 0x61, 0x73, 0x6D}));
  fs::remove(out);
}

TEST_CASE("SuffixMatchesContainer drives W2101 warnings",
          "[common][container][suffix]") {
  using polyglot::common::SuffixMatchesContainer;
  CHECK(SuffixMatchesContainer("a.exe",  BinaryContainer::kPE,    "executable"));
  CHECK(SuffixMatchesContainer("a.dll",  BinaryContainer::kPE,    "shared"));
  CHECK_FALSE(SuffixMatchesContainer("a.so",  BinaryContainer::kPE, "shared"));
  CHECK(SuffixMatchesContainer("libfoo.so",    BinaryContainer::kELF,   "shared"));
  CHECK(SuffixMatchesContainer("libfoo.dylib", BinaryContainer::kMachO, "shared"));
  CHECK_FALSE(SuffixMatchesContainer("libfoo.dll", BinaryContainer::kELF, "shared"));
  CHECK(SuffixMatchesContainer("m.wasm", BinaryContainer::kWasm, "executable"));
  CHECK_FALSE(SuffixMatchesContainer("m.exe", BinaryContainer::kWasm, "executable"));
  // Unix executables: bare names accepted on ELF/Mach-O.
  CHECK(SuffixMatchesContainer("hello", BinaryContainer::kELF,   "executable"));
  CHECK(SuffixMatchesContainer("hello", BinaryContainer::kMachO, "executable"));
  // …but the .exe extension on a non-PE target is flagged.
  CHECK_FALSE(SuffixMatchesContainer("hello.exe", BinaryContainer::kELF, "executable"));
}

TEST_CASE("Legacy target_os folds into triple via constructor",
          "[linker][dispatch][legacy]") {
  LinkerConfig cfg;
  cfg.target_os = "windows";
  cfg.output_format = OutputFormat::kExecutable;
  cfg.output_file = TmpPath("legacy_win.exe");
  Linker l(cfg);
  REQUIRE(l.GenerateOutput());
  auto bytes = Read(cfg.output_file);
  // PE images start with the DOS "MZ" stub.
  REQUIRE(bytes.size() >= 2);
  CHECK(bytes[0] == 'M');
  CHECK(bytes[1] == 'Z');
  fs::remove(cfg.output_file);
}
