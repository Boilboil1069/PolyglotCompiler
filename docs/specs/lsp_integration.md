# LSP Integration Architecture

> Demand: `2026-04-28-19`. Version: 1.20.0. Bilingual companion: [`lsp_integration_zh.md`](./lsp_integration_zh.md).

PolyglotCompiler ships an in-process Language Server Protocol (LSP)
client framework inside the polyui IDE plus a self-developed language
server, **polyls**, that exposes the polyglot frontends (starting with
the `.ploy` language) over the standard LSP wire format.  This document
describes the architecture, the wire-level guarantees, the public types,
and the configuration surface.

## 1. Component map

```
┌──────────────────────────── polyui (Qt) ─────────────────────────────┐
│                                                                      │
│  CodeEditor ─┐                                                       │
│              │ contentsChanged                                       │
│              ▼                                                       │
│   IdeLspBridge ──► QTimer (debounce 200ms) ──► LspClient.DidChange   │
│        │                                          │                  │
│        │ TrackEditor / Untrack / NotifySaved      │ JSON-RPC frames  │
│        ▼                                          ▼                  │
│   LspSessionRegistry            ┌─── StdioTransport (QProcess) ───┐  │
│        │                        │                                 │  │
│        ▼                        ▼                                 │  │
│   LspCapabilityRegistry      polyls / clangd / pyright / …        │  │
│                                                                   │  │
│   LspLogPanel  ◄── SetLogHandler(direction, payload)              │  │
│                                                                   │  │
└──────────────────────────────────────────────────────────────────────┘
```

* **`tools/ui/common/lsp/`** — Qt-free LSP core.  Builds the `lsp_lib`
  static library used by both polyui and the unit tests.
  * `lsp_message.{h,cpp}` — typed JSON-RPC envelopes and LSP message
    payloads (Position, Range, Diagnostic, Location, Hover,
    CompletionItem, SignatureHelp, SymbolInformation, CodeAction,
    TextEdit, WorkspaceEdit, DocumentSymbol, the lifecycle envelopes,
    plus `EncodeFrame` / `TryDecodeFrame`).
  * `lsp_client.{h,cpp}` — `ILspTransport` interface, `LoopbackTransport`
    (in-process pair for unit tests) and `LspClient` (request/response
    correlation, notification dispatch, log handler hooks).
  * `lsp_session.{h,cpp}` — `LspSessionRegistry` keyed on
    `(workspace_uri, language_id)`.
  * `lsp_capability_registry.{h,cpp}` — thread-safe per-session
    `ServerCapabilities` cache, queried via member-pointer.
  * `lsp_log_panel.{h,cpp}` — Qt panel rendering tx/rx envelopes with
    direction / kind / method filters.
* **`tools/ui/common/{include,src}/lsp_bridge.{h,cpp}`** — Qt glue:
  spawns servers via `QProcess`, hooks `CodeEditor` change events into
  the debounced `didChange` pipeline, and routes `publishDiagnostics`
  back to `CodeEditor::SetDiagnostics`.
* **`tools/polyls/`** — headless language server.
  * `polyls_core/polyls_server.{h,cpp}` — protocol-level dispatcher.
  * `polyls.cpp` — stdio driver (binary mode on Windows).

## 2. Wire format

Strict JSON-RPC 2.0 with the LSP `Content-Length: <bytes>\r\n\r\n`
framing.  All field names on the wire are camelCase; the C++ side uses
snake_case fields and converts via the `ToJson` / `FromJson` free
functions in `lsp_message.h` so we never depend on macro-generated
serialisation.

`TryDecodeFrame()` is partial-buffer safe: it returns `false` and leaves
the buffer untouched when fewer than `Content-Length` bytes have arrived
yet, which is what enables the same frame loop to drive both the
synchronous loopback transport and the streaming `QProcess` transport.

## 3. Capabilities advertised in 1.20.0

Per the demand, this revision keeps the surface deliberately narrow:

| Capability                  | Value     |
|-----------------------------|-----------|
| `textDocumentSync`          | `1` (full) |
| `diagnosticProvider`        | `true`    |
| `hoverProvider`             | `false`   |
| `completionProvider`        | `false`   |
| `signatureHelpProvider`     | `false`   |
| `definitionProvider`        | `false`   |
| `referencesProvider`        | `false`   |
| `documentSymbolProvider`    | `false`   |
| `renameProvider`            | `false`   |
| `codeActionProvider`        | `false`   |

Hover / completion / definition / references / rename / code-action are
deferred to demands 2026-04-28-21..23.

## 4. Editor integration

`MainWindow` constructs a single `IdeLspBridge` in
`SetupDockWidgets()`.  Every successful `OpenFileInTab(path)` ends with
`lsp_bridge_->TrackEditor(editor, language)`, which:

1. Looks up `languageServers.servers.<language>` from
   `SettingsService`.  If the entry is missing or
   `languageServers.enabled = false`, the call is a no-op.
2. Resolves the configured `command` via `QStandardPaths::findExecutable`.
   If the binary is not on `PATH`, the bridge surfaces a non-blocking
   status-bar message instead of throwing — matches §4 of the demand.
3. Spawns the server through `StdioTransport` (a `QProcess`-backed
   `ILspTransport`), creates an `LspClient`, runs `initialize` →
   `initialized`, then issues the first `didOpen` with the editor's
   current text and `version = 1`.
4. Connects `editor->document()->contentsChanged` to a `QTimer`
   (`languageServers.changeDebounceMs`, default 200 ms).  Each timeout
   sends a single `didChange` carrying the full document text, with
   `version` monotonically increasing.

`Save()` and `CloseTab()` route through `NotifySaved()` and `Untrack()`,
which emit `didSave` and `didClose` respectively.  `~MainWindow()`
invokes `Shutdown()` on the bridge, which forwards `shutdown` + `exit`
to every initialised session.

## 5. Settings

JSON keys (defaults shipped via the QRC bundle):

| Key                                       | Type     | Default | Description |
|-------------------------------------------|----------|---------|-------------|
| `languageServers.enabled`                 | bool     | `true`  | Master switch. |
| `languageServers.changeDebounceMs`        | integer  | `200`   | `didChange` debounce in ms. |
| `languageServers.logCapacity`             | integer  | `2000`  | LSP log panel ring size. |
| `languageServers.servers.<lang>.command`  | string   | varies  | Executable name. |
| `languageServers.servers.<lang>.args`     | string[] | varies  | Argument list. |
| `languageServers.servers.<lang>.env`      | object   | `{}`    | Extra environment. |
| `languageServers.servers.<lang>.initializationOptions` | object | `{}` | Forwarded to `initialize`. |

Default servers: `polyls` for `.ploy`, `clangd` for `cpp`,
`pyright-langserver --stdio` for `python`, `rust-analyzer` for `rust`,
`jdtls` for `java`, `omnisharp -lsp` for `csharp`.

## 6. Testing

* `tests/unit/polyui/lsp_client_test.cpp` (`test_lsp`) — framing
  round-trip, partial-buffer behaviour, JSON-RPC envelope helpers,
  `LoopbackTransport` delivery, request/response correlation,
  notification dispatch, typed `publishDiagnostics`, log handler,
  capability registry, session registry idempotency.
* `tests/unit/polyls/lifecycle_test.cpp` (`test_polyls`) — pre-init
  rejection (`-32002`), advertised capability shape, document-store
  mechanics, syntax-error → `publishDiagnostics`, shutdown→exit.
* `tests/integration/lsp_diagnostics_e2e_test.cpp` — drives a real
  `LspClient` against an in-process `PolylsServer` through a loopback
  transport pair, opens malformed `.ploy` source, asserts an error
  diagnostic with `source = "polyls"` is published, then validates the
  empty-publish-on-close + shutdown / exit handshake.

## 7. References

* LSP 3.17 wire spec: <https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/>
* JSON-RPC 2.0: <https://www.jsonrpc.org/specification>
* Companion API doc: [`docs/api/polyls.md`](../api/polyls.md)
