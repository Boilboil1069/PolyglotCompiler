// =====================================================================
// object_format_detect_test.cpp
//
// Unit tests for polyglot::linker::DetectObjectFormat. These tests pin
// the magic-byte recognition contract used by the linker's object loader
// dispatch, in particular the requirement that a raw COFF object file
// is recognised by its IMAGE_FILE_HEADER.Machine field rather than the
// MZ DOS stub (which only appears in a fully linked PE image).
// =====================================================================

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "tools/polyld/include/linker.h"

using polyglot::linker::DetectObjectFormat;
using polyglot::linker::ObjectFormat;

namespace {

// Fabricate a minimally byte-correct COFF IMAGE_FILE_HEADER (20 bytes).
// Only the Machine field matters for DetectObjectFormat dispatch.
std::vector<std::uint8_t> MakeCoffHeader(std::uint16_t machine) {
  std::vector<std::uint8_t> hdr(20, 0);
  hdr[0] = static_cast<std::uint8_t>(machine & 0xFF);
  hdr[1] = static_cast<std::uint8_t>((machine >> 8) & 0xFF);
  // NumberOfSections = 1 (offset 2..3)
  hdr[2] = 0x01;
  // PointerToSymbolTable, NumberOfSymbols, OptionalHeaderSize, Characteristics
  // are left zero — DetectObjectFormat only inspects the first two bytes.
  return hdr;
}

} // namespace

TEST_CASE("DetectObjectFormat recognises ELF magic", "[linker][format_detect]") {
  std::vector<std::uint8_t> elf{0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
  REQUIRE(DetectObjectFormat(elf) == ObjectFormat::kELF);
}

TEST_CASE("DetectObjectFormat recognises POBJ magic", "[linker][format_detect]") {
  std::vector<std::uint8_t> pobj{'P', 'O', 'B', 'J', 1, 0, 0, 0};
  REQUIRE(DetectObjectFormat(pobj) == ObjectFormat::kPOBJ);
}

TEST_CASE("DetectObjectFormat recognises PE images via the MZ DOS stub",
          "[linker][format_detect]") {
  // PE images begin with 'MZ' followed by the DOS stub. The dispatcher
  // routes them through the COFF loader, which then walks to the PE
  // signature at the e_lfanew offset.
  std::vector<std::uint8_t> pe(64, 0);
  pe[0] = 'M';
  pe[1] = 'Z';
  REQUIRE(DetectObjectFormat(pe) == ObjectFormat::kCOFF);
}

TEST_CASE("DetectObjectFormat recognises raw COFF AMD64 objects",
          "[linker][format_detect]") {
  auto coff = MakeCoffHeader(0x8664); // IMAGE_FILE_MACHINE_AMD64
  REQUIRE(DetectObjectFormat(coff) == ObjectFormat::kCOFF);
}

TEST_CASE("DetectObjectFormat recognises raw COFF ARM64 objects",
          "[linker][format_detect]") {
  auto coff = MakeCoffHeader(0xAA64); // IMAGE_FILE_MACHINE_ARM64
  REQUIRE(DetectObjectFormat(coff) == ObjectFormat::kCOFF);
}

TEST_CASE("DetectObjectFormat recognises raw COFF i386 objects",
          "[linker][format_detect]") {
  auto coff = MakeCoffHeader(0x014C); // IMAGE_FILE_MACHINE_I386
  REQUIRE(DetectObjectFormat(coff) == ObjectFormat::kCOFF);
}

TEST_CASE("DetectObjectFormat does not misidentify random garbage as COFF",
          "[linker][format_detect]") {
  std::vector<std::uint8_t> garbage(32, 0);
  garbage[0] = 0xDE;
  garbage[1] = 0xAD;
  REQUIRE(DetectObjectFormat(garbage) == ObjectFormat::kUnknown);
}

TEST_CASE("DetectObjectFormat short buffer is unknown", "[linker][format_detect]") {
  std::vector<std::uint8_t> tiny{0x8b, 0x86};
  REQUIRE(DetectObjectFormat(tiny) == ObjectFormat::kUnknown);
}

TEST_CASE("DetectObjectFormat regression: a polyc-emitted ELF .obj is ELF, not COFF",
          "[linker][format_detect][regression]") {
  // Pre-PE-2 regression (2026-04-29-1): polyc on Windows wrote ELF bytes
  // into a .obj file. The detector must classify by magic, not extension,
  // so this stays ELF and the COFF loader is never invoked on it.
  std::vector<std::uint8_t> ploy_obj{0x7f, 'E', 'L', 'F', 2, 1, 1, 0,
                                     0,    0,   0,   0,   0, 0, 0, 0};
  REQUIRE(DetectObjectFormat(ploy_obj) == ObjectFormat::kELF);
}
