# Multi-cursor, folding, formatter, snippets & EditorConfig (demand 2026-04-28-26)

## Goal

Bring power-editor amenities up to VS Code parity:

* **Multi-cursor + column selection** — `Alt+Click`, `Ctrl+Alt+↑/↓`,
  `Ctrl+D`, `Ctrl+Shift+L`, `Shift+Alt+drag`.
* **Code folding** — brace-balanced blocks, multi-line `/* … */`
  comments and explicit `// region` / `// endregion` markers.
* **Formatter** — `polyls` answers `textDocument/formatting`,
  `rangeFormatting`, `onTypeFormatting` for `.ploy`; foreign
  languages route to their own LSP.
* **Snippets** — VS Code-flavoured tabstops, choices, variables;
  user JSON files plus a built-in library.
* **EditorConfig** — minimal `.editorconfig` parser with full glob
  support; resolved settings drive the formatter and the status bar.

## Components

| Concern             | File                                                                                                 | Responsibility                                                              |
|---------------------|------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------|
| Multi-cursor model  | [`tools/ui/common/editing/multi_cursor.{h,cpp}`](../../tools/ui/common/editing/multi_cursor.h)       | Add/below/above, `SelectNextOccurrence`, column selection.                  |
| Folding             | [`tools/ui/common/editing/folding_model.{h,cpp}`](../../tools/ui/common/editing/folding_model.h)     | Brace + comment + region scanner.                                           |
| Formatter           | [`tools/ui/common/editing/format_engine.{h,cpp}`](../../tools/ui/common/editing/format_engine.h)     | `FormatPloy` re-indents by brace nesting.                                   |
| Snippets            | [`tools/ui/common/editing/snippet_engine.{h,cpp}`](../../tools/ui/common/editing/snippet_engine.h)   | `ExpandSnippet`, `SnippetLibrary::LoadJson` / `Match`.                      |
| EditorConfig        | [`tools/ui/common/editing/editor_config.{h,cpp}`](../../tools/ui/common/editing/editor_config.h)     | Section parser + recursive glob matcher.                                    |
| LSP — formatting    | `polyls.HandleFormatting` / `HandleRangeFormatting` / `HandleOnTypeFormatting`                       | Wraps `FormatPloy`, returns LSP `TextEdit[]`.                               |

## Pipelines

### Multi-cursor

```
Alt+Click ──► MultiCursor.AddCursorAt
Ctrl+Alt+↓ ──► MultiCursor.AddCursorBelow
Ctrl+D    ──► MultiCursor.SelectNextOccurrence
Ctrl+Shift+L ──► MultiCursor.SelectAllOccurrences
Shift+Alt+drag ──► MultiCursor.MakeColumnSelection
   │
   ▼
each cursor independently applies the next text mutation
```

### Format on save / on paste / on type

```
Ctrl+S / Ctrl+V / `\n` typed
   │
   ▼
client sends textDocument/{formatting,rangeFormatting,onTypeFormatting}
   │
   ▼
polyls.HandleFormatting → FormatPloy(text, opts) → TextEdit[]
   │
   ▼
client applies single full-document edit (range covering the buffer)
```

### Snippet expansion

```
"fn" prefix typed → SnippetLibrary.Match("fn") → SnippetEntry.body
                                                          │
                                                          ▼
                                                ExpandSnippet(body, vars)
                                                          │
                                                          ▼
                                                 SnippetExpansion {
                                                   text, tabstops
                                                 }
```

### EditorConfig resolution

```
file opened → walk parent dirs collecting `.editorconfig` until
              one with `root = true` (or filesystem root)
            │
            ▼
EditorConfigDocument.ResolveFor(relative_path)
            │                       merges later sections over earlier
            ▼
status bar:  "space 4 · EOL=lf · utf-8 · final-LF"
```

## Tests

* [`tests/unit/polyui/multi_cursor_test.cpp`](../../tests/unit/polyui/multi_cursor_test.cpp)
* [`tests/unit/polyui/folding_model_test.cpp`](../../tests/unit/polyui/folding_model_test.cpp)
* [`tests/unit/polyui/format_engine_test.cpp`](../../tests/unit/polyui/format_engine_test.cpp)
* [`tests/unit/polyui/snippet_engine_test.cpp`](../../tests/unit/polyui/snippet_engine_test.cpp)
* [`tests/unit/polyui/editor_config_test.cpp`](../../tests/unit/polyui/editor_config_test.cpp)
* [`tests/unit/polyls/formatting_test.cpp`](../../tests/unit/polyls/formatting_test.cpp)
