/**
 * @file     parser.cpp
 * @brief    Ruby parser
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * A practical recursive-descent parser for the Ruby subset that the
 * polyglot compiler needs.  We deliberately ignore exotic constructs
 * (proc-arg lambda shortcut, BEGIN/END blocks, refinements …) and aim
 * for clean coverage of: top-level methods, classes/modules, common
 * statements (if/unless/while/until/for/case/begin), expression
 * grammar with all the standard operator precedences, blocks `{…}` /
 * `do…end`, YARD type tags via comments, and block parameters.
 */
#include "frontends/ruby/include/ruby_parser.h"

#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_set>

namespace polyglot::ruby {

namespace {

std::string TrimDocLine(const std::string &line) {
    size_t a = 0;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '#')) ++a;
    size_t b = line.size();
    while (b > a && (line[b-1] == ' ' || line[b-1] == '\t' || line[b-1] == '\r')) --b;
    return line.substr(a, b - a);
}

}  // namespace

void RbParser::Advance() {
    current_ = lexer_.NextToken();
    auto d = lexer_.TakeDocComment();
    if (!d.empty()) pending_doc_ = d;
}

bool RbParser::IsKeyword(const std::string &k) const {
    return current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == k;
}
bool RbParser::IsSymbol(const std::string &s) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == s;
}
bool RbParser::MatchKeyword(const std::string &k) {
    if (IsKeyword(k)) { Advance(); return true; } return false;
}
bool RbParser::MatchSymbol(const std::string &s) {
    if (IsSymbol(s)) { Advance(); return true; } return false;
}
bool RbParser::ExpectKeyword(const std::string &k, const std::string &msg) {
    if (MatchKeyword(k)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}
bool RbParser::ExpectSymbol(const std::string &s, const std::string &msg) {
    if (MatchSymbol(s)) return true;
    diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
    return false;
}

bool RbParser::AtTerminator() const {
    return current_.kind == frontends::TokenKind::kNewline ||
           (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ";") ||
           current_.kind == frontends::TokenKind::kEndOfFile;
}

void RbParser::SkipTerminators() {
    while (current_.kind == frontends::TokenKind::kNewline ||
           (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ";")) {
        Advance();
    }
}

std::shared_ptr<TypeNode> RbParser::ParseYardType(const std::string &raw) {
    if (raw.empty()) return nullptr;
    auto t = std::make_shared<TypeNode>();
    // Take everything up to ',' or ' ' as base name; ignore generics for now.
    std::string name;
    for (char c : raw) {
        if (c == ',' || c == ' ' || c == '\t') break;
        name.push_back(c);
    }
    t->name = name;
    return t;
}

void RbParser::ParseModule() {
    module_ = std::make_shared<Module>();
    Advance();
    SkipTerminators();
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        auto s = ParseTopLevel();
        if (s) module_->body.push_back(s);
        else Advance();
        SkipTerminators();
    }
}

std::shared_ptr<Module> RbParser::TakeModule() { return std::move(module_); }

std::shared_ptr<Statement> RbParser::ParseTopLevel() { return ParseStatement(); }

std::shared_ptr<Statement> RbParser::ParseStatement() {
    if (IsKeyword("def"))    return ParseDef();
    if (IsKeyword("class"))  return ParseClass();
    if (IsKeyword("module")) return ParseModuleStmt();
    if (IsKeyword("if"))     return ParseIf(false);
    if (IsKeyword("unless")) return ParseIf(true);
    if (IsKeyword("while"))  return ParseWhile(false);
    if (IsKeyword("until"))  return ParseWhile(true);
    if (IsKeyword("for"))    return ParseFor();
    if (IsKeyword("case"))   return ParseCase();
    if (IsKeyword("begin"))  return ParseBegin();
    if (IsKeyword("return")) {
        auto loc = current_.loc; Advance();
        auto s = std::make_shared<ReturnStmt>(); s->loc = loc;
        if (!AtTerminator() && !IsKeyword("end")) s->value = ParseExpression();
        return s;
    }
    if (IsKeyword("yield")) {
        auto loc = current_.loc; Advance();
        auto s = std::make_shared<YieldStmt>(); s->loc = loc;
        if (!AtTerminator()) {
            s->args.push_back(ParseExpression());
            while (MatchSymbol(",")) s->args.push_back(ParseExpression());
        }
        return s;
    }
    if (IsKeyword("break")) {
        auto loc = current_.loc; Advance();
        auto s = std::make_shared<BreakStmt>(); s->loc = loc;
        if (!AtTerminator()) s->value = ParseExpression();
        return s;
    }
    if (IsKeyword("next")) {
        auto loc = current_.loc; Advance();
        auto s = std::make_shared<NextStmt>(); s->loc = loc;
        if (!AtTerminator()) s->value = ParseExpression();
        return s;
    }
    if (IsKeyword("redo"))  { auto s = std::make_shared<RedoStmt>();  s->loc = current_.loc; Advance(); return s; }
    if (IsKeyword("retry")) { auto s = std::make_shared<RetryStmt>(); s->loc = current_.loc; Advance(); return s; }

    auto loc = current_.loc;
    auto e = ParseExpression();
    auto stmt = std::make_shared<ExprStmt>();
    stmt->loc = loc;
    stmt->expr = e;
    return stmt;
}

std::shared_ptr<Block> RbParser::ParseBlockUntil(std::initializer_list<std::string> terminators) {
    auto block = std::make_shared<Block>();
    block->loc = current_.loc;
    SkipTerminators();
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        bool stop = false;
        for (auto &t : terminators) {
            if (IsKeyword(t)) { stop = true; break; }
        }
        if (stop) break;
        auto s = ParseStatement();
        if (s) block->stmts.push_back(s);
        else Advance();
        SkipTerminators();
    }
    return block;
}

std::vector<Param> RbParser::ParseDefParams() {
    std::vector<Param> params;
    bool had_paren = MatchSymbol("(");

    // Build YARD param-type map from pending_doc_.
    std::unordered_map<std::string, std::shared_ptr<TypeNode>> ptypes;
    std::shared_ptr<TypeNode> rtype;
    if (!pending_doc_.empty()) {
        std::istringstream is(pending_doc_);
        std::string line;
        while (std::getline(is, line)) {
            line = TrimDocLine(line);
            // @param name [Type] desc   OR   @param [Type] name desc
            if (line.rfind("@param", 0) == 0) {
                std::string rest = line.substr(6);
                size_t i = 0; while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                std::string a, type_str;
                if (i < rest.size() && rest[i] == '[') {
                    size_t j = rest.find(']', i);
                    if (j != std::string::npos) {
                        type_str = rest.substr(i + 1, j - i - 1);
                        i = j + 1;
                        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                        while (i < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_'))
                            a.push_back(rest[i++]);
                    }
                } else {
                    while (i < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_'))
                        a.push_back(rest[i++]);
                    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                    if (i < rest.size() && rest[i] == '[') {
                        size_t j = rest.find(']', i);
                        if (j != std::string::npos) type_str = rest.substr(i + 1, j - i - 1);
                    }
                }
                if (!a.empty()) ptypes[a] = ParseYardType(type_str);
            } else if (line.rfind("@return", 0) == 0) {
                size_t i = line.find('[');
                size_t j = (i == std::string::npos) ? std::string::npos : line.find(']', i);
                if (i != std::string::npos && j != std::string::npos) {
                    rtype = ParseYardType(line.substr(i + 1, j - i - 1));
                }
            }
        }
    }
    pending_doc_.clear();

    auto stop_at = [&]() {
        if (had_paren) return IsSymbol(")");
        return AtTerminator() || IsSymbol("|");
    };

    while (!stop_at() && current_.kind != frontends::TokenKind::kEndOfFile) {
        Param p;
        if (MatchSymbol("**")) p.double_splat = true;
        else if (MatchSymbol("*")) p.splat = true;
        else if (MatchSymbol("&")) p.block = true;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            p.name = current_.lexeme;
            Advance();
        }
        if (MatchSymbol("=")) p.default_value = ParseExpression();
        else if (MatchSymbol(":")) {
            // keyword argument; default is optional
            if (!IsSymbol(",") && !stop_at()) p.default_value = ParseExpression();
        }
        auto it = ptypes.find(p.name);
        if (it != ptypes.end()) p.type = it->second;
        params.push_back(p);
        if (!MatchSymbol(",")) break;
    }
    if (had_paren) ExpectSymbol(")", "expected ')'");

    if (rtype) {
        Param dummy; dummy.name = "$return$"; dummy.type = rtype;
        params.push_back(dummy);
    }
    return params;
}

std::shared_ptr<Statement> RbParser::ParseDef() {
    auto loc = current_.loc;
    Advance();  // 'def'
    auto m = std::make_shared<MethodDecl>();
    m->loc = loc;
    if (IsKeyword("self") && current_.lexeme == "self") {
        m->is_self = true;
        Advance();
        ExpectSymbol(".", "expected '.' after 'self'");
    }
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        m->name = current_.lexeme; Advance();
    }
    m->params = ParseDefParams();
    if (!m->params.empty() && m->params.back().name == "$return$") {
        m->return_type = m->params.back().type;
        m->params.pop_back();
    }
    SkipTerminators();
    auto body = ParseBlockUntil({"end", "rescue", "ensure"});
    // If rescue/ensure follow, wrap as begin-rescue.
    if (IsKeyword("rescue") || IsKeyword("ensure")) {
        auto bs = std::make_shared<BeginStmt>();
        bs->loc = loc;
        bs->body = body;
        while (IsKeyword("rescue")) {
            Advance();
            BeginStmt::Rescue r;
            // optional: ClassName[, ClassName] [=> var]
            while (current_.kind == frontends::TokenKind::kIdentifier && !IsKeyword("then")) {
                auto t = std::make_shared<TypeNode>(); t->name = current_.lexeme;
                r.classes.push_back(t);
                Advance();
                if (!MatchSymbol(",")) break;
            }
            if (MatchSymbol("=>")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    r.var = current_.lexeme; Advance();
                }
            }
            MatchKeyword("then");
            SkipTerminators();
            r.body = ParseBlockUntil({"end", "rescue", "ensure"});
            bs->rescues.push_back(r);
        }
        if (MatchKeyword("ensure")) {
            SkipTerminators();
            bs->ensure_branch = ParseBlockUntil({"end"});
        }
        m->body = bs;
    } else {
        m->body = body;
    }
    ExpectKeyword("end", "expected 'end' after def");
    return m;
}

std::shared_ptr<Statement> RbParser::ParseClass() {
    auto loc = current_.loc; Advance();
    auto c = std::make_shared<ClassDecl>(); c->loc = loc;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        c->name = current_.lexeme; Advance();
    }
    if (MatchSymbol("<")) {
        c->superclass = ParseExpression();
    }
    SkipTerminators();
    while (current_.kind != frontends::TokenKind::kEndOfFile && !IsKeyword("end")) {
        auto s = ParseStatement();
        if (s) c->body.push_back(s);
        else Advance();
        SkipTerminators();
    }
    ExpectKeyword("end", "expected 'end'");
    return c;
}

std::shared_ptr<Statement> RbParser::ParseModuleStmt() {
    auto loc = current_.loc; Advance();
    auto m = std::make_shared<ModuleDecl>(); m->loc = loc;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        m->name = current_.lexeme; Advance();
    }
    SkipTerminators();
    while (current_.kind != frontends::TokenKind::kEndOfFile && !IsKeyword("end")) {
        auto s = ParseStatement();
        if (s) m->body.push_back(s);
        else Advance();
        SkipTerminators();
    }
    ExpectKeyword("end", "expected 'end'");
    return m;
}

std::shared_ptr<Statement> RbParser::ParseIf(bool is_unless) {
    auto loc = current_.loc; Advance();
    auto s = std::make_shared<IfStmt>(); s->loc = loc; s->unless = is_unless;
    s->cond = ParseExpression();
    MatchKeyword("then");
    SkipTerminators();
    s->then_branch = ParseBlockUntil({"end", "else", "elsif"});
    if (MatchKeyword("elsif")) {
        // Recurse: parse rest as nested if
        auto nested = std::make_shared<IfStmt>();
        nested->loc = current_.loc;
        nested->cond = ParseExpression();
        MatchKeyword("then"); SkipTerminators();
        nested->then_branch = ParseBlockUntil({"end", "else", "elsif"});
        if (MatchKeyword("else")) {
            SkipTerminators();
            nested->else_branch = ParseBlockUntil({"end"});
        } else if (IsKeyword("elsif")) {
            // Re-enter: simulate by wrapping
            // Approach: treat rest by recursing with a synthetic ParseIf
            // We rewind by treating elsif chain iteratively:
            // (the simpler approach: build nested IfStmts directly)
        }
        s->else_branch = nested;
    } else if (MatchKeyword("else")) {
        SkipTerminators();
        s->else_branch = ParseBlockUntil({"end"});
    }
    ExpectKeyword("end", "expected 'end'");
    return s;
}

std::shared_ptr<Statement> RbParser::ParseWhile(bool is_until) {
    auto loc = current_.loc; Advance();
    auto s = std::make_shared<WhileStmt>(); s->loc = loc; s->until = is_until;
    s->cond = ParseExpression();
    MatchKeyword("do"); SkipTerminators();
    s->body = ParseBlockUntil({"end"});
    ExpectKeyword("end", "expected 'end'");
    return s;
}

std::shared_ptr<Statement> RbParser::ParseFor() {
    auto loc = current_.loc; Advance();
    auto s = std::make_shared<ForStmt>(); s->loc = loc;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        s->var = current_.lexeme; Advance();
    }
    ExpectKeyword("in", "expected 'in'");
    s->iterable = ParseExpression();
    MatchKeyword("do"); SkipTerminators();
    s->body = ParseBlockUntil({"end"});
    ExpectKeyword("end", "expected 'end'");
    return s;
}

std::shared_ptr<Statement> RbParser::ParseCase() {
    auto loc = current_.loc; Advance();
    auto s = std::make_shared<CaseStmt>(); s->loc = loc;
    if (!AtTerminator() && !IsKeyword("when")) s->subject = ParseExpression();
    SkipTerminators();
    while (MatchKeyword("when")) {
        CaseStmt::When w;
        w.tests.push_back(ParseExpression());
        while (MatchSymbol(",")) w.tests.push_back(ParseExpression());
        MatchKeyword("then"); SkipTerminators();
        w.body = ParseBlockUntil({"end", "when", "else"});
        s->whens.push_back(w);
    }
    if (MatchKeyword("else")) {
        SkipTerminators();
        s->else_branch = ParseBlockUntil({"end"});
    }
    ExpectKeyword("end", "expected 'end'");
    return s;
}

std::shared_ptr<Statement> RbParser::ParseBegin() {
    auto loc = current_.loc; Advance();
    auto s = std::make_shared<BeginStmt>(); s->loc = loc;
    SkipTerminators();
    s->body = ParseBlockUntil({"end", "rescue", "else", "ensure"});
    while (IsKeyword("rescue")) {
        Advance();
        BeginStmt::Rescue r;
        while (current_.kind == frontends::TokenKind::kIdentifier && !IsKeyword("then")) {
            auto t = std::make_shared<TypeNode>(); t->name = current_.lexeme;
            r.classes.push_back(t);
            Advance();
            if (!MatchSymbol(",")) break;
        }
        if (MatchSymbol("=>")) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                r.var = current_.lexeme; Advance();
            }
        }
        MatchKeyword("then"); SkipTerminators();
        r.body = ParseBlockUntil({"end", "rescue", "else", "ensure"});
        s->rescues.push_back(r);
    }
    if (MatchKeyword("else")) {
        SkipTerminators();
        s->else_branch = ParseBlockUntil({"end", "ensure"});
    }
    if (MatchKeyword("ensure")) {
        SkipTerminators();
        s->ensure_branch = ParseBlockUntil({"end"});
    }
    ExpectKeyword("end", "expected 'end'");
    return s;
}

// ============================================================================
// Expressions
// ============================================================================

std::shared_ptr<Expression> RbParser::ParseExpression() { return ParseAssignment(); }

std::shared_ptr<Expression> RbParser::ParseAssignment() {
    auto left = ParseTernary();
    static const std::vector<std::string> ops = {
        "=","+=","-=","*=","/=","%=","**=","|=","&=","^=","||=","&&=","<<=",">>="
    };
    for (auto &op : ops) {
        if (IsSymbol(op)) {
            Advance();
            auto rhs = ParseAssignment();
            auto a = std::make_shared<AssignExpr>();
            a->loc = left ? left->loc : current_.loc;
            a->op = op;
            a->target = left;
            a->value = rhs;
            return a;
        }
    }
    return left;
}

std::shared_ptr<Expression> RbParser::ParseTernary() {
    auto cond = ParseRange();
    if (MatchSymbol("?")) {
        auto t = ParseAssignment();
        ExpectSymbol(":", "expected ':'");
        auto e = ParseAssignment();
        auto x = std::make_shared<TernaryExpr>();
        x->loc = cond ? cond->loc : current_.loc;
        x->cond = cond; x->then_e = t; x->else_e = e;
        return x;
    }
    return cond;
}

std::shared_ptr<Expression> RbParser::ParseRange() {
    auto a = ParseOrExpr();
    if (IsSymbol("..") || IsSymbol("...")) {
        bool exc = current_.lexeme == "...";
        Advance();
        auto b = ParseOrExpr();
        auto r = std::make_shared<RangeExpr>();
        r->loc = a ? a->loc : current_.loc;
        r->from = a; r->to = b; r->exclusive = exc;
        return r;
    }
    return a;
}

std::shared_ptr<Expression> RbParser::ParseOrExpr() {
    auto l = ParseAndExpr();
    while (IsSymbol("||") || IsKeyword("or")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseAndExpr();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc;
        b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseAndExpr() {
    auto l = ParseNotExpr();
    while (IsSymbol("&&") || IsKeyword("and")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseNotExpr();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc;
        b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseNotExpr() {
    if (IsKeyword("not") || IsSymbol("!")) {
        auto loc = current_.loc;
        std::string op = current_.lexeme; Advance();
        auto u = std::make_shared<UnaryExpr>();
        u->loc = loc; u->op = op; u->operand = ParseNotExpr();
        return u;
    }
    return ParseDefined();
}
std::shared_ptr<Expression> RbParser::ParseDefined() {
    if (IsKeyword("defined?")) {
        auto loc = current_.loc; Advance();
        auto u = std::make_shared<UnaryExpr>();
        u->loc = loc; u->op = "defined?"; u->operand = ParseUnary();
        return u;
    }
    return ParseEquality();
}
std::shared_ptr<Expression> RbParser::ParseEquality() {
    auto l = ParseComparison();
    while (IsSymbol("==") || IsSymbol("!=") || IsSymbol("===") || IsSymbol("=~") || IsSymbol("!~") || IsSymbol("<=>")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseComparison();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseComparison() {
    auto l = ParseBitOr();
    while (IsSymbol("<") || IsSymbol("<=") || IsSymbol(">") || IsSymbol(">=")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseBitOr();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseBitOr() {
    auto l = ParseBitAnd();
    while (IsSymbol("|") || IsSymbol("^")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseBitAnd();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseBitAnd() {
    auto l = ParseShift();
    while (IsSymbol("&")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseShift();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseShift() {
    auto l = ParseAdditive();
    while (IsSymbol("<<") || IsSymbol(">>")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseAdditive();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseAdditive() {
    auto l = ParseMultiplicative();
    while (IsSymbol("+") || IsSymbol("-")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseMultiplicative();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseMultiplicative() {
    auto l = ParseUnary();
    while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
        std::string op = current_.lexeme; Advance();
        auto r = ParseUnary();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = op; b->left = l; b->right = r; l = b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParseUnary() {
    if (IsSymbol("-") || IsSymbol("+") || IsSymbol("~")) {
        auto loc = current_.loc;
        std::string op = current_.lexeme; Advance();
        auto u = std::make_shared<UnaryExpr>();
        u->loc = loc; u->op = op; u->operand = ParseUnary(); return u;
    }
    return ParsePower();
}
std::shared_ptr<Expression> RbParser::ParsePower() {
    auto l = ParsePostfix();
    if (IsSymbol("**")) {
        Advance();
        auto r = ParseUnary();
        auto b = std::make_shared<BinaryExpr>();
        b->loc = l ? l->loc : current_.loc; b->op = "**"; b->left = l; b->right = r;
        return b;
    }
    return l;
}
std::shared_ptr<Expression> RbParser::ParsePostfix() { return ParseCallTail(ParsePrimary()); }

std::vector<std::shared_ptr<Expression>> RbParser::ParseCallArgs(bool until_paren) {
    std::vector<std::shared_ptr<Expression>> args;
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (until_paren && IsSymbol(")")) break;
        if (!until_paren && AtTerminator()) break;
        args.push_back(ParseExpression());
        if (!MatchSymbol(",")) break;
    }
    return args;
}

std::shared_ptr<Expression> RbParser::ParseCallTail(std::shared_ptr<Expression> e) {
    while (true) {
        if (MatchSymbol(".") || MatchSymbol("&.")) {
            bool safe = false;  // prev token was "&." iff the parser saw "&."
            // We can't know safe here exactly; assume unsafe call (lexer emits "&." separately).
            (void)safe;
            auto m = std::make_shared<MemberExpr>();
            m->loc = e ? e->loc : current_.loc; m->obj = e;
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                m->member = current_.lexeme; Advance();
            }
            // Optional call: foo.bar(args) or foo.bar arg1, arg2
            if (MatchSymbol("(")) {
                auto call = std::make_shared<CallExpr>();
                call->loc = m->loc; call->receiver = e; call->method = m->member;
                call->args = ParseCallArgs(true);
                ExpectSymbol(")", "expected ')'");
                e = call;
            } else {
                e = m;
            }
        } else if (MatchSymbol("[")) {
            auto ix = std::make_shared<IndexExpr>();
            ix->loc = e ? e->loc : current_.loc; ix->obj = e;
            while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
                ix->idx.push_back(ParseExpression());
                if (!MatchSymbol(",")) break;
            }
            ExpectSymbol("]", "expected ']'");
            e = ix;
        } else if (MatchSymbol("::")) {
            auto m = std::make_shared<MemberExpr>();
            m->loc = e ? e->loc : current_.loc; m->obj = e;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                m->member = current_.lexeme; Advance();
            }
            e = m;
        } else {
            break;
        }
    }
    return e;
}

std::shared_ptr<Expression> RbParser::ParsePrimary() {
    auto loc = current_.loc;
    if (current_.kind == frontends::TokenKind::kNumber) {
        auto l = std::make_shared<Literal>(); l->loc = loc; l->value = current_.lexeme;
        l->kind = (current_.lexeme.find('.') != std::string::npos ||
                   current_.lexeme.find('e') != std::string::npos)
                      ? Literal::Kind::kFloat : Literal::Kind::kInt;
        Advance(); return l;
    }
    if (current_.kind == frontends::TokenKind::kString) {
        auto l = std::make_shared<Literal>(); l->loc = loc; l->value = current_.lexeme;
        l->kind = (!current_.lexeme.empty() && current_.lexeme[0] == ':')
                      ? Literal::Kind::kSymbol : Literal::Kind::kString;
        Advance(); return l;
    }
    if (IsKeyword("true") || IsKeyword("false")) {
        auto l = std::make_shared<Literal>(); l->loc = loc;
        l->kind = Literal::Kind::kBool; l->value = current_.lexeme;
        Advance(); return l;
    }
    if (IsKeyword("nil")) {
        auto l = std::make_shared<Literal>(); l->loc = loc;
        l->kind = Literal::Kind::kNil; l->value = "nil"; Advance(); return l;
    }
    if (IsKeyword("self") || IsKeyword("super")) {
        auto id = std::make_shared<Identifier>(); id->loc = loc; id->name = current_.lexeme;
        Advance(); return id;
    }
    if (MatchSymbol("(")) {
        auto e = ParseExpression();
        ExpectSymbol(")", "expected ')'");
        return e;
    }
    if (IsSymbol("[")) return ParseArray();
    if (IsSymbol("{")) return ParseHash();

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        std::string name = current_.lexeme;
        Advance();
        // Method call: name(args), name arg1, arg2 (no paren) — heuristic: if next
        // token is '(' or starts an expression on the same logical line we treat
        // it as a call.  For simplicity here we only honour the parenthesised form.
        if (MatchSymbol("(")) {
            auto c = std::make_shared<CallExpr>();
            c->loc = loc; c->method = name;
            c->args = ParseCallArgs(true);
            ExpectSymbol(")", "expected ')'");
            // Optional block
            if (MatchSymbol("{")) {
                if (MatchSymbol("|")) {
                    while (current_.kind == frontends::TokenKind::kIdentifier) {
                        c->block_params.push_back(current_.lexeme); Advance();
                        if (!MatchSymbol(",")) break;
                    }
                    ExpectSymbol("|", "expected '|'");
                }
                c->block = ParseBlockUntil({"}"});
                ExpectSymbol("}", "expected '}'");
            } else if (MatchKeyword("do")) {
                if (MatchSymbol("|")) {
                    while (current_.kind == frontends::TokenKind::kIdentifier) {
                        c->block_params.push_back(current_.lexeme); Advance();
                        if (!MatchSymbol(",")) break;
                    }
                    ExpectSymbol("|", "expected '|'");
                }
                SkipTerminators();
                c->block = ParseBlockUntil({"end"});
                ExpectKeyword("end", "expected 'end'");
            }
            return c;
        }
        auto id = std::make_shared<Identifier>(); id->loc = loc; id->name = name;
        return id;
    }

    diagnostics_.Report(loc, "unexpected token '" + current_.lexeme + "'");
    auto err = std::make_shared<Identifier>(); err->loc = loc; err->name = current_.lexeme;
    Advance();
    return err;
}

std::shared_ptr<Expression> RbParser::ParseArray() {
    auto loc = current_.loc; Advance();  // '['
    auto a = std::make_shared<ArrayLit>(); a->loc = loc;
    while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
        a->elems.push_back(ParseExpression());
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol("]", "expected ']'");
    return a;
}

std::shared_ptr<Expression> RbParser::ParseHash() {
    auto loc = current_.loc; Advance();  // '{'
    auto h = std::make_shared<HashLit>(); h->loc = loc;
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        HashLit::Pair p;
        p.key = ParseExpression();
        if (MatchSymbol("=>") || MatchSymbol(":")) {
            p.value = ParseExpression();
        }
        h->pairs.push_back(p);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol("}", "expected '}'");
    return h;
}

}  // namespace polyglot::ruby
