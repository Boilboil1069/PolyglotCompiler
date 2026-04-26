/**
 * @file     go_frontend.h
 * @brief    Go language frontend adapter
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::go {

class GoLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "go"; }
    std::string DisplayName() const override { return "Go"; }
    std::vector<std::string> Aliases() const override { return {"golang"}; }
    std::vector<std::string> Extensions() const override { return {".go"}; }

    std::vector<frontends::Token> Tokenize(const std::string &source,
                                           const std::string &filename) const override;
    bool Analyze(const std::string &source, const std::string &filename,
                 frontends::Diagnostics &d,
                 const frontends::FrontendOptions &opts) const override;
    frontends::FrontendResult Lower(const std::string &source,
                                    const std::string &filename,
                                    ir::IRContext &ir_ctx,
                                    frontends::Diagnostics &d,
                                    const frontends::FrontendOptions &opts) const override;
    std::vector<frontends::ForeignFunctionSignature> ExtractSignatures(
        const std::string &source, const std::string &filename,
        const std::string &module_name) const override;
};

}  // namespace polyglot::go
