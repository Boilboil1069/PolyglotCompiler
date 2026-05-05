/**
 * @file     collab_provider_test.cpp
 * @brief    Unit tests for the collaboration provider.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/collab/collab_provider.h"

using namespace polyglot::tools::ui::collab;

TEST_CASE("Kind names round-trip", "[polyui][collab]") {
  for (auto k : {CollabKind::kInMemory, CollabKind::kGitHub,
                 CollabKind::kGitLab, CollabKind::kGitea})
    CHECK(*CollabKindFromName(CollabKindName(k)) == k);
  CHECK_FALSE(CollabKindFromName("nope"));
  CHECK(PullRequestStateName(PullRequestState::kMerged) == "merged");
  CHECK(IssueStateName(IssueState::kClosed) == "closed");
  CHECK(ReviewVerdictName(ReviewVerdict::kRequestChanges) ==
        "request-changes");
}

TEST_CASE("In-memory PR review flow", "[polyui][collab]") {
  CollabConfig cfg;
  cfg.kind = CollabKind::kInMemory;
  auto p = CreateCollabProvider(cfg);
  REQUIRE(p);

  auto open = p->ListPullRequests(PullRequestState::kOpen);
  REQUIRE(open.size() == 1);
  CHECK(open.front().number == 1);

  auto detail = p->GetPullRequest(1);
  REQUIRE(detail);
  CHECK(detail->summary.title.find("seed") != std::string::npos);

  auto diff = p->GetPullRequestDiff(1);
  REQUIRE(diff.size() == 1);
  CHECK(diff.front().file_path == "src/main.ploy");

  ReviewSubmission rs;
  rs.verdict = ReviewVerdict::kComment;
  rs.body = "lgtm";
  ReviewComment c;
  c.body = "rename here";
  c.file_path = "src/main.ploy";
  c.line = 1;
  rs.comments.push_back(c);
  CHECK(p->SubmitReview(1, rs));
  auto comments = p->ListReviewComments(1);
  REQUIRE(comments.size() == 1);
  CHECK(comments.front().author == "you");
  CHECK(comments.front().id == 1);
}

TEST_CASE("Push to PR creates a new pull request",
          "[polyui][collab]") {
  CollabConfig cfg;
  cfg.kind = CollabKind::kInMemory;
  auto p = CreateCollabProvider(cfg);
  PushRequest req;
  req.branch = "feature/x";
  req.remote_branch = "feature/x";
  auto r = p->PushBranch(req);
  CHECK(r.ok);
  REQUIRE(r.created_pull_request);
  auto detail = p->GetPullRequest(*r.created_pull_request);
  REQUIRE(detail);
  CHECK(detail->summary.head_ref == "feature/x");
  CHECK(detail->summary.url.find("memory://pr/") == 0);
}

TEST_CASE("Issue lifecycle: create / link / reference",
          "[polyui][collab]") {
  CollabConfig cfg;
  cfg.kind = CollabKind::kInMemory;
  auto p = CreateCollabProvider(cfg);
  auto issue = p->CreateIssue("crash on startup", "stack trace below",
                              {"bug", "p1"});
  CHECK(issue.number == 1);
  CHECK(issue.state == IssueState::kOpen);
  CHECK(issue.labels.size() == 2);

  CHECK(p->LinkCommit(issue.number, "abc1234"));
  CHECK_FALSE(p->LinkCommit(99, "abc1234"));

  IssueRef ref;
  ref.file_path = "src/main.ploy";
  ref.line = 42;
  ref.commit_sha = "abc1234";
  CHECK(p->AttachFileReference(issue.number, ref));
  CHECK_FALSE(p->AttachFileReference(99, ref));

  auto open = p->ListIssues(IssueState::kOpen);
  REQUIRE(open.size() == 1);
  CHECK(open.front().title == "crash on startup");
}

TEST_CASE("Forge adapters route through the right base URL",
          "[polyui][collab]") {
  for (auto k : {CollabKind::kGitHub, CollabKind::kGitLab,
                 CollabKind::kGitea}) {
    CollabConfig cfg;
    cfg.kind = k;
    cfg.repo_owner = "polyglot";
    cfg.repo_name = "compiler";
    cfg.token = "tok";
    auto p = CreateCollabProvider(cfg);
    REQUIRE(p);
    PushRequest req;
    req.branch = "main";
    auto r = p->PushBranch(req);
    CHECK(r.ok);
    if (k == CollabKind::kGitHub)
      CHECK(r.message.find("api.github.com") != std::string::npos);
    if (k == CollabKind::kGitLab)
      CHECK(r.message.find("gitlab.com") != std::string::npos);
    if (k == CollabKind::kGitea)
      CHECK(r.message.find("gitea.com") != std::string::npos);
  }
}

TEST_CASE("Forge adapters refuse writes without a token",
          "[polyui][collab]") {
  CollabConfig cfg;
  cfg.kind = CollabKind::kGitHub;
  cfg.repo_owner = "polyglot";
  cfg.repo_name = "compiler";
  auto p = CreateCollabProvider(cfg);
  PushRequest req;
  req.branch = "main";
  auto r = p->PushBranch(req);
  CHECK_FALSE(r.ok);
  CHECK(r.message == "missing token");
  CHECK_FALSE(p->LinkCommit(1, "sha"));
  CHECK_FALSE(p->SubmitReview(1, ReviewSubmission{}));
}
