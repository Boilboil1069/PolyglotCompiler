/**
 * @file     driver.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// driver.cpp — polyc top-level driver (thin orchestration layer)
//
// This file contains only:
//   1. ParseArgs()  — CLI flag parsing → DriverSettings
//   2. main()       — stage orchestration: calls RunXxxStage() in order
//
// All heavy lifting has been moved into the six stage_*.cpp files.
// ============================================================================

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "common/include/version.h"
#include "frontends/common/include/frontend_registry.h"
#include "runtime/include/libs/base.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "tools/polyc/include/driver_stages.h"
#include "tools/polyc/src/stage_backend.h"
#include "tools/polyc/src/stage_bridge.h"
#include "tools/polyc/src/stage_frontend.h"
#include "tools/polyc/src/stage_marshal.h"
#include "tools/polyc/src/stage_packaging.h"
#include "tools/polyc/src/stage_semantic.h"

namespace polyglot::tools {
namespace {

namespace fs = std::filesystem;

// ── Helpers ──────────────────────────────────────────────────────────────────

std::string DetectLanguage(const std::string &path) {
    return polyglot::frontends::FrontendRegistry::Instance().DetectLanguage(path);
}

std::string ReadFileContent(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::string content(static_cast<std::size_t>(size), '\0');
    ifs.read(content.data(), size);
    return content;
}

// Resolve a sibling tool binary (e.g. "polyld") relative to the directory
// containing the current executable (argv[0]).  Returns the bare name if
// no sibling is found so that PATH-based lookup remains the fallback.
std::string ResolveSiblingTool(const char *argv0, const std::string &tool_name) {
    if (!argv0 || !*argv0) return tool_name;
    std::error_code ec;
    fs::path self = fs::canonical(argv0, ec);
    if (ec) self = fs::path(argv0);
    fs::path candidate = self.parent_path() / tool_name;
    if (fs::exists(candidate, ec))
        return candidate.string();
    // On Windows, also try with .exe extension
#if defined(_WIN32)
    candidate = self.parent_path() / (tool_name + ".exe");
    if (fs::exists(candidate, ec))
        return candidate.string();
#endif
    return tool_name;
}

// ── ParseArgs ────────────────────────────────────────────────────────────────

DriverSettings ParseArgs(int argc, char **argv) {
    DriverSettings s;
    bool source_set = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: polyc [options] <source-file-or-code>\n"
                << "\n"
                << "Options:\n"
                << "  --lang=<lang>       Language: ploy|python|cpp|rust|java|dotnet\n"
                << "  -O<0-3>             Optimisation level\n"
                << "  -o <output>         Output file name\n"
                << "  --mode=<mode>       compile|assemble|link\n"
                << "  --arch=<arch>       x86_64|arm64|wasm\n"
                << "  --emit-ir=<path>    Write IR to file\n"
                << "  --emit-asm=<path>   Write assembly to file\n"
                << "  --emit-obj=<path>   Write object to file\n"
                << "  --obj-format=<fmt>  pobj|coff|elf|macho\n"
                << "  --quiet             Suppress progress output\n"
                << "  --no-aux            Do not emit auxiliary files\n"
                << "  --force             Continue despite errors\n"
                << "  --strict            Reject placeholders; disable degraded stubs\n"
                << "  --dev               Dev mode: permissive + degraded fallbacks allowed\n"
                << "  --permissive        Explicit permissive override\n"
                << "  --package-index     Run explicit package-index phase (default)\n"
                << "  --no-package-index  Skip package-index phase\n"
                << "  --pkg-timeout=<ms>  Per-command timeout for package indexing\n"
                << "  -j<N>               Parallelism hint\n"
                << "  --regalloc=<mode>   linear-scan|graph-coloring\n"
                << "\n"
                << "Source can be a file path or inline code.\n";
            std::exit(0);
        }
        if (arg.rfind("--lang=", 0) == 0) {
            s.language = arg.substr(7); s.language_explicit = true; continue;
        }
        if (arg == "--quiet" || arg == "-q") { s.verbose = false; continue; }
        if (arg == "--no-aux") { s.emit_aux = false; continue; }
        if (arg.rfind("--mode=", 0) == 0) { s.mode = arg.substr(7); continue; }
        if (arg.rfind("--opt=", 0) == 0) {
            auto lvl = arg.substr(6);
            if (lvl == "O1") s.opt_level = 1;
            else if (lvl == "O2") s.opt_level = 2;
            else if (lvl == "O3") s.opt_level = 3;
            else s.opt_level = 0;
            continue;
        }
        if (arg.rfind("-O", 0) == 0 && arg.size() == 3) {
            char c = arg[2];
            if (c >= '0' && c <= '3') s.opt_level = c - '0';
            continue;
        }
        if (arg == "-o" && i + 1 < argc) { s.output = argv[++i]; continue; }
        if (arg.rfind("-o", 0) == 0 && arg.size() > 2) { s.output = arg.substr(2); continue; }
        if (arg.rfind("--emit-asm=", 0) == 0) { s.emit_asm_path = arg.substr(11); continue; }
        if (arg.rfind("--emit-obj=", 0) == 0) { s.emit_obj_path = arg.substr(11); continue; }
        if (arg.rfind("--obj-format=", 0) == 0) { s.obj_format = arg.substr(13); continue; }
        if (arg.rfind("--polyld=", 0) == 0) { s.polyld_path = arg.substr(9); continue; }
        if (arg.rfind("--emit-ir=", 0) == 0) { s.emit_ir_path = arg.substr(10); continue; }
        if (arg.rfind("--arch=", 0) == 0) { s.arch = arg.substr(7); continue; }
        if (arg == "--force" || arg == "--ignore-diagnostics") { s.force = true; continue; }
        if (arg == "--strict") { s.strict = true; continue; }
        if (arg == "--dev")   { s.dev_mode = true; s.force = true; continue; }
        if (arg == "--permissive") { s.permissive = true; continue; }
        if (arg == "--package-index") { s.package_index = true; continue; }
        if (arg == "--no-package-index") { s.package_index = false; continue; }
        if (arg.rfind("--pkg-timeout=", 0) == 0) {
            s.package_index_timeout_ms = std::atoi(arg.substr(14).c_str());
            if (s.package_index_timeout_ms < 0) s.package_index_timeout_ms = 0;
            continue;
        }
        if (arg == "-j" && i + 1 < argc) { s.jobs = std::atoi(argv[++i]); continue; }
        if (arg.rfind("-j", 0) == 0 && arg.size() > 2) {
            s.jobs = std::atoi(arg.substr(2).c_str());
            if (s.jobs < 1) s.jobs = 1;
            continue;
        }
        if (arg.rfind("--regalloc=", 0) == 0) {
            auto m = arg.substr(11);
            s.regalloc = (m == "graph" || m == "graph-coloring" || m == "coloring")
                       ? RegAllocChoice::kGraphColoring
                       : RegAllocChoice::kLinearScan;
            continue;
        }
        if (arg == "--pp-cpp")     { s.pp_overrides["cpp"]    = true;  continue; }
        if (arg == "--no-pp-cpp")  { s.pp_overrides["cpp"]    = false; continue; }
        if (arg == "--pp-rust")    { s.pp_overrides["rust"]   = true;  continue; }
        if (arg == "--no-pp-rust") { s.pp_overrides["rust"]   = false; continue; }
        if (arg == "--pp-python")  { s.pp_overrides["python"] = true;  continue; }
        if (arg == "--no-pp-python"){ s.pp_overrides["python"] = false; continue; }
        if (arg.rfind("--I=", 0) == 0) { s.include_paths.push_back(arg.substr(4)); continue; }
        if (arg == "--I" && i + 1 < argc) { s.include_paths.push_back(argv[++i]); continue; }
        if (!source_set) { s.source = arg; source_set = true; continue; }
    }

    // File source: read content + auto-detect language
    if (source_set && fs::exists(s.source)) {
        s.source_path = fs::absolute(s.source).string();
        s.source = ReadFileContent(s.source_path);
        if (s.source.empty()) {
            std::cerr << "[error] could not read file: " << s.source_path << "\n";
            std::exit(1);
        }
        if (!s.language_explicit) {
            auto det = DetectLanguage(s.source_path);
            if (!det.empty()) s.language = det;
        }
        s.include_paths.push_back(fs::path(s.source_path).parent_path().string());
    }
    return s;
}

// ── SetupAuxDir / SourceStem ─────────────────────────────────────────────────

std::string SetupAuxDir(const DriverSettings &s) {
    if (!s.emit_aux || s.source_path.empty()) return {};
    fs::path aux = fs::path(s.source_path).parent_path() / "aux";
    std::error_code ec;
    fs::create_directories(aux, ec);
    if (ec) { std::cerr << "[warn] could not create aux/: " << ec.message() << "\n"; return {}; }
    return aux.string();
}

std::string SourceStem(const DriverSettings &s) {
    return s.source_path.empty() ? "output" : fs::path(s.source_path).stem().string();
}

// ── StageTimer ───────────────────────────────────────────────────────────────

struct StageTimer {
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
    bool verbose;
    StageTimer(const std::string &n, bool v)
        : name(n), verbose(v), start(std::chrono::high_resolution_clock::now()) {
        if (verbose) std::cerr << "[polyc] " << name << "... " << std::flush;
    }
    double Stop() {
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start).count();
        if (verbose)
            std::cerr << "done (" << std::fixed << std::setprecision(1) << ms << "ms)\n";
        return ms;
    }
};

}  // namespace (anonymous)
}  // namespace polyglot::tools

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
    using namespace polyglot::tools;

    auto total_start = std::chrono::high_resolution_clock::now();
    DriverSettings settings = ParseArgs(argc, argv);

    // Auto-resolve polyld path relative to the current executable when the
    // user has not provided an explicit --polyld= override.
    if (settings.polyld_path == "polyld") {
        settings.polyld_path = ResolveSiblingTool(argv[0], "polyld");
    }

    // ── Mode validation ──────────────────────────────────────────────────
    if (settings.mode != "compile" && settings.mode != "assemble" && settings.mode != "link") {
        std::cerr << "[error] Unknown mode: " << settings.mode
                  << " (use compile|assemble|link)\n";
        return 1;
    }

    // ── Strict / permissive / force reconciliation ───────────────────────
#ifdef POLYC_DEFAULT_STRICT
    if (!settings.permissive && !settings.strict && !settings.dev_mode)
        settings.strict = true;
#endif
    if (settings.strict && settings.permissive) {
        std::cerr << "[error] --strict and --permissive are mutually exclusive\n";
        return 1;
    }
    if (settings.strict && settings.force) {
        std::cerr << "[error] --force is not allowed in strict mode\n";
        return 1;
    }

    const bool V = settings.verbose;

    // ── Banner ───────────────────────────────────────────────────────────
    if (V) {
        std::cerr << "========================================\n";
        std::cerr << " " << POLYGLOT_VERSION_BANNER << "  (" << POLYGLOT_POLYC_NAME << ")\n";
        std::cerr << "========================================\n";
        if (!settings.source_path.empty())
            std::cerr << "[polyc] Source: " << settings.source_path << "\n";
        std::cerr << "[polyc] Language: " << settings.language
                  << (settings.language_explicit ? " (explicit)" : " (auto-detected)") << "\n";
        std::cerr << "[polyc] Arch: " << settings.arch << "\n";
        std::cerr << "[polyc] Opt: O" << settings.opt_level << "\n";
        std::cerr << "[polyc] Output: " << settings.output << "\n";
        std::cerr << "[polyc] Mode: "
                  << (settings.dev_mode ? "dev" : settings.strict ? "strict" : "permissive") << "\n";
        std::cerr << "----------------------------------------\n";
    }

    if (settings.jobs > 1 && V)
        std::cerr << "[polyc] -j" << settings.jobs << " noted (single-threaded for now)\n";

    // ── .ploy: delegate to existing CompilationPipeline ──────────────────
    if (settings.language == "ploy") {
        std::string aux_dir = SetupAuxDir(settings);
        std::string source_label = settings.source_path.empty() ? "<cli>" : settings.source_path;

        polyglot::compilation::CompilationContext::Config cfg;
        cfg.source_file   = settings.source_path;
        cfg.source_text   = settings.source;
        cfg.source_language = settings.language;
        cfg.output_file   = settings.output;
        cfg.target_arch   = settings.arch;
        cfg.mode          = settings.mode;
        cfg.object_format = settings.obj_format;
        cfg.polyld_path   = settings.polyld_path;
        cfg.source_label  = source_label;
        cfg.opt_level     = settings.opt_level;
        cfg.verbose       = settings.verbose;
        cfg.strict_mode   = settings.strict;
        cfg.force         = settings.force;
        cfg.aux_dir       = aux_dir;
        cfg.package_index = settings.package_index;
        cfg.package_index_timeout_ms = settings.package_index_timeout_ms;
        cfg.emit_ir_path  = settings.emit_ir_path;
        cfg.emit_asm_path = settings.emit_asm_path;
        cfg.emit_obj_path = settings.emit_obj_path;

        polyglot::compilation::CompilationPipeline pipeline(std::move(cfg));
        bool ok = false;
        {
            StageTimer t("Staged compilation pipeline (.ploy)", V);
            ok = pipeline.RunAll();
            t.Stop();
        }
        if (!ok) {
            std::cerr << "[error] staged pipeline failed.\n";
            for (const auto &d : pipeline.GetContext().diagnostics->All())
                std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
            return 1;
        }
        if (V) {
            for (const auto &tm : pipeline.GetContext().timings)
                std::cerr << "  - " << tm.name << ": "
                          << std::fixed << std::setprecision(1) << tm.elapsed_ms << "ms\n";
            std::cerr << "[polyc] Compilation successful (staged pipeline).\n";
        }
        return 0;
    }

    // ── Non-.ploy: six-stage pipeline ─────────────────────────────────────
    std::string aux_dir = SetupAuxDir(settings);
    std::string stem    = SourceStem(settings);
    if (!aux_dir.empty() && V)
        std::cerr << "[polyc] Aux dir: " << aux_dir << "\n";

    // GC scratch root (required by runtime)
    void *scratch = polyglot_alloc(32);
    polyglot_gc_register_root(&scratch);

    // Stage 1
    FrontendResult frontend;
    { StageTimer t("Stage 1: frontend", V); frontend = RunFrontendStage(settings); t.Stop(); }
    if (!frontend.success && !settings.force) {
        std::cerr << "[error] Frontend stage failed.\n";
        for (const auto &d : frontend.diagnostics.All())
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // Stage 2
    SemanticResult semantic;
    { StageTimer t("Stage 2: semantic", V); semantic = RunSemanticStage(settings, frontend); t.Stop(); }
    if (!semantic.success && !settings.force) {
        std::cerr << "[error] Semantic stage failed.\n";
        for (const auto &d : semantic.diagnostics.All())
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // Stage 3
    MarshalResult marshal;
    { StageTimer t("Stage 3: marshal", V); marshal = RunMarshalStage(settings, semantic); t.Stop(); }

    // Stage 4
    BridgeResult bridge;
    { StageTimer t("Stage 4: bridge", V); bridge = RunBridgeStage(settings, marshal, semantic, aux_dir, stem); t.Stop(); }
    if (!bridge.success && !settings.force) {
        std::cerr << "[error] Bridge stage failed.\n";
        for (const auto &d : bridge.diagnostics.All())
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // Stage 5
    BackendResult backend;
    { StageTimer t("Stage 5: backend", V); backend = RunBackendStage(settings, frontend, semantic, bridge); t.Stop(); }
    if (!backend.success) {
        std::cerr << "[error] Backend stage failed.\n";
        for (const auto &d : backend.diagnostics.All())
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // Stage 6
    PackagingResult packaging;
    { StageTimer t("Stage 6: packaging", V); packaging = RunPackagingStage(settings, backend, bridge, aux_dir, stem); t.Stop(); }
    if (!packaging.success) {
        std::cerr << "[error] Packaging stage failed.\n";
        for (const auto &d : packaging.diagnostics.All())
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // ── Summary ──────────────────────────────────────────────────────────
    double total_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - total_start).count();
    if (V) {
        std::cerr << "----------------------------------------\n";
        std::cerr << "[polyc] Target: " << backend.target_triple << "\n";
        std::cerr << "[polyc] Format: " << settings.obj_format << "\n";
        std::cerr << "[polyc] Total:  " << std::fixed << std::setprecision(1)
                  << total_ms << "ms\n";
        if (!aux_dir.empty())
            std::cerr << "[polyc] Aux: " << aux_dir << "\n";
        std::cerr << "[polyc] Compilation successful.\n";
        std::cerr << "========================================\n";
    }

    polyglot_gc_unregister_root(&scratch);
    return 0;
}