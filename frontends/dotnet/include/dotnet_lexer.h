#pragma once

#include "frontends/common/include/lexer_base.h"

#include <string>

namespace polyglot::dotnet {

class DotnetLexer : public frontends::LexerBase {
  public:
    DotnetLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}
    frontends::Token NextToken() override;

  private:
    frontends::Token ReadNumber();
    frontends::Token ReadString();
    frontends::Token ReadVerbatimString();
    frontends::Token ReadInterpolatedString();
    frontends::Token ReadChar();
    frontends::Token ReadIdentifierOrKeyword();
};

} // namespace polyglot::dotnet
