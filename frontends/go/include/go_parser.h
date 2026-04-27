/**
 * @file     go_parser.h
 * @brief    Go language parser
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
#include "frontends/common/include/parser_base.h"
#include "frontends/go/include/go_ast.h"
#include "frontends/go/include/go_lexer.h"

namespace polyglot::go {

class GoParser : public frontends::ParserBase {
public:
  GoParser(GoLexer &lex, frontends::Diagnostics &d) : frontends::ParserBase(d), lexer_(lex) {
    Advance();
  }

  void ParseModule() override { ParseFile(); }
  void ParseFile();
  std::unique_ptr<File> TakeFile() { return std::move(file_); }

  // Active Go release for syntax gating. Today the parser plumbs the value
  // through for downstream lowering / linker consumption only — once the
  // parser learns about generics or `for range over int` the relevant
  // gates will live here. `kAuto` behaves as `kGoVersionDefault`.
  void SetGoVersion(frontends::GoVersion v) { go_version_ = v; }

private:
  void Advance();
  bool IsKeyword(const std::string &k) const;
  bool IsSymbol(const std::string &s) const;
  bool MatchKeyword(const std::string &k);
  bool MatchSymbol(const std::string &s);
  bool ExpectKeyword(const std::string &k, const std::string &msg);
  bool ExpectSymbol(const std::string &s, const std::string &msg);

  void ParsePackage();
  void ParseImportDecl(GenDecl &dst);
  GenDecl ParseGenDecl(const std::string &keyword);
  ValueSpec ParseValueSpec();
  TypeSpec ParseTypeSpec();
  std::shared_ptr<FuncDecl> ParseFuncDecl();
  void ParseSignature(std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> &params,
                      std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> &results,
                      bool &is_variadic);
  std::shared_ptr<TypeNode> ParseType();
  std::shared_ptr<TypeNode> ParseStructType();
  std::shared_ptr<TypeNode> ParseInterfaceType();
  std::shared_ptr<TypeNode> ParseFuncType();
  std::vector<TypeNode::Field> ParseStructFields();
  std::vector<TypeNode::Field> ParseInterfaceMethods();
  std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> ParseParamGroup(bool *is_variadic);

  std::shared_ptr<Block> ParseBlock();
  std::shared_ptr<Statement> ParseStatement();
  std::shared_ptr<Statement> ParseSimpleStmt(bool allow_range);
  std::shared_ptr<Statement> ParseIfStmt();
  std::shared_ptr<Statement> ParseForStmt();
  std::shared_ptr<Statement> ParseSwitchStmt();
  std::shared_ptr<Statement> ParseSelectStmt();
  std::shared_ptr<Statement> ParseReturnStmt();
  std::shared_ptr<Statement> ParseBranchStmt(const std::string &kw);
  std::shared_ptr<Statement> ParseGoStmt();
  std::shared_ptr<Statement> ParseDeferStmt();

  std::shared_ptr<Expression> ParseExpression();
  std::shared_ptr<Expression> ParseBinaryExpr(int min_prec);
  std::shared_ptr<Expression> ParseUnaryExpr();
  std::shared_ptr<Expression> ParsePrimaryExpr();
  std::shared_ptr<Expression> ParseOperand();
  std::shared_ptr<Expression> ParseCompositeLit(std::shared_ptr<TypeNode> type);
  std::vector<std::shared_ptr<Expression>> ParseExpressionList();
  int BinaryPrecedence(const std::string &op);

  void SkipSemis();

  GoLexer &lexer_;
  frontends::Token current_{};
  std::string pending_doc_;
  std::unique_ptr<File> file_;
  // When true, suppress treating `Ident{...}` as a composite literal in
  // primary-expression suffixes. Used while parsing for/if/switch headers
  // where `{` introduces the body block (Go's standard ambiguity rule).
  bool no_composite_lit_ = false;
  // Active Go release for syntax gating. `kAuto` ⇒ `kGoVersionDefault`.
  frontends::GoVersion go_version_{frontends::GoVersion::kAuto};
};

} // namespace polyglot::go
