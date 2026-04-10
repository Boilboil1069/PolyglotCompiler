// ============================================================================
// stage_semantic.cpp — Stage 2 implementation
// ============================================================================

#include "tools/polyc/src/stage_semantic.h"

#include <filesystem>
#include <iostream>
#include <sstream>

#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyc/src/foreign_signature_extractor.h"

namespace polyglot::tools {

namespace fs = std::filesystem;

SemanticResult RunSemanticStage(const DriverSettings &settings,
                                const FrontendResult &frontend) {
    SemanticResult result;
    const bool V = settings.verbose;

    // Non-.ploy: the frontend already produced IR — nothing to do here.
    if (settings.language != "ploy" || !frontend.ast) {
        result.success = frontend.success;
        return result;
    }

    // ── Sema ─────────────────────────────────────────────────────────────────
    ploy::PloySemaOptions opts;
    opts.strict_mode = settings.strict;
    opts.enable_package_discovery = false;  // sema never shells out; indexer ran in stage 1
    opts.discovery_cache = frontend.pkg_cache;

    result.sema = std::make_shared<ploy::PloySema>(result.diagnostics, opts);
    const bool ok = result.sema->Analyze(frontend.ast);

    // ── Foreign Signature Extraction ─────────────────────────────────────────
    // After sema has processed the .ploy AST (which registers LINK-based
    // signatures), extract type signatures from the actual foreign source
    // files referenced by IMPORT declarations.  This fills in real
    // parameter/return types for functions that were otherwise only known as
    // "Any".
    {
        ForeignExtractionOptions feopts;
        // Base directory = directory containing the .ploy source file.
        if (!settings.source_path.empty()) {
            feopts.base_directory =
                fs::path(settings.source_path).parent_path().string();
        }
        feopts.include_paths = settings.include_paths;
        feopts.verbose = V;

        ForeignSignatureExtractor extractor(feopts);
        auto foreign_sigs = extractor.ExtractAll(*frontend.ast);

        if (!foreign_sigs.empty()) {
            result.sema->InjectForeignSignatures(foreign_sigs);
            if (V) {
                std::cerr << "[stage/semantic] Injected "
                          << foreign_sigs.size()
                          << " foreign signature(s)\n";
            }
        }
    }

    if (!ok && !settings.force) {
        result.success = false;
        if (V) std::cerr << "[stage/semantic] FAILED\n";
        return result;
    }

    result.symbols    = result.sema->Symbols();
    result.signatures = result.sema->KnownSignatures();
    result.link_entries = result.sema->Links();

    if (V) {
        std::cerr << "[stage/semantic] " << result.symbols.size()
                  << " symbols, " << result.link_entries.size()
                  << " link entries\n";
    }

    // Build aux symbol dump
    std::ostringstream oss;
    oss << "Symbol Table (" << result.symbols.size() << " entries)\n";
    for (const auto &[name, sym] : result.symbols) {
        oss << "  " << name << " : kind=" << static_cast<int>(sym.kind) << "\n";
    }
    result.symbols_dump = oss.str();

    result.success = true;
    return result;
}

}  // namespace polyglot::tools
