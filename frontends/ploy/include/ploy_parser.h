#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"

namespace polyglot::ploy {

class PloyParser : public frontends::ParserBase {
  public:
    PloyParser(PloyLexer &lexer, frontends::Diagnostics &diagnostics)
        : ParserBase(diagnostics), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    // Token management
    frontends::Token Consume();
    void Advance();
    bool IsSymbol(const std::string &symbol) const;
    bool MatchSymbol(const std::string &symbol);
    bool MatchKeyword(const std::string &keyword);
    void ExpectSymbol(const std::string &symbol, const std::string &message);
    void ExpectKeyword(const std::string &keyword, const std::string &message);
    void Sync();

    // Top-level declarations
    void ParseTopLevel();
    std::shared_ptr<Statement> ParseLinkDecl();
    std::shared_ptr<Statement> ParseImportDecl();
    std::shared_ptr<Statement> ParseExportDecl();
    std::shared_ptr<Statement> ParseMapTypeDecl();
    std::shared_ptr<Statement> ParsePipelineDecl();
    std::shared_ptr<Statement> ParseFuncDecl();
    std::shared_ptr<Statement> ParseStructDecl();
    std::shared_ptr<Statement> ParseMapFuncDecl();
    std::shared_ptr<Statement> ParseConfigDecl();
    std::shared_ptr<Statement> ParseExtendDecl();

    // Statements
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<Statement> ParseVarDecl(bool is_mutable);
    std::shared_ptr<Statement> ParseIfStatement();
    std::shared_ptr<Statement> ParseWhileStatement();
    std::shared_ptr<Statement> ParseForStatement();
    std::shared_ptr<Statement> ParseMatchStatement();
    std::shared_ptr<Statement> ParseReturnStatement();
    std::shared_ptr<Statement> ParseBlock();
    std::vector<std::shared_ptr<Statement>> ParseBlockBody();

    // Expressions
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseAssignment();
    std::shared_ptr<Expression> ParseLogicalOr();
    std::shared_ptr<Expression> ParseLogicalAnd();
    std::shared_ptr<Expression> ParseEquality();
    std::shared_ptr<Expression> ParseComparison();
    std::shared_ptr<Expression> ParseAdditive();
    std::shared_ptr<Expression> ParseMultiplicative();
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();
    std::shared_ptr<Expression> ParseCallDirective();
    std::shared_ptr<Expression> ParseNewExpression();
    std::shared_ptr<Expression> ParseMethodCallDirective();
    std::shared_ptr<Expression> ParseGetAttrExpression();
    std::shared_ptr<Expression> ParseSetAttrExpression();
    std::shared_ptr<Expression> ParseDeleteExpression();
    std::shared_ptr<Expression> ParseConvertExpression();
    std::shared_ptr<Expression> ParseListLiteral();
    std::shared_ptr<Expression> ParseDictLiteral();
    std::shared_ptr<Expression> ParseStructLiteral(const std::string &struct_name);
    std::vector<std::shared_ptr<Expression>> ParseArguments();

    // Statements
    std::shared_ptr<Statement> ParseWithStatement();

    // Types
    std::shared_ptr<TypeNode> ParseType();
    std::shared_ptr<TypeNode> ParseQualifiedOrSimpleType();

    // Function parameters
    std::vector<FuncDecl::Param> ParseParams();

    PloyLexer &lexer_;
    std::shared_ptr<Module> module_{std::make_shared<Module>()};
    frontends::Token current_{};
};

} // namespace polyglot::ploy
