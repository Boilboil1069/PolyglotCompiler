/**
 * @file     git_provider.cpp
 * @brief    Implementation of `GitProvider` and its parsers.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/scm/git_provider.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace polyglot::tools::ui::scm {

namespace {

FileStatus DecodePorcelainCode(char c) {
  switch (c) {
    case 'A': return FileStatus::kAdded;
    case 'M': return FileStatus::kModified;
    case 'D': return FileStatus::kDeleted;
    case 'R': return FileStatus::kRenamed;
    case 'U': return FileStatus::kConflicted;
    case '?': return FileStatus::kUntracked;
    case ' ': return FileStatus::kUnchanged;
    default:  return FileStatus::kUnchanged;
  }
}

std::vector<std::string> SplitLines(const std::string &s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < s.size()) out.emplace_back(s.substr(start));
  return out;
}

}  // namespace

std::vector<StatusEntry> ParseGitStatus(const std::string &porcelain) {
  std::vector<StatusEntry> out;
  for (const auto &raw : SplitLines(porcelain)) {
    if (raw.size() < 3) continue;
    StatusEntry e;
    e.index_status = DecodePorcelainCode(raw[0]);
    e.worktree_status = DecodePorcelainCode(raw[1]);
    std::string rest = raw.substr(3);
    auto sep = rest.find(" -> ");
    if (sep != std::string::npos) {
      e.rename_from = rest.substr(0, sep);
      e.path = rest.substr(sep + 4);
    } else {
      e.path = rest;
    }
    if (raw[0] == '?' && raw[1] == '?') {
      e.index_status = FileStatus::kUntracked;
      e.worktree_status = FileStatus::kUntracked;
    }
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<FileDiff> ParseUnifiedDiff(const std::string &text) {
  std::vector<FileDiff> files;
  FileDiff *cur_file = nullptr;
  DiffHunk *cur_hunk = nullptr;
  for (const auto &line : SplitLines(text)) {
    if (line.rfind("diff --git", 0) == 0) {
      files.emplace_back();
      cur_file = &files.back();
      cur_hunk = nullptr;
    } else if (line.rfind("--- ", 0) == 0 && cur_file) {
      cur_file->old_path = line.size() > 6 ? line.substr(6) : "";
    } else if (line.rfind("+++ ", 0) == 0 && cur_file) {
      cur_file->new_path = line.size() > 6 ? line.substr(6) : "";
    } else if (line.rfind("@@", 0) == 0 && cur_file) {
      cur_file->hunks.emplace_back();
      cur_hunk = &cur_file->hunks.back();
      cur_hunk->header = line;
      // Parse `@@ -old_start,old_count +new_start,new_count @@`.
      auto parse = [](const std::string &tok, std::uint32_t &start,
                      std::uint32_t &count) {
        std::size_t comma = tok.find(',');
        try {
          start = static_cast<std::uint32_t>(std::stoul(tok.substr(1, comma - 1)));
          count = comma == std::string::npos
                      ? 1u
                      : static_cast<std::uint32_t>(std::stoul(tok.substr(comma + 1)));
        } catch (...) { start = 0; count = 0; }
      };
      std::istringstream is(line);
      std::string atat, oldtok, newtok;
      is >> atat >> oldtok >> newtok;
      if (!oldtok.empty() && oldtok[0] == '-')
        parse(oldtok, cur_hunk->old_start, cur_hunk->old_count);
      if (!newtok.empty() && newtok[0] == '+')
        parse(newtok, cur_hunk->new_start, cur_hunk->new_count);
    } else if (cur_hunk && (line.empty() || line[0] == ' ' || line[0] == '+' ||
                            line[0] == '-')) {
      cur_hunk->lines.push_back(line);
    }
  }
  return files;
}

std::vector<BlameEntry> ParseGitBlamePorcelain(const std::string &text) {
  std::vector<BlameEntry> out;
  auto lines = SplitLines(text);
  BlameEntry pending;
  bool in_block = false;
  for (const auto &raw : lines) {
    if (raw.empty()) continue;
    if (raw[0] == '\t') {
      // Source line — terminates the current block.
      if (in_block) {
        out.push_back(pending);
        in_block = false;
        pending = {};
      }
      continue;
    }
    if (!in_block) {
      // Header: `<sha> <orig_line> <final_line> <count>`.
      std::istringstream is(raw);
      std::string sha;
      std::uint32_t orig, final_line, count;
      is >> sha >> orig >> final_line >> count;
      pending.commit_id = sha;
      pending.short_id = sha.substr(0, 7);
      pending.line = final_line > 0 ? final_line - 1 : 0;
      in_block = true;
      continue;
    }
    auto sp = raw.find(' ');
    std::string key = sp == std::string::npos ? raw : raw.substr(0, sp);
    std::string val = sp == std::string::npos ? "" : raw.substr(sp + 1);
    if (key == "author") pending.author = val;
    else if (key == "author-time") pending.date = val;
    else if (key == "summary") pending.summary = val;
  }
  if (in_block) out.push_back(pending);
  return out;
}

std::vector<CommitInfo> ParseGitLog(const std::string &text) {
  std::vector<CommitInfo> out;
  // Records separated by 0x1e, fields by 0x1f.
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\x1e', start);
    std::string rec = text.substr(
        start, end == std::string::npos ? text.size() - start : end - start);
    if (!rec.empty() && rec.front() == '\n') rec.erase(0, 1);
    if (!rec.empty()) {
      std::vector<std::string> fields;
      std::size_t fs = 0;
      while (fs <= rec.size()) {
        std::size_t fe = rec.find('\x1f', fs);
        fields.emplace_back(rec.substr(
            fs, fe == std::string::npos ? rec.size() - fs : fe - fs));
        if (fe == std::string::npos) break;
        fs = fe + 1;
      }
      if (fields.size() >= 6) {
        CommitInfo c;
        c.id = fields[0];
        c.short_id = fields[1];
        c.author = fields[2];
        c.email = fields[3];
        c.date = fields[4];
        c.title = fields[5];
        c.message = fields.size() > 6 ? fields[6] : c.title;
        out.push_back(std::move(c));
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

// ── GitProvider methods (thin wrappers over RunGit) ───────────────

std::vector<StatusEntry> GitProvider::Status() const {
  auto r = runner_({"status", "--porcelain=v1"}, root_);
  if (r.exit_code != 0) return {};
  return ParseGitStatus(r.stdout_text);
}

std::vector<FileDiff> GitProvider::Diff(const std::string &from,
                                        const std::string &to) const {
  std::vector<std::string> args{"diff", "--unified=3"};
  if (!from.empty()) args.push_back(from);
  if (!to.empty()) args.push_back(to);
  auto r = runner_(args, root_);
  if (r.exit_code != 0) return {};
  return ParseUnifiedDiff(r.stdout_text);
}

bool GitProvider::Stage(const std::string &path) {
  return runner_({"add", "--", path}, root_).exit_code == 0;
}

bool GitProvider::Unstage(const std::string &path) {
  return runner_({"reset", "HEAD", "--", path}, root_).exit_code == 0;
}

std::string GitProvider::Commit(const std::string &message) {
  if (runner_({"commit", "-m", message}, root_).exit_code != 0) return {};
  auto r = runner_({"rev-parse", "HEAD"}, root_);
  std::string head = r.stdout_text;
  while (!head.empty() && (head.back() == '\n' || head.back() == ' '))
    head.pop_back();
  return head;
}

std::vector<BranchInfo> GitProvider::Branches() const {
  auto r = runner_({"branch", "--list", "--all", "-vv"}, root_);
  if (r.exit_code != 0) return {};
  std::vector<BranchInfo> out;
  for (const auto &line : SplitLines(r.stdout_text)) {
    if (line.size() < 3) continue;
    BranchInfo b;
    b.current = line[0] == '*';
    std::size_t i = 2;
    while (i < line.size() && line[i] == ' ') ++i;
    std::size_t name_end = line.find(' ', i);
    b.name = line.substr(i, name_end - i);
    out.push_back(std::move(b));
  }
  return out;
}

bool GitProvider::Checkout(const std::string &branch) {
  return runner_({"checkout", branch}, root_).exit_code == 0;
}

std::vector<CommitInfo> GitProvider::Log(std::uint32_t limit) const {
  auto r = runner_({"log",
                    "--pretty=format:%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%s%x1f%B%x1e",
                    "-n", std::to_string(limit), "--date=iso"},
                   root_);
  if (r.exit_code != 0) return {};
  return ParseGitLog(r.stdout_text);
}

std::vector<BlameEntry> GitProvider::Blame(const std::string &path) const {
  auto r = runner_({"blame", "--porcelain", "--", path}, root_);
  if (r.exit_code != 0) return {};
  return ParseGitBlamePorcelain(r.stdout_text);
}

}  // namespace polyglot::tools::ui::scm
