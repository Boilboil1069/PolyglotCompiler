#include "frontends/cpp/include/cpp_parser.h"

namespace polyglot::cpp {

frontends::Token CppParser::Consume() {
  current_ = lexer_.NextToken();
  return current_;
}

void CppParser::ParseTopLevel() {
  frontends::Token token = Consume();
  if (token.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  if (token.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = token.lexeme;
    ident->loc = token.loc;
    module_->declarations.push_back(ident);
    return;
  }
  diagnostics_.Report(token.loc, "Unexpected token: " + token.lexeme);
}

void CppParser::ParseModule() {
  for (;;) {
    ParseTopLevel();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> CppParser::TakeModule() { return module_; }

}  // namespace polyglot::cpp
