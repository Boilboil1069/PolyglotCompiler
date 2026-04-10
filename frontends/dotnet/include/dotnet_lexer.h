/**
 * @file     dotnet_lexer.h
 * @brief    .NET/C# language frontend
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/lexer_base.h"

#include <string>

namespace polyglot::dotnet {

/** @brief DotnetLexer class. */
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
