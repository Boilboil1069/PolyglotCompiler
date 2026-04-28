/**
 * @file     abi_relocation_test.cpp
 * @brief    Unit tests for the target-independent `RelocationKind` and the
 *           four per-format mapper functions in `common::abi`.
 *
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <catch2/catch_test_macros.hpp>

#include "backends/common/include/abi/relocation.h"

namespace abi = polyglot::backends::common::abi;
using abi::RelocationKind;

TEST_CASE("MapToElfX86_64 returns the canonical R_X86_64_* codes",
          "[backends][abi][relocation]") {
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kAbs64)      == 1u);   // R_X86_64_64
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kPcRel32)    == 2u);   // R_X86_64_PC32
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kPltPcRel32) == 4u);   // R_X86_64_PLT32
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kGotPcRel32) == 9u);   // R_X86_64_GOTPCREL
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kAbs32)      == 10u);  // R_X86_64_32
}

TEST_CASE("MapToElfAArch64 returns the canonical R_AARCH64_* codes",
          "[backends][abi][relocation]") {
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kAbs64)         == 257u); // ABS64
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kPage21)        == 275u); // ADR_PREL_PG_HI21
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kPageOff12)     == 277u); // ADD_ABS_LO12_NC
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kCondBranch19)  == 280u); // CONDBR19
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kBranch26)      == 283u); // CALL26
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kMovwG0Abs)     == 263u); // MOVW_UABS_G0
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kMovwG3Abs)     == 269u); // MOVW_UABS_G3
}

TEST_CASE("MapToMachOX86_64 maps the recognised x86_64 Mach-O relocations",
          "[backends][abi][relocation]") {
    REQUIRE(abi::MapToMachOX86_64(RelocationKind::kPcRel32)    == 2u); // X86_64_RELOC_BRANCH
    REQUIRE(abi::MapToMachOX86_64(RelocationKind::kAbs64)      == 0u); // X86_64_RELOC_UNSIGNED
    REQUIRE(abi::MapToMachOX86_64(RelocationKind::kGotPcRel32) == 3u); // X86_64_RELOC_GOT_LOAD
    REQUIRE(abi::MapToMachOX86_64(RelocationKind::kPltPcRel32) == 4u); // X86_64_RELOC_GOT
}

TEST_CASE("MapToMachOArm64 maps the recognised arm64 Mach-O relocations",
          "[backends][abi][relocation]") {
    REQUIRE(abi::MapToMachOArm64(RelocationKind::kAbs64)      == 0u); // ARM64_RELOC_UNSIGNED
    REQUIRE(abi::MapToMachOArm64(RelocationKind::kBranch26)   == 2u); // ARM64_RELOC_BRANCH26
    REQUIRE(abi::MapToMachOArm64(RelocationKind::kPage21)     == 3u); // ARM64_RELOC_PAGE21
    REQUIRE(abi::MapToMachOArm64(RelocationKind::kPageOff12)  == 4u); // ARM64_RELOC_PAGEOFF12
}

TEST_CASE("Mappers return the unsupported sentinel for non-representable combinations",
          "[backends][abi][relocation]") {
    // x86-64 ELF cannot encode AArch64-only kinds.
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kBranch26)    == abi::kUnsupportedRelocation);
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kPage21)      == abi::kUnsupportedRelocation);
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kMovwG2Abs)   == abi::kUnsupportedRelocation);
    REQUIRE(abi::MapToElfX86_64(RelocationKind::kPcRel64)     == abi::kUnsupportedRelocation);
    // AArch64 ELF has no x86 PLT-relative.
    REQUIRE(abi::MapToElfAArch64(RelocationKind::kPltPcRel32) == abi::kUnsupportedRelocation);
    // Mach-O x86_64 cannot encode AArch64-only kinds.
    REQUIRE(abi::MapToMachOX86_64(RelocationKind::kPage21)    == abi::kUnsupportedRelocation);
    // Mach-O arm64 cannot encode x86-only PC-relative 32.
    REQUIRE(abi::MapToMachOArm64(RelocationKind::kPcRel32)    == abi::kUnsupportedRelocation);
}

TEST_CASE("ToString and ParseRelocationKind round-trip every enum value",
          "[backends][abi][relocation]") {
    constexpr RelocationKind kAll[] = {
        RelocationKind::kAbs32,        RelocationKind::kAbs64,
        RelocationKind::kPcRel32,      RelocationKind::kPcRel64,
        RelocationKind::kGotPcRel32,   RelocationKind::kPltPcRel32,
        RelocationKind::kPage21,       RelocationKind::kPageOff12,
        RelocationKind::kBranch26,     RelocationKind::kCondBranch19,
        RelocationKind::kMovwG0Abs,    RelocationKind::kMovwG1Abs,
        RelocationKind::kMovwG2Abs,    RelocationKind::kMovwG3Abs,
    };
    for (RelocationKind k : kAll) {
        const char* name = abi::ToString(k);
        REQUIRE(name != nullptr);
        RelocationKind parsed{};
        REQUIRE(abi::ParseRelocationKind(name, &parsed));
        REQUIRE(parsed == k);
    }
    RelocationKind dummy{};
    REQUIRE_FALSE(abi::ParseRelocationKind("kNotAKind", &dummy));
    REQUIRE_FALSE(abi::ParseRelocationKind("kBranch26", nullptr));
}

TEST_CASE("AbiDescriptor equality compares all four fields",
          "[backends][abi][descriptor]") {
    abi::AbiDescriptor a{"SysV", 8, 16, 128};
    abi::AbiDescriptor b{"SysV", 8, 16, 128};
    abi::AbiDescriptor c{"SysV", 8, 16, 0};
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE_FALSE(a == c);
}
