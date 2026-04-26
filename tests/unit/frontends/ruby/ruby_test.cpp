// Unit tests for the Ruby frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/ruby/include/ruby_lexer.h"
#include "frontends/ruby/include/ruby_parser.h"
#include "frontends/ruby/include/ruby_sema.h"
#include "frontends/ruby/include/ruby_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::frontends::SemaContext;
using polyglot::ir::IRContext;
using namespace polyglot::ruby;

static std::vector<Token> Tokenize(const char *src) {
    RbLexer lex(src, "<test>");
    std::vector<Token> ts;
    for (;;) {
        auto t = lex.NextToken();
        ts.push_back(t);
        if (t.kind == TokenKind::kEndOfFile) break;
    }
    return ts;
}

static std::shared_ptr<Module> Parse(const char *src, Diagnostics &d) {
    RbLexer lex(src, "<test>");
    RbParser p(lex, d);
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

TEST_CASE("Ruby lexer keywords", "[ruby][lexer]") {
    auto ts = Tokenize("def add(a, b)\n  a + b\nend\n");
    REQUIRE(ts.size() >= 6);
    CHECK(ts[0].kind == TokenKind::kKeyword);
    CHECK(ts[0].lexeme == "def");
    CHECK(ts[1].kind == TokenKind::kIdentifier);
    CHECK(ts[1].lexeme == "add");
}

TEST_CASE("Ruby lexer string literals", "[ruby][lexer]") {
    auto ts = Tokenize("s = \"hi\"\n");
    bool saw = false;
    for (auto &t : ts) if (t.kind == TokenKind::kString) saw = true;
    CHECK(saw);
}

TEST_CASE("Ruby parser parses class with method", "[ruby][parser]") {
    Diagnostics d;
    auto m = Parse("class Foo\n  def bar\n    42\n  end\nend\n", d);
    REQUIRE(m);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Ruby parser accepts if/else", "[ruby][parser]") {
    Diagnostics d;
    auto m = Parse("def f(x)\n  if x > 0\n    1\n  else\n    -1\n  end\nend\n", d);
    REQUIRE(m);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Ruby lowering emits function IR", "[ruby][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("def inc(x)\n  x + 1\nend\n", d);
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("inc") != std::string::npos);
}
