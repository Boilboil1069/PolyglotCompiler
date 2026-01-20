#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontends/common/include/sema_context.h"

#include "frontends/python/include/python_ast.h"

namespace polyglot::python {

using polyglot::core::ScopeKind;
using polyglot::core::Symbol;
using polyglot::core::SymbolKind;
using polyglot::core::Type;

namespace {

struct ScopeState {
    ScopeKind kind{ScopeKind::kBlock};
    std::unordered_set<std::string> globals;
    std::unordered_set<std::string> nonlocals;
};

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx) : module_(mod), ctx_(ctx) {}

    void Run() {
        scope_states_.push_back({ScopeKind::kModule});
        ctx_.Symbols().EnterScope("<module>", ScopeKind::kModule);
        for (const auto &stmt : module_.body) {
            AnalyzeStmt(stmt);
        }
        ctx_.Symbols().ExitScope();
        scope_states_.pop_back();
    }

  private:
    frontends::Diagnostics &Diags() { return ctx_.Diags(); }
    core::TypeSystem &Types() { return ctx_.Types(); }
    core::SymbolTable &Syms() { return ctx_.Symbols(); }

    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;

        if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
            DeclareFunction(*fn);
            AnalyzeFunction(*fn);
            return;
        }
        if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
            DeclareClass(*cls);
            AnalyzeClass(*cls);
            return;
        }
        if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
            auto val_type = AnalyzeExpr(assign->value);
            for (const auto &target : assign->targets) {
                DeclareOrAssign(target, val_type, assign->loc);
            }
            return;
        }
        if (auto expr = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
            AnalyzeExpr(expr->expr);
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            last_return_type_ = AnalyzeExpr(ret->value);
            return;
        }
        if (auto glob = std::dynamic_pointer_cast<GlobalStatement>(stmt)) {
            for (const auto &name : glob->names) {
                CurrentScopeState().globals.insert(name);
            }
            return;
        }
        if (auto nonloc = std::dynamic_pointer_cast<NonlocalStatement>(stmt)) {
            for (const auto &name : nonloc->names) {
                CurrentScopeState().nonlocals.insert(name);
            }
            return;
        }
        // Recurse into compound statements (if/while/for/with/try/match) with new block scopes
        if (auto ifs = std::dynamic_pointer_cast<IfStatement>(stmt)) {
            AnalyzeExpr(ifs->condition);
            AnalyzeBlock(ifs->then_body);
            AnalyzeBlock(ifs->else_body);
            return;
        }
        if (auto w = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
            AnalyzeExpr(w->condition);
            AnalyzeBlock(w->body);
            return;
        }
        if (auto f = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            AnalyzeExpr(f->iterable);
            EnterBlockScope(ScopeKind::kBlock);
            DeclareOrAssign(f->target, Type::Any(), f->loc);
            AnalyzeBlock(f->body);
            ExitScope();
            return;
        }
        if (auto with = std::dynamic_pointer_cast<WithStatement>(stmt)) {
            EnterBlockScope(ScopeKind::kBlock);
            for (auto &item : with->items) {
                AnalyzeExpr(item.context_expr);
                DeclareOrAssign(item.optional_vars, Type::Any(), stmt->loc);
            }
            AnalyzeBlock(with->body);
            ExitScope();
            return;
        }
        if (auto t = std::dynamic_pointer_cast<TryStatement>(stmt)) {
            AnalyzeBlock(t->body);
            for (auto &h : t->handlers) {
                EnterBlockScope(ScopeKind::kBlock);
                AnalyzeExpr(h.type);
                if (!h.name.empty()) {
                    DeclareSimple(h.name, SymbolKind::kVariable, Type::Any(), stmt->loc);
                }
                AnalyzeBlock(h.body);
                ExitScope();
            }
            AnalyzeBlock(t->orelse);
            AnalyzeBlock(t->finalbody);
            return;
        }
        if (auto m = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
            AnalyzeExpr(m->subject);
            for (auto &c : m->cases) {
                EnterBlockScope(ScopeKind::kBlock);
                AnalyzeExpr(c.pattern);
                AnalyzeExpr(c.guard);
                AnalyzeBlock(c.body);
                ExitScope();
            }
            return;
        }
        // Other statements: best effort traverse expressions
        if (auto r = std::dynamic_pointer_cast<RaiseStatement>(stmt)) {
            AnalyzeExpr(r->value);
            AnalyzeExpr(r->from_expr);
            return;
        }
        if (auto a = std::dynamic_pointer_cast<AssertStatement>(stmt)) {
            AnalyzeExpr(a->test);
            AnalyzeExpr(a->msg);
            return;
        }
        if (auto imp = std::dynamic_pointer_cast<ImportStatement>(stmt)) {
            for (auto &al : imp->names) {
                std::string name = al.alias.empty() ? al.name : al.alias;
                DeclareSimple(name, SymbolKind::kModule, Type{core::TypeKind::kModule, name},
                              stmt->loc);
            }
            return;
        }
    }

    void AnalyzeBlock(const std::vector<std::shared_ptr<Statement>> &block) {
        EnterBlockScope(ScopeKind::kBlock);
        for (const auto &stmt : block) {
            AnalyzeStmt(stmt);
        }
        ExitScope();
    }

    void DeclareFunction(const FunctionDef &fn) {
        std::vector<Type> params;
        for (const auto &p : fn.params) {
            params.push_back(Type::Any());
        }
        Type ret = fn.return_annotation ? AnalyzeExpr(fn.return_annotation) : Type::Any();
        Symbol sym{fn.name, Types().FunctionType(fn.name, ret, params), fn.loc, SymbolKind::kFunction,
                   "python"};
        if (!Syms().Declare(sym)) {
            Diags().Report(fn.loc, "Duplicate function: " + fn.name);
        }
    }

    void AnalyzeFunction(const FunctionDef &fn) {
        scope_states_.push_back({ScopeKind::kFunction});
        Syms().EnterScope(fn.name, ScopeKind::kFunction);

        for (const auto &p : fn.params) {
            Symbol param{p.name, Type::Any(), fn.loc, SymbolKind::kParameter, "python"};
            if (!Syms().Declare(param)) {
                Diags().Report(fn.loc, "Duplicate parameter: " + p.name);
            }
        }
        for (const auto &stmt : fn.body) {
            AnalyzeStmt(stmt);
        }

        if (fn.return_annotation) {
            Type expected = AnalyzeExpr(fn.return_annotation);
            if (!Types().IsCompatible(last_return_type_, expected)) {
                Diags().Report(fn.loc, "Function return type mismatch annotation");
            }
        }

        Syms().ExitScope();
        scope_states_.pop_back();
    }

    void DeclareClass(const ClassDef &cls) {
        Symbol sym{cls.name, Type{core::TypeKind::kClass, cls.name}, cls.loc, SymbolKind::kTypeName,
                   "python"};
        if (!Syms().Declare(sym)) {
            Diags().Report(cls.loc, "Duplicate class: " + cls.name);
        }
    }

    void AnalyzeClass(const ClassDef &cls) {
        scope_states_.push_back({ScopeKind::kClass});
        Syms().EnterScope(cls.name, ScopeKind::kClass);
        for (const auto &stmt : cls.body) {
            AnalyzeStmt(stmt);
        }
        Syms().ExitScope();
        scope_states_.pop_back();
    }

    void DeclareOrAssign(const std::shared_ptr<Expression> &target, const Type &type,
                         const core::SourceLoc &loc) {
        if (!target) return;
        if (auto ident = std::dynamic_pointer_cast<Identifier>(target)) {
            DeclareName(ident->name, type, loc, SymbolKind::kVariable);
            return;
        }
        if (auto tuple = std::dynamic_pointer_cast<TupleExpression>(target)) {
            for (auto &elem : tuple->elements) {
                DeclareOrAssign(elem, type, loc);
            }
        }
        if (auto list = std::dynamic_pointer_cast<ListExpression>(target)) {
            for (auto &elem : list->elements) {
                DeclareOrAssign(elem, type, loc);
            }
        }
    }

    void DeclareName(const std::string &name, const Type &type, const core::SourceLoc &loc,
                     SymbolKind kind) {
        Symbol sym{name, type, loc, kind, "python"};

        // Global: declare in module scope.
        if (CurrentScopeState().globals.count(name) > 0) {
            if (!Syms().DeclareInScope(Syms().GlobalScopeId(), sym)) {
                Diags().Report(loc, "Duplicate global: " + name);
            }
            return;
        }

        // Nonlocal: ensure it exists in an enclosing scope (not global).
        if (CurrentScopeState().nonlocals.count(name) > 0) {
            auto resolved = Syms().Lookup(name);
            if (!resolved.has_value() || resolved->scope_distance == 0) {
                Diags().Report(loc, "No binding for nonlocal name: " + name);
                return;
            }
            Syms().MarkCaptured(resolved->symbol);
            return;
        }

        if (!Syms().Declare(sym)) {
            auto resolved = Syms().Lookup(name);
            if (!resolved.has_value()) {
                Diags().Report(loc, "Duplicate or invalid declaration: " + name);
            }
        }
    }

    Type AnalyzeExpr(const std::shared_ptr<Expression> &expr) {
        if (!expr) return Type::Invalid();
        if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
            auto resolved = Syms().Lookup(id->name);
            if (resolved.has_value()) {
                // Closure capture: if symbol is in outer function and current scope is function
                if (resolved->scope_distance > 0 && InFunction()) {
                    Syms().MarkCaptured(resolved->symbol);
                }
                return resolved->symbol->type;
            }
            Diags().Report(id->loc, "Undefined name: " + id->name);
            return Type::Invalid();
        }
        if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
            if (lit->is_string) return Type::String();
            // crude numeric detection
            if (!lit->value.empty() && (isdigit(lit->value[0]) || lit->value[0] == '-')) {
                return Type::Float();
            }
            return Type::Any();
        }
        if (auto tup = std::dynamic_pointer_cast<TupleExpression>(expr)) {
            std::vector<Type> elems;
            for (auto &e : tup->elements) elems.push_back(AnalyzeExpr(e));
            return Types().TupleOf(elems);
        }
        if (auto list = std::dynamic_pointer_cast<ListExpression>(expr)) {
            std::vector<Type> elems;
            for (auto &e : list->elements) elems.push_back(AnalyzeExpr(e));
            Type element = elems.empty() ? Type::Any() : elems.front();
            return Types().Generic("list", {element}, "python");
        }
        if (auto setexpr = std::dynamic_pointer_cast<SetExpression>(expr)) {
            std::vector<Type> elems;
            for (auto &e : setexpr->elements) elems.push_back(AnalyzeExpr(e));
            Type element = elems.empty() ? Type::Any() : elems.front();
            return Types().Generic("set", {element}, "python");
        }
        if (auto dict = std::dynamic_pointer_cast<DictExpression>(expr)) {
            Type k = Type::Any();
            Type v = Type::Any();
            if (!dict->items.empty()) {
                k = AnalyzeExpr(dict->items.front().first);
                v = AnalyzeExpr(dict->items.front().second);
            }
            return Types().Generic("dict", {k, v}, "python");
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
            auto lhs = AnalyzeExpr(bin->left);
            auto rhs = AnalyzeExpr(bin->right);
            if (Types().IsCompatible(lhs, rhs)) return lhs.IsNumeric() ? lhs : rhs;
            Diags().Report(bin->loc, "Type mismatch in binary expression");
            return Type::Any();
        }
        if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
            return AnalyzeExpr(un->operand);
        }
        if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
            auto callee_t = AnalyzeExpr(call->callee);
            std::vector<Type> arg_types;
            for (auto &arg : call->args) {
                arg_types.push_back(AnalyzeExpr(arg.value));
            }
            if (callee_t.kind == core::TypeKind::kFunction && callee_t.type_args.size() >= 1) {
                size_t param_count = callee_t.type_args.size() - 1;
                if (param_count != arg_types.size()) {
                    Diags().Report(call->loc, "Argument count mismatch");
                } else {
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        const auto &expected = callee_t.type_args[i + 1];
                        if (expected.kind == core::TypeKind::kGenericParam && arg_types[i].IsConcrete()) {
                            continue;
                        }
                        if (!Types().IsCompatible(arg_types[i], expected)) {
                            Diags().Report(call->loc, "Argument type mismatch");
                            break;
                        }
                        if (!arg_types[i].IsConcrete()) {
                            Diags().Report(call->loc, "Cannot infer generic parameter (argument unknown)");
                            break;
                        }
                    }
                }
                return callee_t.type_args[0];
            }
            return Type::Any();
        }
        if (auto lambda = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
            // treat lambda as inline function scope
            scope_states_.push_back({ScopeKind::kFunction});
            Syms().EnterScope("<lambda>", ScopeKind::kFunction);
            for (auto &p : lambda->params) {
                Symbol param{p.name, Type::Any(), lambda->loc, SymbolKind::kParameter, "python"};
                Syms().Declare(param);
            }
            AnalyzeExpr(lambda->body);
            Syms().ExitScope();
            scope_states_.pop_back();
            return Types().FunctionType("lambda");
        }
        if (auto comp = std::dynamic_pointer_cast<ComprehensionExpression>(expr)) {
            EnterBlockScope(ScopeKind::kComprehension);
            for (auto &cl : comp->clauses) {
                AnalyzeExpr(cl.iterable);
                DeclareOrAssign(cl.target, Type::Any(), comp->loc);
                for (auto &cond : cl.ifs) {
                    AnalyzeExpr(cond);
                }
            }
            auto elem_t = AnalyzeExpr(comp->elem ? comp->elem : comp->key);
            ExitScope();
            return elem_t;
        }
        // Default fallback
        return Type::Any();
    }

    void EnterBlockScope(ScopeKind kind) {
        scope_states_.push_back({kind});
        Syms().EnterScope("<block>", kind);
    }

    void ExitScope() {
        Syms().ExitScope();
        if (!scope_states_.empty()) scope_states_.pop_back();
    }

    ScopeState &CurrentScopeState() { return scope_states_.back(); }

    bool InFunction() const {
        for (auto it = scope_states_.rbegin(); it != scope_states_.rend(); ++it) {
            if (it->kind == ScopeKind::kFunction) return true;
        }
        return false;
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    std::vector<ScopeState> scope_states_{};
    Type last_return_type_{Type::Any()};
};

}  // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer analyzer(module, context);
    analyzer.Run();
}

} // namespace polyglot::python
