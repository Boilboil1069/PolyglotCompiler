#include "frontends/python/include/python_parser.h"

namespace polyglot::python {

void PythonParser::Advance() { current_ = lexer_.NextToken(); }

frontends::Token PythonParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
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

void PythonParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile &&
           current_.kind != frontends::TokenKind::kDedent &&
           current_.kind != frontends::TokenKind::kNewline) {
        if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "def" || current_.lexeme == "if" || current_.lexeme == "while" ||
                current_.lexeme == "for" || current_.lexeme == "return" ||
                current_.lexeme == "import" || current_.lexeme == "from" ||
                current_.lexeme == "class") {
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
        while (true) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                Parameter p;
                p.name = current_.lexeme;
                Consume();
                if (MatchSymbol(":")) {
                    p.annotation = ParseExpression();
                }
                lam->params.push_back(std::move(p));
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

std::shared_ptr<Expression> PythonParser::ParsePrimary() {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "lambda") {
        return ParseLambda();
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
        Consume();
        return literal;
    }
    if (IsSymbol("(")) {
        Consume();
        auto expr = ParseExpression();
        MatchSymbol(")");
        return expr;
    }
    diagnostics_.Report(current_.loc, "Expected expression");
    return nullptr;
}

std::shared_ptr<Expression> PythonParser::ParsePostfix() {
    auto expr = ParsePrimary();
    while (true) {
        if (IsSymbol("(")) {
            auto call = std::make_shared<CallExpression>();
            call->loc = expr ? expr->loc : current_.loc;
            call->callee = expr;
            Consume();
            if (!IsSymbol(")")) {
                call->args.push_back(ParseExpression());
                while (MatchSymbol(",")) {
                    call->args.push_back(ParseExpression());
                }
            }
            MatchSymbol(")");
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
            Consume();
            auto idx = std::make_shared<IndexExpression>();
            idx->object = expr;
            idx->loc = current_.loc;
            idx->index = ParseExpression();
            MatchSymbol("]");
            expr = idx;
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

std::shared_ptr<Expression> PythonParser::ParseUnary() {
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
    while (IsSymbol("*") || IsSymbol("/") || IsSymbol("//") || IsSymbol("%")) {
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

std::shared_ptr<Expression> PythonParser::ParseComparison() {
    auto expr = ParseAdditive();
    while (current_.kind == frontends::TokenKind::kKeyword || IsSymbol("==") || IsSymbol("!=") ||
           IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=")) {
        std::string op = current_.lexeme;
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = op;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAdditive();
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

std::shared_ptr<Expression> PythonParser::ParseExpression() { return ParseOr(); }

std::shared_ptr<Statement> PythonParser::ParseImport() {
    auto stmt = std::make_shared<ImportStatement>();
    stmt->loc = current_.loc;
    if (MatchKeyword("from")) {
        stmt->is_from = true;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            stmt->module = current_.lexeme;
            Consume();
            while (IsSymbol(".")) {
                Consume();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    stmt->module += "." + current_.lexeme;
                    Consume();
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
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        stmt->name = current_.lexeme;
        Consume();
        while (!stmt->is_from && IsSymbol(".")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                stmt->name += "." + current_.lexeme;
                Consume();
            }
        }
    } else {
        diagnostics_.Report(current_.loc, "Expected module name");
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "as") {
        Consume();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            stmt->alias = current_.lexeme;
            Consume();
        } else {
            diagnostics_.Report(current_.loc, "Expected alias after 'as'");
        }
    }
    return stmt;
}

std::shared_ptr<Statement> PythonParser::ParseReturn() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("return");
    stmt->value = ParseExpression();
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
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "else") {
        Consume();
        stmt->else_body = ParseSuite();
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

std::shared_ptr<Statement> PythonParser::ParseFor() {
    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("for");
    stmt->target = ParseExpression();
    if (!MatchKeyword("in")) {
        diagnostics_.Report(current_.loc, "Expected 'in' in for statement");
    }
    stmt->iterable = ParseExpression();
    stmt->body = ParseSuite();
    return stmt;
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
        while (true) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                Parameter p;
                p.name = current_.lexeme;
                Consume();
                if (MatchSymbol(":")) {
                    p.annotation = ParseExpression();
                }
                if (MatchSymbol("=")) {
                    ParseExpression(); // parse and discard default value
                }
                fn->params.push_back(std::move(p));
            }
            if (MatchSymbol(")")) {
                break;
            }
            ExpectSymbol(",", "Expected ',' between parameters");
        }
    }
    if (MatchSymbol("->")) {
        fn->return_annotation = ParseExpression();
    }
    fn->body = ParseSuite();
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
            cls->bases.push_back(ParseExpression());
            while (MatchSymbol(",")) {
                if (IsSymbol(")"))
                    break;
                cls->bases.push_back(ParseExpression());
            }
        }
        ExpectSymbol(")", "Expected ')' after base classes");
    }
    cls->body = ParseSuite();
    return cls;
}

std::shared_ptr<Statement> PythonParser::ParseStatement() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "import" || current_.lexeme == "from") {
            return ParseImport();
        }
        if (current_.lexeme == "def") {
            return ParseFunction();
        }
        if (current_.lexeme == "class") {
            return ParseClass();
        }
        if (current_.lexeme == "return") {
            return ParseReturn();
        }
        if (current_.lexeme == "if") {
            return ParseIf();
        }
        if (current_.lexeme == "while") {
            return ParseWhile();
        }
        if (current_.lexeme == "for") {
            return ParseFor();
        }
    }
    auto expr = ParseExpression();
    if (IsSymbol(":")) {
        auto assign = std::make_shared<Assignment>();
        assign->loc = expr ? expr->loc : current_.loc;
        assign->target = expr;
        Consume();
        assign->annotation = ParseExpression();
        if (IsSymbol("=")) {
            Consume();
            assign->value = ParseExpression();
        }
        return assign;
    }
    if (IsSymbol("=")) {
        auto assign = std::make_shared<Assignment>();
        assign->loc = expr ? expr->loc : current_.loc;
        assign->target = expr;
        Consume();
        assign->value = ParseExpression();
        return assign;
    }
    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->loc = current_.loc;
    expr_stmt->expr = expr;
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
}

std::shared_ptr<Module> PythonParser::TakeModule() { return module_; }

} // namespace polyglot::python
