# Extension API Reference

This is the canonical reference for the PolyUI extension system.
It describes the `extension.json` manifest, the activation model,
the capability sandbox, every contribution kind and the host
lifecycle calls available to extension authors.

## 1. Manifest (`extension.json`)

Every extension is a self-describing unit: an `extension.json`
file plus a payload (a native dynamic library or a JavaScript /
TypeScript bundle).  The host calls `ParseManifest` to ingest the
file; an extension is rejected when any required field is missing.

| Field                  | Required | Description |
| ---------------------- | -------- | --- |
| `id`                   | yes      | Unique extension id, conventionally `publisher.name`. |
| `version`              | yes      | Semantic version (`MAJOR.MINOR.PATCH`). |
| `entry_point` / `main` | yes      | Path to the payload (relative to the manifest). |
| `name`                 | no       | Human-readable name. |
| `publisher`            | no       | Publisher id. |
| `description`          | no       | One-line description shown in the UI. |
| `loader`               | no       | `native` (default) or `javascript`. |
| `activation`           | no       | Array of activation triggers (see §2). |
| `capabilities`         | no       | Array of capability names (see §3). |
| `contributes`          | no       | Object mapping contribution kinds to arrays (see §4). |

Loaders honour the underlying payload type:

* **`native`** — a shared library exporting the host ABI. Compiled
  C / C++ extensions live here.
* **`javascript`** — a bundled JS/TS file evaluated in the
  embedded engine (QuickJS by default; V8 when the build was
  configured with `POLYUI_V8=ON`).

### 1.1 Version comparison

`CompareVersion("1.10.0", "1.2.3")` returns a positive value;
`CompareVersion("1.0", "1.0.0")` returns zero. Missing components
default to `0`.  Newer versions replace older ones during install
and the host preserves no leftover contributions from the old
manifest.

## 2. Activation events

The host activates an extension when at least one of its
activation triggers matches.  Each trigger is either a string
(treated as `event` with no argument) or an object with `event`
and an optional `argument`.

| Event         | Argument           | Trigger semantics |
| ------------- | ------------------ | --- |
| `onStartup`   | —                  | Fires unconditionally at IDE startup. |
| `onLanguage`  | language id        | Fires the first time a buffer of that language is opened. |
| `onCommand`   | command id         | Fires when the command is invoked from the palette / keybinding. |
| `onView`      | view id            | Fires the first time the view is rendered. |
| `onDebug`     | debug type         | Fires when a debug session of the matching type starts. |
| `onFileOpen`  | glob / extension   | Fires for the first matching file. |

`MatchesActivationEvent(id, event, argument)` returns true when
any trigger of the extension matches; an empty trigger argument
matches every concrete argument.

## 3. Capability sandbox

Every capability the extension wants must appear in
`capabilities`.  The `CapabilityGate` blocks activation when any
required capability has not been granted by the user.

| Capability    | Allows |
| ------------- | --- |
| `filesystem`  | Reading and writing files outside the extension's own folder. |
| `network`     | Initiating outbound network requests. |
| `process`     | Spawning child processes. |
| `clipboard`   | Reading the system clipboard (writes are always allowed). |
| `secrets`     | Reading or writing entries in the user secret store. |

The gate is per-extension and per-capability; granting `network`
to one extension does not grant it to any other.

## 4. Contributions

`contributes` is an object mapping contribution kinds to arrays.
The host normalises every entry into a `Contribution` value
holding `kind`, `id` and free-form string `properties`.  Two
contributions sharing the same `(kind, id)` collapse into one:
the most recently activated extension wins.

Recognised keys: `commands`, `keybindings`, `menus`, `panels`,
`views`, `statusBarItems`, `themes`, `languageClients`,
`debugAdapters`, `fileIconThemes`, `formatters`, `snippets`,
`tasks`, `refactorProviders`.

Each entry must carry an `id`.  The optional `title` field is
used in menus and the command palette; any other string field is
preserved verbatim as a property so contribution-kind-specific
metadata round-trips through the manifest.

## 5. Host lifecycle

| Call                              | Effect |
| --------------------------------- | --- |
| `Install(manifest)`               | Records the extension. Fails when the same id is already installed at a newer or equal version. |
| `Uninstall(id)`                   | Drops the record and every contribution registered by it. |
| `Activate(id)`                    | Verifies capabilities, registers contributions, transitions the record to `kActivated`. |
| `Deactivate(id)`                  | Drops the contributions, transitions the record back to `kInstalled`. |
| `Reload(id)`                      | `Deactivate` then `Activate`; used after settings or grants change. |
| `MatchesActivationEvent(...)`     | Used by the IDE shell to know when to call `Activate`. |
| `Contributions()` / `ContributionsOfKind(kind)` | The deduplicated registry. |

## 6. Worked example

```json
{
  "id": "polyglot.sample",
  "name": "Sample",
  "version": "0.2.1",
  "publisher": "polyglot",
  "main": "out/extension.js",
  "loader": "javascript",
  "activation": [
    "onStartup",
    { "event": "onLanguage", "argument": "ploy" }
  ],
  "capabilities": ["filesystem", "network"],
  "contributes": {
    "commands": [
      { "id": "sample.hello", "title": "Hello" }
    ],
    "keybindings": [
      { "id": "sample.hello.key", "key": "Ctrl+H" }
    ]
  }
}
```

## 7. Tests

The reference behaviour is pinned by
[`tests/unit/polyui/extension_api_test.cpp`](../../tests/unit/polyui/extension_api_test.cpp):
loader / activation / capability name round-trips, semver
comparison with uneven part counts, full manifest parsing, the
capability gate, install / activate / contribution registry,
contribution dedup across two extensions and activation event
matching with both wildcard and concrete arguments.

The full polyui suite runs at 1096 assertions across 216 cases.
