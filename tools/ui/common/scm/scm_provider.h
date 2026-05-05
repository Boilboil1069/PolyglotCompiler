/**
 * @file     scm_provider.h
 * @brief    Source-Control-Management abstraction
 *           (demand 2026-04-28-27 §4).
 *
 * Provides a backend-agnostic interface so the IDE can talk to Git
 * today and to Mercurial / Subversion tomorrow without leaking
 * provider-specific shapes into the UI layer.
 *
 * The interface is intentionally synchronous + Qt-free; the IDE
 * shell wraps each call in a worker `QFuture` to keep the GUI thread
 * responsive.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::scm {

enum class FileStatus {
  kUnchanged,
  kAdded,
  kModified,
  kDeleted,
  kRenamed,
  kUntracked,
  kConflicted,
};

struct StatusEntry {
  std::string path;
  FileStatus index_status{FileStatus::kUnchanged};   ///< staged
  FileStatus worktree_status{FileStatus::kUnchanged};
  std::optional<std::string> rename_from;
};

struct CommitInfo {
  std::string id;          ///< full hash / revision id
  std::string short_id;    ///< 7-char abbreviated id
  std::string author;
  std::string email;
  std::string date;        ///< ISO-8601
  std::string title;       ///< first line of message
  std::string message;     ///< full message
};

struct BlameEntry {
  std::uint32_t line{0};   ///< 0-based file line
  std::string commit_id;
  std::string short_id;
  std::string author;
  std::string date;
  std::string summary;
};

struct DiffHunk {
  std::uint32_t old_start{0};
  std::uint32_t old_count{0};
  std::uint32_t new_start{0};
  std::uint32_t new_count{0};
  std::string header;            ///< raw `@@ … @@` header
  std::vector<std::string> lines;///< prefixed by ' ' / '+' / '-'
};

struct FileDiff {
  std::string old_path;
  std::string new_path;
  std::vector<DiffHunk> hunks;
};

struct BranchInfo {
  std::string name;
  bool current{false};
  std::optional<std::string> upstream;
};

/// Pure-virtual SCM provider interface.  Each method returns an
/// `std::optional` (or empty container) on failure so callers can
/// distinguish success from "no data" without exception handling.
class ScmProvider {
 public:
  virtual ~ScmProvider() = default;

  /// Provider identifier (e.g. `"git"`, `"hg"`, `"svn"`).
  virtual std::string Name() const = 0;

  /// Working-tree status.
  virtual std::vector<StatusEntry> Status() const = 0;

  /// Diff between `from` revision and the working tree (or between
  /// `from` and `to` when `to` is non-empty).  Pass an empty `from`
  /// for "unstaged changes".
  virtual std::vector<FileDiff> Diff(const std::string &from,
                                     const std::string &to) const = 0;

  /// Stage / unstage / commit.  Return value is the new HEAD id (for
  /// `Commit`) or empty string for the staging operations.
  virtual bool Stage(const std::string &path) = 0;
  virtual bool Unstage(const std::string &path) = 0;
  virtual std::string Commit(const std::string &message) = 0;

  /// Branch operations.
  virtual std::vector<BranchInfo> Branches() const = 0;
  virtual bool Checkout(const std::string &branch) = 0;

  /// Revision history (newest first).
  virtual std::vector<CommitInfo> Log(std::uint32_t limit) const = 0;

  /// Per-line blame for `path`.
  virtual std::vector<BlameEntry> Blame(const std::string &path) const = 0;
};

/// Captures the stub data a fake provider should return.  Useful for
/// unit testing the IDE layer without spawning a real `git` process.
struct InMemoryScmFixture {
  std::string name{"in-memory"};
  std::vector<StatusEntry> status;
  std::vector<FileDiff> diff;
  std::vector<BranchInfo> branches;
  std::vector<CommitInfo> log;
  std::vector<BlameEntry> blame;
};

/// Provider backed by an `InMemoryScmFixture`.  All mutating
/// operations are recorded and visible via `staged()` / `commits()`
/// for assertions.
class InMemoryScmProvider final : public ScmProvider {
 public:
  explicit InMemoryScmProvider(InMemoryScmFixture fx)
      : fx_(std::move(fx)) {}

  std::string Name() const override { return fx_.name; }
  std::vector<StatusEntry> Status() const override { return fx_.status; }
  std::vector<FileDiff> Diff(const std::string &, const std::string &) const override {
    return fx_.diff;
  }
  bool Stage(const std::string &p) override { staged_.push_back(p); return true; }
  bool Unstage(const std::string &p) override { unstaged_.push_back(p); return true; }
  std::string Commit(const std::string &m) override {
    commits_.push_back(m);
    return "deadbeef";
  }
  std::vector<BranchInfo> Branches() const override { return fx_.branches; }
  bool Checkout(const std::string &b) override { checked_out_ = b; return true; }
  std::vector<CommitInfo> Log(std::uint32_t limit) const override {
    auto out = fx_.log;
    if (limit < out.size()) out.resize(limit);
    return out;
  }
  std::vector<BlameEntry> Blame(const std::string &) const override {
    return fx_.blame;
  }

  const std::vector<std::string> &staged() const { return staged_; }
  const std::vector<std::string> &unstaged() const { return unstaged_; }
  const std::vector<std::string> &commits() const { return commits_; }
  const std::string &checked_out() const { return checked_out_; }

 private:
  InMemoryScmFixture fx_;
  std::vector<std::string> staged_;
  std::vector<std::string> unstaged_;
  std::vector<std::string> commits_;
  std::string checked_out_;
};

}  // namespace polyglot::tools::ui::scm
