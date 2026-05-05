# `polyls` — Polyglot Language Server

> Bilingual companion: [`polyls_zh.md`](./polyls_zh.md). Demand: `2026-04-28-19`. Version: 1.20.0.

`polyls` is the in-tree LSP server that exposes the polyglot frontends
over the standard Language Server Protocol.  This document specifies
the executable contract (process lifecycle, transport, capabilities,
error semantics) so any LSP-aware editor — not just polyui — can drive
it.

## Invocation

```
polyls
```

The server reads JSON-RPC frames from `stdin` and writes them to
`stdout`.  No command-line arguments are accepted in 1.20.0.  On
Windows, `stdin` and `stdout` are placed in binary mode internally so
that LSP frames are not corrupted by `\r\n` translation.

Process exit codes:

| Code | Meaning |
|------|---------|
| `0`  | Clean shutdown — `shutdown` was received before `exit`. |
| `1`  | `exit` was received without a prior `shutdown` (per LSP spec). |

## Wire format

Strict JSON-RPC 2.0 over LSP framing
(`Content-Length: <n>\r\n\r\n<payload>`).  Field names are camelCase
on the wire.

## Lifecycle

1. **`initialize`** (request) — must be the first message.  The server
   replies with the `ServerCapabilities` block listed below.  Any
   request other than `initialize` received before initialisation
   completes is rejected with JSON-RPC error code `-32002`
   (`ServerNotInitialized`).
2. **`initialized`** (notification) — client signals readiness.
3. **`textDocument/didOpen`**, **`didChange`**, **`didClose`**,
   **`didSave`** — document store mutations.  Each `didOpen` /
   `didChange` triggers a synchronous re-analysis using
   `PloyLanguageFrontend::Analyze`; results are published as
   **`textDocument/publishDiagnostics`**.
4. **`shutdown`** (request) — server flushes state and replies.  After
   this point any further request is rejected with
   `InvalidRequest` (`-32600`).
5. **`exit`** (notification) — terminates the driver loop.

## Advertised capabilities (1.20.0)

```json
{
  "textDocumentSync": 1,
  "diagnosticProvider": true,
  "hoverProvider": false,
  "completionProvider": false,
  "signatureHelpProvider": false,
  "definitionProvider": false,
  "referencesProvider": false,
  "documentSymbolProvider": false,
  "renameProvider": false,
  "codeActionProvider": false
}
```

`textDocumentSync = 1` means **full** document sync; clients must send
the entire document text on every `didChange`.  Incremental sync, hover,
completion, definition and friends are deferred to demands
2026-04-28-21..23.

`InitializeResult.serverInfo` is `{ "name": "polyls", "version": "1.20.0" }`.

## Diagnostics

Every successful `didOpen` / `didChange` produces exactly one
`publishDiagnostics` notification for that URI.  `didClose` produces a
final `publishDiagnostics` with an empty `diagnostics` array so that
the client can clear its overlay.

Each `Diagnostic` carries:

* `range` — 0-based positions converted from the frontend's 1-based
  `core::SourceLoc`.
* `severity` — `1` (Error), `2` (Warning), `3` (Information); maps from
  `frontends::DiagnosticSeverity::{kError, kWarning, kNote}`.
* `code` — the frontend error code prefixed with `E` (e.g. `"E1001"`).
* `source` — always `"polyls"`.
* `message` — the frontend's human-readable message.

## URI handling

`polyls` accepts `file://` URIs.  Percent-encoded paths are decoded;
on Windows the leading slash before the drive letter (`/C:/…`) is
stripped before the path is handed to the frontend.

## Error codes

| Code   | Symbol                | When |
|--------|-----------------------|------|
| `-32700` | `ParseError`        | Malformed JSON received from the client. |
| `-32600` | `InvalidRequest`    | Method called after `shutdown`. |
| `-32601` | `MethodNotFound`    | Server has no handler for the requested method. |
| `-32602` | `InvalidParams`     | Required parameter missing / wrong type. |
| `-32002` | `ServerNotInitialized` | Any non-`initialize` request before init completes. |

## Headless usage example

```sh
# Spawn polyls and pipe a hand-crafted initialize request:
{
  printf 'Content-Length: 73\r\n\r\n'
  printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
} | polyls
```

The server replies with a framed JSON-RPC response on stdout.

## See also

* Architecture overview: [`docs/specs/lsp_integration.md`](../specs/lsp_integration.md)
* IDE integration walkthrough: `docs/USER_GUIDE.md` → "Language Server architecture"
