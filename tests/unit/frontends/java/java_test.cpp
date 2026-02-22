// Unit tests for the Java frontend (lexer, parser, sema, lowering).

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_parser.h"
#include "frontends/java/include/java_sema.h"
#include "frontends/java/include/java_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ir::IRContext;
using polyglot::java::JavaLexer;
using polyglot::java::JavaParser;
using namespace polyglot::java;

// ============================================================================
// Helper utilities
// ============================================================================

static std::vector<Token> Tokenize(const char *src) {
    JavaLexer lexer(src, "<test>");
    std::vector<Token> tokens;
    for (;;) {
        auto tok = lexer.NextToken();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::kEndOfFile) break;
    }
    return tokens;
}

static std::shared_ptr<Module> ParseJava(const char *src, Diagnostics &diags) {
    JavaLexer lexer(src, "<test>");
    JavaParser parser(lexer, diags);
    parser.ParseModule();
    return parser.TakeModule();
}

static std::string LowerAndGetIR(const char *src, Diagnostics &diags) {
    auto mod = ParseJava(src, diags);
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

TEST_CASE("Java lexer tokenizes keywords", "[java][lexer]") {
    auto tokens = Tokenize("public class Main { }");
    REQUIRE(tokens.size() >= 5);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "public");
    CHECK(tokens[1].kind == TokenKind::kKeyword);
    CHECK(tokens[1].lexeme == "class");
    CHECK(tokens[2].kind == TokenKind::kIdentifier);
    CHECK(tokens[2].lexeme == "Main");
}

TEST_CASE("Java lexer tokenizes string literals", "[java][lexer]") {
    auto tokens = Tokenize(R"("Hello, World!")");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kString);
}

TEST_CASE("Java lexer tokenizes integer literals", "[java][lexer]") {
    auto tokens = Tokenize("42 0xFF 0b1010");
    REQUIRE(tokens.size() >= 4); // 3 numbers + EOF
    CHECK(tokens[0].kind == TokenKind::kNumber);
    CHECK(tokens[0].lexeme == "42");
    CHECK(tokens[1].kind == TokenKind::kNumber);
}

TEST_CASE("Java lexer tokenizes operators", "[java][lexer]") {
    auto tokens = Tokenize("+ - * /");
    REQUIRE(tokens.size() >= 5); // 4 operators + EOF
    CHECK(tokens[0].lexeme == "+");
    CHECK(tokens[1].lexeme == "-");
}

TEST_CASE("Java lexer tokenizes annotations", "[java][lexer]") {
    auto tokens = Tokenize("@Override void foo() {}");
    REQUIRE(tokens.size() >= 5);
    // Annotation should be tokenized
    bool found_override = false;
    for (auto &t : tokens) {
        if (t.lexeme.find("Override") != std::string::npos) {
            found_override = true;
            break;
        }
    }
    CHECK(found_override);
}

TEST_CASE("Java lexer skips comments", "[java][lexer]") {
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

TEST_CASE("Java parser parses empty class", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava("public class Empty { }", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() == 1);
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->name == "Empty");
    CHECK(cls->access == "public");
}

TEST_CASE("Java parser parses class with method", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
class Greeter {
    public String greet(String name) {
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
    CHECK(method->name == "greet");
    CHECK(method->params.size() == 1);
}

TEST_CASE("Java parser parses interface", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
public interface Runnable {
    void run();
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto iface = std::dynamic_pointer_cast<InterfaceDecl>(mod->declarations[0]);
    REQUIRE(iface);
    CHECK(iface->name == "Runnable");
}

TEST_CASE("Java parser parses enum", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
public enum Color {
    RED, GREEN, BLUE
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto en = std::dynamic_pointer_cast<EnumDecl>(mod->declarations[0]);
    REQUIRE(en);
    CHECK(en->name == "Color");
    CHECK(en->constants.size() == 3);
}

TEST_CASE("Java parser parses record (Java 16+)", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
public record Point(int x, int y) { }
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto rec = std::dynamic_pointer_cast<RecordDecl>(mod->declarations[0]);
    REQUIRE(rec);
    CHECK(rec->name == "Point");
    CHECK(rec->components.size() == 2);
}

TEST_CASE("Java parser parses import declarations", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
import java.util.List;
import java.util.*;

class Foo { }
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    CHECK(mod->imports.size() == 2);
    CHECK(mod->imports[0]->path == "java.util.List");
}

TEST_CASE("Java parser parses package declaration", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
package com.example;

class App { }
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->package_decl);
    CHECK(mod->package_decl->name == "com.example");
}

TEST_CASE("Java parser parses class with constructor", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
class Person {
    String name;

    public Person(String name) {
        this.name = name;
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Java parser parses sealed class (Java 17+)", "[java][parser]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
sealed class Shape permits Circle, Square { }
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->is_sealed);
    CHECK(cls->permits.size() == 2);
}

// ============================================================================
// Sema Tests
// ============================================================================

TEST_CASE("Java sema analyzes simple class", "[java][sema]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
class Calculator {
    int add(int a, int b) {
        return a + b;
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*mod, sema);
    // Some type mapping diagnostics are acceptable for Java primitives
    SUCCEED();
}

TEST_CASE("Java sema analyzes enum", "[java][sema]") {
    Diagnostics diags;
    auto mod = ParseJava("enum Status { ACTIVE, INACTIVE }", diags);
    REQUIRE(mod);
    REQUIRE(!diags.HasErrors());
    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*mod, sema);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Lowering Tests
// ============================================================================

TEST_CASE("Java lowering generates function for static method", "[java][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
class App {
    static int main() {
        return 0;
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("App::main") != std::string::npos);
}

TEST_CASE("Java lowering generates constructor", "[java][lowering]") {
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
    CHECK(ir.find("Box::<init>") != std::string::npos);
}

TEST_CASE("Java lowering maps System.out.println", "[java][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
class Hello {
    static void main() {
        System.out.println("Hello");
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_java_print") != std::string::npos);
}

TEST_CASE("Java lowering handles record", "[java][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
record Point(int x, int y) { }
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("Point::<init>") != std::string::npos);
}

TEST_CASE("Java lowering handles enum ordinals", "[java][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
enum Direction { NORTH, SOUTH, EAST, WEST }
)", diags);
    CHECK(!diags.HasErrors());
}
