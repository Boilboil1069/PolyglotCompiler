/**
 * @file     package_indexer.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::ploy {

// ============================================================================
// PackageIndexerOptions — configuration for the explicit index phase
// ============================================================================

/** @brief PackageIndexerOptions data structure. */
struct PackageIndexerOptions {
    // Per-command timeout.  Defaults to 10 seconds — enough for `pip list`
    // but short enough to avoid blocking on slow networks (mvn, nuget).
    std::chrono::milliseconds command_timeout{std::chrono::seconds{10}};

    // Maximum number of retry attempts for failed (non-timeout) commands.
    int max_retries{1};

    // Emit progress messages via the progress callback.
    bool verbose{false};
};

// ============================================================================
// PackageIndexer — explicit pre-compilation package-index stage
//
// Instead of letting PloySema invoke external package managers inline during
// semantic analysis, callers run PackageIndexer::BuildIndex() once before
// compilation.  The indexer populates a shared PackageDiscoveryCache that is
// then passed into PloySemaOptions::discovery_cache.  This decouples the
// slow, side-effecting environment probing from the fast, deterministic
// semantic analysis pass.
//
// Typical usage:
//
//   auto cache   = std::make_shared<PackageDiscoveryCache>();
//   auto runner  = std::make_shared<DefaultCommandRunner>(10s);
//   PackageIndexer indexer(cache, runner);
//   indexer.BuildIndex(languages, venv_configs);
//
//   PloySemaOptions sema_opts;
//   sema_opts.enable_package_discovery = false;   // sema never shells out
//   sema_opts.discovery_cache = cache;            // pre-populated
//   PloySema sema(diags, sema_opts);
//   sema.Analyze(module);
//
// ============================================================================

/** @brief PackageIndexer class. */
class PackageIndexer {
  public:
    // Optional progress callback: (language, message)
    using ProgressCallback = std::function<void(const std::string &, const std::string &)>;

    PackageIndexer(
        std::shared_ptr<PackageDiscoveryCache> cache,
        std::shared_ptr<ICommandRunner> runner,
        const PackageIndexerOptions &opts = {});

    // Build the package index for the given set of languages.  For each
    // language, the indexer runs the appropriate package-manager commands
    // and stores the results in the shared cache.
    //
    // `venv_configs` provides optional virtual-environment configuration
    // per language (e.g. conda env name, venv path).  Languages not present
    // in the map use the system default.
    void BuildIndex(
        const std::vector<std::string> &languages,
        const std::vector<VenvConfig> &venv_configs = {});

    // Build the index for a single language.
    void IndexLanguage(
        const std::string &language,
        const std::string &venv_path = "",
        VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv);

    // Set a callback invoked during indexing for progress reporting.
    void SetProgressCallback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

    // Retrieve the shared cache (for passing to PloySemaOptions).
    std::shared_ptr<PackageDiscoveryCache> Cache() const { return cache_; }

    // Statistics from the most recent BuildIndex() call.
    /** @brief Stats data structure. */
    struct Stats {
        int languages_indexed{0};
        int commands_executed{0};
        int commands_timed_out{0};
        int commands_failed{0};
        int packages_found{0};
        std::chrono::milliseconds total_duration{0};
    };
    const Stats &LastStats() const { return stats_; }

  private:
    // Language-specific indexing helpers.  Each populates `packages` and
    // returns the number of packages discovered.
    void IndexPython(const std::string &venv_path, VenvConfigDecl::ManagerKind manager,
                     std::unordered_map<std::string, PackageInfo> &packages);
    void IndexPythonViaPip(const std::string &python_cmd,
                           std::unordered_map<std::string, PackageInfo> &packages);
    void IndexPythonViaConda(const std::string &env_name,
                             std::unordered_map<std::string, PackageInfo> &packages);
    void IndexPythonViaUv(const std::string &venv_path,
                          std::unordered_map<std::string, PackageInfo> &packages);
    void IndexPythonViaPipenv(const std::string &project_path,
                              std::unordered_map<std::string, PackageInfo> &packages);
    void IndexPythonViaPoetry(const std::string &project_path,
                              std::unordered_map<std::string, PackageInfo> &packages);
    void IndexRust(std::unordered_map<std::string, PackageInfo> &packages);
    void IndexCpp(std::unordered_map<std::string, PackageInfo> &packages);
    void IndexJava(const std::string &classpath,
                   std::unordered_map<std::string, PackageInfo> &packages);
    void IndexJavaViaMaven(const std::string &project_path,
                           std::unordered_map<std::string, PackageInfo> &packages);
    void IndexJavaViaGradle(const std::string &project_path,
                            std::unordered_map<std::string, PackageInfo> &packages);
    void IndexDotnet(std::unordered_map<std::string, PackageInfo> &packages);
    void IndexDotnetNuget(std::unordered_map<std::string, PackageInfo> &packages);

    // Execute a command with timeout and retry, updating stats_.
    CommandResult Execute(const std::string &command);

    // Parse pip-style "name==version" output lines.
    void ParseFreezeOutput(const std::string &output, const std::string &language,
                           std::unordered_map<std::string, PackageInfo> &packages);

    void ReportProgress(const std::string &language, const std::string &message);

    std::shared_ptr<PackageDiscoveryCache> cache_;
    std::shared_ptr<ICommandRunner> runner_;
    PackageIndexerOptions opts_;
    ProgressCallback progress_cb_;
    Stats stats_{};
};

} // namespace polyglot::ploy
