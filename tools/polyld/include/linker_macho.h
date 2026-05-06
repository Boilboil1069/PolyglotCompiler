/**
 * @file     linker_macho.h
 * @brief    Mach-O linker support: full load-command emission for
 *           MH_EXECUTE / MH_DYLIB / MH_BUNDLE images, x86_64 / arm64
 *           relocation translation, symbol-table & __LINKEDIT layout,
 *           and an optional ad-hoc LC_CODE_SIGNATURE placeholder.
 *
 * Lives alongside `linker.cpp` and `linker_pe.cpp` in the same
 * `polyglot::linker` family and follows the same style: free helpers in
 * the inner `macho` namespace, no dependency on the rest of the
 * `Linker` class except through the three `Generate*` methods at the
 * bottom of `linker_macho.cpp`.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::linker::macho {

// ---------------------------------------------------------------------------
// Mach-O constants used both by the writer and by external readers in the
// test suite.  Kept local so this header has no transitive includes from
// `linker.h`.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kMachOMagic64    = 0xFEEDFACFu; // MH_MAGIC_64
constexpr std::uint32_t kCpuTypeX86_64   = 0x01000007u;
constexpr std::uint32_t kCpuTypeArm64    = 0x0100000Cu;
constexpr std::uint32_t kCpuSubtypeAll   = 0x00000003u;

constexpr std::uint32_t kFileTypeExecute = 2;  // MH_EXECUTE
constexpr std::uint32_t kFileTypeDylib   = 6;  // MH_DYLIB
constexpr std::uint32_t kFileTypeBundle  = 8;  // MH_BUNDLE

// Header flags (bitfield, can be OR'd together).
constexpr std::uint32_t kMhNoUndefs   = 0x0000001u; // MH_NOUNDEFS
constexpr std::uint32_t kMhDyldLink   = 0x0000004u; // MH_DYLDLINK
constexpr std::uint32_t kMhTwoLevel   = 0x0000080u; // MH_TWOLEVEL
constexpr std::uint32_t kMhNoReexp    = 0x0100000u; // MH_NO_REEXPORTED_DYLIBS
constexpr std::uint32_t kMhPie        = 0x0200000u; // MH_PIE

// Load command identifiers.
constexpr std::uint32_t kLcReqDyld     = 0x80000000u;
constexpr std::uint32_t kLcSegment64   = 0x19u;
constexpr std::uint32_t kLcSymtab      = 0x02u;
constexpr std::uint32_t kLcDysymtab    = 0x0Bu;
constexpr std::uint32_t kLcLoadDylib   = 0x0Cu;
constexpr std::uint32_t kLcIdDylib     = 0x0Du;
constexpr std::uint32_t kLcLoadDylinker= 0x0Eu;
constexpr std::uint32_t kLcUuid        = 0x1Bu;
constexpr std::uint32_t kLcCodeSig     = 0x1Du; // LC_CODE_SIGNATURE
constexpr std::uint32_t kLcDyldChainedFixups = 0x80000034u; // LC_DYLD_CHAINED_FIXUPS | LC_REQ_DYLD
constexpr std::uint32_t kLcDyldExportsTrie   = 0x80000033u; // LC_DYLD_EXPORTS_TRIE | LC_REQ_DYLD
constexpr std::uint32_t kLcFunctionStarts    = 0x00000026u; // LC_FUNCTION_STARTS
constexpr std::uint32_t kLcDataInCode        = 0x00000029u; // LC_DATA_IN_CODE
constexpr std::uint32_t kLcSourceVer   = 0x2Au; // LC_SOURCE_VERSION
constexpr std::uint32_t kLcBuildVer    = 0x32u; // LC_BUILD_VERSION
constexpr std::uint32_t kLcMain        = 0x80000028u; // LC_MAIN | LC_REQ_DYLD

// VM protections.
constexpr std::uint32_t kVmProtRead    = 0x1u;
constexpr std::uint32_t kVmProtWrite   = 0x2u;
constexpr std::uint32_t kVmProtExecute = 0x4u;

// Section attributes / types used in the section header `flags` field.
constexpr std::uint32_t kSectionTypeRegular         = 0x0u; // S_REGULAR
constexpr std::uint32_t kSectionAttrPureInstr       = 0x80000000u; // S_ATTR_PURE_INSTRUCTIONS
constexpr std::uint32_t kSectionAttrSomeInstr       = 0x00000400u; // S_ATTR_SOME_INSTRUCTIONS

// LC_BUILD_VERSION platforms.
constexpr std::uint32_t kPlatformMacOS = 1;

// Symbol nlist constants.
constexpr std::uint8_t kNTypeStab = 0xE0;
constexpr std::uint8_t kNTypePext = 0x10;
constexpr std::uint8_t kNTypeMask = 0x0E;
constexpr std::uint8_t kNTypeExt  = 0x01;
constexpr std::uint8_t kNTypeUndf = 0x0;
constexpr std::uint8_t kNTypeAbs  = 0x2;
constexpr std::uint8_t kNTypeSect = 0xE; // N_SECT
constexpr std::uint8_t kNTypePbud = 0xC;
constexpr std::uint8_t kNTypeIndr = 0xA;

// ---------------------------------------------------------------------------
// Relocation translation
// ---------------------------------------------------------------------------

/// Architectures the Mach-O writer knows how to translate relocations for.
enum class MachOArch {
  kX86_64,
  kArm64,
};

/// Mach-O relocation type codes (`r_type` field in `relocation_info`).
/// These mirror Apple's `<mach-o/x86_64/reloc.h>` and `<mach-o/arm64/reloc.h>`
/// so that downstream tools (otool, llvm-objdump) interpret the bytes
/// emitted by `BuildSectionRelocations` exactly as Apple's toolchain does.
enum class X86_64Reloc : std::uint8_t {
  kUnsigned       = 0,
  kSigned         = 1,
  kBranch         = 2,
  kGotLoad        = 3,
  kGot            = 4,
  kSubtractor     = 5,
  kSigned1        = 6,
  kSigned2        = 7,
  kSigned4        = 8,
  kTlv            = 9,
};

enum class Arm64Reloc : std::uint8_t {
  kUnsigned       = 0,
  kSubtractor     = 1,
  kBranch26       = 2,
  kPage21         = 3,
  kPageoff12      = 4,
  kGotLoadPage21  = 5,
  kGotLoadPageoff12 = 6,
  kPointerToGot   = 7,
  kTlvpPage21     = 8,
  kTlvpPageoff12  = 9,
  kAddend         = 10,
};

/// Light-weight description of a relocation surviving into the final
/// image.  Mirrors the shape of `polyglot::linker::pe::PendingRelocation`
/// so the two translators stay symmetrical and unit-testable in
/// isolation from any `Linker` instance.
struct PendingRelocation {
  std::uint32_t address{0};       ///< Section-relative offset of the patch site.
  std::uint32_t symbol_index{0};  ///< Index into the emitted symbol table.
  bool          is_pc_relative{false};
  bool          is_extern{true};
  std::uint8_t  length{2};        ///< Encoded as 0=byte / 1=word / 2=long / 3=quad.
  std::uint8_t  type{0};          ///< Architecture-specific r_type code.
};

/// One entry in the on-disk `relocation_info[]` array: 8 bytes laid out
/// per `<mach-o/reloc.h>` (`r_address` + a packed bitfield).  The
/// translator returns these in section order ready to memcpy into the
/// `__LINKEDIT` reloc area.
struct OnDiskRelocation {
  std::uint32_t r_address{0};
  std::uint32_t r_packed{0};
};

/// Translate a vector of `PendingRelocation`s into the on-disk
/// `relocation_info` byte shape.  `errors_out` collects any record that
/// references an unknown `type` for the requested architecture
/// (`polyld-err-E3220`).  The function still translates the well-formed
/// entries so callers can surface the full bad-record set in one pass.
bool TranslateRelocations(MachOArch arch,
                          const std::vector<PendingRelocation> &input,
                          std::vector<OnDiskRelocation> &out,
                          std::vector<std::string> &errors_out);

// ---------------------------------------------------------------------------
// Section / segment description fed to the writer
// ---------------------------------------------------------------------------

/// One emittable section under one segment.  `data` is the raw bytes
/// that will be written at `file_offset` once layout is finalised; the
/// writer copies it verbatim and updates the section header in place.
struct SectionDesc {
  std::string   sectname;             ///< e.g. "__text", up to 16 bytes.
  std::string   segname;              ///< e.g. "__TEXT", up to 16 bytes.
  std::uint32_t flags{0};             ///< Section type | attributes.
  std::uint32_t alignment_log2{4};    ///< 2^4 = 16-byte alignment by default.
  std::vector<std::uint8_t> data;
};

/// One emittable segment, owning a group of consecutive sections.
struct SegmentDesc {
  std::string   segname;              ///< e.g. "__TEXT".
  std::uint32_t initprot{0};          ///< VM_PROT_* bitfield.
  std::uint32_t maxprot{0};           ///< VM_PROT_* bitfield.
  std::vector<SectionDesc> sections;  ///< 0..N sections.
  std::uint64_t vmsize_override{0};   ///< 0 = derive from sections, else use as-is.
  std::uint64_t filesize_override{0}; ///< 0 = derive, else use literal value.
};

/// One symbol the writer should emit into the output `__LINKEDIT`.
struct SymbolDesc {
  std::string name;     ///< Mangled symbol name as it appears in the string table.
  std::uint8_t n_type{0}; ///< nlist `n_type` byte (e.g. N_SECT|N_EXT).
  std::uint8_t n_sect{0}; ///< 1-based section index, 0 for undefined.
  std::uint16_t n_desc{0};
  std::uint64_t n_value{0};
};

/// Top-level request handed to `BuildMachOImage`.
struct BuildRequest {
  MachOArch arch{MachOArch::kX86_64};
  std::uint32_t filetype{kFileTypeExecute};
  std::uint32_t header_flags{kMhNoUndefs | kMhDyldLink | kMhTwoLevel | kMhPie};
  std::uint64_t base_address{0};      ///< 0 means writer picks a sensible default.
  std::uint64_t entry_offset{0};      ///< Offset from segment __TEXT to the entry.
  std::uint64_t stack_size{0};        ///< 0 means "let the loader pick".
  std::vector<SegmentDesc> segments;  ///< Excludes __LINKEDIT (writer adds it).
  std::vector<SymbolDesc> symbols;    ///< Local + external + undef, in that order.
  std::uint32_t local_count{0};
  std::uint32_t extdef_count{0};
  std::uint32_t undef_count{0};
  bool emit_pagezero{true};           ///< Executables only.
  bool emit_dyld_info{true};          ///< LC_LOAD_DYLINKER + LC_LOAD_DYLIB.
  bool emit_code_signature{false};    ///< Reserve LC_CODE_SIGNATURE region.
  std::string install_name;           ///< Used for LC_ID_DYLIB (dylib only).
  /// Identifier embedded in the linker-signed CodeDirectory (typically the
  /// output filename's basename).  When empty the writer picks "polyld".
  std::string identifier;
  std::string source_version{"1.0.0"};
  std::array<std::uint8_t, 3> minos{ {26, 0, 0} };
  std::array<std::uint8_t, 3> sdk  { {26, 4, 0} };
};

/// Result of `BuildMachOImage`.
struct BuildResult {
  std::vector<std::uint8_t> image;
  std::uint64_t text_segment_vmaddr{0};
  std::uint64_t entry_vmaddr{0};
  std::uint32_t load_commands_size{0};
  std::uint32_t num_load_commands{0};
};

/// Build a complete Mach-O image from `req`.  Returns an empty `image`
/// when the request is internally inconsistent (e.g. dylib without
/// install_name, executable filetype with no segments).
BuildResult BuildMachOImage(const BuildRequest &req);

// ---------------------------------------------------------------------------
// Hashing helper (exposed for unit tests)
// ---------------------------------------------------------------------------

/// SHA-256 of `bytes`, truncated to 16 bytes — used for the LC_UUID
/// payload (Apple's tools use a 16-byte UUID derived from segment
/// content; truncating SHA-256 gives the same stability guarantees
/// without requiring a Foundation UUID generator).
std::array<std::uint8_t, 16>
Uuid16FromContent(const std::vector<std::uint8_t> &bytes);

} // namespace polyglot::linker::macho
