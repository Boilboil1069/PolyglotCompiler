# LSP & `polyls` Quickstart

> **Document Version**: 2.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Companion**: [lsp_quickstart_zh.md](lsp_quickstart_zh.md)

This tutorial walks through enabling the in-tree LSP integration, opening a `.ploy` file, and verifying that diagnostics from `polyls` land in the editor gutter and the bottom **LSP** panel.

## 1. Build `polyls`

```sh
cmake -S . -B build -G Ninja
cmake --build build --target polyls polyui
```

Both targets produce executables under `build/`.

## 2. Default configuration

`polyui` ships LSP defaults for every supported language; nothing needs to be edited to try `.ploy`:

```jsonc
"languageServers.enabled": true,
"languageServers.changeDebounceMs": 200,
"languageServers.servers.ploy":       { "command": "polyls", "args": [] }
"languageServers.servers.cpp":        { "command": "clangd", "args": [] }
"languageServers.servers.python":     { "command": "pyright-langserver", "args": ["--stdio"] }
"languageServers.servers.rust":       { "command": "rust-analyzer", "args": [] }
"languageServers.servers.java":       { "command": "jdtls", "args": [] }
"languageServers.servers.dotnet":     { "command": "csharp-ls", "args": [] }
"languageServers.servers.go":         { "command": "gopls", "args": [] }
"languageServers.servers.javascript": { "command": "typescript-language-server", "args": ["--stdio"] }
"languageServers.servers.ruby":       { "command": "solargraph", "args": ["stdio"] }
```

If `polyls` is not on `PATH`, either set `command` to its absolute path or add `build/` to `PATH` before launching `polyui`.

## 3. Run the IDE and observe traffic

1. Launch `polyui`.
2. **File → Open File…** and pick a `.ploy` source. The bridge sends `initialize` → `initialized` → `didOpen` to `polyls`.
3. Type a syntax error (for example an unterminated string literal). Within ~200 ms the gutter shows a red squiggle and the **LSP** bottom panel logs the inbound `textDocument/publishDiagnostics` notification.
4. Save the file (`Ctrl+S`); the panel records a `didSave`. Close the tab; the panel records a `didClose` followed by an empty `publishDiagnostics`.
5. Quitting the IDE issues `shutdown` + `exit` to every active session.

## 4. Replacing a language server

Open **Settings → Language Servers** and edit, for example, `languageServers.servers.python.command` to point at a custom Pyright build. Restart the affected tab (close and reopen the file) to pick up the new configuration.

## 5. Capabilities surfaced by `polyls`

`polyls` implements the LSP 3.17 subset listed below; the IDE's **LSP** panel logs each inbound / outbound message live.

| Capability                       | Notes                                                                  |
|----------------------------------|------------------------------------------------------------------------|
| `textDocument/publishDiagnostics`| Live diagnostics; same payload as `polyc --check`.                     |
| `textDocument/hover`             | Type, doc-comment summary (from `///`), source location.               |
| `textDocument/completion`        | Keyword + identifier + member completion, ranked by `test_completion_ranker`. |
| `textDocument/signatureHelp`     | Function signatures with default arguments and generic bounds.         |
| `textDocument/definition`        | Jump to declaration across `.ploy`, `LINK` targets, and host imports.  |
| `textDocument/references`        | Workspace-wide references.                                             |
| `textDocument/documentSymbol`    | File outline for the IDE Symbols panel.                                |
| `workspace/symbol`               | Workspace-wide symbol search.                                          |
| `textDocument/formatting`        | Driven by `polyc`'s built-in `.ploy` formatter.                        |

## 6. Driving `polyls` from another editor

`polyls` is a regular stdio LSP server; any client (VS Code, Neovim, Helix, Emacs `lsp-mode`, …) can spawn it and speak LSP 3.17. See [docs/api/polyls.md](../api/polyls.md) for the full contract. Capture frames for diagnosis with:

```sh
polyls --log polyls.log
```

## See also

- [docs/realization/polyls_server.md](../realization/polyls_server.md) — server design notes.
- [docs/USER_GUIDE.md](../USER_GUIDE.md) chapter 12 — full LSP reference.
- [problems_panel_quickstart.md](problems_panel_quickstart.md) — how the Problems panel consumes `polyls` diagnostics.
