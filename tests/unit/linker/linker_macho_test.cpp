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

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "backends/common/include/object_file.h"
#include "common/include/binary_container.h"
#include "tools/polyld/include/linker.h"
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

std::uint64_t ReadU64(const std::vector<std::uint8_t> &bytes, std::size_t off) {
  REQUIRE(off + 8 <= bytes.size());
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(bytes[off + i]) << (8 * i);
  return value;
}

std::string FixedName(const std::vector<std::uint8_t> &bytes,
                      std::size_t off, std::size_t width) {
  REQUIRE(off + width <= bytes.size());
  std::size_t length = 0;
  while (length < width && bytes[off + length] != 0) ++length;
  return std::string(reinterpret_cast<const char *>(bytes.data() + off), length);
}

struct ParsedSection {
  std::string segment;
  std::string section;
  std::uint64_t address{0};
  std::uint64_t size{0};
  std::uint32_t file_offset{0};
  std::uint8_t index{0};
};

struct ParsedSymbol {
  std::uint8_t type{0};
  std::uint8_t section{0};
  std::uint64_t value{0};
};

struct ParsedMachO {
  std::vector<ParsedSection> sections;
  std::unordered_map<std::string, ParsedSymbol> symbols;
};

ParsedMachO ParseMachO(const std::vector<std::uint8_t> &image) {
  REQUIRE(image.size() >= 32);
  REQUIRE(ReadU32(image, 0) == kMachOMagic64);

  ParsedMachO parsed;
  std::optional<std::size_t> symtab_command;
  const auto command_count = ReadU32(image, 16);
  std::size_t command_offset = 32;
  std::uint16_t section_index = 1;
  for (std::uint32_t i = 0; i < command_count; ++i) {
    REQUIRE(command_offset + 8 <= image.size());
    const auto command = ReadU32(image, command_offset);
    const auto command_size = ReadU32(image, command_offset + 4);
    REQUIRE(command_size >= 8);
    REQUIRE(command_offset + command_size <= image.size());
    if (command == kLcSegment64) {
      const auto segment = FixedName(image, command_offset + 8, 16);
      const auto section_count = ReadU32(image, command_offset + 64);
      std::size_t section_offset = command_offset + 72;
      for (std::uint32_t j = 0; j < section_count; ++j) {
        REQUIRE(section_index <= 255);
        ParsedSection section;
        section.section = FixedName(image, section_offset, 16);
        section.segment = segment;
        section.address = ReadU64(image, section_offset + 32);
        section.size = ReadU64(image, section_offset + 40);
        section.file_offset = ReadU32(image, section_offset + 48);
        section.index = static_cast<std::uint8_t>(section_index++);
        parsed.sections.push_back(std::move(section));
        section_offset += 80;
      }
    } else if (command == kLcSymtab) {
      symtab_command = command_offset;
    }
    command_offset += command_size;
  }

  REQUIRE(symtab_command.has_value());
  const auto symoff = ReadU32(image, *symtab_command + 8);
  const auto symbol_count = ReadU32(image, *symtab_command + 12);
  const auto stroff = ReadU32(image, *symtab_command + 16);
  const auto strsize = ReadU32(image, *symtab_command + 20);
  REQUIRE(static_cast<std::uint64_t>(symoff) +
              static_cast<std::uint64_t>(symbol_count) * 16 <= image.size());
  REQUIRE(static_cast<std::uint64_t>(stroff) + strsize <= image.size());

  for (std::uint32_t i = 0; i < symbol_count; ++i) {
    const std::size_t offset = symoff + static_cast<std::size_t>(i) * 16;
    const auto string_index = ReadU32(image, offset);
    REQUIRE(string_index < strsize);
    const std::size_t name_start = stroff + string_index;
    std::size_t name_end = name_start;
    while (name_end < stroff + strsize && image[name_end] != 0) ++name_end;
    REQUIRE(name_end < stroff + strsize);
    const std::string name(
        reinterpret_cast<const char *>(image.data() + name_start),
        name_end - name_start);
    parsed.symbols.emplace(
        name, ParsedSymbol{image[offset + 4], image[offset + 5],
                           ReadU64(image, offset + 8)});
  }
  return parsed;
}

const ParsedSection &FindSection(const ParsedMachO &parsed,
                                 const std::string &segment,
                                 const std::string &section) {
  const auto found = std::find_if(
      parsed.sections.begin(), parsed.sections.end(),
      [&](const ParsedSection &candidate) {
        return candidate.segment == segment && candidate.section == section;
      });
  REQUIRE(found != parsed.sections.end());
  return *found;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
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

TEST_CASE("Mach-O writer resolves nlist values against final global section indices",
          "[linker_macho][symbols][sections]") {
  BuildRequest req;
  req.arch = MachOArch::kX86_64;
  req.filetype = kFileTypeExecute;
  req.base_address = 0x100000000ULL;
  req.emit_pagezero = false;
  req.emit_dyld_info = false;

  SegmentDesc text;
  text.segname = "__TEXT";
  text.initprot = kVmProtRead | kVmProtExecute;
  text.maxprot = kVmProtRead | kVmProtExecute;
  SectionDesc stubs;
  stubs.sectname = "__stubs";
  stubs.segname = "__TEXT";
  stubs.alignment_log2 = 4;
  stubs.data.assign(19, 0xCC);
  SectionDesc code;
  code.sectname = "__text";
  code.segname = "__TEXT";
  code.alignment_log2 = 5;
  code.flags = kSectionTypeRegular | kSectionAttrPureInstr |
               kSectionAttrSomeInstr;
  code.data = {0x90, 0x90, 0x90, 0x90, 0x90, 0xC3, 0xCC};
  text.sections.push_back(std::move(stubs));
  text.sections.push_back(std::move(code));
  req.segments.push_back(std::move(text));

  SegmentDesc data;
  data.segname = "__DATA";
  data.initprot = kVmProtRead | kVmProtWrite;
  data.maxprot = kVmProtRead | kVmProtWrite;
  SectionDesc data_section;
  data_section.sectname = "__data";
  data_section.segname = "__DATA";
  data_section.alignment_log2 = 3;
  data_section.data = {0x10, 0x20, 0x30, 0x40, 0x50};
  data.sections.push_back(std::move(data_section));
  req.segments.push_back(std::move(data));

  SymbolDesc raw_header;
  raw_header.name = "__mh_execute_header";
  raw_header.n_type = kNTypeSect | kNTypeExt;
  raw_header.n_sect = 1;
  raw_header.n_value = req.base_address;
  req.symbols.push_back(raw_header);

  SymbolDesc function;
  function.name = "_function";
  function.n_type = kNTypeSect | kNTypeExt;
  function.n_sect = 2;  // __TEXT,__text, after __TEXT,__stubs
  function.n_value = 5;
  function.n_value_is_section_relative = true;
  req.symbols.push_back(function);

  SymbolDesc global;
  global.name = "_global";
  global.n_type = kNTypeSect | kNTypeExt;
  global.n_sect = 3;  // __DATA,__data in the second segment
  global.n_value = 3;
  global.n_value_is_section_relative = true;
  req.symbols.push_back(global);
  req.extdef_count = 3;

  const auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  const auto parsed = ParseMachO(result.image);
  const auto &text_section = FindSection(parsed, "__TEXT", "__text");
  const auto &writable_section = FindSection(parsed, "__DATA", "__data");

  REQUIRE(parsed.symbols.at("_function").section == text_section.index);
  REQUIRE(parsed.symbols.at("_function").value == text_section.address + 5);
  REQUIRE(result.image[text_section.file_offset + 5] == 0xC3);

  REQUIRE(parsed.symbols.at("_global").section == writable_section.index);
  REQUIRE(parsed.symbols.at("_global").value == writable_section.address + 3);
  REQUIRE(result.image[writable_section.file_offset + 3] == 0x40);

  // Explicit absolute values remain untouched even when their type is N_SECT.
  REQUIRE(parsed.symbols.at("__mh_execute_header").value == req.base_address);
}

TEST_CASE("Mach-O writer applies cross-section REL32 after final segment layout",
          "[linker_macho][relocation][sections]") {
  auto req = MakeMinimalRequest(kFileTypeExecute, MachOArch::kX86_64);
  // lea rax, [rip + disp32]; ret.  The displacement field starts at byte 3
  // and is relative to the following instruction (addend -4).
  req.segments.front().sections.front().data =
      {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00, 0xc3};

  SegmentDesc data;
  data.segname = "__DATA";
  data.initprot = kVmProtRead | kVmProtWrite;
  data.maxprot = kVmProtRead | kVmProtWrite;
  SectionDesc payload;
  payload.sectname = "__data";
  payload.segname = "__DATA";
  payload.alignment_log2 = 3;
  payload.data = {'c', 's', 'v', 0};
  data.sections.push_back(std::move(payload));
  req.segments.push_back(std::move(data));

  FinalRelocationPatch relocation;
  relocation.source_section = 0;
  relocation.source_offset = 3;
  relocation.target_section = 1;
  relocation.target_offset = 0;
  relocation.addend = -4;
  relocation.kind = FinalRelocationKind::kPcRel32;
  req.final_relocations.push_back(relocation);

  const auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  const auto parsed = ParseMachO(result.image);
  const auto &text = FindSection(parsed, "__TEXT", "__text");
  const auto &data_section = FindSection(parsed, "__DATA", "__data");
  const std::int32_t displacement =
      static_cast<std::int32_t>(ReadU32(result.image, text.file_offset + 3));
  const std::uint64_t resolved = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(text.address + 7) + displacement);
  REQUIRE(resolved == data_section.address);
  REQUIRE(result.image[data_section.file_offset] == static_cast<std::uint8_t>('c'));
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

TEST_CASE("polyld Mach-O symbols match emitted text and data VM addresses",
          "[linker][macho][symbols][integration]") {
  namespace fs = std::filesystem;
  using polyglot::backends::COFFBuilder;
  using polyglot::common::BinaryContainer;
  using polyglot::linker::Linker;
  using polyglot::linker::LinkerConfig;
  using polyglot::linker::OutputFormat;
  using polyglot::linker::TargetArch;

  COFFBuilder builder(/*is_arm64=*/false);
  polyglot::backends::Section text;
  text.name = ".text";
  text.data = {0x90, 0x90, 0xC3, 0xCC, 0xCC};
  builder.AddSection(text);
  polyglot::backends::Section rdata;
  rdata.name = ".rdata";
  rdata.data = {0x11, 0x22, 0x33};
  builder.AddSection(rdata);
  polyglot::backends::Section data;
  data.name = ".data";
  data.data = {0xAA, 0xBB, 0xCC, 0xDD};
  builder.AddSection(data);

  polyglot::backends::Symbol entry;
  entry.name = "macho_entry";
  entry.section = ".text";
  entry.offset = 2;
  entry.size = 1;
  entry.is_global = true;
  entry.is_function = true;
  builder.AddSymbol(entry);
  polyglot::backends::Symbol constant;
  constant.name = "macho_constant";
  constant.section = ".rdata";
  constant.offset = 1;
  constant.size = 1;
  constant.is_global = true;
  constant.is_function = false;
  builder.AddSymbol(constant);
  polyglot::backends::Symbol variable;
  variable.name = "macho_variable";
  variable.section = ".data";
  variable.offset = 2;
  variable.size = 1;
  variable.is_global = true;
  variable.is_function = false;
  builder.AddSymbol(variable);

  static unsigned fixture_id = 0;
  const auto stem =
      fs::temp_directory_path() /
      ("polyld_macho_symbol_address_" + std::to_string(++fixture_id));
  const fs::path object_path = stem.string() + ".obj";
  const fs::path image_path = stem.string() + ".macho";
  const auto object = builder.Build();
  REQUIRE_FALSE(object.empty());
  {
    std::ofstream output(object_path, std::ios::binary);
    REQUIRE(output.good());
    output.write(reinterpret_cast<const char *>(object.data()),
                 static_cast<std::streamsize>(object.size()));
    REQUIRE(output.good());
  }

  LinkerConfig config;
  config.input_files = {object_path.string()};
  config.output_file = image_path.string();
  config.output_format = OutputFormat::kExecutable;
  config.target_arch = TargetArch::kX86_64;
  config.target_os = "macos";
  config.container = BinaryContainer::kMachO;
  config.entry_point = "macho_entry";
  Linker linker(config);
  REQUIRE(linker.Link());

  const auto image = ReadFile(image_path);
  const auto parsed = ParseMachO(image);
  const auto &text_section = FindSection(parsed, "__TEXT", "__text");
  const auto &constant_section =
      FindSection(parsed, "__DATA_CONST", "__const");
  const auto &data_section = FindSection(parsed, "__DATA", "__data");

  REQUIRE(parsed.symbols.count("macho_entry") == 1);
  const auto &entry_symbol = parsed.symbols.at("macho_entry");
  REQUIRE(entry_symbol.section == text_section.index);
  REQUIRE(entry_symbol.value == text_section.address + 2);
  REQUIRE(image[text_section.file_offset + 2] == 0xC3);

  REQUIRE(parsed.symbols.count("macho_constant") == 1);
  const auto &constant_symbol = parsed.symbols.at("macho_constant");
  REQUIRE(constant_symbol.section == constant_section.index);
  REQUIRE(constant_symbol.value == constant_section.address + 1);
  REQUIRE(image[constant_section.file_offset + 1] == 0x22);

  REQUIRE(parsed.symbols.count("macho_variable") == 1);
  const auto &variable_symbol = parsed.symbols.at("macho_variable");
  REQUIRE(variable_symbol.section == data_section.index);
  REQUIRE(variable_symbol.value == data_section.address + 2);
  REQUIRE(image[data_section.file_offset + 2] == 0xCC);

  std::error_code ignored;
  fs::remove(object_path, ignored);
  fs::remove(image_path, ignored);
}

TEST_CASE("ARM64 final Mach-O layout patches data pages and relative branches",
          "[linker_macho][relocation][arm64]") {
  auto req = MakeMinimalRequest(kFileTypeExecute, MachOArch::kArm64);
  // ADRP x9; ADD x9,x9; RET; BL (backwards to RET).
  req.segments.front().sections.front().data = {
      0x09,0x00,0x00,0x90, 0x29,0x01,0x00,0x91,
      0xc0,0x03,0x5f,0xd6, 0x00,0x00,0x00,0x94};
  SegmentDesc data;
  data.segname = "__DATA";
  data.initprot = data.maxprot = kVmProtRead | kVmProtWrite;
  SectionDesc payload;
  payload.sectname = "__data"; payload.segname = "__DATA";
  payload.alignment_log2 = 3; payload.data.resize(512, 0);
  data.sections.push_back(payload); req.segments.push_back(data);
  FinalRelocationPatch patch;
  patch.source_section = 0; patch.target_section = 1; patch.target_offset = 264;
  patch.kind = FinalRelocationKind::kArm64Page21;
  req.final_relocations.push_back(patch);
  patch.source_offset = 4; patch.kind = FinalRelocationKind::kArm64PageOff12;
  req.final_relocations.push_back(patch);
  patch.source_offset = 12; patch.target_section = 0; patch.target_offset = 8;
  patch.kind = FinalRelocationKind::kArm64Branch26;
  req.final_relocations.push_back(patch);
  const auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());
  const auto parsed = ParseMachO(result.image);
  const auto &text = FindSection(parsed, "__TEXT", "__text");
  const auto &writable = FindSection(parsed, "__DATA", "__data");
  const auto adrp = ReadU32(result.image, text.file_offset);
  std::int64_t pages = ((adrp >> 29) & 3u) | (((adrp >> 5) & 0x7ffffu) << 2);
  if (pages & (1 << 20)) pages -= (1 << 21);
  const auto add = ReadU32(result.image, text.file_offset + 4);
  const auto resolved = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(text.address & ~4095ULL) + pages * 4096 + ((add >> 10) & 4095));
  CHECK(resolved == writable.address + 264);
  CHECK(ReadU32(result.image, text.file_offset + 12) == 0x97ffffffu);
  req.final_relocations.back().target_offset = 7;
  CHECK(BuildMachOImage(req).image.empty());
}
