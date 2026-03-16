// plugin_api.h — Public C API for PolyglotCompiler plugins.
//
// Plugins are shared libraries (.so / .dylib / .dll) that export the
// functions declared here.  The host loads them at runtime via dlopen /
// LoadLibrary and calls the entry points through this stable C ABI.
//
// Every plugin MUST export:
//   polyglot_plugin_get_info   — return static metadata
//   polyglot_plugin_create     — allocate and return a plugin instance
//   polyglot_plugin_destroy    — free the instance
//
// The host owns the lifecycle: create → activate → (use) → deactivate → destroy.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Version helpers
// ============================================================================

#define POLYGLOT_PLUGIN_API_VERSION 1

// ============================================================================
// Capability flags — bitwise OR to declare what the plugin provides
// ============================================================================

typedef enum PolyglotPluginCap {
    POLYGLOT_CAP_NONE             = 0,
    POLYGLOT_CAP_LANGUAGE         = 1 << 0,   // Adds a new language frontend
    POLYGLOT_CAP_OPTIMIZER        = 1 << 1,   // Adds optimization passes
    POLYGLOT_CAP_BACKEND          = 1 << 2,   // Adds a code-generation backend
    POLYGLOT_CAP_TOOL             = 1 << 3,   // Adds a CLI tool / subcommand
    POLYGLOT_CAP_UI_PANEL         = 1 << 4,   // Adds an IDE dock panel
    POLYGLOT_CAP_SYNTAX_THEME     = 1 << 5,   // Adds a syntax colour theme
    POLYGLOT_CAP_FILE_TYPE        = 1 << 6,   // Adds file-type associations
    POLYGLOT_CAP_CODE_ACTION      = 1 << 7,   // Adds refactor / code actions
    POLYGLOT_CAP_FORMATTER        = 1 << 8,   // Adds a code formatter
    POLYGLOT_CAP_LINTER           = 1 << 9,   // Adds a linting pass
    POLYGLOT_CAP_DEBUGGER         = 1 << 10,  // Adds debugger integration
} PolyglotPluginCap;

// ============================================================================
// Plugin metadata — returned by polyglot_plugin_get_info()
// ============================================================================

typedef struct PolyglotPluginInfo {
    uint32_t    api_version;       // Must equal POLYGLOT_PLUGIN_API_VERSION
    const char *id;                // Unique identifier, e.g. "com.example.myformatter"
    const char *name;              // Human-readable name
    const char *version;           // SemVer string, e.g. "1.0.0"
    const char *author;            // Author or organisation
    const char *description;       // Short description
    const char *license;           // SPDX identifier, e.g. "MIT"
    const char *homepage;          // URL (may be NULL)
    uint32_t    capabilities;      // Bitwise OR of PolyglotPluginCap values
    const char *min_host_version;  // Minimum PolyglotCompiler version, e.g. "1.0.0"
} PolyglotPluginInfo;

// ============================================================================
// Host context — services provided by the host to the plugin
// ============================================================================

typedef struct PolyglotHostContext PolyglotHostContext;

// Log severity levels
typedef enum PolyglotLogLevel {
    POLYGLOT_LOG_DEBUG   = 0,
    POLYGLOT_LOG_INFO    = 1,
    POLYGLOT_LOG_WARNING = 2,
    POLYGLOT_LOG_ERROR   = 3,
} PolyglotLogLevel;

// Diagnostic severity
typedef enum PolyglotDiagSeverity {
    POLYGLOT_DIAG_NOTE    = 0,
    POLYGLOT_DIAG_WARNING = 1,
    POLYGLOT_DIAG_ERROR   = 2,
} PolyglotDiagSeverity;

// A single diagnostic emitted by a plugin
typedef struct PolyglotDiagnostic {
    const char           *file;
    uint32_t              line;
    uint32_t              column;
    PolyglotDiagSeverity  severity;
    const char           *message;
    const char           *code;       // Optional rule / error code (may be NULL)
} PolyglotDiagnostic;

// Token produced by a language plugin's tokenizer
typedef struct PolyglotToken {
    uint32_t    line;
    uint32_t    column;
    uint32_t    length;
    const char *kind;      // "keyword", "identifier", "number", "string", etc.
    const char *lexeme;    // The raw text — UTF-8 encoded
} PolyglotToken;

// Host-provided function table — the plugin calls these to interact with
// the compiler / IDE.
typedef struct PolyglotHostServices {
    // Logging
    void (*log)(const PolyglotHostContext *ctx,
                PolyglotLogLevel level,
                const char *message);

    // Emit a diagnostic visible in the IDE's problems view
    void (*emit_diagnostic)(const PolyglotHostContext *ctx,
                            const PolyglotDiagnostic *diag);

    // Read a plugin-scoped setting.  Returns NULL if not set.
    const char *(*get_setting)(const PolyglotHostContext *ctx,
                               const char *key);

    // Write a plugin-scoped setting (persisted across sessions).
    void (*set_setting)(const PolyglotHostContext *ctx,
                        const char *key,
                        const char *value);

    // Request the host to open a file in the editor (UI plugins only).
    void (*open_file)(const PolyglotHostContext *ctx,
                      const char *path,
                      uint32_t line);

    // Register a file extension with a language name.
    void (*register_file_type)(const PolyglotHostContext *ctx,
                               const char *extension,
                               const char *language);

    // Query the current workspace root (may be NULL if none is open).
    const char *(*get_workspace_root)(const PolyglotHostContext *ctx);
} PolyglotHostServices;

// ============================================================================
// Language provider callbacks (CAP_LANGUAGE)
// ============================================================================

typedef struct PolyglotLanguageProvider {
    // Unique language name used in file associations, e.g. "go"
    const char *language_name;

    // File extensions this provider handles (NULL-terminated array)
    const char **file_extensions;

    // Tokenize `source` (UTF-8, length `len`).
    // Write tokens into `out_tokens` (caller-allocated, capacity `max_tokens`).
    // Return the number of tokens written.
    uint32_t (*tokenize)(const char *source, size_t len,
                         PolyglotToken *out_tokens, uint32_t max_tokens);

    // Analyse `source` and emit diagnostics via `host->emit_diagnostic`.
    void (*analyze)(const PolyglotHostContext *ctx,
                    const PolyglotHostServices *host,
                    const char *source, size_t len,
                    const char *filename);

    // Compile `source` to IR text (host-defined format).
    // Returns a malloc'd string the host will free, or NULL on failure.
    char *(*compile_to_ir)(const PolyglotHostContext *ctx,
                           const PolyglotHostServices *host,
                           const char *source, size_t len,
                           const char *filename);
} PolyglotLanguageProvider;

// ============================================================================
// Optimizer pass callbacks (CAP_OPTIMIZER)
// ============================================================================

typedef struct PolyglotOptimizerPass {
    const char *pass_name;   // e.g. "my-custom-dce"
    const char *description;

    // Transform IR text in-place.  `ir` is a malloc'd buffer the plugin may
    // realloc.  Returns the (potentially new) buffer, or NULL on error.
    char *(*run)(const PolyglotHostContext *ctx,
                 const PolyglotHostServices *host,
                 char *ir, size_t *ir_len);
} PolyglotOptimizerPass;

// ============================================================================
// Code action / formatter / linter callbacks
// ============================================================================

typedef struct PolyglotCodeAction {
    const char *action_id;
    const char *label;         // Human-readable label shown in the IDE

    // Apply the action to `source`.  Returns a malloc'd replacement string
    // (full file), or NULL to indicate no change.
    char *(*apply)(const PolyglotHostContext *ctx,
                   const PolyglotHostServices *host,
                   const char *source, size_t len,
                   const char *filename,
                   uint32_t sel_start_line, uint32_t sel_start_col,
                   uint32_t sel_end_line,   uint32_t sel_end_col);
} PolyglotCodeAction;

typedef struct PolyglotFormatter {
    // Language this formatter applies to (e.g. "cpp")
    const char *language;

    // Format the entire file.  Returns a malloc'd replacement string.
    char *(*format)(const PolyglotHostContext *ctx,
                    const PolyglotHostServices *host,
                    const char *source, size_t len,
                    const char *filename);
} PolyglotFormatter;

typedef struct PolyglotLinter {
    const char *linter_id;
    // Languages this linter supports (NULL-terminated array)
    const char **languages;

    // Run linting and emit diagnostics through the host services.
    void (*lint)(const PolyglotHostContext *ctx,
                 const PolyglotHostServices *host,
                 const char *source, size_t len,
                 const char *filename);
} PolyglotLinter;

// ============================================================================
// Plugin instance — opaque handle returned by polyglot_plugin_create()
// ============================================================================

typedef struct PolyglotPlugin PolyglotPlugin;

// ============================================================================
// Mandatory exported functions
// ============================================================================

// Return static metadata.  The returned pointer must remain valid for the
// lifetime of the shared library.
typedef const PolyglotPluginInfo *(*PFN_polyglot_plugin_get_info)(void);

// Create a plugin instance, receiving the host context and services table.
typedef PolyglotPlugin *(*PFN_polyglot_plugin_create)(
    const PolyglotHostContext  *ctx,
    const PolyglotHostServices *host);

// Destroy a previously created instance.
typedef void (*PFN_polyglot_plugin_destroy)(PolyglotPlugin *plugin);

// ============================================================================
// Optional exported functions — called if the plugin declares the matching
// capability flag.
// ============================================================================

// CAP_LANGUAGE
typedef const PolyglotLanguageProvider *(*PFN_polyglot_plugin_get_language)(
    PolyglotPlugin *plugin);

// CAP_OPTIMIZER — return a NULL-terminated array of passes
typedef const PolyglotOptimizerPass **(*PFN_polyglot_plugin_get_passes)(
    PolyglotPlugin *plugin, uint32_t *out_count);

// CAP_CODE_ACTION — return a NULL-terminated array of actions
typedef const PolyglotCodeAction **(*PFN_polyglot_plugin_get_code_actions)(
    PolyglotPlugin *plugin, uint32_t *out_count);

// CAP_FORMATTER
typedef const PolyglotFormatter *(*PFN_polyglot_plugin_get_formatter)(
    PolyglotPlugin *plugin);

// CAP_LINTER
typedef const PolyglotLinter *(*PFN_polyglot_plugin_get_linter)(
    PolyglotPlugin *plugin);

// Lifecycle hooks (always called if exported)
typedef int  (*PFN_polyglot_plugin_activate)(PolyglotPlugin *plugin);
typedef void (*PFN_polyglot_plugin_deactivate)(PolyglotPlugin *plugin);

// ============================================================================
// Convenience macros for plugin authors
// ============================================================================

#ifdef _WIN32
#  define POLYGLOT_EXPORT __declspec(dllexport)
#else
#  define POLYGLOT_EXPORT __attribute__((visibility("default")))
#endif

// Declare the mandatory entry points with correct linkage and visibility.
#define POLYGLOT_PLUGIN_ENTRY_POINTS                                         \
    POLYGLOT_EXPORT const PolyglotPluginInfo *polyglot_plugin_get_info(void); \
    POLYGLOT_EXPORT PolyglotPlugin *polyglot_plugin_create(                   \
        const PolyglotHostContext  *ctx,                                      \
        const PolyglotHostServices *host);                                    \
    POLYGLOT_EXPORT void polyglot_plugin_destroy(PolyglotPlugin *plugin);

#ifdef __cplusplus
}  // extern "C"
#endif
