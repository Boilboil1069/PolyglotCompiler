// python_frontend.h — Python language frontend adapter.

#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::python {

class PythonLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "python"; }
    std::string DisplayName() const override { return "Python"; }

    std::vector<std::string> Extensions() const override {
        return {".py"};
    }

    std::vector<std::string> Aliases() const override {
        return {"py"};
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

}  // namespace polyglot::python
