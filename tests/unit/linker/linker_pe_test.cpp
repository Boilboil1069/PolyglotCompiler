/**
 * @file     linker_pe_test.cpp
 * @brief    Unit tests for the BIN-4 PE linker glue: .def parsing,
 *           /EXPORT command-line parsing, MergeExports conflict reporting
 *           (polyld-err-E3201), the export-section byte builder, and the
 *           Relocation→PE base-relocation translator (polyld-err-E3210).
 *
 * @ingroup  Tests / linker
 * @author   Manning Cyrus
 * @date     2026-05-05
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "tools/polyld/include/linker_pe.h"

using namespace polyglot::linker::pe;
using polyglot::linker::RelocationType_x86_64;

namespace {

template <typename T>
T LoadLE(const std::vector<std::uint8_t> &b, std::size_t offset) {
  T v{};
  std::memcpy(&v, b.data() + offset, sizeof(T));
  return v;
}

std::string MakeTempDefFile(const std::string &body) {
  // Use mkstemp to avoid the tmpnam-deprecation warning while keeping the
  // payload self-contained per test invocation.
  char templ[] = "/tmp/polyld_bin4_XXXXXX";
  int fd = ::mkstemp(templ);
  REQUIRE(fd >= 0);
  ::write(fd, body.data(), body.size());
  ::close(fd);
  return std::string(templ);
}

} // namespace

TEST_CASE("BIN-4: ParseDefFile reads LIBRARY + EXPORTS", "[linker_pe][bin4]") {
  const std::string path = MakeTempDefFile(
      "; comment line\n"
      "LIBRARY mathlib\n"
      "EXPORTS\n"
      "  Add\n"
      "  AddPair = Add @ 1\n"
      "  GlobalData DATA\n"
      "  Hidden @ 7 NONAME\n");
  std::string lib;
  std::vector<LinkerExport> exports;
  std::vector<std::string> errs;
  REQUIRE(ParseDefFile(path, lib, exports, errs));
  REQUIRE(errs.empty());
  REQUIRE(lib == "mathlib");
  REQUIRE(exports.size() == 4);

  REQUIRE(exports[0].name == "Add");
  REQUIRE(exports[0].internal.empty());
  REQUIRE(exports[0].ordinal == 0);

  REQUIRE(exports[1].name == "AddPair");
  REQUIRE(exports[1].internal == "Add");
  REQUIRE(exports[1].ordinal == 1);

  REQUIRE(exports[2].name == "GlobalData");
  REQUIRE(exports[2].data == true);

  REQUIRE(exports[3].name == "Hidden");
  REQUIRE(exports[3].ordinal == 7);
  REQUIRE(exports[3].noname == true);

  std::remove(path.c_str());
}

TEST_CASE("BIN-4: ParseCliExportSpec accepts cl-style descriptors",
          "[linker_pe][bin4]") {
  std::vector<std::string> errs;
  LinkerExport e;
  REQUIRE(ParseCliExportSpec("Add=internal_add,@5", e, errs));
  REQUIRE(errs.empty());
  REQUIRE(e.name == "Add");
  REQUIRE(e.internal == "internal_add");
  REQUIRE(e.ordinal == 5);
}

TEST_CASE("BIN-4: MergeExports flags conflicting descriptors as E3201",
          "[linker_pe][bin4]") {
  std::vector<LinkerExport> table;
  std::vector<std::string> errs;

  std::vector<LinkerExport> a{LinkerExport{"Foo", "real_foo", 0, 1, false, false}};
  REQUIRE(MergeExports(table, a, ExportSource::kCommandLine, errs));
  REQUIRE(errs.empty());

  // Same public name, different ordinal → conflict.
  std::vector<LinkerExport> b{LinkerExport{"Foo", "real_foo", 0, 2, false, false}};
  REQUIRE_FALSE(MergeExports(table, b, ExportSource::kDefFile, errs));
  REQUIRE(errs.size() == 1);
  REQUIRE(errs[0].find("polyld-err-E3201") != std::string::npos);
  REQUIRE(errs[0].find("Foo") != std::string::npos);
}

TEST_CASE("BIN-4: BuildExportSection produces a well-formed export directory",
          "[linker_pe][bin4]") {
  std::vector<LinkerExport> exports = {
      LinkerExport{"Add", "", 0x1000, 1, false, false},
      LinkerExport{"Sub", "", 0x1010, 2, false, false},
  };
  const std::uint32_t edata_rva = 0x2000;
  ExportSectionResult R = BuildExportSection(exports, "demo.dll", edata_rva);

  REQUIRE(R.directory_rva == edata_rva);
  REQUIRE(R.directory_size == R.bytes.size());
  REQUIRE(R.bytes.size() >= 40);

  // Directory header: ordinal Base = 1, NumberOfFunctions = 2, NumberOfNames = 2.
  REQUIRE(LoadLE<std::uint32_t>(R.bytes, 0x10) == 1u);
  REQUIRE(LoadLE<std::uint32_t>(R.bytes, 0x14) == 2u);
  REQUIRE(LoadLE<std::uint32_t>(R.bytes, 0x18) == 2u);

  const std::uint32_t eat_rva  = LoadLE<std::uint32_t>(R.bytes, 0x1C);
  const std::uint32_t enpt_rva = LoadLE<std::uint32_t>(R.bytes, 0x20);
  const std::uint32_t eot_rva  = LoadLE<std::uint32_t>(R.bytes, 0x24);
  REQUIRE(eat_rva == edata_rva + 40);
  REQUIRE(enpt_rva == eat_rva + 8);
  REQUIRE(eot_rva == enpt_rva + 8);

  // EAT entries match the exports' RVAs in ordinal order.
  REQUIRE(LoadLE<std::uint32_t>(R.bytes, 40) == 0x1000u);
  REQUIRE(LoadLE<std::uint32_t>(R.bytes, 44) == 0x1010u);

  // Name pointer table points at "Add" then "Sub" (already sorted lex).
  const std::uint32_t name_add_rva = LoadLE<std::uint32_t>(R.bytes, eat_rva - edata_rva + 8);
  const std::uint32_t name_sub_rva = LoadLE<std::uint32_t>(R.bytes, eat_rva - edata_rva + 12);
  REQUIRE(name_add_rva > eot_rva);
  REQUIRE(name_sub_rva > name_add_rva);

  // Verify the actual NUL-terminated strings.
  const auto str_at = [&](std::uint32_t rva) {
    return std::string(reinterpret_cast<const char *>(&R.bytes[rva - edata_rva]));
  };
  REQUIRE(str_at(name_add_rva) == "Add");
  REQUIRE(str_at(name_sub_rva) == "Sub");

  // DLL Name field points at "demo.dll".
  const std::uint32_t name_field = LoadLE<std::uint32_t>(R.bytes, 0x0C);
  REQUIRE(str_at(name_field) == "demo.dll");
}

TEST_CASE("BIN-4: TranslateRelocationsToPEBaseRelocs maps abs and reports E3210",
          "[linker_pe][bin4]") {
  std::vector<PendingRelocation> input = {
      // Absolute 64-bit at RVA 0x1000 → DIR64 (type 10).
      {0x1000, static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_64), false},
      // PC-relative → no PE entry needed.
      {0x1010, static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PC32), true},
      // Absolute 32S → HIGHLOW (type 3).
      {0x1018, static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_32S), false},
      // GOT-bound reloc that has no PE mapping → E3210.
      {0x1020, static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_GOTPCREL), false},
  };
  std::vector<BaseRelocation> out;
  std::vector<std::string> errs;
  const bool ok = TranslateRelocationsToPEBaseRelocs(
      input, PEMachine::kAmd64, out, errs);
  REQUIRE_FALSE(ok);
  REQUIRE(out.size() == 2);
  REQUIRE(out[0].rva == 0x1000u);
  REQUIRE(out[0].type == 10);
  REQUIRE(out[1].rva == 0x1018u);
  REQUIRE(out[1].type == 3);
  REQUIRE(errs.size() == 1);
  REQUIRE(errs[0].find("polyld-err-E3210") != std::string::npos);
}
