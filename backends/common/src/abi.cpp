/**
 * @file     abi.cpp
 * @brief    Definitions for the relocation-kind mappers and string helpers
 *           declared in `backends/common/include/abi/relocation.h`.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/common/include/abi/relocation.h"

#include <cstring>

namespace polyglot::backends::common::abi {

// ----- ELF: x86-64 (System V psABI, /usr/include/elf.h R_X86_64_*) ---------

namespace {

constexpr std::uint32_t kElfX86_64_None      = 0;
constexpr std::uint32_t kElfX86_64_64        = 1;   // R_X86_64_64
constexpr std::uint32_t kElfX86_64_PC32      = 2;   // R_X86_64_PC32
constexpr std::uint32_t kElfX86_64_PLT32     = 4;   // R_X86_64_PLT32
constexpr std::uint32_t kElfX86_64_GOTPCREL  = 9;   // R_X86_64_GOTPCREL
constexpr std::uint32_t kElfX86_64_32        = 10;  // R_X86_64_32

}  // namespace

std::uint32_t MapToElfX86_64(RelocationKind kind) noexcept {
    switch (kind) {
        case RelocationKind::kAbs32:       return kElfX86_64_32;
        case RelocationKind::kAbs64:       return kElfX86_64_64;
        case RelocationKind::kPcRel32:     return kElfX86_64_PC32;
        case RelocationKind::kGotPcRel32:  return kElfX86_64_GOTPCREL;
        case RelocationKind::kPltPcRel32:  return kElfX86_64_PLT32;
        // Kinds without an x86-64 encoding (the AArch64 family of MOVW /
        // page-relative / branch fixups) intentionally fall through to the
        // sentinel below.  `kPcRel64` is also unsupported on x86-64 ELF.
        case RelocationKind::kPcRel64:
        case RelocationKind::kPage21:
        case RelocationKind::kPageOff12:
        case RelocationKind::kBranch26:
        case RelocationKind::kCondBranch19:
        case RelocationKind::kMovwG0Abs:
        case RelocationKind::kMovwG1Abs:
        case RelocationKind::kMovwG2Abs:
        case RelocationKind::kMovwG3Abs:
            return kUnsupportedRelocation;
    }
    return kUnsupportedRelocation;
}

// ----- ELF: AArch64 (ABI for the Arm 64-bit Architecture, ELF section) -----

namespace {

constexpr std::uint32_t kElfA64_Abs64           = 257;
constexpr std::uint32_t kElfA64_Abs32           = 258;
constexpr std::uint32_t kElfA64_Prel64          = 260;
constexpr std::uint32_t kElfA64_Prel32          = 261;
constexpr std::uint32_t kElfA64_MovwUabsG0      = 263;
constexpr std::uint32_t kElfA64_MovwUabsG1      = 265;
constexpr std::uint32_t kElfA64_MovwUabsG2      = 267;
constexpr std::uint32_t kElfA64_MovwUabsG3      = 269;
constexpr std::uint32_t kElfA64_AdrPrelPgHi21   = 275;
constexpr std::uint32_t kElfA64_AddAbsLo12Nc    = 277;
constexpr std::uint32_t kElfA64_CondBr19        = 280;
constexpr std::uint32_t kElfA64_Call26          = 283;
constexpr std::uint32_t kElfA64_AdrGotPage      = 311;  // ADR_GOT_PAGE

}  // namespace

std::uint32_t MapToElfAArch64(RelocationKind kind) noexcept {
    switch (kind) {
        case RelocationKind::kAbs32:       return kElfA64_Abs32;
        case RelocationKind::kAbs64:       return kElfA64_Abs64;
        case RelocationKind::kPcRel32:     return kElfA64_Prel32;
        case RelocationKind::kPcRel64:     return kElfA64_Prel64;
        case RelocationKind::kGotPcRel32:  return kElfA64_AdrGotPage;
        case RelocationKind::kPage21:      return kElfA64_AdrPrelPgHi21;
        case RelocationKind::kPageOff12:   return kElfA64_AddAbsLo12Nc;
        case RelocationKind::kBranch26:    return kElfA64_Call26;
        case RelocationKind::kCondBranch19: return kElfA64_CondBr19;
        case RelocationKind::kMovwG0Abs:   return kElfA64_MovwUabsG0;
        case RelocationKind::kMovwG1Abs:   return kElfA64_MovwUabsG1;
        case RelocationKind::kMovwG2Abs:   return kElfA64_MovwUabsG2;
        case RelocationKind::kMovwG3Abs:   return kElfA64_MovwUabsG3;
        // AArch64 ELF has no direct equivalent for x86-64 PLT-relative;
        // callers should resolve through `kBranch26` instead.
        case RelocationKind::kPltPcRel32:
            return kUnsupportedRelocation;
    }
    return kUnsupportedRelocation;
}

// ----- Mach-O: x86-64 (mach-o/x86_64/reloc.h X86_64_RELOC_*) ---------------

namespace {

constexpr std::uint32_t kMachOX86_64_Unsigned  = 0;
constexpr std::uint32_t kMachOX86_64_Signed    = 1;
constexpr std::uint32_t kMachOX86_64_Branch    = 2;
constexpr std::uint32_t kMachOX86_64_GotLoad   = 3;
constexpr std::uint32_t kMachOX86_64_Got       = 4;

}  // namespace

std::uint32_t MapToMachOX86_64(RelocationKind kind) noexcept {
    switch (kind) {
        case RelocationKind::kAbs32:
        case RelocationKind::kAbs64:       return kMachOX86_64_Unsigned;
        case RelocationKind::kPcRel32:     return kMachOX86_64_Branch;
        case RelocationKind::kPcRel64:     return kMachOX86_64_Signed;
        case RelocationKind::kGotPcRel32:  return kMachOX86_64_GotLoad;
        case RelocationKind::kPltPcRel32:  return kMachOX86_64_Got;
        case RelocationKind::kPage21:
        case RelocationKind::kPageOff12:
        case RelocationKind::kBranch26:
        case RelocationKind::kCondBranch19:
        case RelocationKind::kMovwG0Abs:
        case RelocationKind::kMovwG1Abs:
        case RelocationKind::kMovwG2Abs:
        case RelocationKind::kMovwG3Abs:
            return kUnsupportedRelocation;
    }
    return kUnsupportedRelocation;
}

// ----- Mach-O: ARM64 (mach-o/arm64/reloc.h ARM64_RELOC_*) ------------------

namespace {

constexpr std::uint32_t kMachOArm64_Unsigned         = 0;
constexpr std::uint32_t kMachOArm64_Branch26         = 2;
constexpr std::uint32_t kMachOArm64_Page21           = 3;
constexpr std::uint32_t kMachOArm64_PageOff12        = 4;
constexpr std::uint32_t kMachOArm64_GotLoadPage21    = 5;

}  // namespace

std::uint32_t MapToMachOArm64(RelocationKind kind) noexcept {
    switch (kind) {
        case RelocationKind::kAbs32:
        case RelocationKind::kAbs64:       return kMachOArm64_Unsigned;
        case RelocationKind::kBranch26:    return kMachOArm64_Branch26;
        case RelocationKind::kPage21:      return kMachOArm64_Page21;
        case RelocationKind::kPageOff12:   return kMachOArm64_PageOff12;
        case RelocationKind::kGotPcRel32:  return kMachOArm64_GotLoadPage21;
        case RelocationKind::kPcRel32:
        case RelocationKind::kPcRel64:
        case RelocationKind::kPltPcRel32:
        case RelocationKind::kCondBranch19:
        case RelocationKind::kMovwG0Abs:
        case RelocationKind::kMovwG1Abs:
        case RelocationKind::kMovwG2Abs:
        case RelocationKind::kMovwG3Abs:
            return kUnsupportedRelocation;
    }
    return kUnsupportedRelocation;
}

// ----- String helpers ------------------------------------------------------

const char* ToString(RelocationKind kind) noexcept {
    switch (kind) {
        case RelocationKind::kAbs32:        return "kAbs32";
        case RelocationKind::kAbs64:        return "kAbs64";
        case RelocationKind::kPcRel32:      return "kPcRel32";
        case RelocationKind::kPcRel64:      return "kPcRel64";
        case RelocationKind::kGotPcRel32:   return "kGotPcRel32";
        case RelocationKind::kPltPcRel32:   return "kPltPcRel32";
        case RelocationKind::kPage21:       return "kPage21";
        case RelocationKind::kPageOff12:    return "kPageOff12";
        case RelocationKind::kBranch26:     return "kBranch26";
        case RelocationKind::kCondBranch19: return "kCondBranch19";
        case RelocationKind::kMovwG0Abs:    return "kMovwG0Abs";
        case RelocationKind::kMovwG1Abs:    return "kMovwG1Abs";
        case RelocationKind::kMovwG2Abs:    return "kMovwG2Abs";
        case RelocationKind::kMovwG3Abs:    return "kMovwG3Abs";
    }
    return "<unknown>";
}

bool ParseRelocationKind(const std::string& token, RelocationKind* out) noexcept {
    if (out == nullptr) return false;
    struct Entry { const char* name; RelocationKind kind; };
    static constexpr Entry kTable[] = {
        {"kAbs32",        RelocationKind::kAbs32},
        {"kAbs64",        RelocationKind::kAbs64},
        {"kPcRel32",      RelocationKind::kPcRel32},
        {"kPcRel64",      RelocationKind::kPcRel64},
        {"kGotPcRel32",   RelocationKind::kGotPcRel32},
        {"kPltPcRel32",   RelocationKind::kPltPcRel32},
        {"kPage21",       RelocationKind::kPage21},
        {"kPageOff12",    RelocationKind::kPageOff12},
        {"kBranch26",     RelocationKind::kBranch26},
        {"kCondBranch19", RelocationKind::kCondBranch19},
        {"kMovwG0Abs",    RelocationKind::kMovwG0Abs},
        {"kMovwG1Abs",    RelocationKind::kMovwG1Abs},
        {"kMovwG2Abs",    RelocationKind::kMovwG2Abs},
        {"kMovwG3Abs",    RelocationKind::kMovwG3Abs},
    };
    for (const auto& e : kTable) {
        if (token == e.name) {
            *out = e.kind;
            return true;
        }
    }
    return false;
}

}  // namespace polyglot::backends::common::abi
