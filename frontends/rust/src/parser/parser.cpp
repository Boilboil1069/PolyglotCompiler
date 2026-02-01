#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

frontends::Token RustParser::NextNonComment() {
    frontends::Token tok = lexer_.NextToken();
    while (tok.kind == frontends::TokenKind::kComment) {
        tok = lexer_.NextToken();
    }
    return tok;
}

frontends::Token RustParser::Consume() {
    if (!pushback_.empty()) {
        current_ = pushback_.back();
        pushback_.pop_back();
    } else {
        current_ = NextNonComment();
    }
    return current_;
}

frontends::Token RustParser::PeekToken() {
    if (pushback_.empty()) {
        pushback_.push_back(NextNonComment());
    }
    return pushback_.back();
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

std::vector<Attribute> RustParser::ParseAttributes() {
    std::vector<Attribute> attrs;
    while (IsSymbol("#")) {
        Consume();
        bool is_inner = false;
        if (MatchSymbol("!")) {
            is_inner = true;
        }
        if (!IsSymbol("[")) {
            diagnostics_.Report(current_.loc, "Expected '[' after '#'");
            break;
        }
        Attribute attr;
        attr.is_inner = is_inner;
        attr.text = ParseDelimitedBody("[", "]");
        attrs.push_back(std::move(attr));
    }
    return attrs;
}

std::string RustParser::ParseVisibility() {
    if (!(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "pub")) {
        return "";
    }
    std::string vis = "pub";
    Consume();
    if (MatchSymbol("(")) {
        std::string scope;
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            scope += current_.lexeme;
            Consume();
        }
        ExpectSymbol(")", "Expected ')' after visibility scope");
        vis += "(" + scope + ")";
    }
    return vis;
}

std::string RustParser::ParseWhereClause() {
    if (!(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "where")) {
        return "";
    }
    Consume();
    std::string clause;
    int depth = 0;
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (depth == 0 && (IsSymbol("{") || IsSymbol(";"))) {
            break;
        }
        if (IsSymbol("<")) {
            depth++;
        } else if (IsSymbol(">")) {
            depth = std::max(0, depth - 1);
        }
        clause += current_.lexeme;
        Consume();
    }
    return clause;
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
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        (current_.kind == frontends::TokenKind::kKeyword &&
         (current_.lexeme == "self" || current_.lexeme == "Self" || current_.lexeme == "super" ||
          current_.lexeme == "crate"))) {
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
    auto is_path_segment = [&]() {
        if (current_.kind == frontends::TokenKind::kIdentifier)
            return true;
        if (current_.kind == frontends::TokenKind::kKeyword) {
            return current_.lexeme == "self" || current_.lexeme == "Self" ||
                   current_.lexeme == "super" || current_.lexeme == "crate";
        }
        return false;
    };
    while (is_path_segment()) {
        path->segments.push_back(current_.lexeme);
        path->generic_args.emplace_back();
        Consume();
        if (IsSymbol("::")) {
            Consume();
            if (IsSymbol("<")) {
                path->generic_args.back() = ParseGenericArgList();
                if (IsSymbol("::")) {
                    Consume();
                    continue;
                }
                break;
            }
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
    auto parse_single = [&]() -> std::shared_ptr<Pattern> {
        if (IsSymbol("..") || IsSymbol("..=") || IsSymbol("...")) {
            bool inclusive = current_.lexeme != "..";
            auto range = std::make_shared<RangePattern>();
            range->loc = current_.loc;
            range->inclusive = inclusive;
            Consume();
            if (!IsSymbol(",") && !IsSymbol(")") && !IsSymbol("]")) {
                range->end = ParsePattern();
            }
            return range;
        }
        if (IsSymbol("&")) {
            auto ref = std::make_shared<RefPattern>();
            ref->loc = current_.loc;
            Consume();
            if (MatchKeyword("mut")) {
                ref->is_mut = true;
            }
            ref->inner = ParsePattern();
            return ref;
        }
        if (IsSymbol("[")) {
            auto slice = std::make_shared<SlicePattern>();
            slice->loc = current_.loc;
            Consume();
            if (!IsSymbol("]")) {
                while (true) {
                    if (IsSymbol("..")) {
                        Consume();
                        slice->has_rest = true;
                        if (IsSymbol("]"))
                            break;
                    } else {
                        slice->elements.push_back(ParsePattern());
                    }
                    if (!MatchSymbol(","))
                        break;
                    if (IsSymbol("]"))
                        break;
                }
            }
            MatchSymbol("]");
            return slice;
        }
        bool is_ref = false;
        bool is_mut = false;
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "ref") {
            is_ref = true;
            Consume();
        }
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "mut") {
            is_mut = true;
            Consume();
        }
        if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == "_") {
            auto pat = std::make_shared<WildcardPattern>();
            pat->loc = current_.loc;
            Consume();
            return pat;
        }
        if (current_.kind == frontends::TokenKind::kNumber ||
            current_.kind == frontends::TokenKind::kString ||
            current_.kind == frontends::TokenKind::kChar ||
            (current_.kind == frontends::TokenKind::kKeyword &&
             (current_.lexeme == "true" || current_.lexeme == "false"))) {
            auto lit = std::make_shared<LiteralPattern>();
            lit->loc = current_.loc;
            lit->value = current_.lexeme;
            Consume();
            return lit;
        }
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            (current_.kind == frontends::TokenKind::kKeyword &&
             (current_.lexeme == "self" || current_.lexeme == "Self" ||
              current_.lexeme == "super" || current_.lexeme == "crate"))) {
            auto save_loc = current_.loc;
            auto path = PathPattern{};
            while (current_.kind == frontends::TokenKind::kIdentifier ||
                   (current_.kind == frontends::TokenKind::kKeyword &&
                    (current_.lexeme == "self" || current_.lexeme == "Self" ||
                     current_.lexeme == "super" || current_.lexeme == "crate"))) {
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
            if (IsSymbol("(")) {
                return ParseTupleStructPattern(path);
            }
            if (path.segments.size() == 1) {
                if (MatchSymbol("@")) {
                    auto binding = std::make_shared<BindingPattern>();
                    binding->loc = save_loc;
                    binding->name = path.segments[0];
                    binding->is_ref = is_ref;
                    binding->is_mut = is_mut;
                    binding->pattern = ParsePattern();
                    return binding;
                }
                auto id = std::make_shared<IdentifierPattern>();
                id->loc = save_loc;
                id->name = path.segments[0];
                id->is_ref = is_ref;
                id->is_mut = is_mut;
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
    };

    auto first = parse_single();
    if (!first)
        return nullptr;
    if (IsSymbol("..") || IsSymbol("..=") || IsSymbol("...")) {
        bool inclusive = current_.lexeme != "..";
        auto range = std::make_shared<RangePattern>();
        range->loc = first->loc;
        range->start = first;
        range->inclusive = inclusive;
        Consume();
        if (!IsSymbol(",") && !IsSymbol(")") && !IsSymbol("]")) {
            range->end = parse_single();
        }
        return range;
    }
    if (MatchSymbol("|")) {
        auto orp = std::make_shared<OrPattern>();
        orp->loc = first->loc;
        orp->patterns.push_back(first);
        do {
            orp->patterns.push_back(parse_single());
        } while (MatchSymbol("|"));
        return orp;
    }
    return first;
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
            StructPatternField field;
            field.name = current_.lexeme;
            Consume();
            if (MatchSymbol(":")) {
                field.pattern = ParsePattern();
                field.is_shorthand = false;
            } else {
                field.is_shorthand = true;
            }
            sp->fields.push_back(std::move(field));
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

std::shared_ptr<Pattern> RustParser::ParseTupleStructPattern(PathPattern path) {
    auto tp = std::make_shared<TupleStructPattern>();
    tp->loc = current_.loc;
    tp->path = std::move(path);
    MatchSymbol("(");
    if (!IsSymbol(")")) {
        tp->elements.push_back(ParsePattern());
        while (MatchSymbol(",")) {
            if (IsSymbol(")"))
                break;
            tp->elements.push_back(ParsePattern());
        }
    }
    MatchSymbol(")");
    return tp;
}

std::vector<std::shared_ptr<TypeNode>> RustParser::ParseGenericArgList() {
    std::vector<std::shared_ptr<TypeNode>> args;
    if (!MatchSymbol("<"))
        return args;
    if (!IsSymbol(">")) {
        auto parse_arg = [&]() -> std::shared_ptr<TypeNode> {
            if (current_.kind == frontends::TokenKind::kLifetime) {
                return ParseLifetime();
            }
            if (current_.kind == frontends::TokenKind::kNumber) {
                auto ce = std::make_shared<ConstExprType>();
                ce->loc = current_.loc;
                ce->expr = current_.lexeme;
                Consume();
                return ce;
            }
            return ParseType();
        };
        args.push_back(parse_arg());
        while (MatchSymbol(",")) {
            if (IsSymbol(">"))
                break;
            args.push_back(parse_arg());
        }
    }
    ExpectSymbol(">", "Expected '>' to close generic arguments");
    return args;
}

std::vector<std::shared_ptr<TypeNode>> RustParser::ParseTraitBounds() {
    std::vector<std::shared_ptr<TypeNode>> bounds;
    auto parse_bound = [&]() -> std::shared_ptr<TypeNode> {
        if (current_.kind == frontends::TokenKind::kLifetime) {
            return ParseLifetime();
        }
        return ParseTypePath();
    };
    bounds.push_back(parse_bound());
    while (MatchSymbol("+")) {
        bounds.push_back(parse_bound());
    }
    return bounds;
}

std::shared_ptr<TypePath> RustParser::ParseTypePath() {
    auto type = std::make_shared<TypePath>();
    type->loc = current_.loc;
    if (IsSymbol("::")) {
        type->is_absolute = true;
        Consume();
    }
    auto is_type_segment = [&]() {
        if (current_.kind == frontends::TokenKind::kIdentifier)
            return true;
        if (current_.kind == frontends::TokenKind::kKeyword) {
            return current_.lexeme == "self" || current_.lexeme == "Self" ||
                   current_.lexeme == "super" || current_.lexeme == "crate";
        }
        return false;
    };
    while (is_type_segment()) {
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
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "dyn") {
        auto dyn = std::make_shared<TraitObjectType>();
        dyn->loc = current_.loc;
        Consume();
        dyn->bounds = ParseTraitBounds();
        return dyn;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "impl") {
        auto impl = std::make_shared<ImplTraitType>();
        impl->loc = current_.loc;
        Consume();
        impl->bounds = ParseTraitBounds();
        return impl;
    }
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
            if (current_.kind == frontends::TokenKind::kLifetime) {
                current_param += "'";
            }
            current_param += current_.lexeme;
            Consume();
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
        if (IsSymbol("::")) {
            auto mem = std::dynamic_pointer_cast<MemberExpression>(expr);
            auto path_expr = std::dynamic_pointer_cast<PathExpression>(expr);
            Consume();
            if (IsSymbol("<")) {
                auto args = ParseGenericArgList();
                if (mem) {
                    mem->generic_args = std::move(args);
                } else if (path_expr && !path_expr->generic_args.empty()) {
                    path_expr->generic_args.back() = std::move(args);
                } else {
                    diagnostics_.Report(current_.loc, "Unexpected turbofish");
                }
                continue;
            }
            diagnostics_.Report(current_.loc, "Expected '<' after '::'");
            continue;
        }
        if (IsSymbol(".")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "await") {
                auto aw = std::make_shared<AwaitExpression>();
                aw->loc = current_.loc;
                aw->value = expr;
                aw->future = expr;
                Consume();
                expr = aw;
                continue;
            }
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
        if (IsSymbol("?")) {
            auto tr = std::make_shared<TryExpression>();
            tr->loc = current_.loc;
            tr->value = expr;
            Consume();
            expr = tr;
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
        range->kind = inclusive ? RangeExpression::RangeKind::kInclusive : RangeExpression::RangeKind::kExclusive;
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

std::shared_ptr<Statement> RustParser::ParseConstItem() {
    auto item = std::make_shared<ConstItem>();
    item->loc = current_.loc;
    MatchKeyword("const");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected const name");
    }
    if (MatchSymbol(":")) {
        item->type = ParseType();
    }
    if (MatchSymbol("=")) {
        item->value = ParseExpression();
    }
    MatchSymbol(";");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseTypeAlias() {
    auto item = std::make_shared<TypeAliasItem>();
    item->loc = current_.loc;
    MatchKeyword("type");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected type alias name");
    }
    if (MatchSymbol("=")) {
        item->alias = ParseType();
    }
    MatchSymbol(";");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseMacroRules() {
    auto item = std::make_shared<MacroRulesItem>();
    item->loc = current_.loc;
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        Consume();
    }
    ExpectSymbol("!", "Expected '!' after macro_rules");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        item->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected macro name");
    }
    if (IsSymbol("{")) {
        item->body = ParseDelimitedBody("{", "}");
    } else if (IsSymbol("(")) {
        item->body = ParseDelimitedBody("(", ")");
    } else if (IsSymbol("[")) {
        item->body = ParseDelimitedBody("[", "]");
    } else {
        diagnostics_.Report(current_.loc, "Expected macro body");
    }
    return item;
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
            StructField field;
            field.visibility = ParseVisibility();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                field.name = current_.lexeme;
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected field name");
                break;
            }
            if (MatchSymbol(":")) {
                field.type = ParseType();
            } else {
                diagnostics_.Report(current_.loc, "Expected ':' after field name");
            }
            item->fields.push_back(std::move(field));
            if (IsSymbol(",")) {
                Consume();
            } else {
                break;
            }
        }
        MatchSymbol("}");
        MatchSymbol(";");
        return item;
    }
    if (MatchSymbol("(")) {
        item->is_tuple = true;
        if (!IsSymbol(")")) {
            while (true) {
                StructField field;
                field.visibility = ParseVisibility();
                field.type = ParseType();
                item->fields.push_back(std::move(field));
                if (!MatchSymbol(","))
                    break;
                if (IsSymbol(")"))
                    break;
            }
        }
        ExpectSymbol(")", "Expected ')' after tuple struct fields");
        MatchSymbol(";");
        return item;
    }
    item->is_unit = true;
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
            EnumVariant variant;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                variant.name = current_.lexeme;
                Consume();
            } else {
                diagnostics_.Report(current_.loc, "Expected variant name");
                break;
            }
            if (MatchSymbol("(")) {
                if (!IsSymbol(")")) {
                    while (true) {
                        variant.tuple_fields.push_back(ParseType());
                        if (!MatchSymbol(","))
                            break;
                        if (IsSymbol(")"))
                            break;
                    }
                }
                ExpectSymbol(")", "Expected ')' after tuple variant");
            } else if (MatchSymbol("{")) {
                while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
                    StructField field;
                    if (current_.kind == frontends::TokenKind::kIdentifier) {
                        field.name = current_.lexeme;
                        Consume();
                    } else {
                        diagnostics_.Report(current_.loc, "Expected field name");
                        break;
                    }
                    ExpectSymbol(":", "Expected ':' in variant field");
                    field.type = ParseType();
                    variant.struct_fields.push_back(std::move(field));
                    if (!MatchSymbol(","))
                        break;
                }
                MatchSymbol("}");
            }
            if (MatchSymbol("=")) {
                variant.discriminant = ParseExpression();
            }
            item->variants.push_back(std::move(variant));
            if (!MatchSymbol(","))
                break;
        }
        MatchSymbol("}");
    }
    MatchSymbol(";");
    return item;
}

std::shared_ptr<Statement> RustParser::ParseFunction() {
    auto fn = std::make_shared<FunctionItem>();
    fn->loc = current_.loc;
    bool saw_modifier = true;
    while (saw_modifier) {
        saw_modifier = false;
        if (MatchKeyword("async")) {
            fn->is_async = true;
            saw_modifier = true;
        }
        if (MatchKeyword("const")) {
            fn->is_const = true;
            saw_modifier = true;
        }
        if (MatchKeyword("unsafe")) {
            fn->is_unsafe = true;
            saw_modifier = true;
        }
        if (MatchKeyword("extern")) {
            fn->is_extern = true;
            saw_modifier = true;
            if (current_.kind == frontends::TokenKind::kString) {
                fn->extern_abi = current_.lexeme;
                Consume();
            }
        }
    }
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
        auto parse_param = [&]() {
            FunctionItem::Param p;
            if (IsSymbol("&")) {
                auto ref = std::make_shared<ReferenceType>();
                ref->loc = current_.loc;
                Consume();
                if (MatchKeyword("mut")) {
                    ref->is_mut = true;
                }
                if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "self") {
                    p.name = "self";
                    Consume();
                    auto self_type = std::make_shared<TypePath>();
                    self_type->segments.push_back("Self");
                    ref->inner = self_type;
                    p.type = ref;
                    fn->params.push_back(std::move(p));
                    return;
                }
            }
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "self") {
                p.name = "self";
                Consume();
                if (MatchSymbol(":")) {
                    p.type = ParseType();
                } else {
                    auto self_type = std::make_shared<TypePath>();
                    self_type->segments.push_back("Self");
                    p.type = self_type;
                }
                fn->params.push_back(std::move(p));
                return;
            }
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                p.name = current_.lexeme;
                Consume();
                if (MatchSymbol(":")) {
                    p.type = ParseType();
                }
                fn->params.push_back(std::move(p));
                return;
            }
            diagnostics_.Report(current_.loc, "Expected parameter name");
        };
        parse_param();
        while (MatchSymbol(",")) {
            if (IsSymbol(")"))
                break;
            parse_param();
        }
        ExpectSymbol(")", "Expected ')' after parameters");
    }
    if (MatchSymbol("->")) {
        fn->return_type = ParseType();
    }
    fn->where_clause = ParseWhereClause();
    if (MatchSymbol(";")) {
        fn->has_body = false;
        return fn;
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
    if (MatchKeyword("unsafe")) {
        item->is_unsafe = true;
    }
    MatchKeyword("impl");
    item->type_params = ParseTypeParams();
    auto first_type = ParseType();
    if (MatchKeyword("for")) {
        item->trait_type = first_type;
        item->target_type = ParseType();
    } else {
        item->target_type = first_type;
    }
    item->where_clause = ParseWhereClause();
    ExpectSymbol("{", "Expected '{' in impl body");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto attrs = ParseAttributes();
        auto vis = ParseVisibility();
        std::shared_ptr<Statement> stmt;
        if (current_.kind == frontends::TokenKind::kIdentifier &&
            current_.lexeme == "macro_rules" && PeekToken().kind == frontends::TokenKind::kSymbol &&
            PeekToken().lexeme == "!") {
            stmt = ParseMacroRules();
        } else if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "fn" || current_.lexeme == "async" ||
                current_.lexeme == "unsafe" || current_.lexeme == "extern" ||
                current_.lexeme == "const") {
                if (current_.lexeme == "const") {
                    auto peek = PeekToken();
                    if (peek.kind == frontends::TokenKind::kKeyword && peek.lexeme != "fn") {
                        stmt = ParseConstItem();
                    } else {
                        stmt = ParseFunction();
                    }
                } else {
                    stmt = ParseFunction();
                }
            } else if (current_.lexeme == "const") {
                stmt = ParseConstItem();
            } else if (current_.lexeme == "type") {
                stmt = ParseTypeAlias();
            }
        }
        if (!stmt) {
            diagnostics_.Report(current_.loc, "Expected impl item");
            Sync();
        } else {
            stmt->attributes = std::move(attrs);
            if (!vis.empty()) {
                if (auto fn = std::dynamic_pointer_cast<FunctionItem>(stmt)) {
                    fn->visibility = vis;
                } else if (auto cst = std::dynamic_pointer_cast<ConstItem>(stmt)) {
                    cst->visibility = vis;
                } else if (auto alias = std::dynamic_pointer_cast<TypeAliasItem>(stmt)) {
                    alias->visibility = vis;
                }
            }
            item->items.push_back(stmt);
        }
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
    item->type_params = ParseTypeParams();
    if (MatchSymbol(":")) {
        item->super_traits = ParseTraitBounds();
    }
    item->where_clause = ParseWhereClause();
    ExpectSymbol("{", "Expected '{' for trait body");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto attrs = ParseAttributes();
        auto vis = ParseVisibility();
        std::shared_ptr<Statement> stmt;
        if (current_.kind == frontends::TokenKind::kIdentifier &&
            current_.lexeme == "macro_rules" && PeekToken().kind == frontends::TokenKind::kSymbol &&
            PeekToken().lexeme == "!") {
            stmt = ParseMacroRules();
        } else if (current_.kind == frontends::TokenKind::kKeyword) {
            if (current_.lexeme == "fn" || current_.lexeme == "async" ||
                current_.lexeme == "unsafe" || current_.lexeme == "extern" ||
                current_.lexeme == "const") {
                if (current_.lexeme == "const") {
                    auto peek = PeekToken();
                    if (peek.kind == frontends::TokenKind::kKeyword && peek.lexeme != "fn") {
                        stmt = ParseConstItem();
                    } else {
                        stmt = ParseFunction();
                    }
                } else {
                    stmt = ParseFunction();
                }
            } else if (current_.lexeme == "const") {
                stmt = ParseConstItem();
            } else if (current_.lexeme == "type") {
                stmt = ParseTypeAlias();
            }
        }
        if (!stmt) {
            diagnostics_.Report(current_.loc, "Expected trait item");
            Sync();
        } else {
            stmt->attributes = std::move(attrs);
            if (!vis.empty()) {
                if (auto fn = std::dynamic_pointer_cast<FunctionItem>(stmt)) {
                    fn->visibility = vis;
                } else if (auto cst = std::dynamic_pointer_cast<ConstItem>(stmt)) {
                    cst->visibility = vis;
                } else if (auto alias = std::dynamic_pointer_cast<TypeAliasItem>(stmt)) {
                    alias->visibility = vis;
                }
            }
            item->items.push_back(stmt);
        }
    }
    MatchSymbol("}");
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
    auto attrs = ParseAttributes();
    auto visibility = ParseVisibility();

    std::shared_ptr<Statement> stmt;
    if (current_.kind == frontends::TokenKind::kIdentifier && current_.lexeme == "macro_rules" &&
        PeekToken().kind == frontends::TokenKind::kSymbol && PeekToken().lexeme == "!") {
        stmt = ParseMacroRules();
    } else if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "use") {
            stmt = ParseUse();
        } else if (current_.lexeme == "fn" || current_.lexeme == "async" ||
                   current_.lexeme == "unsafe" || current_.lexeme == "extern") {
            if (current_.lexeme == "unsafe") {
                auto peek = PeekToken();
                if (peek.kind == frontends::TokenKind::kKeyword && peek.lexeme == "impl") {
                    stmt = ParseImpl();
                } else {
                    stmt = ParseFunction();
                }
            } else {
                stmt = ParseFunction();
            }
        } else if (current_.lexeme == "const") {
            auto peek = PeekToken();
            if (peek.kind == frontends::TokenKind::kKeyword && peek.lexeme == "fn") {
                stmt = ParseFunction();
            } else {
                stmt = ParseConstItem();
            }
        } else if (current_.lexeme == "type") {
            stmt = ParseTypeAlias();
        } else if (current_.lexeme == "let") {
            stmt = ParseLet();
        } else if (current_.lexeme == "return") {
            stmt = ParseReturn();
        } else if (current_.lexeme == "break") {
            stmt = ParseBreak();
        } else if (current_.lexeme == "continue") {
            stmt = ParseContinue();
        } else if (current_.lexeme == "loop") {
            stmt = ParseLoop();
        } else if (current_.lexeme == "for") {
            stmt = ParseFor();
        } else if (current_.lexeme == "struct") {
            stmt = ParseStruct();
        } else if (current_.lexeme == "enum") {
            stmt = ParseEnum();
        } else if (current_.lexeme == "impl") {
            stmt = ParseImpl();
        } else if (current_.lexeme == "trait") {
            stmt = ParseTrait();
        } else if (current_.lexeme == "mod") {
            stmt = ParseMod();
        }
    }

    if (stmt) {
        stmt->attributes = std::move(attrs);
        if (!visibility.empty()) {
            if (auto use_decl = std::dynamic_pointer_cast<UseDeclaration>(stmt)) {
                use_decl->visibility = visibility;
            } else if (auto fn = std::dynamic_pointer_cast<FunctionItem>(stmt)) {
                fn->visibility = visibility;
            } else if (auto st = std::dynamic_pointer_cast<StructItem>(stmt)) {
                st->visibility = visibility;
            } else if (auto en = std::dynamic_pointer_cast<EnumItem>(stmt)) {
                en->visibility = visibility;
            } else if (auto im = std::dynamic_pointer_cast<ImplItem>(stmt)) {
                im->visibility = visibility;
            } else if (auto tr = std::dynamic_pointer_cast<TraitItem>(stmt)) {
                tr->visibility = visibility;
            } else if (auto md = std::dynamic_pointer_cast<ModItem>(stmt)) {
                md->visibility = visibility;
            } else if (auto cst = std::dynamic_pointer_cast<ConstItem>(stmt)) {
                cst->visibility = visibility;
            } else if (auto alias = std::dynamic_pointer_cast<TypeAliasItem>(stmt)) {
                alias->visibility = visibility;
            } else {
                diagnostics_.Report(stmt->loc, "Visibility is not allowed here");
            }
        }
        return stmt;
    }

    if (!visibility.empty()) {
        diagnostics_.Report(current_.loc, "Visibility is not allowed here");
    }

    auto expr_stmt = std::make_shared<ExprStatement>();
    expr_stmt->loc = current_.loc;
    expr_stmt->expr = ParseExpression();
    if (!MatchSymbol(";")) {
        if (!allow_trailing_expr || !IsSymbol("}")) {
            diagnostics_.Report(current_.loc, "Expected ';'");
        }
    }
    expr_stmt->attributes = std::move(attrs);
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
