#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

frontends::Token RustParser::Consume() {
    do {
        current_ = lexer_.NextToken();
    } while (current_.kind == frontends::TokenKind::kComment);
    return current_;
}

bool RustParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool RustParser::MatchSymbol(const std::string &symbol) {
    if (IsSymbol(symbol)) {
        Consume();
        return true;
    }
    return false;
}

bool RustParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Consume();
        return true;
    }
    return false;
}

void RustParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
    if (!MatchSymbol(symbol)) {
        diagnostics_.Report(current_.loc, message);
    }
}

void RustParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "fn" || current_.lexeme == "use" || current_.lexeme == "let" ||
                current_.lexeme == "struct" || current_.lexeme == "enum" ||
                current_.lexeme == "impl" || current_.lexeme == "trait" ||
                current_.lexeme == "mod") {
                return;
            }
        }
        if (current_.kind == frontends::TokenKind::kSymbol &&
            (current_.lexeme == ";" || current_.lexeme == "}")) {
            Consume();
            return;
        }
        Consume();
    }
}

int RustParser::GetBinaryPrecedence(const std::string &op) const {
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
    if (op == "<<" || op == ">>")
        return 8;
    if (op == "+" || op == "-")
        return 9;
    if (op == "*" || op == "/" || op == "%")
        return 10;
    return 0;
}

std::shared_ptr<BlockExpression> RustParser::ParseBlockExpression() {
    auto start_loc = current_.loc;
    if (!MatchSymbol("{")) {
        diagnostics_.Report(current_.loc, "Expected '{' to start block");
        return nullptr;
    }
    auto block = std::make_shared<BlockExpression>();
    block->loc = start_loc;
    while (current_.kind != frontends::TokenKind::kEndOfFile && !IsSymbol("}")) {
        auto stmt = ParseStatement(true);
        if (stmt)
            block->statements.push_back(stmt);
    }
    MatchSymbol("}");
    return block;
}

std::shared_ptr<Expression> RustParser::ParsePrimary() {
    if (IsSymbol("|")) {
        return ParseClosure();
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "move") {
        return ParseClosure();
    }
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "true" || current_.lexeme == "false")) {
        auto lit = std::make_shared<Literal>();
        lit->value = current_.lexeme;
        lit->loc = current_.loc;
        Consume();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kChar) {
        auto lit = std::make_shared<Literal>();
        lit->value = current_.lexeme;
        lit->loc = current_.loc;
        Consume();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        return ParsePathExpression();
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
    if (IsSymbol("{")) {
        return ParseBlockExpression();
    }
    diagnostics_.Report(current_.loc, "Expected expression");
    return nullptr;
}

std::shared_ptr<Expression> RustParser::ParseClosure() {
    auto closure = std::make_shared<ClosureExpression>();
    closure->loc = current_.loc;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "move") {
        closure->is_move = true;
        Consume();
    }
    ExpectSymbol("|", "Expected '|' to start closure parameters");
    if (!IsSymbol("|")) {
        // parse params until '|'
        while (!IsSymbol("|") && current_.kind != frontends::TokenKind::kEndOfFile) {
            ClosureExpression::Param p;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                p.name = current_.lexeme;
                Consume();
                if (MatchSymbol(":")) {
                    p.type = ParseType();
                }
                closure->params.push_back(std::move(p));
            } else {
                diagnostics_.Report(current_.loc, "Expected parameter in closure");
                break;
            }
            if (IsSymbol(",")) {
                Consume();
                continue;
            } else {
                break;
            }
        }
    }
    ExpectSymbol("|", "Expected '|' to end closure parameters");
    closure->body = ParseExpression();
    return closure;
}

std::shared_ptr<Expression> RustParser::ParsePathExpression() {
    auto path = std::make_shared<PathExpression>();
    path->loc = current_.loc;
    if (IsSymbol("::")) {
        path->is_absolute = true;
        Consume();
    }
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        path->segments.push_back(current_.lexeme);
        Consume();
        if (IsSymbol("::")) {
            Consume();
            continue;
        }
        break;
    }
    if (path->segments.empty()) {
        diagnostics_.Report(current_.loc, "Expected path segment");
    }
    return path;
}

std::shared_ptr<Pattern> RustParser::ParsePattern() {
    if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "_") {
        auto pat = std::make_shared<WildcardPattern>();
        pat->loc = current_.loc;
        Consume();
        return pat;
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        // could be path/struct/identifier
        auto save_loc = current_.loc;
        auto path = PathPattern{};
        while (current_.kind == frontends::TokenKind::kIdentifier) {
            path.segments.push_back(current_.lexeme);
            Consume();
            if (IsSymbol("::")) {
                Consume();
                continue;
            }
            break;
        }
        if (IsSymbol("{")) {
            return ParseStructPattern(path);
        }
        if (path.segments.size() == 1) {
            auto id = std::make_shared<IdentifierPattern>();
            id->loc = save_loc;
            id->name = path.segments[0];
            return id;
        }
        auto pp = std::make_shared<PathPattern>(path);
        pp->loc = save_loc;
        return pp;
    }
    if (IsSymbol("(")) {
        auto tuple = std::make_shared<TuplePattern>();
        tuple->loc = current_.loc;
        Consume();
        if (!IsSymbol(")")) {
            tuple->elements.push_back(ParsePattern());
            while (MatchSymbol(",")) {
                if (IsSymbol(")"))
                    break;
                tuple->elements.push_back(ParsePattern());
            }
        }
        MatchSymbol(")");
        return tuple;
    }
    diagnostics_.Report(current_.loc, "Expected pattern");
    return nullptr;
}

std::shared_ptr<Pattern> RustParser::ParseStructPattern(PathPattern path) {
    auto sp = std::make_shared<StructPattern>();
    sp->loc = current_.loc;
    sp->path = std::move(path);
    MatchSymbol("{");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (MatchSymbol("..")) {
            sp->has_rest = true;
            break;
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            sp->fields.push_back(current_.lexeme);
            Consume();
        } else {
            diagnostics_.Report(current_.loc, "Expected field pattern");
            break;
        }
        if (IsSymbol(",")) {
            Consume();
            continue;
        } else {
            break;
        }
    }
    MatchSymbol("}");
    return sp;
}

std::vector<std::shared_ptr<TypeNode>> RustParser::ParseGenericArgList() {
    std::vector<std::shared_ptr<TypeNode>> args;
    if (!MatchSymbol("<"))
        return args;
    if (!IsSymbol(">")) {
        args.push_back(ParseType());
        while (MatchSymbol(",")) {
            if (IsSymbol(">"))
                break;
            args.push_back(ParseType());
        }
    }
    ExpectSymbol(">", "Expected '>' to close generic arguments");
    return args;
}

std::shared_ptr<TypePath> RustParser::ParseTypePath() {
    auto type = std::make_shared<TypePath>();
    type->loc = current_.loc;
    if (IsSymbol("::")) {
        type->is_absolute = true;
        Consume();
    }
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        type->segments.push_back(current_.lexeme);
        Consume();
        type->generic_args.push_back(ParseGenericArgList());
        if (IsSymbol("::")) {
            Consume();
            continue;
        }
        break;
    }
    if (type->segments.empty()) {
        diagnostics_.Report(current_.loc, "Expected type path");
    }
    return type;
}

std::shared_ptr<LifetimeType> RustParser::ParseLifetime() {
    if (current_.kind == frontends::TokenKind::kLifetime) {
        auto lt = std::make_shared<LifetimeType>();
        lt->loc = current_.loc;
        lt->name = current_.lexeme;
        Consume();
        return lt;
    }
    return nullptr;
}

std::shared_ptr<TypeNode> RustParser::ParseType() {
    // reference types
    if (IsSymbol("&")) {
        auto ref = std::make_shared<ReferenceType>();
        ref->loc = current_.loc;
        Consume();
        ref->lifetime = ParseLifetime();
        if (MatchKeyword("mut")) {
            ref->is_mut = true;
        }
        ref->inner = ParseType();
        return ref;
    }
    // slices/arrays
    if (IsSymbol("[")) {
        auto start_loc = current_.loc;
        Consume();
        auto inner = ParseType();
        if (MatchSymbol(";")) {
            auto arr = std::make_shared<ArrayType>();
            arr->loc = start_loc;
            arr->inner = inner;
            // capture raw size expression tokens until ']'
            std::string size;
            while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
                size += current_.lexeme;
                Consume();
            }
            arr->size_expr = size;
            MatchSymbol("]");
            return arr;
        }
        auto slice = std::make_shared<SliceType>();
        slice->loc = start_loc;
        slice->inner = inner;
        MatchSymbol("]");
        return slice;
    }
    // function types: fn(...) -> ...
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "fn") {
        auto fn = std::make_shared<FunctionType>();
        fn->loc = current_.loc;
        Consume();
        ExpectSymbol("(", "Expected '(' in function type");
        if (!MatchSymbol(")")) {
            fn->params.push_back(ParseType());
            while (MatchSymbol(",")) {
                if (IsSymbol(")"))
                    break;
                fn->params.push_back(ParseType());
            }
            ExpectSymbol(")", "Expected ')' after function type params");
        }
        if (MatchSymbol("->")) {
            fn->return_type = ParseType();
        }
        return fn;
    }
    // tuple or parenthesized type
    if (IsSymbol("(")) {
        auto start_loc = current_.loc;
        Consume();
        if (IsSymbol(")")) {
            Consume();
            auto tup = std::make_shared<TupleType>();
            tup->loc = start_loc;
            return tup;
        }
        auto first = ParseType();
        if (MatchSymbol(")")) {
            return first; // parenthesized
        }
        auto tup = std::make_shared<TupleType>();
        tup->loc = start_loc;
        tup->elements.push_back(first);
        while (MatchSymbol(",")) {
            if (IsSymbol(")"))
                break;
            tup->elements.push_back(ParseType());
        }
        ExpectSymbol(")", "Expected ')' to close tuple type");
        return tup;
    }
    return ParseTypePath();
}

std::string RustParser::ParseDelimitedBody(const std::string &open, const std::string &close) {
    std::string body;
    int depth = 1;
    Consume(); // consume the opening delimiter already at current
    while (current_.kind != frontends::TokenKind::kEndOfFile && depth > 0) {
        if (IsSymbol(open)) {
            depth++;
        } else if (IsSymbol(close)) {
            depth--;
            if (depth == 0) {
                Consume();
                break;
            }
        }
        if (depth > 0) {
            body += current_.lexeme;
            Consume();
        }
    }
    return body;
}

std::vector<std::string> RustParser::ParseTypeParams() {
    std::vector<std::string> params;
    if (!MatchSymbol("<"))
        return params;
    if (!IsSymbol(">")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            params.push_back(current_.lexeme);
            Consume();
            while (MatchSymbol(",")) {
                if (IsSymbol(">"))
                    break;
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    params.push_back(current_.lexeme);
                    Consume();
                } else {
                    diagnostics_.Report(current_.loc, "Expected type parameter name");
                    break;
                }
            }
        }
    }
    ExpectSymbol(">", "Expected '>' to close type parameters");
    return params;
}

std::shared_ptr<Expression> RustParser::ParsePostfix() {
    auto expr = ParsePrimary();
    while (expr) {
        if (IsSymbol("!")) {
            // macro call: only if callee is path
            auto path = std::dynamic_pointer_cast<PathExpression>(expr);
            if (path) {
                Consume();
                auto macro = std::make_shared<MacroCallExpression>();
                macro->loc = expr->loc;
                macro->path = *path;
                if (IsSymbol("(")) {
                    macro->delimiter = "(";
                    macro->body = ParseDelimitedBody("(", ")");
                } else if (IsSymbol("{")) {
                    macro->delimiter = "{";
                    macro->body = ParseDelimitedBody("{", "}");
                } else if (IsSymbol("[")) {
                    macro->delimiter = "[";
                    macro->body = ParseDelimitedBody("[", "]");
                } else {
                    diagnostics_.Report(current_.loc, "Expected delimiter after macro '!'");
                }
                expr = macro;
                continue;
            }
        }
        if (IsSymbol("(")) {
            auto call = std::make_shared<CallExpression>();
            call->callee = expr;
            call->loc = expr->loc;
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
                auto mem = std::make_shared<MemberExpression>();
                mem->object = expr;
                mem->member = current_.lexeme;
                mem->loc = current_.loc;
                Consume();
                expr = mem;
                continue;
            }
            diagnostics_.Report(current_.loc, "Expected member name after '.'");
            break;
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

std::shared_ptr<Expression> RustParser::ParseUnary() {
    if (IsSymbol("+") || IsSymbol("-") || IsSymbol("!") || IsSymbol("&") || IsSymbol("*")) {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = current_.lexeme;
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseUnary();
        return unary;
    }
    return ParsePostfix();
}

std::shared_ptr<Expression> RustParser::ParseMultiplicative() {
    auto expr = ParseUnary();
    while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
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

std::shared_ptr<Expression> RustParser::ParseAdditive() {
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

std::shared_ptr<Expression> RustParser::ParseShift() {
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

std::shared_ptr<Expression> RustParser::ParseRelational() {
    auto expr = ParseShift();
    while (IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=")) {
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

std::shared_ptr<Expression> RustParser::ParseEquality() {
    auto expr = ParseRelational();
    while (IsSymbol("==") || IsSymbol("!=")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseRelational();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> RustParser::ParseBitwiseAnd() {
    auto expr = ParseEquality();
    while (IsSymbol("&")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseEquality();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> RustParser::ParseBitwiseXor() {
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

std::shared_ptr<Expression> RustParser::ParseBitwiseOr() {
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

std::shared_ptr<Expression> RustParser::ParseLogicalAnd() {
    auto expr = ParseBitwiseOr();
    while (IsSymbol("&&")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseBitwiseOr();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> RustParser::ParseLogicalOr() {
    auto expr = ParseLogicalAnd();
    while (IsSymbol("||")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseLogicalAnd();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> RustParser::ParseRange() {
    auto expr = ParseLogicalOr();
    while (IsSymbol("..") || IsSymbol("..=") || IsSymbol("...")) {
        bool inclusive = current_.lexeme != "..";
        Consume();
        auto range = std::make_shared<RangeExpression>();
        range->loc = expr ? expr->loc : current_.loc;
        range->start = expr;
        range->inclusive = inclusive;
        range->end = ParseLogicalOr();
        expr = range;
    }
    return expr;
}

std::shared_ptr<Expression> RustParser::ParseAssignment() {
    auto lhs = ParseRange();
    if (current_.kind == frontends::TokenKind::kSymbol) {
        const std::string &op = current_.lexeme;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
            op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
            Consume();
            auto assign = std::make_shared<AssignmentExpression>();
            assign->op = op;
            assign->loc = lhs ? lhs->loc : current_.loc;
            assign->left = lhs;
            assign->right = ParseAssignment();
            return assign;
        }
    }
    return lhs;
}

std::shared_ptr<Expression> RustParser::ParseIfExpression() {
    auto if_expr = std::make_shared<IfExpression>();
    if_expr->loc = current_.loc;
    MatchKeyword("if");
    if_expr->condition = ParseExpression();
    auto then_block = ParseBlockExpression();
    if (then_block) {
        if_expr->then_body = then_block->statements;
    }
    if (MatchKeyword("else")) {
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "if") {
            auto nested = ParseIfExpression();
            if (nested) {
                auto stmt = std::make_shared<ExprStatement>();
                stmt->expr = nested;
                if_expr->else_body.push_back(stmt);
            }
        } else {
            auto else_block = ParseBlockExpression();
            if (else_block) {
                if_expr->else_body = else_block->statements;
            }
        }
    }
    return if_expr;
}

std::shared_ptr<Expression> RustParser::ParseWhileExpression() {
    auto wh = std::make_shared<WhileExpression>();
    wh->loc = current_.loc;
    MatchKeyword("while");
    wh->condition = ParseExpression();
    auto body = ParseBlockExpression();
    if (body)
        wh->body = body->statements;
    return wh;
}

std::shared_ptr<Expression> RustParser::ParseMatchExpression() {
    auto m = std::make_shared<MatchExpression>();
    m->loc = current_.loc;
    MatchKeyword("match");
    m->scrutinee = ParseExpression();
    ExpectSymbol("{", "Expected '{' after match scrutinee");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto arm = std::make_shared<MatchArm>();
        arm->loc = current_.loc;
        arm->pattern = ParsePattern();
        if (MatchKeyword("if")) {
            arm->guard = ParseExpression();
        }
        ExpectSymbol("=>", "Expected '=>' in match arm");
        // arm body: expression or block
        if (IsSymbol("{")) {
            arm->body = ParseBlockExpression();
        } else {
            arm->body = ParseExpression();
        }
        m->arms.push_back(arm);
        if (IsSymbol(",")) {
            Consume();
        } else if (IsSymbol("}")) {
            break;
        }
    }
    MatchSymbol("}");
    return m;
}

std::shared_ptr<Expression> RustParser::ParseExpression() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "if") {
            return ParseIfExpression();
        }
        if (current_.lexeme == "while") {
            return ParseWhileExpression();
        }
        if (current_.lexeme == "match") {
            return ParseMatchExpression();
        }
    }
    return ParseAssignment();
}

std::shared_ptr<Statement> RustParser::ParseUse() {
    auto stmt = std::make_shared<UseDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("use");
    std::string path;
    if (IsSymbol("::")) {
        path += "::";
        Consume();
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        path += current_.lexeme;
        Consume();
        while (IsSymbol("::")) {
            path += "::";
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                path += current_.lexeme;
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected path segment");
                break;
            }
        }
    } else {
        diagnostics_.Report(current_.loc, "Expected use path");
    }
    stmt->path = path;
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseLet() {
    auto stmt = std::make_shared<LetStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("let");
    if (MatchKeyword("mut")) {
        stmt->is_mut = true;
    }
    stmt->pattern = ParsePattern();
    if (MatchSymbol(":")) {
        stmt->type_annotation = ParseType();
    }
    if (MatchSymbol("=")) {
        stmt->init = ParseExpression();
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseReturn() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("return");
    if (!IsSymbol(";")) {
        stmt->value = ParseExpression();
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseBreak() {
    auto stmt = std::make_shared<BreakStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("break");
    if (!IsSymbol(";")) {
        stmt->value = ParseExpression();
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseContinue() {
    auto stmt = std::make_shared<ContinueStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("continue");
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseLoop() {
    auto stmt = std::make_shared<LoopStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("loop");
    auto body = ParseBlockExpression();
    if (body)
        stmt->body = body->statements;
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseFor() {
    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("for");
    stmt->pattern = ParsePattern();
    if (!MatchKeyword("in")) {
        diagnostics_.Report(current_.loc, "Expected 'in' in for loop");
    }
    stmt->iterable = ParseExpression();
    auto body = ParseBlockExpression();
    if (body)
        stmt->body = body->statements;
    return stmt;
}

std::shared_ptr<Statement> RustParser::ParseStruct() {
    auto item = std::make_shared<StructItem>();
    item->loc = current_.loc;
    MatchKeyword("struct");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected struct name");
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                item->fields.push_back(current_.lexeme);
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected field name");
                break;
            }
            if (IsSymbol(",")) {
                Consume();
            } else {
                break;
            }
        }
        MatchSymbol("}");
    }
    MatchSymbol(";");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseEnum() {
    auto item = std::make_shared<EnumItem>();
    item->loc = current_.loc;
    MatchKeyword("enum");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected enum name");
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                item->variants.push_back(current_.lexeme);
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected variant name");
                break;
            }
            if (IsSymbol(",")) {
                Consume();
            } else {
                break;
            }
        }
        MatchSymbol("}");
    }
    MatchSymbol(";");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseFunction() {
    auto fn = std::make_shared<FunctionItem>();
    fn->loc = current_.loc;
    MatchKeyword("fn");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        fn->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected function name");
    }
    fn->type_params = ParseTypeParams();
    ExpectSymbol("(", "Expected '(' after function name");
    if (!MatchSymbol(")")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            FunctionItem::Param p;
            p.name = current_.lexeme;
            Consume();
            if (MatchSymbol(":")) {
                p.type = ParseType();
            }
            fn->params.push_back(std::move(p));
            while (MatchSymbol(",")) {
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    FunctionItem::Param p2;
                    p2.name = current_.lexeme;
                    Consume();
                    if (MatchSymbol(":")) {
                        p2.type = ParseType();
                    }
                    fn->params.push_back(std::move(p2));
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name");
                    break;
                }
            }
        }
        ExpectSymbol(")", "Expected ')' after parameters");
    }
    if (MatchSymbol("->")) {
        fn->return_type = ParseType();
    }
    auto body = ParseBlockExpression();
    if (body) {
        fn->body = body->statements;
    }
    return fn;
}

std::shared_ptr<Statement> RustParser::ParseImpl() {
    auto item = std::make_shared<ImplItem>();
    item->loc = current_.loc;
    MatchKeyword("impl");
    item->target_type = ParseType();
    ExpectSymbol("{", "Expected '{' in impl body");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto stmt = ParseStatement();
        if (stmt)
            item->items.push_back(stmt);
    }
    MatchSymbol("}");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseTrait() {
    auto item = std::make_shared<TraitItem>();
    item->loc = current_.loc;
    MatchKeyword("trait");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected trait name");
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto stmt = ParseStatement();
            if (stmt)
                item->items.push_back(stmt);
        }
        MatchSymbol("}");
    } else {
        diagnostics_.Report(current_.loc, "Expected '{' for trait body");
    }
    return item;
}

std::shared_ptr<Statement> RustParser::ParseMod() {
    auto item = std::make_shared<ModItem>();
    item->loc = current_.loc;
    MatchKeyword("mod");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected module name");
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto stmt = ParseStatement();
            if (stmt)
                item->items.push_back(stmt);
        }
        MatchSymbol("}");
    } else {
        MatchSymbol(";");
    }
    return item;
}

std::shared_ptr<Statement> RustParser::ParseStatement(bool allow_trailing_expr) {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "use") {
            return ParseUse();
        }
        if (current_.lexeme == "fn") {
            return ParseFunction();
        }
        if (current_.lexeme == "let") {
            return ParseLet();
        }
        if (current_.lexeme == "return") {
            return ParseReturn();
        }
        if (current_.lexeme == "break") {
            return ParseBreak();
        }
        if (current_.lexeme == "continue") {
            return ParseContinue();
        }
        if (current_.lexeme == "loop") {
            return ParseLoop();
        }
        if (current_.lexeme == "for") {
            return ParseFor();
        }
        if (current_.lexeme == "struct") {
            return ParseStruct();
        }
        if (current_.lexeme == "enum") {
            return ParseEnum();
        }
        if (current_.lexeme == "impl") {
            return ParseImpl();
        }
        if (current_.lexeme == "trait") {
            return ParseTrait();
        }
        if (current_.lexeme == "mod") {
            return ParseMod();
        }
    }
    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->loc = current_.loc;
    expr_stmt->expr = ParseExpression();
    if (!MatchSymbol(";")) {
        if (!allow_trailing_expr || !IsSymbol("}")) {
            diagnostics_.Report(current_.loc, "Expected ';'");
        }
    }
    return expr_stmt;
}

void RustParser::ParseItem() {
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
        return;
    }
    auto stmt = ParseStatement();
    if (stmt) {
        module_->items.push_back(stmt);
    } else {
        Sync();
    }
}

void RustParser::ParseModule() {
    Consume();
    for (;;) {
        ParseItem();
        if (current_.kind == frontends::TokenKind::kEndOfFile) {
            break;
        }
    }
}

std::shared_ptr<Module> RustParser::TakeModule() { return module_; }

} // namespace polyglot::rust
