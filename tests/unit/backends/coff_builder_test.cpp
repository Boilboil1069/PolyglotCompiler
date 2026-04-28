// =====================================================================
// coff_builder_test.cpp
//
// Unit tests for polyglot::backends::COFFBuilder. The intent is to lock
// down byte-level layout so that downstream consumers (MS link.exe,
// lld-link, polyld) keep treating the output as a first-class COFF
// translation unit. The tests do not invoke an external linker; instead
// they parse the IMAGE_FILE_HEADER, IMAGE_SECTION_HEADERs and the symbol
// table directly and assert structural invariants.
// =====================================================================

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "backends/common/include/object_file.h"

using polyglot::backends::COFFBuilder;
using polyglot::backends::Section;
using polyglot::backends::Symbol;

namespace {

#pragma pack(push, 1)
struct CoffFileHeader {
  std::uint16_t Machine;
  std::uint16_t NumberOfSections;
  std::uint32_t TimeDateStamp;
  std::uint32_t PointerToSymbolTable;
  std::uint32_t NumberOfSymbols;
  std::uint16_t SizeOfOptionalHeader;
  std::uint16_t Characteristics;
};
struct CoffSectionHeader {
  char Name[8];
  std::uint32_t VirtualSize;
  std::uint32_t VirtualAddress;
  std::uint32_t SizeOfRawData;
  std::uint32_t PointerToRawData;
  std::uint32_t PointerToRelocations;
  std::uint32_t PointerToLinenumbers;
  std::uint16_t NumberOfRelocations;
  std::uint16_t NumberOfLinenumbers;
  std::uint32_t Characteristics;
};
struct CoffSymbol {
  union {
    char ShortName[8];
    struct {
      std::uint32_t Zeroes;
      std::uint32_t Offset;
    } LongName;
  } Name;
  std::uint32_t Value;
  std::int16_t SectionNumber;
  std::uint16_t Type;
  std::uint8_t StorageClass;
  std::uint8_t NumberOfAuxSymbols;
};
#pragma pack(pop)

constexpr std::uint16_t kMachineAMD64 = 0x8664;
constexpr std::uint16_t kMachineARM64 = 0xAA64;
constexpr std::uint32_t kScnText = 0x60000020;

Section MakeTextSection(std::vector<std::uint8_t> code) {
  Section s;
  s.name = ".text";
  s.data = std::move(code);
  return s;
}

} // namespace

TEST_CASE("COFFBuilder emits AMD64 IMAGE_FILE_HEADER with .text",
          "[backends][coff_builder]") {
  COFFBuilder b(/*is_arm64=*/false);
  // ret instruction.
  b.AddSection(MakeTextSection({0xC3}));
  Symbol main_sym;
  main_sym.name = "main";
  main_sym.section = ".text";
  main_sym.offset = 0;
  main_sym.size = 1;
  main_sym.is_global = true;
  main_sym.is_function = true;
  b.AddSymbol(main_sym);

  auto bytes = b.Build();
  REQUIRE(bytes.size() >= sizeof(CoffFileHeader));

  CoffFileHeader fh{};
  std::memcpy(&fh, bytes.data(), sizeof(fh));
  REQUIRE(fh.Machine == kMachineAMD64);
  REQUIRE(fh.NumberOfSections == 1);
  REQUIRE(fh.SizeOfOptionalHeader == 0);

  // section symbol + aux + main symbol  =>  3 entries
  REQUIRE(fh.NumberOfSymbols == 3);

  CoffSectionHeader sh{};
  std::memcpy(&sh, bytes.data() + sizeof(CoffFileHeader), sizeof(sh));
  REQUIRE(std::string(sh.Name, 5) == ".text");
  REQUIRE(sh.SizeOfRawData == 1);
  REQUIRE((sh.Characteristics & kScnText) == kScnText);
  REQUIRE(sh.PointerToRawData != 0);
  REQUIRE(bytes[sh.PointerToRawData] == 0xC3); // ret survived round-trip
}

TEST_CASE("COFFBuilder emits ARM64 IMAGE_FILE_HEADER",
          "[backends][coff_builder]") {
  COFFBuilder b(/*is_arm64=*/true);
  // mov x0,#0 ; ret
  b.AddSection(MakeTextSection({0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6}));
  auto bytes = b.Build();
  REQUIRE(bytes.size() >= sizeof(CoffFileHeader));

  CoffFileHeader fh{};
  std::memcpy(&fh, bytes.data(), sizeof(fh));
  REQUIRE(fh.Machine == kMachineARM64);
  REQUIRE(fh.NumberOfSections == 1);
}

TEST_CASE("COFFBuilder records global function symbols as IMAGE_SYM_CLASS_EXTERNAL",
          "[backends][coff_builder]") {
  COFFBuilder b;
  b.AddSection(MakeTextSection({0x90, 0xC3})); // nop ; ret

  Symbol s;
  s.name = "polymain"; // exactly 8 chars: stays in the short-name slot
  s.section = ".text";
  s.offset = 0;
  s.size = 2;
  s.is_global = true;
  s.is_function = true;
  b.AddSymbol(s);

  auto bytes = b.Build();
  CoffFileHeader fh{};
  std::memcpy(&fh, bytes.data(), sizeof(fh));

  // Walk the symbol table and find the user symbol.
  const auto *base = bytes.data() + fh.PointerToSymbolTable;
  bool found = false;
  for (std::uint32_t i = 0; i < fh.NumberOfSymbols; ++i) {
    CoffSymbol cs{};
    std::memcpy(&cs, base + i * sizeof(CoffSymbol), sizeof(cs));
    char tag[9] = {};
    std::memcpy(tag, cs.Name.ShortName, 8);
    tag[8] = '\0';
    if (std::string(tag) == "polymain") {
      REQUIRE(cs.StorageClass == 2u); // IMAGE_SYM_CLASS_EXTERNAL
      REQUIRE(cs.Type == 0x20u);      // IMAGE_SYM_DTYPE_FUNCTION
      REQUIRE(cs.SectionNumber == 1); // first (and only) section
      found = true;
      break;
    }
  }
  REQUIRE(found);
}

TEST_CASE("COFFBuilder long section name spills into string table",
          "[backends][coff_builder]") {
  COFFBuilder b;
  Section s;
  s.name = ".text$mn"; // 8 chars exactly, fits short-name slot
  s.data = {0xC3};
  b.AddSection(s);

  Section big;
  big.name = ".rdata.long_name_for_string_table"; // > 8 chars
  big.data = {0x00, 0x00};
  b.AddSection(big);

  auto bytes = b.Build();
  CoffFileHeader fh{};
  std::memcpy(&fh, bytes.data(), sizeof(fh));
  REQUIRE(fh.NumberOfSections == 2);

  CoffSectionHeader sh1{};
  std::memcpy(&sh1, bytes.data() + sizeof(fh), sizeof(sh1));
  REQUIRE(std::string(sh1.Name, 8) == ".text$mn");

  CoffSectionHeader sh2{};
  std::memcpy(&sh2, bytes.data() + sizeof(fh) + sizeof(sh1), sizeof(sh2));
  // Long name encoded as '/<decimal_offset>'.
  REQUIRE(sh2.Name[0] == '/');
}
