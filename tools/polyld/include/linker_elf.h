/**
 * @file     linker_elf.h
 * @brief    ELF64 linker support: real `ET_EXEC` writer for Linux
 *           x86_64 / aarch64 with a polyld-injected `_start` stub that
 *           tail-calls `main` and translates its return value into a
 *           libc-free `exit` syscall.  Lives alongside `linker_macho.h`
 *           in the same `polyglot::linker` family and follows the same
 *           free-function style: a single `BuildELFImage(BuildRequest)`
 *           entry point producing a self-contained byte image, plus a
 *           pair of `BuildStartStub*` helpers exposed for unit tests so
 *           they can assert byte-exact equality against the bytes that
 *           land at the head of `.text`.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::linker::elf {

// ---------------------------------------------------------------------------
// ELF identification + selected constants used both by the writer and by
// external readers in the test suite.  Kept local so this header has no
// transitive include of the host `<elf.h>` (which is missing on Windows).
// ---------------------------------------------------------------------------

constexpr std::uint8_t  kElfMag0     = 0x7Fu;
constexpr std::uint8_t  kElfMag1     = 'E';
constexpr std::uint8_t  kElfMag2     = 'L';
constexpr std::uint8_t  kElfMag3     = 'F';
constexpr std::uint8_t  kElfClass64  = 2;
constexpr std::uint8_t  kElfData2Lsb = 1;
constexpr std::uint8_t  kEvCurrent   = 1;

constexpr std::uint16_t kEtExec      = 2;

constexpr std::uint16_t kEmX86_64    = 62;
constexpr std::uint16_t kEmAarch64   = 183;

// Program-header types and flags.
constexpr std::uint32_t kPtNull      = 0;
constexpr std::uint32_t kPtLoad      = 1;
constexpr std::uint32_t kPtNote      = 4;
constexpr std::uint32_t kPtPhdr      = 6;
constexpr std::uint32_t kPtGnuStack  = 0x6474E551u;
constexpr std::uint32_t kPtGnuRelRo  = 0x6474E552u;

constexpr std::uint32_t kPfX         = 0x1u;
constexpr std::uint32_t kPfW         = 0x2u;
constexpr std::uint32_t kPfR         = 0x4u;

// Section-header types and flags.
constexpr std::uint32_t kShtNull     = 0;
constexpr std::uint32_t kShtProgBits = 1;
constexpr std::uint32_t kShtSymTab   = 2;
constexpr std::uint32_t kShtStrTab   = 3;
constexpr std::uint32_t kShtNoBits   = 8;

constexpr std::uint64_t kShfWrite    = 0x1u;
constexpr std::uint64_t kShfAlloc    = 0x2u;
constexpr std::uint64_t kShfExecInst = 0x4u;

// Default executable load address — chosen below the typical Linux
// ASLR range so a static `ET_EXEC` is mappable on every kernel since
// 3.x without requiring `MAP_FIXED_NOREPLACE` heroics.
constexpr std::uint64_t kDefaultExeBase = 0x400000ull;

// Page size assumed by the writer when laying out R/X versus R/W
// segments.  Matches both x86_64 and aarch64 default user pages so the
// resulting image satisfies `(p_offset & (p_align-1)) == (p_vaddr &
// (p_align-1))` on either architecture.
constexpr std::uint64_t kPageSize       = 0x1000ull;

// Fixed length of the polyld-injected `_start` stub.  Both the
// x86_64 and the aarch64 variants are hand-assembled and padded to
// this value so callers can hard-code "main lives at offset
// kStartStubSize" when constructing `.text` payloads.
constexpr std::size_t   kStartStubSize  = 16;

// ---------------------------------------------------------------------------
// Writer-facing data model
// ---------------------------------------------------------------------------

/// Architectures the ELF writer knows how to emit.  Matches the
/// `e_machine` axis of the ELF header.
enum class Arch {
  kX86_64,
  kAArch64,
};

/// Top-level request handed to `BuildELFImage`.  Mirrors the field
/// shape of `polyglot::linker::macho::BuildRequest` so callers porting
/// between the two writers see the same vocabulary.
struct BuildRequest {
  Arch          arch{Arch::kX86_64};
  std::uint64_t base_address{kDefaultExeBase};
  /// Raw user `.text` payload.  The writer prepends the architecture-
  /// specific `_start` stub so the final on-disk `.text` byte stream
  /// looks like `[ stub | user_text ]`.  `entry_offset` is therefore
  /// implicit (always 0): the ELF entry point points at the stub, not
  /// at user code.  Set this to the bytes of `main` itself.
  std::vector<std::uint8_t> text;
  /// Optional read-only data section.  When empty the writer omits
  /// both the `.rodata` section header and any redundant PT_LOAD.
  std::vector<std::uint8_t> rodata;
  /// Optional read-write data section.  Backing PT_LOAD is only
  /// emitted when at least one of `data` or `bss_size` is non-zero.
  std::vector<std::uint8_t> data;
  /// Trailing zero-initialised payload appended to the R/W segment in
  /// memory image (`p_memsz`) but not in file image (`p_filesz`).
  std::uint64_t bss_size{0};
};

/// Result of `BuildELFImage`.  An empty `image` indicates the writer
/// rejected the request (currently never — kept for parity with
/// `BuildMachOImage`'s contract).
struct BuildResult {
  std::vector<std::uint8_t> image;
  std::uint64_t entry_vaddr{0};
  std::uint64_t text_vaddr{0};
  std::uint64_t text_file_offset{0};
};

/// Returns the byte template polyld emits for the architecture's
/// `_start` stub.  Always exactly `kStartStubSize` bytes long.  The
/// stub:
///   * tail-calls `main` (a `call`/`bl` whose displacement assumes
///     `main` lives immediately after the stub);
///   * forwards `main`'s return value (`%rax` / `x0`) into the kernel
///     `exit` syscall (`__NR_exit = 60` on x86_64, `93` on aarch64);
///   * is padded with the architecture's canonical NOP so the .text
///     headroom remains a fixed 16 bytes.
std::vector<std::uint8_t> BuildStartStubX86_64();
std::vector<std::uint8_t> BuildStartStubArm64();

/// Build a complete static-`ET_EXEC` ELF image from `req`.
BuildResult BuildELFImage(const BuildRequest &req);

/// Build a self-contained Linux syscall text payload that writes each message
/// to stdout in order and then exits with status 0.  The caller may pass the
/// returned bytes as `BuildRequest::text`; the ELF `_start` stub will call it
/// as `main`, but the payload normally terminates the process itself.
std::vector<std::uint8_t>
BuildPrintlnSequenceELF(Arch arch, const std::vector<std::string> &messages);

} // namespace polyglot::linker::elf
