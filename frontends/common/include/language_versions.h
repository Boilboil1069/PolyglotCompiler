/**
 * @file     language_versions.h
 * @brief    Per-language version / dialect enumerations and helpers
 *
 * Multi-language version management infrastructure.
 *
 * Each supported source language gets a strongly-typed enum describing the
 * dialect / language standard / runtime version that the corresponding
 * frontend must honour.  Every enum reserves the `kAuto` member which means
 * "let the frontend infer the version from source pragmas / project files /
 * tool-chain probing / conservative default" (see documentation in
 * `docs/realization/language_versions.md` for the precise inference order).
 *
 * The helpers in this file (`Parse*Version` / `*VersionToString` /
 * `*VersionAtLeast`) are deliberately kept header-only so that they can be
 * used from CLIs (`polyc`, `polyver`), GUI components and tests without
 * forcing every consumer to link against the frontend libraries.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace polyglot::frontends {

// ===========================================================================
// C++ dialect
// ===========================================================================

/**
 * @brief C++ dialect / language standard.
 *
 * The numeric ordering is monotonic with the publication year so that
 * `static_cast<int>(a) < static_cast<int>(b)` is equivalent to
 * "dialect a is older than dialect b".
 */
enum class CppDialect {
  kAuto = 0,
  kCpp98,
  kCpp03,
  kCpp11,
  kCpp14,
  kCpp17,
  kCpp20,
  kCpp23,
  kCpp26,
};

/** @brief Conservative fallback when no inference source agrees. */
constexpr CppDialect kCppDialectDefault = CppDialect::kCpp20;

/**
 * @brief Parse a textual dialect name.
 *
 * Accepts the long form (`c++17`, `c++20`, ...) as well as the short form
 * (`17`, `20`, ...).  Case insensitive.  Returns `std::nullopt` for
 * unrecognized input so that the caller can emit a precise diagnostic.
 */
inline std::optional<CppDialect> ParseCppDialect(std::string_view text) {
  std::string s;
  s.reserve(text.size());
  for (char c : text)
    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (s == "auto" || s.empty())
    return CppDialect::kAuto;
  // Strip optional "c++" / "cpp" prefix.
  if (s.rfind("c++", 0) == 0)
    s.erase(0, 3);
  else if (s.rfind("cpp", 0) == 0)
    s.erase(0, 3);
  if (s == "98")
    return CppDialect::kCpp98;
  if (s == "03")
    return CppDialect::kCpp03;
  if (s == "11" || s == "0x")
    return CppDialect::kCpp11;
  if (s == "14" || s == "1y")
    return CppDialect::kCpp14;
  if (s == "17" || s == "1z")
    return CppDialect::kCpp17;
  if (s == "20" || s == "2a")
    return CppDialect::kCpp20;
  if (s == "23" || s == "2b")
    return CppDialect::kCpp23;
  if (s == "26" || s == "2c")
    return CppDialect::kCpp26;
  return std::nullopt;
}

inline const char *CppDialectToString(CppDialect d) {
  switch (d) {
  case CppDialect::kAuto:  return "auto";
  case CppDialect::kCpp98: return "c++98";
  case CppDialect::kCpp03: return "c++03";
  case CppDialect::kCpp11: return "c++11";
  case CppDialect::kCpp14: return "c++14";
  case CppDialect::kCpp17: return "c++17";
  case CppDialect::kCpp20: return "c++20";
  case CppDialect::kCpp23: return "c++23";
  case CppDialect::kCpp26: return "c++26";
  }
  return "auto";
}

/** @brief True iff @p actual is at least as new as @p required. */
inline bool CppDialectAtLeast(CppDialect actual, CppDialect required) {
  if (actual == CppDialect::kAuto)
    actual = kCppDialectDefault;
  if (required == CppDialect::kAuto)
    required = kCppDialectDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Python interpreter version
// ===========================================================================

enum class PythonVersion {
  kAuto = 0,
  kPy2_7,
  kPy3_6,
  kPy3_8,
  kPy3_10,
  kPy3_11,
  kPy3_12,
  kPy3_13,
};

constexpr PythonVersion kPythonVersionDefault = PythonVersion::kPy3_11;

inline std::optional<PythonVersion> ParsePythonVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return PythonVersion::kAuto;
  if (s.rfind("py", 0) == 0)
    s.erase(0, 2);
  if (s == "2.7" || s == "2")
    return PythonVersion::kPy2_7;
  if (s == "3.6")
    return PythonVersion::kPy3_6;
  if (s == "3.7" || s == "3.8")
    return PythonVersion::kPy3_8;
  if (s == "3.9" || s == "3.10")
    return PythonVersion::kPy3_10;
  if (s == "3.11")
    return PythonVersion::kPy3_11;
  if (s == "3.12")
    return PythonVersion::kPy3_12;
  if (s == "3.13" || s == "3")
    return PythonVersion::kPy3_13;
  return std::nullopt;
}

inline const char *PythonVersionToString(PythonVersion v) {
  switch (v) {
  case PythonVersion::kAuto:   return "auto";
  case PythonVersion::kPy2_7:  return "2.7";
  case PythonVersion::kPy3_6:  return "3.6";
  case PythonVersion::kPy3_8:  return "3.8";
  case PythonVersion::kPy3_10: return "3.10";
  case PythonVersion::kPy3_11: return "3.11";
  case PythonVersion::kPy3_12: return "3.12";
  case PythonVersion::kPy3_13: return "3.13";
  }
  return "auto";
}

inline bool PythonVersionAtLeast(PythonVersion actual, PythonVersion required) {
  if (actual == PythonVersion::kAuto)
    actual = kPythonVersionDefault;
  if (required == PythonVersion::kAuto)
    required = kPythonVersionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Java release
// ===========================================================================

enum class JavaRelease {
  kAuto = 0,
  kJava8,
  kJava11,
  kJava17,
  kJava21,
  kJava23,
};

constexpr JavaRelease kJavaReleaseDefault = JavaRelease::kJava17;

inline std::optional<JavaRelease> ParseJavaRelease(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return JavaRelease::kAuto;
  if (s.rfind("java", 0) == 0)
    s.erase(0, 4);
  if (s.rfind("jdk", 0) == 0)
    s.erase(0, 3);
  if (s == "8" || s == "1.8")
    return JavaRelease::kJava8;
  if (s == "11")
    return JavaRelease::kJava11;
  if (s == "17")
    return JavaRelease::kJava17;
  if (s == "21")
    return JavaRelease::kJava21;
  if (s == "23")
    return JavaRelease::kJava23;
  return std::nullopt;
}

inline const char *JavaReleaseToString(JavaRelease v) {
  switch (v) {
  case JavaRelease::kAuto:   return "auto";
  case JavaRelease::kJava8:  return "8";
  case JavaRelease::kJava11: return "11";
  case JavaRelease::kJava17: return "17";
  case JavaRelease::kJava21: return "21";
  case JavaRelease::kJava23: return "23";
  }
  return "auto";
}

inline bool JavaReleaseAtLeast(JavaRelease actual, JavaRelease required) {
  if (actual == JavaRelease::kAuto)
    actual = kJavaReleaseDefault;
  if (required == JavaRelease::kAuto)
    required = kJavaReleaseDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// .NET / C# language version
// ===========================================================================

enum class DotnetLangVersion {
  kAuto = 0,
  kCs7_3,
  kCs8,
  kCs9,
  kCs10,
  kCs11,
  kCs12,
};

constexpr DotnetLangVersion kDotnetLangVersionDefault = DotnetLangVersion::kCs11;

inline std::optional<DotnetLangVersion> ParseDotnetLangVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return DotnetLangVersion::kAuto;
  if (s.rfind("cs", 0) == 0)
    s.erase(0, 2);
  if (s.rfind("c#", 0) == 0)
    s.erase(0, 2);
  if (s == "7.3" || s == "73")
    return DotnetLangVersion::kCs7_3;
  if (s == "8" || s == "8.0")
    return DotnetLangVersion::kCs8;
  if (s == "9" || s == "9.0")
    return DotnetLangVersion::kCs9;
  if (s == "10" || s == "10.0")
    return DotnetLangVersion::kCs10;
  if (s == "11" || s == "11.0")
    return DotnetLangVersion::kCs11;
  if (s == "12" || s == "12.0")
    return DotnetLangVersion::kCs12;
  return std::nullopt;
}

inline const char *DotnetLangVersionToString(DotnetLangVersion v) {
  switch (v) {
  case DotnetLangVersion::kAuto:  return "auto";
  case DotnetLangVersion::kCs7_3: return "7.3";
  case DotnetLangVersion::kCs8:   return "8";
  case DotnetLangVersion::kCs9:   return "9";
  case DotnetLangVersion::kCs10:  return "10";
  case DotnetLangVersion::kCs11:  return "11";
  case DotnetLangVersion::kCs12:  return "12";
  }
  return "auto";
}

inline bool DotnetLangVersionAtLeast(DotnetLangVersion actual, DotnetLangVersion required) {
  if (actual == DotnetLangVersion::kAuto)
    actual = kDotnetLangVersionDefault;
  if (required == DotnetLangVersion::kAuto)
    required = kDotnetLangVersionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// .NET target framework
// ===========================================================================

enum class DotnetTargetFramework {
  kAuto = 0,
  kNet6,
  kNet7,
  kNet8,
  kNet9,
};

constexpr DotnetTargetFramework kDotnetTargetFrameworkDefault = DotnetTargetFramework::kNet8;

inline std::optional<DotnetTargetFramework> ParseDotnetTargetFramework(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return DotnetTargetFramework::kAuto;
  if (s.rfind("net", 0) == 0)
    s.erase(0, 3);
  if (s == "6" || s == "6.0")
    return DotnetTargetFramework::kNet6;
  if (s == "7" || s == "7.0")
    return DotnetTargetFramework::kNet7;
  if (s == "8" || s == "8.0")
    return DotnetTargetFramework::kNet8;
  if (s == "9" || s == "9.0")
    return DotnetTargetFramework::kNet9;
  return std::nullopt;
}

inline const char *DotnetTargetFrameworkToString(DotnetTargetFramework v) {
  switch (v) {
  case DotnetTargetFramework::kAuto: return "auto";
  case DotnetTargetFramework::kNet6: return "net6";
  case DotnetTargetFramework::kNet7: return "net7";
  case DotnetTargetFramework::kNet8: return "net8";
  case DotnetTargetFramework::kNet9: return "net9";
  }
  return "auto";
}

inline bool DotnetTargetFrameworkAtLeast(DotnetTargetFramework actual,
                                         DotnetTargetFramework required) {
  if (actual == DotnetTargetFramework::kAuto)
    actual = kDotnetTargetFrameworkDefault;
  if (required == DotnetTargetFramework::kAuto)
    required = kDotnetTargetFrameworkDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Rust edition
// ===========================================================================

enum class RustEdition {
  kAuto = 0,
  kE2015,
  kE2018,
  kE2021,
  kE2024,
};

constexpr RustEdition kRustEditionDefault = RustEdition::kE2021;

inline std::optional<RustEdition> ParseRustEdition(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return RustEdition::kAuto;
  if (s.rfind("edition", 0) == 0)
    s.erase(0, 7);
  while (!s.empty() && (s.front() == ' ' || s.front() == '=' || s.front() == '"'))
    s.erase(s.begin());
  while (!s.empty() && s.back() == '"')
    s.pop_back();
  if (s == "2015")
    return RustEdition::kE2015;
  if (s == "2018")
    return RustEdition::kE2018;
  if (s == "2021")
    return RustEdition::kE2021;
  if (s == "2024")
    return RustEdition::kE2024;
  return std::nullopt;
}

inline const char *RustEditionToString(RustEdition v) {
  switch (v) {
  case RustEdition::kAuto:  return "auto";
  case RustEdition::kE2015: return "2015";
  case RustEdition::kE2018: return "2018";
  case RustEdition::kE2021: return "2021";
  case RustEdition::kE2024: return "2024";
  }
  return "auto";
}

inline bool RustEditionAtLeast(RustEdition actual, RustEdition required) {
  if (actual == RustEdition::kAuto)
    actual = kRustEditionDefault;
  if (required == RustEdition::kAuto)
    required = kRustEditionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Go release
// ===========================================================================

enum class GoVersion {
  kAuto = 0,
  kGo1_18,
  kGo1_20,
  kGo1_21,
  kGo1_22,
  kGo1_23,
};

constexpr GoVersion kGoVersionDefault = GoVersion::kGo1_21;

inline std::optional<GoVersion> ParseGoVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return GoVersion::kAuto;
  if (s.rfind("go", 0) == 0)
    s.erase(0, 2);
  if (s == "1.18")
    return GoVersion::kGo1_18;
  if (s == "1.19" || s == "1.20")
    return GoVersion::kGo1_20;
  if (s == "1.21")
    return GoVersion::kGo1_21;
  if (s == "1.22")
    return GoVersion::kGo1_22;
  if (s == "1.23")
    return GoVersion::kGo1_23;
  return std::nullopt;
}

inline const char *GoVersionToString(GoVersion v) {
  switch (v) {
  case GoVersion::kAuto:   return "auto";
  case GoVersion::kGo1_18: return "1.18";
  case GoVersion::kGo1_20: return "1.20";
  case GoVersion::kGo1_21: return "1.21";
  case GoVersion::kGo1_22: return "1.22";
  case GoVersion::kGo1_23: return "1.23";
  }
  return "auto";
}

inline bool GoVersionAtLeast(GoVersion actual, GoVersion required) {
  if (actual == GoVersion::kAuto)
    actual = kGoVersionDefault;
  if (required == GoVersion::kAuto)
    required = kGoVersionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// JavaScript / ECMAScript edition
// ===========================================================================

enum class EcmaVersion {
  kAuto = 0,
  kEs5,
  kEs2015,
  kEs2017,
  kEs2020,
  kEs2022,
  kEs2023,
  kEsNext,
};

constexpr EcmaVersion kEcmaVersionDefault = EcmaVersion::kEs2022;

inline std::optional<EcmaVersion> ParseEcmaVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return EcmaVersion::kAuto;
  if (s.rfind("es", 0) == 0)
    s.erase(0, 2);
  if (s == "5")
    return EcmaVersion::kEs5;
  if (s == "2015" || s == "6")
    return EcmaVersion::kEs2015;
  if (s == "2017" || s == "8")
    return EcmaVersion::kEs2017;
  if (s == "2020" || s == "11")
    return EcmaVersion::kEs2020;
  if (s == "2022" || s == "13")
    return EcmaVersion::kEs2022;
  if (s == "2023" || s == "14")
    return EcmaVersion::kEs2023;
  if (s == "next" || s == "nxt")
    return EcmaVersion::kEsNext;
  return std::nullopt;
}

inline const char *EcmaVersionToString(EcmaVersion v) {
  switch (v) {
  case EcmaVersion::kAuto:   return "auto";
  case EcmaVersion::kEs5:    return "es5";
  case EcmaVersion::kEs2015: return "es2015";
  case EcmaVersion::kEs2017: return "es2017";
  case EcmaVersion::kEs2020: return "es2020";
  case EcmaVersion::kEs2022: return "es2022";
  case EcmaVersion::kEs2023: return "es2023";
  case EcmaVersion::kEsNext: return "esnext";
  }
  return "auto";
}

inline bool EcmaVersionAtLeast(EcmaVersion actual, EcmaVersion required) {
  if (actual == EcmaVersion::kAuto)
    actual = kEcmaVersionDefault;
  if (required == EcmaVersion::kAuto)
    required = kEcmaVersionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Ruby version
// ===========================================================================

enum class RubyVersion {
  kAuto = 0,
  kRuby1_9,
  kRuby2_7,
  kRuby3_0,
  kRuby3_2,
  kRuby3_3,
};

constexpr RubyVersion kRubyVersionDefault = RubyVersion::kRuby3_2;

inline std::optional<RubyVersion> ParseRubyVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return RubyVersion::kAuto;
  if (s.rfind("ruby", 0) == 0)
    s.erase(0, 4);
  if (s == "1.9")
    return RubyVersion::kRuby1_9;
  if (s == "2.7" || s == "2")
    return RubyVersion::kRuby2_7;
  if (s == "3.0" || s == "3")
    return RubyVersion::kRuby3_0;
  if (s == "3.1" || s == "3.2")
    return RubyVersion::kRuby3_2;
  if (s == "3.3")
    return RubyVersion::kRuby3_3;
  return std::nullopt;
}

inline const char *RubyVersionToString(RubyVersion v) {
  switch (v) {
  case RubyVersion::kAuto:    return "auto";
  case RubyVersion::kRuby1_9: return "1.9";
  case RubyVersion::kRuby2_7: return "2.7";
  case RubyVersion::kRuby3_0: return "3.0";
  case RubyVersion::kRuby3_2: return "3.2";
  case RubyVersion::kRuby3_3: return "3.3";
  }
  return "auto";
}

inline bool RubyVersionAtLeast(RubyVersion actual, RubyVersion required) {
  if (actual == RubyVersion::kAuto)
    actual = kRubyVersionDefault;
  if (required == RubyVersion::kAuto)
    required = kRubyVersionDefault;
  return static_cast<int>(actual) >= static_cast<int>(required);
}

// ===========================================================================
// Generic helpers 鈥?used by ploy `LANG` directives, polyver, and CLI
// ===========================================================================

/**
 * @brief Parse and serialize a "language version" pair as a single string.
 *
 * The textual form is `<lang>=<version>` (case-insensitive on the language
 * identifier, the version part is delegated to the per-language parser).
 *
 * Recognized language identifiers: cpp, c++, python, py, java, dotnet, cs,
 * c#, csharp, rust, go, golang, javascript, js, ecma, ruby, rb.
 *
 * The function returns true on success and writes the canonical
 * `<lang>=<canonical-version>` form into @p out_canonical.  It does NOT
 * modify any global state.
 */
bool CanonicalizeLanguageVersion(std::string_view text, std::string &out_canonical);

} // namespace polyglot::frontends
