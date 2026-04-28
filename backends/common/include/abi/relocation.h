/**
 * @file     relocation.h
 * @brief    Target-independent relocation kinds and per-format mappers.
 *
 * `RelocationKind` is the union of every relocation semantic that any current
 * polyglot backend (x86-64 SysV ELF / Mach-O, AArch64 ELF / Mach-O) emits.
 * The four `MapTo*` helpers translate a `RelocationKind` into the integer
 * code expected by the matching object-file format; combinations that are
 * not representable in a given format return the sentinel `kUnsupported`
 * value (0xFFFFFFFFu) so callers may emit a diagnostic.
 *
 * `RelocationEntry` mirrors the field layout of `polyglot::backends::
 * MCRelocation` (declared in `target_backend.h`) so backends can switch
 * between the MC-side and ABI-side type without copying.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <cstdint>
#include <string>

namespace polyglot::backends::common::abi {

/// @brief Sentinel returned by `MapTo*` for relocation kinds that are not
///        representable in the requested object-file format.
inline constexpr std::uint32_t kUnsupportedRelocation = 0xFFFFFFFFu;

/// @brief Target-independent relocation semantics.
///
/// The list deliberately covers both x86-64 and AArch64 ELF / Mach-O needs;
/// future architectures (RISC-V, WASM custom-section relocations) may extend
/// the enum, but existing values must keep their current meaning so that
/// downstream `MapTo*` tables remain stable.
enum class RelocationKind : std::uint32_t {
    kAbs32 = 0,        ///< 32-bit absolute address.
    kAbs64,            ///< 64-bit absolute address.
    kPcRel32,          ///< 32-bit PC-relative reference.
    kPcRel64,          ///< 64-bit PC-relative reference.
    kGotPcRel32,       ///< 32-bit PC-relative GOT entry reference.
    kPltPcRel32,       ///< 32-bit PC-relative PLT trampoline reference.
    kPage21,           ///< AArch64 ADRP page-relative high 21 bits.
    kPageOff12,        ///< AArch64 page-offset low 12 bits (ADD #lo12).
    kBranch26,         ///< AArch64 unconditional branch / call (26-bit imm).
    kCondBranch19,     ///< AArch64 conditional branch (19-bit imm).
    kMovwG0Abs,        ///< AArch64 `MOVZ` absolute bits  0..15.
    kMovwG1Abs,        ///< AArch64 `MOVZ` absolute bits 16..31.
    kMovwG2Abs,        ///< AArch64 `MOVZ` absolute bits 32..47.
    kMovwG3Abs,        ///< AArch64 `MOVZ` absolute bits 48..63.
};

/// @brief One relocation record. Field layout intentionally mirrors
///        `polyglot::backends::MCRelocation` so MC-side and ABI-side code
///        can share data without translation.
struct RelocationEntry {
    std::string    section;   ///< Section that owns the relocation site.
    std::uint64_t  offset{0}; ///< Byte offset within `section`.
    RelocationKind kind{RelocationKind::kAbs64};
    std::string    symbol;    ///< Symbol referenced by the relocation.
    std::int64_t   addend{0}; ///< Addend (RELA-style; 0 when unused).
};

/// @brief Compact ABI summary used by tooling and the front-end of the
///        backend registry.  Keeps the fields of the legacy
///        `polyglot::backends::ABI` struct so existing code that consumes
///        `backends::ABI` continues to compile via the forwarding alias in
///        `backends/common/include/abi.h`.
struct AbiDescriptor {
    std::string name;                ///< Human-readable ABI name (e.g. "SysV").
    std::size_t pointer_size{8};     ///< Pointer / GPR width in bytes.
    std::size_t stack_alignment{16}; ///< Required SP alignment in bytes.
    std::size_t red_zone_size{0};    ///< Red-zone size in bytes (0 if none).

    friend bool operator==(const AbiDescriptor& a, const AbiDescriptor& b) noexcept {
        return a.name == b.name && a.pointer_size == b.pointer_size &&
               a.stack_alignment == b.stack_alignment && a.red_zone_size == b.red_zone_size;
    }
    friend bool operator!=(const AbiDescriptor& a, const AbiDescriptor& b) noexcept {
        return !(a == b);
    }
};

/// @brief Map a `RelocationKind` to the matching ELF x86-64 `R_X86_64_*` code.
///        Returns `kUnsupportedRelocation` when the kind is not expressible
///        in the SysV ELF x86-64 relocation table.
std::uint32_t MapToElfX86_64(RelocationKind kind) noexcept;

/// @brief Map a `RelocationKind` to the matching ELF AArch64 `R_AARCH64_*`
///        code.  Returns `kUnsupportedRelocation` for kinds not expressible
///        in the AArch64 ELF relocation table.
std::uint32_t MapToElfAArch64(RelocationKind kind) noexcept;

/// @brief Map a `RelocationKind` to the matching Mach-O `X86_64_RELOC_*`
///        code.  Returns `kUnsupportedRelocation` when no Mach-O encoding
///        exists for the kind.
std::uint32_t MapToMachOX86_64(RelocationKind kind) noexcept;

/// @brief Map a `RelocationKind` to the matching Mach-O `ARM64_RELOC_*`
///        code.  Returns `kUnsupportedRelocation` when no Mach-O encoding
///        exists for the kind.
std::uint32_t MapToMachOArm64(RelocationKind kind) noexcept;

/// @brief Render a `RelocationKind` as the `kFoo` token used in diagnostics.
const char* ToString(RelocationKind kind) noexcept;

/// @brief Parse the textual form produced by `ToString`.  Returns `false`
///        and leaves `*out` untouched when the token is unknown.
bool ParseRelocationKind(const std::string& token, RelocationKind* out) noexcept;

}  // namespace polyglot::backends::common::abi
