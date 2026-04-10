/**
 * @file     parser.cpp
 * @brief    Java language frontend implementation
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/java/include/java_parser.h"

namespace polyglot::java {

void JavaParser::Advance() {
    current_ = lexer_.NextToken();
}

frontends::Token JavaParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
        Advance();
    }
    return current_;
}

bool JavaParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool JavaParser::MatchSymbol(const std::string &symbol) {
    if (IsSymbol(symbol)) {
        Consume();
        return true;
    }
    return false;
}

bool JavaParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Consume();
        return true;
    }
    return false;
}

void JavaParser::ExpectSymbol(const std::string &symbol, const std::string &message) {
    if (!MatchSymbol(symbol)) {
        diagnostics_.Report(current_.loc, message);
    }
}

void JavaParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsSymbol(";") || IsSymbol("{") || IsSymbol("}")) return;
        if (current_.kind == frontends::TokenKind::kKeyword) {
            auto &kw = current_.lexeme;
            if (kw == "class" || kw == "interface" || kw == "enum" ||
                kw == "import" || kw == "package" || kw == "public" ||
                kw == "private" || kw == "protected") {
                return;
            }
        }
        Consume();
    }
}

std::string JavaParser::ParseQualifiedName() {
    std::string name;
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        name = current_.lexeme;
        Consume();
        while (IsSymbol(".")) {
            Consume(); // '.'
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                name += "." + current_.lexeme;
                Consume();
            } else if (IsSymbol("*")) {
                name += ".*";
                Consume();
                break;
            } else {
                break;
            }
        }
    }
    return name;
}

// ============================================================================
// Top-Level Parsing
// ============================================================================

void JavaParser::ParseModule() {
    module_ = std::make_shared<Module>();
    Consume(); // prime the parser

    // Optional package declaration
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "package") {
        module_->package_decl = ParsePackageDecl();
    }

    // Import declarations
    while (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "import") {
        module_->imports.push_back(ParseImportDecl());
    }

    // Type declarations
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        ParseTopLevel();
    }
}

std::shared_ptr<Module> JavaParser::TakeModule() {
    return std::move(module_);
}

std::shared_ptr<PackageDecl> JavaParser::ParsePackageDecl() {
    auto node = std::make_shared<PackageDecl>();
    node->loc = current_.loc;
    Consume(); // 'package'
    node->name = ParseQualifiedName();
    ExpectSymbol(";", "expected ';' after package declaration");
    return node;
}

std::shared_ptr<ImportDecl> JavaParser::ParseImportDecl() {
    auto node = std::make_shared<ImportDecl>();
    node->loc = current_.loc;
    Consume(); // 'import'

    if (MatchKeyword("static")) {
        node->is_static = true;
    }

    node->path = ParseQualifiedName();
    ExpectSymbol(";", "expected ';' after import declaration");
    return node;
}

std::vector<Annotation> JavaParser::ParseAnnotations() {
    std::vector<Annotation> annotations;
    while (current_.kind == frontends::TokenKind::kKeyword &&
           !current_.lexeme.empty() && current_.lexeme[0] == '@') {
        Annotation ann;
        ann.name = current_.lexeme;
        Consume();
        if (IsSymbol("(")) {
            Consume();
            // Skip annotation arguments for now
            int depth = 1;
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                if (IsSymbol("(")) depth++;
                else if (IsSymbol(")")) depth--;
                if (depth > 0) Consume();
            }
            if (IsSymbol(")")) Consume();
        }
        annotations.push_back(ann);
    }
    return annotations;
}

std::string JavaParser::ParseAccessModifier() {
    if (MatchKeyword("public")) return "public";
    if (MatchKeyword("private")) return "private";
    if (MatchKeyword("protected")) return "protected";
    return "";
}

void JavaParser::ParseTopLevel() {
    auto annotations = ParseAnnotations();
    std::string access = ParseAccessModifier();

    // Consume additional modifiers
    bool is_abstract = false, is_final = false, is_static = false;
    bool is_sealed = false, is_non_sealed = false;
    bool is_strictfp = false;

    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "abstract") { is_abstract = true; Consume(); }
        else if (kw == "final") { is_final = true; Consume(); }
        else if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "sealed") { is_sealed = true; Consume(); }
        else if (kw == "non-sealed") { is_non_sealed = true; Consume(); }
        else if (kw == "strictfp") { is_strictfp = true; Consume(); }
        else break;
    }
    (void)is_strictfp;

    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class") {
        auto cls = ParseClassDecl(access, annotations);
        cls->is_abstract = is_abstract;
        cls->is_final = is_final;
        cls->is_static = is_static;
        cls->is_sealed = is_sealed;
        cls->is_non_sealed = is_non_sealed;
        module_->declarations.push_back(cls);
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "interface") {
        auto iface = ParseInterfaceDecl(access, annotations);
        iface->is_sealed = is_sealed;
        module_->declarations.push_back(iface);
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "enum") {
        module_->declarations.push_back(ParseEnumDecl(access, annotations));
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "record") {
        module_->declarations.push_back(ParseRecordDecl(access, annotations));
    } else {
        // Skip unknown tokens
        if (current_.kind != frontends::TokenKind::kEndOfFile) {
            Consume();
        }
    }
}

// ============================================================================
// Type Declarations
// ============================================================================

std::shared_ptr<ClassDecl> JavaParser::ParseClassDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    auto node = std::make_shared<ClassDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->annotations = annotations;
    Consume(); // 'class'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    // Type parameters
    node->type_params = ParseTypeParameters();

    // extends
    if (MatchKeyword("extends")) {
        node->superclass = ParseType();
    }

    // implements
    if (MatchKeyword("implements")) {
        do {
            node->interfaces.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    // permits (Java 17+)
    if (MatchKeyword("permits")) {
        do {
            node->permits.push_back(ParseQualifiedName());
        } while (MatchSymbol(","));
    }

    // Class body
    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto member_annotations = ParseAnnotations();
            std::string member_access = ParseAccessModifier();

            // Check for class/interface/enum nesting
            bool member_static = false, member_final = false, member_abstract = false;
            bool member_synchronized = false, member_native = false, member_default = false;
            bool member_volatile = false, member_transient = false;

            while (current_.kind == frontends::TokenKind::kKeyword) {
                auto &kw = current_.lexeme;
                if (kw == "static") { member_static = true; Consume(); }
                else if (kw == "final") { member_final = true; Consume(); }
                else if (kw == "abstract") { member_abstract = true; Consume(); }
                else if (kw == "synchronized") { member_synchronized = true; Consume(); }
                else if (kw == "native") { member_native = true; Consume(); }
                else if (kw == "default") { member_default = true; Consume(); }
                else if (kw == "volatile") { member_volatile = true; Consume(); }
                else if (kw == "transient") { member_transient = true; Consume(); }
                else break;
            }

            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class") {
                auto inner = ParseClassDecl(member_access, member_annotations);
                inner->is_static = member_static;
                node->members.push_back(inner);
                continue;
            }

            // Constructor check: identifier matches class name and followed by '('
            if (current_.kind == frontends::TokenKind::kIdentifier &&
                current_.lexeme == node->name && !member_static) {
                auto ctor = ParseConstructorDecl(member_access, node->name);
                ctor->annotations = member_annotations;
                node->members.push_back(ctor);
                continue;
            }

            // Type parameters for generic methods
            auto method_type_params = ParseTypeParameters();

            // Return type or field type
            auto type = ParseType();

            if (current_.kind == frontends::TokenKind::kIdentifier) {
                std::string name = current_.lexeme;
                Consume();

                if (IsSymbol("(")) {
                    // Method declaration
                    auto method = std::make_shared<MethodDecl>();
                    method->loc = type->loc;
                    method->name = name;
                    method->return_type = type;
                    method->access = member_access;
                    method->is_static = member_static;
                    method->is_final = member_final;
                    method->is_abstract = member_abstract;
                    method->is_synchronized = member_synchronized;
                    method->is_native = member_native;
                    method->is_default = member_default;
                    method->annotations = member_annotations;
                    method->type_params = method_type_params;
                    method->params = ParseParameters();

                    // throws clause
                    if (MatchKeyword("throws")) {
                        do {
                            method->throws_types.push_back(ParseType());
                        } while (MatchSymbol(","));
                    }

                    if (IsSymbol("{")) {
                        auto block = ParseBlock();
                        method->body = block->statements;
                    } else {
                        ExpectSymbol(";", "expected ';' after method declaration");
                    }

                    node->members.push_back(method);
                } else {
                    // Field declaration
                    auto field = std::make_shared<FieldDecl>();
                    field->loc = type->loc;
                    field->name = name;
                    field->type = type;
                    field->access = member_access;
                    field->is_static = member_static;
                    field->is_final = member_final;
                    field->is_volatile = member_volatile;
                    field->is_transient = member_transient;
                    field->annotations = member_annotations;

                    if (MatchSymbol("=")) {
                        field->init = ParseExpression();
                    }
                    ExpectSymbol(";", "expected ';' after field declaration");
                    node->members.push_back(field);
                }
            } else {
                Sync();
                if (IsSymbol(";")) Consume();
            }
        }
        ExpectSymbol("}", "expected '}' at end of class body");
    }

    return node;
}

std::shared_ptr<InterfaceDecl> JavaParser::ParseInterfaceDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    auto node = std::make_shared<InterfaceDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->annotations = annotations;
    Consume(); // 'interface'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    node->type_params = ParseTypeParameters();

    if (MatchKeyword("extends")) {
        do {
            node->extends_types.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    if (MatchKeyword("permits")) {
        do {
            node->permits.push_back(ParseQualifiedName());
        } while (MatchSymbol(","));
    }

    // Interface body
    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto ann = ParseAnnotations();
            std::string acc = ParseAccessModifier();
            bool is_static = false, is_default = false, is_abstract = false;

            while (current_.kind == frontends::TokenKind::kKeyword) {
                if (current_.lexeme == "static") { is_static = true; Consume(); }
                else if (current_.lexeme == "default") { is_default = true; Consume(); }
                else if (current_.lexeme == "abstract") { is_abstract = true; Consume(); }
                else break;
            }

            auto type_params = ParseTypeParameters();
            auto type = ParseType();

            if (current_.kind == frontends::TokenKind::kIdentifier) {
                std::string name = current_.lexeme;
                Consume();

                if (IsSymbol("(")) {
                    auto method = std::make_shared<MethodDecl>();
                    method->loc = type->loc;
                    method->name = name;
                    method->return_type = type;
                    method->access = acc;
                    method->is_static = is_static;
                    method->is_default = is_default;
                    method->is_abstract = is_abstract;
                    method->annotations = ann;
                    method->type_params = type_params;
                    method->params = ParseParameters();

                    if (MatchKeyword("throws")) {
                        do {
                            method->throws_types.push_back(ParseType());
                        } while (MatchSymbol(","));
                    }

                    if (IsSymbol("{")) {
                        auto block = ParseBlock();
                        method->body = block->statements;
                    } else {
                        ExpectSymbol(";", "expected ';' after interface method");
                    }
                    node->members.push_back(method);
                } else {
                    // Constant field
                    auto field = std::make_shared<FieldDecl>();
                    field->loc = type->loc;
                    field->name = name;
                    field->type = type;
                    field->is_static = true;
                    field->is_final = true;
                    if (MatchSymbol("=")) {
                        field->init = ParseExpression();
                    }
                    ExpectSymbol(";", "expected ';' after constant");
                    node->members.push_back(field);
                }
            } else {
                Sync();
                if (IsSymbol(";")) Consume();
            }
        }
        ExpectSymbol("}", "expected '}' at end of interface body");
    }

    return node;
}

std::shared_ptr<EnumDecl> JavaParser::ParseEnumDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    auto node = std::make_shared<EnumDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->annotations = annotations;
    Consume(); // 'enum'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    if (MatchKeyword("implements")) {
        do {
            node->interfaces.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    if (IsSymbol("{")) {
        Consume();
        // Parse enum constants
        while (!IsSymbol("}") && !IsSymbol(";") &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            EnumDecl::EnumConstant constant;
            constant.annotations = ParseAnnotations();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                constant.name = current_.lexeme;
                Consume();
            }
            if (IsSymbol("(")) {
                Consume();
                while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                    constant.args.push_back(ParseExpression());
                    if (!MatchSymbol(",")) break;
                }
                ExpectSymbol(")", "expected ')' after enum constant arguments");
            }
            node->constants.push_back(constant);
            if (!MatchSymbol(",")) break;
        }

        // Optional semicolon followed by enum body declarations
        if (IsSymbol(";")) {
            Consume();
            while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
                // Parse methods/fields within enum
                auto ann = ParseAnnotations();
                auto acc = ParseAccessModifier();
                (void)ann;
                (void)acc;
                Sync();
                if (IsSymbol(";")) Consume();
                else if (IsSymbol("}")) break;
                else Consume();
            }
        }
        ExpectSymbol("}", "expected '}' at end of enum body");
    }

    return node;
}

std::shared_ptr<RecordDecl> JavaParser::ParseRecordDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    auto node = std::make_shared<RecordDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->annotations = annotations;
    Consume(); // 'record'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    node->type_params = ParseTypeParameters();

    // Record components
    if (IsSymbol("(")) {
        node->components = ParseParameters();
    }

    if (MatchKeyword("implements")) {
        do {
            node->interfaces.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    // Record body
    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            Consume(); // Skip body for now
        }
        ExpectSymbol("}", "expected '}' at end of record body");
    }

    return node;
}

// ============================================================================
// Members
// ============================================================================

std::shared_ptr<ConstructorDecl> JavaParser::ParseConstructorDecl(
    const std::string &access, const std::string &class_name) {
    auto node = std::make_shared<ConstructorDecl>();
    node->loc = current_.loc;
    node->name = class_name;
    node->access = access;
    Consume(); // constructor name (same as class name)

    node->params = ParseParameters();

    if (MatchKeyword("throws")) {
        do {
            node->throws_types.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    if (IsSymbol("{")) {
        auto block = ParseBlock();
        node->body = block->statements;
    }

    return node;
}

std::vector<TypeParameter> JavaParser::ParseTypeParameters() {
    std::vector<TypeParameter> params;
    if (!IsSymbol("<")) return params;
    Consume(); // '<'

    while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
        TypeParameter tp;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            tp.name = current_.lexeme;
            Consume();
        }
        if (MatchKeyword("extends")) {
            tp.bounds.push_back(ParseType());
            while (MatchSymbol("&")) {
                tp.bounds.push_back(ParseType());
            }
        }
        params.push_back(tp);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol(">", "expected '>' after type parameters");
    return params;
}

std::vector<Parameter> JavaParser::ParseParameters() {
    std::vector<Parameter> params;
    ExpectSymbol("(", "expected '('");
    while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        Parameter p;
        p.annotations = ParseAnnotations();
        if (MatchKeyword("final")) p.is_final = true;
        p.type = ParseType();
        // Check for varargs
        if (IsSymbol(".")) {
            // Consume "..."
            Consume();
            if (IsSymbol(".")) Consume();
            if (IsSymbol(".")) Consume();
            p.is_varargs = true;
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            p.name = current_.lexeme;
            Consume();
        }
        params.push_back(p);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol(")", "expected ')'");
    return params;
}

// ============================================================================
// Types
// ============================================================================

std::shared_ptr<TypeNode> JavaParser::ParseType() {
    auto node = std::make_shared<SimpleType>();
    node->loc = current_.loc;

    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        node->name = ParseQualifiedName();
    } else {
        node->name = "void";
    }

    // Generic type arguments
    if (IsSymbol("<")) {
        auto gen = std::make_shared<GenericType>();
        gen->loc = node->loc;
        gen->name = node->name;
        Consume(); // '<'
        while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
            gen->type_args.push_back(ParseTypeArgument());
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol(">", "expected '>' after type arguments");

        // Array dimensions
        while (IsSymbol("[")) {
            Consume();
            ExpectSymbol("]", "expected ']' for array type");
            auto arr = std::make_shared<ArrayType>();
            arr->loc = gen->loc;
            arr->element_type = gen;
            return arr;
        }
        return gen;
    }

    // Array dimensions
    while (IsSymbol("[")) {
        Consume();
        ExpectSymbol("]", "expected ']' for array type");
        auto arr = std::make_shared<ArrayType>();
        arr->loc = node->loc;
        arr->element_type = node;
        return arr;
    }

    return node;
}

std::shared_ptr<TypeNode> JavaParser::ParseTypeArgument() {
    if (IsSymbol("?")) {
        auto wc = std::make_shared<WildcardType>();
        wc->loc = current_.loc;
        Consume();
        if (MatchKeyword("extends")) {
            wc->bound_kind = WildcardType::BoundKind::kExtends;
            wc->bound = ParseType();
        } else if (MatchKeyword("super")) {
            wc->bound_kind = WildcardType::BoundKind::kSuper;
            wc->bound = ParseType();
        }
        return wc;
    }
    return ParseType();
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> JavaParser::ParseStatement() {
    if (IsSymbol("{")) return ParseBlock();
    if (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "if") return ParseIf();
        if (kw == "while") return ParseWhile();
        if (kw == "for") return ParseFor();
        if (kw == "switch") return ParseSwitch();
        if (kw == "try") return ParseTry();
        if (kw == "return") return ParseReturn();
        if (kw == "throw") return ParseThrow();
        if (kw == "break") {
            auto node = std::make_shared<BreakStatement>();
            node->loc = current_.loc;
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->label = current_.lexeme;
                Consume();
            }
            ExpectSymbol(";", "expected ';' after break");
            return node;
        }
        if (kw == "continue") {
            auto node = std::make_shared<ContinueStatement>();
            node->loc = current_.loc;
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                node->label = current_.lexeme;
                Consume();
            }
            ExpectSymbol(";", "expected ';' after continue");
            return node;
        }
        if (kw == "assert") {
            auto node = std::make_shared<AssertStatement>();
            node->loc = current_.loc;
            Consume();
            node->condition = ParseExpression();
            if (MatchSymbol(":")) {
                node->message = ParseExpression();
            }
            ExpectSymbol(";", "expected ';' after assert");
            return node;
        }
        if (kw == "synchronized") {
            auto node = std::make_shared<SynchronizedStatement>();
            node->loc = current_.loc;
            Consume();
            ExpectSymbol("(", "expected '(' after synchronized");
            node->monitor = ParseExpression();
            ExpectSymbol(")", "expected ')' after monitor expression");
            node->body = ParseBlock();
            return node;
        }
        if (kw == "yield") {
            auto node = std::make_shared<YieldStatement>();
            node->loc = current_.loc;
            Consume();
            node->value = ParseExpression();
            ExpectSymbol(";", "expected ';' after yield");
            return node;
        }
        if (kw == "var" || kw == "final") {
            return ParseVarDecl();
        }
    }

    // Variable declaration or expression statement
    // Try variable declaration: type name = ...;
    auto expr = ParseExpression();
    if (expr) {
        auto stmt = std::make_shared<ExprStatement>();
        stmt->loc = expr->loc;
        stmt->expr = expr;
        ExpectSymbol(";", "expected ';' after expression");
        return stmt;
    }
    Consume();
    return std::make_shared<ExprStatement>();
}

std::shared_ptr<BlockStatement> JavaParser::ParseBlock() {
    auto node = std::make_shared<BlockStatement>();
    node->loc = current_.loc;
    ExpectSymbol("{", "expected '{'");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        auto stmt = ParseStatement();
        if (stmt) node->statements.push_back(stmt);
    }
    ExpectSymbol("}", "expected '}'");
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseVarDecl() {
    auto node = std::make_shared<VarDecl>();
    node->loc = current_.loc;

    if (MatchKeyword("final")) node->is_final = true;

    // Type (or 'var' for Java 10+)
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
        node->type = nullptr; // inferred
        Consume();
    } else {
        node->type = ParseType();
    }

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    if (MatchSymbol("=")) {
        node->init = ParseExpression();
    }

    ExpectSymbol(";", "expected ';' after variable declaration");
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseIf() {
    auto node = std::make_shared<IfStatement>();
    node->loc = current_.loc;
    Consume(); // 'if'
    ExpectSymbol("(", "expected '(' after if");
    node->condition = ParseExpression();
    ExpectSymbol(")", "expected ')' after condition");
    node->then_body = ParseStatement();
    if (MatchKeyword("else")) {
        node->else_body = ParseStatement();
    }
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseWhile() {
    auto node = std::make_shared<WhileStatement>();
    node->loc = current_.loc;
    Consume(); // 'while'
    ExpectSymbol("(", "expected '(' after while");
    node->condition = ParseExpression();
    ExpectSymbol(")", "expected ')' after condition");
    node->body = ParseStatement();
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseFor() {
    auto node_loc = current_.loc;
    Consume(); // 'for'
    ExpectSymbol("(", "expected '(' after for");

    // Enhanced for (for-each) check: type name : expr
    // Regular for: init; cond; update
    // We try to detect the enhanced form by looking ahead
    auto stmt = ParseStatement();
    // simplified: treat all for loops as regular for
    auto for_node = std::make_shared<ForStatement>();
    for_node->loc = node_loc;
    for_node->init = stmt;
    for_node->condition = ParseExpression();
    ExpectSymbol(";", "expected ';' after for condition");
    for_node->update = ParseExpression();
    ExpectSymbol(")", "expected ')' after for update");
    for_node->body = ParseStatement();
    return for_node;
}

std::shared_ptr<Statement> JavaParser::ParseSwitch() {
    auto node = std::make_shared<SwitchStatement>();
    node->loc = current_.loc;
    Consume(); // 'switch'
    ExpectSymbol("(", "expected '(' after switch");
    node->selector = ParseExpression();
    ExpectSymbol(")", "expected ')' after switch selector");
    ExpectSymbol("{", "expected '{'");

    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        SwitchStatement::Case c;
        if (MatchKeyword("case")) {
            c.labels.push_back(ParseExpression());
            while (MatchSymbol(",")) {
                c.labels.push_back(ParseExpression());
            }
            if (IsSymbol("->")) {
                c.is_arrow = true;
                Consume();
                c.body.push_back(ParseStatement());
            } else {
                ExpectSymbol(":", "expected ':' after case label");
                while (!IsSymbol("}") &&
                       !(current_.kind == frontends::TokenKind::kKeyword &&
                         (current_.lexeme == "case" || current_.lexeme == "default")) &&
                       current_.kind != frontends::TokenKind::kEndOfFile) {
                    c.body.push_back(ParseStatement());
                }
            }
        } else if (MatchKeyword("default")) {
            if (IsSymbol("->")) {
                c.is_arrow = true;
                Consume();
                c.body.push_back(ParseStatement());
            } else {
                ExpectSymbol(":", "expected ':' after default");
                while (!IsSymbol("}") &&
                       !(current_.kind == frontends::TokenKind::kKeyword &&
                         (current_.lexeme == "case" || current_.lexeme == "default")) &&
                       current_.kind != frontends::TokenKind::kEndOfFile) {
                    c.body.push_back(ParseStatement());
                }
            }
        } else {
            Consume();
            continue;
        }
        node->cases.push_back(c);
    }
    ExpectSymbol("}", "expected '}'");
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseTry() {
    auto node = std::make_shared<TryStatement>();
    node->loc = current_.loc;
    Consume(); // 'try'

    // Try-with-resources (Java 7+)
    if (IsSymbol("(")) {
        Consume();
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            node->resources.push_back(ParseVarDecl());
            MatchSymbol(";"); // optional trailing semicolon
        }
        ExpectSymbol(")", "expected ')' after try resources");
    }

    node->body = ParseBlock();

    while (MatchKeyword("catch")) {
        TryStatement::CatchClause cc;
        ExpectSymbol("(", "expected '(' after catch");
        cc.exception_types.push_back(ParseType());
        while (MatchSymbol("|")) {
            cc.exception_types.push_back(ParseType());
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            cc.var_name = current_.lexeme;
            Consume();
        }
        ExpectSymbol(")", "expected ')' after catch parameter");
        cc.body = ParseBlock();
        node->catches.push_back(cc);
    }

    if (MatchKeyword("finally")) {
        node->finally_body = ParseBlock();
    }

    return node;
}

std::shared_ptr<Statement> JavaParser::ParseReturn() {
    auto node = std::make_shared<ReturnStatement>();
    node->loc = current_.loc;
    Consume(); // 'return'
    if (!IsSymbol(";")) {
        node->value = ParseExpression();
    }
    ExpectSymbol(";", "expected ';' after return");
    return node;
}

std::shared_ptr<Statement> JavaParser::ParseThrow() {
    auto node = std::make_shared<ThrowStatement>();
    node->loc = current_.loc;
    Consume(); // 'throw'
    node->expr = ParseExpression();
    ExpectSymbol(";", "expected ';' after throw");
    return node;
}

// ============================================================================
// Expressions
// ============================================================================

int JavaParser::GetPrecedence(const std::string &op) const {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "^") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
    if (op == "<<" || op == ">>" || op == ">>>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    return 0;
}

std::shared_ptr<Expression> JavaParser::ParseExpression() {
    return ParseTernary();
}

std::shared_ptr<Expression> JavaParser::ParseTernary() {
    auto expr = ParseBinary(1);
    if (IsSymbol("?")) {
        auto node = std::make_shared<TernaryExpression>();
        node->loc = expr->loc;
        node->condition = expr;
        Consume(); // '?'
        node->then_expr = ParseExpression();
        ExpectSymbol(":", "expected ':' in ternary expression");
        node->else_expr = ParseTernary();
        return node;
    }
    // Assignment operators
    if (current_.kind == frontends::TokenKind::kSymbol) {
        auto &op = current_.lexeme;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
            op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
            op == "<<=" || op == ">>=" || op == ">>>=") {
            auto bin = std::make_shared<BinaryExpression>();
            bin->loc = expr->loc;
            bin->op = op;
            bin->left = expr;
            Consume();
            bin->right = ParseTernary();
            return bin;
        }
    }
    return expr;
}

std::shared_ptr<Expression> JavaParser::ParseBinary(int min_precedence) {
    auto left = ParseUnary();
    while (current_.kind == frontends::TokenKind::kSymbol) {
        int prec = GetPrecedence(current_.lexeme);
        if (prec < min_precedence) break;
        auto op = current_.lexeme;
        Consume();
        auto right = ParseBinary(prec + 1);
        auto bin = std::make_shared<BinaryExpression>();
        bin->loc = left->loc;
        bin->op = op;
        bin->left = left;
        bin->right = right;
        left = bin;
    }
    // instanceof expression
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "instanceof") {
        auto inst = std::make_shared<InstanceofExpression>();
        inst->loc = left->loc;
        inst->expr = left;
        Consume();
        inst->type = ParseType();
        // Pattern matching instanceof (Java 16+)
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            inst->pattern_var = current_.lexeme;
            Consume();
        }
        return inst;
    }
    return left;
}

std::shared_ptr<Expression> JavaParser::ParseUnary() {
    if (current_.kind == frontends::TokenKind::kSymbol) {
        auto &op = current_.lexeme;
        if (op == "!" || op == "~" || op == "-" || op == "+" || op == "++" || op == "--") {
            auto node = std::make_shared<UnaryExpression>();
            node->loc = current_.loc;
            node->op = op;
            Consume();
            node->operand = ParseUnary();
            return node;
        }
    }
    // Cast expression: (Type) expr
    if (IsSymbol("(")) {
        // Simplified: we don't distinguish cast from parenthesized expression here
    }
    return ParsePostfix();
}

std::shared_ptr<Expression> JavaParser::ParsePostfix() {
    auto expr = ParsePrimary();

    while (true) {
        if (IsSymbol(".")) {
            Consume();
            auto member = std::make_shared<MemberExpression>();
            member->loc = expr->loc;
            member->object = expr;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                member->member = current_.lexeme;
                Consume();
            }
            expr = member;
        } else if (IsSymbol("[")) {
            Consume();
            auto index = std::make_shared<ArrayAccessExpression>();
            index->loc = expr->loc;
            index->array = expr;
            index->index = ParseExpression();
            ExpectSymbol("]", "expected ']' after array index");
            expr = index;
        } else if (IsSymbol("(")) {
            auto call = std::make_shared<CallExpression>();
            call->loc = expr->loc;
            call->callee = expr;
            Consume();
            while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                call->args.push_back(ParseExpression());
                if (!MatchSymbol(",")) break;
            }
            ExpectSymbol(")", "expected ')' after arguments");
            expr = call;
        } else if (IsSymbol("++") || IsSymbol("--")) {
            auto unary = std::make_shared<UnaryExpression>();
            unary->loc = current_.loc;
            unary->op = current_.lexeme;
            unary->operand = expr;
            unary->postfix = true;
            Consume();
            expr = unary;
        } else if (IsSymbol("::")) {
            // Method reference (Java 8+)
            Consume();
            auto ref = std::make_shared<MethodReferenceExpression>();
            ref->loc = expr->loc;
            ref->object = expr;
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "new")) {
                ref->method_name = current_.lexeme;
                Consume();
            }
            expr = ref;
        } else {
            break;
        }
    }
    return expr;
}

std::shared_ptr<Expression> JavaParser::ParsePrimary() {
    // Literals
    if (current_.kind == frontends::TokenKind::kNumber ||
        current_.kind == frontends::TokenKind::kString ||
        current_.kind == frontends::TokenKind::kChar) {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->value = current_.lexeme;
        Consume();
        return lit;
    }

    // Boolean and null literals
    if (current_.kind == frontends::TokenKind::kKeyword &&
        (current_.lexeme == "true" || current_.lexeme == "false" || current_.lexeme == "null")) {
        auto lit = std::make_shared<Literal>();
        lit->loc = current_.loc;
        lit->value = current_.lexeme;
        Consume();
        return lit;
    }

    // this
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "this") {
        auto id = std::make_shared<Identifier>();
        id->loc = current_.loc;
        id->name = "this";
        Consume();
        return id;
    }

    // super
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "super") {
        auto id = std::make_shared<Identifier>();
        id->loc = current_.loc;
        id->name = "super";
        Consume();
        return id;
    }

    // new expression
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "new") {
        auto node = std::make_shared<NewExpression>();
        node->loc = current_.loc;
        Consume();
        node->type = ParseType();
        if (IsSymbol("(")) {
            Consume();
            while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                node->args.push_back(ParseExpression());
                if (!MatchSymbol(",")) break;
            }
            ExpectSymbol(")", "expected ')' after constructor arguments");
        } else if (IsSymbol("[")) {
            // Array creation: new int[10]
            Consume();
            node->args.push_back(ParseExpression());
            ExpectSymbol("]", "expected ']' after array size");
        }
        return node;
    }

    // Parenthesized expression
    if (IsSymbol("(")) {
        Consume();
        auto expr = ParseExpression();
        ExpectSymbol(")", "expected ')'");
        return expr;
    }

    // Identifier
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        auto id = std::make_shared<Identifier>();
        id->loc = current_.loc;
        id->name = current_.lexeme;
        Consume();
        return id;
    }

    // Fallback
    auto lit = std::make_shared<Literal>();
    lit->loc = current_.loc;
    lit->value = current_.lexeme;
    Consume();
    return lit;
}

std::shared_ptr<Expression> JavaParser::ParseLambda() {
    // Lambda expressions (Java 8+): (params) -> body
    auto node = std::make_shared<LambdaExpression>();
    node->loc = current_.loc;

    if (IsSymbol("(")) {
        Consume();
        while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
            LambdaExpression::Param p;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                p.name = current_.lexeme;
                Consume();
            }
            node->params.push_back(p);
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol(")", "expected ')' after lambda parameters");
    }

    ExpectSymbol("->", "expected '->' in lambda expression");

    if (IsSymbol("{")) {
        node->body = ParseBlock();
    } else {
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = ParseExpression();
        node->body = expr_stmt;
    }

    return node;
}

// Standalone ParseMethodDecl — parses a method declaration outside a class
// body context.  Used by external tools that need to parse method signatures.
std::shared_ptr<MethodDecl> JavaParser::ParseMethodDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    // Parse optional modifiers
    bool is_static = false, is_final = false, is_abstract = false;
    bool is_synchronized = false, is_native = false, is_default = false;
    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "final") { is_final = true; Consume(); }
        else if (kw == "abstract") { is_abstract = true; Consume(); }
        else if (kw == "synchronized") { is_synchronized = true; Consume(); }
        else if (kw == "native") { is_native = true; Consume(); }
        else if (kw == "default") { is_default = true; Consume(); }
        else break;
    }

    auto type_params = ParseTypeParameters();
    auto return_type = ParseType();
    if (!return_type) return nullptr;

    if (current_.kind != frontends::TokenKind::kIdentifier) return nullptr;
    std::string name = current_.lexeme;
    Consume();

    if (!IsSymbol("(")) return nullptr;

    auto method = std::make_shared<MethodDecl>();
    method->loc = return_type->loc;
    method->name = name;
    method->return_type = return_type;
    method->access = access;
    method->is_static = is_static;
    method->is_final = is_final;
    method->is_abstract = is_abstract;
    method->is_synchronized = is_synchronized;
    method->is_native = is_native;
    method->is_default = is_default;
    method->annotations = annotations;
    method->type_params = type_params;
    method->params = ParseParameters();

    if (MatchKeyword("throws")) {
        do {
            method->throws_types.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    if (IsSymbol("{")) {
        auto block = ParseBlock();
        method->body = block->statements;
    } else {
        ExpectSymbol(";", "expected ';' after method declaration");
    }

    return method;
}

// Standalone ParseFieldDecl — parses a field declaration outside a class body.
std::shared_ptr<FieldDecl> JavaParser::ParseFieldDecl(
    const std::string &access, const std::vector<Annotation> &annotations) {
    bool is_static = false, is_final = false;
    bool is_volatile = false, is_transient = false;
    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "final") { is_final = true; Consume(); }
        else if (kw == "volatile") { is_volatile = true; Consume(); }
        else if (kw == "transient") { is_transient = true; Consume(); }
        else break;
    }

    auto type = ParseType();
    if (!type) return nullptr;

    if (current_.kind != frontends::TokenKind::kIdentifier) return nullptr;
    std::string name = current_.lexeme;
    Consume();

    auto field = std::make_shared<FieldDecl>();
    field->loc = type->loc;
    field->name = name;
    field->type = type;
    field->access = access;
    field->is_static = is_static;
    field->is_final = is_final;
    field->is_volatile = is_volatile;
    field->is_transient = is_transient;
    field->annotations = annotations;

    if (MatchSymbol("=")) {
        field->init = ParseExpression();
    }
    ExpectSymbol(";", "expected ';' after field declaration");

    return field;
}

} // namespace polyglot::java
