/**
 * @file     cpp_frontend.h
 * @brief    C++ language frontend adapter
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::cpp {

/** @brief CppLanguageFrontend class. */
class CppLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "cpp"; }
    std::string DisplayName() const override { return "C++"; }

    std::vector<std::string> Extensions() const override {
        return {".cpp", ".cc", ".cxx", ".c", ".hpp", ".h"};
    }

    std::vector<std::string> Aliases() const override {
        return {"c", "c++"};
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

    bool NeedsPreprocessing() const override { return true; }

    std::vector<frontends::ForeignFunctionSignature> ExtractSignatures(
        const std::string &source,
        const std::string &filename,
        const std::string &module_name) const override;
};

}  // namespace polyglot::cpp
