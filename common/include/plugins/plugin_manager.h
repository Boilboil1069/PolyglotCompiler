/**
 * @file     plugin_manager.h
 * @brief    Runtime plugin loader and lifecycle manager
 *
 * @ingroup  Common / Plugins
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/plugins/plugin_api.h"

namespace polyglot::plugins {

// ============================================================================
// PluginHandle — RAII wrapper around a single loaded plugin
// ============================================================================

/** @brief PluginHandle data structure. */
struct PluginHandle {
    std::string                     library_path;
    void                           *dl_handle{nullptr};    // dlopen / LoadLibrary handle
    const PolyglotPluginInfo       *info{nullptr};
    PolyglotPlugin                 *instance{nullptr};
    bool                            active{false};

    // Resolved function pointers
    PFN_polyglot_plugin_get_info    fn_get_info{nullptr};
    PFN_polyglot_plugin_create      fn_create{nullptr};
    PFN_polyglot_plugin_destroy     fn_destroy{nullptr};
    PFN_polyglot_plugin_activate    fn_activate{nullptr};
    PFN_polyglot_plugin_deactivate  fn_deactivate{nullptr};

    // Capability-specific providers
    PFN_polyglot_plugin_get_language     fn_get_language{nullptr};
    PFN_polyglot_plugin_get_passes       fn_get_passes{nullptr};
    PFN_polyglot_plugin_get_code_actions fn_get_code_actions{nullptr};
    PFN_polyglot_plugin_get_formatter    fn_get_formatter{nullptr};
    PFN_polyglot_plugin_get_linter       fn_get_linter{nullptr};
    PFN_polyglot_plugin_get_completion   fn_get_completion{nullptr};
    PFN_polyglot_plugin_get_diagnostic   fn_get_diagnostic{nullptr};
    PFN_polyglot_plugin_get_template     fn_get_template{nullptr};
    PFN_polyglot_plugin_get_topology_proc fn_get_topology_proc{nullptr};

    // Cached providers (populated after activation)
    const PolyglotLanguageProvider *language_provider{nullptr};
    const PolyglotFormatter        *formatter{nullptr};
    const PolyglotLinter           *linter{nullptr};
    const PolyglotCompletionProvider  *completion_provider{nullptr};
    const PolyglotDiagnosticProvider  *diagnostic_provider{nullptr};
    const PolyglotTemplateProvider    *template_provider{nullptr};
    const PolyglotTopologyProcessor   *topology_processor{nullptr};
    std::vector<const PolyglotOptimizerPass *>  optimizer_passes;
    std::vector<const PolyglotCodeAction *>     code_actions;

    // Event system — resolved handler and subscribed event types
    PFN_polyglot_plugin_on_event fn_on_event{nullptr};
    std::set<PolyglotEventType>  subscribed_events;

    // Menu contributions registered by this plugin
    std::vector<PolyglotMenuContribution> menu_contributions;

    // Sandbox state — failure tracking for automatic circuit-breaker
    std::atomic<uint32_t> consecutive_failures{0};
    std::atomic<bool>     circuit_open{false};      // Plugin disabled by breaker
    std::string           last_error;               // Last recorded crash/error
};

// ============================================================================
// PluginManager — singleton managing all loaded plugins
// ============================================================================

/** @brief PluginManager class. */
class PluginManager {
  public:
    // Access the global instance.
    static PluginManager &Instance();

    /** @name Discovery & Loading */
    /** @{ */

    // Add a directory to the plugin search path.
    void AddSearchPath(const std::string &dir);

    // Scan all search-path directories for plugin shared libraries and
    // load their metadata (but do not activate yet).
    void DiscoverPlugins();

    // Load a single plugin by file path.  Returns the plugin id on success
    // or an empty string on failure.
    std::string LoadPlugin(const std::string &path);

    // Unload a plugin by id.  Deactivates first if needed.
    bool UnloadPlugin(const std::string &id);

    /** @} */

    /** @name Activation */
    /** @{ */

    // Activate a previously loaded plugin.
    bool ActivatePlugin(const std::string &id);

    // Deactivate an active plugin without unloading it.
    bool DeactivatePlugin(const std::string &id);

    /** @} */

    /** @name Queries */
    /** @{ */

    // All currently loaded plugins (active or not).
    std::vector<const PolyglotPluginInfo *> ListPlugins() const;

    // Check whether a specific plugin is loaded / active.
    bool IsLoaded(const std::string &id) const;
    bool IsActive(const std::string &id) const;

    // Retrieve a plugin handle by id (nullptr if not found).
    const PluginHandle *GetPlugin(const std::string &id) const;

    /** @} */

    /** @name Capability aggregation */
    /** @{ */

    // Collect all active language providers.
    std::vector<const PolyglotLanguageProvider *> GetLanguageProviders() const;

    // Collect all active optimizer passes.
    std::vector<const PolyglotOptimizerPass *> GetOptimizerPasses() const;

    // Find the formatter for a given language (first match wins).
    const PolyglotFormatter *FindFormatter(const std::string &language) const;

    // Collect all active linters that support a given language.
    std::vector<const PolyglotLinter *> FindLinters(const std::string &language) const;

    // Collect all active code actions.
    std::vector<const PolyglotCodeAction *> GetCodeActions() const;

    // Collect all active completion providers for a given language.
    std::vector<const PolyglotCompletionProvider *>
    FindCompletionProviders(const std::string &language) const;

    // Collect all active diagnostic providers for a given language.
    std::vector<const PolyglotDiagnosticProvider *>
    FindDiagnosticProviders(const std::string &language) const;

    // Collect all active template providers.
    std::vector<const PolyglotTemplateProvider *> GetTemplateProviders() const;

    // Collect all active topology post-processors.
    std::vector<const PolyglotTopologyProcessor *> GetTopologyProcessors() const;

    /** @} */

    /** @name Conflict detection */
    /** @{ */

    // A conflict record describing a capability collision between plugins.
    struct PluginConflict {
        std::string plugin_a;
        std::string plugin_b;
        uint32_t    capability;     // The conflicting PolyglotPluginCap bit
        std::string description;
    };

    // Detect capability conflicts among all loaded plugins.  Returns a list
    // of conflicts (e.g. two formatters for the same language).
    std::vector<PluginConflict> DetectConflicts() const;

    /** @} */

    /** @name Sandbox policy */
    /** @{ */

    struct SandboxPolicy {
        uint32_t call_timeout_ms{5000};       // Max time per plugin callback
        uint64_t memory_limit_bytes{0};       // 0 = unlimited
        uint32_t max_consecutive_failures{3}; // Trips circuit breaker
    };

    // Set or query the global sandbox policy.
    void SetSandboxPolicy(const SandboxPolicy &policy);
    SandboxPolicy GetSandboxPolicy() const;

    // Record a plugin callback failure.  Increments the consecutive failure
    // counter and opens the circuit breaker when the threshold is reached.
    void RecordPluginFailure(const std::string &plugin_id,
                             const std::string &error_msg);

    // Record a successful callback — resets the failure counter.
    void RecordPluginSuccess(const std::string &plugin_id);

    // Query whether a plugin's circuit breaker is open (auto-disabled).
    bool IsCircuitOpen(const std::string &plugin_id) const;

    // Reset a tripped circuit breaker, allowing the plugin to run again.
    void ResetCircuitBreaker(const std::string &plugin_id);

    // Return crash/error log entries for a specific plugin.
    std::string GetPluginLastError(const std::string &plugin_id) const;

    /** @} */

    /** @name Event system */
    /** @{ */

    // Subscribe a plugin to a specific event type.
    void SubscribeEvent(const std::string &plugin_id, PolyglotEventType type);

    // Unsubscribe a plugin from a specific event type.
    void UnsubscribeEvent(const std::string &plugin_id, PolyglotEventType type);

    // Fire an event to all subscribed plugins.  Thread-safe; dispatches to
    // each subscriber's on_event handler.
    void FireEvent(const PolyglotEvent &event);

    // Convenience helpers for common event patterns.
    void FireFileOpened(const std::string &path);
    void FireFileSaved(const std::string &path);
    void FireFileClosed(const std::string &path);
    void FireBuildStarted();
    void FireBuildFinished(int result_code);
    void FireWorkspaceChanged(const std::string &root);
    void FireThemeChanged(const std::string &theme_name);

    /** @} */

    /** @name File type registry */
    /** @{ */

    // Register a file extension with a language name.  Plugins call this to
    // associate new file types.
    void RegisterFileType(const std::string &plugin_id,
                          const std::string &extension,
                          const std::string &language);

    // Query the language for a file extension (returns empty string if unknown).
    std::string GetLanguageForExtension(const std::string &extension) const;

    // Return all registered file type mappings.
    std::vector<std::pair<std::string, std::string>> GetRegisteredFileTypes() const;

    /** @} */

    /** @name Menu contributions */
    /** @{ */

    // Register a menu item contributed by a plugin.
    void RegisterMenuItem(const std::string &plugin_id,
                          const PolyglotMenuContribution &item);

    // Unregister a menu item by action_id.
    void UnregisterMenuItem(const std::string &plugin_id,
                            const std::string &action_id);

    // Retrieve all menu contributions from all active plugins.
    std::vector<std::pair<std::string, PolyglotMenuContribution>>
    GetMenuContributions() const;

    // Execute a menu action callback by action_id.
    bool ExecuteMenuAction(const std::string &action_id);

    /** @} */

    /** @name Settings */
    /** @{ */

    // Plugin-scoped settings storage.
    std::string GetPluginSetting(const std::string &plugin_id,
                                 const std::string &key) const;
    void SetPluginSetting(const std::string &plugin_id,
                          const std::string &key,
                          const std::string &value);

    /** @} */

    /** @name Workspace context */
    /** @{ */

    void SetWorkspaceRoot(const std::string &root);
    const std::string &GetWorkspaceRoot() const;

    /** @} */

    /** @name Logging callback */
    /** @{ */

    using LogCallback = std::function<void(const std::string &plugin_id,
                                           PolyglotLogLevel level,
                                           const std::string &message)>;
    void SetLogCallback(LogCallback cb);

    /** @} */

    /** @name Diagnostics callback */
    /** @{ */

    using DiagCallback = std::function<void(const std::string &plugin_id,
                                            const PolyglotDiagnostic &diag)>;
    void SetDiagnosticCallback(DiagCallback cb);

    /** @} */

    /** @name File-open callback (UI integration) */
    /** @{ */

    using OpenFileCallback = std::function<void(const std::string &path,
                                                uint32_t line)>;
    void SetOpenFileCallback(OpenFileCallback cb);

    // Dispatch an open-file request (called from host trampolines).
    void DispatchOpenFile(const std::string &path, uint32_t line);

    /** @} */

    /** @name Menu item callback (UI integration) */
    /** @{ */

    using MenuItemCallback = std::function<void(const std::string &plugin_id,
                                                const PolyglotMenuContribution &item)>;
    void SetMenuItemRegisteredCallback(MenuItemCallback cb);

    /** @} */

    /** @name File type registered callback (UI integration) */
    /** @{ */

    using FileTypeCallback = std::function<void(const std::string &extension,
                                                const std::string &language)>;
    void SetFileTypeRegisteredCallback(FileTypeCallback cb);

    /** @} */

    /** @name Cleanup */
    /** @{ */

    // Deactivate and unload all plugins.  Called on application shutdown.
    void UnloadAll();

    ~PluginManager();

  private:
    PluginManager() = default;
    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;

    // Resolve capability-specific function pointers from the DL handle.
    void ResolveCapabilitySymbols(PluginHandle &handle);

    // Populate cached providers from the resolved function pointers.
    void CacheProviders(PluginHandle &handle);

    // Build host service table for a specific plugin.
    void BuildHostServices();

    // Parse a SemVer string "major.minor.patch" into three integers.
    // Returns false if the string is null or malformed.
    static bool ParseSemVer(const char *str, int &major, int &minor, int &patch);

    // Compare plugin's min_host_version against the running host version.
    // Returns true if the plugin is compatible.
    static bool CheckHostVersionConstraint(const PolyglotPluginInfo *info);

    mutable std::mutex mu_;
    std::vector<std::string> search_paths_;
    std::unordered_map<std::string, std::unique_ptr<PluginHandle>> plugins_;

    // Host context shared by all plugins
    PolyglotHostContext  *host_ctx_{nullptr};
    PolyglotHostServices  host_services_{};

    // Sandbox policy
    SandboxPolicy sandbox_policy_;

    // Application callbacks
    LogCallback       log_cb_;
    DiagCallback      diag_cb_;
    OpenFileCallback  open_file_cb_;
    MenuItemCallback  menu_item_cb_;
    FileTypeCallback  file_type_cb_;

    // Global state exposed to plugins
    std::string workspace_root_;

    // Plugin-scoped settings: plugin_id → { key → value }
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>> settings_;

    // File type registry: extension → language
    std::unordered_map<std::string, std::string> file_type_registry_;
};

}  // namespace polyglot::plugins

/** @} */