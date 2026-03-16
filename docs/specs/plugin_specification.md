# PolyglotCompiler Plugin Specification

Version: 1.0.0 | API Version: 1.0.0 | Date: 2026-03-15

---

## 1. Overview

PolyglotCompiler supports runtime-loadable plugins through a stable **C ABI** shared-library interface. Plugins can extend the compiler and IDE with new language frontends, optimization passes, code formatters, linters, debugger integrations, and UI panels.

### Key Properties

| Property | Value |
|----------|-------|
| API version | 1.0.0 |
| Library format | `.dll` (Windows) / `.so` (Linux) / `.dylib` (macOS) |
| Naming convention | `polyplug_<name>` (e.g. `polyplug_go.dll`) |
| ABI | C (extern "C"), no C++ name mangling |
| Memory model | Plugin allocates, plugin owns; host copies if needed |
| Thread safety | Host calls into plugin from a single thread unless documented otherwise |

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────┐
│                  PolyglotCompiler Host              │
│                                                     │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────┐  │
│  │PluginManager│  │CompilerService│ │  polyui    │  │
│  └──────┬──────┘  └──────┬───────┘  └─────┬──────┘  │
│         │                │                │         │
│         ▼                ▼                ▼         │
│  ┌─────────────────────────────────────────────┐    │
│  │           Host Services (C ABI)             │    │
│  │  log · emit_diagnostic · get/set_setting    │    │
│  │  open_file · register_file_type               │    │
│  └──────────────────┬──────────────────────────┘    │
│                     │                               │
└─────────────────────┼───────────────────────────────┘
                      │ dlopen / LoadLibrary
          ┌───────────┼───────────┐
          ▼           ▼           ▼
    ┌───────────┐┌───────────┐┌───────────┐
    │ Plugin A  ││ Plugin B  ││ Plugin C  │
    │ (Language)││ (Linter)  ││(Formatter)│
    └───────────┘└───────────┘└───────────┘
```

### Lifecycle

```
Load  →  get_info()  →  create()  →  activate()  →  [use]  →  deactivate()  →  destroy()  →  Unload
```

1. **Load**: Host calls `dlopen` / `LoadLibrary` to load the shared library.
2. **get_info()**: Host reads static metadata (id, name, version, capabilities).
3. **create()**: Host allocates an instance, passing the host context and service table.
4. **activate()**: Host signals the plugin to initialise its resources. Returns 0 on success.
5. **Use**: Host queries capability providers and invokes plugin callbacks.
6. **deactivate()**: Host signals the plugin to release resources.
7. **destroy()**: Host frees the plugin instance.
8. **Unload**: Host calls `dlclose` / `FreeLibrary`.

---

## 3. Mandatory Exports

Every plugin shared library **MUST** export these three functions:

### 3.1 `polyglot_plugin_get_info`

```c
const PolyglotPluginInfo *polyglot_plugin_get_info(void);
```

Returns a pointer to static metadata. The returned pointer must remain valid for the lifetime of the library. The `api_version` field must equal `POLYGLOT_PLUGIN_API_VERSION` (currently 1).

### 3.2 `polyglot_plugin_create`

```c
PolyglotPlugin *polyglot_plugin_create(
    const PolyglotHostContext  *ctx,
    const PolyglotHostServices *host);
```

Allocates and returns a new plugin instance. The host passes a context handle and a table of service functions. The plugin should store these for later use. Returns `NULL` on failure.

### 3.3 `polyglot_plugin_destroy`

```c
void polyglot_plugin_destroy(PolyglotPlugin *plugin);
```

Frees the instance previously created by `polyglot_plugin_create`.

---

## 4. Optional Exports

These functions are called only if the plugin declares the corresponding capability flag.

### 4.1 Lifecycle Hooks

```c
int  polyglot_plugin_activate(PolyglotPlugin *plugin);    // return 0 on success
void polyglot_plugin_deactivate(PolyglotPlugin *plugin);
```

### 4.2 Language Provider (`POLYGLOT_CAP_LANGUAGE`)

```c
const PolyglotLanguageProvider *polyglot_plugin_get_language(PolyglotPlugin *plugin);
```

Returns a static struct with:
- `language_name` — unique language identifier (e.g. `"go"`)
- `file_extensions` — NULL-terminated array (e.g. `{".go", NULL}`)
- `tokenize()` — tokenize source for syntax highlighting
- `analyze()` — emit diagnostics via `host->emit_diagnostic`
- `compile_to_ir()` — compile source to IR text

### 4.3 Optimizer Passes (`POLYGLOT_CAP_OPTIMIZER`)

```c
const PolyglotOptimizerPass **polyglot_plugin_get_passes(
    PolyglotPlugin *plugin, uint32_t *out_count);
```

Returns an array of pass descriptors. Each pass has a `run()` callback that transforms IR text.

### 4.4 Code Actions (`POLYGLOT_CAP_CODE_ACTION`)

```c
const PolyglotCodeAction **polyglot_plugin_get_code_actions(
    PolyglotPlugin *plugin, uint32_t *out_count);
```

### 4.5 Formatter (`POLYGLOT_CAP_FORMATTER`)

```c
const PolyglotFormatter *polyglot_plugin_get_formatter(PolyglotPlugin *plugin);
```

### 4.6 Linter (`POLYGLOT_CAP_LINTER`)

```c
const PolyglotLinter *polyglot_plugin_get_linter(PolyglotPlugin *plugin);
```

---

## 5. Capability Flags

| Flag | Value | Description |
|------|-------|-------------|
| `POLYGLOT_CAP_LANGUAGE` | `1 << 0` | Adds a new language frontend |
| `POLYGLOT_CAP_OPTIMIZER` | `1 << 1` | Adds optimization passes |
| `POLYGLOT_CAP_BACKEND` | `1 << 2` | Adds a code-generation backend |
| `POLYGLOT_CAP_TOOL` | `1 << 3` | Adds a CLI tool or subcommand |
| `POLYGLOT_CAP_UI_PANEL` | `1 << 4` | Adds an IDE dock panel |
| `POLYGLOT_CAP_SYNTAX_THEME` | `1 << 5` | Adds a syntax colour theme |
| `POLYGLOT_CAP_FILE_TYPE` | `1 << 6` | Adds file-type associations |
| `POLYGLOT_CAP_CODE_ACTION` | `1 << 7` | Adds refactor / code actions |
| `POLYGLOT_CAP_FORMATTER` | `1 << 8` | Adds a code formatter |
| `POLYGLOT_CAP_LINTER` | `1 << 9` | Adds a linting pass |
| `POLYGLOT_CAP_DEBUGGER` | `1 << 10` | Adds debugger integration |

Multiple capabilities can be combined: `POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_LINTER`.

---

## 6. Host Services

The host provides a `PolyglotHostServices` struct with the following callbacks:

| Service | Signature | Description |
|---------|-----------|-------------|
| `log` | `void (*)(ctx, level, message)` | Write to the host's log output |
| `emit_diagnostic` | `void (*)(ctx, diag)` | Report an error/warning/note to the IDE |
| `get_setting` | `const char *(*)(ctx, key)` | Read a plugin-scoped persistent setting |
| `set_setting` | `void (*)(ctx, key, value)` | Write a plugin-scoped persistent setting |
| `open_file` | `void (*)(ctx, path, line)` | Open a file in the IDE editor |
| `register_file_type` | `void (*)(ctx, ext, language)` | Associate a file extension with a language |
| `get_workspace_root` | `const char *(*)(ctx)` | Query the current workspace directory |

### Settings Scope

Each plugin has its own key namespace. A plugin with id `com.example.myplugin` that calls `set_setting(ctx, "indent_size", "4")` will not conflict with another plugin's `indent_size` setting.

---

## 7. Plugin Discovery

The host searches the following directories for plugin libraries:

1. `<app_dir>/plugins/` — next to the `polyui` / `polyc` executable
2. **Linux**: `$XDG_DATA_HOME/polyglot/plugins/` (default: `~/.local/share/polyglot/plugins/`)
3. **macOS**: `~/Library/Application Support/PolyglotCompiler/plugins/`
4. **Windows**: `%APPDATA%/PolyglotCompiler/plugins/`
5. User-configured directories via the Settings → Plugins page

Only shared libraries matching the `polyplug_*` naming prefix are loaded.

---

## 8. Example: Minimal Plugin (C)

```c
#include "common/include/plugins/plugin_api.h"
#include <stdlib.h>

typedef struct {
    const PolyglotHostContext  *ctx;
    const PolyglotHostServices *host;
} MyPlugin;

static const PolyglotPluginInfo kInfo = {
    .api_version     = POLYGLOT_PLUGIN_API_VERSION,
    .id              = "com.example.hello",
    .name            = "Hello Plugin",
    .version         = "1.0.0",
    .author          = "Example Author",
    .description     = "A minimal example plugin that logs on activation.",
    .license         = "MIT",
    .homepage        = NULL,
    .capabilities    = POLYGLOT_CAP_NONE,
    .min_host_version = "1.0.0",
};

POLYGLOT_EXPORT
const PolyglotPluginInfo *polyglot_plugin_get_info(void) {
    return &kInfo;
}

POLYGLOT_EXPORT
PolyglotPlugin *polyglot_plugin_create(
        const PolyglotHostContext  *ctx,
        const PolyglotHostServices *host) {
    MyPlugin *p = (MyPlugin *)calloc(1, sizeof(MyPlugin));
    p->ctx  = ctx;
    p->host = host;
    return (PolyglotPlugin *)p;
}

POLYGLOT_EXPORT
void polyglot_plugin_destroy(PolyglotPlugin *plugin) {
    free(plugin);
}

POLYGLOT_EXPORT
int polyglot_plugin_activate(PolyglotPlugin *plugin) {
    MyPlugin *p = (MyPlugin *)plugin;
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Hello from the plugin!");
    return 0;
}

POLYGLOT_EXPORT
void polyglot_plugin_deactivate(PolyglotPlugin *plugin) {
    MyPlugin *p = (MyPlugin *)plugin;
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Goodbye from the plugin!");
}
```

Build:
```bash
# Linux
gcc -shared -fPIC -o polyplug_hello.so hello_plugin.c -I /path/to/PolyglotCompiler

# macOS
clang -shared -fPIC -o polyplug_hello.dylib hello_plugin.c -I /path/to/PolyglotCompiler

# Windows (MSVC)
cl /LD hello_plugin.c /I C:\path\to\PolyglotCompiler /Fe:polyplug_hello.dll
```

---

## 9. Example: Language Plugin (C++)

```cpp
#include "common/include/plugins/plugin_api.h"
#include <cstring>
#include <cstdlib>
#include <string>

struct GoPlugin {
    const PolyglotHostContext  *ctx;
    const PolyglotHostServices *host;
};

// -- Tokenizer --
static uint32_t GoTokenize(const char *source, size_t len,
                           PolyglotToken *out, uint32_t max) {
    // Simplified tokenizer — real implementation would use a proper lexer.
    uint32_t count = 0;
    // ... tokenization logic ...
    (void)source; (void)len; (void)out; (void)max;
    return count;
}

// -- Analyzer --
static void GoAnalyze(const PolyglotHostContext *ctx,
                      const PolyglotHostServices *host,
                      const char *source, size_t len,
                      const char *filename) {
    // ... analysis logic, emit diagnostics via host->emit_diagnostic ...
    (void)ctx; (void)host; (void)source; (void)len; (void)filename;
}

// -- Compiler --
static char *GoCompileToIR(const PolyglotHostContext *ctx,
                           const PolyglotHostServices *host,
                           const char *source, size_t len,
                           const char *filename) {
    // ... compile to IR text ...
    (void)ctx; (void)host; (void)source; (void)len; (void)filename;
    return nullptr;
}

static const char *kGoExtensions[] = {".go", nullptr};

static const PolyglotLanguageProvider kGoProvider = {
    .language_name  = "go",
    .file_extensions = kGoExtensions,
    .tokenize       = GoTokenize,
    .analyze        = GoAnalyze,
    .compile_to_ir  = GoCompileToIR,
};

static const PolyglotPluginInfo kInfo = {
    .api_version     = POLYGLOT_PLUGIN_API_VERSION,
    .id              = "com.example.go-frontend",
    .name            = "Go Language Support",
    .version         = "0.1.0",
    .author          = "Example Author",
    .description     = "Adds Go language support to PolyglotCompiler.",
    .license         = "Apache-2.0",
    .homepage        = nullptr,
    .capabilities    = POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_FILE_TYPE,
    .min_host_version = "1.0.0",
};

extern "C" {

POLYGLOT_EXPORT const PolyglotPluginInfo *polyglot_plugin_get_info(void) {
    return &kInfo;
}

POLYGLOT_EXPORT PolyglotPlugin *polyglot_plugin_create(
        const PolyglotHostContext *ctx, const PolyglotHostServices *host) {
    auto *p = new GoPlugin{ctx, host};
    return reinterpret_cast<PolyglotPlugin *>(p);
}

POLYGLOT_EXPORT void polyglot_plugin_destroy(PolyglotPlugin *plugin) {
    delete reinterpret_cast<GoPlugin *>(plugin);
}

POLYGLOT_EXPORT int polyglot_plugin_activate(PolyglotPlugin *plugin) {
    auto *p = reinterpret_cast<GoPlugin *>(plugin);
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Go frontend activated");
    return 0;
}

POLYGLOT_EXPORT void polyglot_plugin_deactivate(PolyglotPlugin *) {}

POLYGLOT_EXPORT const PolyglotLanguageProvider *polyglot_plugin_get_language(
        PolyglotPlugin *) {
    return &kGoProvider;
}

}  // extern "C"
```

---

## 10. Security Considerations

- Plugins run in the **same process** as the host. A malicious plugin has full access to memory.
- Only load plugins from trusted sources.
- The host validates `api_version` before creating an instance.
- Plugin-scoped settings are sandboxed per plugin id.
- Future versions may add signature verification for plugin binaries.

---

## 11. Versioning Policy

- The `api_version` field is incremented when **breaking changes** are made to the C ABI.
- Additive changes (new optional exports) do **not** bump the API version.
- Plugins built against API version N are guaranteed to work with hosts that support version N.
- The host rejects plugins whose `api_version` does not match `POLYGLOT_PLUGIN_API_VERSION`.

---

## 12. File Locations

| File | Purpose |
|------|---------|
| `common/include/plugins/plugin_api.h` | Public C API (stable ABI) |
| `common/include/plugins/plugin_manager.h` | C++ plugin manager interface |
| `common/src/plugins/plugin_manager.cpp` | Plugin manager implementation |
| `tests/unit/plugins/plugin_manager_test.cpp` | Unit tests |
