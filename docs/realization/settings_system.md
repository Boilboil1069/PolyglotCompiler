# VS Code-style Settings System — Implementation Notes

> Realization document for demand **2026-04-27-4** (JSON-first settings).
> Covers all 11 sections of the demand entry.

---

## 1. Goal

Replace the legacy `QSettings`-backed preferences with a **JSON-first**, three-layer
settings system inspired by VS Code:

- `settings.json` is the **single source of truth**.
- Both the IDE (`polyui`) and every CLI tool (`polyc`, `polyld`, `polyrt`,
  `polytopo`, `polybench`) load the same file through the same shared loader.
- A JSON Schema describes every key, drives bilingual descriptions, validation,
  and the auto-generated form view.

---

## 2. Layer Model

| Priority | Layer       | Path (Windows)                                            | Path (Linux/macOS)                                  |
|---------:|-------------|-----------------------------------------------------------|-----------------------------------------------------|
| 1 (lo)   | Default     | embedded resource `:/polyglot/settings/default_settings.json` | same |
| 2        | User        | `%APPDATA%\PolyglotCompiler\settings.json`                 | `~/.config/PolyglotCompiler/settings.json` (or `$XDG_CONFIG_HOME`) — macOS uses `~/Library/Application Support/PolyglotCompiler/` |
| 3 (hi)   | Workspace   | `<workspace>/.polyglot/settings.json`                      | same |

`DeepMerge(default, user, workspace)` produces the **effective tree**.
For canonical storage we use **flat dotted keys** at the top level
(e.g. `{"editor.tabSize": 4}`); `GetByDottedKey()` falls back to nested paths
for compatibility.

---

## 3. Components

| Component                | Location                                                  | Purpose                                                                 |
|--------------------------|-----------------------------------------------------------|-------------------------------------------------------------------------|
| `polyglot_tools_settings` (static lib) | `tools/common/{include,src}/effective_settings_loader.{h,cpp}` | Pure-C++ loader, schema validator, dotted-key get/set, CLI flag helper. |
| `SettingsService`        | `tools/ui/common/{include,src}/settings_service.{h,cpp}`  | Qt singleton, `QFileSystemWatcher` (200 ms debounce), `Get/Set/Reset`, signals `settingsChanged` & `settingsReloaded`, `MigrateLegacyQSettings()`. |
| `KeybindingService`      | `tools/ui/common/{include,src}/keybinding_service.{h,cpp}`| Command registry, default + user bindings, chord parser, `when` expression evaluator (`!`, `&&`, `||`, parens, idents). |
| `CommandPalette`         | `tools/ui/common/{include,src}/command_palette.{h,cpp}`   | `QDialog` with fuzzy filter; bound to `Ctrl+Shift+P`.                   |
| `SettingsPage`           | `tools/ui/common/{include,src}/settings_page.{h,cpp}`     | VS Code-style two-pane editor (left namespaces, right schema-driven form); per-field source badge; “Open User/Workspace/Defaults JSON” buttons. |

The CLI loader and the Qt service share the **same** `polyglot_tools_settings`
library — there is no second JSON parser or schema validator.

---

## 4. Built-in Commands

Registered in `MainWindow::RegisterBuiltinCommands()`:

| Command id                                         | Default keybinding | Action                                                  |
|----------------------------------------------------|--------------------|---------------------------------------------------------|
| `workbench.action.openSettings`                    | —                  | Open the settings form (`SettingsPage`).                |
| `workbench.action.openSettingsJson`                | —                  | Open user `settings.json` in a tab.                     |
| `workbench.action.openWorkspaceSettingsJson`       | —                  | Open `<workspace>/.polyglot/settings.json` (created on demand). |
| `workbench.action.openDefaultSettingsJson`         | —                  | Open the materialised default tree (read-only).         |
| `workbench.action.openKeybindings`                 | —                  | Open `keybindings.json`.                                |
| `workbench.action.files.save` / `saveAs` / `openFile` / `newUntitled` | as in editor menu | File operations exposed to the palette. |
| `workbench.action.showCommands`                    | `Ctrl+Shift+P`     | Open the command palette.                               |
| `editor.action.toggleMarkdownPreview`              | —                  | Toggle Markdown preview pane (demand 2026-04-27-2).     |

User keybindings live in `keybindings.json` next to `settings.json` and are
hot-reloaded by `KeybindingService::LoadUserKeybindings()`.

---

## 5. CLI Integration

Every CLI tool calls, at the very top of `main()`:

```cpp
if (auto rc = polyglot::tools::common::HandleSettingsCliFlags(argc, argv);
    rc.has_value()) {
  return *rc;
}
```

The helper recognises:

- `--settings <path>` — override the user file with an explicit one.
- `--print-effective-settings` — print the merged JSON to stdout and exit 0.

`polyc::ParseArgs()` was extended to skip these tokens so they do not produce
an "unknown argument" diagnostic.

---

## 6. Schema-driven Form

`SettingsPage` reads `settings_schema.json` and dispatches per `type`:

| Schema type             | Widget               |
|-------------------------|----------------------|
| `boolean`               | `QCheckBox`          |
| `integer` (+min/max)    | `QSpinBox`           |
| `number`                | `QDoubleSpinBox`     |
| `string` + `enum`       | `QComboBox`          |
| `string`                | `QLineEdit`          |
| `array` / `object`      | `QLineEdit` (JSON literal) |

Each row shows a **source badge** (`(default)` / `(user)` / `(workspace)`) so
the user knows which layer currently provides the value.

---

## 7. Migration from `QSettings`

`SettingsService::MigrateLegacyQSettings()` runs once on first launch:

| Legacy QSettings key             | New JSON key                |
|----------------------------------|-----------------------------|
| `appearance/font_family`         | `editor.fontFamily`         |
| `appearance/font_size`           | `editor.fontSize`           |
| `editor/tab_width`               | `editor.tabSize`            |
| `editor/insert_spaces`           | `editor.insertSpaces`       |
| `editor/word_wrap`               | `editor.wordWrap`           |
| `editor/auto_indent`             | `editor.autoIndent`         |
| `editor/line_numbers`            | `editor.lineNumbers`        |
| `topology/layout_mode`           | `topology.layoutAlgorithm`  |
| ...                              | ...                         |

The original QSettings store is **not** mutated; it is mirrored and a marker
file (`settings.json.qsettings.bak`) is written so the migration is idempotent.

---

## 8. Hot Reload

`SettingsService` uses `QFileSystemWatcher` with a 200 ms `QTimer` debounce.
On reload:

1. The new JSON is parsed and validated.
2. A diff between the old and new effective trees is computed.
3. For each changed key the signal `settingsChanged(key, oldValue, newValue)`
   is emitted.
4. `settingsReloaded()` is emitted last so global listeners can refresh.

`MainWindow::ApplyEditorSettings()` reacts to the signal to repaint editors
without a restart.

---

## 9. Tests

`tests/unit/tools/settings_loader_test.cpp` (test binary `test_settings`)
covers:

- `DeepMerge` recursive object merging.
- Flat dotted-key round-trip.
- Schema validation: `type`, `enum`, `minimum`, `maximum`.
- Three-layer precedence (`workspace` > `user` > `default`).
- Broken user JSON → diagnostic, defaults still applied.
- Path resolvers return platform-appropriate locations.

The Qt-bound services (`SettingsService`, `KeybindingService`,
`CommandPalette`, `SettingsPage`) are exercised through the existing
`test_topology_ui` Qt-conditional test harness pattern; pure-C++ logic in
`KeybindingService::ParseChord` and the `when`-expression evaluator is
unit-tested by the same `test_settings` binary in follow-up tickets.

---

## 10. File Layout Summary

```
tools/
  common/
    include/effective_settings_loader.h         ← shared API
    src/effective_settings_loader.cpp
  ui/common/
    resources/
      default_settings.json                     ← layer 1
      settings_schema.json                      ← schema
      settings_resources.qrc
    include/
      settings_service.h
      keybinding_service.h
      command_palette.h
      settings_page.h
    src/
      settings_service.cpp
      keybinding_service.cpp
      command_palette.cpp
      settings_page.cpp
tests/unit/tools/settings_loader_test.cpp       ← `test_settings`
```

---

## 11. Future Work

- Materialise a JSON-schema-aware editor with hover descriptions and
  auto-completion in the JSON tab (current implementation reuses the regular
  syntax-highlighted editor).
- Profile-based settings (`workbench.profile`) to switch entire JSON trees.
- Sync settings via Git or cloud (out of scope for this demand).
