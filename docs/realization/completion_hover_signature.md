# Completion · Hover · Signature Help (IDE P0-3)

> Realization note for the language-feature triple shipped in
> v1.22.0.  Pairs with `completion_hover_signature_zh.md`.

## 1. Architecture

```
┌──────────────┐   textDocument/           ┌──────────────┐
│  CodeEditor  │   completion / hover /    │   polyls     │
│  (Qt widget) │ ─signatureHelp──────────▶ │   server     │
│              │ ◀─JSON-RPC reply───────── │ (Qt-free)    │
└──────┬───────┘                           └──────────────┘
       │ IdeLspBridge::Request*
       │ (QPointer-guarded callbacks
       │  hopped to the GUI thread via
       │  QMetaObject::invokeMethod)
       ▼
  CompletionPopup / HoverTooltip / SignatureHelpWidget
       │
       ▼
  SnippetExpander (LSP `${1:name}` parser + Tab navigation)
```

* The server side lives in `tools/polyls/polyls_core/polyls_features.cpp`
  and runs without Qt so it can be reused by the CLI driver.
* The Qt side lives in `tools/ui/common/src/code_assist.cpp` and
  `tools/ui/common/src/completion_ranker.cpp`.
* The bridge methods (`RequestCompletion` / `RequestHover` /
  `RequestSignatureHelp`) marshal the JSON-RPC traffic and re-enter the
  Qt main thread before invoking the supplied callback.

## 2. Server endpoints

| Method                          | Purpose                                  |
| ------------------------------- | ---------------------------------------- |
| `textDocument/completion`       | Keywords, document symbols, `LINK <lang>::` cross-language template |
| `completionItem/resolve`        | Echoes the resolved item (we send full text up-front) |
| `textDocument/hover`            | Markdown signature + documentation       |
| `textDocument/signatureHelp`    | Overload list with active parameter index |

The capability flags reported in `initialize` are
`completionProvider`, `hoverProvider` and `signatureHelpProvider`.

### Cross-language LINK trigger

When the cursor sits after `LINK <lang>::` (or a typed prefix
afterwards), the completion handler emits one item of kind
`Module` (`9`) carrying a snippet template
`<lang>::${1:module}` so that pressing Tab lets the user fill in the
module name immediately.  This implements demand §1.1's cross-language
flow without yet requiring a full module index.

## 3. Match strategies

The Qt-free helper in `completion_ranker.{h,cpp}` ranks a label against
the user's needle with one of three strategies, switchable via the
setting `languageServers.completionMatchStrategy` (`prefix`,
`subsequence`, `fuzzy`):

* **Prefix** — case-insensitive prefix match; bonuses for exact case
  and exact length.
* **Subsequence** — every needle character must appear in order.
  Bonuses for adjacency, word boundaries (`_`, `:`, `.`, `-`, ` ` and
  CamelCase transitions), and early position; small length-difference
  penalty.
* **Fuzzy** — clean subsequence preferred, but if the strict pass
  fails we tolerate one stray needle character (penalty −50).  Only
  enabled when the needle has at least 3 characters to avoid spurious
  matches on very short input.

The ranker returns `-1` to filter the candidate out, otherwise a
non-negative score (higher = better).  The editor stable-sorts
descending before opening the popup.

## 4. Snippet expander

`SnippetExpander` parses an LSP snippet template (`$0`, `$1`,
`${1}`, `${1:default}`, escaped `\$`) into plain text plus a vector
of `SnippetStop` ranges.  Stops are sorted by tab index, with `$0`
intentionally placed last (it represents the final cursor).

* `ExpandAtCursor` inserts the plain text at the editor's cursor,
  promotes relative offsets to absolute document positions, and
  selects the first stop.
* Pressing **Tab** advances to the next stop, selecting its current
  text so the user can type to overwrite.  When the last stop is
  visited the expander cancels itself and Tab regains its default
  behaviour (insert spaces).

## 5. Hover

`mouseMoveEvent` records the global cursor position and starts a
single-shot `QTimer` (450 ms).  When the timer fires the bridge sends
`textDocument/hover`; the reply's Markdown is shown in a frameless
`HoverTooltip`.  The same path is reachable manually via
**Ctrl+K Ctrl+I**.

## 6. Signature help

Typing `(` triggers `RequestSignatureHelp`.  The server walks the
current line backwards, counting unmatched `(` to find the active
call site and top-level commas to compute the active parameter index.
The widget shows every overload, highlights the active parameter, and
allows up/down arrows to switch between overloads.

## 7. Tests

* `tests/unit/polyls/completion_test.cpp` — keyword, user-FUNC and
  cross-language LINK completion shapes.
* `tests/unit/polyls/hover_test.cpp` — Markdown content and the null
  reply for whitespace positions.
* `tests/unit/polyls/signature_help_test.cpp` — active parameter
  inside `add(1, |)` and the null reply outside any call.
* `tests/unit/polyui/completion_ranker_test.cpp` — ranking
  invariants for all three strategies.

A future integration test (`integration/lsp_completion_e2e_test`) is
planned to drive the QProcess transport end-to-end.
