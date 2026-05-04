/**
 * @file     ploy_parser.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <optional>

#include "frontends/common/include/parser_base.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"

namespace polyglot::ploy {

/** @brief PloyParser class. */
class PloyParser : public frontends::ParserBase {
public:
  PloyParser(PloyLexer &lexer, frontends::Diagnostics &diagnostics) :
      ParserBase(diagnostics), lexer_(lexer) {}

  void ParseModule() override;
  std::shared_ptr<Module> TakeModule();

private:
  // Token management
  frontends::Token Consume();
  void Advance();
  // One-token lookahead.  Returned reference remains valid until the next
  // Advance() or Peek() call.
  const frontends::Token &Peek();
  bool IsSymbol(const std::string &symbol) const;
  bool MatchSymbol(const std::string &symbol);
  bool MatchKeyword(const std::string &keyword);
  void ExpectSymbol(const std::string &symbol, const std::string &message);
  void ExpectKeyword(const std::string &keyword, const std::string &message);
  void Sync();

  // Top-level declarations
  void ParseTopLevel();
  std::shared_ptr<Statement> ParseLinkDecl();
  std::shared_ptr<Statement> ParseSignedLinkDecl();
  std::shared_ptr<Statement> ParseImportDecl();
  std::shared_ptr<Statement> ParseExportDecl();
  std::shared_ptr<Statement> ParseMapTypeDecl();
  std::shared_ptr<Statement> ParsePipelineDecl();
  std::shared_ptr<Statement> ParseStageDecl();
  std::shared_ptr<Statement> ParseFuncDecl();
  // Parses `ASYNC FUNC … { … }` and forwards body parsing to ParseFuncDecl
  // after marking the resulting node `is_async = true` (since v1.14.0).
  std::shared_ptr<Statement> ParseAsyncFuncDecl();
  std::shared_ptr<Statement> ParseStructDecl();
  // Generic parameter list `<T: Bound1+Bound2, U>` (since v1.15.0).  The
  // caller has already verified the leading `<` is present.  WHERE clauses
  // are merged into the matching parameter's bounds vector by
  // `ParseWhereClause`.
  std::vector<FuncDecl::TypeParam> ParseTypeParams();
  void ParseWhereClause(std::vector<FuncDecl::TypeParam> &type_params);
  // Visibility / attribute prefix on a top-level declaration (since
  // v1.16.0).  Consumes any leading `@name(args)` annotations and at
  // most one `PUB` / `PRIVATE` keyword.  `@LANG(...)` is *not* matched
  // here; the caller routes that case to `ParseLangAnnotation` first.
  void ParseAttributesAndVisibility(std::vector<Attribute> &out_attrs,
                                    Visibility &out_vis,
                                    bool &has_explicit_vis);
  std::shared_ptr<Statement> ParseMapFuncDecl();
  std::shared_ptr<Statement> ParseConfigDecl();
  std::shared_ptr<Statement> ParseExtendDecl();
  // Foreign-class signature block (demand 2026-04-28-9).
  std::shared_ptr<Statement> ParseClassDecl();
  // Type alias and compile-time constant (demand 2026-04-28-7).
  std::shared_ptr<Statement> ParseTypeAliasDecl();
  std::shared_ptr<Statement> ParseConstDecl();
  // Language version pinning.
  std::shared_ptr<Statement> ParseLangPragma();              // top-level: LANG cpp = c++23;
  std::shared_ptr<Statement> ParseWithLangBlock();           // WITH LANG (cpp=c++23, ...) { ... }
  std::shared_ptr<Statement> ParseLangAnnotation();          // @LANG(cpp=c++23, ...) <stmt>
  // Helper that fills a (lang, version) pin-list from the current `(lang=ver, ...)` form.
  void ParseLangPinList(std::vector<WithLangBlock::Pin> &out_pins);

  // Statements
  std::shared_ptr<Statement> ParseStatement();
  bool in_pipeline_context_{false};
  std::shared_ptr<Statement> ParseVarDecl(bool is_mutable);
  std::shared_ptr<Statement> ParseIfStatement();
  // IF LET Some(x) = expr { … } ELSE { … }   (since v1.18.0)
  // Called from ParseIfStatement after the IF keyword has been consumed
  // and the `LET` look-ahead is confirmed.  `if_loc` is the source
  // location of the IF token to attach to the resulting node.
  std::shared_ptr<Statement> ParseIfLetStatement(core::SourceLoc if_loc);
  std::shared_ptr<Statement> ParseWhileStatement();
  std::shared_ptr<Statement> ParseForStatement();
  std::shared_ptr<Statement> ParseMatchStatement();
  // MATCH/CASE pattern grammar (demand 2026-04-28-10).
  std::shared_ptr<Pattern> ParsePattern();
  std::shared_ptr<Pattern> ParsePatternPrimary();
  std::shared_ptr<Statement> ParseReturnStatement();
  std::shared_ptr<Statement> ParsePrintlnStatement();
  std::shared_ptr<Statement> ParseBlock();
  std::vector<std::shared_ptr<Statement>> ParseBlockBody();
  // Structured exception handling (since v1.13.0).
  std::shared_ptr<Statement> ParseTryStatement();
  std::shared_ptr<Statement> ParseThrowStatement();

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
  // Build a string-literal expression from a raw lexer token (since v1.17.0).
  // The lexer hands the parser a single `kString` token whose lexeme is one
  // of `"..."` (regular / raw / multiline — all canonicalised by the lexer)
  // or `f"..."` / `f"""..."""` (template).  This helper returns either a
  // plain `Literal` or a `TemplateString` AST node accordingly.
  std::shared_ptr<Expression> BuildStringExpression(const std::string &lexeme,
                                                    core::SourceLoc loc);
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
  // One-token lookahead buffer.  When non-empty, Advance() pulls from this
  // buffer instead of the lexer.  Used by Peek() to inspect the next token
  // without permanently consuming it (e.g. to disambiguate named-argument
  // syntax `name: value` from a parenthesised expression starting with an
  // identifier).
  std::optional<frontends::Token> peeked_{};
};

} // namespace polyglot::ploy
