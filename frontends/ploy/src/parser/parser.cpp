#include "frontends/ploy/include/ploy_parser.h"

#include <stdexcept>

namespace polyglot::ploy {

// ============================================================================
// Token Management
// ============================================================================

frontends::Token PloyParser::Consume() {
    frontends::Token tok = current_;
    Advance();
    return tok;
}

void PloyParser::Advance() {
    current_ = lexer_.NextToken();
}

bool PloyParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool PloyParser::MatchSymbol(const std::string &symbol) {
    if (IsSymbol(symbol)) {
        Advance();
        return true;
    }
    return false;
}

bool PloyParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Advance();
        return true;
    }
    return false;
}

void PloyParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
    if (!MatchSymbol(symbol)) {
        diagnostics_.Report(current_.loc, message + ", got '" + current_.lexeme + "'");
        Sync();
    }
}

void PloyParser::ExpectKeyword(const std::string &keyword, const std::string &message) {
    if (!MatchKeyword(keyword)) {
        diagnostics_.Report(current_.loc, message + ", got '" + current_.lexeme + "'");
        Sync();
    }
}

void PloyParser::Sync() {
    // Recovery: skip tokens until we find a synchronization point
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsSymbol(";") || IsSymbol("}")) {
            Advance();
            return;
        }
        if (current_.kind == frontends::TokenKind::kKeyword) {
            // Stop at top-level keywords
            const std::string &kw = current_.lexeme;
            if (kw == "LINK" || kw == "IMPORT" || kw == "EXPORT" || kw == "MAP_TYPE" ||
                kw == "PIPELINE" || kw == "FUNC" || kw == "LET" || kw == "VAR" ||
                kw == "IF" || kw == "WHILE" || kw == "FOR" || kw == "MATCH" ||
                kw == "RETURN" || kw == "STRUCT" || kw == "MAP_FUNC") {
                return;
            }
        }
        Advance();
    }
}

// ============================================================================
// Module Parsing
// ============================================================================

void PloyParser::ParseModule() {
    Advance(); // Prime the first token
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        ParseTopLevel();
    }
}

std::shared_ptr<Module> PloyParser::TakeModule() {
    return std::move(module_);
}

void PloyParser::ParseTopLevel() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        const std::string &kw = current_.lexeme;
        if (kw == "LINK") {
            module_->declarations.push_back(ParseLinkDecl());
            return;
        }
        if (kw == "IMPORT") {
            module_->declarations.push_back(ParseImportDecl());
            return;
        }
        if (kw == "EXPORT") {
            module_->declarations.push_back(ParseExportDecl());
            return;
        }
        if (kw == "MAP_TYPE") {
            module_->declarations.push_back(ParseMapTypeDecl());
            return;
        }
        if (kw == "PIPELINE") {
            module_->declarations.push_back(ParsePipelineDecl());
            return;
        }
        if (kw == "FUNC") {
            module_->declarations.push_back(ParseFuncDecl());
            return;
        }
        if (kw == "STRUCT") {
            module_->declarations.push_back(ParseStructDecl());
            return;
        }
        if (kw == "MAP_FUNC") {
            module_->declarations.push_back(ParseMapFuncDecl());
            return;
        }
    }
    // Fallback: parse as a statement
    module_->declarations.push_back(ParseStatement());
}

// ============================================================================
// LINK Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseLinkDecl() {
    auto node = std::make_shared<LinkDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'LINK'

    ExpectSymbol("(", "expected '(' after LINK");

    // target_language
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        node->target_language = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected target language identifier");
        Sync();
        return node;
    }
    ExpectSymbol(",", "expected ',' after target language");

    // source_language
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        node->source_language = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected source language identifier");
        Sync();
        return node;
    }
    ExpectSymbol(",", "expected ',' after source language");

    // target_function (may be qualified: module::func)
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->target_symbol = current_.lexeme;
        Advance();
        while (IsSymbol("::")) {
            node->target_symbol += "::";
            Advance();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->target_symbol += current_.lexeme;
                Advance();
            }
        }
    } else {
        diagnostics_.Report(current_.loc, "expected target symbol");
        Sync();
        return node;
    }
    ExpectSymbol(",", "expected ',' after target symbol");

    // source_function (may be qualified)
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->source_symbol = current_.lexeme;
        Advance();
        while (IsSymbol("::")) {
            node->source_symbol += "::";
            Advance();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->source_symbol += current_.lexeme;
                Advance();
            }
        }
    } else {
        diagnostics_.Report(current_.loc, "expected source symbol");
        Sync();
        return node;
    }

    ExpectSymbol(")", "expected ')' after LINK arguments");

    // Optional: AS VAR or AS STRUCT
    if (MatchKeyword("AS")) {
        if (MatchKeyword("VAR")) {
            node->link_kind = LinkDecl::LinkKind::kVariable;
        } else if (MatchKeyword("STRUCT")) {
            node->link_kind = LinkDecl::LinkKind::kStruct;
        } else {
            diagnostics_.Report(current_.loc, "expected VAR or STRUCT after AS");
        }
    }

    // Optional body with MAP_TYPE directives
    if (IsSymbol("{")) {
        Advance();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kKeyword &&
                current_.lexeme == "MAP_TYPE") {
                node->body.push_back(ParseMapTypeDecl());
            } else {
                diagnostics_.Report(current_.loc, "expected MAP_TYPE inside LINK body");
                Sync();
            }
        }
        ExpectSymbol("}", "expected '}' to close LINK body");
    } else {
        // Simple LINK without body — expect semicolon
        ExpectSymbol(";", "expected ';' after LINK directive");
    }

    return node;
}

// ============================================================================
// IMPORT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseImportDecl() {
    auto node = std::make_shared<ImportDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'IMPORT'

    // IMPORT "path" AS alias; or IMPORT lang::module;
    if (current_.kind == frontends::TokenKind::kString) {
        // Path import: IMPORT "path/to/module"
        std::string raw = current_.lexeme;
        // Strip surrounding quotes
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        node->module_path = raw;
        Advance();
    } else if (current_.kind == frontends::TokenKind::kIdentifier ||
               current_.kind == frontends::TokenKind::kKeyword) {
        // Qualified import: IMPORT cpp::module_name
        node->language = current_.lexeme;
        Advance();
        if (IsSymbol("::")) {
            Advance();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->module_path = current_.lexeme;
                Advance();
                // Handle deeper qualification: module::sub
                while (IsSymbol("::")) {
                    node->module_path += "::";
                    Advance();
                    if (current_.kind == frontends::TokenKind::kIdentifier) {
                        node->module_path += current_.lexeme;
                        Advance();
                    }
                }
            }
        } else {
            // Just an identifier without ::
            node->module_path = node->language;
            node->language.clear();
        }
    } else {
        diagnostics_.Report(current_.loc, "expected module path or identifier after IMPORT");
        Sync();
        return node;
    }

    // Optional: AS alias
    if (MatchKeyword("AS")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            node->alias = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected alias name after AS");
        }
    }

    ExpectSymbol(";", "expected ';' after IMPORT");
    return node;
}

// ============================================================================
// EXPORT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseExportDecl() {
    auto node = std::make_shared<ExportDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'EXPORT'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->symbol_name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected symbol name after EXPORT");
        Sync();
        return node;
    }

    // Optional: AS "external_name"
    if (MatchKeyword("AS")) {
        if (current_.kind == frontends::TokenKind::kString) {
            std::string raw = current_.lexeme;
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                raw = raw.substr(1, raw.size() - 2);
            }
            node->external_name = raw;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected string literal after AS");
        }
    }

    ExpectSymbol(";", "expected ';' after EXPORT");
    return node;
}

// ============================================================================
// MAP_TYPE Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseMapTypeDecl() {
    auto node = std::make_shared<MapTypeDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'MAP_TYPE'

    ExpectSymbol("(", "expected '(' after MAP_TYPE");

    node->source_type = ParseQualifiedOrSimpleType();

    ExpectSymbol(",", "expected ',' between types in MAP_TYPE");

    node->target_type = ParseQualifiedOrSimpleType();

    ExpectSymbol(")", "expected ')' after MAP_TYPE arguments");
    ExpectSymbol(";", "expected ';' after MAP_TYPE");

    return node;
}

// ============================================================================
// PIPELINE Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParsePipelineDecl() {
    auto node = std::make_shared<PipelineDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'PIPELINE'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected pipeline name");
        Sync();
        return node;
    }

    ExpectSymbol("{", "expected '{' after pipeline name");
    node->body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close pipeline");

    return node;
}

// ============================================================================
// FUNC Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseFuncDecl() {
    auto node = std::make_shared<FuncDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'FUNC'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected function name");
        Sync();
        return node;
    }

    ExpectSymbol("(", "expected '(' after function name");
    node->params = ParseParams();
    ExpectSymbol(")", "expected ')' after parameters");

    // Optional return type: -> TYPE
    if (IsSymbol("->")) {
        Advance();
        node->return_type = ParseType();
    }

    ExpectSymbol("{", "expected '{' for function body");
    node->body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close function");

    return node;
}

// ============================================================================
// Parameters
// ============================================================================

std::vector<FuncDecl::Param> PloyParser::ParseParams() {
    std::vector<FuncDecl::Param> params;
    if (IsSymbol(")")) return params;

    do {
        FuncDecl::Param param;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            param.name = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected parameter name");
            break;
        }

        ExpectSymbol(":", "expected ':' after parameter name");
        param.type = ParseType();
        params.push_back(std::move(param));
    } while (MatchSymbol(","));

    return params;
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseStatement() {
    if (current_.kind == frontends::TokenKind::kKeyword) {
        const std::string &kw = current_.lexeme;
        if (kw == "LET") { Advance(); return ParseVarDecl(false); }
        if (kw == "VAR") { Advance(); return ParseVarDecl(true); }
        if (kw == "IF") { return ParseIfStatement(); }
        if (kw == "WHILE") { return ParseWhileStatement(); }
        if (kw == "FOR") { return ParseForStatement(); }
        if (kw == "MATCH") { return ParseMatchStatement(); }
        if (kw == "RETURN") { return ParseReturnStatement(); }
        if (kw == "BREAK") {
            auto node = std::make_shared<BreakStatement>();
            node->loc = current_.loc;
            Advance();
            ExpectSymbol(";", "expected ';' after BREAK");
            return node;
        }
        if (kw == "CONTINUE") {
            auto node = std::make_shared<ContinueStatement>();
            node->loc = current_.loc;
            Advance();
            ExpectSymbol(";", "expected ';' after CONTINUE");
            return node;
        }
        // LINK, IMPORT, EXPORT, MAP_TYPE, PIPELINE, FUNC at statement level
        if (kw == "LINK") return ParseLinkDecl();
        if (kw == "IMPORT") return ParseImportDecl();
        if (kw == "EXPORT") return ParseExportDecl();
        if (kw == "MAP_TYPE") return ParseMapTypeDecl();
        if (kw == "PIPELINE") return ParsePipelineDecl();
        if (kw == "FUNC") return ParseFuncDecl();
    }

    // Expression statement
    auto node = std::make_shared<ExprStatement>();
    node->loc = current_.loc;
    node->expr = ParseExpression();
    ExpectSymbol(";", "expected ';' after expression");
    return node;
}

std::shared_ptr<Statement> PloyParser::ParseVarDecl(bool is_mutable) {
    auto node = std::make_shared<VarDecl>();
    node->loc = current_.loc;
    node->is_mutable = is_mutable;

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected variable name");
        Sync();
        return node;
    }

    // Optional type annotation: : TYPE
    if (IsSymbol(":")) {
        Advance();
        node->type = ParseType();
    }

    // Optional initializer: = expr
    if (IsSymbol("=")) {
        Advance();
        node->init = ParseExpression();
    }

    ExpectSymbol(";", "expected ';' after variable declaration");
    return node;
}

std::shared_ptr<Statement> PloyParser::ParseIfStatement() {
    auto node = std::make_shared<IfStatement>();
    node->loc = current_.loc;
    Advance(); // consume 'IF'

    node->condition = ParseExpression();
    ExpectSymbol("{", "expected '{' after IF condition");
    node->then_body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close IF body");

    // Optional ELSE / ELSE IF
    if (MatchKeyword("ELSE")) {
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "IF") {
            // ELSE IF — nest as a single statement in else_body
            auto elif = ParseIfStatement();
            node->else_body.push_back(elif);
        } else {
            ExpectSymbol("{", "expected '{' after ELSE");
            node->else_body = ParseBlockBody();
            ExpectSymbol("}", "expected '}' to close ELSE body");
        }
    }

    return node;
}

std::shared_ptr<Statement> PloyParser::ParseWhileStatement() {
    auto node = std::make_shared<WhileStatement>();
    node->loc = current_.loc;
    Advance(); // consume 'WHILE'

    node->condition = ParseExpression();
    ExpectSymbol("{", "expected '{' after WHILE condition");
    node->body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close WHILE body");

    return node;
}

std::shared_ptr<Statement> PloyParser::ParseForStatement() {
    auto node = std::make_shared<ForStatement>();
    node->loc = current_.loc;
    Advance(); // consume 'FOR'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->iterator_name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected iterator variable name after FOR");
        Sync();
        return node;
    }

    ExpectKeyword("IN", "expected 'IN' in FOR statement");
    node->iterable = ParseExpression();

    ExpectSymbol("{", "expected '{' after FOR iterable");
    node->body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' to close FOR body");

    return node;
}

std::shared_ptr<Statement> PloyParser::ParseMatchStatement() {
    auto node = std::make_shared<MatchStatement>();
    node->loc = current_.loc;
    Advance(); // consume 'MATCH'

    node->value = ParseExpression();
    ExpectSymbol("{", "expected '{' after MATCH value");

    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        MatchStatement::Case match_case;

        if (MatchKeyword("CASE")) {
            match_case.pattern = ParseExpression();
        } else if (MatchKeyword("DEFAULT")) {
            match_case.pattern = nullptr; // default case
        } else {
            diagnostics_.Report(current_.loc, "expected CASE or DEFAULT in MATCH");
            Sync();
            continue;
        }

        ExpectSymbol("{", "expected '{' after CASE/DEFAULT");
        match_case.body = ParseBlockBody();
        ExpectSymbol("}", "expected '}' to close CASE/DEFAULT");

        node->cases.push_back(std::move(match_case));
    }

    ExpectSymbol("}", "expected '}' to close MATCH");
    return node;
}

std::shared_ptr<Statement> PloyParser::ParseReturnStatement() {
    auto node = std::make_shared<ReturnStatement>();
    node->loc = current_.loc;
    Advance(); // consume 'RETURN'

    if (!IsSymbol(";")) {
        node->value = ParseExpression();
    }

    ExpectSymbol(";", "expected ';' after RETURN");
    return node;
}

std::shared_ptr<Statement> PloyParser::ParseBlock() {
    auto node = std::make_shared<BlockStatement>();
    node->loc = current_.loc;
    ExpectSymbol("{", "expected '{'");
    node->statements = ParseBlockBody();
    ExpectSymbol("}", "expected '}'");
    return node;
}

std::vector<std::shared_ptr<Statement>> PloyParser::ParseBlockBody() {
    std::vector<std::shared_ptr<Statement>> stmts;
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        stmts.push_back(ParseStatement());
    }
    return stmts;
}

// ============================================================================
// Expressions
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseExpression() {
    return ParseAssignment();
}

std::shared_ptr<Expression> PloyParser::ParseAssignment() {
    auto left = ParseLogicalOr();

    if (IsSymbol("=") && !IsSymbol("==")) {
        // Check for '=' vs '=='
        // IsSymbol already matched '=', but we need to be careful
        // Actually '==' is handled as a separate token, so '=' is safe here
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = "=";
        Advance();
        node->left = left;
        node->right = ParseAssignment();
        return node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseLogicalOr() {
    auto left = ParseLogicalAnd();

    while (IsSymbol("||") || MatchKeyword("OR")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = "||";
        if (IsSymbol("||")) Advance();
        node->left = left;
        node->right = ParseLogicalAnd();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseLogicalAnd() {
    auto left = ParseEquality();

    while (IsSymbol("&&") || MatchKeyword("AND")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = "&&";
        if (IsSymbol("&&")) Advance();
        node->left = left;
        node->right = ParseEquality();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseEquality() {
    auto left = ParseComparison();

    while (IsSymbol("==") || IsSymbol("!=")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = current_.lexeme;
        Advance();
        node->left = left;
        node->right = ParseComparison();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseComparison() {
    auto left = ParseAdditive();

    while (IsSymbol("<") || IsSymbol(">") || IsSymbol("<=") || IsSymbol(">=")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = current_.lexeme;
        Advance();
        node->left = left;
        node->right = ParseAdditive();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseAdditive() {
    auto left = ParseMultiplicative();

    while (IsSymbol("+") || IsSymbol("-")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = current_.lexeme;
        Advance();
        node->left = left;
        node->right = ParseMultiplicative();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseMultiplicative() {
    auto left = ParseUnary();

    while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
        auto node = std::make_shared<BinaryExpression>();
        node->loc = current_.loc;
        node->op = current_.lexeme;
        Advance();
        node->left = left;
        node->right = ParseUnary();
        left = node;
    }

    return left;
}

std::shared_ptr<Expression> PloyParser::ParseUnary() {
    if (IsSymbol("-") || IsSymbol("!")) {
        auto node = std::make_shared<UnaryExpression>();
        node->loc = current_.loc;
        node->op = current_.lexeme;
        Advance();
        node->operand = ParseUnary();
        return node;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NOT") {
        auto node = std::make_shared<UnaryExpression>();
        node->loc = current_.loc;
        node->op = "!";
        Advance();
        node->operand = ParseUnary();
        return node;
    }

    return ParsePostfix();
}

std::shared_ptr<Expression> PloyParser::ParsePostfix() {
    auto expr = ParsePrimary();

    while (true) {
        if (IsSymbol("(")) {
            // Function call
            Advance();
            auto call = std::make_shared<CallExpression>();
            call->loc = expr->loc;
            call->callee = expr;
            call->args = ParseArguments();
            ExpectSymbol(")", "expected ')' after call arguments");
            expr = call;
        } else if (IsSymbol(".")) {
            // Member access
            Advance();
            auto member = std::make_shared<MemberExpression>();
            member->loc = current_.loc;
            member->object = expr;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                member->member = current_.lexeme;
                Advance();
            } else {
                diagnostics_.Report(current_.loc, "expected member name after '.'");
            }
            expr = member;
        } else if (IsSymbol("[")) {
            // Index access
            Advance();
            auto index = std::make_shared<IndexExpression>();
            index->loc = current_.loc;
            index->object = expr;
            index->index = ParseExpression();
            ExpectSymbol("]", "expected ']' after index");
            expr = index;
        } else {
            break;
        }
    }

    return expr;
}

std::shared_ptr<Expression> PloyParser::ParsePrimary() {
    // CALL directive
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CALL") {
        return ParseCallDirective();
    }

    // CONVERT expression: CONVERT(expr, Type)
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "CONVERT") {
        return ParseConvertExpression();
    }

    // Boolean literals
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "TRUE") {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->kind = Literal::Kind::kBool;
        lit->value = "true";
        Advance();
        return lit;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "FALSE") {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->kind = Literal::Kind::kBool;
        lit->value = "false";
        Advance();
        return lit;
    }

    // NULL literal
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "NULL") {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->kind = Literal::Kind::kNull;
        lit->value = "null";
        Advance();
        return lit;
    }

    // Identifier (possibly qualified with :: or struct literal with {)
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        std::string name = current_.lexeme;
        core::SourceLoc loc = current_.loc;
        Advance();

        // Check for qualified identifier: name::sub
        if (IsSymbol("::")) {
            auto qid = std::make_shared<QualifiedIdentifier>();
            qid->loc = loc;
            qid->qualifier = name;
            Advance(); // consume '::'
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                qid->name = current_.lexeme;
                Advance();
                // Handle deeper qualification
                while (IsSymbol("::")) {
                    qid->name += "::";
                    Advance();
                    if (current_.kind == frontends::TokenKind::kIdentifier) {
                        qid->name += current_.lexeme;
                        Advance();
                    }
                }
            }
            return qid;
        }

        // Check for struct literal: Name { field: value, ... }
        if (IsSymbol("{")) {
            // Peek ahead to see if this looks like a struct literal
            // (identifier followed by ':') vs a block statement
            return ParseStructLiteral(name);
        }

        // Check for range: name..end
        if (IsSymbol("..")) {
            auto range = std::make_shared<RangeExpression>();
            range->loc = loc;
            auto start = std::make_shared<Identifier>();
            start->loc = loc;
            start->name = name;
            range->start = start;
            Advance(); // consume '..'
            range->end = ParseExpression();
            return range;
        }

        auto id = std::make_shared<Identifier>();
        id->loc = loc;
        id->name = name;
        return id;
    }

    // Number literal
    if (current_.kind == frontends::TokenKind::kNumber) {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        // Determine if integer or float
        bool has_dot = current_.lexeme.find('.') != std::string::npos;
        bool has_exp = current_.lexeme.find('e') != std::string::npos ||
                       current_.lexeme.find('E') != std::string::npos;
        lit->kind = (has_dot || has_exp) ? Literal::Kind::kFloat : Literal::Kind::kInteger;
        lit->value = current_.lexeme;
        Advance();

        // Check for range: 0..10
        if (IsSymbol("..")) {
            auto range = std::make_shared<RangeExpression>();
            range->loc = lit->loc;
            range->start = lit;
            Advance();
            range->end = ParseExpression();
            return range;
        }

        return lit;
    }

    // String literal
    if (current_.kind == frontends::TokenKind::kString) {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->kind = Literal::Kind::kString;
        lit->value = current_.lexeme;
        Advance();
        return lit;
    }

    // List literal: [expr, expr, ...]
    if (IsSymbol("[")) {
        return ParseListLiteral();
    }

    // Dict literal: {"key": value, ...}
    if (IsSymbol("{")) {
        return ParseDictLiteral();
    }

    // Grouped expression or tuple literal: (expr) or (expr, expr, ...)
    if (IsSymbol("(")) {
        core::SourceLoc loc = current_.loc;
        Advance(); // consume '('
        if (IsSymbol(")")) {
            // Empty tuple literal
            Advance();
            auto tuple = std::make_shared<TupleLiteral>();
            tuple->loc = loc;
            return tuple;
        }
        auto first = ParseExpression();
        if (MatchSymbol(",")) {
            // This is a tuple literal
            auto tuple = std::make_shared<TupleLiteral>();
            tuple->loc = loc;
            tuple->elements.push_back(first);
            if (!IsSymbol(")")) {
                tuple->elements.push_back(ParseExpression());
                while (MatchSymbol(",")) {
                    if (IsSymbol(")")) break; // trailing comma
                    tuple->elements.push_back(ParseExpression());
                }
            }
            ExpectSymbol(")", "expected ')' after tuple literal");
            return tuple;
        }
        // Single expression in parens — grouped expression
        ExpectSymbol(")", "expected ')' after grouped expression");
        return first;
    }

    // Error recovery
    diagnostics_.Report(current_.loc, "unexpected token '" + current_.lexeme + "'");
    auto err = std::make_shared<Literal>();
    err->loc = current_.loc;
    err->kind = Literal::Kind::kNull;
    err->value = "null";
    Advance();
    return err;
}

std::shared_ptr<Expression> PloyParser::ParseCallDirective() {
    auto node = std::make_shared<CrossLangCallExpression>();
    node->loc = current_.loc;
    Advance(); // consume 'CALL'

    ExpectSymbol("(", "expected '(' after CALL");

    // language
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        node->language = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected language name in CALL");
        Sync();
        return node;
    }
    ExpectSymbol(",", "expected ',' after language in CALL");

    // function name (possibly qualified)
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->function = current_.lexeme;
        Advance();
        while (IsSymbol("::")) {
            node->function += "::";
            Advance();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->function += current_.lexeme;
                Advance();
            }
        }
    } else {
        diagnostics_.Report(current_.loc, "expected function name in CALL");
        Sync();
        return node;
    }

    // Remaining arguments (optional)
    while (MatchSymbol(",")) {
        node->args.push_back(ParseExpression());
    }

    ExpectSymbol(")", "expected ')' after CALL arguments");
    return node;
}

std::vector<std::shared_ptr<Expression>> PloyParser::ParseArguments() {
    std::vector<std::shared_ptr<Expression>> args;
    if (IsSymbol(")")) return args;

    do {
        args.push_back(ParseExpression());
    } while (MatchSymbol(","));

    return args;
}

// ============================================================================
// STRUCT Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseStructDecl() {
    auto node = std::make_shared<StructDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'STRUCT'

    // Struct name
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected struct name after 'STRUCT'");
        Sync();
        return node;
    }

    // Body: { field1: Type1, field2: Type2, ... }
    ExpectSymbol("{", "expected '{' after struct name");

    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        StructDecl::Field field;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            field.name = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected field name in struct");
            Sync();
            break;
        }

        ExpectSymbol(":", "expected ':' after field name");
        field.type = ParseType();
        node->fields.push_back(field);

        // Fields separated by ',' or newlines; trailing comma is allowed
        if (!MatchSymbol(",")) {
            // If no comma, we must be at '}' or have implicit separation
            if (!IsSymbol("}")) {
                // Allow optional semicolons
                MatchSymbol(";");
            }
        }
    }

    ExpectSymbol("}", "expected '}' at end of struct definition");
    return node;
}

// ============================================================================
// MAP_FUNC Declaration
// ============================================================================

std::shared_ptr<Statement> PloyParser::ParseMapFuncDecl() {
    auto node = std::make_shared<MapFuncDecl>();
    node->loc = current_.loc;
    Advance(); // consume 'MAP_FUNC'

    // Function name
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Advance();
    } else {
        diagnostics_.Report(current_.loc, "expected function name after 'MAP_FUNC'");
        Sync();
        return node;
    }

    // Parameters
    ExpectSymbol("(", "expected '(' after MAP_FUNC name");
    node->params = ParseParams();
    ExpectSymbol(")", "expected ')' after MAP_FUNC parameters");

    // Return type
    if (MatchSymbol("->")) {
        node->return_type = ParseType();
    }

    // Body
    ExpectSymbol("{", "expected '{' for MAP_FUNC body");
    node->body = ParseBlockBody();
    ExpectSymbol("}", "expected '}' at end of MAP_FUNC body");

    return node;
}

// ============================================================================
// CONVERT Expression
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseConvertExpression() {
    auto node = std::make_shared<ConvertExpression>();
    node->loc = current_.loc;
    Advance(); // consume 'CONVERT'

    ExpectSymbol("(", "expected '(' after CONVERT");

    // Source expression
    node->expr = ParseExpression();

    ExpectSymbol(",", "expected ',' between expression and target type in CONVERT");

    // Target type
    node->target_type = ParseType();

    ExpectSymbol(")", "expected ')' after CONVERT");
    return node;
}

// ============================================================================
// List Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseListLiteral() {
    auto node = std::make_shared<ListLiteral>();
    node->loc = current_.loc;
    Advance(); // consume '['

    if (!IsSymbol("]")) {
        node->elements.push_back(ParseExpression());
        while (MatchSymbol(",")) {
            if (IsSymbol("]")) break; // trailing comma
            node->elements.push_back(ParseExpression());
        }
    }

    ExpectSymbol("]", "expected ']' after list literal");
    return node;
}

// ============================================================================
// Dict Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseDictLiteral() {
    auto node = std::make_shared<DictLiteral>();
    node->loc = current_.loc;
    Advance(); // consume '{'

    if (!IsSymbol("}")) {
        // Parse first entry: key ':' value
        DictLiteral::Entry entry;
        entry.key = ParseExpression();
        ExpectSymbol(":", "expected ':' between key and value in dict literal");
        entry.value = ParseExpression();
        node->entries.push_back(entry);

        while (MatchSymbol(",")) {
            if (IsSymbol("}")) break; // trailing comma
            DictLiteral::Entry e;
            e.key = ParseExpression();
            ExpectSymbol(":", "expected ':' between key and value in dict literal");
            e.value = ParseExpression();
            node->entries.push_back(e);
        }
    }

    ExpectSymbol("}", "expected '}' after dict literal");
    return node;
}

// ============================================================================
// Struct Literal
// ============================================================================

std::shared_ptr<Expression> PloyParser::ParseStructLiteral(const std::string &struct_name) {
    auto node = std::make_shared<StructLiteral>();
    node->loc = current_.loc;
    node->struct_name = struct_name;
    Advance(); // consume '{'

    if (!IsSymbol("}")) {
        // Parse first field: name ':' value
        StructLiteral::FieldInit field;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            field.name = current_.lexeme;
            Advance();
        } else {
            diagnostics_.Report(current_.loc, "expected field name in struct literal");
            Sync();
            return node;
        }
        ExpectSymbol(":", "expected ':' after field name in struct literal");
        field.value = ParseExpression();
        node->fields.push_back(field);

        while (MatchSymbol(",")) {
            if (IsSymbol("}")) break; // trailing comma
            StructLiteral::FieldInit f;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                f.name = current_.lexeme;
                Advance();
            } else {
                diagnostics_.Report(current_.loc, "expected field name in struct literal");
                break;
            }
            ExpectSymbol(":", "expected ':' after field name in struct literal");
            f.value = ParseExpression();
            node->fields.push_back(f);
        }
    }

    ExpectSymbol("}", "expected '}' after struct literal");
    return node;
}

// ============================================================================
// Type Parsing
// ============================================================================

std::shared_ptr<TypeNode> PloyParser::ParseType() {
    return ParseQualifiedOrSimpleType();
}

std::shared_ptr<TypeNode> PloyParser::ParseQualifiedOrSimpleType() {
    if (current_.kind != frontends::TokenKind::kIdentifier &&
        current_.kind != frontends::TokenKind::kKeyword) {
        diagnostics_.Report(current_.loc, "expected type name");
        auto fallback = std::make_shared<SimpleType>();
        fallback->loc = current_.loc;
        fallback->name = "VOID";
        return fallback;
    }

    std::string name = current_.lexeme;
    core::SourceLoc loc = current_.loc;
    Advance();

    // Check for qualified type: lang::type
    if (IsSymbol("::")) {
        auto qt = std::make_shared<QualifiedType>();
        qt->loc = loc;
        qt->language = name;
        Advance();
        if (current_.kind == frontends::TokenKind::kIdentifier ||
            current_.kind == frontends::TokenKind::kKeyword) {
            qt->type_name = current_.lexeme;
            Advance();
            // Deeper qualification
            while (IsSymbol("::")) {
                qt->type_name += "::";
                Advance();
                if (current_.kind == frontends::TokenKind::kIdentifier ||
                    current_.kind == frontends::TokenKind::kKeyword) {
                    qt->type_name += current_.lexeme;
                    Advance();
                }
            }
        }
        return qt;
    }

    // Check for parameterized type: ARRAY[INT]
    if (IsSymbol("[")) {
        auto pt = std::make_shared<ParameterizedType>();
        pt->loc = loc;
        pt->name = name;
        Advance();
        pt->type_args.push_back(ParseType());
        while (MatchSymbol(",")) {
            pt->type_args.push_back(ParseType());
        }
        ExpectSymbol("]", "expected ']' after type arguments");
        return pt;
    }

    // Check for function type: (INT, FLOAT) -> BOOL
    if (name == "(" || IsSymbol("(")) {
        // This is handled at a higher level; fall through to simple type
    }

    auto st = std::make_shared<SimpleType>();
    st->loc = loc;
    st->name = name;
    return st;
}

} // namespace polyglot::ploy
