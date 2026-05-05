# Workspace Symbol Index for Navigation

`polyls` answers the LSP navigation requests
(`textDocument/definition`, `declaration`, `implementation`,
`typeDefinition`, `references`) on top of a workspace-level symbol
index living in `tools/polyls/polyls_core/symbol_index.{h,cpp}`.  The
index is rebuilt incrementally on every `didOpen` / `didChange` /
`didSave` notification and persisted to `.polyc-cache/symbol_index.json`
so a fresh server start can answer queries without re-parsing the whole
workspace.

## Goals

* Sub-millisecond per-keystroke updates: the index relies on regex-free,
  single-pass scans tuned to the cross-language `LINK` / `IMPORT` /
  `EXPORT` vocabulary instead of running a full parser.
* Cross-language `.ploy` ↔ host-language jumps for `cpp`, `python`,
  `rust`, `java`, and `dotnet`.
* A self-describing JSON cache so an editor restart finds an already-
  populated workspace immediately.

## Indexed entities

| Source           | Entity captured                                           |
|------------------|-----------------------------------------------------------|
| `.ploy`          | `FUNC`, `PIPELINE`, `STRUCT`, `LET`/`VAR` bindings        |
| `.ploy`          | `IMPORT lang::module` and `IMPORT lang PACKAGE pkg`       |
| `.ploy`          | `LINK target_lang::… AS …` and tuple `LINK(target,…)`     |
| `.ploy`          | `EXPORT name AS lang::func`                               |
| C++              | `namespace`-qualified classes / structs and free functions |
| Python           | `def name`, `class Name`                                  |
| Rust             | `fn`, `struct`, `enum`, `trait`, `impl` (incl. `pub`)     |
| Java / .NET      | `class`, `interface`, `enum`, `record`                    |

Every entry stores a `SymbolLocation` (LSP-ready `(uri, line,
character, end_line, end_character)`), a `kind`, the bare and
fully-qualified names, and — for `LINK` entries — the host-language
target descriptor used by cross-language navigation.

## Reference collection

After per-language parsing the index walks each line a second time and
records every identifier whose token text matches a known entry name as
a `ReferenceSite`.  Definition sites are tagged with
`is_definition=true` so `textDocument/references` can include or
exclude them based on the request's `context.includeDeclaration` flag.
A small keyword set (FUNC, PIPELINE, STRUCT, LET, …) is excluded so
language keywords never count as references.

## Cross-language LINK resolution

`SymbolIndex::CrossLanguageTarget(lang, qualified)` looks up the host
language's `kForeignFunction` / `kForeignClass` entries by either the
fully-qualified name or its bare last component (so a `.ploy`
declaration `LINK cpp::image_processor::enhance` can find a top-level
`void enhance(...)` defined without an enclosing namespace in the C++
file).

`CrossLanguageBackrefs(lang, qualified)` performs the reverse lookup:
given a host-language symbol, return every `.ploy` `LINK` site whose
`link_target_language` and `link_target_qualified` match.  This is what
powers a `references` query issued from inside a host-language file: in
addition to the host-language uses, the response includes every
`.ploy` LINK that imports the symbol.

## Server wiring

* `initialize` records `rootUri`, derives
  `<root>/.polyc-cache`, calls `SymbolIndex::LoadFromCache()`, and
  advertises the five new server capabilities (`definitionProvider`,
  `declarationProvider`, `implementationProvider`,
  `typeDefinitionProvider`, `referencesProvider`).
* `didOpen` / `didChange` / `didSave` re-index the affected document
  and call `SaveToCache()` (best effort).
* `didClose` drops the document from the in-memory index but keeps the
  on-disk snapshot so historical references survive an editor close.
* `shutdown` flushes one final snapshot.

## Editor shortcuts

The Qt editor in `tools/ui/common/src/code_editor.cpp` binds the LSP
endpoints to the standard navigation shortcuts:

| Shortcut      | LSP endpoint                       |
|---------------|------------------------------------|
| `F12`         | `textDocument/definition`          |
| `Shift+F12`   | `textDocument/references`          |
| `Ctrl+F12`    | `textDocument/implementation`      |
| `Ctrl+K F12`  | Inline Peek view (definition)      |
| `Ctrl+Click`  | `textDocument/definition`          |

## Cache format

`<workspace>/.polyc-cache/symbol_index.json`:

```json
{
  "version": 1,
  "generator": "polyls.symbol_index",
  "documents": [
    {
      "uri": "file:///path/main.ploy",
      "entries": [ { "name": "compute", "kind": "function", … } ],
      "references": [ { "name": "compute", "isDefinition": true, … } ]
    }
  ]
}
```

The version field guards forward compatibility: a non-matching version
causes a silent re-index from in-memory documents on next start.

## Tests

* `tests/unit/polyls/symbol_index_test.cpp` — round-trip persistence,
  incremental rebuild, cross-language target/backref lookup.
* `tests/unit/polyls/navigation_test.cpp` — the five LSP handlers
  exercised through the JSON-RPC façade.
* `tests/integration/lsp_navigation_e2e_test.cpp` — full client ↔
  server round trip using the `09_mixed_pipeline` sample, validating
  forward (.ploy → C++) and reverse (C++ → .ploy) navigation.
