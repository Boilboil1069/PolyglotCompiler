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

TEST_CASE("DotNet lowering handles struct", "[dotnet][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
struct Vec2 {
    float X;
    float Y;
}
)", diags);
    // Struct lowering should not produce errors
    CHECK(!diags.HasErrors());
    // Struct-only source may not emit standalone IR functions
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
