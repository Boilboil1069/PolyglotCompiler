// dotnet_frontend.h — .NET/C# language frontend adapter.

#pragma once

#include "frontends/common/include/language_frontend.h"

namespace polyglot::dotnet {

class DotnetLanguageFrontend : public frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "dotnet"; }
    std::string DisplayName() const override { return ".NET/C#"; }

    std::vector<std::string> Extensions() const override {
        return {".cs", ".vb"};
    }

    std::vector<std::string> Aliases() const override {
        return {"csharp", "c#"};
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
};

}  // namespace polyglot::dotnet
