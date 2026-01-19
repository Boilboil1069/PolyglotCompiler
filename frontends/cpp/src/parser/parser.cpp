#include "frontends/cpp/include/cpp_parser.h"

namespace polyglot::cpp {

void CppParser::Advance() { current_ = lexer_.NextToken(); }

frontends::Token CppParser::Consume() {
  Advance();
  while (current_.kind == frontends::TokenKind::kComment) {
    Advance();
  }
  return current_;
}

bool CppParser::IsSymbol(const std::string &symbol) const {
  return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool CppParser::MatchSymbol(const std::string &symbol) {
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol) {
    Consume();
    return true;
  }
  return false;
}

bool CppParser::MatchKeyword(const std::string &keyword) {
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
    Consume();
    return true;
  }
  return false;
}

void CppParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
  if (!MatchSymbol(symbol)) {
    diagnostics_.Report(current_.loc, message);
  }
}

void CppParser::Sync() {
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (current_.kind == frontends::TokenKind::kKeyword) {
      if (current_.lexeme == "import" || current_.lexeme == "int" ||
          current_.lexeme == "void") {
        return;
      }
    }
    if (current_.kind == frontends::TokenKind::kSymbol &&
        (current_.lexeme == ";" || current_.lexeme == "}")) {
      Consume();
      return;
    }
    Consume();
  }
}

std::shared_ptr<Expression> CppParser::ParsePrimary() {
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = current_.lexeme;
    ident->loc = current_.loc;
    Consume();
    return ident;
  }
  if (current_.kind == frontends::TokenKind::kNumber ||
      current_.kind == frontends::TokenKind::kString) {
    auto lit = std::make_shared<Literal>();
    lit->value = current_.lexeme;
    lit->loc = current_.loc;
    Consume();
    return lit;
  }
  if (IsSymbol("(")) {
    Consume();
    auto expr = ParseExpression();
    MatchSymbol(")");
    return expr;
  }
  diagnostics_.Report(current_.loc, "Expected expression");
  return nullptr;
}

std::shared_ptr<Expression> CppParser::ParsePostfix() {
  auto expr = ParsePrimary();
  while (true) {
    if (IsSymbol("(")) {
      auto call = std::make_shared<CallExpression>();
      call->callee = expr;
      call->loc = expr ? expr->loc : current_.loc;
      Consume();
      if (!IsSymbol(")")) {
        call->args.push_back(ParseExpression());
        while (MatchSymbol(",")) {
          call->args.push_back(ParseExpression());
        }
      }
      MatchSymbol(")");
      expr = call;
      continue;
    }
    if (IsSymbol(".")) {
      Consume();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto mem = std::make_shared<MemberExpression>();
        mem->object = expr;
        mem->member = current_.lexeme;
        mem->loc = current_.loc;
        Consume();
        expr = mem;
        continue;
      }
    }
    if (IsSymbol("->")) {
      Consume();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto mem = std::make_shared<MemberExpression>();
        mem->object = expr;
        mem->member = current_.lexeme;
        mem->is_arrow = true;
        mem->loc = current_.loc;
        Consume();
        expr = mem;
        continue;
      }
    }
    if (IsSymbol("[")) {
      Consume();
      auto idx = std::make_shared<IndexExpression>();
      idx->object = expr;
      idx->loc = current_.loc;
      idx->index = ParseExpression();
      MatchSymbol("]");
      expr = idx;
      continue;
    }
    break;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseUnary() {
  if (IsSymbol("+") || IsSymbol("-") || IsSymbol("!") || IsSymbol("~") ||
      IsSymbol("&") || IsSymbol("*")) {
    auto unary = std::make_shared<UnaryExpression>();
    unary->op = current_.lexeme;
    unary->loc = current_.loc;
    Consume();
    unary->operand = ParseUnary();
    return unary;
  }
  return ParsePostfix();
}

std::shared_ptr<Expression> CppParser::ParseMultiplicative() {
  auto expr = ParseUnary();
  while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseUnary();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseAdditive() {
  auto expr = ParseMultiplicative();
  while (IsSymbol("+") || IsSymbol("-")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseMultiplicative();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseRelational() {
  auto expr = ParseAdditive();
  while (IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=") ) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseAdditive();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseEquality() {
  auto expr = ParseRelational();
  while (IsSymbol("==") || IsSymbol("!=")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseRelational();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseLogicalAnd() {
  auto expr = ParseEquality();
  while (IsSymbol("&&")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseEquality();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseLogicalOr() {
  auto expr = ParseLogicalAnd();
  while (IsSymbol("||")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = current_.lexeme;
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseLogicalAnd();
    expr = bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseAssignment() {
  auto expr = ParseLogicalOr();
  if (IsSymbol("=")) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = "=";
    bin->loc = expr ? expr->loc : current_.loc;
    Consume();
    bin->left = expr;
    bin->right = ParseAssignment();
    return bin;
  }
  return expr;
}

std::shared_ptr<Expression> CppParser::ParseExpression() { return ParseAssignment(); }

std::shared_ptr<Statement> CppParser::ParseImport() {
  auto stmt = std::make_shared<ImportDeclaration>();
  stmt->loc = current_.loc;
  MatchKeyword("import");
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    stmt->module = current_.lexeme;
    Consume();
  } else {
    diagnostics_.Report(current_.loc, "Expected module name");
  }
  MatchSymbol(";");
  return stmt;
}

std::shared_ptr<Statement> CppParser::ParseReturn() {
  auto stmt = std::make_shared<ReturnStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("return");
  stmt->value = ParseExpression();
  MatchSymbol(";");
  return stmt;
}

std::shared_ptr<Statement> CppParser::ParseVarDecl(const std::string &type_name,
                                                   const std::string &name) {
  auto decl = std::make_shared<VarDecl>();
  decl->loc = current_.loc;
  decl->type_name = type_name;
  decl->name = name;
  if (IsSymbol("=")) {
    Consume();
    decl->init = ParseExpression();
  }
  MatchSymbol(";");
  return decl;
}

std::shared_ptr<Statement> CppParser::ParseFunctionWithSignature(
    const std::string &ret_type, const std::string &name) {
  auto fn = std::make_shared<FunctionDecl>();
  fn->loc = current_.loc;
  fn->return_type = ret_type;
  fn->name = name;
  ExpectSymbol("(", "Expected '(' after function name");
  if (!IsSymbol(")")) {
    while (true) {
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        fn->params.push_back(current_.lexeme);
        Consume();
      }
      if (!MatchSymbol(",")) break;
    }
  }
  ExpectSymbol(")", "Expected ')' after parameters");
  if (!MatchSymbol("{")) {
    diagnostics_.Report(current_.loc, "Expected '{' to start function body");
    return fn;
  }
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    fn->body.push_back(ParseStatement());
  }
  MatchSymbol("}");
  return fn;
}

std::shared_ptr<Statement> CppParser::ParseFunction() {
  // assumes current_ is at return type token
  std::string ret = current_.lexeme;
  Consume();
  if (current_.kind != frontends::TokenKind::kIdentifier) {
    diagnostics_.Report(current_.loc, "Expected function name");
    return nullptr;
  }
  std::string name = current_.lexeme;
  Consume();
  return ParseFunctionWithSignature(ret, name);
}

std::shared_ptr<Statement> CppParser::ParseBlock() {
  auto block = std::make_shared<CompoundStatement>();
  MatchSymbol("{");
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    block->statements.push_back(ParseStatement());
  }
  MatchSymbol("}");
  return block;
}

std::shared_ptr<Statement> CppParser::ParseIf() {
  auto stmt = std::make_shared<IfStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("if");
  ExpectSymbol("(", "Expected '(' after if");
  stmt->condition = ParseExpression();
  ExpectSymbol(")", "Expected ')' after condition");
  stmt->then_body.push_back(ParseBlock());
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "else") {
    Consume();
    stmt->else_body.push_back(ParseBlock());
  }
  return stmt;
}

std::shared_ptr<Statement> CppParser::ParseWhile() {
  auto stmt = std::make_shared<WhileStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("while");
  ExpectSymbol("(", "Expected '(' after while");
  stmt->condition = ParseExpression();
  ExpectSymbol(")", "Expected ')' after condition");
  stmt->body.push_back(ParseBlock());
  return stmt;
}

std::shared_ptr<Statement> CppParser::ParseFor() {
  auto stmt = std::make_shared<ForStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("for");
  ExpectSymbol("(", "Expected '(' after for");
  // init
  if (!IsSymbol(";")) {
    stmt->init = ParseStatement();
  } else {
    MatchSymbol(";");
  }
  // condition
  if (!IsSymbol(";")) {
    stmt->condition = ParseExpression();
  }
  MatchSymbol(";");
  // increment
  if (!IsSymbol(")")) {
    stmt->increment = ParseExpression();
  }
  ExpectSymbol(")", "Expected ')' after for clauses");
  stmt->body.push_back(ParseBlock());
  return stmt;
}

std::shared_ptr<Statement> CppParser::ParseStatement() {
  if (current_.kind == frontends::TokenKind::kKeyword) {
    if (current_.lexeme == "import") {
      return ParseImport();
    }
    if (current_.lexeme == "return") {
      return ParseReturn();
    }
    if (current_.lexeme == "if") {
      return ParseIf();
    }
    if (current_.lexeme == "while") {
      return ParseWhile();
    }
    if (current_.lexeme == "for") {
      return ParseFor();
    }
    // type keywords for var or function decls
    if (current_.lexeme == "int" || current_.lexeme == "void" ||
        current_.lexeme == "float" || current_.lexeme == "double" ||
        current_.lexeme == "char" || current_.lexeme == "bool") {
      std::string type_name = current_.lexeme;
      Consume();
      if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "Expected identifier after type");
        MatchSymbol(";");
        return nullptr;
      }
      std::string name = current_.lexeme;
      Consume();
      if (IsSymbol("(")) {
        return ParseFunctionWithSignature(type_name, name);
      }
      return ParseVarDecl(type_name, name);
    }
  }
  if (IsSymbol("{")) {
    return ParseBlock();
  }

  auto expr_stmt = std::make_shared<ExprStatement>();
  expr_stmt->loc = current_.loc;
  expr_stmt->expr = ParseExpression();
  MatchSymbol(";");
  return expr_stmt;
}

void CppParser::ParseTopLevel() {
  if (current_.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  auto stmt = ParseStatement();
  if (stmt) {
    module_->declarations.push_back(stmt);
  } else {
    Sync();
  }
}

void CppParser::ParseModule() {
  Consume();
  for (;;) {
    ParseTopLevel();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> CppParser::TakeModule() { return module_; }

}  // namespace polyglot::cpp
