#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lexer.h"

namespace polyglot::rust {

class RustParser : public frontends::ParserBase {
 public:
  RustParser(RustLexer &lexer, frontends::Diagnostics &diagnostics)
      : ParserBase(diagnostics), lexer_(lexer) {}

  void ParseModule() override;
  std::shared_ptr<Module> TakeModule();

 private:
  frontends::Token Consume();
  void ParseItem();

  RustLexer &lexer_;
  std::shared_ptr<Module> module_{std::make_shared<Module>()};
  frontends::Token current_{};
};

}  // namespace polyglot::rust
