/**
 * @file     javascript_parser.h
 * @brief    JavaScript (ES2020+) parser
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/javascript/include/javascript_ast.h"
#include "frontends/javascript/include/javascript_lexer.h"

namespace polyglot::javascript {

/** @brief JavaScript parser (ES2020+). */
class JsParser : public frontends::ParserBase {
  public:
    JsParser(JsLexer &lexer, frontends::Diagnostics &diagnostics)
        : ParserBase(diagnostics), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    /** @name Token stream */
    /** @{ */
    void Advance();
    bool IsKeyword(const std::string &kw) const;
    bool IsSymbol(const std::string &sym) const;
    bool MatchKeyword(const std::string &kw);
    bool MatchSymbol(const std::string &sym);
    bool ExpectSymbol(const std::string &sym, const std::string &msg);
    bool ExpectKeyword(const std::string &kw, const std::string &msg);
    void ConsumeSemicolon();
    /** @} */

    /** @name Top-level */
    /** @{ */
    std::shared_ptr<Statement> ParseTopLevel();
    std::shared_ptr<Statement> ParseImportDecl();
    std::shared_ptr<Statement> ParseExportDecl();
    /** @} */

    /** @name Statements */
    /** @{ */
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<BlockStatement> ParseBlock();
    std::shared_ptr<Statement> ParseVariableDecl();
    std::shared_ptr<Statement> ParseIf();
    std::shared_ptr<Statement> ParseWhile();
    std::shared_ptr<Statement> ParseDoWhile();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseSwitch();
    std::shared_ptr<Statement> ParseTry();
    std::shared_ptr<Statement> ParseReturn();
    std::shared_ptr<Statement> ParseThrow();
    std::shared_ptr<Statement> ParseFunctionDecl(bool exported, bool exported_default);
    std::shared_ptr<Statement> ParseClassDecl(bool exported, bool exported_default);
    std::shared_ptr<Statement> ParseExprStatement();
    /** @} */

    /** @name Expressions */
    /** @{ */
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseAssignment();
    std::shared_ptr<Expression> ParseConditional();
    std::shared_ptr<Expression> ParseBinary(int min_precedence);
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParseUpdate();
    std::shared_ptr<Expression> ParseLeftHandSide();
    std::shared_ptr<Expression> ParseCallTail(std::shared_ptr<Expression> callee, bool is_new);
    std::shared_ptr<Expression> ParsePrimary();
    std::shared_ptr<Expression> ParseArrayLiteral();
    std::shared_ptr<Expression> ParseObjectLiteral();
    std::shared_ptr<Expression> ParseArrowOrParenExpr();
    std::shared_ptr<Expression> ParseTemplateLiteral();
    /** @} */

    /** @name Helpers */
    /** @{ */
    std::vector<ArrowFunction::Param> ParseFunctionParams();
    std::shared_ptr<MethodDecl> ParseMethodDecl(bool is_static);
    std::shared_ptr<TypeNode> ParseJsdocType(const std::string &raw);
    int GetBinaryPrecedence(const std::string &op) const;
    /** @} */

    JsLexer &lexer_;
    frontends::Token current_{};
    std::shared_ptr<Module> module_;
    std::string pending_doc_;
};

}  // namespace polyglot::javascript
