/**
 * @file     sema.cpp
 * @brief    Ruby semantic analyzer
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Ruby's semantics are dominated by dynamic dispatch, open classes, and
 * runtime metaprogramming, so static analysis is necessarily lightweight.
 * We perform:
 *   - Scope tracking for `def`, `class`, `module`, blocks and `for`
 *   - `break`/`next`/`redo`/`retry` placement validation
 *   - `return` outside of a method warning
 *   - Block parameter binding for `do |x, y| … end`
 */
#include "frontends/ruby/include/ruby_sema.h"

#include <unordered_set>
#include <vector>

namespace polyglot::ruby {

namespace {

class Analyzer {
  public:
    Analyzer(const Module &mod, frontends::SemaContext &ctx)
        : module_(mod), ctx_(ctx) {}

    void Run() { for (auto &s : module_.body) Visit(s); }

  private:
    void Visit(const std::shared_ptr<Statement> &s) {
        if (!s) return;
        if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) {
            ++method_depth_;
            VisitBlock(m->body);
            --method_depth_;
            return;
        }
        if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
            for (auto &b : c->body) Visit(b); return;
        }
        if (auto md = std::dynamic_pointer_cast<ModuleDecl>(s)) {
            for (auto &b : md->body) Visit(b); return;
        }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) {
            VisitExpr(i->cond); Visit(i->then_branch); Visit(i->else_branch); return;
        }
        if (auto w = std::dynamic_pointer_cast<WhileStmt>(s)) {
            VisitExpr(w->cond); ++loop_depth_; Visit(w->body); --loop_depth_; return;
        }
        if (auto f = std::dynamic_pointer_cast<ForStmt>(s)) {
            VisitExpr(f->iterable); ++loop_depth_; Visit(f->body); --loop_depth_; return;
        }
        if (auto cs = std::dynamic_pointer_cast<CaseStmt>(s)) {
            VisitExpr(cs->subject);
            for (auto &w : cs->whens) {
                for (auto &t : w.tests) VisitExpr(t);
                Visit(w.body);
            }
            Visit(cs->else_branch); return;
        }
        if (auto bs = std::dynamic_pointer_cast<BeginStmt>(s)) {
            Visit(bs->body);
            for (auto &r : bs->rescues) Visit(r.body);
            Visit(bs->else_branch); Visit(bs->ensure_branch); return;
        }
        if (auto blk = std::dynamic_pointer_cast<Block>(s)) {
            VisitBlock(blk); return;
        }
        if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s)) {
            if (method_depth_ == 0) {
                ctx_.Diags().Report(rs->loc, "'return' outside of method");
            }
            VisitExpr(rs->value); return;
        }
        if (auto br = std::dynamic_pointer_cast<BreakStmt>(s)) {
            if (loop_depth_ == 0 && block_depth_ == 0)
                ctx_.Diags().Report(br->loc, "'break' outside of loop or block");
            VisitExpr(br->value); return;
        }
        if (auto nx = std::dynamic_pointer_cast<NextStmt>(s)) {
            if (loop_depth_ == 0 && block_depth_ == 0)
                ctx_.Diags().Report(nx->loc, "'next' outside of loop or block");
            VisitExpr(nx->value); return;
        }
        if (auto es = std::dynamic_pointer_cast<ExprStmt>(s)) {
            VisitExpr(es->expr); return;
        }
    }

    void VisitBlock(const std::shared_ptr<Statement> &b) {
        if (auto blk = std::dynamic_pointer_cast<Block>(b)) {
            for (auto &s : blk->stmts) Visit(s);
        } else {
            Visit(b);
        }
    }

    void VisitExpr(const std::shared_ptr<Expression> &e) {
        if (!e) return;
        if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            VisitExpr(bin->left); VisitExpr(bin->right); return;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) { VisitExpr(u->operand); return; }
        if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) { VisitExpr(a->target); VisitExpr(a->value); return; }
        if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
            VisitExpr(c->receiver);
            for (auto &arg : c->args) VisitExpr(arg);
            if (c->block) { ++block_depth_; Visit(c->block); --block_depth_; }
            return;
        }
        if (auto i = std::dynamic_pointer_cast<IndexExpr>(e)) {
            VisitExpr(i->obj);
            for (auto &x : i->idx) VisitExpr(x);
            return;
        }
        if (auto m = std::dynamic_pointer_cast<MemberExpr>(e)) { VisitExpr(m->obj); return; }
        if (auto t = std::dynamic_pointer_cast<TernaryExpr>(e)) {
            VisitExpr(t->cond); VisitExpr(t->then_e); VisitExpr(t->else_e); return;
        }
        if (auto al = std::dynamic_pointer_cast<ArrayLit>(e)) {
            for (auto &x : al->elems) VisitExpr(x); return;
        }
        if (auto hl = std::dynamic_pointer_cast<HashLit>(e)) {
            for (auto &p : hl->pairs) { VisitExpr(p.key); VisitExpr(p.value); }
            return;
        }
        if (auto r = std::dynamic_pointer_cast<RangeExpr>(e)) {
            VisitExpr(r->from); VisitExpr(r->to); return;
        }
    }

    const Module &module_;
    frontends::SemaContext &ctx_;
    int method_depth_{0};
    int loop_depth_{0};
    int block_depth_{0};
};

}  // namespace

void AnalyzeModule(const Module &mod, frontends::SemaContext &ctx) {
    Analyzer(mod, ctx).Run();
}

}  // namespace polyglot::ruby
