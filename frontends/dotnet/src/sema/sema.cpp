/**
 * @file     sema.cpp
 * @brief    .NET/C# language frontend implementation
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <string>
#include <vector>

#include "frontends/dotnet/include/dotnet_sema.h"
#include "frontends/dotnet/include/metadata_reader.h"

namespace polyglot::dotnet {

using polyglot::core::ScopeKind;
using polyglot::core::Symbol;
using polyglot::core::SymbolKind;
using polyglot::core::Type;

namespace {

struct ScopeState {
    ScopeKind kind{ScopeKind::kBlock};
};

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx,
             AssemblyLoader *loader)
        : module_(mod), ctx_(ctx), loader_(loader) {}

    void Run() {
        scope_stack_.push_back({ScopeKind::kModule});
        Syms().EnterScope("<dotnet-module>", ScopeKind::kModule);

        // Process using directives
        for (const auto &u : module_.usings) {
            AnalyzeUsing(*u);
        }

        // Analyze declarations
        for (const auto &decl : module_.declarations) {
            AnalyzeDecl(decl);
        }

        // Top-level statements (C# 9.0+)
        for (const auto &stmt : module_.top_level_statements) {
            AnalyzeStmt(stmt);
        }

        Syms().ExitScope();
        scope_stack_.pop_back();
    }

  private:
    frontends::Diagnostics &Diags() { return ctx_.Diags(); }
    core::TypeSystem &Types() { return ctx_.Types(); }
    core::SymbolTable &Syms() { return ctx_.Symbols(); }

    void EnterScope(ScopeKind kind, const std::string &name = "<block>") {
        scope_stack_.push_back({kind});
        Syms().EnterScope(name, kind);
    }

    void ExitScope() {
        Syms().ExitScope();
        scope_stack_.pop_back();
    }

    /** @name Type mapping */
    /** @{ */
    Type MapType(const std::shared_ptr<TypeNode> &node) {
        if (!node) return Type::Any();
        if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
            return Types().MapFromLanguage("csharp", simple->name);
        }
        if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
            return Types().PointerTo(MapType(arr->element_type));
        }
        if (auto gen = std::dynamic_pointer_cast<GenericType>(node)) {
            Type base = Types().MapFromLanguage("csharp", gen->name);
            for (auto &arg : gen->type_args) {
                base.type_args.push_back(MapType(arg));
            }
            return base;
        }
        if (auto nullable = std::dynamic_pointer_cast<NullableType>(node)) {
            return MapType(nullable->inner);
        }
        if (auto tuple = std::dynamic_pointer_cast<TupleType>(node)) {
            Type t = Type::Struct("ValueTuple", "csharp");
            for (auto &elem : tuple->elements) {
                t.type_args.push_back(MapType(elem.type));
            }
            return t;
        }
        return Type::Any();
    }

    /** @} */

    /** @name Using directives */
    /** @{ */
    // Declare a single resolved CLI type as a symbol in the current scope.
    // When `import_static` is true, also exposes its public static methods
    // and fields as top-level symbols (C# 6+ `using static` semantics).
    void DeclareUsingType(const CliTypeMeta &t, const core::SourceLoc &loc,
                          bool import_static) {
        // The simple (unqualified) name is what shows up in source after
        // `using static System.Console;` or `using System;` + `Console`.
        std::string simple = t.name;
        Type ty = Type::Struct(t.full_name, "csharp");
        Symbol class_sym{simple, ty, loc, SymbolKind::kTypeName, "csharp"};
        Syms().Declare(class_sym);

        if (!import_static) return;

        // ECMA-335 §II.23.1.10 method flags: 0x0010 = Static, 0x0006 = Public
        constexpr std::uint16_t kMethodPublic = 0x0006;
        constexpr std::uint16_t kMethodStatic = 0x0010;
        constexpr std::uint16_t kFieldPublic  = 0x0006;
        constexpr std::uint16_t kFieldStatic  = 0x0010;

        for (const auto &f : t.fields) {
            if ((f.flags & kFieldStatic) == 0) continue;
            if ((f.flags & 0x0007) != kFieldPublic) continue;
            Symbol fs{f.name, f.type, loc, SymbolKind::kVariable, "csharp"};
            Syms().Declare(fs);
        }
        for (const auto &m : t.methods) {
            if ((m.flags & kMethodStatic) == 0) continue;
            if ((m.flags & 0x0007) != kMethodPublic) continue;
            Symbol ms{m.name, m.return_type, loc, SymbolKind::kFunction, "csharp"};
            Syms().Declare(ms);
        }
    }

    void AnalyzeUsing(const UsingDirective &u) {
        // 1) Always declare the legacy module symbol so unit tests continue
        //    to observe the imported namespace name.
        Symbol module_sym{u.alias.empty() ? u.ns : u.alias,
                          Type::Module(u.ns, "csharp"), u.loc,
                          SymbolKind::kModule, "csharp"};
        Syms().Declare(module_sym);

        if (loader_ == nullptr) return;

        // 2) `using static System.Console;` — last segment names a type.
        if (u.is_static) {
            const CliTypeMeta *t = loader_->ResolveType(u.ns);
            if (t == nullptr) {
                // try interpreting as namespace.Type when the user wrote it
                // as a single dotted token.
                auto dot = u.ns.find_last_of('.');
                if (dot != std::string::npos) {
                    t = loader_->ResolveType(u.ns);
                }
            }
            if (t) DeclareUsingType(*t, u.loc, /*import_static=*/true);
            return;
        }

        // 3) Plain `using System;` — bring every type in the namespace into
        //    scope under its simple name.
        for (const CliTypeMeta *t : loader_->ListNamespace(u.ns)) {
            if (t) DeclareUsingType(*t, u.loc, /*import_static=*/false);
        }
    }
    /** @} */

    /** @name Declarations */
    /** @{ */
    void AnalyzeDecl(const std::shared_ptr<Statement> &decl) {
        if (!decl) return;

        if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
            AnalyzeNamespace(*ns);
            return;
        }
        if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
            AnalyzeClass(*cls);
            return;
        }
        if (auto st = std::dynamic_pointer_cast<StructDecl>(decl)) {
            AnalyzeStruct(*st);
            return;
        }
        if (auto iface = std::dynamic_pointer_cast<InterfaceDecl>(decl)) {
            AnalyzeInterface(*iface);
            return;
        }
        if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
            AnalyzeEnum(*en);
            return;
        }
        if (auto del = std::dynamic_pointer_cast<DelegateDecl>(decl)) {
            AnalyzeDelegate(*del);
            return;
        }
        if (auto method = std::dynamic_pointer_cast<MethodDecl>(decl)) {
            DeclareMethod(*method);
            AnalyzeMethod(*method);
            return;
        }
        if (auto field = std::dynamic_pointer_cast<FieldDecl>(decl)) {
            AnalyzeField(*field);
            return;
        }
        if (auto prop = std::dynamic_pointer_cast<PropertyDecl>(decl)) {
            AnalyzeProperty(*prop);
            return;
        }
        if (auto ctor = std::dynamic_pointer_cast<ConstructorDecl>(decl)) {
            AnalyzeConstructor(*ctor);
            return;
        }
        if (auto var = std::dynamic_pointer_cast<VarDecl>(decl)) {
            auto t = MapType(var->type);
            if (t.kind == core::TypeKind::kAny && var->init) {
                t = AnalyzeExpr(var->init);
            }
            Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "csharp"};
            Syms().Declare(sym);
            return;
        }
        AnalyzeStmt(decl);
    }

    void AnalyzeNamespace(const NamespaceDecl &ns) {
        EnterScope(ScopeKind::kModule, ns.name);
        for (auto &m : ns.members) AnalyzeDecl(m);
        ExitScope();
    }

    void AnalyzeClass(const ClassDecl &cls) {
        Type t = Type::Struct(cls.name, "csharp");
        Symbol sym{cls.name, t, cls.loc, SymbolKind::kTypeName, "csharp"};
        sym.access = cls.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, cls.name);
        int sid = Syms().CurrentScopeId();
        Syms().RegisterTypeScope(cls.name, sid);

        std::vector<std::string> bases;
        if (cls.base_type) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(cls.base_type)) {
                bases.push_back(st->name);
            }
        }
        for (auto &iface : cls.interfaces) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(iface)) {
                bases.push_back(st->name);
            }
        }
        if (!bases.empty()) Syms().RegisterTypeBases(cls.name, bases);

        // Primary constructor parameters (C# 12)
        for (auto &p : cls.primary_ctor_params) {
            Symbol ps{p.name, MapType(p.type), cls.loc, SymbolKind::kParameter, "csharp"};
            Syms().Declare(ps);
        }

        for (auto &member : cls.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeStruct(const StructDecl &st) {
        Type t = Type::Struct(st.name, "csharp");
        Symbol sym{st.name, t, st.loc, SymbolKind::kTypeName, "csharp"};
        sym.access = st.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, st.name);

        for (auto &member : st.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeInterface(const InterfaceDecl &iface) {
        Type t = Type::Struct(iface.name, "csharp");
        Symbol sym{iface.name, t, iface.loc, SymbolKind::kTypeName, "csharp"};
        sym.access = iface.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, iface.name);

        std::vector<std::string> bases;
        for (auto &ext : iface.extends_types) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(ext)) {
                bases.push_back(st->name);
            }
        }
        if (!bases.empty()) Syms().RegisterTypeBases(iface.name, bases);

        for (auto &member : iface.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeEnum(const EnumDecl &en) {
        Type t = Type::Enum(en.name, "csharp");
        Symbol sym{en.name, t, en.loc, SymbolKind::kTypeName, "csharp"};
        sym.access = en.access;
        Syms().Declare(sym);

        for (auto &m : en.members) {
            Symbol ms{m.name, t, en.loc, SymbolKind::kVariable, "csharp"};
            Syms().Declare(ms);
        }
    }

    void AnalyzeDelegate(const DelegateDecl &del) {
        std::vector<Type> params;
        for (auto &p : del.params) params.push_back(MapType(p.type));
        Type ret = MapType(del.return_type);
        Type ft = Types().FunctionType(del.name, ret, params);
        ft.language = "csharp";
        Symbol sym{del.name, ft, del.loc, SymbolKind::kTypeName, "csharp"};
        sym.access = del.access;
        Syms().Declare(sym);
    }

    void DeclareMethod(const MethodDecl &method) {
        std::vector<Type> params;
        for (auto &p : method.params) params.push_back(MapType(p.type));
        Type ret = MapType(method.return_type);
        Type fnt = Types().FunctionType(method.name, ret, params);
        fnt.language = "csharp";
        Symbol sym{method.name, fnt, method.loc, SymbolKind::kFunction, "csharp"};
        sym.access = method.access;
        Syms().Declare(sym);
    }

    void AnalyzeMethod(const MethodDecl &method) {
        EnterScope(ScopeKind::kFunction, method.name);
        current_return_type_ = MapType(method.return_type);

        for (auto &p : method.params) {
            Symbol param{p.name, MapType(p.type), method.loc, SymbolKind::kParameter, "csharp"};
            Syms().Declare(param);
        }

        if (method.expression_body) {
            AnalyzeExpr(method.expression_body);
        }

        for (auto &stmt : method.body) {
            AnalyzeStmt(stmt);
        }

        ExitScope();
    }

    void AnalyzeField(const FieldDecl &field) {
        Type t = MapType(field.type);
        if (field.init) AnalyzeExpr(field.init);
        Symbol sym{field.name, t, field.loc, SymbolKind::kField, "csharp"};
        sym.access = field.access;
        Syms().Declare(sym);
    }

    void AnalyzeProperty(const PropertyDecl &prop) {
        Type t = MapType(prop.type);
        Symbol sym{prop.name, t, prop.loc, SymbolKind::kField, "csharp"};
        sym.access = prop.access;
        Syms().Declare(sym);

        if (prop.expression_body) AnalyzeExpr(prop.expression_body);
        if (prop.init) AnalyzeExpr(prop.init);
    }

    void AnalyzeConstructor(const ConstructorDecl &ctor) {
        EnterScope(ScopeKind::kFunction, ctor.name);

        for (auto &p : ctor.params) {
            Symbol param{p.name, MapType(p.type), ctor.loc, SymbolKind::kParameter, "csharp"};
            Syms().Declare(param);
        }

        for (auto &a : ctor.initializer_args) {
            AnalyzeExpr(a);
        }

        for (auto &stmt : ctor.body) {
            AnalyzeStmt(stmt);
        }

        ExitScope();
    }

    /** @} */

    /** @name Statements */
    /** @{ */
    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;

        if (std::dynamic_pointer_cast<ClassDecl>(stmt) ||
            std::dynamic_pointer_cast<StructDecl>(stmt) ||
            std::dynamic_pointer_cast<InterfaceDecl>(stmt) ||
            std::dynamic_pointer_cast<EnumDecl>(stmt) ||
            std::dynamic_pointer_cast<DelegateDecl>(stmt) ||
            std::dynamic_pointer_cast<MethodDecl>(stmt) ||
            std::dynamic_pointer_cast<FieldDecl>(stmt) ||
            std::dynamic_pointer_cast<PropertyDecl>(stmt) ||
            std::dynamic_pointer_cast<ConstructorDecl>(stmt) ||
            std::dynamic_pointer_cast<NamespaceDecl>(stmt)) {
            AnalyzeDecl(stmt);
            return;
        }

        if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            AnalyzeDecl(stmt);
            return;
        }

        if (auto block = std::dynamic_pointer_cast<BlockStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            for (auto &s : block->statements) AnalyzeStmt(s);
            ExitScope();
            return;
        }

        if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
            if (expr_stmt->expr) AnalyzeExpr(expr_stmt->expr);
            return;
        }

        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            Type value = ret->value ? AnalyzeExpr(ret->value) : Type::Void();
            if (!Types().IsCompatible(value, current_return_type_)) {
                Diags().Report(ret->loc, "Return type mismatch");
            }
            return;
        }

        if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
            Type cond = AnalyzeExpr(if_stmt->condition);
            if (cond.kind != core::TypeKind::kBool && cond.kind != core::TypeKind::kAny) {
                Diags().Report(if_stmt->loc, "Condition must be boolean");
            }
            AnalyzeStmt(if_stmt->then_body);
            if (if_stmt->else_body) AnalyzeStmt(if_stmt->else_body);
            return;
        }

        if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
            AnalyzeExpr(while_stmt->condition);
            AnalyzeStmt(while_stmt->body);
            return;
        }

        if (auto do_stmt = std::dynamic_pointer_cast<DoWhileStatement>(stmt)) {
            AnalyzeStmt(do_stmt->body);
            AnalyzeExpr(do_stmt->condition);
            return;
        }

        if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            if (for_stmt->init) AnalyzeStmt(for_stmt->init);
            if (for_stmt->condition) AnalyzeExpr(for_stmt->condition);
            if (for_stmt->update) AnalyzeExpr(for_stmt->update);
            AnalyzeStmt(for_stmt->body);
            ExitScope();
            return;
        }

        if (auto foreach_stmt = std::dynamic_pointer_cast<ForEachStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            AnalyzeExpr(foreach_stmt->iterable);
            Type elem = MapType(foreach_stmt->var_type);
            Symbol sym{foreach_stmt->var_name, elem, foreach_stmt->loc,
                       SymbolKind::kVariable, "csharp"};
            Syms().Declare(sym);
            AnalyzeStmt(foreach_stmt->body);
            ExitScope();
            return;
        }

        if (auto switch_stmt = std::dynamic_pointer_cast<SwitchStatement>(stmt)) {
            AnalyzeExpr(switch_stmt->governing);
            for (auto &sec : switch_stmt->sections) {
                for (auto &lbl : sec.labels) AnalyzeExpr(lbl);
                for (auto &s : sec.body) AnalyzeStmt(s);
            }
            return;
        }

        if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
            AnalyzeStmt(try_stmt->body);
            for (auto &c : try_stmt->catches) {
                EnterScope(ScopeKind::kBlock);
                if (!c.var_name.empty()) {
                    Type et = c.exception_type ? MapType(c.exception_type) : Type::Any();
                    Symbol ex{c.var_name, et,
                              c.body ? c.body->loc : try_stmt->loc,
                              SymbolKind::kVariable, "csharp"};
                    Syms().Declare(ex);
                }
                if (c.filter) AnalyzeExpr(c.filter);
                AnalyzeStmt(c.body);
                ExitScope();
            }
            if (try_stmt->finally_body) AnalyzeStmt(try_stmt->finally_body);
            return;
        }

        if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
            if (throw_stmt->expr) AnalyzeExpr(throw_stmt->expr);
            return;
        }

        if (auto using_stmt = std::dynamic_pointer_cast<UsingStatement>(stmt)) {
            if (using_stmt->declaration) {
                auto t = MapType(using_stmt->declaration->type);
                if (using_stmt->declaration->init) AnalyzeExpr(using_stmt->declaration->init);
                Symbol sym{using_stmt->declaration->name, t, using_stmt->loc,
                           SymbolKind::kVariable, "csharp"};
                Syms().Declare(sym);
            }
            if (using_stmt->expression) AnalyzeExpr(using_stmt->expression);
            if (using_stmt->body) AnalyzeStmt(using_stmt->body);
            return;
        }

        if (auto lock_stmt = std::dynamic_pointer_cast<LockStatement>(stmt)) {
            AnalyzeExpr(lock_stmt->expr);
            AnalyzeStmt(lock_stmt->body);
            return;
        }

        if (auto yield_stmt = std::dynamic_pointer_cast<YieldStatement>(stmt)) {
            if (yield_stmt->value) AnalyzeExpr(yield_stmt->value);
            return;
        }

        if (auto checked = std::dynamic_pointer_cast<CheckedStatement>(stmt)) {
            AnalyzeStmt(checked->body);
            return;
        }
    }

    /** @} */

    /** @name Expressions */
    /** @{ */
    Type AnalyzeExpr(const std::shared_ptr<Expression> &expr) {
        if (!expr) return Type::Any();

        if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
            auto sym = Syms().Lookup(id->name);
            if (!sym && id->name != "this" && id->name != "base") {
                Diags().Report(id->loc, "Undeclared identifier: " + id->name);
                return Type::Any();
            }
            return sym ? sym->symbol->type : Type::Any();
        }

        if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
            auto &v = lit->value;
            if (v == "true" || v == "false") return Type::Bool();
            if (v == "null") return Type::Any();
            if (!v.empty() && v[0] == '"') return Type::String();
            if (!v.empty() && v[0] == '\'') return Type::Int();
            if (v.find('.') != std::string::npos) return Type::Float();
            return Type::Int();
        }

        if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
            return AnalyzeExpr(unary->operand);
        }

        if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
            Type left = AnalyzeExpr(bin->left);
            Type right = AnalyzeExpr(bin->right);
            if (bin->op == "==" || bin->op == "!=" || bin->op == "<" ||
                bin->op == ">" || bin->op == "<=" || bin->op == ">=" ||
                bin->op == "&&" || bin->op == "||") {
                return Type::Bool();
            }
            if (bin->op == "=" || bin->op == "+=" || bin->op == "-=") return left;
            if (bin->op == "+" &&
                (left.kind == core::TypeKind::kString || right.kind == core::TypeKind::kString)) {
                return Type::String();
            }
            if (left.kind == core::TypeKind::kFloat || right.kind == core::TypeKind::kFloat) {
                return Type::Float();
            }
            return Type::Int();
        }

        if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
            for (auto &arg : call->args) AnalyzeExpr(arg);
            if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
                auto sym = Syms().Lookup(id->name);
                if (sym && sym->symbol->type.kind == core::TypeKind::kFunction) {
                    return sym->symbol->type.type_args.empty() ? Type::Any() : sym->symbol->type.type_args[0];
                }
            }
            if (auto member = std::dynamic_pointer_cast<MemberExpression>(call->callee)) {
                AnalyzeExpr(member->object);
            }
            return Type::Any();
        }

        if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
            AnalyzeExpr(member->object);
            return Type::Any();
        }

        if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
            for (auto &arg : new_expr->args) AnalyzeExpr(arg);
            for (auto &i : new_expr->initializer) AnalyzeExpr(i);
            return MapType(new_expr->type);
        }

        if (auto cast = std::dynamic_pointer_cast<CastExpression>(expr)) {
            AnalyzeExpr(cast->expr);
            return MapType(cast->target_type);
        }

        if (auto is_expr = std::dynamic_pointer_cast<IsExpression>(expr)) {
            AnalyzeExpr(is_expr->expr);
            if (!is_expr->pattern_var.empty()) {
                Type pt = MapType(is_expr->type);
                Symbol ps{is_expr->pattern_var, pt, is_expr->loc,
                          SymbolKind::kVariable, "csharp"};
                Syms().Declare(ps);
            }
            return Type::Bool();
        }

        if (auto as_expr = std::dynamic_pointer_cast<AsExpression>(expr)) {
            AnalyzeExpr(as_expr->expr);
            return MapType(as_expr->type);
        }

        if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr)) {
            Type arr_type = AnalyzeExpr(index->object);
            AnalyzeExpr(index->index);
            if (!arr_type.type_args.empty()) return arr_type.type_args[0];
            return Type::Any();
        }

        if (auto lambda = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
            EnterScope(ScopeKind::kFunction, "<lambda>");
            for (auto &p : lambda->params) {
                Type pt = MapType(p.type);
                Symbol ps{p.name, pt, expr->loc, SymbolKind::kParameter, "csharp"};
                Syms().Declare(ps);
            }
            if (lambda->body) AnalyzeStmt(lambda->body);
            if (lambda->expr) AnalyzeExpr(lambda->expr);
            ExitScope();
            return Type::Any();
        }

        if (auto tern = std::dynamic_pointer_cast<TernaryExpression>(expr)) {
            AnalyzeExpr(tern->condition);
            Type then_t = AnalyzeExpr(tern->then_expr);
            AnalyzeExpr(tern->else_expr);
            return then_t;
        }

        if (auto nc = std::dynamic_pointer_cast<NullCoalescingExpression>(expr)) {
            Type left = AnalyzeExpr(nc->left);
            AnalyzeExpr(nc->right);
            return left;
        }

        if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
            return AnalyzeExpr(await->operand);
        }

        if (auto throw_expr = std::dynamic_pointer_cast<ThrowExpression>(expr)) {
            AnalyzeExpr(throw_expr->operand);
            return Type::Any();
        }

        if (auto sw = std::dynamic_pointer_cast<SwitchExpression>(expr)) {
            AnalyzeExpr(sw->governing);
            for (auto &arm : sw->arms) {
                if (arm.pattern) AnalyzeExpr(arm.pattern);
                if (arm.guard) AnalyzeExpr(arm.guard);
                if (arm.value) AnalyzeExpr(arm.value);
            }
            return Type::Any();
        }

        if (auto tuple = std::dynamic_pointer_cast<TupleExpression>(expr)) {
            for (auto &e : tuple->elements) AnalyzeExpr(e.value);
            return Type::Any();
        }

        return Type::Any();
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    AssemblyLoader *loader_{nullptr};
    std::vector<ScopeState> scope_stack_;
    Type current_return_type_{Type::Void()};
};

} // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer a(module, context, /*loader=*/nullptr);
    a.Run();
}

void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const DotNetSemaOptions &options) {
    Analyzer a(module, context, options.loader);
    a.Run();
}

} // namespace polyglot::dotnet

/** @} */