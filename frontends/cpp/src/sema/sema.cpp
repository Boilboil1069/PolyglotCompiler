#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "frontends/cpp/include/cpp_sema.h"

namespace polyglot::cpp {

using polyglot::core::ScopeKind;
using polyglot::core::Symbol;
using polyglot::core::SymbolKind;
using polyglot::core::Type;
using polyglot::core::TypeUnifier;

namespace {

struct ScopeState {
    ScopeKind kind{ScopeKind::kBlock};
};

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx) : module_(mod), ctx_(ctx) {}

    void Run() {
        scope_stack_.push_back({ScopeKind::kModule});
        Syms().EnterScope("<cpp-module>", ScopeKind::kModule);
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

    // ---- Types ----
    Type MapType(const std::shared_ptr<TypeNode> &node) {
        if (!node) return Type::Any();
        if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
            return Types().MapFromLanguage("cpp", simple->name);
        }
        if (auto ptr = std::dynamic_pointer_cast<PointerType>(node)) {
            return Types().PointerToWithCV(MapType(ptr->pointee), ptr->is_const, ptr->is_volatile);
        }
        if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node)) {
            return Types().ReferenceTo(MapType(ref->referent), ref->is_rvalue);
        }
        if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
            return Types().PointerTo(MapType(arr->element_type));
        }
        if (auto fn = std::dynamic_pointer_cast<FunctionType>(node)) {
            std::vector<Type> params;
            for (auto &p : fn->params) params.push_back(MapType(p));
            auto ret = MapType(fn->return_type);
            return Types().FunctionType("fn", ret, params);
        }
        if (auto qual = std::dynamic_pointer_cast<QualifiedType>(node)) {
            Type inner = MapType(qual->inner);
            inner.is_const = qual->is_const || inner.is_const;
            inner.is_volatile = qual->is_volatile || inner.is_volatile;
            return inner;
        }
        return Type::Any();
    }

    std::string TypeName(const Type &t) {
        if (t.type_args.empty()) return t.name;
        std::string res = t.name + "<";
        for (size_t i = 0; i < t.type_args.size(); ++i) {
            if (i) res += ",";
            res += TypeName(t.type_args[i]);
        }
        res += ">";
        return res;
    }

    // ---- Declarations ----
    void AnalyzeDecl(const std::shared_ptr<Statement> &decl) {
        if (!decl) return;
        if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl)) {
            DeclareFunction(*fn);
            AnalyzeFunction(*fn);
            return;
        }
        if (auto var = std::dynamic_pointer_cast<VarDecl>(decl)) {
            auto t = MapType(var->type);
            if (t.kind == core::TypeKind::kAny && var->init) t = AnalyzeExpr(var->init);
            Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "cpp"};
            sym.access = var->access;
            if (!Syms().Declare(sym)) {
                Diags().Report(var->loc, "Duplicate variable: " + var->name);
            }
            return;
        }
        if (auto rec = std::dynamic_pointer_cast<RecordDecl>(decl)) {
            Type t = Type::Struct(rec->name, "cpp");
            Symbol sym{rec->name, t, rec->loc, SymbolKind::kTypeName, "cpp"};
            Syms().Declare(sym);
            scope_stack_.push_back({ScopeKind::kClass});
            int sid = Syms().EnterScope(rec->name, ScopeKind::kClass);
            Syms().RegisterTypeScope(rec->name, sid);
            std::vector<std::string> bases;
            for (auto &b : rec->bases) bases.push_back(b.name);
            Syms().RegisterTypeBases(rec->name, bases);
            std::string default_access = (rec->kind == "class") ? "private" : "public";
            for (auto &field : rec->fields) {
                Symbol fsym{field.name, MapType(field.type), rec->loc, SymbolKind::kField, "cpp"};
                fsym.access = field.access.empty() ? default_access : field.access;
                Syms().Declare(fsym);
            }
            for (auto &m : rec->methods) {
                if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(m)) {
                    if (fd->access.empty()) fd->access = default_access;
                }
                AnalyzeDecl(m);
            }
            Syms().ExitScope();
            scope_stack_.pop_back();
            return;
        }
        if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
            Type t = Type::Enum(en->name, "cpp");
            Symbol sym{en->name, t, en->loc, SymbolKind::kTypeName, "cpp"};
            Syms().Declare(sym);
            for (auto &e : en->enumerators) {
                Symbol esym{e, t, en->loc, SymbolKind::kVariable, "cpp"};
                Syms().Declare(esym);
            }
            return;
        }
        if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
            scope_stack_.push_back({ScopeKind::kModule});
            int sid = Syms().EnterScope(ns->name, ScopeKind::kModule);
            Syms().RegisterTypeScope(ns->name, sid);
            for (auto &m : ns->members) AnalyzeDecl(m);
            Syms().ExitScope();
            scope_stack_.pop_back();
            return;
        }
        if (auto tpl = std::dynamic_pointer_cast<TemplateDecl>(decl)) {
            // treat template parameters as generic placeholders; analyze inner in a template scope
            scope_stack_.push_back({ScopeKind::kBlock});
            Syms().EnterScope("<template>", ScopeKind::kBlock);
            for (auto &p : tpl->params) {
                Symbol ts{p, Type::GenericParam(p, "cpp"), decl->loc, SymbolKind::kTypeName, "cpp"};
                Syms().Declare(ts);
            }
            AnalyzeDecl(tpl->inner);
            Syms().ExitScope();
            scope_stack_.pop_back();
            return;
        }
        if (auto fr = std::dynamic_pointer_cast<FriendDecl>(decl)) {
            AnalyzeDecl(fr->decl);
            return;
        }
        if (auto mod = std::dynamic_pointer_cast<ModuleDeclaration>(decl)) {
            Symbol ms{mod->name, Type::Module(mod->name, "cpp"), mod->loc, SymbolKind::kModule, "cpp"};
            Syms().Declare(ms);
            return;
        }
        // Fallback: traverse contained statements if any
        if (auto comp = std::dynamic_pointer_cast<CompoundStatement>(decl)) {
            EnterScope(ScopeKind::kBlock);
            for (auto &s : comp->statements) AnalyzeDecl(s);
            ExitScope();
            return;
        }
        if (auto sw = std::dynamic_pointer_cast<SwitchStatement>(decl)) {
            AnalyzeExpr(sw->condition);
            EnterScope(ScopeKind::kBlock);
            for (auto &c : sw->cases) {
                for (auto &lbl : c.labels) AnalyzeExpr(lbl);
                for (auto &s : c.body) AnalyzeDecl(s);
            }
            ExitScope();
            return;
        }
        if (auto tr = std::dynamic_pointer_cast<TryStatement>(decl)) {
            EnterScope(ScopeKind::kBlock);
            for (auto &s : tr->try_body) AnalyzeDecl(s);
            ExitScope();
            for (auto &c : tr->catches) {
                EnterScope(ScopeKind::kBlock);
                if (!c.name.empty()) {
                    Symbol ex{c.name, MapType(c.exception_type), c.exception_type ? c.exception_type->loc : c.body.front()->loc, SymbolKind::kVariable, "cpp"};
                    Syms().Declare(ex);
                }
                for (auto &s : c.body) AnalyzeDecl(s);
                ExitScope();
            }
            for (auto &f : tr->finally_body) AnalyzeDecl(f);
            return;
        }
    }

    void DeclareFunction(const FunctionDecl &fn) {
        std::vector<Type> params;
        for (auto &p : fn.params) params.push_back(MapType(p.type));
        Type ret = MapType(fn.return_type);
        Type fnt = Types().FunctionType(fn.name, ret, params);
        fnt.language = "cpp";
        Symbol sym{fn.name, fnt, fn.loc, SymbolKind::kFunction, "cpp"};
        sym.access = fn.access;
        if (!Syms().Declare(sym)) {
            Diags().Report(fn.loc, "Duplicate function: " + fn.name);
        }
    }

    void AnalyzeFunction(const FunctionDecl &fn) {
        scope_stack_.push_back({ScopeKind::kFunction});
        Syms().EnterScope(fn.name, ScopeKind::kFunction);
        current_return_type_ = MapType(fn.return_type);
        for (auto &p : fn.params) {
            Symbol param{p.name, MapType(p.type), fn.loc, SymbolKind::kParameter, "cpp"};
            Syms().Declare(param);
        }
        for (auto &stmt : fn.body) AnalyzeStmt(stmt);
        Syms().ExitScope();
        scope_stack_.pop_back();
    }

    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;
        if (std::dynamic_pointer_cast<FunctionDecl>(stmt) ||
            std::dynamic_pointer_cast<RecordDecl>(stmt) ||
            std::dynamic_pointer_cast<EnumDecl>(stmt) ||
            std::dynamic_pointer_cast<NamespaceDecl>(stmt) ||
            std::dynamic_pointer_cast<TemplateDecl>(stmt)) {
            AnalyzeDecl(stmt);
            return;
        }
        if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            auto t = MapType(var->type);
            if (t.kind == core::TypeKind::kAny && var->init) t = AnalyzeExpr(var->init);
            Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "cpp"};
            Syms().Declare(sym);
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            Type value = AnalyzeExpr(ret->value);
            if (!Types().IsCompatible(value, current_return_type_)) {
                Diags().Report(ret->loc, "Return type mismatch");
            }
            return;
        }
        if (auto expr = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
            AnalyzeExpr(expr->expr);
            return;
        }
        if (auto ifs = std::dynamic_pointer_cast<IfStatement>(stmt)) {
            AnalyzeExpr(ifs->condition);
            AnalyzeBlock(ifs->then_body);
            AnalyzeBlock(ifs->else_body);
            return;
        }
        if (auto wh = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
            AnalyzeExpr(wh->condition);
            AnalyzeBlock(wh->body);
            return;
        }
        if (auto fr = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            AnalyzeStmt(fr->init);
            AnalyzeExpr(fr->condition);
            AnalyzeExpr(fr->increment);
            AnalyzeBlock(fr->body);
            ExitScope();
            return;
        }
        if (auto rf = std::dynamic_pointer_cast<RangeForStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            AnalyzeStmt(rf->loop_var);
            AnalyzeExpr(rf->range);
            AnalyzeBlock(rf->body);
            ExitScope();
            return;
        }
        if (auto comp = std::dynamic_pointer_cast<CompoundStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            for (auto &s : comp->statements) AnalyzeStmt(s);
            ExitScope();
            return;
        }
    }

    void AnalyzeBlock(const std::vector<std::shared_ptr<Statement>> &stmts) {
        EnterScope(ScopeKind::kBlock);
        for (auto &s : stmts) AnalyzeStmt(s);
        ExitScope();
    }

    Type AnalyzeExpr(const std::shared_ptr<Expression> &expr) {
        if (!expr) return Type::Invalid();
        if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
            auto resolved = Syms().Lookup(id->name);
            if (resolved.has_value()) {
                if (resolved->scope_distance > 0 && InFunction()) {
                    Syms().MarkCaptured(resolved->symbol);
                }
                return resolved->symbol->type;
            }
            Diags().Report(id->loc, "Undefined identifier: " + id->name);
            return Type::Invalid();
        }
        if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
            if (!lit->value.empty() && (isdigit(lit->value[0]) || lit->value[0] == '-'))
                return Type::Float();
            return Type::String();
        }
        if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
            auto callee_t = AnalyzeExpr(call->callee);
            std::vector<Type> arg_types;
            for (auto &a : call->args) arg_types.push_back(AnalyzeExpr(a));
            // If callee is identifier, attempt overload resolution.
            if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
                if (auto resolved = Syms().ResolveFunction(id->name, arg_types, Types())) {
                    callee_t = resolved->symbol->type;
                }
            }
            if (callee_t.kind == core::TypeKind::kFunction && callee_t.type_args.size() >= 1) {
                TypeUnifier uf;
                size_t param_count = callee_t.type_args.size() - 1;
                if (param_count != arg_types.size()) {
                    Diags().Report(call->loc, "Argument count mismatch");
                } else {
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        const auto &expected = callee_t.type_args[i + 1];
                        if (!uf.Unify(expected, arg_types[i])) {
                            Diags().Report(call->loc, "Argument type mismatch");
                            break;
                        }
                    }
                }
                Type ret = uf.Apply(callee_t.type_args[0]);
                if (!ret.IsConcrete()) {
                    Diags().Report(call->loc, "Cannot infer template argument(s)");
                    return Type::Any();
                }
                return ret;
            }
            return Type::Any();
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
            auto lhs = AnalyzeExpr(bin->left);
            auto rhs = AnalyzeExpr(bin->right);
            if (!Types().IsCompatible(lhs, rhs)) {
                Diags().Report(bin->loc, "Type mismatch in binary expression");
            }
            return lhs.IsNumeric() ? lhs : rhs;
        }
        if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
            return AnalyzeExpr(un->operand);
        }
        if (auto mem = std::dynamic_pointer_cast<MemberExpression>(expr)) {
            AnalyzeExpr(mem->object);
            auto obj_t = AnalyzeExpr(mem->object);
            if (auto member_res = Syms().LookupMember(obj_t.name, mem->member)) {
                return member_res->symbol->type;
            }
            Diags().Report(mem->loc, "Unknown member: " + mem->member);
            return Type::Any();
        }
        if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
            auto obj_t = AnalyzeExpr(idx->object);
            AnalyzeExpr(idx->index);
            if (!obj_t.type_args.empty() &&
                (obj_t.kind == core::TypeKind::kPointer || obj_t.kind == core::TypeKind::kReference)) {
                return obj_t.type_args.front();
            }
            return Type::Any();
        }
        if (auto cond = std::dynamic_pointer_cast<ConditionalExpression>(expr)) {
            AnalyzeExpr(cond->condition);
            auto t1 = AnalyzeExpr(cond->then_expr);
            auto t2 = AnalyzeExpr(cond->else_expr);
            return Types().IsCompatible(t1, t2) ? t1 : Type::Any();
        }
        if (auto lam = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
            // mark captured ids
            for (auto &cap : lam->captures) {
                auto resolved = Syms().Lookup(cap);
                if (resolved.has_value()) Syms().MarkCaptured(resolved->symbol);
            }
            scope_stack_.push_back({ScopeKind::kFunction});
            Syms().EnterScope("<lambda>", ScopeKind::kFunction);
            for (auto &p : lam->params) {
                Symbol ps{p.name, MapType(p.type), lam->loc, SymbolKind::kParameter, "cpp"};
                Syms().Declare(ps);
            }
            for (auto &st : lam->body) AnalyzeStmt(st);
            Syms().ExitScope();
            scope_stack_.pop_back();
            return Types().FunctionType("lambda");
        }
        return Type::Any();
    }

    void EnterScope(ScopeKind kind) {
        scope_stack_.push_back({kind});
        Syms().EnterScope("<block>", kind);
    }

    void ExitScope() {
        Syms().ExitScope();
        if (!scope_stack_.empty()) scope_stack_.pop_back();
    }

    bool InFunction() const {
        for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
            if (it->kind == ScopeKind::kFunction) return true;
        }
        return false;
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    std::vector<ScopeState> scope_stack_{};
    Type current_return_type_{Type::Any()};
};

}  // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer analyzer(module, context);
    analyzer.Run();
}

}  // namespace polyglot::cpp
