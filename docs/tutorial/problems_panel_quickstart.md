# Problems Panel Quickstart

> **Document Version**: 2.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Companion**: [problems_panel_quickstart_zh.md](problems_panel_quickstart_zh.md)

This 5-minute walk-through shows the real-time diagnostics flow first shipped in v1.21.0 and refined through v1.45.2.

## 1. Open a `.ploy` file

Launch `polyui`, then **File → Open** any `.ploy` source. As soon as the editor opens, `IdeLspBridge` spawns `polyls` for that buffer and sends `textDocument/didOpen`.

## 2. Introduce an error

Type a deliberately broken line, e.g.:

```ploy
LET x = 1 +
```

Within ~200 ms (the change-debounce window) you should see:

- a red squiggle under the offending column in the editor;
- the status-bar counter switching from `E:0 W:0 H:0` to `E:1 W:0 H:0`, with the `E:1` painted in red.

## 3. Open the Problems Panel

Click the `E:1` link in the status bar — `MainWindow` calls `ShowPanel("problems")` and the dock pops up at the bottom. You will see one row grouped under the file name with source `polyls:ploy`.

## 4. Filter

- Untick **Warning / Info / Hint** to show errors only.
- Type part of the filename in the **File** box — case-insensitive substring match on the basename.
- Type a regex like `^expected` in **Message regex** — invalid patterns are silently ignored, so experimenting is safe.

## 5. Jump

Double-click any row. The corresponding tab is activated and the cursor is placed at the diagnostic position.

## 6. CLI fallback (`polyc --check`)

For environments without a running LSP (CI, sandboxed editors), the same diagnostics are reachable via the compiler driver:

```sh
polyc --check path/to/file.ploy
```

`polyc` writes a single JSON document to `stdout` matching the LSP `PublishDiagnosticsParams` shape and exits with `0` (clean), `1` (errors), or `2` (usage / I/O failure). Pipe it into `jq` for inspection:

```sh
polyc --check broken.ploy | jq '.diagnostics[] | {sev:.severity, msg:.message}'
```

## 7. Background scan on large workspaces

When the active workspace contains more than 2 000 files, the first scan runs asynchronously in batches of 50 files per 50 ms tick. Progress is reported as `Scanning N/M …` in the status bar. Once the sweep finishes, `QFileSystemWatcher` keeps results live for any subsequent create / delete / rename event.

## 8. Diagnostic id catalogue

Every diagnostic carries a stable id of the form `polyc-(err|warn)-<E####|W####>`. The complete catalogue lives in [docs/specs/ploy_diagnostics.md](../specs/ploy_diagnostics.md); the most common entries are listed in section 20 of [ploy_language_tutorial.md](ploy_language_tutorial.md).

## See also

- [docs/realization/problems_panel.md](../realization/problems_panel.md) — design notes.
- [docs/USER_GUIDE.md](../USER_GUIDE.md) chapter 13 — full reference.
- [lsp_quickstart.md](lsp_quickstart.md) — base LSP setup.
