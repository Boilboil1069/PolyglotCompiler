/**
 * @file     rust_frontend.h
 * @brief    Rust language frontend adapter
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::rust {

/** @brief RustLanguageFrontend class. */
class RustLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "rust"; }
    std::string DisplayName() const override { return "Rust"; }

    std::vector<std::string> Extensions() const override {
        return {".rs"};
    }

    std::vector<std::string> Aliases() const override {
        return {"rs"};
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

}  // namespace polyglot::rust
