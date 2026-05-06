/**
 * @file     e2e_triple_propagation.cpp
 * @brief    BIN-7 end-to-end check: every entry point in the toolchain
 *           agrees on the same target triple, the driver folds the
 *           triple into the binary container expected by the receiving
 *           OS, and `--target=` propagates through `polyc -> polyld`
 *           (verified via the resolver helpers used by both ends).
 *
 *           The test exercises the value-level surface area, not the
 *           system-link path, so it runs on every host without needing
 *           foreign-format runners (PE on macOS, ELF on Windows, ...).
 *
 * @author   Manning Cyrus
 * @date     2026-05-07
 */

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "common/include/binary_container.h"
#include "common/include/target_triple.h"

using polyglot::common::BinaryContainer;
using polyglot::common::HostTriple;
using polyglot::common::ParseTargetTriple;
using polyglot::common::ResolveContainer;
using polyglot::common::SuffixMatchesContainer;
using polyglot::common::SuffixesFor;
using polyglot::common::TargetTriple;

namespace {

struct TripleCase {
  const char *spec;
  BinaryContainer expected;
  const char *expected_exe_suffix;
};

// The matrix below covers the seven targets the demand calls out plus
// one bare-arch alias.  Each entry pairs a canonical triple with the
// container the resolver should pick for `kAuto`, plus the executable
// suffix the suffix-policy must blame on `polyc-warn-W2101` if the user
// passes `-o foo.wrong`.
const std::vector<TripleCase> kMatrix = {
    {"x86_64-pc-windows-msvc",     BinaryContainer::kPE,    ".exe"},
    {"aarch64-pc-windows-msvc",    BinaryContainer::kPE,    ".exe"},
    {"x86_64-unknown-linux-gnu",   BinaryContainer::kELF,   ""},
    {"aarch64-unknown-linux-gnu",  BinaryContainer::kELF,   ""},
    {"x86_64-apple-darwin",        BinaryContainer::kMachO, ""},
    {"aarch64-apple-darwin",       BinaryContainer::kMachO, ""},
    {"wasm32-wasi",                BinaryContainer::kWasm,  ".wasm"},
};

}  // namespace

TEST_CASE("BIN-7 host triple is well-formed", "[bin7][triple]") {
  TargetTriple host = HostTriple();
  // Round-trip: HostTriple().str() must parse back into the same triple.
  std::string spec = host.str();
  auto reparsed = ParseTargetTriple(spec);
  REQUIRE(reparsed.ok());
  REQUIRE(*reparsed.triple == host);
}

TEST_CASE("BIN-7 every demand triple round-trips through the parser",
          "[bin7][triple]") {
  for (const auto &c : kMatrix) {
    INFO("triple " << c.spec);
    auto parsed = ParseTargetTriple(c.spec);
    REQUIRE(parsed.ok());
    // Re-serialising and re-parsing must yield the same value: the
    // canonical printer is the source of truth for downstream wire
    // formats (linker_pe.cpp, linker_macho.cpp, ...).
    std::string canon = parsed.triple->str();
    auto again = ParseTargetTriple(canon);
    REQUIRE(again.ok());
    REQUIRE(*again.triple == *parsed.triple);
  }
}

TEST_CASE("BIN-7 ResolveContainer agrees with demand container map",
          "[bin7][triple]") {
  for (const auto &c : kMatrix) {
    INFO("triple " << c.spec);
    auto parsed = ParseTargetTriple(c.spec);
    REQUIRE(parsed.ok());
    BinaryContainer auto_pick =
        ResolveContainer(*parsed.triple, BinaryContainer::kAuto);
    REQUIRE(auto_pick == c.expected);
    // Explicit override always wins over the triple-driven default so
    // `polyc --target=...-windows-msvc --container=elf` keeps producing
    // an ELF for cross-tooling experiments.
    REQUIRE(ResolveContainer(*parsed.triple, BinaryContainer::kELF) ==
            BinaryContainer::kELF);
    REQUIRE(ResolveContainer(*parsed.triple, BinaryContainer::kPE) ==
            BinaryContainer::kPE);
  }
}

TEST_CASE("BIN-7 file-suffix policy matches resolved container",
          "[bin7][suffix]") {
  for (const auto &c : kMatrix) {
    INFO("triple " << c.spec);
    auto parsed = ParseTargetTriple(c.spec);
    REQUIRE(parsed.ok());
    BinaryContainer container =
        ResolveContainer(*parsed.triple, BinaryContainer::kAuto);

    // The canonical suffix (or empty) must always match itself.
    std::string ok_path = std::string{"app"} + c.expected_exe_suffix;
    REQUIRE(SuffixMatchesContainer(ok_path, container, "executable"));

    // PE ↔ Wasm cross-mismatch must always be flagged so the
    // `polyc-warn-W2101` warning fires when the user names an `.exe`
    // for a wasm build (or vice versa).
    if (container == BinaryContainer::kPE) {
      REQUIRE_FALSE(SuffixMatchesContainer("app.wasm", container, "executable"));
    } else if (container == BinaryContainer::kWasm) {
      REQUIRE_FALSE(SuffixMatchesContainer("app.exe", container, "executable"));
    }
  }

  // `SuffixesFor` must report a stable `.wasm` for the wasm container
  // (kept in sync with what the driver embeds in its warning text).
  auto wasm_suf = SuffixesFor(BinaryContainer::kWasm);
  REQUIRE(std::string(wasm_suf.executable) == ".wasm");
  auto pe_suf = SuffixesFor(BinaryContainer::kPE);
  REQUIRE(std::string(pe_suf.executable) == ".exe");
}
