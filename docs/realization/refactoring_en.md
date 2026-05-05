# Workspace Refactoring (`polyls`)

`polyls` answers three LSP refactoring requests on top of the workspace
`SymbolIndex` introduced in v1.23.0:

* `textDocument/prepareRename`
* `textDocument/rename`
* `textDocument/codeAction`

The implementation lives in
[`tools/polyls/polyls_core/refactor.{h,cpp}`](../../tools/polyls/polyls_core/refactor.h)
plus the LSP wiring in
[`polyls_refactor.cpp`](../../tools/polyls/polyls_core/polyls_refactor.cpp).

## Goals

* Single, atomic `WorkspaceEdit` for every rename — the editor applies
  it through one undo step.
* Cross-language rename: a host-language identifier rename also
  rewrites every `.ploy` `LINK` / `EXPORT` site that imports it, and
  vice-versa.
* Token-aware text rewrites that skip occurrences inside string
  literals and `//` / `#` comments so identifier substrings inside
  payloads are never corrupted.
* Refactoring catalogue exposed through `codeAction` so the editor's
  lightbulb menu always shows the same five entries.

## Rename pipeline

1. **Token resolution** (`ResolveIdentifierAt`) — locates the
   identifier under the cursor and rejects whitespace, punctuation, or
   reserved keywords (`FUNC`, `class`, `let`, …).
2. **Validation** (`IsValidIdentifier`) — the new name must match
   `[A-Za-z_][A-Za-z0-9_]*` and must not collide with the conservative
   reserved-word set.
3. **Open-buffer rewrite** — every open document is scanned line by
   line; identifier matches outside strings/comments become `TextEdit`
   entries on `WorkspaceEdit::changes[uri]`.
4. **Index fall-back** — for files that the index knows about but the
   editor has not opened, the locations recorded by
   `SymbolIndex::References` are emitted as edits and the editor
   patches them through its own file IO.
5. **Cross-language hop** — when the rename starts inside a
   host-language file, `SymbolIndex::CrossLanguageBackrefs` is
   additionally queried so `.ploy` LINK qualifiers are rewritten in
   the same edit.

## CodeAction catalogue

| `kind`                          | Behaviour                                              |
|---------------------------------|--------------------------------------------------------|
| `refactor.extract.function`     | Wrap the selected lines in a new `FUNC`, replace with a call. |
| `refactor.inline.variable`      | Detect `LET name = …;` and emit a comment-out edit at the binding. |
| `refactor.inline.function`      | Lightbulb entry; the editor's wizard performs the call-site rewrite. |
| `refactor.changeSignature`      | Lightbulb entry; surfaces the editor's signature wizard. |
| `refactor.move.file`            | Lightbulb entry; surfaces the editor's file-move wizard. |

The first two entries arrive with a fully populated `WorkspaceEdit`;
the remaining three are informational so the editor can render the
same menu regardless of polyls' static-analysis depth.

## Editor integration

`tools/ui/common/src/code_editor.{h,cpp}` exposes a
`RenameRequested(symbol, line, column)` signal bound to the standard
`F2` shortcut, plus a `Ctrl+Shift+R` chord that issues the
extract-function `codeAction`.  `mainwindow.cpp` shows a confirmation
popup grouped by file — the user can untick any individual edit before
pressing **Apply**, and the entire `WorkspaceEdit` is applied as a
single undo step on the editor's text document.

## Tests

* [tests/unit/polyls/refactor_test.cpp](../../tests/unit/polyls/refactor_test.cpp)
  — identifier validation, prepareRename, intra-file rename,
  string-literal isolation, cross-language LINK propagation,
  codeAction catalogue.
* [tests/integration/lsp_refactor_e2e_test.cpp](../../tests/integration/lsp_refactor_e2e_test.cpp)
  — full client ↔ server round trip on `09_mixed_pipeline`: rename
  initiated from `image_processor.cpp` rewrites both the host file and
  `mixed_pipeline.ploy`.
