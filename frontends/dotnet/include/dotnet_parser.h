#pragma once

#include "frontends/common/include/parser_base.h"
#include "frontends/dotnet/include/dotnet_ast.h"
#include "frontends/dotnet/include/dotnet_lexer.h"

namespace polyglot::dotnet {

class DotnetParser : public frontends::ParserBase {
  public:
    DotnetParser(DotnetLexer &lexer, frontends::Diagnostics &diags)
        : ParserBase(diags), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    DotnetLexer &lexer_;
    frontends::Token current_{};
    std::shared_ptr<Module> module_;

    void Advance();
    frontends::Token Consume();
    bool IsSymbol(const std::string &symbol) const;
    bool MatchSymbol(const std::string &symbol);
    bool MatchKeyword(const std::string &keyword);
    void ExpectSymbol(const std::string &symbol, const std::string &msg);
    void Sync();
    std::string ParseQualifiedName();

    // Declarations
    void ParseTopLevel();
    std::shared_ptr<NamespaceDecl> ParseNamespaceDecl();
    std::shared_ptr<UsingDirective> ParseUsingDirective();
    std::vector<Attribute> ParseAttributes();
    std::string ParseAccessModifier();

    std::shared_ptr<ClassDecl> ParseClassDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<StructDecl> ParseStructDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<InterfaceDecl> ParseInterfaceDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<EnumDecl> ParseEnumDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<DelegateDecl> ParseDelegateDecl(const std::string &access, const std::vector<Attribute> &attrs);

    // Members
    std::shared_ptr<MethodDecl> ParseMethodDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<FieldDecl> ParseFieldDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<PropertyDecl> ParsePropertyDecl(const std::string &access, const std::vector<Attribute> &attrs);
    std::shared_ptr<ConstructorDecl> ParseConstructorDecl(const std::string &access, const std::string &class_name);
    std::shared_ptr<DestructorDecl> ParseDestructorDecl(const std::string &class_name);

    // Types and type params
    std::shared_ptr<TypeNode> ParseType();
    std::shared_ptr<TypeNode> ParseTypeArgument();
    std::vector<TypeParameter> ParseTypeParameters();
    std::vector<Parameter> ParseParameters();

    // Statements
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<BlockStatement> ParseBlock();
    std::shared_ptr<Statement> ParseVarDecl();
    std::shared_ptr<Statement> ParseIf();
    std::shared_ptr<Statement> ParseWhile();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseForEach();
    std::shared_ptr<Statement> ParseSwitch();
    std::shared_ptr<Statement> ParseTry();
    std::shared_ptr<Statement> ParseReturn();
    std::shared_ptr<Statement> ParseThrow();
    std::shared_ptr<Statement> ParseUsing();
    std::shared_ptr<Statement> ParseLock();

    // Expressions
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseTernary();
    std::shared_ptr<Expression> ParseNullCoalescing();
    std::shared_ptr<Expression> ParseBinary(int min_prec);
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();
    int GetPrecedence(const std::string &op) const;
};

} // namespace polyglot::dotnet
