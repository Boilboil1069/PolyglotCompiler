/**
 * @file     parser.cpp
 * @brief    JavaScript parser implementation (ES2020+)
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Implements a recursive-descent parser for an idiomatic subset of ES2020+
 * sufficient for cross-language signature extraction and IR lowering:
 *   - Module-level imports/exports
 *   - Function and class declarations (including async, generators, getters,
 *     setters, private fields, static members)
 *   - let/const/var declarations
 *   - Full expression grammar (assignment, ternary, binary precedence climb,
 *     unary, update, member, call, new, optional chaining, spread, await,
 *     yield, template literals, arrow functions)
 *   - Statements: if/else, while, do-while, for, for-in, for-of, switch,
 *     try/catch/finally, throw, return, labeled, break/continue.
 *
 * Type information is harvested from immediately preceding JSDoc comments
 * (@param {T} name, @returns {T}) and surfaced via the AST so that
 * `ExtractSignatures` can supply useful types to the polyglot signature DB.
 */
#include "frontends/javascript/include/javascript_parser.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>

namespace polyglot::javascript {

namespace {

// Trim ASCII whitespace and leading "*" characters that JSDoc lines start with.
std::string TrimDocLine(const std::string &line) {
    size_t a = 0;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '*'))
        ++a;
    size_t b = line.size();
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r'))
        --b;
    return line.substr(a, b - a);
}

}  // namespace

// ============================================================================
// Token helpers
// ============================================================================

void JsParser::Advance() {
    current_ = lexer_.NextToken();
    auto doc = lexer_.TakeDocComment();
    if (!doc.empty()) pending_doc_ = doc;
}

bool JsParser::IsKeyword(const std::string &kw) const {
    return current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == kw;
}

bool JsParser::IsSymbol(const std::string &sym) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == sym;
}

bool JsParser::MatchKeyword(const std::string &kw) {
    if (IsKeyword(kw)) { Advance(); return true; }
    return false;
}

bool JsParser::MatchSymbol(const std::string &sym) {
    if (IsSymbol(sym)) { Advance(); return true; }
    return false;
}

bool JsParser::ExpectSymbol(const std::string &sym, const std::string &msg) {
    if (MatchSymbol(sym)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}

bool JsParser::ExpectKeyword(const std::string &kw, const std::string &msg) {
    if (MatchKeyword(kw)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}

void JsParser::ConsumeSemicolon() {
    // Automatic Semicolon Insertion: accept ';' but never error on its absence.
    MatchSymbol(";");
}

// ============================================================================
// JSDoc type parsing — convert "{number}" / "{Array<string>}" / "{a|b}" to AST.
// ============================================================================

std::shared_ptr<TypeNode> JsParser::ParseJsdocType(const std::string &raw) {
    if (raw.empty()) return nullptr;

    // Recursive descent over a tiny grammar:
    //   type   := union
    //   union  := generic ('|' generic)*
    //   generic:= ident ('<' type (',' type)* '>')?
    struct Cursor {
        const std::string &s;
        size_t i{0};
        void Skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }
    };
    Cursor c{raw};

    std::function<std::shared_ptr<TypeNode>()> parseType;
    auto parseGeneric = [&]() -> std::shared_ptr<TypeNode> {
        c.Skip();
        std::string ident;
        while (c.i < raw.size() &&
               (std::isalnum(static_cast<unsigned char>(raw[c.i])) ||
                raw[c.i] == '_' || raw[c.i] == '$' || raw[c.i] == '.')) {
            ident.push_back(raw[c.i++]);
        }
        if (ident.empty()) return nullptr;
        c.Skip();
        if (c.i < raw.size() && raw[c.i] == '<') {
            ++c.i;
            auto gt = std::make_shared<GenericType>();
            gt->name = ident;
            while (true) {
                auto a = parseType();
                if (a) gt->args.push_back(a);
                c.Skip();
                if (c.i < raw.size() && raw[c.i] == ',') { ++c.i; continue; }
                break;
            }
            c.Skip();
            if (c.i < raw.size() && raw[c.i] == '>') ++c.i;
            return gt;
        }
        auto nt = std::make_shared<NamedType>();
        nt->name = ident;
        return nt;
    };
    parseType = [&]() -> std::shared_ptr<TypeNode> {
        auto first = parseGeneric();
        if (!first) return nullptr;
        c.Skip();
        if (c.i < raw.size() && raw[c.i] == '|') {
            auto un = std::make_shared<UnionType>();
            un->options.push_back(first);
            while (c.i < raw.size() && raw[c.i] == '|') {
                ++c.i;
                auto more = parseGeneric();
                if (more) un->options.push_back(more);
                c.Skip();
            }
            return un;
        }
        return first;
    };
    return parseType();
}

// ============================================================================
// ParseModule
// ============================================================================

void JsParser::ParseModule() {
    module_ = std::make_shared<Module>();
    Advance();
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        auto stmt = ParseTopLevel();
        if (stmt) module_->body.push_back(stmt);
        else Advance();  // resync on error
    }
}

std::shared_ptr<Module> JsParser::TakeModule() { return std::move(module_); }

std::shared_ptr<Statement> JsParser::ParseTopLevel() {
    if (IsKeyword("import")) return ParseImportDecl();
    if (IsKeyword("export")) return ParseExportDecl();
    return ParseStatement();
}

// ============================================================================
// Imports / Exports
// ============================================================================

std::shared_ptr<Statement> JsParser::ParseImportDecl() {
    auto loc = current_.loc;
    auto decl = std::make_shared<ImportDecl>();
    decl->loc = loc;
    Advance();  // consume 'import'

    // Handle "import 'side-effect';" — bare module specifier
    if (current_.kind == frontends::TokenKind::kString) {
        decl->source = current_.lexeme;
        Advance();
        ConsumeSemicolon();
        return decl;
    }

    // Default import?
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        ImportDecl::Specifier spec;
        spec.local = current_.lexeme;
        spec.imported = "default";
        spec.is_default = true;
        decl->specifiers.push_back(spec);
        Advance();
        if (MatchSymbol(",")) {
            // continue with namespace/named imports
        }
    }

    // Namespace import: * as ns
    if (MatchSymbol("*")) {
        ExpectKeyword("as", "expected 'as' after '*'");
        ImportDecl::Specifier spec;
        spec.is_namespace = true;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            spec.local = current_.lexeme;
            Advance();
        }
        decl->specifiers.push_back(spec);
    } else if (MatchSymbol("{")) {
        // Named imports
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            ImportDecl::Specifier spec;
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                spec.imported = current_.lexeme;
                spec.local = spec.imported;
                Advance();
            }
            if (MatchKeyword("as")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    spec.local = current_.lexeme;
                    Advance();
                }
            }
            decl->specifiers.push_back(spec);
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol("}", "expected '}' to close import specifiers");
    }

    if (MatchKeyword("from")) {
        if (current_.kind == frontends::TokenKind::kString) {
            decl->source = current_.lexeme;
            Advance();
        }
    }
    ConsumeSemicolon();
    return decl;
}

std::shared_ptr<Statement> JsParser::ParseExportDecl() {
    auto loc = current_.loc;
    Advance();  // consume 'export'

    bool is_default = MatchKeyword("default");

    // export { a, b as c } [from "..."]
    if (IsSymbol("{")) {
        auto decl = std::make_shared<ExportDecl>();
        decl->loc = loc;
        decl->is_default = is_default;
        Advance();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            ExportDecl::Specifier spec;
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                spec.local = current_.lexeme;
                spec.exported = spec.local;
                Advance();
            }
            if (MatchKeyword("as")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    spec.exported = current_.lexeme;
                    Advance();
                }
            }
            decl->specifiers.push_back(spec);
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol("}", "expected '}'");
        if (MatchKeyword("from") && current_.kind == frontends::TokenKind::kString) {
            decl->source = current_.lexeme;
            Advance();
        }
        ConsumeSemicolon();
        return decl;
    }

    // export function/class/let/const/var ...
    auto inner = std::shared_ptr<Statement>();
    if (IsKeyword("function") || (IsKeyword("async") /* function */)) {
        inner = ParseFunctionDecl(true, is_default);
    } else if (IsKeyword("class")) {
        inner = ParseClassDecl(true, is_default);
    } else if (IsKeyword("let") || IsKeyword("const") || IsKeyword("var")) {
        inner = ParseVariableDecl();
    } else if (is_default) {
        // export default <expression>;
        auto decl = std::make_shared<ExportDecl>();
        decl->loc = loc;
        decl->is_default = true;
        decl->default_expr = ParseAssignment();
        ConsumeSemicolon();
        return decl;
    } else {
        inner = ParseStatement();
    }

    auto decl = std::make_shared<ExportDecl>();
    decl->loc = loc;
    decl->is_default = is_default;
    decl->declaration = inner;
    return decl;
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> JsParser::ParseStatement() {
    if (IsSymbol("{"))             return ParseBlock();
    if (IsSymbol(";"))             { Advance(); return std::make_shared<BlockStatement>(); }
    if (IsKeyword("let") || IsKeyword("const") || IsKeyword("var"))
                                   return ParseVariableDecl();
    if (IsKeyword("if"))           return ParseIf();
    if (IsKeyword("while"))        return ParseWhile();
    if (IsKeyword("do"))           return ParseDoWhile();
    if (IsKeyword("for"))          return ParseFor();
    if (IsKeyword("switch"))       return ParseSwitch();
    if (IsKeyword("try"))          return ParseTry();
    if (IsKeyword("return"))       return ParseReturn();
    if (IsKeyword("throw"))        return ParseThrow();
    if (IsKeyword("function"))     return ParseFunctionDecl(false, false);
    if (IsKeyword("class"))        return ParseClassDecl(false, false);
    if (IsKeyword("break")) {
        auto loc = current_.loc;
        Advance();
        auto s = std::make_shared<BreakStatement>();
        s->loc = loc;
        if (current_.kind == frontends::TokenKind::kIdentifier && !IsSymbol(";")) {
            s->label = current_.lexeme;
            Advance();
        }
        ConsumeSemicolon();
        return s;
    }
    if (IsKeyword("continue")) {
        auto loc = current_.loc;
        Advance();
        auto s = std::make_shared<ContinueStatement>();
        s->loc = loc;
        if (current_.kind == frontends::TokenKind::kIdentifier && !IsSymbol(";")) {
            s->label = current_.lexeme;
            Advance();
        }
        ConsumeSemicolon();
        return s;
    }
    if (IsKeyword("async")) {
        // async function ...
        // (async arrow handled inside primary)
        auto saved = current_;
        Advance();
        if (IsKeyword("function")) {
            auto fn = std::dynamic_pointer_cast<FunctionDecl>(ParseFunctionDecl(false, false));
            if (fn) fn->is_async = true;
            return fn;
        }
        // not actually a function — fall through as an expression starting with 'async'
        // We rebuild the expression manually because ParsePrimary needs the original token.
        auto ident = std::make_shared<Identifier>();
        ident->loc = saved.loc;
        ident->name = saved.lexeme;
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->loc = saved.loc;
        expr_stmt->expr = ident;
        ConsumeSemicolon();
        return expr_stmt;
    }

    // Labeled statement: identifier ':' statement
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto saved = current_;
        auto state = lexer_.SaveState();
        auto saved_doc = pending_doc_;
        Advance();
        if (IsSymbol(":")) {
            Advance();
            auto labeled = std::make_shared<LabeledStatement>();
            labeled->loc = saved.loc;
            labeled->label = saved.lexeme;
            labeled->body = ParseStatement();
            return labeled;
        }
        // Roll back and treat as expression statement.
        lexer_.RestoreState(state);
        current_ = saved;
        pending_doc_ = saved_doc;
    }

    return ParseExprStatement();
}

std::shared_ptr<BlockStatement> JsParser::ParseBlock() {
    auto block = std::make_shared<BlockStatement>();
    block->loc = current_.loc;
    ExpectSymbol("{", "expected '{'");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto stmt = ParseStatement();
        if (stmt) block->statements.push_back(stmt);
        else Advance();
    }
    ExpectSymbol("}", "expected '}'");
    return block;
}

std::shared_ptr<Statement> JsParser::ParseVariableDecl() {
    auto loc = current_.loc;
    auto decl = std::make_shared<VariableDecl>();
    decl->loc = loc;
    decl->kind = current_.lexeme;
    Advance();

    while (true) {
        VariableDecl::Declarator d;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            d.name = current_.lexeme;
            Advance();
        } else if (IsSymbol("[") || IsSymbol("{")) {
            // Destructuring — capture as opaque text for now and skip.
            std::string opener = current_.lexeme;
            std::string closer = (opener == "[") ? "]" : "}";
            d.name = opener;
            int depth = 1;
            Advance();
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                if (current_.lexeme == opener) depth++;
                else if (current_.lexeme == closer) depth--;
                if (depth > 0) d.name += current_.lexeme;
                Advance();
            }
            d.name += closer;
        } else {
            diagnostics_.Report(current_.loc, "expected variable name");
            break;
        }
        if (MatchSymbol("=")) {
            d.init = ParseAssignment();
        }
        decl->decls.push_back(d);
        if (!MatchSymbol(",")) break;
    }
    ConsumeSemicolon();
    return decl;
}

std::shared_ptr<Statement> JsParser::ParseIf() {
    auto loc = current_.loc;
    Advance();
    ExpectSymbol("(", "expected '(' after 'if'");
    auto stmt = std::make_shared<IfStatement>();
    stmt->loc = loc;
    stmt->condition = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    stmt->then_branch = ParseStatement();
    if (MatchKeyword("else")) {
        stmt->else_branch = ParseStatement();
    }
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseWhile() {
    auto loc = current_.loc;
    Advance();
    ExpectSymbol("(", "expected '(' after 'while'");
    auto stmt = std::make_shared<WhileStatement>();
    stmt->loc = loc;
    stmt->condition = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    stmt->body = ParseStatement();
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseDoWhile() {
    auto loc = current_.loc;
    Advance();
    auto stmt = std::make_shared<DoWhileStatement>();
    stmt->loc = loc;
    stmt->body = ParseStatement();
    ExpectKeyword("while", "expected 'while' after do-block");
    ExpectSymbol("(", "expected '('");
    stmt->condition = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    ConsumeSemicolon();
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseFor() {
    auto loc = current_.loc;
    Advance();
    ExpectSymbol("(", "expected '(' after 'for'");

    // Distinguish for-in/of from C-style for. Save state to roll back.
    auto state = lexer_.SaveState();
    auto saved = current_;
    auto saved_doc = pending_doc_;

    // Try for-in / for-of: optionally [let|const|var] IDENT in|of EXPR
    std::string var_kind;
    if (IsKeyword("let") || IsKeyword("const") || IsKeyword("var")) {
        var_kind = current_.lexeme;
        Advance();
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        std::string vname = current_.lexeme;
        auto vloc = current_.loc;
        Advance();
        if (MatchKeyword("in") || (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "of")) {
            bool is_of = current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "of";
            if (is_of) Advance();
            auto stmt = std::make_shared<ForInOfStatement>();
            stmt->loc = loc;
            stmt->is_of = is_of;
            stmt->var_kind = var_kind;
            stmt->var_name = vname;
            (void)vloc;
            stmt->iterable = ParseExpression();
            ExpectSymbol(")", "expected ')'");
            stmt->body = ParseStatement();
            return stmt;
        }
        // Not for-in/of; restore.
        lexer_.RestoreState(state);
        current_ = saved;
        pending_doc_ = saved_doc;
    } else {
        lexer_.RestoreState(state);
        current_ = saved;
        pending_doc_ = saved_doc;
    }

    // C-style for(init; cond; update) body
    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = loc;
    if (!IsSymbol(";")) {
        if (IsKeyword("let") || IsKeyword("const") || IsKeyword("var")) {
            stmt->init = ParseVariableDecl();   // already consumes ';'
        } else {
            auto e = std::make_shared<ExprStatement>();
            e->loc = current_.loc;
            e->expr = ParseExpression();
            stmt->init = e;
            ExpectSymbol(";", "expected ';'");
        }
    } else {
        Advance();
    }
    if (!IsSymbol(";")) {
        stmt->condition = ParseExpression();
    }
    ExpectSymbol(";", "expected ';'");
    if (!IsSymbol(")")) {
        stmt->update = ParseExpression();
    }
    ExpectSymbol(")", "expected ')'");
    stmt->body = ParseStatement();
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseSwitch() {
    auto loc = current_.loc;
    Advance();
    ExpectSymbol("(", "expected '('");
    auto stmt = std::make_shared<SwitchStatement>();
    stmt->loc = loc;
    stmt->discriminant = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    ExpectSymbol("{", "expected '{'");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        SwitchStatement::Case c;
        if (MatchKeyword("case")) {
            c.test = ParseExpression();
            ExpectSymbol(":", "expected ':'");
        } else if (MatchKeyword("default")) {
            ExpectSymbol(":", "expected ':'");
        } else {
            diagnostics_.Report(current_.loc, "expected 'case' or 'default'");
            Advance();
            continue;
        }
        while (!IsKeyword("case") && !IsKeyword("default") && !IsSymbol("}") &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            auto s = ParseStatement();
            if (s) c.body.push_back(s);
            else Advance();
        }
        stmt->cases.push_back(c);
    }
    ExpectSymbol("}", "expected '}'");
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseTry() {
    auto loc = current_.loc;
    Advance();
    auto stmt = std::make_shared<TryStatement>();
    stmt->loc = loc;
    stmt->block = ParseBlock();
    if (MatchKeyword("catch")) {
        if (MatchSymbol("(")) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                stmt->catch_var = current_.lexeme;
                Advance();
            }
            ExpectSymbol(")", "expected ')'");
        }
        stmt->handler = ParseBlock();
    }
    if (MatchKeyword("finally")) {
        stmt->finalizer = ParseBlock();
    }
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseReturn() {
    auto loc = current_.loc;
    Advance();
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = loc;
    if (!IsSymbol(";") && !IsSymbol("}") &&
        current_.kind != frontends::TokenKind::kEndOfFile) {
        stmt->value = ParseExpression();
    }
    ConsumeSemicolon();
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseThrow() {
    auto loc = current_.loc;
    Advance();
    auto stmt = std::make_shared<ThrowStatement>();
    stmt->loc = loc;
    stmt->value = ParseExpression();
    ConsumeSemicolon();
    return stmt;
}

std::shared_ptr<Statement> JsParser::ParseExprStatement() {
    auto loc = current_.loc;
    auto expr = ParseExpression();
    ConsumeSemicolon();
    auto stmt = std::make_shared<ExprStatement>();
    stmt->loc = loc;
    stmt->expr = expr;
    return stmt;
}

// ============================================================================
// Function / Class
// ============================================================================

std::vector<ArrowFunction::Param> JsParser::ParseFunctionParams() {
    std::vector<ArrowFunction::Param> params;
    ExpectSymbol("(", "expected '(' before parameter list");

    // Build a JSDoc @param map from pending_doc_ so we can fill in types.
    std::unordered_map<std::string, std::shared_ptr<TypeNode>> param_types;
    std::shared_ptr<TypeNode> return_type;
    if (!pending_doc_.empty()) {
        std::istringstream is(pending_doc_);
        std::string line;
        while (std::getline(is, line)) {
            line = TrimDocLine(line);
            // @param {Type} name [- desc]
            if (line.rfind("@param", 0) == 0) {
                auto a = line.find('{');
                auto b = (a == std::string::npos) ? std::string::npos : line.find('}', a);
                std::string type_str;
                std::string rest;
                if (a != std::string::npos && b != std::string::npos) {
                    type_str = line.substr(a + 1, b - a - 1);
                    rest = line.substr(b + 1);
                } else {
                    rest = line.substr(6);
                }
                // Skip whitespace.
                size_t i = 0;
                while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                std::string name;
                while (i < rest.size() &&
                       (std::isalnum(static_cast<unsigned char>(rest[i])) ||
                        rest[i] == '_' || rest[i] == '$')) {
                    name.push_back(rest[i++]);
                }
                if (!name.empty()) {
                    param_types[name] = ParseJsdocType(type_str);
                }
            } else if (line.rfind("@returns", 0) == 0 || line.rfind("@return", 0) == 0) {
                auto a = line.find('{');
                auto b = (a == std::string::npos) ? std::string::npos : line.find('}', a);
                if (a != std::string::npos && b != std::string::npos) {
                    return_type = ParseJsdocType(line.substr(a + 1, b - a - 1));
                }
            }
        }
    }
    pending_doc_.clear();

    while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        ArrowFunction::Param param;
        if (MatchSymbol("...")) param.rest = true;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            param.name = current_.lexeme;
            Advance();
        } else if (IsSymbol("[") || IsSymbol("{")) {
            std::string opener = current_.lexeme;
            std::string closer = (opener == "[") ? "]" : "}";
            param.name = opener;
            int depth = 1;
            Advance();
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                if (current_.lexeme == opener) depth++;
                else if (current_.lexeme == closer) depth--;
                if (depth > 0) param.name += current_.lexeme;
                Advance();
            }
            param.name += closer;
        }
        if (MatchSymbol("=")) {
            param.default_value = ParseAssignment();
        }
        auto it = param_types.find(param.name);
        if (it != param_types.end()) param.type = it->second;
        params.push_back(param);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol(")", "expected ')'");
    // Stash the JSDoc-derived return type on the parser context — caller picks it up.
    // We accomplish this by briefly setting pending_doc_ to a sentinel marker
    // and using a side channel — simpler: caller passes by reference.  For
    // brevity here we rely on the helper below.
    if (return_type) {
        // Encode as an extra "$return$" pseudo-param so the caller can pop it.
        ArrowFunction::Param ret;
        ret.name = "$return$";
        ret.type = return_type;
        params.push_back(ret);
    }
    return params;
}

namespace {
// Pop the encoded pseudo-return-param appended by ParseFunctionParams.
std::shared_ptr<TypeNode> ExtractReturnType(std::vector<ArrowFunction::Param> &params) {
    if (!params.empty() && params.back().name == "$return$") {
        auto t = params.back().type;
        params.pop_back();
        return t;
    }
    return nullptr;
}
}  // namespace

std::shared_ptr<Statement> JsParser::ParseFunctionDecl(bool exported, bool exported_default) {
    auto loc = current_.loc;
    bool is_async = false;
    if (MatchKeyword("async")) is_async = true;
    ExpectKeyword("function", "expected 'function'");
    bool is_generator = MatchSymbol("*");

    auto fn = std::make_shared<FunctionDecl>();
    fn->loc = loc;
    fn->is_async = is_async;
    fn->is_generator = is_generator;
    fn->exported = exported;
    fn->exported_default = exported_default;

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        fn->name = current_.lexeme;
        Advance();
    }
    fn->params = ParseFunctionParams();
    fn->return_type = ExtractReturnType(fn->params);
    if (IsSymbol("{")) {
        fn->body = ParseBlock();
    }
    return fn;
}

std::shared_ptr<MethodDecl> JsParser::ParseMethodDecl(bool is_static) {
    auto method = std::make_shared<MethodDecl>();
    method->loc = current_.loc;
    method->is_static = is_static;

    if (MatchKeyword("async")) method->is_async = true;

    // get / set / generator marker
    bool is_getter = false, is_setter = false;
    if (current_.kind == frontends::TokenKind::kIdentifier &&
        (current_.lexeme == "get" || current_.lexeme == "set")) {
        // Look-ahead: "get name(" → getter.  "get(" → method named "get".
        auto saved = current_;
        auto state = lexer_.SaveState();
        auto saved_doc = pending_doc_;
        std::string keyword = current_.lexeme;
        Advance();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            if (keyword == "get") is_getter = true; else is_setter = true;
        } else {
            lexer_.RestoreState(state);
            current_ = saved;
            pending_doc_ = saved_doc;
        }
    }
    if (MatchSymbol("*")) method->is_generator = true;

    // Method name (identifier, # private, or string literal)
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        method->name = current_.lexeme;
        if (!method->name.empty() && method->name[0] == '#') method->is_private = true;
        Advance();
    } else if (current_.kind == frontends::TokenKind::kString) {
        method->name = current_.lexeme;
        Advance();
    }

    if (is_getter)         method->kind = MethodDecl::Kind::kGetter;
    else if (is_setter)    method->kind = MethodDecl::Kind::kSetter;
    else if (method->name == "constructor") method->kind = MethodDecl::Kind::kConstructor;

    method->params = ParseFunctionParams();
    method->return_type = ExtractReturnType(method->params);
    if (IsSymbol("{")) {
        method->body = ParseBlock();
    }
    return method;
}

std::shared_ptr<Statement> JsParser::ParseClassDecl(bool exported, bool exported_default) {
    auto loc = current_.loc;
    Advance();  // consume 'class'

    auto cls = std::make_shared<ClassDecl>();
    cls->loc = loc;
    cls->exported = exported;
    cls->exported_default = exported_default;

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        cls->name = current_.lexeme;
        Advance();
    }
    if (MatchKeyword("extends")) {
        cls->superclass = ParseLeftHandSide();
    }
    ExpectSymbol("{", "expected '{'");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (MatchSymbol(";")) continue;
        bool is_static = MatchKeyword("static");

        // Field detection: identifier (or # private) followed by '=' or ';'
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "#")) {
            auto state = lexer_.SaveState();
            auto saved = current_;
            auto saved_doc = pending_doc_;
            std::string field_name = saved.lexeme;
            bool is_private = false;
            if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "#") {
                is_private = true;
                Advance();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    field_name = "#" + current_.lexeme;
                    Advance();
                }
            } else {
                Advance();
            }
            if (IsSymbol("=") || IsSymbol(";")) {
                auto field = std::make_shared<FieldDecl>();
                field->loc = saved.loc;
                field->name = field_name;
                field->is_static = is_static;
                field->is_private = is_private;
                if (MatchSymbol("=")) field->init = ParseAssignment();
                ConsumeSemicolon();
                cls->members.push_back(field);
                continue;
            }
            // Not a field: roll back and treat as method.
            lexer_.RestoreState(state);
            current_ = saved;
            pending_doc_ = saved_doc;
        }

        auto method = ParseMethodDecl(is_static);
        if (method) cls->members.push_back(method);
    }
    ExpectSymbol("}", "expected '}'");
    return cls;
}

// ============================================================================
// Expressions
// ============================================================================

std::shared_ptr<Expression> JsParser::ParseExpression() {
    auto expr = ParseAssignment();
    while (IsSymbol(",")) {
        Advance();
        if (!expr) break;
        // Convert to or extend a SequenceExpr.
        auto seq = std::dynamic_pointer_cast<SequenceExpr>(expr);
        if (!seq) {
            seq = std::make_shared<SequenceExpr>();
            seq->loc = expr->loc;
            seq->exprs.push_back(expr);
            expr = seq;
        }
        seq->exprs.push_back(ParseAssignment());
    }
    return expr;
}

std::shared_ptr<Expression> JsParser::ParseAssignment() {
    // Try arrow function before falling back to conditional.
    if (IsKeyword("async")) {
        // `async (params) =>` or `async ident =>` or `async function …`
        auto state = lexer_.SaveState();
        auto saved = current_;
        auto saved_doc = pending_doc_;
        Advance();
        if (IsKeyword("function")) {
            // Parse function expression manually.
            auto loc = saved.loc;
            Advance();  // consume 'function'
            bool is_generator = MatchSymbol("*");
            auto fe = std::make_shared<FunctionExpr>();
            fe->loc = loc;
            fe->is_async = true;
            fe->is_generator = is_generator;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                fe->name = current_.lexeme;
                Advance();
            }
            fe->params = ParseFunctionParams();
            fe->return_type = ExtractReturnType(fe->params);
            if (IsSymbol("{")) fe->body = ParseBlock();
            return fe;
        }
        // not async function — restore and fall through.
        lexer_.RestoreState(state);
        current_ = saved;
        pending_doc_ = saved_doc;
    }

    if (IsKeyword("function")) {
        auto loc = current_.loc;
        Advance();
        bool is_generator = MatchSymbol("*");
        auto fe = std::make_shared<FunctionExpr>();
        fe->loc = loc;
        fe->is_generator = is_generator;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            fe->name = current_.lexeme;
            Advance();
        }
        fe->params = ParseFunctionParams();
        fe->return_type = ExtractReturnType(fe->params);
        if (IsSymbol("{")) fe->body = ParseBlock();
        return fe;
    }

    auto cond = ParseConditional();

    static const std::vector<std::string> assign_ops = {
        "=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=",
        "&=", "|=", "^=", "&&=", "||=", "??="
    };
    for (auto &op : assign_ops) {
        if (IsSymbol(op)) {
            Advance();
            auto rhs = ParseAssignment();
            auto a = std::make_shared<AssignExpr>();
            a->loc = cond ? cond->loc : current_.loc;
            a->op = op;
            a->target = cond;
            a->value = rhs;
            return a;
        }
    }
    return cond;
}

std::shared_ptr<Expression> JsParser::ParseConditional() {
    auto test = ParseBinary(0);
    if (MatchSymbol("?")) {
        auto then_branch = ParseAssignment();
        ExpectSymbol(":", "expected ':' in ternary");
        auto else_branch = ParseAssignment();
        auto c = std::make_shared<ConditionalExpr>();
        c->loc = test ? test->loc : current_.loc;
        c->test = test;
        c->then_branch = then_branch;
        c->else_branch = else_branch;
        return c;
    }
    return test;
}

int JsParser::GetBinaryPrecedence(const std::string &op) const {
    // Mirrors ECMAScript operator precedence (lower = looser binding).
    if (op == "??" || op == "||") return 1;
    if (op == "&&")               return 2;
    if (op == "|")                return 3;
    if (op == "^")                return 4;
    if (op == "&")                return 5;
    if (op == "==" || op == "!=" || op == "===" || op == "!==") return 6;
    if (op == "<" || op == "<=" || op == ">" || op == ">=" ||
        op == "instanceof" || op == "in") return 7;
    if (op == "<<" || op == ">>" || op == ">>>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    if (op == "**") return 11;
    return 0;
}

std::shared_ptr<Expression> JsParser::ParseBinary(int min_precedence) {
    auto left = ParseUnary();
    while (true) {
        std::string op = current_.lexeme;
        bool is_op_keyword = (current_.kind == frontends::TokenKind::kKeyword &&
                              (op == "instanceof" || op == "in"));
        bool is_op_symbol = (current_.kind == frontends::TokenKind::kSymbol);
        if (!is_op_keyword && !is_op_symbol) break;
        int prec = GetBinaryPrecedence(op);
        if (prec < min_precedence || prec == 0) break;
        Advance();
        // Right-associative for **.
        int next_min = (op == "**") ? prec : prec + 1;
        auto right = ParseBinary(next_min);
        auto loc = left ? left->loc : current_.loc;
        if (op == "&&" || op == "||" || op == "??") {
            auto le = std::make_shared<LogicalExpr>();
            le->loc = loc;
            le->op = op;
            le->left = left;
            le->right = right;
            left = le;
        } else {
            auto be = std::make_shared<BinaryExpr>();
            be->loc = loc;
            be->op = op;
            be->left = left;
            be->right = right;
            left = be;
        }
    }
    return left;
}

std::shared_ptr<Expression> JsParser::ParseUnary() {
    static const std::vector<std::string> prefix_ops_kw = {
        "typeof", "void", "delete"
    };
    for (auto &kw : prefix_ops_kw) {
        if (IsKeyword(kw)) {
            auto loc = current_.loc;
            Advance();
            auto u = std::make_shared<UnaryExpr>();
            u->loc = loc;
            u->op = kw;
            u->operand = ParseUnary();
            return u;
        }
    }
    if (IsKeyword("await")) {
        auto loc = current_.loc;
        Advance();
        auto a = std::make_shared<AwaitExpr>();
        a->loc = loc;
        a->arg = ParseUnary();
        return a;
    }
    if (IsKeyword("yield")) {
        auto loc = current_.loc;
        Advance();
        auto y = std::make_shared<YieldExpr>();
        y->loc = loc;
        y->delegate = MatchSymbol("*");
        if (!IsSymbol(";") && !IsSymbol(",") && !IsSymbol(")") && !IsSymbol("]") &&
            !IsSymbol("}")) {
            y->arg = ParseAssignment();
        }
        return y;
    }
    static const std::vector<std::string> prefix_ops = { "!", "-", "+", "~" };
    for (auto &op : prefix_ops) {
        if (IsSymbol(op)) {
            auto loc = current_.loc;
            Advance();
            auto u = std::make_shared<UnaryExpr>();
            u->loc = loc;
            u->op = op;
            u->operand = ParseUnary();
            return u;
        }
    }
    return ParseUpdate();
}

std::shared_ptr<Expression> JsParser::ParseUpdate() {
    if (IsSymbol("++") || IsSymbol("--")) {
        auto loc = current_.loc;
        std::string op = current_.lexeme;
        Advance();
        auto u = std::make_shared<UpdateExpr>();
        u->loc = loc;
        u->op = op;
        u->prefix = true;
        u->target = ParseLeftHandSide();
        return u;
    }
    auto expr = ParseLeftHandSide();
    if (IsSymbol("++") || IsSymbol("--")) {
        auto loc = current_.loc;
        std::string op = current_.lexeme;
        Advance();
        auto u = std::make_shared<UpdateExpr>();
        u->loc = loc;
        u->op = op;
        u->prefix = false;
        u->target = expr;
        return u;
    }
    return expr;
}

std::shared_ptr<Expression> JsParser::ParseLeftHandSide() {
    if (MatchKeyword("new")) {
        auto callee = ParseLeftHandSide();
        auto call = std::make_shared<CallExpr>();
        call->loc = callee ? callee->loc : current_.loc;
        call->callee = callee;
        call->is_new = true;
        // If the previous parse already consumed a CallExpr, peel one layer.
        if (auto inner = std::dynamic_pointer_cast<CallExpr>(callee)) {
            call->callee = inner->callee;
            call->args = inner->args;
        }
        return ParseCallTail(call, true);
    }
    auto primary = ParsePrimary();
    return ParseCallTail(primary, false);
}

std::shared_ptr<Expression> JsParser::ParseCallTail(
    std::shared_ptr<Expression> expr, bool /*is_new*/) {
    while (true) {
        if (MatchSymbol(".")) {
            auto m = std::make_shared<MemberExpr>();
            m->loc = expr ? expr->loc : current_.loc;
            m->object = expr;
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                m->property = current_.lexeme;
                Advance();
            }
            expr = m;
        } else if (MatchSymbol("?.")) {
            auto m = std::make_shared<MemberExpr>();
            m->loc = expr ? expr->loc : current_.loc;
            m->object = expr;
            m->optional = true;
            if (IsSymbol("(")) {
                // Optional call: a?.()
                auto call = std::make_shared<CallExpr>();
                call->loc = expr ? expr->loc : current_.loc;
                call->callee = expr;
                call->optional = true;
                Advance();  // '('
                while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                    if (MatchSymbol("...")) {
                        auto sp = std::make_shared<SpreadExpr>();
                        sp->arg = ParseAssignment();
                        call->args.push_back(sp);
                    } else {
                        call->args.push_back(ParseAssignment());
                    }
                    if (!MatchSymbol(",")) break;
                }
                ExpectSymbol(")", "expected ')'");
                expr = call;
                continue;
            }
            if (IsSymbol("[")) {
                Advance();
                m->computed = true;
                auto key = ParseExpression();
                if (auto id = std::dynamic_pointer_cast<Identifier>(key)) {
                    m->property = id->name;
                } else if (auto lit = std::dynamic_pointer_cast<Literal>(key)) {
                    m->property = lit->value;
                }
                ExpectSymbol("]", "expected ']'");
                expr = m;
                continue;
            }
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                m->property = current_.lexeme;
                Advance();
            }
            expr = m;
        } else if (MatchSymbol("[")) {
            auto m = std::make_shared<MemberExpr>();
            m->loc = expr ? expr->loc : current_.loc;
            m->object = expr;
            m->computed = true;
            auto idx = ParseExpression();
            if (auto id = std::dynamic_pointer_cast<Identifier>(idx)) {
                m->property = id->name;
            } else if (auto lit = std::dynamic_pointer_cast<Literal>(idx)) {
                m->property = lit->value;
            }
            ExpectSymbol("]", "expected ']'");
            expr = m;
        } else if (IsSymbol("(")) {
            Advance();
            auto call = std::make_shared<CallExpr>();
            call->loc = expr ? expr->loc : current_.loc;
            call->callee = expr;
            while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                if (MatchSymbol("...")) {
                    auto sp = std::make_shared<SpreadExpr>();
                    sp->arg = ParseAssignment();
                    call->args.push_back(sp);
                } else {
                    call->args.push_back(ParseAssignment());
                }
                if (!MatchSymbol(",")) break;
            }
            ExpectSymbol(")", "expected ')'");
            expr = call;
        } else {
            break;
        }
    }
    return expr;
}

std::shared_ptr<Expression> JsParser::ParsePrimary() {
    auto loc = current_.loc;

    if (current_.kind == frontends::TokenKind::kNumber) {
        auto lit = std::make_shared<Literal>();
        lit->loc = loc;
        lit->value = current_.lexeme;
        lit->kind = (!current_.lexeme.empty() && current_.lexeme.back() == 'n')
                        ? Literal::Kind::kBigInt
                        : Literal::Kind::kNumber;
        Advance();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kString) {
        auto lit = std::make_shared<Literal>();
        lit->loc = loc;
        lit->value = current_.lexeme;
        lit->kind = (!current_.lexeme.empty() && current_.lexeme[0] == '`')
                        ? Literal::Kind::kTemplateString
                        : (!current_.lexeme.empty() && current_.lexeme[0] == '/')
                              ? Literal::Kind::kRegex
                              : Literal::Kind::kString;
        Advance();
        // Convert template literal lexeme into a TemplateLiteral expression for
        // simple cases (we keep the raw string here; lowering-time evaluation
        // suffices for IR purposes).
        return lit;
    }
    if (IsKeyword("true") || IsKeyword("false")) {
        auto lit = std::make_shared<Literal>();
        lit->loc = loc;
        lit->kind = Literal::Kind::kBool;
        lit->value = current_.lexeme;
        Advance();
        return lit;
    }
    if (IsKeyword("null")) {
        auto lit = std::make_shared<Literal>();
        lit->loc = loc;
        lit->kind = Literal::Kind::kNull;
        lit->value = "null";
        Advance();
        return lit;
    }
    if (IsKeyword("undefined")) {
        auto lit = std::make_shared<Literal>();
        lit->loc = loc;
        lit->kind = Literal::Kind::kUndefined;
        lit->value = "undefined";
        Advance();
        return lit;
    }
    if (IsKeyword("this") || IsKeyword("super")) {
        auto id = std::make_shared<Identifier>();
        id->loc = loc;
        id->name = current_.lexeme;
        Advance();
        return id;
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto saved = current_;
        auto state = lexer_.SaveState();
        auto saved_doc = pending_doc_;
        Advance();
        // Single-identifier arrow: `x => …`
        if (IsSymbol("=>")) {
            Advance();
            auto arrow = std::make_shared<ArrowFunction>();
            arrow->loc = saved.loc;
            ArrowFunction::Param p;
            p.name = saved.lexeme;
            arrow->params.push_back(p);
            if (IsSymbol("{")) {
                arrow->body = ParseBlock();
            } else {
                auto expr = ParseAssignment();
                auto wrap = std::make_shared<ReturnStatement>();
                wrap->loc = expr ? expr->loc : current_.loc;
                wrap->value = expr;
                arrow->body = wrap;
                arrow->body_is_expression = true;
            }
            return arrow;
        }
        auto id = std::make_shared<Identifier>();
        id->loc = saved.loc;
        id->name = saved.lexeme;
        return id;
    }
    if (IsSymbol("(")) {
        return ParseArrowOrParenExpr();
    }
    if (IsSymbol("[")) {
        return ParseArrayLiteral();
    }
    if (IsSymbol("{")) {
        return ParseObjectLiteral();
    }

    diagnostics_.Report(current_.loc, "unexpected token '" + current_.lexeme + "'");
    auto err = std::make_shared<Identifier>();
    err->loc = loc;
    err->name = current_.lexeme;
    Advance();
    return err;
}

std::shared_ptr<Expression> JsParser::ParseArrayLiteral() {
    auto loc = current_.loc;
    Advance();  // consume '['
    auto arr = std::make_shared<ArrayExpr>();
    arr->loc = loc;
    while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (MatchSymbol(",")) {
            arr->elements.push_back(nullptr);
            continue;
        }
        if (MatchSymbol("...")) {
            auto sp = std::make_shared<SpreadExpr>();
            sp->arg = ParseAssignment();
            arr->elements.push_back(sp);
        } else {
            arr->elements.push_back(ParseAssignment());
        }
        if (!IsSymbol("]")) MatchSymbol(",");
    }
    ExpectSymbol("]", "expected ']'");
    return arr;
}

std::shared_ptr<Expression> JsParser::ParseObjectLiteral() {
    auto loc = current_.loc;
    Advance();  // consume '{'
    auto obj = std::make_shared<ObjectExpr>();
    obj->loc = loc;
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        ObjectExpr::Property prop;
        if (MatchSymbol("...")) {
            prop.spread = true;
            prop.value = ParseAssignment();
            obj->properties.push_back(prop);
            if (!MatchSymbol(",")) break;
            continue;
        }
        if (MatchSymbol("[")) {
            prop.computed = true;
            auto key_expr = ParseExpression();
            if (auto id = std::dynamic_pointer_cast<Identifier>(key_expr)) {
                prop.key = id->name;
            }
            ExpectSymbol("]", "expected ']'");
        } else if (current_.kind == frontends::TokenKind::kIdentifier ||
                   current_.kind == frontends::TokenKind::kKeyword) {
            prop.key = current_.lexeme;
            Advance();
        } else if (current_.kind == frontends::TokenKind::kString ||
                   current_.kind == frontends::TokenKind::kNumber) {
            prop.key = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "unexpected token in object literal");
            Advance();
            continue;
        }
        if (MatchSymbol(":")) {
            prop.value = ParseAssignment();
        } else if (IsSymbol("(")) {
            // Method shorthand: `key(args) { … }`
            prop.is_method = true;
            auto fn = std::make_shared<FunctionExpr>();
            fn->loc = current_.loc;
            fn->params = ParseFunctionParams();
            fn->return_type = ExtractReturnType(fn->params);
            if (IsSymbol("{")) fn->body = ParseBlock();
            prop.value = fn;
        } else {
            // Shorthand: `{ key }` -> { key: key }
            prop.shorthand = true;
            auto id = std::make_shared<Identifier>();
            id->name = prop.key;
            id->loc = current_.loc;
            prop.value = id;
        }
        obj->properties.push_back(prop);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol("}", "expected '}'");
    return obj;
}

std::shared_ptr<Expression> JsParser::ParseArrowOrParenExpr() {
    auto loc = current_.loc;
    auto state = lexer_.SaveState();
    auto saved = current_;
    auto saved_doc = pending_doc_;

    // Try to parse as arrow function: `(params) =>` or `() =>`
    auto params = ParseFunctionParams();   // consumes through ')'
    auto ret_type = ExtractReturnType(params);
    if (IsSymbol("=>")) {
        Advance();
        auto arrow = std::make_shared<ArrowFunction>();
        arrow->loc = loc;
        arrow->params = params;
        arrow->return_type = ret_type;
        if (IsSymbol("{")) {
            arrow->body = ParseBlock();
        } else {
            auto expr = ParseAssignment();
            auto wrap = std::make_shared<ReturnStatement>();
            wrap->loc = expr ? expr->loc : current_.loc;
            wrap->value = expr;
            arrow->body = wrap;
            arrow->body_is_expression = true;
        }
        return arrow;
    }
    // Not an arrow; restore and parse as parenthesised expression.
    lexer_.RestoreState(state);
    current_ = saved;
    pending_doc_ = saved_doc;
    Advance();  // consume '('
    auto expr = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    return expr;
}

}  // namespace polyglot::javascript
