// ploy_frontend.cpp — Ploy language frontend adapter implementation.

#include "frontends/ploy/include/ploy_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"

namespace polyglot::ploy {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<PloyLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> PloyLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    PloyLexer lexer(source, filename);
    std::vector<frontends::Token> tokens;
    while (true) {
        auto tok = lexer.NextToken();
        if (tok.kind == frontends::TokenKind::kEndOfFile) break;
        tokens.push_back(tok);
    }
    return tokens;
}

// ============================================================================
// Analyze
// ============================================================================

bool PloyLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions &options) const {

    PloyLexer lexer(source, filename);
    PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    if (diagnostics.HasErrors()) return false;

    auto module = parser.TakeModule();
    if (!module) return false;

    PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    sema_opts.strict_mode = options.strict;
    PloySema sema(diagnostics, sema_opts);
    return sema.Analyze(module);
}

// ============================================================================
// Lower (base interface — delegates to LowerWithDescriptors)
// ============================================================================

frontends::FrontendResult PloyLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions &options) const {

    PloyFrontendResult extended = LowerWithDescriptors(source, filename, ir_ctx,
                                                       diagnostics, options);
    // Slice down to base result
    frontends::FrontendResult base;
    base.success = extended.success;
    base.lowered = extended.lowered;
    return base;
}

// ============================================================================
// LowerWithDescriptors — full pipeline with cross-language metadata
// ============================================================================

PloyFrontendResult PloyLanguageFrontend::LowerWithDescriptors(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions &options) const {

    PloyFrontendResult result;

    // Phase 1: Lex + Parse
    PloyLexer lexer(source, filename);
    PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (!module || diagnostics.HasErrors()) return result;

    // Phase 2: Semantic analysis
    PloySemaOptions sema_opts;
    sema_opts.strict_mode = options.strict;
    // In UI context discovery is off; in CLI it follows the default.
    // The caller controls this via options.
    sema_opts.enable_package_discovery = false;
    PloySema sema(diagnostics, sema_opts);
    if (!sema.Analyze(module) && !options.force) return result;

    // Snapshot sema outputs
    result.link_entries.assign(sema.Links().begin(), sema.Links().end());
    result.symbols = sema.Symbols();

    // Phase 3: IR Lowering
    PloyLowering lowering(ir_ctx, diagnostics, sema);
    bool lower_ok = lowering.Lower(module);
    result.lowered = lower_ok || options.force;
    result.success = lower_ok;

    // Snapshot lowering outputs
    result.call_descriptors = lowering.CallDescriptors();

    return result;
}

}  // namespace polyglot::ploy
