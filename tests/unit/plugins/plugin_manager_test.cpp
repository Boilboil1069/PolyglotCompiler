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
