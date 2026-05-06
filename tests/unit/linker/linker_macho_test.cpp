/**
 * @file     linker_macho_test.cpp
 * @brief    Unit coverage for the Mach-O writer in
 *           `tools/polyld/src/linker_macho.cpp`: relocation translation
 *           (x86_64 / arm64), UUID derivation, and on-disk image shape
 *           for MH_EXECUTE / MH_DYLIB / MH_BUNDLE filetypes.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "tools/polyld/include/linker_macho.h"

using namespace polyglot::linker::macho;

namespace {

// Build a small but legal request: one __TEXT segment carrying a
// __text section with three NOP-equivalent bytes, so the writer has
// real content to lay out.
BuildRequest MakeMinimalRequest(std::uint32_t filetype, MachOArch arch) {
  BuildRequest r;
  r.arch     = arch;
  r.filetype = filetype;
  r.base_address = 0x100000000ULL;
  r.entry_offset = 0;
  SegmentDesc text;
  text.segname = "__TEXT";
  text.initprot = kVmProtRead | kVmProtExecute;
  text.maxprot  = kVmProtRead | kVmProtExecute;
  SectionDesc s;
  s.sectname = "__text";
  s.segname  = "__TEXT";
  s.flags    = kSectionTypeRegular | kSectionAttrPureInstr |
               kSectionAttrSomeInstr;
  s.alignment_log2 = 4;
  s.data = { 0x90, 0x90, 0x90 };           // x86_64 NOP × 3 — content irrelevant.
  text.sections.push_back(std::move(s));
  r.segments.push_back(std::move(text));
  return r;
}

// Read a little-endian u32 from a given byte offset of `bytes`.
std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes, std::size_t off) {
  REQUIRE(off + 4 <= bytes.size());
  return static_cast<std::uint32_t>(bytes[off]) |
         (static_cast<std::uint32_t>(bytes[off + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[off + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[off + 3]) << 24);
}

} // namespace

TEST_CASE("BIN-5: TranslateRelocations accepts every documented x86_64 type",
          "[linker_macho][bin5]") {
  std::vector<PendingRelocation> in = {
      {0x10, 1, true,  true, 2, static_cast<std::uint8_t>(X86_64Reloc::kBranch)},
      {0x20, 2, false, true, 3, static_cast<std::uint8_t>(X86_64Reloc::kUnsigned)},
      {0x30, 3, true,  true, 2, static_cast<std::uint8_t>(X86_64Reloc::kGotLoad)},
  };
  std::vector<OnDiskRelocation> out;
  std::vector<std::string> errs;
  REQUIRE(TranslateRelocations(MachOArch::kX86_64, in, out, errs));
  REQUIRE(out.size() == in.size());
  REQUIRE(errs.empty());
  // The first record must round-trip its r_address verbatim.
  REQUIRE(out[0].r_address == 0x10u);
}

TEST_CASE("BIN-5: TranslateRelocations rejects unknown x86_64 types with E3220",
          "[linker_macho][bin5]") {
  std::vector<PendingRelocation> in = {
      {0x40, 4, true, true, 2, /*type=*/0x7Fu},
  };
  std::vector<OnDiskRelocation> out;
  std::vector<std::string> errs;
  REQUIRE_FALSE(TranslateRelocations(MachOArch::kX86_64, in, out, errs));
  REQUIRE_FALSE(errs.empty());
  bool tagged = false;
  for (const auto &m : errs)
    if (m.find("polyld-err-E3220") != std::string::npos) tagged = true;
  REQUIRE(tagged);
}

TEST_CASE("BIN-5: TranslateRelocations handles arm64 PAGE21 / PAGEOFF12",
          "[linker_macho][bin5]") {
  std::vector<PendingRelocation> in = {
      {0x00, 1, true, true, 2, static_cast<std::uint8_t>(Arm64Reloc::kPage21)},
      {0x04, 1, true, true, 2, static_cast<std::uint8_t>(Arm64Reloc::kPageoff12)},
  };
  std::vector<OnDiskRelocation> out;
  std::vector<std::string> errs;
  REQUIRE(TranslateRelocations(MachOArch::kArm64, in, out, errs));
  REQUIRE(errs.empty());
  REQUIRE(out.size() == 2);
}

TEST_CASE("BIN-5: Uuid16FromContent is deterministic and RFC4122 v4-shaped",
          "[linker_macho][bin5]") {
  std::vector<std::uint8_t> a = { 1, 2, 3, 4, 5 };
  std::vector<std::uint8_t> b = a;
  auto u1 = Uuid16FromContent(a);
  auto u2 = Uuid16FromContent(b);
  REQUIRE(u1 == u2);
  // Version nibble (top of byte 6) must be 4 and variant (top of byte 8)
  // must be 10xx — same constraints Apple's `uuidgen` enforces.
  REQUIRE(((u1[6] & 0xF0) >> 4) == 0x4);
  REQUIRE((u1[8] & 0xC0) == 0x80);
}

TEST_CASE("BIN-5: BuildMachOImage executable carries MH_EXECUTE filetype byte",
          "[linker_macho][bin5]") {
  auto req = MakeMinimalRequest(kFileTypeExecute, MachOArch::kX86_64);
  auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  REQUIRE(ReadU32(result.image, 0)  == kMachOMagic64);
  REQUIRE(ReadU32(result.image, 4)  == kCpuTypeX86_64);
  REQUIRE(ReadU32(result.image, 12) == kFileTypeExecute);
  REQUIRE(result.num_load_commands == ReadU32(result.image, 16));
}

TEST_CASE("BIN-5: BuildMachOImage dylib carries MH_DYLIB filetype + install name",
          "[linker_macho][bin5]") {
  auto req = MakeMinimalRequest(kFileTypeDylib, MachOArch::kArm64);
  req.emit_pagezero = false;
  req.install_name  = "@rpath/libpolyglot_test.dylib";
  auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  REQUIRE(ReadU32(result.image, 4)  == kCpuTypeArm64);
  REQUIRE(ReadU32(result.image, 12) == kFileTypeDylib);
  // Install name should appear verbatim somewhere in the load commands.
  const auto &img = result.image;
  std::string needle = req.install_name;
  bool found = std::search(img.begin(), img.end(),
                           needle.begin(), needle.end()) != img.end();
  REQUIRE(found);
}

TEST_CASE("BIN-5: BuildMachOImage bundle carries MH_BUNDLE filetype",
          "[linker_macho][bin5]") {
  auto req = MakeMinimalRequest(kFileTypeBundle, MachOArch::kX86_64);
  req.emit_pagezero = false;
  auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  REQUIRE(ReadU32(result.image, 12) == kFileTypeBundle);
}

TEST_CASE("BIN-5: BuildMachOImage rejects empty segment list",
          "[linker_macho][bin5]") {
  BuildRequest req;
  req.filetype = kFileTypeExecute;
  req.arch     = MachOArch::kX86_64;
  auto result = BuildMachOImage(req);
  REQUIRE(result.image.empty());
}
