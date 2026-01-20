#include "frontends/python/include/python_parser.h"

namespace polyglot::python {

namespace {

void ExtractDocstring(std::vector<std::shared_ptr<Statement>> &body, AstNode &node) {
    if (body.empty())
        return;
    auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(body.front());
    if (!expr_stmt)
        return;
    auto lit = std::dynamic_pointer_cast<Literal>(expr_stmt->expr);
    if (!lit || !lit->is_string)
        return;
    if (!node.doc.empty()) {
        node.doc += "\n";
    }
    node.doc += lit->value;
    body.erase(body.begin());
}

} // namespace

void PythonParser::Advance() { current_ = lexer_.NextToken(); }

frontends::Token PythonParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
        if (current_.is_doc) {
            if (!pending_doc_.empty()) {
                pending_doc_ += "\n";
            }
            pending_doc_ += current_.lexeme;
        }
        Advance();
    }
    return current_;
}

void PythonParser::SkipNewlines() {
    while (current_.kind == frontends::TokenKind::kNewline) {
        Advance();
        while (current_.kind == frontends::TokenKind::kComment) {
            Advance();
        }
    }
}

bool PythonParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool PythonParser::MatchSymbol(const std::string &symbol) {
    if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol) {
        Consume();
        return true;
    }
    return false;
}

bool PythonParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Consume();
        return true;
    }
    return false;
}

void PythonParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
    if (!MatchSymbol(symbol)) {
        diagnostics_.Report(current_.loc, message);
    }
}

void PythonParser::AttachPendingDoc(const std::shared_ptr<AstNode> &node) {
    if (!node || pending_doc_.empty())
        return;
    if (!node->doc.empty()) {
        node->doc += "\n";
    }
    node->doc += pending_doc_;
    pending_doc_.clear();
}

void PythonParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile &&
           current_.kind != frontends::TokenKind::kDedent &&
           current_.kind != frontends::TokenKind::kNewline) {
        if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "def" || current_.lexeme == "if" || current_.lexeme == "while" ||
                current_.lexeme == "for" || current_.lexeme == "return" ||
                current_.lexeme == "import" || current_.lexeme == "from" ||
                current_.lexeme == "class" || current_.lexeme == "try" ||
                current_.lexeme == "with" || current_.lexeme == "match" ||
                current_.lexeme == "async" || current_.lexeme == "raise" ||
                current_.lexeme == "pass" || current_.lexeme == "break" ||
                current_.lexeme == "continue") {
                return;
            }
        }
        Consume();
    }
}

std::shared_ptr<Expression> PythonParser::ParseLambda() {
    auto lam = std::make_shared<LambdaExpression>();
    lam->loc = current_.loc;
    MatchKeyword("lambda");
    if (!IsSymbol(":")) {
        bool kwonly = false;
        bool seen_kwarg = false;
        while (true) {
            if (MatchSymbol("*")) {
                if (IsSymbol(",") || IsSymbol(":")) {
                    kwonly = true;
                } else if (current_.kind == frontends::TokenKind::kIdentifier) {
                    Parameter p;
                    p.name = current_.lexeme;
                    p.is_vararg = true;
                    Consume();
                    if (MatchSymbol(":")) {
                        p.annotation = ParseExpression();
                    }
                    if (MatchSymbol("=")) {
                        p.default_value = ParseExpression();
                    }
                    lam->params.push_back(std::move(p));
                    kwonly = true;
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name after '*'");
                    kwonly = true;
                }
            } else if (MatchSymbol("**")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    Parameter p;
                    p.name = current_.lexeme;
                    p.is_kwarg = true;
                    Consume();
                    if (MatchSymbol(":")) {
                        p.annotation = ParseExpression();
                    }
                    if (MatchSymbol("=")) {
                        p.default_value = ParseExpression();
                    }
                    lam->params.push_back(std::move(p));
                    seen_kwarg = true;
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name after '**'");
                }
            } else if (current_.kind == frontends::TokenKind::kIdentifier) {
                Parameter p;
                p.name = current_.lexeme;
                p.is_kwonly = kwonly;
                Consume();
                if (MatchSymbol(":")) {
                    p.annotation = ParseExpression();
                }
                if (MatchSymbol("=")) {
                    p.default_value = ParseExpression();
                }
                lam->params.push_back(std::move(p));
            }
            if (seen_kwarg) {
                if (MatchSymbol(",")) {
                    diagnostics_.Report(current_.loc, "Unexpected parameter after '**'");
                }
                break;
            }
            if (!MatchSymbol(","))
                break;
            if (IsSymbol(":"))
                break;
        }
    }
    ExpectSymbol(":", "Expected ':' in lambda expression");
    lam->body = ParseExpression();
    return lam;
}

std::shared_ptr<Expression> PythonParser::ParseAtom() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "lambda")
            return ParseLambda();
        if (current_.lexeme == "True" || current_.lexeme == "False" ||
            current_.lexeme == "None") {
            auto lit = std::make_shared<Literal>();
            lit->loc = current_.loc;
            lit->value = current_.lexeme;
            lit->is_string = false;
            Consume();
            return lit;
        }
        if (current_.lexeme == "yield") {
            return ParseYield();
        }
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto ident = std::make_shared<Identifier>();
        ident->name = current_.lexeme;
        ident->loc = current_.loc;
        Consume();
        return ident;
    }
    if (current_.kind == frontends::TokenKind::kNumber ||
        current_.kind == frontends::TokenKind::kString) {
        auto literal = std::make_shared<Literal>();
        literal->value = current_.lexeme;
        literal->loc = current_.loc;
        literal->is_string = current_.kind == frontends::TokenKind::kString;
        Consume();
        return literal;
    }
    if (IsSymbol("...")) {
        auto literal = std::make_shared<Literal>();
        literal->value = "...";
        literal->loc = current_.loc;
        literal->is_string = false;
        Consume();
        return literal;
    }
    if (IsSymbol("(")) {
        core::SourceLoc loc = current_.loc;
        Consume();
        if (IsSymbol(")")) {
            Consume();
            auto tup = std::make_shared<TupleExpression>();
            tup->loc = loc;
            tup->is_parenthesized = true;
            return tup;
        }
        auto first = ParseExpression();
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
            auto comp = ParseComprehensionTail(first, false, false, true);
            ExpectSymbol(")", "Expected ')' after generator expression");
            return comp;
        }
        if (MatchSymbol(",")) {
            auto tup = std::make_shared<TupleExpression>();
            tup->loc = loc;
            tup->is_parenthesized = true;
            tup->elements.push_back(first);
            while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                tup->elements.push_back(ParseExpression());
                if (!MatchSymbol(","))
                    break;
            }
            MatchSymbol(")");
            return tup;
        }
        MatchSymbol(")");
        return first;
    }
    if (IsSymbol("[")) {
        auto list_loc = current_.loc;
        Consume();
        if (MatchSymbol("]")) {
            auto lst = std::make_shared<ListExpression>();
            lst->loc = list_loc;
            return lst;
        }
        auto first = ParseExpression();
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
            auto comp = ParseComprehensionTail(first, false, false, false);
            ExpectSymbol("]", "Expected ']' after list comprehension");
            return comp;
        }
        auto lst = std::make_shared<ListExpression>();
        lst->loc = list_loc;
        lst->elements.push_back(first);
        while (MatchSymbol(",")) {
            if (IsSymbol("]"))
                break;
            lst->elements.push_back(ParseExpression());
        }
        ExpectSymbol("]", "Expected ']' to close list");
        return lst;
    }
    if (IsSymbol("{")) {
        auto brace_loc = current_.loc;
        Consume();
        if (MatchSymbol("}")) {
            auto dict = std::make_shared<DictExpression>();
            dict->loc = brace_loc;
            return dict;
        }
        auto key = ParseExpression();
        if (MatchSymbol(":")) {
            auto dict = std::make_shared<DictExpression>();
            dict->loc = brace_loc;
            auto value = ParseExpression();
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
                auto comp = ParseComprehensionTail(key, true, false, false, value);
                ExpectSymbol("}", "Expected '}' after dict comprehension");
                return comp;
            }
            dict->items.push_back({key, value});
            while (MatchSymbol(",")) {
                if (IsSymbol("}"))
                    break;
                auto k = ParseExpression();
                ExpectSymbol(":", "Expected ':' in dict literal");
                auto v = ParseExpression();
                dict->items.push_back({k, v});
            }
            ExpectSymbol("}", "Expected '}' to close dict");
            return dict;
        }
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
            auto comp = ParseComprehensionTail(key, false, true, false);
            ExpectSymbol("}", "Expected '}' after set comprehension");
            return comp;
        }
        auto set = std::make_shared<SetExpression>();
        set->loc = brace_loc;
        set->elements.push_back(key);
        while (MatchSymbol(",")) {
            if (IsSymbol("}"))
                break;
            set->elements.push_back(ParseExpression());
        }
        ExpectSymbol("}", "Expected '}' to close set literal");
        return set;
    }
    diagnostics_.Report(current_.loc, "Expected expression");
    return nullptr;
}

std::shared_ptr<Expression> PythonParser::ParsePostfix() {
    auto expr = ParseAtom();
    while (true) {
        if (IsSymbol("(")) {
            auto call = std::make_shared<CallExpression>();
            call->loc = expr ? expr->loc : current_.loc;
            call->callee = expr;
            Consume();
            ParseCallArguments(*call);
            expr = call;
            continue;
        }
        if (IsSymbol(".")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                auto attr = std::make_shared<AttributeExpression>();
                attr->object = expr;
                attr->attribute = current_.lexeme;
                attr->loc = current_.loc;
                Consume();
                expr = attr;
                continue;
            }
        }
        if (IsSymbol("[")) {
            expr = ParseSliceOrIndex(expr);
            continue;
        }
        break;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParsePower() {
    auto expr = ParsePostfix();
    if (IsSymbol("**")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = "**";
        bin->left = expr;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->right = ParseUnary();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseYield() {
    auto y = std::make_shared<YieldExpression>();
    y->loc = current_.loc;
    MatchKeyword("yield");
    if (MatchKeyword("from")) {
        y->is_from = true;
        y->value = ParseExpression();
        return y;
    }
    if (current_.kind != frontends::TokenKind::kNewline &&
        current_.kind != frontends::TokenKind::kDedent && !IsSymbol(")")) {
        y->value = ParseExpressionListAsExpr();
    }
    return y;
}

std::shared_ptr<Expression> PythonParser::ParseUnary() {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "await") {
        auto aw = std::make_shared<AwaitExpression>();
        aw->loc = current_.loc;
        Consume();
        aw->value = ParseUnary();
        return aw;
    }
    if (IsSymbol("+") || IsSymbol("-") || IsSymbol("~")) {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = current_.lexeme;
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseUnary();
        return unary;
    }
    return ParsePower();
}

std::shared_ptr<Expression> PythonParser::ParseMultiplicative() {
    auto expr = ParseUnary();
    while (IsSymbol("*") || IsSymbol("/") || IsSymbol("//") || IsSymbol("%") || IsSymbol("@")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseUnary();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseAdditive() {
    auto expr = ParseMultiplicative();
    while (IsSymbol("+") || IsSymbol("-")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseMultiplicative();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseShift() {
    auto expr = ParseAdditive();
    while (IsSymbol("<<") || IsSymbol(">>")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAdditive();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseBitwiseAnd() {
    auto expr = ParseShift();
    while (IsSymbol("&")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseShift();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseBitwiseXor() {
    auto expr = ParseBitwiseAnd();
    while (IsSymbol("^")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseBitwiseAnd();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseBitwiseOr() {
    auto expr = ParseBitwiseXor();
    while (IsSymbol("|")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseBitwiseXor();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseComparison() {
    auto expr = ParseBitwiseOr();
    while (true) {
        std::string op;
        if (IsSymbol("==") || IsSymbol("!=") || IsSymbol("<") || IsSymbol(">") ||
            IsSymbol("<=") || IsSymbol(">=")) {
            op = current_.lexeme;
            Consume();
        } else if (current_.kind == frontends::TokenKind::kKeyword &&
                   (current_.lexeme == "is" || current_.lexeme == "in" ||
                    current_.lexeme == "not")) {
            if (current_.lexeme == "is") {
                Consume();
                op = "is";
                if (current_.kind == frontends::TokenKind::kKeyword &&
                    current_.lexeme == "not") {
                    Consume();
                    op = "is not";
                }
            } else if (current_.lexeme == "in") {
                Consume();
                op = "in";
            } else if (current_.lexeme == "not") {
                Consume();
                if (current_.kind == frontends::TokenKind::kKeyword &&
                    current_.lexeme == "in") {
                    Consume();
                    op = "not in";
                } else {
                    diagnostics_.Report(current_.loc, "Expected 'in' after 'not'");
                    break;
                }
            }
        } else {
            break;
        }
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = op;
        bin->loc = expr ? expr->loc : current_.loc;
        bin->left = expr;
        bin->right = ParseBitwiseOr();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseNot() {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "not") {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = "not";
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseNot();
        return unary;
    }
    return ParseComparison();
}

std::shared_ptr<Expression> PythonParser::ParseAnd() {
    auto expr = ParseNot();
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "and") {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = "and";
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseNot();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseOr() {
    auto expr = ParseAnd();
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "or") {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = "or";
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAnd();
        expr = bin;
    }
    return expr;
}

std::vector<std::shared_ptr<Expression>> PythonParser::ParseExpressionList() {
    std::vector<std::shared_ptr<Expression>> elems;
    elems.push_back(ParseExpression());
    while (MatchSymbol(",")) {
        if (current_.kind == frontends::TokenKind::kNewline || IsSymbol(")") ||
            IsSymbol("]") || IsSymbol("}")) {
            break;
        }
        elems.push_back(ParseExpression());
    }
    return elems;
}

std::shared_ptr<Expression> PythonParser::ParseExpressionListAsExpr() {
    auto elems = ParseExpressionList();
    if (elems.size() == 1) {
        return elems[0];
    }
    auto tup = std::make_shared<TupleExpression>();
    tup->loc = elems.empty() ? current_.loc : elems.front()->loc;
    tup->elements = std::move(elems);
    tup->is_parenthesized = false;
    return tup;
}

std::shared_ptr<Expression> PythonParser::ParseComprehensionTail(std::shared_ptr<Expression> first,
                                                                 bool is_dict, bool is_set,
                                                                 bool is_generator,
                                                                 std::shared_ptr<Expression> value) {
    auto comp = std::make_shared<ComprehensionExpression>();
    comp->loc = current_.loc;
    comp->kind = is_dict ? ComprehensionExpression::Kind::kDict
                         : (is_set ? ComprehensionExpression::Kind::kSet
                                   : (is_generator ? ComprehensionExpression::Kind::kGenerator
                                                   : ComprehensionExpression::Kind::kList));
    if (is_dict) {
        comp->key = first;
        if (value) {
            comp->elem = value;
        } else {
            ExpectSymbol(":", "Expected ':' after dict comprehension key");
            comp->elem = ParseExpression();
        }
    } else {
        comp->elem = first;
    }
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
        Consume();
        Comprehension clause;
        clause.target = ParseExpressionListAsExpr();
        if (!MatchKeyword("in")) {
            diagnostics_.Report(current_.loc, "Expected 'in' in comprehension");
        }
        clause.iterable = ParseExpression();
        while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "if") {
            Consume();
            clause.ifs.push_back(ParseExpression());
        }
        comp->clauses.push_back(std::move(clause));
    }
    return comp;
}

void PythonParser::ParseCallArguments(CallExpression &call) {
    if (IsSymbol(")")) {
        Consume();
        return;
    }
    while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        CallArg arg;
        if (IsSymbol("*")) {
            arg.is_star = true;
            Consume();
            arg.value = ParseExpression();
        } else if (IsSymbol("**")) {
            arg.is_kwstar = true;
            Consume();
            arg.value = ParseExpression();
        } else {
            auto expr = ParseExpression();
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
                arg.value = ParseComprehensionTail(expr, false, false, true);
                ExpectSymbol(")", "Expected ')' after generator expression argument");
                call.args.push_back(std::move(arg));
                return;
            }
            if (IsSymbol("=") && std::dynamic_pointer_cast<Identifier>(expr)) {
                arg.keyword = std::dynamic_pointer_cast<Identifier>(expr)->name;
                Consume();
                arg.value = ParseExpression();
            } else {
                arg.value = expr;
            }
        }
        call.args.push_back(std::move(arg));
        if (!MatchSymbol(","))
            break;
        if (IsSymbol(")"))
            break;
    }
    MatchSymbol(")");
}

std::shared_ptr<Expression> PythonParser::ParseSliceOrIndex(std::shared_ptr<Expression> obj) {
    core::SourceLoc loc = current_.loc;
    Consume();
    std::shared_ptr<Expression> start;
    if (!IsSymbol(":") && !IsSymbol("]")) {
        start = ParseExpression();
    }

    if (MatchSymbol(":")) {
        std::shared_ptr<Expression> stop;
        std::shared_ptr<Expression> step;
        if (!IsSymbol("]")) {
            stop = ParseExpression();
        }
        if (MatchSymbol(":")) {
            if (!IsSymbol("]"))
                step = ParseExpression();
        }
        ExpectSymbol("]", "Expected ']' after slice");
        auto s = std::make_shared<SliceExpression>();
        s->loc = loc;
        s->start = start;
        s->stop = stop;
        s->step = step;
        auto idx = std::make_shared<IndexExpression>();
        idx->loc = loc;
        idx->object = obj;
        idx->index = s;
        return idx;
    }
    ExpectSymbol("]", "Expected ']' after subscript");
    auto idx = std::make_shared<IndexExpression>();
    idx->loc = loc;
    idx->object = obj;
    idx->index = start;
    return idx;
}

std::shared_ptr<Expression> PythonParser::ParseNamedExpression() {
    auto expr = ParseOr();
    if (MatchSymbol(":=")) {
        auto named = std::make_shared<NamedExpression>();
        named->loc = expr ? expr->loc : current_.loc;
        named->target = expr;
        named->value = ParseNamedExpression();
        return named;
    }
    return expr;
}

std::shared_ptr<Expression> PythonParser::ParseExpression() { return ParseNamedExpression(); }

std::shared_ptr<Statement> PythonParser::ParseImport() {
    auto stmt = std::make_shared<ImportStatement>();
    stmt->loc = current_.loc;
    if (MatchKeyword("from")) {
        stmt->is_from = true;
        int dots = 0;
        while (IsSymbol(".") || IsSymbol("...")) {
            dots += current_.lexeme == "..." ? 3 : 1;
            Consume();
        }
        if (dots > 0) {
            stmt->module.append(static_cast<size_t>(dots), '.');
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            stmt->module += current_.lexeme;
            Consume();
            while (MatchSymbol(".")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    stmt->module += "." + current_.lexeme;
                    Consume();
                } else {
                    diagnostics_.Report(current_.loc, "Expected module name after '.'");
                    break;
                }
            }
        }
        if (!MatchKeyword("import")) {
            diagnostics_.Report(current_.loc, "Expected 'import' in from-import");
        }
    } else {
        MatchKeyword("import");
        stmt->is_from = false;
    }
    auto parse_alias = [&]() -> ImportStatement::Alias {
        ImportStatement::Alias alias;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            alias.name = current_.lexeme;
            Consume();
            while (MatchSymbol(".")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    alias.name += "." + current_.lexeme;
                    Consume();
                } else {
                    diagnostics_.Report(current_.loc, "Expected module name after '.'");
                    break;
                }
            }
        } else {
            diagnostics_.Report(current_.loc, "Expected module name");
        }
        if (MatchKeyword("as")) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                alias.alias = current_.lexeme;
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected alias after 'as'");
            }
        }
        return alias;
    };
    if (stmt->is_from) {
        bool has_paren = MatchSymbol("(");
        if (IsSymbol("*")) {
            Consume();
            stmt->is_star = true;
        } else {
            stmt->names.push_back(parse_alias());
            while (MatchSymbol(",")) {
                if (IsSymbol(")"))
                    break;
                stmt->names.push_back(parse_alias());
            }
        }
        if (has_paren) {
            ExpectSymbol(")", "Expected ')' after import list");
        }
    } else {
        stmt->names.push_back(parse_alias());
        while (MatchSymbol(",")) {
            stmt->names.push_back(parse_alias());
        }
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseReturn() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("return");
    if (current_.kind != frontends::TokenKind::kNewline &&
        current_.kind != frontends::TokenKind::kDedent &&
        current_.kind != frontends::TokenKind::kEndOfFile) {
        stmt->value = ParseExpressionListAsExpr();
    }
    return stmt;
}

std::vector<std::shared_ptr<Statement>> PythonParser::ParseSuite() {
    std::vector<std::shared_ptr<Statement>> body;
    if (IsSymbol(":")) {
        Consume();
        if (current_.kind == frontends::TokenKind::kNewline) {
            Consume();
            if (current_.kind != frontends::TokenKind::kIndent) {
                diagnostics_.Report(current_.loc, "Expected indentation");
                return body;
            }
            Consume();
            SkipNewlines();
            while (current_.kind != frontends::TokenKind::kDedent &&
                   current_.kind != frontends::TokenKind::kEndOfFile) {
                auto stmt = ParseStatement();
                if (stmt)
                    body.push_back(stmt);
                SkipNewlines();
            }
            if (current_.kind == frontends::TokenKind::kDedent) {
                Consume();
            }
        } else {
            // single-line suite
            auto stmt = ParseStatement();
            if (stmt)
                body.push_back(stmt);
        }
    } else {
        diagnostics_.Report(current_.loc, "Expected ':' after header");
    }
    return body;
}

std::shared_ptr<Statement> PythonParser::ParseIf() {
    auto stmt = std::make_shared<IfStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("if");
    stmt->condition = ParseExpression();
    stmt->then_body = ParseSuite();
    SkipNewlines();
    auto tail = stmt;
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "elif") {
        auto elif_stmt = std::make_shared<IfStatement>();
        elif_stmt->loc = current_.loc;
        Consume();
        elif_stmt->condition = ParseExpression();
        elif_stmt->then_body = ParseSuite();
        tail->else_body.push_back(elif_stmt);
        tail = elif_stmt;
        SkipNewlines();
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "else") {
        Consume();
        tail->else_body = ParseSuite();
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseWhile() {
    auto stmt = std::make_shared<WhileStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("while");
    stmt->condition = ParseExpression();
    stmt->body = ParseSuite();
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseFor(bool is_async) {
    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = current_.loc;
    stmt->is_async = is_async;
    MatchKeyword("for");
    stmt->target = ParseExpressionListAsExpr();
    if (!MatchKeyword("in")) {
        diagnostics_.Report(current_.loc, "Expected 'in' in for statement");
    }
    stmt->iterable = ParseExpression();
    stmt->body = ParseSuite();
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseAsyncFunction() {
    MatchKeyword("async");
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "def") {
        auto stmt = ParseFunction();
        if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
            fn->is_async = true;
        }
        return stmt;
    }
    diagnostics_.Report(current_.loc, "Expected 'def' after 'async'");
    return nullptr;
}

std::shared_ptr<Statement> PythonParser::ParseWith(bool is_async) {
    auto stmt = std::make_shared<WithStatement>();
    stmt->loc = current_.loc;
    stmt->is_async = is_async;
    MatchKeyword("with");
    while (true) {
        WithItem item;
        item.context_expr = ParseExpression();
        if (MatchKeyword("as")) {
            item.optional_vars = ParseExpression();
        }
        stmt->items.push_back(std::move(item));
        if (!MatchSymbol(","))
            break;
    }
    stmt->body = ParseSuite();
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseTry() {
    auto stmt = std::make_shared<TryStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("try");
    stmt->body = ParseSuite();
    SkipNewlines();
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "except") {
        ExceptHandler handler;
        handler.body.clear();
        Consume();
        if (!IsSymbol(":")) {
            handler.type = ParseExpression();
            if (MatchKeyword("as")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    handler.name = current_.lexeme;
                    Consume();
                } else {
                    diagnostics_.Report(current_.loc, "Expected name after 'as'");
                }
            }
        }
        handler.body = ParseSuite();
        stmt->handlers.push_back(std::move(handler));
        SkipNewlines();
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "else") {
        Consume();
        stmt->orelse = ParseSuite();
        SkipNewlines();
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "finally") {
        Consume();
        stmt->finalbody = ParseSuite();
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseMatch() {
    auto stmt = std::make_shared<MatchStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("match");
    stmt->subject = ParseExpression();
    ExpectSymbol(":", "Expected ':' after match subject");
    if (current_.kind == frontends::TokenKind::kNewline) {
        Consume();
        if (current_.kind != frontends::TokenKind::kIndent) {
            diagnostics_.Report(current_.loc, "Expected indentation after match");
            return stmt;
        }
        Consume();
        SkipNewlines();
        while (current_.kind != frontends::TokenKind::kDedent &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            if (!MatchKeyword("case")) {
                diagnostics_.Report(current_.loc, "Expected 'case' in match statement");
                Sync();
                SkipNewlines();
                continue;
            }
            MatchCase mc;
            mc.pattern = ParseExpression();
            if (MatchKeyword("if")) {
                mc.guard = ParseExpression();
            }
            mc.body = ParseSuite();
            stmt->cases.push_back(std::move(mc));
            SkipNewlines();
        }
        if (current_.kind == frontends::TokenKind::kDedent) {
            Consume();
        }
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseRaise() {
    auto stmt = std::make_shared<RaiseStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("raise");
    if (current_.kind != frontends::TokenKind::kNewline &&
        current_.kind != frontends::TokenKind::kDedent &&
        current_.kind != frontends::TokenKind::kEndOfFile) {
        stmt->value = ParseExpression();
        if (MatchKeyword("from")) {
            stmt->from_expr = ParseExpression();
        }
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParsePass() {
    auto stmt = std::make_shared<PassStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("pass");
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseBreak() {
    auto stmt = std::make_shared<BreakStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("break");
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseContinue() {
    auto stmt = std::make_shared<ContinueStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("continue");
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseGlobal() {
    auto stmt = std::make_shared<GlobalStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("global");
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        stmt->names.push_back(current_.lexeme);
        Consume();
        if (!MatchSymbol(","))
            break;
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseNonlocal() {
    auto stmt = std::make_shared<NonlocalStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("nonlocal");
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        stmt->names.push_back(current_.lexeme);
        Consume();
        if (!MatchSymbol(","))
            break;
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseAssert() {
    auto stmt = std::make_shared<AssertStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("assert");
    stmt->test = ParseExpression();
    if (MatchSymbol(",")) {
        stmt->msg = ParseExpression();
    }
    return stmt;
}

std::vector<std::shared_ptr<Expression>> PythonParser::ParseDecorators() {
    std::vector<std::shared_ptr<Expression>> decorators;
    while (IsSymbol("@")) {
        Consume();
        decorators.push_back(ParseExpression());
        if (current_.kind == frontends::TokenKind::kNewline) {
            Consume();
        }
        SkipNewlines();
    }
    return decorators;
}

std::shared_ptr<Statement> PythonParser::ParseFunction() {
    auto fn = std::make_shared<FunctionDef>();
    fn->loc = current_.loc;
    MatchKeyword("def");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        fn->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected function name");
    }
    ExpectSymbol("(", "Expected '(' after function name");
    if (!MatchSymbol(")")) {
        bool kwonly = false;
        bool seen_kwarg = false;
        while (true) {
            if (MatchSymbol("*")) {
                if (IsSymbol(")") || IsSymbol(",")) {
                    kwonly = true;
                } else if (current_.kind == frontends::TokenKind::kIdentifier) {
                    Parameter p;
                    p.name = current_.lexeme;
                    p.is_vararg = true;
                    Consume();
                    if (MatchSymbol(":")) {
                        p.annotation = ParseExpression();
                    }
                    if (MatchSymbol("=")) {
                        p.default_value = ParseExpression();
                    }
                    fn->params.push_back(std::move(p));
                    kwonly = true;
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name after '*'");
                    kwonly = true;
                }
            } else if (MatchSymbol("**")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    Parameter p;
                    p.name = current_.lexeme;
                    p.is_kwarg = true;
                    Consume();
                    if (MatchSymbol(":")) {
                        p.annotation = ParseExpression();
                    }
                    if (MatchSymbol("=")) {
                        p.default_value = ParseExpression();
                    }
                    fn->params.push_back(std::move(p));
                    seen_kwarg = true;
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name after '**'");
                }
            } else if (current_.kind == frontends::TokenKind::kIdentifier) {
                Parameter p;
                p.name = current_.lexeme;
                p.is_kwonly = kwonly;
                Consume();
                if (MatchSymbol(":")) {
                    p.annotation = ParseExpression();
                }
                if (MatchSymbol("=")) {
                    p.default_value = ParseExpression();
                }
                fn->params.push_back(std::move(p));
            } else {
                diagnostics_.Report(current_.loc, "Expected parameter name");
            }
            if (seen_kwarg) {
                if (MatchSymbol(",")) {
                    diagnostics_.Report(current_.loc, "Unexpected parameter after '**'");
                }
                break;
            }
            if (!MatchSymbol(","))
                break;
            if (IsSymbol(")"))
                break;
        }
        ExpectSymbol(")", "Expected ')' after parameters");
    }
    if (MatchSymbol("->")) {
        fn->return_annotation = ParseExpression();
    }
    fn->body = ParseSuite();
    ExtractDocstring(fn->body, *fn);
    return fn;
}

std::shared_ptr<Statement> PythonParser::ParseClass() {
    auto cls = std::make_shared<ClassDef>();
    cls->loc = current_.loc;
    MatchKeyword("class");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        cls->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected class name");
    }
    if (MatchSymbol("(")) {
        if (!IsSymbol(")")) {
            while (true) {
                auto expr = ParseExpression();
                if (IsSymbol("=") && std::dynamic_pointer_cast<Identifier>(expr)) {
                    CallArg kw;
                    kw.keyword = std::dynamic_pointer_cast<Identifier>(expr)->name;
                    Consume();
                    kw.value = ParseExpression();
                    cls->keywords.push_back(std::move(kw));
                } else {
                    cls->bases.push_back(expr);
                }
                if (!MatchSymbol(","))
                    break;
                if (IsSymbol(")"))
                    break;
            }
        }
        ExpectSymbol(")", "Expected ')' after base classes");
    }
    cls->body = ParseSuite();
    ExtractDocstring(cls->body, *cls);
    return cls;
}

std::shared_ptr<Statement> PythonParser::ParseStatement() {
    if (IsSymbol("@")) {
        auto decorators = ParseDecorators();
        std::shared_ptr<Statement> stmt;
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "async") {
            Consume();
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "def") {
                stmt = ParseFunction();
                if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
                    fn->is_async = true;
                    fn->decorators = std::move(decorators);
                }
            }
        } else if (current_.kind == frontends::TokenKind::kKeyword &&
                   current_.lexeme == "def") {
            stmt = ParseFunction();
            if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
                fn->decorators = std::move(decorators);
            }
        } else if (current_.kind == frontends::TokenKind::kKeyword &&
                   current_.lexeme == "class") {
            stmt = ParseClass();
            if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
                cls->decorators = std::move(decorators);
            }
        } else {
            diagnostics_.Report(current_.loc, "Expected 'class' or 'def' after decorator");
        }
        AttachPendingDoc(stmt);
        return stmt;
    }

    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "async") {
            Consume();
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "def") {
                auto stmt = ParseFunction();
                if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
                    fn->is_async = true;
                }
                AttachPendingDoc(stmt);
                return stmt;
            }
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "with") {
                auto stmt = ParseWith(true);
                AttachPendingDoc(stmt);
                return stmt;
            }
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "for") {
                auto stmt = ParseFor(true);
                AttachPendingDoc(stmt);
                return stmt;
            }
            diagnostics_.Report(current_.loc, "Expected 'def', 'with', or 'for' after 'async'");
        }
        if (current_.lexeme == "import" || current_.lexeme == "from") {
            auto stmt = ParseImport();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "def") {
            auto stmt = ParseFunction();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "class") {
            auto stmt = ParseClass();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "return") {
            auto stmt = ParseReturn();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "if") {
            auto stmt = ParseIf();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "while") {
            auto stmt = ParseWhile();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "for") {
            auto stmt = ParseFor(false);
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "with") {
            auto stmt = ParseWith(false);
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "try") {
            auto stmt = ParseTry();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "match") {
            auto stmt = ParseMatch();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "raise") {
            auto stmt = ParseRaise();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "pass") {
            auto stmt = ParsePass();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "break") {
            auto stmt = ParseBreak();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "continue") {
            auto stmt = ParseContinue();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "global") {
            auto stmt = ParseGlobal();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "nonlocal") {
            auto stmt = ParseNonlocal();
            AttachPendingDoc(stmt);
            return stmt;
        }
        if (current_.lexeme == "assert") {
            auto stmt = ParseAssert();
            AttachPendingDoc(stmt);
            return stmt;
        }
    }

    auto expr_list = ParseExpressionList();
    std::shared_ptr<Expression> expr;
    if (expr_list.size() == 1) {
        expr = expr_list.front();
    } else {
        auto tup = std::make_shared<TupleExpression>();
        tup->loc = expr_list.front() ? expr_list.front()->loc : current_.loc;
        tup->elements = expr_list;
        tup->is_parenthesized = false;
        expr = tup;
    }

    if (IsSymbol(":")) {
        auto assign = std::make_shared<Assignment>();
        assign->loc = expr ? expr->loc : current_.loc;
        assign->targets.push_back(expr);
        Consume();
        assign->annotation = ParseExpression();
        if (IsSymbol("=")) {
            Consume();
            assign->value = ParseExpressionListAsExpr();
        }
        AttachPendingDoc(assign);
        return assign;
    }

    if (current_.kind == frontends::TokenKind::kSymbol) {
        const std::string &op = current_.lexeme;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
            op == "%=" || op == "//=" || op == "**=" || op == "&=" || op == "|=" ||
            op == "^=" || op == "<<=" || op == ">>=" || op == "@=") {
            auto assign = std::make_shared<Assignment>();
            assign->loc = expr ? expr->loc : current_.loc;
            assign->op = op;
            assign->targets.push_back(expr);
            Consume();
            assign->value = ParseExpressionListAsExpr();
            if (assign->op == "=") {
                while (IsSymbol("=")) {
                    assign->targets.push_back(assign->value);
                    Consume();
                    assign->value = ParseExpressionListAsExpr();
                }
            } else if (assign->targets.size() > 1) {
                diagnostics_.Report(assign->loc, "Augmented assignment expects a single target");
            }
            AttachPendingDoc(assign);
            return assign;
        }
    }

    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->loc = current_.loc;
    expr_stmt->expr = expr;
    AttachPendingDoc(expr_stmt);
    return expr_stmt;
}

void PythonParser::ParseTopLevel() {
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
        return;
    }
    auto stmt = ParseStatement();
    if (stmt) {
        module_->body.push_back(stmt);
    } else {
        Sync();
    }
}

void PythonParser::ParseModule() {
    Consume();
    SkipNewlines();
    for (;;) {
        ParseTopLevel();
        SkipNewlines();
        if (current_.kind == frontends::TokenKind::kEndOfFile) {
            break;
        }
    }
    ExtractDocstring(module_->body, *module_);
}

std::shared_ptr<Module> PythonParser::TakeModule() { return module_; }

} // namespace polyglot::python
