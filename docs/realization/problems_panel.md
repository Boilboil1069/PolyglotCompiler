# Problems Panel & Real-time Diagnostics

Status: shipped in v1.21.0.

This document describes the IDE Problems Panel, the diagnostics aggregator
behind it, the workspace background scanner, and the `polyc --check` CLI
fallback.  Together they provide *edit-as-you-check* behaviour: the user
sees errors, warnings, and hints in real time as they type, both inline
in the editor gutter and grouped centrally in a dockable panel.

## Components

| Component             | File(s)                                                                  | Threading           |
|-----------------------|--------------------------------------------------------------------------|---------------------|
| `ProblemsAggregator`  | `tools/ui/common/{include,src}/problems_aggregator.{h,cpp}`              | Qt-free, mutex      |
| `ProblemsPanel`       | `tools/ui/common/{include,src}/problems_panel.{h,cpp}`                   | Qt main thread      |
| `WorkspaceScanner`    | `tools/ui/common/{include,src}/workspace_scanner.{h,cpp}`                | Qt + `QTimer` ticks |
| Status-bar counter    | `MainWindow::SetupStatusBar` (rich-text `QLabel` + `linkActivated`)      | Qt main thread      |
| `polyc --check` flag  | `tools/polyc/src/driver.cpp` (pre-stage handler)                         | CLI                 |
| LSP mirror            | `IdeLspBridge::PublishDiagnosticsToEditor`                               | Qt main thread      |

## Data Model

`ProblemsAggregator` is intentionally **Qt-free** so that it can be unit
tested without spinning up a `QApplication`.  It owns a thread-safe map:

```
std::map<file_path, std::map<source_label, std::vector<ProblemEntry>>>
```

Positions in `ProblemEntry` are **1-based** (matching
`compiler_service::DiagnosticInfo`).  The LSP wire format is 0-based;
conversion happens at the bridge boundary.

### Source labels

Three well-known labels namespace the providers:

| Label              | Producer                                          |
|--------------------|---------------------------------------------------|
| `polyls:<lang>`    | LSP server `polyls`, mirrored by `IdeLspBridge`.  |
| `polyc`            | Foreground in-process compile / analyze.          |
| `polyc-bg`         | `WorkspaceScanner` background batch scan.         |

`ClearSource(file, source)` is used by each producer to scope its own
entries; `ClearFile(file)` is invoked when a tab is closed.

### Severity

`ClassifySeverity(label)` maps free-form text into the `Severity` enum
(`kError | kWarning | kInfo | kHint`).  The aggregator stores both the
classified enum and the original label; the latter is shown in the panel.
A `SeverityMask` bitmask drives filtering.

## Filters

`ProblemFilter` exposes:

* `severity` — bitmask (default = all)
* `file_substring` — case-insensitive substring on the **basename**
* `source_substring` — case-insensitive substring on the source label
* `message_pattern` — ECMAScript regex on the message; an invalid
  pattern silently degrades to *match everything* (no exceptions thrown
  to the UI thread).

The panel re-runs `Snapshot(filter)` on every `AggregatorChanged`
signal, sorted by `(file, line, column, severity)`.

## Real-time Pipeline

1. The editor emits `textChanged`; `IdeLspBridge` debounces 200 ms and
   sends `textDocument/didChange` to `polyls`.
2. `polyls` returns `textDocument/publishDiagnostics`; the bridge
   converts the LSP positions to 1-based, calls
   `editor->SetDiagnostics(...)` for the gutter, and mirrors the same
   list into the aggregator under `polyls:<language_id>`.
3. The panel receives `AggregatorChanged`, refreshes its tree, and the
   status-bar counter (`E:N W:N H:N`) updates.

When the LSP server is unavailable, the in-process compile path
publishes under the `polyc` source instead — `MainWindow::ShowDiagnostics`
routes every diagnostic display through a single wrapper that mirrors to
the aggregator.

## Workspace Scanner

`WorkspaceScanner` walks the active workspace root via `QDirIterator`,
skipping `.git`, `build*`, `node_modules`, `target`, `.venv`, etc.  It
batches **50 files per 50 ms tick** so the UI thread stays responsive.
For workspaces above the `kLargeWorkspaceFiles = 2000` threshold, a
`Scanning N/M…` progress message is posted on the status bar.

`QFileSystemWatcher` is wired to invalidate cached results when files
or directories change; the watcher is capped at 1024 directories to stay
within OS handle limits.

## Status-bar Counter

`MainWindow::SetupStatusBar` adds a rich-text `QLabel` bound to
`AggregatorChanged`.  The label renders `E:3 W:5 H:1` with HTML anchor
tags; clicking any colour-coded count fires `QLabel::linkActivated` and
calls `ShowPanel("problems")`.  This avoids subclassing `QLabel`.

## `polyc --check`

Used as a fallback for environments without LSP (CI, sandboxed editors,
language plug-ins for foreign IDEs).  Synopsis:

```
polyc --check <file> [--lang=<id>]
```

The handler runs `FrontendRegistry::Get(language)->Analyze(...)` and
writes a single JSON document to `stdout` matching the LSP
`PublishDiagnosticsParams` shape:

```json
{
  "uri": "file:///abs/path/to/file.ploy",
  "diagnostics": [
    {
      "range": {
        "start": { "line": 12, "character": 4 },
        "end":   { "line": 12, "character": 18 }
      },
      "severity": 1,
      "code": "E1042",
      "source": "polyc",
      "message": "expected ';' before '}'"
    }
  ]
}
```

Exit codes: `0` = clean, `1` = diagnostics with at least one error,
`2` = usage / I/O failure.  Positions on the wire are 0-based as per LSP.

## Tests

* `tests/unit/polyui/problems_panel_model_test.cpp` — eleven Catch2
  cases covering severity classification, snapshot ordering,
  scoped clears, the four filter facets, `CountAll` buckets,
  `KnownSources` deduplication, change-callback semantics, and the
  `ReplaceFromDiagnosticInfo` adapter.  The target `test_problems`
  builds the aggregator translation unit directly so the suite stays
  Qt-free and runs under any CI worker.

Integration parity with the gutter is enforced by routing every
`SetDiagnostics` call through `MainWindow::ShowDiagnostics`, which is
the single mirror point into the aggregator under the `polyc` source.

## Future Work

* Quick-fix application from the panel (`textDocument/codeAction`).
* Persistent per-workspace mute lists.
* Cross-language symbol-aware regrouping (currently only by file).
