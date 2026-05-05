/**
 * @file     target_triple.cpp
 * @brief    Target triple parser, host detection and string output.
 *
 * @ingroup  Common / Core
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "common/include/target_triple.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace polyglot::common {

namespace {

std::string Lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> Split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(std::move(cur)); cur.clear(); }
    else { cur.push_back(c); }
  }
  out.push_back(std::move(cur));
  return out;
}

std::optional<Arch> ParseArch(const std::string &t, SubArch &sub) {
  sub = SubArch::kNone;
  if (t == "x86_64" || t == "amd64" || t == "x64") return Arch::kX86_64;
  if (t == "i386" || t == "i486" || t == "i586" || t == "i686" ||
      t == "x86")
    return Arch::kX86;
  if (t == "aarch64" || t == "arm64") { sub = SubArch::kV8; return Arch::kAArch64; }
  if (t == "armv7" || t == "armv7a" || t == "armv7-a") {
    sub = SubArch::kV7;
    return Arch::kArm;
  }
  if (t == "arm") return Arch::kArm;
  if (t == "riscv64") return Arch::kRiscv64;
  if (t == "riscv32") return Arch::kRiscv32;
  if (t == "wasm32") return Arch::kWasm32;
  if (t == "wasm64") return Arch::kWasm64;
  return std::nullopt;
}

std::optional<Vendor> ParseVendor(const std::string &t) {
  if (t == "unknown") return Vendor::kUnknown;
  if (t == "pc")      return Vendor::kPc;
  if (t == "apple")   return Vendor::kApple;
  return std::nullopt;
}

std::optional<OS> ParseOS(const std::string &t) {
  if (t == "linux")             return OS::kLinux;
  if (t == "darwin" ||
      t == "macos" || t == "macosx" || t == "ios" || t == "tvos" ||
      t == "watchos")
    return OS::kDarwin;
  if (t == "windows" || t == "win32" || t == "mingw32" || t == "cygwin")
    return OS::kWindows;
  if (t == "freebsd")  return OS::kFreeBSD;
  if (t == "wasi")     return OS::kWasi;
  if (t == "none" || t == "elf" || t == "unknown" || t.empty())
    return OS::kNone;
  return std::nullopt;
}

std::optional<Env> ParseEnv(const std::string &t) {
  if (t.empty())             return Env::kUnknown;
  if (t == "gnu" || t == "gnueabi" || t == "gnueabihf")
    return Env::kGnu;
  if (t == "msvc")           return Env::kMsvc;
  if (t == "musl" || t == "musleabi" || t == "musleabihf")
    return Env::kMusl;
  if (t == "android")        return Env::kAndroid;
  if (t == "macabi")         return Env::kMacAbi;
  if (t == "eabi" || t == "eabihf" || t == "elf")
    return Env::kEabi;
  return std::nullopt;
}

}  // namespace

TripleParseResult ParseTargetTriple(const std::string &spec) {
  TripleParseResult out;
  if (spec.empty()) {
    out.error = ParseError{"", "empty triple"};
    return out;
  }
  auto parts = Split(Lower(spec), '-');
  if (parts.size() < 2 || parts.size() > 5) {
    out.error = ParseError{spec, "expected 2..5 hyphen-separated tokens"};
    return out;
  }

  TargetTriple t;
  auto arch = ParseArch(parts[0], t.sub);
  if (!arch) {
    out.error = ParseError{parts[0], "unknown architecture"};
    return out;
  }
  t.arch = *arch;

  // Possible layouts:
  //   arch-os                       (2)
  //   arch-vendor-os                (3)
  //   arch-vendor-os-env            (4)
  //   arch-os-env                   (3, vendor omitted: e.g. wasm32-wasi)
  size_t i = 1;

  std::optional<Vendor> v;
  if (i < parts.size()) v = ParseVendor(parts[i]);
  if (v) { t.vendor = *v; ++i; }

  if (i >= parts.size()) {
    out.error = ParseError{spec, "missing OS token"};
    return out;
  }
  auto os = ParseOS(parts[i]);
  if (!os) {
    out.error = ParseError{parts[i], "unknown OS"};
    return out;
  }
  t.os = *os;
  ++i;

  if (i < parts.size()) {
    auto e = ParseEnv(parts[i]);
    if (!e) {
      out.error = ParseError{parts[i], "unknown environment"};
      return out;
    }
    t.env = *e;
    ++i;
  }

  if (i < parts.size()) {
    out.error = ParseError{parts[i], "trailing token"};
    return out;
  }

  // wasm32-wasi has no vendor; default vendor stays kUnknown.
  out.triple = t;
  return out;
}

const char *ArchName(Arch a) {
  switch (a) {
    case Arch::kX86_64:  return "x86_64";
    case Arch::kX86:     return "i386";
    case Arch::kAArch64: return "aarch64";
    case Arch::kArm:     return "arm";
    case Arch::kRiscv64: return "riscv64";
    case Arch::kRiscv32: return "riscv32";
    case Arch::kWasm32:  return "wasm32";
    case Arch::kWasm64:  return "wasm64";
    case Arch::kUnknown: return "unknown";
  }
  return "unknown";
}

const char *VendorName(Vendor v) {
  switch (v) {
    case Vendor::kPc:     return "pc";
    case Vendor::kApple:  return "apple";
    case Vendor::kUnknown: return "unknown";
  }
  return "unknown";
}

const char *OSName(OS o) {
  switch (o) {
    case OS::kLinux:   return "linux";
    case OS::kDarwin:  return "darwin";
    case OS::kWindows: return "windows";
    case OS::kFreeBSD: return "freebsd";
    case OS::kWasi:    return "wasi";
    case OS::kNone:    return "none";
    case OS::kUnknown: return "unknown";
  }
  return "unknown";
}

const char *EnvName(Env e) {
  switch (e) {
    case Env::kGnu:     return "gnu";
    case Env::kMsvc:    return "msvc";
    case Env::kMusl:    return "musl";
    case Env::kAndroid: return "android";
    case Env::kMacAbi:  return "macabi";
    case Env::kEabi:    return "eabi";
    case Env::kUnknown: return "";
  }
  return "";
}

const char *SubArchName(SubArch s) {
  switch (s) {
    case SubArch::kV7: return "v7";
    case SubArch::kV8: return "v8";
    case SubArch::kV9: return "v9";
    case SubArch::kNone: return "";
  }
  return "";
}

std::string TargetTriple::str() const {
  std::string out = ArchName(arch);
  // wasm short form: arch-os[-env].
  if (arch == Arch::kWasm32 || arch == Arch::kWasm64) {
    out += '-';
    out += OSName(os);
    if (env != Env::kUnknown) { out += '-'; out += EnvName(env); }
    return out;
  }
  out += '-';
  out += VendorName(vendor);
  out += '-';
  out += OSName(os);
  if (env != Env::kUnknown) {
    out += '-';
    out += EnvName(env);
  }
  return out;
}

bool TargetTriple::operator==(const TargetTriple &o) const {
  return arch == o.arch && vendor == o.vendor && os == o.os &&
         env == o.env && sub == o.sub;
}

TargetTriple HostTriple() {
  TargetTriple t;
#if defined(__x86_64__) || defined(_M_X64)
  t.arch = Arch::kX86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
  t.arch = Arch::kAArch64;
  t.sub = SubArch::kV8;
#elif defined(__i386__) || defined(_M_IX86)
  t.arch = Arch::kX86;
#elif defined(__arm__) || defined(_M_ARM)
  t.arch = Arch::kArm;
#elif defined(__wasm__)
  t.arch = Arch::kWasm32;
#else
  t.arch = Arch::kUnknown;
#endif

#if defined(_WIN32)
  t.vendor = Vendor::kPc;
  t.os = OS::kWindows;
#  if defined(_MSC_VER)
  t.env = Env::kMsvc;
#  else
  t.env = Env::kGnu;
#  endif
#elif defined(__APPLE__)
  t.vendor = Vendor::kApple;
  t.os = OS::kDarwin;
#elif defined(__linux__)
  t.vendor = Vendor::kPc;
  t.os = OS::kLinux;
  t.env = Env::kGnu;
#elif defined(__FreeBSD__)
  t.vendor = Vendor::kPc;
  t.os = OS::kFreeBSD;
#elif defined(__wasi__)
  t.os = OS::kWasi;
#else
  t.vendor = Vendor::kUnknown;
  t.os = OS::kUnknown;
#endif
  return t;
}

}  // namespace polyglot::common
