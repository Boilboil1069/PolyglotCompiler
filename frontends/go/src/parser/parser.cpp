/**
 * @file     parser.cpp
 * @brief    Go language parser
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Recursive-descent parser covering the Go subset that the polyglot
 * compiler can lower to IR.  Supported constructs:
 *   - Package and import declarations
 *   - var / const / type declarations (single and grouped)
 *   - Functions (with method receivers, multi-return, variadic params)
 *   - Block statements with the full Go statement grammar including
 *     if/for/switch/select/go/defer/return/break/continue/goto/fallthrough
 *   - Composite literals, function literals, type assertions, channel
 *     send/receive, and the standard expression precedence ladder.
 *
 * The parser leans on the lexer's automatic semicolon insertion: the
 * `;` token may be either explicit or synthetic at end-of-line.
 */
#include "frontends/go/include/go_parser.h"

#include <utility>

namespace polyglot::go {

void GoParser::Advance() {
    current_ = lexer_.NextToken();
    auto d = lexer_.TakePendingDoc();
    if (!d.empty()) pending_doc_ = std::move(d);
}

bool GoParser::IsKeyword(const std::string &k) const {
    return current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == k;
}
bool GoParser::IsSymbol(const std::string &s) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == s;
}
bool GoParser::MatchKeyword(const std::string &k) {
    if (IsKeyword(k)) { Advance(); return true; }
    return false;
}
bool GoParser::MatchSymbol(const std::string &s) {
    if (IsSymbol(s)) { Advance(); return true; }
    return false;
}
bool GoParser::ExpectKeyword(const std::string &k, const std::string &msg) {
    if (MatchKeyword(k)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}
bool GoParser::ExpectSymbol(const std::string &s, const std::string &msg) {
    if (MatchSymbol(s)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}

void GoParser::SkipSemis() {
    while (IsSymbol(";")) Advance();
}

// ============================================================================
// File
// ============================================================================

void GoParser::ParseFile() {
    file_ = std::make_unique<File>();
    SkipSemis();
    ParsePackage();
    SkipSemis();
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsKeyword("import")) {
            GenDecl gd; gd.keyword = "import";
            ParseImportDecl(gd);
            file_->decls.push_back(std::move(gd));
        } else if (IsKeyword("var") || IsKeyword("const") || IsKeyword("type")) {
            std::string kw = current_.lexeme;
            file_->decls.push_back(ParseGenDecl(kw));
        } else if (IsKeyword("func")) {
            auto fn = ParseFuncDecl();
            if (fn) file_->funcs.push_back(fn);
        } else {
            diagnostics_.Report(current_.loc,
                                "unexpected token at top level: '" + current_.lexeme + "'");
            Advance();
        }
        SkipSemis();
    }
}

void GoParser::ParsePackage() {
    if (!ExpectKeyword("package", "expected 'package' clause")) return;
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "expected package name");
        return;
    }
    file_->package_name = current_.lexeme;
    Advance();
}

void GoParser::ParseImportDecl(GenDecl &dst) {
    Advance();  // 'import'
    auto parse_one = [&]() {
        ImportSpec spec; spec.loc = current_.loc;
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ".")) {
            spec.alias = current_.lexeme;
            Advance();
        }
        if (current_.kind == frontends::TokenKind::kString) {
            spec.path = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected import path string");
        }
        dst.imports.push_back(std::move(spec));
    };
    if (MatchSymbol("(")) {
        SkipSemis();
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            parse_one();
            SkipSemis();
        }
        ExpectSymbol(")", "expected ')'");
    } else {
        parse_one();
    }
}

// ============================================================================
// GenDecl: var / const / type
// ============================================================================

GenDecl GoParser::ParseGenDecl(const std::string &keyword) {
    GenDecl gd; gd.loc = current_.loc; gd.keyword = keyword;
    Advance();  // consume keyword
    auto parse_one = [&]() {
        if (keyword == "type") gd.types.push_back(ParseTypeSpec());
        else                   gd.values.push_back(ParseValueSpec());
    };
    if (MatchSymbol("(")) {
        SkipSemis();
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            parse_one();
            SkipSemis();
        }
        ExpectSymbol(")", "expected ')'");
    } else {
        parse_one();
    }
    return gd;
}

ValueSpec GoParser::ParseValueSpec() {
    ValueSpec vs; vs.loc = current_.loc;
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        vs.names.push_back(current_.lexeme);
        Advance();
        if (!MatchSymbol(",")) break;
    }
    // Optional type
    if (!IsSymbol("=") && !IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) {
        vs.type = ParseType();
    }
    if (MatchSymbol("=")) {
        vs.values = ParseExpressionList();
    }
    return vs;
}

TypeSpec GoParser::ParseTypeSpec() {
    TypeSpec ts; ts.loc = current_.loc;
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "expected type name");
        return ts;
    }
    ts.name = current_.lexeme;
    Advance();
    if (MatchSymbol("=")) ts.is_alias = true;
    ts.type = ParseType();
    return ts;
}

// ============================================================================
// Types
// ============================================================================

std::shared_ptr<TypeNode> GoParser::ParseType() {
    auto t = std::make_shared<TypeNode>();
    t->loc = current_.loc;
    if (IsSymbol("*")) {
        Advance();
        t->kind = TypeKind::kPointer;
        t->elem = ParseType();
        return t;
    }
    if (IsSymbol("[")) {
        Advance();
        if (MatchSymbol("]")) {
            t->kind = TypeKind::kSlice;
            t->elem = ParseType();
            return t;
        }
        if (IsSymbol("...")) {
            Advance();
            ExpectSymbol("]", "expected ']'");
            t->kind = TypeKind::kArray;
            t->array_len = -1;
            t->elem = ParseType();
            return t;
        }
        // [N]T
        long long len = 0;
        if (current_.kind == frontends::TokenKind::kNumber) {
            try { len = std::stoll(current_.lexeme); } catch (...) {}
            Advance();
        } else {
            // expression length — skip
            ParseExpression();
        }
        ExpectSymbol("]", "expected ']'");
        t->kind = TypeKind::kArray;
        t->array_len = len;
        t->elem = ParseType();
        return t;
    }
    if (IsKeyword("map")) {
        Advance();
        ExpectSymbol("[", "expected '[' after 'map'");
        t->kind = TypeKind::kMap;
        t->key = ParseType();
        ExpectSymbol("]", "expected ']'");
        t->elem = ParseType();
        return t;
    }
    if (IsKeyword("chan")) {
        Advance();
        t->kind = TypeKind::kChan;
        if (IsSymbol("<-")) { Advance(); t->chan_dir = 1; }
        t->elem = ParseType();
        return t;
    }
    if (IsSymbol("<-")) {
        Advance();
        ExpectKeyword("chan", "expected 'chan' after '<-'");
        t->kind = TypeKind::kChan;
        t->chan_dir = 2;
        t->elem = ParseType();
        return t;
    }
    if (IsKeyword("struct"))    return ParseStructType();
    if (IsKeyword("interface")) return ParseInterfaceType();
    if (IsKeyword("func"))      return ParseFuncType();
    if (IsSymbol("(")) {
        Advance();
        auto inner = ParseType();
        ExpectSymbol(")", "expected ')'");
        return inner;
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        t->kind = TypeKind::kNamed;
        t->name = current_.lexeme;
        Advance();
        if (IsSymbol(".")) {
            Advance();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                t->name += "." + current_.lexeme;
                Advance();
            }
        }
        return t;
    }
    diagnostics_.Report(current_.loc, "expected type, got '" + current_.lexeme + "'");
    return t;
}

std::shared_ptr<TypeNode> GoParser::ParseStructType() {
    auto t = std::make_shared<TypeNode>();
    t->loc = current_.loc;
    t->kind = TypeKind::kStruct;
    Advance();  // 'struct'
    if (MatchSymbol("{")) {
        t->fields = ParseStructFields();
        ExpectSymbol("}", "expected '}'");
    }
    return t;
}

std::vector<TypeNode::Field> GoParser::ParseStructFields() {
    std::vector<TypeNode::Field> fields;
    SkipSemis();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        TypeNode::Field f;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            // Either: "Name [, Name]* Type" or single ident as embedded type
            std::vector<std::string> names;
            names.push_back(current_.lexeme);
            Advance();
            while (MatchSymbol(",")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    names.push_back(current_.lexeme);
                    Advance();
                } else break;
            }
            // If next is a type-start token, names are field names
            bool has_type = !(IsSymbol(";") || IsSymbol("}") ||
                              current_.kind == frontends::TokenKind::kString);
            if (has_type) {
                f.names = names;
                f.type = ParseType();
            } else {
                // Embedded
                auto t = std::make_shared<TypeNode>();
                t->kind = TypeKind::kNamed;
                t->name = names.front();
                f.type = t;
            }
        } else if (IsSymbol("*")) {
            // Embedded pointer type
            Advance();
            auto t = std::make_shared<TypeNode>();
            t->kind = TypeKind::kPointer;
            t->elem = ParseType();
            f.type = t;
        } else {
            diagnostics_.Report(current_.loc, "expected struct field");
            break;
        }
        if (current_.kind == frontends::TokenKind::kString) {
            f.tag = current_.lexeme;
            Advance();
        }
        fields.push_back(std::move(f));
        SkipSemis();
    }
    return fields;
}

std::shared_ptr<TypeNode> GoParser::ParseInterfaceType() {
    auto t = std::make_shared<TypeNode>();
    t->loc = current_.loc;
    t->kind = TypeKind::kInterface;
    Advance();  // 'interface'
    if (MatchSymbol("{")) {
        t->methods = ParseInterfaceMethods();
        ExpectSymbol("}", "expected '}'");
    }
    return t;
}

std::vector<TypeNode::Field> GoParser::ParseInterfaceMethods() {
    std::vector<TypeNode::Field> ms;
    SkipSemis();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        TypeNode::Field f;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            std::string name = current_.lexeme;
            Advance();
            if (IsSymbol("(")) {
                // method
                f.names.push_back(name);
                auto fty = std::make_shared<TypeNode>();
                fty->kind = TypeKind::kFunc;
                bool variadic = false;
                std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> ps, rs;
                ParseSignature(ps, rs, variadic);
                for (auto &p : ps) fty->params.push_back(p.second);
                for (auto &r : rs) fty->results.push_back(r.second);
                f.type = fty;
            } else {
                // embedded interface
                auto et = std::make_shared<TypeNode>();
                et->kind = TypeKind::kNamed;
                et->name = name;
                if (IsSymbol(".")) {
                    Advance();
                    if (current_.kind == frontends::TokenKind::kIdentifier) {
                        et->name += "." + current_.lexeme;
                        Advance();
                    }
                }
                f.type = et;
            }
        } else {
            diagnostics_.Report(current_.loc, "expected interface method");
            break;
        }
        ms.push_back(std::move(f));
        SkipSemis();
    }
    return ms;
}

std::shared_ptr<TypeNode> GoParser::ParseFuncType() {
    auto t = std::make_shared<TypeNode>();
    t->loc = current_.loc;
    t->kind = TypeKind::kFunc;
    Advance();  // 'func'
    bool variadic = false;
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> ps, rs;
    ParseSignature(ps, rs, variadic);
    for (auto &p : ps) t->params.push_back(p.second);
    for (auto &r : rs) t->results.push_back(r.second);
    return t;
}

// ============================================================================
// Function declarations and signatures
// ============================================================================

std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>>
GoParser::ParseParamGroup(bool *is_variadic) {
    // Parses one parenthesised list. Each entry is "name1, name2 Type" or just
    // "Type" (for un-named return values / interface methods).
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> out;
    if (!MatchSymbol("(")) return out;
    while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        // Look ahead: collect identifiers possibly followed by a type
        std::vector<std::string> names;
        // We may parse a leading list of identifiers separated by ','
        // but identifiers might also be a type name.  Strategy: parse
        // expressions of identifiers; if a type-start token follows, we
        // had names; otherwise treat each identifier as a type.
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            // Try lookahead through "ident (',' ident)*" then Type
            // We'll do a single-pass: collect first ident, if followed by ','
            // continue, else decide.
            // Save state-ish via re-implementation: just collect idents
            // separated by commas, then check if there's a type.
            std::vector<std::string> possible = {current_.lexeme};
            Advance();
            while (IsSymbol(",")) {
                // Peek: next must be identifier to keep collecting names
                Advance();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    possible.push_back(current_.lexeme);
                    Advance();
                } else {
                    // The previous identifiers were unnamed type names
                    for (auto &p : possible) {
                        auto t = std::make_shared<TypeNode>();
                        t->kind = TypeKind::kNamed;
                        t->name = p;
                        out.emplace_back("", t);
                    }
                    possible.clear();
                    break;
                }
            }
            // After collecting, either we have a type follows (named params)
            // or we hit ')' / ',' continuation as types
            bool type_follows = (IsSymbol("...") || IsSymbol("*") || IsSymbol("[") ||
                                 IsSymbol("(") || IsSymbol(".") ||
                                 IsKeyword("func") || IsKeyword("map") ||
                                 IsKeyword("chan") || IsKeyword("struct") ||
                                 IsKeyword("interface") ||
                                 current_.kind == frontends::TokenKind::kIdentifier);
            if (!possible.empty() && IsSymbol(".") && possible.size() == 1) {
                // Could be qualified type "pkg.Name" – treat as type
                auto t = std::make_shared<TypeNode>();
                t->kind = TypeKind::kNamed;
                t->name = possible.front();
                Advance();  // '.'
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    t->name += "." + current_.lexeme;
                    Advance();
                }
                out.emplace_back("", t);
                possible.clear();
                if (MatchSymbol(",")) continue;
                else break;
            }
            if (!possible.empty() && type_follows) {
                names = std::move(possible);
                bool variadic_here = false;
                if (IsSymbol("...")) { Advance(); variadic_here = true; if (is_variadic) *is_variadic = true; }
                auto ty = ParseType();
                if (variadic_here) {
                    auto wrap = std::make_shared<TypeNode>();
                    wrap->kind = TypeKind::kEllipsis;
                    wrap->elem = ty;
                    ty = wrap;
                }
                for (auto &n : names) out.emplace_back(n, ty);
            } else if (!possible.empty()) {
                // No type follows: treat each as a bare type
                for (auto &p : possible) {
                    auto t = std::make_shared<TypeNode>();
                    t->kind = TypeKind::kNamed;
                    t->name = p;
                    out.emplace_back("", t);
                }
            }
        } else if (IsSymbol("...")) {
            Advance();
            if (is_variadic) *is_variadic = true;
            auto wrap = std::make_shared<TypeNode>();
            wrap->kind = TypeKind::kEllipsis;
            wrap->elem = ParseType();
            out.emplace_back("", wrap);
        } else {
            // A leading non-ident type
            out.emplace_back("", ParseType());
        }
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol(")", "expected ')'");
    return out;
}

void GoParser::ParseSignature(
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> &params,
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> &results,
    bool &is_variadic) {
    bool v = false;
    params = ParseParamGroup(&v);
    is_variadic = v;
    // Optional results
    if (IsSymbol("(")) {
        bool v2 = false;
        results = ParseParamGroup(&v2);
    } else if (!IsSymbol("{") && !IsSymbol(";") &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
        // Single un-named return type
        auto t = ParseType();
        if (t) results.emplace_back("", t);
    }
}

std::shared_ptr<FuncDecl> GoParser::ParseFuncDecl() {
    auto fn = std::make_shared<FuncDecl>();
    fn->loc = current_.loc;
    fn->doc = std::move(pending_doc_);
    pending_doc_.clear();
    Advance();  // 'func'
    // Optional receiver
    if (IsSymbol("(")) {
        bool v = false;
        auto recv = ParseParamGroup(&v);
        if (!recv.empty()) {
            Receiver r;
            r.name = recv.front().first;
            r.type = recv.front().second;
            fn->receiver = std::move(r);
        }
    }
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "expected function name");
        return nullptr;
    }
    fn->name = current_.lexeme;
    Advance();
    bool variadic = false;
    ParseSignature(fn->params, fn->results, variadic);
    fn->is_variadic = variadic;
    if (IsSymbol("{")) fn->body = ParseBlock();
    return fn;
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Block> GoParser::ParseBlock() {
    auto b = std::make_shared<Block>();
    b->loc = current_.loc;
    if (!ExpectSymbol("{", "expected '{'")) return b;
    SkipSemis();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto s = ParseStatement();
        if (s) b->stmts.push_back(s);
        SkipSemis();
    }
    ExpectSymbol("}", "expected '}'");
    return b;
}

std::shared_ptr<Statement> GoParser::ParseStatement() {
    if (IsSymbol("{")) return ParseBlock();
    if (IsKeyword("if"))      return ParseIfStmt();
    if (IsKeyword("for"))     return ParseForStmt();
    if (IsKeyword("switch"))  return ParseSwitchStmt();
    if (IsKeyword("select"))  return ParseSelectStmt();
    if (IsKeyword("return"))  return ParseReturnStmt();
    if (IsKeyword("break") || IsKeyword("continue") ||
        IsKeyword("goto")  || IsKeyword("fallthrough")) {
        std::string kw = current_.lexeme;
        return ParseBranchStmt(kw);
    }
    if (IsKeyword("go"))      return ParseGoStmt();
    if (IsKeyword("defer"))   return ParseDeferStmt();
    if (IsKeyword("var") || IsKeyword("const") || IsKeyword("type")) {
        std::string kw = current_.lexeme;
        auto ds = std::make_shared<DeclStmt>();
        ds->loc = current_.loc;
        ds->decl = std::make_shared<GenDecl>(ParseGenDecl(kw));
        return ds;
    }
    return ParseSimpleStmt(false);
}

std::shared_ptr<Statement> GoParser::ParseSimpleStmt(bool /*allow_range*/) {
    auto loc = current_.loc;
    if (IsSymbol(";") || IsSymbol("}")) {
        // empty statement
        auto e = std::make_shared<ExprStmt>();
        e->loc = loc;
        return e;
    }
    auto first = ParseExpression();
    // Inc/Dec
    if (IsSymbol("++") || IsSymbol("--")) {
        auto s = std::make_shared<IncDecStmt>();
        s->loc = loc;
        s->target = first;
        s->inc = (current_.lexeme == "++");
        Advance();
        return s;
    }
    // Send
    if (IsSymbol("<-")) {
        Advance();
        auto v = ParseExpression();
        auto s = std::make_shared<SendStmt>();
        s->loc = loc;
        s->chan = first;
        s->value = v;
        return s;
    }
    // Possibly more LHS exprs
    std::vector<std::shared_ptr<Expression>> lhs{first};
    while (MatchSymbol(",")) lhs.push_back(ParseExpression());
    // Assignment
    if (IsSymbol("=") || IsSymbol(":=") ||
        IsSymbol("+=") || IsSymbol("-=") || IsSymbol("*=") || IsSymbol("/=") ||
        IsSymbol("%=") || IsSymbol("&=") || IsSymbol("|=") || IsSymbol("^=") ||
        IsSymbol("<<=") || IsSymbol(">>=") || IsSymbol("&^=")) {
        auto s = std::make_shared<AssignStmt>();
        s->loc = loc;
        s->lhs = std::move(lhs);
        s->op = current_.lexeme;
        Advance();
        s->rhs = ParseExpressionList();
        return s;
    }
    // Plain expression statement
    if (lhs.size() == 1) {
        auto e = std::make_shared<ExprStmt>();
        e->loc = loc;
        e->expr = lhs.front();
        return e;
    }
    diagnostics_.Report(loc, "expected statement");
    return nullptr;
}

std::shared_ptr<Statement> GoParser::ParseIfStmt() {
    auto s = std::make_shared<IfStmt>();
    s->loc = current_.loc;
    Advance();  // 'if'
    // Optional init; cond
    auto saved = lexer_.SaveState();
    auto saved_cur = current_;
    auto saved_doc = pending_doc_;
    // Try parsing 'simple_stmt ;' then condition. Otherwise just condition.
    auto first = ParseSimpleStmt(false);
    if (MatchSymbol(";")) {
        s->init = first;
        s->cond = ParseExpression();
    } else {
        // We over-consumed: but ParseSimpleStmt already returned an ExprStmt.
        // Convert back: use its expression as the condition.
        if (auto e = std::dynamic_pointer_cast<ExprStmt>(first)) s->cond = e->expr;
        else { (void)saved; (void)saved_cur; (void)saved_doc; }
    }
    s->body = ParseBlock();
    if (MatchKeyword("else")) {
        if (IsKeyword("if")) s->else_branch = ParseIfStmt();
        else                 s->else_branch = ParseBlock();
    }
    return s;
}

std::shared_ptr<Statement> GoParser::ParseForStmt() {
    auto s = std::make_shared<ForStmt>();
    s->loc = current_.loc;
    Advance();  // 'for'
    if (IsSymbol("{")) { s->body = ParseBlock(); return s; }
    // Three-part or condition-only or range
    // Try "lhs := range x" / "lhs = range x"
    // Simple approach: read tokens up to '{' and look for 'range' / ';'
    // Implement by trying a simple_stmt; check for ';' or 'range'.
    auto first = ParseSimpleStmt(true);
    if (auto as = std::dynamic_pointer_cast<AssignStmt>(first)) {
        // Check if RHS is a single range expression: range x
        if (as->rhs.size() == 1) {
            if (auto un = std::dynamic_pointer_cast<UnaryExpr>(as->rhs.front()); un && un->op == "range") {
                s->is_range = true;
                s->range_lhs = as->lhs;
                s->range_assign = as->op;
                s->range_x = un->operand;
                s->body = ParseBlock();
                return s;
            }
        }
    }
    if (MatchSymbol(";")) {
        s->init = first;
        if (!IsSymbol(";")) s->cond = ParseExpression();
        ExpectSymbol(";", "expected ';' in for clause");
        if (!IsSymbol("{")) s->post = ParseSimpleStmt(false);
    } else {
        if (auto e = std::dynamic_pointer_cast<ExprStmt>(first)) s->cond = e->expr;
    }
    s->body = ParseBlock();
    return s;
}

std::shared_ptr<Statement> GoParser::ParseSwitchStmt() {
    auto s = std::make_shared<SwitchStmt>();
    s->loc = current_.loc;
    Advance();  // 'switch'
    if (!IsSymbol("{")) {
        auto first = ParseSimpleStmt(false);
        if (MatchSymbol(";")) {
            s->init = first;
            if (!IsSymbol("{")) {
                if (auto es = std::dynamic_pointer_cast<ExprStmt>(ParseSimpleStmt(false))) {
                    s->tag = es->expr;
                }
            }
        } else if (auto e = std::dynamic_pointer_cast<ExprStmt>(first)) {
            s->tag = e->expr;
        }
    }
    ExpectSymbol("{", "expected '{'");
    SkipSemis();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        SwitchClause c; c.loc = current_.loc;
        if (MatchKeyword("case")) {
            c.values = ParseExpressionList();
        } else if (!MatchKeyword("default")) {
            diagnostics_.Report(current_.loc, "expected 'case' or 'default'");
            break;
        }
        ExpectSymbol(":", "expected ':'");
        SkipSemis();
        while (!IsKeyword("case") && !IsKeyword("default") &&
               !IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto st = ParseStatement();
            if (st) c.body.push_back(st);
            SkipSemis();
        }
        s->clauses.push_back(std::move(c));
    }
    ExpectSymbol("}", "expected '}'");
    return s;
}

std::shared_ptr<Statement> GoParser::ParseSelectStmt() {
    auto s = std::make_shared<SelectStmt>();
    s->loc = current_.loc;
    Advance();  // 'select'
    ExpectSymbol("{", "expected '{'");
    SkipSemis();
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        CommClause c; c.loc = current_.loc;
        if (MatchKeyword("case")) {
            c.comm = ParseSimpleStmt(false);
        } else if (!MatchKeyword("default")) {
            diagnostics_.Report(current_.loc, "expected 'case' or 'default' in select");
            break;
        }
        ExpectSymbol(":", "expected ':'");
        SkipSemis();
        while (!IsKeyword("case") && !IsKeyword("default") &&
               !IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto st = ParseStatement();
            if (st) c.body.push_back(st);
            SkipSemis();
        }
        s->clauses.push_back(std::move(c));
    }
    ExpectSymbol("}", "expected '}'");
    return s;
}

std::shared_ptr<Statement> GoParser::ParseReturnStmt() {
    auto s = std::make_shared<ReturnStmt>();
    s->loc = current_.loc;
    Advance();  // 'return'
    if (!IsSymbol(";") && !IsSymbol("}")) {
        s->results = ParseExpressionList();
    }
    return s;
}

std::shared_ptr<Statement> GoParser::ParseBranchStmt(const std::string &kw) {
    auto s = std::make_shared<BranchStmt>();
    s->loc = current_.loc;
    s->keyword = kw;
    Advance();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        s->label = current_.lexeme;
        Advance();
    }
    return s;
}

std::shared_ptr<Statement> GoParser::ParseGoStmt() {
    auto s = std::make_shared<GoStmt>();
    s->loc = current_.loc;
    Advance();
    s->call = ParseExpression();
    return s;
}

std::shared_ptr<Statement> GoParser::ParseDeferStmt() {
    auto s = std::make_shared<DeferStmt>();
    s->loc = current_.loc;
    Advance();
    s->call = ParseExpression();
    return s;
}

// ============================================================================
// Expressions
// ============================================================================

int GoParser::BinaryPrecedence(const std::string &op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return 3;
    if (op == "+" || op == "-" || op == "|" || op == "^") return 4;
    if (op == "*" || op == "/" || op == "%" || op == "<<" || op == ">>" ||
        op == "&" || op == "&^") return 5;
    return 0;
}

std::vector<std::shared_ptr<Expression>> GoParser::ParseExpressionList() {
    std::vector<std::shared_ptr<Expression>> out;
    out.push_back(ParseExpression());
    while (MatchSymbol(",")) out.push_back(ParseExpression());
    return out;
}

std::shared_ptr<Expression> GoParser::ParseExpression() {
    return ParseBinaryExpr(1);
}

std::shared_ptr<Expression> GoParser::ParseBinaryExpr(int min_prec) {
    auto lhs = ParseUnaryExpr();
    while (true) {
        if (current_.kind != frontends::TokenKind::kSymbol) break;
        int p = BinaryPrecedence(current_.lexeme);
        if (p < min_prec) break;
        std::string op = current_.lexeme;
        auto loc = current_.loc;
        Advance();
        auto rhs = ParseBinaryExpr(p + 1);
        auto b = std::make_shared<BinaryExpr>();
        b->loc = loc;
        b->op = op;
        b->left = lhs;
        b->right = rhs;
        lhs = b;
    }
    return lhs;
}

std::shared_ptr<Expression> GoParser::ParseUnaryExpr() {
    if (current_.kind == frontends::TokenKind::kSymbol) {
        const auto &lx = current_.lexeme;
        if (lx == "+" || lx == "-" || lx == "!" || lx == "^" || lx == "*" ||
            lx == "&" || lx == "<-") {
            auto u = std::make_shared<UnaryExpr>();
            u->loc = current_.loc;
            u->op = lx;
            Advance();
            u->operand = ParseUnaryExpr();
            return u;
        }
    }
    if (IsKeyword("range")) {
        auto u = std::make_shared<UnaryExpr>();
        u->loc = current_.loc;
        u->op = "range";
        Advance();
        u->operand = ParseUnaryExpr();
        return u;
    }
    return ParsePrimaryExpr();
}

std::shared_ptr<Expression> GoParser::ParsePrimaryExpr() {
    auto e = ParseOperand();
    while (true) {
        if (IsSymbol(".")) {
            Advance();
            if (IsSymbol("(")) {
                // type assertion: x.(T) or x.(type)
                Advance();
                auto ta = std::make_shared<TypeAssertExpr>();
                ta->loc = current_.loc;
                ta->x = e;
                if (!MatchKeyword("type")) ta->type = ParseType();
                ExpectSymbol(")", "expected ')'");
                e = ta;
            } else if (current_.kind == frontends::TokenKind::kIdentifier) {
                auto sel = std::make_shared<SelectorExpr>();
                sel->loc = current_.loc;
                sel->x = e;
                sel->sel = current_.lexeme;
                Advance();
                e = sel;
            } else {
                diagnostics_.Report(current_.loc, "expected selector or type assertion");
                break;
            }
        } else if (IsSymbol("[")) {
            Advance();
            // index or slice
            std::shared_ptr<Expression> low, high, max;
            bool three = false;
            if (!IsSymbol(":")) low = ParseExpression();
            if (MatchSymbol(":")) {
                if (!IsSymbol("]") && !IsSymbol(":")) high = ParseExpression();
                if (MatchSymbol(":")) { three = true; if (!IsSymbol("]")) max = ParseExpression(); }
                ExpectSymbol("]", "expected ']'");
                auto sl = std::make_shared<SliceExpr>();
                sl->loc = current_.loc;
                sl->x = e;
                sl->low = low; sl->high = high; sl->max = max;
                sl->three_index = three;
                e = sl;
            } else {
                ExpectSymbol("]", "expected ']'");
                auto idx = std::make_shared<IndexExpr>();
                idx->loc = current_.loc;
                idx->x = e;
                idx->index = low;
                e = idx;
            }
        } else if (IsSymbol("(")) {
            Advance();
            auto call = std::make_shared<CallExpr>();
            call->loc = current_.loc;
            call->fun = e;
            if (!IsSymbol(")")) {
                call->args.push_back(ParseExpression());
                while (MatchSymbol(",")) {
                    if (IsSymbol(")")) break;
                    call->args.push_back(ParseExpression());
                }
                if (IsSymbol("...")) { Advance(); call->has_ellipsis = true; }
            }
            ExpectSymbol(")", "expected ')'");
            e = call;
        } else if (IsSymbol("{")) {
            // Composite literal on previously-parsed type-name expression.
            // Only treat as composite literal if `e` looks like a type.
            // Heuristic: only if `e` is an Identifier or SelectorExpr.
            if (std::dynamic_pointer_cast<Identifier>(e) ||
                std::dynamic_pointer_cast<SelectorExpr>(e)) {
                auto t = std::make_shared<TypeNode>();
                t->kind = TypeKind::kNamed;
                if (auto id = std::dynamic_pointer_cast<Identifier>(e)) t->name = id->name;
                else if (auto sel = std::dynamic_pointer_cast<SelectorExpr>(e)) {
                    if (auto ix = std::dynamic_pointer_cast<Identifier>(sel->x))
                        t->name = ix->name + "." + sel->sel;
                }
                e = ParseCompositeLit(t);
            } else break;
        } else break;
    }
    return e;
}

std::shared_ptr<Expression> GoParser::ParseOperand() {
    auto loc = current_.loc;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto id = std::make_shared<Identifier>();
        id->loc = loc;
        id->name = current_.lexeme;
        Advance();
        return id;
    }
    if (current_.kind == frontends::TokenKind::kNumber) {
        auto lit = std::make_shared<BasicLit>();
        lit->loc = loc;
        lit->value = current_.lexeme;
        bool has_dot = false, has_e = false, has_i = false;
        for (char c : current_.lexeme) {
            if (c == '.') has_dot = true;
            if (c == 'e' || c == 'E') has_e = true;
            if (c == 'i') has_i = true;
        }
        if (has_i) lit->kind = BasicLit::Kind::kImag;
        else if (has_dot || has_e) lit->kind = BasicLit::Kind::kFloat;
        else lit->kind = BasicLit::Kind::kInt;
        Advance();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kString) {
        auto lit = std::make_shared<BasicLit>();
        lit->loc = loc;
        lit->kind = BasicLit::Kind::kString;
        lit->value = current_.lexeme;
        Advance();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kChar) {
        auto lit = std::make_shared<BasicLit>();
        lit->loc = loc;
        lit->kind = BasicLit::Kind::kRune;
        lit->value = current_.lexeme;
        Advance();
        return lit;
    }
    if (IsKeyword("true") || IsKeyword("false")) {
        auto lit = std::make_shared<BasicLit>();
        lit->loc = loc;
        lit->kind = BasicLit::Kind::kBool;
        lit->value = current_.lexeme;
        Advance();
        return lit;
    }
    if (IsKeyword("nil")) {
        auto lit = std::make_shared<BasicLit>();
        lit->loc = loc;
        lit->kind = BasicLit::Kind::kNil;
        lit->value = "nil";
        Advance();
        return lit;
    }
    if (IsKeyword("iota")) {
        auto id = std::make_shared<Identifier>();
        id->loc = loc;
        id->name = "iota";
        Advance();
        return id;
    }
    if (IsSymbol("(")) {
        Advance();
        auto inner = ParseExpression();
        ExpectSymbol(")", "expected ')'");
        auto p = std::make_shared<ParenExpr>();
        p->loc = loc;
        p->inner = inner;
        return p;
    }
    if (IsKeyword("func")) {
        auto fl = std::make_shared<FuncLit>();
        fl->loc = loc;
        fl->type = ParseFuncType();
        if (IsSymbol("{")) fl->body = ParseBlock();
        return fl;
    }
    if (IsSymbol("[") || IsKeyword("map") || IsKeyword("struct")) {
        auto t = ParseType();
        if (IsSymbol("{")) return ParseCompositeLit(t);
        // Bare type used as expression (e.g. type assertion on the right of `.`)
        auto id = std::make_shared<Identifier>();
        id->loc = loc;
        id->name = "<type>";
        return id;
    }
    diagnostics_.Report(loc, "unexpected token in expression: '" + current_.lexeme + "'");
    Advance();
    auto bad = std::make_shared<Identifier>();
    bad->loc = loc;
    bad->name = "<error>";
    return bad;
}

std::shared_ptr<Expression> GoParser::ParseCompositeLit(std::shared_ptr<TypeNode> type) {
    auto cl = std::make_shared<CompositeLit>();
    cl->loc = current_.loc;
    cl->type = type;
    if (!ExpectSymbol("{", "expected '{'")) return cl;
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        CompositeLit::Element elem;
        auto first = ParseExpression();
        if (MatchSymbol(":")) {
            elem.key = first;
            elem.value = ParseExpression();
        } else {
            elem.value = first;
        }
        cl->elements.push_back(std::move(elem));
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol("}", "expected '}'");
    return cl;
}

}  // namespace polyglot::go
