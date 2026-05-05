/**
 * @file     pe_writer.h
 * @brief    PE32+ executable image writer for the Polyglot linker
 *
 * Standalone, header + impl, no dependency on the rest of the linker class.
 * Builds a fully valid Windows PE32+ image (`MZ` / `PE\0\0` / Optional Header
 * with Magic = 0x20B for AMD64) from raw caller-supplied byte buffers and a
 * description of the imports needed.
 *
 * The PE image produced by this writer is the **bare minimum** runnable shape:
 *   - One `.text` section containing the caller's code bytes (RX).
 *   - One `.idata` section holding the Import Directory + IAT + name table.
 *   - One DOS stub (the conventional 64-byte stub printing
 *     "This program cannot be run in DOS mode.").
 *   - Optional Header pointing AddressOfEntryPoint at a caller-supplied RVA
 *     inside `.text`.
 *
 * This writer deliberately does NOT yet support:
 *   - Base relocations (ImageBase is fixed; the loader will not relocate).
 *   - Export Directory.
 *   - TLS / Resource / Debug directories.
 *   - Multiple TEXT sections.
 *
 * Those will be added incrementally as the rest of the linker grows beyond
 * the current "ExitProcess(0) smoke" milestone.  See PolyglotCompiler v1.5.0
 * release notes for the staged plan.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::linker::pe {

/// One DLL import to be wired into the produced PE image.  The IAT slot for
/// each named function is referenced via `IATSlotRVA(dll, name)` after
/// `BuildPE32PlusImage()` returns.
struct DllImport {
  std::string dll_name;                  ///< e.g. "kernel32.dll"
  std::vector<std::string> function_names; ///< e.g. {"ExitProcess"}
};

// ===========================================================================
// BIN-3 hardening: extended PE32+ feature set
//
// The fields below extend `BuildRequest` additively.  All defaults reproduce
// the legacy single-section AMD64-only console layout, so callers that do
// not opt in see byte-identical output to pre-BIN-3 behaviour.
// ===========================================================================

/// PE Optional Header `Subsystem` field (selected canonical values).
enum class PESubsystem : std::uint16_t {
  kWindowsCui = 3,    ///< Console subsystem (default)
  kWindowsGui = 2,    ///< GUI subsystem (no console window)
  kEfiApplication = 10, ///< EFI application
  kNativeDriver = 1,  ///< Native / driver (NT Native API)
};

/// COFF File Header `Machine` field.  Drives the Optional Header magic
/// only indirectly: this writer always emits PE32+ (Magic = 0x20B), so the
/// `kI386` value here exists primarily for compatibility metadata; the
/// resulting image will still be a 64-bit PE.  arm64-windows uses
/// `IMAGE_FILE_MACHINE_ARM64` (0xAA64).
enum class PEMachine : std::uint16_t {
  kAmd64 = 0x8664,    ///< x86_64 (default)
  kArm64 = 0xAA64,    ///< AArch64 / arm64-windows
  kI386  = 0x014C,    ///< 32-bit metadata; image still PE32+
};

/// Single base-relocation entry: the `rva` is the address of a value inside
/// the produced image whose stored bytes encode an absolute virtual address
/// based on `BuildRequest::image_base` and which therefore must be patched
/// by the loader if the image is rebased.  `type` is one of the PE
/// `IMAGE_REL_BASED_*` 4-bit codes:
///
///   0  IMAGE_REL_BASED_ABSOLUTE                (padding only)
///   3  IMAGE_REL_BASED_HIGHLOW                 (x86 32-bit absolute)
///   10 IMAGE_REL_BASED_DIR64                   (x64 64-bit absolute)
///   3  IMAGE_REL_BASED_ARM64_BRANCH26          (arm64-only, value reused)
///   4  IMAGE_REL_BASED_ARM64_PAGEBASE_REL21    (arm64 ADRP)
///   7  IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A    (arm64 ADD imm12)
///   9  IMAGE_REL_BASED_ARM64_PAGEOFFSET_12L    (arm64 LDR/STR imm12)
struct BaseRelocation {
  std::uint32_t rva{0}; ///< RVA of the patched location
  std::uint8_t  type{10}; ///< IMAGE_REL_BASED_* (defaults to DIR64 for x64)
};

/// Result of a PE32+ build.  `image` is the file bytes ready to be written to
/// disk.  `entry_rva` echoes back the entry RVA passed in so callers can
/// double-check.  `iat_slot_rva` exposes the runtime-IAT slot RVA for each
/// imported function so caller-emitted code can encode `call qword ptr
/// [rip+disp32]` against the correct slot.  `rdata_rva` is non-zero when
/// the request supplied `rdata_bytes`; callers can encode `lea reg,
/// [rip+disp32]` against `rdata_rva + offset_within_rdata`.
struct BuildResult {
  std::vector<std::uint8_t> image;
  std::uint32_t entry_rva{0};
  std::uint32_t rdata_rva{0};
  /// Map of "dll!Name" -> RVA of the IAT qword slot.  Use the lowercase
  /// joined key, e.g. "kernel32.dll!ExitProcess".
  std::vector<std::pair<std::string, std::uint32_t>> iat_slot_rva;
};

/// Inputs for `BuildPE32PlusImage`.  `text_bytes` is laid out at RVA
/// `kTextRVA` (= 0x1000) inside the produced image.  `entry_offset_in_text`
/// is added to that base to compute AddressOfEntryPoint.  When `rdata_bytes`
/// is non-empty, an additional read-only `.rdata` section is emitted between
/// `.text` and `.idata`; its RVA is reported back via `BuildResult::rdata_rva`
/// so callers can encode RIP-relative addressing into the data buffer.
struct BuildRequest {
  /// Code bytes that will be placed in the `.text` section.
  std::vector<std::uint8_t> text_bytes;
  /// Optional read-only data bytes (string literals, jump tables, etc.).
  /// Laid out in a `.rdata` section between `.text` and `.idata`.  Empty
  /// means no `.rdata` section is emitted.
  std::vector<std::uint8_t> rdata_bytes;
  /// Offset (in bytes) inside `text_bytes` where execution should begin.
  std::uint32_t entry_offset_in_text{0};
  /// One entry per imported DLL.  May be empty (no imports).
  std::vector<DllImport> imports;
  /// Optional override of the produced image's preferred load address.
  /// Default 0x140000000 matches the standard Windows AMD64 console base.
  std::uint64_t image_base{0x140000000ULL};

  // -------------------------------------------------------------------------
  // BIN-3 additions (all default-quiet: empty / Console / AMD64).
  // -------------------------------------------------------------------------

  /// Optional read-write `.data` section bytes (initialised data).  Laid
  /// out between `.rdata` and `.bss` when present.  Empty ⇒ no `.data`.
  std::vector<std::uint8_t> data_bytes;

  /// Size in bytes of the `.bss` section (uninitialised, zero-filled at
  /// load time).  Zero ⇒ no `.bss`.  No raw bytes are written for `.bss`.
  std::uint32_t bss_size{0};

  /// Optional x64 exception unwind tables.  When non-empty, `.pdata` is
  /// emitted with `pdata_bytes` (an array of `RUNTIME_FUNCTION` records,
  /// 12 bytes each) and `.xdata` with `xdata_bytes` (the matching
  /// `UNWIND_INFO` blocks).  The PE Optional Header's Exception Table
  /// directory entry (index 3) is populated to point at `.pdata`.  An
  /// empty `.pdata` still emits a zero-sized directory entry per spec.
  std::vector<std::uint8_t> pdata_bytes;
  std::vector<std::uint8_t> xdata_bytes;

  /// Base relocations to be packed into a `.reloc` section.  Each entry
  /// is grouped by its 4 KiB page; per-page blocks are emitted as an
  /// `IMAGE_BASE_RELOCATION` header followed by an array of WORDs whose
  /// high 4 bits are `type` and low 12 bits are the offset within the
  /// page.  The block size is rounded up to a 4-byte boundary by
  /// appending an `IMAGE_REL_BASED_ABSOLUTE` (type=0) padding entry as
  /// needed.  Non-empty implies `IMAGE_FILE_RELOCS_STRIPPED` is cleared
  /// and `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE` is set.
  std::vector<BaseRelocation> base_relocations;

  /// Target machine.  Drives the COFF File Header `Machine` field as well
  /// as the default `IMAGE_REL_BASED_*` reloc type chosen by callers.
  /// Defaults to AMD64 to preserve legacy behaviour.
  PEMachine machine{PEMachine::kAmd64};

  /// Optional Header `Subsystem`.  Defaults to Windows console (CUI).
  PESubsystem subsystem{PESubsystem::kWindowsCui};

  /// Optional Header `DllCharacteristics`.  Zero ⇒ writer picks a
  /// platform-appropriate default:
  ///   * AMD64 / ARM64 ⇒ DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
  ///                     | HIGH_ENTROPY_VA
  ///   * I386          ⇒ DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
  /// When `base_relocations` is empty the writer additionally falls back
  /// to the legacy "no DYNAMIC_BASE / no HIGH_ENTROPY_VA" default so that
  /// pre-BIN-3 callers still produce byte-identical output.  Pass an
  /// explicit non-zero value to override completely.
  std::uint16_t dll_characteristics{0};

  /// Optional Image File Header `Characteristics` to OR into the writer's
  /// computed value.  Useful for callers that need `IMAGE_FILE_DLL`
  /// (0x2000) without rerouting through a separate writer.  Bit
  /// `IMAGE_FILE_RELOCS_STRIPPED` (0x0001) is automatically cleared by
  /// the writer when `base_relocations` is non-empty.
  std::uint16_t extra_file_characteristics{0};
};

/// Build a complete PE32+ image from a code buffer and an import description.
/// On any malformed input (e.g. entry offset out of range) returns a
/// `BuildResult` with `image.empty() == true`.
BuildResult BuildPE32PlusImage(const BuildRequest &req);

/// Encode the on-disk byte sequence for the `.reloc` section corresponding
/// to a flat list of `BaseRelocation` entries.  The output groups entries by
/// their 4 KiB virtual page, sorts within each page by offset, and emits one
/// `IMAGE_BASE_RELOCATION` block per non-empty page.  Each block's
/// `SizeOfBlock` is rounded up to a 4-byte boundary by appending an
/// `IMAGE_REL_BASED_ABSOLUTE` padding entry when the entry count is odd.
/// Returns an empty vector if `relocs` is empty.
std::vector<std::uint8_t>
BuildBaseRelocSection(const std::vector<BaseRelocation> &relocs);

/// Decode a `.reloc` section byte buffer back into the equivalent
/// `BaseRelocation` list (skipping `IMAGE_REL_BASED_ABSOLUTE` padding
/// entries).  Used by tests to round-trip the encoder.  Returns an empty
/// vector when the buffer is malformed (e.g. truncated header / inconsistent
/// `SizeOfBlock`).
std::vector<BaseRelocation>
DecodeBaseRelocSection(const std::vector<std::uint8_t> &bytes);

/// Convenience: produce a self-contained runnable PE32+ image whose only
/// behaviour is to call `kernel32!ExitProcess(0)`.  Used both by the smoke
/// test and as the universal fallback when the linker has no real user
/// code to lay out.  The returned `BuildResult.image` is byte-for-byte
/// runnable on Windows AMD64.
BuildResult BuildMinimalExitZeroImage();

/// Wrap a caller-supplied `.text` byte buffer with an appended Win32 entry
/// shim that terminates the process via `kernel32!ExitProcess(0)`, then
/// produce a fully runnable PE32+ image around it.
///
/// The produced image's AddressOfEntryPoint is set to the shim, NOT to the
/// start of `user_text_bytes`.  The user bytes are still embedded in `.text`
/// (so debuggers can disassemble them and a future linker iteration can
/// re-target the entry to a real `main` once Win32 ABI shims are in place),
/// but they are not invoked by this iteration.
///
/// `user_text_bytes` may be empty, in which case the result is identical to
/// `BuildMinimalExitZeroImage()`.
BuildResult BuildExitZeroPE(const std::vector<std::uint8_t> &user_text_bytes);

/// Wrap a caller-supplied `.text` byte buffer with an appended Win32 entry
/// shim that:
///
///   1. invokes the user's `main` function (located at
///      `user_main_offset_in_text` bytes into `user_text_bytes`) using the
///      Windows AMD64 ABI (32 bytes of shadow space, RSP 16-byte aligned);
///   2. captures the user's `int` return value (low 32 bits of RAX) into
///      ECX as the first argument to `kernel32!ExitProcess`;
///   3. terminates the process via `kernel32!ExitProcess(exit_code)`.
///
/// The produced image's `AddressOfEntryPoint` is set to the SHIM (so the
/// OS first runs our ABI prologue), and the shim then `call`s into the user
/// `main` at its real RVA.  This is the path that lets a `.ploy` program
/// such as `FUNC main() -> i32 { RETURN 42; }` produce a `.exe` whose
/// `GetExitCodeProcess` returns `42`.
///
/// On invalid input (`user_text_bytes` empty, or `user_main_offset_in_text`
/// out of range) the function returns a `BuildResult` with
/// `image.empty() == true`.
BuildResult BuildExeWithUserEntry(const std::vector<std::uint8_t> &user_text_bytes,
                                  std::uint32_t user_main_offset_in_text);

/// Generate the AMD64 byte sequence for the **user-entry** shim: invoke the
/// user `main` at `user_main_rva`, then forward its return value (low 32
/// bits of RAX) to `kernel32!ExitProcess`.
///
///   sub  rsp, 0x28                     ; 48 83 EC 28
///   call user_main                     ; E8 disp32
///   mov  ecx, eax                      ; 89 C1
///   call qword ptr [rip + disp32]      ; FF 15 disp32
///   int3                               ; CC
///
/// Total: 18 bytes.  All disp32 fields are encoded against `shim_rva` so
/// the returned bytes can be appended verbatim into `.text` at that RVA.
std::vector<std::uint8_t> BuildUserMainExitShim(std::uint32_t shim_rva,
                                                std::uint32_t user_main_rva,
                                                std::uint32_t exit_process_iat_rva);

/// Produce a self-contained runnable PE32+ image whose only behaviour is to
/// write `message` to the standard output handle and then call
/// `kernel32!ExitProcess(0)`.  This is the first runtime-IO milestone of the
/// PolyglotCompiler PE writer: the image imports `GetStdHandle`, `WriteFile`
/// and `ExitProcess` from kernel32.dll, embeds `message` in a `.rdata`
/// section, and lays down a 68-byte AMD64 entry shim that drives the three
/// API calls in sequence.  `message` is written verbatim (no trailing NUL,
/// no encoding conversion); callers wanting a CRLF-terminated banner should
/// include the `"\r\n"` themselves.
///
/// Returns a `BuildResult` whose `image` is byte-for-byte runnable on
/// Windows AMD64.  Returns an empty `image` if `message` exceeds 4 GiB
/// (the WriteFile API's count argument is a 32-bit DWORD).
BuildResult BuildHelloWorldPE(const std::string &message);

/// Produce a self-contained runnable PE32+ image that issues one
/// `kernel32!WriteFile` call per entry of `call_messages` (in the supplied
/// order) against the standard-output handle, then terminates the process via
/// `kernel32!ExitProcess(0)`.  This is Stage B4 of the runtime-stdout
/// pipeline (demand `2026-04-28-49`): it accepts the byte sequences extracted
/// from `polyrt_println` IR call sites and lays them out as a real PE32+ that
/// drives stdout end-to-end.
///
/// Identical messages are deduplicated inside `.rdata` so a program that
/// PRINTLNs the same literal twice still pays for only one global, matching
/// the IR-layer interning contract.  An empty `call_messages` vector is
/// equivalent to `BuildExitZeroPE({})` — the image just exits with code 0.
///
/// Each message is bounded by 4 GiB (WriteFile's DWORD count); any oversized
/// entry causes the call to return an empty `BuildResult`.  Total `.rdata`
/// size is bounded only by the PE section-size DWORD field.
BuildResult BuildPrintlnSequencePE(const std::vector<std::string> &call_messages);

/// Generate the AMD64 byte sequence for the entry shim:
///   sub  rsp, 0x28                     ; 48 83 EC 28   (shadow space + align)
///   xor  ecx, ecx                      ; 31 C9         (arg0 = 0)
///   call qword ptr [rip + disp32]      ; FF 15 xx xx xx xx
///   int3                               ; CC            (unreachable)
/// `disp32` is computed as `iat_slot_rva - (shim_rva + 4 + 2 + 6)` where 4
/// is the length of `sub rsp, 0x28`, 2 is the length of `xor ecx, ecx`, and
/// 6 is the length of the entire `call` instruction.  Returns the 13-byte
/// sequence with the disp32 already encoded.
std::vector<std::uint8_t> BuildExitProcessShim(std::uint32_t shim_rva,
                                               std::uint32_t exit_process_iat_rva);

} // namespace polyglot::linker::pe
