/**
 * @file     git_provider.h
 * @brief    `ScmProvider` implementation that shells out to `git`
 *           (demand 2026-04-28-27 §4).
 *
 * The provider runs `git` as a subprocess and parses its porcelain /
 * blame / log output.  Process invocation is funnelled through a
 * single `RunGit(args)` helper so tests can substitute it with a
 * stub via `SetGitRunner` without touching the real filesystem.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <string>

#include "tools/ui/common/scm/scm_provider.h"

namespace polyglot::tools::ui::scm {

/// Result of a git invocation.  `exit_code == 0` means success;
/// `stdout_text` contains the captured standard output.
struct GitInvocationResult {
  int exit_code{0};
  std::string stdout_text;
};

using GitRunner = std::function<GitInvocationResult(
    const std::vector<std::string> &args, const std::string &cwd)>;

class GitProvider final : public ScmProvider {
 public:
  GitProvider(std::string repo_root, GitRunner runner)
      : root_(std::move(repo_root)), runner_(std::move(runner)) {}

  std::string Name() const override { return "git"; }

  std::vector<StatusEntry> Status() const override;
  std::vector<FileDiff> Diff(const std::string &from,
                             const std::string &to) const override;
  bool Stage(const std::string &path) override;
  bool Unstage(const std::string &path) override;
  std::string Commit(const std::string &message) override;
  std::vector<BranchInfo> Branches() const override;
  bool Checkout(const std::string &branch) override;
  std::vector<CommitInfo> Log(std::uint32_t limit) const override;
  std::vector<BlameEntry> Blame(const std::string &path) const override;

 private:
  std::string root_;
  GitRunner runner_;
};

// ── Pure parsers (exposed for unit tests) ──────────────────────────

/// Parses the output of `git status --porcelain=v1 -z` (or the
/// LF-delimited equivalent for the test fixture).
std::vector<StatusEntry> ParseGitStatus(const std::string &porcelain);

/// Parses unified-diff output (one or more files).
std::vector<FileDiff> ParseUnifiedDiff(const std::string &text);

/// Parses `git blame --porcelain` output.
std::vector<BlameEntry> ParseGitBlamePorcelain(const std::string &text);

/// Parses `git log --pretty=format:%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%s%x1f%B%x1e`.
std::vector<CommitInfo> ParseGitLog(const std::string &text);

}  // namespace polyglot::tools::ui::scm
