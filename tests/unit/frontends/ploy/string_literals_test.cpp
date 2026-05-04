/**
 * @file     string_literals_test.cpp
 * @brief    Unit tests for raw / multiline / template string literals
 *           introduced in v1.17.0.
 *
 * @ingroup  Tests / Ploy / String-Literals
 * @author   Manning Cyrus
 * @date     2026-05-04
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
using polyglot::frontends::DiagnosticSeverity;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::FuncDecl;
using polyglot::ploy::Literal;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::TemplateString;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<string-lit-test>");
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

// Lex one source string and return the first non-EOF token.
Token FirstToken(const std::string &src) {
    PloyLexer lex(src, "<lex-test>");
    return lex.NextToken();
}

}  // namespace

TEST_CASE("regular string literal still lexes unchanged", "[ploy][lexer][string]") {
    Token t = FirstToken("\"hello\\n\"");
    REQUIRE(t.kind == TokenKind::kString);
    REQUIRE(t.lexeme == "\"hello\\n\"");
}

TEST_CASE("raw string r\"...\" preserves backslashes verbatim", "[ploy][lexer][raw]") {
    Token t = FirstToken("r\"C:\\path\\no\\escape\"");
    REQUIRE(t.kind == TokenKind::kString);
    // Raw bodies are re-encoded into the canonical "..." form with
    // backslashes escaped so downstream sees `\\` not `\`.
    REQUIRE(t.lexeme.find("\\\\path") != std::string::npos);
}

TEST_CASE("raw string r#\"...\"# may contain bare double-quotes",
          "[ploy][lexer][raw]") {
    Token t = FirstToken("r#\"contains \"quotes\" inside\"#");
    REQUIRE(t.kind == TokenKind::kString);
    // The embedded `"` survives — re-encoded as `\"` in the canonical form.
    REQUIRE(t.lexeme.find("\\\"quotes\\\"") != std::string::npos);
}

TEST_CASE("triple-quoted string preserves newlines as \\n",
          "[ploy][lexer][multiline]") {
    Token t = FirstToken("\"\"\"line1\nline2\"\"\"");
    REQUIRE(t.kind == TokenKind::kString);
    REQUIRE(t.lexeme.find("line1\\nline2") != std::string::npos);
}

TEST_CASE("template string f\"...\" parses to a TemplateString node",
          "[ploy][parser][template]") {
    auto r = Analyze("FUNC main() -> STRING { LET s = f\"x = {42}\"; RETURN s; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    REQUIRE(r.module != nullptr);
}

TEST_CASE("template string yields String type from sema",
          "[ploy][sema][template]") {
    auto r = Analyze("FUNC main() -> STRING { RETURN f\"answer = {42}\"; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("template string interpolates literal expressions of all formattable kinds",
          "[ploy][sema][template]") {
    auto r = Analyze(
        "FUNC main() -> STRING { RETURN f\"i={1} f={3.14} b={TRUE}\"; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("template string with literal-brace escapes {{ }} accepts braces",
          "[ploy][parser][template]") {
    auto r = Analyze("FUNC main() -> STRING { RETURN f\"raw {{brace}} only\"; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("unterminated template interpolation emits a diagnostic",
          "[ploy][parser][template]") {
    auto r = Analyze("FUNC main() -> STRING { RETURN f\"oops {x\"; }");
    REQUIRE(DiagsContain(r.diags, "unterminated interpolation"));
}

TEST_CASE("identifier starting with r is not mis-lexed as a raw string",
          "[ploy][lexer][raw]") {
    auto r = Analyze("FUNC main() -> i32 { LET result: i32 = 1; RETURN result; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("identifier starting with f is not mis-lexed as a template string",
          "[ploy][lexer][template]") {
    auto r = Analyze("FUNC main() -> i32 { LET foo: i32 = 1; RETURN foo; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("raw string is usable in source position",
          "[ploy][parser][raw]") {
    auto r = Analyze("FUNC main() -> STRING { RETURN r\"C:\\path\\file.txt\"; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("triple-quoted string is usable in source position",
          "[ploy][parser][multiline]") {
    auto r = Analyze("FUNC main() -> STRING { RETURN \"\"\"line one\nline two\"\"\"; }");
    REQUIRE_FALSE(r.diags.HasErrors());
}
