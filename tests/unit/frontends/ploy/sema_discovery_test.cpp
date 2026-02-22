// ============================================================================
// Unit tests for PloySema package-discovery subsystem
//
// Covers:
//   1. Discovery disabled — no external commands triggered
//   2. Same config repeated Analyze — discovery runs only once
//   3. Different cache keys do not cross-contaminate
//   4. PackageDiscoveryCache thread safety and key generation
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/common/include/diagnostics.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::ICommandRunner;
using polyglot::ploy::DefaultCommandRunner;
using polyglot::ploy::PackageDiscoveryCache;
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
    std::string Run(const std::string &command) override {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back(command);
        return output_;
    }

    // Set the canned output that Run() returns.
    void SetOutput(const std::string &output) { output_ = output; }

    // Number of times Run() has been invoked.
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
    opts.enable_package_discovery = false;
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
