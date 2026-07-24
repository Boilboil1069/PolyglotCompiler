// Unit tests for the Ruby frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/ruby/include/ruby_lexer.h"
#include "frontends/ruby/include/ruby_frontend.h"
#include "frontends/ruby/include/ruby_parser.h"
#include "frontends/ruby/include/ruby_sema.h"
#include "frontends/ruby/include/ruby_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
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

static std::shared_ptr<Module> ParseVersion(const char *src, Diagnostics &d,
                                            polyglot::frontends::RubyVersion version) {
    RbLexer lex(src, "<test>");
    RbParser p(lex, d);
    p.SetRubyVersion(version);
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

TEST_CASE("Ruby lexer emits safe navigation as one token", "[ruby][lexer][safe-navigation]") {
    auto ts = Tokenize("object&.value\n");
    REQUIRE(ts.size() >= 4);
    CHECK(ts[1].kind == TokenKind::kSymbol);
    CHECK(ts[1].lexeme == "&.");
}

TEST_CASE("Ruby heredoc lexer does not confuse shifts and preserves simple bodies",
          "[ruby][lexer][heredoc]") {
    auto shift_tokens = Tokenize("a << b\n");
    REQUIRE(shift_tokens.size() >= 4);
    REQUIRE(shift_tokens[1].kind == TokenKind::kSymbol);
    REQUIRE(shift_tokens[1].lexeme == "<<");

    auto heredoc_tokens = Tokenize("value = <<~TEXT\n  hello\nTEXT\n");
    bool found = false;
    for (const auto &token : heredoc_tokens) {
        if (token.kind == TokenKind::kString) {
            found = true;
            REQUIRE(token.lexeme == "hello\n");
            REQUIRE(token.raw_lexeme == "__polyglot_ruby_heredoc__");
        }
    }
    REQUIRE(found);
}

TEST_CASE("Ruby parser marks heredoc AST and rejects lossy interpolation",
          "[ruby][parser][heredoc]") {
    Diagnostics plain_d;
    auto module = Parse("message = <<TEXT\nhello\nTEXT\nnext_value = 1\n", plain_d);
    REQUIRE_FALSE(plain_d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto stmt = std::dynamic_pointer_cast<ExprStmt>(module->body[0]);
    auto assignment = std::dynamic_pointer_cast<AssignExpr>(stmt->expr);
    auto literal = std::dynamic_pointer_cast<Literal>(assignment->value);
    REQUIRE(literal);
    REQUIRE(literal->is_heredoc);
    REQUIRE(literal->value == "hello\n");

    Diagnostics interpolated_d;
    Parse("message = <<TEXT\nhello #{name}\nTEXT\n", interpolated_d);
    bool unsupported = false;
    for (const auto &diagnostic : interpolated_d.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    REQUIRE(unsupported);

    Diagnostics unterminated_d;
    Parse("message = <<TEXT\nhello\n", unterminated_d);
    bool unterminated = false;
    for (const auto &diagnostic : unterminated_d.All())
        unterminated = unterminated ||
                       diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    REQUIRE(unterminated);
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

TEST_CASE("Ruby parser accepts case-in capture patterns", "[ruby][parser][pattern]") {
    const char *source = "case value\nin {x:}\n  x\nelse\n  nil\nend\n";
    Diagnostics d;
    auto module = ParseVersion(source, d, polyglot::frontends::RubyVersion::kRuby3_0);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(module->body.size() == 1);
    auto case_stmt = std::dynamic_pointer_cast<CaseStmt>(module->body[0]);
    REQUIRE(case_stmt);
    REQUIRE(case_stmt->whens.size() == 1);
    REQUIRE(case_stmt->whens[0].is_pattern);
    auto pattern = std::dynamic_pointer_cast<HashLit>(case_stmt->whens[0].tests[0]);
    REQUIRE(pattern);
    REQUIRE(pattern->pairs.size() == 1);
    auto capture = std::dynamic_pointer_cast<Identifier>(pattern->pairs[0].value);
    REQUIRE(capture);
    REQUIRE(capture->name == "x");

    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::RubyVersion::kRuby1_9);
    size_t mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++mismatches;
    REQUIRE(mismatches >= 1);
}

TEST_CASE("Ruby parser represents endless methods and argument forwarding",
          "[ruby][parser][endless-def][forwarding]") {
    Diagnostics d;
    auto module = ParseVersion(
        "def identity(value) = value\ndef forward(...)\n  target(...)\nend\n", d,
        polyglot::frontends::RubyVersion::kRuby3_0);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto identity = std::dynamic_pointer_cast<MethodDecl>(module->body[0]);
    REQUIRE(identity);
    auto identity_body = std::dynamic_pointer_cast<Block>(identity->body);
    REQUIRE(identity_body);
    REQUIRE(identity_body->stmts.size() == 1);

    auto forward = std::dynamic_pointer_cast<MethodDecl>(module->body[1]);
    REQUIRE(forward);
    REQUIRE(forward->params.size() == 1);
    REQUIRE(forward->params[0].forwarding);
    auto forward_body = std::dynamic_pointer_cast<Block>(forward->body);
    auto call_stmt = std::dynamic_pointer_cast<ExprStmt>(forward_body->stmts[0]);
    auto call = std::dynamic_pointer_cast<CallExpr>(call_stmt->expr);
    REQUIRE(call);
    REQUIRE(call->args.size() == 1);
    REQUIRE(std::dynamic_pointer_cast<Identifier>(call->args[0])->name == "...");
}

TEST_CASE("Ruby parser represents rightward assignment and hash omission",
          "[ruby][parser][rightward][hash-omission]") {
    Diagnostics d;
    auto module = ParseVersion("value => captured\noptions = {captured:}\n", d,
                               polyglot::frontends::RubyVersion::kRuby3_1);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto rightward_stmt = std::dynamic_pointer_cast<ExprStmt>(module->body[0]);
    auto rightward = std::dynamic_pointer_cast<AssignExpr>(rightward_stmt->expr);
    REQUIRE(rightward);
    REQUIRE(rightward->op == "=>");
    REQUIRE(std::dynamic_pointer_cast<Identifier>(rightward->target)->name == "captured");
    REQUIRE(std::dynamic_pointer_cast<Identifier>(rightward->value)->name == "value");

    auto hash_stmt = std::dynamic_pointer_cast<ExprStmt>(module->body[1]);
    auto hash_assignment = std::dynamic_pointer_cast<AssignExpr>(hash_stmt->expr);
    auto hash = std::dynamic_pointer_cast<HashLit>(hash_assignment->value);
    REQUIRE(hash);
    REQUIRE(std::dynamic_pointer_cast<Identifier>(hash->pairs[0].value)->name == "captured");

    Diagnostics old_d;
    ParseVersion("captured = 1\noptions = {captured:}\n", old_d,
                 polyglot::frontends::RubyVersion::kRuby3_0);
    bool mismatch = false;
    for (const auto &diagnostic : old_d.All())
        mismatch |= diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);
}

TEST_CASE("Ruby parser preserves safe navigation and brace blocks",
          "[ruby][parser][safe-navigation][block]") {
    Diagnostics d;
    auto module = ParseVersion("result = object&.fetch(1)\nwork(1) { |x| x }\n", d,
                               polyglot::frontends::RubyVersion::kRuby3_2);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto assignment_stmt = std::dynamic_pointer_cast<ExprStmt>(module->body[0]);
    auto assignment = std::dynamic_pointer_cast<AssignExpr>(assignment_stmt->expr);
    auto safe_call = std::dynamic_pointer_cast<CallExpr>(assignment->value);
    REQUIRE(safe_call);
    REQUIRE(safe_call->safe);

    auto block_stmt = std::dynamic_pointer_cast<ExprStmt>(module->body[1]);
    auto block_call = std::dynamic_pointer_cast<CallExpr>(block_stmt->expr);
    REQUIRE(block_call);
    REQUIRE(block_call->block);
    REQUIRE(block_call->block_params == std::vector<std::string>{"x"});
}

TEST_CASE("Ruby 4.0 accepts line-leading logical continuation with an exact gate",
          "[ruby][parser][version-gating][ruby-4]") {
    const char *source = "enabled\n  && ready\n  or fallback\n";

    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::RubyVersion::kRuby3_4);
    size_t old_mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++old_mismatches;
    REQUIRE(old_mismatches == 2);

    Diagnostics modern_d;
    auto module = ParseVersion(source, modern_d, polyglot::frontends::RubyVersion::kRuby4_0);
    REQUIRE_FALSE(modern_d.HasErrors());
    REQUIRE(module->body.size() == 1);
    auto statement = std::dynamic_pointer_cast<ExprStmt>(module->body[0]);
    auto outer = std::dynamic_pointer_cast<BinaryExpr>(statement->expr);
    REQUIRE(outer);
    REQUIRE(outer->op == "or");
    auto inner = std::dynamic_pointer_cast<BinaryExpr>(outer->left);
    REQUIRE(inner);
    REQUIRE(inner->op == "&&");
}

TEST_CASE("Ruby 3.4 implicit block parameter it is represented and gated",
          "[ruby][parser][version-gating][implicit-it]") {
    const char *source = "work() { it }\n";

    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::RubyVersion::kRuby3_3);
    size_t old_mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++old_mismatches;
    REQUIRE(old_mismatches == 1);

    Diagnostics modern_d;
    auto module = ParseVersion(source, modern_d, polyglot::frontends::RubyVersion::kRuby3_4);
    REQUIRE_FALSE(modern_d.HasErrors());
    auto statement = std::dynamic_pointer_cast<ExprStmt>(module->body[0]);
    auto call = std::dynamic_pointer_cast<CallExpr>(statement->expr);
    REQUIRE(call);
    REQUIRE(call->uses_implicit_it);
    REQUIRE(call->block_params == std::vector<std::string>{"it"});
}

TEST_CASE("Ruby signature extraction uses the caller's language version",
          "[ruby][signatures][version-gating]") {
    const std::string source =
        "def enabled(flag)\n"
        "  flag\n"
        "    && true\n"
        "end\n";
    RubyLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions modern_options;
    modern_options.ruby_version = polyglot::frontends::RubyVersion::kRuby4_0;
    Diagnostics modern_d;
    auto signatures = frontend.ExtractSignatures(source, "<test>", "api", modern_d,
                                                  modern_options);
    REQUIRE_FALSE(modern_d.HasErrors());
    REQUIRE(signatures.size() == 1);
    REQUIRE(signatures[0].name == "enabled");

    polyglot::frontends::FrontendOptions old_options;
    old_options.ruby_version = polyglot::frontends::RubyVersion::kRuby3_4;
    Diagnostics old_d;
    REQUIRE(frontend.ExtractSignatures(source, "<test>", "api", old_d, old_options).empty());
    REQUIRE(old_d.HasErrors());
}

TEST_CASE("Ruby lowering emits function IR", "[ruby][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("# @param x [Float]\n# @return [Float]\n"
                      "def inc(x)\n  x + 1.0\nend\n", d);
    CHECK_FALSE(d.HasErrors());
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("inc") != std::string::npos);
}

TEST_CASE("Ruby lowering rejects dynamic truthiness and missing YARD ABIs",
          "[ruby][lowering][fail-closed]") {
    Diagnostics missing_types;
    LowerIR("def dynamic(value)\n  value\nend\n", missing_types);
    REQUIRE(missing_types.HasErrors());

    Diagnostics truthiness;
    LowerIR("# @param value [Integer]\n# @return [Integer]\n"
            "def choose(value)\n  if value\n    1\n  end\nend\n", truthiness);
    bool unsupported = false;
    for (const auto &diagnostic : truthiness.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(unsupported);
}

TEST_CASE("Ruby lowering fails closed for dynamic modern constructs",
          "[ruby][lowering][pattern][safe-navigation]") {
    Diagnostics pattern_d;
    LowerIR("def classify(value)\n  case value\n  in {x:}\n    x\n  end\nend\n", pattern_d);
    bool pattern_unsupported = false;
    for (const auto &diagnostic : pattern_d.All())
        pattern_unsupported = pattern_unsupported ||
                              diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(pattern_unsupported);

    Diagnostics safe_d;
    LowerIR("def fetch(object)\n  object&.value(1)\nend\n", safe_d);
    bool safe_unsupported = false;
    for (const auto &diagnostic : safe_d.All())
        safe_unsupported = safe_unsupported ||
                           diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(safe_unsupported);

    Diagnostics forwarding_d;
    LowerIR("def forward(...)\n  target(...)\nend\n", forwarding_d);
    bool forwarding_unsupported = false;
    for (const auto &diagnostic : forwarding_d.All())
        forwarding_unsupported = forwarding_unsupported ||
                                 diagnostic.code ==
                                     polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(forwarding_unsupported);

    Diagnostics block_d;
    LowerIR("def transform(items)\n  items.map { it }\nend\n", block_d);
    bool block_unsupported = false;
    for (const auto &diagnostic : block_d.All())
        block_unsupported = block_unsupported ||
                            diagnostic.code ==
                                polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(block_unsupported);
}
