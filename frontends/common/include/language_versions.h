/**
 * @file     language_versions.h
 * @brief    Per-language version / dialect enumerations and helpers
 *
 * Multi-language version management infrastructure.
 *
 * Each supported source language gets a strongly-typed enum describing the
 * dialect / language standard / runtime version that the corresponding
 * frontend must honour.  Every enum reserves the `kAuto` member which means
 * "use the deterministic stable default".  Project/toolchain discovery
 * is performed by higher-level callers and must be resolved to an explicit
 * enum before entering a frontend.
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

/** @brief Deterministic stable fallback when no explicit dialect is selected. */
constexpr CppDialect kCppDialectDefault = CppDialect::kCpp23;

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

/** @brief Value exposed through the standard `__cplusplus` feature macro. */
inline const char *CppDialectCplusplusValue(CppDialect d) {
  if (d == CppDialect::kAuto)
    d = kCppDialectDefault;
  switch (d) {
  case CppDialect::kCpp98:
  case CppDialect::kCpp03: return "199711L";
  case CppDialect::kCpp11: return "201103L";
  case CppDialect::kCpp14: return "201402L";
  case CppDialect::kCpp17: return "201703L";
  case CppDialect::kCpp20: return "202002L";
  case CppDialect::kCpp23: return "202302L";
  case CppDialect::kCpp26: return "202400L";
  case CppDialect::kAuto: break;
  }
  return "202302L";
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
  kPy3_7,
  kPy3_8,
  kPy3_9,
  kPy3_10,
  kPy3_11,
  kPy3_12,
  kPy3_13,
  kPy3_14,
};

constexpr PythonVersion kPythonVersionDefault = PythonVersion::kPy3_14;

inline std::optional<PythonVersion> ParsePythonVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return PythonVersion::kAuto;
  if (s.rfind("python", 0) == 0)
    s.erase(0, 6);
  else if (s.rfind("py", 0) == 0)
    s.erase(0, 2);
  if (s == "2.7" || s == "2")
    return PythonVersion::kPy2_7;
  if (s == "3.6")
    return PythonVersion::kPy3_6;
  if (s == "3.7")
    return PythonVersion::kPy3_7;
  if (s == "3.8")
    return PythonVersion::kPy3_8;
  if (s == "3.9")
    return PythonVersion::kPy3_9;
  if (s == "3.10")
    return PythonVersion::kPy3_10;
  if (s == "3.11")
    return PythonVersion::kPy3_11;
  if (s == "3.12")
    return PythonVersion::kPy3_12;
  if (s == "3.13")
    return PythonVersion::kPy3_13;
  if (s == "3.14" || s == "3")
    return PythonVersion::kPy3_14;
  return std::nullopt;
}

inline const char *PythonVersionToString(PythonVersion v) {
  switch (v) {
  case PythonVersion::kAuto:   return "auto";
  case PythonVersion::kPy2_7:  return "2.7";
  case PythonVersion::kPy3_6:  return "3.6";
  case PythonVersion::kPy3_7:  return "3.7";
  case PythonVersion::kPy3_8:  return "3.8";
  case PythonVersion::kPy3_9:  return "3.9";
  case PythonVersion::kPy3_10: return "3.10";
  case PythonVersion::kPy3_11: return "3.11";
  case PythonVersion::kPy3_12: return "3.12";
  case PythonVersion::kPy3_13: return "3.13";
  case PythonVersion::kPy3_14: return "3.14";
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
  kJava9,
  kJava10,
  kJava11,
  kJava12,
  kJava13,
  kJava14,
  kJava15,
  kJava16,
  kJava17,
  kJava18,
  kJava19,
  kJava20,
  kJava21,
  kJava22,
  kJava23,
  kJava24,
  kJava25,
  kJava26,
};

constexpr JavaRelease kJavaReleaseDefault = JavaRelease::kJava26;

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
  if (s == "9")
    return JavaRelease::kJava9;
  if (s == "10")
    return JavaRelease::kJava10;
  if (s == "11")
    return JavaRelease::kJava11;
  if (s == "12")
    return JavaRelease::kJava12;
  if (s == "13")
    return JavaRelease::kJava13;
  if (s == "14")
    return JavaRelease::kJava14;
  if (s == "15")
    return JavaRelease::kJava15;
  if (s == "16")
    return JavaRelease::kJava16;
  if (s == "17")
    return JavaRelease::kJava17;
  if (s == "18")
    return JavaRelease::kJava18;
  if (s == "19")
    return JavaRelease::kJava19;
  if (s == "20")
    return JavaRelease::kJava20;
  if (s == "21")
    return JavaRelease::kJava21;
  if (s == "22")
    return JavaRelease::kJava22;
  if (s == "23")
    return JavaRelease::kJava23;
  if (s == "24")
    return JavaRelease::kJava24;
  if (s == "25")
    return JavaRelease::kJava25;
  if (s == "26")
    return JavaRelease::kJava26;
  return std::nullopt;
}

inline const char *JavaReleaseToString(JavaRelease v) {
  switch (v) {
  case JavaRelease::kAuto:   return "auto";
  case JavaRelease::kJava8:  return "8";
  case JavaRelease::kJava9:  return "9";
  case JavaRelease::kJava10: return "10";
  case JavaRelease::kJava11: return "11";
  case JavaRelease::kJava12: return "12";
  case JavaRelease::kJava13: return "13";
  case JavaRelease::kJava14: return "14";
  case JavaRelease::kJava15: return "15";
  case JavaRelease::kJava16: return "16";
  case JavaRelease::kJava17: return "17";
  case JavaRelease::kJava18: return "18";
  case JavaRelease::kJava19: return "19";
  case JavaRelease::kJava20: return "20";
  case JavaRelease::kJava21: return "21";
  case JavaRelease::kJava22: return "22";
  case JavaRelease::kJava23: return "23";
  case JavaRelease::kJava24: return "24";
  case JavaRelease::kJava25: return "25";
  case JavaRelease::kJava26: return "26";
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
  kCs13,
  kCs14,
  kPreview,
};

constexpr DotnetLangVersion kDotnetLangVersionDefault = DotnetLangVersion::kCs14;

inline std::optional<DotnetLangVersion> ParseDotnetLangVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return DotnetLangVersion::kAuto;
  if (s.rfind("csharp", 0) == 0)
    s.erase(0, 6);
  else if (s.rfind("dotnet", 0) == 0)
    s.erase(0, 6);
  else if (s.rfind("cs", 0) == 0)
    s.erase(0, 2);
  else if (s.rfind("c#", 0) == 0)
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
  if (s == "13" || s == "13.0")
    return DotnetLangVersion::kCs13;
  if (s == "14" || s == "14.0" || s == "latest")
    return DotnetLangVersion::kCs14;
  if (s == "preview")
    return DotnetLangVersion::kPreview;
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
  case DotnetLangVersion::kCs13:  return "13";
  case DotnetLangVersion::kCs14:  return "14";
  case DotnetLangVersion::kPreview: return "preview";
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
  kNet10,
};

constexpr DotnetTargetFramework kDotnetTargetFrameworkDefault = DotnetTargetFramework::kNet10;

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
  if (s == "10" || s == "10.0")
    return DotnetTargetFramework::kNet10;
  return std::nullopt;
}

inline const char *DotnetTargetFrameworkToString(DotnetTargetFramework v) {
  switch (v) {
  case DotnetTargetFramework::kAuto: return "auto";
  case DotnetTargetFramework::kNet6: return "net6";
  case DotnetTargetFramework::kNet7: return "net7";
  case DotnetTargetFramework::kNet8: return "net8";
  case DotnetTargetFramework::kNet9: return "net9";
  case DotnetTargetFramework::kNet10: return "net10";
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

constexpr RustEdition kRustEditionDefault = RustEdition::kE2024;

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
  kGo1_19,
  kGo1_20,
  kGo1_21,
  kGo1_22,
  kGo1_23,
  kGo1_24,
  kGo1_25,
  kGo1_26,
};

constexpr GoVersion kGoVersionDefault = GoVersion::kGo1_26;

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
  if (s == "1.19")
    return GoVersion::kGo1_19;
  if (s == "1.20")
    return GoVersion::kGo1_20;
  if (s == "1.21")
    return GoVersion::kGo1_21;
  if (s == "1.22")
    return GoVersion::kGo1_22;
  if (s == "1.23")
    return GoVersion::kGo1_23;
  if (s == "1.24")
    return GoVersion::kGo1_24;
  if (s == "1.25")
    return GoVersion::kGo1_25;
  if (s == "1.26")
    return GoVersion::kGo1_26;
  return std::nullopt;
}

inline const char *GoVersionToString(GoVersion v) {
  switch (v) {
  case GoVersion::kAuto:   return "auto";
  case GoVersion::kGo1_18: return "1.18";
  case GoVersion::kGo1_19: return "1.19";
  case GoVersion::kGo1_20: return "1.20";
  case GoVersion::kGo1_21: return "1.21";
  case GoVersion::kGo1_22: return "1.22";
  case GoVersion::kGo1_23: return "1.23";
  case GoVersion::kGo1_24: return "1.24";
  case GoVersion::kGo1_25: return "1.25";
  case GoVersion::kGo1_26: return "1.26";
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
  kEs2016,
  kEs2017,
  kEs2018,
  kEs2019,
  kEs2020,
  kEs2021,
  kEs2022,
  kEs2023,
  kEs2024,
  kEs2025,
  kEs2026,
  kEsNext,
};

constexpr EcmaVersion kEcmaVersionDefault = EcmaVersion::kEs2026;

inline std::optional<EcmaVersion> ParseEcmaVersion(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "auto" || s.empty())
    return EcmaVersion::kAuto;
  if (s.rfind("ecmascript", 0) == 0)
    s.erase(0, 10);
  else if (s.rfind("es", 0) == 0)
    s.erase(0, 2);
  if (s == "5")
    return EcmaVersion::kEs5;
  if (s == "2015" || s == "6")
    return EcmaVersion::kEs2015;
  if (s == "2016" || s == "7")
    return EcmaVersion::kEs2016;
  if (s == "2017" || s == "8")
    return EcmaVersion::kEs2017;
  if (s == "2018" || s == "9")
    return EcmaVersion::kEs2018;
  if (s == "2019" || s == "10")
    return EcmaVersion::kEs2019;
  if (s == "2020" || s == "11")
    return EcmaVersion::kEs2020;
  if (s == "2021" || s == "12")
    return EcmaVersion::kEs2021;
  if (s == "2022" || s == "13")
    return EcmaVersion::kEs2022;
  if (s == "2023" || s == "14")
    return EcmaVersion::kEs2023;
  if (s == "2024" || s == "15")
    return EcmaVersion::kEs2024;
  if (s == "2025" || s == "16")
    return EcmaVersion::kEs2025;
  if (s == "2026" || s == "17")
    return EcmaVersion::kEs2026;
  if (s == "next" || s == "nxt")
    return EcmaVersion::kEsNext;
  return std::nullopt;
}

inline const char *EcmaVersionToString(EcmaVersion v) {
  switch (v) {
  case EcmaVersion::kAuto:   return "auto";
  case EcmaVersion::kEs5:    return "es5";
  case EcmaVersion::kEs2015: return "es2015";
  case EcmaVersion::kEs2016: return "es2016";
  case EcmaVersion::kEs2017: return "es2017";
  case EcmaVersion::kEs2018: return "es2018";
  case EcmaVersion::kEs2019: return "es2019";
  case EcmaVersion::kEs2020: return "es2020";
  case EcmaVersion::kEs2021: return "es2021";
  case EcmaVersion::kEs2022: return "es2022";
  case EcmaVersion::kEs2023: return "es2023";
  case EcmaVersion::kEs2024: return "es2024";
  case EcmaVersion::kEs2025: return "es2025";
  case EcmaVersion::kEs2026: return "es2026";
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
  kRuby3_1,
  kRuby3_2,
  kRuby3_3,
  kRuby3_4,
  kRuby4_0,
};

constexpr RubyVersion kRubyVersionDefault = RubyVersion::kRuby4_0;

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
  if (s == "3.1")
    return RubyVersion::kRuby3_1;
  if (s == "3.2")
    return RubyVersion::kRuby3_2;
  if (s == "3.3")
    return RubyVersion::kRuby3_3;
  if (s == "3.4")
    return RubyVersion::kRuby3_4;
  if (s == "4" || s == "4.0")
    return RubyVersion::kRuby4_0;
  return std::nullopt;
}

inline const char *RubyVersionToString(RubyVersion v) {
  switch (v) {
  case RubyVersion::kAuto:    return "auto";
  case RubyVersion::kRuby1_9: return "1.9";
  case RubyVersion::kRuby2_7: return "2.7";
  case RubyVersion::kRuby3_0: return "3.0";
  case RubyVersion::kRuby3_1: return "3.1";
  case RubyVersion::kRuby3_2: return "3.2";
  case RubyVersion::kRuby3_3: return "3.3";
  case RubyVersion::kRuby3_4: return "3.4";
  case RubyVersion::kRuby4_0: return "4.0";
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
// Generic helpers - used by poly `LANG` directives, polyver, and CLI
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
