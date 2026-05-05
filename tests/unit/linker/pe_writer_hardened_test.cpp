/**
 * @file     pe_writer_hardened_test.cpp
 * @brief    Unit tests for the BIN-3 PE32+ writer hardening additions.
 *
 * Covers the multi-section layout, base-relocation encoder/decoder
 * round-trip, configurable Subsystem / Machine / DllCharacteristics fields,
 * the .pdata exception-table directory wiring, and the .bss virtual-only
 * section behaviour.  Legacy single-section assertions live in the original
 * `pe_writer_test.cpp`; the cases here exercise only the new code paths so
 * that the legacy file can keep its byte-level golden assertions intact.
 *
 * @ingroup  Tests / linker
 * @author   Manning Cyrus
 * @date     2026-05-05
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

#include "tools/polyld/include/pe_writer.h"

using namespace polyglot::linker::pe;

namespace {

template <typename T> T LoadLE(const std::vector<std::uint8_t> &b, std::size_t offset) {
  T v{};
  std::memcpy(&v, b.data() + offset, sizeof(T));
  return v;
}

// Locate the start of the section table and return a pointer to the first
// IMAGE_SECTION_HEADER + the section count.  The PE signature is always at
// `e_lfanew`, the COFF File Header is 20 bytes, and the Optional Header is
// 240 bytes for PE32+.
struct SectionTableView {
  std::size_t base;          // file offset of first IMAGE_SECTION_HEADER
  std::uint16_t count;       // NumberOfSections
};

SectionTableView LocateSectionTable(const std::vector<std::uint8_t> &img) {
  const std::uint32_t lfanew = LoadLE<std::uint32_t>(img, 0x3C);
  const std::uint16_t num_sections = LoadLE<std::uint16_t>(img, lfanew + 4 + 2);
  const std::size_t section_table = lfanew + 4 /*"PE\0\0"*/ + 20 /*COFF*/ + 240 /*OptHdr64*/;
  return {section_table, num_sections};
}

struct SectionInfo {
  std::string name;
  std::uint32_t vsize;
  std::uint32_t rva;
  std::uint32_t rsize;
  std::uint32_t roff;
  std::uint32_t characteristics;
};

std::vector<SectionInfo> ReadSections(const std::vector<std::uint8_t> &img) {
  const auto v = LocateSectionTable(img);
  std::vector<SectionInfo> out;
  out.reserve(v.count);
  for (std::uint16_t i = 0; i < v.count; ++i) {
    const std::size_t base = v.base + static_cast<std::size_t>(i) * 40;
    SectionInfo s;
    char name[9] = {0};
    std::memcpy(name, img.data() + base, 8);
    s.name = name;
    s.vsize = LoadLE<std::uint32_t>(img, base + 8);
    s.rva   = LoadLE<std::uint32_t>(img, base + 12);
    s.rsize = LoadLE<std::uint32_t>(img, base + 16);
    s.roff  = LoadLE<std::uint32_t>(img, base + 20);
    s.characteristics = LoadLE<std::uint32_t>(img, base + 36);
    out.push_back(s);
  }
  return out;
}

const SectionInfo *Find(const std::vector<SectionInfo> &sects, const char *name) {
  for (const auto &s : sects)
    if (s.name == name)
      return &s;
  return nullptr;
}

constexpr std::uint16_t kCoffRelocsStripped = 0x0001;
constexpr std::uint16_t kDllCharDynamicBase = 0x0040;
constexpr std::uint16_t kDllCharHighEntropyVa = 0x0020;
constexpr std::uint32_t kScnCntCode = 0x00000020;
constexpr std::uint32_t kScnCntInitData = 0x00000040;
constexpr std::uint32_t kScnCntUninitData = 0x00000080;
constexpr std::uint32_t kScnMemDiscardable = 0x02000000;
constexpr std::uint32_t kScnMemExecute = 0x20000000;
constexpr std::uint32_t kScnMemRead = 0x40000000;
constexpr std::uint32_t kScnMemWrite = 0x80000000u;

} // namespace

TEST_CASE("BuildBaseRelocSection encoder packs entries into 4 KiB pages",
          "[pe_writer][reloc]") {
  std::vector<BaseRelocation> input = {
      {0x1000, 10}, // DIR64
      {0x100C, 10},
      {0x2008, 10},
      {0x2010, 10},
      {0x2024, 10},
  };
  const auto bytes = BuildBaseRelocSection(input);
  REQUIRE_FALSE(bytes.empty());

  // Block 0: page 0x1000, 2 entries → 8 (header) + 2 * 2 (entries) padded
  // up to a multiple of 4 → 12 bytes total.
  const std::uint32_t page0 = LoadLE<std::uint32_t>(bytes, 0);
  const std::uint32_t size0 = LoadLE<std::uint32_t>(bytes, 4);
  REQUIRE(page0 == 0x1000);
  REQUIRE(size0 == 12);

  // Block 1: page 0x2000, 3 entries → 8 + 3*2 padded to 4 → 16 bytes total.
  const std::uint32_t page1 = LoadLE<std::uint32_t>(bytes, size0);
  const std::uint32_t size1 = LoadLE<std::uint32_t>(bytes, size0 + 4);
  REQUIRE(page1 == 0x2000);
  REQUIRE(size1 == 16);
  REQUIRE(bytes.size() == size0 + size1);
}

TEST_CASE("BuildBaseRelocSection round-trips through DecodeBaseRelocSection",
          "[pe_writer][reloc][roundtrip]") {
  std::vector<BaseRelocation> input = {
      {0x1234, 10},
      {0x1000, 10},
      {0x3FFC, 10},
      {0x4000, 10},
      {0xABCDE, 3}, // HIGHLOW (or arm64 BRANCH26 — same nibble)
  };
  const auto bytes = BuildBaseRelocSection(input);
  auto decoded = DecodeBaseRelocSection(bytes);

  // Sort both sides on (rva, type) before comparing — the encoder reorders
  // entries within a page for stability.
  auto by_rva = [](const BaseRelocation &a, const BaseRelocation &b) {
    return a.rva < b.rva || (a.rva == b.rva && a.type < b.type);
  };
  std::sort(input.begin(), input.end(), by_rva);
  std::sort(decoded.begin(), decoded.end(), by_rva);

  REQUIRE(decoded.size() == input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    REQUIRE(decoded[i].rva == input[i].rva);
    REQUIRE(decoded[i].type == input[i].type);
  }
}

TEST_CASE("Hardened PE emits the full multi-section layout in canonical order",
          "[pe_writer][bin3][sections]") {
  BuildRequest req;
  req.text_bytes = std::vector<std::uint8_t>(0x40, 0x90); // 64 NOPs
  req.entry_offset_in_text = 0;
  req.data_bytes = {1, 2, 3, 4, 5, 6, 7, 8};
  req.rdata_bytes = {'h', 'e', 'l', 'l', 'o', 0};
  req.bss_size = 0x200;
  req.pdata_bytes = std::vector<std::uint8_t>(12, 0); // one zero RUNTIME_FUNCTION
  req.xdata_bytes = std::vector<std::uint8_t>(8, 0);
  req.imports = {DllImport{"kernel32.dll", {"ExitProcess"}}};
  req.base_relocations = {{0x1000, 10}};

  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  auto sects = ReadSections(r.image);
  REQUIRE(sects.size() == 8);

  const char *expected_order[] = {
      ".text", ".data", ".rdata", ".bss", ".pdata", ".xdata", ".idata", ".reloc",
  };
  for (std::size_t i = 0; i < sects.size(); ++i)
    REQUIRE(sects[i].name == expected_order[i]);

  // .bss is zero-fill on disk: VirtualSize > 0 but rsize / roff == 0.
  const auto *bss = Find(sects, ".bss");
  REQUIRE(bss != nullptr);
  REQUIRE(bss->vsize == 0x200);
  REQUIRE(bss->rsize == 0);
  REQUIRE(bss->roff == 0);
  REQUIRE((bss->characteristics & kScnCntUninitData) != 0u);
  REQUIRE((bss->characteristics & kScnMemWrite) != 0u);

  // .text is RX, no write.
  const auto *text = Find(sects, ".text");
  REQUIRE(text != nullptr);
  REQUIRE((text->characteristics & kScnCntCode) != 0u);
  REQUIRE((text->characteristics & kScnMemExecute) != 0u);
  REQUIRE((text->characteristics & kScnMemRead) != 0u);
  REQUIRE((text->characteristics & kScnMemWrite) == 0u);

  // .reloc must carry the discardable bit.
  const auto *reloc = Find(sects, ".reloc");
  REQUIRE(reloc != nullptr);
  REQUIRE((reloc->characteristics & kScnMemDiscardable) != 0u);
  REQUIRE((reloc->characteristics & kScnCntInitData) != 0u);

  // RVAs are page-aligned and strictly monotonic.
  for (std::size_t i = 1; i < sects.size(); ++i) {
    REQUIRE(sects[i].rva > sects[i - 1].rva);
    REQUIRE((sects[i].rva % 0x1000u) == 0u);
  }
}

TEST_CASE("Hardened PE wires Exception / BaseReloc data directories",
          "[pe_writer][bin3][datadir]") {
  BuildRequest req;
  req.text_bytes = std::vector<std::uint8_t>(8, 0xCC);
  req.pdata_bytes = std::vector<std::uint8_t>(12, 0);
  req.base_relocations = {{0x1000, 10}, {0x1008, 10}};

  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  const std::uint32_t lfanew = LoadLE<std::uint32_t>(r.image, 0x3C);
  // Data directories begin at: lfanew + 4 + 20 + (240 - 16*8) = lfanew + 24 + 112 = lfanew + 136.
  // Rather than hardcode the offset, find each directory through Optional
  // Header layout: Optional Header starts at lfanew+24; the directory array
  // is the last 128 bytes of the 240-byte structure → starts at +112.
  const std::size_t opt = lfanew + 24;
  const std::size_t dir = opt + 112;

  auto sects = ReadSections(r.image);
  const auto *pdata = Find(sects, ".pdata");
  const auto *reloc = Find(sects, ".reloc");
  REQUIRE(pdata != nullptr);
  REQUIRE(reloc != nullptr);

  // Directory[3] = Exception (.pdata).
  REQUIRE(LoadLE<std::uint32_t>(r.image, dir + 3 * 8 + 0) == pdata->rva);
  REQUIRE(LoadLE<std::uint32_t>(r.image, dir + 3 * 8 + 4) == pdata->vsize);
  // Directory[5] = Base Relocation (.reloc).
  REQUIRE(LoadLE<std::uint32_t>(r.image, dir + 5 * 8 + 0) == reloc->rva);
  REQUIRE(LoadLE<std::uint32_t>(r.image, dir + 5 * 8 + 4) == reloc->vsize);
}

TEST_CASE("Hardened PE drops RELOCS_STRIPPED and sets DYNAMIC_BASE when relocations present",
          "[pe_writer][bin3][characteristics]") {
  BuildRequest req;
  req.text_bytes = std::vector<std::uint8_t>(8, 0xCC);
  req.base_relocations = {{0x1004, 10}};

  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  const std::uint32_t lfanew = LoadLE<std::uint32_t>(r.image, 0x3C);
  const std::uint16_t coff_chars = LoadLE<std::uint16_t>(r.image, lfanew + 4 + 18);
  const std::uint16_t dll_chars  = LoadLE<std::uint16_t>(r.image, lfanew + 24 + 70);

  REQUIRE((coff_chars & kCoffRelocsStripped) == 0u);
  REQUIRE((dll_chars & kDllCharDynamicBase) != 0u);
  REQUIRE((dll_chars & kDllCharHighEntropyVa) != 0u); // x64 default
}

TEST_CASE("Subsystem and Machine fields honour the request",
          "[pe_writer][bin3][subsystem]") {
  BuildRequest req;
  req.text_bytes = std::vector<std::uint8_t>(8, 0xCC);
  req.machine = PEMachine::kArm64;
  req.subsystem = PESubsystem::kWindowsGui;

  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  const std::uint32_t lfanew = LoadLE<std::uint32_t>(r.image, 0x3C);
  // Machine is the first WORD after the "PE\0\0" signature.
  const std::uint16_t machine = LoadLE<std::uint16_t>(r.image, lfanew + 4);
  // Subsystem lives at OptionalHeader + 68.
  const std::uint16_t subsystem = LoadLE<std::uint16_t>(r.image, lfanew + 24 + 68);

  REQUIRE(machine == 0xAA64);
  REQUIRE(subsystem == 2);
}

TEST_CASE("DLL builds toggle IMAGE_FILE_DLL via extra_file_characteristics",
          "[pe_writer][bin3][dll]") {
  BuildRequest req;
  req.text_bytes = std::vector<std::uint8_t>(8, 0xCC);
  req.extra_file_characteristics = 0x2000; // IMAGE_FILE_DLL

  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  const std::uint32_t lfanew = LoadLE<std::uint32_t>(r.image, 0x3C);
  const std::uint16_t coff_chars = LoadLE<std::uint16_t>(r.image, lfanew + 4 + 18);
  REQUIRE((coff_chars & 0x2000u) != 0u);
}
