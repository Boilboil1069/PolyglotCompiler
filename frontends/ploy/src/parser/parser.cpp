/**
 * @file     parser.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <stdexcept>

#include "frontends/ploy/include/ploy_config_registry.h"
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
  if (peeked_.has_value()) {
    current_ = *peeked_;
    peeked_.reset();
  } else {
    current_ = lexer_.NextToken();
  }
}

// Returns the token immediately after `current_` without consuming it.
// Lazily fills the one-token lookahead buffer on first call between two
// Advance()s.  Subsequent Advance() will promote the peeked token into
// `current_` and clear the buffer.
const frontends::Token &PloyParser::Peek() {
  if (!peeked_.has_value()) {
    peeked_ = lexer_.NextToken();
  }
  return *peeked_;
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
  // `@LANG(...)` annotation can prefix any top-level statement.  All other
  // `@<name>(...)` annotations are visibility/attribute prefixes (since
  // v1.16.0) and are routed through ParseAttributesAndVisibility below.
  if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "@" &&
      Peek().kind == frontends::TokenKind::kKeyword && Peek().lexeme == "LANG") {
    module_->declarations.push_back(ParseLangAnnotation());
    return;
  }

  // Visibility / attribute prefix (since v1.16.0).  Captures any `@name(...)`
  // annotations and at most one `PUB` / `PRIVATE` keyword.  Attached below
  // to FUNC / ASYNC FUNC / STRUCT declarations; rejected with a diagnostic
  // when followed by any other top-level form.
  std::vector<Attribute> attrs;
  Visibility vis = Visibility::kPrivate;
  bool has_explicit_vis = false;
  core::SourceLoc prefix_loc = current_.loc;
  ParseAttributesAndVisibility(attrs, vis, has_explicit_vis);

  if (current_.kind == frontends::TokenKind::kKeyword) {
    const std::string &kw = current_.lexeme;
    // Reject prefixes on declaration kinds that do not yet support them.
    if ((!attrs.empty() || has_explicit_vis) && kw != "FUNC" && kw != "ASYNC" &&
        kw != "STRUCT") {
      diagnostics_.Report(prefix_loc,
                          "PUB / PRIVATE / @attribute prefix is only allowed on FUNC, "
                          "ASYNC FUNC, or STRUCT declarations");
    }
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
      auto stmt = ParseFuncDecl();
      if (auto fn = std::dynamic_pointer_cast<FuncDecl>(stmt)) {
        fn->visibility = vis;
        fn->visibility_explicit = has_explicit_vis;
        fn->attributes = std::move(attrs);
      }
      module_->declarations.push_back(std::move(stmt));
      return;
    }
    if (kw == "ASYNC") {
      auto stmt = ParseAsyncFuncDecl();
      if (auto fn = std::dynamic_pointer_cast<FuncDecl>(stmt)) {
        fn->visibility = vis;
        fn->visibility_explicit = has_explicit_vis;
        fn->attributes = std::move(attrs);
      }
      module_->declarations.push_back(std::move(stmt));
      return;
    }
    if (kw == "STRUCT") {
      auto stmt = ParseStructDecl();
      if (auto sd = std::dynamic_pointer_cast<StructDecl>(stmt)) {
        sd->visibility = vis;
        sd->visibility_explicit = has_explicit_vis;
        sd->attributes = std::move(attrs);
      }
      module_->declarations.push_back(std::move(stmt));
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
    if (!attrs.empty() || has_explicit_vis) {
      diagnostics_.Report(prefix_loc,
                          "PUB / PRIVATE / @attribute prefix is only allowed on FUNC, "
                          "ASYNC FUNC, or STRUCT declarations");
    }
    module_->declarations.push_back(ParseClassDecl());
    return;
  }
  if (!attrs.empty() || has_explicit_vis) {
    diagnostics_.Report(prefix_loc,
                        "PUB / PRIVATE / @attribute prefix is only allowed on FUNC, "
                        "ASYNC FUNC, or STRUCT declarations");
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
  // Pull any `///` doc-comment lines that the lexer accumulated
  // immediately above this declaration (since v1.18.0).
  node->doc_comment = lexer_.TakePendingDoc();
  Advance(); // consume 'FUNC'

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->name = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected function name");
    Sync();
    return node;
  }

  // Optional generic parameter list `<T: Bound, U>` (since v1.15.0).
  if (IsSymbol("<")) {
    node->type_params = ParseTypeParams();
  }

  ExpectSymbol("(", "expected '(' after function name");
  node->params = ParseParams();
  ExpectSymbol(")", "expected ')' after parameters");

  // Optional return type: -> TYPE
  if (IsSymbol("->")) {
    Advance();
    node->return_type = ParseType();
  }

  // Optional WHERE clause that augments existing type-parameter bounds
  // (since v1.15.0).
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "WHERE") {
    ParseWhereClause(node->type_params);
  }

  ExpectSymbol("{", "expected '{' for function body");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close function");

  return node;
}

// ASYNC FUNC name(params) -> return_type { ... }
//
// Consumes the leading `ASYNC` keyword then defers to `ParseFuncDecl`
// for the actual signature/body grammar, marking the resulting node so
// downstream phases can wrap the return type as `Future<T>` and emit
// cooperative-scheduler intrinsics (since v1.14.0).
std::shared_ptr<Statement> PloyParser::ParseAsyncFuncDecl() {
  auto loc = current_.loc;
  Advance(); // consume 'ASYNC'

  if (!(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "FUNC")) {
    diagnostics_.Report(current_.loc, "expected 'FUNC' after 'ASYNC'");
    Sync();
    auto node = std::make_shared<FuncDecl>();
    node->loc = loc;
    node->is_async = true;
    return node;
  }

  auto stmt = ParseFuncDecl();
  if (auto fn = std::dynamic_pointer_cast<FuncDecl>(stmt)) {
    fn->is_async = true;
    fn->loc = loc;
  }
  return stmt;
}

// ============================================================================
// Generics — type parameter list and WHERE clause
// ============================================================================

// Parses `<T: Bound1+Bound2, U>` from the position of the opening `<`.
// Bounds are recorded as plain identifier names; sema validates each
// against the built-in trait registry (since v1.15.0).
std::vector<FuncDecl::TypeParam> PloyParser::ParseTypeParams() {
  std::vector<FuncDecl::TypeParam> params;
  if (!IsSymbol("<")) return params;
  Advance(); // consume '<'

  if (IsSymbol(">")) {
    diagnostics_.Report(current_.loc, "type parameter list '<>' must declare at least one parameter");
    Advance();
    return params;
  }

  do {
    FuncDecl::TypeParam tp;
    tp.loc = current_.loc;
    if (current_.kind != frontends::TokenKind::kIdentifier) {
      diagnostics_.Report(current_.loc, "expected type parameter name");
      Sync();
      break;
    }
    tp.name = current_.lexeme;
    Advance();

    // Optional `: Bound1 + Bound2 + ...`
    if (IsSymbol(":")) {
      Advance();
      do {
        if (current_.kind != frontends::TokenKind::kIdentifier &&
            current_.kind != frontends::TokenKind::kKeyword) {
          diagnostics_.Report(current_.loc, "expected bound name after ':'");
          break;
        }
        tp.bounds.push_back(current_.lexeme);
        Advance();
      } while (MatchSymbol("+"));
    }

    params.push_back(std::move(tp));
  } while (MatchSymbol(","));

  ExpectSymbol(">", "expected '>' to close type parameter list");
  return params;
}

// Parses `WHERE T: Bound1 + Bound2, U: Bound3` and merges each constraint
// into the bounds vector of the matching parameter.  Constraints that
// reference an unknown parameter are reported here so sema only sees a
// well-formed list.
void PloyParser::ParseWhereClause(std::vector<FuncDecl::TypeParam> &type_params) {
  Advance(); // consume 'WHERE'

  do {
    if (current_.kind != frontends::TokenKind::kIdentifier) {
      diagnostics_.Report(current_.loc, "expected type parameter name in WHERE clause");
      break;
    }
    std::string name = current_.lexeme;
    auto loc = current_.loc;
    Advance();

    FuncDecl::TypeParam *target = nullptr;
    for (auto &tp : type_params) {
      if (tp.name == name) {
        target = &tp;
        break;
      }
    }
    if (target == nullptr) {
      diagnostics_.Report(loc, "WHERE clause references unknown type parameter '" + name + "'");
    }

    ExpectSymbol(":", "expected ':' after type parameter in WHERE clause");
    do {
      if (current_.kind != frontends::TokenKind::kIdentifier &&
          current_.kind != frontends::TokenKind::kKeyword) {
        diagnostics_.Report(current_.loc, "expected bound name after ':' in WHERE clause");
        break;
      }
      if (target != nullptr) target->bounds.push_back(current_.lexeme);
      Advance();
    } while (MatchSymbol("+"));
  } while (MatchSymbol(","));
}

// ============================================================================
// Parameters
// ============================================================================

std::vector<FuncDecl::Param> PloyParser::ParseParams() {
  std::vector<FuncDecl::Param> params;
  if (IsSymbol(")"))
    return params;

  // Track whether a defaulted parameter has been seen so we can reject any
  // following non-defaulted parameter at parse time (matches the language
  // rule that required parameters must precede defaulted ones).
  bool seen_default = false;

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

    // Optional default value: `= <expr>`.
    if (IsSymbol("=")) {
      Advance();
      param.default_value = ParseExpression();
      seen_default = true;
    } else if (seen_default) {
      diagnostics_.Report(param.type ? param.type->loc : current_.loc,
                          "required parameter '" + param.name +
                              "' cannot follow a parameter with a default value");
    }

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
    if (kw == "ASYNC")
      return ParseAsyncFuncDecl();
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
    if (kw == "TRY") {
      return ParseTryStatement();
    }
    if (kw == "THROW") {
      return ParseThrowStatement();
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
  // Attach pending `///` doc lines (since v1.18.0).
  node->doc_comment = lexer_.TakePendingDoc();

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
  core::SourceLoc if_loc = current_.loc;
  Advance(); // consume 'IF'

  // IF LET destructuring (since v1.18.0).  Lexed as a separate AST node
  // so sema can introduce the bound names into the THEN-body's scope
  // without colliding with the boolean-condition path.  We do not back-
  // track over IF; once we see `LET` here we are committed.
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "LET") {
    return ParseIfLetStatement(if_loc);
  }

  auto node = std::make_shared<IfStatement>();
  node->loc = if_loc;

  // The expression-grammar already accepts `(cond)` as a grouped
  // expression, so `IF (cond) { … }` parses identically to
  // `IF cond { … }` (since v1.18.0).
  node->condition = ParseExpression();
  ExpectSymbol("{", "expected '{' after IF condition");
  node->then_body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close IF body");

  // Optional ELSE / ELSE IF
  if (MatchKeyword("ELSE")) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "IF") {
      // ELSE IF nests as a single statement in else_body
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

std::shared_ptr<Statement> PloyParser::ParseIfLetStatement(core::SourceLoc if_loc) {
  auto node = std::make_shared<IfLetStatement>();
  node->loc = if_loc;
  Advance(); // consume 'LET'

  // Constructor: must be an identifier (`Some` or `None` for the MVP).
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    node->ctor = current_.lexeme;
    Advance();
  } else {
    diagnostics_.Report(current_.loc, "expected constructor name (Some / None) after IF LET");
    Sync();
    return node;
  }

  // Bindings: `Some(x)` requires one; `None` takes no arguments.
  if (MatchSymbol("(")) {
    if (!IsSymbol(")")) {
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->bindings.push_back(current_.lexeme);
        Advance();
      } else {
        diagnostics_.Report(current_.loc, "expected binding name in IF LET pattern");
      }
      while (MatchSymbol(",")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
          node->bindings.push_back(current_.lexeme);
          Advance();
        } else {
          diagnostics_.Report(current_.loc, "expected binding name in IF LET pattern");
          break;
        }
      }
    }
    ExpectSymbol(")", "expected ')' to close IF LET pattern");
  }

  ExpectSymbol("=", "expected '=' after IF LET pattern");
  node->scrutinee = ParseExpression();
  ExpectSymbol("{", "expected '{' after IF LET scrutinee");
  node->then_body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close IF LET body");

  if (MatchKeyword("ELSE")) {
    ExpectSymbol("{", "expected '{' after ELSE");
    node->else_body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close ELSE body");
  }
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseWhileStatement() {
  auto node = std::make_shared<WhileStatement>();
  node->loc = current_.loc;
  Advance(); // consume 'WHILE'

  // `WHILE (cond) { … }` parses identically because parens are part of
  // the expression grammar (since v1.18.0).
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

  // Optional outer parens: `FOR (i IN xs) { … }` (since v1.18.0).
  bool wrapped = MatchSymbol("(");

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

  if (wrapped) {
    ExpectSymbol(")", "expected ')' to close FOR header");
  }

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
      // Pattern grammar (demand 2026-04-28-10): an or-pattern, optionally
      // followed by an `IF expr` guard.  See ParsePattern() for the full
      // sub-grammar.
      match_case.pattern = ParsePattern();
      if (MatchKeyword("IF")) {
        match_case.guard = ParseExpression();
      }
    } else if (MatchKeyword("DEFAULT")) {
      match_case.pattern = nullptr; // default case
    } else {
      diagnostics_.Report(current_.loc, "expected CASE or DEFAULT in MATCH");
      Sync();
      continue;
    }

    // Optional arm separator. Both `->` and `=>` are accepted between the
    // pattern (or guard) and the arm body so source written against any of
    // the historically-published surface syntaxes parses identically.  The
    // canonical form documented in the user-facing material omits the
    // arrow; the arrow is preserved here for backward compatibility.
    if (!MatchSymbol("->")) {
      MatchSymbol("=>");
    }

    ExpectSymbol("{", "expected '{' after CASE/DEFAULT");
    match_case.body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close CASE/DEFAULT");

    node->cases.push_back(std::move(match_case));
  }

  ExpectSymbol("}", "expected '}' to close MATCH");
  return node;
}

// Top-level pattern: an or-pattern of one or more primary patterns.
//   pattern  ::= pattern_primary ('|' pattern_primary)*
std::shared_ptr<Pattern> PloyParser::ParsePattern() {
  auto first = ParsePatternPrimary();
  if (!IsSymbol("|")) {
    return first;
  }
  auto node = std::make_shared<OrPattern>();
  node->loc = first ? first->loc : current_.loc;
  node->alternatives.push_back(first);
  while (MatchSymbol("|")) {
    node->alternatives.push_back(ParsePatternPrimary());
  }
  return node;
}

// Primary pattern. Order matters because some prefixes overlap.
//   pattern_primary ::= '_'
//                     | literal_or_range
//                     | identifier_pattern
//                     | tuple_or_grouped
namespace {
// Parse a literal token in a pattern position. Recognises integer / float /
// string / TRUE / FALSE / NULL plus an optional unary minus on numerics so
// patterns like `CASE -1` parse cleanly.
std::shared_ptr<Literal> ParsePatternLiteralFrom(PloyParser & /*self*/) {
  // Implemented inline below via lambda capture; this NS exists only to
  // hide a couple of small helpers without cluttering the class header.
  return nullptr;
}
} // namespace

std::shared_ptr<Pattern> PloyParser::ParsePatternPrimary() {
  // Local helper: parse a literal token (with optional leading '-') and
  // return it as a shared Literal; nullptr if the current token does not
  // begin a literal.
  auto try_parse_literal = [this]() -> std::shared_ptr<Literal> {
    bool negate = false;
    core::SourceLoc start = current_.loc;
    if (IsSymbol("-")) {
      negate = true;
      Advance();
    }
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "TRUE" || current_.lexeme == "FALSE")) {
      auto lit = std::make_shared<Literal>();
      lit->loc = start;
      lit->kind = Literal::Kind::kBool;
      lit->value = (current_.lexeme == "TRUE") ? "true" : "false";
      Advance();
      return lit;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NULL") {
      auto lit = std::make_shared<Literal>();
      lit->loc = start;
      lit->kind = Literal::Kind::kNull;
      lit->value = "null";
      Advance();
      return lit;
    }
    if (current_.kind == frontends::TokenKind::kNumber) {
      auto lit = std::make_shared<Literal>();
      lit->loc = start;
      bool has_dot = current_.lexeme.find('.') != std::string::npos;
      bool has_exp = current_.lexeme.find('e') != std::string::npos ||
                     current_.lexeme.find('E') != std::string::npos;
      lit->kind = (has_dot || has_exp) ? Literal::Kind::kFloat : Literal::Kind::kInteger;
      lit->value = (negate ? "-" : "") + current_.lexeme;
      Advance();
      return lit;
    }
    if (current_.kind == frontends::TokenKind::kString) {
      auto lit = std::make_shared<Literal>();
      lit->loc = start;
      lit->kind = Literal::Kind::kString;
      lit->value = current_.lexeme;
      Advance();
      return lit;
    }
    return nullptr;
  };

  core::SourceLoc loc = current_.loc;

  // Wildcard `_` is lexed as an identifier.
  if (current_.kind == frontends::TokenKind::kIdentifier && current_.lexeme == "_") {
    Advance();
    auto node = std::make_shared<WildcardPattern>();
    node->loc = loc;
    return node;
  }

  // Tuple or grouped: `(p)` is just `p`; `(p1, p2, ...)` is a tuple.
  if (IsSymbol("(")) {
    Advance();
    if (IsSymbol(")")) {
      // Empty tuple pattern.
      Advance();
      auto node = std::make_shared<TuplePattern>();
      node->loc = loc;
      return node;
    }
    std::vector<std::shared_ptr<Pattern>> elems;
    elems.push_back(ParsePattern());
    bool is_tuple = false;
    while (MatchSymbol(",")) {
      is_tuple = true;
      if (IsSymbol(")")) break; // trailing comma allowed
      elems.push_back(ParsePattern());
    }
    ExpectSymbol(")", "expected ')' to close tuple pattern");
    if (!is_tuple) {
      return elems.front();
    }
    auto node = std::make_shared<TuplePattern>();
    node->loc = loc;
    node->elements = std::move(elems);
    return node;
  }

  // Identifier-headed forms: bare identifier, `name @ sub`, `name : Type`,
  // `Name(...)` constructor, `Name { ... }` struct.  We accept either a
  // raw identifier or a few canonical keywords (`Some`/`None` etc. are
  // user-space identifiers, but this branch also has to cope with
  // `TRUE`/`FALSE` falling through from try_parse_literal above).
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    std::string name = current_.lexeme;
    core::SourceLoc id_loc = current_.loc;
    Advance();

    // Constructor pattern: Name(p1, p2, ...)  — used for OPTION's Some/None
    // and any nominal constructor.
    if (IsSymbol("(")) {
      Advance();
      auto node = std::make_shared<ConstructorPattern>();
      node->loc = id_loc;
      node->name = name;
      if (!IsSymbol(")")) {
        node->args.push_back(ParsePattern());
        while (MatchSymbol(",")) {
          if (IsSymbol(")")) break;
          node->args.push_back(ParsePattern());
        }
      }
      ExpectSymbol(")", "expected ')' to close constructor pattern");
      return node;
    }

    // Struct pattern: Name { f1, f2: sub, .. }
    //
    // Lookahead disambiguation: a bare identifier followed by `{` could
    // either be a struct pattern (`Point { x, y }`) or a plain identifier
    // pattern that captures the scrutinee, with `{` opening the arm body
    // (`CASE None { RETURN ...; }`).  We peek past the `{` and only
    // commit to the struct-pattern branch when the next token is a
    // syntactic field-list element: an identifier, `..`, or the
    // immediate closer `}`.  Anything else (statement-starting keyword,
    // literal, expression token, ...) means the `{` opens the arm body
    // and we fall through to the identifier-pattern branch below.
    if (IsSymbol("{")) {
      const auto &next = Peek();
      bool looks_like_struct =
          (next.kind == frontends::TokenKind::kIdentifier) ||
          (next.kind == frontends::TokenKind::kSymbol &&
           (next.lexeme == ".." || next.lexeme == "}"));
      if (looks_like_struct) {
        Advance();
        auto node = std::make_shared<StructPattern>();
        node->loc = id_loc;
        node->struct_name = name;
        while (!IsSymbol("}") &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
          if (IsSymbol("..")) {
            Advance();
            node->has_rest = true;
            MatchSymbol(","); // trailing comma after `..` allowed
            break;
          }
          if (current_.kind != frontends::TokenKind::kIdentifier) {
            diagnostics_.Report(current_.loc,
                                "expected field name in struct pattern, got '" +
                                    current_.lexeme + "'");
            Sync();
            break;
          }
          StructPattern::FieldPattern fp;
          fp.name = current_.lexeme;
          Advance();
          if (MatchSymbol(":")) {
            fp.sub = ParsePattern();
          }
          node->fields.push_back(std::move(fp));
          if (!MatchSymbol(",")) break;
        }
        ExpectSymbol("}", "expected '}' to close struct pattern");
        return node;
      }
    }

    // Binding pattern: name @ sub
    if (IsSymbol("@")) {
      Advance();
      auto node = std::make_shared<BindingPattern>();
      node->loc = id_loc;
      node->name = name;
      node->sub = ParsePatternPrimary();
      return node;
    }

    // Type-guard pattern: name : Type
    if (IsSymbol(":")) {
      Advance();
      auto node = std::make_shared<TypePattern>();
      node->loc = id_loc;
      node->name = name;
      node->type_node = ParseType();
      return node;
    }

    // Plain identifier: bare bind.  One historical exception: the unit
    // variant `None` of the built-in `OPTION` type is universally written
    // without parentheses (`CASE None { ... }`), to match the way the
    // value-side spells it (`RETURN None;`).  Promote that single name to
    // a zero-arg ConstructorPattern so the OPTION exhaustiveness checker
    // sees the case as a None-arm rather than as an irrefutable bind.
    if (name == "None") {
      auto node = std::make_shared<ConstructorPattern>();
      node->loc = id_loc;
      node->name = name;
      return node;
    }
    auto node = std::make_shared<IdentifierPattern>();
    node->loc = id_loc;
    node->name = name;
    return node;
  }

  // Literal-headed forms (with optional negative sign, see helper). These
  // may participate in a range pattern: `lo .. hi` or `lo ..= hi`.
  if (auto lit = try_parse_literal()) {
    if (IsSymbol("..") || IsSymbol("..=")) {
      bool inclusive = IsSymbol("..=");
      Advance();
      auto rhs = try_parse_literal();
      if (!rhs) {
        diagnostics_.Report(current_.loc,
                            "expected literal after range operator in pattern, got '" +
                                current_.lexeme + "'");
        rhs = lit; // fall back so subsequent recovery does not crash
      }
      auto node = std::make_shared<RangePattern>();
      node->loc = loc;
      node->low = lit;
      node->high = rhs;
      node->inclusive = inclusive;
      return node;
    }
    auto node = std::make_shared<LiteralPattern>();
    node->loc = loc;
    node->literal = lit;
    return node;
  }

  diagnostics_.Report(current_.loc,
                      "expected pattern, got '" + current_.lexeme + "'");
  Advance(); // consume offending token to make progress
  auto fallback = std::make_shared<WildcardPattern>();
  fallback->loc = loc;
  return fallback;
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

// ----------------------------------------------------------------------------
// Structured exception handling (since v1.13.0).
// ----------------------------------------------------------------------------

std::shared_ptr<Statement> PloyParser::ParseThrowStatement() {
  auto node = std::make_shared<ThrowStatement>();
  node->loc = current_.loc;
  ExpectKeyword("THROW", "expected 'THROW'");
  node->value = ParseExpression();
  ExpectSymbol(";", "expected ';' after THROW expression");
  return node;
}

std::shared_ptr<Statement> PloyParser::ParseTryStatement() {
  auto node = std::make_shared<TryStatement>();
  node->loc = current_.loc;
  ExpectKeyword("TRY", "expected 'TRY'");
  ExpectSymbol("{", "expected '{' after TRY");
  node->body = ParseBlockBody();
  ExpectSymbol("}", "expected '}' to close TRY block");

  // Zero or more CATCH clauses.  At least one of CATCH/FINALLY is
  // required to make the construct meaningful.
  while (current_.kind == frontends::TokenKind::kKeyword &&
         current_.lexeme == "CATCH") {
    TryStatement::CatchClause clause;
    clause.loc = current_.loc;
    Advance();
    ExpectSymbol("(", "expected '(' after CATCH");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      clause.var_name = current_.lexeme;
      Advance();
    } else {
      diagnostics_.Report(current_.loc,
                          "expected identifier for caught Error binding");
      Sync();
      return node;
    }
    if (IsSymbol(":")) {
      Advance();
      clause.var_type = ParseType();
    } else {
      // Implicit `Error` type when the user omits the annotation.
      auto t = std::make_shared<SimpleType>();
      t->name = "Error";
      clause.var_type = t;
    }
    ExpectSymbol(")", "expected ')' after CATCH binding");
    ExpectSymbol("{", "expected '{' to open CATCH body");
    clause.body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close CATCH body");
    node->catches.push_back(std::move(clause));
  }

  // Optional FINALLY clause.
  if (current_.kind == frontends::TokenKind::kKeyword &&
      current_.lexeme == "FINALLY") {
    Advance();
    ExpectSymbol("{", "expected '{' after FINALLY");
    node->finally_body = ParseBlockBody();
    node->has_finally = true;
    ExpectSymbol("}", "expected '}' to close FINALLY block");
  }

  if (node->catches.empty() && !node->has_finally) {
    diagnostics_.Report(node->loc,
                        "TRY must be followed by at least one CATCH or a FINALLY clause");
  }
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
  // AWAIT <expr>: suspend the surrounding ASYNC function until the
  // operand future resolves.  Sema enforces that AWAIT only appears
  // inside an ASYNC FUNC body (since v1.14.0).
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "AWAIT") {
    auto node = std::make_shared<AwaitExpression>();
    node->loc = current_.loc;
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

  // String literal (regular / raw / multiline / template — see helper).
  if (current_.kind == frontends::TokenKind::kString) {
    core::SourceLoc s_loc = current_.loc;
    std::string s_lex = current_.lexeme;
    Advance();
    return BuildStringExpression(s_lex, s_loc);
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
    // Look ahead for the canonical `name: value` named-argument form.  We
    // only consume the colon form when the next two tokens are exactly
    // <Identifier> <:>, so that nested type annotations inside expressions
    // (e.g. struct literals) cannot be mis-parsed as named args.
    if (current_.kind == frontends::TokenKind::kIdentifier) {
      const auto &next = Peek();
      if (next.kind == frontends::TokenKind::kSymbol && next.lexeme == ":") {
        auto label = current_;
        Advance(); // consume identifier
        Advance(); // consume ':'
        auto value = ParseExpression();
        auto named = std::make_shared<NamedArgument>();
        named->loc = label.loc;
        named->name = label.lexeme;
        named->value = value;
        args.push_back(named);
        seen_named = true;
        continue;
      }
    }

    auto expr = ParseExpression();

    // Legacy named-argument pattern:
    //   name = value  ->  BinaryExpression(op="=", left=Identifier, right=value)
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
  // Attach pending `///` doc lines (since v1.18.0).
  node->doc_comment = lexer_.TakePendingDoc();
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

  // Optional generic parameter list `<A, B>` (since v1.15.0).
  if (IsSymbol("<")) {
    node->type_params = ParseTypeParams();
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

  // ------------------------------------------------------------------
  // Helper: extract the unquoted body of a string-literal token.
  // ------------------------------------------------------------------
  auto strip_quotes = [](const std::string &raw) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
      return raw.substr(1, raw.size() - 2);
    }
    return raw;
  };

  // ------------------------------------------------------------------
  // Legacy form (kept for backward compatibility, sema emits a
  // deprecation diagnostic):
  //
  //   CONFIG VENV   ["python"] "<path>";
  //   CONFIG CONDA  ["python"] "<env_name>";
  //   CONFIG UV     ["python"] "<path>";
  //   CONFIG PIPENV ["python"] "<path>";
  //   CONFIG POETRY ["python"] "<path>";
  // ------------------------------------------------------------------
  if (current_.kind == frontends::TokenKind::kKeyword) {
    auto legacy = LegacyConfigKeywordToManagerName(current_.lexeme);
    if (legacy.has_value()) {
      const std::string keyword_lex = current_.lexeme;
      auto node = std::make_shared<VenvConfigDecl>();
      node->loc = loc;
      node->is_legacy_form = true;
      node->manager_name = *legacy;
      Advance(); // consume legacy keyword

      // Optional language specifier (defaults to "python" — every
      // legacy keyword maps to a Python package manager).
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->language = current_.lexeme;
        Advance();
      } else {
        node->language = "python";
      }

      if (current_.kind == frontends::TokenKind::kString) {
        node->venv_path = strip_quotes(current_.lexeme);
        Advance();
      } else {
        diagnostics_.Report(current_.loc,
                            "expected string path after CONFIG " + keyword_lex);
      }

      // Resolve the manager kind via the registry so sema sees the
      // same canonical representation as for the new form.
      if (auto entry = ResolveConfigManager(node->language, node->manager_name)) {
        node->manager = entry->kind;
      } else {
        // Legacy keyword paired with an unsupported language (e.g.
        // `CONFIG VENV rust "..."`).  Still emit the AST node so sema
        // can produce a precise diagnostic.
        node->manager = VenvConfigDecl::ManagerKind::kUnknown;
      }

      ExpectSymbol(";", "expected ';' after CONFIG " + keyword_lex);
      return node;
    }
  }

  // ------------------------------------------------------------------
  // Canonical (stringified) form, since v1.12.0:
  //
  //   CONFIG <language> "<package_manager>" "<path_or_env>";
  // ------------------------------------------------------------------
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    auto node = std::make_shared<VenvConfigDecl>();
    node->loc = loc;
    node->is_legacy_form = false;
    node->language = current_.lexeme;
    Advance(); // consume language identifier

    if (current_.kind != frontends::TokenKind::kString) {
      diagnostics_.Report(current_.loc,
                          "expected string package-manager name after CONFIG " +
                              node->language +
                              " (e.g. CONFIG python \"venv\" \".venv\";)");
      Sync();
      auto dummy = std::make_shared<ExprStatement>();
      dummy->loc = loc;
      return dummy;
    }
    node->manager_name = strip_quotes(current_.lexeme);
    Advance();

    if (current_.kind != frontends::TokenKind::kString) {
      diagnostics_.Report(current_.loc,
                          "expected string path after CONFIG " + node->language +
                              " \"" + node->manager_name + "\"");
      Sync();
      auto dummy = std::make_shared<ExprStatement>();
      dummy->loc = loc;
      return dummy;
    }
    node->venv_path = strip_quotes(current_.lexeme);
    Advance();

    if (auto entry = ResolveConfigManager(node->language, node->manager_name)) {
      node->manager = entry->kind;
    } else {
      node->manager = VenvConfigDecl::ManagerKind::kUnknown;
    }

    ExpectSymbol(";", "expected ';' after CONFIG declaration");
    return node;
  }

  diagnostics_.Report(
      current_.loc,
      "expected language name or legacy keyword (VENV / CONDA / UV / PIPENV / POETRY) "
      "after CONFIG; canonical form is `CONFIG <language> \"<package_manager>\" "
      "\"<path_or_env>\";`");
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

  // Generic instantiation: Pair<i32, String>, Box<T>, etc. (since v1.15.0).
  // We are unconditionally inside a type-position here, so the angle
  // brackets cannot be confused with comparison operators.  HANDLE<...>
  // is handled earlier and does not reach this branch.
  if (IsSymbol("<")) {
    auto pt = std::make_shared<ParameterizedType>();
    pt->loc = loc;
    pt->name = name;
    Advance(); // consume '<'
    if (!IsSymbol(">")) {
      pt->type_args.push_back(ParseType());
      while (MatchSymbol(",")) {
        pt->type_args.push_back(ParseType());
      }
    }
    ExpectSymbol(">", "expected '>' after generic type arguments");
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

// ============================================================================
// Visibility / attribute prefix (since v1.16.0)
// ============================================================================

void PloyParser::ParseAttributesAndVisibility(std::vector<Attribute> &out_attrs,
                                              Visibility &out_vis,
                                              bool &has_explicit_vis) {
  while (true) {
    // `@<name>(arg, arg, ...)` annotation.  `@LANG(...)` is intentionally
    // not consumed here; the top-level dispatcher routes it elsewhere.
    if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "@") {
      const auto &peek = Peek();
      if (peek.kind == frontends::TokenKind::kKeyword && peek.lexeme == "LANG") {
        break; // belongs to ParseLangAnnotation
      }
      Attribute attr;
      attr.loc = current_.loc;
      Advance(); // consume '@'
      if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "expected attribute name after '@'");
        Sync();
        break;
      }
      attr.name = current_.lexeme;
      Advance();
      if (IsSymbol("(")) {
        Advance(); // consume '('
        if (!IsSymbol(")")) {
          while (true) {
            // Capture the argument's source spelling verbatim — string,
            // identifier, or numeric literal — and keep enclosing quotes
            // for string args so sema can recover the original text.
            attr.args.push_back(current_.lexeme);
            Advance();
            if (!MatchSymbol(",")) {
              break;
            }
          }
        }
        ExpectSymbol(")", "expected ')' after attribute arguments");
      }
      out_attrs.push_back(std::move(attr));
      continue;
    }
    // PUB / PRIVATE visibility keyword.
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "PUB" || current_.lexeme == "PRIVATE")) {
      if (has_explicit_vis) {
        diagnostics_.Report(current_.loc,
                            "duplicate visibility modifier; PUB / PRIVATE may "
                            "appear at most once per declaration");
      }
      out_vis = (current_.lexeme == "PUB") ? Visibility::kPub : Visibility::kPrivate;
      has_explicit_vis = true;
      Advance();
      continue;
    }
    break;
  }
}

// ============================================================================
// String literal helper (since v1.17.0)
// ============================================================================

std::shared_ptr<Expression> PloyParser::BuildStringExpression(
    const std::string &lexeme, core::SourceLoc loc) {
  // Detect the template-string prefix `f"` (single-quote form) or `f"""`
  // (triple-quote form).  Anything else is a regular / raw / multiline
  // literal — the lexer has already canonicalised these into a `"..."`
  // form, so the existing kString lowering path handles them unchanged.
  bool is_template = lexeme.size() >= 2 && lexeme[0] == 'f' && lexeme[1] == '"';
  if (!is_template) {
    auto lit = std::make_shared<Literal>();
    lit->loc = loc;
    lit->kind = Literal::Kind::kString;
    lit->value = lexeme;
    return lit;
  }

  // Carve the body out of `f"..."` or `f"""..."""`.
  std::string body;
  bool triple = lexeme.size() >= 4 && lexeme[1] == '"' && lexeme[2] == '"' &&
                lexeme[3] == '"';
  if (triple && lexeme.size() >= 7 && lexeme.substr(lexeme.size() - 3) == "\"\"\"") {
    body = lexeme.substr(4, lexeme.size() - 4 - 3);
  } else {
    // Strip the leading f" and the trailing ".
    if (lexeme.size() >= 3 && lexeme.back() == '"') {
      body = lexeme.substr(2, lexeme.size() - 3);
    }
  }

  auto tmpl = std::make_shared<TemplateString>();
  tmpl->loc = loc;

  // Walk the body character by character, splitting on `{` / `}`.  A literal
  // brace is written `{{` / `}}` (Python f-string compatible).  Backslash
  // escapes that survive into a template literal are preserved verbatim
  // here — the lowering layer's existing escape decoder handles them.
  std::string text_buf;
  for (size_t i = 0; i < body.size(); ++i) {
    char c = body[i];
    if (c == '{' && i + 1 < body.size() && body[i + 1] == '{') {
      text_buf.push_back('{');
      ++i;
      continue;
    }
    if (c == '}' && i + 1 < body.size() && body[i + 1] == '}') {
      text_buf.push_back('}');
      ++i;
      continue;
    }
    if (c == '{') {
      // Flush the accumulated literal text (re-quoted so downstream sees
      // a canonical `"..."` Literal value).
      if (!text_buf.empty()) {
        TemplateString::Part p;
        p.is_text = true;
        p.text = "\"" + text_buf + "\"";
        p.loc = loc;
        tmpl->parts.push_back(std::move(p));
        text_buf.clear();
      }
      // Find the matching `}` (single-level — nested template strings are
      // not supported in this MVP).
      size_t end = body.find('}', i + 1);
      if (end == std::string::npos) {
        diagnostics_.Report(loc,
                            "unterminated interpolation in template string; "
                            "expected matching '}'");
        return tmpl;
      }
      std::string inner = body.substr(i + 1, end - i - 1);
      i = end;
      // Sub-parse the inner expression by spinning up a fresh lexer / parser
      // pair over the carved substring.  Diagnostics flow through the
      // surrounding parser's diagnostics sink.
      PloyLexer sub_lex(inner, "<template>");
      PloyParser sub_parser(sub_lex, diagnostics_);
      sub_parser.Advance();
      auto expr = sub_parser.ParseExpression();
      if (!expr) {
        diagnostics_.Report(loc,
                            "failed to parse interpolated expression in "
                            "template string");
        continue;
      }
      TemplateString::Part p;
      p.is_text = false;
      p.expr = expr;
      p.loc = loc;
      tmpl->parts.push_back(std::move(p));
      continue;
    }
    text_buf.push_back(c);
  }
  if (!text_buf.empty()) {
    TemplateString::Part p;
    p.is_text = true;
    p.text = "\"" + text_buf + "\"";
    p.loc = loc;
    tmpl->parts.push_back(std::move(p));
  }
  return tmpl;
}

} // namespace polyglot::ploy
