/**
 * @file     toolchain_db.h
 * @brief    Tool-chain database 鈥?discovery, persistence, and lookup.
 *
 * Backs the runtime version-management surface: the `polyver` CLI as well as
 * the polyc/polyld auto-detection paths and the polyui Toolchains tab.
 *
 * On disk we maintain two JSON files:
 *
 *   * `~/.polyglot/toolchains.json`     鈥?global (per-user) tool-chain
 *                                         catalog produced by `polyver
 *                                         detect` and edited via
 *                                         `polyver use`.
 *   * `<project>/.polyglot/toolchains.lock` 鈥?per-project pinned versions
 *                                         (written by `polyver use` when
 *                                         executed inside a project).
 *
 * @ingroup  Tool / polyver
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::polyver {

/**
 * @brief A single tool-chain installation found on the host.
 */
struct ToolchainEntry {
  std::string language;            // canonical: cpp, python, java, dotnet, rust, go, javascript, ruby
  std::string version;             // canonical, e.g. "3.11", "17", "1.21", "2021"
  std::string path;                // absolute path to the executable / install root
  std::string vendor;              // e.g. "Anaconda", "Homebrew", "system", "msvc"
  bool        is_default{false};   // marked as the user-level default for this language
};

/**
 * @brief In-memory tool-chain catalog with JSON serialization helpers.
 *
 * Keeps insertion order stable so that `polyver list` output is
 * deterministic across invocations.
 */
class ToolchainDb {
public:
  ToolchainDb() = default;

  // ---------- Construction & persistence -----------------------------------

  /// Path of the user-level catalog file (created if missing).
  static std::filesystem::path UserCatalogPath();

  /// Find the nearest enclosing project root (a directory containing a
  /// `.polyglot/` folder).  Returns empty path if none is found.
  static std::filesystem::path FindProjectRoot(const std::filesystem::path &start);

  /// Path of the project lock file, given a project root.
  static std::filesystem::path ProjectLockPath(const std::filesystem::path &project_root);

  /// Load entries from a JSON file.  Returns an empty database if the file
  /// does not exist or fails to parse (errors are pushed into @p errors).
  static ToolchainDb LoadFromFile(const std::filesystem::path &path,
                                  std::vector<std::string>   &errors);

  /// Persist the catalog to disk in pretty-printed JSON.  Returns false on
  /// I/O failure; the caller is expected to surface the error.
  bool SaveToFile(const std::filesystem::path &path) const;

  // ---------- Mutation -----------------------------------------------------

  /// Insert (or de-duplicate) an entry.  Two entries are considered equal
  /// iff their (language, version, path) triple matches.
  void AddEntry(const ToolchainEntry &entry);

  /// Mark @p version as default for @p language and clear the flag on any
  /// other entry of the same language.  Returns true if a matching entry
  /// existed.
  bool SetDefault(const std::string &language, const std::string &version);

  // ---------- Query --------------------------------------------------------

  const std::vector<ToolchainEntry> &Entries() const { return entries_; }

  /// All entries belonging to @p language.
  std::vector<ToolchainEntry> ByLanguage(const std::string &language) const;

  /// Lookup the default entry for @p language (if any).
  std::optional<ToolchainEntry> Default(const std::string &language) const;

  /// Lookup an entry by (language, version) pair.
  std::optional<ToolchainEntry> Find(const std::string &language,
                                     const std::string &version) const;

private:
  std::vector<ToolchainEntry> entries_;
};

/**
 * @brief Run cross-platform tool-chain detection.
 *
 * Scans `PATH` and, on a best-effort basis, the well-known install
 * directories listed in `docs/realization/language_versions.md`.  The
 * returned database is *not* persisted automatically; callers usually
 * merge it into the on-disk catalog and then call SaveToFile().
 */
ToolchainDb DetectToolchains();

/**
 * @brief Merge @p incoming into @p existing, preserving the existing
 * `is_default` flags whenever possible.
 */
void MergeToolchainDb(ToolchainDb &existing, const ToolchainDb &incoming);

/**
 * @brief Resolve the effective version string for @p language.
 *
 * Lookup order:
 *   1. Project lock file at @p project_root (if non-empty).
 *   2. User catalog default.
 *   3. Empty optional if nothing is registered.
 */
std::optional<std::string> ResolveEffectiveVersion(
    const std::string                &language,
    const std::filesystem::path      &project_root = {});

} // namespace polyglot::polyver
