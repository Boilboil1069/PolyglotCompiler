/**
 * @file     macho_linkedit_data_emit_test.cpp
 * @brief    Structural coverage for `LC_FUNCTION_STARTS` and
 *           `LC_DATA_IN_CODE` emission inside the polyld Mach-O writer.
 *           Asserts load-command ordering, dataoff chaining across the
 *           LINKEDIT segment, and `__LINKEDIT.filesize` consistency
 *           against the trailing code-signature blob.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "tools/polyld/include/linker_macho.h"

using namespace polyglot::linker::macho;

namespace {

std::uint32_t ReadU32(const std::vector<std::uint8_t> &b, std::size_t off) {
  REQUIRE(off + 4 <= b.size());
  return static_cast<std::uint32_t>(b[off]) |
         (static_cast<std::uint32_t>(b[off + 1]) << 8) |
         (static_cast<std::uint32_t>(b[off + 2]) << 16) |
         (static_cast<std::uint32_t>(b[off + 3]) << 24);
}
std::uint64_t ReadU64(const std::vector<std::uint8_t> &b, std::size_t off) {
  REQUIRE(off + 8 <= b.size());
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[off + i]) << (8 * i);
  return v;
}

struct LcEntry {
  std::uint32_t cmd;
  std::uint32_t cmdsize;
  std::size_t   offset; // file offset of the LC header
};

std::vector<LcEntry> WalkLoadCommands(const std::vector<std::uint8_t> &img) {
  REQUIRE(img.size() >= 32);
  const std::uint32_t ncmds = ReadU32(img, 16);
  std::vector<LcEntry> out;
  std::size_t off = 32; // mach_header_64 size
  for (std::uint32_t i = 0; i < ncmds; ++i) {
    LcEntry e{ReadU32(img, off), ReadU32(img, off + 4), off};
    out.push_back(e);
    off += e.cmdsize;
  }
  return out;
}

BuildRequest MakeMinimalArm64Request() {
  BuildRequest r;
  r.arch                = MachOArch::kArm64;
  r.filetype            = kFileTypeExecute;
  r.base_address        = 0x100000000ULL;
  r.entry_offset        = 0;
  r.emit_code_signature = true;
  SegmentDesc text;
  text.segname  = "__TEXT";
  text.initprot = kVmProtRead | kVmProtExecute;
  text.maxprot  = kVmProtRead | kVmProtExecute;
  SectionDesc s;
  s.sectname       = "__text";
  s.segname        = "__TEXT";
  s.flags          = kSectionTypeRegular | kSectionAttrPureInstr |
                     kSectionAttrSomeInstr;
  s.alignment_log2 = 4;
  // arm64 `ret` (0xd65f03c0) — content is irrelevant for layout assertions.
  s.data = {0xc0, 0x03, 0x5f, 0xd6};
  text.sections.push_back(std::move(s));
  r.segments.push_back(std::move(text));
  return r;
}

} // namespace

TEST_CASE("polyld emits LC_FUNCTION_STARTS and LC_DATA_IN_CODE in the Mach-O LINKEDIT chain",
          "[macho][linkedit][polyld]") {
  auto req    = MakeMinimalArm64Request();
  auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());

  const auto &img = result.image;
  auto cmds = WalkLoadCommands(img);

  std::optional<LcEntry> chained, exports_trie, function_starts, data_in_code,
      symtab, code_signature;
  std::optional<LcEntry> linkedit_seg;
  std::vector<std::uint32_t> seq;
  for (const auto &c : cmds) {
    seq.push_back(c.cmd);
    switch (c.cmd) {
      case kLcDyldChainedFixups: chained = c; break;
      case kLcDyldExportsTrie:   exports_trie = c; break;
      case kLcFunctionStarts:    function_starts = c; break;
      case kLcDataInCode:        data_in_code = c; break;
      case kLcSymtab:            symtab = c; break;
      case 0x1Du /*LC_CODE_SIGNATURE*/: code_signature = c; break;
      case kLcSegment64: {
        // segname starts 8 bytes into the LC header.
        const char *seg = reinterpret_cast<const char *>(&img[c.offset + 8]);
        if (std::strncmp(seg, "__LINKEDIT", 16) == 0) linkedit_seg = c;
        break;
      }
      default: break;
    }
  }

  REQUIRE(chained.has_value());
  REQUIRE(exports_trie.has_value());
  REQUIRE(function_starts.has_value());
  REQUIRE(data_in_code.has_value());
  REQUIRE(symtab.has_value());
  REQUIRE(code_signature.has_value());
  REQUIRE(linkedit_seg.has_value());

  // Strict relative ordering of the LINKEDIT-payload-bearing LCs.
  auto idx = [&](std::uint32_t c) {
    for (std::size_t i = 0; i < seq.size(); ++i)
      if (seq[i] == c) return static_cast<int>(i);
    return -1;
  };
  REQUIRE(idx(kLcDyldChainedFixups) < idx(kLcDyldExportsTrie));
  REQUIRE(idx(kLcDyldExportsTrie)   < idx(kLcFunctionStarts));
  REQUIRE(idx(kLcFunctionStarts)    < idx(kLcDataInCode));
  REQUIRE(idx(kLcDataInCode)        < idx(kLcSymtab));

  // Decode the four 16-byte linkedit_data_command bodies.
  auto fs_dataoff  = ReadU32(img, function_starts->offset + 8);
  auto fs_datasize = ReadU32(img, function_starts->offset + 12);
  auto dic_dataoff  = ReadU32(img, data_in_code->offset + 8);
  auto dic_datasize = ReadU32(img, data_in_code->offset + 12);
  auto et_dataoff  = ReadU32(img, exports_trie->offset + 8);
  auto et_datasize = ReadU32(img, exports_trie->offset + 12);
  auto sym_symoff  = ReadU32(img, symtab->offset + 8);

  // LC_FUNCTION_STARTS — single ULEB terminator padded to 8 bytes.
  REQUIRE(fs_datasize == 8u);
  for (std::uint32_t i = 0; i < 8; ++i) REQUIRE(img[fs_dataoff + i] == 0u);

  // LC_DATA_IN_CODE — empty payload.
  REQUIRE(dic_datasize == 0u);

  // dataoff chaining: exports_trie → function_starts → data_in_code → symtab.
  REQUIRE(fs_dataoff  == et_dataoff + et_datasize);
  REQUIRE(dic_dataoff == fs_dataoff + fs_datasize);
  REQUIRE(sym_symoff  == dic_dataoff + dic_datasize);

  // __LINKEDIT.filesize must extend exactly to the end of the
  // LC_CODE_SIGNATURE blob.  segment_command_64 layout: name[16] at +8,
  // vmaddr at +24, vmsize at +32, fileoff at +40, filesize at +48.
  auto linkedit_fileoff  = ReadU64(img, linkedit_seg->offset + 40);
  auto linkedit_filesize = ReadU64(img, linkedit_seg->offset + 48);
  auto cs_dataoff  = ReadU32(img, code_signature->offset + 8);
  auto cs_datasize = ReadU32(img, code_signature->offset + 12);
  REQUIRE(linkedit_filesize ==
          static_cast<std::uint64_t>(cs_dataoff + cs_datasize) - linkedit_fileoff);
}
