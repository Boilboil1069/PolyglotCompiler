/**
 * @file     cpp_parser.h
 * @brief    C++ language frontend
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/cpp/include/cpp_ast.h"
#include "frontends/cpp/include/cpp_lexer.h"

namespace polyglot::cpp {

/** @brief CppParser class. */
class CppParser : public frontends::ParserBase {
  public:
    CppParser(CppLexer &lexer, frontends::Diagnostics &diagnostics)
        : ParserBase(diagnostics), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    frontends::Token Consume();
    void Advance();
    bool IsSymbol(const std::string &symbol) const;
    bool MatchSymbol(const std::string &symbol);
    bool MatchKeyword(const std::string &keyword);
    void ExpectSymbol(const std::string &symbol, const std::string &message);
    void Sync();
    void ParseTopLevel();
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<Statement> ParseBlock();
    std::shared_ptr<Statement> ParseBlockOrStatement();
    std::shared_ptr<Statement> ParseIf();
    std::shared_ptr<Statement> ParseWhile();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseSwitch();
    std::shared_ptr<Statement> ParseTry();
    std::shared_ptr<Statement> ParseThrow();
    std::shared_ptr<Statement> ParseImport();
    std::shared_ptr<Statement> ParseModuleDecl(bool is_export);
    std::shared_ptr<Statement> ParseConcept();
    std::shared_ptr<Statement> ParseUsing();
    std::shared_ptr<Statement> ParseUsingNamespace();
    std::shared_ptr<Statement> ParseUsingEnum();
    std::shared_ptr<Statement> ParseNamespaceAlias();
    std::shared_ptr<Statement> ParseTypedef();
    std::shared_ptr<Statement> ParseUsingAlias();
    std::shared_ptr<Statement> ParseFriend();
    std::shared_ptr<Statement> ParseFunction();
    std::shared_ptr<FunctionDecl>
    ParseFunctionWithSignature(std::shared_ptr<TypeNode> ret_type, const std::string &name,
                               bool is_constexpr, bool is_consteval, bool is_inline, bool is_static,
                               bool is_operator = false, const std::string &op_symbol = "",
                               const std::string &access = "", bool inside_record = false);
    std::shared_ptr<Statement> ParseVarDecl(std::shared_ptr<TypeNode> type, const std::string &name,
                                            bool is_constexpr, bool is_inline, bool is_static,
                                            bool is_constinit, const std::string &access = "");
    std::shared_ptr<Statement> ParseStructuredBinding(std::shared_ptr<TypeNode> type,
                                                      bool is_constexpr, bool is_inline,
                                                      bool is_static);
    std::shared_ptr<Statement> ParseRecord(const std::string &kind);
    std::shared_ptr<Statement> ParseEnum();
    std::shared_ptr<Statement> ParseNamespace();
    std::shared_ptr<Statement> ParseTemplate();
    std::shared_ptr<Statement> ParseReturn();
    std::string ParseRequiresClause();
    std::vector<Attribute> ParseAttributes();
    std::string ParseQualifiedName();
    std::shared_ptr<TypeNode> ParseType();
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseComma();
    std::shared_ptr<Expression> ParseConditional();
    std::shared_ptr<Expression> ParseAssignment();
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
    std::shared_ptr<Expression> ParseLambda();
    std::vector<std::string> ParseTemplateParams();
    std::vector<std::shared_ptr<Expression>> ParseTemplateArgs();

    CppLexer &lexer_;
    std::shared_ptr<Module> module_{std::make_shared<Module>()};
    frontends::Token current_{};
    std::vector<frontends::Token> pushback_{};
};

} // namespace polyglot::cpp
