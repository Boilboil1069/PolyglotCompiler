#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lexer.h"

namespace polyglot::rust {

class RustParser : public frontends::ParserBase {
  public:
    RustParser(RustLexer &lexer, frontends::Diagnostics &diagnostics)
        : ParserBase(diagnostics), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    frontends::Token Consume();
    bool IsSymbol(const std::string &symbol) const;
    bool MatchSymbol(const std::string &symbol);
    bool MatchKeyword(const std::string &keyword);
    void ExpectSymbol(const std::string &symbol, const std::string &message);
    void Sync();
    void ParseItem();
    std::shared_ptr<Statement> ParseStatement(bool allow_trailing_expr = false);
    std::shared_ptr<Statement> ParseUse();
    std::shared_ptr<Statement> ParseLet();
    std::shared_ptr<Statement> ParseFunction();
    std::shared_ptr<Statement> ParseReturn();
    std::shared_ptr<Statement> ParseBreak();
    std::shared_ptr<Statement> ParseContinue();
    std::shared_ptr<Statement> ParseLoop();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseStruct();
    std::shared_ptr<Statement> ParseEnum();
    std::shared_ptr<Statement> ParseImpl();
    std::shared_ptr<Statement> ParseTrait();
    std::shared_ptr<Statement> ParseMod();
    std::shared_ptr<Expression> ParseIfExpression();
    std::shared_ptr<Expression> ParseWhileExpression();
    std::shared_ptr<Expression> ParseMatchExpression();
    std::shared_ptr<BlockExpression> ParseBlockExpression();
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseAssignment();
    std::shared_ptr<Expression> ParseRange();
    std::shared_ptr<Expression> ParseLogicalOr();
    std::shared_ptr<Expression> ParseLogicalAnd();
    std::shared_ptr<Expression> ParseBitwiseOr();
    std::shared_ptr<Expression> ParseBitwiseXor();
    std::shared_ptr<Expression> ParseBitwiseAnd();
    std::shared_ptr<Expression> ParseEquality();
    std::shared_ptr<Expression> ParseRelational();
    std::shared_ptr<Expression> ParseShift();
    std::shared_ptr<Expression> ParseAdditive();
    std::shared_ptr<Expression> ParseMultiplicative();
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();
    std::shared_ptr<Expression> ParseClosure();
    std::shared_ptr<Expression> ParsePathExpression();
    std::string ParseDelimitedBody(const std::string &open, const std::string &close);
    std::shared_ptr<Pattern> ParsePattern();
    std::shared_ptr<Pattern> ParseStructPattern(PathPattern path);
    std::shared_ptr<LifetimeType> ParseLifetime();
    std::shared_ptr<TypeNode> ParseType();
    std::shared_ptr<TypePath> ParseTypePath();
    std::vector<std::string> ParseTypeParams();
    std::vector<std::shared_ptr<TypeNode>> ParseGenericArgList();
    int GetBinaryPrecedence(const std::string &op) const;

    RustLexer &lexer_;
    std::shared_ptr<Module> module_{std::make_shared<Module>()};
    frontends::Token current_{};
};

} // namespace polyglot::rust
