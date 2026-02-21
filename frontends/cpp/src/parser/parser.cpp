#include "frontends/cpp/include/cpp_parser.h"

namespace polyglot::cpp {

void CppParser::Advance() {
    if (!pushback_.empty()) {
        current_ = pushback_.back();
        pushback_.pop_back();
        return;
    }
    current_ = lexer_.NextToken();
}

frontends::Token CppParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
        Advance();
    }
    return current_;
}

bool CppParser::IsSymbol(const std::string &symbol) const {
    if (current_.kind != frontends::TokenKind::kSymbol)
        return false;
    if (current_.lexeme == symbol)
        return true;
    if (symbol == ">" && current_.lexeme == ">>")
        return true;
    return false;
}

bool CppParser::MatchSymbol(const std::string &symbol) {
    if (current_.kind != frontends::TokenKind::kSymbol)
        return false;
    if (current_.lexeme == symbol) {
        Consume();
        return true;
    }
    if (symbol == ">" && current_.lexeme == ">>") {
        auto second = current_;
        second.lexeme = ">";
        Consume();
        pushback_.push_back(second);
        return true;
    }
    return false;
}

bool CppParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Consume();
        return true;
    }
    return false;
}

void CppParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
    if (!MatchSymbol(symbol)) {
        diagnostics_.Report(current_.loc, message);
    }
}

void CppParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "import" || current_.lexeme == "using" ||
                current_.lexeme == "module" || current_.lexeme == "concept" ||
                current_.lexeme == "switch" || current_.lexeme == "try" ||
                current_.lexeme == "throw" || current_.lexeme == "int" ||
                current_.lexeme == "void" || current_.lexeme == "float" ||
                current_.lexeme == "double" || current_.lexeme == "char" ||
                current_.lexeme == "bool" || current_.lexeme == "auto" ||
                current_.lexeme == "class" || current_.lexeme == "struct" ||
                current_.lexeme == "enum" || current_.lexeme == "namespace" ||
                current_.lexeme == "template") {
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

std::vector<Attribute> CppParser::ParseAttributes() {
    std::vector<Attribute> attrs;
    while (IsSymbol("[")) {
        // attempt to parse C++11 attribute [[...]]
        frontends::Token first = current_;
        Consume();
        if (!IsSymbol("[")) {
            // not actually an attribute, rewind token consumption by pushing back
            pushback_.push_back(current_);
            current_ = first;
            break;
        }
        Consume();
        std::string text;
        while (!(IsSymbol("]") || current_.kind == frontends::TokenKind::kEndOfFile)) {
            text += current_.lexeme;
            Consume();
        }
        ExpectSymbol("]", "Expected ']' to close attribute");
        ExpectSymbol("]", "Expected closing ']' for attribute");
        Attribute attr;
        attr.text = text;
        attrs.push_back(std::move(attr));
    }
    return attrs;
}

std::string CppParser::ParseQualifiedName() {
    std::string name;
    while (current_.kind == frontends::TokenKind::kIdentifier ||
           current_.kind == frontends::TokenKind::kKeyword) {
        if (!name.empty())
            name += "::";
        name += current_.lexeme;
        Consume();
        if (!MatchSymbol("::"))
            break;
    }
    return name;
}

// --- Types ----------------------------------------------------
std::shared_ptr<TypeNode> CppParser::ParseType() {
    auto parse_cv = [&]() -> std::pair<bool, bool> {
        bool c = false, v = false;
        while (current_.kind == frontends::TokenKind::kKeyword &&
               (current_.lexeme == "const" || current_.lexeme == "volatile")) {
            if (current_.lexeme == "const")
                c = true;
            if (current_.lexeme == "volatile")
                v = true;
            Consume();
        }
        return {c, v};
    };

    auto wrap_cv = [&](std::shared_ptr<TypeNode> inner, bool c, bool v) {
        if (!c && !v)
            return inner;
        auto q = std::make_shared<QualifiedType>();
        q->loc = inner ? inner->loc : current_.loc;
        q->is_const = c;
        q->is_volatile = v;
        q->inner = inner;
        return std::static_pointer_cast<TypeNode>(q);
    };

    auto [leading_const, leading_volatile] = parse_cv();

    std::shared_ptr<TypeNode> base;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "decltype") {
        auto simple = std::make_shared<SimpleType>();
        simple->loc = current_.loc;
        std::string name = "decltype";
        Consume();
        if (MatchSymbol("(")) {
            int depth = 1;
            name += "(";
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                name += current_.lexeme;
                if (IsSymbol("("))
                    depth++;
                if (IsSymbol(")"))
                    depth--;
                Consume();
            }
            name += ")";
        }
        simple->name = name;
        base = simple;
    } else if (current_.kind == frontends::TokenKind::kIdentifier ||
               current_.kind == frontends::TokenKind::kKeyword) {
        auto simple = std::make_shared<SimpleType>();
        simple->loc = current_.loc;
        std::string name = ParseQualifiedName();
        if (IsSymbol("<")) {
            int depth = 0;
            name += "<";
            Consume();
            depth++;
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                name += current_.lexeme;
                if (IsSymbol("<"))
                    depth++;
                else if (IsSymbol(">"))
                    depth--;
                Consume();
                if (depth > 0 && MatchSymbol(">") && depth == 1) {
                    name += ">";
                    depth--;
                    break;
                }
            }
            name += ">";
        }
        simple->name = name;
        base = simple;
    }

    base = wrap_cv(base, leading_const, leading_volatile);

    while (true) {
        if (IsSymbol("*")) {
            auto ptr = std::make_shared<PointerType>();
            ptr->loc = current_.loc;
            Consume();
            auto [pc, pv] = parse_cv();
            ptr->is_const = pc;
            ptr->is_volatile = pv;
            ptr->pointee = base;
            base = ptr;
            continue;
        }
        if (IsSymbol("&") || IsSymbol("&&")) {
            auto ref = std::make_shared<ReferenceType>();
            ref->loc = current_.loc;
            ref->is_rvalue = IsSymbol("&&");
            Consume();
            ref->referent = base;
            base = ref;
            continue;
        }
        if (IsSymbol("[")) {
            auto arr = std::make_shared<ArrayType>();
            arr->loc = current_.loc;
            Consume();
            std::string size;
            while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
                size += current_.lexeme;
                Consume();
            }
            MatchSymbol("]");
            arr->element_type = base;
            arr->size_expr = size;
            base = arr;
            continue;
        }
        if (IsSymbol("(")) {
            auto fn = std::make_shared<FunctionType>();
            fn->loc = current_.loc;
            fn->return_type = base;
            Consume();
            if (!IsSymbol(")")) {
                fn->params.push_back(ParseType());
                while (MatchSymbol(",")) {
                    if (IsSymbol(")"))
                        break;
                    fn->params.push_back(ParseType());
                }
            }
            ExpectSymbol(")", "Expected ')' in function type");
            base = fn;
            continue;
        }
        auto [tc, tv] = parse_cv();
        if (tc || tv) {
            base = wrap_cv(base, tc, tv);
            continue;
        }
        break;
    }

    return base;
}

std::vector<std::string> CppParser::ParseTemplateParams() {
    std::vector<std::string> params;
    if (!MatchSymbol("<"))
        return params;
    std::string current_param;
    int depth = 0;
    auto flush_param = [&]() {
        if (!current_param.empty()) {
            params.push_back(current_param);
            current_param.clear();
        }
    };
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsSymbol("<")) {
            depth++;
        } else if (IsSymbol(">")) {
            if (depth == 0) {
                flush_param();
                break;
            }
            depth = std::max(0, depth - 1);
        }
        if (depth == 0 && IsSymbol(",")) {
            flush_param();
            Consume();
            continue;
        }
        if (!current_param.empty())
            current_param += " ";
        current_param += current_.lexeme;
        Consume();
    }
    ExpectSymbol(">", "Expected '>' after template parameters");
    return params;
}

std::vector<std::shared_ptr<Expression>> CppParser::ParseTemplateArgs() {
    std::vector<std::shared_ptr<Expression>> args;
    if (!MatchSymbol("<"))
        return args;
    int depth = 1;
    while (current_.kind != frontends::TokenKind::kEndOfFile && depth > 0) {
        args.push_back(ParseAssignment());
        if (MatchSymbol(","))
            continue;
        if (MatchSymbol(">")) {
            depth--;
            break;
        }
        if (IsSymbol(">")) {
            MatchSymbol(">");
            depth--;
            break;
        }
        if (MatchSymbol("<")) {
            depth++;
            continue;
        }
    }
    return args;
}

std::string CppParser::ParseRequiresClause() {
    if (!(current_.kind == frontends::TokenKind::kKeyword &&
          current_.lexeme == "requires")) {
        return "";
    }
    Consume();
    std::string clause;
    int paren = 0;
    int bracket = 0;
    int angle = 0;
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (paren == 0 && bracket == 0 && angle == 0 &&
            (IsSymbol("{") || IsSymbol(";"))) {
            break;
        }
        if (IsSymbol("("))
            paren++;
        else if (IsSymbol(")"))
            paren = std::max(0, paren - 1);
        else if (IsSymbol("["))
            bracket++;
        else if (IsSymbol("]"))
            bracket = std::max(0, bracket - 1);
        else if (IsSymbol("<"))
            angle++;
        else if (IsSymbol(">"))
            angle = std::max(0, angle - 1);
        if (!clause.empty())
            clause += " ";
        clause += current_.lexeme;
        Consume();
    }
    return clause;
}

// --- Expressions ----------------------------------------------------
std::shared_ptr<Expression> CppParser::ParsePrimary() {
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto ident = std::make_shared<Identifier>();
        ident->name = current_.lexeme;
        ident->loc = current_.loc;
        Consume();
        return ident;
    }
    if (current_.kind == frontends::TokenKind::kNumber ||
        current_.kind == frontends::TokenKind::kString) {
        auto lit = std::make_shared<Literal>();
        lit->value = current_.lexeme;
        lit->loc = current_.loc;
        Consume();
        return lit;
    }
    if (IsSymbol("(")) {
        Consume();
        if (IsSymbol("...")) {
            auto fold = std::make_shared<FoldExpression>();
            fold->loc = current_.loc;
            fold->is_left_fold = false;
            fold->is_pack_fold = true;
            Consume();
            if (current_.kind == frontends::TokenKind::kSymbol) {
                fold->op = current_.lexeme;
                Consume();
            }
            fold->rhs = ParseExpression();
            ExpectSymbol(")", "Expected ')' to end fold expression");
            return fold;
        }
        auto first = ParseExpression();
        if (IsSymbol("...")) {
            auto fold = std::make_shared<FoldExpression>();
            fold->loc = first ? first->loc : current_.loc;
            fold->lhs = first;
            fold->is_left_fold = true;
            fold->is_pack_fold = true;
            Consume();
            if (current_.kind == frontends::TokenKind::kSymbol) {
                fold->op = current_.lexeme;
                Consume();
            }
            if (!IsSymbol(")")) {
                fold->rhs = ParseExpression();
            }
            ExpectSymbol(")", "Expected ')' to end fold expression");
            return fold;
        }
        if (current_.kind == frontends::TokenKind::kSymbol) {
            auto op_token = current_;
            auto la = lexer_.NextToken();
            pushback_.push_back(la);
            if (la.kind == frontends::TokenKind::kSymbol && la.lexeme == "...") {
                Consume(); // operator
                Consume(); // ellipsis
                auto fold = std::make_shared<FoldExpression>();
                fold->loc = first ? first->loc : op_token.loc;
                fold->lhs = first;
                fold->op = op_token.lexeme;
                fold->is_left_fold = true;
                fold->is_pack_fold = true;
                if (!IsSymbol(")")) {
                    fold->rhs = ParseExpression();
                }
                ExpectSymbol(")", "Expected ')' to end fold expression");
                return fold;
            }
            auto bin = std::make_shared<BinaryExpression>();
            bin->op = op_token.lexeme;
            bin->loc = first ? first->loc : op_token.loc;
            Consume();
            bin->left = first;
            bin->right = ParseExpression();
            MatchSymbol(")");
            return bin;
        }
        MatchSymbol(")");
        return first;
    }
    if (IsSymbol("[")) {
        return ParseLambda();
    }
    if (IsSymbol("{")) {
        auto init = std::make_shared<InitializerListExpression>();
        init->loc = current_.loc;
        Consume();
        if (!IsSymbol("}")) {
            init->elements.push_back(ParseAssignment());
            while (MatchSymbol(",")) {
                if (IsSymbol("}"))
                    break;
                init->elements.push_back(ParseAssignment());
            }
        }
        ExpectSymbol("}", "Expected '}' to close initializer list");
        return init;
    }
    diagnostics_.Report(current_.loc, "Expected expression");
    Consume();  // Consume the unexpected token to avoid infinite loops
    return nullptr;
}

std::shared_ptr<Expression> CppParser::ParsePostfix() {
    auto expr = ParsePrimary();
    while (true) {
        if (auto ident = std::dynamic_pointer_cast<Identifier>(expr)) {
            if (IsSymbol("<")) {
                auto args = ParseTemplateArgs();
                auto tid = std::make_shared<TemplateIdExpression>();
                tid->name = ident->name;
                tid->args = std::move(args);
                tid->loc = ident->loc;
                expr = tid;
                continue;
            }
        }
        if (IsSymbol("(")) {
            auto call = std::make_shared<CallExpression>();
            call->callee = expr;
            call->loc = expr ? expr->loc : current_.loc;
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
        }
        if (IsSymbol("->")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                auto mem = std::make_shared<MemberExpression>();
                mem->object = expr;
                mem->member = current_.lexeme;
                mem->is_arrow = true;
                mem->loc = current_.loc;
                Consume();
                expr = mem;
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
        if (IsSymbol("++") || IsSymbol("--")) {
            auto post = std::make_shared<UnaryExpression>();
            post->op = current_.lexeme + "_post";
            post->operand = expr;
            post->loc = current_.loc;
            Consume();
            expr = post;
            continue;
        }
        break;
    }
    return expr;
}

std::shared_ptr<Expression> CppParser::ParseLambda() {
    auto lam = std::make_shared<LambdaExpression>();
    lam->loc = current_.loc;
    MatchSymbol("[");
    if (!IsSymbol("]")) {
        while (true) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                lam->captures.push_back(current_.lexeme);
                Consume();
            }
            if (!MatchSymbol(","))
                break;
            if (IsSymbol("]"))
                break;
        }
    }
    ExpectSymbol("]", "Expected ']' after lambda capture list");
    ExpectSymbol("(", "Expected '(' for lambda parameters");
    if (!IsSymbol(")")) {
        while (true) {
            LambdaExpression::Param p;
            // C++ lambda params: type name (e.g., "int y")
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                p.type = ParseType();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    p.name = current_.lexeme;
                    Consume();
                }
                lam->params.push_back(std::move(p));
            }
            if (!MatchSymbol(","))
                break;
            if (IsSymbol(")"))
                break;
        }
    }
    ExpectSymbol(")", "Expected ')' after lambda parameters");
    if (MatchSymbol("->")) {
        lam->return_type = ParseType();
    }
    if (IsSymbol("{")) {
        auto body = std::dynamic_pointer_cast<CompoundStatement>(ParseBlock());
        if (body) {
            lam->body = body->statements;
        }
    } else {
        auto expr_body = ParseExpression();
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = expr_body;
        lam->body.push_back(expr_stmt);
    }
    return lam;
}

std::shared_ptr<Expression> CppParser::ParseUnary() {
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "co_await" || current_.lexeme == "co_yield")) {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = current_.lexeme;
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseUnary();
        return unary;
    }
    if (IsSymbol("+") || IsSymbol("-") || IsSymbol("!") || IsSymbol("~") || IsSymbol("&") ||
        IsSymbol("*") || IsSymbol("++") || IsSymbol("--")) {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = current_.lexeme;
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseUnary();
        return unary;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "sizeof") {
        auto sz = std::make_shared<SizeofExpression>();
        sz->loc = current_.loc;
        Consume();
        if (MatchSymbol("(")) {
            auto type = ParseType();
            if (MatchSymbol(")")) {
                sz->is_type = true;
                sz->type_arg = type;
                return sz;
            }
        }
        sz->expr_arg = ParseUnary();
        return sz;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "typeid") {
        auto ti = std::make_shared<TypeidExpression>();
        ti->loc = current_.loc;
        Consume();
        if (MatchSymbol("(")) {
            auto type = ParseType();
            if (MatchSymbol(")")) {
                ti->is_type = true;
                ti->type_arg = type;
                return ti;
            }
        }
        ti->expr_arg = ParseUnary();
        return ti;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "new") {
        auto nw = std::make_shared<NewExpression>();
        nw->loc = current_.loc;
        Consume();
        nw->type = ParseType();
        if (MatchSymbol("[")) {
            nw->is_array = true;
            nw->args.push_back(ParseExpression());
            ExpectSymbol("]", "Expected ']' after new[] size");
        }
        if (MatchSymbol("(")) {
            if (!IsSymbol(")")) {
                nw->args.push_back(ParseExpression());
                while (MatchSymbol(",")) {
                    nw->args.push_back(ParseExpression());
                }
            }
            ExpectSymbol(")", "Expected ')' after new arguments");
        }
        return nw;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "delete") {
        auto del = std::make_shared<DeleteExpression>();
        del->loc = current_.loc;
        Consume();
        if (MatchSymbol("[")) {
            MatchSymbol("]");
            del->is_array = true;
        }
        del->operand = ParseUnary();
        return del;
    }
    return ParsePostfix();
}

std::shared_ptr<Expression> CppParser::ParseMultiplicative() {
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

std::shared_ptr<Expression> CppParser::ParseAdditive() {
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

std::shared_ptr<Expression> CppParser::ParseShift() {
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

std::shared_ptr<Expression> CppParser::ParseRelational() {
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

std::shared_ptr<Expression> CppParser::ParseEquality() {
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

std::shared_ptr<Expression> CppParser::ParseBitwiseAnd() {
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

std::shared_ptr<Expression> CppParser::ParseBitwiseXor() {
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

std::shared_ptr<Expression> CppParser::ParseBitwiseOr() {
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

std::shared_ptr<Expression> CppParser::ParseLogicalAnd() {
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

std::shared_ptr<Expression> CppParser::ParseLogicalOr() {
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

std::shared_ptr<Expression> CppParser::ParseConditional() {
    auto expr = ParseLogicalOr();
    if (MatchSymbol("?")) {
        auto cond = std::make_shared<ConditionalExpression>();
        cond->condition = expr;
        cond->then_expr = ParseExpression();
        ExpectSymbol(":", "Expected ':' in conditional expression");
        cond->else_expr = ParseConditional();
        cond->loc = expr ? expr->loc : current_.loc;
        return cond;
    }
    return expr;
}

std::shared_ptr<Expression> CppParser::ParseAssignment() {
    auto expr = ParseConditional();
    if (IsSymbol("=") || IsSymbol("+=") || IsSymbol("-=") || IsSymbol("*=") ||
        IsSymbol("/=") || IsSymbol("%=") || IsSymbol("<<=") || IsSymbol(">>=") ||
        IsSymbol("&=") || IsSymbol("|=") || IsSymbol("^=")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = current_.lexeme;
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAssignment();
        return bin;
    }
    return expr;
}

std::shared_ptr<Expression> CppParser::ParseComma() {
    auto expr = ParseAssignment();
    while (IsSymbol(",")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = ",";
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAssignment();
        expr = bin;
    }
    return expr;
}

std::shared_ptr<Expression> CppParser::ParseExpression() { return ParseComma(); }

// --- Statements ----------------------------------------------------
std::shared_ptr<Statement> CppParser::ParseImport() {
    auto stmt = std::make_shared<ImportDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("import");
    if (IsSymbol("<")) {
        std::string header;
        Consume();
        while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
            header += current_.lexeme;
            Consume();
        }
        MatchSymbol(">");
        stmt->module = "<" + header + ">";
        stmt->is_header_unit = true;
    } else if (current_.kind == frontends::TokenKind::kString) {
        stmt->module = current_.lexeme;
        stmt->is_header_unit = true;
        Consume();
    } else {
        std::string name;
        while (!IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (!name.empty())
                name += " ";
            name += current_.lexeme;
            Consume();
        }
        stmt->module = name;
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseModuleDecl(bool is_export) {
    auto mod = std::make_shared<ModuleDeclaration>();
    mod->loc = current_.loc;
    mod->is_export = is_export;
    MatchKeyword("module");
    if (MatchSymbol(";")) {
        mod->is_global = true;
        return mod;
    }
    std::string name;
    while (!IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (!name.empty())
            name += " ";
        name += current_.lexeme;
        Consume();
    }
    mod->name = name;
    if (mod->name.find(':') != std::string::npos) {
        mod->is_partition = true;
    }
    MatchSymbol(";");
    return mod;
}

std::shared_ptr<Statement> CppParser::ParseConcept() {
    auto decl = std::make_shared<ConceptDecl>();
    decl->loc = current_.loc;
    MatchKeyword("concept");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        decl->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected concept name");
    }
    if (MatchSymbol("=")) {
        std::string constraint;
        while (!IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (!constraint.empty())
                constraint += " ";
            constraint += current_.lexeme;
            Consume();
        }
        decl->constraint = constraint;
    }
    MatchSymbol(";");
    return decl;
}

std::shared_ptr<Statement> CppParser::ParseUsing() {
    MatchKeyword("using");
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "enum") {
        return ParseUsingEnum();
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "namespace") {
        return ParseUsingNamespace();
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        std::string name = current_.lexeme;
        Consume();
        if (MatchSymbol("=")) {
            // "using Name = Type;" — produce UsingDeclaration for simple
            // aliases and UsingAliasDeclaration for template aliases.
            auto type = ParseType();
            std::string type_text;
            if (auto simple = std::dynamic_pointer_cast<SimpleType>(type)) {
                type_text = simple->name;
            }
            MatchSymbol(";");
            if (!type_text.empty()) {
                auto decl = std::make_shared<UsingDeclaration>();
                decl->loc = current_.loc;
                decl->name = name;
                decl->aliased = type_text;
                return decl;
            }
            auto alias = std::make_shared<UsingAliasDeclaration>();
            alias->loc = current_.loc;
            alias->alias = name;
            alias->aliased_type = type;
            return alias;
        }
        auto stmt = std::make_shared<UsingDeclaration>();
        stmt->loc = current_.loc;
        stmt->name = name;
        if (MatchSymbol("::")) {
            stmt->aliased = ParseQualifiedName();
        }
        MatchSymbol(";");
        return stmt;
    }
    diagnostics_.Report(current_.loc, "Expected identifier after using");
    MatchSymbol(";");
    return nullptr;
}

std::shared_ptr<Statement> CppParser::ParseUsingEnum() {
    auto stmt = std::make_shared<UsingEnumDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("enum");
    stmt->name = ParseQualifiedName();
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseUsingNamespace() {
    auto stmt = std::make_shared<UsingNamespaceDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("namespace");
    stmt->ns = ParseQualifiedName();
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseNamespaceAlias() {
    auto alias = std::make_shared<NamespaceAliasDeclaration>();
    alias->loc = current_.loc;
    MatchKeyword("namespace");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        alias->alias = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected namespace alias name");
    }
    ExpectSymbol("=", "Expected '=' in namespace alias");
    alias->target = ParseQualifiedName();
    MatchSymbol(";");
    return alias;
}

std::shared_ptr<Statement> CppParser::ParseUsingAlias() {
    auto alias = std::make_shared<UsingAliasDeclaration>();
    alias->loc = current_.loc;
    alias->aliased_type = ParseType();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        alias->alias = current_.lexeme;
        Consume();
    }
    MatchSymbol(";");
    return alias;
}

std::shared_ptr<Statement> CppParser::ParseFriend() {
    MatchKeyword("friend");
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "class" || current_.lexeme == "struct")) {
        std::string kind = current_.lexeme;
        Consume();
        auto fwd = std::make_shared<ForwardDecl>();
        fwd->loc = current_.loc;
        fwd->kind = kind;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            fwd->name = current_.lexeme;
            Consume();
        }
        MatchSymbol(";");
        auto fr = std::make_shared<FriendDecl>();
        fr->loc = fwd->loc;
        fr->decl = fwd;
        return fr;
    }
    auto ret = ParseType();
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "Expected friend function name");
        MatchSymbol(";");
        return nullptr;
    }
    std::string name = current_.lexeme;
    Consume();
    auto fn = ParseFunctionWithSignature(ret, name, false, false, false, false, false, "", "",
                                         true);
    if (fn)
        fn->is_friend = true;
    return fn;
}

std::shared_ptr<Statement> CppParser::ParseTypedef() {
    auto td = std::make_shared<TypedefDeclaration>();
    td->loc = current_.loc;
    MatchKeyword("typedef");
    td->type = ParseType();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        td->alias = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected identifier in typedef");
    }
    MatchSymbol(";");
    return td;
}

std::shared_ptr<Statement> CppParser::ParseReturn() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = current_.loc;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "co_return") {
        stmt->is_co_return = true;
        Consume();
    } else {
        MatchKeyword("return");
    }
    if (!IsSymbol(";")) {
        stmt->value = ParseExpression();
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseThrow() {
    auto stmt = std::make_shared<ThrowStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("throw");
    if (!IsSymbol(";")) {
        stmt->value = ParseExpression();
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseSwitch() {
    auto sw = std::make_shared<SwitchStatement>();
    sw->loc = current_.loc;
    MatchKeyword("switch");
    ExpectSymbol("(", "Expected '(' after switch");
    sw->condition = ParseExpression();
    ExpectSymbol(")", "Expected ')' after switch condition");
    ExpectSymbol("{", "Expected '{' after switch");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        SwitchCase cs;
        bool has_label = false;
        while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "case") {
            Consume();
            cs.labels.push_back(ParseExpression());
            has_label = true;
            ExpectSymbol(":", "Expected ':' after case label");
        }
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "default") {
            Consume();
            cs.is_default = true;
            ExpectSymbol(":", "Expected ':' after default");
            has_label = true;
        }
        if (!has_label)
            break;
        while (!(IsSymbol("}") || (current_.kind == frontends::TokenKind::kKeyword &&
                                   (current_.lexeme == "case" || current_.lexeme == "default"))) &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            auto before_line = current_.loc.line;
            auto before_col  = current_.loc.column;
            cs.body.push_back(ParseStatement());
            if (current_.loc.line == before_line &&
                current_.loc.column == before_col) {
                Consume();
            }
        }
        sw->cases.push_back(std::move(cs));
    }
    MatchSymbol("}");
    return sw;
}

std::shared_ptr<Statement> CppParser::ParseTry() {
    auto tr = std::make_shared<TryStatement>();
    tr->loc = current_.loc;
    MatchKeyword("try");
    tr->try_body.push_back(ParseBlock());
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "catch") {
        Consume();
        CatchClause cc;
        ExpectSymbol("(", "Expected '(' after catch");
        cc.exception_type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            cc.name = current_.lexeme;
            Consume();
        }
        ExpectSymbol(")", "Expected ')' after catch param");
        auto body = std::dynamic_pointer_cast<CompoundStatement>(ParseBlock());
        if (body)
            cc.body = body->statements;
        tr->catches.push_back(std::move(cc));
    }
    return tr;
}

std::shared_ptr<Statement> CppParser::ParseRecord(const std::string &kind) {
    auto rec = std::make_shared<RecordDecl>();
    rec->loc = current_.loc;
    rec->attributes = ParseAttributes();
    rec->kind = kind;
    MatchKeyword(kind);
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        rec->name = current_.lexeme;
        Consume();
    }
    if (MatchSymbol(";")) {
        rec->is_forward = true;
        return rec;
    }
    if (MatchSymbol(":")) {
        do {
            BaseSpecifier base;
            if (current_.kind == frontends::TokenKind::kKeyword &&
                current_.lexeme == "virtual") {
                base.is_virtual = true;
                Consume();
            }
            if (current_.kind == frontends::TokenKind::kKeyword &&
                (current_.lexeme == "public" || current_.lexeme == "protected" ||
                 current_.lexeme == "private")) {
                base.access = current_.lexeme;
                Consume();
            }
            if (current_.kind == frontends::TokenKind::kKeyword &&
                current_.lexeme == "virtual") {
                base.is_virtual = true;
                Consume();
            }
            base.name = ParseQualifiedName();
            rec->bases.push_back(std::move(base));
        } while (MatchSymbol(","));
    }

    ExpectSymbol("{", "Expected '{' for record body");
    if (IsSymbol("{"))
        MatchSymbol("{");
    std::string current_access = (kind == "struct") ? "public" : "private";
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto before_line = current_.loc.line;
        auto before_col  = current_.loc.column;

        if (current_.kind == frontends::TokenKind::kKeyword &&
            (current_.lexeme == "public" || current_.lexeme == "protected" ||
             current_.lexeme == "private")) {
            std::string acc = current_.lexeme;
            Consume();
            MatchSymbol(":");
            current_access = acc;
            continue;
        }

        auto attrs = ParseAttributes();

        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "friend") {
            auto fr = ParseFriend();
            if (fr)
                rec->methods.push_back(fr);
            continue;
        }

        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
            rec->methods.push_back(ParseUsing());
            continue;
        }
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "typedef") {
            rec->methods.push_back(ParseTypedef());
            continue;
        }

        bool is_constexpr = false, is_consteval = false, is_inline = false, is_static = false,
             is_virtual = false, is_mutable = false;
        while (current_.kind == frontends::TokenKind::kKeyword &&
               (current_.lexeme == "constexpr" || current_.lexeme == "consteval" ||
                current_.lexeme == "inline" || current_.lexeme == "static" ||
                current_.lexeme == "virtual" || current_.lexeme == "mutable")) {
            if (current_.lexeme == "constexpr")
                is_constexpr = true;
            if (current_.lexeme == "consteval")
                is_consteval = true;
            if (current_.lexeme == "inline")
                is_inline = true;
            if (current_.lexeme == "static")
                is_static = true;
            if (current_.lexeme == "virtual")
                is_virtual = true;
            if (current_.lexeme == "mutable")
                is_mutable = true;
            Consume();
        }

        if (IsSymbol("~")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier && current_.lexeme == rec->name) {
                Consume();
                auto fn = ParseFunctionWithSignature(nullptr, "~" + rec->name, is_constexpr,
                                                     is_consteval, is_inline, is_static, false,
                                                     "", current_access, true);
                if (fn) {
                    fn->is_destructor = true;
                    fn->is_virtual = is_virtual;
                    fn->attributes = attrs;
                    rec->methods.push_back(fn);
                }
                continue;
            }
        }

        // Detect constructor: identifier matching the struct name followed by '('
        if (current_.kind == frontends::TokenKind::kIdentifier &&
            current_.lexeme == rec->name && !is_static) {
            frontends::Token saved = current_;
            Consume();
            if (IsSymbol("(")) {
                auto fn = ParseFunctionWithSignature(nullptr, saved.lexeme, is_constexpr,
                                                     is_consteval, is_inline, is_static, false,
                                                     "", current_access, true);
                if (fn) {
                    fn->is_constructor = true;
                    fn->is_virtual = is_virtual;
                    fn->attributes = attrs;
                    rec->methods.push_back(fn);
                }
                continue;
            }
            // Not a constructor — push back and fall through to normal member
            pushback_.push_back(current_);
            current_ = saved;
        }

        auto member_type = ParseType();
        bool is_operator = false;
        std::string name;
        std::string op_symbol;
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "operator") {
            is_operator = true;
            Consume();
            op_symbol = current_.lexeme;
            name = "operator" + op_symbol;
            Consume();
        } else if (current_.kind == frontends::TokenKind::kIdentifier) {
            name = current_.lexeme;
            Consume();
        }

        if (IsSymbol("(")) {
            auto fn = ParseFunctionWithSignature(member_type, name,
                                                 is_constexpr, is_consteval, is_inline, is_static,
                                                 is_operator, op_symbol, current_access, true);
            if (fn) {
                fn->is_virtual = is_virtual;
                fn->attributes = attrs;
                rec->methods.push_back(fn);
            }
            continue;
        }

        FieldDecl f;
        f.type = member_type;
        f.name = name;
        f.access = current_access;
        f.is_constexpr = is_constexpr;
        f.is_static = is_static;
        f.is_mutable = is_mutable;
        f.attributes = attrs;
        MatchSymbol(";");
        rec->fields.push_back(std::move(f));

        // If no progress was made, force-consume to prevent infinite loop
        if (current_.loc.line == before_line &&
            current_.loc.column == before_col) {
            Consume();
        }
    }
    MatchSymbol("}");
    MatchSymbol(";");
    return rec;
}

std::shared_ptr<Statement> CppParser::ParseEnum() {
    auto en = std::make_shared<EnumDecl>();
    en->loc = current_.loc;
    MatchKeyword("enum");
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "class" || current_.lexeme == "struct")) {
        en->is_scoped = true;
        Consume();
    }
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        en->name = current_.lexeme;
        Consume();
    }
    if (MatchSymbol(":")) {
        en->underlying_type = ParseType();
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                en->enumerators.push_back(current_.lexeme);
                Consume();
            }
            if (!MatchSymbol(","))
                break;
        }
        MatchSymbol("}");
    } else {
        en->is_forward = true;
    }
    MatchSymbol(";");
    return en;
}

std::shared_ptr<Statement> CppParser::ParseNamespace() {
    auto ns = std::make_shared<NamespaceDecl>();
    ns->loc = current_.loc;
    MatchKeyword("namespace");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        ns->name = current_.lexeme;
        Consume();
    }
    if (MatchSymbol("=")) {
        // namespace alias
        auto alias = std::make_shared<NamespaceAliasDeclaration>();
        alias->loc = ns->loc;
        alias->alias = ns->name;
        alias->target = ParseQualifiedName();
        MatchSymbol(";");
        return alias;
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto before_line = current_.loc.line;
            auto before_col  = current_.loc.column;
            auto stmt = ParseStatement();
            if (stmt)
                ns->members.push_back(stmt);
            // If no progress was made, force-consume to prevent infinite loop
            if (current_.loc.line == before_line &&
                current_.loc.column == before_col) {
                Consume();
            }
        }
        MatchSymbol("}");
    } else {
        MatchSymbol(";");
    }
    return ns;
}

std::shared_ptr<Statement> CppParser::ParseTemplate() {
    auto tmpl = std::make_shared<TemplateDecl>();
    tmpl->loc = current_.loc;
    MatchKeyword("template");
    tmpl->params = ParseTemplateParams();
    tmpl->requires_clause = ParseRequiresClause();
    tmpl->inner = ParseStatement();
    if (auto concept_ = std::dynamic_pointer_cast<ConceptDecl>(tmpl->inner)) {
        concept_->params = tmpl->params;
    }
    return tmpl;
}

std::shared_ptr<Statement> CppParser::ParseVarDecl(std::shared_ptr<TypeNode> type,
                                                   const std::string &name, bool is_constexpr,
                                                   bool is_inline, bool is_static,
                                                   bool is_constinit,
                                                   const std::string &access) {
    auto decl = std::make_shared<VarDecl>();
    decl->loc = current_.loc;
    decl->type = type;
    decl->name = name;
    decl->is_constexpr = is_constexpr;
    decl->is_constinit = is_constinit;
    decl->is_inline = is_inline;
    decl->is_static = is_static;
    decl->access = access;
    if (IsSymbol("=")) {
        Consume();
        decl->init = ParseExpression();
    }
    MatchSymbol(";");
    return decl;
}

std::shared_ptr<Statement> CppParser::ParseStructuredBinding(std::shared_ptr<TypeNode> type,
                                                             bool is_constexpr, bool is_inline,
                                                             bool is_static) {
    auto decl = std::make_shared<StructuredBindingDecl>();
    decl->loc = current_.loc;
    decl->type = type;
    ExpectSymbol("[", "Expected '[' in structured binding");
    while (current_.kind == frontends::TokenKind::kIdentifier) {
        decl->names.push_back(current_.lexeme);
        Consume();
        if (!MatchSymbol(","))
            break;
    }
    ExpectSymbol("]", "Expected ']' in structured binding");
    ExpectSymbol("=", "Expected '=' in structured binding");
    decl->init = ParseExpression();
    MatchSymbol(";");
    return decl;
}

std::shared_ptr<FunctionDecl>
CppParser::ParseFunctionWithSignature(std::shared_ptr<TypeNode> ret_type, const std::string &name,
                                      bool is_constexpr, bool is_consteval, bool is_inline,
                                      bool is_static,
                                      bool is_operator, const std::string &op_symbol,
                                      const std::string &access, bool inside_record) {
    auto fn = std::make_shared<FunctionDecl>();
    fn->loc = current_.loc;
    fn->return_type = ret_type;
    fn->name = name;
    fn->is_constexpr = is_constexpr;
    fn->is_consteval = is_consteval;
    fn->is_inline = is_inline;
    fn->is_static = is_static;
    fn->access = access;
    fn->is_operator = is_operator;
    fn->operator_symbol = op_symbol;
    (void)inside_record;
    ExpectSymbol("(", "Expected '(' after function name");
    if (!IsSymbol(")")) {
        while (true) {
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                auto param_type = ParseType();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    FunctionDecl::Param param;
                    param.name = current_.lexeme;
                    Consume();
                    if (MatchSymbol("=")) {
                        param.default_value = ParseExpression();
                    }
                    param.type = param_type;
                    fn->params.push_back(std::move(param));
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name");
                }
            }
            if (!MatchSymbol(","))
                break;
        }
    }
    ExpectSymbol(")", "Expected ')' after parameters");

    while (current_.kind == frontends::TokenKind::kKeyword &&
           (current_.lexeme == "const" || current_.lexeme == "noexcept" ||
            current_.lexeme == "override")) {
        if (current_.lexeme == "const") {
            fn->is_const_qualified = true;
            Consume();
            continue;
        }
        if (current_.lexeme == "noexcept") {
            fn->is_noexcept = true;
            Consume();
            if (MatchSymbol("(")) {
                fn->noexcept_expr = ParseExpression();
                ExpectSymbol(")", "Expected ')' after noexcept");
            }
            continue;
        }
        if (current_.lexeme == "override") {
            fn->is_override = true;
            Consume();
            continue;
        }
    }

    fn->requires_clause = ParseRequiresClause();

    if (MatchSymbol("=")) {
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "default") {
            fn->is_defaulted = true;
            Consume();
            MatchSymbol(";");
            return fn;
        }
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "delete") {
            fn->is_deleted = true;
            Consume();
            MatchSymbol(";");
            return fn;
        }
        if (current_.kind == frontends::TokenKind::kNumber && current_.lexeme == "0") {
            fn->is_pure_virtual = true;
            Consume();
            MatchSymbol(";");
            return fn;
        }
    }

    if (MatchSymbol(";")) {
        return fn;
    }
    ExpectSymbol("{", "Expected '{' to start function body");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto before_line = current_.loc.line;
        auto before_col  = current_.loc.column;
        fn->body.push_back(ParseStatement());
        // If no progress was made, force-consume to prevent infinite loop
        if (current_.loc.line == before_line &&
            current_.loc.column == before_col) {
            Consume();
        }
    }
    MatchSymbol("}");
    return fn;
}

std::shared_ptr<Statement> CppParser::ParseFunction() {
    auto ret = ParseType();
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "Expected function name");
        return nullptr;
    }
    std::string name = current_.lexeme;
    Consume();
    return ParseFunctionWithSignature(ret, name, false, false, false, false, false, "", "",
                                      false);
}

std::shared_ptr<Statement> CppParser::ParseBlock() {
    auto block = std::make_shared<CompoundStatement>();
    MatchSymbol("{");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto before_line = current_.loc.line;
        auto before_col  = current_.loc.column;
        block->statements.push_back(ParseStatement());
        // If no progress was made, force-consume to prevent infinite loop
        if (current_.loc.line == before_line &&
            current_.loc.column == before_col) {
            Consume();
        }
    }
    MatchSymbol("}");
    return block;
}

std::shared_ptr<Statement> CppParser::ParseIf() {
    auto stmt = std::make_shared<IfStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("if");
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "constexpr") {
        stmt->is_constexpr = true;
        Consume();
    }
    ExpectSymbol("(", "Expected '(' after if");
    stmt->condition = ParseExpression();
    ExpectSymbol(")", "Expected ')' after condition");
    stmt->then_body.push_back(ParseBlock());
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "else") {
        Consume();
        stmt->else_body.push_back(ParseBlock());
    }
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseWhile() {
    auto stmt = std::make_shared<WhileStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("while");
    ExpectSymbol("(", "Expected '(' after while");
    stmt->condition = ParseExpression();
    ExpectSymbol(")", "Expected ')' after condition");
    stmt->body.push_back(ParseBlock());
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseFor() {
    MatchKeyword("for");
    ExpectSymbol("(", "Expected '(' after for");

    auto attrs = ParseAttributes();

    bool looks_decl = (current_.kind == frontends::TokenKind::kKeyword);
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto la = lexer_.NextToken();
        pushback_.push_back(la);
        if (la.kind == frontends::TokenKind::kIdentifier ||
            (la.kind == frontends::TokenKind::kSymbol && la.lexeme == "::") ||
            (la.kind == frontends::TokenKind::kSymbol && la.lexeme == "*")) {
            looks_decl = true;
        }
    }

    if (looks_decl && !IsSymbol(";")) {
        auto type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            std::string var_name = current_.lexeme;
            auto var_loc = current_.loc;
            Consume();
            if (MatchSymbol(":")) {
                auto rng = std::make_shared<RangeForStatement>();
                auto loop = std::make_shared<VarDecl>();
                loop->loc = var_loc;
                loop->type = type;
                loop->name = var_name;
                loop->is_constexpr = false;
                loop->is_inline = false;
                loop->is_static = false;
                loop->attributes = attrs;
                rng->loop_var = loop;
                rng->range = ParseExpression();
                ExpectSymbol(")", "Expected ')' after range-for");
                auto body = std::dynamic_pointer_cast<CompoundStatement>(ParseBlock());
                if (body)
                    rng->body = body->statements;
                return rng;
            }

            auto stmt = std::make_shared<ForStatement>();
            stmt->loc = current_.loc;
            auto init_decl = std::make_shared<VarDecl>();
            init_decl->loc = var_loc;
            init_decl->type = type;
            init_decl->name = var_name;
            init_decl->is_constexpr = false;
            init_decl->is_inline = false;
            init_decl->is_static = false;
            init_decl->attributes = attrs;
            if (IsSymbol("=")) {
                Consume();
                init_decl->init = ParseExpression();
            }
            MatchSymbol(";");
            stmt->init = init_decl;
            if (!IsSymbol(";")) {
                stmt->condition = ParseExpression();
            }
            MatchSymbol(";");
            if (!IsSymbol(")")) {
                stmt->increment = ParseExpression();
            }
            ExpectSymbol(")", "Expected ')' after for clauses");
            stmt->body.push_back(ParseBlock());
            return stmt;
        }
    }

    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = current_.loc;
    stmt->init = nullptr;
    if (!IsSymbol(";")) {
        stmt->init = std::make_shared<ExprStatement>();
        stmt->init->loc = current_.loc;
        std::dynamic_pointer_cast<ExprStatement>(stmt->init)->expr = ParseExpression();
    }
    MatchSymbol(";");
    if (!IsSymbol(";")) {
        stmt->condition = ParseExpression();
    }
    MatchSymbol(";");
    if (!IsSymbol(")")) {
        stmt->increment = ParseExpression();
    }
    ExpectSymbol(")", "Expected ')' after for clauses");
    stmt->body.push_back(ParseBlock());
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseStatement() {
    auto leading_attrs = ParseAttributes();
    bool is_export = false;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "export") {
        is_export = true;
        Consume();
    }
    bool is_constexpr = false, is_consteval = false, is_constinit = false;
    bool is_inline = false, is_static = false;
    while (current_.kind == frontends::TokenKind::kKeyword &&
           (current_.lexeme == "constexpr" || current_.lexeme == "consteval" ||
            current_.lexeme == "constinit" || current_.lexeme == "inline" ||
            current_.lexeme == "static")) {
        if (current_.lexeme == "constexpr")
            is_constexpr = true;
        if (current_.lexeme == "consteval")
            is_consteval = true;
        if (current_.lexeme == "constinit")
            is_constinit = true;
        if (current_.lexeme == "inline")
            is_inline = true;
        if (current_.lexeme == "static")
            is_static = true;
        Consume();
    }

    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "module") {
            return ParseModuleDecl(is_export);
        }
        if (current_.lexeme == "import") {
            auto imp = ParseImport();
            if (auto decl = std::dynamic_pointer_cast<ImportDeclaration>(imp)) {
                decl->is_export = is_export;
            }
            return imp;
        }
        if (current_.lexeme == "concept") {
            return ParseConcept();
        }
        if (current_.lexeme == "using") {
            return ParseUsing();
        }
        if (current_.lexeme == "typedef") {
            return ParseTypedef();
        }
        if (current_.lexeme == "friend") {
            return ParseFriend();
        }
        if (current_.lexeme == "throw") {
            return ParseThrow();
        }
        if (current_.lexeme == "switch") {
            return ParseSwitch();
        }
        if (current_.lexeme == "try") {
            return ParseTry();
        }
        if (current_.lexeme == "return" || current_.lexeme == "co_return") {
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
        if (current_.lexeme == "class" || current_.lexeme == "struct") {
            std::string kind = current_.lexeme;
            auto rec = ParseRecord(kind);
            if (auto decl = std::dynamic_pointer_cast<RecordDecl>(rec)) {
                decl->is_export = is_export;
            }
            return rec;
        }
        if (current_.lexeme == "enum") {
            auto en = ParseEnum();
            if (auto decl = std::dynamic_pointer_cast<EnumDecl>(en)) {
                decl->is_export = is_export;
            }
            return en;
        }
        if (current_.lexeme == "namespace") {
            return ParseNamespace();
        }
        if (current_.lexeme == "template") {
            return ParseTemplate();
        }
        if (current_.lexeme == "int" || current_.lexeme == "void" || current_.lexeme == "float" ||
            current_.lexeme == "double" || current_.lexeme == "char" || current_.lexeme == "bool" ||
            current_.lexeme == "auto" || current_.lexeme == "long" || current_.lexeme == "short" ||
            current_.lexeme == "signed" || current_.lexeme == "unsigned" ||
            current_.lexeme == "decltype" || current_.lexeme == "typename") {
            auto type = ParseType();
            if (IsSymbol("[")) {
                return ParseStructuredBinding(type, is_constexpr, is_inline, is_static);
            }
            if (current_.kind != frontends::TokenKind::kIdentifier &&
                !(current_.kind == frontends::TokenKind::kKeyword &&
                  current_.lexeme == "operator")) {
                diagnostics_.Report(current_.loc, "Expected identifier after type");
                MatchSymbol(";");
                return nullptr;
            }
            std::string name;
            bool is_operator = false;
            std::string op_symbol;
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "operator") {
                is_operator = true;
                Consume();
                op_symbol = current_.lexeme;
                name = "operator" + op_symbol;
                Consume();
            } else {
                name = current_.lexeme;
                Consume();
            }
            if (IsSymbol("(")) {
                auto fn = ParseFunctionWithSignature(type, name, is_constexpr, is_consteval,
                                                     is_inline, is_static, is_operator,
                                                     op_symbol, "", false);
                if (fn) {
                    fn->attributes = leading_attrs;
                    fn->is_export = is_export;
                }
                return fn;
            }
            auto vd = ParseVarDecl(type, name, is_constexpr, is_inline, is_static, is_constinit);
            if (auto var = std::dynamic_pointer_cast<VarDecl>(vd)) {
                var->attributes = leading_attrs;
            }
            return vd;
        }
    }
    if (IsSymbol("{")) {
        return ParseBlock();
    }

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto type = ParseType();
        if (IsSymbol("[")) {
            return ParseStructuredBinding(type, is_constexpr, is_inline, is_static);
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            std::string name = current_.lexeme;
            Consume();
            if (IsSymbol("(")) {
                auto fn = ParseFunctionWithSignature(type, name, is_constexpr, is_consteval,
                                                     is_inline, is_static, false, "", "", false);
                if (fn) {
                    fn->attributes = leading_attrs;
                    fn->is_export = is_export;
                }
                return fn;
            }
            auto vd = ParseVarDecl(type, name, is_constexpr, is_inline, is_static, is_constinit);
            if (auto var = std::dynamic_pointer_cast<VarDecl>(vd)) {
                var->attributes = leading_attrs;
            }
            return vd;
        }
    }

    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->loc = current_.loc;
    expr_stmt->expr = ParseExpression();
    MatchSymbol(";");
    return expr_stmt;
}

void CppParser::ParseTopLevel() {
    if (current_.kind == frontends::TokenKind::kEndOfFile) {
        return;
    }
    auto stmt = ParseStatement();
    if (stmt) {
        module_->declarations.push_back(stmt);
    } else {
        Sync();
    }
}

void CppParser::ParseModule() {
    Consume();
    for (;;) {
        // Safety: record position before parsing so we can detect lack
        // of progress and avoid an infinite loop on malformed input.
        auto before_line = current_.loc.line;
        auto before_col  = current_.loc.column;
        ParseTopLevel();
        if (current_.kind == frontends::TokenKind::kEndOfFile) {
            break;
        }
        // If no progress was made (same position), force-consume to
        // prevent an infinite loop.
        if (current_.loc.line == before_line &&
            current_.loc.column == before_col) {
            Consume();
        }
    }
}

std::shared_ptr<Module> CppParser::TakeModule() { return module_; }

} // namespace polyglot::cpp
