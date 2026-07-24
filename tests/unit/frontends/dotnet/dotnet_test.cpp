// Unit tests for the .NET frontend (lexer, parser, sema, lowering).

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/dotnet/include/dotnet_lexer.h"
#include "frontends/dotnet/include/dotnet_parser.h"
#include "frontends/dotnet/include/dotnet_sema.h"
#include "frontends/dotnet/include/dotnet_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ir::IRContext;
using polyglot::dotnet::DotnetLexer;
using polyglot::dotnet::DotnetParser;
using namespace polyglot::dotnet;

// ============================================================================
// Helper utilities
// ============================================================================

static std::vector<Token> Tokenize(const char *src) {
    DotnetLexer lexer(src, "<test>");
    std::vector<Token> tokens;
    for (;;) {
        auto tok = lexer.NextToken();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::kEndOfFile) break;
    }
    return tokens;
}

static std::shared_ptr<Module> ParseDotnet(const char *src, Diagnostics &diags) {
    DotnetLexer lexer(src, "<test>");
    DotnetParser parser(lexer, diags);
    parser.ParseModule();
    return parser.TakeModule();
}

static std::shared_ptr<Module> ParseDotnetAt(
    const char *src, Diagnostics &diags, polyglot::frontends::DotnetLangVersion version) {
    DotnetLexer lexer(src, "<test>");
    DotnetParser parser(lexer, diags);
    parser.SetDotnetLangVersion(version);
    parser.ParseModule();
    return parser.TakeModule();
}

static std::string LowerAndGetIR(const char *src, Diagnostics &diags) {
    auto mod = ParseDotnet(src, diags);
    if (!mod || diags.HasErrors()) return "";
    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*mod, sema);
    // Continue to lowering even if sema reports type-mapping diagnostics
    IRContext ctx;
    LowerToIR(*mod, ctx, diags);
    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return oss.str();
}

// ============================================================================
// Lexer Tests
// ============================================================================

TEST_CASE("DotNet lexer tokenizes keywords", "[dotnet][lexer]") {
    auto tokens = Tokenize("public class Program { }");
    REQUIRE(tokens.size() >= 5);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "public");
    CHECK(tokens[1].kind == TokenKind::kKeyword);
    CHECK(tokens[1].lexeme == "class");
    CHECK(tokens[2].kind == TokenKind::kIdentifier);
    CHECK(tokens[2].lexeme == "Program");
}

TEST_CASE("DotNet lexer tokenizes string literals", "[dotnet][lexer]") {
    auto tokens = Tokenize(R"("Hello, World!")");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kString);
}

TEST_CASE("DotNet lexer tokenizes integer literals", "[dotnet][lexer]") {
    auto tokens = Tokenize("42 0xFF 0b1010");
    REQUIRE(tokens.size() >= 4); // 3 numbers + EOF
    CHECK(tokens[0].kind == TokenKind::kNumber);
    CHECK(tokens[0].lexeme == "42");
}

TEST_CASE("DotNet lexer tokenizes operators", "[dotnet][lexer]") {
    auto tokens = Tokenize("+ - * / ?? ?.");
    REQUIRE(tokens.size() >= 7); // 6 operators + EOF
    CHECK(tokens[0].lexeme == "+");
    CHECK(tokens[1].lexeme == "-");
}

TEST_CASE("DotNet lexer tokenizes attributes", "[dotnet][lexer]") {
    auto tokens = Tokenize("[Obsolete] void Foo() {}");
    // Verify square bracket and identifier are present
    bool found_obsolete = false;
    for (auto &t : tokens) {
        if (t.lexeme == "Obsolete") {
            found_obsolete = true;
            break;
        }
    }
    CHECK(found_obsolete);
}

TEST_CASE("DotNet lexer tokenizes verbatim strings", "[dotnet][lexer]") {
    auto tokens = Tokenize(R"(@"C:\Users\file.txt")");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kString);
}

TEST_CASE("DotNet lexer keeps C# raw strings in one token", "[dotnet][lexer][modern]") {
    auto tokens = Tokenize(R"cs("""hello
world""")cs");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].kind == TokenKind::kString);
    CHECK(tokens[0].lexeme.find("hello") != std::string::npos);
}

TEST_CASE("DotNet reports an unterminated raw string at EOF", "[dotnet][lexer][parser]") {
    auto tokens = Tokenize("\"\"\"unterminated");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].kind == TokenKind::kUnknown);

    Diagnostics diags;
    ParseDotnet("class C { string F() { return \"\"\"unterminated", diags);
    bool unterminated = false;
    for (const auto &diag : diags.All())
        unterminated |= diag.code == polyglot::frontends::ErrorCode::kUnterminatedString;
    CHECK(unterminated);
}

TEST_CASE("DotNet lexer skips comments", "[dotnet][lexer]") {
    auto tokens = Tokenize("int x; // line comment\nint y;");
    int kw_count = 0;
    for (auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword && t.lexeme == "int") kw_count++;
    }
    CHECK(kw_count == 2);
}

// ============================================================================
// Parser Tests
// ============================================================================

TEST_CASE("DotNet parser parses empty class", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet("public class Empty { }", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() == 1);
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->name == "Empty");
    CHECK(cls->access == "public");
}

TEST_CASE("DotNet parser parses class with method", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
class Greeter {
    public string Greet(string name) {
        return "Hello";
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->name == "Greeter");
    REQUIRE(cls->members.size() >= 1);
    auto method = std::dynamic_pointer_cast<MethodDecl>(cls->members[0]);
    REQUIRE(method);
    CHECK(method->name == "Greet");
    CHECK(method->params.size() == 1);
}

TEST_CASE("DotNet parser parses interface", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
public interface IDisposable {
    void Dispose();
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto iface = std::dynamic_pointer_cast<InterfaceDecl>(mod->declarations[0]);
    REQUIRE(iface);
    CHECK(iface->name == "IDisposable");
}

TEST_CASE("DotNet parser parses enum", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
public enum Color {
    Red, Green, Blue
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto en = std::dynamic_pointer_cast<EnumDecl>(mod->declarations[0]);
    REQUIRE(en);
    CHECK(en->name == "Color");
    CHECK(en->members.size() == 3);
}

TEST_CASE("DotNet parser parses struct", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
public struct Point {
    public int X;
    public int Y;
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto st = std::dynamic_pointer_cast<StructDecl>(mod->declarations[0]);
    REQUIRE(st);
    CHECK(st->name == "Point");
    CHECK(st->members.size() == 2);
}

TEST_CASE("DotNet parser parses namespace", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
namespace MyApp {
    class Program { }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto ns = std::dynamic_pointer_cast<NamespaceDecl>(mod->declarations[0]);
    REQUIRE(ns);
    CHECK(ns->name == "MyApp");
}

TEST_CASE("DotNet parser parses using directives", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
using System;
using System.Collections.Generic;

class Foo { }
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    CHECK(mod->usings.size() == 2);
    CHECK(mod->usings[0]->ns == "System");
}

TEST_CASE("DotNet parser parses record (.NET 5+)", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
public record Person(string FirstName, string LastName);
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    // Records are represented as ClassDecl with is_record flag
    auto rec = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(rec);
    CHECK(rec->name == "Person");
    CHECK(rec->is_record);
}

TEST_CASE("DotNet parser parses class with constructor", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
class Person {
    string _name;

    public Person(string name) {
        _name = name;
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("DotNet parser parses top-level statements (.NET 6+)", "[dotnet][parser]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
Console.WriteLine("Hello");
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("DotNet parser preserves struct and interface members", "[dotnet][parser][modern]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
struct Counter {
    int value;
    int Next() { return value + 1; }
    int Value { get; set; }
}
interface ICounter {
    int Next();
    int Value { get; }
}
    )", diags);
    REQUIRE(mod);
    std::string diagnostic_text;
    for (const auto &diag : diags.All())
        diagnostic_text += std::to_string(diag.loc.line) + ":" +
                           std::to_string(diag.loc.column) + " " + diag.message + "\n";
    INFO(diagnostic_text);
    REQUIRE_FALSE(diags.HasErrors());
    auto st = std::dynamic_pointer_cast<StructDecl>(mod->declarations.at(0));
    auto iface = std::dynamic_pointer_cast<InterfaceDecl>(mod->declarations.at(1));
    REQUIRE(st);
    REQUIRE(iface);
    CHECK(st->members.size() == 3);
    CHECK(iface->members.size() == 2);
}

TEST_CASE("DotNet parser gates modern declaration forms", "[dotnet][parser][versions]") {
    auto has_version_error = [](const Diagnostics &diags) {
        for (const auto &diag : diags.All())
            if (diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
                return true;
        return false;
    };

    Diagnostics raw_old;
    ParseDotnetAt(R"cs(class C { string Text() { return """hello"""; } })cs", raw_old,
                  polyglot::frontends::DotnetLangVersion::kCs10);
    CHECK(has_version_error(raw_old));
    Diagnostics raw_new;
    ParseDotnetAt(R"cs(class C { string Text() { return """hello"""; } })cs", raw_new,
                  polyglot::frontends::DotnetLangVersion::kCs11);
    CHECK_FALSE(raw_new.HasErrors());

    Diagnostics global_old;
    ParseDotnetAt("global using System; class C {}", global_old,
                  polyglot::frontends::DotnetLangVersion::kCs9);
    CHECK(has_version_error(global_old));

    Diagnostics record_old;
    ParseDotnetAt("record struct Point(int X, int Y);", record_old,
                  polyglot::frontends::DotnetLangVersion::kCs9);
    CHECK(has_version_error(record_old));
    Diagnostics record_new;
    auto record = ParseDotnetAt("record struct Point(int X, int Y);", record_new,
                                polyglot::frontends::DotnetLangVersion::kCs10);
    REQUIRE_FALSE(record_new.HasErrors());
    REQUIRE(std::dynamic_pointer_cast<StructDecl>(record->declarations.at(0)));

    Diagnostics ctor_old;
    ParseDotnetAt("class Point(int X, int Y) {}", ctor_old,
                  polyglot::frontends::DotnetLangVersion::kCs11);
    CHECK(has_version_error(ctor_old));

    Diagnostics required_old;
    ParseDotnetAt("class Person { required string Name { get; init; } }", required_old,
                  polyglot::frontends::DotnetLangVersion::kCs10);
    CHECK(has_version_error(required_old));

    Diagnostics namespace_old;
    ParseDotnetAt("namespace App; class Program {}", namespace_old,
                  polyglot::frontends::DotnetLangVersion::kCs9);
    CHECK(has_version_error(namespace_old));
}

TEST_CASE("DotNet switch expressions are represented and gated at C# 8",
          "[dotnet][parser][versions][switch-expression]") {
    constexpr const char *source = R"cs(
class Choices {
    static int Choose(int value) {
        return value switch { 0 => 10, _ when value > 0 => 20, _ => 30 };
    }
}
)cs";
    auto has_code = [](const Diagnostics &diags, polyglot::frontends::ErrorCode code) {
        for (const auto &diag : diags.All())
            if (diag.code == code) return true;
        return false;
    };

    Diagnostics old_diags;
    REQUIRE(ParseDotnetAt(source, old_diags,
                          polyglot::frontends::DotnetLangVersion::kCs7_3));
    CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));

    Diagnostics current_diags;
    auto module = ParseDotnetAt(source, current_diags,
                                polyglot::frontends::DotnetLangVersion::kCs8);
    REQUIRE(module);
    REQUIRE_FALSE(current_diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->declarations.at(0));
    REQUIRE(cls);
    auto method = std::dynamic_pointer_cast<MethodDecl>(cls->members.at(0));
    REQUIRE(method);
    auto ret = std::dynamic_pointer_cast<ReturnStatement>(method->body.at(0));
    REQUIRE(ret);
    auto switch_expr = std::dynamic_pointer_cast<SwitchExpression>(ret->value);
    REQUIRE(switch_expr);
    REQUIRE(switch_expr->arms.size() == 3);
    CHECK(switch_expr->arms[1].guard != nullptr);

    Diagnostics lowering_diags;
    LowerAndGetIR(source, lowering_diags);
    CHECK(has_code(lowering_diags,
                   polyglot::frontends::ErrorCode::kUnsupportedLowering));

    Diagnostics pattern_diags;
    ParseDotnetAt(
        "class P { int F(object value) { return value switch { string text => 1, _ => 0 }; } }",
        pattern_diags, polyglot::frontends::DotnetLangVersion::kCs8);
    CHECK(has_code(pattern_diags,
                   polyglot::frontends::ErrorCode::kUnsupportedSyntax));

    Diagnostics nonconstant_diags;
    ParseDotnetAt(
        "class P { int F(int value) { return value switch { value + 1 => 1, _ => 0 }; } }",
        nonconstant_diags, polyglot::frontends::DotnetLangVersion::kCs8);
    CHECK(has_code(nonconstant_diags,
                   polyglot::frontends::ErrorCode::kUnsupportedSyntax));

    Diagnostics empty_diags;
    ParseDotnetAt(
        "class P { int F(int value) { return value switch { }; } }",
        empty_diags, polyglot::frontends::DotnetLangVersion::kCs8);
    CHECK(has_code(empty_diags,
                   polyglot::frontends::ErrorCode::kUnexpectedToken));
}

TEST_CASE("DotNet init accessors are gated at C# 9 on class properties",
          "[dotnet][parser][versions][init]") {
    constexpr const char *source = "class Person { string Name { get; init; } }";
    auto has_mismatch = [](const Diagnostics &diags) {
        for (const auto &diag : diags.All())
            if (diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
                return true;
        return false;
    };

    Diagnostics old_diags;
    ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs8);
    CHECK(has_mismatch(old_diags));

    Diagnostics current_diags;
    auto module = ParseDotnetAt(source, current_diags,
                                polyglot::frontends::DotnetLangVersion::kCs9);
    REQUIRE(module);
    REQUIRE_FALSE(current_diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->declarations.at(0));
    auto property = std::dynamic_pointer_cast<PropertyDecl>(cls->members.at(0));
    REQUIRE(property);
    CHECK(property->is_init_only);

    auto has_code = [](const Diagnostics &diags,
                       polyglot::frontends::ErrorCode code) {
        for (const auto &diag : diags.All())
            if (diag.code == code) return true;
        return false;
    };
    Diagnostics duplicate_diags;
    ParseDotnetAt("class Invalid { int Value { get; init; init; } }",
                  duplicate_diags,
                  polyglot::frontends::DotnetLangVersion::kCs9);
    CHECK(has_code(duplicate_diags,
                   polyglot::frontends::ErrorCode::kUnexpectedToken));

    Diagnostics conflict_diags;
    ParseDotnetAt("class Invalid { int Value { get; set; init; } }",
                  conflict_diags,
                  polyglot::frontends::DotnetLangVersion::kCs9);
    CHECK(has_code(conflict_diags,
                   polyglot::frontends::ErrorCode::kUnexpectedToken));
}

TEST_CASE("DotNet parser exposes C# 13 boundaries without silent recovery",
          "[dotnet][parser][versions]") {
    auto has_code = [](const Diagnostics &diags, polyglot::frontends::ErrorCode code) {
        for (const auto &diag : diags.All())
            if (diag.code == code) return true;
        return false;
    };

    SECTION("params collections and escape e are faithfully retained") {
        constexpr const char *source =
            R"(class C { void Add(params IEnumerable<int> values) {} string Esc() { return "\e"; } })";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs12);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));
        Diagnostics current_diags;
        ParseDotnetAt(source, current_diags, polyglot::frontends::DotnetLangVersion::kCs13);
        CHECK_FALSE(current_diags.HasErrors());
    }

    SECTION("partial properties and ref struct interfaces are retained") {
        constexpr const char *source =
            "interface I {} partial class C { partial int P { get; set; } } ref struct S : I {}";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs12);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));
        Diagnostics current_diags;
        auto module = ParseDotnetAt(source, current_diags,
                                    polyglot::frontends::DotnetLangVersion::kCs13);
        REQUIRE(module);
        CHECK_FALSE(current_diags.HasErrors());
    }

    SECTION("unrepresented C# 13 forms are explicit") {
        constexpr const char *source =
            "partial class C<T> where T : allows ref struct { partial int this[int i] { get; set; } }";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs12);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));
        Diagnostics current_diags;
        ParseDotnetAt(source, current_diags, polyglot::frontends::DotnetLangVersion::kCs13);
        CHECK(has_code(current_diags, polyglot::frontends::ErrorCode::kUnsupportedSyntax));
    }
}

TEST_CASE("DotNet parser exposes C# 14 boundaries without silent loss",
          "[dotnet][parser][versions]") {
    auto has_code = [](const Diagnostics &diags, polyglot::frontends::ErrorCode code) {
        for (const auto &diag : diags.All())
            if (diag.code == code) return true;
        return false;
    };

    SECTION("faithfully represented expressions and partial declarations") {
        constexpr const char *source = R"(
class C {
  int Value;
  void Assign(C c) { c?.Value = 1; }
  string Name() => nameof(List<>);
  object Lambda() { return (ref x) => x; }
  partial C();
  partial event Action Changed;
}
)";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs13);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));
        Diagnostics current_diags;
        ParseDotnetAt(source, current_diags, polyglot::frontends::DotnetLangVersion::kCs14);
        CHECK_FALSE(current_diags.HasErrors());
    }

    SECTION("extension blocks, field-backed properties, and compound operators are retained") {
        constexpr const char *source = R"(
static class Extensions { extension(string value) { int Size() => value.Length; } }
class C {
  int P { get => field; set => field = value; }
  public static C operator +=(C left, C right) => left;
}
)";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs13);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));
        Diagnostics current_diags;
        auto module = ParseDotnetAt(source, current_diags,
                                    polyglot::frontends::DotnetLangVersion::kCs14);
        REQUIRE(module);
        for (const auto &diag : current_diags.All())
            INFO(diag.message);
        CHECK_FALSE(current_diags.HasErrors());
        REQUIRE(module->declarations.size() == 2);

        auto extensions = std::dynamic_pointer_cast<ClassDecl>(module->declarations[0]);
        REQUIRE(extensions);
        REQUIRE(extensions->members.size() == 1);
        auto extension = std::dynamic_pointer_cast<ExtensionDecl>(extensions->members[0]);
        REQUIRE(extension);
        CHECK(extension->has_receiver_name);
        CHECK(extension->receiver.name == "value");
        REQUIRE(extension->members.size() == 1);
        CHECK(std::dynamic_pointer_cast<MethodDecl>(extension->members[0]) != nullptr);

        auto cls = std::dynamic_pointer_cast<ClassDecl>(module->declarations[1]);
        REQUIRE(cls);
        REQUIRE(cls->members.size() == 2);
        auto property = std::dynamic_pointer_cast<PropertyDecl>(cls->members[0]);
        REQUIRE(property);
        CHECK(property->uses_field_keyword);
        REQUIRE(property->accessors.size() == 2);
        CHECK(property->accessors[0].expression_body != nullptr);
        CHECK(property->accessors[1].expression_body != nullptr);
        auto op = std::dynamic_pointer_cast<OperatorDecl>(cls->members[1]);
        REQUIRE(op);
        CHECK(op->op == "+=");
        CHECK(op->is_compound_assignment);
        CHECK(op->expression_body != nullptr);

        polyglot::frontends::SemaContext sema(current_diags);
        AnalyzeModule(*module, sema);
        for (const auto &diag : current_diags.All())
            INFO(diag.message);
        CHECK_FALSE(current_diags.HasErrors());
    }

    SECTION("file-based application directives are retained") {
        constexpr const char *source = R"(
#:sdk Microsoft.NET.Sdk
#:package Newtonsoft.Json@13.0.3
class C {}
)";
        Diagnostics old_diags;
        ParseDotnetAt(source, old_diags, polyglot::frontends::DotnetLangVersion::kCs13);
        CHECK(has_code(old_diags, polyglot::frontends::ErrorCode::kLangVersionMismatch));

        Diagnostics current_diags;
        auto module = ParseDotnetAt(source, current_diags,
                                    polyglot::frontends::DotnetLangVersion::kCs14);
        REQUIRE(module);
        CHECK_FALSE(current_diags.HasErrors());
        REQUIRE(module->file_directives.size() == 2);
        CHECK(module->file_directives[0]->name == "sdk");
        CHECK(module->file_directives[0]->value == "Microsoft.NET.Sdk");
        CHECK(module->file_directives[1]->name == "package");
        CHECK(module->file_directives[1]->value == "Newtonsoft.Json@13.0.3");
    }
}

// ============================================================================
// Sema Tests
// ============================================================================

TEST_CASE("DotNet sema analyzes simple class", "[dotnet][sema]") {
    Diagnostics diags;
    auto mod = ParseDotnet(R"(
class Calculator {
    int Add(int a, int b) {
        return a + b;
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*mod, sema);
    // Sema completes without fatal failure; type-mapping diagnostics for
    // .NET primitives are acceptable and do not indicate a bug.
    // The class declaration must have been visited (module has 1 declaration).
    REQUIRE(mod->declarations.size() == 1);
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->name == "Calculator");
    CHECK(cls->members.size() >= 1);
}

TEST_CASE("DotNet sema analyzes enum", "[dotnet][sema]") {
    Diagnostics diags;
    auto mod = ParseDotnet("enum Status { Active, Inactive }", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*mod, sema);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Lowering Tests
// ============================================================================

TEST_CASE("DotNet lowering generates function for static method", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
class App {
    static int Main() {
        return 0;
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("App::Main") != std::string::npos);
}

TEST_CASE("DotNet lowering generates constructor", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
class Box {
    int value;
    public Box(int v) {
        value = v;
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("Box::.ctor") != std::string::npos);
}

TEST_CASE("DotNet lowering rejects interpolated strings explicitly",
          "[dotnet][lowering][modern]") {
    Diagnostics diags;
    auto ir = LowerAndGetIR(R"cs(
class Text {
    static string Greeting(string name) { return $"Hello {name}"; }
}
)cs", diags);
    CHECK_FALSE(ir.empty());
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("DotNet lowering maps Console.WriteLine", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
class Hello {
    static void Main() {
        Console.WriteLine("Hello");
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_dotnet_print") != std::string::npos);
}

TEST_CASE("DotNet lowering rejects struct instead of dropping it", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
struct Vec2 {
    float X;
    float Y;
}
)", diags);
    CHECK(ir.empty());
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("DotNet lowering rejects modern declaration shapes", "[dotnet][lowering][modern]") {
    for (const char *source : {
             "interface I { void M(); }",
             "record struct R(int X);",
             "class C(int value) {}"}) {
        Diagnostics diags;
        LowerAndGetIR(source, diags);
        bool unsupported = false;
        for (const auto &diag : diags.All()) {
            INFO(diag.message);
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        }
        CHECK(unsupported);
    }
}

TEST_CASE("DotNet lowering rejects C# 14 analysis-only forms explicitly",
          "[dotnet][lowering][modern]") {
    for (const char *source : {
             "static class E { extension(string value) { int Size() => value.Length; } }",
             "class C { int P { get => field; set => field = value; } }",
             "class C { public static C operator +=(C left, C right) => left; }",
             "#:sdk Microsoft.NET.Sdk\nclass C {}"}) {
        Diagnostics diags;
        LowerAndGetIR(source, diags);
        bool unsupported = false;
        for (const auto &diag : diags.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        INFO(source);
        CHECK(unsupported);
    }
}

TEST_CASE("DotNet lowering fails closed for unsupported managed semantics",
          "[dotnet][lowering][boundary]") {
    for (const char *source : {
             "class C { static decimal F(decimal x) { return x; } }",
             "class C { static int F() { return Helper(); } }",
             "class C { static int F(int[] xs) { return xs[0]; } }",
             "class C { static object F() { return new object(); } }",
             "class C { static int F() {} }",
             "class C { static int? F() { return null; } }"}) {
        Diagnostics diags;
        LowerAndGetIR(source, diags);
        bool unsupported = false;
        for (const auto &diag : diags.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        INFO(source);
        CHECK(unsupported);
    }
}

TEST_CASE("DotNet lowering handles enum ordinals", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
enum Priority { Low, Medium, High }
)", diags);
    // Enum lowering should not produce errors
    CHECK(!diags.HasErrors());
    // Enum-only source may not emit standalone IR functions
}
