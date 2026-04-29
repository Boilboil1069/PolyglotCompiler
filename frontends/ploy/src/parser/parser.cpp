/**
 * @file     parser.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <stdexcept>

#include "frontends/ploy/include/ploy_parser.h"

namespace polyglot::ploy {

namespace {

// Case-insensitive ASCII string equality.  Used to recognise contextual
// keywords (CLASS / HANDLE / ATTR — demand 2026-04-28-9) without
// promoting them to canonical lexer keywords, which would silently
// shadow common identifiers like `handle` in user code.
bool IEqualsAscii(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (std::toupper(ca) != std::toupper(cb))
      return false;
  }
  return true;
}

} // namespace

// ============================================================================
// Token Management
// ============================================================================

frontends::Token PloyParser::Consume() {
  frontends::Token tok = current_;
  Advance();
  return tok;
}

void PloyParser::Advance() {
  current_ = lexer_.NextToken();
}

bool PloyParser::IsSymbol(const std::string &symbol) const {
  return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool PloyParser::MatchSymbol(const std::string &symbol) {
  if (IsSymbol(symbol)) {
    Advance();
    return true;
  }
  return false;
}

bool PloyParser::MatchKeyword(const std::string &keyword) {
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
    Advance();
    return true;
  }
  return false;
}

void PloyParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
  if (!MatchSymbol(symbol)) {
    diagnostics_.Report(current_.loc, message + ", got '" + current_.lexeme + "'");
    Sync();
  }
}

void PloyParser::ExpectKeyword(const std::string &keyword, const std::string &message) {
  if (!MatchKeyword(keyword)) {
    diagnostics_.Report(current_.loc, message + ", got '" + current_.lexeme + "'");
    Sync();
  }
}

void PloyParser::Sync() {
  // Recovery: skip tokens until we find a synchronization point
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (IsSymbol(";") || IsSymbol("}")) {
      Advance();
      return;
    }
    if (current_.kind == frontends::TokenKind::kKeyword) {
      // Stop at top-level keywords
      const std::string &kw = current_.lexeme;
      if (kw == "LINK" || kw == "IMPORT" || kw == "EXPORT" || kw == "MAP_TYPE" ||
          kw == "PIPELINE" || kw == "FUNC" || kw == "LET" || kw == "VAR" || kw == "IF" ||
          kw == "WHILE" || kw == "FOR" || kw == "MATCH" || kw == "RETURN" || kw == "STRUCT" ||
          kw == "MAP_FUNC" || kw == "PRINTLN" || kw == "TYPE" || kw == "CONST" ||
          kw == "CLASS") {
        return;
      }
    }
    Advance();
  }
}

// ============================================================================
// Module Parsing
// ============================================================================

void PloyParser::ParseModule() {
  Advance(); // Prime the first token
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    ParseTopLevel();
  }
}

std::shared_ptr<Module> PloyParser::TakeModule() {
  return std::move(module_);
}

void PloyParser::ParseTopLevel() {
  // `@LANG(...)` annotation can prefix any top-level statement.
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "@") {
    module_->declarations.push_back(ParseLangAnnotation());
    return;
  }
  if (current_.kind == frontends::TokenKind::kKeyword) {
    const std::string &kw = current_.lexeme;
    if (kw == "LINK") {
      // ParseLinkDecl is a dispatcher: it consumes 'LINK' and then chooses
      // between the legacy comma form and the canonical signed form.
      module_->declarations.push_back(ParseLinkDecl());
      return;
    }
    if (kw == "LANG") {
      module_->declarations.push_back(ParseLangPragma());
      return;
    }
    if (kw == "IMPORT") {
      module_->declarations.push_back(ParseImportDecl());
      return;
    }
    if (kw == "EXPORT") {
      module_->declarations.push_back(ParseExportDecl());
      return;
    }
    if (kw == "MAP_TYPE") {
      module_->declarations.push_back(ParseMapTypeDecl());
      return;
    }
    if (kw == "PIPELINE") {
      module_->declarations.push_back(ParsePipelineDecl());
      return;
    }
    if (kw == "FUNC") {
      module_->declarations.push_back(ParseFuncDecl());
      return;
    }
    if (kw == "STRUCT") {
      module_->declarations.push_back(ParseStructDecl());
      return;
    }
    if (kw == "MAP_FUNC") {
      module_->declarations.push_back(ParseMapFuncDecl());
      return;
    }
    if (kw == "CONFIG") {
      module_->declarations.push_back(ParseConfigDecl());
      return;
    }
    if (kw == "EXTEND") {
      module_->declarations.push_back(ParseExtendDecl());
      return;
    }
    if (kw == "PRINTLN") {
      module_->declarations.push_back(ParsePrintlnStatement());
      return;
    }
    if (kw == "TYPE") {
      module_->declarations.push_back(ParseTypeAliasDecl());
      return;
    }
    if (kw == "CONST") {
      module_->declarations.push_back(ParseConstDecl());
      return;
    }
    if (kw == "CLASS") {
      module_->declarations.push_back(ParseClassDecl());
      return;
    }
  }
  // Contextual top-level keyword: CLASS (demand 2026-04-28-9).  Recognised
  // when the current token is an identifier whose case-insensitive spelling
  // is "CLASS".  Kept contextual rather than a true keyword so existing
  // identifier uses of `class` (rare but legal) keep parsing as identifiers.
  if (current_.kind == frontends::TokenKind::kIdentifier &&
      IEqualsAscii(current_.lexeme, "CLASS")) {
    module_->declarations.push_back(ParseClassDecl());
    return;
  }
  // Fallback: parse as a statement
  module_->declarations.push_back(ParseStatement());
}

// ============================================================================
// LINK Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseLinkDecl() {
  auto node = std::make_shared<LinkDecl>();
  node->is_legacy_form = true;
  node->loc = current_.loc;
  Advance(); // consume 'LINK'

  // Dispatch to canonical signed form when no '(' follows the LINK keyword.
  // The signed form is: LINK <lang>::<module>::<func> AS FUNC(...) -> <ret>;
  if (!IsSymbol("(")) {
    return ParseSignedLinkDecl();
  }

  ExpectSymbol("(", "expected '(' after LINK");

  // target_language
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->target_language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected target language identifier");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after target language");

  // source_language
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->source_language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected source language identifier");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after source language");

  // target_function (may be qualified: module::func)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->target_symbol = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->target_symbol += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->target_symbol += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected target symbol");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after target symbol");

  // source_function (may be qualified)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->source_symbol = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->source_symbol += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->source_symbol += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected source symbol");
    Sync();
    return node;
  }

  ExpectSymbol(")", "expected ')' after LINK arguments");

  // Optional RETURNS clause: LINK(...) RETURNS type { ... }
  // The RETURNS clause is preserved for backward compatibility but is
  // deprecated since Ploy 1.5.2; new code should declare the return type
  // via the canonical "-> Type" arrow syntax on the function signature.
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "RETURNS") {
    const core::SourceLoc returns_loc = current_.loc;
    const std::string spelled_as = current_.SourceText();
    diagnostics_.ReportWarning(
        returns_loc, frontends::ErrorCode::kDeprecatedKeyword,
        "'" + spelled_as + "' clause is deprecated; declare the return type via '-> Type' on the LINK signature instead");
    Advance();
    node->return_type = ParseQualifiedOrSimpleType();
  }

  // Optional: AS VAR or AS STRUCT
  if (MatchKeyword("AS")) {
    if (MatchKeyword("VAR")) {
      node->link_kind = LinkDecl::LinkKind::kVariable;
    } else if (MatchKeyword("STRUCT")) {
      node->link_kind = LinkDecl::LinkKind::kStruct;
    } else {
      diagnostics_.Report(current_.loc, "expected VAR or STRUCT after AS");
    }
  }

  // Optional body with MAP_TYPE directives
  if (IsSymbol("{")) {
    Advance();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "MAP_TYPE") {
        node->body.push_back(ParseMapTypeDecl());
      } else {
        diagnostics_.Report(current_.loc, "expected MAP_TYPE inside LINK body");
        Sync();
      }
    }
    ExpectSymbol("}", "expected '}' to close LINK body");
  } else {
    // Simple LINK without body �?expect semicolon
    ExpectSymbol(";", "expected ';' after LINK directive");
  }

  return node;
}

// ============================================================================
// Signed / canonical LINK form:
//   LINK <lang>::<module>::<function> AS FUNC(<types>) -> <ret_type> { ... }
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseSignedLinkDecl() {
  // Caller (ParseLinkDecl dispatcher) has already consumed the LINK keyword.
  auto node = std::make_shared<LinkDecl>();
  node->is_legacy_form = false;
  node->loc = current_.loc;

  // target symbol: lang::module::func
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->target_symbol = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->target_symbol += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->target_symbol += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected target symbol after LINK");
    Sync();
    return node;
  }

  // Optional AS FUNC(...) -> ret
  if (MatchKeyword("AS")) {
    if (MatchKeyword("FUNC")) {
      ExpectSymbol("(", "expected '(' after FUNC");
      // parse parameter types (simple form)
      std::vector<std::shared_ptr<TypeNode>> params;
      if (!IsSymbol(")")) {
        do {
          params.push_back(ParseType());
        } while (MatchSymbol(","));
      }
      ExpectSymbol(")", "expected ')' after FUNC parameter list");

      // optional return type arrow
      if (IsSymbol("->")) {
        Advance();
        node->return_type = ParseType();
      }
    } else {
      diagnostics_.Report(current_.loc, "expected FUNC after AS in LINK");
    }
  }

  // Optional body with MAP_TYPE directives
  if (IsSymbol("{")) {
    Advance();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "MAP_TYPE") {
        node->body.push_back(ParseMapTypeDecl());
      } else {
        diagnostics_.Report(current_.loc, "expected MAP_TYPE inside LINK body");
        Sync();
      }
    }
    ExpectSymbol("}", "expected '}' to close LINK body");
  } else {
    ExpectSymbol(";", "expected ';' after LINK directive");
  }

  return node;
}

// ============================================================================
// IMPORT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseImportDecl() {
  auto node = std::make_shared<ImportDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'IMPORT'

  // IMPORT "path" AS alias;
  // IMPORT lang::module;
  // IMPORT lang PACKAGE pkg;
  // IMPORT lang PACKAGE pkg AS alias;
  if (current_.kind == frontends::TokenKind::kString) {
    // Path import: IMPORT "path/to/module"
    std::string raw = current_.lexeme;
    // Strip surrounding quotes
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
      raw = raw.substr(1, raw.size() - 2);
    }
    node->module_path = raw;
    Advance();
  } else if (current_.kind == frontends::TokenKind::kIdentifier ||
             current_.kind == frontends::TokenKind::kKeyword) {
    // Could be: IMPORT cpp::module_name;
    //       or: IMPORT python PACKAGE numpy;
    //       or: IMPORT python PACKAGE numpy AS np;
    std::string first = current_.lexeme;
    Advance();

    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "PACKAGE") {
      // Package import: IMPORT lang PACKAGE pkg [:: (sym1, sym2)] [>= ver] [AS alias];
      node->language = first;
      Advance(); // consume 'PACKAGE'

      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->package_name = current_.lexeme;
        node->module_path = current_.lexeme;
        Advance();
        // Handle dotted package paths: numpy.linalg
        while (IsSymbol(".")) {
          node->package_name += ".";
          node->module_path += ".";
          Advance();
          if (current_.kind == frontends::TokenKind::kIdentifier) {
            node->package_name += current_.lexeme;
            node->module_path += current_.lexeme;
            Advance();
          }
        }
      } else {
        diagnostics_.Report(current_.loc, "expected package name after PACKAGE");
      }

      // Optional: selective imports via ::(sym1, sym2, ...)
      if (IsSymbol("::")) {
        Advance(); // consume '::'
        if (IsSymbol("(")) {
          Advance(); // consume '('
          // Parse comma-separated list of symbol names
          while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
              node->selected_symbols.push_back(current_.lexeme);
              Advance();
            } else {
              diagnostics_.Report(current_.loc, "expected symbol name in selective import list");
              break;
            }
            if (IsSymbol(",")) {
              Advance(); // consume ','
            } else {
              break;
            }
          }
          ExpectSymbol(")", "expected ')' after selective import list");
        } else {
          diagnostics_.Report(current_.loc, "expected '(' after '::' in selective import");
        }
      }

      // Optional: version constraint (>=, <=, ==, >, <, ~=)
      if (IsSymbol(">=") || IsSymbol("<=") || IsSymbol("==") || IsSymbol(">") || IsSymbol("<") ||
          IsSymbol("~=")) {
        node->version_op = current_.lexeme;
        Advance(); // consume version operator

        // Parse version string: may be numeric tokens joined by dots (1.20.0)
        if (current_.kind == frontends::TokenKind::kNumber ||
            current_.kind == frontends::TokenKind::kIdentifier) {
          node->version_constraint = current_.lexeme;
          Advance();
          // Handle dotted version: 1.20.0
          while (IsSymbol(".")) {
            node->version_constraint += ".";
            Advance();
            if (current_.kind == frontends::TokenKind::kNumber ||
                current_.kind == frontends::TokenKind::kIdentifier) {
              node->version_constraint += current_.lexeme;
              Advance();
            }
          }
        } else if (current_.kind == frontends::TokenKind::kString) {
          // Allow quoted version strings: >= "1.20"
          std::string raw = current_.lexeme;
          if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
          }
          node->version_constraint = raw;
          Advance();
        } else {
          diagnostics_.Report(current_.loc,
                              "expected version number after '" + node->version_op + "'");
        }
      }
    } else if (IsSymbol("::")) {
      // Qualified import: IMPORT cpp::module_name
      node->language = first;
      Advance(); // consume '::'
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->module_path = current_.lexeme;
        Advance();
        // Handle deeper qualification: module::sub
        while (IsSymbol("::")) {
          node->module_path += "::";
          Advance();
          if (current_.kind == frontends::TokenKind::kIdentifier) {
            node->module_path += current_.lexeme;
            Advance();
          }
        }
      }
    } else {
      // Just an identifier without :: or PACKAGE
      node->module_path = first;
      node->language.clear();
    }
  } else {
    diagnostics_.Report(current_.loc, "expected module path or identifier after IMPORT");
    Sync();
    return node;
  }

  // Optional: AS alias
  if (MatchKeyword("AS")) {
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->alias = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected alias name after AS");
    }
  }

  ExpectSymbol(";", "expected ';' after IMPORT");
  return node;
}

// ============================================================================
// EXPORT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseExportDecl() {
  auto node = std::make_shared<ExportDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'EXPORT'

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->symbol_name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected symbol name after EXPORT");
    Sync();
    return node;
  }

  // Optional: AS "external_name"
  if (MatchKeyword("AS")) {
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->external_name = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected string literal after AS");
    }
  }

  ExpectSymbol(";", "expected ';' after EXPORT");
  return node;
}

// ============================================================================
// MAP_TYPE Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseMapTypeDecl() {
  auto node = std::make_shared<MapTypeDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'MAP_TYPE'

  ExpectSymbol("(", "expected '(' after MAP_TYPE");

  node->source_type = ParseQualifiedOrSimpleType();

  ExpectSymbol(",", "expected ',' between types in MAP_TYPE");

  node->target_type = ParseQualifiedOrSimpleType();

  ExpectSymbol(")", "expected ')' after MAP_TYPE arguments");
  ExpectSymbol(";", "expected ';' after MAP_TYPE");

  return node;
}

// ============================================================================
// PIPELINE Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParsePipelineDecl() {
  auto node = std::make_shared<PipelineDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'PIPELINE'

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected pipeline name");
    Sync();
    return node;
  }

  ExpectSymbol("{", "expected '{' after pipeline name");
  in_pipeline_context_ = true;
  node->body = ParseBlockBody();
  in_pipeline_context_ = false;
  ExpectSymbol("}", "expected '}' to close pipeline");

  return node;
}

// ============================================================================
// STAGE Declaration (only valid inside PIPELINE)
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseStageDecl() {
  auto node = std::make_shared<StageDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'STAGE'

  // optional identifier name
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  }

  // Expect CALL keyword then language-qualified target
  if (MatchKeyword("CALL") || (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CALL")) {
    // already consumed by MatchKeyword if present
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CALL") {
    Advance();
  }

  // Parse language-qualified target like cpp::module::func
  if (current_.kind == frontends::TokenKind::kIdentifier || current_.kind == frontends::TokenKind::kKeyword) {
    node->call_target = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->call_target += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->call_target += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected call target after CALL in STAGE");
    Sync();
    return node;
  }

  ExpectSymbol(";", "expected ';' after STAGE declaration");
  return node;
}

// ============================================================================
// FUNC Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseFuncDecl() {
  auto node = std::make_shared<FuncDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'FUNC'

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected function name");
    Sync();
    return node;
  }

  ExpectSymbol("(", "expected '(' after function name");
  node->params = ParseParams();
  ExpectSymbol(")", "expected ')' after parameters");

  // Optional return type: -> TYPE
  if (IsSymbol("->")) {
    Advance();
    node->return_type = ParseType();
  }

  ExpectSymbol("{", "expected '{' for function body");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close function");

  return node;
}

// ============================================================================
// Parameters
// ============================================================================

std::vector<FuncDecl::Param> PloyParser::ParseParams() {
  std::vector<FuncDecl::Param> params;
  if (IsSymbol(")"))
    return params;

  do {
    FuncDecl::Param param;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      param.name = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected parameter name");
      break;
    }

    ExpectSymbol(":", "expected ':' after parameter name");
    param.type = ParseType();
    params.push_back(std::move(param));
  } while (MatchSymbol(","));

  return params;
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseStatement() {
  // `@LANG(...)` annotation at statement level.
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "@") {
    return ParseLangAnnotation();
  }
  if (current_.kind == frontends::TokenKind::kKeyword) {
    const std::string &kw = current_.lexeme;
    if (kw == "LANG") {
      // Top-level pin can also appear as a statement.
      return ParseLangPragma();
    }
    if (kw == "LET") {
      Advance();
      return ParseVarDecl(false);
    }
    if (kw == "VAR") {
      Advance();
      return ParseVarDecl(true);
    }
    if (kw == "IF") {
      return ParseIfStatement();
    }
    if (kw == "WHILE") {
      return ParseWhileStatement();
    }
    if (kw == "FOR") {
      return ParseForStatement();
    }
    if (kw == "MATCH") {
      return ParseMatchStatement();
    }
    if (kw == "RETURN") {
      return ParseReturnStatement();
    }
    if (kw == "WITH") {
      // `WITH LANG (...) { ... }` is a version-pinning
      // block; the legacy `WITH (lang, expr) AS name { ... }` is the
      // resource-management form.
      // We lex one token ahead to disambiguate.
      auto saved = current_;
      Advance();
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "LANG") {
        // current_ points at LANG; ParseWithLangBlock consumes LANG and the rest.
        return ParseWithLangBlock();
      }
      // Roll back: the WITH-resource form expects `current_` to be just past
      // WITH already, so feed the parser the right state by emulating the
      // original entry point.
      // (ParseWithStatement re-reads `current_` and immediately Advance()s
      //  over WITH, so we restore by pushing the saved WITH back into
      //  current_; subsequent token is already cached in our peek.)
      // Simpler approach: re-implement the body inline here.
      auto node = std::make_shared<WithStatement>();
      node->loc = saved.loc;
      // We are already past WITH; the parser body of ParseWithStatement after
      // its own `Advance()` continues from the same point.
      ExpectSymbol("(", "expected '(' after WITH");
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        node->language = current_.lexeme;
        Advance();
      } else {
        diagnostics_.Report(current_.loc, "expected language name in WITH");
        Sync();
        return node;
      }
      ExpectSymbol(",", "expected ',' after language in WITH");
      node->resource_expr = ParseExpression();
      ExpectSymbol(")", "expected ')' after WITH arguments");
      ExpectKeyword("AS", "expected 'AS' after WITH(...)");
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->var_name = current_.lexeme;
        Advance();
      } else {
        diagnostics_.Report(current_.loc, "expected variable name after AS in WITH");
        Sync();
        return node;
      }
      ExpectSymbol("{", "expected '{' after WITH ... AS name");
      node->body = ParseBlockBody();
      ExpectSymbol("}", "expected '}' to close WITH block");
      return node;
    }
    if (kw == "BREAK") {
      auto node = std::make_shared<BreakStatement>();
      node->loc = current_.loc;
      Advance();
      ExpectSymbol(";", "expected ';' after BREAK");
      return node;
    }
    if (kw == "CONTINUE") {
      auto node = std::make_shared<ContinueStatement>();
      node->loc = current_.loc;
      Advance();
      ExpectSymbol(";", "expected ';' after CONTINUE");
      return node;
    }
    // LINK, IMPORT, EXPORT, MAP_TYPE, PIPELINE, FUNC at statement level
    if (kw == "LINK")
      return ParseLinkDecl();
    if (kw == "IMPORT")
      return ParseImportDecl();
    if (kw == "EXPORT")
      return ParseExportDecl();
    if (kw == "MAP_TYPE")
      return ParseMapTypeDecl();
    if (kw == "PIPELINE")
      return ParsePipelineDecl();
    if (kw == "FUNC")
      return ParseFuncDecl();
    if (kw == "PRINTLN")
      return ParsePrintlnStatement();
    if (kw == "TYPE")
      return ParseTypeAliasDecl();
    if (kw == "CONST")
      return ParseConstDecl();
    if (kw == "STAGE") {
      if (!in_pipeline_context_) {
        diagnostics_.Report(current_.loc, "unexpected keyword 'STAGE' outside PIPELINE");
      }
      return ParseStageDecl();
    }
  }

  // Expression statement
  auto node = std::make_shared<ExprStatement>();
  node->loc = current_.loc;
  node->expr = ParseExpression();
  ExpectSymbol(";", "expected ';' after expression");
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseVarDecl(bool is_mutable) {
  auto node = std::make_shared<VarDecl>();
  node->loc = current_.loc;
  node->is_mutable = is_mutable;

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected variable name");
    Sync();
    return node;
  }

  // Optional type annotation: : TYPE
  if (IsSymbol(":")) {
    Advance();
    node->type = ParseType();
  }

  // Optional initializer: = expr
  if (IsSymbol("=")) {
    Advance();
    node->init = ParseExpression();
  }

  ExpectSymbol(";", "expected ';' after variable declaration");
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseIfStatement() {
  auto node = std::make_shared<IfStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'IF'

  node->condition = ParseExpression();
  ExpectSymbol("{", "expected '{' after IF condition");
  node->then_body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close IF body");

  // Optional ELSE / ELSE IF
  if (MatchKeyword("ELSE")) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "IF") {
      // ELSE IF �?nest as a single statement in else_body
      auto elif = ParseIfStatement();
      node->else_body.push_back(elif);
    } else {
      ExpectSymbol("{", "expected '{' after ELSE");
      node->else_body = ParseBlockBody();
      ExpectSymbol("}", "expected '}' to close ELSE body");
    }
  }

  return node;
}

std::shared_ptr<Statement> PloyParser::ParseWhileStatement() {
  auto node = std::make_shared<WhileStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'WHILE'

  node->condition = ParseExpression();
  ExpectSymbol("{", "expected '{' after WHILE condition");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close WHILE body");

  return node;
}

std::shared_ptr<Statement> PloyParser::ParseForStatement() {
  auto node = std::make_shared<ForStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'FOR'

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->iterator_name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected iterator variable name after FOR");
    Sync();
    return node;
  }

  ExpectKeyword("IN", "expected 'IN' in FOR statement");
  node->iterable = ParseExpression();

  ExpectSymbol("{", "expected '{' after FOR iterable");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close FOR body");

  return node;
}

std::shared_ptr<Statement> PloyParser::ParseMatchStatement() {
  auto node = std::make_shared<MatchStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'MATCH'

  node->value = ParseExpression();
  ExpectSymbol("{", "expected '{' after MATCH value");

  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    MatchStatement::Case match_case;

    if (MatchKeyword("CASE")) {
      match_case.pattern = ParseExpression();
    } else if (MatchKeyword("DEFAULT")) {
      match_case.pattern = nullptr; // default case
    } else {
      diagnostics_.Report(current_.loc, "expected CASE or DEFAULT in MATCH");
      Sync();
      continue;
    }

    ExpectSymbol("{", "expected '{' after CASE/DEFAULT");
    match_case.body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close CASE/DEFAULT");

    node->cases.push_back(std::move(match_case));
  }

  ExpectSymbol("}", "expected '}' to close MATCH");
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseReturnStatement() {
  auto node = std::make_shared<ReturnStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'RETURN'

  if (!IsSymbol(";")) {
    node->value = ParseExpression();
  }

  ExpectSymbol(";", "expected ';' after RETURN");
  return node;
}

// PRINTLN "literal" ;
//
// Minimal runtime-IO statement (Stage B2 of the stdout pipeline). Accepts a
// *single* string literal — no expressions, no concatenation, no formatting —
// in order to keep the front-end change small while later stages (B3/B4)
// build the IR / codegen path. The lexer hands us the literal lexeme with
// the surrounding double-quote characters still attached and any backslash
// escapes preserved verbatim; we strip the quotes here but leave escape
// sequences as-is so that downstream codegen owns the single canonical
// decoder. The trailing ';' is mandatory for grammar uniformity.
std::shared_ptr<Statement> PloyParser::ParsePrintlnStatement() {
  auto node = std::make_shared<PrintlnStmt>();
  node->loc = current_.loc;
  ExpectKeyword("PRINTLN", "expected 'PRINTLN'");

  if (current_.kind != frontends::TokenKind::kString) {
    diagnostics_.Report(current_.loc,
                        "expected string literal after PRINTLN, got '" +
                            current_.lexeme + "'");
    Sync();
    return node;
  }

  std::string raw = current_.lexeme;
  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
    raw = raw.substr(1, raw.size() - 2);
  }
  node->message = std::move(raw);
  Advance(); // consume the string literal token

  ExpectSymbol(";", "expected ';' after PRINTLN literal");
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseBlock() {
  auto node = std::make_shared<BlockStatement>();
  node->loc = current_.loc;
  ExpectSymbol("{", "expected '{'");
  node->statements = ParseBlockBody();
  ExpectSymbol("}", "expected '}'");
  return node;
}

std::vector<std::shared_ptr<Statement>> PloyParser::ParseBlockBody() {
  std::vector<std::shared_ptr<Statement>> stmts;
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    stmts.push_back(ParseStatement());
  }
  return stmts;
}

// ============================================================================
// Expressions
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseExpression() {
  return ParseAssignment();
}

std::shared_ptr<Expression> PloyParser::ParseAssignment() {
  auto left = ParseLogicalOr();

  if (IsSymbol("=") && !IsSymbol("==")) {
    // Check for '=' vs '=='
    // IsSymbol already matched '=', but we need to be careful
    // Actually '==' is handled as a separate token, so '=' is safe here
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = "=";
    Advance();
    node->left = left;
    node->right = ParseAssignment();
    return node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseLogicalOr() {
  auto left = ParseLogicalAnd();

  while (IsSymbol("||") || MatchKeyword("OR")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = "||";
    if (IsSymbol("||"))
      Advance();
    node->left = left;
    node->right = ParseLogicalAnd();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseLogicalAnd() {
  auto left = ParseEquality();

  while (IsSymbol("&&") || MatchKeyword("AND")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = "&&";
    if (IsSymbol("&&"))
      Advance();
    node->left = left;
    node->right = ParseEquality();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseEquality() {
  auto left = ParseComparison();

  while (IsSymbol("==") || IsSymbol("!=")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = current_.lexeme;
    Advance();
    node->left = left;
    node->right = ParseComparison();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseComparison() {
  auto left = ParseAdditive();

  while (IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = current_.lexeme;
    Advance();
    node->left = left;
    node->right = ParseAdditive();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseAdditive() {
  auto left = ParseMultiplicative();

  while (IsSymbol("+") || IsSymbol("-")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = current_.lexeme;
    Advance();
    node->left = left;
    node->right = ParseMultiplicative();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseMultiplicative() {
  auto left = ParseUnary();

  while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
    auto node = std::make_shared<BinaryExpression>();
    node->loc = current_.loc;
    node->op = current_.lexeme;
    Advance();
    node->left = left;
    node->right = ParseUnary();
    left = node;
  }

  return left;
}

std::shared_ptr<Expression> PloyParser::ParseUnary() {
  if (IsSymbol("-") || IsSymbol("!")) {
    auto node = std::make_shared<UnaryExpression>();
    node->loc = current_.loc;
    node->op = current_.lexeme;
    Advance();
    node->operand = ParseUnary();
    return node;
  }
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NOT") {
    auto node = std::make_shared<UnaryExpression>();
    node->loc = current_.loc;
    node->op = "!";
    Advance();
    node->operand = ParseUnary();
    return node;
  }

  return ParsePostfix();
}

std::shared_ptr<Expression> PloyParser::ParsePostfix() {
  auto expr = ParsePrimary();

  while (true) {
    if (IsSymbol("(")) {
      // Function call
      Advance();
      auto call = std::make_shared<CallExpression>();
      call->loc = expr->loc;
      call->callee = expr;
      call->args = ParseArguments();
      ExpectSymbol(")", "expected ')' after call arguments");
      expr = call;
    } else if (IsSymbol(".")) {
      // Member access
      Advance();
      auto member = std::make_shared<MemberExpression>();
      member->loc = current_.loc;
      member->object = expr;
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        member->member = current_.lexeme;
        Advance();
      } else {
        diagnostics_.Report(current_.loc, "expected member name after '.'");
      }
      expr = member;
    } else if (IsSymbol("[")) {
      // Index access
      Advance();
      auto index = std::make_shared<IndexExpression>();
      index->loc = current_.loc;
      index->object = expr;
      index->index = ParseExpression();
      ExpectSymbol("]", "expected ']' after index");
      expr = index;
    } else {
      break;
    }
  }

  return expr;
}

std::shared_ptr<Expression> PloyParser::ParsePrimary() {
  // CALL directive
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CALL") {
    return ParseCallDirective();
  }

  // NEW expression: NEW(language, class, args...)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NEW") {
    return ParseNewExpression();
  }

  // METHOD expression: METHOD(language, object, method, args...)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "METHOD") {
    return ParseMethodCallDirective();
  }

  // GET expression: GET(language, object, attribute)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "GET") {
    return ParseGetAttrExpression();
  }

  // SET expression: SET(language, object, attribute, value)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "SET") {
    return ParseSetAttrExpression();
  }

  // DELETE expression: DELETE(language, object)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "DELETE") {
    return ParseDeleteExpression();
  }

  // CONVERT expression: CONVERT(expr, Type)
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CONVERT") {
    return ParseConvertExpression();
  }

  // Boolean literals
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "TRUE") {
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->kind = Literal::Kind::kBool;
    lit->value = "true";
    Advance();
    return lit;
  }
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "FALSE") {
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->kind = Literal::Kind::kBool;
    lit->value = "false";
    Advance();
    return lit;
  }

  // NULL literal
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NULL") {
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->kind = Literal::Kind::kNull;
    lit->value = "null";
    Advance();
    return lit;
  }

  // Identifier (possibly qualified with :: or struct literal with {)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    std::string name = current_.lexeme;
    core::SourceLoc loc = current_.loc;
    Advance();

    // Check for qualified identifier: name::sub
    if (IsSymbol("::")) {
      auto qid = std::make_shared<QualifiedIdentifier>();
      qid->loc = loc;
      qid->qualifier = name;
      Advance(); // consume '::'
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        qid->name = current_.lexeme;
        Advance();
        // Handle deeper qualification
        while (IsSymbol("::")) {
          qid->name += "::";
          Advance();
          if (current_.kind == frontends::TokenKind::kIdentifier) {
            qid->name += current_.lexeme;
            Advance();
          }
        }
      }
      return qid;
    }

    // Check for struct literal: Name { field: value, ... }
    // Use lookahead to distinguish from block statement: peek past '{' to see
    // if the next two tokens are 'identifier :' (struct literal pattern)
    if (IsSymbol("{")) {
      auto saved = lexer_.SaveState();
      auto saved_current = current_;
      Advance(); // consume '{'
      bool is_struct = false;
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto field_name_tok = current_;
        Advance();
        if (IsSymbol(":")) {
          is_struct = true;
        }
      }
      // Also treat empty braces `{}` as empty struct literal
      if (IsSymbol("}")) {
        is_struct = true;
      }
      // Restore lexer and parser state
      lexer_.RestoreState(saved);
      current_ = saved_current;
      if (is_struct) {
        return ParseStructLiteral(name);
      }
    }

    // Check for range: name..end
    if (IsSymbol("..")) {
      auto range = std::make_shared<RangeExpression>();
      range->loc = loc;
      auto start = std::make_shared<Identifier>();
      start->loc = loc;
      start->name = name;
      range->start = start;
      Advance(); // consume '..'
      range->end = ParseExpression();
      return range;
    }

    auto id = std::make_shared<Identifier>();
    id->loc = loc;
    id->name = name;
    return id;
  }

  // Number literal
  if (current_.kind == frontends::TokenKind::kNumber) {
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    // Determine if integer or float
    bool has_dot = current_.lexeme.find('.') != std::string::npos;
    bool has_exp = current_.lexeme.find('e') != std::string::npos ||
                   current_.lexeme.find('E') != std::string::npos;
    lit->kind = (has_dot || has_exp) ? Literal::Kind::kFloat : Literal::Kind::kInteger;
    lit->value = current_.lexeme;
    Advance();

    // Check for range: 0..10
    if (IsSymbol("..")) {
      auto range = std::make_shared<RangeExpression>();
      range->loc = lit->loc;
      range->start = lit;
      Advance();
      range->end = ParseExpression();
      return range;
    }

    return lit;
  }

  // String literal
  if (current_.kind == frontends::TokenKind::kString) {
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->kind = Literal::Kind::kString;
    lit->value = current_.lexeme;
    Advance();
    return lit;
  }

  // List literal: [expr, expr, ...]
  if (IsSymbol("[")) {
    return ParseListLiteral();
  }

  // Dict literal: {"key": value, ...}
  if (IsSymbol("{")) {
    return ParseDictLiteral();
  }

  // Grouped expression or tuple literal: (expr) or (expr, expr, ...)
  if (IsSymbol("(")) {
    core::SourceLoc loc = current_.loc;
    Advance(); // consume '('
    if (IsSymbol(")")) {
      // Empty tuple literal
      Advance();
      auto tuple = std::make_shared<TupleLiteral>();
      tuple->loc = loc;
      return tuple;
    }
    auto first = ParseExpression();
    if (MatchSymbol(",")) {
      // This is a tuple literal
      auto tuple = std::make_shared<TupleLiteral>();
      tuple->loc = loc;
      tuple->elements.push_back(first);
      if (!IsSymbol(")")) {
        tuple->elements.push_back(ParseExpression());
        while (MatchSymbol(",")) {
          if (IsSymbol(")"))
            break; // trailing comma
          tuple->elements.push_back(ParseExpression());
        }
      }
      ExpectSymbol(")", "expected ')' after tuple literal");
      return tuple;
    }
    // Single expression in parens �?grouped expression
    ExpectSymbol(")", "expected ')' after grouped expression");
    return first;
  }

  // Error recovery
  diagnostics_.Report(current_.loc, "unexpected token '" + current_.lexeme + "'");
  auto err = std::make_shared<Literal>();
  err->loc = current_.loc;
  err->kind = Literal::Kind::kNull;
  err->value = "null";
  Advance();
  return err;
}

std::shared_ptr<Expression> PloyParser::ParseCallDirective() {
  auto node = std::make_shared<CrossLangCallExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'CALL'

  ExpectSymbol("(", "expected '(' after CALL");

  // language
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in CALL");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in CALL");

  // function name (possibly qualified)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->function = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->function += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->function += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected function name in CALL");
    Sync();
    return node;
  }

  // Remaining arguments (optional)
  while (MatchSymbol(",")) {
    node->args.push_back(ParseExpression());
  }

  ExpectSymbol(")", "expected ')' after CALL arguments");
  return node;
}

std::shared_ptr<Expression> PloyParser::ParseNewExpression() {
  auto node = std::make_shared<NewExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'NEW'

  ExpectSymbol("(", "expected '(' after NEW");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in NEW");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in NEW");

  // Class name (possibly qualified with ::)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->class_name = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->class_name += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->class_name += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected class name in NEW");
    Sync();
    return node;
  }

  // Constructor arguments (optional)
  while (MatchSymbol(",")) {
    node->args.push_back(ParseExpression());
  }

  ExpectSymbol(")", "expected ')' after NEW arguments");
  return node;
}

std::shared_ptr<Expression> PloyParser::ParseMethodCallDirective() {
  auto node = std::make_shared<MethodCallExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'METHOD'

  ExpectSymbol("(", "expected '(' after METHOD");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in METHOD");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in METHOD");

  // Object expression
  node->object = ParseExpression();

  ExpectSymbol(",", "expected ',' after object in METHOD");

  // Method name (possibly qualified with ::)
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->method_name = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->method_name += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->method_name += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected method name in METHOD");
    Sync();
    return node;
  }

  // Method arguments (optional)
  while (MatchSymbol(",")) {
    node->args.push_back(ParseExpression());
  }

  ExpectSymbol(")", "expected ')' after METHOD arguments");
  return node;
}

std::shared_ptr<Expression> PloyParser::ParseGetAttrExpression() {
  auto node = std::make_shared<GetAttrExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'GET'

  ExpectSymbol("(", "expected '(' after GET");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in GET");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in GET");

  // Object expression
  node->object = ParseExpression();

  ExpectSymbol(",", "expected ',' after object in GET");

  // Attribute name
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->attr_name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected attribute name in GET");
    Sync();
    return node;
  }

  ExpectSymbol(")", "expected ')' after GET arguments");
  return node;
}

std::shared_ptr<Expression> PloyParser::ParseSetAttrExpression() {
  auto node = std::make_shared<SetAttrExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'SET'

  ExpectSymbol("(", "expected '(' after SET");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in SET");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in SET");

  // Object expression
  node->object = ParseExpression();

  ExpectSymbol(",", "expected ',' after object in SET");

  // Attribute name
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->attr_name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected attribute name in SET");
    Sync();
    return node;
  }

  ExpectSymbol(",", "expected ',' after attribute name in SET");

  // Value expression
  node->value = ParseExpression();

  ExpectSymbol(")", "expected ')' after SET arguments");
  return node;
}

// ============================================================================
// DELETE Expression: DELETE(language, object)
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseDeleteExpression() {
  auto node = std::make_shared<DeleteExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'DELETE'

  ExpectSymbol("(", "expected '(' after DELETE");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in DELETE");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in DELETE");

  // Object expression
  node->object = ParseExpression();

  ExpectSymbol(")", "expected ')' after DELETE arguments");
  return node;
}

// ============================================================================
// WITH Statement: WITH(language, resource_expr) AS name { body }
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseWithStatement() {
  auto node = std::make_shared<WithStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'WITH'

  ExpectSymbol("(", "expected '(' after WITH");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in WITH");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in WITH");

  // Resource expression (typically a NEW expression)
  node->resource_expr = ParseExpression();

  ExpectSymbol(")", "expected ')' after WITH arguments");

  // AS variable_name
  ExpectKeyword("AS", "expected 'AS' after WITH(...)");
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->var_name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected variable name after AS in WITH");
    Sync();
    return node;
  }

  // Body block
  ExpectSymbol("{", "expected '{' after WITH ... AS name");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close WITH block");

  return node;
}

std::vector<std::shared_ptr<Expression>> PloyParser::ParseArguments() {
  std::vector<std::shared_ptr<Expression>> args;
  if (IsSymbol(")"))
    return args;

  bool seen_named = false;

  do {
    auto expr = ParseExpression();

    // Check if the parsed expression is a named argument pattern:
    //   name = value  �? BinaryExpression(op="=", left=Identifier, right=value)
    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
      if (bin->op == "=") {
        if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
          auto named = std::make_shared<NamedArgument>();
          named->loc = bin->loc;
          named->name = id->name;
          named->value = bin->right;
          args.push_back(named);
          seen_named = true;
          continue;
        }
      }
    }

    // Positional argument.
    if (seen_named) {
      diagnostics_.Report(expr->loc, "positional argument cannot follow named argument");
    }
    args.push_back(expr);
  } while (MatchSymbol(","));

  return args;
}

// ============================================================================
// STRUCT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseStructDecl() {
  auto node = std::make_shared<StructDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'STRUCT'

  // Struct name
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected struct name after 'STRUCT'");
    Sync();
    return node;
  }

  // Body: { field1: Type1, field2: Type2, ... }
  ExpectSymbol("{", "expected '{' after struct name");

  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    StructDecl::Field field;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      field.name = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected field name in struct");
      Sync();
      break;
    }

    ExpectSymbol(":", "expected ':' after field name");
    field.type = ParseType();
    node->fields.push_back(field);

    // Fields separated by ',' or newlines; trailing comma is allowed
    if (!MatchSymbol(",")) {
      // If no comma, we must be at '}' or have implicit separation
      if (!IsSymbol("}")) {
        // Allow optional semicolons
        MatchSymbol(";");
      }
    }
  }

  ExpectSymbol("}", "expected '}' at end of struct definition");
  return node;
}

// ============================================================================
// MAP_FUNC Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseMapFuncDecl() {
  auto node = std::make_shared<MapFuncDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'MAP_FUNC'

  // Function name
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected function name after 'MAP_FUNC'");
    Sync();
    return node;
  }

  // Parameters
  ExpectSymbol("(", "expected '(' after MAP_FUNC name");
  node->params = ParseParams();
  ExpectSymbol(")", "expected ')' after MAP_FUNC parameters");

  // Return type
  if (MatchSymbol("->")) {
    node->return_type = ParseType();
  }

  // Body
  ExpectSymbol("{", "expected '{' for MAP_FUNC body");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' at end of MAP_FUNC body");

  return node;
}

// ============================================================================
// CONFIG Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseConfigDecl() {
  auto loc = current_.loc;
  Advance(); // consume 'CONFIG'

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "VENV") {
    // CONFIG VENV "path/to/venv";
    // CONFIG VENV python "path/to/venv";
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->manager = VenvConfigDecl::ManagerKind::kVenv;
    Advance(); // consume 'VENV'

    // Optional language specifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->language = current_.lexeme;
      Advance();
    } else {
      // Default to python if no language specified
      node->language = "python";
    }

    // Expect the venv path as a string literal
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->venv_path = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected string path after CONFIG VENV");
    }

    ExpectSymbol(";", "expected ';' after CONFIG VENV");
    return node;
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CONDA") {
    // CONFIG CONDA "env_name";
    // CONFIG CONDA python "env_name";
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->manager = VenvConfigDecl::ManagerKind::kConda;
    Advance(); // consume 'CONDA'

    // Optional language specifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->language = current_.lexeme;
      Advance();
    } else {
      node->language = "python";
    }

    // Expect the conda environment name or path as a string literal
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->venv_path = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc,
                          "expected string environment name or path after CONFIG CONDA");
    }

    ExpectSymbol(";", "expected ';' after CONFIG CONDA");
    return node;
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "UV") {
    // CONFIG UV "path/to/venv";
    // CONFIG UV python "path/to/venv";
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->manager = VenvConfigDecl::ManagerKind::kUv;
    Advance(); // consume 'UV'

    // Optional language specifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->language = current_.lexeme;
      Advance();
    } else {
      node->language = "python";
    }

    // Expect the uv venv path as a string literal
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->venv_path = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected string path after CONFIG UV");
    }

    ExpectSymbol(";", "expected ';' after CONFIG UV");
    return node;
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "PIPENV") {
    // CONFIG PIPENV "path/to/project";
    // CONFIG PIPENV python "path/to/project";
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->manager = VenvConfigDecl::ManagerKind::kPipenv;
    Advance(); // consume 'PIPENV'

    // Optional language specifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->language = current_.lexeme;
      Advance();
    } else {
      node->language = "python";
    }

    // Expect the pipenv project path as a string literal
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->venv_path = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected string path after CONFIG PIPENV");
    }

    ExpectSymbol(";", "expected ';' after CONFIG PIPENV");
    return node;
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "POETRY") {
    // CONFIG POETRY "path/to/project";
    // CONFIG POETRY python "path/to/project";
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->manager = VenvConfigDecl::ManagerKind::kPoetry;
    Advance(); // consume 'POETRY'

    // Optional language specifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->language = current_.lexeme;
      Advance();
    } else {
      node->language = "python";
    }

    // Expect the poetry project path as a string literal
    if (current_.kind == frontends::TokenKind::kString) {
      std::string raw = current_.lexeme;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
      }
      node->venv_path = raw;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected string path after CONFIG POETRY");
    }

    ExpectSymbol(";", "expected ';' after CONFIG POETRY");
    return node;
  }

  diagnostics_.Report(
      current_.loc,
      "expected configuration directive after CONFIG (e.g., VENV, CONDA, UV, PIPENV, POETRY)");
  Sync();
  // Return a dummy statement so parsing can continue
  auto dummy = std::make_shared<ExprStatement>();
  dummy->loc = loc;
  return dummy;
}

// ============================================================================
// CONVERT Expression
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseConvertExpression() {
  auto node = std::make_shared<ConvertExpression>();
  node->loc = current_.loc;
  Advance(); // consume 'CONVERT'

  ExpectSymbol("(", "expected '(' after CONVERT");

  // Source expression
  node->expr = ParseExpression();

  ExpectSymbol(",", "expected ',' between expression and target type in CONVERT");

  // Target type
  node->target_type = ParseType();

  ExpectSymbol(")", "expected ')' after CONVERT");
  return node;
}

// ============================================================================
// List Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseListLiteral() {
  auto node = std::make_shared<ListLiteral>();
  node->loc = current_.loc;
  Advance(); // consume '['

  if (!IsSymbol("]")) {
    node->elements.push_back(ParseExpression());
    while (MatchSymbol(",")) {
      if (IsSymbol("]"))
        break; // trailing comma
      node->elements.push_back(ParseExpression());
    }
  }

  ExpectSymbol("]", "expected ']' after list literal");
  return node;
}

// ============================================================================
// Dict Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseDictLiteral() {
  auto node = std::make_shared<DictLiteral>();
  node->loc = current_.loc;
  Advance(); // consume '{'

  if (!IsSymbol("}")) {
    // Parse first entry: key ':' value
    DictLiteral::Entry entry;
    entry.key = ParseExpression();
    ExpectSymbol(":", "expected ':' between key and value in dict literal");
    entry.value = ParseExpression();
    node->entries.push_back(entry);

    while (MatchSymbol(",")) {
      if (IsSymbol("}"))
        break; // trailing comma
      DictLiteral::Entry e;
      e.key = ParseExpression();
      ExpectSymbol(":", "expected ':' between key and value in dict literal");
      e.value = ParseExpression();
      node->entries.push_back(e);
    }
  }

  ExpectSymbol("}", "expected '}' after dict literal");
  return node;
}

// ============================================================================
// Struct Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseStructLiteral(const std::string &struct_name) {
  auto node = std::make_shared<StructLiteral>();
  node->loc = current_.loc;
  node->struct_name = struct_name;
  Advance(); // consume '{'

  if (!IsSymbol("}")) {
    // Parse first field: name ':' value
    StructLiteral::FieldInit field;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      field.name = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected field name in struct literal");
      Sync();
      return node;
    }
    ExpectSymbol(":", "expected ':' after field name in struct literal");
    field.value = ParseExpression();
    node->fields.push_back(field);

    while (MatchSymbol(",")) {
      if (IsSymbol("}"))
        break; // trailing comma
      StructLiteral::FieldInit f;
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        f.name = current_.lexeme;
        Advance();
      } else {
        diagnostics_.Report(current_.loc, "expected field name in struct literal");
        break;
      }
      ExpectSymbol(":", "expected ':' after field name in struct literal");
      f.value = ParseExpression();
      node->fields.push_back(f);
    }
  }

  ExpectSymbol("}", "expected '}' after struct literal");
  return node;
}

// ============================================================================
// Type Parsing
// ============================================================================

std::shared_ptr<TypeNode> PloyParser::ParseType() {
  return ParseQualifiedOrSimpleType();
}

std::shared_ptr<TypeNode> PloyParser::ParseQualifiedOrSimpleType() {
  if (current_.kind != frontends::TokenKind::kIdentifier &&
      current_.kind != frontends::TokenKind::kKeyword) {
    diagnostics_.Report(current_.loc, "expected type name");
    auto fallback = std::make_shared<SimpleType>();
    fallback->loc = current_.loc;
    fallback->name = "VOID";
    return fallback;
  }

  std::string name = current_.lexeme;
  core::SourceLoc loc = current_.loc;
  Advance();

  // HANDLE<lang::module::ClassName> — typed cross-language object handle.
  // HANDLE is a *contextual* keyword (demand 2026-04-28-9): we recognise
  // any identifier whose case-insensitive spelling is "HANDLE" provided
  // it's followed by '<', so existing variable names like `handle` are
  // still parsed as identifiers in expression positions.  The angle-
  // bracket grammar is restricted to this single contextual spelling to
  // avoid colliding with the comparison operators '<' and '>'.
  if (IEqualsAscii(name, "HANDLE") && IsSymbol("<")) {
    auto ht = std::make_shared<HandleType>();
    ht->loc = loc;
    Advance(); // consume '<'

    if (current_.kind != frontends::TokenKind::kIdentifier &&
        current_.kind != frontends::TokenKind::kKeyword) {
      diagnostics_.Report(current_.loc, "expected language identifier inside HANDLE<...>");
      ExpectSymbol(">", "expected '>' to close HANDLE<...>");
      return ht;
    }
    ht->language = current_.lexeme;
    Advance();
    ExpectSymbol("::", "expected '::' after language inside HANDLE<...>");

    if (current_.kind != frontends::TokenKind::kIdentifier &&
        current_.kind != frontends::TokenKind::kKeyword) {
      diagnostics_.Report(current_.loc, "expected qualified class path inside HANDLE<...>");
      ExpectSymbol(">", "expected '>' to close HANDLE<...>");
      return ht;
    }
    ht->class_path = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      ht->class_path += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        ht->class_path += current_.lexeme;
        Advance();
      }
    }
    ExpectSymbol(">", "expected '>' to close HANDLE<...>");
    return ht;
  }

  // Check for qualified type: lang::type
  if (IsSymbol("::")) {
    auto qt = std::make_shared<QualifiedType>();
    qt->loc = loc;
    qt->language = name;
    Advance();
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
      qt->type_name = current_.lexeme;
      Advance();
      // Deeper qualification
      while (IsSymbol("::")) {
        qt->type_name += "::";
        Advance();
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            current_.kind == frontends::TokenKind::kKeyword) {
          qt->type_name += current_.lexeme;
          Advance();
        }
      }
    }
    return qt;
  }

  // Check for parameterized type: ARRAY[INT] or LIST(FLOAT), DICT(K, V)
  if (IsSymbol("[")) {
    auto pt = std::make_shared<ParameterizedType>();
    pt->loc = loc;
    pt->name = name;
    Advance();
    pt->type_args.push_back(ParseType());
    while (MatchSymbol(",")) {
      pt->type_args.push_back(ParseType());
    }
    ExpectSymbol("]", "expected ']' after type arguments");
    return pt;
  }

  // Support parenthesized parameterized types: LIST(FLOAT), TUPLE(INT, STRING), DICT(STRING, INT),
  // OPTION(T)
  if (IsSymbol("(") && (name == "LIST" || name == "TUPLE" || name == "DICT" || name == "OPTION" ||
                        name == "ARRAY" || name == "MAP" || name == "SET")) {
    auto pt = std::make_shared<ParameterizedType>();
    pt->loc = loc;
    pt->name = name;
    Advance(); // consume '('
    pt->type_args.push_back(ParseType());
    while (MatchSymbol(",")) {
      pt->type_args.push_back(ParseType());
    }
    ExpectSymbol(")", "expected ')' after type arguments");
    return pt;
  }

  // Check for function type: (INT, FLOAT) -> BOOL
  if (name == "(" || IsSymbol("(")) {
    // This is handled at a higher level; fall through to simple type
  }

  auto st = std::make_shared<SimpleType>();
  st->loc = loc;
  st->name = name;
  return st;
}

// ============================================================================
// EXTEND Declaration: EXTEND(language, base_class) AS DerivedName { methods... }
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseExtendDecl() {
  auto node = std::make_shared<ExtendDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'EXTEND'

  ExpectSymbol("(", "expected '(' after EXTEND");

  // Language name
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->language = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected language name in EXTEND");
    Sync();
    return node;
  }
  ExpectSymbol(",", "expected ',' after language in EXTEND");

  // Base class name (possibly qualified: module::ClassName)
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->base_class = current_.lexeme;
    Advance();
    // Handle qualified names: module::ClassName
    while (IsSymbol("::")) {
      node->base_class += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        node->base_class += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected base class name in EXTEND");
    Sync();
    return node;
  }

  ExpectSymbol(")", "expected ')' after EXTEND arguments");

  // AS DerivedName
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "AS") {
    Advance();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      node->derived_name = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc, "expected derived type name after AS in EXTEND");
    }
  } else {
    diagnostics_.Report(current_.loc, "expected 'AS' after EXTEND(...)");
  }

  // Parse body: { FUNC ... FUNC ... }
  ExpectSymbol("{", "expected '{' for EXTEND body");
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "FUNC") {
      node->methods.push_back(ParseFuncDecl());
    } else {
      diagnostics_.Report(current_.loc, "only FUNC declarations are allowed inside EXTEND body");
      Advance();
    }
  }
  ExpectSymbol("}", "expected '}' after EXTEND body");

  return node;
}

// ============================================================================
// CLASS Declaration (demand 2026-04-28-9)
//
// Grammar:
//   CLASS <lang>::<module>::<Name> {
//       METHOD <name> ( <param_list> ) [ -> <type> ] ;
//       ATTR   <name> : <type> ;
//       ...
//   }
//
// Registers an explicit type schema for a foreign-language class so that
// cross-language NEW / METHOD / GET / SET expressions can be statically
// type-checked.  The body intentionally accepts only METHOD and ATTR
// signature lines; full bodies live in the foreign language.
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseClassDecl() {
  auto node = std::make_shared<ClassDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'CLASS' (canonical keyword OR contextual identifier)

  // Header: language identifier, then '::' separated qualified class path.
  if (current_.kind != frontends::TokenKind::kIdentifier &&
      current_.kind != frontends::TokenKind::kKeyword) {
    diagnostics_.Report(current_.loc, "expected language identifier after CLASS");
    Sync();
    return node;
  }
  node->language = current_.lexeme;
  Advance();
  ExpectSymbol("::", "expected '::' after language in CLASS header");

  // Qualified class path (one or more identifiers separated by '::').
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->class_path = current_.lexeme;
    Advance();
    while (IsSymbol("::")) {
      node->class_path += "::";
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        node->class_path += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected qualified class path in CLASS header");
    Sync();
    return node;
  }

  ExpectSymbol("{", "expected '{' to begin CLASS body");

  // Body rows: each line is either METHOD or ATTR.  METHOD is a real
  // canonical keyword (cross-language method-call form predates this
  // demand and is reused here).  ATTR is a *contextual* keyword
  // recognised by case-insensitive identifier match so common variable
  // names like `attr` keep working in user code outside CLASS bodies.
  // Any other token at row start is a diagnostic; the parser advances
  // one token to make progress.
  auto IsMethodRow = [&]() {
    return (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "METHOD") ||
           (current_.kind == frontends::TokenKind::kIdentifier &&
            IEqualsAscii(current_.lexeme, "METHOD"));
  };
  auto IsAttrRow = [&]() {
    return (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "ATTR") ||
           (current_.kind == frontends::TokenKind::kIdentifier &&
            IEqualsAscii(current_.lexeme, "ATTR"));
  };

  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    if (IsMethodRow()) {
      ClassMethodSig m;
      m.loc = current_.loc;
      Advance(); // consume METHOD
      // Method names can clash with existing canonical keywords (e.g.
      // `set`, `get`, `new`).  We accept either an identifier or a
      // keyword so the schema can faithfully describe foreign APIs.
      if (current_.kind != frontends::TokenKind::kIdentifier &&
          current_.kind != frontends::TokenKind::kKeyword) {
        diagnostics_.Report(current_.loc, "expected method name after METHOD in CLASS body");
        Sync();
        continue;
      }
      m.name = current_.lexeme;
      Advance();

      ExpectSymbol("(", "expected '(' after method name");
      // Parameter list: name : type, ... (names are optional but recommended).
      if (!IsSymbol(")")) {
        do {
          std::string pname;
          if (current_.kind == frontends::TokenKind::kIdentifier) {
            pname = current_.lexeme;
            Advance();
            ExpectSymbol(":", "expected ':' after parameter name in METHOD signature");
          }
          auto pty = ParseType();
          m.params.emplace_back(std::move(pname), std::move(pty));
        } while (MatchSymbol(","));
      }
      ExpectSymbol(")", "expected ')' to close METHOD parameter list");

      // Optional return type after '->'.  Absence implies VOID.
      if (IsSymbol("->")) {
        Advance();
        m.return_type = ParseType();
      }
      ExpectSymbol(";", "expected ';' after METHOD signature");
      node->methods.push_back(std::move(m));
      continue;
    }

    if (IsAttrRow()) {
      ClassAttrSig a;
      a.loc = current_.loc;
      Advance(); // consume ATTR
      // Same lenient rule as for methods: accept identifiers and
      // keywords so foreign attribute names like `type`, `class`,
      // `new` can be modelled.
      if (current_.kind != frontends::TokenKind::kIdentifier &&
          current_.kind != frontends::TokenKind::kKeyword) {
        diagnostics_.Report(current_.loc, "expected attribute name after ATTR in CLASS body");
        Sync();
        continue;
      }
      a.name = current_.lexeme;
      Advance();
      ExpectSymbol(":", "expected ':' after attribute name");
      a.type = ParseType();
      ExpectSymbol(";", "expected ';' after ATTR declaration");
      node->attrs.push_back(std::move(a));
      continue;
    }

    diagnostics_.Report(current_.loc,
                        "only METHOD and ATTR declarations are allowed inside CLASS body");
    Advance();
  }

  ExpectSymbol("}", "expected '}' to close CLASS body");
  return node;
}

// ============================================================================
// Type alias and compile-time constant declarations (demand 2026-04-28-7)
// ============================================================================

// Grammar:
//   TYPE <ident> = <type_expr> ;
//
// The right-hand side is parsed by `ParseType`, so any type expression that
// can appear in a parameter or LET position is also legal here (including
// qualified types like `python::numpy::ndarray` and parameterised types
// like `LIST(I32)`).  Forward references to later-declared types are not
// supported in this version (mirrors C++ `using`).
std::shared_ptr<Statement> PloyParser::ParseTypeAliasDecl() {
  auto node = std::make_shared<TypeAliasDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'TYPE'

  if (current_.kind != frontends::TokenKind::kIdentifier) {
    diagnostics_.Report(current_.loc, "expected identifier after TYPE");
    Sync();
    return node;
  }
  node->name = current_.lexeme;
  Advance();

  ExpectSymbol("=", "expected '=' after TYPE alias name");
  node->aliased_type = ParseType();
  ExpectSymbol(";", "expected ';' after TYPE alias declaration");
  return node;
}

// Grammar:
//   CONST <ident> : <type_expr> = <const_expr> ;
//
// The type annotation is mandatory.  The initializer must be foldable by
// the sema's constant-evaluation pass; the parser does not validate
// foldability — it only constructs the AST.
std::shared_ptr<Statement> PloyParser::ParseConstDecl() {
  auto node = std::make_shared<ConstDecl>();
  node->loc = current_.loc;
  Advance(); // consume 'CONST'

  if (current_.kind != frontends::TokenKind::kIdentifier) {
    diagnostics_.Report(current_.loc, "expected identifier after CONST");
    Sync();
    return node;
  }
  node->name = current_.lexeme;
  Advance();

  ExpectSymbol(":", "CONST declaration requires an explicit ': <type>' annotation");
  node->type = ParseType();
  ExpectSymbol("=", "CONST declaration requires an initializer");
  node->value = ParseExpression();
  ExpectSymbol(";", "expected ';' after CONST declaration");
  return node;
}

// ============================================================================
// Language-version pinning
// ============================================================================

// Helper: parse `(lang = ver, lang2 = ver2, ...)` into a flat pin list.
// `current_` must point at the opening '('.
void PloyParser::ParseLangPinList(std::vector<WithLangBlock::Pin> &out_pins) {
  ExpectSymbol("(", "expected '(' to begin language pin list");
  if (IsSymbol(")")) {
    Advance();
    diagnostics_.Report(current_.loc, "language pin list must contain at least one entry");
    return;
  }
  do {
    WithLangBlock::Pin pin;
    if (current_.kind != frontends::TokenKind::kIdentifier &&
        current_.kind != frontends::TokenKind::kKeyword) {
      diagnostics_.Report(current_.loc, "expected language name in language pin list");
      Sync();
      return;
    }
    pin.language = current_.lexeme;
    Advance();
    ExpectSymbol("=", "expected '=' between language and version");
    // Version token: identifier ("c++23" comes through as 'c' '+' '+' so we
    // accept either an identifier OR a string literal OR a number; we then
    // gather any trailing punctuation that forms a contiguous version token.
    if (current_.kind == frontends::TokenKind::kString) {
      // The ploy lexer keeps the surrounding quote characters as part of
      // the string lexeme.  Version pins are pure ASCII tokens, so strip
      // the outer quotes so downstream sema gets `c++23` rather than
      // `"c++23"`.
      pin.version = current_.lexeme;
      if (pin.version.size() >= 2 && pin.version.front() == '"' &&
          pin.version.back() == '"') {
        pin.version = pin.version.substr(1, pin.version.size() - 2);
      }
      Advance();
    } else if (current_.kind == frontends::TokenKind::kIdentifier ||
               current_.kind == frontends::TokenKind::kKeyword ||
               current_.kind == frontends::TokenKind::kNumber) {
      pin.version = current_.lexeme;
      Advance();
      // Consume contiguous "+" "+" "23" / ".11" tokens that the lexer split.
      while (current_.kind == frontends::TokenKind::kSymbol &&
             (current_.lexeme == "+" || current_.lexeme == "." ||
              current_.lexeme == "-")) {
        pin.version += current_.lexeme;
        Advance();
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            current_.kind == frontends::TokenKind::kKeyword ||
            current_.kind == frontends::TokenKind::kNumber) {
          pin.version += current_.lexeme;
          Advance();
        }
      }
    } else {
      diagnostics_.Report(current_.loc, "expected version token after '='");
      Sync();
      return;
    }
    out_pins.push_back(std::move(pin));
  } while (MatchSymbol(","));
  ExpectSymbol(")", "expected ')' to close language pin list");
}

// LANG <lang> = <version>;   (top-level / statement form)
std::shared_ptr<Statement> PloyParser::ParseLangPragma() {
  auto node = std::make_shared<LangPragma>();
  node->loc = current_.loc;
  Advance(); // consume 'LANG'
  if (current_.kind != frontends::TokenKind::kIdentifier &&
      current_.kind != frontends::TokenKind::kKeyword) {
    diagnostics_.Report(current_.loc, "expected language name after LANG");
    Sync();
    return node;
  }
  node->language = current_.lexeme;
  Advance();
  ExpectSymbol("=", "expected '=' after language name in LANG pragma");
  if (current_.kind == frontends::TokenKind::kString) {
    // Strip surrounding quotes the lexer preserves on string literals.
    node->version = current_.lexeme;
    if (node->version.size() >= 2 && node->version.front() == '"' &&
        node->version.back() == '"') {
      node->version = node->version.substr(1, node->version.size() - 2);
    }
    Advance();
  } else if (current_.kind == frontends::TokenKind::kIdentifier ||
             current_.kind == frontends::TokenKind::kKeyword ||
             current_.kind == frontends::TokenKind::kNumber) {
    node->version = current_.lexeme;
    Advance();
    while (current_.kind == frontends::TokenKind::kSymbol &&
           (current_.lexeme == "+" || current_.lexeme == "." ||
            current_.lexeme == "-")) {
      node->version += current_.lexeme;
      Advance();
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword ||
          current_.kind == frontends::TokenKind::kNumber) {
        node->version += current_.lexeme;
        Advance();
      }
    }
  } else {
    diagnostics_.Report(current_.loc, "expected version token after '='");
    Sync();
    return node;
  }
  ExpectSymbol(";", "expected ';' after LANG pragma");
  return node;
}

// WITH LANG (lang = ver, ...) { body }
// On entry, `current_` points at the LANG keyword (WITH already consumed).
std::shared_ptr<Statement> PloyParser::ParseWithLangBlock() {
  auto node = std::make_shared<WithLangBlock>();
  node->loc = current_.loc;
  Advance(); // consume 'LANG'
  ParseLangPinList(node->pins);
  ExpectSymbol("{", "expected '{' after WITH LANG (...)");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close WITH LANG block");
  return node;
}

// @LANG(lang = ver, ...) <statement>
std::shared_ptr<Statement> PloyParser::ParseLangAnnotation() {
  auto node = std::make_shared<LangAnnotation>();
  node->loc = current_.loc;
  // Consume '@'.
  if (!(current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "@")) {
    diagnostics_.Report(current_.loc, "internal: ParseLangAnnotation called without leading '@'");
    Sync();
    return node;
  }
  Advance(); // consume '@'
  if (!(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "LANG")) {
    diagnostics_.Report(current_.loc, "expected 'LANG' after '@'");
    Sync();
    return node;
  }
  Advance(); // consume 'LANG'
  ParseLangPinList(node->pins);
  // Body is a single subsequent statement.
  node->target = ParseStatement();
  return node;
}

} // namespace polyglot::ploy
