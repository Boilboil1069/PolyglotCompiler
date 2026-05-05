# 代码协作 —— Pull Request、评审、Issue

## 目标

把 PR 评审、分支 push、issue 管理与 commit / 文件行链接，统一
在 IDE 内的一套 provider 抽象之后，由 GitHub、GitLab、Gitea 三
家共享同一接口。IDE 永远不在 forge 层面分支；切换托管方仅是
配置变更。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `CollabProvider` | [`tools/ui/common/collab/collab_provider.h`](../../tools/ui/common/collab/collab_provider.h) | 抽象接口：`ListPullRequests`、`GetPullRequest`、`GetPullRequestDiff`、`ListReviewComments`、`SubmitReview`、`PushBranch`、`ListIssues`、`CreateIssue`、`LinkCommit`、`AttachFileReference`。 |
| `InMemoryProvider` | 同上 | 确定性适配器，供测试与离线使用；预置一个 PR 与一个 diff hunk。 |
| `ForgeAdapter` | 同上 | GitHub / GitLab / Gitea 三家共用的信封；根据 kind 选择 base URL（`api.github.com` / `gitlab.com/api/v4` / `gitea.com/api/v1`），未配置 token 时直接拒绝写入。 |
| 值类型 | 同上 | `PullRequestSummary` / `PullRequestDetail` / `DiffHunk` / `ReviewComment` / `ReviewSubmission` / `IssueSummary` / `IssueRef` / `PushRequest` / `PushResult`。 |

## 流程

* **PR 评审。** PR 列表来自 `ListPullRequests`；diff 来自
  `GetPullRequestDiff`；既有评论来自 `ListReviewComments`；用户
  本次评审（含逐行评论与 `Approve` / `RequestChanges` /
  `Comment` 结论）通过 `SubmitReview` 提交。
* **Push 到 PR。** `PushBranch` 推送本地分支；内存适配器会自动
  开一个 draft PR，让测试无需网络也能跑完"推送→评审"的完整链
  路。Forge 适配器要求配置 token，否则直接失败。
* **Issue 跟踪。** `CreateIssue` 返回新的 `IssueSummary`；
  `LinkCommit(issue_number, sha)` 与
  `AttachFileReference(issue_number, IssueRef)` 把 issue 与
  commit、编辑器中拉取的 `file:line` 引用相互交叉链接。

## 测试

* [`tests/unit/polyui/collab_provider_test.cpp`](../../tests/unit/polyui/collab_provider_test.cpp)
  ——kind 名往返与状态名覆盖；内存 PR 评审流（列表 → 详情 →
  diff → 提交 → 评论入库）；Push 到 PR 自动开新 PR；issue 生命
  周期（创建 → 关联 commit → 挂文件引用）；forge 适配器路由到
  正确 base URL；无 token 时 forge 适配器拒绝写入。

polyui 全套合计 199 例 986 条断言全部通过。
