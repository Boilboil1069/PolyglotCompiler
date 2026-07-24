// Unit tests for the JavaScript frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/javascript/include/javascript_lexer.h"
#include "frontends/javascript/include/javascript_frontend.h"
#include "frontends/javascript/include/javascript_parser.h"
#include "frontends/javascript/include/javascript_sema.h"
#include "frontends/javascript/include/javascript_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
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

static std::shared_ptr<Module> ParseVersion(const char *src, Diagnostics &d,
                                            polyglot::frontends::EcmaVersion version) {
    JsLexer lex(src, "<test>");
    JsParser p(lex, d);
    p.SetEcmaVersion(version);
    p.ParseModule();
    return p.TakeModule();
}

static std::string LowerIRVersion(const char *src, Diagnostics &d,
                                  polyglot::frontends::EcmaVersion version) {
    auto m = ParseVersion(src, d, version);
    if (!m) return "";
    SemaContext ctx(d);
    AnalyzeModule(*m, ctx);
    IRContext ir;
    LowerToIR(*m, ir, d);
    std::ostringstream os;
    for (const auto &fn : ir.Functions()) polyglot::ir::PrintFunction(*fn, os);
    return os.str();
}

static std::string LowerIR(const char *src, Diagnostics &d) {
    return LowerIRVersion(src, d, polyglot::frontends::kEcmaVersionDefault);
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

TEST_CASE("JS lexer keeps contextual words as identifiers", "[javascript][lexer][contextual]") {
    auto ts = Tokenize("async await of static as from let undefined");
    REQUIRE(ts.size() == 9);
    for (size_t i = 0; i < 8; ++i)
        CHECK(ts[i].kind == TokenKind::kIdentifier);
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

TEST_CASE("JS ES2015 syntax is rejected by ES5 and accepted by ES2015",
          "[javascript][parser][version-gating]") {
    auto mismatch_count = [](const Diagnostics &diagnostics) {
        size_t count = 0;
        for (const auto &diagnostic : diagnostics.All())
            if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
                ++count;
        return count;
    };

    struct VersionCase {
        const char *source;
        size_t expected_es5_mismatches;
    };
    const VersionCase cases[] = {
        {"let mutable_value = 1; const fixed_value = 2;", 2},
        {"class Box {}", 1},
        {"var single = value => value; var parenthesized = (value) => value;", 2},
        {"import { value } from 'pkg'; export { value };", 2},
    };

    for (const auto &version_case : cases) {
        Diagnostics es5_diagnostics;
        ParseVersion(version_case.source, es5_diagnostics,
                     polyglot::frontends::EcmaVersion::kEs5);
        CHECK(mismatch_count(es5_diagnostics) == version_case.expected_es5_mismatches);

        Diagnostics es2015_diagnostics;
        ParseVersion(version_case.source, es2015_diagnostics,
                     polyglot::frontends::EcmaVersion::kEs2015);
        CHECK_FALSE(es2015_diagnostics.HasErrors());
    }
}

TEST_CASE("JS parser builds template interpolation AST", "[javascript][parser][template]") {
    Diagnostics d;
    auto m = Parse("const text = `hi ${name + \"!\"}!`;", d);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(m->body.size() == 1);
    auto decl = std::dynamic_pointer_cast<VariableDecl>(m->body[0]);
    REQUIRE(decl);
    REQUIRE(decl->decls.size() == 1);
    auto templ = std::dynamic_pointer_cast<TemplateLiteral>(decl->decls[0].init);
    REQUIRE(templ);
    REQUIRE(templ->quasis == std::vector<std::string>{"hi ", "!"});
    REQUIRE(templ->expressions.size() == 1);
    REQUIRE(std::dynamic_pointer_cast<BinaryExpr>(templ->expressions[0]));
}

TEST_CASE("JS return observes ASI line terminator restriction", "[javascript][parser][asi]") {
    Diagnostics d;
    auto m = Parse("function f() { return\n1; }", d);
    REQUIRE_FALSE(d.HasErrors());
    auto fn = std::dynamic_pointer_cast<FunctionDecl>(m->body[0]);
    REQUIRE(fn);
    auto body = std::dynamic_pointer_cast<BlockStatement>(fn->body);
    REQUIRE(body);
    REQUIRE(body->statements.size() == 2);
    auto ret = std::dynamic_pointer_cast<ReturnStatement>(body->statements[0]);
    REQUIRE(ret);
    REQUIRE_FALSE(ret->value);
    REQUIRE(std::dynamic_pointer_cast<ExprStatement>(body->statements[1]));
}

TEST_CASE("JS parser accepts async arrows and contextual identifier calls",
          "[javascript][parser][async][contextual]") {
    Diagnostics d;
    auto m = Parse("const f = async x => x; const g = async(x);", d);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(m->body.size() == 2);
    auto first = std::dynamic_pointer_cast<VariableDecl>(m->body[0]);
    auto second = std::dynamic_pointer_cast<VariableDecl>(m->body[1]);
    REQUIRE(first);
    REQUIRE(second);
    auto arrow = std::dynamic_pointer_cast<ArrowFunction>(first->decls[0].init);
    REQUIRE(arrow);
    REQUIRE(arrow->is_async);
    auto call = std::dynamic_pointer_cast<CallExpr>(second->decls[0].init);
    REQUIRE(call);
    REQUIRE(std::dynamic_pointer_cast<Identifier>(call->callee)->name == "async");
}

TEST_CASE("JS private fields and static blocks use ES2022 gates",
          "[javascript][parser][class][version-gating]") {
    const char *source = "class C { #value = 1; static { this.ready = true; } }";
    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::EcmaVersion::kEs2020);
    size_t mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++mismatches;
    REQUIRE(mismatches == 2);

    Diagnostics new_d;
    auto module = ParseVersion(source, new_d, polyglot::frontends::EcmaVersion::kEs2022);
    REQUIRE_FALSE(new_d.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->body[0]);
    REQUIRE(cls);
    REQUIRE(cls->members.size() == 2);
    auto field = std::dynamic_pointer_cast<FieldDecl>(cls->members[0]);
    REQUIRE(field);
    REQUIRE(field->is_private);
    REQUIRE(field->name == "#value");
    REQUIRE(std::dynamic_pointer_cast<StaticBlock>(cls->members[1]));
}

TEST_CASE("JS static and async remain valid class member names",
          "[javascript][parser][class][contextual]") {
    Diagnostics d;
    auto module = Parse("class C { static() {} async() {} }", d);
    REQUIRE_FALSE(d.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->body[0]);
    REQUIRE(cls);
    REQUIRE(cls->members.size() == 2);
    auto static_method = std::dynamic_pointer_cast<MethodDecl>(cls->members[0]);
    auto async_method = std::dynamic_pointer_cast<MethodDecl>(cls->members[1]);
    REQUIRE(static_method->name == "static");
    REQUIRE_FALSE(static_method->is_static);
    REQUIRE(async_method->name == "async");
    REQUIRE_FALSE(async_method->is_async);
}

TEST_CASE("JS parses dynamic import and import.meta as expressions",
          "[javascript][parser][module]") {
    Diagnostics d;
    auto module = Parse("const p = import(\"pkg\"); const u = import.meta;", d);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto dynamic_decl = std::dynamic_pointer_cast<VariableDecl>(module->body[0]);
    auto meta_decl = std::dynamic_pointer_cast<VariableDecl>(module->body[1]);
    REQUIRE(std::dynamic_pointer_cast<CallExpr>(dynamic_decl->decls[0].init));
    auto meta = std::dynamic_pointer_cast<MemberExpr>(meta_decl->decls[0].init);
    REQUIRE(meta);
    REQUIRE(meta->property == "meta");
}

TEST_CASE("JS parser flags unterminated brace", "[javascript][parser]") {
    Diagnostics d;
    Parse("function bad() { return 1; ", d);
    CHECK(d.HasErrors());
}

TEST_CASE("JS lowering produces IR for function", "[javascript][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("/**\n * @param {number} x\n * @returns {number}\n */\n"
                      "function inc(x) { return x + 1; }", d);
    CHECK_FALSE(d.HasErrors());
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("inc") != std::string::npos);
}

TEST_CASE("JS lowering rejects missing static signatures, fallthrough, and compound assignment",
          "[javascript][lowering][fail-closed]") {
    Diagnostics missing_types;
    LowerIR("function dynamic(x) { return x; }", missing_types);
    REQUIRE(missing_types.HasErrors());

    Diagnostics fallthrough;
    LowerIR("/** @returns {number} */ function missing() { const x = 1; }", fallthrough);
    REQUIRE(fallthrough.HasErrors());

    Diagnostics compound;
    LowerIR("/**\n * @param {number} x\n * @returns {number}\n */\n"
            "function update(x) { x += 1; return x; }", compound);
    REQUIRE(compound.HasErrors());
    bool saw_unsupported = false;
    for (const auto &diagnostic : compound.All())
        saw_unsupported = saw_unsupported ||
                          diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(saw_unsupported);
}

TEST_CASE("JS lowering fails closed for modern class runtime features",
          "[javascript][lowering][class]") {
    Diagnostics d;
    LowerIR("class C { #value = 1; static { this.ready = true; } }", d);
    bool unsupported = false;
    for (const auto &diagnostic : d.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(unsupported);

    Diagnostics ordinary_d;
    LowerIR("class Plain { method() { return 1; } }", ordinary_d);
    bool ordinary_unsupported = false;
    for (const auto &diagnostic : ordinary_d.All())
        ordinary_unsupported = ordinary_unsupported ||
                               diagnostic.code ==
                                   polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(ordinary_unsupported);
}

TEST_CASE("JS using declarations are versioned AST nodes and fail closed in lowering",
          "[javascript][parser][using][version-gating]") {
    const char *source = "using resource = acquire(); await using asyncResource = acquireAsync();";
    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::EcmaVersion::kEs2026);
    size_t mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++mismatches;
    REQUIRE(mismatches == 2);

    Diagnostics modern_d;
    auto module = ParseVersion(source, modern_d, polyglot::frontends::EcmaVersion::kEsNext);
    REQUIRE_FALSE(modern_d.HasErrors());
    REQUIRE(module->body.size() == 2);
    auto sync_using = std::dynamic_pointer_cast<VariableDecl>(module->body[0]);
    auto async_using = std::dynamic_pointer_cast<VariableDecl>(module->body[1]);
    REQUIRE(sync_using);
    REQUIRE(async_using);
    REQUIRE(sync_using->kind == "using");
    REQUIRE(async_using->kind == "await using");

    Diagnostics lowering_d;
    LowerIRVersion("/** @returns {void} */ function f() { using resource = acquire(); }",
                   lowering_d, polyglot::frontends::EcmaVersion::kEsNext);
    bool unsupported = false;
    for (const auto &diagnostic : lowering_d.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(unsupported);
}

TEST_CASE("JS decorators produce explicit unsupported-syntax diagnostics",
          "[javascript][parser][decorator]") {
    Diagnostics d;
    auto module = Parse("@sealed\nclass C {\n@logged\nmethod() {}\n}", d);
    size_t unsupported = 0;
    for (const auto &diagnostic : d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax)
            ++unsupported;
    REQUIRE(unsupported == 2);
    REQUIRE(module->body.size() == 1);
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->body[0]);
    REQUIRE(cls);
    REQUIRE(cls->members.size() == 1);
    REQUIRE(std::dynamic_pointer_cast<MethodDecl>(cls->members[0]));
}

TEST_CASE("JS import attributes are preserved and gated at ES2025",
          "[javascript][parser][import-attributes][version-gating]") {
    const char *source = "import data from './data.json' with { type: 'json' };";
    Diagnostics old_d;
    ParseVersion(source, old_d, polyglot::frontends::EcmaVersion::kEs2024);
    size_t mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++mismatches;
    REQUIRE(mismatches == 1);

    Diagnostics modern_d;
    auto module = ParseVersion(source, modern_d, polyglot::frontends::EcmaVersion::kEs2025);
    REQUIRE_FALSE(modern_d.HasErrors());
    auto declaration = std::dynamic_pointer_cast<ImportDecl>(module->body[0]);
    REQUIRE(declaration);
    REQUIRE(declaration->attributes.size() == 1);
    REQUIRE(declaration->attributes[0].key == "type");
    REQUIRE(declaration->attributes[0].value == "'json'");

    Diagnostics legacy_d;
    ParseVersion("import data from './data.json' assert { type: 'json' };", legacy_d,
                 polyglot::frontends::EcmaVersion::kEs2025);
    bool unsupported = false;
    for (const auto &diagnostic : legacy_d.All())
        unsupported = unsupported ||
                      diagnostic.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    REQUIRE(unsupported);
}

TEST_CASE("JS RegExp literals preserve pattern and modern flags without becoming division",
          "[javascript][parser][regexp][version-gating]") {
    const char *source = "const matcher = /[a&&b]/v; const ratio = value / divisor;";
    Diagnostics modern_d;
    auto module = ParseVersion(source, modern_d, polyglot::frontends::EcmaVersion::kEs2024);
    REQUIRE_FALSE(modern_d.HasErrors());
    auto matcher_decl = std::dynamic_pointer_cast<VariableDecl>(module->body[0]);
    auto regex = std::dynamic_pointer_cast<Literal>(matcher_decl->decls[0].init);
    REQUIRE(regex);
    REQUIRE(regex->kind == Literal::Kind::kRegex);
    REQUIRE(regex->regex_pattern == "[a&&b]");
    REQUIRE(regex->regex_flags == "v");
    auto ratio_decl = std::dynamic_pointer_cast<VariableDecl>(module->body[1]);
    auto division = std::dynamic_pointer_cast<BinaryExpr>(ratio_decl->decls[0].init);
    REQUIRE(division);
    REQUIRE(division->op == "/");

    Diagnostics old_d;
    ParseVersion("const matcher = /[a&&b]/v;", old_d,
                 polyglot::frontends::EcmaVersion::kEs2023);
    size_t mismatches = 0;
    for (const auto &diagnostic : old_d.All())
        if (diagnostic.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
            ++mismatches;
    REQUIRE(mismatches == 1);

    Diagnostics lowering_d;
    LowerIR("function matcher() { return /[a&&b]/v; }", lowering_d);
    bool lowering_unsupported = false;
    for (const auto &diagnostic : lowering_d.All())
        lowering_unsupported = lowering_unsupported ||
                               diagnostic.code ==
                                   polyglot::frontends::ErrorCode::kUnsupportedLowering;
    REQUIRE(lowering_unsupported);
}

TEST_CASE("JS signature extraction uses the caller's ECMAScript version",
          "[javascript][signatures][version-gating]") {
    const std::string source =
        "using resource = 1; function exposed(value) { return value; }";
    JsLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions modern_options;
    modern_options.ecma_version = polyglot::frontends::EcmaVersion::kEsNext;
    Diagnostics modern_d;
    auto signatures = frontend.ExtractSignatures(source, "<test>", "api", modern_d,
                                                  modern_options);
    REQUIRE_FALSE(modern_d.HasErrors());
    REQUIRE(signatures.size() == 1);
    REQUIRE(signatures[0].name == "exposed");

    polyglot::frontends::FrontendOptions old_options;
    old_options.ecma_version = polyglot::frontends::EcmaVersion::kEs2026;
    Diagnostics old_d;
    REQUIRE(frontend.ExtractSignatures(source, "<test>", "api", old_d, old_options).empty());
    REQUIRE(old_d.HasErrors());
}
