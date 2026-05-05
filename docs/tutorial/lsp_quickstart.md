# LSP & polyls Quickstart

> Companion: [`lsp_quickstart_zh.md`](./lsp_quickstart_zh.md). Demand `2026-04-28-19`, version 1.20.0.

This tutorial walks through enabling the in-tree LSP integration, opening
a `.ploy` file, and verifying that diagnostics from `polyls` land in the
editor gutter and the bottom **LSP** panel.

## 1. Build polyls

```sh
cmake -S . -B build
cmake --build build --target polyls polyui
```

Both targets produce executables under `build/`.

## 2. Default configuration

`polyui` ships defaults for every supported language; nothing needs to
be edited to try `.ploy`:

```jsonc
"languageServers.enabled": true,
"languageServers.changeDebounceMs": 200,
"languageServers.servers.ploy": { "command": "polyls", "args": [] }
```

If `polyls` is not on `PATH`, either set `command` to its absolute path
or add `build/` to `PATH` before launching `polyui`.

## 3. Run the IDE and observe traffic

1. Launch `polyui`.
2. **File -> Open File...** and pick a `.ploy` source.  The bridge
   sends `initialize` -> `initialized` -> `didOpen` to `polyls`.
3. Type a syntax error (for example an unterminated string literal).
   Within ~200 ms the gutter shows a red squiggle and the **LSP**
   bottom panel logs the inbound `textDocument/publishDiagnostics`
   notification.
4. Save the file (`Ctrl+S`); the panel records a `didSave`.  Close the
   tab; the panel records a `didClose` followed by an empty
   `publishDiagnostics`.
5. Quitting the IDE issues `shutdown` + `exit` to every active session.

## 4. Replacing a language server

Open **Settings -> Language Servers** and edit, for example,
`languageServers.servers.python.command` to point at a custom Pyright
build.  Restart the affected tab (close and reopen the file) to pick
up the new configuration.

## 5. Driving polyls from another editor

`polyls` is a regular stdio LSP server; any client (VS Code,
Neovim, Helix...) can spawn it and speak LSP 3.17.  See
[`docs/api/polyls.md`](../api/polyls.md) for the full contract.
