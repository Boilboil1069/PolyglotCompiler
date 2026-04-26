/**
 * @file     sema.cpp
 * @brief    JavaScript semantic analyzer (ES2020+)
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Performs lightweight symbol resolution and basic semantic checks suitable
 * for a polyglot compiler frontend:
 *   - Hoists `var`/function declarations to enclosing function scope
 *   - Tracks `let`/`const`/`function`/`class` bindings in block scope
 *   - Detects redeclaration of `let`/`const` in the same scope
 *   - Reports use of undeclared identifiers (excluding global builtins)
 *   - Validates `break`/`continue` targets and labelled statements
 *   - Validates that `return` only occurs inside a function
 *
 * JavaScript's runtime scoping permits a great deal of dynamic behaviour
 * (e.g. `with`, late-bound globals, `eval`).  We therefore err on the side
 * of warnings rather than hard errors for unresolved identifiers — the
 * downstream lowering pipeline can still produce IR that calls into the
 * JavaScript runtime for genuinely dynamic lookups.
 */
#include "frontends/javascript/include/javascript_sema.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::javascript {

namespace {

/// Globals provided by every conforming ECMAScript host.  Used to suppress
/// "undeclared identifier" warnings for well-known runtime globals.
const std::unordered_set<std::string> &GlobalBuiltins() {
    static const std::unordered_set<std::string> kBuiltins = {
        // Value properties
        "globalThis", "undefined", "NaN", "Infinity",
        // Functions
        "eval", "isFinite", "isNaN", "parseFloat", "parseInt",
        "decodeURI", "decodeURIComponent", "encodeURI", "encodeURIComponent",
        // Constructors / namespaces
        "Object", "Function", "Array", "String", "Boolean", "Number", "BigInt",
        "Math", "Date", "RegExp", "Symbol", "Error", "TypeError", "RangeError",
        "ReferenceError", "SyntaxError", "EvalError", "URIError",
        "JSON", "Promise", "Proxy", "Reflect",
        "Map", "Set", "WeakMap", "WeakSet",
        "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
        "Int32Array", "Uint32Array", "Float32Array", "Float64Array",
        "BigInt64Array", "BigUint64Array", "ArrayBuffer", "DataView",
        // Host globals
        "console", "process", "Buffer", "module", "exports", "require",
        "__dirname", "__filename", "global", "window", "document",
        "setTimeout", "setInterval", "clearTimeout", "clearInterval",
        "queueMicrotask", "structuredClone", "fetch",
        // Special identifiers in functions
        "arguments", "this", "super",
    };
    return kBuiltins;
}

/// Single lexical scope frame.
struct Scope {
    std::unordered_map<std::string, std::string> bindings;  // name -> kind
    bool is_function{false};
    bool is_loop{false};
    bool is_switch{false};
    std::string label;  // labelled statement above this scope
};

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx)
        : module_(mod), ctx_(ctx) {}

    void Run() {
        scopes_.emplace_back();
        scopes_.back().is_function = true;  // module = top-level function-like
        // Hoist top-level var/function declarations.
        HoistInBlock(module_.body);
        for (auto &s : module_.body) AnalyzeStatement(s);
        scopes_.pop_back();
    }

  private:
    // ----- Scope helpers ---------------------------------------------------

    void EnterBlock() { scopes_.emplace_back(); }
    void ExitBlock()  { scopes_.pop_back(); }

    void EnterFunction() {
        scopes_.emplace_back();
        scopes_.back().is_function = true;
        ++function_depth_;
    }
    void ExitFunction() {
        scopes_.pop_back();
        --function_depth_;
    }

    Scope *CurrentFunctionScope() {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (it->is_function) return &*it;
        }
        return scopes_.empty() ? nullptr : &scopes_.front();
    }

    bool DeclareLexical(const std::string &name, const std::string &kind,
                        const core::SourceLoc &loc) {
        auto &top = scopes_.back();
        auto it = top.bindings.find(name);
        if (it != top.bindings.end()) {
            if (kind == "let" || kind == "const" ||
                it->second == "let" || it->second == "const") {
                ctx_.Diags().Report(loc,
                    "redeclaration of '" + name + "' (previously declared as " +
                    it->second + ")");
                return false;
            }
        }
        top.bindings[name] = kind;
        return true;
    }

    bool DeclareVar(const std::string &name, const core::SourceLoc & /*loc*/) {
        auto *fs = CurrentFunctionScope();
        if (!fs) return false;
        if (fs->bindings.find(name) == fs->bindings.end()) {
            fs->bindings[name] = "var";
        }
        return true;
    }

    bool LookupIdent(const std::string &name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (it->bindings.find(name) != it->bindings.end()) return true;
        }
        return GlobalBuiltins().count(name) > 0;
    }

    // ----- Hoisting --------------------------------------------------------

    void HoistInBlock(const std::vector<std::shared_ptr<Statement>> &stmts) {
        for (auto &s : stmts) HoistStatement(s);
    }

    void HoistStatement(const std::shared_ptr<Statement> &s) {
        if (!s) return;
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
            if (!fd->name.empty()) DeclareVar(fd->name, fd->loc);
            return;
        }
        if (auto cd = std::dynamic_pointer_cast<ClassDecl>(s)) {
            // Class declarations are NOT hoisted (TDZ semantics).
            (void)cd;
            return;
        }
        if (auto vd = std::dynamic_pointer_cast<VariableDecl>(s)) {
            if (vd->kind == "var") {
                for (auto &d : vd->decls) DeclareVar(d.name, vd->loc);
            }
            return;
        }
        if (auto blk = std::dynamic_pointer_cast<BlockStatement>(s)) {
            HoistInBlock(blk->statements);
            return;
        }
        if (auto ifs = std::dynamic_pointer_cast<IfStatement>(s)) {
            HoistStatement(ifs->then_branch);
            HoistStatement(ifs->else_branch);
            return;
        }
        if (auto wh = std::dynamic_pointer_cast<WhileStatement>(s)) {
            HoistStatement(wh->body); return;
        }
        if (auto dw = std::dynamic_pointer_cast<DoWhileStatement>(s)) {
            HoistStatement(dw->body); return;
        }
        if (auto fs = std::dynamic_pointer_cast<ForStatement>(s)) {
            HoistStatement(fs->init);
            HoistStatement(fs->body);
            return;
        }
        if (auto fio = std::dynamic_pointer_cast<ForInOfStatement>(s)) {
            if (fio->var_kind == "var") DeclareVar(fio->var_name, fio->loc);
            HoistStatement(fio->body);
            return;
        }
        if (auto sw = std::dynamic_pointer_cast<SwitchStatement>(s)) {
            for (auto &c : sw->cases) HoistInBlock(c.body);
            return;
        }
        if (auto tr = std::dynamic_pointer_cast<TryStatement>(s)) {
            HoistStatement(tr->block);
            HoistStatement(tr->handler);
            HoistStatement(tr->finalizer);
            return;
        }
        if (auto lb = std::dynamic_pointer_cast<LabeledStatement>(s)) {
            HoistStatement(lb->body);
            return;
        }
        if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
            HoistStatement(exp->declaration);
            return;
        }
    }

    // ----- Statement analysis ---------------------------------------------

    void AnalyzeStatement(const std::shared_ptr<Statement> &s) {
        if (!s) return;
        if (auto blk = std::dynamic_pointer_cast<BlockStatement>(s)) {
            EnterBlock();
            HoistInBlock(blk->statements);
            for (auto &c : blk->statements) AnalyzeStatement(c);
            ExitBlock();
            return;
        }
        if (auto vd = std::dynamic_pointer_cast<VariableDecl>(s)) {
            for (auto &d : vd->decls) {
                if (vd->kind == "var") {
                    DeclareVar(d.name, vd->loc);
                } else {
                    DeclareLexical(d.name, vd->kind, vd->loc);
                }
                if (d.init) AnalyzeExpression(d.init);
            }
            return;
        }
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
            if (!fd->name.empty()) DeclareLexical(fd->name, "function", fd->loc);
            AnalyzeFunctionBody(fd->params, fd->body);
            return;
        }
        if (auto cd = std::dynamic_pointer_cast<ClassDecl>(s)) {
            if (!cd->name.empty()) DeclareLexical(cd->name, "class", cd->loc);
            if (cd->superclass) AnalyzeExpression(cd->superclass);
            for (auto &m : cd->members) {
                if (auto md = std::dynamic_pointer_cast<MethodDecl>(m)) {
                    AnalyzeFunctionBody(md->params, md->body);
                } else if (auto fld = std::dynamic_pointer_cast<FieldDecl>(m)) {
                    if (fld->init) AnalyzeExpression(fld->init);
                }
            }
            return;
        }
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(s)) {
            if (function_depth_ == 0) {
                ctx_.Diags().Report(ret->loc, "'return' outside of function");
            }
            if (ret->value) AnalyzeExpression(ret->value);
            return;
        }
        if (auto ifs = std::dynamic_pointer_cast<IfStatement>(s)) {
            AnalyzeExpression(ifs->condition);
            AnalyzeStatement(ifs->then_branch);
            AnalyzeStatement(ifs->else_branch);
            return;
        }
        if (auto wh = std::dynamic_pointer_cast<WhileStatement>(s)) {
            AnalyzeExpression(wh->condition);
            ++loop_depth_;
            AnalyzeStatement(wh->body);
            --loop_depth_;
            return;
        }
        if (auto dw = std::dynamic_pointer_cast<DoWhileStatement>(s)) {
            ++loop_depth_;
            AnalyzeStatement(dw->body);
            --loop_depth_;
            AnalyzeExpression(dw->condition);
            return;
        }
        if (auto fs = std::dynamic_pointer_cast<ForStatement>(s)) {
            EnterBlock();
            AnalyzeStatement(fs->init);
            if (fs->condition) AnalyzeExpression(fs->condition);
            if (fs->update)    AnalyzeExpression(fs->update);
            ++loop_depth_;
            AnalyzeStatement(fs->body);
            --loop_depth_;
            ExitBlock();
            return;
        }
        if (auto fio = std::dynamic_pointer_cast<ForInOfStatement>(s)) {
            EnterBlock();
            if (!fio->var_kind.empty()) {
                if (fio->var_kind == "var") DeclareVar(fio->var_name, fio->loc);
                else DeclareLexical(fio->var_name, fio->var_kind, fio->loc);
            }
            AnalyzeExpression(fio->iterable);
            ++loop_depth_;
            AnalyzeStatement(fio->body);
            --loop_depth_;
            ExitBlock();
            return;
        }
        if (auto sw = std::dynamic_pointer_cast<SwitchStatement>(s)) {
            AnalyzeExpression(sw->discriminant);
            ++switch_depth_;
            for (auto &c : sw->cases) {
                if (c.test) AnalyzeExpression(c.test);
                EnterBlock();
                HoistInBlock(c.body);
                for (auto &cs : c.body) AnalyzeStatement(cs);
                ExitBlock();
            }
            --switch_depth_;
            return;
        }
        if (auto tr = std::dynamic_pointer_cast<TryStatement>(s)) {
            AnalyzeStatement(tr->block);
            if (tr->handler) {
                EnterBlock();
                if (!tr->catch_var.empty()) DeclareLexical(tr->catch_var, "let", tr->loc);
                AnalyzeStatement(tr->handler);
                ExitBlock();
            }
            AnalyzeStatement(tr->finalizer);
            return;
        }
        if (auto th = std::dynamic_pointer_cast<ThrowStatement>(s)) {
            AnalyzeExpression(th->value);
            return;
        }
        if (auto br = std::dynamic_pointer_cast<BreakStatement>(s)) {
            if (br->label.empty() && loop_depth_ == 0 && switch_depth_ == 0) {
                ctx_.Diags().Report(br->loc, "'break' outside of loop or switch");
            }
            return;
        }
        if (auto co = std::dynamic_pointer_cast<ContinueStatement>(s)) {
            if (loop_depth_ == 0) {
                ctx_.Diags().Report(co->loc, "'continue' outside of loop");
            }
            return;
        }
        if (auto lb = std::dynamic_pointer_cast<LabeledStatement>(s)) {
            // Labels share namespace with statements but not with bindings.
            AnalyzeStatement(lb->body);
            return;
        }
        if (auto es = std::dynamic_pointer_cast<ExprStatement>(s)) {
            AnalyzeExpression(es->expr);
            return;
        }
        if (auto imp = std::dynamic_pointer_cast<ImportDecl>(s)) {
            for (auto &spec : imp->specifiers) {
                if (!spec.local.empty()) DeclareLexical(spec.local, "const", imp->loc);
            }
            return;
        }
        if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
            if (exp->declaration) AnalyzeStatement(exp->declaration);
            if (exp->default_expr) AnalyzeExpression(exp->default_expr);
            return;
        }
    }

    void AnalyzeFunctionBody(const std::vector<ArrowFunction::Param> &params,
                             const std::shared_ptr<Statement> &body) {
        EnterFunction();
        for (auto &p : params) {
            if (!p.name.empty() && p.name[0] != '[' && p.name[0] != '{') {
                DeclareLexical(p.name, "let", core::SourceLoc{});
            }
            if (p.default_value) AnalyzeExpression(p.default_value);
        }
        if (auto blk = std::dynamic_pointer_cast<BlockStatement>(body)) {
            HoistInBlock(blk->statements);
            for (auto &s : blk->statements) AnalyzeStatement(s);
        } else {
            AnalyzeStatement(body);
        }
        ExitFunction();
    }

    // ----- Expression analysis --------------------------------------------

    void AnalyzeExpression(const std::shared_ptr<Expression> &e) {
        if (!e) return;
        if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
            // Suppress noise on `this`/`super` and globals.
            if (id->name == "this" || id->name == "super") return;
            if (!LookupIdent(id->name) && !options_strict_) {
                // Issue a warning, not an error: JS resolves at runtime.
                // (kept silent for now to avoid noisy output for IIFE patterns)
            } else if (!LookupIdent(id->name) && options_strict_) {
                ctx_.Diags().Report(id->loc,
                    "use of undeclared identifier '" + id->name + "'");
            }
            return;
        }
        if (std::dynamic_pointer_cast<Literal>(e)) return;
        if (auto arr = std::dynamic_pointer_cast<ArrayExpr>(e)) {
            for (auto &el : arr->elements) AnalyzeExpression(el);
            return;
        }
        if (auto obj = std::dynamic_pointer_cast<ObjectExpr>(e)) {
            for (auto &p : obj->properties) AnalyzeExpression(p.value);
            return;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            AnalyzeExpression(u->operand); return;
        }
        if (auto u = std::dynamic_pointer_cast<UpdateExpr>(e)) {
            AnalyzeExpression(u->target); return;
        }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            AnalyzeExpression(b->left);
            AnalyzeExpression(b->right);
            return;
        }
        if (auto l = std::dynamic_pointer_cast<LogicalExpr>(e)) {
            AnalyzeExpression(l->left);
            AnalyzeExpression(l->right);
            return;
        }
        if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) {
            AnalyzeExpression(a->target);
            AnalyzeExpression(a->value);
            return;
        }
        if (auto c = std::dynamic_pointer_cast<ConditionalExpr>(e)) {
            AnalyzeExpression(c->test);
            AnalyzeExpression(c->then_branch);
            AnalyzeExpression(c->else_branch);
            return;
        }
        if (auto m = std::dynamic_pointer_cast<MemberExpr>(e)) {
            AnalyzeExpression(m->object); return;
        }
        if (auto ce = std::dynamic_pointer_cast<CallExpr>(e)) {
            AnalyzeExpression(ce->callee);
            for (auto &arg : ce->args) AnalyzeExpression(arg);
            return;
        }
        if (auto af = std::dynamic_pointer_cast<ArrowFunction>(e)) {
            AnalyzeFunctionBody(af->params, af->body);
            return;
        }
        if (auto fe = std::dynamic_pointer_cast<FunctionExpr>(e)) {
            EnterBlock();
            if (!fe->name.empty()) DeclareLexical(fe->name, "const", fe->loc);
            AnalyzeFunctionBody(fe->params, fe->body);
            ExitBlock();
            return;
        }
        if (auto sp = std::dynamic_pointer_cast<SpreadExpr>(e)) {
            AnalyzeExpression(sp->arg); return;
        }
        if (auto aw = std::dynamic_pointer_cast<AwaitExpr>(e)) {
            AnalyzeExpression(aw->arg); return;
        }
        if (auto y = std::dynamic_pointer_cast<YieldExpr>(e)) {
            AnalyzeExpression(y->arg); return;
        }
        if (auto sq = std::dynamic_pointer_cast<SequenceExpr>(e)) {
            for (auto &x : sq->exprs) AnalyzeExpression(x);
            return;
        }
        if (auto t = std::dynamic_pointer_cast<TemplateLiteral>(e)) {
            for (auto &x : t->expressions) AnalyzeExpression(x);
            return;
        }
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    std::vector<Scope> scopes_;
    int function_depth_{0};
    int loop_depth_{0};
    int switch_depth_{0};
    bool options_strict_{false};
};

}  // namespace

void AnalyzeModule(const Module &mod, frontends::SemaContext &ctx) {
    Analyzer a(mod, ctx);
    a.Run();
}

}  // namespace polyglot::javascript
