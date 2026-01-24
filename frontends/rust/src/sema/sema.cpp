#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/rust/include/rust_sema.h"

namespace polyglot::rust {

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
        Syms().EnterScope("<rust-module>", ScopeKind::kModule);
                borrow_stack_.push_back({});
        for (auto &item : module_.items) AnalyzeItem(item);
        Syms().ExitScope();
        scope_stack_.pop_back();
                borrow_stack_.pop_back();
    }

  private:
    frontends::Diagnostics &Diags() { return ctx_.Diags(); }
    core::TypeSystem &Types() { return ctx_.Types(); }
    core::SymbolTable &Syms() { return ctx_.Symbols(); }

    struct BorrowInfo {
        int imm{0};
        int mut{0};
    };

    std::vector<std::unordered_map<std::string, BorrowInfo>> borrow_stack_{};
        std::unordered_map<std::string, std::unordered_map<std::string, Type>> trait_methods_{};
        std::unordered_map<std::string, std::vector<std::string>> impl_traits_{};

    Type MapType(const std::shared_ptr<TypeNode> &node) {
        if (!node) return Type::Any();
        if (auto tp = std::dynamic_pointer_cast<TypePath>(node)) {
            std::string fq;
            for (size_t i = 0; i < tp->segments.size(); ++i) {
                if (i) fq += "::";
                fq += tp->segments[i];
            }
            if (!tp->generic_args.empty()) {
                std::vector<Type> args;
                for (auto &arg_list : tp->generic_args) {
                    for (auto &arg : arg_list) args.push_back(MapType(arg));
                }
                return Types().Generic(fq, args, "rust");
            }
            return Types().MapFromLanguage("rust", fq);
        }
        if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node)) {
            Type t = Types().PointerTo(MapType(ref->inner));
            t.lifetime = ref->lifetime ? ref->lifetime->name : "anon";
            return t;
        }
        if (auto sl = std::dynamic_pointer_cast<SliceType>(node)) {
            return Types().PointerTo(MapType(sl->inner));
        }
        if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
            return Types().PointerTo(MapType(arr->inner));
        }
        if (auto tup = std::dynamic_pointer_cast<TupleType>(node)) {
            std::vector<Type> elems;
            for (auto &e : tup->elements) elems.push_back(MapType(e));
            return Types().TupleOf(elems);
        }
        if (auto fn = std::dynamic_pointer_cast<FunctionType>(node)) {
            std::vector<Type> params;
            for (auto &p : fn->params) params.push_back(MapType(p));
            auto ret = MapType(fn->return_type);
            return Types().FunctionType("fn", ret, params);
        }
        return Type::Any();
    }

    std::string MapTypeName(const Type &t) {
        if (t.type_args.empty()) return t.name;
        std::string res = t.name + "<";
        for (size_t i = 0; i < t.type_args.size(); ++i) {
            if (i) res += ",";
            res += MapTypeName(t.type_args[i]);
        }
        res += ">";
        return res;
    }

    void AnalyzeItem(const std::shared_ptr<Statement> &item) {
        if (!item) return;
        if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
            DeclareFunction(*fn);
            if (fn->has_body) AnalyzeFunction(*fn);
            return;
        }
        if (auto st = std::dynamic_pointer_cast<StructItem>(item)) {
            Type t = Type::Struct(st->name, "rust");
            Symbol sym{st->name, t, st->loc, SymbolKind::kTypeName, "rust"};
            Syms().Declare(sym);
            scope_stack_.push_back({ScopeKind::kClass});
            int sid = Syms().EnterScope(st->name, ScopeKind::kClass);
            Syms().RegisterTypeScope(st->name, sid);
            for (auto &f : st->fields) {
                Symbol fs{f.name, MapType(f.type), st->loc, SymbolKind::kField, "rust"};
                Syms().Declare(fs);
            }
            Syms().ExitScope();
            scope_stack_.pop_back();
            return;
        }
        if (auto en = std::dynamic_pointer_cast<EnumItem>(item)) {
            Type t = Type::Enum(en->name, "rust");
            Symbol sym{en->name, t, en->loc, SymbolKind::kTypeName, "rust"};
            Syms().Declare(sym);
            for (auto &v : en->variants) {
                Symbol vs{v.name, t, en->loc, SymbolKind::kVariable, "rust"};
                Syms().Declare(vs);
            }
            return;
        }
        if (auto impl = std::dynamic_pointer_cast<ImplItem>(item)) {
            Type target = MapType(impl->target_type);
            scope_stack_.push_back({ScopeKind::kClass});
            int sid = Syms().EnterScope(MapTypeName(target), ScopeKind::kClass);
            Syms().RegisterTypeScope(MapTypeName(target), sid);
            if (impl->trait_type) {
                std::string trait_name = MapTypeName(MapType(impl->trait_type));
                impl_traits_[MapTypeName(target)].push_back(trait_name);
            }
            for (auto &m : impl->items) AnalyzeItem(m);
            Syms().ExitScope();
            scope_stack_.pop_back();
            return;
        }
        if (auto alias = std::dynamic_pointer_cast<TypeAliasItem>(item)) {
            Symbol ts{alias->name, MapType(alias->alias), alias->loc, SymbolKind::kTypeName, "rust"};
            Syms().Declare(ts);
            return;
        }
        if (auto rmod = std::dynamic_pointer_cast<ModItem>(item)) {
            scope_stack_.push_back({ScopeKind::kModule});
            Syms().EnterScope(rmod->name, ScopeKind::kModule);
            borrow_stack_.push_back({});
            for (auto &m : rmod->items) AnalyzeItem(m);
            Syms().ExitScope();
            scope_stack_.pop_back();
            borrow_stack_.pop_back();
            return;
        }
        if (auto ruse = std::dynamic_pointer_cast<UseDeclaration>(item)) {
            Symbol us{ruse->path, Type::Module(ruse->path, "rust"), ruse->loc, SymbolKind::kModule, "rust"};
            Syms().Declare(us);
            return;
        }
        if (auto trait = std::dynamic_pointer_cast<TraitItem>(item)) {
            for (auto &m : trait->items) {
                if (auto fn = std::dynamic_pointer_cast<FunctionItem>(m)) {
                    std::vector<Type> params;
                    for (auto &p : fn->params) params.push_back(MapType(p.type));
                    Type ret = MapType(fn->return_type);
                    trait_methods_[trait->name][fn->name] = Types().FunctionType(fn->name, ret, params);
                }
            }
            return;
        }
        if (auto cn = std::dynamic_pointer_cast<ConstItem>(item)) {
            Symbol cs{cn->name, MapType(cn->type), cn->loc, SymbolKind::kVariable, "rust"};
            Syms().Declare(cs);
            AnalyzeExpr(cn->value);
            return;
        }
        if (auto mr = std::dynamic_pointer_cast<MacroRulesItem>(item)) {
            Symbol ms{mr->name, Type::Any(), mr->loc, SymbolKind::kFunction, "rust"};
            Syms().Declare(ms);
            return;
        }
    }

    void DeclareFunction(const FunctionItem &fn) {
        std::vector<Type> params;
        for (auto &p : fn.params) params.push_back(MapType(p.type));
        Type ret = MapType(fn.return_type);
    Type fnt = Types().FunctionType(fn.name, ret, params);
    fnt.language = "rust";
        Symbol sym{fn.name, fnt, fn.loc, SymbolKind::kFunction, "rust"};
        if (!Syms().Declare(sym)) {
            Diags().Report(fn.loc, "Duplicate function: " + fn.name);
        }
    }

    void AnalyzeFunction(const FunctionItem &fn) {
        scope_stack_.push_back({ScopeKind::kFunction});
        Syms().EnterScope(fn.name, ScopeKind::kFunction);
        current_return_type_ = MapType(fn.return_type);
        saw_return_ = false;
        borrow_stack_.push_back({});

        for (auto &p : fn.params) {
            Symbol param{p.name, MapType(p.type), fn.loc, SymbolKind::kParameter, "rust"};
            Syms().Declare(param);
            borrow_stack_.back()[p.name] = {};
        }
        if (fn.has_body) {
            for (auto &stmt : fn.body) AnalyzeStmt(stmt);
        }

        // basic borrow/lifetime note: if return is a pointer with empty lifetime, warn (placeholder)
        if (current_return_type_.kind == core::TypeKind::kPointer && current_return_type_.lifetime.empty()) {
            Diags().Report(fn.loc, "Return reference without lifetime (placeholder borrow check)");
        }
        if (!saw_return_ && current_return_type_.kind != core::TypeKind::kVoid && current_return_type_.kind != core::TypeKind::kAny) {
            Diags().Report(fn.loc, "Function may exit without returning required value");
        }
        Syms().ExitScope();
        scope_stack_.pop_back();
        borrow_stack_.pop_back();
    }

    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;
        if (auto let = std::dynamic_pointer_cast<LetStatement>(stmt)) {
            auto t = MapType(let->type_annotation);
            if (t.kind == core::TypeKind::kAny && let->init) t = AnalyzeExpr(let->init);
            DeclarePattern(let->pattern, t, let->loc);
            if (t.kind == core::TypeKind::kPointer && t.lifetime.empty()) {
                Diags().Report(let->loc, "Borrow without lifetime annotation (placeholder)");
            }
            return;
        }
        if (auto exprs = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
            AnalyzeExpr(exprs->expr);
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            Type value = AnalyzeExpr(ret->value);
            if (!Types().IsCompatible(value, current_return_type_)) {
                Diags().Report(ret->loc, "Return type mismatch");
            }
            saw_return_ = true;
            return;
        }
        if (auto br = std::dynamic_pointer_cast<BreakStatement>(stmt)) {
            AnalyzeExpr(br->value);
            return;
        }
        if (auto cont = std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
            (void)cont;
            return;
        }
        if (auto loop = std::dynamic_pointer_cast<LoopStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            borrow_stack_.push_back({});
            for (auto &s : loop->body) AnalyzeStmt(s);
            ExitScope();
            borrow_stack_.pop_back();
            return;
        }
        if (auto fr = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            borrow_stack_.push_back({});
            DeclarePattern(fr->pattern, Type::Any(), fr->loc);
            AnalyzeExpr(fr->iterable);
            for (auto &s : fr->body) AnalyzeStmt(s);
            ExitScope();
            borrow_stack_.pop_back();
            return;
        }
        if (auto ife = std::dynamic_pointer_cast<IfExpression>(stmt)) {
            AnalyzeExpr(ife->condition);
            AnalyzeBlock(ife->then_body);
            AnalyzeBlock(ife->else_body);
            return;
        }
        if (auto wh = std::dynamic_pointer_cast<WhileExpression>(stmt)) {
            AnalyzeExpr(wh->condition);
            AnalyzeBlock(wh->body);
            return;
        }
        if (auto blk = std::dynamic_pointer_cast<BlockExpression>(stmt)) {
            AnalyzeBlock(blk->statements);
            return;
        }
        if (auto mt = std::dynamic_pointer_cast<MatchExpression>(stmt)) {
            AnalyzeExpr(mt->scrutinee);
            for (auto &arm : mt->arms) {
                EnterScope(ScopeKind::kBlock);
                DeclarePattern(arm->pattern, Type::Any(), arm->loc);
                AnalyzeExpr(arm->guard);
                AnalyzeExpr(arm->body);
                ExitScope();
            }
            if (!IsExhaustive(mt)) {
                Diags().Report(mt->loc, "Non-exhaustive match");
            }
            return;
        }
    }

    void AnalyzeBlock(const std::vector<std::shared_ptr<Statement>> &stmts) {
        EnterScope(ScopeKind::kBlock);
        borrow_stack_.push_back({});
        for (auto &s : stmts) AnalyzeStmt(s);
        ExitScope();
        borrow_stack_.pop_back();
    }

    void DeclarePattern(const std::shared_ptr<Pattern> &pat, const Type &type,
                        const core::SourceLoc &loc) {
        if (!pat) return;
        if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(pat)) {
            Symbol sym{id->name, type, loc, SymbolKind::kVariable, "rust"};
            Syms().Declare(sym);
            if (!borrow_stack_.empty()) borrow_stack_.back()[id->name] = {};
            return;
        }
        if (auto bind = std::dynamic_pointer_cast<BindingPattern>(pat)) {
            Symbol sym{bind->name, type, loc, SymbolKind::kVariable, "rust"};
            Syms().Declare(sym);
            if (!borrow_stack_.empty()) borrow_stack_.back()[bind->name] = {};
            if (bind->pattern) DeclarePattern(bind->pattern, type, loc);
            return;
        }
        if (auto tup = std::dynamic_pointer_cast<TuplePattern>(pat)) {
            size_t i = 0;
            for (auto &elem : tup->elements) {
                DeclarePattern(elem, Type::Any(), loc);
                ++i;
            }
            return;
        }
        if (auto strct = std::dynamic_pointer_cast<StructPattern>(pat)) {
            for (auto &f : strct->fields) {
                DeclarePattern(f.pattern, Type::Any(), loc);
            }
            return;
        }
        if (auto orp = std::dynamic_pointer_cast<OrPattern>(pat)) {
            for (auto &p : orp->patterns) DeclarePattern(p, type, loc);
            return;
        }
    }

    Type AnalyzeExpr(const std::shared_ptr<Expression> &expr) {
        if (!expr) return Type::Invalid();
        if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
            auto resolved = Syms().Lookup(id->name);
            if (resolved.has_value()) {
                if (resolved->scope_distance > 0 && InFunction()) Syms().MarkCaptured(resolved->symbol);
                auto active = AggregateBorrow(id->name);
                if (active.mut > 0) {
                    Diags().Report(id->loc, "cannot immutably use `" + id->name + "` while mutably borrowed");
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
        if (auto path = std::dynamic_pointer_cast<PathExpression>(expr)) {
            std::string fq;
            for (size_t i = 0; i < path->segments.size(); ++i) {
                if (i) fq += "::";
                fq += path->segments[i];
            }
            auto resolved = Syms().Lookup(fq);
            if (resolved.has_value()) return resolved->symbol->type;
            return Types().MapFromLanguage("rust", fq);
        }
        if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
            auto callee_t = AnalyzeExpr(call->callee);
            std::vector<Type> arg_types;
            for (auto &a : call->args) arg_types.push_back(AnalyzeExpr(a));
            if (callee_t.kind == core::TypeKind::kFunction && callee_t.type_args.size() >= 1) {
                size_t param_count = callee_t.type_args.size() - 1;
                if (param_count != arg_types.size()) {
                    Diags().Report(call->loc, "Argument count mismatch");
                } else {
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        const auto &expected = callee_t.type_args[i + 1];
                        if (expected.kind == core::TypeKind::kGenericParam && arg_types[i].IsConcrete()) {
                            // allow inference by concreteness
                        } else if (!Types().IsCompatible(arg_types[i], expected)) {
                            Diags().Report(call->loc, "Argument type mismatch");
                            break;
                        }
                        if (arg_types[i].kind == core::TypeKind::kPointer && arg_types[i].lifetime.empty()) {
                            Diags().Report(call->loc, "Passing reference without lifetime (placeholder borrow check)");
                        }
                    }
                }
                return callee_t.type_args[0];
            }
            return Type::Any();
        }
        if (auto assign = std::dynamic_pointer_cast<AssignmentExpression>(expr)) {
            // check conflicts before assignment
            if (auto lhs_id = std::dynamic_pointer_cast<Identifier>(assign->left)) {
                auto active = AggregateBorrow(lhs_id->name);
                if (active.mut > 0 || active.imm > 0) {
                    Diags().Report(assign->loc, "cannot assign to `" + lhs_id->name + "` while it is borrowed");
                }
            }
            auto lhs_t = AnalyzeExpr(assign->left);
            auto rhs_t = AnalyzeExpr(assign->right);
            return lhs_t.kind != core::TypeKind::kInvalid ? lhs_t : rhs_t;
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
            if (un->op == "&" || un->op == "&mut") {
                auto inner = AnalyzeExpr(un->operand);
                std::string name;
                if (auto id = std::dynamic_pointer_cast<Identifier>(un->operand)) name = id->name;
                if (!name.empty()) {
                    auto active = AggregateBorrow(name);
                    auto &local = EnsureLocalBorrow(name);
                    if (un->op == "&mut") {
                        if (active.imm > 0 || active.mut > 0) {
                            Diags().Report(un->loc, "cannot take mutable borrow while borrowed");
                        } else {
                            local.mut += 1;
                        }
                        inner = Types().ReferenceTo(inner, false, false, false);
                    } else {
                        if (active.mut > 0) {
                            Diags().Report(un->loc, "cannot take shared borrow while mutable borrow active");
                        }
                        local.imm += 1;
                        inner = Types().ReferenceTo(inner, false, true, false);
                    }
                }
                return inner;
            }
            return AnalyzeExpr(un->operand);
        }
        if (auto mem = std::dynamic_pointer_cast<MemberExpression>(expr)) {
            auto obj_t = AnalyzeExpr(mem->object);
            if (auto member_res = Syms().LookupMember(obj_t.name, mem->member)) {
                return member_res->symbol->type;
            }
            auto impl_it = impl_traits_.find(obj_t.name);
            if (impl_it != impl_traits_.end()) {
                for (auto &tr : impl_it->second) {
                    auto tm = trait_methods_.find(tr);
                    if (tm != trait_methods_.end()) {
                        auto mf = tm->second.find(mem->member);
                        if (mf != tm->second.end()) return mf->second;
                    }
                }
            }
            Diags().Report(mem->loc, "Unknown member: " + mem->member);
            return Type::Any();
        }
        if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
            AnalyzeExpr(idx->object);
            AnalyzeExpr(idx->index);
            return Type::Any();
        }
        if (auto cls = std::dynamic_pointer_cast<ClosureExpression>(expr)) {
            scope_stack_.push_back({ScopeKind::kFunction});
            Syms().EnterScope("<closure>", ScopeKind::kFunction);
            for (auto &p : cls->params) {
                Symbol ps{p.name, MapType(p.type), cls->loc, SymbolKind::kParameter, "rust"};
                Syms().Declare(ps);
            }
            AnalyzeExpr(cls->body);
            Syms().ExitScope();
            scope_stack_.pop_back();
            return Types().FunctionType("closure");
        }
        if (auto blk = std::dynamic_pointer_cast<BlockExpression>(expr)) {
            AnalyzeBlock(blk->statements);
            return Type::Any();
        }
        if (auto mt = std::dynamic_pointer_cast<MatchExpression>(expr)) {
            AnalyzeExpr(mt->scrutinee);
            for (auto &arm : mt->arms) {
                EnterScope(ScopeKind::kBlock);
                DeclarePattern(arm->pattern, Type::Any(), arm->loc);
                AnalyzeExpr(arm->guard);
                AnalyzeExpr(arm->body);
                ExitScope();
            }
            if (!IsExhaustive(mt)) {
                Diags().Report(mt->loc, "Non-exhaustive match");
            }
            return Type::Any();
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

    BorrowInfo AggregateBorrow(const std::string &name) const {
        BorrowInfo total{};
        for (auto it = borrow_stack_.rbegin(); it != borrow_stack_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                total.imm += found->second.imm;
                total.mut += found->second.mut;
            }
        }
        return total;
    }

    BorrowInfo &EnsureLocalBorrow(const std::string &name) {
        if (borrow_stack_.empty()) {
            static BorrowInfo dummy;
            return dummy;
        }
        return borrow_stack_.back()[name];
    }

    bool IsWildcardPattern(const std::shared_ptr<Pattern> &pat) const {
        if (!pat) return false;
        if (std::dynamic_pointer_cast<WildcardPattern>(pat)) return true;
        if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(pat)) return id->name == "_";
        if (auto orp = std::dynamic_pointer_cast<OrPattern>(pat)) {
            for (auto &p : orp->patterns) if (IsWildcardPattern(p)) return true;
        }
        return false;
    }

    bool IsExhaustive(const std::shared_ptr<MatchExpression> &mt) const {
        if (!mt) return true;
        for (auto &arm : mt->arms) {
            if (IsWildcardPattern(arm->pattern)) return true;
        }
        return false;
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    std::vector<ScopeState> scope_stack_{};
    Type current_return_type_{Type::Any()};
    bool saw_return_{false};
};

}  // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer analyzer(module, context);
    analyzer.Run();
}

}  // namespace polyglot::rust
