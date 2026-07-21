/**
 * @file     collab_provider.h
 * @brief    Code-collaboration provider abstraction (PRs, issues,
 *           reviews) shared by the GitHub, GitLab and Gitea
 *           adapters.
 *
 * The IDE never branches on the underlying forge — pull requests,
 * issues, reviews and pushes go through the same surface.  Real
 * network traffic happens in the adapter; the value model is
 * fully unit-testable through the bundled `kInMemory` adapter.
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

namespace polyglot::tools::ui::collab {

enum class CollabKind {
  kInMemory,    ///< Test/offline adapter.
  kGitHub,
  kGitLab,
  kGitea,
};

std::string CollabKindName(CollabKind k);
std::optional<CollabKind> CollabKindFromName(const std::string &name);

struct CollabConfig {
  CollabKind kind{CollabKind::kInMemory};
  std::string endpoint;        ///< API base URL.
  std::string repo_owner;
  std::string repo_name;
  std::string token;           ///< Personal access token (never logged).
};

enum class PullRequestState {
  kOpen,
  kMerged,
  kClosed,
};

std::string PullRequestStateName(PullRequestState s);

struct PullRequestSummary {
  int number{0};
  std::string title;
  std::string author;
  PullRequestState state{PullRequestState::kOpen};
  std::string head_ref;
  std::string base_ref;
  std::string url;
};

struct PullRequestDetail {
  PullRequestSummary summary;
  std::string body;
  std::vector<std::string> reviewers;
  std::vector<std::string> labels;
};

struct ReviewComment {
  int id{0};
  std::string author;
  std::string body;
  std::string file_path;
  int line{0};
};

enum class ReviewVerdict {
  kComment,
  kApprove,
  kRequestChanges,
};

std::string ReviewVerdictName(ReviewVerdict v);

struct ReviewSubmission {
  ReviewVerdict verdict{ReviewVerdict::kComment};
  std::string body;
  std::vector<ReviewComment> comments;
};

struct DiffHunk {
  std::string file_path;
  int old_start{0};
  int old_count{0};
  int new_start{0};
  int new_count{0};
  std::string body;            ///< Raw `+`/`-`/` ` prefixed lines.
};

enum class IssueState {
  kOpen,
  kClosed,
};

std::string IssueStateName(IssueState s);

struct IssueSummary {
  int number{0};
  std::string title;
  std::string author;
  IssueState state{IssueState::kOpen};
  std::vector<std::string> labels;
  std::string url;
};

struct IssueRef {
  std::string file_path;
  int line{0};
  std::string commit_sha;
};

struct PushRequest {
  std::string branch;
  std::string remote_branch;
  bool force{false};
};

struct PushResult {
  bool ok{false};
  std::string message;
  std::optional<int> created_pull_request;
};

class CollabProvider {
 public:
  virtual ~CollabProvider() = default;
  const CollabConfig &config() const { return config_; }

  // Pull requests.
  virtual std::vector<PullRequestSummary> ListPullRequests(
      PullRequestState filter) = 0;
  virtual std::optional<PullRequestDetail> GetPullRequest(int number) = 0;
  virtual std::vector<DiffHunk> GetPullRequestDiff(int number) = 0;
  virtual std::vector<ReviewComment> ListReviewComments(int number) = 0;
  virtual bool SubmitReview(int number, const ReviewSubmission &review) = 0;
  virtual PushResult PushBranch(const PushRequest &req) = 0;

  // Issues.
  virtual std::vector<IssueSummary> ListIssues(IssueState filter) = 0;
  virtual IssueSummary CreateIssue(const std::string &title,
                                   const std::string &body,
                                   const std::vector<std::string> &labels) = 0;
  virtual bool LinkCommit(int issue_number,
                          const std::string &commit_sha) = 0;
  virtual bool AttachFileReference(int issue_number,
                                   const IssueRef &ref) = 0;

 protected:
  CollabConfig config_{};
};

std::unique_ptr<CollabProvider> CreateCollabProvider(CollabConfig cfg);

}  // namespace polyglot::tools::ui::collab
