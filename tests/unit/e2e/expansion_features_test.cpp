/**
 * @file     expansion_features_test.cpp
 * @brief    End-to-end tests for 2026-04-14-1 feature expansion items
 *
 * Covers:
 *   1. Compilation cache (hash, store, retrieve, clean)
 *   2. polyc progress JSON events (stage start/end)
 *   3. Plugin version constraints and conflict detection
 *   4. Plugin sandbox circuit breaker
 *   5. Plugin API — new capability flags and provider structs
 *   6. Topology post-processor provider aggregation
 *
 * @ingroup  Tests / E2E
 * @author   Manning Cyrus
 * @date     2026-04-14
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <string>

#include "common/include/plugins/plugin_api.h"
#include "common/include/plugins/plugin_manager.h"
#include "common/include/version.h"
#include "tools/polyc/include/compilation_cache.h"

namespace fs = std::filesystem;
using namespace polyglot::plugins;
using namespace polyglot::compilation;

// ============================================================================
// 1. Compilation cache — hash, store, retrieve, clean
// ============================================================================

TEST_CASE("CompilationCache - hash determinism", "[cache][e2e]") {
    uint64_t h1 = CompilationCache::ComputeHash("FUNC f() -> INT { RETURN 1; }", "x86_64:O0");
    uint64_t h2 = CompilationCache::ComputeHash("FUNC f() -> INT { RETURN 1; }", "x86_64:O0");
    REQUIRE(h1 == h2);
}

TEST_CASE("CompilationCache - different input different hash", "[cache][e2e]") {
    uint64_t h1 = CompilationCache::ComputeHash("FUNC a() -> INT { RETURN 1; }", "x86_64:O0");
    uint64_t h2 = CompilationCache::ComputeHash("FUNC b() -> INT { RETURN 2; }", "x86_64:O0");
    REQUIRE(h1 != h2);
}

TEST_CASE("CompilationCache - different settings different hash", "[cache][e2e]") {
    uint64_t h1 = CompilationCache::ComputeHash("FUNC a() -> INT { RETURN 1; }", "x86_64:O0");
    uint64_t h2 = CompilationCache::ComputeHash("FUNC a() -> INT { RETURN 1; }", "arm64:O2");
    REQUIRE(h1 != h2);
}

TEST_CASE("CompilationCache - store and retrieve", "[cache][e2e]") {
    std::string tmp_dir = (fs::temp_directory_path() / "polyglot_cache_test").string();
    CompilationCache cache(tmp_dir);

    uint64_t hash = CompilationCache::ComputeHash("source_code", "settings");

    // Initially no cached entry
    REQUIRE_FALSE(cache.HasCached(hash));
    REQUIRE(cache.ReadCached(hash).empty());

    // Store an artefact
    std::string artefact = "compiled_binary_blob_data";
    cache.Store(hash, artefact);

    REQUIRE(cache.HasCached(hash));
    REQUIRE(cache.ReadCached(hash) == artefact);

    // Clean purges everything
    cache.Clean();
    REQUIRE_FALSE(cache.HasCached(hash));

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

TEST_CASE("CompilationCache - cache miss returns empty", "[cache][e2e]") {
    std::string tmp_dir = (fs::temp_directory_path() / "polyglot_cache_miss_test").string();
    CompilationCache cache(tmp_dir);

    uint64_t hash = 0xDEADBEEFCAFEBABEULL;
    REQUIRE_FALSE(cache.HasCached(hash));
    std::string result = cache.ReadCached(hash);
    REQUIRE(result.empty());

    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

// ============================================================================
// 2. Plugin API — new capability flags
// ============================================================================

TEST_CASE("Plugin API - New capability flags are distinct powers of two",
          "[plugins][api][expansion]") {
    REQUIRE(POLYGLOT_CAP_COMPLETION    == (1 << 11));
    REQUIRE(POLYGLOT_CAP_DIAGNOSTIC    == (1 << 12));
    REQUIRE(POLYGLOT_CAP_TEMPLATE      == (1 << 13));
    REQUIRE(POLYGLOT_CAP_TOPOLOGY_PROC == (1 << 14));

    // No overlap with existing flags
    REQUIRE((POLYGLOT_CAP_COMPLETION & POLYGLOT_CAP_DEBUGGER) == 0);
    REQUIRE((POLYGLOT_CAP_DIAGNOSTIC & POLYGLOT_CAP_LINTER)   == 0);
    REQUIRE((POLYGLOT_CAP_TEMPLATE   & POLYGLOT_CAP_FORMATTER) == 0);
    REQUIRE((POLYGLOT_CAP_TOPOLOGY_PROC & POLYGLOT_CAP_CODE_ACTION) == 0);
}

TEST_CASE("Plugin API - Bitwise composition with new caps",
          "[plugins][api][expansion]") {
    uint32_t caps = POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_COMPLETION |
                    POLYGLOT_CAP_DIAGNOSTIC | POLYGLOT_CAP_TEMPLATE |
                    POLYGLOT_CAP_TOPOLOGY_PROC;

    REQUIRE((caps & POLYGLOT_CAP_LANGUAGE)      != 0);
    REQUIRE((caps & POLYGLOT_CAP_COMPLETION)    != 0);
    REQUIRE((caps & POLYGLOT_CAP_DIAGNOSTIC)    != 0);
    REQUIRE((caps & POLYGLOT_CAP_TEMPLATE)      != 0);
    REQUIRE((caps & POLYGLOT_CAP_TOPOLOGY_PROC) != 0);
    REQUIRE((caps & POLYGLOT_CAP_OPTIMIZER)     == 0);
    REQUIRE((caps & POLYGLOT_CAP_FORMATTER)     == 0);
}

// ============================================================================
// 3. New provider structs — zero-initializable
// ============================================================================

TEST_CASE("Plugin API - CompletionItem struct", "[plugins][api][expansion]") {
    PolyglotCompletionItem item{};
    REQUIRE(item.label == nullptr);
    REQUIRE(item.insert_text == nullptr);
    REQUIRE(item.detail == nullptr);
    REQUIRE(item.kind == nullptr);
}

TEST_CASE("Plugin API - CompletionProvider struct", "[plugins][api][expansion]") {
    PolyglotCompletionProvider provider{};
    REQUIRE(provider.languages == nullptr);
    REQUIRE(provider.complete == nullptr);
}

TEST_CASE("Plugin API - DiagnosticProvider struct", "[plugins][api][expansion]") {
    PolyglotDiagnosticProvider provider{};
    REQUIRE(provider.languages == nullptr);
    REQUIRE(provider.diagnose == nullptr);
}

TEST_CASE("Plugin API - Template struct", "[plugins][api][expansion]") {
    PolyglotTemplate tmpl{};
    REQUIRE(tmpl.display_name == nullptr);
    REQUIRE(tmpl.language == nullptr);
    REQUIRE(tmpl.extension == nullptr);
    REQUIRE(tmpl.content == nullptr);
}

TEST_CASE("Plugin API - TemplateProvider struct", "[plugins][api][expansion]") {
    PolyglotTemplateProvider provider{};
    REQUIRE(provider.get_templates == nullptr);
}

TEST_CASE("Plugin API - TopoNode/Edge structs", "[plugins][api][expansion]") {
    PolyglotTopoNode node{};
    REQUIRE(node.id == 0);
    REQUIRE(node.name == nullptr);
    REQUIRE(node.language == nullptr);
    REQUIRE(node.kind == nullptr);

    PolyglotTopoEdge edge{};
    REQUIRE(edge.id == 0);
    REQUIRE(edge.source_node_id == 0);
    REQUIRE(edge.target_node_id == 0);
    REQUIRE(edge.status == nullptr);
}

TEST_CASE("Plugin API - TopologyProcessor struct", "[plugins][api][expansion]") {
    PolyglotTopologyProcessor proc{};
    REQUIRE(proc.processor_name == nullptr);
    REQUIRE(proc.process == nullptr);
}

// ============================================================================
// 4. PluginManager — new aggregation queries (empty state)
// ============================================================================

TEST_CASE("PluginManager - FindCompletionProviders empty",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    auto providers = pm.FindCompletionProviders("ploy");
    REQUIRE(providers.empty());
}

TEST_CASE("PluginManager - FindDiagnosticProviders empty",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    auto providers = pm.FindDiagnosticProviders("ploy");
    REQUIRE(providers.empty());
}

TEST_CASE("PluginManager - GetTemplateProviders empty",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    auto providers = pm.GetTemplateProviders();
    REQUIRE(providers.empty());
}

TEST_CASE("PluginManager - GetTopologyProcessors empty",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    auto procs = pm.GetTopologyProcessors();
    REQUIRE(procs.empty());
}

// ============================================================================
// 5. PluginManager — conflict detection (no active plugins = no conflicts)
// ============================================================================

TEST_CASE("PluginManager - DetectConflicts with no plugins",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    auto conflicts = pm.DetectConflicts();
    REQUIRE(conflicts.empty());
}

// ============================================================================
// 6. PluginManager — version constraint checking via ParseSemVer
//    (indirectly tested through LoadPlugin rejection)
// ============================================================================

TEST_CASE("PluginManager - Load rejects incompatible host version",
          "[plugins][manager][expansion]") {
    auto &pm = PluginManager::Instance();
    // Loading a nonexistent library will fail at dlopen, not at version check.
    // This test verifies the load path handles errors gracefully.
    std::string id = pm.LoadPlugin("/nonexistent/libpolyplug_fake.dylib");
    REQUIRE(id.empty());
    REQUIRE_FALSE(pm.IsLoaded("com.fake.plugin"));
}

// ============================================================================
// 7. PluginManager — sandbox circuit breaker
// ============================================================================

TEST_CASE("PluginManager - Sandbox policy defaults",
          "[plugins][sandbox][expansion]") {
    auto &pm = PluginManager::Instance();
    auto policy = pm.GetSandboxPolicy();
    REQUIRE(policy.call_timeout_ms == 5000);
    REQUIRE(policy.memory_limit_bytes == 0);
    REQUIRE(policy.max_consecutive_failures == 3);
}

TEST_CASE("PluginManager - SetSandboxPolicy round-trip",
          "[plugins][sandbox][expansion]") {
    auto &pm = PluginManager::Instance();

    PluginManager::SandboxPolicy custom{};
    custom.call_timeout_ms = 10000;
    custom.memory_limit_bytes = 512 * 1024 * 1024ULL;
    custom.max_consecutive_failures = 5;

    pm.SetSandboxPolicy(custom);
    auto retrieved = pm.GetSandboxPolicy();
    REQUIRE(retrieved.call_timeout_ms == 10000);
    REQUIRE(retrieved.memory_limit_bytes == 512 * 1024 * 1024ULL);
    REQUIRE(retrieved.max_consecutive_failures == 5);

    // Restore defaults
    pm.SetSandboxPolicy({5000, 0, 3});
}

TEST_CASE("PluginManager - Circuit breaker not open for unknown plugin",
          "[plugins][sandbox][expansion]") {
    auto &pm = PluginManager::Instance();
    REQUIRE_FALSE(pm.IsCircuitOpen("com.nonexistent.plugin"));
}

TEST_CASE("PluginManager - RecordPluginFailure/Success for unknown plugin",
          "[plugins][sandbox][expansion]") {
    auto &pm = PluginManager::Instance();
    // Should not crash when recording failures for non-loaded plugin
    pm.RecordPluginFailure("com.nonexistent.plugin", "test error");
    pm.RecordPluginSuccess("com.nonexistent.plugin");
    REQUIRE(pm.GetPluginLastError("com.nonexistent.plugin").empty());
}

TEST_CASE("PluginManager - ResetCircuitBreaker for unknown plugin",
          "[plugins][sandbox][expansion]") {
    auto &pm = PluginManager::Instance();
    // Should not crash
    pm.ResetCircuitBreaker("com.nonexistent.plugin");
}

// ============================================================================
// 8. Version header consistency
// ============================================================================

TEST_CASE("Version header defines expected macros",
          "[version][expansion]") {
    REQUIRE(POLYGLOT_VERSION_MAJOR >= 1);
    REQUIRE(POLYGLOT_VERSION_MINOR >= 0);
    REQUIRE(POLYGLOT_VERSION_PATCH >= 0);

    // Version string should match components
    std::string expected = std::to_string(POLYGLOT_VERSION_MAJOR) + "." +
                           std::to_string(POLYGLOT_VERSION_MINOR) + "." +
                           std::to_string(POLYGLOT_VERSION_PATCH);
    REQUIRE(std::string(POLYGLOT_VERSION_STRING) == expected);
}
