/**
 * @file     target_triple.h
 * @brief    Strongly-typed LLVM-style target triple
 *           (`arch-vendor-os-env[-sub]`) with parser, host detection
 *           and round-tripping.
 *
 * @ingroup  Common / Core
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace polyglot::common {

enum class Arch {
  kUnknown,
  kX86,
  kX86_64,
  kArm,
  kAArch64,
  kRiscv32,
  kRiscv64,
  kWasm32,
  kWasm64,
};

enum class Vendor {
  kUnknown,
  kPc,
  kApple,
};

enum class OS {
  kUnknown,
  kLinux,
  kDarwin,
  kWindows,
  kFreeBSD,
  kWasi,
  kNone,        ///< Bare-metal / freestanding.
};

enum class Env {
  kUnknown,
  kGnu,
  kMsvc,
  kMusl,
  kAndroid,
  kMacAbi,
  kEabi,
};

enum class SubArch {
  kNone,
  kV7,
  kV8,
  kV9,
};

struct ParseError {
  std::string token;
  std::string message;
};

struct TargetTriple {
  Arch    arch{Arch::kUnknown};
  Vendor  vendor{Vendor::kUnknown};
  OS      os{OS::kUnknown};
  Env     env{Env::kUnknown};
  SubArch sub{SubArch::kNone};

  std::string str() const;
  bool operator==(const TargetTriple &o) const;
  bool operator!=(const TargetTriple &o) const { return !(*this == o); }
};

/// Parse an LLVM-style triple (`x86_64-pc-windows-msvc`,
/// `aarch64-apple-darwin`, `x86_64-unknown-linux-gnu`,
/// `wasm32-wasi`, ...).  Returns the parsed triple on success;
/// otherwise the `ParseError` describes the offending token.
/// Never throws and never aborts.
struct TripleParseResult {
  std::optional<TargetTriple> triple;
  std::optional<ParseError>   error;
  bool ok() const { return triple.has_value(); }
};
TripleParseResult ParseTargetTriple(const std::string &spec);

/// Host triple derived from the build-time platform / arch macros
/// (`_WIN32`, `__APPLE__`, `__linux__`, `__aarch64__`, `_M_X64`,
/// `__x86_64__`, `__wasm__`).
TargetTriple HostTriple();

const char *ArchName(Arch a);
const char *VendorName(Vendor v);
const char *OSName(OS o);
const char *EnvName(Env e);
const char *SubArchName(SubArch s);

}  // namespace polyglot::common

namespace std {
template <>
struct hash<polyglot::common::TargetTriple> {
  size_t operator()(const polyglot::common::TargetTriple &t) const noexcept {
    size_t h = 1469598103934665603ull;
    auto mix = [&](size_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    mix(static_cast<size_t>(t.arch));
    mix(static_cast<size_t>(t.vendor));
    mix(static_cast<size_t>(t.os));
    mix(static_cast<size_t>(t.env));
    mix(static_cast<size_t>(t.sub));
    return h;
  }
};
}  // namespace std
