# SCM, diff, blame & merge resolver (demand 2026-04-28-27)

## Goal

Bring source-control workflows to first-class IDE quality:

* **SCM provider abstraction** so Git, Mercurial and Subversion share
  a single set of view-models.
* **Diff engine** with side-by-side / inline display, hunk-level
  stage / unstage, and editor coexistence (LSP hovers and go-to
  still work inside diff regions).
* **Blame** in the gutter and on hover.
* **Merge conflict resolver** with Accept current / incoming / both
  and free-form editing.

## Components

| Concern                  | File                                                                                              | Responsibility                                                            |
|--------------------------|---------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------|
| Provider interface       | [`tools/ui/common/scm/scm_provider.{h,cpp}`](../../tools/ui/common/scm/scm_provider.h)            | `ScmProvider` + `InMemoryScmProvider` test double.                        |
| Git backend              | [`tools/ui/common/scm/git_provider.{h,cpp}`](../../tools/ui/common/scm/git_provider.h)            | `GitProvider` + porcelain / blame / log parsers.                          |
| Diff engine              | [`tools/ui/common/scm/diff_engine.{h,cpp}`](../../tools/ui/common/scm/diff_engine.h)              | `DiffLines`, `BuildHunks`, `ApplyHunk`, `RevertHunk`.                     |
| Merge resolver           | [`tools/ui/common/scm/merge_resolver.{h,cpp}`](../../tools/ui/common/scm/merge_resolver.h)        | `ParseMergeConflicts`, `ResolveConflicts`, custom resolutions.            |

## Pipelines

### Diff view

```
"Compare with HEAD" ──► provider.Diff("HEAD", "")
                              │
                              ▼
                       FileDiff[] (parsed unified diff)
                              │
                              ▼
                  inline / side-by-side renderer
                              │
                              ▼
   user clicks "Stage hunk" ──► provider.Stage(file)
   user clicks "Revert hunk" ──► RevertHunk(text, hunk) → write back
```

### Blame

```
file opened ──► provider.Blame(path)
                       │
                       ▼
                BlameEntry[] (commit_id, short_id, author, date, summary)
                       │
                       ▼
              gutter renders short_id + author
              hover popup shows full message; "Open commit" → log panel
```

### Merge resolver

```
file with `<<<<<<<` markers
        │
        ▼
ParseMergeConflicts(text) → MergeConflict[]
        │                      (current_label, incoming_label,
        │                       current/base/incoming line vectors)
        ▼
three-way panel renders sections
        │
        ▼
ResolveConflicts(text, choice, custom)
        │
        ▼
written back, file re-loaded
```

## Tests

* [`tests/unit/polyui/diff_engine_test.cpp`](../../tests/unit/polyui/diff_engine_test.cpp)
* [`tests/unit/polyui/merge_resolver_test.cpp`](../../tests/unit/polyui/merge_resolver_test.cpp)
* [`tests/unit/polyui/scm_provider_test.cpp`](../../tests/unit/polyui/scm_provider_test.cpp)
* [`tests/unit/polyui/git_provider_test.cpp`](../../tests/unit/polyui/git_provider_test.cpp)
