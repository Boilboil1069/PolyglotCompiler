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
                current_.lexeme == "int" || current_.lexeme == "void" ||
                current_.lexeme == "float" || current_.lexeme == "double" ||
                current_.lexeme == "char" || current_.lexeme == "bool" ||
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

std::shared_ptr<Expression> CppParser::ParseAssignment() {
    auto expr = ParseLogicalOr();
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

std::shared_ptr<Statement> CppParser::ParseRecord(const std::string &kind) {
    auto rec = std::make_shared<RecordDecl>();
    rec->loc = current_.loc;
    rec->kind = kind;
    MatchKeyword(kind);
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        rec->name = current_.lexeme;
        Consume();
    }
    if (MatchSymbol("{")) {
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto field_type = ParseType();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                FieldDecl f;
                f.type = field_type;
                f.name = current_.lexeme;
                Consume();
                rec->fields.push_back(std::move(f));
                MatchSymbol(";");
            } else {
                // maybe method
                auto stmt = ParseStatement();
                if (stmt)
                    rec->methods.push_back(stmt);
            }
        }
        MatchSymbol("}");
    }
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
                                                   const std::string &name) {
    auto decl = std::make_shared<VarDecl>();
    decl->loc = current_.loc;
    decl->type = type;
    decl->name = name;
    if (IsSymbol("=")) {
        Consume();
        decl->init = ParseExpression();
    }
    MatchSymbol(";");
    return decl;
}

std::shared_ptr<Statement> CppParser::ParseFunctionWithSignature(std::shared_ptr<TypeNode> ret_type,
                                                                 const std::string &name) {
    auto fn = std::make_shared<FunctionDecl>();
    fn->loc = current_.loc;
    fn->return_type = ret_type;
    fn->name = name;
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
    // assumes current_ is at return type token
    auto ret = ParseType();
    if (current_.kind != frontends::TokenKind::kIdentifier) {
        diagnostics_.Report(current_.loc, "Expected function name");
        return nullptr;
    }
    std::string name = current_.lexeme;
    Consume();
    return ParseFunctionWithSignature(ret, name);
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
    // init
    if (!IsSymbol(";")) {
        stmt->init = ParseStatement();
    } else {
        MatchSymbol(";");
    }
    // condition
    if (!IsSymbol(";")) {
        stmt->condition = ParseExpression();
    }
    MatchSymbol(";");
    // increment
    if (!IsSymbol(")")) {
        stmt->increment = ParseExpression();
    }
    ExpectSymbol(")", "Expected ')' after for clauses");
    stmt->body.push_back(ParseBlock());
    return stmt;
}

std::shared_ptr<Statement> CppParser::ParseStatement() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        if (current_.lexeme == "import") {
            return ParseImport();
        }
        if (current_.lexeme == "using") {
            return ParseUsing();
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
        // type keywords for var or function decls
        if (current_.lexeme == "int" || current_.lexeme == "void" || current_.lexeme == "float" ||
            current_.lexeme == "double" || current_.lexeme == "char" || current_.lexeme == "bool") {
            auto type = ParseType();
            if (current_.kind != frontends::TokenKind::kIdentifier) {
                diagnostics_.Report(current_.loc, "Expected identifier after type");
                MatchSymbol(";");
                return nullptr;
            }
            std::string name = current_.lexeme;
            Consume();
            if (IsSymbol("(")) {
                return ParseFunctionWithSignature(type, name);
            }
            return ParseVarDecl(type, name);
        }
    }
    if (IsSymbol("{")) {
        return ParseBlock();
    }

    // user-defined type starting identifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            std::string name = current_.lexeme;
            Consume();
            if (IsSymbol("(")) {
                return ParseFunctionWithSignature(type, name);
            }
            return ParseVarDecl(type, name);
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
