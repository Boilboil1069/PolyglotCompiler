# Multi-tab editor, Quick Open, global search & Outline (demand 2026-04-28-25)

## Goal

Bring the editor surface up to modern IDE expectations:

* **Multi-tab editor with up to 4×4 splits** — tabs can be pinned,
  closed in bulk, dragged across split groups.
* **Quick Open (Ctrl+P)** — VS Code-style fuzzy file ranker with
  recent-file boost.
* **Symbol search** — `Ctrl+T` (workspace) and `Ctrl+Shift+O` (current
  file) backed by `polyls`.
* **Global find / replace** — regex / case / whole-word, glob include
  / exclude, capture-group replacement, streaming results.
* **Outline / Breadcrumbs / Minimap** — single source of truth for
  the symbol tree and the navigation breadcrumb.

The legacy command palette (`Ctrl+Shift+P`) is preserved unchanged.

## Components

| Concern             | File                                                                                                | Responsibility                                                              |
|---------------------|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------|
| Tab/group model     | [`tools/ui/common/editor/editor_group.{h,cpp}`](../../tools/ui/common/editor/editor_group.h)        | Pin, reorder, close, close-others, close-to-right.                          |
| Split grid          | [`tools/ui/common/editor/editor_grid.{h,cpp}`](../../tools/ui/common/editor/editor_grid.h)          | Up to 4×4 of `EditorGroup` cells; split / merge / move-tab.                 |
| Quick Open ranker   | [`tools/ui/common/quickopen/quick_open_ranker.{h,cpp}`](../../tools/ui/common/quickopen/quick_open_ranker.h) | Fuzzy path scoring with recency tie-break.                                  |
| Search engine       | [`tools/ui/common/search/global_search_engine.{h,cpp}`](../../tools/ui/common/search/global_search_engine.h) | Regex / glob / streaming sink / capture-group replace.                      |
| Outline tree        | [`tools/ui/common/outline/outline_model.{h,cpp}`](../../tools/ui/common/outline/outline_model.h)    | Filterable tree consumed by Outline panel + Breadcrumbs.                    |
| LSP — file symbols  | `polyls.HandleDocumentSymbol`                                                                       | Returns LSP `DocumentSymbol[]` for `.ploy` buffers.                         |
| LSP — workspace     | `polyls.HandleWorkspaceSymbol`                                                                      | Walks `SymbolIndex::Entries` and applies the query substring filter.        |

## Pipelines

### Quick Open

```
Ctrl+P pressed
   │
   ▼
collect candidates (workspace files ∪ MRU list with recency stamps)
   │
   ▼
RankQuickOpen(needle, candidates, limit)
   │   1. score each path, drop misses
   │   2. stable_sort by (score desc, recency desc, path asc)
   ▼
populate palette
```

### Global search

```
GlobalSearchOptions { pattern, regex, whole_word, case_sensitive,
                      include_globs, exclude_globs, max_results }
   │
   ▼
walk workspace ──► MatchesGlobFilter ──► open file ──► SearchInBuffer
                                                         │
                                                         ▼
                                                    GlobalSearchSink
                                                         │
                                                         ▼
                                                  results panel (streamed)
```

### Outline / Breadcrumbs

```
polyls textDocument/documentSymbol
   │
   ▼
OutlineModel.SetRoots( … )
   │                          ◄── Breadcrumbs(line, col) returns the
   ▼                              root→leaf chain for the cursor pos
Outline panel (filter via SetFilter)
```

## Tab pinning + bulk-close semantics

Pinned tabs sit at the front of the strip and are immune to
`CloseOthers` / `CloseToRight`.  `TogglePin` re-orders the tab to the
boundary between pinned and unpinned regions so the relative order
inside each region remains stable.  The split grid keeps tab moves
purely additive — a tab moved to a non-empty cell is appended, never
inserted, to keep keyboard navigation deterministic.

## Wire interplay with `polyls`

The server now advertises both `documentSymbolProvider` and
`workspaceSymbolProvider` in `initialize.result.capabilities`; the
editor consults the legend at hand-shake time and only routes the
`Ctrl+T` / `Ctrl+Shift+O` shortcuts through LSP when the capability
is present.

## Tests

* [`tests/unit/polyui/editor_group_test.cpp`](../../tests/unit/polyui/editor_group_test.cpp)
* [`tests/unit/polyui/quick_open_ranker_test.cpp`](../../tests/unit/polyui/quick_open_ranker_test.cpp)
* [`tests/unit/polyui/global_search_engine_test.cpp`](../../tests/unit/polyui/global_search_engine_test.cpp)
* [`tests/unit/polyui/outline_model_test.cpp`](../../tests/unit/polyui/outline_model_test.cpp)
* [`tests/unit/polyls/workspace_symbol_test.cpp`](../../tests/unit/polyls/workspace_symbol_test.cpp)
