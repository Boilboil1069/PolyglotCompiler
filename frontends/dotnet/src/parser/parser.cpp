#include "frontends/dotnet/include/dotnet_parser.h"

namespace polyglot::dotnet {

void DotnetParser::Advance() {
    current_ = lexer_.NextToken();
}

frontends::Token DotnetParser::Consume() {
    Advance();
    while (current_.kind == frontends::TokenKind::kComment) {
        Advance();
    }
    return current_;
}

bool DotnetParser::IsSymbol(const std::string &symbol) const {
    return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == symbol;
}

bool DotnetParser::MatchSymbol(const std::string &symbol) {
    if (IsSymbol(symbol)) { Consume(); return true; }
    return false;
}

bool DotnetParser::MatchKeyword(const std::string &keyword) {
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == keyword) {
        Consume(); return true;
    }
    return false;
}

void DotnetParser::ExpectSymbol(const std::string &symbol, const std::string &msg) {
    if (!MatchSymbol(symbol)) diagnostics_.Report(current_.loc, msg);
}

void DotnetParser::Sync() {
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (IsSymbol(";") || IsSymbol("{") || IsSymbol("}")) return;
        if (current_.kind == frontends::TokenKind::kKeyword) {
            auto &kw = current_.lexeme;
            if (kw == "class" || kw == "struct" || kw == "interface" || kw == "enum" ||
                kw == "namespace" || kw == "using" || kw == "public" ||
                kw == "private" || kw == "protected" || kw == "internal") return;
        }
        Consume();
    }
}

std::string DotnetParser::ParseQualifiedName() {
    std::string name;
    if (current_.kind == frontends::TokenKind::kIdentifier ||
        current_.kind == frontends::TokenKind::kKeyword) {
        name = current_.lexeme;
        Consume();
        while (IsSymbol(".")) {
            Consume();
            if (current_.kind == frontends::TokenKind::kIdentifier ||
                current_.kind == frontends::TokenKind::kKeyword) {
                name += "." + current_.lexeme;
                Consume();
            } else break;
        }
    }
    return name;
}

// ============================================================================
// Top-Level Parsing
// ============================================================================

void DotnetParser::ParseModule() {
    module_ = std::make_shared<Module>();
    Consume();

    // using directives and top-level declarations
    while (current_.kind != frontends::TokenKind::kEndOfFile) {
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
            module_->usings.push_back(ParseUsingDirective());
        } else {
            ParseTopLevel();
        }
    }
}

std::shared_ptr<Module> DotnetParser::TakeModule() {
    return std::move(module_);
}

std::shared_ptr<UsingDirective> DotnetParser::ParseUsingDirective() {
    auto node = std::make_shared<UsingDirective>();
    node->loc = current_.loc;
    Consume(); // 'using'

    if (MatchKeyword("global")) node->is_global = true;
    if (MatchKeyword("static")) node->is_static = true;

    // Check for alias: name = namespace;
    std::string first = ParseQualifiedName();
    if (IsSymbol("=")) {
        Consume();
        node->alias = first;
        node->ns = ParseQualifiedName();
    } else {
        node->ns = first;
    }

    ExpectSymbol(";", "expected ';' after using directive");
    return node;
}

std::vector<Attribute> DotnetParser::ParseAttributes() {
    std::vector<Attribute> attrs;
    while (IsSymbol("[")) {
        Consume();
        Attribute a;
        a.name = ParseQualifiedName();
        if (IsSymbol("(")) {
            Consume();
            int depth = 1;
            while (depth > 0 && current_.kind != frontends::TokenKind::kEndOfFile) {
                if (IsSymbol("(")) depth++;
                else if (IsSymbol(")")) depth--;
                if (depth > 0) Consume();
            }
            if (IsSymbol(")")) Consume();
        }
        attrs.push_back(a);
        ExpectSymbol("]", "expected ']' after attribute");
    }
    return attrs;
}

std::string DotnetParser::ParseAccessModifier() {
    if (MatchKeyword("public"))    return "public";
    if (MatchKeyword("private"))   return "private";
    if (MatchKeyword("protected")) return "protected";
    if (MatchKeyword("internal"))  return "internal";
    return "";
}

void DotnetParser::ParseTopLevel() {
    auto attrs = ParseAttributes();
    std::string access = ParseAccessModifier();

    // Additional access: protected internal, private protected
    if (access == "protected" && current_.kind == frontends::TokenKind::kKeyword &&
        current_.lexeme == "internal") {
        access = "protected internal";
        Consume();
    } else if (access == "private" && current_.kind == frontends::TokenKind::kKeyword &&
               current_.lexeme == "protected") {
        access = "private protected";
        Consume();
    }

    // Consume modifiers
    bool is_abstract = false, is_sealed = false, is_static = false;
    bool is_partial = false, is_readonly = false, is_ref = false;
    bool is_file = false, is_record = false;

    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "abstract")     { is_abstract = true; Consume(); }
        else if (kw == "sealed")  { is_sealed = true; Consume(); }
        else if (kw == "static")  { is_static = true; Consume(); }
        else if (kw == "partial") { is_partial = true; Consume(); }
        else if (kw == "readonly"){ is_readonly = true; Consume(); }
        else if (kw == "ref")     { is_ref = true; Consume(); }
        else if (kw == "file")    { is_file = true; Consume(); }
        else if (kw == "record")  { is_record = true; break; }
        else if (kw == "new" || kw == "unsafe") { Consume(); }
        else break;
    }

    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "namespace") {
        module_->declarations.push_back(ParseNamespaceDecl());
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class") {
        auto cls = ParseClassDecl(access, attrs);
        cls->is_abstract = is_abstract;
        cls->is_sealed = is_sealed;
        cls->is_static = is_static;
        cls->is_partial = is_partial;
        cls->is_file_scoped = is_file;
        module_->declarations.push_back(cls);
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "struct") {
        auto st = ParseStructDecl(access, attrs);
        st->is_readonly = is_readonly;
        st->is_ref = is_ref;
        st->is_partial = is_partial;
        module_->declarations.push_back(st);
    } else if (is_record) {
        // record class or record struct
        Consume(); // 'record'
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "struct") {
            auto st = ParseStructDecl(access, attrs);
            st->is_record = true;
            st->is_readonly = is_readonly;
            module_->declarations.push_back(st);
        } else {
            if (MatchKeyword("class")) { /* record class */ }
            auto cls = ParseClassDecl(access, attrs);
            cls->is_record = true;
            module_->declarations.push_back(cls);
        }
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "interface") {
        auto iface = ParseInterfaceDecl(access, attrs);
        iface->is_partial = is_partial;
        module_->declarations.push_back(iface);
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "enum") {
        module_->declarations.push_back(ParseEnumDecl(access, attrs));
    } else if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "delegate") {
        module_->declarations.push_back(ParseDelegateDecl(access, attrs));
    } else {
        // Top-level statement (C# 9.0+) or skip
        if (current_.kind != frontends::TokenKind::kEndOfFile) {
            auto stmt = ParseStatement();
            if (stmt) module_->top_level_statements.push_back(stmt);
        }
    }
}

std::shared_ptr<NamespaceDecl> DotnetParser::ParseNamespaceDecl() {
    auto node = std::make_shared<NamespaceDecl>();
    node->loc = current_.loc;
    Consume(); // 'namespace'
    node->name = ParseQualifiedName();

    // File-scoped namespace (C# 10): namespace Foo;
    if (IsSymbol(";")) {
        node->is_file_scoped = true;
        Consume();
        while (current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
                module_->usings.push_back(ParseUsingDirective());
            } else {
                ParseTopLevel();
                // Move last top-level decl into namespace
                if (!module_->declarations.empty()) {
                    node->members.push_back(module_->declarations.back());
                    module_->declarations.pop_back();
                }
            }
        }
    } else {
        ExpectSymbol("{", "expected '{' after namespace name");
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "using") {
                module_->usings.push_back(ParseUsingDirective());
            } else {
                auto saved_size = module_->declarations.size();
                ParseTopLevel();
                while (module_->declarations.size() > saved_size) {
                    node->members.push_back(module_->declarations.back());
                    module_->declarations.pop_back();
                }
            }
        }
        ExpectSymbol("}", "expected '}'");
    }

    return node;
}

// ============================================================================
// Type Declarations
// ============================================================================

std::shared_ptr<ClassDecl> DotnetParser::ParseClassDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    auto node = std::make_shared<ClassDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->attributes = attrs;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "class")
        Consume();

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    node->type_params = ParseTypeParameters();

    // Primary constructor parameters (C# 12)
    if (IsSymbol("(")) {
        node->primary_ctor_params = ParseParameters();
    }

    // Base type / interfaces
    if (IsSymbol(":")) {
        Consume();
        auto first = ParseType();
        node->base_type = first;
        while (MatchSymbol(",")) {
            node->interfaces.push_back(ParseType());
        }
    }

    // Type parameter constraints
    while (MatchKeyword("where")) {
        ParseQualifiedName(); // T
        ExpectSymbol(":", "expected ':' after where");
        while (!IsSymbol("{") && !IsSymbol(";") &&
               !(current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "where") &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            Consume();
        }
    }

    // Class body
    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto member_attrs = ParseAttributes();
            std::string member_access = ParseAccessModifier();

            bool member_static = false, member_virtual = false, member_override = false;
            bool member_abstract = false, member_sealed = false, member_readonly = false;
            bool member_async = false, member_new = false, member_extern = false;
            bool member_partial = false, member_volatile = false, member_const = false;
            bool member_required = false;

            while (current_.kind == frontends::TokenKind::kKeyword) {
                auto &kw = current_.lexeme;
                if (kw == "static")        { member_static = true; Consume(); }
                else if (kw == "virtual")  { member_virtual = true; Consume(); }
                else if (kw == "override") { member_override = true; Consume(); }
                else if (kw == "abstract") { member_abstract = true; Consume(); }
                else if (kw == "sealed")   { member_sealed = true; Consume(); }
                else if (kw == "readonly") { member_readonly = true; Consume(); }
                else if (kw == "async")    { member_async = true; Consume(); }
                else if (kw == "new")      { member_new = true; Consume(); }
                else if (kw == "extern")   { member_extern = true; Consume(); }
                else if (kw == "partial")  { member_partial = true; Consume(); }
                else if (kw == "volatile") { member_volatile = true; Consume(); }
                else if (kw == "const")    { member_const = true; Consume(); }
                else if (kw == "unsafe")   { Consume(); }
                else if (kw == "required") { member_required = true; Consume(); }
                else break;
            }

            // Nested type declarations
            if (current_.kind == frontends::TokenKind::kKeyword &&
                (current_.lexeme == "class" || current_.lexeme == "struct" ||
                 current_.lexeme == "interface" || current_.lexeme == "enum" ||
                 current_.lexeme == "delegate" || current_.lexeme == "record")) {
                auto &kw = current_.lexeme;
                if (kw == "class") {
                    node->members.push_back(ParseClassDecl(member_access, member_attrs));
                } else if (kw == "struct") {
                    node->members.push_back(ParseStructDecl(member_access, member_attrs));
                } else if (kw == "interface") {
                    node->members.push_back(ParseInterfaceDecl(member_access, member_attrs));
                } else if (kw == "enum") {
                    node->members.push_back(ParseEnumDecl(member_access, member_attrs));
                } else if (kw == "delegate") {
                    node->members.push_back(ParseDelegateDecl(member_access, member_attrs));
                } else if (kw == "record") {
                    Consume();
                    auto inner = ParseClassDecl(member_access, member_attrs);
                    inner->is_record = true;
                    node->members.push_back(inner);
                }
                continue;
            }

            // Destructor
            if (IsSymbol("~")) {
                Consume();
                auto dtor = ParseDestructorDecl(node->name);
                node->members.push_back(dtor);
                continue;
            }

            // Constructor check
            if (current_.kind == frontends::TokenKind::kIdentifier &&
                current_.lexeme == node->name && !member_static) {
                auto ctor = ParseConstructorDecl(member_access, node->name);
                ctor->attributes = member_attrs;
                node->members.push_back(ctor);
                continue;
            }

            // Static constructor
            if (member_static && current_.kind == frontends::TokenKind::kIdentifier &&
                current_.lexeme == node->name) {
                auto ctor = ParseConstructorDecl(member_access, node->name);
                ctor->is_static = true;
                node->members.push_back(ctor);
                continue;
            }

            // Type for method/field/property
            auto type = ParseType();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                std::string name = current_.lexeme;
                Consume();

                if (IsSymbol("(")) {
                    // Method
                    auto method = std::make_shared<MethodDecl>();
                    method->loc = type->loc;
                    method->name = name;
                    method->return_type = type;
                    method->access = member_access;
                    method->is_static = member_static;
                    method->is_virtual = member_virtual;
                    method->is_override = member_override;
                    method->is_abstract = member_abstract;
                    method->is_sealed = member_sealed;
                    method->is_async = member_async;
                    method->is_new = member_new;
                    method->is_extern = member_extern;
                    method->is_partial = member_partial;
                    method->attributes = member_attrs;
                    method->params = ParseParameters();

                    // where constraints
                    while (MatchKeyword("where")) {
                        ParseQualifiedName();
                        ExpectSymbol(":", "expected ':'");
                        while (!IsSymbol("{") && !IsSymbol(";") && !IsSymbol("=>") &&
                               !(current_.kind == frontends::TokenKind::kKeyword &&
                                 current_.lexeme == "where") &&
                               current_.kind != frontends::TokenKind::kEndOfFile) {
                            Consume();
                        }
                    }

                    // Expression body: => expr;
                    if (IsSymbol("=>")) {
                        Consume();
                        method->expression_body = ParseExpression();
                        ExpectSymbol(";", "expected ';'");
                    } else if (IsSymbol("{")) {
                        auto block = ParseBlock();
                        method->body = block->statements;
                    } else {
                        ExpectSymbol(";", "expected ';'");
                    }
                    node->members.push_back(method);
                } else if (IsSymbol("{") || IsSymbol("=>")) {
                    // Property
                    auto prop = std::make_shared<PropertyDecl>();
                    prop->loc = type->loc;
                    prop->name = name;
                    prop->type = type;
                    prop->access = member_access;
                    prop->is_static = member_static;
                    prop->is_virtual = member_virtual;
                    prop->is_override = member_override;
                    prop->is_abstract = member_abstract;
                    prop->is_required = member_required;
                    prop->attributes = member_attrs;

                    if (IsSymbol("=>")) {
                        Consume();
                        prop->expression_body = ParseExpression();
                        ExpectSymbol(";", "expected ';'");
                        prop->has_getter = true;
                    } else {
                        Consume(); // '{'
                        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
                            if (MatchKeyword("get")) {
                                prop->has_getter = true;
                                if (IsSymbol(";")) Consume();
                                else if (IsSymbol("{")) ParseBlock();
                                else if (IsSymbol("=>")) { Consume(); ParseExpression(); ExpectSymbol(";", ""); }
                            } else if (MatchKeyword("set")) {
                                prop->has_setter = true;
                                if (IsSymbol(";")) Consume();
                                else if (IsSymbol("{")) ParseBlock();
                                else if (IsSymbol("=>")) { Consume(); ParseExpression(); ExpectSymbol(";", ""); }
                            } else if (MatchKeyword("init")) {
                                prop->is_init_only = true;
                                prop->has_setter = true;
                                if (IsSymbol(";")) Consume();
                                else if (IsSymbol("{")) ParseBlock();
                                else if (IsSymbol("=>")) { Consume(); ParseExpression(); ExpectSymbol(";", ""); }
                            } else {
                                Consume();
                            }
                        }
                        ExpectSymbol("}", "expected '}'");
                    }

                    // Default value
                    if (MatchSymbol("=")) {
                        prop->init = ParseExpression();
                        ExpectSymbol(";", "expected ';'");
                    }

                    node->members.push_back(prop);
                } else {
                    // Field
                    auto field = std::make_shared<FieldDecl>();
                    field->loc = type->loc;
                    field->name = name;
                    field->type = type;
                    field->access = member_access;
                    field->is_static = member_static;
                    field->is_readonly = member_readonly;
                    field->is_const = member_const;
                    field->is_volatile = member_volatile;
                    field->is_required = member_required;
                    field->attributes = member_attrs;

                    if (MatchSymbol("=")) {
                        field->init = ParseExpression();
                    }
                    ExpectSymbol(";", "expected ';'");
                    node->members.push_back(field);
                }
            } else {
                Sync();
                if (IsSymbol(";")) Consume();
            }
        }
        ExpectSymbol("}", "expected '}'");
    } else if (IsSymbol(";")) {
        Consume(); // record Point(int X, int Y);
    }

    return node;
}

std::shared_ptr<StructDecl> DotnetParser::ParseStructDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    auto node = std::make_shared<StructDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->attributes = attrs;
    Consume(); // 'struct'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    node->type_params = ParseTypeParameters();

    if (IsSymbol("(")) {
        node->primary_ctor_params = ParseParameters();
    }

    if (IsSymbol(":")) {
        Consume();
        do {
            node->interfaces.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    // Constraints
    while (MatchKeyword("where")) {
        ParseQualifiedName();
        ExpectSymbol(":", "");
        while (!IsSymbol("{") && !IsSymbol(";") && current_.kind != frontends::TokenKind::kEndOfFile) Consume();
    }

    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            Consume(); // simplified: skip struct body
        }
        ExpectSymbol("}", "expected '}'");
    } else if (IsSymbol(";")) {
        Consume();
    }

    return node;
}

std::shared_ptr<InterfaceDecl> DotnetParser::ParseInterfaceDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    auto node = std::make_shared<InterfaceDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->attributes = attrs;
    Consume(); // 'interface'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    node->type_params = ParseTypeParameters();

    if (IsSymbol(":")) {
        Consume();
        do {
            node->extends_types.push_back(ParseType());
        } while (MatchSymbol(","));
    }

    while (MatchKeyword("where")) {
        ParseQualifiedName();
        ExpectSymbol(":", "");
        while (!IsSymbol("{") && current_.kind != frontends::TokenKind::kEndOfFile) Consume();
    }

    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            auto ma = ParseAttributes();
            auto acc = ParseAccessModifier();
            (void)ma; (void)acc;
            Sync();
            if (IsSymbol(";")) Consume();
            else if (IsSymbol("}")) break;
            else Consume();
        }
        ExpectSymbol("}", "expected '}'");
    }

    return node;
}

std::shared_ptr<EnumDecl> DotnetParser::ParseEnumDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    auto node = std::make_shared<EnumDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->attributes = attrs;
    Consume(); // 'enum'

    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }

    if (IsSymbol(":")) {
        Consume();
        node->underlying_type = ParseType();
    }

    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            EnumDecl::EnumMember em;
            em.attributes = ParseAttributes();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                em.name = current_.lexeme;
                Consume();
            }
            if (MatchSymbol("=")) {
                em.value = ParseExpression();
            }
            node->members.push_back(em);
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol("}", "expected '}'");
    }

    return node;
}

std::shared_ptr<DelegateDecl> DotnetParser::ParseDelegateDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    auto node = std::make_shared<DelegateDecl>();
    node->loc = current_.loc;
    node->access = access;
    node->attributes = attrs;
    Consume(); // 'delegate'

    node->return_type = ParseType();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->name = current_.lexeme;
        Consume();
    }
    node->type_params = ParseTypeParameters();
    node->params = ParseParameters();
    ExpectSymbol(";", "expected ';'");
    return node;
}

// ============================================================================
// Members
// ============================================================================

std::shared_ptr<ConstructorDecl> DotnetParser::ParseConstructorDecl(
    const std::string &access, const std::string &class_name) {
    auto node = std::make_shared<ConstructorDecl>();
    node->loc = current_.loc;
    node->name = class_name;
    node->access = access;
    Consume(); // constructor name

    node->params = ParseParameters();

    // Base/this initializer
    if (IsSymbol(":")) {
        Consume();
        if (MatchKeyword("base")) node->initializer_kind = "base";
        else if (MatchKeyword("this")) node->initializer_kind = "this";
        if (IsSymbol("(")) {
            Consume();
            while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                node->initializer_args.push_back(ParseExpression());
                if (!MatchSymbol(",")) break;
            }
            ExpectSymbol(")", "expected ')'");
        }
    }

    if (IsSymbol("{")) {
        auto block = ParseBlock();
        node->body = block->statements;
    } else if (IsSymbol("=>")) {
        Consume();
        auto stmt = std::make_shared<ExprStatement>();
        stmt->expr = ParseExpression();
        node->body.push_back(stmt);
        ExpectSymbol(";", "expected ';'");
    }

    return node;
}

std::shared_ptr<DestructorDecl> DotnetParser::ParseDestructorDecl(const std::string &class_name) {
    auto node = std::make_shared<DestructorDecl>();
    node->loc = current_.loc;
    node->name = "~" + class_name;

    if (current_.kind == frontends::TokenKind::kIdentifier) Consume();
    ExpectSymbol("(", "expected '('");
    ExpectSymbol(")", "expected ')'");

    if (IsSymbol("{")) {
        auto block = ParseBlock();
        node->body = block->statements;
    }

    return node;
}

std::vector<TypeParameter> DotnetParser::ParseTypeParameters() {
    std::vector<TypeParameter> params;
    if (!IsSymbol("<")) return params;
    Consume();
    while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
        TypeParameter tp;
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            tp.name = current_.lexeme;
            Consume();
        }
        params.push_back(tp);
        if (!MatchSymbol(",")) break;
    }
    ExpectSymbol(">", "expected '>'");
    return params;
}

std::vector<Parameter> DotnetParser::ParseParameters() {
    std::vector<Parameter> params;
    ExpectSymbol("(", "expected '('");
    while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
        Parameter p;
        p.attributes = ParseAttributes();
        if (MatchKeyword("this"))   p.is_this = true;
        if (MatchKeyword("ref"))    p.is_ref = true;
        if (MatchKeyword("out"))    p.is_out = true;
        if (MatchKeyword("in"))     p.is_in = true;
        if (MatchKeyword("params")) p.is_params = true;

        p.type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            p.name = current_.lexeme;
            Consume();
        }
        if (MatchSymbol("=")) {
            p.default_value = ParseExpression();
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

std::shared_ptr<TypeNode> DotnetParser::ParseType() {
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
        Consume();
        while (!IsSymbol(">") && current_.kind != frontends::TokenKind::kEndOfFile) {
            gen->type_args.push_back(ParseTypeArgument());
            if (!MatchSymbol(",")) break;
        }
        ExpectSymbol(">", "expected '>'");

        // Nullable
        if (IsSymbol("?")) {
            Consume();
            auto nullable = std::make_shared<NullableType>();
            nullable->loc = gen->loc;
            nullable->inner = gen;
            return nullable;
        }

        // Array
        if (IsSymbol("[")) {
            Consume();
            ExpectSymbol("]", "expected ']'");
            auto arr = std::make_shared<ArrayType>();
            arr->loc = gen->loc;
            arr->element_type = gen;
            return arr;
        }

        return gen;
    }

    // Nullable
    if (IsSymbol("?")) {
        Consume();
        auto nullable = std::make_shared<NullableType>();
        nullable->loc = node->loc;
        nullable->inner = node;
        return nullable;
    }

    // Array
    if (IsSymbol("[")) {
        Consume();
        ExpectSymbol("]", "expected ']'");
        auto arr = std::make_shared<ArrayType>();
        arr->loc = node->loc;
        arr->element_type = node;
        return arr;
    }

    return node;
}

std::shared_ptr<TypeNode> DotnetParser::ParseTypeArgument() {
    return ParseType();
}

// ============================================================================
// Statements
// ============================================================================

std::shared_ptr<Statement> DotnetParser::ParseStatement() {
    if (IsSymbol("{")) return ParseBlock();
    if (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "if")       return ParseIf();
        if (kw == "while")    return ParseWhile();
        if (kw == "for")      return ParseFor();
        if (kw == "foreach")  return ParseForEach();
        if (kw == "switch")   return ParseSwitch();
        if (kw == "try")      return ParseTry();
        if (kw == "return")   return ParseReturn();
        if (kw == "throw")    return ParseThrow();
        if (kw == "using")    return ParseUsing();
        if (kw == "lock")     return ParseLock();
        if (kw == "var" || kw == "const") return ParseVarDecl();
        if (kw == "break") {
            auto n = std::make_shared<BreakStatement>();
            n->loc = current_.loc;
            Consume();
            ExpectSymbol(";", "expected ';'");
            return n;
        }
        if (kw == "continue") {
            auto n = std::make_shared<ContinueStatement>();
            n->loc = current_.loc;
            Consume();
            ExpectSymbol(";", "expected ';'");
            return n;
        }
        if (kw == "yield") {
            auto n = std::make_shared<YieldStatement>();
            n->loc = current_.loc;
            Consume();
            if (MatchKeyword("break")) {
                n->is_break = true;
            } else if (MatchKeyword("return")) {
                n->value = ParseExpression();
            }
            ExpectSymbol(";", "expected ';'");
            return n;
        }
        if (kw == "checked" || kw == "unchecked") {
            auto n = std::make_shared<CheckedStatement>();
            n->loc = current_.loc;
            n->is_unchecked = (kw == "unchecked");
            Consume();
            n->body = ParseBlock();
            return n;
        }
        if (kw == "do") {
            auto n = std::make_shared<DoWhileStatement>();
            n->loc = current_.loc;
            Consume();
            n->body = ParseStatement();
            ExpectSymbol("while", "expected 'while'");
            ExpectSymbol("(", "expected '('");
            n->condition = ParseExpression();
            ExpectSymbol(")", "expected ')'");
            ExpectSymbol(";", "expected ';'");
            return n;
        }
    }

    auto expr = ParseExpression();
    if (expr) {
        auto stmt = std::make_shared<ExprStatement>();
        stmt->loc = expr->loc;
        stmt->expr = expr;
        ExpectSymbol(";", "expected ';'");
        return stmt;
    }
    Consume();
    return std::make_shared<ExprStatement>();
}

std::shared_ptr<BlockStatement> DotnetParser::ParseBlock() {
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

std::shared_ptr<Statement> DotnetParser::ParseVarDecl() {
    auto node = std::make_shared<VarDecl>();
    node->loc = current_.loc;
    if (MatchKeyword("const")) node->is_const = true;
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
        node->type = nullptr;
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
    ExpectSymbol(";", "expected ';'");
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseIf() {
    auto node = std::make_shared<IfStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->condition = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    node->then_body = ParseStatement();
    if (MatchKeyword("else")) node->else_body = ParseStatement();
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseWhile() {
    auto node = std::make_shared<WhileStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->condition = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    node->body = ParseStatement();
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseFor() {
    auto node = std::make_shared<ForStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->init = ParseStatement();
    node->condition = ParseExpression();
    ExpectSymbol(";", "expected ';'");
    node->update = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    node->body = ParseStatement();
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseForEach() {
    auto node = std::make_shared<ForEachStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->var_type = ParseType();
    if (current_.kind == frontends::TokenKind::kIdentifier) {
        node->var_name = current_.lexeme;
        Consume();
    }
    if (MatchKeyword("in")) {
        node->iterable = ParseExpression();
    }
    ExpectSymbol(")", "expected ')'");
    node->body = ParseStatement();
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseSwitch() {
    auto node = std::make_shared<SwitchStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->governing = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    ExpectSymbol("{", "expected '{'");
    while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
        SwitchStatement::Section sec;
        while (MatchKeyword("case") || MatchKeyword("default")) {
            if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "default") {
                sec.is_default = true;
            } else {
                sec.labels.push_back(ParseExpression());
            }
            ExpectSymbol(":", "expected ':'");
        }
        while (!IsSymbol("}") &&
               !(current_.kind == frontends::TokenKind::kKeyword &&
                 (current_.lexeme == "case" || current_.lexeme == "default")) &&
               current_.kind != frontends::TokenKind::kEndOfFile) {
            sec.body.push_back(ParseStatement());
        }
        node->sections.push_back(sec);
    }
    ExpectSymbol("}", "expected '}'");
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseTry() {
    auto node = std::make_shared<TryStatement>();
    node->loc = current_.loc;
    Consume();
    node->body = ParseBlock();

    while (MatchKeyword("catch")) {
        TryStatement::CatchClause cc;
        if (IsSymbol("(")) {
            Consume();
            cc.exception_type = ParseType();
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                cc.var_name = current_.lexeme;
                Consume();
            }
            ExpectSymbol(")", "expected ')'");
            if (MatchKeyword("when")) {
                ExpectSymbol("(", "expected '('");
                cc.filter = ParseExpression();
                ExpectSymbol(")", "expected ')'");
            }
        }
        cc.body = ParseBlock();
        node->catches.push_back(cc);
    }

    if (MatchKeyword("finally")) {
        node->finally_body = ParseBlock();
    }

    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseReturn() {
    auto node = std::make_shared<ReturnStatement>();
    node->loc = current_.loc;
    Consume();
    if (!IsSymbol(";")) node->value = ParseExpression();
    ExpectSymbol(";", "expected ';'");
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseThrow() {
    auto node = std::make_shared<ThrowStatement>();
    node->loc = current_.loc;
    Consume();
    if (!IsSymbol(";")) node->expr = ParseExpression();
    ExpectSymbol(";", "expected ';'");
    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseUsing() {
    auto node = std::make_shared<UsingStatement>();
    node->loc = current_.loc;
    Consume();

    if (MatchKeyword("await")) node->is_await = true;

    if (IsSymbol("(")) {
        Consume();
        auto var = std::make_shared<VarDecl>();
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
            Consume();
        } else {
            var->type = ParseType();
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            var->name = current_.lexeme;
            Consume();
        }
        if (MatchSymbol("=")) var->init = ParseExpression();
        node->declaration = var;
        ExpectSymbol(")", "expected ')'");
        node->body = ParseStatement();
    } else {
        auto var = std::make_shared<VarDecl>();
        if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "var") {
            Consume();
        } else {
            var->type = ParseType();
        }
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            var->name = current_.lexeme;
            Consume();
        }
        if (MatchSymbol("=")) var->init = ParseExpression();
        node->declaration = var;
        ExpectSymbol(";", "expected ';'");
    }

    return node;
}

std::shared_ptr<Statement> DotnetParser::ParseLock() {
    auto node = std::make_shared<LockStatement>();
    node->loc = current_.loc;
    Consume();
    ExpectSymbol("(", "expected '('");
    node->expr = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    node->body = ParseStatement();
    return node;
}

// ============================================================================
// Expressions
// ============================================================================

int DotnetParser::GetPrecedence(const std::string &op) const {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|")  return 3;
    if (op == "^")  return 4;
    if (op == "&")  return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
    if (op == "<<" || op == ">>" || op == ">>>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    return 0;
}

std::shared_ptr<Expression> DotnetParser::ParseExpression() {
    return ParseTernary();
}

std::shared_ptr<Expression> DotnetParser::ParseTernary() {
    auto expr = ParseNullCoalescing();
    if (IsSymbol("?")) {
        auto node = std::make_shared<TernaryExpression>();
        node->loc = expr->loc;
        node->condition = expr;
        Consume();
        node->then_expr = ParseExpression();
        ExpectSymbol(":", "expected ':'");
        node->else_expr = ParseTernary();
        return node;
    }
    // Assignment
    if (current_.kind == frontends::TokenKind::kSymbol) {
        auto &op = current_.lexeme;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
            op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
            op == "<<=" || op == ">>=" || op == "\?\?=") {
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

std::shared_ptr<Expression> DotnetParser::ParseNullCoalescing() {
    auto left = ParseBinary(1);
    if (IsSymbol("??")) {
        auto node = std::make_shared<NullCoalescingExpression>();
        node->loc = left->loc;
        node->left = left;
        Consume();
        node->right = ParseNullCoalescing();
        return node;
    }
    return left;
}

std::shared_ptr<Expression> DotnetParser::ParseBinary(int min_prec) {
    auto left = ParseUnary();
    while (current_.kind == frontends::TokenKind::kSymbol) {
        int prec = GetPrecedence(current_.lexeme);
        if (prec < min_prec) break;
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
    // is / as expressions
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "is") {
        auto node = std::make_shared<IsExpression>();
        node->loc = left->loc;
        node->expr = left;
        Consume();
        node->type = ParseType();
        if (current_.kind == frontends::TokenKind::kIdentifier) {
            node->pattern_var = current_.lexeme;
            Consume();
        }
        return node;
    }
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "as") {
        auto node = std::make_shared<AsExpression>();
        node->loc = left->loc;
        node->expr = left;
        Consume();
        node->type = ParseType();
        return node;
    }
    return left;
}

std::shared_ptr<Expression> DotnetParser::ParseUnary() {
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
    if (current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == "await") {
        auto node = std::make_shared<AwaitExpression>();
        node->loc = current_.loc;
        Consume();
        node->operand = ParseUnary();
        return node;
    }
    return ParsePostfix();
}

std::shared_ptr<Expression> DotnetParser::ParsePostfix() {
    auto expr = ParsePrimary();

    while (true) {
        if (IsSymbol(".") || IsSymbol("?.")) {
            bool null_cond = IsSymbol("?.");
            Consume();
            auto member = std::make_shared<MemberExpression>();
            member->loc = expr->loc;
            member->object = expr;
            member->null_conditional = null_cond;
            if (current_.kind == frontends::TokenKind::kIdentifier) {
                member->member = current_.lexeme;
                Consume();
            }
            expr = member;
        } else if (IsSymbol("[") || IsSymbol("?[")) {
            bool null_cond = IsSymbol("?[");
            Consume();
            auto index = std::make_shared<IndexExpression>();
            index->loc = expr->loc;
            index->object = expr;
            index->null_conditional = null_cond;
            index->index = ParseExpression();
            ExpectSymbol("]", "expected ']'");
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
            ExpectSymbol(")", "expected ')'");
            expr = call;
        } else if (IsSymbol("++") || IsSymbol("--")) {
            auto u = std::make_shared<UnaryExpression>();
            u->loc = current_.loc;
            u->op = current_.lexeme;
            u->operand = expr;
            u->postfix = true;
            Consume();
            expr = u;
        } else if (IsSymbol("!")) {
            // Null-forgiving operator
            Consume();
            // Null-forgiving is compile-time only; no transformation needed.
        } else {
            break;
        }
    }
    return expr;
}

std::shared_ptr<Expression> DotnetParser::ParsePrimary() {
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

    if (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "true" || kw == "false" || kw == "null") {
            auto lit = std::make_shared<Literal>();
            lit->loc = current_.loc;
            lit->value = kw;
            Consume();
            return lit;
        }
        if (kw == "this" || kw == "base") {
            auto id = std::make_shared<Identifier>();
            id->loc = current_.loc;
            id->name = kw;
            Consume();
            return id;
        }
        if (kw == "new") {
            auto node = std::make_shared<NewExpression>();
            node->loc = current_.loc;
            Consume();
            if (IsSymbol("{")) {
                // new { ... } anonymous type
            } else if (!IsSymbol("(") && !IsSymbol("[")) {
                node->type = ParseType();
            }
            if (IsSymbol("(")) {
                Consume();
                while (!IsSymbol(")") && current_.kind != frontends::TokenKind::kEndOfFile) {
                    node->args.push_back(ParseExpression());
                    if (!MatchSymbol(",")) break;
                }
                ExpectSymbol(")", "expected ')'");
            } else if (IsSymbol("[")) {
                Consume();
                node->args.push_back(ParseExpression());
                ExpectSymbol("]", "expected ']'");
            }
            // Collection/object initializer
            if (IsSymbol("{")) {
                Consume();
                while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
                    node->initializer.push_back(ParseExpression());
                    if (!MatchSymbol(",")) break;
                }
                ExpectSymbol("}", "expected '}'");
            }
            return node;
        }
        if (kw == "typeof") {
            auto node = std::make_shared<TypeofExpression>();
            node->loc = current_.loc;
            Consume();
            ExpectSymbol("(", "expected '('");
            node->type = ParseType();
            ExpectSymbol(")", "expected ')'");
            return node;
        }
        if (kw == "nameof") {
            auto node = std::make_shared<NameofExpression>();
            node->loc = current_.loc;
            Consume();
            ExpectSymbol("(", "expected '('");
            node->name = ParseQualifiedName();
            ExpectSymbol(")", "expected ')'");
            return node;
        }
        if (kw == "default") {
            auto node = std::make_shared<DefaultExpression>();
            node->loc = current_.loc;
            Consume();
            if (IsSymbol("(")) {
                Consume();
                node->type = ParseType();
                ExpectSymbol(")", "expected ')'");
            }
            return node;
        }
        if (kw == "throw") {
            auto node = std::make_shared<ThrowExpression>();
            node->loc = current_.loc;
            Consume();
            node->operand = ParseUnary();
            return node;
        }
    }

    // Parenthesized expression or tuple
    if (IsSymbol("(")) {
        Consume();
        auto first = ParseExpression();
        if (MatchSymbol(",")) {
            // Tuple expression
            auto tuple = std::make_shared<TupleExpression>();
            tuple->loc = first->loc;
            TupleExpression::Element e1;
            e1.value = first;
            tuple->elements.push_back(e1);
            do {
                TupleExpression::Element e;
                e.value = ParseExpression();
                tuple->elements.push_back(e);
            } while (MatchSymbol(","));
            ExpectSymbol(")", "expected ')'");
            return tuple;
        }
        ExpectSymbol(")", "expected ')'");
        return first;
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

// Standalone ParseMethodDecl — parses a method declaration outside a class body.
std::shared_ptr<MethodDecl> DotnetParser::ParseMethodDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    bool is_static = false, is_virtual = false, is_override = false;
    bool is_abstract = false, is_sealed = false, is_async = false;
    bool is_partial = false, is_extern = false, is_new = false;
    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "virtual") { is_virtual = true; Consume(); }
        else if (kw == "override") { is_override = true; Consume(); }
        else if (kw == "abstract") { is_abstract = true; Consume(); }
        else if (kw == "sealed") { is_sealed = true; Consume(); }
        else if (kw == "async") { is_async = true; Consume(); }
        else if (kw == "partial") { is_partial = true; Consume(); }
        else if (kw == "extern") { is_extern = true; Consume(); }
        else if (kw == "new") { is_new = true; Consume(); }
        else break;
    }

    auto return_type = ParseType();
    if (!return_type) return nullptr;

    if (current_.kind != frontends::TokenKind::kIdentifier) return nullptr;
    std::string name = current_.lexeme;
    Consume();

    auto type_params = ParseTypeParameters();

    if (!IsSymbol("(")) return nullptr;

    auto method = std::make_shared<MethodDecl>();
    method->loc = return_type->loc;
    method->name = name;
    method->return_type = return_type;
    method->access = access;
    method->attributes = attrs;
    method->type_params = type_params;
    method->is_static = is_static;
    method->is_virtual = is_virtual;
    method->is_override = is_override;
    method->is_abstract = is_abstract;
    method->is_sealed = is_sealed;
    method->is_async = is_async;
    method->is_partial = is_partial;
    method->is_extern = is_extern;
    method->is_new = is_new;
    method->params = ParseParameters();

    // Expression-bodied method: => expr;
    if (MatchSymbol("=>")) {
        method->expression_body = ParseExpression();
        ExpectSymbol(";", "expected ';' after expression body");
    } else if (IsSymbol("{")) {
        auto block = ParseBlock();
        method->body = block->statements;
    } else {
        ExpectSymbol(";", "expected ';' after method declaration");
    }

    return method;
}

// Standalone ParseFieldDecl — parses a field declaration outside a class body.
std::shared_ptr<FieldDecl> DotnetParser::ParseFieldDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    bool is_static = false, is_readonly = false, is_const = false;
    bool is_volatile = false, is_required = false;
    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "readonly") { is_readonly = true; Consume(); }
        else if (kw == "const") { is_const = true; Consume(); }
        else if (kw == "volatile") { is_volatile = true; Consume(); }
        else if (kw == "required") { is_required = true; Consume(); }
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
    field->attributes = attrs;
    field->is_static = is_static;
    field->is_readonly = is_readonly;
    field->is_const = is_const;
    field->is_volatile = is_volatile;
    field->is_required = is_required;

    if (MatchSymbol("=")) {
        field->init = ParseExpression();
    }
    ExpectSymbol(";", "expected ';' after field declaration");

    return field;
}

// Standalone ParsePropertyDecl — parses a property declaration outside a class body.
std::shared_ptr<PropertyDecl> DotnetParser::ParsePropertyDecl(
    const std::string &access, const std::vector<Attribute> &attrs) {
    bool is_static = false, is_virtual = false, is_override = false;
    bool is_abstract = false, is_required = false;
    while (current_.kind == frontends::TokenKind::kKeyword) {
        auto &kw = current_.lexeme;
        if (kw == "static") { is_static = true; Consume(); }
        else if (kw == "virtual") { is_virtual = true; Consume(); }
        else if (kw == "override") { is_override = true; Consume(); }
        else if (kw == "abstract") { is_abstract = true; Consume(); }
        else if (kw == "required") { is_required = true; Consume(); }
        else break;
    }

    auto type = ParseType();
    if (!type) return nullptr;

    if (current_.kind != frontends::TokenKind::kIdentifier) return nullptr;
    std::string name = current_.lexeme;
    Consume();

    auto prop = std::make_shared<PropertyDecl>();
    prop->loc = type->loc;
    prop->name = name;
    prop->type = type;
    prop->access = access;
    prop->attributes = attrs;
    prop->is_static = is_static;
    prop->is_virtual = is_virtual;
    prop->is_override = is_override;
    prop->is_abstract = is_abstract;
    prop->is_required = is_required;

    // Property accessor block: { get; set; } or { get { ... } set { ... } }
    if (IsSymbol("{")) {
        Consume();
        while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
            if (MatchKeyword("get")) {
                prop->has_getter = true;
                if (IsSymbol("{")) {
                    ParseBlock();  // discard block for now
                } else {
                    ExpectSymbol(";", "expected ';' after 'get'");
                }
            } else if (MatchKeyword("set")) {
                prop->has_setter = true;
                if (IsSymbol("{")) {
                    ParseBlock();
                } else {
                    ExpectSymbol(";", "expected ';' after 'set'");
                }
            } else if (MatchKeyword("init")) {
                prop->is_init_only = true;
                prop->has_setter = true;
                if (IsSymbol("{")) {
                    ParseBlock();
                } else {
                    ExpectSymbol(";", "expected ';' after 'init'");
                }
            } else {
                Sync();
                if (IsSymbol(";")) Consume();
            }
        }
        ExpectSymbol("}", "expected '}' after property accessors");
    }

    // Expression-bodied property: => expr;
    if (MatchSymbol("=>")) {
        prop->expression_body = ParseExpression();
        prop->has_getter = true;
        ExpectSymbol(";", "expected ';' after expression body");
    }

    // Property initializer: = value;
    if (MatchSymbol("=")) {
        prop->init = ParseExpression();
        ExpectSymbol(";", "expected ';' after property initializer");
    }

    return prop;
}

} // namespace polyglot::dotnet
