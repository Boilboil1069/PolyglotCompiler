// Unit tests for the Go frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/go/include/go_lexer.h"
#include "frontends/go/include/go_parser.h"
#include "frontends/go/include/go_sema.h"
#include "frontends/go/include/go_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::frontends::SemaContext;
using polyglot::ir::IRContext;
using namespace polyglot::go;

static std::vector<Token> Tokenize(const char *src) {
    GoLexer lex(src, "<test>");
    std::vector<Token> ts;
    for (;;) {
        auto t = lex.NextToken();
        ts.push_back(t);
        if (t.kind == TokenKind::kEndOfFile) break;
    }
    return ts;
}

static std::unique_ptr<File> Parse(const char *src, Diagnostics &d) {
    GoLexer lex(src, "<test>");
    GoParser p(lex, d);
    p.ParseFile();
    return p.TakeFile();
}

static std::string LowerIR(const char *src, Diagnostics &d) {
    auto f = Parse(src, d);
    if (!f) return "";
    SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    IRContext ir;
    LowerToIR(*f, ir, d);
    std::ostringstream os;
    for (const auto &fn : ir.Functions()) polyglot::ir::PrintFunction(*fn, os);
    return os.str();
}

TEST_CASE("Go lexer keywords and idents", "[go][lexer]") {
    auto ts = Tokenize("package main\nfunc Add(a int, b int) int { return a + b }\n");
    REQUIRE(ts.size() >= 10);
    CHECK(ts[0].kind == TokenKind::kKeyword);
    CHECK(ts[0].lexeme == "package");
    CHECK(ts[1].kind == TokenKind::kIdentifier);
    CHECK(ts[1].lexeme == "main");
}

TEST_CASE("Go lexer raw string literal", "[go][lexer]") {
    auto ts = Tokenize("package m\nvar s = `raw\nstring`\n");
    bool saw = false;
    for (auto &t : ts) if (t.kind == TokenKind::kString) saw = true;
    CHECK(saw);
}

TEST_CASE("Go lexer auto-semicolon insertion", "[go][lexer]") {
    auto ts = Tokenize("package m\nvar x = 1\nvar y = 2\n");
    int semis = 0;
    for (auto &t : ts) if (t.kind == TokenKind::kSymbol && t.lexeme == ";") ++semis;
    CHECK(semis >= 2);
}

TEST_CASE("Go parser parses package + func", "[go][parser]") {
    Diagnostics d;
    auto f = Parse("package main\nfunc Inc(x int) int { return x + 1 }\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
    CHECK(f->package_name == "main");
}

TEST_CASE("Go parser accepts struct + method", "[go][parser]") {
    Diagnostics d;
    auto f = Parse(
        "package geom\n"
        "type Point struct { X int; Y int }\n"
        "func (p Point) Sum() int { return p.X + p.Y }\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Go parser accepts if/for/range", "[go][parser]") {
    Diagnostics d;
    auto f = Parse(
        "package m\n"
        "func F(xs []int) int {\n"
        "  s := 0\n"
        "  for i, v := range xs { if v > 0 { s = s + v } else { s = s + i } }\n"
        "  return s\n"
        "}\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Go sema flags break outside loop", "[go][sema]") {
    Diagnostics d;
    auto f = Parse("package m\nfunc F() { break }\n", d);
    REQUIRE(f);
    SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    CHECK(d.HasErrors());
}

TEST_CASE("Go lowering emits IR for function", "[go][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("package main\nfunc Inc(x int) int { return x + 1 }\n", d);
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("Inc") != std::string::npos);
}
