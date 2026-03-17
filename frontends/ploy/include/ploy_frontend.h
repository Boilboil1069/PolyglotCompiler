// ploy_frontend.h — Ploy language frontend adapter.
//
// The ploy frontend has a richer pipeline than generic language frontends
// because it includes cross-language link resolution.  PloyLanguageFrontend
// exposes extra accessors for the link descriptors produced during lowering
// so that the driver can feed them to the PolyglotLinker.

#pragma once

#include <vector>

#include "frontends/common/include/language_frontend.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::ploy {

// ============================================================================
// PloyFrontendResult — extended result carrying cross-language metadata
// ============================================================================

struct PloyFrontendResult : public frontends::FrontendResult {
    // Cross-language call descriptors emitted during lowering
    std::vector<CrossLangCallDescriptor> call_descriptors;

    // LINK entries registered during semantic analysis
    std::vector<LinkEntry> link_entries;

    // Symbol table snapshot from semantic analysis
    std::unordered_map<std::string, PloySymbol> symbols;
};

// ============================================================================
// PloyLanguageFrontend
// ============================================================================

class PloyLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "ploy"; }
    std::string DisplayName() const override { return "Ploy"; }

    std::vector<std::string> Extensions() const override {
        return {".ploy", ".poly"};
    }

    std::vector<frontends::Token> Tokenize(const std::string &source,
                                           const std::string &filename) const override;

    bool Analyze(const std::string &source,
                 const std::string &filename,
                 frontends::Diagnostics &diagnostics,
                 const frontends::FrontendOptions &options) const override;

    frontends::FrontendResult Lower(const std::string &source,
                                    const std::string &filename,
                                    ir::IRContext &ir_ctx,
                                    frontends::Diagnostics &diagnostics,
                                    const frontends::FrontendOptions &options) const override;

    // Extended lowering that returns cross-language metadata in addition
    // to the base FrontendResult fields.  The driver should prefer this
    // method when it needs to feed descriptors into the PolyglotLinker.
    PloyFrontendResult LowerWithDescriptors(
        const std::string &source,
        const std::string &filename,
        ir::IRContext &ir_ctx,
        frontends::Diagnostics &diagnostics,
        const frontends::FrontendOptions &options) const;
};

}  // namespace polyglot::ploy
