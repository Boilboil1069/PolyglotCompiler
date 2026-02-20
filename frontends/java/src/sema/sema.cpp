#include <string>
#include <vector>

#include "frontends/java/include/java_sema.h"

namespace polyglot::java {

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
    Analyzer(const Module &mod, frontends::SemaContext &ctx) : module_(mod), ctx_(ctx) {}

    void Run() {
        scope_stack_.push_back({ScopeKind::kModule});
        Syms().EnterScope("<java-module>", ScopeKind::kModule);

        // Register package scope
        if (module_.package_decl) {
            Symbol pkg{module_.package_decl->name, Type::Module(module_.package_decl->name, "java"),
                       module_.package_decl->loc, SymbolKind::kModule, "java"};
            Syms().Declare(pkg);
        }

        // Process imports
        for (const auto &imp : module_.imports) {
            AnalyzeImport(*imp);
        }

        // Analyze declarations
        for (const auto &decl : module_.declarations) {
            AnalyzeDecl(decl);
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

    // ---- Type mapping ----
    Type MapType(const std::shared_ptr<TypeNode> &node) {
        if (!node) return Type::Any();
        if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
            return Types().MapFromLanguage("java", simple->name);
        }
        if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
            return Types().PointerTo(MapType(arr->element_type));
        }
        if (auto gen = std::dynamic_pointer_cast<GenericType>(node)) {
            Type base = Types().MapFromLanguage("java", gen->name);
            for (auto &arg : gen->type_args) {
                base.type_args.push_back(MapType(arg));
            }
            return base;
        }
        if (auto wc = std::dynamic_pointer_cast<WildcardType>(node)) {
            if (wc->bound) return MapType(wc->bound);
            return Type::Any();
        }
        return Type::Any();
    }

    // ---- Imports ----
    void AnalyzeImport(const ImportDecl &imp) {
        // Register imported symbol or wildcard
        Symbol sym{imp.path, Type::Module(imp.path, "java"), imp.loc, SymbolKind::kModule, "java"};
        Syms().Declare(sym);
    }

    // ---- Declarations ----
    void AnalyzeDecl(const std::shared_ptr<Statement> &decl) {
        if (!decl) return;

        if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
            AnalyzeClass(*cls);
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
        if (auto rec = std::dynamic_pointer_cast<RecordDecl>(decl)) {
            AnalyzeRecord(*rec);
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
        if (auto ctor = std::dynamic_pointer_cast<ConstructorDecl>(decl)) {
            AnalyzeConstructor(*ctor);
            return;
        }
        if (auto var = std::dynamic_pointer_cast<VarDecl>(decl)) {
            auto t = MapType(var->type);
            if (t.kind == core::TypeKind::kAny && var->init) {
                t = AnalyzeExpr(var->init);
            }
            Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "java"};
            if (!Syms().Declare(sym)) {
                Diags().Report(var->loc, "Duplicate variable: " + var->name);
            }
            return;
        }
        // Statement analysis fallback
        AnalyzeStmt(decl);
    }

    void AnalyzeClass(const ClassDecl &cls) {
        Type t = Type::Struct(cls.name, "java");
        Symbol sym{cls.name, t, cls.loc, SymbolKind::kTypeName, "java"};
        sym.access = cls.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, cls.name);
        int sid = Syms().CurrentScopeId();
        Syms().RegisterTypeScope(cls.name, sid);

        // Register superclass and interfaces as bases
        std::vector<std::string> bases;
        if (cls.superclass) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(cls.superclass)) {
                bases.push_back(st->name);
            }
        }
        for (auto &iface : cls.interfaces) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(iface)) {
                bases.push_back(st->name);
            }
        }
        if (!bases.empty()) {
            Syms().RegisterTypeBases(cls.name, bases);
        }

        // Sealed class: register permitted subclasses (Java 17+)
        if (cls.is_sealed && !cls.permits.empty()) {
            // Record sealed permits as metadata
            for (auto &p : cls.permits) {
                Symbol ps{p, Type::Struct(p, "java"), cls.loc, SymbolKind::kTypeName, "java"};
                Syms().Declare(ps);
            }
        }

        for (auto &member : cls.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeInterface(const InterfaceDecl &iface) {
        Type t = Type::Struct(iface.name, "java");
        Symbol sym{iface.name, t, iface.loc, SymbolKind::kTypeName, "java"};
        sym.access = iface.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, iface.name);

        std::vector<std::string> bases;
        for (auto &ext : iface.extends_types) {
            if (auto st = std::dynamic_pointer_cast<SimpleType>(ext)) {
                bases.push_back(st->name);
            }
        }
        if (!bases.empty()) {
            Syms().RegisterTypeBases(iface.name, bases);
        }

        for (auto &member : iface.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeEnum(const EnumDecl &en) {
        Type t = Type::Enum(en.name, "java");
        Symbol sym{en.name, t, en.loc, SymbolKind::kTypeName, "java"};
        sym.access = en.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, en.name);

        for (auto &c : en.constants) {
            Symbol cs{c.name, t, en.loc, SymbolKind::kVariable, "java"};
            Syms().Declare(cs);
        }

        for (auto &member : en.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void AnalyzeRecord(const RecordDecl &rec) {
        Type t = Type::Struct(rec.name, "java");
        Symbol sym{rec.name, t, rec.loc, SymbolKind::kTypeName, "java"};
        sym.access = rec.access;
        Syms().Declare(sym);

        EnterScope(ScopeKind::kClass, rec.name);

        // Record components become implicit fields and accessor methods
        for (auto &comp : rec.components) {
            Type ct = MapType(comp.type);
            Symbol fs{comp.name, ct, rec.loc, SymbolKind::kField, "java"};
            Syms().Declare(fs);

            // Implicit accessor method
            Type ft = Types().FunctionType(comp.name, ct, {});
            ft.language = "java";
            Symbol ms{comp.name, ft, rec.loc, SymbolKind::kFunction, "java"};
            ms.access = "public";
            Syms().Declare(ms);
        }

        for (auto &member : rec.members) {
            AnalyzeDecl(member);
        }

        ExitScope();
    }

    void DeclareMethod(const MethodDecl &method) {
        std::vector<Type> params;
        for (auto &p : method.params) {
            params.push_back(MapType(p.type));
        }
        Type ret = MapType(method.return_type);
        Type fnt = Types().FunctionType(method.name, ret, params);
        fnt.language = "java";
        Symbol sym{method.name, fnt, method.loc, SymbolKind::kFunction, "java"};
        sym.access = method.access;
        if (!Syms().Declare(sym)) {
            // Overloading is allowed in Java; suppress duplicate error
        }
    }

    void AnalyzeMethod(const MethodDecl &method) {
        EnterScope(ScopeKind::kFunction, method.name);
        current_return_type_ = MapType(method.return_type);

        for (auto &p : method.params) {
            Symbol param{p.name, MapType(p.type), method.loc, SymbolKind::kParameter, "java"};
            Syms().Declare(param);
        }

        for (auto &stmt : method.body) {
            AnalyzeStmt(stmt);
        }

        ExitScope();
    }

    void AnalyzeField(const FieldDecl &field) {
        Type t = MapType(field.type);
        if (field.init) {
            AnalyzeExpr(field.init);
        }
        Symbol sym{field.name, t, field.loc, SymbolKind::kField, "java"};
        sym.access = field.access;
        if (!Syms().Declare(sym)) {
            Diags().Report(field.loc, "Duplicate field: " + field.name);
        }
    }

    void AnalyzeConstructor(const ConstructorDecl &ctor) {
        EnterScope(ScopeKind::kFunction, ctor.name);

        for (auto &p : ctor.params) {
            Symbol param{p.name, MapType(p.type), ctor.loc, SymbolKind::kParameter, "java"};
            Syms().Declare(param);
        }

        for (auto &stmt : ctor.body) {
            AnalyzeStmt(stmt);
        }

        ExitScope();
    }

    // ---- Statements ----
    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;

        // Check for declarations first
        if (std::dynamic_pointer_cast<ClassDecl>(stmt) ||
            std::dynamic_pointer_cast<InterfaceDecl>(stmt) ||
            std::dynamic_pointer_cast<EnumDecl>(stmt) ||
            std::dynamic_pointer_cast<RecordDecl>(stmt) ||
            std::dynamic_pointer_cast<MethodDecl>(stmt) ||
            std::dynamic_pointer_cast<FieldDecl>(stmt) ||
            std::dynamic_pointer_cast<ConstructorDecl>(stmt)) {
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
            Symbol sym{foreach_stmt->var_name, elem, foreach_stmt->loc, SymbolKind::kVariable, "java"};
            Syms().Declare(sym);
            AnalyzeStmt(foreach_stmt->body);
            ExitScope();
            return;
        }

        if (auto switch_stmt = std::dynamic_pointer_cast<SwitchStatement>(stmt)) {
            AnalyzeExpr(switch_stmt->selector);
            for (auto &c : switch_stmt->cases) {
                for (auto &lbl : c.labels) AnalyzeExpr(lbl);
                for (auto &s : c.body) AnalyzeStmt(s);
            }
            return;
        }

        if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
            // Resources (try-with-resources Java 7+)
            EnterScope(ScopeKind::kBlock);
            for (auto &r : try_stmt->resources) AnalyzeStmt(r);
            AnalyzeStmt(try_stmt->body);
            ExitScope();

            for (auto &c : try_stmt->catches) {
                EnterScope(ScopeKind::kBlock);
                if (!c.var_name.empty()) {
                    Type et = c.exception_types.empty() ? Type::Any()
                                                        : MapType(c.exception_types[0]);
                    Symbol ex{c.var_name, et, c.body ? c.body->loc : try_stmt->loc,
                              SymbolKind::kVariable, "java"};
                    Syms().Declare(ex);
                }
                AnalyzeStmt(c.body);
                ExitScope();
            }

            if (try_stmt->finally_body) {
                AnalyzeStmt(try_stmt->finally_body);
            }
            return;
        }

        if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
            AnalyzeExpr(throw_stmt->expr);
            return;
        }

        if (auto assert_stmt = std::dynamic_pointer_cast<AssertStatement>(stmt)) {
            AnalyzeExpr(assert_stmt->condition);
            if (assert_stmt->message) AnalyzeExpr(assert_stmt->message);
            return;
        }

        if (auto sync_stmt = std::dynamic_pointer_cast<SynchronizedStatement>(stmt)) {
            AnalyzeExpr(sync_stmt->monitor);
            AnalyzeStmt(sync_stmt->body);
            return;
        }

        if (auto yield_stmt = std::dynamic_pointer_cast<YieldStatement>(stmt)) {
            AnalyzeExpr(yield_stmt->value);
            return;
        }
    }

    // ---- Expressions ----
    Type AnalyzeExpr(const std::shared_ptr<Expression> &expr) {
        if (!expr) return Type::Any();

        if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
            auto sym = Syms().Lookup(id->name);
            if (!sym && id->name != "this" && id->name != "super") {
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
            // Number
            if (v.find('.') != std::string::npos || v.find('e') != std::string::npos ||
                v.find('E') != std::string::npos) {
                if (!v.empty() && (v.back() == 'f' || v.back() == 'F'))
                    return Type::Float(); // float
                return Type::Float(); // double
            }
            if (!v.empty() && (v.back() == 'l' || v.back() == 'L'))
                return Type::Int(); // long
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
                bin->op == "&&" || bin->op == "||" || bin->op == "instanceof") {
                return Type::Bool();
            }
            // Assignment operators return left type
            if (bin->op == "=" || bin->op == "+=" || bin->op == "-=" ||
                bin->op == "*=" || bin->op == "/=") {
                return left;
            }
            // String concatenation
            if (bin->op == "+" &&
                (left.kind == core::TypeKind::kString || right.kind == core::TypeKind::kString)) {
                return Type::String();
            }
            // Numeric promotion
            if (left.kind == core::TypeKind::kFloat || right.kind == core::TypeKind::kFloat) {
                return Type::Float();
            }
            return Type::Int();
        }

        if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
            for (auto &arg : call->args) AnalyzeExpr(arg);
            if (auto member = std::dynamic_pointer_cast<MemberExpression>(call->callee)) {
                AnalyzeExpr(member->object);
            } else if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
                auto sym = Syms().Lookup(id->name);
                if (sym && sym->symbol->type.kind == core::TypeKind::kFunction) {
                    return sym->symbol->type.type_args.empty() ? Type::Any() : sym->symbol->type.type_args[0];
                }
            }
            return Type::Any();
        }

        if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
            AnalyzeExpr(member->object);
            return Type::Any(); // field type resolution requires full type info
        }

        if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
            for (auto &arg : new_expr->args) AnalyzeExpr(arg);
            return MapType(new_expr->type);
        }

        if (auto cast = std::dynamic_pointer_cast<CastExpression>(expr)) {
            AnalyzeExpr(cast->expr);
            return MapType(cast->target_type);
        }

        if (auto arr = std::dynamic_pointer_cast<ArrayAccessExpression>(expr)) {
            Type arrType = AnalyzeExpr(arr->array);
            AnalyzeExpr(arr->index);
            if (!arrType.type_args.empty()) return arrType.type_args[0];
            return Type::Any();
        }

        if (auto lambda = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
            EnterScope(ScopeKind::kFunction, "<lambda>");
            for (auto &p : lambda->params) {
                Type pt = MapType(p.type);
                Symbol ps{p.name, pt, expr->loc, SymbolKind::kParameter, "java"};
                Syms().Declare(ps);
            }
            AnalyzeStmt(lambda->body);
            ExitScope();
            return Type::Any(); // lambda types require target type inference
        }

        if (auto tern = std::dynamic_pointer_cast<TernaryExpression>(expr)) {
            Type cond = AnalyzeExpr(tern->condition);
            if (cond.kind != core::TypeKind::kBool && cond.kind != core::TypeKind::kAny) {
                Diags().Report(tern->loc, "Ternary condition must be boolean");
            }
            Type then_t = AnalyzeExpr(tern->then_expr);
            Type else_t = AnalyzeExpr(tern->else_expr);
            return Types().IsCompatible(then_t, else_t) ? then_t : Type::Any();
        }

        if (auto inst = std::dynamic_pointer_cast<InstanceofExpression>(expr)) {
            AnalyzeExpr(inst->expr);
            // Pattern variable (Java 16+)
            if (!inst->pattern_var.empty()) {
                Type pt = MapType(inst->type);
                Symbol ps{inst->pattern_var, pt, inst->loc, SymbolKind::kVariable, "java"};
                Syms().Declare(ps);
            }
            return Type::Bool();
        }

        if (auto mref = std::dynamic_pointer_cast<MethodReferenceExpression>(expr)) {
            AnalyzeExpr(mref->object);
            return Type::Any();
        }

        if (auto sw = std::dynamic_pointer_cast<SwitchExpression>(expr)) {
            AnalyzeExpr(sw->selector);
            for (auto &c : sw->cases) {
                for (auto &lbl : c.labels) AnalyzeExpr(lbl);
                if (c.value) AnalyzeExpr(c.value);
                for (auto &s : c.body) AnalyzeStmt(s);
            }
            return Type::Any();
        }

        return Type::Any();
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    std::vector<ScopeState> scope_stack_;
    Type current_return_type_{Type::Void()};
};

} // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer a(module, context);
    a.Run();
}

} // namespace polyglot::java
