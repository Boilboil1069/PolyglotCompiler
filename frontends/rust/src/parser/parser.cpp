#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

frontends::Token RustParser::Consume() {
  current_ = lexer_.NextToken();
  return current_;
}

void RustParser::ParseItem() {
  frontends::Token token = Consume();
  if (token.kind == frontends::TokenKind::kEndOfFile) {
    return;
  }
  if (token.kind == frontends::TokenKind::kIdentifier) {
    auto ident = std::make_shared<Identifier>();
    ident->name = token.lexeme;
    ident->loc = token.loc;
    module_->items.push_back(ident);
    return;
  }
  diagnostics_.Report(token.loc, "Unexpected token: " + token.lexeme);
}

void RustParser::ParseModule() {
  for (;;) {
    ParseItem();
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
      break;
    }
  }
}

std::shared_ptr<Module> RustParser::TakeModule() { return module_; }

}  // namespace polyglot::rust
