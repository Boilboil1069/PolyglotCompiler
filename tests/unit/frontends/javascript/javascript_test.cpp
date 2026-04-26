// Unit tests for the JavaScript frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/javascript/include/javascript_lexer.h"
#include "frontends/javascript/include/javascript_parser.h"
#include "frontends/javascript/include/javascript_sema.h"
#include "frontends/javascript/include/javascript_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::frontends::SemaContext;
using polyglot::ir::IRContext;
using namespace polyglot::javascript;

static std::vector<Token> Tokenize(const char *src) {
    JsLexer lex(src, "<test>");
    std::vector<Token> ts;
    for (;;) {
        auto t = lex.NextToken();
        ts.push_back(t);
        if (t.kind == TokenKind::kEndOfFile) break;
    }
    return ts;
}

static std::shared_ptr<Module> Parse(const char *src, Diagnostics &d) {
    JsLexer lex(src, "<test>");
    JsParser p(lex, d);
    p.ParseModule();
    return p.TakeModule();
}

static std::string LowerIR(const char *src, Diagnostics &d) {
    auto m = Parse(src, d);
    if (!m) return "";
    SemaContext ctx(d);
    AnalyzeModule(*m, ctx);
    IRContext ir;
    LowerToIR(*m, ir, d);
    std::ostringstream os;
    for (const auto &fn : ir.Functions()) polyglot::ir::PrintFunction(*fn, os);
    return os.str();
}

TEST_CASE("JS lexer recognises keywords and identifiers", "[javascript][lexer]") {
    auto ts = Tokenize("function add(a, b) { return a + b; }");
    REQUIRE(ts.size() >= 12);
    CHECK(ts[0].kind == TokenKind::kKeyword);
    CHECK(ts[0].lexeme == "function");
    CHECK(ts[1].kind == TokenKind::kIdentifier);
    CHECK(ts[1].lexeme == "add");
}

TEST_CASE("JS lexer handles template literal", "[javascript][lexer]") {
    auto ts = Tokenize("let x = `hi ${name}!`;");
    bool saw_string = false;
    for (auto &t : ts) if (t.kind == TokenKind::kString) saw_string = true;
    CHECK(saw_string);
}

TEST_CASE("JS parser builds module for simple function", "[javascript][parser]") {
    Diagnostics d;
    auto m = Parse("function f(x) { return x * 2; }", d);
    REQUIRE(m);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("JS parser accepts arrow functions and let/const", "[javascript][parser]") {
    Diagnostics d;
    auto m = Parse("const sq = (n) => n * n;\nlet y = sq(3);", d);
    REQUIRE(m);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("JS parser flags unterminated brace", "[javascript][parser]") {
    Diagnostics d;
    Parse("function bad() { return 1; ", d);
    CHECK(d.HasErrors());
}

TEST_CASE("JS lowering produces IR for function", "[javascript][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("function inc(x) { return x + 1; }", d);
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("inc") != std::string::npos);
}
