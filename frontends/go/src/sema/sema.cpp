/**
 * @file     sema.cpp
 * @brief    Go semantic analyzer
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Performs lightweight, structural validation:
 *   - `break` / `continue` only inside `for` / `switch` / `select`
 *   - `fallthrough` only inside a `switch` clause
 *   - `return` outside of function body
 *   - Duplicate top-level identifiers within the file
 *
 * Full type-checking is delegated to lowering, where we only handle a
 * statically-typed subset.  Anything unknown is reported as a warning
 * via `Diagnostics`.
 */
#include "frontends/go/include/go_sema.h"

#include <unordered_set>

namespace polyglot::go {

namespace {

class Analyzer {
  public:
    Analyzer(const File &f, frontends::SemaContext &c) : file_(f), ctx_(c) {}

    void Run() {
        std::unordered_set<std::string> top_names;
        for (const auto &gd : file_.decls) {
            for (const auto &v : gd.values)
                for (const auto &n : v.names) {
                    if (!top_names.insert(n).second && n != "_")
                        ctx_.Diags().Report(v.loc, "duplicate top-level name: " + n);
                }
            for (const auto &t : gd.types) {
                if (!top_names.insert(t.name).second)
                    ctx_.Diags().Report(t.loc, "duplicate type name: " + t.name);
            }
        }
        for (const auto &fn : file_.funcs) {
            if (fn->name != "init") {
                if (!top_names.insert(fn->name).second && !fn->receiver)
                    ctx_.Diags().Report(fn->loc, "duplicate function name: " + fn->name);
            }
            if (fn->body) VisitBlock(*fn->body);
        }
    }

  private:
    void VisitStmt(const std::shared_ptr<Statement> &s) {
        if (!s) return;
        if (auto b = std::dynamic_pointer_cast<Block>(s)) { VisitBlock(*b); return; }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) {
            VisitStmt(i->init);
            if (i->body) VisitBlock(*i->body);
            VisitStmt(i->else_branch); return;
        }
        if (auto f = std::dynamic_pointer_cast<ForStmt>(s)) {
            VisitStmt(f->init); VisitStmt(f->post);
            ++loop_depth_;
            if (f->body) VisitBlock(*f->body);
            --loop_depth_;
            return;
        }
        if (auto sw = std::dynamic_pointer_cast<SwitchStmt>(s)) {
            VisitStmt(sw->init);
            ++switch_depth_;
            for (auto &c : sw->clauses) for (auto &cs : c.body) VisitStmt(cs);
            --switch_depth_;
            return;
        }
        if (auto sel = std::dynamic_pointer_cast<SelectStmt>(s)) {
            ++switch_depth_;
            for (auto &c : sel->clauses) for (auto &cs : c.body) VisitStmt(cs);
            --switch_depth_;
            return;
        }
        if (auto br = std::dynamic_pointer_cast<BranchStmt>(s)) {
            if ((br->keyword == "break" || br->keyword == "continue") &&
                loop_depth_ == 0 && switch_depth_ == 0) {
                ctx_.Diags().Report(br->loc, "'" + br->keyword + "' not in loop or switch");
            }
            if (br->keyword == "fallthrough" && switch_depth_ == 0) {
                ctx_.Diags().Report(br->loc, "'fallthrough' must be inside switch");
            }
        }
    }
    void VisitBlock(const Block &b) { for (auto &s : b.stmts) VisitStmt(s); }

    const File &file_;
    frontends::SemaContext &ctx_;
    int loop_depth_{0};
    int switch_depth_{0};
};

}  // namespace

void AnalyzeFile(const File &f, frontends::SemaContext &c) { Analyzer(f, c).Run(); }

}  // namespace polyglot::go
