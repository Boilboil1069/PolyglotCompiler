#include "frontends/python/include/python_parser.h"

namespace polyglot::python {

frontends::Token PythonParser::Consume() {
  current_ = lexer_.NextToken();
  return current_;
}

bool PythonParser::MatchSymbol(const std::string &symbol) {
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol) {
    Consume();
    return true;
  }
  return false;
}

bool PythonParser::MatchKeyword(const std::string &keyword) {
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
    Consume();
    return true;
  }
  return false;
}

void PythonParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
  if (!MatchSymbol(symbol)) {
    diagnostics_.Report(current_.loc, message);
  }
}

void PythonParser::Sync() {
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (current_.kind == frontends::TokenKind::kKeyword) {
      if (current_.lexeme == "def" || current_.lexeme == "import" ||
          current_.lexeme == "from" || current_.lexeme == "return") {
        return;
      }
    }
    if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ";") {
      Consume();
      return;
    }
    Consume();
  }
}

std::shared_ptr<Expression> PythonParser::ParsePrimary() {
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = current_.lexeme;
    ident->loc = current_.loc;
    Consume();
    if (MatchSymbol("(")) {
      auto call = std::make_shared<CallExpression>();
      call->loc = ident->loc;
      call->callee = ident;
      if (!MatchSymbol(")")) {
        call->args.push_back(ParseExpression());
        MatchSymbol(")");
      }
      return call;
    }
    return ident;
  }
  if (current_.kind == frontends::TokenKind::kNumber ||
      current_.kind == frontends::TokenKind::kString) {
    auto literal = std::make_shared<Literal>();
    literal->value = current_.lexeme;
    literal->loc = current_.loc;
    Consume();
    return literal;
  }
  diagnostics_.Report(current_.loc, "Expected expression");
  return nullptr;
}

std::shared_ptr<Expression> PythonParser::ParseExpression() {
  return ParsePrimary();
}

std::shared_ptr<Statement> PythonParser::ParseImport() {
  auto stmt = std::make_shared<ImportStatement>();
  stmt->loc = current_.loc;
  if (MatchKeyword("from")) {
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      stmt->module = current_.lexeme;
      Consume();
    }
    if (!MatchKeyword("import")) {
      diagnostics_.Report(current_.loc, "Expected 'import' in from-import");
    }
  } else {
    MatchKeyword("import");
  }
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    stmt->name = current_.lexeme;
    Consume();
  } else {
    diagnostics_.Report(current_.loc, "Expected module name");
  }
  return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseReturn() {
  auto stmt = std::make_shared<ReturnStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("return");
  stmt->value = ParseExpression();
  return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseFunction() {
  auto fn = std::make_shared<FunctionDef>();
  fn->loc = current_.loc;
  MatchKeyword("def");
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    fn->name = current_.lexeme;
    Consume();
  } else {
    diagnostics_.Report(current_.loc, "Expected function name");
  }
  ExpectSymbol("(", "Expected '(' after function name");
  if (!MatchSymbol(")")) {
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      fn->params.push_back(current_.lexeme);
      Consume();
    }
    ExpectSymbol(")", "Expected ')' after parameters");
  }
  ExpectSymbol(":", "Expected ':' after function signature");
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "return") {
    fn->body.push_back(ParseReturn());
  } else {
    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->expr = ParseExpression();
    fn->body.push_back(expr_stmt);
  }
  return fn;
}

std::shared_ptr<Statement> PythonParser::ParseStatement() {
  if (current_.kind == frontends::TokenKind::kKeyword) {
    if (current_.lexeme == "import" || current_.lexeme == "from") {
      return ParseImport();
    }
    if (current_.lexeme == "def") {
      return ParseFunction();
    }
    if (current_.lexeme == "return") {
      return ParseReturn();
    }
  }
  auto expr_stmt = std::make_shared<ExprStatement>();
  expr_stmt->loc = current_.loc;
  expr_stmt->expr = ParseExpression();
  return expr_stmt;
}

void PythonParser::ParseTopLevel() {
  if (current_.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  auto stmt = ParseStatement();
  if (stmt) {
    module_->body.push_back(stmt);
  } else {
    Sync();
  }
}

void PythonParser::ParseModule() {
  Consume();
  for (;;) {
    ParseTopLevel();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> PythonParser::TakeModule() { return module_; }

}  // namespace polyglot::python
