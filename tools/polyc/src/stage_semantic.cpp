// ============================================================================
// stage_semantic.cpp — Stage 2 implementation
// ============================================================================

#include "tools/polyc/src/stage_semantic.h"

#include <iostream>
#include <sstream>

#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::tools {

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
