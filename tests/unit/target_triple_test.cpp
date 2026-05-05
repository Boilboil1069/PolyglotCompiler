/**
 * @file     target_triple_test.cpp
 * @brief    Triple parser, host detection and string round-trip
 *           tests for `polyglot::common::TargetTriple`.
 *
 * @ingroup  Common / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "common/include/binary_container.h"
#include "common/include/target_triple.h"

using namespace polyglot::common;

namespace {
struct Case {
  const char *spec;
  Arch arch;
  Vendor vendor;
  OS os;
  Env env;
};
}  // namespace

TEST_CASE("ParseTargetTriple covers the major triples",
          "[common][triple]") {
  Case table[] = {
      {"x86_64-pc-windows-msvc",   Arch::kX86_64,  Vendor::kPc,    OS::kWindows, Env::kMsvc},
      {"x86_64-pc-windows-gnu",    Arch::kX86_64,  Vendor::kPc,    OS::kWindows, Env::kGnu},
      {"aarch64-pc-windows-msvc",  Arch::kAArch64, Vendor::kPc,    OS::kWindows, Env::kMsvc},
      {"i386-pc-windows-msvc",     Arch::kX86,     Vendor::kPc,    OS::kWindows, Env::kMsvc},
      {"x86_64-apple-darwin",      Arch::kX86_64,  Vendor::kApple, OS::kDarwin,  Env::kUnknown},
      {"aarch64-apple-darwin",     Arch::kAArch64, Vendor::kApple, OS::kDarwin,  Env::kUnknown},
      {"arm64-apple-macos",        Arch::kAArch64, Vendor::kApple, OS::kDarwin,  Env::kUnknown},
      {"x86_64-unknown-linux-gnu", Arch::kX86_64,  Vendor::kUnknown, OS::kLinux, Env::kGnu},
      {"x86_64-unknown-linux-musl",Arch::kX86_64,  Vendor::kUnknown, OS::kLinux, Env::kMusl},
      {"aarch64-unknown-linux-gnu",Arch::kAArch64, Vendor::kUnknown, OS::kLinux, Env::kGnu},
      {"aarch64-linux-android",    Arch::kAArch64, Vendor::kUnknown, OS::kLinux, Env::kAndroid},
      {"riscv64-unknown-linux-gnu",Arch::kRiscv64, Vendor::kUnknown, OS::kLinux, Env::kGnu},
      {"riscv32-unknown-none-elf", Arch::kRiscv32, Vendor::kUnknown, OS::kNone,  Env::kEabi},
      {"armv7-unknown-linux-gnueabihf", Arch::kArm, Vendor::kUnknown, OS::kLinux, Env::kGnu},
      {"wasm32-wasi",              Arch::kWasm32,  Vendor::kUnknown, OS::kWasi,  Env::kUnknown},
      {"wasm32-unknown-unknown",   Arch::kWasm32,  Vendor::kUnknown, OS::kNone, Env::kUnknown},
      {"x86_64-unknown-freebsd",   Arch::kX86_64,  Vendor::kUnknown, OS::kFreeBSD, Env::kUnknown},
  };
  for (const auto &c : table) {
    auto r = ParseTargetTriple(c.spec);
    INFO("triple: " << c.spec);
    REQUIRE(r.ok());
    CHECK(r.triple->arch == c.arch);
    CHECK(r.triple->vendor == c.vendor);
    CHECK(r.triple->os == c.os);
    CHECK(r.triple->env == c.env);
  }
}

TEST_CASE("Triple aliases collapse to canonical arch",
          "[common][triple][alias]") {
  CHECK(ParseTargetTriple("amd64-pc-windows-msvc").triple->arch ==
        Arch::kX86_64);
  CHECK(ParseTargetTriple("x64-pc-windows-msvc").triple->arch ==
        Arch::kX86_64);
  CHECK(ParseTargetTriple("arm64-apple-darwin").triple->arch ==
        Arch::kAArch64);
}

TEST_CASE("ParseTargetTriple reports structured errors",
          "[common][triple][error]") {
  auto a = ParseTargetTriple("");
  REQUIRE_FALSE(a.ok());
  CHECK(a.error->message == "empty triple");

  auto b = ParseTargetTriple("foo");
  REQUIRE_FALSE(b.ok());

  auto c = ParseTargetTriple("nonsense-pc-windows-msvc");
  REQUIRE_FALSE(c.ok());
  CHECK(c.error->token == "nonsense");

  auto d = ParseTargetTriple("x86_64-pc-narnia");
  REQUIRE_FALSE(d.ok());
  CHECK(d.error->token == "narnia");

  auto e = ParseTargetTriple("x86_64-pc-windows-msvc-extra-noise");
  REQUIRE_FALSE(e.ok());
}

TEST_CASE("Triple round-trips through str()", "[common][triple]") {
  for (const char *spec : {
           "x86_64-pc-windows-msvc",
           "aarch64-apple-darwin",
           "x86_64-unknown-linux-gnu",
           "wasm32-wasi",
       }) {
    auto t = ParseTargetTriple(spec);
    REQUIRE(t.ok());
    auto s = t.triple->str();
    auto round = ParseTargetTriple(s);
    REQUIRE(round.ok());
    CHECK(*round.triple == *t.triple);
  }
}

TEST_CASE("HostTriple yields a non-trivial value",
          "[common][triple][host]") {
  auto h = HostTriple();
  CHECK(h.arch != Arch::kUnknown);
  CHECK(h.os != OS::kUnknown);
}

TEST_CASE("TargetTriple is hashable + equality-comparable",
          "[common][triple][hash]") {
  std::unordered_set<TargetTriple> set;
  set.insert(*ParseTargetTriple("x86_64-pc-windows-msvc").triple);
  set.insert(*ParseTargetTriple("x86_64-pc-windows-msvc").triple);
  CHECK(set.size() == 1);
  set.insert(*ParseTargetTriple("aarch64-apple-darwin").triple);
  CHECK(set.size() == 2);
}

TEST_CASE("ContainerForOS / DefaultOSForContainer round-trip",
          "[common][container]") {
  CHECK(ContainerForOS(OS::kWindows) == BinaryContainer::kPE);
  CHECK(ContainerForOS(OS::kDarwin)  == BinaryContainer::kMachO);
  CHECK(ContainerForOS(OS::kLinux)   == BinaryContainer::kELF);
  CHECK(ContainerForOS(OS::kFreeBSD) == BinaryContainer::kELF);
  CHECK(ContainerForOS(OS::kWasi)    == BinaryContainer::kWasm);
  CHECK(ContainerForOS(OS::kNone)    == BinaryContainer::kELF);

  CHECK(DefaultOSForContainer(BinaryContainer::kPE)    == OS::kWindows);
  CHECK(DefaultOSForContainer(BinaryContainer::kMachO) == OS::kDarwin);
  CHECK(DefaultOSForContainer(BinaryContainer::kELF)   == OS::kLinux);
  CHECK(DefaultOSForContainer(BinaryContainer::kWasm)  == OS::kWasi);
}

TEST_CASE("ResolveContainer dispatches via triple + override",
          "[common][container][dispatch]") {
  auto win = *ParseTargetTriple("x86_64-pc-windows-msvc").triple;
  auto mac = *ParseTargetTriple("aarch64-apple-darwin").triple;
  auto lin = *ParseTargetTriple("x86_64-unknown-linux-gnu").triple;
  auto was = *ParseTargetTriple("wasm32-wasi").triple;

  CHECK(ResolveContainer(win, BinaryContainer::kAuto) == BinaryContainer::kPE);
  CHECK(ResolveContainer(mac, BinaryContainer::kAuto) == BinaryContainer::kMachO);
  CHECK(ResolveContainer(lin, BinaryContainer::kAuto) == BinaryContainer::kELF);
  CHECK(ResolveContainer(was, BinaryContainer::kAuto) == BinaryContainer::kWasm);

  // Explicit request always wins.
  CHECK(ResolveContainer(win, BinaryContainer::kELF) == BinaryContainer::kELF);
  CHECK(ResolveContainer(lin, BinaryContainer::kPE)  == BinaryContainer::kPE);
}
