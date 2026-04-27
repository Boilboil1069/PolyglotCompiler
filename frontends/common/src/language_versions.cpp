/**
 * @file     language_versions.cpp
 * @brief    Implementation of CanonicalizeLanguageVersion()
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "frontends/common/include/language_versions.h"

#include <algorithm>
#include <string>

namespace polyglot::frontends {

namespace {

std::string ToLower(std::string_view text) {
  std::string s(text);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

} // namespace

bool CanonicalizeLanguageVersion(std::string_view text, std::string &out_canonical) {
  // Locate the '=' separator; reject if absent.
  auto eq = text.find('=');
  if (eq == std::string_view::npos)
    return false;
  std::string lang = ToLower(text.substr(0, eq));
  std::string ver(text.substr(eq + 1));
  // Trim whitespace on both sides for forgiving CLI input.
  auto trim = [](std::string &s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
      s.pop_back();
  };
  trim(lang);
  trim(ver);

  // Normalize aliases.
  if (lang == "c++")
    lang = "cpp";
  if (lang == "py")
    lang = "python";
  if (lang == "cs" || lang == "c#" || lang == "csharp")
    lang = "dotnet";
  if (lang == "golang")
    lang = "go";
  if (lang == "js" || lang == "ecma")
    lang = "javascript";
  if (lang == "rb")
    lang = "ruby";

  if (lang == "cpp") {
    auto v = ParseCppDialect(ver);
    if (!v)
      return false;
    out_canonical = std::string("cpp=") + CppDialectToString(*v);
    return true;
  }
  if (lang == "python") {
    auto v = ParsePythonVersion(ver);
    if (!v)
      return false;
    out_canonical = std::string("python=") + PythonVersionToString(*v);
    return true;
  }
  if (lang == "java") {
    auto v = ParseJavaRelease(ver);
    if (!v)
      return false;
    out_canonical = std::string("java=") + JavaReleaseToString(*v);
    return true;
  }
  if (lang == "dotnet") {
    auto v = ParseDotnetLangVersion(ver);
    if (!v)
      return false;
    out_canonical = std::string("dotnet=") + DotnetLangVersionToString(*v);
    return true;
  }
  if (lang == "rust") {
    auto v = ParseRustEdition(ver);
    if (!v)
      return false;
    out_canonical = std::string("rust=") + RustEditionToString(*v);
    return true;
  }
  if (lang == "go") {
    auto v = ParseGoVersion(ver);
    if (!v)
      return false;
    out_canonical = std::string("go=") + GoVersionToString(*v);
    return true;
  }
  if (lang == "javascript") {
    auto v = ParseEcmaVersion(ver);
    if (!v)
      return false;
    out_canonical = std::string("javascript=") + EcmaVersionToString(*v);
    return true;
  }
  if (lang == "ruby") {
    auto v = ParseRubyVersion(ver);
    if (!v)
      return false;
    out_canonical = std::string("ruby=") + RubyVersionToString(*v);
    return true;
  }
  return false;
}

} // namespace polyglot::frontends
