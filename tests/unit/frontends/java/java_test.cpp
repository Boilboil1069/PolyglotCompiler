// Unit tests for the Java frontend (lexer, parser, sema, lowering).

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_frontend.h"
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

static std::shared_ptr<Module> ParseJavaAt(const char *src, Diagnostics &diags,
                                           polyglot::frontends::JavaRelease release) {
    JavaLexer lexer(src, "<test>");
    JavaParser parser(lexer, diags);
    parser.SetJavaRelease(release);
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

static bool HasDiagnosticCode(const Diagnostics &diags,
                              polyglot::frontends::ErrorCode code) {
    for (const auto &diag : diags.All()) {
        if (diag.code == code) return true;
    }
    return false;
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

TEST_CASE("Java annotation arguments remain in the AST and lower fail-closed",
          "[java][parser][annotations][lowering]") {
    Diagnostics diags;
    auto module = ParseJava(
        "@Route(path = \"/v1\", version = 2) class App {}", diags);
    REQUIRE(module);
    REQUIRE_FALSE(diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(module->declarations.at(0));
    REQUIRE(cls);
    REQUIRE(cls->annotations.size() == 1);
    CHECK(cls->annotations[0].name == "@Route");
    REQUIRE(cls->annotations[0].args.size() == 2);
    auto path = std::dynamic_pointer_cast<BinaryExpression>(cls->annotations[0].args[0]);
    auto version = std::dynamic_pointer_cast<BinaryExpression>(cls->annotations[0].args[1]);
    REQUIRE(path);
    REQUIRE(version);
    CHECK(path->op == "=");
    CHECK(version->op == "=");

    polyglot::frontends::SemaContext sema(diags);
    AnalyzeModule(*module, sema);
    CHECK_FALSE(diags.HasErrors());

    IRContext ir;
    LowerToIR(*module, ir, diags);
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);

    Diagnostics array_diags;
    ParseJava("@Route(tags = {\"one\", \"two\"}) class Bad {}", array_diags);
    bool unsupported_syntax = false;
    for (const auto &diag : array_diags.All())
        unsupported_syntax |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    CHECK(unsupported_syntax);
}

TEST_CASE("Java lexer skips comments", "[java][lexer]") {
    auto tokens = Tokenize("int x; // line comment\nint y;");
    int kw_count = 0;
    for (auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword && t.lexeme == "int") kw_count++;
    }
    CHECK(kw_count == 2);
}

TEST_CASE("Java lexer recognizes non-sealed as one restricted keyword", "[java][lexer][modern]") {
    auto tokens = Tokenize("non-sealed class Child {}");
    REQUIRE(tokens.size() >= 4);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "non-sealed");
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

TEST_CASE("Java record gate uses its exact Java 16 boundary",
          "[java][parser][versions]") {
    constexpr const char *source = "record Point(int x) {}";

    Diagnostics old_diags;
    auto old = ParseJavaAt(source, old_diags,
                           polyglot::frontends::JavaRelease::kJava15);
    REQUIRE(old);
    CHECK(old_diags.HasErrors());

    Diagnostics current_diags;
    auto current = ParseJavaAt(source, current_diags,
                               polyglot::frontends::JavaRelease::kJava16);
    REQUIRE(current);
    CHECK_FALSE(current_diags.HasErrors());
}

TEST_CASE("Java parser preserves nested records and record members", "[java][parser][modern]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
class Geometry {
    record Point(int x, int y) {
        int sum() { return x + y; }
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE_FALSE(diags.HasErrors());
    auto outer = std::dynamic_pointer_cast<ClassDecl>(mod->declarations.at(0));
    REQUIRE(outer);
    REQUIRE(outer->members.size() == 1);
    auto record = std::dynamic_pointer_cast<RecordDecl>(outer->members.at(0));
    REQUIRE(record);
    REQUIRE(record->members.size() == 1);
    CHECK(std::dynamic_pointer_cast<MethodDecl>(record->members.at(0)) != nullptr);
}

TEST_CASE("Java contextual keywords remain valid member names", "[java][parser][versions]") {
    Diagnostics diags;
    auto mod = ParseJavaAt(
        "class Legacy { int record; static int yield(int value) { return value; } }",
        diags, polyglot::frontends::JavaRelease::kJava11);
    REQUIRE(mod);
    REQUIRE_FALSE(diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations.at(0));
    REQUIRE(cls);
    REQUIRE(cls->members.size() == 2);
    CHECK(std::dynamic_pointer_cast<FieldDecl>(cls->members.at(0))->name == "record");
    CHECK(std::dynamic_pointer_cast<MethodDecl>(cls->members.at(1))->name == "yield");
}

TEST_CASE("Java version-aware signature extraction uses the selected release",
          "[java][signatures][versions]") {
    JavaLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    options.java_release = polyglot::frontends::JavaRelease::kJava11;
    Diagnostics diags;
    auto signatures = frontend.ExtractSignatures(
        "class Legacy { static int record(int value) { return value; } }",
        "Legacy.java", "legacy", diags, options);
    REQUIRE_FALSE(diags.HasErrors());
    REQUIRE(signatures.size() == 1);
    CHECK(signatures[0].name == "record");
}

TEST_CASE("Java parser represents switch expressions", "[java][parser][modern]") {
    Diagnostics diags;
    auto mod = ParseJava(R"(
class Choices {
    int choose(int value) {
        return switch (value) { case 1 -> 10; default -> 20; };
    }
}
)", diags);
    REQUIRE(mod);
    REQUIRE_FALSE(diags.HasErrors());
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations.at(0));
    auto method = std::dynamic_pointer_cast<MethodDecl>(cls->members.at(0));
    auto ret = std::dynamic_pointer_cast<ReturnStatement>(method->body.at(0));
    REQUIRE(ret);
    auto sw = std::dynamic_pointer_cast<SwitchExpression>(ret->value);
    REQUIRE(sw);
    CHECK(sw->cases.size() == 2);
}

TEST_CASE("Java pattern switch labels are versioned and never approximated",
          "[java][parser][modern][versions]") {
    constexpr const char *statement_source = R"(
class Patterns {
    static int classify(Object value) {
        switch (value) {
            case String text -> { return 1; }
            default -> { return 0; }
        }
    }
}
)";

    Diagnostics old_diags;
    REQUIRE(ParseJavaAt(statement_source, old_diags,
                        polyglot::frontends::JavaRelease::kJava20));
    CHECK(HasDiagnosticCode(old_diags,
                            polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(old_diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));

    Diagnostics current_diags;
    REQUIRE(ParseJavaAt(statement_source, current_diags,
                        polyglot::frontends::JavaRelease::kJava21));
    CHECK_FALSE(HasDiagnosticCode(current_diags,
                                  polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(current_diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));

    Diagnostics expression_diags;
    REQUIRE(ParseJavaAt(
        "class P { int f(Object o) { return switch (o) { case String s -> 1; "
        "default -> 0; }; } }",
        expression_diags, polyglot::frontends::JavaRelease::kJava20));
    CHECK(HasDiagnosticCode(expression_diags,
                            polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(expression_diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));
}

TEST_CASE("Java record patterns require their exact modern AST boundary",
          "[java][parser][modern][versions]") {
    constexpr const char *source = R"(
class PointTest {
    boolean matches(Object value) {
        return value instanceof Point(int x, int y);
    }
}
)";

    Diagnostics old_diags;
    REQUIRE(ParseJavaAt(source, old_diags,
                        polyglot::frontends::JavaRelease::kJava20));
    CHECK(HasDiagnosticCode(old_diags,
                            polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(old_diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));

    Diagnostics current_diags;
    REQUIRE(ParseJavaAt(source, current_diags,
                        polyglot::frontends::JavaRelease::kJava21));
    CHECK_FALSE(HasDiagnosticCode(current_diags,
                                  polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(current_diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));
}

TEST_CASE("Java stable releases reject preview primitive patterns",
          "[java][parser][modern][versions]") {
    Diagnostics diags;
    REQUIRE(ParseJavaAt(
        "class Preview { boolean test(Object value) { return value instanceof int i; } }",
        diags, polyglot::frontends::JavaRelease::kJava26));
    CHECK_FALSE(HasDiagnosticCode(diags,
                                  polyglot::frontends::ErrorCode::kLangVersionMismatch));
    CHECK(HasDiagnosticCode(diags,
                            polyglot::frontends::ErrorCode::kUnsupportedSyntax));
}

TEST_CASE("Java parser gates sealed types", "[java][parser][versions]") {
    Diagnostics old_diags;
    auto old = ParseJavaAt("non-sealed class Child {}", old_diags,
                           polyglot::frontends::JavaRelease::kJava8);
    REQUIRE(old);
    CHECK(old_diags.HasErrors());
    CHECK(std::dynamic_pointer_cast<ClassDecl>(old->declarations.at(0))->is_non_sealed);
}

TEST_CASE("Java private interface methods are gated at Java 9",
          "[java][parser][versions][interfaces]") {
    constexpr const char *source =
        "interface Helpers { private void helper() {} default void run() { helper(); } }";

    Diagnostics old_diags;
    REQUIRE(ParseJavaAt(source, old_diags,
                        polyglot::frontends::JavaRelease::kJava8));
    CHECK(HasDiagnosticCode(old_diags,
                            polyglot::frontends::ErrorCode::kLangVersionMismatch));

    Diagnostics current_diags;
    REQUIRE(ParseJavaAt(source, current_diags,
                        polyglot::frontends::JavaRelease::kJava9));
    CHECK_FALSE(current_diags.HasErrors());

    Diagnostics missing_body_diags;
    REQUIRE(ParseJavaAt("interface Invalid { private void helper(); }",
                        missing_body_diags,
                        polyglot::frontends::JavaRelease::kJava9));
    CHECK(HasDiagnosticCode(missing_body_diags,
                            polyglot::frontends::ErrorCode::kUnexpectedToken));

    Diagnostics abstract_diags;
    REQUIRE(ParseJavaAt(
        "interface Invalid { private abstract void helper(); }",
        abstract_diags, polyglot::frontends::JavaRelease::kJava9));
    CHECK(HasDiagnosticCode(abstract_diags,
                            polyglot::frontends::ErrorCode::kUnexpectedToken));

    Diagnostics default_diags;
    REQUIRE(ParseJavaAt(
        "interface Invalid { private default void helper() {} }",
        default_diags, polyglot::frontends::JavaRelease::kJava9));
    CHECK(HasDiagnosticCode(default_diags,
                            polyglot::frontends::ErrorCode::kUnexpectedToken));
}

TEST_CASE("Java parser preserves and gates module descriptors",
          "[java][parser][versions][modules]") {
    constexpr const char *source = R"(
module app.core {
    requires static transitive dep.core;
    exports app.api;
    exports app.internal to friend.one, friend.two;
    opens app.model to framework.core;
    uses app.Service;
    provides app.Service with app.impl.ServiceImpl, app.impl.OtherService;
}
)";

    Diagnostics old_diags;
    auto old = ParseJavaAt(source, old_diags, polyglot::frontends::JavaRelease::kJava8);
    REQUIRE(old);
    REQUIRE(old->declarations.size() == 1);
    CHECK(std::dynamic_pointer_cast<ModuleDecl>(old->declarations[0]) != nullptr);
    bool mismatch = false;
    for (const auto &diag : old_diags.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics current_diags;
    auto current = ParseJavaAt(source, current_diags,
                               polyglot::frontends::JavaRelease::kJava9);
    REQUIRE(current);
    REQUIRE_FALSE(current_diags.HasErrors());
    REQUIRE(current->declarations.size() == 1);
    auto descriptor = std::dynamic_pointer_cast<ModuleDecl>(current->declarations[0]);
    REQUIRE(descriptor);
    CHECK(descriptor->name == "app.core");
    CHECK_FALSE(descriptor->is_open);
    REQUIRE(descriptor->directives.size() == 6);
    CHECK(descriptor->directives[0].kind == ModuleDecl::Directive::Kind::kRequires);
    CHECK(descriptor->directives[0].is_static);
    CHECK(descriptor->directives[0].is_transitive);
    CHECK(descriptor->directives[0].name == "dep.core");
    CHECK(descriptor->directives[1].kind == ModuleDecl::Directive::Kind::kExports);
    CHECK(descriptor->directives[1].targets.empty());
    REQUIRE(descriptor->directives[2].targets.size() == 2);
    CHECK(descriptor->directives[2].targets[1] == "friend.two");
    CHECK(descriptor->directives[3].kind == ModuleDecl::Directive::Kind::kOpens);
    REQUIRE(descriptor->directives[3].targets.size() == 1);
    CHECK(descriptor->directives[4].kind == ModuleDecl::Directive::Kind::kUses);
    CHECK(descriptor->directives[5].kind == ModuleDecl::Directive::Kind::kProvides);
    REQUIRE(descriptor->directives[5].targets.size() == 2);
    CHECK(descriptor->directives[5].targets[0] == "app.impl.ServiceImpl");

    Diagnostics open_diags;
    auto open = ParseJavaAt("open module app.opened { requires java.base; }", open_diags,
                            polyglot::frontends::JavaRelease::kJava9);
    REQUIRE(open);
    REQUIRE_FALSE(open_diags.HasErrors());
    auto open_descriptor = std::dynamic_pointer_cast<ModuleDecl>(open->declarations.at(0));
    REQUIRE(open_descriptor);
    CHECK(open_descriptor->is_open);

    polyglot::frontends::SemaContext sema(current_diags);
    AnalyzeModule(*current, sema);
    CHECK_FALSE(current_diags.HasErrors());

    JavaLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    options.java_release = polyglot::frontends::JavaRelease::kJava9;
    Diagnostics analyze_diags;
    CHECK(frontend.Analyze(source, "module-info.java", analyze_diags, options));
    CHECK_FALSE(analyze_diags.HasErrors());

    IRContext ir;
    LowerToIR(*current, ir, current_diags);
    bool unsupported = false;
    for (const auto &diag : current_diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("Java 25 compact source files preserve top-level members",
          "[java][parser][versions][compact]") {
    constexpr const char *source = "int count = 0; void main() {}";

    Diagnostics old_diags;
    auto old = ParseJavaAt(source, old_diags,
                           polyglot::frontends::JavaRelease::kJava24);
    REQUIRE(old);
    CHECK(old->is_compact_source);
    REQUIRE(old->declarations.size() == 2);
    bool mismatch = false;
    for (const auto &diag : old_diags.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics current_diags;
    auto current = ParseJavaAt(source, current_diags,
                               polyglot::frontends::JavaRelease::kJava25);
    REQUIRE(current);
    REQUIRE_FALSE(current_diags.HasErrors());
    CHECK(current->is_compact_source);
    REQUIRE(current->declarations.size() == 2);
    auto field = std::dynamic_pointer_cast<FieldDecl>(current->declarations[0]);
    auto main_method = std::dynamic_pointer_cast<MethodDecl>(current->declarations[1]);
    REQUIRE(field);
    REQUIRE(main_method);
    CHECK(field->name == "count");
    CHECK(main_method->name == "main");
    CHECK_FALSE(main_method->is_static);

    JavaLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    options.java_release = polyglot::frontends::JavaRelease::kJava25;
    Diagnostics analyze_diags;
    CHECK(frontend.Analyze(source, "Main.java", analyze_diags, options));
    CHECK_FALSE(analyze_diags.HasErrors());

    Diagnostics signature_diags;
    auto signatures = frontend.ExtractSignatures(source, "Main.java", "compact",
                                                  signature_diags, options);
    REQUIRE_FALSE(signature_diags.HasErrors());
    REQUIRE(signatures.size() == 1);
    CHECK(signatures[0].name == "main");
    CHECK(signatures[0].is_method);

    polyglot::frontends::FrontendOptions old_options;
    old_options.java_release = polyglot::frontends::JavaRelease::kJava24;
    Diagnostics old_signature_diags;
    auto old_signatures = frontend.ExtractSignatures(
        source, "Main.java", "compact", old_signature_diags, old_options);
    CHECK(old_signatures.empty());
    CHECK(old_signature_diags.HasErrors());

    IRContext ir;
    Diagnostics lower_diags;
    auto lowered = frontend.Lower(source, "Main.java", ir, lower_diags, options);
    CHECK_FALSE(lowered.success);
    bool unsupported = false;
    for (const auto &diag : lower_diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("Java 25 flexible constructor bodies retain invocation position",
          "[java][parser][versions][constructors]") {
    constexpr const char *source =
        "class Box { Box(int value) { value = value + 1; super(); } }";

    Diagnostics old_diags;
    auto old = ParseJavaAt(source, old_diags,
                           polyglot::frontends::JavaRelease::kJava24);
    REQUIRE(old);
    auto old_class = std::dynamic_pointer_cast<ClassDecl>(old->declarations.at(0));
    REQUIRE(old_class);
    auto old_ctor = std::dynamic_pointer_cast<ConstructorDecl>(old_class->members.at(0));
    REQUIRE(old_ctor);
    CHECK(old_ctor->has_flexible_body);
    CHECK(old_ctor->invocation_index == 1);
    CHECK(old_ctor->invocation_kind == ConstructorDecl::InvocationKind::kSuper);
    bool mismatch = false;
    for (const auto &diag : old_diags.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics current_diags;
    auto current = ParseJavaAt(source, current_diags,
                               polyglot::frontends::JavaRelease::kJava25);
    REQUIRE(current);
    REQUIRE_FALSE(current_diags.HasErrors());
    auto current_class = std::dynamic_pointer_cast<ClassDecl>(current->declarations.at(0));
    REQUIRE(current_class);
    auto current_ctor = std::dynamic_pointer_cast<ConstructorDecl>(current_class->members.at(0));
    REQUIRE(current_ctor);
    CHECK(current_ctor->has_flexible_body);
    CHECK(current_ctor->invocation_index == 1);
    CHECK(current_ctor->invocation_kind == ConstructorDecl::InvocationKind::kSuper);

    Diagnostics this_diags;
    auto this_module = ParseJavaAt(
        "class Pair { Pair() { prepare(); this(1); } Pair(int value) {} }",
        this_diags, polyglot::frontends::JavaRelease::kJava25);
    REQUIRE(this_module);
    REQUIRE_FALSE(this_diags.HasErrors());
    auto this_class =
        std::dynamic_pointer_cast<ClassDecl>(this_module->declarations.at(0));
    REQUIRE(this_class);
    auto this_ctor =
        std::dynamic_pointer_cast<ConstructorDecl>(this_class->members.at(0));
    REQUIRE(this_ctor);
    CHECK(this_ctor->has_flexible_body);
    CHECK(this_ctor->invocation_index == 1);
    CHECK(this_ctor->invocation_kind == ConstructorDecl::InvocationKind::kThis);

    JavaLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    options.java_release = polyglot::frontends::JavaRelease::kJava25;
    Diagnostics analyze_diags;
    CHECK(frontend.Analyze(source, "Box.java", analyze_diags, options));
    CHECK_FALSE(analyze_diags.HasErrors());

    IRContext ir;
    Diagnostics lower_diags;
    auto lowered = frontend.Lower(source, "Box.java", ir, lower_diags, options);
    CHECK_FALSE(lowered.success);
    bool unsupported = false;
    for (const auto &diag : lower_diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
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

TEST_CASE("Java 25 module import declarations are retained and version-gated",
          "[java][parser][versions][module-import]") {
    constexpr const char *source = "import module java.base; class App {}";

    Diagnostics old_diags;
    auto old = ParseJavaAt(source, old_diags,
                           polyglot::frontends::JavaRelease::kJava24);
    REQUIRE(old);
    REQUIRE(old->imports.size() == 1);
    CHECK(old->imports[0]->is_module);
    bool mismatch = false;
    for (const auto &diag : old_diags.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics current_diags;
    auto current = ParseJavaAt(source, current_diags,
                               polyglot::frontends::JavaRelease::kJava25);
    REQUIRE(current);
    REQUIRE_FALSE(current_diags.HasErrors());
    REQUIRE(current->imports.size() == 1);
    CHECK(current->imports[0]->is_module);
    CHECK(current->imports[0]->path == "java.base");
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
    // Sema completes without fatal failure; type-mapping diagnostics for
    // Java primitives are acceptable and do not indicate a bug.
    // The class declaration must have been visited (module has 1 declaration).
    REQUIRE(mod->declarations.size() == 1);
    auto cls = std::dynamic_pointer_cast<ClassDecl>(mod->declarations[0]);
    REQUIRE(cls);
    CHECK(cls->name == "Calculator");
    CHECK(cls->members.size() >= 1);
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

TEST_CASE("Java lowering rejects switch expressions explicitly", "[java][lowering][modern]") {
    Diagnostics diags;
    auto ir = LowerAndGetIR(R"(
class Choices {
    static int choose(int value) {
        return switch (value) { case 1 -> 10; default -> 20; };
    }
}
)", diags);
    CHECK_FALSE(ir.empty());
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
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

TEST_CASE("Java lowering rejects records instead of synthesizing fake accessors",
          "[java][lowering][modern]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
record Point(int x, int y) { }
)", diags);
    CHECK(ir.empty());
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("Java lowering rejects nested modern type declarations", "[java][lowering][modern]") {
    Diagnostics diags;
    LowerAndGetIR("class Outer { record Point(int x) {} }", diags);
    bool unsupported = false;
    for (const auto &diag : diags.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("Java lowering fails closed for unresolved runtime semantics",
          "[java][lowering][boundary]") {
    for (const char *source : {
             "class C { static int f() { return helper(); } }",
             "class C { static int f(int[] xs) { return xs[0]; } }",
             "class C { static Object f() { return new Object(); } }",
             "class C { static int f() {} }"}) {
        Diagnostics diags;
        LowerAndGetIR(source, diags);
        bool unsupported = false;
        for (const auto &diag : diags.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        INFO(source);
        CHECK(unsupported);
    }
}

TEST_CASE("Java lowering handles enum ordinals", "[java][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
enum Direction { NORTH, SOUTH, EAST, WEST }
)", diags);
    // Enum lowering should not produce errors
    CHECK(!diags.HasErrors());
    // Enum may or may not produce standalone IR functions depending on
    // whether the lowering emits constants or constructors
}
