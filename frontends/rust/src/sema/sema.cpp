/**
 * @file     sema.cpp
 * @brief    Rust language frontend implementation
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontends/rust/include/rust_sema.h"
#include "frontends/rust/include/crate_loader.h"

namespace polyglot::rust {

using polyglot::core::ScopeKind;
using polyglot::core::Symbol;
using polyglot::core::SymbolKind;
using polyglot::core::Type;

namespace {

// Scope state for tracking context during analysis
struct ScopeState {
    ScopeKind kind{ScopeKind::kBlock};
    std::string name{};  // Name of the scope for error messages
};

// Extended borrow information for comprehensive borrow checking
struct BorrowInfo {
    int imm{0};     // Count of active immutable borrows
    int mut{0};     // Count of active mutable borrows
    bool moved{false};  // Whether the value has been moved
    std::string lifetime;  // Associated lifetime if any
    core::SourceLoc loc;   // Location where borrow was created
};

// Lifetime information for lifetime analysis
struct LifetimeInfo {
    std::string name;
    bool is_static{false};
    int scope_depth{0};  // Depth at which lifetime is valid
    std::vector<std::string> outlives;  // Lifetimes this one must outlive
};

// Move state for tracking ownership
enum class OwnershipState {
    kOwned,         // Value is owned
    kMoved,         // Value has been moved
    kBorrowed,      // Value is currently borrowed
    kMutBorrowed    // Value is mutably borrowed
};

// Variable ownership tracking
struct VarOwnership {
    OwnershipState state{OwnershipState::kOwned};
    std::string borrow_holder;  // Who holds the borrow
    core::SourceLoc borrow_loc; // Where the borrow was created
    bool is_copy{false};        // Whether the type implements Copy
};

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx,
             CrateLoader *loader = nullptr)
        : module_(mod), ctx_(ctx), loader_(loader) {}

    void Run() {
        scope_stack_.push_back({ScopeKind::kModule, "<rust-module>"});
        Syms().EnterScope("<rust-module>", ScopeKind::kModule);
        borrow_stack_.push_back({});
        ownership_stack_.push_back({});
        lifetime_stack_.push_back({});
        current_scope_depth_ = 0;
        for (auto &item : module_.items) AnalyzeItem(item);
        Syms().ExitScope();
        scope_stack_.pop_back();
        borrow_stack_.pop_back();
        ownership_stack_.pop_back();
        lifetime_stack_.pop_back();
    }

  private:
    frontends::Diagnostics &Diags() { return ctx_.Diags(); }
    core::TypeSystem &Types() { return ctx_.Types(); }
    core::SymbolTable &Syms() { return ctx_.Symbols(); }

    // Borrow tracking stacks
    std::vector<std::unordered_map<std::string, BorrowInfo>> borrow_stack_{};
    // Ownership tracking stacks
    std::vector<std::unordered_map<std::string, VarOwnership>> ownership_stack_{};
    // Lifetime tracking stacks
    std::vector<std::unordered_map<std::string, LifetimeInfo>> lifetime_stack_{};
    // Trait method signatures
    std::unordered_map<std::string, std::unordered_map<std::string, Type>> trait_methods_{};
    // Types implementing traits
    std::unordered_map<std::string, std::vector<std::string>> impl_traits_{};
    // Copy types (types implementing Copy trait)
    std::unordered_set<std::string> copy_types_{"i8", "i16", "i32", "i64", "i128",
                                                  "u8", "u16", "u32", "u64", "u128",
                                                  "f32", "f64", "bool", "char"};
    // Current scope depth for lifetime analysis
    int current_scope_depth_{0};
    // Current function's lifetime parameters
    std::vector<std::string> current_fn_lifetimes_{};

    // Check if a type implements the Copy trait
    bool IsCopyType(const Type &t) const {
        if (t.kind == core::TypeKind::kInt || t.kind == core::TypeKind::kFloat ||
            t.kind == core::TypeKind::kBool) {
            return true;
        }
        return copy_types_.count(t.name) > 0;
    }

    // Register a lifetime in the current scope
    void RegisterLifetime(const std::string &name, bool is_static = false) {
        if (lifetime_stack_.empty()) return;
        LifetimeInfo info;
        info.name = name;
        info.is_static = is_static;
        info.scope_depth = current_scope_depth_;
        lifetime_stack_.back()[name] = info;
    }

    // Check if lifetime 'a outlives lifetime 'b
    bool LifetimeOutlives(const std::string &a, const std::string &b) const {
        if (a == "'static") return true;
        if (b == "'static") return false;
        if (a == b) return true;
        
        // Look up lifetime scopes
        int depth_a = -1, depth_b = -1;
        for (auto it = lifetime_stack_.rbegin(); it != lifetime_stack_.rend(); ++it) {
            if (auto found = it->find(a); found != it->end()) {
                depth_a = found->second.scope_depth;
            }
            if (auto found = it->find(b); found != it->end()) {
                depth_b = found->second.scope_depth;
            }
        }
        // Lifetime at lower scope depth outlives one at higher depth
        return depth_a <= depth_b;
    }

    // Validate lifetime for return type - comprehensive check
    bool ValidateReturnLifetime(const Type &ret_type, const std::vector<std::string> &fn_lifetimes,
                                 const core::SourceLoc &loc) {
        if (ret_type.kind != core::TypeKind::kPointer) return true;
        
        if (ret_type.lifetime.empty() || ret_type.lifetime == "anon") {
            // Reference return type must have explicit lifetime
            Diags().Report(loc, "returning reference requires explicit lifetime annotation");
            return false;
        }
        
        // Check if the lifetime is one of the function's lifetime parameters
        bool valid_lifetime = ret_type.lifetime == "'static";
        for (const auto &lt : fn_lifetimes) {
            if (ret_type.lifetime == lt || ret_type.lifetime == lt.substr(1)) {
                valid_lifetime = true;
                break;
            }
        }
        
        if (!valid_lifetime) {
            Diags().Report(loc, "return type has unknown lifetime '" + ret_type.lifetime + "'");
            return false;
        }
        
        return true;
    }

    // Validate borrow lifetime when passing references
    bool ValidateBorrowLifetime(const Type &arg_type, const std::string &param_lifetime,
                                 const core::SourceLoc &loc) {
        if (arg_type.kind != core::TypeKind::kPointer) return true;
        
        // If parameter expects a lifetime, arg must have compatible lifetime
        if (!param_lifetime.empty() && param_lifetime != "anon") {
            if (arg_type.lifetime.empty() || arg_type.lifetime == "anon") {
                Diags().Report(loc, "passing reference without lifetime to parameter expecting '" + 
                              param_lifetime + "'");
                return false;
            }
            
            // Check lifetime compatibility
            if (!LifetimeOutlives(arg_type.lifetime, param_lifetime)) {
                Diags().Report(loc, "lifetime '" + arg_type.lifetime + 
                              "' does not outlive '" + param_lifetime + "'");
                return false;
            }
        }
        
        return true;
    }

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
            // Always declare the path itself as a module symbol (legacy behaviour).
            Symbol us{ruse->path, Type::Module(ruse->path, "rust"), ruse->loc, SymbolKind::kModule, "rust"};
            Syms().Declare(us);

            // If a crate loader is configured, resolve concrete items so that
            // names brought into scope by `use crate_x::Foo` (or
            // `use crate_x::Foo as Bar`) become real symbols.
            if (loader_) {
                std::string path = ruse->path;
                std::string alias_name;
                // Handle "use a::b as c"
                auto as_pos = path.find(" as ");
                if (as_pos != std::string::npos) {
                    alias_name = path.substr(as_pos + 4);
                    // Strip whitespace
                    while (!alias_name.empty() && std::isspace(static_cast<unsigned char>(alias_name.front()))) alias_name.erase(0, 1);
                    while (!alias_name.empty() && std::isspace(static_cast<unsigned char>(alias_name.back())))  alias_name.pop_back();
                    path.erase(as_pos);
                }
                // Last segment is the bound name unless the user wrote `as`.
                std::string bind = alias_name;
                if (bind.empty()) {
                    auto sep = path.rfind("::");
                    bind = (sep == std::string::npos) ? path : path.substr(sep + 2);
                    // Strip glob/curly artefacts (e.g. "*", "{Foo, Bar}").
                    if (bind == "*" || (!bind.empty() && bind.front() == '{')) bind.clear();
                }

                if (const auto *item = loader_->ResolvePath(path)) {
                    if (!bind.empty()) {
                        SymbolKind kind = SymbolKind::kVariable;
                        switch (item->kind) {
                            case CrateItemKind::kFunction:
                            case CrateItemKind::kMacro:    kind = SymbolKind::kFunction; break;
                            case CrateItemKind::kStruct:
                            case CrateItemKind::kEnum:
                            case CrateItemKind::kTrait:
                            case CrateItemKind::kTypeAlias: kind = SymbolKind::kTypeName; break;
                            case CrateItemKind::kConst:
                            case CrateItemKind::kStatic:    kind = SymbolKind::kVariable; break;
                            case CrateItemKind::kModule:    kind = SymbolKind::kModule;   break;
                        }
                        Symbol sym{bind, item->type, ruse->loc, kind, "rust"};
                        Syms().Declare(sym);
                    }
                } else {
                    // Diagnose only when a crate loader is configured AND the
                    // crate prefix is one we have actually loaded — this
                    // avoids false positives for `use crate::self_mod::*`.
                    auto sep = path.find("::");
                    std::string head = (sep == std::string::npos) ? path : path.substr(0, sep);
                    if (loader_->ResolveCrate(head)) {
                        Diags().Report(ruse->loc, "Cannot resolve `use " + path + "` in crate `" + head + "`");
                    }
                }
            }
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
        // Fall through for expression statements and other statement kinds
        if (auto es = std::dynamic_pointer_cast<ExprStatement>(item)) {
            AnalyzeExpr(es->expr);
            return;
        }
        AnalyzeStmt(item);
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

    // Extract lifetime parameters from function type parameters
    std::vector<std::string> ExtractLifetimeParams(const std::vector<std::string> &type_params) {
        std::vector<std::string> lifetimes;
        for (const auto &tp : type_params) {
            if (!tp.empty() && tp[0] == '\'') {
                lifetimes.push_back(tp);
            }
        }
        return lifetimes;
    }

    void AnalyzeFunction(const FunctionItem &fn) {
        scope_stack_.push_back({ScopeKind::kFunction, fn.name});
        Syms().EnterScope(fn.name, ScopeKind::kFunction);
        current_return_type_ = MapType(fn.return_type);
        saw_return_ = false;
        borrow_stack_.push_back({});
        ownership_stack_.push_back({});
        lifetime_stack_.push_back({});
        current_scope_depth_++;

        // Extract and register lifetime parameters
        current_fn_lifetimes_ = ExtractLifetimeParams(fn.type_params);
        for (const auto &lt : current_fn_lifetimes_) {
            RegisterLifetime(lt);
        }
        // Always have 'static available
        RegisterLifetime("'static", true);

        // Process parameters and track their ownership
        for (auto &p : fn.params) {
            Type param_type = MapType(p.type);
            Symbol param{p.name, param_type, fn.loc, SymbolKind::kParameter, "rust"};
            Syms().Declare(param);
            
            // Initialize borrow tracking for parameter
            BorrowInfo binfo;
            binfo.lifetime = param_type.lifetime;
            binfo.loc = fn.loc;
            borrow_stack_.back()[p.name] = binfo;
            
            // Track ownership - parameters are owned unless borrowed
            VarOwnership ownership;
            ownership.is_copy = IsCopyType(param_type);
            if (param_type.kind == core::TypeKind::kPointer) {
                ownership.state = OwnershipState::kBorrowed;
            }
            ownership_stack_.back()[p.name] = ownership;
        }

        // Analyze function body
        if (fn.has_body) {
            for (auto &stmt : fn.body) AnalyzeStmt(stmt);
        }

        // Comprehensive return type lifetime validation
        if (current_return_type_.kind == core::TypeKind::kPointer) {
            if (current_return_type_.lifetime.empty() || current_return_type_.lifetime == "anon") {
                // Return reference must have explicit lifetime unless elision applies
                // For single parameter functions, lifetime elision allows implicit lifetime
                bool elision_applies = false;
                if (fn.params.size() == 1) {
                    Type pt = MapType(fn.params[0].type);
                    if (pt.kind == core::TypeKind::kPointer && !pt.lifetime.empty()) {
                        elision_applies = true;
                    }
                }
                // Check for &self parameter (method receiver)
                for (const auto &p : fn.params) {
                    if (p.name == "self") {
                        Type pt = MapType(p.type);
                        if (pt.kind == core::TypeKind::kPointer) {
                            elision_applies = true;
                            break;
                        }
                    }
                }
                
                if (!elision_applies && current_fn_lifetimes_.empty()) {
                    Diags().Report(fn.loc, "return type contains reference but function has no lifetime parameters");
                }
            } else {
                // Validate the return lifetime against function lifetime parameters
                ValidateReturnLifetime(current_return_type_, current_fn_lifetimes_, fn.loc);
            }
        }

        // Check that all non-void functions return
        if (!saw_return_ && current_return_type_.kind != core::TypeKind::kVoid && 
            current_return_type_.kind != core::TypeKind::kAny) {
            Diags().Report(fn.loc, "function may exit without returning required value");
        }

        // Clean up scopes
        current_scope_depth_--;
        current_fn_lifetimes_.clear();
        Syms().ExitScope();
        scope_stack_.pop_back();
        borrow_stack_.pop_back();
        ownership_stack_.pop_back();
        lifetime_stack_.pop_back();
    }

    void AnalyzeStmt(const std::shared_ptr<Statement> &stmt) {
        if (!stmt) return;
        if (auto let = std::dynamic_pointer_cast<LetStatement>(stmt)) {
            auto t = MapType(let->type_annotation);
            if (t.kind == core::TypeKind::kAny && let->init) t = AnalyzeExpr(let->init);
            
            // Track ownership for the new binding
            std::string var_name;
            if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(let->pattern)) {
                var_name = id->name;
            }
            
            DeclarePattern(let->pattern, t, let->loc);
            
            // Check for references without proper lifetime annotation
            if (t.kind == core::TypeKind::kPointer) {
                if (t.lifetime.empty() || t.lifetime == "anon") {
                    // Try to infer lifetime from the initializer
                    bool can_infer = false;
                    if (let->init) {
                        // If initializer is a reference to a local, inherit its lifetime
                        if (auto ref = std::dynamic_pointer_cast<UnaryExpression>(let->init)) {
                            if (ref->op == "&" || ref->op == "&mut") {
                                can_infer = true;  // Local reference - lifetime is current scope
                            }
                        }
                    }
                    if (!can_infer) {
                        Diags().Report(let->loc, "reference binding requires lifetime annotation or initialization from local reference");
                    }
                }
            }
            
            // Initialize ownership tracking for this variable
            if (!var_name.empty() && !ownership_stack_.empty()) {
                VarOwnership ownership;
                ownership.is_copy = IsCopyType(t);
                if (t.kind == core::TypeKind::kPointer) {
                    ownership.state = OwnershipState::kBorrowed;
                }
                ownership_stack_.back()[var_name] = ownership;
            }
            
            return;
        }
        if (auto exprs = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
            AnalyzeExpr(exprs->expr);
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            Type value = AnalyzeExpr(ret->value);
            
            // Check return value lifetime
            if (current_return_type_.kind == core::TypeKind::kPointer && ret->value) {
                // Ensure returned reference doesn't outlive its source
                if (value.kind == core::TypeKind::kPointer) {
                    if (!value.lifetime.empty() && value.lifetime != "'static") {
                        // Check if the lifetime is valid for return
                        bool valid = false;
                        for (const auto &lt : current_fn_lifetimes_) {
                            if (value.lifetime == lt || value.lifetime == lt.substr(1)) {
                                valid = true;
                                break;
                            }
                        }
                        if (!valid) {
                            Diags().Report(ret->loc, "cannot return reference with local lifetime");
                        }
                    }
                }
            }
            
            if (!Types().IsCompatible(value, current_return_type_)) {
                Diags().Report(ret->loc, "return type mismatch");
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
            ownership_stack_.push_back({});
            for (auto &s : loop->body) AnalyzeStmt(s);
            ExitScope();
            borrow_stack_.pop_back();
            ownership_stack_.pop_back();
            return;
        }
        if (auto fr = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            EnterScope(ScopeKind::kBlock);
            borrow_stack_.push_back({});
            ownership_stack_.push_back({});
            DeclarePattern(fr->pattern, Type::Any(), fr->loc);
            AnalyzeExpr(fr->iterable);
            for (auto &s : fr->body) AnalyzeStmt(s);
            ExitScope();
            borrow_stack_.pop_back();
            ownership_stack_.pop_back();
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
            for (auto &elem : tup->elements) {
                DeclarePattern(elem, Type::Any(), loc);
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
                    Diags().Report(call->loc, "argument count mismatch");
                } else {
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        const auto &expected = callee_t.type_args[i + 1];
                        if (expected.kind == core::TypeKind::kGenericParam && arg_types[i].IsConcrete()) {
                            // allow inference by concreteness
                        } else if (!Types().IsCompatible(arg_types[i], expected)) {
                            Diags().Report(call->loc, "argument type mismatch");
                            break;
                        }
                        
                        // Comprehensive lifetime check for reference arguments
                        if (arg_types[i].kind == core::TypeKind::kPointer) {
                            if (expected.kind == core::TypeKind::kPointer) {
                                // Both are references - validate lifetime compatibility
                                if (!ValidateBorrowLifetime(arg_types[i], expected.lifetime, call->loc)) {
                                    // Error already reported
                                }
                            } else if (arg_types[i].lifetime.empty() || arg_types[i].lifetime == "anon") {
                                // Passing reference to non-reference parameter without lifetime
                                // This is typically a move or auto-deref situation
                            }
                        }
                        
                        // Check for move semantics on non-Copy types
                        if (auto arg_id = std::dynamic_pointer_cast<Identifier>(call->args[i])) {
                            if (!IsCopyType(arg_types[i]) && expected.kind != core::TypeKind::kPointer) {
                                // Value is being moved into the function
                                CheckMove(arg_id->name, call->loc);
                            }
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
        scope_stack_.push_back({kind, "<block>"});
        Syms().EnterScope("<block>", kind);
        current_scope_depth_++;
    }

    void ExitScope() {
        Syms().ExitScope();
        if (!scope_stack_.empty()) scope_stack_.pop_back();
        current_scope_depth_--;
    }

    bool InFunction() const {
        for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
            if (it->kind == ScopeKind::kFunction) return true;
        }
        return false;
    }

    // Check if a variable has been moved
    bool IsMoved(const std::string &name) const {
        for (auto it = ownership_stack_.rbegin(); it != ownership_stack_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second.state == OwnershipState::kMoved;
            }
        }
        return false;
    }

    // Mark a variable as moved and check for use-after-move
    void CheckMove(const std::string &name, const core::SourceLoc &loc) {
        // First check if already moved
        if (IsMoved(name)) {
            Diags().Report(loc, "use of moved value: `" + name + "`");
            return;
        }
        
        // Check for active borrows
        auto active = AggregateBorrow(name);
        if (active.imm > 0 || active.mut > 0) {
            Diags().Report(loc, "cannot move out of `" + name + "` while it is borrowed");
            return;
        }
        
        // Mark as moved in current scope
        for (auto it = ownership_stack_.rbegin(); it != ownership_stack_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second.state = OwnershipState::kMoved;
                return;
            }
        }
    }

    // Check use of a value - detects use-after-move
    void CheckUse(const std::string &name, const core::SourceLoc &loc) {
        if (IsMoved(name)) {
            Diags().Report(loc, "use of moved value: `" + name + "`");
        }
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
        return false;;
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    CrateLoader *loader_{nullptr};
    std::vector<ScopeState> scope_stack_{};
    Type current_return_type_{Type::Any()};
    bool saw_return_{false};
};

}  // namespace

void AnalyzeModule(const Module &module, frontends::SemaContext &context) {
    Analyzer analyzer(module, context);
    analyzer.Run();
}

void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const RustSemaOptions &options) {
    Analyzer analyzer(module, context, options.loader);
    analyzer.Run();
}

}  // namespace polyglot::rust
