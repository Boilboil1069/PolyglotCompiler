# Code Collaboration — Pull Requests, Reviews, Issues

## Goal

Bring pull-request review, branch push, issue management and
commit / file-line linking into the IDE behind one provider
abstraction shared by GitHub, GitLab and Gitea.  The IDE never
branches on the underlying forge; switching hosts is a config
change.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `CollabProvider` | [`tools/ui/common/collab/collab_provider.h`](../../tools/ui/common/collab/collab_provider.h) | Abstract surface: `ListPullRequests`, `GetPullRequest`, `GetPullRequestDiff`, `ListReviewComments`, `SubmitReview`, `PushBranch`, `ListIssues`, `CreateIssue`, `LinkCommit`, `AttachFileReference`. |
| `InMemoryProvider` | same | Deterministic adapter for tests and offline use; seeds one PR with one diff hunk. |
| `ForgeAdapter` | same | Shared envelope for the GitHub / GitLab / Gitea adapters; routes URLs against the right base (`api.github.com` / `gitlab.com/api/v4` / `gitea.com/api/v1`) and refuses writes when no token is configured. |
| Value types | same | `PullRequestSummary` / `PullRequestDetail` / `DiffHunk` / `ReviewComment` / `ReviewSubmission` / `IssueSummary` / `IssueRef` / `PushRequest` / `PushResult`. |

## Pipelines

* **PR review.** The PR list is fetched via `ListPullRequests`,
  the diff hunks via `GetPullRequestDiff`, existing review
  comments via `ListReviewComments`, and the user's review (with
  per-line comments and an `Approve` / `RequestChanges` /
  `Comment` verdict) is sent through `SubmitReview`.
* **Push to PR.** `PushBranch` pushes a local branch and, when the
  in-memory adapter is in use, opens a draft PR automatically so
  tests can exercise the full create-and-review path without
  network access.  Forge adapters require a configured token; the
  push fails fast otherwise.
* **Issue tracking.** `CreateIssue` returns the new
  `IssueSummary`; `LinkCommit(issue_number, sha)` and
  `AttachFileReference(issue_number, IssueRef)` cross-link the
  issue with commits and `file:line` references the user pulls
  from the editor.

## Tests

* [`tests/unit/polyui/collab_provider_test.cpp`](../../tests/unit/polyui/collab_provider_test.cpp)
  — kind-name round-trips and state-name coverage; the in-memory
  PR review flow (list → detail → diff → submit → comments
  appended); push-to-PR creating a new pull request; the issue
  lifecycle (create → link commit → attach file reference); forge
  adapters routing to the right base URL; forge adapters refusing
  writes without a token.

The full polyui suite runs at 986 assertions across 199 cases.
