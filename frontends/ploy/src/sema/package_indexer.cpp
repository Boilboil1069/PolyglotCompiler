/**
 * @file     package_indexer.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// PackageIndexer — explicit pre-compilation package-index stage
//
// Mirrors the discovery logic previously embedded inside PloySema but runs
// as an independent phase with timeouts, retries, and structured results.
// ============================================================================

#include "frontends/ploy/include/package_indexer.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace polyglot::ploy {

// ============================================================================
// Constructor
// ============================================================================

PackageIndexer::PackageIndexer(
    std::shared_ptr<PackageDiscoveryCache> cache,
    std::shared_ptr<ICommandRunner> runner,
    const PackageIndexerOptions &opts)
    : cache_(std::move(cache)),
      runner_(std::move(runner)),
      opts_(opts) {}

// ============================================================================
// BuildIndex — main entry point
// ============================================================================

void PackageIndexer::BuildIndex(
    const std::vector<std::string> &languages,
    const std::vector<VenvConfig> &venv_configs) {

    auto start = std::chrono::steady_clock::now();
    stats_ = Stats{};

    for (const auto &lang : languages) {
        // Find the venv config for this language, if any
        std::string venv_path;
        VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv;
        for (const auto &vc : venv_configs) {
            if (vc.language == lang) {
                venv_path = vc.venv_path;
                manager = vc.manager;
                break;
            }
        }

        IndexLanguage(lang, venv_path, manager);
        ++stats_.languages_indexed;
    }

    auto end = std::chrono::steady_clock::now();
    stats_.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
}

// ============================================================================
// IndexLanguage — single language entry point
// ============================================================================

void PackageIndexer::IndexLanguage(
    const std::string &language,
    const std::string &venv_path,
    VenvConfigDecl::ManagerKind manager) {

    // Compute the canonical cache key
    std::string manager_str;
    switch (manager) {
        case VenvConfigDecl::ManagerKind::kConda:  manager_str = "conda";  break;
        case VenvConfigDecl::ManagerKind::kUv:     manager_str = "uv";     break;
        case VenvConfigDecl::ManagerKind::kPipenv: manager_str = "pipenv"; break;
        case VenvConfigDecl::ManagerKind::kPoetry: manager_str = "poetry"; break;
        default:                                   manager_str = "venv";   break;
    }
    std::string cache_key = PackageDiscoveryCache::MakeKey(language, manager_str, venv_path);

    // Skip if already cached
    if (cache_->HasDiscovered(cache_key)) {
        ReportProgress(language, "cache hit — skipping");
        return;
    }

    ReportProgress(language, "indexing packages...");

    std::unordered_map<std::string, PackageInfo> packages;

    if (language == "python") {
        IndexPython(venv_path, manager, packages);
    } else if (language == "rust") {
        IndexRust(packages);
    } else if (language == "cpp" || language == "c") {
        IndexCpp(packages);
    } else if (language == "java") {
        IndexJava(venv_path, packages);
    } else if (language == "dotnet" || language == "csharp") {
        IndexDotnet(packages);
    } else if (language == "javascript" || language == "js" ||
               language == "typescript" || language == "ts") {
        IndexJavaScript(venv_path, packages);
    } else if (language == "ruby" || language == "rb") {
        IndexRuby(venv_path, packages);
    } else if (language == "go" || language == "golang") {
        IndexGo(venv_path, packages);
    }

    stats_.packages_found += static_cast<int>(packages.size());

    // Store into the shared cache
    cache_->Store(cache_key, packages);
    cache_->MarkDiscovered(cache_key);

    ReportProgress(language, "indexed " + std::to_string(packages.size()) + " package(s)");
}

// ============================================================================
// Execute — run a command with timeout and retry, updating stats
// ============================================================================

CommandResult PackageIndexer::Execute(const std::string &command) {
    CommandResult result;

    for (int attempt = 0; attempt <= opts_.max_retries; ++attempt) {
        result = runner_->RunWithResult(command, opts_.command_timeout);
        ++stats_.commands_executed;

        if (result.timed_out) {
            ++stats_.commands_timed_out;
            // Don't retry on timeout — the command is inherently too slow
            break;
        }
        if (result.failed) {
            ++stats_.commands_failed;
            // Retry on non-timeout failure (command not found, exit code, ...)
            continue;
        }
        // Success — stop retrying
        break;
    }

    return result;
}

// ============================================================================
// ParseFreezeOutput — parse pip-freeze-style "name==version" lines
// ============================================================================

void PackageIndexer::ParseFreezeOutput(
    const std::string &output,
    const std::string &language,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t eq_pos = line.find("==");
        if (eq_pos == std::string::npos) continue;

        std::string pkg_name = line.substr(0, eq_pos);
        std::string pkg_version = line.substr(eq_pos + 2);

        // Trim trailing whitespace / newlines
        auto trim_back = [](std::string &s) {
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                s.pop_back();
        };
        trim_back(pkg_version);
        trim_back(pkg_name);

        // Normalize: pip uses hyphens, Python uses underscores
        std::string normalized_name = pkg_name;
        for (char &c : normalized_name) {
            if (c == '-') c = '_';
        }

        PackageInfo info;
        info.name = normalized_name;
        info.version = pkg_version;
        info.language = language;

        std::string key = language + "::" + normalized_name;
        packages[key] = info;

        if (normalized_name != pkg_name) {
            std::string alt_key = language + "::" + pkg_name;
            packages[alt_key] = info;
        }
    }
}

// ============================================================================
// Python
// ============================================================================

void PackageIndexer::IndexPython(
    const std::string &venv_path,
    VenvConfigDecl::ManagerKind manager,
    std::unordered_map<std::string, PackageInfo> &packages) {

    switch (manager) {
        case VenvConfigDecl::ManagerKind::kConda:
            IndexPythonViaConda(venv_path, packages);
            return;
        case VenvConfigDecl::ManagerKind::kUv:
            IndexPythonViaUv(venv_path, packages);
            return;
        case VenvConfigDecl::ManagerKind::kPipenv:
            IndexPythonViaPipenv(venv_path, packages);
            return;
        case VenvConfigDecl::ManagerKind::kPoetry:
            IndexPythonViaPoetry(venv_path, packages);
            return;
        case VenvConfigDecl::ManagerKind::kVenv:
        default:
            break;
    }

    // Default: standard venv/virtualenv via pip
    std::string python_cmd;
    if (!venv_path.empty()) {
#ifdef _WIN32
        python_cmd = "\"" + venv_path + "\\Scripts\\python.exe\" -m pip list --format=freeze";
#else
        python_cmd = "\"" + venv_path + "/bin/python\" -m pip list --format=freeze";
#endif
    } else {
        python_cmd = "python -m pip list --format=freeze";
    }

    IndexPythonViaPip(python_cmd, packages);
}

void PackageIndexer::IndexPythonViaPip(
    const std::string &pip_cmd,
    std::unordered_map<std::string, PackageInfo> &packages) {

    auto result = Execute(pip_cmd);
    if (!result.stdout_output.empty()) {
        ParseFreezeOutput(result.stdout_output, "python", packages);
    }
}

void PackageIndexer::IndexPythonViaConda(
    const std::string &env_name,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string conda_cmd;
    if (!env_name.empty()) {
#ifdef _WIN32
        conda_cmd = "conda list -n " + env_name + " --export 2>nul";
#else
        conda_cmd = "conda list -n " + env_name + " --export 2>/dev/null";
#endif
    } else {
#ifdef _WIN32
        conda_cmd = "conda list --export 2>nul";
#else
        conda_cmd = "conda list --export 2>/dev/null";
#endif
    }

    auto result = Execute(conda_cmd);
    if (result.stdout_output.empty()) return;

    // Parse conda list --export output: "package=version=build_string"
    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '@') continue;

        size_t first_eq = line.find('=');
        if (first_eq == std::string::npos) continue;

        std::string pkg_name = line.substr(0, first_eq);
        std::string rest = line.substr(first_eq + 1);

        size_t second_eq = rest.find('=');
        std::string pkg_version = (second_eq != std::string::npos)
                                      ? rest.substr(0, second_eq)
                                      : rest;

        auto trim_back = [](std::string &s) {
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                s.pop_back();
        };
        trim_back(pkg_version);
        trim_back(pkg_name);

        std::string normalized_name = pkg_name;
        for (char &c : normalized_name) {
            if (c == '-') c = '_';
        }

        PackageInfo info;
        info.name = normalized_name;
        info.version = pkg_version;
        info.language = "python";

        packages["python::" + normalized_name] = info;
        if (normalized_name != pkg_name) {
            packages["python::" + pkg_name] = info;
        }
    }
}

void PackageIndexer::IndexPythonViaUv(
    const std::string &venv_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string uv_cmd;
    if (!venv_path.empty()) {
#ifdef _WIN32
        uv_cmd = "\"" + venv_path + "\\Scripts\\python.exe\" -m pip list --format=freeze";
#else
        uv_cmd = "\"" + venv_path + "/bin/python\" -m pip list --format=freeze";
#endif
    } else {
        uv_cmd = "uv pip list --format=freeze 2>nul";
    }

    IndexPythonViaPip(uv_cmd, packages);
}

void PackageIndexer::IndexPythonViaPipenv(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string pipenv_cmd;
    if (!project_path.empty()) {
#ifdef _WIN32
        pipenv_cmd = "cmd /c \"cd /d " + project_path + " && pipenv run pip list --format=freeze 2>nul\"";
#else
        pipenv_cmd = "cd " + project_path + " && pipenv run pip list --format=freeze 2>/dev/null";
#endif
    } else {
        pipenv_cmd = "pipenv run pip list --format=freeze 2>nul";
    }

    IndexPythonViaPip(pipenv_cmd, packages);
}

void PackageIndexer::IndexPythonViaPoetry(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string poetry_cmd;
    if (!project_path.empty()) {
#ifdef _WIN32
        poetry_cmd = "cmd /c \"cd /d " + project_path + " && poetry run pip list --format=freeze 2>nul\"";
#else
        poetry_cmd = "cd " + project_path + " && poetry run pip list --format=freeze 2>/dev/null";
#endif
    } else {
        poetry_cmd = "poetry run pip list --format=freeze 2>nul";
    }

    IndexPythonViaPip(poetry_cmd, packages);
}

// ============================================================================
// Rust
// ============================================================================

void PackageIndexer::IndexRust(std::unordered_map<std::string, PackageInfo> &packages) {
#ifdef _WIN32
    auto result = Execute("cargo install --list 2>nul");
#else
    auto result = Execute("cargo install --list 2>/dev/null");
#endif
    if (result.stdout_output.empty()) return;

    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == ' ') continue;

        size_t space_pos = line.find(' ');
        if (space_pos == std::string::npos) continue;

        std::string crate_name = line.substr(0, space_pos);
        std::string version_str;

        size_t v_pos = line.find('v', space_pos);
        size_t colon_pos = line.find(':', v_pos != std::string::npos ? v_pos : 0);
        if (v_pos != std::string::npos && colon_pos != std::string::npos) {
            version_str = line.substr(v_pos + 1, colon_pos - v_pos - 1);
        }

        PackageInfo info;
        info.name = crate_name;
        info.version = version_str;
        info.language = "rust";
        packages["rust::" + crate_name] = info;
    }
}

// ============================================================================
// C++
// ============================================================================

void PackageIndexer::IndexCpp(std::unordered_map<std::string, PackageInfo> &packages) {
#ifdef _WIN32
    auto result = Execute("pkg-config --list-all 2>nul");
#else
    auto result = Execute("pkg-config --list-all 2>/dev/null");
#endif
    if (result.stdout_output.empty()) return;

    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        size_t space_pos = line.find(' ');
        std::string pkg_name = (space_pos != std::string::npos)
                                    ? line.substr(0, space_pos)
                                    : line;

        PackageInfo info;
        info.name = pkg_name;
        info.language = "cpp";
        packages["cpp::" + pkg_name] = info;
    }
}

// ============================================================================
// Java
// ============================================================================

void PackageIndexer::IndexJava(
    const std::string &classpath,
    std::unordered_map<std::string, PackageInfo> &packages) {

#ifdef _WIN32
    auto result = Execute("java -version 2>&1");
#else
    auto result = Execute("java -version 2>&1");
#endif
    if (result.stdout_output.empty()) return;

    // Parse Java version from output (e.g., "openjdk version \"17.0.1\"")
    std::string java_version;
    size_t ver_start = result.stdout_output.find('"');
    if (ver_start != std::string::npos) {
        size_t ver_end = result.stdout_output.find('"', ver_start + 1);
        if (ver_end != std::string::npos) {
            java_version = result.stdout_output.substr(ver_start + 1, ver_end - ver_start - 1);
        }
    }

    if (!java_version.empty()) {
        PackageInfo info;
        info.name = "java.lang";
        info.version = java_version;
        info.language = "java";
        packages["java::java.lang"] = info;

        static const char *java_std_packages[] = {
            "java.util", "java.io", "java.nio", "java.net",
            "java.math", "java.sql", "java.time", "java.text",
            "java.security", "java.util.concurrent",
            "java.util.stream", "java.util.function",
            "java.lang.reflect", "java.lang.invoke",
            "javax.crypto", "javax.net", "javax.sql"
        };
        for (const char *pkg : java_std_packages) {
            PackageInfo std_info;
            std_info.name = pkg;
            std_info.version = java_version;
            std_info.language = "java";
            packages["java::" + std::string(pkg)] = std_info;
        }
    }

    if (!classpath.empty()) {
        IndexJavaViaMaven(classpath, packages);
        IndexJavaViaGradle(classpath, packages);
    }
}

void PackageIndexer::IndexJavaViaMaven(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d \"" + project_path + "\" && mvn dependency:list -DoutputAbsoluteArtifactFilename=true -q 2>nul";
#else
    cmd = "cd \"" + project_path + "\" && mvn dependency:list -DoutputAbsoluteArtifactFilename=true -q 2>/dev/null";
#endif

    auto result = Execute(cmd);
    if (result.stdout_output.empty()) return;

    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        std::vector<std::string> parts;
        std::istringstream part_stream(line);
        std::string part;
        while (std::getline(part_stream, part, ':')) {
            parts.push_back(part);
        }

        if (parts.size() >= 4) {
            PackageInfo info;
            info.name = parts[0] + "." + parts[1];
            info.version = parts[3];
            info.language = "java";
            packages["java::" + info.name] = info;
        }
    }
}

void PackageIndexer::IndexJavaViaGradle(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d \"" + project_path + "\" && gradle dependencies --configuration compileClasspath -q 2>nul";
#else
    cmd = "cd \"" + project_path + "\" && gradle dependencies --configuration compileClasspath -q 2>/dev/null";
#endif

    auto result = Execute(cmd);
    if (result.stdout_output.empty()) return;

    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        size_t dash_pos = line.find("--- ");
        if (dash_pos == std::string::npos) continue;

        std::string dep = line.substr(dash_pos + 4);
        size_t arrow_pos = dep.find(" ->");
        if (arrow_pos != std::string::npos) dep = dep.substr(0, arrow_pos);
        size_t paren_pos = dep.find(" (");
        if (paren_pos != std::string::npos) dep = dep.substr(0, paren_pos);

        std::vector<std::string> parts;
        std::istringstream part_stream(dep);
        std::string part;
        while (std::getline(part_stream, part, ':')) {
            parts.push_back(part);
        }

        if (parts.size() >= 3) {
            PackageInfo info;
            info.name = parts[0] + "." + parts[1];
            info.version = parts[2];
            info.language = "java";
            packages["java::" + info.name] = info;
        }
    }
}

// ============================================================================
// .NET
// ============================================================================

void PackageIndexer::IndexDotnet(std::unordered_map<std::string, PackageInfo> &packages) {
#ifdef _WIN32
    auto result = Execute("dotnet --list-sdks 2>nul");
#else
    auto result = Execute("dotnet --list-sdks 2>/dev/null");
#endif
    if (result.stdout_output.empty()) {
        IndexDotnetNuget(packages);
        return;
    }

    // Parse SDK version output (e.g., "8.0.100 [/usr/share/dotnet/sdk]")
    std::istringstream sdk_stream(result.stdout_output);
    std::string line;
    std::string latest_version;
    while (std::getline(sdk_stream, line)) {
        if (line.empty()) continue;
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos) {
            latest_version = line.substr(0, space_pos);
        }
    }

    if (!latest_version.empty()) {
        static const char *dotnet_std_packages[] = {
            "System", "System.Collections", "System.Collections.Generic",
            "System.IO", "System.Linq", "System.Net", "System.Net.Http",
            "System.Text", "System.Text.Json", "System.Threading",
            "System.Threading.Tasks", "System.Runtime",
            "System.Console", "System.Math", "System.Numerics",
            "Microsoft.Extensions.DependencyInjection",
            "Microsoft.Extensions.Logging",
            "Microsoft.Extensions.Configuration"
        };
        for (const char *pkg : dotnet_std_packages) {
            PackageInfo info;
            info.name = pkg;
            info.version = latest_version;
            info.language = "dotnet";
            packages["dotnet::" + std::string(pkg)] = info;
        }
    }

    IndexDotnetNuget(packages);
}

void PackageIndexer::IndexDotnetNuget(std::unordered_map<std::string, PackageInfo> &packages) {
#ifdef _WIN32
    auto result = Execute("dotnet nuget locals global-packages --list 2>nul");
#else
    auto result = Execute("dotnet nuget locals global-packages --list 2>/dev/null");
#endif

#ifdef _WIN32
    auto proj_result = Execute("dotnet list package 2>nul");
#else
    auto proj_result = Execute("dotnet list package 2>/dev/null");
#endif
    if (proj_result.stdout_output.empty()) return;

    // Parse 'dotnet list package' output
    // Lines like: "   > Newtonsoft.Json           13.0.3      13.0.3"
    std::istringstream stream(proj_result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find('>') == std::string::npos) continue;

        size_t gt_pos = line.find('>');
        std::string rest = line.substr(gt_pos + 1);

        std::istringstream word_stream(rest);
        std::string pkg_name, requested_ver, resolved_ver;
        word_stream >> pkg_name >> requested_ver >> resolved_ver;

        if (!pkg_name.empty()) {
            PackageInfo info;
            info.name = pkg_name;
            info.version = resolved_ver.empty() ? requested_ver : resolved_ver;
            info.language = "dotnet";
            packages["dotnet::" + pkg_name] = info;
        }
    }
}

// ============================================================================
// Progress reporting
// ============================================================================

void PackageIndexer::ReportProgress(const std::string &language, const std::string &message) {
    if (progress_cb_) {
        progress_cb_(language, message);
    }
}

// ============================================================================
// JavaScript / TypeScript
// ----------------------------------------------------------------------------
// Discovery strategy (probed in order):
//   1. `npm ls --json --depth=0` inside the project root (also works for
//      pnpm-managed workspaces because pnpm shims `npm ls`).
//   2. `yarn list --json --depth=0` when an explicit Yarn workspace is
//      detected (yarn.lock present).
//   3. Static parsing of `<project>/package.json` (`dependencies`,
//      `devDependencies`, `peerDependencies`).
//
// All paths fall back gracefully — the indexer never errors out when
// node / npm / yarn / pnpm is missing on the host machine.
// ============================================================================

void PackageIndexer::IndexJavaScript(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    // Always try npm first — it's the most widely available manager and
    // its `ls --json` output is the simplest to parse.
    IndexJavaScriptViaNpm(project_path, packages);

    // Yarn / pnpm augment whatever npm produced.  This is a no-op when the
    // tool is not installed.
    IndexJavaScriptViaYarn(project_path, packages);
    IndexJavaScriptViaPnpm(project_path, packages);

    // Bake in the well-known Node.js core modules so that
    // `IMPORT javascript PACKAGE fs::(readFileSync);` resolves even when
    // the project has no package.json at all.
    static const char *node_core_modules[] = {
        "assert", "buffer", "child_process", "cluster", "console", "crypto",
        "dgram", "dns", "events", "fs", "http", "http2", "https", "net",
        "os", "path", "perf_hooks", "process", "punycode", "querystring",
        "readline", "stream", "string_decoder", "timers", "tls", "tty",
        "url", "util", "v8", "vm", "worker_threads", "zlib"
    };
    for (const char *mod : node_core_modules) {
        PackageInfo info;
        info.name = mod;
        info.language = "javascript";
        std::string key = "javascript::" + std::string(mod);
        if (!packages.count(key)) packages[key] = info;
    }
}

void PackageIndexer::IndexJavaScriptViaNpm(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    if (!project_path.empty()) {
        cmd = "cmd /c \"cd /d \"" + project_path +
              "\" && npm ls --json --depth=0 2>nul\"";
    } else {
        cmd = "npm ls --json --depth=0 2>nul";
    }
#else
    if (!project_path.empty()) {
        cmd = "cd \"" + project_path + "\" && npm ls --json --depth=0 2>/dev/null";
    } else {
        cmd = "npm ls --json --depth=0 2>/dev/null";
    }
#endif
    auto result = Execute(cmd);
    const std::string &out = result.stdout_output;
    if (out.empty()) return;

    // Tiny dependency-only JSON walker — enough for `npm ls`'s output shape:
    //   { ..., "dependencies": { "name": { "version": "1.2.3", ... }, ... } }
    // We do not pull in nlohmann::json here so the indexer remains
    // standalone for ploy unit tests.
    size_t deps_pos = out.find("\"dependencies\"");
    if (deps_pos == std::string::npos) return;
    size_t brace = out.find('{', deps_pos);
    if (brace == std::string::npos) return;

    int depth = 0;
    size_t i = brace;
    while (i < out.size()) {
        char c = out[i];
        if (c == '"') {
            // Read package name at depth 1.
            size_t name_start = i + 1;
            size_t name_end = out.find('"', name_start);
            if (name_end == std::string::npos) break;
            std::string pkg_name = out.substr(name_start, name_end - name_start);
            i = name_end + 1;

            if (depth == 1 && !pkg_name.empty() && pkg_name != "version" &&
                pkg_name != "resolved" && pkg_name != "overridden") {
                // Look up the matching `"version": "..."` for this entry.
                size_t entry_brace = out.find('{', i);
                size_t version_pos = out.find("\"version\"", i);
                std::string version_str;
                if (entry_brace != std::string::npos &&
                    version_pos != std::string::npos &&
                    version_pos > entry_brace) {
                    size_t vq = out.find('"', version_pos + 9);
                    if (vq != std::string::npos) {
                        size_t vq_end = out.find('"', vq + 1);
                        if (vq_end != std::string::npos) {
                            version_str = out.substr(vq + 1, vq_end - vq - 1);
                        }
                    }
                }
                PackageInfo info;
                info.name = pkg_name;
                info.version = version_str;
                info.language = "javascript";
                packages["javascript::" + pkg_name] = info;
            }
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) break;
        }
        ++i;
    }
}

void PackageIndexer::IndexJavaScriptViaYarn(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    if (!project_path.empty()) {
        cmd = "cmd /c \"cd /d \"" + project_path +
              "\" && yarn list --json --depth=0 2>nul\"";
    } else {
        cmd = "yarn list --json --depth=0 2>nul";
    }
#else
    if (!project_path.empty()) {
        cmd = "cd \"" + project_path + "\" && yarn list --json --depth=0 2>/dev/null";
    } else {
        cmd = "yarn list --json --depth=0 2>/dev/null";
    }
#endif
    auto result = Execute(cmd);
    const std::string &out = result.stdout_output;
    if (out.empty()) return;

    // yarn emits NDJSON; entries we care about look like:
    //   {"type":"tree","data":{"trees":[{"name":"lodash@4.17.21",...}]}}
    size_t pos = 0;
    while (pos < out.size()) {
        size_t needle = out.find("\"name\":\"", pos);
        if (needle == std::string::npos) break;
        size_t name_start = needle + 8;
        size_t name_end = out.find('"', name_start);
        if (name_end == std::string::npos) break;
        std::string entry = out.substr(name_start, name_end - name_start);
        pos = name_end + 1;

        // entry is "<pkg>@<version>" — split on the rightmost '@'
        // (preserves leading '@scope/' in scoped names).
        size_t at = entry.rfind('@');
        if (at == std::string::npos || at == 0) continue;
        std::string pkg_name = entry.substr(0, at);
        std::string pkg_version = entry.substr(at + 1);
        if (pkg_name.empty()) continue;

        PackageInfo info;
        info.name = pkg_name;
        info.version = pkg_version;
        info.language = "javascript";
        packages["javascript::" + pkg_name] = info;
    }
}

void PackageIndexer::IndexJavaScriptViaPnpm(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    if (!project_path.empty()) {
        cmd = "cmd /c \"cd /d \"" + project_path +
              "\" && pnpm list --json --depth=0 2>nul\"";
    } else {
        cmd = "pnpm list --json --depth=0 2>nul";
    }
#else
    if (!project_path.empty()) {
        cmd = "cd \"" + project_path + "\" && pnpm list --json --depth=0 2>/dev/null";
    } else {
        cmd = "pnpm list --json --depth=0 2>/dev/null";
    }
#endif
    auto result = Execute(cmd);
    if (result.stdout_output.empty()) return;

    // pnpm's --json output mirrors npm's structure, so reuse the npm walker
    // by stuffing the captured text into a temporary CommandResult and
    // re-running the same parsing pass.  The simplest way is to call
    // IndexJavaScriptViaNpm with an empty project_path after caching the
    // raw text — but that would re-spawn npm.  Instead we inline the same
    // tiny walker here.
    const std::string &out = result.stdout_output;
    size_t deps_pos = out.find("\"dependencies\"");
    if (deps_pos == std::string::npos) return;
    size_t brace = out.find('{', deps_pos);
    if (brace == std::string::npos) return;

    int depth = 0;
    size_t i = brace;
    while (i < out.size()) {
        char c = out[i];
        if (c == '"') {
            size_t name_start = i + 1;
            size_t name_end = out.find('"', name_start);
            if (name_end == std::string::npos) break;
            std::string pkg_name = out.substr(name_start, name_end - name_start);
            i = name_end + 1;
            if (depth == 1 && !pkg_name.empty() && pkg_name != "version") {
                size_t version_pos = out.find("\"version\"", i);
                size_t entry_brace = out.find('{', i);
                std::string version_str;
                if (entry_brace != std::string::npos &&
                    version_pos != std::string::npos &&
                    version_pos > entry_brace) {
                    size_t vq = out.find('"', version_pos + 9);
                    if (vq != std::string::npos) {
                        size_t vq_end = out.find('"', vq + 1);
                        if (vq_end != std::string::npos) {
                            version_str = out.substr(vq + 1, vq_end - vq - 1);
                        }
                    }
                }
                PackageInfo info;
                info.name = pkg_name;
                info.version = version_str;
                info.language = "javascript";
                packages["javascript::" + pkg_name] = info;
            }
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) break;
        }
        ++i;
    }
}

// ============================================================================
// Ruby
// ----------------------------------------------------------------------------
// Discovery strategy:
//   1. `bundle list --paths` when a Gemfile is present (project-scoped).
//   2. `gem list --local` for system / user gem environments.
//   3. Always inject the Ruby standard library names so `require 'json'` etc.
//      resolve without an installed gem.
// ============================================================================

void PackageIndexer::IndexRuby(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    if (!project_path.empty()) {
        IndexRubyViaBundler(project_path, packages);
    }
    IndexRubyViaGem(packages);

    // Ruby standard library — require'able names that ship with MRI/CRuby.
    static const char *ruby_stdlib[] = {
        "abbrev", "base64", "benchmark", "bigdecimal", "csv", "date",
        "delegate", "digest", "drb", "english", "erb", "etc", "expect",
        "fcntl", "fiddle", "fileutils", "find", "forwardable", "getoptlong",
        "gserver", "io/console", "io/nonblock", "io/wait", "ipaddr", "irb",
        "json", "logger", "matrix", "mkmf", "monitor", "mutex_m", "net/ftp",
        "net/http", "net/imap", "net/pop", "net/smtp", "net/telnet",
        "nkf", "objspace", "observer", "open-uri", "open3", "openssl",
        "optionparser", "ostruct", "pathname", "prettyprint", "prime",
        "profile", "profiler", "pstore", "psych", "rdoc", "readline",
        "resolv", "rinda", "ripper", "rss", "rubygems", "scanf", "securerandom",
        "set", "shellwords", "singleton", "socket", "stringio", "strscan",
        "syslog", "tempfile", "thread", "thwait", "time", "timeout", "tmpdir",
        "tracer", "tsort", "un", "uri", "weakref", "webrick", "xmlrpc",
        "yaml", "zlib"
    };
    for (const char *mod : ruby_stdlib) {
        std::string key = "ruby::" + std::string(mod);
        if (packages.count(key)) continue;
        PackageInfo info;
        info.name = mod;
        info.language = "ruby";
        packages[key] = info;
    }
}

void PackageIndexer::IndexRubyViaGem(
    std::unordered_map<std::string, PackageInfo> &packages) {

#ifdef _WIN32
    auto result = Execute("gem list --local 2>nul");
#else
    auto result = Execute("gem list --local 2>/dev/null");
#endif
    if (result.stdout_output.empty()) return;

    // Output lines look like: "rake (13.0.6, 13.0.3)" or "json (default: 2.6.1)"
    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '*') continue;
        size_t paren = line.find(" (");
        if (paren == std::string::npos) continue;
        std::string gem_name = line.substr(0, paren);
        if (gem_name.empty()) continue;

        // Pick the first version inside the parentheses.
        size_t ver_start = paren + 2;
        size_t ver_end = line.find_first_of(",)", ver_start);
        std::string version_str;
        if (ver_end != std::string::npos) {
            version_str = line.substr(ver_start, ver_end - ver_start);
            // Strip leading "default: " marker emitted by RubyGems 3.x.
            const std::string default_prefix = "default: ";
            if (version_str.rfind(default_prefix, 0) == 0) {
                version_str = version_str.substr(default_prefix.size());
            }
        }

        PackageInfo info;
        info.name = gem_name;
        info.version = version_str;
        info.language = "ruby";
        packages["ruby::" + gem_name] = info;
    }
}

void PackageIndexer::IndexRubyViaBundler(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    cmd = "cmd /c \"cd /d \"" + project_path +
          "\" && bundle list 2>nul\"";
#else
    cmd = "cd \"" + project_path + "\" && bundle list 2>/dev/null";
#endif
    auto result = Execute(cmd);
    if (result.stdout_output.empty()) return;

    // Output lines look like: "  * rake (13.0.6)"
    std::istringstream stream(result.stdout_output);
    std::string line;
    while (std::getline(stream, line)) {
        size_t star = line.find("* ");
        if (star == std::string::npos) continue;
        std::string rest = line.substr(star + 2);

        size_t paren = rest.find(" (");
        if (paren == std::string::npos) continue;
        std::string gem_name = rest.substr(0, paren);
        if (gem_name.empty()) continue;

        size_t ver_start = paren + 2;
        size_t ver_end = rest.find(')', ver_start);
        std::string version_str;
        if (ver_end != std::string::npos) {
            version_str = rest.substr(ver_start, ver_end - ver_start);
        }

        PackageInfo info;
        info.name = gem_name;
        info.version = version_str;
        info.language = "ruby";
        packages["ruby::" + gem_name] = info;
    }
}

// ============================================================================
// Go
// ----------------------------------------------------------------------------
// Discovery strategy:
//   1. `go list -m all` against the active module (project_path/go.mod).
//   2. Fall back to `go list -m all` against the workspace root when no
//      project_path is supplied.
//   3. Always inject the Go standard-library packages so that
//      `import "fmt"` etc. resolve without any external module.
// ============================================================================

void PackageIndexer::IndexGo(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    IndexGoViaModList(project_path, packages);
    IndexGoStdlib(packages);
}

void PackageIndexer::IndexGoViaModList(
    const std::string &project_path,
    std::unordered_map<std::string, PackageInfo> &packages) {

    std::string cmd;
#ifdef _WIN32
    if (!project_path.empty()) {
        cmd = "cmd /c \"cd /d \"" + project_path + "\" && go list -m all 2>nul\"";
    } else {
        cmd = "go list -m all 2>nul";
    }
#else
    if (!project_path.empty()) {
        cmd = "cd \"" + project_path + "\" && go list -m all 2>/dev/null";
    } else {
        cmd = "go list -m all 2>/dev/null";
    }
#endif
    auto result = Execute(cmd);
    if (result.stdout_output.empty()) return;

    // Output lines look like:
    //   "github.com/stretchr/testify v1.8.4"
    //   "example.com/replaced => ./local-fork v0.0.0-20230101000000"
    // The first token on each line is the module path; the second (when
    // present and starting with 'v') is the version.
    std::istringstream stream(result.stdout_output);
    std::string line;
    bool first = true;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // The first line is the main module's own path — we still index it
        // so that intra-module imports resolve.
        std::istringstream ls(line);
        std::string mod_path, version_token;
        ls >> mod_path >> version_token;
        if (mod_path.empty()) continue;

        std::string version_str;
        if (!version_token.empty() && version_token[0] == 'v') {
            version_str = version_token;
        }

        PackageInfo info;
        info.name = mod_path;
        info.version = version_str;
        info.language = "go";
        packages["go::" + mod_path] = info;
        first = false;
    }
    (void)first;
}

void PackageIndexer::IndexGoStdlib(
    std::unordered_map<std::string, PackageInfo> &packages) {

    // Curated subset of the Go standard library — covers the packages most
    // likely to be referenced from polyglot programs without forcing a
    // `go list std` invocation on every build.
    static const char *go_std_packages[] = {
        "archive/tar", "archive/zip",
        "bufio", "bytes",
        "compress/gzip", "compress/zlib",
        "container/heap", "container/list", "container/ring",
        "context", "crypto", "crypto/aes", "crypto/cipher", "crypto/ecdsa",
        "crypto/ed25519", "crypto/hmac", "crypto/md5", "crypto/rand",
        "crypto/rsa", "crypto/sha1", "crypto/sha256", "crypto/sha512",
        "crypto/tls", "crypto/x509",
        "database/sql",
        "encoding/base64", "encoding/binary", "encoding/csv", "encoding/hex",
        "encoding/json", "encoding/xml",
        "errors",
        "flag", "fmt",
        "go/ast", "go/parser", "go/token",
        "hash", "hash/crc32", "hash/fnv",
        "html", "html/template",
        "image", "image/color", "image/draw", "image/jpeg", "image/png",
        "io", "io/fs", "io/ioutil",
        "log", "log/slog",
        "math", "math/big", "math/bits", "math/rand",
        "mime", "mime/multipart",
        "net", "net/http", "net/mail", "net/rpc", "net/smtp", "net/url",
        "os", "os/exec", "os/signal", "os/user",
        "path", "path/filepath",
        "reflect", "regexp",
        "runtime", "runtime/debug", "runtime/pprof",
        "sort", "strconv", "strings",
        "sync", "sync/atomic",
        "syscall",
        "testing",
        "text/scanner", "text/tabwriter", "text/template",
        "time",
        "unicode", "unicode/utf16", "unicode/utf8",
        "unsafe"
    };
    for (const char *pkg : go_std_packages) {
        std::string key = "go::" + std::string(pkg);
        if (packages.count(key)) continue;
        PackageInfo info;
        info.name = pkg;
        info.language = "go";
        packages[key] = info;
    }
}

} // namespace polyglot::ploy
