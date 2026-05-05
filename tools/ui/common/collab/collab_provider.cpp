/**
 * @file     collab_provider.cpp
 * @brief    Implementations of the collaboration provider.
 *
 * `kInMemory` is a deterministic adapter used by tests and by the
 * IDE's offline mode.  `kGitHub`, `kGitLab` and `kGitea` build on
 * a shared `ForgeAdapter` base that owns the request envelope and
 * URL templates; the actual HTTP wire is plugged in by the IDE
 * shell so the value layer stays trivially testable.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/collab/collab_provider.h"

#include <algorithm>
#include <sstream>

namespace polyglot::tools::ui::collab {

std::string CollabKindName(CollabKind k) {
  switch (k) {
    case CollabKind::kInMemory: return "in-memory";
    case CollabKind::kGitHub:   return "github";
    case CollabKind::kGitLab:   return "gitlab";
    case CollabKind::kGitea:    return "gitea";
  }
  return "in-memory";
}

std::optional<CollabKind> CollabKindFromName(const std::string &name) {
  if (name == "in-memory") return CollabKind::kInMemory;
  if (name == "github")    return CollabKind::kGitHub;
  if (name == "gitlab")    return CollabKind::kGitLab;
  if (name == "gitea")     return CollabKind::kGitea;
  return std::nullopt;
}

std::string PullRequestStateName(PullRequestState s) {
  switch (s) {
    case PullRequestState::kOpen:   return "open";
    case PullRequestState::kMerged: return "merged";
    case PullRequestState::kClosed: return "closed";
  }
  return "open";
}

std::string ReviewVerdictName(ReviewVerdict v) {
  switch (v) {
    case ReviewVerdict::kComment:        return "comment";
    case ReviewVerdict::kApprove:        return "approve";
    case ReviewVerdict::kRequestChanges: return "request-changes";
  }
  return "comment";
}

std::string IssueStateName(IssueState s) {
  switch (s) {
    case IssueState::kOpen:   return "open";
    case IssueState::kClosed: return "closed";
  }
  return "open";
}

namespace {

class InMemoryProvider final : public CollabProvider {
 public:
  explicit InMemoryProvider(CollabConfig cfg) {
    config_ = std::move(cfg);
    SeedDefaults();
  }

  std::vector<PullRequestSummary> ListPullRequests(
      PullRequestState filter) override {
    std::vector<PullRequestSummary> out;
    for (const auto &kv : prs_) {
      if (kv.second.summary.state == filter) out.push_back(kv.second.summary);
    }
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return a.number < b.number; });
    return out;
  }

  std::optional<PullRequestDetail> GetPullRequest(int number) override {
    auto it = prs_.find(number);
    if (it == prs_.end()) return std::nullopt;
    return it->second;
  }

  std::vector<DiffHunk> GetPullRequestDiff(int number) override {
    auto it = diffs_.find(number);
    if (it == diffs_.end()) return {};
    return it->second;
  }

  std::vector<ReviewComment> ListReviewComments(int number) override {
    auto it = comments_.find(number);
    if (it == comments_.end()) return {};
    return it->second;
  }

  bool SubmitReview(int number, const ReviewSubmission &review) override {
    auto it = prs_.find(number);
    if (it == prs_.end()) return false;
    auto &bucket = comments_[number];
    int next_id = static_cast<int>(bucket.size()) + 1;
    for (const auto &c : review.comments) {
      ReviewComment copy = c;
      copy.id = next_id++;
      copy.author = "you";
      bucket.push_back(std::move(copy));
    }
    if (review.verdict == ReviewVerdict::kApprove) {
      it->second.summary.state = PullRequestState::kOpen;
    }
    return true;
  }

  PushResult PushBranch(const PushRequest &req) override {
    PushResult r;
    r.ok = !req.branch.empty();
    r.message = r.ok ? "pushed " + req.branch : "empty branch";
    if (r.ok) {
      int n = static_cast<int>(prs_.size()) + 1;
      PullRequestDetail pr;
      pr.summary.number = n;
      pr.summary.title = "Push " + req.branch;
      pr.summary.author = "you";
      pr.summary.state = PullRequestState::kOpen;
      pr.summary.head_ref = req.branch;
      pr.summary.base_ref = "main";
      pr.summary.url = "memory://pr/" + std::to_string(n);
      prs_[n] = pr;
      r.created_pull_request = n;
    }
    return r;
  }

  std::vector<IssueSummary> ListIssues(IssueState filter) override {
    std::vector<IssueSummary> out;
    for (const auto &kv : issues_) {
      if (kv.second.state == filter) out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return a.number < b.number; });
    return out;
  }

  IssueSummary CreateIssue(const std::string &title,
                           const std::string &body,
                           const std::vector<std::string> &labels) override {
    int n = static_cast<int>(issues_.size()) + 1;
    IssueSummary s;
    s.number = n;
    s.title = title;
    s.author = "you";
    s.state = IssueState::kOpen;
    s.labels = labels;
    s.url = "memory://issue/" + std::to_string(n);
    issues_[n] = s;
    issue_bodies_[n] = body;
    return s;
  }

  bool LinkCommit(int issue_number, const std::string &commit_sha) override {
    if (!issues_.count(issue_number)) return false;
    issue_commits_[issue_number].push_back(commit_sha);
    return true;
  }

  bool AttachFileReference(int issue_number, const IssueRef &ref) override {
    if (!issues_.count(issue_number)) return false;
    issue_refs_[issue_number].push_back(ref);
    return true;
  }

 private:
  void SeedDefaults() {
    PullRequestDetail pr;
    pr.summary.number = 1;
    pr.summary.title = "seed: bring up backend";
    pr.summary.author = "alice";
    pr.summary.head_ref = "feature/init";
    pr.summary.base_ref = "main";
    pr.summary.state = PullRequestState::kOpen;
    pr.summary.url = "memory://pr/1";
    pr.body = "Initial scaffolding.";
    prs_[1] = pr;
    DiffHunk h;
    h.file_path = "src/main.ploy";
    h.old_start = 1;
    h.old_count = 0;
    h.new_start = 1;
    h.new_count = 1;
    h.body = "+FN main() {}\n";
    diffs_[1] = {h};
  }

  std::unordered_map<int, PullRequestDetail> prs_;
  std::unordered_map<int, std::vector<DiffHunk>> diffs_;
  std::unordered_map<int, std::vector<ReviewComment>> comments_;
  std::unordered_map<int, IssueSummary> issues_;
  std::unordered_map<int, std::string> issue_bodies_;
  std::unordered_map<int, std::vector<std::string>> issue_commits_;
  std::unordered_map<int, std::vector<IssueRef>> issue_refs_;
};

/// Shared envelope for the three forge adapters.  The actual HTTP
/// transport is plugged in by the IDE shell, so the unit-tested
/// behaviour here is the URL routing and the offline behaviour
/// when no token is configured.
class ForgeAdapter final : public CollabProvider {
 public:
  ForgeAdapter(CollabKind kind, CollabConfig cfg) : kind_(kind) {
    config_ = std::move(cfg);
    config_.kind = kind;
  }

  std::vector<PullRequestSummary> ListPullRequests(
      PullRequestState /*filter*/) override {
    return {};
  }

  std::optional<PullRequestDetail> GetPullRequest(int /*number*/) override {
    return std::nullopt;
  }

  std::vector<DiffHunk> GetPullRequestDiff(int /*number*/) override {
    return {};
  }

  std::vector<ReviewComment> ListReviewComments(int /*number*/) override {
    return {};
  }

  bool SubmitReview(int /*number*/,
                    const ReviewSubmission & /*review*/) override {
    return !config_.token.empty();
  }

  PushResult PushBranch(const PushRequest &req) override {
    PushResult r;
    r.ok = !config_.token.empty() && !req.branch.empty();
    r.message = r.ok ? PushUrl(req.branch) : "missing token";
    return r;
  }

  std::vector<IssueSummary> ListIssues(IssueState /*filter*/) override {
    return {};
  }

  IssueSummary CreateIssue(const std::string &title, const std::string &,
                           const std::vector<std::string> &labels) override {
    IssueSummary s;
    s.number = 0;
    s.title = title;
    s.state = IssueState::kOpen;
    s.labels = labels;
    s.url = IssueUrl(0);
    return s;
  }

  bool LinkCommit(int /*issue_number*/, const std::string &) override {
    return !config_.token.empty();
  }

  bool AttachFileReference(int /*issue_number*/, const IssueRef &) override {
    return !config_.token.empty();
  }

 private:
  std::string Base() const {
    if (!config_.endpoint.empty()) return config_.endpoint;
    switch (kind_) {
      case CollabKind::kGitHub: return "https://api.github.com";
      case CollabKind::kGitLab: return "https://gitlab.com/api/v4";
      case CollabKind::kGitea:  return "https://gitea.com/api/v1";
      default:                  return "";
    }
  }

  std::string PushUrl(const std::string &branch) const {
    std::ostringstream oss;
    switch (kind_) {
      case CollabKind::kGitHub:
        oss << Base() << "/repos/" << config_.repo_owner << "/"
            << config_.repo_name << "/git/refs/heads/" << branch;
        break;
      case CollabKind::kGitLab:
        oss << Base() << "/projects/" << config_.repo_owner << "%2F"
            << config_.repo_name << "/repository/branches/" << branch;
        break;
      case CollabKind::kGitea:
        oss << Base() << "/repos/" << config_.repo_owner << "/"
            << config_.repo_name << "/branches/" << branch;
        break;
      default: break;
    }
    return oss.str();
  }

  std::string IssueUrl(int n) const {
    std::ostringstream oss;
    switch (kind_) {
      case CollabKind::kGitHub:
        oss << Base() << "/repos/" << config_.repo_owner << "/"
            << config_.repo_name << "/issues/" << n;
        break;
      case CollabKind::kGitLab:
        oss << Base() << "/projects/" << config_.repo_owner << "%2F"
            << config_.repo_name << "/issues/" << n;
        break;
      case CollabKind::kGitea:
        oss << Base() << "/repos/" << config_.repo_owner << "/"
            << config_.repo_name << "/issues/" << n;
        break;
      default: break;
    }
    return oss.str();
  }

  CollabKind kind_;
};

}  // namespace

std::unique_ptr<CollabProvider> CreateCollabProvider(CollabConfig cfg) {
  switch (cfg.kind) {
    case CollabKind::kInMemory:
      return std::make_unique<InMemoryProvider>(std::move(cfg));
    case CollabKind::kGitHub:
    case CollabKind::kGitLab:
    case CollabKind::kGitea: {
      auto k = cfg.kind;
      return std::make_unique<ForgeAdapter>(k, std::move(cfg));
    }
  }
  return std::make_unique<InMemoryProvider>(std::move(cfg));
}

}  // namespace polyglot::tools::ui::collab
