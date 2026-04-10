// java_frontend.h — Java language frontend adapter.

#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::java {

class JavaLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "java"; }
    std::string DisplayName() const override { return "Java"; }

    std::vector<std::string> Extensions() const override {
        return {".java"};
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

}  // namespace polyglot::java
