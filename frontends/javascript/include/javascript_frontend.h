/**
 * @file     javascript_frontend.h
 * @brief    JavaScript language frontend adapter
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::javascript {

/** @brief JavaScript frontend (ES2020+). */
class JsLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "javascript"; }
    std::string DisplayName() const override { return "JavaScript"; }

    std::vector<std::string> Extensions() const override {
        return {".js", ".mjs", ".cjs"};
    }

    std::vector<std::string> Aliases() const override {
        return {"js", "node", "es", "ecmascript"};
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

    std::vector<frontends::ForeignFunctionSignature> ExtractSignatures(
        const std::string &source,
        const std::string &filename,
        const std::string &module_name) const override;
};

}  // namespace polyglot::javascript
