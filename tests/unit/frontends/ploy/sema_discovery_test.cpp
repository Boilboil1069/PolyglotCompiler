// ============================================================================
// Unit tests for PloySema package-discovery subsystem
//
// Covers:
//   1. Discovery disabled — no external commands triggered
//   2. Same config repeated Analyze — discovery runs only once
//   3. Different cache keys do not cross-contaminate
//   4. PackageDiscoveryCache thread safety and key generation
//   5. PackageIndexer pre-compilation phase
//   6. CommandResult timeout and structured output
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/common/include/diagnostics.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::CommandResult;
using polyglot::ploy::ICommandRunner;
using polyglot::ploy::DefaultCommandRunner;
using polyglot::ploy::PackageDiscoveryCache;
using polyglot::ploy::PackageIndexer;
using polyglot::ploy::PackageIndexerOptions;
using polyglot::ploy::PackageInfo;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

// ============================================================================
// MockCommandRunner — records every command and returns configurable output
// ============================================================================

namespace {

class MockCommandRunner : public ICommandRunner {
  public:
    CommandResult RunWithResult(
        const std::string &command,
        std::chrono::milliseconds /*timeout*/ = std::chrono::milliseconds{0}) override {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back(command);
        CommandResult r;
        r.stdout_output = output_;
        r.exit_code = exit_code_;
        r.timed_out = force_timeout_;
        r.failed = force_timeout_;
        return r;
    }

    // Set the canned output that RunWithResult() returns.
    void SetOutput(const std::string &output) { output_ = output; }

    // Set the exit code returned by RunWithResult().
    void SetExitCode(int code) { exit_code_ = code; }

    // Force all future calls to report a timeout.
    void SetForceTimeout(bool timeout) { force_timeout_ = timeout; }

    // Number of times RunWithResult() has been invoked.
    size_t CallCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return commands_.size();
    }

    // All commands received, in order.
    std::vector<std::string> Commands() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return commands_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<std::string> commands_;
    std::string output_;
    int exit_code_{0};
    bool force_timeout_{false};
};

// Helper: parse + sema-analyze a .ploy source string.
bool AnalyzeCode(const std::string &code, Diagnostics &diags, PloySema &sema) {
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return false;
    return sema.Analyze(module);
}

} // namespace

// ============================================================================
// PackageDiscoveryCache unit tests
// ============================================================================

TEST_CASE("PackageDiscoveryCache: MakeKey builds canonical key", "[ploy][discovery][cache]") {
    auto key = PackageDiscoveryCache::MakeKey("python", "pip", "/env/py310");
    REQUIRE(key == "python|pip|/env/py310");
}

TEST_CASE("PackageDiscoveryCache: store and retrieve round-trip", "[ploy][discovery][cache]") {
    PackageDiscoveryCache cache;
    const auto key = PackageDiscoveryCache::MakeKey("python", "pip", "");

    REQUIRE_FALSE(cache.HasDiscovered(key));

    std::unordered_map<std::string, polyglot::ploy::PackageInfo> pkgs;
    pkgs["numpy"] = {"numpy", "1.24.3", "python", "", {}};
    cache.Store(key, pkgs);
    cache.MarkDiscovered(key);

    REQUIRE(cache.HasDiscovered(key));
    auto retrieved = cache.Retrieve(key);
    REQUIRE(retrieved.size() == 1);
    REQUIRE(retrieved.count("numpy") == 1);
    REQUIRE(retrieved["numpy"].version == "1.24.3");
}

TEST_CASE("PackageDiscoveryCache: different keys do not cross-contaminate", "[ploy][discovery][cache]") {
    PackageDiscoveryCache cache;
    const auto key_pip   = PackageDiscoveryCache::MakeKey("python", "pip", "/env/pip");
    const auto key_conda = PackageDiscoveryCache::MakeKey("python", "conda", "myenv");

    std::unordered_map<std::string, polyglot::ploy::PackageInfo> pip_pkgs;
    pip_pkgs["requests"] = {"requests", "2.31.0", "python", "", {}};
    cache.Store(key_pip, pip_pkgs);
    cache.MarkDiscovered(key_pip);

    // Conda key is still undiscovered
    REQUIRE_FALSE(cache.HasDiscovered(key_conda));
    auto conda_result = cache.Retrieve(key_conda);
    REQUIRE(conda_result.empty());

    // Pip key should still be accessible
    REQUIRE(cache.HasDiscovered(key_pip));
    auto pip_result = cache.Retrieve(key_pip);
    REQUIRE(pip_result.count("requests") == 1);
}

TEST_CASE("PackageDiscoveryCache: Clear removes all entries", "[ploy][discovery][cache]") {
    PackageDiscoveryCache cache;
    const auto key = PackageDiscoveryCache::MakeKey("rust", "cargo", "");

    std::unordered_map<std::string, polyglot::ploy::PackageInfo> pkgs;
    pkgs["serde"] = {"serde", "1.0.0", "rust", "", {}};
    cache.Store(key, pkgs);
    cache.MarkDiscovered(key);
    REQUIRE(cache.HasDiscovered(key));

    cache.Clear();
    REQUIRE_FALSE(cache.HasDiscovered(key));
    REQUIRE(cache.Retrieve(key).empty());
}

// ============================================================================
// PloySema discovery integration tests
// ============================================================================

TEST_CASE("Discovery disabled — no external commands triggered", "[ploy][discovery][sema]") {
    auto mock = std::make_shared<MockCommandRunner>();
    // Return some fake package output — should never be used when disabled
    mock->SetOutput("numpy==1.24.3\nrequests==2.31.0\n");

    PloySemaOptions opts;
    opts.enable_package_discovery = false; // default is now false
    opts.command_runner = mock;

    Diagnostics diags;
    PloySema sema(diags, opts);
    REQUIRE_FALSE(sema.IsDiscoveryEnabled());

    // This code has an IMPORT PACKAGE which would normally trigger discovery
    std::string code = R"(
        IMPORT python PACKAGE numpy;
    )";

    // Should still succeed (import recorded but no discovery)
    AnalyzeCode(code, diags, sema);

    // The mock runner must NOT have been invoked at all
    REQUIRE(mock->CallCount() == 0);
}

TEST_CASE("PloySemaOptions defaults to discovery disabled", "[ploy][discovery][sema]") {
    PloySemaOptions opts;
    REQUIRE_FALSE(opts.enable_package_discovery);

    Diagnostics diags;
    PloySema sema(diags, opts);
    REQUIRE_FALSE(sema.IsDiscoveryEnabled());
}

TEST_CASE("Same config repeated Analyze — discovery runs only once", "[ploy][discovery][sema]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("numpy==1.24.3\nrequests==2.31.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PloySemaOptions opts;
    opts.enable_package_discovery = true;
    opts.command_runner = mock;
    opts.discovery_cache = cache;

    std::string code = R"(
        IMPORT python PACKAGE numpy;
    )";

    // First compilation — discovery should run
    {
        Diagnostics diags1;
        PloySema sema1(diags1, opts);
        AnalyzeCode(code, diags1, sema1);
    }
    size_t first_count = mock->CallCount();
    REQUIRE(first_count > 0);

    // Second compilation with the SAME shared cache — discovery must NOT run again
    {
        Diagnostics diags2;
        PloySema sema2(diags2, opts);
        AnalyzeCode(code, diags2, sema2);
    }
    size_t second_count = mock->CallCount();
    REQUIRE(second_count == first_count);
}

TEST_CASE("Different discovery keys do not share cache", "[ploy][discovery][sema]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("serde==1.0.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PloySemaOptions opts;
    opts.enable_package_discovery = true;
    opts.command_runner = mock;
    opts.discovery_cache = cache;

    std::string python_code = R"(
        IMPORT python PACKAGE numpy;
    )";
    std::string rust_code = R"(
        IMPORT rust PACKAGE serde;
    )";

    // Analyze Python code — triggers Python discovery
    {
        Diagnostics diags;
        PloySema sema(diags, opts);
        AnalyzeCode(python_code, diags, sema);
    }
    size_t after_python = mock->CallCount();
    REQUIRE(after_python > 0);

    // Analyze Rust code — should trigger a separate Rust discovery (different cache key)
    {
        Diagnostics diags;
        PloySema sema(diags, opts);
        AnalyzeCode(rust_code, diags, sema);
    }
    size_t after_rust = mock->CallCount();
    REQUIRE(after_rust > after_python);
}

TEST_CASE("Discovery with shared cache surfaces packages to new sema instances", "[ploy][discovery][sema]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("flask==3.0.0\nrequests==2.31.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PloySemaOptions opts;
    opts.enable_package_discovery = true;
    opts.command_runner = mock;
    opts.discovery_cache = cache;

    std::string code = R"(
        IMPORT python PACKAGE flask;
    )";

    // First compilation populates the cache
    {
        Diagnostics diags;
        PloySema sema(diags, opts);
        AnalyzeCode(code, diags, sema);

        auto pkgs = sema.DiscoveredPackages();
        // The discovery populated some entries
        REQUIRE_FALSE(pkgs.empty());
    }

    // Second compilation reuses the cache — packages are still available
    {
        Diagnostics diags;
        PloySema sema(diags, opts);
        AnalyzeCode(code, diags, sema);

        auto pkgs = sema.DiscoveredPackages();
        REQUIRE_FALSE(pkgs.empty());
    }
}

TEST_CASE("MockCommandRunner records commands in order", "[ploy][discovery][mock]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("result");

    mock->Run("cmd1");
    mock->Run("cmd2");
    mock->Run("cmd3");

    REQUIRE(mock->CallCount() == 3);
    auto cmds = mock->Commands();
    REQUIRE(cmds[0] == "cmd1");
    REQUIRE(cmds[1] == "cmd2");
    REQUIRE(cmds[2] == "cmd3");
}

// ============================================================================
// CommandResult tests
// ============================================================================

TEST_CASE("CommandResult::Ok() reports success correctly", "[ploy][discovery][result]") {
    CommandResult ok_result;
    ok_result.exit_code = 0;
    ok_result.timed_out = false;
    ok_result.failed = false;
    REQUIRE(ok_result.Ok());

    CommandResult timeout_result;
    timeout_result.exit_code = 0;
    timeout_result.timed_out = true;
    timeout_result.failed = true;
    REQUIRE_FALSE(timeout_result.Ok());

    CommandResult failed_result;
    failed_result.exit_code = 1;
    failed_result.timed_out = false;
    failed_result.failed = false;
    REQUIRE_FALSE(failed_result.Ok());
}

TEST_CASE("MockCommandRunner returns structured CommandResult", "[ploy][discovery][result]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("some output");
    mock->SetExitCode(0);

    auto result = mock->RunWithResult("test-cmd");
    REQUIRE(result.stdout_output == "some output");
    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(result.timed_out);
    REQUIRE(result.Ok());
}

TEST_CASE("MockCommandRunner can simulate timeout", "[ploy][discovery][result]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("partial");
    mock->SetForceTimeout(true);

    auto result = mock->RunWithResult("slow-cmd", std::chrono::seconds{5});
    REQUIRE(result.timed_out);
    REQUIRE(result.failed);
    REQUIRE_FALSE(result.Ok());
}

// ============================================================================
// PackageIndexer tests
// ============================================================================

TEST_CASE("PackageIndexer populates cache from mock commands", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("numpy==1.24.3\nrequests==2.31.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PackageIndexerOptions idx_opts;
    idx_opts.command_timeout = std::chrono::seconds{5};
    idx_opts.max_retries = 0;

    PackageIndexer indexer(cache, mock, idx_opts);
    indexer.BuildIndex({"python"});

    // Cache should now be populated
    auto key = PackageDiscoveryCache::MakeKey("python", "venv", "");
    REQUIRE(cache->HasDiscovered(key));

    auto pkgs = cache->Retrieve(key);
    REQUIRE(pkgs.count("python::numpy") == 1);
    REQUIRE(pkgs["python::numpy"].version == "1.24.3");
    REQUIRE(pkgs.count("python::requests") == 1);
    REQUIRE(pkgs["python::requests"].version == "2.31.0");

    // Stats should reflect the indexing
    auto stats = indexer.LastStats();
    REQUIRE(stats.languages_indexed == 1);
    REQUIRE(stats.commands_executed >= 1);
    REQUIRE(stats.packages_found >= 2);
}

TEST_CASE("PackageIndexer skips already-cached languages", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("flask==3.0.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PackageIndexer indexer(cache, mock);

    // First index populates the cache
    indexer.BuildIndex({"python"});
    size_t first_count = mock->CallCount();
    REQUIRE(first_count > 0);

    // Second index should skip — cache hit
    indexer.BuildIndex({"python"});
    size_t second_count = mock->CallCount();
    REQUIRE(second_count == first_count);
}

TEST_CASE("PackageIndexer handles timeout gracefully", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetForceTimeout(true);

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PackageIndexerOptions idx_opts;
    idx_opts.command_timeout = std::chrono::milliseconds{100};
    idx_opts.max_retries = 0; // no retries

    PackageIndexer indexer(cache, mock, idx_opts);
    indexer.BuildIndex({"python"});

    auto stats = indexer.LastStats();
    REQUIRE(stats.commands_timed_out >= 1);
    REQUIRE(stats.packages_found == 0);
}

TEST_CASE("PackageIndexer indexes multiple languages independently", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("serde==1.0.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    PackageIndexer indexer(cache, mock);
    indexer.BuildIndex({"python", "rust"});

    auto stats = indexer.LastStats();
    REQUIRE(stats.languages_indexed == 2);

    // Both languages should have cache entries
    auto py_key = PackageDiscoveryCache::MakeKey("python", "venv", "");
    auto rs_key = PackageDiscoveryCache::MakeKey("rust", "venv", "");
    REQUIRE(cache->HasDiscovered(py_key));
    REQUIRE(cache->HasDiscovered(rs_key));
}

TEST_CASE("PackageIndexer pre-phase feeds sema via shared cache", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("numpy==1.24.3\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();

    // Phase 1: Run the indexer to populate the cache
    PackageIndexer indexer(cache, mock);
    indexer.BuildIndex({"python"});

    size_t index_count = mock->CallCount();
    REQUIRE(index_count > 0);

    // Phase 2: Run sema with discovery OFF but cache pre-populated
    PloySemaOptions opts;
    opts.enable_package_discovery = false;
    opts.discovery_cache = cache;
    opts.command_runner = mock;

    std::string code = R"(
        IMPORT python PACKAGE numpy;
    )";

    Diagnostics diags;
    PloySema sema(diags, opts);
    AnalyzeCode(code, diags, sema);

    // Sema should NOT have invoked any additional commands
    REQUIRE(mock->CallCount() == index_count);
}

TEST_CASE("PackageIndexer progress callback is invoked", "[ploy][discovery][indexer]") {
    auto mock = std::make_shared<MockCommandRunner>();
    mock->SetOutput("pkg==1.0.0\n");

    auto cache = std::make_shared<PackageDiscoveryCache>();
    PackageIndexerOptions idx_opts;
    idx_opts.verbose = true;

    PackageIndexer indexer(cache, mock, idx_opts);

    std::vector<std::string> progress_messages;
    indexer.SetProgressCallback(
        [&](const std::string &lang, const std::string &msg) {
            progress_messages.push_back(lang + ": " + msg);
        });

    indexer.BuildIndex({"python"});

    // At least one progress message should have been emitted
    REQUIRE_FALSE(progress_messages.empty());
}
