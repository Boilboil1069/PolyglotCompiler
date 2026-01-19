#include "frontends/python/include/python_parser.h"

namespace polyglot::python {

frontends::Token PythonParser::Consume() {
  current_ = lexer_.NextToken();
  return current_;
}

void PythonParser::ParseTopLevel() {
  frontends::Token token = Consume();
  if (token.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  if (token.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = token.lexeme;
    ident->loc = token.loc;
    module_->body.push_back(ident);
    return;
  }
  if (token.kind != frontends::TokenKind::kEndOfFile) {
    diagnostics_.Report(token.loc, "Unexpected token: " + token.lexeme);
  }
}

void PythonParser::ParseModule() {
  for (;;) {
    ParseTopLevel();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> PythonParser::TakeModule() { return module_; }

}  // namespace polyglot::python
