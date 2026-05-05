# Problems Panel Quickstart

This 5-minute walk-through shows the real-time diagnostics flow added in
v1.21.0.

## 1. Open a Ploy file

Launch `polyui`, then **File -> Open** any `.ploy` source.  As soon as
the editor opens, `IdeLspBridge` spawns `polyls` for that buffer and
sends `textDocument/didOpen`.

## 2. Introduce an error

Type a deliberately broken line, e.g.:

```ploy
LET x = 1 +
```

Within ~200 ms (the change-debounce window) you should see:

* a red squiggle under the offending column in the editor;
* the status-bar counter switching from `E:0 W:0 H:0` to `E:1 W:0 H:0`,
  with the `E:1` painted in red.

## 3. Open the Problems Panel

Click the `E:1` link in the status bar — `MainWindow` calls
`ShowPanel("problems")` and the dock pops up at the bottom.  You will see
one row grouped under the file name with source `polyls:ploy`.

## 4. Filter

* Untick **Warning / Info / Hint** to show errors only.
* Type part of the filename in the **File** box — case-insensitive
  substring match on the basename.
* Type a regex like `^expected` in **Message regex** — invalid patterns
  are silently ignored, so experimenting is safe.

## 5. Jump

Double-click any row.  The corresponding tab is activated and the cursor
is placed at the diagnostic position.

## 6. CLI fallback (`polyc --check`)

For environments without a running LSP (CI, sandboxed editors), the
same diagnostics are reachable via the compiler driver:

```
polyc --check path/to/file.ploy
```

`polyc` writes a single JSON document to `stdout` matching the LSP
`PublishDiagnosticsParams` shape and exits with `0` (clean), `1`
(errors), or `2` (usage / I/O failure).  Pipe it into `jq` for inspection:

```
polyc --check broken.ploy | jq '.diagnostics[] | {sev:.severity, msg:.message}'
```

## 7. Background scan on large workspaces

When the active workspace contains more than 2 000 files, the first
scan runs asynchronously in batches of 50 files per 50 ms tick.
Progress is reported as `Scanning N/M ...` in the status bar.  Once the
sweep finishes, `QFileSystemWatcher` keeps results live for any
subsequent create / delete / rename event.

## See also

* `docs/realization/problems_panel.md` — design notes.
* `docs/USER_GUIDE.md` chapter 13 — reference.
* `docs/tutorial/lsp_quickstart.md` — base LSP setup.
