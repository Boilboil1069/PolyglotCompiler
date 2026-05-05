# Semantic syntax highlighting (demand 2026-04-28-24)

## Goal

Replace the regex-driven highlighter that ships in
`tools/ui/common/src/syntax_highlighter.cpp` with a real semantic
pipeline that:

1. Parses every editor buffer through a tree-sitter-shaped runtime.
2. Surfaces folding regions, document outline and smart-select
   ranges from the same parse so the editor never disagrees with
   itself.
3. Emits LSP 3.16 semantic tokens so any third-party LSP client
   speaking the standard wire format can also paint our buffers.

The regex highlighter is retained as a fallback for offline scenarios
and for languages without a registered grammar.

## Components

| Layer            | File                                                                                                | Responsibility                                                                  |
|------------------|-----------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| Grammar table    | [`tools/polyls/grammar/grammar_descriptor.{h,cpp}`](../../tools/polyls/grammar/grammar_descriptor.h) | Declares keyword / type / builtin sets and the shared semantic-token legend.    |
| Runtime          | [`tools/ui/common/syntax/tree_sitter_runtime.{h,cpp}`](../../tools/ui/common/syntax/tree_sitter_runtime.h) | Pure-C++ parser exposing `Parse`/`Edit`/`Tokens`/`Folds`/`Outline`/`SmartSelect`. |
| LSP handler      | [`tools/polyls/polyls_core/polyls_semantic.cpp`](../../tools/polyls/polyls_core/polyls_semantic.cpp) | Implements `textDocument/semanticTokens/full` and `/range`.                     |
| Editor consumer  | [`tools/ui/common/syntax/semantic_tokens_client.{h,cpp}`](../../tools/ui/common/syntax/semantic_tokens_client.h) | Decodes the wire payload and applies `QTextCharFormat`s on top of the document. |

## Pipeline

```
editor buffer (didOpen / didChange)
        │
        ▼
polyls_server  ─►  PolylsServer::HandleSemanticTokensFull
        │                       │
        ▼                       ▼
   document text     polyls::ts::Parse(language, source)
                              │
                              ▼
                      Tree::Tokens()      ◄── grammar descriptor
                              │
                              ▼
                  EncodeSemanticTokens()
                              │
                              ▼
                JSON-RPC `SemanticTokens.data`
                              │
                              ▼
              SemanticTokensClient::Apply()
                              │
                              ▼
                editor renders coloured spans
```

## Token legend

The legend is stable across the codebase (see
`grammar_descriptor.h::kTokenTypes`):

| Index | Type      | Default colour (Dark theme) |
|------:|-----------|-----------------------------|
| 0     | namespace | `editor_text`               |
| 1     | type      | `#4ec9b0`                   |
| 2     | struct    | `#4ec9b0`                   |
| 3     | function  | `#dcdcaa`                   |
| 4     | variable  | `#9cdcfe`                   |
| 5     | parameter | `#9cdcfe`                   |
| 6     | keyword   | `#569cd6` (bold)            |
| 7     | comment   | `#6a9955` (italic)          |
| 8     | string    | `#ce9178`                   |
| 9     | number    | `#b5cea8`                   |
| 10    | operator  | `editor_text`               |

Modifiers: `declaration`, `readonly`, `static`, `deprecated`,
`definition`.  `LINK` / `IMPORT` / `EXPORT` directive keywords are
emitted with the `definition` modifier so themes can paint them
distinctively.

## Wire format

`textDocument/semanticTokens/full` returns:

```json
{ "data": [delta_line, delta_char, length, type_index, modifier_mask, …] }
```

The runtime guarantees document-order tokens, so re-decoding via
`DecodeSemanticTokens()` yields the original absolute coordinates.
`textDocument/semanticTokens/range` re-uses the same encoder after
filtering tokens by line.

## Editor integration

* The new `SemanticTokensClient` keeps an instance per editor; it
  remembers the legend that the server advertised at `initialize`.
* Toggle: `editor/useLspSemanticTokens` (default `true`).  When
  disabled, the regex highlighter remains the sole authority.
* `SemanticTokensClient::Apply()` re-publishes per-kind
  `QTextCharFormat`s to the existing `SyntaxHighlighter`.  This keeps
  the regex pipeline consistent with the LSP palette so users see no
  flicker when toggling the setting.

## Tree-sitter grammar source

The runtime is wire-compatible with the upstream tree-sitter API.
When pre-built `tree-sitter-<lang>.{a,so}` artefacts become available
(via the offline cache at `<repo>/.cache/deps/tree-sitter-*/`), the
runtime can be linked against them by enabling
`-DPOLYGLOT_USE_LIBTREE_SITTER=ON`; the call sites are unaffected.
The bundled C++ implementation continues to be the default because it
ships with the source tree and does not require a network round-trip.

## Tests

* [`tests/unit/polyls/tree_sitter_runtime_test.cpp`](../../tests/unit/polyls/tree_sitter_runtime_test.cpp)
  — grammar table, parser, folds, outline, smart-select, encoder
  inverse.
* [`tests/unit/polyls/semantic_tokens_test.cpp`](../../tests/unit/polyls/semantic_tokens_test.cpp)
  — LSP boundary contract for full + range.
* [`tests/integration/lsp_semantic_tokens_e2e_test.cpp`](../../tests/integration/lsp_semantic_tokens_e2e_test.cpp)
  — end-to-end round-trip through the loopback transport.
