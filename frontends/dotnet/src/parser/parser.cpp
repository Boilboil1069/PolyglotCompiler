/**
 * @file     parser.cpp
 * @brief    .NET/C# language frontend implementation
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/dotnet/include/dotnet_parser.h"

#include <cctype>
#include <unordered_set>
#include <utility>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"

namespace polyglot::dotnet {

namespace {

bool IsIdentifierLike(const frontends::Token &token) {
  if (token.kind == frontends::TokenKind::kIdentifier)
    return true;
  if (token.kind != frontends::TokenKind::kKeyword)
    return false;
  // Contextual keywords are identifiers unless the surrounding grammar gives
  // them their special meaning.
  static const std::unordered_set<std::string> contextual = {
      "add",       "allows",    "and",       "ascending", "async",    "await",
      "by",        "descending","dynamic",   "equals",    "file",     "from",
      "get",       "global",    "group",      "init",      "into",     "join",
      "field",     "let",       "managed",    "nameof",    "not",      "notnull",  "on",
      "or",        "orderby",   "partial",    "record",    "remove",   "required",
      "scoped",    "select",    "set",        "unmanaged", "value",    "var",
      "when",      "where",     "with",       "yield"};
  return contextual.contains(token.lexeme);
}

bool ContainsEscapeE(const std::string &spelling) {
  const auto quote = spelling.find_first_of("\"'");
  if (quote == std::string::npos)
    return false;
  if (quote > 0 && spelling[quote - 1] == '@')
    return false;
  if (spelling.compare(quote, 3, "\"\"\"") == 0)
    return false;
  for (std::size_t i = quote + 1; i < spelling.size(); ++i) {
    if (spelling[i] != 'e')
      continue;
    std::size_t slashes = 0;
    for (std::size_t j = i; j > quote + 1 && spelling[j - 1] == '\\'; --j)
      ++slashes;
    if ((slashes % 2) == 1)
      return true;
  }
  return false;
}

bool ContainsNullConditional(const std::shared_ptr<Expression> &expr) {
  if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr))
    return member->null_conditional || ContainsNullConditional(member->object);
  if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr))
    return index->null_conditional || ContainsNullConditional(index->object);
  return false;
}

const char *PropertyAccessorName(PropertyDecl::Accessor::Kind kind) {
  switch (kind) {
  case PropertyDecl::Accessor::Kind::kGet:
    return "get";
  case PropertyDecl::Accessor::Kind::kSet:
    return "set";
  case PropertyDecl::Accessor::Kind::kInit:
    return "init";
  }
  return "accessor";
}

bool HasPropertyAccessor(const PropertyDecl &property,
                         PropertyDecl::Accessor::Kind kind) {
  for (const auto &accessor : property.accessors) {
    if (accessor.kind == kind)
      return true;
  }
  return false;
}

void DiagnosePropertyAccessorCombination(
    const PropertyDecl &property, PropertyDecl::Accessor::Kind kind,
    const core::SourceLoc &loc, frontends::Diagnostics &diagnostics) {
  if (HasPropertyAccessor(property, kind)) {
    diagnostics.ReportError(
        loc, frontends::ErrorCode::kUnexpectedToken,
        std::string("duplicate '") + PropertyAccessorName(kind) +
            "' property accessor");
  }
  const bool conflicts_with_set =
      kind == PropertyDecl::Accessor::Kind::kInit &&
      HasPropertyAccessor(property, PropertyDecl::Accessor::Kind::kSet);
  const bool conflicts_with_init =
      kind == PropertyDecl::Accessor::Kind::kSet &&
      HasPropertyAccessor(property, PropertyDecl::Accessor::Kind::kInit);
  if (conflicts_with_set || conflicts_with_init) {
    diagnostics.ReportError(
        loc, frontends::ErrorCode::kUnexpectedToken,
        "a property cannot declare both 'set' and 'init' accessors");
  }
}

bool IsRepresentableSwitchConstantPattern(
    const std::shared_ptr<Expression> &expression) {
  if (std::dynamic_pointer_cast<Literal>(expression))
    return true;
  if (const auto identifier = std::dynamic_pointer_cast<Identifier>(expression))
    return identifier->name == "_";
  if (const auto unary = std::dynamic_pointer_cast<UnaryExpression>(expression)) {
    return (unary->op == "+" || unary->op == "-" || unary->op == "!" ||
            unary->op == "~") &&
           IsRepresentableSwitchConstantPattern(unary->operand);
  }
  return false;
}

} // namespace

void DotnetParser::Advance() {
  current_ = lexer_.NextToken();
}

frontends::Token DotnetParser::Consume() {
  Advance();
  while (current_.kind == frontends::TokenKind::kComment) {
    Advance();
  }
  return current_;
}

frontends::Token DotnetParser::PeekToken() {
  const auto state = lexer_.SaveState();
  auto token = lexer_.NextToken();
  while (token.kind == frontends::TokenKind::kComment)
    token = lexer_.NextToken();
  lexer_.RestoreState(state);
  return token;
}

bool DotnetParser::IsSymbol(const std::string &symbol) const {
  return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool DotnetParser::MatchSymbol(const std::string &symbol) {
  if (IsSymbol(symbol)) {
    Consume();
    return true;
  }
  return false;
}

bool DotnetParser::MatchKeyword(const std::string &keyword) {
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
    Consume();
    return true;
  }
  return false;
}

void DotnetParser::ExpectSymbol(const std::string &symbol, const std::string &msg) {
  if (!MatchSymbol(symbol))
    diagnostics_.Report(current_.loc, msg);
}

void DotnetParser::Sync() {
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (IsSymbol(";") || IsSymbol("{") || IsSymbol("}"))
      return;
    if (current_.kind == frontends::TokenKind::kKeyword) {
      auto &kw = current_.lexeme;
      if (kw == "class" || kw == "struct" || kw == "interface" || kw == "enum" ||
          kw == "namespace" || kw == "using" || kw == "public" || kw == "private" ||
          kw == "protected" || kw == "internal")
        return;
    }
    Consume();
  }
}

void DotnetParser::ReportFeatureBoundary(const core::SourceLoc &loc,
                                         frontends::DotnetLangVersion required,
                                         const std::string &feature,
                                         bool faithfully_represented) {
  if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_, required)) {
    diagnostics_.ReportError(
        loc, frontends::ErrorCode::kLangVersionMismatch,
        feature + " requires C# " + frontends::DotnetLangVersionToString(required) +
            " or newer (current: " +
            frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
  } else if (!faithfully_represented) {
    diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedSyntax,
                             feature + " is recognized but not represented by the C# AST");
  }
}

void DotnetParser::SkipRecognizedMember() {
  int paren_depth = 0;
  int bracket_depth = 0;
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (IsSymbol("(") )
      ++paren_depth;
    else if (IsSymbol(")") && paren_depth > 0)
      --paren_depth;
    else if (IsSymbol("["))
      ++bracket_depth;
    else if (IsSymbol("]") && bracket_depth > 0)
      --bracket_depth;
    else if (paren_depth == 0 && bracket_depth == 0 && IsSymbol(";")) {
      Consume();
      return;
    } else if (paren_depth == 0 && bracket_depth == 0 && IsSymbol("=>")) {
      Consume();
      ParseExpression();
      ExpectSymbol(";", "expected ';' after expression-bodied member");
      return;
    } else if (paren_depth == 0 && bracket_depth == 0 && IsSymbol("{")) {
      int brace_depth = 0;
      do {
        if (IsSymbol("{"))
          ++brace_depth;
        else if (IsSymbol("}"))
          --brace_depth;
        Consume();
      } while (brace_depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile);
      return;
    } else if (paren_depth == 0 && bracket_depth == 0 && IsSymbol("}")) {
      return;
    }
    Consume();
  }
}

PropertyDecl::Accessor DotnetParser::ParsePropertyAccessorBody(
    PropertyDecl::Accessor::Kind kind, const core::SourceLoc &loc) {
  PropertyDecl::Accessor accessor;
  accessor.kind = kind;
  accessor.loc = loc;
  const bool saved = in_property_accessor_;
  const bool saved_saw_field = saw_field_keyword_;
  in_property_accessor_ = true;
  saw_field_keyword_ = false;
  if (IsSymbol(";")) {
    Consume();
    accessor.is_auto = true;
  } else if (IsSymbol("{")) {
    auto block = ParseBlock();
    accessor.body = std::move(block->statements);
  } else if (MatchSymbol("=>")) {
    accessor.expression_body = ParseExpression();
    ExpectSymbol(";", "expected ';' after accessor expression body");
  } else {
    diagnostics_.Report(current_.loc, "expected accessor body or ';'");
  }
  accessor.uses_field_keyword = saw_field_keyword_;
  in_property_accessor_ = saved;
  saw_field_keyword_ = saved_saw_field;
  return accessor;
}

std::shared_ptr<Expression> DotnetParser::ParsePropertyExpressionBody(
    bool &uses_field_keyword) {
  const bool saved = in_property_accessor_;
  const bool saved_saw_field = saw_field_keyword_;
  in_property_accessor_ = true;
  saw_field_keyword_ = false;
  auto expression = ParseExpression();
  uses_field_keyword = saw_field_keyword_;
  in_property_accessor_ = saved;
  saw_field_keyword_ = saved_saw_field;
  return expression;
}

std::string DotnetParser::ParseQualifiedName() {
  std::string name;
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    name = current_.lexeme;
    Consume();
    while (IsSymbol(".")) {
      Consume();
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        name += "." + current_.lexeme;
        Consume();
      } else
        break;
    }
  }
  return name;
}

// ============================================================================
// Top-Level Parsing
// ============================================================================

void DotnetParser::ParseModule() {
  module_ = std::make_shared<Module>();
  Consume();

  // using directives and top-level declarations
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (current_.kind == frontends::TokenKind::kPreprocessor) {
      const auto directive_loc = current_.loc;
      const std::string spelling = current_.lexeme;
      if (spelling.rfind("#:", 0) == 0) {
        ReportFeatureBoundary(directive_loc, frontends::DotnetLangVersion::kCs14,
                              "file-based application directives", true);
        auto directive = std::make_shared<FileDirective>();
        directive->loc = directive_loc;
        directive->spelling = spelling;
        std::size_t begin = 2;
        while (begin < spelling.size() && std::isspace(static_cast<unsigned char>(spelling[begin])))
          ++begin;
        const auto end = spelling.find_first_of(" \t", begin);
        directive->name = spelling.substr(begin, end == std::string::npos
                                                     ? std::string::npos
                                                     : end - begin);
        if (end != std::string::npos) {
          std::size_t value_begin = end;
          while (value_begin < spelling.size() &&
                 std::isspace(static_cast<unsigned char>(spelling[value_begin])))
            ++value_begin;
          directive->value = spelling.substr(value_begin);
        }
        module_->file_directives.push_back(std::move(directive));
      } else {
        diagnostics_.ReportError(
            directive_loc, frontends::ErrorCode::kUnsupportedSyntax,
            "C# conditional/nullable preprocessor directives require a preprocessing model");
      }
      Consume();
    } else if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "using" || current_.lexeme == "global")) {
      module_->usings.push_back(ParseUsingDirective());
    } else {
      ParseTopLevel();
    }
  }
}

std::shared_ptr<Module> DotnetParser::TakeModule() {
  return std::move(module_);
}

std::shared_ptr<UsingDirective> DotnetParser::ParseUsingDirective() {
  auto node = std::make_shared<UsingDirective>();
  node->loc = current_.loc;
  if (MatchKeyword("global")) {
    node->is_global = true;
    if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                             frontends::DotnetLangVersion::kCs10)) {
      diagnostics_.ReportError(
          node->loc, frontends::ErrorCode::kLangVersionMismatch,
          std::string("global using directives require C# 10 or newer (current: ") +
              frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
    }
    if (!MatchKeyword("using"))
      diagnostics_.Report(current_.loc, "expected 'using' after 'global'");
  } else {
    MatchKeyword("using");
  }
  if (MatchKeyword("static"))
    node->is_static = true;

  // Check for alias: name = namespace;
  std::string first = ParseQualifiedName();
  if (IsSymbol("=")) {
    Consume();
    node->alias = first;
    node->ns = ParseQualifiedName();
  } else {
    node->ns = first;
  }

  ExpectSymbol(";", "expected ';' after using directive");
  return node;
}

std::vector<Attribute> DotnetParser::ParseAttributes() {
  std::vector<Attribute> attrs;
  while (IsSymbol("[")) {
    Consume();
    Attribute a;
    a.name = ParseQualifiedName();
    if (IsSymbol("(")) {
      Consume();
      int depth = 1;
      while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsSymbol("("))
          depth++;
        else if (IsSymbol(")"))
          depth--;
        if (depth > 0)
          Consume();
      }
      if (IsSymbol(")"))
        Consume();
    }
    attrs.push_back(a);
    ExpectSymbol("]", "expected ']' after attribute");
  }
  return attrs;
}

std::string DotnetParser::ParseAccessModifier() {
  if (MatchKeyword("public"))
    return "public";
  if (MatchKeyword("private"))
    return "private";
  if (MatchKeyword("protected"))
    return "protected";
  if (MatchKeyword("internal"))
    return "internal";
  return "";
}

void DotnetParser::ParseTopLevel() {
  auto attrs = ParseAttributes();
  std::string access = ParseAccessModifier();

  // Additional access: protected internal, private protected
  if (access == "protected" && current_.kind == frontends::TokenKind::kKeyword &&
      current_.lexeme == "internal") {
    access = "protected internal";
    Consume();
  } else if (access == "private" && current_.kind == frontends::TokenKind::kKeyword &&
             current_.lexeme == "protected") {
    access = "private protected";
    Consume();
  }

  // Consume modifiers
  bool is_abstract = false, is_sealed = false, is_static = false;
  bool is_partial = false, is_readonly = false, is_ref = false;
  bool is_file = false, is_record = false;

  while (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "abstract") {
      is_abstract = true;
      Consume();
    } else if (kw == "sealed") {
      is_sealed = true;
      Consume();
    } else if (kw == "static") {
      is_static = true;
      Consume();
    } else if (kw == "partial") {
      is_partial = true;
      Consume();
    } else if (kw == "readonly") {
      is_readonly = true;
      Consume();
    } else if (kw == "ref") {
      is_ref = true;
      Consume();
    } else if (kw == "file") {
      if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs11)) {
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                                 std::string("'file'-scoped types require C# 11 or newer "
                                             "(current: ") +
                                     frontends::DotnetLangVersionToString(dotnet_lang_version_) +
                                     ")");
      }
      is_file = true;
      Consume();
    } else if (kw == "record") {
      if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs9)) {
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                                 std::string("'record' declarations require C# 9 or newer "
                                             "(current: ") +
                                     frontends::DotnetLangVersionToString(dotnet_lang_version_) +
                                     ")");
      }
      is_record = true;
      break;
    } else if (kw == "new" || kw == "unsafe") {
      Consume();
    } else
      break;
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "namespace") {
    module_->declarations.push_back(ParseNamespaceDecl());
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class") {
    auto cls = ParseClassDecl(access, attrs);
    cls->is_abstract = is_abstract;
    cls->is_sealed = is_sealed;
    cls->is_static = is_static;
    cls->is_partial = is_partial;
    cls->is_file_scoped = is_file;
    module_->declarations.push_back(cls);
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "struct") {
    auto st = ParseStructDecl(access, attrs);
    st->is_readonly = is_readonly;
    st->is_ref = is_ref;
    st->is_partial = is_partial;
    if (is_ref && !st->interfaces.empty()) {
      ReportFeatureBoundary(st->loc, frontends::DotnetLangVersion::kCs13,
                            "ref structs implementing interfaces", true);
    }
    module_->declarations.push_back(st);
  } else if (is_record) {
    // record class or record struct
    Consume(); // 'record'
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "struct") {
      if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs10)) {
        diagnostics_.ReportError(
            current_.loc, frontends::ErrorCode::kLangVersionMismatch,
            std::string("record structs require C# 10 or newer (current: ") +
                frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
      }
      auto st = ParseStructDecl(access, attrs, true);
      st->is_record = true;
      st->is_readonly = is_readonly;
      module_->declarations.push_back(st);
    } else {
      if (MatchKeyword("class")) { /* record class */
      }
      auto cls = ParseClassDecl(access, attrs, true);
      cls->is_record = true;
      module_->declarations.push_back(cls);
    }
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "interface") {
    auto iface = ParseInterfaceDecl(access, attrs);
    iface->is_partial = is_partial;
    module_->declarations.push_back(iface);
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "enum") {
    module_->declarations.push_back(ParseEnumDecl(access, attrs));
  } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "delegate") {
    module_->declarations.push_back(ParseDelegateDecl(access, attrs));
  } else {
    // Top-level statements were introduced in C# 9.
    if (current_.kind != frontends::TokenKind::kEndOfFile) {
      if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs9)) {
        diagnostics_.ReportError(
            current_.loc, frontends::ErrorCode::kLangVersionMismatch,
            std::string("top-level statements require C# 9 or newer (current: ") +
                frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
      }
      auto stmt = ParseStatement();
      if (stmt)
        module_->top_level_statements.push_back(stmt);
    }
  }
}

std::shared_ptr<NamespaceDecl> DotnetParser::ParseNamespaceDecl() {
  auto node = std::make_shared<NamespaceDecl>();
  node->loc = current_.loc;
  Consume(); // 'namespace'
  node->name = ParseQualifiedName();

  // File-scoped namespace (C# 10): namespace Foo;
  if (IsSymbol(";")) {
    if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                             frontends::DotnetLangVersion::kCs10)) {
      diagnostics_.ReportError(
          current_.loc, frontends::ErrorCode::kLangVersionMismatch,
          std::string("file-scoped namespaces require C# 10 or newer (current: ") +
              frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
    }
    node->is_file_scoped = true;
    Consume();
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
        module_->usings.push_back(ParseUsingDirective());
      } else {
        ParseTopLevel();
        // Move last top-level decl into namespace
        if (!module_->declarations.empty()) {
          node->members.push_back(module_->declarations.back());
          module_->declarations.pop_back();
        }
      }
    }
  } else {
    ExpectSymbol("{", "expected '{' after namespace name");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
        module_->usings.push_back(ParseUsingDirective());
      } else {
        auto saved_size = module_->declarations.size();
        ParseTopLevel();
        while (module_->declarations.size() > saved_size) {
          node->members.push_back(module_->declarations.back());
          module_->declarations.pop_back();
        }
      }
    }
    ExpectSymbol("}", "expected '}'");
  }

  return node;
}

// ============================================================================
// Type Declarations
// ============================================================================

std::shared_ptr<ClassDecl> DotnetParser::ParseClassDecl(const std::string &access,
                                                        const std::vector<Attribute> &attrs,
                                                        bool is_record) {
  auto node = std::make_shared<ClassDecl>();
  node->loc = current_.loc;
  node->access = access;
  node->attributes = attrs;
  node->is_record = is_record;
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class")
    Consume();

  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }

  node->type_params = ParseTypeParameters();

  // Primary constructor parameters (C# 12)
  if (IsSymbol("(") && !is_record) {
    if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                             frontends::DotnetLangVersion::kCs12)) {
      diagnostics_.ReportError(
          current_.loc, frontends::ErrorCode::kLangVersionMismatch,
          std::string("primary constructors require C# 12 or newer (current: ") +
              frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
    }
    node->primary_ctor_params = ParseParameters();
  } else if (IsSymbol("(")) {
    node->primary_ctor_params = ParseParameters();
  }

  // Base type / interfaces
  if (IsSymbol(":")) {
    Consume();
    auto first = ParseType();
    node->base_type = first;
    while (MatchSymbol(",")) {
      node->interfaces.push_back(ParseType());
    }
  }

  // Type parameter constraints
  while (MatchKeyword("where")) {
    ParseQualifiedName(); // T
    ExpectSymbol(":", "expected ':' after where");
    while (!IsSymbol("{") && !IsSymbol(";") &&
           !(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "where") &&
           current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "allows") {
        const auto loc = current_.loc;
        Consume();
        if (MatchKeyword("ref") && MatchKeyword("struct")) {
          ReportFeatureBoundary(loc, frontends::DotnetLangVersion::kCs13,
                                "the allows ref struct anti-constraint", false);
        }
        continue;
      }
      Consume();
    }
  }

  // Class body
  if (IsSymbol("{")) {
    Consume();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      auto member_attrs = ParseAttributes();
      std::string member_access = ParseAccessModifier();

      if (IsIdentifierLike(current_) && current_.lexeme == "extension") {
        node->members.push_back(ParseExtensionDecl(member_access, member_attrs));
        continue;
      }

      bool member_static = false, member_virtual = false, member_override = false;
      bool member_abstract = false, member_sealed = false, member_readonly = false;
      bool member_async = false, member_new = false, member_extern = false;
      bool member_partial = false, member_volatile = false, member_const = false;
      bool member_required = false;

      while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") {
          member_static = true;
          Consume();
        } else if (kw == "virtual") {
          member_virtual = true;
          Consume();
        } else if (kw == "override") {
          member_override = true;
          Consume();
        } else if (kw == "abstract") {
          member_abstract = true;
          Consume();
        } else if (kw == "sealed") {
          member_sealed = true;
          Consume();
        } else if (kw == "readonly") {
          member_readonly = true;
          Consume();
        } else if (kw == "async") {
          member_async = true;
          Consume();
        } else if (kw == "new") {
          member_new = true;
          Consume();
        } else if (kw == "extern") {
          member_extern = true;
          Consume();
        } else if (kw == "partial") {
          member_partial = true;
          Consume();
        } else if (kw == "volatile") {
          member_volatile = true;
          Consume();
        } else if (kw == "const") {
          member_const = true;
          Consume();
        } else if (kw == "unsafe") {
          Consume();
        } else if (kw == "required") {
          if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                                   frontends::DotnetLangVersion::kCs11)) {
            diagnostics_.ReportError(
                current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                std::string("required members require C# 11 or newer (current: ") +
                    frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
          }
          member_required = true;
          Consume();
        } else
          break;
      }

      // Nested type declarations
      if (current_.kind == frontends::TokenKind::kKeyword &&
          (current_.lexeme == "class" || current_.lexeme == "struct" ||
           current_.lexeme == "interface" || current_.lexeme == "enum" ||
           current_.lexeme == "delegate" || current_.lexeme == "record")) {
        auto &kw = current_.lexeme;
        if (kw == "class") {
          node->members.push_back(ParseClassDecl(member_access, member_attrs));
        } else if (kw == "struct") {
          node->members.push_back(ParseStructDecl(member_access, member_attrs));
        } else if (kw == "interface") {
          node->members.push_back(ParseInterfaceDecl(member_access, member_attrs));
        } else if (kw == "enum") {
          node->members.push_back(ParseEnumDecl(member_access, member_attrs));
        } else if (kw == "delegate") {
          node->members.push_back(ParseDelegateDecl(member_access, member_attrs));
        } else if (kw == "record") {
          if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                                   frontends::DotnetLangVersion::kCs9)) {
            diagnostics_.ReportError(
                current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                std::string("record declarations require C# 9 or newer (current: ") +
                    frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
          }
          Consume();
          if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "struct") {
            if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                                     frontends::DotnetLangVersion::kCs10)) {
              diagnostics_.ReportError(
                  current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                  std::string("record structs require C# 10 or newer (current: ") +
                      frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
            }
            node->members.push_back(ParseStructDecl(member_access, member_attrs, true));
          } else {
            MatchKeyword("class");
            node->members.push_back(ParseClassDecl(member_access, member_attrs, true));
          }
        }
        continue;
      }

      // Destructor
      if (IsSymbol("~")) {
        Consume();
        auto dtor = ParseDestructorDecl(node->name);
        node->members.push_back(dtor);
        continue;
      }

      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "event") {
        auto event = std::make_shared<EventDecl>();
        event->loc = current_.loc;
        event->access = member_access;
        event->attributes = member_attrs;
        event->is_static = member_static;
        event->is_partial = member_partial;
        if (member_partial) {
          ReportFeatureBoundary(event->loc, frontends::DotnetLangVersion::kCs14,
                                "partial events", true);
        }
        Consume(); // event
        event->type = ParseType();
        if (IsIdentifierLike(current_)) {
          event->name = current_.lexeme;
          Consume();
        } else {
          diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                                   "expected event name");
        }
        if (MatchSymbol("=")) {
          diagnostics_.ReportError(
              event->loc, frontends::ErrorCode::kUnsupportedSyntax,
              "event initializers are recognized but not represented by the C# AST");
          ParseExpression();
          ExpectSymbol(";", "expected ';' after event");
        } else if (IsSymbol("{")) {
          diagnostics_.ReportError(
              event->loc, frontends::ErrorCode::kUnsupportedSyntax,
              "custom event accessors are recognized but not represented by the C# AST");
          SkipRecognizedMember();
        } else {
          ExpectSymbol(";", "expected ';' after event");
        }
        node->members.push_back(event);
        continue;
      }

      // Constructor check
      const auto next_member_token = PeekToken();
      if (IsIdentifierLike(current_) && current_.lexeme == node->name &&
          !member_static && next_member_token.kind == frontends::TokenKind::kSymbol &&
          next_member_token.lexeme == "(") {
        auto ctor = ParseConstructorDecl(member_access, node->name);
        ctor->attributes = member_attrs;
        ctor->is_partial = member_partial;
        if (member_partial) {
          ReportFeatureBoundary(ctor->loc, frontends::DotnetLangVersion::kCs14,
                                "partial constructors", true);
          MatchSymbol(";");
        }
        node->members.push_back(ctor);
        continue;
      }

      // Static constructor
      if (member_static && IsIdentifierLike(current_) &&
          current_.lexeme == node->name &&
          next_member_token.kind == frontends::TokenKind::kSymbol &&
          next_member_token.lexeme == "(") {
        auto ctor = ParseConstructorDecl(member_access, node->name);
        ctor->is_static = true;
        ctor->is_partial = member_partial;
        if (member_partial) {
          ReportFeatureBoundary(ctor->loc, frontends::DotnetLangVersion::kCs14,
                                "partial constructors", true);
          MatchSymbol(";");
        }
        node->members.push_back(ctor);
        continue;
      }

      // Type for method/field/property
      auto type = ParseType();
      if (IsIdentifierLike(current_) && current_.lexeme == "operator") {
        node->members.push_back(ParseOperatorDecl(member_access, member_attrs, type,
                                                  member_static, type->loc));
        continue;
      }
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "this") {
        if (member_partial) {
          ReportFeatureBoundary(current_.loc, frontends::DotnetLangVersion::kCs13,
                                "partial indexers", false);
        } else {
          diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                                   "indexers are recognized but not represented by this parser");
        }
        SkipRecognizedMember();
        continue;
      }
      if (IsIdentifierLike(current_)) {
        std::string name = current_.lexeme;
        Consume();

        if (IsSymbol("(")) {
          // Method
          auto method = std::make_shared<MethodDecl>();
          method->loc = type->loc;
          method->name = name;
          method->return_type = type;
          method->access = member_access;
          method->is_static = member_static;
          method->is_virtual = member_virtual;
          method->is_override = member_override;
          method->is_abstract = member_abstract;
          method->is_sealed = member_sealed;
          method->is_async = member_async;
          method->is_new = member_new;
          method->is_extern = member_extern;
          method->is_partial = member_partial;
          method->attributes = member_attrs;
          method->params = ParseParameters();

          // where constraints
          while (MatchKeyword("where")) {
            ParseQualifiedName();
            ExpectSymbol(":", "expected ':'");
              while (
                !IsSymbol("{") && !IsSymbol(";") && !IsSymbol("=>") &&
                !(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "where") &&
                current_.kind != frontends::TokenKind::kEndOfFile) {
              if (current_.kind == frontends::TokenKind::kKeyword &&
                  current_.lexeme == "allows") {
                const auto loc = current_.loc;
                Consume();
                if (MatchKeyword("ref") && MatchKeyword("struct")) {
                  ReportFeatureBoundary(loc, frontends::DotnetLangVersion::kCs13,
                                        "the allows ref struct anti-constraint", false);
                }
                continue;
              }
              Consume();
            }
          }

          // Expression body: => expr;
          if (IsSymbol("=>")) {
            Consume();
            method->expression_body = ParseExpression();
            ExpectSymbol(";", "expected ';'");
          } else if (IsSymbol("{")) {
            auto block = ParseBlock();
            method->body = block->statements;
          } else {
            ExpectSymbol(";", "expected ';'");
          }
          node->members.push_back(method);
        } else if (IsSymbol("{") || IsSymbol("=>")) {
          // Property
          auto prop = std::make_shared<PropertyDecl>();
          prop->loc = type->loc;
          prop->name = name;
          prop->type = type;
          prop->access = member_access;
          prop->is_static = member_static;
          prop->is_virtual = member_virtual;
          prop->is_override = member_override;
          prop->is_abstract = member_abstract;
          prop->is_required = member_required;
          prop->is_partial = member_partial;
          if (member_partial) {
            ReportFeatureBoundary(prop->loc, frontends::DotnetLangVersion::kCs13,
                                  "partial properties", true);
          }
          prop->attributes = member_attrs;

          if (IsSymbol("=>")) {
            Consume();
            bool uses_field = false;
            prop->expression_body = ParsePropertyExpressionBody(uses_field);
            prop->uses_field_keyword |= uses_field;
            ExpectSymbol(";", "expected ';'");
            prop->has_getter = true;
          } else {
            Consume(); // '{'
            while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
              if (MatchKeyword("get")) {
                prop->has_getter = true;
                const auto loc = current_.loc;
                DiagnosePropertyAccessorCombination(
                    *prop, PropertyDecl::Accessor::Kind::kGet, loc, diagnostics_);
                auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kGet, loc);
                prop->uses_field_keyword |= accessor.uses_field_keyword;
                prop->accessors.push_back(std::move(accessor));
              } else if (MatchKeyword("set")) {
                prop->has_setter = true;
                const auto loc = current_.loc;
                DiagnosePropertyAccessorCombination(
                    *prop, PropertyDecl::Accessor::Kind::kSet, loc, diagnostics_);
                auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kSet, loc);
                prop->uses_field_keyword |= accessor.uses_field_keyword;
                prop->accessors.push_back(std::move(accessor));
              } else if (MatchKeyword("init")) {
                ReportFeatureBoundary(prop->loc, frontends::DotnetLangVersion::kCs9,
                                      "init accessors", true);
                prop->is_init_only = true;
                prop->has_setter = true;
                const auto loc = current_.loc;
                DiagnosePropertyAccessorCombination(
                    *prop, PropertyDecl::Accessor::Kind::kInit, loc, diagnostics_);
                auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kInit, loc);
                prop->uses_field_keyword |= accessor.uses_field_keyword;
                prop->accessors.push_back(std::move(accessor));
              } else {
                Consume();
              }
            }
            ExpectSymbol("}", "expected '}'");
          }

          // Default value
          if (MatchSymbol("=")) {
            prop->init = ParseExpression();
            ExpectSymbol(";", "expected ';'");
          }

          node->members.push_back(prop);
        } else {
          // Field
          auto field = std::make_shared<FieldDecl>();
          field->loc = type->loc;
          field->name = name;
          field->type = type;
          field->access = member_access;
          field->is_static = member_static;
          field->is_readonly = member_readonly;
          field->is_const = member_const;
          field->is_volatile = member_volatile;
          field->is_required = member_required;
          field->attributes = member_attrs;

          if (MatchSymbol("=")) {
            field->init = ParseExpression();
          }
          ExpectSymbol(";", "expected ';'");
          node->members.push_back(field);
        }
      } else {
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                                 "unsupported or malformed class member");
        Sync();
        if (IsSymbol(";"))
          Consume();
        else if (IsSymbol("{"))
          ParseBlock();
        else if (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile)
          Consume();
      }
    }
    ExpectSymbol("}", "expected '}'");
  } else if (IsSymbol(";")) {
    Consume(); // record Point(int X, int Y);
  }

  return node;
}

std::shared_ptr<StructDecl> DotnetParser::ParseStructDecl(const std::string &access,
                                                          const std::vector<Attribute> &attrs,
                                                          bool is_record) {
  auto node = std::make_shared<StructDecl>();
  node->loc = current_.loc;
  node->access = access;
  node->attributes = attrs;
  node->is_record = is_record;
  Consume(); // 'struct'

  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }

  node->type_params = ParseTypeParameters();

  if (IsSymbol("(") && !is_record) {
    if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                             frontends::DotnetLangVersion::kCs12)) {
      diagnostics_.ReportError(
          current_.loc, frontends::ErrorCode::kLangVersionMismatch,
          std::string("primary constructors require C# 12 or newer (current: ") +
              frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
    }
    node->primary_ctor_params = ParseParameters();
  } else if (IsSymbol("(")) {
    node->primary_ctor_params = ParseParameters();
  }

  if (IsSymbol(":")) {
    Consume();
    do {
      node->interfaces.push_back(ParseType());
    } while (MatchSymbol(","));
  }

  // Constraints
  while (MatchKeyword("where")) {
    ParseQualifiedName();
    ExpectSymbol(":", "");
    while (!IsSymbol("{") && !IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "allows") {
        const auto loc = current_.loc;
        Consume();
        if (MatchKeyword("ref") && MatchKeyword("struct")) {
          ReportFeatureBoundary(loc, frontends::DotnetLangVersion::kCs13,
                                "the allows ref struct anti-constraint", false);
        }
        continue;
      }
      Consume();
    }
  }

  if (IsSymbol("{")) {
    Consume();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      auto attrs = ParseAttributes();
      auto access = ParseAccessModifier();
      auto member = ParseCommonTypeMember(node->name, access, attrs, true);
      if (member)
        node->members.push_back(member);
    }
    ExpectSymbol("}", "expected '}'");
  } else if (IsSymbol(";")) {
    Consume();
  }

  return node;
}

std::shared_ptr<InterfaceDecl> DotnetParser::ParseInterfaceDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
  auto node = std::make_shared<InterfaceDecl>();
  node->loc = current_.loc;
  node->access = access;
  node->attributes = attrs;
  Consume(); // 'interface'

  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }

  node->type_params = ParseTypeParameters();

  if (IsSymbol(":")) {
    Consume();
    do {
      node->extends_types.push_back(ParseType());
    } while (MatchSymbol(","));
  }

  while (MatchKeyword("where")) {
    ParseQualifiedName();
    ExpectSymbol(":", "");
    while (!IsSymbol("{") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "allows") {
        const auto loc = current_.loc;
        Consume();
        if (MatchKeyword("ref") && MatchKeyword("struct")) {
          ReportFeatureBoundary(loc, frontends::DotnetLangVersion::kCs13,
                                "the allows ref struct anti-constraint", false);
        }
        continue;
      }
      Consume();
    }
  }

  if (IsSymbol("{")) {
    Consume();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      auto attrs = ParseAttributes();
      auto access = ParseAccessModifier();
      auto member = ParseCommonTypeMember(node->name, access, attrs, false);
      if (member)
        node->members.push_back(member);
    }
    ExpectSymbol("}", "expected '}'");
  }

  return node;
}

std::shared_ptr<EnumDecl> DotnetParser::ParseEnumDecl(const std::string &access,
                                                      const std::vector<Attribute> &attrs) {
  auto node = std::make_shared<EnumDecl>();
  node->loc = current_.loc;
  node->access = access;
  node->attributes = attrs;
  Consume(); // 'enum'

  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }

  if (IsSymbol(":")) {
    Consume();
    node->underlying_type = ParseType();
  }

  if (IsSymbol("{")) {
    Consume();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      EnumDecl::EnumMember em;
      em.attributes = ParseAttributes();
      if (IsIdentifierLike(current_)) {
        em.name = current_.lexeme;
        Consume();
      }
      if (MatchSymbol("=")) {
        em.value = ParseExpression();
      }
      node->members.push_back(em);
      if (!MatchSymbol(","))
        break;
    }
    ExpectSymbol("}", "expected '}'");
  }

  return node;
}

std::shared_ptr<DelegateDecl> DotnetParser::ParseDelegateDecl(const std::string &access,
                                                              const std::vector<Attribute> &attrs) {
  auto node = std::make_shared<DelegateDecl>();
  node->loc = current_.loc;
  node->access = access;
  node->attributes = attrs;
  Consume(); // 'delegate'

  node->return_type = ParseType();
  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }
  node->type_params = ParseTypeParameters();
  node->params = ParseParameters();
  ExpectSymbol(";", "expected ';'");
  return node;
}

// ============================================================================
// Members
// ============================================================================

std::shared_ptr<ConstructorDecl> DotnetParser::ParseConstructorDecl(const std::string &access,
                                                                    const std::string &class_name) {
  auto node = std::make_shared<ConstructorDecl>();
  node->loc = current_.loc;
  node->name = class_name;
  node->access = access;
  Consume(); // constructor name

  node->params = ParseParameters();

  // Base/this initializer
  if (IsSymbol(":")) {
    Consume();
    if (MatchKeyword("base"))
      node->initializer_kind = "base";
    else if (MatchKeyword("this"))
      node->initializer_kind = "this";
    if (IsSymbol("(")) {
      Consume();
      while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        node->initializer_args.push_back(ParseExpression());
        if (!MatchSymbol(","))
          break;
      }
      ExpectSymbol(")", "expected ')'");
    }
  }

  if (IsSymbol("{")) {
    auto block = ParseBlock();
    node->body = block->statements;
  } else if (IsSymbol("=>")) {
    Consume();
    auto stmt = std::make_shared<ExprStatement>();
    stmt->expr = ParseExpression();
    node->body.push_back(stmt);
    ExpectSymbol(";", "expected ';'");
  }

  return node;
}

std::shared_ptr<DestructorDecl> DotnetParser::ParseDestructorDecl(const std::string &class_name) {
  auto node = std::make_shared<DestructorDecl>();
  node->loc = current_.loc;
  node->name = "~" + class_name;

  if (IsIdentifierLike(current_))
    Consume();
  ExpectSymbol("(", "expected '('");
  ExpectSymbol(")", "expected ')'");

  if (IsSymbol("{")) {
    auto block = ParseBlock();
    node->body = block->statements;
  }

  return node;
}

std::vector<TypeParameter> DotnetParser::ParseTypeParameters() {
  std::vector<TypeParameter> params;
  if (!IsSymbol("<"))
    return params;
  Consume();
  while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
    TypeParameter tp;
    if (IsIdentifierLike(current_)) {
      tp.name = current_.lexeme;
      Consume();
    }
    params.push_back(tp);
    if (!MatchSymbol(","))
      break;
  }
  ExpectSymbol(">", "expected '>'");
  return params;
}

std::vector<Parameter> DotnetParser::ParseParameters() {
  std::vector<Parameter> params;
  ExpectSymbol("(", "expected '('");
  while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
    Parameter p;
    p.attributes = ParseAttributes();
    if (MatchKeyword("this"))
      p.is_this = true;
    if (MatchKeyword("ref"))
      p.is_ref = true;
    if (MatchKeyword("out"))
      p.is_out = true;
    if (MatchKeyword("in"))
      p.is_in = true;
    if (MatchKeyword("params"))
      p.is_params = true;

    p.type = ParseType();
    if (p.is_params && !std::dynamic_pointer_cast<ArrayType>(p.type)) {
      ReportFeatureBoundary(p.type ? p.type->loc : current_.loc,
                            frontends::DotnetLangVersion::kCs13,
                            "params collections", true);
    }
    if (IsIdentifierLike(current_)) {
      p.name = current_.lexeme;
      Consume();
    }
    if (MatchSymbol("=")) {
      p.default_value = ParseExpression();
    }
    params.push_back(p);
    if (!MatchSymbol(","))
      break;
  }
  ExpectSymbol(")", "expected ')'");
  return params;
}

// ============================================================================
// Types
// ============================================================================

std::shared_ptr<TypeNode> DotnetParser::ParseType() {
  auto node = std::make_shared<SimpleType>();
  node->loc = current_.loc;

  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    node->name = ParseQualifiedName();
  } else {
    node->name = "void";
  }

  // Generic type arguments
  if (IsSymbol("<")) {
    auto gen = std::make_shared<GenericType>();
    gen->loc = node->loc;
    gen->name = node->name;
    Consume();
    while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
      gen->type_args.push_back(ParseTypeArgument());
      if (!MatchSymbol(","))
        break;
    }
    ExpectSymbol(">", "expected '>'");

    // Nullable
    if (IsSymbol("?")) {
      Consume();
      auto nullable = std::make_shared<NullableType>();
      nullable->loc = gen->loc;
      nullable->inner = gen;
      return nullable;
    }

    // Array
    if (IsSymbol("[")) {
      Consume();
      ExpectSymbol("]", "expected ']'");
      auto arr = std::make_shared<ArrayType>();
      arr->loc = gen->loc;
      arr->element_type = gen;
      return arr;
    }

    return gen;
  }

  // Nullable
  if (IsSymbol("?")) {
    Consume();
    auto nullable = std::make_shared<NullableType>();
    nullable->loc = node->loc;
    nullable->inner = node;
    return nullable;
  }

  // Array
  if (IsSymbol("[")) {
    Consume();
    ExpectSymbol("]", "expected ']'");
    auto arr = std::make_shared<ArrayType>();
    arr->loc = node->loc;
    arr->element_type = node;
    return arr;
  }

  return node;
}

std::shared_ptr<TypeNode> DotnetParser::ParseTypeArgument() {
  return ParseType();
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> DotnetParser::ParseStatement() {
  if (IsSymbol("{"))
    return ParseBlock();
  if (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "if")
      return ParseIf();
    if (kw == "while")
      return ParseWhile();
    if (kw == "for")
      return ParseFor();
    if (kw == "foreach")
      return ParseForEach();
    if (kw == "switch")
      return ParseSwitch();
    if (kw == "try")
      return ParseTry();
    if (kw == "return")
      return ParseReturn();
    if (kw == "throw")
      return ParseThrow();
    if (kw == "using")
      return ParseUsing();
    if (kw == "lock")
      return ParseLock();
    if (kw == "var" || kw == "const")
      return ParseVarDecl();
    if (kw == "break") {
      auto n = std::make_shared<BreakStatement>();
      n->loc = current_.loc;
      Consume();
      ExpectSymbol(";", "expected ';'");
      return n;
    }
    if (kw == "continue") {
      auto n = std::make_shared<ContinueStatement>();
      n->loc = current_.loc;
      Consume();
      ExpectSymbol(";", "expected ';'");
      return n;
    }
    if (kw == "yield") {
      auto n = std::make_shared<YieldStatement>();
      n->loc = current_.loc;
      Consume();
      if (MatchKeyword("break")) {
        n->is_break = true;
      } else if (MatchKeyword("return")) {
        n->value = ParseExpression();
      }
      ExpectSymbol(";", "expected ';'");
      return n;
    }
    if (kw == "checked" || kw == "unchecked") {
      auto n = std::make_shared<CheckedStatement>();
      n->loc = current_.loc;
      n->is_unchecked = (kw == "unchecked");
      Consume();
      n->body = ParseBlock();
      return n;
    }
    if (kw == "do") {
      auto n = std::make_shared<DoWhileStatement>();
      n->loc = current_.loc;
      Consume();
      n->body = ParseStatement();
      ExpectSymbol("while", "expected 'while'");
      ExpectSymbol("(", "expected '('");
      n->condition = ParseExpression();
      ExpectSymbol(")", "expected ')'");
      ExpectSymbol(";", "expected ';'");
      return n;
    }
  }

  auto expr = ParseExpression();
  if (expr) {
    auto stmt = std::make_shared<ExprStatement>();
    stmt->loc = expr->loc;
    stmt->expr = expr;
    ExpectSymbol(";", "expected ';'");
    return stmt;
  }
  Consume();
  return std::make_shared<ExprStatement>();
}

std::shared_ptr<BlockStatement> DotnetParser::ParseBlock() {
  auto node = std::make_shared<BlockStatement>();
  node->loc = current_.loc;
  ExpectSymbol("{", "expected '{'");
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    auto stmt = ParseStatement();
    if (stmt)
      node->statements.push_back(stmt);
  }
  ExpectSymbol("}", "expected '}'");
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseVarDecl() {
  auto node = std::make_shared<VarDecl>();
  node->loc = current_.loc;
  if (MatchKeyword("const"))
    node->is_const = true;
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
    node->type = nullptr;
    Consume();
  } else {
    node->type = ParseType();
  }
  if (IsIdentifierLike(current_)) {
    node->name = current_.lexeme;
    Consume();
  }
  if (MatchSymbol("=")) {
    node->init = ParseExpression();
  }
  ExpectSymbol(";", "expected ';'");
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseIf() {
  auto node = std::make_shared<IfStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->condition = ParseExpression();
  ExpectSymbol(")", "expected ')'");
  node->then_body = ParseStatement();
  if (MatchKeyword("else"))
    node->else_body = ParseStatement();
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseWhile() {
  auto node = std::make_shared<WhileStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->condition = ParseExpression();
  ExpectSymbol(")", "expected ')'");
  node->body = ParseStatement();
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseFor() {
  auto node = std::make_shared<ForStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->init = ParseStatement();
  node->condition = ParseExpression();
  ExpectSymbol(";", "expected ';'");
  node->update = ParseExpression();
  ExpectSymbol(")", "expected ')'");
  node->body = ParseStatement();
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseForEach() {
  auto node = std::make_shared<ForEachStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->var_type = ParseType();
  if (IsIdentifierLike(current_)) {
    node->var_name = current_.lexeme;
    Consume();
  }
  if (MatchKeyword("in")) {
    node->iterable = ParseExpression();
  }
  ExpectSymbol(")", "expected ')'");
  node->body = ParseStatement();
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseSwitch() {
  auto node = std::make_shared<SwitchStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->governing = ParseExpression();
  ExpectSymbol(")", "expected ')'");
  ExpectSymbol("{", "expected '{'");
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    SwitchStatement::Section sec;
    while (MatchKeyword("case") || MatchKeyword("default")) {
      if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "default") {
        sec.is_default = true;
      } else {
        sec.labels.push_back(ParseExpression());
      }
      ExpectSymbol(":", "expected ':'");
    }
    while (!IsSymbol("}") &&
           !(current_.kind == frontends::TokenKind::kKeyword &&
             (current_.lexeme == "case" || current_.lexeme == "default")) &&
           current_.kind != frontends::TokenKind::kEndOfFile) {
      sec.body.push_back(ParseStatement());
    }
    node->sections.push_back(sec);
  }
  ExpectSymbol("}", "expected '}'");
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseTry() {
  auto node = std::make_shared<TryStatement>();
  node->loc = current_.loc;
  Consume();
  node->body = ParseBlock();

  while (MatchKeyword("catch")) {
    TryStatement::CatchClause cc;
    if (IsSymbol("(")) {
      Consume();
      cc.exception_type = ParseType();
      if (IsIdentifierLike(current_)) {
        cc.var_name = current_.lexeme;
        Consume();
      }
      ExpectSymbol(")", "expected ')'");
      if (MatchKeyword("when")) {
        ExpectSymbol("(", "expected '('");
        cc.filter = ParseExpression();
        ExpectSymbol(")", "expected ')'");
      }
    }
    cc.body = ParseBlock();
    node->catches.push_back(cc);
  }

  if (MatchKeyword("finally")) {
    node->finally_body = ParseBlock();
  }

  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseReturn() {
  auto node = std::make_shared<ReturnStatement>();
  node->loc = current_.loc;
  Consume();
  if (!IsSymbol(";"))
    node->value = ParseExpression();
  ExpectSymbol(";", "expected ';'");
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseThrow() {
  auto node = std::make_shared<ThrowStatement>();
  node->loc = current_.loc;
  Consume();
  if (!IsSymbol(";"))
    node->expr = ParseExpression();
  ExpectSymbol(";", "expected ';'");
  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseUsing() {
  auto node = std::make_shared<UsingStatement>();
  node->loc = current_.loc;
  Consume();

  if (MatchKeyword("await"))
    node->is_await = true;

  if (IsSymbol("(")) {
    Consume();
    auto var = std::make_shared<VarDecl>();
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
      Consume();
    } else {
      var->type = ParseType();
    }
    if (IsIdentifierLike(current_)) {
      var->name = current_.lexeme;
      Consume();
    }
    if (MatchSymbol("="))
      var->init = ParseExpression();
    node->declaration = var;
    ExpectSymbol(")", "expected ')'");
    node->body = ParseStatement();
  } else {
    auto var = std::make_shared<VarDecl>();
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
      Consume();
    } else {
      var->type = ParseType();
    }
    if (IsIdentifierLike(current_)) {
      var->name = current_.lexeme;
      Consume();
    }
    if (MatchSymbol("="))
      var->init = ParseExpression();
    node->declaration = var;
    ExpectSymbol(";", "expected ';'");
  }

  return node;
}

std::shared_ptr<Statement> DotnetParser::ParseLock() {
  auto node = std::make_shared<LockStatement>();
  node->loc = current_.loc;
  Consume();
  ExpectSymbol("(", "expected '('");
  node->expr = ParseExpression();
  ExpectSymbol(")", "expected ')'");
  node->body = ParseStatement();
  return node;
}

// ============================================================================
// Expressions
// ============================================================================

int DotnetParser::GetPrecedence(const std::string &op) const {
  if (op == "||")
    return 1;
  if (op == "&&")
    return 2;
  if (op == "|")
    return 3;
  if (op == "^")
    return 4;
  if (op == "&")
    return 5;
  if (op == "==" || op == "!=")
    return 6;
  if (op == "<" || op == ">" || op == "<=" || op == ">=")
    return 7;
  if (op == "<<" || op == ">>" || op == ">>>")
    return 8;
  if (op == "+" || op == "-")
    return 9;
  if (op == "*" || op == "/" || op == "%")
    return 10;
  return 0;
}

std::shared_ptr<Expression> DotnetParser::ParseExpression() {
  return ParseTernary();
}

std::shared_ptr<Expression> DotnetParser::ParseTernary() {
  auto expr = ParseNullCoalescing();
  if (IsSymbol("?")) {
    auto node = std::make_shared<TernaryExpression>();
    node->loc = expr->loc;
    node->condition = expr;
    Consume();
    node->then_expr = ParseExpression();
    ExpectSymbol(":", "expected ':'");
    node->else_expr = ParseTernary();
    return node;
  }
  // Assignment
  if (current_.kind == frontends::TokenKind::kSymbol) {
    auto &op = current_.lexeme;
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
        op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" || op == "\?\?=") {
      if (ContainsNullConditional(expr)) {
        ReportFeatureBoundary(expr->loc, frontends::DotnetLangVersion::kCs14,
                              "null-conditional assignment", true);
      }
      auto bin = std::make_shared<BinaryExpression>();
      bin->loc = expr->loc;
      bin->op = op;
      bin->left = expr;
      Consume();
      bin->right = ParseTernary();
      return bin;
    }
  }
  return expr;
}

std::shared_ptr<Expression> DotnetParser::ParseNullCoalescing() {
  auto left = ParseBinary(1);
  if (IsSymbol("??")) {
    auto node = std::make_shared<NullCoalescingExpression>();
    node->loc = left->loc;
    node->left = left;
    Consume();
    node->right = ParseNullCoalescing();
    return node;
  }
  return left;
}

std::shared_ptr<Expression> DotnetParser::ParseBinary(int min_prec) {
  auto left = ParseUnary();
  while (current_.kind == frontends::TokenKind::kSymbol) {
    int prec = GetPrecedence(current_.lexeme);
    if (prec < min_prec)
      break;
    auto op = current_.lexeme;
    Consume();
    auto right = ParseBinary(prec + 1);
    auto bin = std::make_shared<BinaryExpression>();
    bin->loc = left->loc;
    bin->op = op;
    bin->left = left;
    bin->right = right;
    left = bin;
  }
  // is / as expressions
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "is") {
    auto node = std::make_shared<IsExpression>();
    node->loc = left->loc;
    node->expr = left;
    Consume();
    node->type = ParseType();
    if (IsIdentifierLike(current_)) {
      node->pattern_var = current_.lexeme;
      Consume();
    }
    return node;
  }
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "as") {
    auto node = std::make_shared<AsExpression>();
    node->loc = left->loc;
    node->expr = left;
    Consume();
    node->type = ParseType();
    return node;
  }
  return left;
}

std::shared_ptr<Expression> DotnetParser::ParseUnary() {
  if (current_.kind == frontends::TokenKind::kSymbol) {
    auto &op = current_.lexeme;
    if (op == "!" || op == "~" || op == "-" || op == "+" || op == "++" || op == "--") {
      auto node = std::make_shared<UnaryExpression>();
      node->loc = current_.loc;
      node->op = op;
      Consume();
      node->operand = ParseUnary();
      return node;
    }
  }
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "await") {
    auto node = std::make_shared<AwaitExpression>();
    node->loc = current_.loc;
    Consume();
    node->operand = ParseUnary();
    return node;
  }
  return ParsePostfix();
}

std::shared_ptr<Expression> DotnetParser::ParsePostfix() {
  auto expr = ParsePrimary();

  while (true) {
    if (IsSymbol(".") || IsSymbol("?.")) {
      bool null_cond = IsSymbol("?.");
      Consume();
      auto member = std::make_shared<MemberExpression>();
      member->loc = expr->loc;
      member->object = expr;
      member->null_conditional = null_cond;
      if (IsIdentifierLike(current_)) {
        member->member = current_.lexeme;
        Consume();
      }
      expr = member;
    } else if (IsSymbol("[") || IsSymbol("?[")) {
      bool null_cond = IsSymbol("?[");
      Consume();
      auto index = std::make_shared<IndexExpression>();
      index->loc = expr->loc;
      index->object = expr;
      index->null_conditional = null_cond;
      index->index = ParseExpression();
      ExpectSymbol("]", "expected ']'");
      expr = index;
    } else if (IsSymbol("(")) {
      auto call = std::make_shared<CallExpression>();
      call->loc = expr->loc;
      call->callee = expr;
      Consume();
      while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        call->args.push_back(ParseExpression());
        if (!MatchSymbol(","))
          break;
      }
      ExpectSymbol(")", "expected ')'");
      expr = call;
    } else if (IsSymbol("++") || IsSymbol("--")) {
      auto u = std::make_shared<UnaryExpression>();
      u->loc = current_.loc;
      u->op = current_.lexeme;
      u->operand = expr;
      u->postfix = true;
      Consume();
      expr = u;
    } else if (IsSymbol("!")) {
      // Null-forgiving operator
      Consume();
      // Null-forgiving is compile-time only; no transformation needed.
    } else if (current_.kind == frontends::TokenKind::kKeyword &&
               current_.lexeme == "switch") {
      expr = ParseSwitchExpression(std::move(expr));
    } else {
      break;
    }
  }
  return expr;
}

std::shared_ptr<Expression> DotnetParser::ParseSwitchExpression(
    std::shared_ptr<Expression> governing) {
  auto node = std::make_shared<SwitchExpression>();
  node->loc = current_.loc;
  node->governing = std::move(governing);
  ReportFeatureBoundary(node->loc, frontends::DotnetLangVersion::kCs8,
                        "switch expressions", true);
  Consume(); // switch
  ExpectSymbol("{", "expected '{' after switch expression");
  if (IsSymbol("}")) {
    diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnexpectedToken,
                             "a switch expression must declare at least one arm");
  }

  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    SwitchExpression::Arm arm;
    arm.pattern = ParseExpression();

    // The compact AST faithfully retains constant/discard expression
    // patterns. Recursive, relational, declaration and combinator patterns
    // need a dedicated pattern tree; diagnose those instead of discarding
    // their trailing tokens and pretending the first expression was enough.
    bool requires_pattern_ast = false;
    if (!(current_.kind == frontends::TokenKind::kKeyword &&
          current_.lexeme == "when") &&
        !IsSymbol("=>")) {
      requires_pattern_ast = true;
      diagnostics_.ReportError(
          current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
          "this switch-expression pattern requires a dedicated C# pattern AST");
      int paren_depth = 0;
      int bracket_depth = 0;
      int brace_depth = 0;
      while (current_.kind != frontends::TokenKind::kEndOfFile) {
        const bool at_boundary = paren_depth == 0 && bracket_depth == 0 && brace_depth == 0;
        if (at_boundary && ((current_.kind == frontends::TokenKind::kKeyword &&
                             current_.lexeme == "when") ||
                            IsSymbol("=>") || IsSymbol(",") || IsSymbol("}")))
          break;
        if (IsSymbol("("))
          ++paren_depth;
        else if (IsSymbol(")") && paren_depth > 0)
          --paren_depth;
        else if (IsSymbol("["))
          ++bracket_depth;
        else if (IsSymbol("]") && bracket_depth > 0)
          --bracket_depth;
        else if (IsSymbol("{"))
          ++brace_depth;
        else if (IsSymbol("}") && brace_depth > 0)
          --brace_depth;
        Consume();
      }
    }
    if (!requires_pattern_ast &&
        !IsRepresentableSwitchConstantPattern(arm.pattern)) {
      diagnostics_.ReportError(
          arm.pattern ? arm.pattern->loc : node->loc,
          frontends::ErrorCode::kUnsupportedSyntax,
          "this switch-expression constant pattern cannot be proven without "
          "constant-binding/evaluation support");
    }

    if (MatchKeyword("when"))
      arm.guard = ParseExpression();
    ExpectSymbol("=>", "expected '=>' in switch expression arm");
    arm.value = ParseExpression();
    node->arms.push_back(std::move(arm));

    if (!MatchSymbol(",")) {
      if (!IsSymbol("}"))
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnexpectedToken,
                                 "expected ',' or '}' after switch expression arm");
      break;
    }
  }
  ExpectSymbol("}", "expected '}' after switch expression");
  return node;
}

std::shared_ptr<Expression> DotnetParser::ParsePrimary() {
  if (current_.kind == frontends::TokenKind::kUnknown &&
      current_.lexeme.starts_with("unterminated C# ")) {
    auto loc = current_.loc;
    diagnostics_.ReportError(loc, frontends::ErrorCode::kUnterminatedString,
                             current_.lexeme);
    Consume();
    auto invalid = std::make_shared<Literal>();
    invalid->loc = loc;
    return invalid;
  }

  // Literals
  if (current_.kind == frontends::TokenKind::kNumber ||
      current_.kind == frontends::TokenKind::kString ||
      current_.kind == frontends::TokenKind::kChar) {
    if ((current_.kind == frontends::TokenKind::kString ||
         current_.kind == frontends::TokenKind::kChar) &&
        ContainsEscapeE(current_.lexeme)) {
      ReportFeatureBoundary(current_.loc, frontends::DotnetLangVersion::kCs13,
                            "the \\e escape sequence", true);
    }
    if (current_.kind == frontends::TokenKind::kString) {
      auto first_quote = current_.lexeme.find('"');
      if (first_quote != std::string::npos &&
          current_.lexeme.compare(first_quote, 3, "\"\"\"") == 0 &&
          !frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs11)) {
        diagnostics_.ReportError(
            current_.loc, frontends::ErrorCode::kLangVersionMismatch,
            std::string("raw string literals require C# 11 or newer (current: ") +
                frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
      }
    }
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->value = current_.lexeme;
    Consume();
    return lit;
  }

  if (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "true" || kw == "false" || kw == "null") {
      auto lit = std::make_shared<Literal>();
      lit->loc = current_.loc;
      lit->value = kw;
      Consume();
      return lit;
    }
    if (kw == "this" || kw == "base") {
      auto id = std::make_shared<Identifier>();
      id->loc = current_.loc;
      id->name = kw;
      Consume();
      return id;
    }
    if (kw == "new") {
      auto node = std::make_shared<NewExpression>();
      node->loc = current_.loc;
      Consume();
      if (IsSymbol("{")) {
        // new { ... } anonymous type
      } else if (!IsSymbol("(") && !IsSymbol("[")) {
        node->type = ParseType();
      }
      if (IsSymbol("(")) {
        Consume();
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
          node->args.push_back(ParseExpression());
          if (!MatchSymbol(","))
            break;
        }
        ExpectSymbol(")", "expected ')'");
      } else if (IsSymbol("[")) {
        Consume();
        node->args.push_back(ParseExpression());
        ExpectSymbol("]", "expected ']'");
      }
      // Collection/object initializer
      if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
          node->initializer.push_back(ParseExpression());
          if (!MatchSymbol(","))
            break;
        }
        ExpectSymbol("}", "expected '}'");
      }
      return node;
    }
    if (kw == "typeof") {
      auto node = std::make_shared<TypeofExpression>();
      node->loc = current_.loc;
      Consume();
      ExpectSymbol("(", "expected '('");
      node->type = ParseType();
      ExpectSymbol(")", "expected ')'");
      return node;
    }
    if (kw == "nameof") {
      auto node = std::make_shared<NameofExpression>();
      node->loc = current_.loc;
      Consume();
      ExpectSymbol("(", "expected '('");
      node->name = ParseQualifiedName();
      if (MatchSymbol("<")) {
        std::string suffix = "<";
        while (MatchSymbol(","))
          suffix += ",";
        if (IsSymbol(">")) {
          suffix += ">";
          Consume();
          node->name += suffix;
          ReportFeatureBoundary(node->loc, frontends::DotnetLangVersion::kCs14,
                                "nameof on an unbound generic type", true);
        } else {
          diagnostics_.ReportError(
              current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
              "constructed generic arguments inside nameof are not represented by this AST");
          while (!IsSymbol(">") && !IsSymbol(")") &&
                 current_.kind != frontends::TokenKind::kEndOfFile)
            Consume();
          MatchSymbol(">");
        }
      }
      ExpectSymbol(")", "expected ')'");
      return node;
    }
    if (kw == "default") {
      auto node = std::make_shared<DefaultExpression>();
      node->loc = current_.loc;
      Consume();
      if (IsSymbol("(")) {
        Consume();
        node->type = ParseType();
        ExpectSymbol(")", "expected ')'");
      }
      return node;
    }
    if (kw == "throw") {
      auto node = std::make_shared<ThrowExpression>();
      node->loc = current_.loc;
      Consume();
      node->operand = ParseUnary();
      return node;
    }
  }

  // Parenthesized expression or tuple
  if (IsSymbol("(")) {
    const auto lambda_loc = current_.loc;
    Consume();
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "ref" || current_.lexeme == "out" || current_.lexeme == "in" ||
         current_.lexeme == "scoped")) {
      auto lambda = std::make_shared<LambdaExpression>();
      lambda->loc = lambda_loc;
      while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        LambdaExpression::Param param;
        if (MatchKeyword("scoped"))
          param.is_scoped = true;
        if (MatchKeyword("ref"))
          param.is_ref = true;
        else if (MatchKeyword("out"))
          param.is_out = true;
        else if (MatchKeyword("in"))
          param.is_in = true;
        if (IsIdentifierLike(current_)) {
          param.name = current_.lexeme;
          Consume();
        } else {
          diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                                   "expected implicit lambda parameter name");
        }
        lambda->params.push_back(std::move(param));
        if (!MatchSymbol(","))
          break;
      }
      ExpectSymbol(")", "expected ')' after lambda parameters");
      ReportFeatureBoundary(lambda_loc, frontends::DotnetLangVersion::kCs14,
                            "modifiers on implicitly typed lambda parameters", true);
      if (!MatchSymbol("=>")) {
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                                 "expected '=>' after lambda parameters");
        return lambda;
      }
      if (IsSymbol("{"))
        lambda->body = ParseBlock();
      else
        lambda->expr = ParseExpression();
      return lambda;
    }
    auto first = ParseExpression();
    if (MatchSymbol(",")) {
      // Tuple expression
      auto tuple = std::make_shared<TupleExpression>();
      tuple->loc = first->loc;
      TupleExpression::Element e1;
      e1.value = first;
      tuple->elements.push_back(e1);
      do {
        TupleExpression::Element e;
        e.value = ParseExpression();
        tuple->elements.push_back(e);
      } while (MatchSymbol(","));
      ExpectSymbol(")", "expected ')'");
      return tuple;
    }
    ExpectSymbol(")", "expected ')'");
    return first;
  }

  // Identifier
  if (IsIdentifierLike(current_)) {
    if (in_property_accessor_ && current_.lexeme == "field") {
      ReportFeatureBoundary(current_.loc, frontends::DotnetLangVersion::kCs14,
                            "field-backed property access", true);
      saw_field_keyword_ = true;
    }
    auto id = std::make_shared<Identifier>();
    id->loc = current_.loc;
    id->name = current_.lexeme;
    Consume();
    return id;
  }

  // Fallback
  auto lit = std::make_shared<Literal>();
  lit->loc = current_.loc;
  lit->value = current_.lexeme;
  Consume();
  return lit;
}

std::shared_ptr<ExtensionDecl> DotnetParser::ParseExtensionDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
  auto extension = std::make_shared<ExtensionDecl>();
  extension->loc = current_.loc;
  extension->access = access;
  extension->attributes = attrs;
  ReportFeatureBoundary(extension->loc, frontends::DotnetLangVersion::kCs14,
                        "extension blocks", true);
  Consume(); // extension
  extension->type_params = ParseTypeParameters();
  auto receivers = ParseParameters();
  if (receivers.size() != 1) {
    diagnostics_.ReportError(extension->loc, frontends::ErrorCode::kUnsupportedSyntax,
                             "an extension block requires exactly one receiver");
  } else {
    extension->receiver = std::move(receivers.front());
    extension->has_receiver_name = !extension->receiver.name.empty();
  }

  if (!IsSymbol("{")) {
    diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                             "expected extension block body");
    return extension;
  }
  Consume();
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    auto member_attrs = ParseAttributes();
    auto member_access = ParseAccessModifier();
    auto member = ParseCommonTypeMember("<extension>", member_access, member_attrs, false);
    if (member)
      extension->members.push_back(std::move(member));
  }
  ExpectSymbol("}", "expected '}' after extension block");
  return extension;
}

std::shared_ptr<OperatorDecl> DotnetParser::ParseOperatorDecl(
    const std::string &access, const std::vector<Attribute> &attrs,
    const std::shared_ptr<TypeNode> &return_type, bool is_static,
    const core::SourceLoc &loc) {
  auto op = std::make_shared<OperatorDecl>();
  op->loc = loc;
  op->access = access;
  op->attributes = attrs;
  op->return_type = return_type;
  op->is_static = is_static;
  Consume(); // operator
  if (current_.kind == frontends::TokenKind::kEndOfFile) {
    diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedSyntax,
                             "expected an operator token");
    return op;
  }
  op->op = current_.lexeme;
  op->is_compound_assignment =
      op->op == "+=" || op->op == "-=" || op->op == "*=" || op->op == "/=" ||
      op->op == "%=" || op->op == "&=" || op->op == "|=" || op->op == "^=" ||
      op->op == "<<=" || op->op == ">>=" || op->op == ">>>=";
  if (op->is_compound_assignment) {
    ReportFeatureBoundary(loc, frontends::DotnetLangVersion::kCs14,
                          "user-defined compound assignment operators", true);
  }
  Consume();
  op->params = ParseParameters();
  if (MatchSymbol("=>")) {
    op->expression_body = ParseExpression();
    ExpectSymbol(";", "expected ';' after expression-bodied operator");
  } else if (IsSymbol("{")) {
    auto body = ParseBlock();
    op->body = std::move(body->statements);
  } else {
    ExpectSymbol(";", "expected operator body or ';'");
  }
  return op;
}

std::shared_ptr<Statement> DotnetParser::ParseCommonTypeMember(
    const std::string &owner, const std::string &access, const std::vector<Attribute> &attrs,
    bool allow_constructor) {
  const auto start_loc = current_.loc;
  if (IsIdentifierLike(current_) && current_.lexeme == "extension") {
    return ParseExtensionDecl(access, attrs);
  }
  bool is_static = false, is_readonly = false, is_const = false, is_volatile = false;
  bool is_virtual = false, is_override = false, is_abstract = false, is_async = false;
  bool is_required = false, is_partial = false;

  while (current_.kind == frontends::TokenKind::kKeyword) {
    const auto kw = current_.lexeme;
    if (kw == "static")
      is_static = true;
    else if (kw == "readonly")
      is_readonly = true;
    else if (kw == "const")
      is_const = true;
    else if (kw == "volatile")
      is_volatile = true;
    else if (kw == "virtual")
      is_virtual = true;
    else if (kw == "override")
      is_override = true;
    else if (kw == "abstract")
      is_abstract = true;
    else if (kw == "async")
      is_async = true;
    else if (kw == "partial")
      is_partial = true;
    else if (kw == "required") {
      is_required = true;
      if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                               frontends::DotnetLangVersion::kCs11)) {
        diagnostics_.ReportError(
            current_.loc, frontends::ErrorCode::kLangVersionMismatch,
            std::string("required members require C# 11 or newer (current: ") +
                frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
      }
    } else if (kw != "new" && kw != "extern" && kw != "unsafe" &&
               kw != "sealed") {
      break;
    }
    Consume();
  }

  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "event") {
    auto event = std::make_shared<EventDecl>();
    event->loc = current_.loc;
    event->access = access;
    event->attributes = attrs;
    event->is_static = is_static;
    event->is_partial = is_partial;
    if (is_partial) {
      ReportFeatureBoundary(event->loc, frontends::DotnetLangVersion::kCs14,
                            "partial events", true);
    }
    Consume();
    event->type = ParseType();
    if (IsIdentifierLike(current_)) {
      event->name = current_.lexeme;
      Consume();
    }
    if (IsSymbol("{")) {
      diagnostics_.ReportError(
          event->loc, frontends::ErrorCode::kUnsupportedSyntax,
          "custom event accessors are recognized but not represented by the C# AST");
      SkipRecognizedMember();
    } else {
      ExpectSymbol(";", "expected ';' after event");
    }
    return event;
  }

  const auto next_member_token = PeekToken();
  if (allow_constructor && IsIdentifierLike(current_) && current_.lexeme == owner &&
      next_member_token.kind == frontends::TokenKind::kSymbol &&
      next_member_token.lexeme == "(") {
    auto ctor = ParseConstructorDecl(access, owner);
    ctor->attributes = attrs;
    ctor->is_static = is_static;
    ctor->is_partial = is_partial;
    if (is_partial) {
      ReportFeatureBoundary(ctor->loc, frontends::DotnetLangVersion::kCs14,
                            "partial constructors", true);
      MatchSymbol(";");
    }
    return ctor;
  }

  auto type = ParseType();
  if (IsIdentifierLike(current_) && current_.lexeme == "operator") {
    return ParseOperatorDecl(access, attrs, type, is_static, start_loc);
  }
  if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "this") {
    if (is_partial) {
      ReportFeatureBoundary(current_.loc, frontends::DotnetLangVersion::kCs13,
                            "partial indexers", false);
    } else {
      diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnsupportedSyntax,
                               "indexers are recognized but not represented by this parser");
    }
    SkipRecognizedMember();
    return nullptr;
  }
  // The lexer intentionally classifies contextual words (for example
  // `value`, `record`, and `required`) as keywords. They remain legal names
  // outside their special grammar contexts.
  if (!type || (current_.kind != frontends::TokenKind::kIdentifier &&
                current_.kind != frontends::TokenKind::kKeyword)) {
    diagnostics_.Report(start_loc, "unsupported or malformed type member");
    Sync();
    if (IsSymbol(";"))
      Consume();
    else if (IsSymbol("{"))
      ParseBlock();
    else if (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile)
      Consume();
    return nullptr;
  }

  const std::string name = current_.lexeme;
  Consume();
  auto type_params = ParseTypeParameters();

  if (IsSymbol("(")) {
    auto method = std::make_shared<MethodDecl>();
    method->loc = start_loc;
    method->name = name;
    method->return_type = type;
    method->access = access;
    method->attributes = attrs;
    method->type_params = std::move(type_params);
    method->is_static = is_static;
    method->is_virtual = is_virtual;
    method->is_override = is_override;
    method->is_abstract = is_abstract;
    method->is_async = is_async;
    method->params = ParseParameters();
    if (MatchSymbol("=>")) {
      method->expression_body = ParseExpression();
      ExpectSymbol(";", "expected ';' after expression body");
    } else if (IsSymbol("{")) {
      auto body = ParseBlock();
      method->body = body->statements;
    } else {
      ExpectSymbol(";", "expected ';' after method declaration");
    }
    return method;
  }

  if (IsSymbol("{") || IsSymbol("=>")) {
    auto prop = std::make_shared<PropertyDecl>();
    prop->loc = start_loc;
    prop->name = name;
    prop->type = type;
    prop->access = access;
    prop->attributes = attrs;
    prop->is_static = is_static;
    prop->is_virtual = is_virtual;
    prop->is_override = is_override;
    prop->is_abstract = is_abstract;
    prop->is_required = is_required;
    prop->is_partial = is_partial;
    if (is_partial) {
      ReportFeatureBoundary(prop->loc, frontends::DotnetLangVersion::kCs13,
                            "partial properties", true);
    }
    if (MatchSymbol("=>")) {
      prop->has_getter = true;
      bool uses_field = false;
      prop->expression_body = ParsePropertyExpressionBody(uses_field);
      prop->uses_field_keyword |= uses_field;
      ExpectSymbol(";", "expected ';' after expression body");
    } else {
      Consume(); // {
      while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        bool parsed_accessor = true;
        if (MatchKeyword("get")) {
          prop->has_getter = true;
          DiagnosePropertyAccessorCombination(
              *prop, PropertyDecl::Accessor::Kind::kGet, current_.loc,
              diagnostics_);
          auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kGet,
                                                    current_.loc);
          prop->uses_field_keyword |= accessor.uses_field_keyword;
          prop->accessors.push_back(std::move(accessor));
        } else if (MatchKeyword("set")) {
          prop->has_setter = true;
          DiagnosePropertyAccessorCombination(
              *prop, PropertyDecl::Accessor::Kind::kSet, current_.loc,
              diagnostics_);
          auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kSet,
                                                    current_.loc);
          prop->uses_field_keyword |= accessor.uses_field_keyword;
          prop->accessors.push_back(std::move(accessor));
        } else if (MatchKeyword("init")) {
          if (!frontends::DotnetLangVersionAtLeast(dotnet_lang_version_,
                                                   frontends::DotnetLangVersion::kCs9)) {
            diagnostics_.ReportError(
                current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                std::string("init accessors require C# 9 or newer (current: ") +
                    frontends::DotnetLangVersionToString(dotnet_lang_version_) + ")");
          }
          prop->has_setter = true;
          prop->is_init_only = true;
          DiagnosePropertyAccessorCombination(
              *prop, PropertyDecl::Accessor::Kind::kInit, current_.loc,
              diagnostics_);
          auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kInit,
                                                    current_.loc);
          prop->uses_field_keyword |= accessor.uses_field_keyword;
          prop->accessors.push_back(std::move(accessor));
        } else {
          parsed_accessor = false;
          diagnostics_.Report(current_.loc, "expected property accessor");
          Consume();
        }
        if (!parsed_accessor)
          continue;
      }
      ExpectSymbol("}", "expected '}' after property accessors");
      if (MatchSymbol("=")) {
        prop->init = ParseExpression();
        ExpectSymbol(";", "expected ';' after property initializer");
      }
    }
    return prop;
  }

  auto field = std::make_shared<FieldDecl>();
  field->loc = start_loc;
  field->name = name;
  field->type = type;
  field->access = access;
  field->attributes = attrs;
  field->is_static = is_static;
  field->is_readonly = is_readonly;
  field->is_const = is_const;
  field->is_volatile = is_volatile;
  field->is_required = is_required;
  if (MatchSymbol("="))
    field->init = ParseExpression();
  ExpectSymbol(";", "expected ';' after field declaration");
  return field;
}

// Standalone ParseMethodDecl — parses a method declaration outside a class body.
std::shared_ptr<MethodDecl> DotnetParser::ParseMethodDecl(const std::string &access,
                                                          const std::vector<Attribute> &attrs) {
  bool is_static = false, is_virtual = false, is_override = false;
  bool is_abstract = false, is_sealed = false, is_async = false;
  bool is_partial = false, is_extern = false, is_new = false;
  while (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "static") {
      is_static = true;
      Consume();
    } else if (kw == "virtual") {
      is_virtual = true;
      Consume();
    } else if (kw == "override") {
      is_override = true;
      Consume();
    } else if (kw == "abstract") {
      is_abstract = true;
      Consume();
    } else if (kw == "sealed") {
      is_sealed = true;
      Consume();
    } else if (kw == "async") {
      is_async = true;
      Consume();
    } else if (kw == "partial") {
      is_partial = true;
      Consume();
    } else if (kw == "extern") {
      is_extern = true;
      Consume();
    } else if (kw == "new") {
      is_new = true;
      Consume();
    } else
      break;
  }

  auto return_type = ParseType();
  if (!return_type)
    return nullptr;

  if (!IsIdentifierLike(current_))
    return nullptr;
  std::string name = current_.lexeme;
  Consume();

  auto type_params = ParseTypeParameters();

  if (!IsSymbol("("))
    return nullptr;

  auto method = std::make_shared<MethodDecl>();
  method->loc = return_type->loc;
  method->name = name;
  method->return_type = return_type;
  method->access = access;
  method->attributes = attrs;
  method->type_params = type_params;
  method->is_static = is_static;
  method->is_virtual = is_virtual;
  method->is_override = is_override;
  method->is_abstract = is_abstract;
  method->is_sealed = is_sealed;
  method->is_async = is_async;
  method->is_partial = is_partial;
  method->is_extern = is_extern;
  method->is_new = is_new;
  method->params = ParseParameters();

  // Expression-bodied method: => expr;
  if (MatchSymbol("=>")) {
    method->expression_body = ParseExpression();
    ExpectSymbol(";", "expected ';' after expression body");
  } else if (IsSymbol("{")) {
    auto block = ParseBlock();
    method->body = block->statements;
  } else {
    ExpectSymbol(";", "expected ';' after method declaration");
  }

  return method;
}

// Standalone ParseFieldDecl — parses a field declaration outside a class body.
std::shared_ptr<FieldDecl> DotnetParser::ParseFieldDecl(const std::string &access,
                                                        const std::vector<Attribute> &attrs) {
  bool is_static = false, is_readonly = false, is_const = false;
  bool is_volatile = false, is_required = false;
  while (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "static") {
      is_static = true;
      Consume();
    } else if (kw == "readonly") {
      is_readonly = true;
      Consume();
    } else if (kw == "const") {
      is_const = true;
      Consume();
    } else if (kw == "volatile") {
      is_volatile = true;
      Consume();
    } else if (kw == "required") {
      is_required = true;
      Consume();
    } else
      break;
  }

  auto type = ParseType();
  if (!type)
    return nullptr;

  if (!IsIdentifierLike(current_))
    return nullptr;
  std::string name = current_.lexeme;
  Consume();

  auto field = std::make_shared<FieldDecl>();
  field->loc = type->loc;
  field->name = name;
  field->type = type;
  field->access = access;
  field->attributes = attrs;
  field->is_static = is_static;
  field->is_readonly = is_readonly;
  field->is_const = is_const;
  field->is_volatile = is_volatile;
  field->is_required = is_required;

  if (MatchSymbol("=")) {
    field->init = ParseExpression();
  }
  ExpectSymbol(";", "expected ';' after field declaration");

  return field;
}

// Standalone ParsePropertyDecl — parses a property declaration outside a class body.
std::shared_ptr<PropertyDecl> DotnetParser::ParsePropertyDecl(const std::string &access,
                                                              const std::vector<Attribute> &attrs) {
  bool is_static = false, is_virtual = false, is_override = false;
  bool is_abstract = false, is_required = false;
  while (current_.kind == frontends::TokenKind::kKeyword) {
    auto &kw = current_.lexeme;
    if (kw == "static") {
      is_static = true;
      Consume();
    } else if (kw == "virtual") {
      is_virtual = true;
      Consume();
    } else if (kw == "override") {
      is_override = true;
      Consume();
    } else if (kw == "abstract") {
      is_abstract = true;
      Consume();
    } else if (kw == "required") {
      is_required = true;
      Consume();
    } else
      break;
  }

  auto type = ParseType();
  if (!type)
    return nullptr;

  if (!IsIdentifierLike(current_))
    return nullptr;
  std::string name = current_.lexeme;
  Consume();

  auto prop = std::make_shared<PropertyDecl>();
  prop->loc = type->loc;
  prop->name = name;
  prop->type = type;
  prop->access = access;
  prop->attributes = attrs;
  prop->is_static = is_static;
  prop->is_virtual = is_virtual;
  prop->is_override = is_override;
  prop->is_abstract = is_abstract;
  prop->is_required = is_required;

  // Property accessor block: { get; set; } or { get { ... } set { ... } }
  if (IsSymbol("{")) {
    Consume();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
      if (MatchKeyword("get")) {
        prop->has_getter = true;
        DiagnosePropertyAccessorCombination(
            *prop, PropertyDecl::Accessor::Kind::kGet, current_.loc,
            diagnostics_);
        auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kGet,
                                                  current_.loc);
        prop->uses_field_keyword |= accessor.uses_field_keyword;
        prop->accessors.push_back(std::move(accessor));
      } else if (MatchKeyword("set")) {
        prop->has_setter = true;
        DiagnosePropertyAccessorCombination(
            *prop, PropertyDecl::Accessor::Kind::kSet, current_.loc,
            diagnostics_);
        auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kSet,
                                                  current_.loc);
        prop->uses_field_keyword |= accessor.uses_field_keyword;
        prop->accessors.push_back(std::move(accessor));
      } else if (MatchKeyword("init")) {
        ReportFeatureBoundary(prop->loc, frontends::DotnetLangVersion::kCs9,
                              "init accessors", true);
        prop->is_init_only = true;
        prop->has_setter = true;
        DiagnosePropertyAccessorCombination(
            *prop, PropertyDecl::Accessor::Kind::kInit, current_.loc,
            diagnostics_);
        auto accessor = ParsePropertyAccessorBody(PropertyDecl::Accessor::Kind::kInit,
                                                  current_.loc);
        prop->uses_field_keyword |= accessor.uses_field_keyword;
        prop->accessors.push_back(std::move(accessor));
      } else {
        Sync();
        if (IsSymbol(";"))
          Consume();
      }
    }
    ExpectSymbol("}", "expected '}' after property accessors");
  }

  // Expression-bodied property: => expr;
  if (MatchSymbol("=>")) {
    bool uses_field = false;
    prop->expression_body = ParsePropertyExpressionBody(uses_field);
    prop->uses_field_keyword |= uses_field;
    prop->has_getter = true;
    ExpectSymbol(";", "expected ';' after expression body");
  }

  // Property initializer: = value;
  if (MatchSymbol("=")) {
    prop->init = ParseExpression();
    ExpectSymbol(";", "expected ';' after property initializer");
  }

  return prop;
}

} // namespace polyglot::dotnet
