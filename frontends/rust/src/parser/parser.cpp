#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

frontends::Token RustParser::Consume() {
  current_ = lexer_.NextToken();
  return current_;
}

bool RustParser::MatchSymbol(const std::string &symbol) {
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol) {
    Consume();
    return true;
  }
  return false;
}

bool RustParser::MatchKeyword(const std::string &keyword) {
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
    Consume();
    return true;
  }
  return false;
}

void RustParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
  if (!MatchSymbol(symbol)) {
    diagnostics_.Report(current_.loc, message);
  }
}

void RustParser::Sync() {
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (current_.kind == frontends::TokenKind::kKeyword) {
      if (current_.lexeme == "fn" || current_.lexeme == "use") {
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

std::shared_ptr<Expression> RustParser::ParsePrimary() {
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = current_.lexeme;
    ident->loc = current_.loc;
    Consume();
    if (MatchSymbol("(")) {
      auto call = std::make_shared<CallExpression>();
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

std::shared_ptr<Expression> RustParser::ParseExpression() { return ParsePrimary(); }

std::shared_ptr<Statement> RustParser::ParseUse() {
  auto stmt = std::make_shared<UseDeclaration>();
  stmt->loc = current_.loc;
  MatchKeyword("use");
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    stmt->path = current_.lexeme;
    Consume();
  } else {
    diagnostics_.Report(current_.loc, "Expected use path");
  }
  MatchSymbol(";");
  return stmt;
}

std::shared_ptr<Statement> RustParser::ParseReturn() {
  auto stmt = std::make_shared<ReturnStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("return");
  stmt->value = ParseExpression();
  MatchSymbol(";");
  return stmt;
}

std::shared_ptr<Statement> RustParser::ParseFunction() {
  auto fn = std::make_shared<FunctionItem>();
  fn->loc = current_.loc;
  MatchKeyword("fn");
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
  ExpectSymbol("{", "Expected '{' to start function body");
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (MatchSymbol("}")) {
      break;
    }
    if (current_.kind == frontends::TokenKind::kKeyword &&
        current_.lexeme == "return") {
      fn->body.push_back(ParseReturn());
    } else {
      auto expr_stmt = std::make_shared<ExprStatement>();
      expr_stmt->expr = ParseExpression();
      fn->body.push_back(expr_stmt);
      MatchSymbol(";");
    }
  }
  return fn;
}

std::shared_ptr<Statement> RustParser::ParseStatement() {
  if (current_.kind == frontends::TokenKind::kKeyword) {
    if (current_.lexeme == "use") {
      return ParseUse();
    }
    if (current_.lexeme == "fn") {
      return ParseFunction();
    }
    if (current_.lexeme == "return") {
      return ParseReturn();
    }
  }
  auto expr_stmt = std::make_shared<ExprStatement>();
  expr_stmt->loc = current_.loc;
  expr_stmt->expr = ParseExpression();
  MatchSymbol(";");
  return expr_stmt;
}

void RustParser::ParseItem() {
  if (current_.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  auto stmt = ParseStatement();
  if (stmt) {
    module_->items.push_back(stmt);
  } else {
    Sync();
  }
}

void RustParser::ParseModule() {
  Consume();
  for (;;) {
    ParseItem();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> RustParser::TakeModule() { return module_; }

}  // namespace polyglot::rust
