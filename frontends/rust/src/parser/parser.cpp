#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

frontends::Token RustParser::Consume() {
  do {
    current_ = lexer_.NextToken();
  } while (current_.kind == frontends::TokenKind::kComment);
  return current_;
}

bool RustParser::IsSymbol(const std::string &symbol) const {
  return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool RustParser::MatchSymbol(const std::string &symbol) {
  if (IsSymbol(symbol)) {
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
      if (current_.lexeme == "fn" || current_.lexeme == "use" ||
          current_.lexeme == "let") {
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

int RustParser::GetPrecedence() const {
  if (current_.kind != frontends::TokenKind::kSymbol) return 0;
  const std::string &op = current_.lexeme;
  if (op == "||") return 1;
  if (op == "&&") return 2;
  if (op == "==" || op == "!=") return 3;
  if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
  if (op == "+" || op == "-") return 5;
  if (op == "*" || op == "/" || op == "%") return 6;
  return 0;
}

std::shared_ptr<BlockExpression> RustParser::ParseBlockExpression() {
  auto start_loc = current_.loc;
  if (!MatchSymbol("{")) {
    diagnostics_.Report(current_.loc, "Expected '{' to start block");
    return nullptr;
  }
  auto block = std::make_shared<BlockExpression>();
  block->loc = start_loc;
  while (current_.kind != frontends::TokenKind::kEndOfFile && !IsSymbol("}")) {
    auto stmt = ParseStatement();
    if (stmt) block->statements.push_back(stmt);
  }
  MatchSymbol("}");
  return block;
}

std::shared_ptr<Expression> RustParser::ParsePrimary() {
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = current_.lexeme;
    ident->loc = current_.loc;
    Consume();
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
  if (IsSymbol("(")) {
    Consume();
    auto expr = ParseExpression();
    MatchSymbol(")");
    return expr;
  }
  if (IsSymbol("{")) {
    return ParseBlockExpression();
  }
  diagnostics_.Report(current_.loc, "Expected expression");
  return nullptr;
}

std::shared_ptr<Expression> RustParser::ParsePostfix() {
  auto expr = ParsePrimary();
  while (expr) {
    if (IsSymbol("(")) {
      auto call = std::make_shared<CallExpression>();
      call->callee = expr;
      call->loc = expr->loc;
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
      diagnostics_.Report(current_.loc, "Expected member name after '.'");
      break;
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

std::shared_ptr<Expression> RustParser::ParseUnary() {
  if (IsSymbol("+") || IsSymbol("-") || IsSymbol("!")) {
    auto unary = std::make_shared<UnaryExpression>();
    unary->op = current_.lexeme;
    unary->loc = current_.loc;
    Consume();
    unary->operand = ParseUnary();
    return unary;
  }
  return ParsePostfix();
}

std::shared_ptr<Expression> RustParser::ParseBinaryExpression(int min_prec) {
  auto lhs = ParseUnary();
  while (true) {
    int prec = GetPrecedence();
    if (prec < min_prec) break;
    std::string op = current_.lexeme;
    Consume();
    auto rhs = ParseUnary();
    int next_prec = GetPrecedence();
    if (next_prec > prec) {
      rhs = ParseBinaryExpression(prec + 1);
    }
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = op;
    bin->left = lhs;
    bin->right = rhs;
    bin->loc = lhs ? lhs->loc : current_.loc;
    lhs = bin;
  }
  return lhs;
}

std::shared_ptr<Expression> RustParser::ParseIfExpression() {
  auto if_expr = std::make_shared<IfExpression>();
  if_expr->loc = current_.loc;
  MatchKeyword("if");
  if_expr->condition = ParseExpression();
  auto then_block = ParseBlockExpression();
  if (then_block) {
    if_expr->then_body = then_block->statements;
  }
  if (MatchKeyword("else")) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "if") {
      auto nested = ParseIfExpression();
      if (nested) {
        auto stmt = std::make_shared<ExprStatement>();
        stmt->expr = nested;
        if_expr->else_body.push_back(stmt);
      }
    } else {
      auto else_block = ParseBlockExpression();
      if (else_block) {
        if_expr->else_body = else_block->statements;
      }
    }
  }
  return if_expr;
}

std::shared_ptr<Expression> RustParser::ParseWhileExpression() {
  auto wh = std::make_shared<WhileExpression>();
  wh->loc = current_.loc;
  MatchKeyword("while");
  wh->condition = ParseExpression();
  auto body = ParseBlockExpression();
  if (body) wh->body = body->statements;
  return wh;
}

std::shared_ptr<Expression> RustParser::ParseExpression() {
  if (current_.kind == frontends::TokenKind::kKeyword) {
    if (current_.lexeme == "if") {
      return ParseIfExpression();
    }
    if (current_.lexeme == "while") {
      return ParseWhileExpression();
    }
  }
  return ParseBinaryExpression();
}

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

std::shared_ptr<Statement> RustParser::ParseLet() {
  auto stmt = std::make_shared<LetStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("let");
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    stmt->name = current_.lexeme;
    Consume();
  } else {
    diagnostics_.Report(current_.loc, "Expected identifier after let");
  }
  if (MatchSymbol("=")) {
    stmt->init = ParseExpression();
  }
  MatchSymbol(";");
  return stmt;
}

std::shared_ptr<Statement> RustParser::ParseReturn() {
  auto stmt = std::make_shared<ReturnStatement>();
  stmt->loc = current_.loc;
  MatchKeyword("return");
  if (!IsSymbol(";")) {
    stmt->value = ParseExpression();
  }
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
      while (MatchSymbol(",")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
          fn->params.push_back(current_.lexeme);
          Consume();
        } else {
          diagnostics_.Report(current_.loc, "Expected parameter name");
          break;
        }
      }
    }
    ExpectSymbol(")", "Expected ')' after parameters");
  }
  auto body = ParseBlockExpression();
  if (body) {
    fn->body = body->statements;
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
    if (current_.lexeme == "let") {
      return ParseLet();
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
