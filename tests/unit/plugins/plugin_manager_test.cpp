// plugin_manager_test.cpp — Unit tests for the plugin manager.

#include <catch2/catch_test_macros.hpp>

#include "common/include/plugins/plugin_api.h"
#include "common/include/plugins/plugin_manager.h"

using namespace polyglot::plugins;

// ============================================================================
// Plugin API struct validation
// ============================================================================

TEST_CASE("Plugin API - Info struct defaults", "[plugins][api]") {
    PolyglotPluginInfo info{};
    REQUIRE(info.api_version == 0);
    REQUIRE(info.id == nullptr);
    REQUIRE(info.name == nullptr);
    REQUIRE(info.capabilities == 0);
}

TEST_CASE("Plugin API - Capability flags are distinct", "[plugins][api]") {
    REQUIRE(POLYGLOT_CAP_LANGUAGE   != POLYGLOT_CAP_OPTIMIZER);
    REQUIRE(POLYGLOT_CAP_BACKEND    != POLYGLOT_CAP_TOOL);
    REQUIRE(POLYGLOT_CAP_UI_PANEL   != POLYGLOT_CAP_SYNTAX_THEME);
    REQUIRE(POLYGLOT_CAP_FORMATTER  != POLYGLOT_CAP_LINTER);
    REQUIRE(POLYGLOT_CAP_DEBUGGER   != POLYGLOT_CAP_CODE_ACTION);

    // Verify bitwise composition
    uint32_t combined = POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_LINTER;
    REQUIRE((combined & POLYGLOT_CAP_LANGUAGE) != 0);
    REQUIRE((combined & POLYGLOT_CAP_LINTER) != 0);
    REQUIRE((combined & POLYGLOT_CAP_FORMATTER) == 0);
}

// ============================================================================
// PluginManager — basic operations (no actual shared libraries)
// ============================================================================

TEST_CASE("PluginManager - Singleton access", "[plugins][manager]") {
    auto &a = PluginManager::Instance();
    auto &b = PluginManager::Instance();
    REQUIRE(&a == &b);
}

TEST_CASE("PluginManager - Search path management", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    pm.AddSearchPath("/tmp/polyglot_test_plugins");
    pm.AddSearchPath("/tmp/polyglot_test_plugins");  // duplicate — should be ignored
    // No crash; no public API to query paths, but verify no exception
}

TEST_CASE("PluginManager - Load nonexistent plugin", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    std::string id = pm.LoadPlugin("/nonexistent/path/libpolyplug_fake.so");
    REQUIRE(id.empty());
}

TEST_CASE("PluginManager - Unload unknown plugin", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    REQUIRE_FALSE(pm.UnloadPlugin("com.nonexistent.plugin"));
}

TEST_CASE("PluginManager - Activate unknown plugin", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    REQUIRE_FALSE(pm.ActivatePlugin("com.nonexistent.plugin"));
}

TEST_CASE("PluginManager - IsLoaded for unknown id", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    REQUIRE_FALSE(pm.IsLoaded("com.unknown.plugin"));
    REQUIRE_FALSE(pm.IsActive("com.unknown.plugin"));
}

TEST_CASE("PluginManager - Workspace root", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    pm.SetWorkspaceRoot("/test/workspace");
    REQUIRE(pm.GetWorkspaceRoot() == "/test/workspace");
}

TEST_CASE("PluginManager - Plugin settings storage", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    // Initially empty
    REQUIRE(pm.GetPluginSetting("test.plugin", "key1").empty());

    // Set and retrieve
    pm.SetPluginSetting("test.plugin", "key1", "value1");
    REQUIRE(pm.GetPluginSetting("test.plugin", "key1") == "value1");

    // Different plugin, same key
    REQUIRE(pm.GetPluginSetting("other.plugin", "key1").empty());

    // Overwrite
    pm.SetPluginSetting("test.plugin", "key1", "value2");
    REQUIRE(pm.GetPluginSetting("test.plugin", "key1") == "value2");
}

TEST_CASE("PluginManager - Empty capability queries", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    auto langs = pm.GetLanguageProviders();
    REQUIRE(langs.empty());

    auto passes = pm.GetOptimizerPasses();
    REQUIRE(passes.empty());

    auto fmt = pm.FindFormatter("cpp");
    REQUIRE(fmt == nullptr);

    auto linters = pm.FindLinters("python");
    REQUIRE(linters.empty());

    auto actions = pm.GetCodeActions();
    REQUIRE(actions.empty());
}

TEST_CASE("PluginManager - ListPlugins on empty manager", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    // After the previous tests there may be no plugins loaded
    auto list = pm.ListPlugins();
    // Just verify no crash and valid return
    (void)list;
}

TEST_CASE("PluginManager - Log callback", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    bool called = false;
    pm.SetLogCallback([&called](const std::string &, PolyglotLogLevel, const std::string &) {
        called = true;
    });

    // The callback is stored but we cannot trigger it without a real plugin.
    // Just verify no crash on setting it.
    pm.SetLogCallback(nullptr);  // Clear
}

TEST_CASE("PluginManager - Discover with empty search paths", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    // Discover should complete without error even with no valid directories
    pm.DiscoverPlugins();
}

// ============================================================================
// Host services struct validation
// ============================================================================

TEST_CASE("Plugin API - Host services struct is zero-initializable", "[plugins][api]") {
    PolyglotHostServices svc{};
    REQUIRE(svc.log == nullptr);
    REQUIRE(svc.emit_diagnostic == nullptr);
    REQUIRE(svc.get_setting == nullptr);
    REQUIRE(svc.open_file == nullptr);
    REQUIRE(svc.subscribe_event == nullptr);
    REQUIRE(svc.unsubscribe_event == nullptr);
    REQUIRE(svc.register_menu_item == nullptr);
    REQUIRE(svc.unregister_menu_item == nullptr);
}

TEST_CASE("Plugin API - Diagnostic struct", "[plugins][api]") {
    PolyglotDiagnostic diag{};
    diag.file = "test.ploy";
    diag.line = 10;
    diag.column = 5;
    diag.severity = POLYGLOT_DIAG_ERROR;
    diag.message = "test error";
    diag.code = "E001";

    REQUIRE(diag.line == 10);
    REQUIRE(diag.severity == POLYGLOT_DIAG_ERROR);
}

// ============================================================================
// Event system
// ============================================================================

TEST_CASE("Plugin API - Event types are distinct", "[plugins][api]") {
    REQUIRE(POLYGLOT_EVENT_FILE_OPENED != POLYGLOT_EVENT_FILE_SAVED);
    REQUIRE(POLYGLOT_EVENT_BUILD_STARTED != POLYGLOT_EVENT_BUILD_FINISHED);
    REQUIRE(POLYGLOT_EVENT_DIAGNOSTIC != POLYGLOT_EVENT_WORKSPACE_CHANGED);
    REQUIRE(POLYGLOT_EVENT_THEME_CHANGED != POLYGLOT_EVENT_FILE_CLOSED);
}

TEST_CASE("Plugin API - Event struct is zero-initializable", "[plugins][api]") {
    PolyglotEvent event{};
    REQUIRE(event.type == POLYGLOT_EVENT_FILE_OPENED);
    REQUIRE(event.file == nullptr);
    REQUIRE(event.line == 0);
    REQUIRE(event.data == nullptr);
    REQUIRE(event.result_code == 0);
}

TEST_CASE("Plugin API - Menu contribution struct", "[plugins][api]") {
    PolyglotMenuContribution menu{};
    REQUIRE(menu.menu_path == nullptr);
    REQUIRE(menu.action_id == nullptr);
    REQUIRE(menu.label == nullptr);
    REQUIRE(menu.shortcut == nullptr);
    REQUIRE(menu.callback == nullptr);
}

// ============================================================================
// PluginManager — event system
// ============================================================================

TEST_CASE("PluginManager - Subscribe/unsubscribe with no plugin", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    // Should not crash when subscribing for a nonexistent plugin
    pm.SubscribeEvent("nonexistent.plugin", POLYGLOT_EVENT_FILE_OPENED);
    pm.UnsubscribeEvent("nonexistent.plugin", POLYGLOT_EVENT_FILE_OPENED);
}

TEST_CASE("PluginManager - FireEvent with no subscribers", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    // Should not crash when firing events with no subscribers
    pm.FireFileOpened("/test/file.ploy");
    pm.FireFileSaved("/test/file.ploy");
    pm.FireFileClosed("/test/file.ploy");
    pm.FireBuildStarted();
    pm.FireBuildFinished(0);
    pm.FireWorkspaceChanged("/test/workspace");
    pm.FireThemeChanged("Dark");
}

// ============================================================================
// PluginManager — file type registry
// ============================================================================

TEST_CASE("PluginManager - File type registry", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    // Initially unknown extension
    REQUIRE(pm.GetLanguageForExtension("xyz").empty());

    // Register a file type
    pm.RegisterFileType("test.plugin", "xyz", "xyz-lang");
    REQUIRE(pm.GetLanguageForExtension("xyz") == "xyz-lang");

    // Overwrite with a different language
    pm.RegisterFileType("test.plugin2", "xyz", "xyz-lang-v2");
    REQUIRE(pm.GetLanguageForExtension("xyz") == "xyz-lang-v2");

    // List all registered types
    auto types = pm.GetRegisteredFileTypes();
    bool found = false;
    for (const auto &[ext, lang] : types) {
        if (ext == "xyz" && lang == "xyz-lang-v2") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ============================================================================
// PluginManager — menu contributions
// ============================================================================

TEST_CASE("PluginManager - Menu contributions empty", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    auto contribs = pm.GetMenuContributions();
    // No active plugins means no contributions — just verify no crash
    (void)contribs;
}

TEST_CASE("PluginManager - ExecuteMenuAction for unknown id", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    REQUIRE_FALSE(pm.ExecuteMenuAction("nonexistent.action"));
}

// ============================================================================
// PluginManager — dispatch callbacks
// ============================================================================

TEST_CASE("PluginManager - DispatchOpenFile with callback", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    bool called = false;
    std::string received_path;
    uint32_t received_line = 0;

    pm.SetOpenFileCallback([&](const std::string &path, uint32_t line) {
        called = true;
        received_path = path;
        received_line = line;
    });

    pm.DispatchOpenFile("/test/path.ploy", 42);

    REQUIRE(called);
    REQUIRE(received_path == "/test/path.ploy");
    REQUIRE(received_line == 42);

    // Clear callback
    pm.SetOpenFileCallback(nullptr);
}

TEST_CASE("PluginManager - DispatchOpenFile without callback", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();
    pm.SetOpenFileCallback(nullptr);
    // Should not crash
    pm.DispatchOpenFile("/test/path.ploy", 1);
}

TEST_CASE("PluginManager - FileType registered callback", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    bool called = false;
    std::string ext_received;
    std::string lang_received;

    pm.SetFileTypeRegisteredCallback([&](const std::string &ext,
                                         const std::string &lang) {
        called = true;
        ext_received = ext;
        lang_received = lang;
    });

    pm.RegisterFileType("test.plugin", "abc", "abc-lang");

    REQUIRE(called);
    REQUIRE(ext_received == "abc");
    REQUIRE(lang_received == "abc-lang");

    // Clear callback
    pm.SetFileTypeRegisteredCallback(nullptr);
}

TEST_CASE("PluginManager - Diagnostic callback", "[plugins][manager]") {
    auto &pm = PluginManager::Instance();

    bool called = false;
    pm.SetDiagnosticCallback([&](const std::string &,
                                 const PolyglotDiagnostic &) {
        called = true;
    });

    // The callback is stored; it would be triggered by a plugin calling
    // emit_diagnostic.  Just verify no crash on setting/clearing.
    pm.SetDiagnosticCallback(nullptr);
}
