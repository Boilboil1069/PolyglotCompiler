/**
 * @file     stage_frontend.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// stage_frontend.cpp — Stage 1 implementation
// ============================================================================

#include "tools/polyc/src/stage_frontend.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/language_frontend.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::tools {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono;

/// Apply include paths to a Preprocessor instance.
void ApplyIncludePaths(frontends::Preprocessor &pp,
                       const std::vector<std::string> &paths) {
    for (const auto &p : paths) {
        pp.AddIncludePath(p);
    }
}

/// Query an environment variable: return true if it exists and is non-empty.
bool HasEnvVar(const char *name) {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t len = 0;
    errno_t rc = _dupenv_s(&value, &len, name);
    if (rc != 0 || value == nullptr) {
        if (value) std::free(value);
        return false;
    }
    const bool present = (len > 1 && value[0] != '\0');
    std::free(value);
    return present;
#else
    return std::getenv(name) != nullptr;
#endif
}

}  // namespace

// ============================================================================
// RunFrontendStage
// ============================================================================

FrontendResult RunFrontendStage(const DriverSettings &settings) {
    FrontendResult result;
    result.language = settings.language;
    result.source_label =
        settings.source_path.empty() ? "<cli>" : settings.source_path;
    result.processed_source = settings.source;
    result.pkg_cache = std::make_shared<ploy::PackageDiscoveryCache>();

    const bool V = settings.verbose;

    // ── Stage 1a: Preprocessing ─────────────────────────────────────────────
    if (settings.language != "ploy") {
        auto *fe = frontends::FrontendRegistry::Instance()
                       .GetFrontend(settings.language);
        if (fe && fe->NeedsPreprocessing()) {
            bool pp_enabled = true;
            auto it = settings.pp_overrides.find(settings.language);
            if (it != settings.pp_overrides.end()) pp_enabled = it->second;
            if (settings.language == "rust" && HasEnvVar("POLYC_PREPROCESS_RUST"))
                pp_enabled = true;

            if (pp_enabled) {
                frontends::Preprocessor pp(result.diagnostics);
                ApplyIncludePaths(pp, settings.include_paths);
                result.processed_source = pp.Process(
                    settings.source,
                    result.source_label);
                if (V) std::cerr << "[stage/frontend] preprocessed\n";
            }
        }
    }
    // .ploy never uses the preprocessor

    // ── Stage 1b: Non-.ploy → FrontendRegistry → IR directly ───────────────
    if (settings.language != "ploy") {
        auto *fe = frontends::FrontendRegistry::Instance()
                       .GetFrontend(settings.language);
        if (!fe) {
            result.diagnostics.Report(
                core::SourceLoc{result.source_label, 1, 1},
                "Unknown language: " + settings.language);
            result.success = false;
            return result;
        }

        result.ir_ctx = std::make_shared<ir::IRContext>();
        frontends::FrontendOptions fe_opts;
        fe_opts.verbose = V;
        fe_opts.strict = settings.strict;
        fe_opts.force = settings.force;
        fe_opts.include_paths = settings.include_paths;

        auto fe_result = fe->Lower(result.processed_source, result.source_label,
                                   *result.ir_ctx, result.diagnostics, fe_opts);
        result.success = fe_result.lowered;
        return result;
    }

    // ── Stage 1c: .ploy — Lexing ─────────────────────────────────────────────
    {
        ploy::PloyLexer token_lexer(result.processed_source, result.source_label);
        std::ostringstream toss;
        while (true) {
            auto tok = token_lexer.NextToken();
            if (tok.kind == frontends::TokenKind::kEndOfFile) break;
            toss << "L" << tok.loc.line << ":" << tok.loc.column
                 << "  [" << static_cast<int>(tok.kind) << "] "
                 << tok.lexeme << "\n";
            result.tokens.push_back(std::move(tok));
        }
        result.token_dump = toss.str();
        if (V) std::cerr << "[stage/frontend] lexing done ("
                         << result.tokens.size() << " tokens)\n";
    }

    // ── Stage 1d: .ploy — Parsing ────────────────────────────────────────────
    {
        ploy::PloyLexer parse_lexer(result.processed_source, result.source_label);
        ploy::PloyParser parser(parse_lexer, result.diagnostics);
        parser.ParseModule();
        result.ast = parser.TakeModule();

        if (!result.ast || result.diagnostics.HasErrors()) {
            result.success = false;
            if (V) std::cerr << "[stage/frontend] parse FAILED\n";
            return result;
        }
        if (V) std::cerr << "[stage/frontend] parsed ("
                         << result.ast->declarations.size() << " decls)\n";

        // AST summary for aux
        std::ostringstream ast_oss;
        ast_oss << "Module: " << result.ast->declarations.size() << " declarations\n";
        for (std::size_t i = 0; i < result.ast->declarations.size(); ++i) {
            auto &decl = result.ast->declarations[i];
            ast_oss << "  [" << i << "] loc=" << decl->loc.line
                    << ":" << decl->loc.column << "\n";
        }
        result.ast_dump = ast_oss.str();
    }

    // ── Stage 1e: .ploy — Package index ──────────────────────────────────────
    if (settings.package_index && result.ast) {
        std::unordered_set<std::string> seen;
        std::vector<std::string> languages;
        for (const auto &decl : result.ast->declarations) {
            auto imp = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
            if (imp && !imp->language.empty() && !imp->package_name.empty()) {
                if (seen.insert(imp->language).second)
                    languages.push_back(imp->language);
            }
        }
        if (!languages.empty()) {
            ploy::PackageIndexerOptions idx_opts;
            idx_opts.command_timeout =
                milliseconds{settings.package_index_timeout_ms};
            idx_opts.verbose = V;
            auto runner = std::make_shared<ploy::DefaultCommandRunner>(
                idx_opts.command_timeout);
            ploy::PackageIndexer indexer(result.pkg_cache, runner, idx_opts);
            if (V) {
                indexer.SetProgressCallback(
                    [](const std::string &lang, const std::string &msg) {
                        std::cerr << "  [pkg-index:" << lang << "] " << msg << "\n";
                    });
            }
            indexer.BuildIndex(languages);
            if (V) {
                auto stats = indexer.LastStats();
                std::cerr << "  [pkg-index] " << stats.packages_found
                          << " package(s) indexed\n";
            }
        }
    }

    result.success = true;
    return result;
}

}  // namespace polyglot::tools
