#include "frontends/cpp/include/cpp_parser.h"

namespace polyglot::cpp {

void CppParser::Advance() { current_ = lexer_.NextToken(); }

frontends::Token CppParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
        Advance();
    }
    return current_;
}

bool CppParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool CppParser::MatchSymbol(const std::string &symbol) {
    if (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol) {
        Consume();
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

// --- Types ----------------------------------------------------
std::shared_ptr<TypeNode> CppParser::ParseType() {
    bool is_const = false, is_volatile = false;
    while (current_.kind == frontends::TokenKind::kKeyword &&
           (current_.lexeme == "const" || current_.lexeme == "volatile")) {
        if (current_.lexeme == "const")
            is_const = true;
        if (current_.lexeme == "volatile")
            is_volatile = true;
        Consume();
    }

    std::shared_ptr<TypeNode> base;
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        auto simple = std::make_shared<SimpleType>();
        simple->loc = current_.loc;
        std::string name = current_.lexeme;
        Consume();

        while (true) {
            if (MatchSymbol("::")) {
                name += "::";
                if (current_.kind == frontends::TokenKind::kIdentifier ||
                    current_.kind == frontends::TokenKind::kKeyword) {
                    name += current_.lexeme;
                    Consume();
                    continue;
                }
            }
            if (IsSymbol("<")) {
                int depth = 0;
                std::string templ;
                do {
                    templ += current_.lexeme;
                    if (current_.lexeme == "<")
                        depth++;
                    if (current_.lexeme == ">")
                        depth--;
                    Consume();
                } while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile);
                name += templ;
                continue;
            }
            break;
        }

        simple->name = name;
        base = simple;
    }

    while (true) {
        if (IsSymbol("*")) {
            auto ptr = std::make_shared<PointerType>();
            ptr->loc = current_.loc;
            Consume();
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
        break;
    }

    if (is_const || is_volatile) {
        auto q = std::make_shared<QualifiedType>();
        q->loc = base ? base->loc : current_.loc;
        q->is_const = is_const;
        q->is_volatile = is_volatile;
        q->inner = base;
        base = q;
    }
    return base;
}

std::vector<std::string> CppParser::ParseTemplateParams() {
    std::vector<std::string> params;
    if (!MatchSymbol("<"))
        return params;
    while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            current_.kind == frontends::TokenKind::kKeyword) {
            params.push_back(current_.lexeme);
            Consume();
        }
        if (!MatchSymbol(","))
            break;
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
        args.push_back(ParseExpression());
        if (MatchSymbol(">")) {
            depth--;
            break;
        }
        if (MatchSymbol(","))
            continue;
        if (MatchSymbol("<")) {
            depth++;
            args.push_back(ParseExpression());
        }
    }
    return args;
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
        auto expr = ParseExpression();
        MatchSymbol(")");
        return expr;
    }
    if (IsSymbol("[")) {
        return ParseLambda();
    }
    diagnostics_.Report(current_.loc, "Expected expression");
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
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                p.name = current_.lexeme;
                Consume();
                if (MatchSymbol(":")) {
                    p.type = ParseType();
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
    if (IsSymbol("+") || IsSymbol("-") || IsSymbol("!") || IsSymbol("~") || IsSymbol("&") ||
        IsSymbol("*")) {
        auto unary = std::make_shared<UnaryExpression>();
        unary->op = current_.lexeme;
        unary->loc = current_.loc;
        Consume();
        unary->operand = ParseUnary();
        return unary;
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

std::shared_ptr<Expression> CppParser::ParseRelational() {
    auto expr = ParseAdditive();
    while (IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=")) {
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

std::shared_ptr<Expression> CppParser::ParseLogicalAnd() {
    auto expr = ParseEquality();
    while (IsSymbol("&&")) {
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
    if (IsSymbol("=")) {
        auto bin = std::make_shared<BinaryExpression>();
        bin->op = "=";
        bin->loc = expr ? expr->loc : current_.loc;
        Consume();
        bin->left = expr;
        bin->right = ParseAssignment();
        return bin;
    }
    return expr;
}

std::shared_ptr<Expression> CppParser::ParseExpression() { return ParseAssignment(); }

// --- Statements ----------------------------------------------------
std::shared_ptr<Statement> CppParser::ParseImport() {
    auto stmt = std::make_shared<ImportDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("import");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        stmt->module = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected module name");
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseUsing() {
    auto stmt = std::make_shared<UsingDeclaration>();
    stmt->loc = current_.loc;
    MatchKeyword("using");
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        stmt->name = current_.lexeme;
        Consume();
    } else {
        diagnostics_.Report(current_.loc, "Expected identifier after using");
    }
    if (MatchSymbol("=")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            stmt->aliased = current_.lexeme;
            Consume();
        }
    }
    MatchSymbol(";");
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseReturn() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("return");
    stmt->value = ParseExpression();
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
                                   (current_.lexeme == "case" || current_.lexeme == "default")))) {
            cs.body.push_back(ParseStatement());
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
    rec->kind = kind;
    MatchKeyword(kind);
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        rec->name = current_.lexeme;
        Consume();
    }
    if (MatchSymbol(":")) {
        do {
            BaseSpecifier base;
            if (current_.kind == frontends::TokenKind::kKeyword &&
                (current_.lexeme == "public" || current_.lexeme == "protected" ||
                 current_.lexeme == "private")) {
                base.access = current_.lexeme;
                Consume();
            }
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                std::string name = current_.lexeme;
                Consume();
                while (MatchSymbol("::")) {
                    name += "::";
                    if (current_.kind == frontends::TokenKind::kIdentifier ||
                        current_.kind == frontends::TokenKind::kKeyword) {
                        name += current_.lexeme;
                        Consume();
                    }
                }
                base.name = name;
            }
            rec->bases.push_back(std::move(base));
        } while (MatchSymbol(","));
    }

    ExpectSymbol("{", "Expected '{' for record body");
    if (IsSymbol("{"))
        MatchSymbol("{");
    std::string current_access = (kind == "struct") ? "public" : "private";
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        if (current_.kind == frontends::TokenKind::kKeyword &&
            (current_.lexeme == "public" || current_.lexeme == "protected" ||
             current_.lexeme == "private")) {
            std::string acc = current_.lexeme;
            Consume();
            MatchSymbol(":");
            current_access = acc;
            continue;
        }

        bool is_constexpr = false, is_inline = false, is_static = false;
        while (current_.kind == frontends::TokenKind::kKeyword &&
               (current_.lexeme == "constexpr" || current_.lexeme == "inline" ||
                current_.lexeme == "static")) {
            if (current_.lexeme == "constexpr")
                is_constexpr = true;
            if (current_.lexeme == "inline")
                is_inline = true;
            if (current_.lexeme == "static")
                is_static = true;
            Consume();
        }

        auto member_type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "operator")) {
            bool is_operator = false;
            std::string name;
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
                auto fn = ParseFunctionWithSignature(member_type, name, is_constexpr, is_inline,
                                                     is_static, is_operator, op_symbol);
                fn->access = current_access;
                rec->methods.push_back(fn);
                continue;
            }

            FieldDecl f;
            f.type = member_type;
            f.name = name;
            f.access = current_access;
            f.is_constexpr = is_constexpr;
            f.is_static = is_static;
            MatchSymbol(";");
            rec->fields.push_back(std::move(f));
            continue;
        }

        auto stmt = ParseStatement();
        if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(stmt)) {
            fn->access = current_access;
            rec->methods.push_back(fn);
        } else if (auto vd = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            vd->access = current_access;
            rec->methods.push_back(vd);
        } else if (stmt) {
            rec->methods.push_back(stmt);
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
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        en->name = current_.lexeme;
        Consume();
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
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto stmt = ParseStatement();
            if (stmt)
                ns->members.push_back(stmt);
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
    tmpl->inner = ParseStatement();
    return tmpl;
}

std::shared_ptr<Statement> CppParser::ParseVarDecl(std::shared_ptr<TypeNode> type,
                                                   const std::string &name, bool is_constexpr,
                                                   bool is_inline, bool is_static,
                                                   const std::string &access) {
    auto decl = std::make_shared<VarDecl>();
    decl->loc = current_.loc;
    decl->type = type;
    decl->name = name;
    decl->is_constexpr = is_constexpr;
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
                                      bool is_constexpr, bool is_inline, bool is_static,
                                      bool is_operator, const std::string &op_symbol) {
    auto fn = std::make_shared<FunctionDecl>();
    fn->loc = current_.loc;
    fn->return_type = ret_type;
    fn->name = name;
    fn->is_constexpr = is_constexpr;
    fn->is_inline = is_inline;
    fn->is_static = is_static;
    fn->is_operator = is_operator;
    fn->operator_symbol = op_symbol;
    ExpectSymbol("(", "Expected '(' after function name");
    if (!IsSymbol(")")) {
        while (true) {
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                auto param_type = ParseType();
                if (current_.kind == frontends::TokenKind::kIdentifier) {
                    std::string param_name = current_.lexeme;
                    Consume();
                    fn->params.emplace_back(param_name, param_type);
                } else {
                    diagnostics_.Report(current_.loc, "Expected parameter name");
                }
            }
            if (!MatchSymbol(","))
                break;
        }
    }
    ExpectSymbol(")", "Expected ')' after parameters");
    if (!MatchSymbol("{")) {
        diagnostics_.Report(current_.loc, "Expected '{' to start function body");
        return fn;
    }
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        fn->body.push_back(ParseStatement());
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
    return ParseFunctionWithSignature(ret, name, false, false, false, false, "");
}

std::shared_ptr<Statement> CppParser::ParseBlock() {
    auto block = std::make_shared<CompoundStatement>();
    MatchSymbol("{");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        block->statements.push_back(ParseStatement());
    }
    MatchSymbol("}");
    return block;
}

std::shared_ptr<Statement> CppParser::ParseIf() {
    auto stmt = std::make_shared<IfStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("if");
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
    auto stmt = std::make_shared<ForStatement>();
    stmt->loc = current_.loc;
    MatchKeyword("for");
    ExpectSymbol("(", "Expected '(' after for");
    if (!IsSymbol(";")) {
        stmt->init = ParseStatement();
    } else {
        MatchSymbol(";");
    }
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
    bool is_constexpr = false, is_inline = false, is_static = false;
    while (current_.kind == frontends::TokenKind::kKeyword &&
           (current_.lexeme == "constexpr" || current_.lexeme == "inline" ||
            current_.lexeme == "static")) {
        if (current_.lexeme == "constexpr")
            is_constexpr = true;
        if (current_.lexeme == "inline")
            is_inline = true;
        if (current_.lexeme == "static")
            is_static = true;
        Consume();
    }

    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "import") {
            return ParseImport();
        }
        if (current_.lexeme == "using") {
            return ParseUsing();
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
        if (current_.lexeme == "class" || current_.lexeme == "struct") {
            std::string kind = current_.lexeme;
            return ParseRecord(kind);
        }
        if (current_.lexeme == "enum") {
            return ParseEnum();
        }
        if (current_.lexeme == "namespace") {
            return ParseNamespace();
        }
        if (current_.lexeme == "template") {
            return ParseTemplate();
        }
        if (current_.lexeme == "int" || current_.lexeme == "void" || current_.lexeme == "float" ||
            current_.lexeme == "double" || current_.lexeme == "char" || current_.lexeme == "bool" ||
            current_.lexeme == "auto") {
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
                return ParseFunctionWithSignature(type, name, is_constexpr, is_inline, is_static,
                                                  is_operator, op_symbol);
            }
            return ParseVarDecl(type, name, is_constexpr, is_inline, is_static);
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
                return ParseFunctionWithSignature(type, name, is_constexpr, is_inline, is_static,
                                                  false, "");
            }
            return ParseVarDecl(type, name, is_constexpr, is_inline, is_static);
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
        ParseTopLevel();
        if (current_.kind == frontends::TokenKind::kEndOfFile) {
            break;
        }
    }
}

std::shared_ptr<Module> CppParser::TakeModule() { return module_; }

} // namespace polyglot::cpp
