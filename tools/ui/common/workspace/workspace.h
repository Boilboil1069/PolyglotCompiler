/**
 * @file     workspace.h
 * @brief    Multi-root workspace model with per-root settings,
 *           cross-root search / jump, and language-server scoping.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::workspace {

struct WorkspaceFolder {
  std::string name;
  std::string path;
  std::unordered_map<std::string, std::string> settings;
};

struct WorkspaceFile {
  std::vector<WorkspaceFolder> folders;
  std::unordered_map<std::string, std::string> settings;  ///< global
};

/// Parse a `polyui.code-workspace` JSON blob.  Returns nullopt
/// when the document is not a JSON object or `folders` is missing.
std::optional<WorkspaceFile> ParseWorkspaceFile(const std::string &json);

/// Serialise back to JSON (stable key order: folders, settings).
std::string SerializeWorkspaceFile(const WorkspaceFile &wf);

class Workspace {
 public:
  void Load(WorkspaceFile wf);

  bool AddRoot(WorkspaceFolder folder);
  bool RemoveRoot(const std::string &name);
  const std::vector<WorkspaceFolder> &roots() const { return file_.folders; }
  const WorkspaceFolder *FindRoot(const std::string &name) const;

  /// Effective setting for `key` evaluated at `folder_name`:
  /// per-folder value, falling back to the workspace value.
  std::optional<std::string> EffectiveSetting(
      const std::string &folder_name, const std::string &key) const;

  /// True iff `path` lives under one of the roots; sets
  /// `*matched_root` to the owning folder name when non-null.
  bool ContainsPath(const std::string &path,
                    std::string *matched_root = nullptr) const;

  /// Cross-root search — returns `(folder, relative_path)` pairs
  /// for every path that contains `query`.
  struct SearchHit {
    std::string folder;
    std::string path;
  };
  std::vector<SearchHit> Search(
      const std::vector<std::pair<std::string, std::string>> &index,
      const std::string &query) const;

  WorkspaceFile snapshot() const { return file_; }

 private:
  WorkspaceFile file_;
};

/// Identifier for an LSP / DAP instance scoped to a root.  Two
/// roots that pin different versions of a language receive
/// independent instances.
struct LanguageServerKey {
  std::string folder;
  std::string language;
  std::string version;
  bool operator==(const LanguageServerKey &o) const {
    return folder == o.folder && language == o.language &&
           version == o.version;
  }
};

struct LanguageServerKeyHash {
  size_t operator()(const LanguageServerKey &k) const noexcept {
    return std::hash<std::string>()(k.folder) ^
           (std::hash<std::string>()(k.language) << 1) ^
           (std::hash<std::string>()(k.version) << 2);
  }
};

/// Pool that hands out one instance per unique
/// `(folder, language, version)`.
class LanguageServerPool {
 public:
  /// Returns the instance id for `key`, creating a fresh one when
  /// the combination has not been seen before.
  std::string Acquire(const LanguageServerKey &key);
  bool Release(const LanguageServerKey &key);
  size_t size() const { return instances_.size(); }
  std::vector<LanguageServerKey> keys() const;

 private:
  std::unordered_map<LanguageServerKey, std::string,
                     LanguageServerKeyHash> instances_;
  int next_id_{0};
};

}  // namespace polyglot::tools::ui::workspace
