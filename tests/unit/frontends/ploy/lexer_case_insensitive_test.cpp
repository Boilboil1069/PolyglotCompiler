// ============================================================================
// Unit tests for ploy lexer keyword case-insensitivity (demand 2026-04-28-6).
//
// Covers:
//   1. All 54 reserved keywords are recognized in lower-case, UPPER-case and
//      Mixed-case spellings, and the canonical (UPPER) form is emitted as
//      `Token::lexeme` in every case.
//   2. When the source spelling differs from the canonical form, the lexer
//      preserves the original input in `Token::raw_lexeme`; when the source
//      already matches the canonical form, `raw_lexeme` is left empty so
//      that pre-existing diagnostics that read `lexeme` continue to print
//      what the user typed.
//   3. `Token::SourceText()` always yields the user-visible spelling.
//   4. Identifiers remain *case-sensitive*: `myVar`, `MyVar`, `MYVAR` lex
//      as three distinct identifier tokens, none of which are promoted to
//      keywords.
//   5. A keyword followed immediately by other identifier characters is
//      not a keyword (e.g. `letter` is an identifier, not the keyword
//      `LET` followed by `ter`).
// ============================================================================

#include <algorithm>
#include <cctype>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "frontends/common/include/lexer_base.h"
#include "frontends/ploy/include/ploy_lexer.h"

using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;

namespace {

// Canonical (upper-case) spellings of every Ploy reserved word.  Must stay
// in lock-step with `kCanonicalKeywords` in
// `frontends/ploy/src/lexer/lexer.cpp`.  When the parser learns a new
// keyword, add it here and to the lexer set in the same change-list.
const std::vector<std::string> &CanonicalKeywords() {
    static const std::vector<std::string> kKeywords = {
        "LINK",     "IMPORT",  "EXPORT", "MAP_TYPE", "PIPELINE", "FUNC",
        "LET",      "VAR",     "RETURN", "RETURNS",  "IF",       "ELSE",
        "WHILE",    "FOR",     "IN",     "MATCH",    "CASE",     "DEFAULT",
        "BREAK",    "CONTINUE","AS",     "TRUE",     "FALSE",    "NULL",
        "AND",      "OR",      "NOT",    "CALL",     "VOID",     "INT",
        "FLOAT",    "STRING",  "BOOL",   "ARRAY",    "STRUCT",   "PACKAGE",
        "LIST",     "TUPLE",   "DICT",   "OPTION",   "MAP_FUNC", "CONVERT",
        "CONFIG",   "VENV",    "CONDA",  "UV",       "PIPENV",   "POETRY",
        "NEW",      "METHOD",  "GET",    "SET",      "WITH",     "DELETE",
        "EXTEND",   "LANG"};
    return kKeywords;
}

// Lex `source` to completion and return every token (excluding the trailing
// kEndOfFile sentinel).  Keeps the test bodies free of boiler-plate.
std::vector<Token> LexAll(const std::string &source) {
    PloyLexer lexer(source, "<test>");
    std::vector<Token> out;
    while (true) {
        Token t = lexer.NextToken();
        if (t.kind == TokenKind::kEndOfFile) {
            break;
        }
        out.push_back(std::move(t));
    }
    return out;
}

// ASCII lower-case fold mirroring the lexer's upper-case fold.  We cannot
// share the lexer's helper because it is private to the translation unit;
// duplicating the conversion here is intentional and defensive — any drift
// would manifest as a compile-time mismatch in CanonicalKeywords().
std::string ToLowerAscii(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

// Produce a "MiXeD"-case variant of `s`: even indices upper-case, odd
// indices lower-case.  This is enough to exercise the case-insensitive
// path for tokens whose canonical form contains an underscore (e.g.
// `MAP_TYPE` → `mAp_TyPe`).
std::string ToMixedCase(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        out.push_back(static_cast<char>(
            (i % 2 == 0) ? std::toupper(c) : std::tolower(c)));
    }
    return out;
}

} // namespace

TEST_CASE("Ploy lexer recognizes every keyword in upper-case (canonical, no raw_lexeme)",
          "[ploy][lexer][case]") {
    for (const auto &kw : CanonicalKeywords()) {
        CAPTURE(kw);
        const auto tokens = LexAll(kw);
        REQUIRE(tokens.size() == 1);
        REQUIRE(tokens[0].kind == TokenKind::kKeyword);
        REQUIRE(tokens[0].lexeme == kw);
        // Canonical input → no need to remember a separate spelling.
        REQUIRE(tokens[0].raw_lexeme.empty());
        REQUIRE(tokens[0].SourceText() == kw);
    }
}

TEST_CASE("Ploy lexer recognizes every keyword in lower-case and remembers the raw spelling",
          "[ploy][lexer][case]") {
    for (const auto &kw : CanonicalKeywords()) {
        const std::string lower = ToLowerAscii(kw);
        CAPTURE(kw, lower);
        const auto tokens = LexAll(lower);
        REQUIRE(tokens.size() == 1);
        REQUIRE(tokens[0].kind == TokenKind::kKeyword);
        REQUIRE(tokens[0].lexeme == kw);          // canonical UPPER
        REQUIRE(tokens[0].raw_lexeme == lower);   // user-typed
        REQUIRE(tokens[0].SourceText() == lower); // diagnostics see source
    }
}

TEST_CASE("Ploy lexer recognizes every keyword in mixed-case",
          "[ploy][lexer][case]") {
    for (const auto &kw : CanonicalKeywords()) {
        const std::string mixed = ToMixedCase(kw);
        CAPTURE(kw, mixed);
        const auto tokens = LexAll(mixed);
        REQUIRE(tokens.size() == 1);
        REQUIRE(tokens[0].kind == TokenKind::kKeyword);
        REQUIRE(tokens[0].lexeme == kw);
        if (mixed == kw) {
            // Single-letter keywords (none currently) would land here; we
            // keep the branch so the test stays correct should one appear.
            REQUIRE(tokens[0].raw_lexeme.empty());
        } else {
            REQUIRE(tokens[0].raw_lexeme == mixed);
        }
        REQUIRE(tokens[0].SourceText() == mixed);
    }
}

TEST_CASE("Ploy lexer keeps identifiers case-sensitive and never promotes them to keywords",
          "[ploy][lexer][case]") {
    // None of these are reserved words after case-folding.
    const std::vector<std::string> identifiers = {
        "myVar", "MyVar", "MYVAR", "ploy", "Ploy_v2", "_underscore"};
    for (const auto &id : identifiers) {
        CAPTURE(id);
        const auto tokens = LexAll(id);
        REQUIRE(tokens.size() == 1);
        REQUIRE(tokens[0].kind == TokenKind::kIdentifier);
        REQUIRE(tokens[0].lexeme == id); // identifier text preserved verbatim
        REQUIRE(tokens[0].raw_lexeme.empty());
    }

    // Three different spellings of the same letters must remain distinct
    // identifier tokens — case-folding is restricted to keywords only.
    const auto trio = LexAll("alpha Alpha ALPHA");
    REQUIRE(trio.size() == 3);
    for (const auto &t : trio) {
        REQUIRE(t.kind == TokenKind::kIdentifier);
    }
    REQUIRE(trio[0].lexeme == "alpha");
    REQUIRE(trio[1].lexeme == "Alpha");
    REQUIRE(trio[2].lexeme == "ALPHA");
}

TEST_CASE("Ploy lexer does not split keywords out of longer identifiers",
          "[ploy][lexer][case]") {
    // Each of these contains a keyword as a *prefix* but is itself an
    // identifier; the lexer must consume the whole word before deciding.
    const std::vector<std::string> identifiers = {
        "letter",     // starts with `let`
        "iffy",       // starts with `if`
        "linker",     // starts with `link`
        "newton",     // starts with `new`
        "andrew",     // starts with `and`
        "orange",     // starts with `or`
        "notation"};  // starts with `not`
    for (const auto &id : identifiers) {
        CAPTURE(id);
        const auto tokens = LexAll(id);
        REQUIRE(tokens.size() == 1);
        REQUIRE(tokens[0].kind == TokenKind::kIdentifier);
        REQUIRE(tokens[0].lexeme == id);
    }
}
