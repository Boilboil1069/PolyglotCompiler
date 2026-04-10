/**
 * @file     plugin_manager.cpp
 * @brief    PluginManager implementation
 *
 * @ingroup  Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "common/include/plugins/plugin_manager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace polyglot::plugins {

namespace fs = std::filesystem;

// ============================================================================
// Platform abstraction for shared-library loading
// ============================================================================

namespace {

void *DlOpen(const char *path) {
#ifdef _WIN32
    return reinterpret_cast<void *>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *DlSym(void *handle, const char *symbol) {
#ifdef _WIN32
    return reinterpret_cast<void *>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol));
#else
    return dlsym(handle, symbol);
#endif
}

void DlClose(void *handle) {
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

std::string DlError() {
#ifdef _WIN32
    DWORD err = GetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0,
                   buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    const char *e = dlerror();
    return e ? std::string(e) : "unknown error";
#endif
}

// Shared-library file extension for the current platform
const char *SharedLibExtension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

}  // anonymous namespace

}  // namespace polyglot::plugins

// ============================================================================
// Host context — opaque struct passed to every plugin callback.
// Defined at global scope so it matches the forward declaration in plugin_api.h.
// ============================================================================

struct PolyglotHostContext {
    polyglot::plugins::PluginManager *manager{nullptr};
    std::string                       current_plugin_id;
};

// ============================================================================
// Host service trampolines — route C callbacks to the PluginManager.
// Declared at global scope to match the C ABI function-pointer types.
// ============================================================================

namespace {

void HostLog(const PolyglotHostContext *ctx,
             PolyglotLogLevel level,
             const char *message) {
    if (!ctx || !ctx->manager) return;
    std::string id = ctx->current_plugin_id;
    std::string msg = message ? message : "";

    const char *level_str = "INFO";
    switch (level) {
        case POLYGLOT_LOG_DEBUG:   level_str = "DEBUG"; break;
        case POLYGLOT_LOG_INFO:    level_str = "INFO";  break;
        case POLYGLOT_LOG_WARNING: level_str = "WARN";  break;
        case POLYGLOT_LOG_ERROR:   level_str = "ERROR"; break;
    }
    std::cerr << "[plugin:" << id << "] " << level_str << ": " << msg << "\n";
}

void HostEmitDiag(const PolyglotHostContext *ctx,
                  const PolyglotDiagnostic *diag) {
    if (!ctx || !ctx->manager || !diag) return;

    // Forward to the diagnostic callback so the UI / compiler can display it
    PolyglotDiagnostic d = *diag;
    std::string plugin_id = ctx->current_plugin_id;

    // Log the diagnostic to stderr for CLI visibility
    const char *sev_str = "note";
    switch (d.severity) {
        case POLYGLOT_DIAG_WARNING: sev_str = "warning"; break;
        case POLYGLOT_DIAG_ERROR:   sev_str = "error";   break;
        default: break;
    }
    std::cerr << "[plugin:" << plugin_id << "] "
              << (d.file ? d.file : "<unknown>") << ":"
              << d.line << ":" << d.column << ": "
              << sev_str << ": " << (d.message ? d.message : "") << "\n";

    // Fire event to other subscribed plugins
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_DIAGNOSTIC;
    event.file = d.file;
    event.line = d.line;
    event.data = d.message;
    event.result_code = static_cast<int>(d.severity);
    ctx->manager->FireEvent(event);
}

const char *HostGetSetting(const PolyglotHostContext *ctx,
                           const char *key) {
    if (!ctx || !ctx->manager || !key) return nullptr;
    static thread_local std::string result_buf;
    result_buf = ctx->manager->GetPluginSetting(ctx->current_plugin_id, key);
    return result_buf.empty() ? nullptr : result_buf.c_str();
}

void HostSetSetting(const PolyglotHostContext *ctx,
                    const char *key,
                    const char *value) {
    if (!ctx || !ctx->manager || !key) return;
    ctx->manager->SetPluginSetting(ctx->current_plugin_id,
                                   key,
                                   value ? value : "");
}

void HostOpenFile(const PolyglotHostContext *ctx,
                  const char *path,
                  uint32_t line) {
    if (!ctx || !ctx->manager || !path) return;
    // Delegate to the PluginManager which dispatches to the UI layer
    // via SetOpenFileCallback().
    ctx->manager->DispatchOpenFile(std::string(path), line);
}

void HostRegisterFileType(const PolyglotHostContext *ctx,
                          const char *extension,
                          const char *language) {
    if (!ctx || !ctx->manager || !extension || !language) return;
    // Register the file type in the PluginManager's central registry
    ctx->manager->RegisterFileType(ctx->current_plugin_id,
                                   std::string(extension),
                                   std::string(language));
}

const char *HostGetWorkspaceRoot(const PolyglotHostContext *ctx) {
    if (!ctx || !ctx->manager) return nullptr;
    const auto &root = ctx->manager->GetWorkspaceRoot();
    return root.empty() ? nullptr : root.c_str();
}

void HostSubscribeEvent(const PolyglotHostContext *ctx,
                        PolyglotEventType event_type) {
    if (!ctx || !ctx->manager) return;
    ctx->manager->SubscribeEvent(ctx->current_plugin_id, event_type);
}

void HostUnsubscribeEvent(const PolyglotHostContext *ctx,
                          PolyglotEventType event_type) {
    if (!ctx || !ctx->manager) return;
    ctx->manager->UnsubscribeEvent(ctx->current_plugin_id, event_type);
}

void HostRegisterMenuItem(const PolyglotHostContext *ctx,
                          const PolyglotMenuContribution *item) {
    if (!ctx || !ctx->manager || !item) return;
    ctx->manager->RegisterMenuItem(ctx->current_plugin_id, *item);
}

void HostUnregisterMenuItem(const PolyglotHostContext *ctx,
                            const char *action_id) {
    if (!ctx || !ctx->manager || !action_id) return;
    ctx->manager->UnregisterMenuItem(ctx->current_plugin_id,
                                     std::string(action_id));
}

}  // anonymous namespace

namespace polyglot::plugins {

// ============================================================================
// PluginManager — singleton
// ============================================================================

PluginManager &PluginManager::Instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::~PluginManager() {
    UnloadAll();
    delete host_ctx_;
}

// ============================================================================
// Search path management
// ============================================================================

void PluginManager::AddSearchPath(const std::string &dir) {
    std::lock_guard lock(mu_);
    // Avoid duplicate paths
    if (std::find(search_paths_.begin(), search_paths_.end(), dir) ==
        search_paths_.end()) {
        search_paths_.push_back(dir);
    }
}

// ============================================================================
// Discovery
// ============================================================================

void PluginManager::DiscoverPlugins() {
    std::vector<std::string> paths_snapshot;
    {
        std::lock_guard lock(mu_);
        paths_snapshot = search_paths_;
    }

    const std::string ext = SharedLibExtension();

    for (const auto &dir : paths_snapshot) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;

        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            const auto &p = entry.path();
            if (p.extension().string() != ext) continue;

            // Skip libraries that don't start with "polyplug_" prefix
            // (optional convention to avoid loading unrelated .so/.dll files)
            const auto stem = p.stem().string();
            if (stem.rfind("polyplug_", 0) != 0 &&
                stem.rfind("libpolyplug_", 0) != 0) {
                continue;
            }

            LoadPlugin(p.string());
        }
    }
}

// ============================================================================
// Load / Unload
// ============================================================================

std::string PluginManager::LoadPlugin(const std::string &path) {
    void *handle = DlOpen(path.c_str());
    if (!handle) {
        std::cerr << "[plugin-manager] Failed to load " << path
                  << ": " << DlError() << "\n";
        return {};
    }

    // Resolve the mandatory get_info entry point
    auto fn_info = reinterpret_cast<PFN_polyglot_plugin_get_info>(
        DlSym(handle, "polyglot_plugin_get_info"));
    if (!fn_info) {
        std::cerr << "[plugin-manager] " << path
                  << " does not export polyglot_plugin_get_info\n";
        DlClose(handle);
        return {};
    }

    const PolyglotPluginInfo *info = fn_info();
    if (!info || !info->id || !info->name) {
        std::cerr << "[plugin-manager] " << path
                  << " returned invalid plugin info\n";
        DlClose(handle);
        return {};
    }

    // API version check
    if (info->api_version != POLYGLOT_PLUGIN_API_VERSION) {
        std::cerr << "[plugin-manager] " << info->id
                  << " requires API version " << info->api_version
                  << ", host provides " << POLYGLOT_PLUGIN_API_VERSION << "\n";
        DlClose(handle);
        return {};
    }

    // Resolve mandatory create / destroy
    auto fn_create = reinterpret_cast<PFN_polyglot_plugin_create>(
        DlSym(handle, "polyglot_plugin_create"));
    auto fn_destroy = reinterpret_cast<PFN_polyglot_plugin_destroy>(
        DlSym(handle, "polyglot_plugin_destroy"));
    if (!fn_create || !fn_destroy) {
        std::cerr << "[plugin-manager] " << info->id
                  << " missing polyglot_plugin_create/destroy\n";
        DlClose(handle);
        return {};
    }

    std::string id = info->id;

    std::lock_guard lock(mu_);

    // Reject if already loaded
    if (plugins_.count(id)) {
        std::cerr << "[plugin-manager] " << id << " is already loaded\n";
        DlClose(handle);
        return {};
    }

    auto ph = std::make_unique<PluginHandle>();
    ph->library_path = path;
    ph->dl_handle    = handle;
    ph->info         = info;
    ph->fn_get_info  = fn_info;
    ph->fn_create    = fn_create;
    ph->fn_destroy   = fn_destroy;

    // Resolve optional lifecycle hooks
    ph->fn_activate   = reinterpret_cast<PFN_polyglot_plugin_activate>(
        DlSym(handle, "polyglot_plugin_activate"));
    ph->fn_deactivate = reinterpret_cast<PFN_polyglot_plugin_deactivate>(
        DlSym(handle, "polyglot_plugin_deactivate"));

    // Resolve capability-specific symbols
    ResolveCapabilitySymbols(*ph);

    plugins_[id] = std::move(ph);
    std::cerr << "[plugin-manager] Loaded plugin: " << id
              << " (" << info->name << " v" << (info->version ? info->version : "?")
              << ")\n";

    return id;
}

bool PluginManager::UnloadPlugin(const std::string &id) {
    std::lock_guard lock(mu_);

    auto it = plugins_.find(id);
    if (it == plugins_.end()) return false;

    auto &ph = *it->second;

    // Deactivate if active
    if (ph.active) {
        if (ph.fn_deactivate && ph.instance)
            ph.fn_deactivate(ph.instance);
        ph.active = false;
    }

    // Destroy instance
    if (ph.instance && ph.fn_destroy) {
        ph.fn_destroy(ph.instance);
        ph.instance = nullptr;
    }

    // Close shared library
    if (ph.dl_handle) {
        DlClose(ph.dl_handle);
        ph.dl_handle = nullptr;
    }

    plugins_.erase(it);
    std::cerr << "[plugin-manager] Unloaded plugin: " << id << "\n";
    return true;
}

// ============================================================================
// Activation / Deactivation
// ============================================================================

bool PluginManager::ActivatePlugin(const std::string &id) {
    std::lock_guard lock(mu_);

    auto it = plugins_.find(id);
    if (it == plugins_.end()) return false;

    auto &ph = *it->second;
    if (ph.active) return true;  // Already active

    // Ensure host services are built
    BuildHostServices();

    // Set the current plugin id in the host context
    host_ctx_->current_plugin_id = id;

    // Create instance
    if (!ph.instance) {
        ph.instance = ph.fn_create(host_ctx_, &host_services_);
        if (!ph.instance) {
            std::cerr << "[plugin-manager] " << id
                      << " create() returned null\n";
            return false;
        }
    }

    // Call activate hook
    if (ph.fn_activate) {
        int rc = ph.fn_activate(ph.instance);
        if (rc != 0) {
            std::cerr << "[plugin-manager] " << id
                      << " activate() returned " << rc << "\n";
            ph.fn_destroy(ph.instance);
            ph.instance = nullptr;
            return false;
        }
    }

    ph.active = true;

    // Cache capability providers
    CacheProviders(ph);

    std::cerr << "[plugin-manager] Activated plugin: " << id << "\n";
    return true;
}

bool PluginManager::DeactivatePlugin(const std::string &id) {
    std::lock_guard lock(mu_);

    auto it = plugins_.find(id);
    if (it == plugins_.end()) return false;

    auto &ph = *it->second;
    if (!ph.active) return true;

    if (ph.fn_deactivate && ph.instance)
        ph.fn_deactivate(ph.instance);

    ph.active = false;

    // Clear cached providers
    ph.language_provider = nullptr;
    ph.formatter = nullptr;
    ph.linter = nullptr;
    ph.optimizer_passes.clear();
    ph.code_actions.clear();

    std::cerr << "[plugin-manager] Deactivated plugin: " << id << "\n";
    return true;
}

// ============================================================================
// Queries
// ============================================================================

std::vector<const PolyglotPluginInfo *> PluginManager::ListPlugins() const {
    std::lock_guard lock(mu_);
    std::vector<const PolyglotPluginInfo *> result;
    result.reserve(plugins_.size());
    for (const auto &[id, ph] : plugins_) {
        result.push_back(ph->info);
    }
    return result;
}

bool PluginManager::IsLoaded(const std::string &id) const {
    std::lock_guard lock(mu_);
    return plugins_.count(id) > 0;
}

bool PluginManager::IsActive(const std::string &id) const {
    std::lock_guard lock(mu_);
    auto it = plugins_.find(id);
    return it != plugins_.end() && it->second->active;
}

const PluginHandle *PluginManager::GetPlugin(const std::string &id) const {
    std::lock_guard lock(mu_);
    auto it = plugins_.find(id);
    return it != plugins_.end() ? it->second.get() : nullptr;
}

// ============================================================================
// Capability aggregation
// ============================================================================

std::vector<const PolyglotLanguageProvider *>
PluginManager::GetLanguageProviders() const {
    std::lock_guard lock(mu_);
    std::vector<const PolyglotLanguageProvider *> result;
    for (const auto &[id, ph] : plugins_) {
        if (ph->active && ph->language_provider)
            result.push_back(ph->language_provider);
    }
    return result;
}

std::vector<const PolyglotOptimizerPass *>
PluginManager::GetOptimizerPasses() const {
    std::lock_guard lock(mu_);
    std::vector<const PolyglotOptimizerPass *> result;
    for (const auto &[id, ph] : plugins_) {
        if (ph->active) {
            result.insert(result.end(),
                          ph->optimizer_passes.begin(),
                          ph->optimizer_passes.end());
        }
    }
    return result;
}

const PolyglotFormatter *PluginManager::FindFormatter(
    const std::string &language) const {
    std::lock_guard lock(mu_);
    for (const auto &[id, ph] : plugins_) {
        if (ph->active && ph->formatter &&
            ph->formatter->language &&
            language == ph->formatter->language) {
            return ph->formatter;
        }
    }
    return nullptr;
}

std::vector<const PolyglotLinter *>
PluginManager::FindLinters(const std::string &language) const {
    std::lock_guard lock(mu_);
    std::vector<const PolyglotLinter *> result;
    for (const auto &[id, ph] : plugins_) {
        if (!ph->active || !ph->linter || !ph->linter->languages) continue;
        for (const char **lang = ph->linter->languages; *lang; ++lang) {
            if (language == *lang) {
                result.push_back(ph->linter);
                break;
            }
        }
    }
    return result;
}

std::vector<const PolyglotCodeAction *>
PluginManager::GetCodeActions() const {
    std::lock_guard lock(mu_);
    std::vector<const PolyglotCodeAction *> result;
    for (const auto &[id, ph] : plugins_) {
        if (ph->active) {
            result.insert(result.end(),
                          ph->code_actions.begin(),
                          ph->code_actions.end());
        }
    }
    return result;
}

// ============================================================================
// Settings
// ============================================================================

std::string PluginManager::GetPluginSetting(const std::string &plugin_id,
                                            const std::string &key) const {
    std::lock_guard lock(mu_);
    auto pit = settings_.find(plugin_id);
    if (pit == settings_.end()) return {};
    auto kit = pit->second.find(key);
    return kit != pit->second.end() ? kit->second : std::string{};
}

void PluginManager::SetPluginSetting(const std::string &plugin_id,
                                     const std::string &key,
                                     const std::string &value) {
    std::lock_guard lock(mu_);
    settings_[plugin_id][key] = value;
}

// ============================================================================
// Workspace context
// ============================================================================

void PluginManager::SetWorkspaceRoot(const std::string &root) {
    std::lock_guard lock(mu_);
    workspace_root_ = root;
}

const std::string &PluginManager::GetWorkspaceRoot() const {
    // No lock needed — workspace root changes infrequently and is safe to
    // read in a slightly stale state.
    return workspace_root_;
}

// ============================================================================
// Callbacks
// ============================================================================

void PluginManager::SetLogCallback(LogCallback cb) {
    std::lock_guard lock(mu_);
    log_cb_ = std::move(cb);
}

void PluginManager::SetDiagnosticCallback(DiagCallback cb) {
    std::lock_guard lock(mu_);
    diag_cb_ = std::move(cb);
}

void PluginManager::SetOpenFileCallback(OpenFileCallback cb) {
    std::lock_guard lock(mu_);
    open_file_cb_ = std::move(cb);
}

void PluginManager::DispatchOpenFile(const std::string &path, uint32_t line) {
    OpenFileCallback cb;
    {
        std::lock_guard lock(mu_);
        cb = open_file_cb_;
    }
    // Call outside lock to avoid deadlock if callback re-enters plugin system
    if (cb) {
        cb(path, line);
    }
}

void PluginManager::SetMenuItemRegisteredCallback(MenuItemCallback cb) {
    std::lock_guard lock(mu_);
    menu_item_cb_ = std::move(cb);
}

void PluginManager::SetFileTypeRegisteredCallback(FileTypeCallback cb) {
    std::lock_guard lock(mu_);
    file_type_cb_ = std::move(cb);
}

// ============================================================================
// Event System
// ============================================================================

void PluginManager::SubscribeEvent(const std::string &plugin_id,
                                   PolyglotEventType type) {
    std::lock_guard lock(mu_);
    auto it = plugins_.find(plugin_id);
    if (it != plugins_.end()) {
        it->second->subscribed_events.insert(type);
    }
}

void PluginManager::UnsubscribeEvent(const std::string &plugin_id,
                                     PolyglotEventType type) {
    std::lock_guard lock(mu_);
    auto it = plugins_.find(plugin_id);
    if (it != plugins_.end()) {
        it->second->subscribed_events.erase(type);
    }
}

void PluginManager::FireEvent(const PolyglotEvent &event) {
    // Snapshot the list of subscribers under lock, then dispatch outside
    // the lock to avoid deadlocks when handlers re-enter the manager.
    struct Subscriber {
        PolyglotPlugin               *instance;
        PFN_polyglot_plugin_on_event  handler;
    };
    std::vector<Subscriber> subscribers;

    {
        std::lock_guard lock(mu_);
        for (auto &[id, ph] : plugins_) {
            if (!ph->active || !ph->fn_on_event) continue;
            if (ph->subscribed_events.count(event.type) > 0) {
                subscribers.push_back({ph->instance, ph->fn_on_event});
            }
        }
    }

    for (auto &sub : subscribers) {
        sub.handler(sub.instance, &event);
    }
}

void PluginManager::FireFileOpened(const std::string &path) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_FILE_OPENED;
    event.file = path.c_str();
    FireEvent(event);
}

void PluginManager::FireFileSaved(const std::string &path) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_FILE_SAVED;
    event.file = path.c_str();
    FireEvent(event);
}

void PluginManager::FireFileClosed(const std::string &path) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_FILE_CLOSED;
    event.file = path.c_str();
    FireEvent(event);
}

void PluginManager::FireBuildStarted() {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_BUILD_STARTED;
    FireEvent(event);
}

void PluginManager::FireBuildFinished(int result_code) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_BUILD_FINISHED;
    event.result_code = result_code;
    FireEvent(event);
}

void PluginManager::FireWorkspaceChanged(const std::string &root) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_WORKSPACE_CHANGED;
    event.data = root.c_str();
    FireEvent(event);
}

void PluginManager::FireThemeChanged(const std::string &theme_name) {
    PolyglotEvent event{};
    event.type = POLYGLOT_EVENT_THEME_CHANGED;
    event.data = theme_name.c_str();
    FireEvent(event);
}

// ============================================================================
// File Type Registry
// ============================================================================

void PluginManager::RegisterFileType(const std::string &plugin_id,
                                     const std::string &extension,
                                     const std::string &language) {
    FileTypeCallback cb;
    {
        std::lock_guard lock(mu_);
        file_type_registry_[extension] = language;
        cb = file_type_cb_;
    }
    std::cerr << "[plugin-manager] Registered file type: ." << extension
              << " -> " << language << " (from " << plugin_id << ")\n";
    if (cb) {
        cb(extension, language);
    }
}

std::string PluginManager::GetLanguageForExtension(
    const std::string &extension) const {
    std::lock_guard lock(mu_);
    auto it = file_type_registry_.find(extension);
    return it != file_type_registry_.end() ? it->second : std::string{};
}

std::vector<std::pair<std::string, std::string>>
PluginManager::GetRegisteredFileTypes() const {
    std::lock_guard lock(mu_);
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(file_type_registry_.size());
    for (const auto &[ext, lang] : file_type_registry_) {
        result.emplace_back(ext, lang);
    }
    return result;
}

// ============================================================================
// Menu Contributions
// ============================================================================

void PluginManager::RegisterMenuItem(const std::string &plugin_id,
                                     const PolyglotMenuContribution &item) {
    MenuItemCallback cb;
    {
        std::lock_guard lock(mu_);
        auto it = plugins_.find(plugin_id);
        if (it != plugins_.end()) {
            it->second->menu_contributions.push_back(item);
        }
        cb = menu_item_cb_;
    }
    std::cerr << "[plugin-manager] Registered menu item: "
              << (item.menu_path ? item.menu_path : "") << " / "
              << (item.action_id ? item.action_id : "") << "\n";
    if (cb) {
        cb(plugin_id, item);
    }
}

void PluginManager::UnregisterMenuItem(const std::string &plugin_id,
                                       const std::string &action_id) {
    std::lock_guard lock(mu_);
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end()) return;

    auto &contribs = it->second->menu_contributions;
    contribs.erase(
        std::remove_if(contribs.begin(), contribs.end(),
                       [&action_id](const PolyglotMenuContribution &c) {
                           return c.action_id && action_id == c.action_id;
                       }),
        contribs.end());
}

std::vector<std::pair<std::string, PolyglotMenuContribution>>
PluginManager::GetMenuContributions() const {
    std::lock_guard lock(mu_);
    std::vector<std::pair<std::string, PolyglotMenuContribution>> result;
    for (const auto &[id, ph] : plugins_) {
        if (!ph->active) continue;
        for (const auto &item : ph->menu_contributions) {
            result.emplace_back(id, item);
        }
    }
    return result;
}

bool PluginManager::ExecuteMenuAction(const std::string &action_id) {
    PolyglotMenuContribution found{};
    bool matched = false;

    {
        std::lock_guard lock(mu_);
        for (const auto &[id, ph] : plugins_) {
            if (!ph->active) continue;
            for (const auto &item : ph->menu_contributions) {
                if (item.action_id && action_id == item.action_id) {
                    found = item;
                    matched = true;
                    break;
                }
            }
            if (matched) break;
        }
    }

    if (matched && found.callback) {
        found.callback(host_ctx_);
        return true;
    }
    return false;
}

// ============================================================================
// Cleanup
// ============================================================================

void PluginManager::UnloadAll() {
    std::lock_guard lock(mu_);

    for (auto &[id, ph] : plugins_) {
        if (ph->active) {
            if (ph->fn_deactivate && ph->instance)
                ph->fn_deactivate(ph->instance);
            ph->active = false;
        }
        if (ph->instance && ph->fn_destroy) {
            ph->fn_destroy(ph->instance);
            ph->instance = nullptr;
        }
        if (ph->dl_handle) {
            DlClose(ph->dl_handle);
            ph->dl_handle = nullptr;
        }
    }
    plugins_.clear();
}

// ============================================================================
// Internal helpers
// ============================================================================

void PluginManager::ResolveCapabilitySymbols(PluginHandle &handle) {
    uint32_t caps = handle.info->capabilities;

    if (caps & POLYGLOT_CAP_LANGUAGE) {
        handle.fn_get_language =
            reinterpret_cast<PFN_polyglot_plugin_get_language>(
                DlSym(handle.dl_handle, "polyglot_plugin_get_language"));
    }
    if (caps & POLYGLOT_CAP_OPTIMIZER) {
        handle.fn_get_passes =
            reinterpret_cast<PFN_polyglot_plugin_get_passes>(
                DlSym(handle.dl_handle, "polyglot_plugin_get_passes"));
    }
    if (caps & POLYGLOT_CAP_CODE_ACTION) {
        handle.fn_get_code_actions =
            reinterpret_cast<PFN_polyglot_plugin_get_code_actions>(
                DlSym(handle.dl_handle, "polyglot_plugin_get_code_actions"));
    }
    if (caps & POLYGLOT_CAP_FORMATTER) {
        handle.fn_get_formatter =
            reinterpret_cast<PFN_polyglot_plugin_get_formatter>(
                DlSym(handle.dl_handle, "polyglot_plugin_get_formatter"));
    }
    if (caps & POLYGLOT_CAP_LINTER) {
        handle.fn_get_linter =
            reinterpret_cast<PFN_polyglot_plugin_get_linter>(
                DlSym(handle.dl_handle, "polyglot_plugin_get_linter"));
    }

    // Always try to resolve the event handler — any plugin can subscribe
    handle.fn_on_event =
        reinterpret_cast<PFN_polyglot_plugin_on_event>(
            DlSym(handle.dl_handle, "polyglot_plugin_on_event"));
}

void PluginManager::CacheProviders(PluginHandle &handle) {
    if (handle.fn_get_language) {
        handle.language_provider = handle.fn_get_language(handle.instance);
    }
    if (handle.fn_get_formatter) {
        handle.formatter = handle.fn_get_formatter(handle.instance);
    }
    if (handle.fn_get_linter) {
        handle.linter = handle.fn_get_linter(handle.instance);
    }
    if (handle.fn_get_passes) {
        uint32_t count = 0;
        auto passes = handle.fn_get_passes(handle.instance, &count);
        handle.optimizer_passes.clear();
        if (passes) {
            for (uint32_t i = 0; i < count; ++i) {
                if (passes[i]) handle.optimizer_passes.push_back(passes[i]);
            }
        }
    }
    if (handle.fn_get_code_actions) {
        uint32_t count = 0;
        auto actions = handle.fn_get_code_actions(handle.instance, &count);
        handle.code_actions.clear();
        if (actions) {
            for (uint32_t i = 0; i < count; ++i) {
                if (actions[i]) handle.code_actions.push_back(actions[i]);
            }
        }
    }
}

void PluginManager::BuildHostServices() {
    if (host_ctx_) return;  // Already built

    host_ctx_ = new PolyglotHostContext{};
    host_ctx_->manager = this;

    host_services_.log                = HostLog;
    host_services_.emit_diagnostic    = HostEmitDiag;
    host_services_.get_setting        = HostGetSetting;
    host_services_.set_setting        = HostSetSetting;
    host_services_.open_file          = HostOpenFile;
    host_services_.register_file_type = HostRegisterFileType;
    host_services_.get_workspace_root = HostGetWorkspaceRoot;
    host_services_.subscribe_event    = HostSubscribeEvent;
    host_services_.unsubscribe_event  = HostUnsubscribeEvent;
    host_services_.register_menu_item = HostRegisterMenuItem;
    host_services_.unregister_menu_item = HostUnregisterMenuItem;
}

}  // namespace polyglot::plugins
