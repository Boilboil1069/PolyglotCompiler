/**
 * @file     polish_grammar_test.cpp
 * @brief    Unit tests for the v1.18.0 grammar polish bundle:
 *           optional outer parens on IF/WHILE/FOR, `IF LET Some(x)` /
 *           `IF LET None` unwrap, NULL-with-OPTION<T> diagnostic, and
 *           `///` doc-comment capture on FUNC/STRUCT/LET declarations.
 *
 * @ingroup  Tests / Ploy / Grammar Polish
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::ForStatement;
using polyglot::ploy::FuncDecl;
using polyglot::ploy::IfLetStatement;
using polyglot::ploy::IfStatement;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::StructDecl;
using polyglot::ploy::VarDecl;
using polyglot::ploy::WhileStatement;

namespace {

struct ParseResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
};

ParseResult Parse(const std::string &code) {
    ParseResult r;
    PloyLexer lexer(code, "<polish-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    return r;
}

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<polish-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

bool DiagsContain(const Diagnostics &diags, const std::string &needle) {
    for (const auto &d : diags.All()) {
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

template <typename T>
std::shared_ptr<T> FirstStmtOfKind(const FuncDecl &fn) {
    for (const auto &s : fn.body) {
        if (auto p = std::dynamic_pointer_cast<T>(s)) return p;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("IF accepts optional outer parens", "[ploy][parser][polish]") {
    auto r = Parse("FUNC f() -> i32 { IF (1 == 1) { RETURN 1; } RETURN 0; }");
    REQUIRE(r.module->declarations.size() == 1);
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto if_stmt = FirstStmtOfKind<IfStatement>(*fn);
    REQUIRE(if_stmt);
}

TEST_CASE("WHILE accepts optional outer parens", "[ploy][parser][polish]") {
    auto r = Parse("FUNC f() { WHILE (1 == 1) { BREAK; } }");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto w = FirstStmtOfKind<WhileStatement>(*fn);
    REQUIRE(w);
}

TEST_CASE("FOR accepts optional outer parens", "[ploy][parser][polish]") {
    auto r = Parse("FUNC f() { FOR (i IN [1, 2, 3]) { CONTINUE; } }");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto fr = FirstStmtOfKind<ForStatement>(*fn);
    REQUIRE(fr);
    REQUIRE(fr->iterator_name == "i");
}

TEST_CASE("FOR still accepts the no-parens form", "[ploy][parser][polish]") {
    auto r = Parse("FUNC f() { FOR i IN [1, 2, 3] { CONTINUE; } }");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto fr = FirstStmtOfKind<ForStatement>(*fn);
    REQUIRE(fr);
}

TEST_CASE("IF LET Some(x) parses with binding", "[ploy][parser][polish][iflet]") {
    auto r = Parse("FUNC f(opt: OPTION<i32>) { IF LET Some(x) = opt { RETURN; } }");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto il = FirstStmtOfKind<IfLetStatement>(*fn);
    REQUIRE(il);
    REQUIRE(il->ctor == "Some");
    REQUIRE(il->bindings.size() == 1);
    REQUIRE(il->bindings[0] == "x");
}

TEST_CASE("IF LET None parses without bindings and supports ELSE",
          "[ploy][parser][polish][iflet]") {
    auto r = Parse("FUNC f(opt: OPTION<i32>) { IF LET None = opt { RETURN; } "
                   "ELSE { RETURN; } }");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    auto il = FirstStmtOfKind<IfLetStatement>(*fn);
    REQUIRE(il);
    REQUIRE(il->ctor == "None");
    REQUIRE(il->bindings.empty());
    REQUIRE(!il->else_body.empty());
}

TEST_CASE("NULL bound to OPTION<T> raises a targeted diagnostic",
          "[ploy][sema][polish][null-option]") {
    auto r = Analyze("FUNC f() { LET o: OPTION<i32> = NULL; }");
    REQUIRE(DiagsContain(r.diags, "OPTION<T> with NULL"));
}

TEST_CASE("/// doc comments attach to FUNC", "[ploy][lexer][polish][doc]") {
    auto r = Parse("/// First line\n"
                   "/// Second line\n"
                   "FUNC add(a: i32, b: i32) -> i32 { RETURN a + b; }\n");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    REQUIRE(fn->doc_comment.size() == 2);
    REQUIRE(fn->doc_comment[0] == "First line");
    REQUIRE(fn->doc_comment[1] == "Second line");
}

TEST_CASE("/// doc comments attach to STRUCT and LET",
          "[ploy][lexer][polish][doc]") {
    auto r = Parse("/// A point.\n"
                   "STRUCT Point { x: i32, y: i32 }\n"
                   "/// The answer.\n"
                   "LET ANSWER: i32 = 42;\n");
    REQUIRE(r.module->declarations.size() == 2);
    auto sd = std::dynamic_pointer_cast<StructDecl>(r.module->declarations[0]);
    auto vd = std::dynamic_pointer_cast<VarDecl>(r.module->declarations[1]);
    REQUIRE(sd);
    REQUIRE(vd);
    REQUIRE(sd->doc_comment.size() == 1);
    REQUIRE(sd->doc_comment[0] == "A point.");
    REQUIRE(vd->doc_comment.size() == 1);
    REQUIRE(vd->doc_comment[0] == "The answer.");
}

TEST_CASE("Plain // comments do not become docs",
          "[ploy][lexer][polish][doc]") {
    auto r = Parse("// not a doc\n"
                   "FUNC f() {}\n");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    REQUIRE(fn->doc_comment.empty());
}

TEST_CASE("//// (four slashes) is a regular line comment",
          "[ploy][lexer][polish][doc]") {
    auto r = Parse("//// banner line\n"
                   "FUNC f() {}\n");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations.front());
    REQUIRE(fn);
    REQUIRE(fn->doc_comment.empty());
}
