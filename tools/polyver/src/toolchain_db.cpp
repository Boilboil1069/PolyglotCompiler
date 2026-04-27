/**
 * @file     toolchain_db.cpp
 * @brief    Tool-chain discovery + persistence implementation.
 *
 * @ingroup  Tool / polyver
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/polyver/include/toolchain_db.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace polyglot::polyver {

// ===========================================================================
// Path helpers
// ===========================================================================

namespace {

fs::path HomeDir() {
#if defined(_WIN32)
  if (const char *p = std::getenv("USERPROFILE"))
    return fs::path(p);
  if (const char *p = std::getenv("HOMEPATH"))
    return fs::path(p);
#else
  if (const char *p = std::getenv("HOME"))
    return fs::path(p);
#endif
  return fs::current_path();
}

/// Run an external command and capture its stdout (trimmed).
/// Returns std::nullopt if the command cannot be launched or exits non-zero.
std::optional<std::string> RunCapture(const std::string &cmd) {
#if defined(_WIN32)
  FILE *pipe = _popen((cmd + " 2>NUL").c_str(), "r");
#else
  FILE *pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
#endif
  if (!pipe)
    return std::nullopt;
  std::string out;
  char        buf[256];
  while (std::fgets(buf, sizeof(buf), pipe))
    out.append(buf);
#if defined(_WIN32)
  int rc = _pclose(pipe);
#else
  int rc = pclose(pipe);
#endif
  if (rc != 0 || out.empty())
    return std::nullopt;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
    out.pop_back();
  return out;
}

/// Split PATH/colon-or-semicolon-separated lists.
std::vector<std::string> SplitPath(const std::string &raw) {
#if defined(_WIN32)
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::vector<std::string> out;
  std::string              cur;
  for (char c : raw) {
    if (c == sep) {
      if (!cur.empty())
        out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

#if defined(_WIN32)
constexpr const char *kExeExt = ".exe";
#else
constexpr const char *kExeExt = "";
#endif

/// Locate every directory entry on PATH whose filename matches @p needle.
std::vector<fs::path> FindOnPath(const std::string &needle) {
  std::vector<fs::path> hits;
  const char *raw = std::getenv("PATH");
  if (!raw)
    return hits;
  for (const auto &dir : SplitPath(raw)) {
    fs::path        p(dir);
    std::error_code ec;
    if (!fs::is_directory(p, ec))
      continue;
    fs::path candidate = p / (needle + kExeExt);
    if (fs::exists(candidate, ec))
      hits.push_back(candidate);
  }
  return hits;
}

/// Convenience wrapper: probe `<exe> <flag>` and pull out the first version
/// number with a trailing newline stripped.
std::optional<std::string> ProbeVersion(const fs::path &exe, const std::string &flag,
                                        const std::regex &re) {
  std::string cmd = "\"" + exe.string() + "\" " + flag;
  auto        out = RunCapture(cmd);
  if (!out)
    return std::nullopt;
  std::smatch m;
  if (!std::regex_search(*out, m, re))
    return std::nullopt;
  return m[1].str();
}

} // namespace

// ===========================================================================
// ToolchainDb static path helpers
// ===========================================================================

fs::path ToolchainDb::UserCatalogPath() {
  fs::path        dir = HomeDir() / ".polyglot";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir / "toolchains.json";
}

fs::path ToolchainDb::FindProjectRoot(const fs::path &start) {
  std::error_code ec;
  fs::path        cur = fs::absolute(start, ec);
  while (!cur.empty()) {
    if (fs::is_directory(cur / ".polyglot", ec))
      return cur;
    fs::path parent = cur.parent_path();
    if (parent == cur)
      break;
    cur = parent;
  }
  return {};
}

fs::path ToolchainDb::ProjectLockPath(const fs::path &project_root) {
  return project_root / ".polyglot" / "toolchains.lock";
}

// ===========================================================================
// Serialization
// ===========================================================================

ToolchainDb ToolchainDb::LoadFromFile(const fs::path &path, std::vector<std::string> &errors) {
  ToolchainDb     db;
  std::error_code ec;
  if (!fs::exists(path, ec))
    return db;
  std::ifstream in(path);
  if (!in.is_open()) {
    errors.push_back("polyver: cannot open " + path.string());
    return db;
  }
  json j;
  try {
    in >> j;
  } catch (const std::exception &e) {
    errors.push_back("polyver: malformed JSON in " + path.string() + ": " + e.what());
    return db;
  }
  if (!j.contains("toolchains") || !j["toolchains"].is_array())
    return db;
  for (const auto &item : j["toolchains"]) {
    ToolchainEntry e;
    e.language   = item.value("language", "");
    e.version    = item.value("version", "");
    e.path       = item.value("path", "");
    e.vendor     = item.value("vendor", "");
    e.is_default = item.value("default", false);
    if (e.language.empty() || e.version.empty())
      continue;
    db.entries_.push_back(std::move(e));
  }
  return db;
}

bool ToolchainDb::SaveToFile(const fs::path &path) const {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out.is_open())
    return false;
  json j;
  j["schema"]                  = "polyglot.toolchains.v1";
  j["generated_by"]            = "polyver";
  j["toolchains"]              = json::array();
  for (const auto &e : entries_) {
    j["toolchains"].push_back({
        {"language", e.language},
        {"version",  e.version},
        {"path",     e.path},
        {"vendor",   e.vendor},
        {"default",  e.is_default},
    });
  }
  out << j.dump(2) << '\n';
  return out.good();
}

// ===========================================================================
// Mutation / Query
// ===========================================================================

void ToolchainDb::AddEntry(const ToolchainEntry &entry) {
  for (const auto &e : entries_) {
    if (e.language == entry.language && e.version == entry.version && e.path == entry.path)
      return;
  }
  entries_.push_back(entry);
}

bool ToolchainDb::SetDefault(const std::string &language, const std::string &version) {
  bool any = false;
  for (auto &e : entries_) {
    if (e.language == language && e.version == version) {
      e.is_default = true;
      any          = true;
    } else if (e.language == language) {
      e.is_default = false;
    }
  }
  return any;
}

std::vector<ToolchainEntry> ToolchainDb::ByLanguage(const std::string &language) const {
  std::vector<ToolchainEntry> out;
  for (const auto &e : entries_)
    if (e.language == language)
      out.push_back(e);
  return out;
}

std::optional<ToolchainEntry> ToolchainDb::Default(const std::string &language) const {
  for (const auto &e : entries_)
    if (e.language == language && e.is_default)
      return e;
  // Fallback: first entry of that language if no explicit default.
  for (const auto &e : entries_)
    if (e.language == language)
      return e;
  return std::nullopt;
}

std::optional<ToolchainEntry> ToolchainDb::Find(const std::string &language,
                                                const std::string &version) const {
  for (const auto &e : entries_)
    if (e.language == language && e.version == version)
      return e;
  return std::nullopt;
}

// ===========================================================================
// Discovery — per-language probes.
// Each probe is intentionally conservative: it only reports tool-chains that
// it can actually run.  No registry / system-image inspection is performed.
// ===========================================================================

namespace {

void DetectPython(ToolchainDb &db) {
  static const std::array<const char *, 9> kBins = {
      "python3.13", "python3.12", "python3.11", "python3.10",
      "python3.9",  "python3.8",  "python3.7",  "python3",
      "python",
  };
  static const std::regex kRe("Python ([0-9]+\\.[0-9]+)");
  for (const char *bin : kBins) {
    for (const auto &exe : FindOnPath(bin)) {
      auto v = ProbeVersion(exe, "--version", kRe);
      if (!v)
        continue;
      ToolchainEntry e;
      e.language = "python";
      e.version  = *v;
      e.path     = exe.string();
      e.vendor   = "system";
      db.AddEntry(e);
    }
  }
}

void DetectJava(ToolchainDb &db) {
  static const std::regex kRe("(?:openjdk|java) version \"?([0-9]+)");
  for (const auto &exe : FindOnPath("java")) {
    auto v = ProbeVersion(exe, "-version", kRe);
    if (!v)
      continue;
    ToolchainEntry e;
    e.language = "java";
    e.version  = *v;
    e.path     = exe.string();
    e.vendor   = "system";
    db.AddEntry(e);
  }
}

void DetectDotnet(ToolchainDb &db) {
  for (const auto &exe : FindOnPath("dotnet")) {
    auto out = RunCapture("\"" + exe.string() + "\" --list-runtimes");
    if (!out)
      continue;
    static const std::regex kRe("Microsoft\\.NETCore\\.App ([0-9]+)\\.[0-9]+\\.[0-9]+");
    auto                    begin = std::sregex_iterator(out->begin(), out->end(), kRe);
    auto                    end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
      ToolchainEntry e;
      e.language = "dotnet";
      e.version  = (*it)[1].str();
      e.path     = exe.string();
      e.vendor   = "system";
      db.AddEntry(e);
    }
  }
}

void DetectRust(ToolchainDb &db) {
  static const std::regex kRe("rustc ([0-9]+\\.[0-9]+\\.[0-9]+)");
  for (const auto &exe : FindOnPath("rustc")) {
    auto v = ProbeVersion(exe, "--version", kRe);
    if (!v)
      continue;
    // We expose Rust *editions*, not rustc versions, in the canonical
    // entry — but the rustc full version is preserved in the vendor
    // string for traceability.
    ToolchainEntry e;
    e.language = "rust";
    e.version  = "2021"; // conservative: rustc >=1.56 supports 2021
    e.path     = exe.string();
    e.vendor   = "rustc " + *v;
    db.AddEntry(e);
  }
}

void DetectGo(ToolchainDb &db) {
  static const std::regex kRe("go([0-9]+\\.[0-9]+)");
  for (const auto &exe : FindOnPath("go")) {
    auto v = ProbeVersion(exe, "version", kRe);
    if (!v)
      continue;
    ToolchainEntry e;
    e.language = "go";
    e.version  = *v;
    e.path     = exe.string();
    e.vendor   = "system";
    db.AddEntry(e);
  }
}

void DetectNode(ToolchainDb &db) {
  static const std::regex kRe("v([0-9]+)");
  for (const auto &exe : FindOnPath("node")) {
    auto v = ProbeVersion(exe, "--version", kRe);
    if (!v)
      continue;
    int major = std::atoi(v->c_str());
    // Map node major to ECMAScript baseline: node >= 14 supports es2020,
    // node >= 16 supports es2022, node >= 20 supports es2023.
    std::string ecma = "es2020";
    if (major >= 20)
      ecma = "es2023";
    else if (major >= 16)
      ecma = "es2022";
    ToolchainEntry e;
    e.language = "javascript";
    e.version  = ecma;
    e.path     = exe.string();
    e.vendor   = "node " + *v;
    db.AddEntry(e);
  }
}

void DetectRuby(ToolchainDb &db) {
  static const std::regex kRe("ruby ([0-9]+\\.[0-9]+)");
  for (const auto &exe : FindOnPath("ruby")) {
    auto v = ProbeVersion(exe, "--version", kRe);
    if (!v)
      continue;
    ToolchainEntry e;
    e.language = "ruby";
    e.version  = *v;
    e.path     = exe.string();
    e.vendor   = "system";
    db.AddEntry(e);
  }
}

void DetectCpp(ToolchainDb &db) {
  // We surface the host toolchain name + version in the vendor string but
  // canonicalize the version cell to a single dialect that the host
  // compiler is known to accept.  This keeps the schema uniform.
  static const struct {
    const char *bin;
    const char *flag;
    const char *re;
  } kCompilers[] = {
      {"clang++", "--version", "clang version ([0-9]+)"},
      {"g++",     "--version", "([0-9]+)\\.[0-9]+\\.[0-9]+"},
      {"cl",      "/?",        "Version ([0-9]+)"},
  };
  for (const auto &c : kCompilers) {
    for (const auto &exe : FindOnPath(c.bin)) {
      std::regex re(c.re);
      auto       v = ProbeVersion(exe, c.flag, re);
      if (!v)
        continue;
      int                      major   = std::atoi(v->c_str());
      std::string              dialect = "c++17";
      if (std::string(c.bin) == "cl") {
        dialect = (major >= 19) ? "c++20" : "c++17";
      } else {
        if (major >= 13)
          dialect = "c++23";
        else if (major >= 10)
          dialect = "c++20";
      }
      ToolchainEntry e;
      e.language = "cpp";
      e.version  = dialect;
      e.path     = exe.string();
      e.vendor   = std::string(c.bin) + " " + *v;
      db.AddEntry(e);
    }
  }
}

} // namespace

ToolchainDb DetectToolchains() {
  ToolchainDb db;
  DetectCpp(db);
  DetectPython(db);
  DetectJava(db);
  DetectDotnet(db);
  DetectRust(db);
  DetectGo(db);
  DetectNode(db);
  DetectRuby(db);
  return db;
}

void MergeToolchainDb(ToolchainDb &existing, const ToolchainDb &incoming) {
  // Remember which languages already had a default chosen so that a
  // re-detection does not silently flip the user's pinned choice.
  std::map<std::string, std::string> existing_default;
  for (const auto &e : existing.Entries())
    if (e.is_default)
      existing_default[e.language] = e.version;

  for (const auto &in : incoming.Entries())
    existing.AddEntry(in);

  for (const auto &[lang, ver] : existing_default)
    existing.SetDefault(lang, ver);
}

// ===========================================================================
// ResolveEffectiveVersion
// ===========================================================================

std::optional<std::string> ResolveEffectiveVersion(const std::string &language,
                                                   const fs::path    &project_root) {
  std::vector<std::string> errors;
  if (!project_root.empty()) {
    auto lock = ToolchainDb::ProjectLockPath(project_root);
    auto db   = ToolchainDb::LoadFromFile(lock, errors);
    if (auto e = db.Default(language))
      return e->version;
  }
  auto user = ToolchainDb::LoadFromFile(ToolchainDb::UserCatalogPath(), errors);
  if (auto e = user.Default(language))
    return e->version;
  return std::nullopt;
}

} // namespace polyglot::polyver
