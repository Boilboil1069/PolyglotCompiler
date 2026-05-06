/**
 * @file     macho_exports_trie_test.cpp
 * @brief    Coverage for the polyld Mach-O `LC_DYLD_EXPORTS_TRIE` and
 *           `LC_FUNCTION_STARTS` payloads emitted for `MH_EXECUTE`
 *           images.  Re-decodes the trie with a hand-rolled ULEB128
 *           walker and asserts that exactly two regular exports —
 *           `_main` (address = `LC_MAIN.entryoff + __TEXT.fileoff`)
 *           and `_mh_execute_header` (address = 0) — are reachable
 *           from the trie root.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tools/polyld/include/linker_macho.h"

using namespace polyglot::linker::macho;

namespace {

// ---------------------------------------------------------------------------
// Hand-rolled little-endian readers.  Avoiding `<mach-o/loader.h>` keeps
// this test buildable on every host OS rather than only on macOS.
// ---------------------------------------------------------------------------

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

// ULEB128 decoder operating on a contiguous buffer view.  Returns the
// decoded value and advances `cursor` past the consumed bytes.
std::uint64_t DecodeULEB128(const std::uint8_t *base, std::size_t limit,
                            std::size_t &cursor) {
  std::uint64_t value = 0;
  unsigned shift = 0;
  while (cursor < limit) {
    std::uint8_t b = base[cursor++];
    value |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
    if ((b & 0x80u) == 0) return value;
    shift += 7;
    REQUIRE(shift < 64);
  }
  FAIL("ULEB128 ran past the end of the trie buffer");
  return 0;
}

struct LcEntry {
  std::uint32_t cmd;
  std::uint32_t cmdsize;
  std::size_t   offset;
};

std::vector<LcEntry> WalkLoadCommands(const std::vector<std::uint8_t> &img) {
  REQUIRE(img.size() >= 32);
  const std::uint32_t ncmds = ReadU32(img, 16);
  std::vector<LcEntry> out;
  std::size_t off = 32;
  for (std::uint32_t i = 0; i < ncmds; ++i) {
    LcEntry e{ReadU32(img, off), ReadU32(img, off + 4), off};
    out.push_back(e);
    off += e.cmdsize;
  }
  return out;
}

// Recursive trie walker.  Emits `(symbol_name → address)` for every
// terminal node reachable from `node_offset`.  Re-export trie nodes
// (which would carry an extra ULEB / NUL-terminated importname after
// the address) never appear in polyld's emitted trie because every
// export uses `EXPORT_SYMBOL_FLAGS_KIND_REGULAR`, so we can keep the
// walker minimal while still detecting an unexpected re-export by
// asserting the terminal payload size matches `flags + address`.
void WalkTrie(const std::uint8_t *trie, std::size_t trie_size,
              std::uint32_t node_offset,
              const std::string &prefix,
              std::map<std::string, std::uint64_t> &out) {
  REQUIRE(node_offset < trie_size);
  std::size_t cursor = node_offset;
  std::uint64_t terminal_size = DecodeULEB128(trie, trie_size, cursor);
  if (terminal_size > 0) {
    std::size_t payload_start = cursor;
    std::uint64_t flags   = DecodeULEB128(trie, trie_size, cursor);
    std::uint64_t address = DecodeULEB128(trie, trie_size, cursor);
    REQUIRE(cursor - payload_start == terminal_size);
    // Regular exports only — flags must stay within the lower nibble.
    REQUIRE((flags & ~static_cast<std::uint64_t>(0x0Fu)) == 0u);
    out.emplace(prefix, address);
    cursor = payload_start + terminal_size;
  }
  REQUIRE(cursor < trie_size);
  std::uint8_t children_count = trie[cursor++];
  for (std::uint8_t i = 0; i < children_count; ++i) {
    std::string edge;
    while (cursor < trie_size && trie[cursor] != 0) {
      edge.push_back(static_cast<char>(trie[cursor++]));
    }
    REQUIRE(cursor < trie_size);
    REQUIRE(trie[cursor] == 0);
    ++cursor; // consume NUL
    std::uint64_t child_off = DecodeULEB128(trie, trie_size, cursor);
    REQUIRE(child_off < trie_size);
    WalkTrie(trie, trie_size, static_cast<std::uint32_t>(child_off),
             prefix + edge, out);
  }
}

BuildRequest MakeMinimalArm64Request() {
  BuildRequest r;
  r.arch                = MachOArch::kArm64;
  r.filetype            = kFileTypeExecute;
  r.base_address        = 0x100000000ULL;
  r.entry_offset        = 0x10;
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
  // Padding bytes up to (entry_offset + arm64 ret) — entry_offset = 0x10
  // means the first 16 bytes are dead instructions and the actual entry
  // is the `ret` at offset 0x10.
  s.data.assign(0x10, 0xCC);
  // arm64 `ret` (0xd65f03c0) at the entry slot.
  s.data.insert(s.data.end(), {0xC0, 0x03, 0x5F, 0xD6});
  text.sections.push_back(std::move(s));
  r.segments.push_back(std::move(text));
  return r;
}

} // namespace

TEST_CASE("polyld emits a real Mach-O exports trie carrying _main and "
          "_mh_execute_header",
          "[macho][exports_trie][polyld]") {
  auto req    = MakeMinimalArm64Request();
  auto result = BuildMachOImage(req);
  REQUIRE_FALSE(result.image.empty());

  const auto &img = result.image;
  auto cmds = WalkLoadCommands(img);

  std::optional<LcEntry> exports_trie, function_starts, lc_main, text_seg;
  std::uint64_t text_segment_fileoff = 0;
  for (const auto &c : cmds) {
    switch (c.cmd) {
      case kLcDyldExportsTrie: exports_trie = c; break;
      case kLcFunctionStarts:  function_starts = c; break;
      case kLcMain:            lc_main = c; break;
      case kLcSegment64: {
        const char *seg = reinterpret_cast<const char *>(&img[c.offset + 8]);
        if (std::strncmp(seg, "__TEXT", 16) == 0) {
          text_seg = c;
          // segment_command_64 fileoff lives at byte +40.
          text_segment_fileoff = ReadU64(img, c.offset + 40);
        }
        break;
      }
      default: break;
    }
  }
  REQUIRE(exports_trie.has_value());
  REQUIRE(function_starts.has_value());
  REQUIRE(lc_main.has_value());
  REQUIRE(text_seg.has_value());

  // ----- LC_DYLD_EXPORTS_TRIE size + content ------------------------------
  std::uint32_t et_dataoff  = ReadU32(img, exports_trie->offset + 8);
  std::uint32_t et_datasize = ReadU32(img, exports_trie->offset + 12);
  REQUIRE(et_datasize >= 24u);

  std::map<std::string, std::uint64_t> exports;
  WalkTrie(img.data() + et_dataoff, et_datasize, 0, "", exports);
  REQUIRE(exports.size() == 2u);
  REQUIRE(exports.count("_main") == 1u);
  REQUIRE(exports.count("_mh_execute_header") == 1u);

  // _main address must equal LC_MAIN.entryoff + __TEXT.fileoff.
  // entry_command layout: cmd (4) | cmdsize (4) | entryoff (8) | stacksize (8).
  std::uint64_t lc_main_entryoff = ReadU64(img, lc_main->offset + 8);
  REQUIRE(exports.at("_main") == lc_main_entryoff + text_segment_fileoff);
  REQUIRE(exports.at("_mh_execute_header") == 0u);

  // ----- LC_FUNCTION_STARTS first byte = first ULEB byte of entryoff ------
  std::uint32_t fs_dataoff  = ReadU32(img, function_starts->offset + 8);
  std::uint32_t fs_datasize = ReadU32(img, function_starts->offset + 12);
  REQUIRE(fs_datasize >= 2u);

  std::size_t cursor = 0;
  std::uint64_t decoded =
      DecodeULEB128(img.data() + fs_dataoff, fs_datasize, cursor);
  REQUIRE(decoded == lc_main_entryoff);
  // The byte immediately following the ULEB run is the 0x00 terminator.
  REQUIRE(img[fs_dataoff + cursor] == 0u);
  // The very last byte of the padded 8-byte payload is also 0x00 because
  // the writer aligns the blob with zero bytes.
  REQUIRE(img[fs_dataoff + fs_datasize - 1] == 0u);
}
