# Polyglot UI Theme System

`polyui` ships with a VS Code-style external theme system: themes are plain
`.polytheme.json` files (with optional sibling `.qss` overrides), discovered
from a 3-layer directory contract, validated against an embedded JSON Schema,
and applied at runtime through `ThemeService` (which composes on top of the
existing `ThemeManager`).

## File format — `.polytheme.json`

```jsonc
{
  "$schema": "polyglot://themes/theme_schema.json",
  "id":      "polyglot.dark",          // reverse-DNS, unique
  "name":    "Polyglot Dark",          // display name
  "type":    "dark",                   // "dark" | "light" | "high-contrast"
  "version": "1.0.0",
  "author":  "Polyglot Team",
  "description": "The default dark theme.",
  "extends": "polyglot.base",          // optional parent id
  "qss":     "polyglot.dark.qss",      // optional sibling QSS file
  "colors": {                          // VS-Code-style dotted keys
    "editor.background":   "#1e1e1e",
    "editor.foreground":   "#d4d4d4",
    "statusBar.background":"#007acc",
    /* …  ~60 keys, see api_reference.md … */
  },
  "tokenColors": [                     // TextMate-style scope rules
    { "scope": "comment",  "settings": { "foreground": "#6a9955" } },
    { "scope": "keyword",  "settings": { "foreground": "#569cd6" } },
    { "scope": ["string", "string.quoted"],
                          "settings": { "foreground": "#ce9178" } }
  ]
}
```

A theme may inherit from another via `extends`; resolution walks
parent → child so children override parents.  Cycles are detected and broken.

## 3-layer discovery

Themes are resolved with this precedence (highest wins):

| Layer       | Path                                                            |
| ----------- | --------------------------------------------------------------- |
| `workspace` | `<workspace_root>/.polyglot/themes/*.polytheme.json`            |
| `user`      | `<config>/PolyglotCompiler/themes/*.polytheme.json`             |
| `builtin`   | Embedded under `:/polyglot/themes/` (ships with the executable) |

When two themes share an `id`, the higher layer wins.  Built-in themes
cannot be uninstalled.

## Built-in themes (5)

| id                          | type            | inspiration         |
| --------------------------- | --------------- | ------------------- |
| `polyglot.dark`             | `dark`          | VS Code Dark+       |
| `polyglot.light`            | `light`         | VS Code Light+      |
| `polyglot.high-contrast`    | `high-contrast` | WCAG-AAA contrast   |
| `polyglot.solarized-dark`   | `dark`          | Ethan Schoonover    |
| `polyglot.solarized-light`  | `light`         |                     |

## Runtime API — `ThemeService`

```cpp
auto &svc = polyglot::tools::ui::ThemeService::Instance();
svc.SetWorkspaceRoot("/path/to/project");   // optional
svc.Scan();                                  // discover all 3 layers
svc.Activate("polyglot.dark");               // apply to QApplication
QString fg = svc.ResolveColor("editor.background");
QString kw = svc.ResolveTokenColor("keyword.control");
QObject::connect(&svc, &ThemeService::themeChanged,
                 widget, &MyWidget::OnThemeChanged);
```

The service emits `themeChanged(id)` whenever the active theme changes
(either via the API, by editing a file on disk — a `QFileSystemWatcher`
re-scans with a 500 ms debounce — or by the user picking another theme
in the Theme Manager dialog).  The chosen id is persisted to the user's
`workbench.colorTheme` setting.

Existing `ThemeManager::Instance().XStylesheet()` call sites keep working
unchanged: `ThemeService` registers each loaded theme into the legacy
`ThemeManager` map under its display name and invokes `SetActiveTheme`,
so all existing widgets re-style automatically.

## Theme Manager dialog

Open from **View → Theme Manager…**, the command palette (**Preferences:
Color Theme** or **Preferences: Manage Themes**), or the keyboard shortcut
`Ctrl+K Ctrl+T`.  The 3-pane dialog lets you:

- search & filter discovered themes by name and type;
- preview the active theme as a mock workbench (menu, editor, status bar);
- inspect every dotted color key in a tree view with colored swatches;
- **Activate** a theme, **Install** a `.polytheme.json` from disk, or
  **Uninstall** a user theme;
- **Duplicate** a theme into the user layer with `extends` set to the
  original, ready for editing;
- **Export** the currently selected theme into a self-contained
  `.polytheme.json` with all `extends` chains pre-flattened.

## Developer commands (command palette)

| Command id                                    | Title                                              |
| --------------------------------------------- | -------------------------------------------------- |
| `workbench.action.selectTheme`                | Preferences: Color Theme                           |
| `workbench.action.openColorTheme`             | Preferences: Manage Themes                         |
| `workbench.action.generateColorTheme`         | Developer: Generate Color Theme From Current Settings |
| `editor.action.inspectTMScopes`               | Developer: Inspect Editor Token                    |
| `workbench.action.inspectColorTheme`          | Developer: Inspect UI Color Key                    |

## Polyui CLI flags

```
polyui --theme <id|path>          Activate theme by id or .polytheme.json path
polyui --list-themes              Print every discovered theme to stdout
polyui --validate-theme <path>    Validate file, print JSON diagnostics
polyui --headless                 Use the offscreen QPA platform
polyui --headless --screenshot <out.png>
                                  Render once, save PNG, exit (CI friendly)
```

## Hot reload

`ThemeService` installs a `QFileSystemWatcher` on every directory and file
it loaded from.  When any `.polytheme.json` or its sibling `.qss` changes,
a 500 ms debounce timer fires `RescanNow()`, which re-loads everything and
re-activates the previously selected theme.  This means theme authors can
keep their JSON open in an external editor and see changes appear in
`polyui` within half a second.

## Authoring workflow

1. Run **Developer: Generate Color Theme From Current Settings** to fork
   the current theme into `~/.config/PolyglotCompiler/themes/`.
2. Open the resulting `.polytheme.json` in any editor.
3. Save — `polyui` reloads automatically (hot reload).
4. Use **Developer: Inspect UI Color Key** / **Inspect Editor Token** to
   discover the names of the keys you want to override.
5. When happy, distribute the file as-is, or as a `.polythemepack` (a
   ZIP containing one or more `.polytheme.json` files plus a manifest).

## Validation

The embedded schema (`:/polyglot/themes/theme_schema.json`) requires:

- `id`, `name`, `type` (one of `dark`/`light`/`high-contrast`)
- `colors`: an object whose values are `#rrggbb` or `#rrggbbaa`
- `tokenColors`: optional array of `{scope, settings.foreground}` rules

`ThemeService::ValidateString` performs structural checks; the CLI flag
`--validate-theme` prints a JSON diagnostic report and exits with a
non-zero status on failure.

## Removing hardcoded styling

The system intentionally complements (not replaces) `ThemeManager`.
Existing widgets call `ThemeManager::Instance().XStylesheet()` accessors
which are now driven by the active theme's color map.  When authoring
new widgets, use those accessors instead of writing literal QSS strings.