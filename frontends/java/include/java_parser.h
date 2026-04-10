/**
 * @file     java_parser.h
 * @brief    Java language frontend
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/java/include/java_ast.h"
#include "frontends/java/include/java_lexer.h"

namespace polyglot::java {

/** @brief JavaParser class. */
class JavaParser : public frontends::ParserBase {
  public:
    JavaParser(JavaLexer &lexer, frontends::Diagnostics &diagnostics)
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

    // Top-level parsing
    void ParseTopLevel();
    std::shared_ptr<PackageDecl> ParsePackageDecl();
    std::shared_ptr<ImportDecl> ParseImportDecl();
    std::vector<Annotation> ParseAnnotations();
    std::string ParseAccessModifier();

    // Type declarations
    std::shared_ptr<ClassDecl> ParseClassDecl(const std::string &access,
                                               const std::vector<Annotation> &annotations);
    std::shared_ptr<InterfaceDecl> ParseInterfaceDecl(const std::string &access,
                                                       const std::vector<Annotation> &annotations);
    std::shared_ptr<EnumDecl> ParseEnumDecl(const std::string &access,
                                             const std::vector<Annotation> &annotations);
    std::shared_ptr<RecordDecl> ParseRecordDecl(const std::string &access,
                                                 const std::vector<Annotation> &annotations);

    // Members
    std::shared_ptr<MethodDecl> ParseMethodDecl(const std::string &access,
                                                 const std::vector<Annotation> &annotations);
    std::shared_ptr<ConstructorDecl> ParseConstructorDecl(const std::string &access,
                                                           const std::string &class_name);
    std::shared_ptr<FieldDecl> ParseFieldDecl(const std::string &access,
                                               const std::vector<Annotation> &annotations);
    std::vector<TypeParameter> ParseTypeParameters();
    std::vector<Parameter> ParseParameters();

    // Types
    std::shared_ptr<TypeNode> ParseType();
    std::shared_ptr<TypeNode> ParseTypeArgument();

    // Statements
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<BlockStatement> ParseBlock();
    std::shared_ptr<Statement> ParseVarDecl();
    std::shared_ptr<Statement> ParseIf();
    std::shared_ptr<Statement> ParseWhile();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseSwitch();
    std::shared_ptr<Statement> ParseTry();
    std::shared_ptr<Statement> ParseReturn();
    std::shared_ptr<Statement> ParseThrow();

    // Expressions
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseTernary();
    std::shared_ptr<Expression> ParseBinary(int min_precedence);
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();
    std::shared_ptr<Expression> ParseLambda();
    int GetPrecedence(const std::string &op) const;

    // Helpers
    std::string ParseQualifiedName();

    JavaLexer &lexer_;
    frontends::Token current_{};
    std::shared_ptr<Module> module_;
};

} // namespace polyglot::java
