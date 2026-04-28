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
};

/// Build a complete PE32+ image from a code buffer and an import description.
/// On any malformed input (e.g. entry offset out of range) returns a
/// `BuildResult` with `image.empty() == true`.
BuildResult BuildPE32PlusImage(const BuildRequest &req);

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
