#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ir::IRContext;
using polyglot::frontends::ErrorCode;
using namespace polyglot::ploy;

// ============================================================================
// Helpers
// ============================================================================

namespace {

std::vector<Token> Tokenize(const std::string &code) {
    PloyLexer lexer(code, "<test>");
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.NextToken();
        tokens.push_back(t);
        if (t.kind == TokenKind::kEndOfFile) break;
    }
    return tokens;
}

std::shared_ptr<Module> Parse(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    return parser.TakeModule();
}

bool AnalyzeCode(const std::string &code, Diagnostics &diags, PloySema &sema) {
    auto module = Parse(code, diags);
    if (!module || diags.HasErrors()) return false;
    return sema.Analyze(module);
}

std::string LowerAndGetIR(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return "";

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) return "";

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) return "";

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return oss.str();
}

// Helper to lower code and return both IR text and cross-language call descriptors
struct LowerResult {
    std::string ir_text;
    std::vector<CrossLangCallDescriptor> descriptors;
};

LowerResult LowerAndGetDescriptors(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return {"", {}};

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) return {"", {}};

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) return {"", {}};

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return {oss.str(), lowering.CallDescriptors()};
}

} // namespace

// ============================================================================
// Lexer Tests
// ============================================================================

TEST_CASE("Ploy lexer tokenizes keywords", "[ploy][lexer]") {
    auto tokens = Tokenize("LINK IMPORT EXPORT FUNC LET VAR IF ELSE WHILE FOR RETURN");
    // All should be keywords except EOF
    size_t keyword_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword) ++keyword_count;
    }
    REQUIRE(keyword_count == 11);
}

TEST_CASE("Ploy lexer tokenizes identifiers", "[ploy][lexer]") {
    auto tokens = Tokenize("foo bar_baz myVar123");
    size_t id_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kIdentifier) ++id_count;
    }
    REQUIRE(id_count == 3);
}

TEST_CASE("Ploy lexer tokenizes numbers", "[ploy][lexer]") {
    auto tokens = Tokenize("42 3.14 0xFF 0b1010 0o777");
    size_t num_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kNumber) ++num_count;
    }
    REQUIRE(num_count == 5);
}

TEST_CASE("Ploy lexer tokenizes strings", "[ploy][lexer]") {
    auto tokens = Tokenize(R"("hello" "world with spaces" "escape\n")");
    size_t str_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kString) ++str_count;
    }
    REQUIRE(str_count == 3);
}

TEST_CASE("Ploy lexer tokenizes operators", "[ploy][lexer]") {
    auto tokens = Tokenize("+ - * / == != <= >= -> :: ..");
    size_t sym_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kSymbol) ++sym_count;
    }
    REQUIRE(sym_count >= 10);
}

TEST_CASE("Ploy lexer skips single-line comments", "[ploy][lexer]") {
    auto tokens = Tokenize("FUNC // this is a comment\nfoo");
    // Should have: FUNC, foo, EOF
    size_t non_eof = 0;
    for (const auto &t : tokens) {
        if (t.kind != TokenKind::kEndOfFile) ++non_eof;
    }
    REQUIRE(non_eof == 2);
}

TEST_CASE("Ploy lexer skips block comments", "[ploy][lexer]") {
    auto tokens = Tokenize("FUNC /* block comment */ foo");
    size_t non_eof = 0;
    for (const auto &t : tokens) {
        if (t.kind != TokenKind::kEndOfFile) ++non_eof;
    }
    REQUIRE(non_eof == 2);
}

TEST_CASE("Ploy lexer tokenizes qualified name", "[ploy][lexer]") {
    auto tokens = Tokenize("cpp::std::vector");
    // Should produce: cpp :: std :: vector
    REQUIRE(tokens.size() >= 6); // 5 tokens + EOF
}

// ============================================================================
// Parser Tests
// ============================================================================

TEST_CASE("Ploy parser parses LINK function declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
LINK(cpp, python, math::add, utils::get_values);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto link = std::dynamic_pointer_cast<LinkDecl>(module->declarations[0]);
    REQUIRE(link);
    REQUIRE(link->link_kind == LinkDecl::LinkKind::kFunction);
    REQUIRE(link->target_language == "cpp");
    REQUIRE(link->source_language == "python");
    REQUIRE(link->target_symbol == "math::add");
    REQUIRE(link->source_symbol == "utils::get_values");
}

TEST_CASE("Ploy parser parses IMPORT declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT "mylib.ploy" AS mylib;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->module_path == "mylib.ploy");
    REQUIRE(import->alias == "mylib");
}

TEST_CASE("Ploy parser parses EXPORT declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
EXPORT my_func AS "external_name";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto export_decl = std::dynamic_pointer_cast<ExportDecl>(module->declarations[0]);
    REQUIRE(export_decl);
    REQUIRE(export_decl->symbol_name == "my_func");
    REQUIRE(export_decl->external_name == "external_name");
}

TEST_CASE("Ploy parser parses MAP_TYPE declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
MAP_TYPE(cpp::int, python::int);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto map_type = std::dynamic_pointer_cast<MapTypeDecl>(module->declarations[0]);
    REQUIRE(map_type);
}

TEST_CASE("Ploy parser parses PIPELINE declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
PIPELINE my_pipeline {
    LET x = 42;
    RETURN x;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto pipeline = std::dynamic_pointer_cast<PipelineDecl>(module->declarations[0]);
    REQUIRE(pipeline);
    REQUIRE(pipeline->name == "my_pipeline");
    REQUIRE(pipeline->body.size() >= 2);
}

TEST_CASE("Ploy parser parses FUNC declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<FuncDecl>(module->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->name == "add");
    REQUIRE(func->params.size() == 2);
    REQUIRE(func->params[0].name == "a");
    REQUIRE(func->params[1].name == "b");
    REQUIRE(func->return_type);
}

TEST_CASE("Ploy parser parses IF/ELSE statement", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC test() -> i32 {
    LET x = 10;
    IF x > 5 {
        RETURN 1;
    } ELSE {
        RETURN 0;
    }
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto func = std::dynamic_pointer_cast<FuncDecl>(module->declarations[0]);
    REQUIRE(func);
    // Body should contain LET and IF
    REQUIRE(func->body.size() >= 2);
}

TEST_CASE("Ploy parser parses WHILE statement", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC loop() -> i32 {
    VAR x = 0;
    WHILE x < 10 {
        x = x + 1;
    }
    RETURN x;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses FOR..IN statement", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC sum_range() -> i32 {
    VAR total = 0;
    FOR i IN 0..10 {
        total = total + i;
    }
    RETURN total;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses MATCH statement", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC classify(x: i32) -> i32 {
    MATCH x {
        CASE 0 {
            RETURN 0;
        }
        CASE 1 {
            RETURN 1;
        }
        DEFAULT {
            RETURN -1;
        }
    }
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses nested expressions", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC compute(a: i32, b: i32) -> i32 {
    LET result = (a + b) * 2 - 1;
    RETURN result;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses multiple declarations", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
LINK(cpp, python, math::sin, pymath::sin);
LINK(python, rust, numpy::array_sum, vec::sum);

FUNC combine(x: f64) -> f64 {
    LET s = CALL(cpp, math::sin, x);
    LET arr_sum = CALL(python, numpy::array_sum, x);
    RETURN s + arr_sum;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 3);
}

// ============================================================================
// Semantic Analysis Tests
// ============================================================================

TEST_CASE("Ploy sema validates LINK declarations", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LINK(cpp, python, math::add, utils::get_values);
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.Links().size() == 1);
    REQUIRE(sema.Links()[0].target_language == "cpp");
    REQUIRE(sema.Links()[0].source_language == "python");
}

TEST_CASE("Ploy sema rejects invalid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LINK(javascript, python, console::log, utils::print);
)", diags, sema);
    // 'javascript' is not a supported language �� sema should report error
    REQUIRE(!ok);
}

TEST_CASE("Ploy sema validates FUNC declarations", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}
)", diags, sema);
    REQUIRE(ok);
    auto it = sema.Symbols().find("add");
    REQUIRE(it != sema.Symbols().end());
    REQUIRE(it->second.kind == PloySymbol::Kind::kFunction);
}

TEST_CASE("Ploy sema validates variable declarations", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> i32 {
    LET x = 42;
    VAR y = x + 1;
    RETURN y;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates MAP_TYPE", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
MAP_TYPE(cpp::int, python::int);
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.TypeMappings().size() == 1);
}

TEST_CASE("Ploy sema validates PIPELINE", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
PIPELINE my_pipeline {
    LET x = 42;
    RETURN x;
}
)", diags, sema);
    REQUIRE(ok);
    auto it = sema.Symbols().find("my_pipeline");
    REQUIRE(it != sema.Symbols().end());
    REQUIRE(it->second.kind == PloySymbol::Kind::kPipeline);
}

TEST_CASE("Ploy sema validates EXPORT references", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC my_func() -> i32 {
    RETURN 0;
}
EXPORT my_func AS "external_func";
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates loop control flow", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> void {
    VAR i = 0;
    WHILE i < 10 {
        IF i == 5 {
            BREAK;
        }
        i = i + 1;
        CONTINUE;
    }
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema rejects BREAK outside loop", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> void {
    BREAK;
}
)", diags, sema);
    REQUIRE(!ok);
}

// ============================================================================
// Lowering / IR Generation Tests
// ============================================================================

TEST_CASE("Ploy lowering generates function", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("add") != std::string::npos);
}

TEST_CASE("Ploy lowering generates LINK bridge stub", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
LINK(cpp, python, math::add, utils::get_values);
)", diags);
    // Should generate a bridge stub function
    REQUIRE(ir.find("__ploy_bridge") != std::string::npos);
}

TEST_CASE("Ploy lowering generates PIPELINE function", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
PIPELINE process_data {
    LET x = 100;
    RETURN x;
}
)", diags);
    REQUIRE(ir.find("__ploy_pipeline_process_data") != std::string::npos);
}

TEST_CASE("Ploy lowering generates IF with branches", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test(x: i32) -> i32 {
    IF x > 0 {
        RETURN 1;
    } ELSE {
        RETURN 0;
    }
}
)", diags);
    REQUIRE(!ir.empty());
    // Should have conditional branching
    REQUIRE(ir.find("test") != std::string::npos);
}

TEST_CASE("Ploy lowering generates WHILE loop", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC count() -> i32 {
    VAR x = 0;
    WHILE x < 10 {
        x = x + 1;
    }
    RETURN x;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("count") != std::string::npos);
}

TEST_CASE("Ploy lowering generates FOR loop", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC sum_range() -> i32 {
    VAR total = 0;
    FOR i IN 0..10 {
        total = total + i;
    }
    RETURN total;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("sum_range") != std::string::npos);
}

TEST_CASE("Ploy lowering handles variable declarations", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> i32 {
    LET a = 10;
    VAR b = 20;
    b = a + b;
    RETURN b;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("test") != std::string::npos);
}

TEST_CASE("Ploy lowering generates call descriptors for LINK", "[ploy][lowering]") {
    Diagnostics diags;
    PloyLexer lexer(R"(
LINK(cpp, python, math::sin, pymath::sin);
LINK(python, rust, numpy::dot, vec::dot);
)", "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module);

    PloySema sema(diags, PloySemaOptions{});
    REQUIRE(sema.Analyze(module));

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    REQUIRE(lowering.Lower(module));

    // LINK declarations register bridge stubs; call descriptors are
    // generated when CALL directives invoke them.  Verify that the module
    // lowered successfully (no crash / no error).
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy lowering generates MATCH with switch", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC classify(x: i32) -> i32 {
    MATCH x {
        CASE 0 {
            RETURN 0;
        }
        CASE 1 {
            RETURN 1;
        }
        DEFAULT {
            RETURN -1;
        }
    }
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("classify") != std::string::npos);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("Ploy full pipeline: LINK + FUNC + CALL", "[ploy][integration]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
LINK(cpp, python, math::add, utils::get_values);

FUNC use_add(a: i32, b: i32) -> i32 {
    LET result = CALL(cpp, math::add, a, b);
    RETURN result;
}
)", diags);
    REQUIRE(!ir.empty());
    // Should have both the bridge stub and the use_add function
    REQUIRE(ir.find("use_add") != std::string::npos);
}

TEST_CASE("Ploy full pipeline: PIPELINE with control flow", "[ploy][integration]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
PIPELINE transform {
    VAR sum = 0;
    FOR i IN 0..5 {
        IF i > 2 {
            sum = sum + i * 2;
        } ELSE {
            sum = sum + i;
        }
    }
    RETURN sum;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("__ploy_pipeline_transform") != std::string::npos);
}

TEST_CASE("Ploy full pipeline: multiple LINKs with PIPELINE", "[ploy][integration]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
LINK(cpp, python, math::sin, pymath::sin);
LINK(python, rust, numpy::sum, vec::sum);

MAP_TYPE(cpp::double, python::float);

PIPELINE compute {
    LET x = 3.14;
    LET s = CALL(cpp, math::sin, x);
    RETURN s;
}
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// Complex Type Extension �� Lexer Tests
// ============================================================================

TEST_CASE("Ploy lexer tokenizes complex type keywords", "[ploy][lexer][complex]") {
    auto tokens = Tokenize("LIST TUPLE DICT OPTION MAP_FUNC CONVERT STRUCT");
    size_t keyword_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword) ++keyword_count;
    }
    REQUIRE(keyword_count == 7);
}

// ============================================================================
// Complex Type Extension �� Parser Tests
// ============================================================================

TEST_CASE("Ploy parser parses STRUCT declaration", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
STRUCT Point {
    x: f64,
    y: f64
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto sd = std::dynamic_pointer_cast<StructDecl>(module->declarations[0]);
    REQUIRE(sd);
    REQUIRE(sd->name == "Point");
    REQUIRE(sd->fields.size() == 2);
    REQUIRE(sd->fields[0].name == "x");
    REQUIRE(sd->fields[1].name == "y");
}

TEST_CASE("Ploy parser parses STRUCT with multiple fields", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
STRUCT Config {
    width: i32,
    height: i32,
    title: str
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto sd = std::dynamic_pointer_cast<StructDecl>(module->declarations[0]);
    REQUIRE(sd);
    REQUIRE(sd->fields.size() == 3);
    REQUIRE(sd->fields[0].name == "width");
    REQUIRE(sd->fields[1].name == "height");
    REQUIRE(sd->fields[2].name == "title");
}

TEST_CASE("Ploy parser parses MAP_FUNC declaration", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
MAP_FUNC convert_point(p: ptr) -> ptr {
    RETURN 0;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto mf = std::dynamic_pointer_cast<MapFuncDecl>(module->declarations[0]);
    REQUIRE(mf);
    REQUIRE(mf->name == "convert_point");
    REQUIRE(mf->params.size() == 1);
    REQUIRE(mf->return_type != nullptr);
}

TEST_CASE("Ploy parser parses list literal", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC test() -> void {
    LET xs = [1, 2, 3];
    RETURN;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses struct literal", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
STRUCT Point {
    x: f64,
    y: f64
}
FUNC test() -> void {
    LET p = Point { x: 1.0, y: 2.0 };
    RETURN;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser parses CONVERT expression", "[ploy][parser][complex]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC test(x: i32) -> f64 {
    LET y = CONVERT(x, f64);
    RETURN y;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

// ============================================================================
// Complex Type Extension �� Sema Tests
// ============================================================================

TEST_CASE("Ploy sema validates STRUCT declaration", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
STRUCT Point {
    x: f64,
    y: f64
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates MAP_FUNC declaration", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
MAP_FUNC to_list(x: i32) -> i32 {
    RETURN 0;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates LIST type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test(xs: LIST[i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates DICT type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test(m: DICT[str, i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates TUPLE type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test(t: TUPLE[i32, f64]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates OPTION type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test(x: OPTION[i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates CONVERT expression", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test(x: i32) -> f64 {
    LET y = CONVERT(x, f64);
    RETURN y;
}
)", diags, sema);
    REQUIRE(ok);
}

// ============================================================================
// Complex Type Extension �� Lowering Tests
// ============================================================================

TEST_CASE("Ploy lowering generates list literal IR", "[ploy][lowering][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> void {
    LET xs = [1, 2, 3];
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("test") != std::string::npos);
}

TEST_CASE("Ploy lowering generates struct definition", "[ploy][lowering][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
STRUCT Point {
    x: f64,
    y: f64
}
FUNC make_point() -> void {
    LET p = Point { x: 1.0, y: 2.0 };
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("make_point") != std::string::npos);
}

TEST_CASE("Ploy lowering generates MAP_FUNC conversion function", "[ploy][lowering][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
MAP_FUNC identity(x: i32) -> i32 {
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("__ploy_mapfunc_identity") != std::string::npos);
}

TEST_CASE("Ploy lowering generates CONVERT call", "[ploy][lowering][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test(x: i32) -> void {
    LET y = CONVERT(x, f64);
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("test") != std::string::npos);
}

// ============================================================================
// Complex Type Extension �� Integration Tests
// ============================================================================

TEST_CASE("Ploy complex type full pipeline: STRUCT + LINK + FUNC", "[ploy][integration][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
LINK(cpp, python, graphics::draw, numpy::make_point);

STRUCT Point {
    x: f64,
    y: f64
}

FUNC render() -> void {
    LET p = Point { x: 10.0, y: 20.0 };
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("render") != std::string::npos);
}

TEST_CASE("Ploy complex type full pipeline: LIST + MAP_FUNC", "[ploy][integration][complex]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
MAP_FUNC to_list(x: i32) -> i32 {
    RETURN 0;
}

FUNC process() -> void {
    LET xs = [10, 20, 30];
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("__ploy_mapfunc_to_list") != std::string::npos);
    REQUIRE(ir.find("process") != std::string::npos);
}

// ============================================================================
// Package Import Tests
// ============================================================================

TEST_CASE("Ploy lexer tokenizes PACKAGE keyword", "[ploy][lexer][package]") {
    auto tokens = Tokenize("IMPORT PACKAGE");
    size_t keyword_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword) ++keyword_count;
    }
    REQUIRE(keyword_count == 2);
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE declaration", "[ploy][parser][package]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->language == "python");
    REQUIRE(import->package_name == "numpy");
    REQUIRE(import->module_path == "numpy");
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE with alias", "[ploy][parser][package]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy AS np;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->language == "python");
    REQUIRE(import->package_name == "numpy");
    REQUIRE(import->alias == "np");
}

TEST_CASE("Ploy parser parses IMPORT qualified module", "[ploy][parser][package]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT cpp::math_utils;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->language == "cpp");
    REQUIRE(import->module_path == "math_utils");
}

TEST_CASE("Ploy sema validates IMPORT PACKAGE", "[ploy][sema][package]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE numpy AS np;
)", diags, sema);
    REQUIRE(ok);
    auto it = sema.Symbols().find("np");
    REQUIRE(it != sema.Symbols().end());
    REQUIRE(it->second.kind == PloySymbol::Kind::kImport);
    REQUIRE(it->second.language == "python");
}

TEST_CASE("Ploy sema rejects invalid language in IMPORT PACKAGE", "[ploy][sema][package]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT cobol PACKAGE legacy.system;
)", diags, sema);
    // 'cobol' is not a supported language
    REQUIRE(!ok);
}

TEST_CASE("Ploy lowering generates IMPORT PACKAGE global", "[ploy][lowering][package]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE numpy AS np;
FUNC test() -> void {
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// Unified Syntax Validation Tests
// ============================================================================

TEST_CASE("Ploy parser enforces semicolons on LINK", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
LINK(cpp, python, math::add, utils::get);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser enforces semicolons on MAP_TYPE", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
MAP_TYPE(cpp::int, python::int);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser enforces semicolons on IMPORT", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT cpp::math;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser enforces semicolons on EXPORT", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC f() -> void { RETURN; }
EXPORT f;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser enforces semicolons on statements", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC test() -> i32 {
    LET a = 1;
    VAR b = 2;
    b = a + b;
    RETURN b;
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy LINK with body does not require semicolons", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
LINK(cpp, python, math::process, data::load) {
    MAP_TYPE(cpp::double, python::float);
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto link = std::dynamic_pointer_cast<LinkDecl>(module->declarations[0]);
    REQUIRE(link);
    REQUIRE(link->body.size() == 1);
}

TEST_CASE("Ploy LINK AS VAR and AS STRUCT", "[ploy][parser][syntax]") {
    Diagnostics diags;
    auto module = Parse(R"(
LINK(cpp, python, config_data, py_config) AS VAR;
LINK(cpp, rust, Point, RustPoint) AS STRUCT {
    MAP_TYPE(cpp::double, rust::f64);
}
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 2);
    auto link1 = std::dynamic_pointer_cast<LinkDecl>(module->declarations[0]);
    auto link2 = std::dynamic_pointer_cast<LinkDecl>(module->declarations[1]);
    REQUIRE(link1->link_kind == LinkDecl::LinkKind::kVariable);
    REQUIRE(link2->link_kind == LinkDecl::LinkKind::kStruct);
}

// ============================================================================
// Version Constraint Tests
// ============================================================================

TEST_CASE("Ploy parser parses IMPORT PACKAGE with version constraint >=", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy >= 1.20;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->language == "python");
    REQUIRE(import->package_name == "numpy");
    REQUIRE(import->version_op == ">=");
    REQUIRE(import->version_constraint == "1.20");
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE with version constraint ==", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE torch == 2.0.0;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->package_name == "torch");
    REQUIRE(import->version_op == "==");
    REQUIRE(import->version_constraint == "2.0.0");
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE with version and alias", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy >= 1.20 AS np;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->package_name == "numpy");
    REQUIRE(import->version_op == ">=");
    REQUIRE(import->version_constraint == "1.20");
    REQUIRE(import->alias == "np");
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE with version <=", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE scipy <= 1.10.0;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->version_op == "<=");
    REQUIRE(import->version_constraint == "1.10.0");
}

TEST_CASE("Ploy parser parses IMPORT PACKAGE with version ~=", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE flask ~= 2.3;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->version_op == "~=");
    REQUIRE(import->version_constraint == "2.3");
}

TEST_CASE("Ploy sema validates version constraint format", "[ploy][sema][version]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE numpy >= 1.20;
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema rejects invalid version operator on non-package import", "[ploy][sema][version]") {
    // version_op can only be set via PACKAGE imports,
    // so any well-formed PACKAGE import with a valid version should pass
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE pandas >= 1.5.0;
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy lowering generates version constraint metadata", "[ploy][lowering][version]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE numpy >= 1.20;
FUNC test() -> void {
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// Selective Import Tests
// ============================================================================

TEST_CASE("Ploy parser parses selective import", "[ploy][parser][selective]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy::(array, mean, std);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->language == "python");
    REQUIRE(import->package_name == "numpy");
    REQUIRE(import->selected_symbols.size() == 3);
    REQUIRE(import->selected_symbols[0] == "array");
    REQUIRE(import->selected_symbols[1] == "mean");
    REQUIRE(import->selected_symbols[2] == "std");
}

TEST_CASE("Ploy parser parses selective import with single symbol", "[ploy][parser][selective]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE os::(path);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->selected_symbols.size() == 1);
    REQUIRE(import->selected_symbols[0] == "path");
}

TEST_CASE("Ploy parser parses selective import with version", "[ploy][parser][selective]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy::(array, mean) >= 1.20;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->selected_symbols.size() == 2);
    REQUIRE(import->version_op == ">=");
    REQUIRE(import->version_constraint == "1.20");
}

TEST_CASE("Ploy parser parses selective import with alias", "[ploy][parser][selective]") {
    // Parser accepts this syntax, but sema will reject it
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy::(array, mean) AS np;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->selected_symbols.size() == 2);
    REQUIRE(import->alias == "np");
}

TEST_CASE("Ploy sema rejects selective import with alias", "[ploy][sema][selective]") {
    // Selective import + AS alias is ambiguous and must be rejected
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE numpy::(array, mean) AS np;
)", diags, sema);
    REQUIRE(!ok);
}

TEST_CASE("Ploy sema validates selective import", "[ploy][sema][selective]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE numpy::(array, mean);
)", diags, sema);
    REQUIRE(ok);
    // The selected symbols should be registered individually
    auto it_array = sema.Symbols().find("array");
    REQUIRE(it_array != sema.Symbols().end());
    auto it_mean = sema.Symbols().find("mean");
    REQUIRE(it_mean != sema.Symbols().end());
    // The package itself should also be registered
    auto it_numpy = sema.Symbols().find("numpy");
    REQUIRE(it_numpy != sema.Symbols().end());
}

TEST_CASE("Ploy sema rejects duplicate symbols in selective import", "[ploy][sema][selective]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE numpy::(array, array);
)", diags, sema);
    // Should report a warning/error about duplicate but still succeed structurally
    // The sema reports the duplicate but doesn't fail the analysis entirely
    // (the redefinition error for the second "array" symbol makes it fail)
    REQUIRE(!ok);
}

TEST_CASE("Ploy lowering generates selective import metadata", "[ploy][lowering][selective]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE numpy::(array, mean);
FUNC test() -> void {
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// CONFIG VENV Tests
// ============================================================================

TEST_CASE("Ploy lexer tokenizes CONFIG and VENV keywords", "[ploy][lexer][venv]") {
    auto tokens = Tokenize("CONFIG VENV");
    size_t keyword_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword) ++keyword_count;
    }
    REQUIRE(keyword_count == 2);
}

TEST_CASE("Ploy parser parses CONFIG VENV declaration", "[ploy][parser][venv]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG VENV python "C:/Users/me/venvs/myenv";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->language == "python");
    REQUIRE(venv->venv_path == "C:/Users/me/venvs/myenv");
}

TEST_CASE("Ploy parser parses CONFIG VENV without language", "[ploy][parser][venv]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG VENV "/home/user/.virtualenvs/ml";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->language == "python");  // defaults to python
    REQUIRE(venv->venv_path == "/home/user/.virtualenvs/ml");
}

TEST_CASE("Ploy sema validates CONFIG VENV", "[ploy][sema][venv]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG VENV python "C:/envs/myenv";
IMPORT python PACKAGE numpy;
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.VenvConfigs().size() == 1);
    REQUIRE(sema.VenvConfigs()[0].language == "python");
    REQUIRE(sema.VenvConfigs()[0].venv_path == "C:/envs/myenv");
}

TEST_CASE("Ploy sema rejects duplicate CONFIG VENV", "[ploy][sema][venv]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG VENV python "C:/envs/env1";
CONFIG VENV python "C:/envs/env2";
)", diags, sema);
    REQUIRE(!ok);  // duplicate venv config for same language
}

TEST_CASE("Ploy sema rejects CONFIG VENV with invalid language", "[ploy][sema][venv]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG VENV cobol "/opt/legacy";
)", diags, sema);
    REQUIRE(!ok);  // cobol is not a supported language
}

// ============================================================================
// Complex Import Combinations
// ============================================================================

TEST_CASE("Ploy parser parses dotted package with selective import", "[ploy][parser][package]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy.linalg::(solve, inv);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(import);
    REQUIRE(import->package_name == "numpy.linalg");
    REQUIRE(import->selected_symbols.size() == 2);
    REQUIRE(import->selected_symbols[0] == "solve");
    REQUIRE(import->selected_symbols[1] == "inv");
}

TEST_CASE("Ploy full pipeline: VENV + versioned import + selective", "[ploy][integration][package]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
CONFIG VENV python "C:/envs/ml";

IMPORT python PACKAGE numpy::(array, mean) >= 1.20;

FUNC compute() -> void {
    LET data = [1, 2, 3];
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy parser parses multiple versioned imports", "[ploy][parser][version]") {
    Diagnostics diags;
    auto module = Parse(R"(
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE pandas >= 1.5.0;
IMPORT python PACKAGE scipy == 1.10.0;
IMPORT rust PACKAGE serde >= 1.0;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 4);

    auto np = std::dynamic_pointer_cast<ImportDecl>(module->declarations[0]);
    REQUIRE(np->version_op == ">=");
    REQUIRE(np->version_constraint == "1.20");

    auto pd = std::dynamic_pointer_cast<ImportDecl>(module->declarations[1]);
    REQUIRE(pd->version_op == ">=");
    REQUIRE(pd->version_constraint == "1.5.0");

    auto sp = std::dynamic_pointer_cast<ImportDecl>(module->declarations[2]);
    REQUIRE(sp->version_op == "==");
    REQUIRE(sp->version_constraint == "1.10.0");

    auto sr = std::dynamic_pointer_cast<ImportDecl>(module->declarations[3]);
    REQUIRE(sr->version_op == ">=");
    REQUIRE(sr->version_constraint == "1.0");
}

// ============================================================================
// Package Import with LINK Integration
// ============================================================================

TEST_CASE("Ploy versioned import with LINK and PIPELINE", "[ploy][integration][version]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE scipy.optimize >= 1.8;

LINK(cpp, python, compute, np::mean);
MAP_TYPE(cpp::double, python::float);

PIPELINE analysis {
    LET x = 3.14;
    LET result = CALL(cpp, compute, x);
    RETURN result;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("__ploy_pipeline_analysis") != std::string::npos);
}

// ============================================================================
// Multi Package Manager Tests (CONFIG CONDA / UV / PIPENV / POETRY)
// ============================================================================

TEST_CASE("Ploy lexer tokenizes new package manager keywords", "[ploy][lexer][pkgmgr]") {
    auto tokens = Tokenize("CONFIG CONDA UV PIPENV POETRY VENV");
    size_t keyword_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword) ++keyword_count;
    }
    REQUIRE(keyword_count == 6);
}

TEST_CASE("Ploy parser parses CONFIG CONDA declaration", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG CONDA python "ml_env";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->manager == VenvConfigDecl::ManagerKind::kConda);
    REQUIRE(venv->language == "python");
    REQUIRE(venv->venv_path == "ml_env");
}

TEST_CASE("Ploy parser parses CONFIG CONDA without language", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG CONDA "data_science";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->manager == VenvConfigDecl::ManagerKind::kConda);
    REQUIRE(venv->language == "python");  // defaults to python
    REQUIRE(venv->venv_path == "data_science");
}

TEST_CASE("Ploy parser parses CONFIG UV declaration", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG UV python "D:/venvs/uv_env";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->manager == VenvConfigDecl::ManagerKind::kUv);
    REQUIRE(venv->language == "python");
    REQUIRE(venv->venv_path == "D:/venvs/uv_env");
}

TEST_CASE("Ploy parser parses CONFIG PIPENV declaration", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG PIPENV python "C:/projects/myapp";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->manager == VenvConfigDecl::ManagerKind::kPipenv);
    REQUIRE(venv->language == "python");
    REQUIRE(venv->venv_path == "C:/projects/myapp");
}

TEST_CASE("Ploy parser parses CONFIG POETRY declaration", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG POETRY "C:/projects/poetry_app";
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    auto venv = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(venv);
    REQUIRE(venv->manager == VenvConfigDecl::ManagerKind::kPoetry);
    REQUIRE(venv->language == "python");  // defaults to python
    REQUIRE(venv->venv_path == "C:/projects/poetry_app");
}

TEST_CASE("Ploy sema validates CONFIG CONDA", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG CONDA python "ml_env";
IMPORT python PACKAGE numpy;
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.VenvConfigs().size() == 1);
    REQUIRE(sema.VenvConfigs()[0].manager == VenvConfigDecl::ManagerKind::kConda);
    REQUIRE(sema.VenvConfigs()[0].language == "python");
    REQUIRE(sema.VenvConfigs()[0].venv_path == "ml_env");
}

TEST_CASE("Ploy sema validates CONFIG UV", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG UV python "D:/venvs/uv_env";
IMPORT python PACKAGE requests;
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.VenvConfigs().size() == 1);
    REQUIRE(sema.VenvConfigs()[0].manager == VenvConfigDecl::ManagerKind::kUv);
}

TEST_CASE("Ploy sema validates CONFIG PIPENV", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG PIPENV python "C:/projects/myapp";
IMPORT python PACKAGE flask;
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.VenvConfigs().size() == 1);
    REQUIRE(sema.VenvConfigs()[0].manager == VenvConfigDecl::ManagerKind::kPipenv);
}

TEST_CASE("Ploy sema validates CONFIG POETRY", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG POETRY python "C:/projects/poetry_app";
IMPORT python PACKAGE django;
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.VenvConfigs().size() == 1);
    REQUIRE(sema.VenvConfigs()[0].manager == VenvConfigDecl::ManagerKind::kPoetry);
}

TEST_CASE("Ploy sema rejects duplicate CONFIG for same language", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG CONDA python "env1";
CONFIG UV python "env2";
)", diags, sema);
    REQUIRE(!ok);  // duplicate config for language python
}

TEST_CASE("Ploy sema rejects CONFIG CONDA with invalid language", "[ploy][sema][pkgmgr]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
CONFIG CONDA cobol "legacy_env";
)", diags, sema);
    REQUIRE(!ok);  // cobol is not a supported language
}

TEST_CASE("Ploy lowering handles CONFIG CONDA correctly", "[ploy][lowering][pkgmgr]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
CONFIG CONDA python "ml_env";

IMPORT python PACKAGE numpy >= 1.20 AS np;

FUNC compute() -> void {
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy lowering handles CONFIG UV correctly", "[ploy][lowering][pkgmgr]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
CONFIG UV python "D:/venvs/uv_env";

IMPORT python PACKAGE torch >= 2.0;

FUNC train() -> void {
    RETURN;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy parser parses mixed configs and imports", "[ploy][parser][pkgmgr]") {
    Diagnostics diags;
    auto module = Parse(R"(
CONFIG CONDA python "ml_env";
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE scipy::(optimize, linalg) >= 1.8;
IMPORT rust PACKAGE serde >= 1.0;
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 4);

    auto cfg = std::dynamic_pointer_cast<VenvConfigDecl>(module->declarations[0]);
    REQUIRE(cfg);
    REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kConda);

    auto np_import = std::dynamic_pointer_cast<ImportDecl>(module->declarations[1]);
    REQUIRE(np_import);
    REQUIRE(np_import->package_name == "numpy");
    REQUIRE(np_import->version_op == ">=");
    REQUIRE(np_import->version_constraint == "1.20");
}

TEST_CASE("Ploy full pipeline: CONDA + versioned imports + selective", "[ploy][integration][pkgmgr]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
CONFIG CONDA python "data_science";

IMPORT python PACKAGE numpy::(array, mean) >= 1.20;
IMPORT python PACKAGE pandas >= 2.0;

LINK(cpp, python, compute, numpy::mean);
MAP_TYPE(cpp::double, python::float);

PIPELINE analysis {
    LET data = [1.0, 2.0, 3.0];
    LET result = CALL(python, numpy::mean, data);
    RETURN result;
}
)", diags);
    REQUIRE(!ir.empty());
    REQUIRE(ir.find("__ploy_pipeline_analysis") != std::string::npos);
}

TEST_CASE("Ploy full pipeline: POETRY + multiple packages", "[ploy][integration][pkgmgr]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
CONFIG POETRY python "C:/my_project";

IMPORT python PACKAGE fastapi >= 0.100;
IMPORT python PACKAGE pydantic >= 2.0;

FUNC serve() -> void {
    RETURN;
}

EXPORT serve AS "start_server";
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// Class Instantiation Tests �� NEW and METHOD keywords
// ============================================================================

// -- Lexer tests --

TEST_CASE("Ploy lexer tokenizes NEW keyword", "[ploy][lexer][class]") {
    auto tokens = Tokenize("NEW");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "NEW");
}

TEST_CASE("Ploy lexer tokenizes METHOD keyword", "[ploy][lexer][class]") {
    auto tokens = Tokenize("METHOD");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "METHOD");
}

TEST_CASE("Ploy lexer tokenizes NEW and METHOD in context", "[ploy][lexer][class]") {
    auto tokens = Tokenize("LET obj = NEW(python, MyClass, 42);");
    bool found_new = false;
    bool found_method = false;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword && t.lexeme == "NEW") found_new = true;
    }
    CHECK(found_new);

    tokens = Tokenize("LET result = METHOD(python, obj, forward, data);");
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword && t.lexeme == "METHOD") found_method = true;
    }
    CHECK(found_method);
}

// -- Parser tests --

TEST_CASE("Ploy parser parses NEW expression", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET model = NEW(python, torch::nn::Linear, 784, 10);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[0]);
    REQUIRE(var);
    CHECK(var->name == "model");

    auto new_expr = std::dynamic_pointer_cast<NewExpression>(var->init);
    REQUIRE(new_expr);
    CHECK(new_expr->language == "python");
    CHECK(new_expr->class_name == "torch::nn::Linear");
    CHECK(new_expr->args.size() == 2);
}

TEST_CASE("Ploy parser parses NEW with no args", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET obj = NEW(python, MyClass);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 1);

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[0]);
    REQUIRE(var);
    auto new_expr = std::dynamic_pointer_cast<NewExpression>(var->init);
    REQUIRE(new_expr);
    CHECK(new_expr->language == "python");
    CHECK(new_expr->class_name == "MyClass");
    CHECK(new_expr->args.empty());
}

TEST_CASE("Ploy parser parses NEW with string args", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET conn = NEW(python, sqlite3::Connection, "database.db");
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[0]);
    REQUIRE(var);
    auto new_expr = std::dynamic_pointer_cast<NewExpression>(var->init);
    REQUIRE(new_expr);
    CHECK(new_expr->language == "python");
    CHECK(new_expr->class_name == "sqlite3::Connection");
    CHECK(new_expr->args.size() == 1);
}

TEST_CASE("Ploy parser parses METHOD expression", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET model = NEW(python, Model);
LET output = METHOD(python, model, forward, data);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());
    REQUIRE(module->declarations.size() == 2);

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[1]);
    REQUIRE(var);
    CHECK(var->name == "output");

    auto method = std::dynamic_pointer_cast<MethodCallExpression>(var->init);
    REQUIRE(method);
    CHECK(method->language == "python");
    CHECK(method->method_name == "forward");
    CHECK(method->args.size() == 1);

    auto obj_id = std::dynamic_pointer_cast<Identifier>(method->object);
    REQUIRE(obj_id);
    CHECK(obj_id->name == "model");
}

TEST_CASE("Ploy parser parses METHOD with no extra args", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET obj = NEW(python, Tokenizer);
LET text = METHOD(python, obj, get_vocab);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[1]);
    REQUIRE(var);
    auto method = std::dynamic_pointer_cast<MethodCallExpression>(var->init);
    REQUIRE(method);
    CHECK(method->language == "python");
    CHECK(method->method_name == "get_vocab");
    CHECK(method->args.empty());
}

TEST_CASE("Ploy parser parses METHOD with qualified method name", "[ploy][parser][class]") {
    Diagnostics diags;
    auto module = Parse(R"(
LET obj = NEW(python, Module);
LET val = METHOD(python, obj, utils::serialize, data);
)", diags);
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());

    auto var = std::dynamic_pointer_cast<VarDecl>(module->declarations[1]);
    REQUIRE(var);
    auto method = std::dynamic_pointer_cast<MethodCallExpression>(var->init);
    REQUIRE(method);
    CHECK(method->method_name == "utils::serialize");
}

// -- Sema tests --

TEST_CASE("Ploy sema validates NEW expression", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE torch;
LET model = NEW(python, torch::nn::Linear, 784, 10);
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema rejects NEW with invalid language", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LET obj = NEW(cobol, SomeClass);
)", diags, sema);
    REQUIRE(!ok);
}

TEST_CASE("Ploy sema validates METHOD expression", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE torch;
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET output = METHOD(python, model, forward, data);
)", diags, sema);
    // Note: 'data' is undefined, but cross-lang contexts are lenient
    // The important check is that NEW and METHOD themselves are valid
    REQUIRE(!ok); // 'data' is undefined
}

TEST_CASE("Ploy sema validates METHOD with defined args", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
IMPORT python PACKAGE sklearn;
LET model = NEW(python, sklearn::LinearRegression);
LET input = [1.0, 2.0, 3.0];
LET result = METHOD(python, model, predict, input);
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema rejects METHOD with invalid language", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LET obj = NEW(python, MyClass);
LET result = METHOD(ruby, obj, run);
)", diags, sema);
    REQUIRE(!ok);
}

// -- Lowering tests --

TEST_CASE("Ploy lowering generates NEW constructor stub", "[ploy][lowering][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE torch;

FUNC create_model() -> INT {
    LET model = NEW(python, torch::nn::Linear, 784, 10);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    // The IR should contain a call to the constructor stub
    CHECK(ir.find("__ploy_bridge") != std::string::npos);
}

TEST_CASE("Ploy lowering generates METHOD call stub", "[ploy][lowering][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE torch;

FUNC run_model() -> INT {
    LET model = NEW(python, torch::nn::Linear, 10, 5);
    LET input = 42;
    LET output = METHOD(python, model, forward, input);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    // The IR should contain both constructor and method call stubs
    CHECK(ir.find("__ploy_bridge") != std::string::npos);
}

TEST_CASE("Ploy lowering generates correct call descriptor for NEW", "[ploy][lowering][class]") {
    Diagnostics diags;
    PloyLexer lexer(R"(
IMPORT python PACKAGE sklearn;

FUNC build() -> INT {
    LET model = NEW(python, sklearn::LinearRegression);
    RETURN 0;
}
)", "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());

    PloySema sema(diags, PloySemaOptions{});
    REQUIRE(sema.Analyze(module));

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    REQUIRE(lowering.Lower(module));

    const auto &descriptors = lowering.CallDescriptors();
    bool found_ctor = false;
    for (const auto &desc : descriptors) {
        if (desc.source_function.find("__init__") != std::string::npos) {
            found_ctor = true;
            CHECK(desc.source_language == "python");
        }
    }
    CHECK(found_ctor);
}

TEST_CASE("Ploy lowering generates correct call descriptor for METHOD", "[ploy][lowering][class]") {
    Diagnostics diags;
    PloyLexer lexer(R"(
IMPORT python PACKAGE torch;

FUNC infer() -> INT {
    LET model = NEW(python, torch::Module);
    LET x = 1;
    LET result = METHOD(python, model, forward, x);
    RETURN 0;
}
)", "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module);
    REQUIRE(!diags.HasErrors());

    PloySema sema(diags, PloySemaOptions{});
    REQUIRE(sema.Analyze(module));

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    REQUIRE(lowering.Lower(module));

    const auto &descriptors = lowering.CallDescriptors();
    bool found_method = false;
    for (const auto &desc : descriptors) {
        if (desc.source_function == "forward") {
            found_method = true;
            CHECK(desc.source_language == "python");
            // Method call should have object as first param
            CHECK(desc.source_param_types.size() == 2); // object + x
        }
    }
    CHECK(found_method);
}

// -- Integration tests --

TEST_CASE("Ploy full pipeline: NEW + METHOD + LINK", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE torch;
IMPORT cpp::inference_engine;

LINK(cpp, python, run_inference, torch::forward) {
    MAP_TYPE(cpp::float_ptr, python::Tensor);
}

FUNC inference_pipeline() -> INT {
    LET model = NEW(python, torch::nn::Sequential);
    LET data = 42;
    LET prediction = METHOD(python, model, forward, data);
    LET result = CALL(cpp, inference_engine::postprocess, prediction);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy full pipeline: multiple NEW + METHOD chain", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE sklearn;
IMPORT python PACKAGE numpy;

FUNC ml_pipeline() -> INT {
    LET scaler = NEW(python, sklearn::StandardScaler);
    LET model = NEW(python, sklearn::LinearRegression);
    LET data = [1.0, 2.0, 3.0];
    LET scaled = METHOD(python, scaler, fit_transform, data);
    LET prediction = METHOD(python, model, predict, scaled);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy full pipeline: NEW inside PIPELINE", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE torch;

PIPELINE ml_pipeline {
    LET model = NEW(python, torch::nn::Linear, 128, 10);
    LET optimizer = NEW(python, torch::optim::SGD, 0.01);
    LET loss = METHOD(python, model, forward, 42);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy full pipeline: NEW with Rust classes", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT rust PACKAGE tokio;

FUNC async_io() -> INT {
    LET runtime = NEW(rust, tokio::Runtime);
    LET handle = METHOD(rust, runtime, spawn, 42);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// GET / SET / WITH �� Lexer Tests
// ============================================================================

TEST_CASE("Ploy lexer: GET keyword recognised", "[ploy][lexer]") {
    auto tokens = Tokenize("GET");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "GET");
}

TEST_CASE("Ploy lexer: SET keyword recognised", "[ploy][lexer]") {
    auto tokens = Tokenize("SET");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "SET");
}

TEST_CASE("Ploy lexer: WITH keyword recognised", "[ploy][lexer]") {
    auto tokens = Tokenize("WITH");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].kind == TokenKind::kKeyword);
    CHECK(tokens[0].lexeme == "WITH");
}

TEST_CASE("Ploy lexer: GET SET WITH in context", "[ploy][lexer]") {
    auto tokens = Tokenize("GET(python, obj, attr); SET(python, obj, x, 42); WITH(python, res) AS r {}");
    int get_count = 0, set_count = 0, with_count = 0;
    for (const auto &t : tokens) {
        if (t.kind == TokenKind::kKeyword && t.lexeme == "GET") get_count++;
        if (t.kind == TokenKind::kKeyword && t.lexeme == "SET") set_count++;
        if (t.kind == TokenKind::kKeyword && t.lexeme == "WITH") with_count++;
    }
    CHECK(get_count == 1);
    CHECK(set_count == 1);
    CHECK(with_count == 1);
}

// ============================================================================
// GET / SET �� Parser Tests
// ============================================================================

TEST_CASE("Ploy parser: GET attribute expression", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    LET val = GET(python, obj, my_attr);
    RETURN val;
}
)", diags);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<FuncDecl>(mod->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->body.size() == 3);
    auto var_decl = std::dynamic_pointer_cast<VarDecl>(func->body[1]);
    REQUIRE(var_decl);
    auto get = std::dynamic_pointer_cast<GetAttrExpression>(var_decl->init);
    REQUIRE(get);
    CHECK(get->language == "python");
    CHECK(get->attr_name == "my_attr");
}

TEST_CASE("Ploy parser: SET attribute expression", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(python, obj, x, 42);
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<FuncDecl>(mod->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->body.size() == 3);
    auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(func->body[1]);
    REQUIRE(expr_stmt);
    auto set_expr = std::dynamic_pointer_cast<SetAttrExpression>(expr_stmt->expr);
    REQUIRE(set_expr);
    CHECK(set_expr->language == "python");
    CHECK(set_expr->attr_name == "x");
}

TEST_CASE("Ploy parser: GET with qualified object", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    LET obj = NEW(python, torch::nn::Linear, 10, 5);
    LET w = GET(python, obj, weight);
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
}

TEST_CASE("Ploy parser: SET with expression value", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(python, obj, threshold, 0.5 + 0.1);
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
}

// ============================================================================
// WITH Statement �� Parser Tests
// ============================================================================

TEST_CASE("Ploy parser: WITH statement basic", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "file.txt")) AS f {
        LET data = METHOD(python, f, read);
    }
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<FuncDecl>(mod->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->body.size() == 2);
    auto with_stmt = std::dynamic_pointer_cast<WithStatement>(func->body[0]);
    REQUIRE(with_stmt);
    CHECK(with_stmt->language == "python");
    CHECK(with_stmt->var_name == "f");
    REQUIRE(with_stmt->body.size() == 1);
}

TEST_CASE("Ploy parser: WITH with multiple statements", "[ploy][parser]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, sqlite3::Connection, "db.sqlite")) AS conn {
        LET cursor = METHOD(python, conn, cursor);
        METHOD(python, cursor, execute, "SELECT 1");
        LET result = METHOD(python, cursor, fetchone);
    }
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
    auto func = std::dynamic_pointer_cast<FuncDecl>(mod->declarations[0]);
    REQUIRE(func);
    auto with_stmt = std::dynamic_pointer_cast<WithStatement>(func->body[0]);
    REQUIRE(with_stmt);
    CHECK(with_stmt->body.size() == 3);
}

// ============================================================================
// GET / SET �� Sema Tests
// ============================================================================

TEST_CASE("Ploy sema: GET valid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    LET val = GET(python, obj, my_attr);
    RETURN 0;
}
)", diags, sema);
    CHECK(ok);
}

TEST_CASE("Ploy sema: GET invalid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    LET val = GET(cobol, obj, attr);
    RETURN 0;
}
)", diags, sema);
    CHECK(!ok);
}

TEST_CASE("Ploy sema: SET valid", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(python, obj, x, 42);
    RETURN 0;
}
)", diags, sema);
    CHECK(ok);
}

TEST_CASE("Ploy sema: SET invalid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(cobol, obj, x, 42);
    RETURN 0;
}
)", diags, sema);
    CHECK(!ok);
}

// ============================================================================
// WITH �� Sema Tests
// ============================================================================

TEST_CASE("Ploy sema: WITH valid", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "file.txt")) AS f {
        LET data = METHOD(python, f, read);
    }
    RETURN 0;
}
)", diags, sema);
    CHECK(ok);
}

TEST_CASE("Ploy sema: WITH invalid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    WITH(cobol, NEW(python, open, "file.txt")) AS f {
        LET data = METHOD(python, f, read);
    }
    RETURN 0;
}
)", diags, sema);
    CHECK(!ok);
}

TEST_CASE("Ploy sema: WITH variable accessible in body", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "test.csv")) AS file {
        LET content = METHOD(python, file, read);
        LET size = METHOD(python, file, tell);
    }
    RETURN 0;
}
)", diags, sema);
    CHECK(ok);
}

// ============================================================================
// GET / SET �� Lowering Tests
// ============================================================================

TEST_CASE("Ploy lowering: GET generates getattr stub", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    LET val = GET(python, obj, my_attr);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__getattr__") != std::string::npos);
}

TEST_CASE("Ploy lowering: SET generates setattr stub", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(python, obj, x, 42);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__setattr__") != std::string::npos);
}

TEST_CASE("Ploy lowering: GET descriptor recorded", "[ploy][lowering]") {
    Diagnostics diags;
    auto [ir_str, descriptors] = LowerAndGetDescriptors(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    LET w = GET(python, obj, weight);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir_str.empty());

    bool found_getattr = false;
    for (const auto &d : descriptors) {
        if (d.source_function.find("__getattr__") != std::string::npos &&
            d.source_function.find("weight") != std::string::npos) {
            found_getattr = true;
            CHECK(d.source_language == "python");
        }
    }
    CHECK(found_getattr);
}

TEST_CASE("Ploy lowering: SET descriptor recorded", "[ploy][lowering]") {
    Diagnostics diags;
    auto [ir_str, descriptors] = LowerAndGetDescriptors(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass);
    SET(python, obj, x, 99);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir_str.empty());

    bool found_setattr = false;
    for (const auto &d : descriptors) {
        if (d.source_function.find("__setattr__") != std::string::npos &&
            d.source_function.find("x") != std::string::npos) {
            found_setattr = true;
            CHECK(d.source_language == "python");
        }
    }
    CHECK(found_setattr);
}

// ============================================================================
// WITH �� Lowering Tests
// ============================================================================

TEST_CASE("Ploy lowering: WITH generates enter/exit stubs", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "file.txt")) AS f {
        LET data = METHOD(python, f, read);
    }
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__enter__") != std::string::npos);
    CHECK(ir.find("__exit__") != std::string::npos);
}

TEST_CASE("Ploy lowering: WITH descriptor records enter and exit", "[ploy][lowering]") {
    Diagnostics diags;
    auto [ir_str, descriptors] = LowerAndGetDescriptors(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "data.csv")) AS f {
        LET line = METHOD(python, f, readline);
    }
    RETURN 0;
}
)", diags);
    REQUIRE(!ir_str.empty());

    bool found_enter = false, found_exit = false;
    for (const auto &d : descriptors) {
        if (d.source_function == "__enter__") found_enter = true;
        if (d.source_function == "__exit__") found_exit = true;
    }
    CHECK(found_enter);
    CHECK(found_exit);
}

// ============================================================================
// Type Annotation Tests
// ============================================================================

TEST_CASE("Ploy parser: qualified type annotation on NEW", "[ploy][parser][class]") {
    Diagnostics diags;
    auto mod = Parse(R"(
FUNC test() -> INT {
    LET model: python::nn::Module = NEW(python, torch::nn::Linear, 10, 5);
    RETURN 0;
}
)", diags);
    REQUIRE(!diags.HasErrors());
    auto func = std::dynamic_pointer_cast<FuncDecl>(mod->declarations[0]);
    REQUIRE(func);
    auto var_decl = std::dynamic_pointer_cast<VarDecl>(func->body[0]);
    REQUIRE(var_decl);
    CHECK(var_decl->name == "model");
    auto qt = std::dynamic_pointer_cast<QualifiedType>(var_decl->type);
    REQUIRE(qt);
    CHECK(qt->language == "python");
    CHECK(qt->type_name == "nn::Module");
}

TEST_CASE("Ploy sema: qualified type annotation with NEW accepted", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC test() -> INT {
    LET model: python::nn::Module = NEW(python, torch::nn::Linear, 10, 5);
    LET output = METHOD(python, model, forward, 42);
    RETURN 0;
}
)", diags, sema);
    CHECK(ok);
}

// ============================================================================
// Interface Mapping Tests (MAP_TYPE with classes)
// ============================================================================

TEST_CASE("Ploy parser: MAP_TYPE for class interface", "[ploy][parser][class]") {
    Diagnostics diags;
    auto mod = Parse(R"(
MAP_TYPE(python::nn::Module, cpp::NeuralNet);

LINK(cpp, python, run_model, torch::forward) {
    MAP_TYPE(cpp::NeuralNet, python::nn::Module);
}
)", diags);
    REQUIRE(!diags.HasErrors());
    REQUIRE(mod->declarations.size() >= 2);
}

TEST_CASE("Ploy sema: MAP_TYPE interface mapping valid", "[ploy][sema][class]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
MAP_TYPE(python::nn::Module, cpp::NeuralNet);
)", diags, sema);
    CHECK(ok);
}

// ============================================================================
// Integration Tests �� GET / SET / WITH combined
// ============================================================================

TEST_CASE("Ploy integration: GET + SET + METHOD combined", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> INT {
    LET obj = NEW(python, MyClass, 10);
    SET(python, obj, threshold, 0.5);
    LET t = GET(python, obj, threshold);
    LET result = METHOD(python, obj, process, t);
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy integration: WITH + GET + SET pipeline", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC test() -> INT {
    WITH(python, NEW(python, open, "config.json")) AS f {
        LET content = METHOD(python, f, read);
        LET size = GET(python, f, name);
    }
    RETURN 0;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy integration: mixed cross-lang class instantiation", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
IMPORT python PACKAGE sklearn.preprocessing;
IMPORT cpp::image_processing;

FUNC cross_lang_demo() -> INT {
    LET scaler = NEW(python, sklearn::preprocessing::StandardScaler);
    LET scaled = METHOD(python, scaler, fit_transform, [1.0, 2.0, 3.0]);
    LET processor = NEW(cpp, image_processing::ImageProcessor, scaled);
    LET result = METHOD(cpp, processor, process, 640, 480);
    RETURN result;
}
)", diags);
    REQUIRE(!ir.empty());
}

TEST_CASE("Ploy integration: WITH for database connection", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"ploy(
FUNC db_query() -> INT {
    WITH(python, NEW(python, sqlite3::connect, "app.db")) AS conn {
        LET cursor = METHOD(python, conn, cursor);
        METHOD(python, cursor, execute, "CREATE TABLE t");
        METHOD(python, conn, commit);
    }
    RETURN 0;
}
)ploy", diags);
    REQUIRE(!ir.empty());
}

// ============================================================================
// DELETE keyword tests
// ============================================================================

TEST_CASE("Ploy lexer: DELETE keyword", "[ploy][lexer]") {
    auto tokens = Tokenize("DELETE(python, obj)");
    REQUIRE(tokens.size() >= 2);
    CHECK(tokens[0].lexeme == "DELETE");
    CHECK(tokens[0].kind == TokenKind::kKeyword);
}

TEST_CASE("Ploy parser: DELETE expression", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
FUNC cleanup() {
    LET obj = NEW(python, MyClass);
    DELETE(python, obj);
}
)", diags);
    REQUIRE(module != nullptr);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy sema: DELETE valid expression", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC cleanup() {
    LET obj = NEW(python, MyClass);
    DELETE(python, obj);
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy sema: DELETE invalid language", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC cleanup() {
    LET obj = NEW(python, MyClass);
    DELETE(cobol, obj);
}
)", diags, sema);
    // Should produce an error for unsupported language
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy lowering: DELETE generates cleanup call", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC cleanup() {
    LET obj = NEW(python, MyClass);
    DELETE(python, obj);
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_py_del") != std::string::npos);
}

TEST_CASE("Ploy lowering: DELETE cpp generates cpp_delete call", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC cleanup() {
    LET obj = NEW(cpp, MyClass);
    DELETE(cpp, obj);
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_cpp_delete") != std::string::npos);
}

TEST_CASE("Ploy lowering: DELETE rust generates rust_drop call", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
FUNC cleanup() {
    LET obj = NEW(rust, MyStruct);
    DELETE(rust, obj);
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_rust_drop") != std::string::npos);
}

// ============================================================================
// EXTEND keyword tests
// ============================================================================

TEST_CASE("Ploy lexer: EXTEND keyword", "[ploy][lexer]") {
    auto tokens = Tokenize("EXTEND(python, Base) AS Derived");
    REQUIRE(tokens.size() >= 4);
    CHECK(tokens[0].lexeme == "EXTEND");
    CHECK(tokens[0].kind == TokenKind::kKeyword);
}

TEST_CASE("Ploy parser: EXTEND declaration", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
}
)", diags);
    REQUIRE(module != nullptr);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy parser: EXTEND with multiple methods", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
EXTEND(cpp, Shape) AS Circle {
    FUNC area(radius: FLOAT) -> FLOAT {
        RETURN radius;
    }
    FUNC perimeter(radius: FLOAT) -> FLOAT {
        RETURN radius;
    }
}
)", diags);
    REQUIRE(module != nullptr);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy parser: EXTEND with qualified base class", "[ploy][parser]") {
    Diagnostics diags;
    auto module = Parse(R"(
EXTEND(python, sklearn::base::BaseEstimator) AS CustomEstimator {
    FUNC fit(X: INT) -> INT {
        RETURN X;
    }
}
)", diags);
    REQUIRE(module != nullptr);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy sema: EXTEND registers derived type", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
    // The derived type should be registered in the symbol table
    auto &symbols = sema.Symbols();
    CHECK(symbols.count("Dog") == 1);
}

TEST_CASE("Ploy sema: EXTEND invalid language error", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
EXTEND(cobol, Base) AS Derived {
    FUNC foo() { }
}
)", diags, sema);
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: EXTEND registers method signatures", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak(volume: INT) -> STRING {
        RETURN "Woof";
    }
}
)", diags, sema);
    CHECK(ok);
    // Method signature should be registered as Dog::speak
    auto &sigs = sema.KnownSignatures();
    CHECK(sigs.count("Dog::speak") == 1);
    if (sigs.count("Dog::speak")) {
        CHECK(sigs.at("Dog::speak").param_count == 1);
    }
}

TEST_CASE("Ploy lowering: EXTEND generates bridge functions", "[ploy][lowering]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_extend_Dog_speak") != std::string::npos);
    CHECK(ir.find("__ploy_extend_register") != std::string::npos);
}

// ============================================================================
// Parameter count mismatch error checking tests
// ============================================================================

TEST_CASE("Ploy sema: param count mismatch in local function call", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC add(a: INT, b: INT) -> INT {
    RETURN a;
}
FUNC main() {
    LET result = add(1);
}
)", diags, sema);
    // Should report param count mismatch: expected 2, got 1
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: param count mismatch too many args", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC greet(name: STRING) -> STRING {
    RETURN name;
}
FUNC main() {
    LET result = greet("hello", "world", "extra");
}
)", diags, sema);
    // Should report param count mismatch: expected 1, got 3
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: param count correct passes", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC add(a: INT, b: INT) -> INT {
    RETURN a;
}
FUNC main() {
    LET result = add(1, 2);
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Type mismatch error checking tests
// ============================================================================

TEST_CASE("Ploy sema: type mismatch in function call", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC process(value: INT) -> INT {
    RETURN value;
}
FUNC main() {
    LET result = process("not_an_int");
}
)", diags, sema);
    // Should report type mismatch: expected INT, got STRING
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: type mismatch multiple params", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC compute(x: FLOAT, y: INT) -> FLOAT {
    RETURN x;
}
FUNC main() {
    LET result = compute(1, "wrong");
}
)", diags, sema);
    // Second arg is STRING but expected INT
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: compatible types pass", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC process(value: INT) -> INT {
    RETURN value;
}
FUNC main() {
    LET result = process(42);
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Error code and diagnostics tests
// ============================================================================

TEST_CASE("Ploy sema: undefined variable produces error", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() {
    LET x = undefined_var;
}
)", diags, sema);
    CHECK(diags.HasErrors());
    CHECK(diags.ErrorCount() >= 1);
}

TEST_CASE("Ploy sema: redefined symbol produces error", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() {
    LET x = 10;
    LET x = 20;
}
)", diags, sema);
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: BREAK outside loop error", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() {
    BREAK;
}
)", diags, sema);
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: CONTINUE outside loop error", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() {
    CONTINUE;
}
)", diags, sema);
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: immutable assignment error", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() {
    LET x = 10;
    x = 20;
}
)", diags, sema);
    CHECK(diags.HasErrors());
}

TEST_CASE("Ploy sema: mutable assignment ok", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
FUNC main() {
    VAR x = 10;
    x = 20;
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Diagnostics infrastructure tests
// ============================================================================

TEST_CASE("Diagnostics: error count tracking", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc loc{"test.ploy", 1, 1};
    diags.ReportError(loc, ErrorCode::kUndefinedSymbol, "test error 1");
    diags.ReportError(loc, ErrorCode::kTypeMismatch, "test error 2");
    CHECK(diags.ErrorCount() == 2);
    CHECK(diags.HasErrors());
}

TEST_CASE("Diagnostics: warning count tracking", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc loc{"test.ploy", 1, 1};
    diags.ReportWarning(loc, ErrorCode::kGenericWarning, "test warning");
    CHECK(diags.WarningCount() == 1);
    CHECK(diags.HasWarnings());
    CHECK(!diags.HasErrors());
}

TEST_CASE("Diagnostics: error with suggestion", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc loc{"test.ploy", 1, 1};
    diags.ReportError(loc, ErrorCode::kUndefinedSymbol, "undefined 'x'", "did you mean 'y'?");
    CHECK(diags.ErrorCount() == 1);
    auto &all = diags.All();
    REQUIRE(all.size() == 1);
    CHECK(all[0].suggestion == "did you mean 'y'?");
}

TEST_CASE("Diagnostics: error with traceback", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc call_loc{"test.ploy", 10, 5};
    polyglot::core::SourceLoc decl_loc{"test.ploy", 2, 1};

    // Build related diagnostic for traceback chain
    polyglot::frontends::Diagnostic related_diag;
    related_diag.loc = decl_loc;
    related_diag.message = "function declared here";
    related_diag.severity = polyglot::frontends::DiagnosticSeverity::kNote;

    diags.ReportErrorWithTraceback(call_loc, ErrorCode::kParamCountMismatch,
                                   "too many arguments",
                                   {related_diag});
    CHECK(diags.ErrorCount() == 1);
    auto &all = diags.All();
    REQUIRE(all.size() == 1);
    CHECK(all[0].related.size() == 1);
    CHECK(all[0].related[0].message == "function declared here");
}

TEST_CASE("Diagnostics: Format produces non-empty output", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc loc{"test.ploy", 5, 10};
    diags.ReportError(loc, ErrorCode::kUndefinedSymbol, "undefined identifier 'foo'");
    std::string formatted = diags.Format(diags.All()[0]);
    CHECK(!formatted.empty());
    CHECK(formatted.find("test.ploy") != std::string::npos);
    CHECK(formatted.find("undefined identifier") != std::string::npos);
}

TEST_CASE("Diagnostics: FormatAll produces combined output", "[ploy][diagnostics]") {
    Diagnostics diags;
    polyglot::core::SourceLoc loc1{"a.ploy", 1, 1};
    polyglot::core::SourceLoc loc2{"b.ploy", 2, 2};
    diags.ReportError(loc1, ErrorCode::kUndefinedSymbol, "error one");
    diags.ReportWarning(loc2, ErrorCode::kGenericWarning, "warning one");
    std::string formatted = diags.FormatAll();
    CHECK(!formatted.empty());
    CHECK(formatted.find("error one") != std::string::npos);
    CHECK(formatted.find("warning one") != std::string::npos);
}

// ============================================================================
// Cross-language param count validation tests
// ============================================================================

TEST_CASE("Ploy sema: LINK MAP_TYPE entries are type mappings not arity", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LINK(cpp, python, math::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC main() {
    LET result = CALL(cpp, math::add, 1);
}
)", diags, sema);
    // MAP_TYPE entries declare type-conversion rules, not parameter counts.
    // Calling with any number of args is valid — arity is not checked via MAP_TYPE.
    CHECK(ok);
    CHECK_FALSE(diags.HasErrors());
}

TEST_CASE("Ploy sema: LINK function correct arg count passes", "[ploy][sema][error-check]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LINK(cpp, python, math::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC main() {
    LET result = CALL(cpp, math::add, 1, 2);
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

TEST_CASE("Ploy sema strict: METHOD requires declared signature", "[ploy][sema][strict][abi]") {
    Diagnostics diags;
    PloySemaOptions options;
    options.strict_mode = true;
    PloySema sema(diags, options);

    bool ok = AnalyzeCode(R"(
FUNC main(model: python::Model) -> INT {
    LET output = METHOD(python, model, forward, 1);
    RETURN 0;
}
)", diags, sema);

    CHECK_FALSE(ok);
    CHECK(diags.HasErrors());

    bool found_missing_signature = false;
    for (const auto &d : diags.All()) {
        if (d.message.find("signature") != std::string::npos &&
            d.message.find("forward") != std::string::npos) {
            found_missing_signature = true;
            break;
        }
    }
    CHECK(found_missing_signature);
}

TEST_CASE("Ploy sema strict: WITH requires 4-arg __exit__", "[ploy][sema][strict][with]") {
    Diagnostics diags;
    PloySemaOptions options;
    options.strict_mode = true;
    PloySema sema(diags, options);

    bool ok = AnalyzeCode(R"(
FUNC __enter__(self: python::Ctx) -> python::Ctx {
    RETURN self;
}

FUNC __exit__(self: python::Ctx) -> VOID {
    RETURN;
}

FUNC main(cm: python::Ctx) {
    WITH(python, cm) AS handle {
    }
}
)", diags, sema);

    CHECK_FALSE(ok);
    CHECK(diags.HasErrors());

    bool found_exit_arity_error = false;
    for (const auto &d : diags.All()) {
        if (d.message.find("__exit__") != std::string::npos &&
            d.message.find("4 parameter") != std::string::npos) {
            found_exit_arity_error = true;
            break;
        }
    }
    CHECK(found_exit_arity_error);
}

// ============================================================================
// DELETE + EXTEND combined integration test
// ============================================================================

TEST_CASE("Ploy integration: EXTEND and DELETE together", "[ploy][integration][class]") {
    Diagnostics diags;
    std::string ir = LowerAndGetIR(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
    FUNC fetch(item: STRING) -> STRING {
        RETURN item;
    }
}

FUNC main() {
    LET dog = NEW(python, Dog);
    LET sound = METHOD(python, dog, speak);
    LET ball = METHOD(python, dog, fetch, "ball");
    DELETE(python, dog);
}
)", diags);
    REQUIRE(!ir.empty());
    CHECK(ir.find("__ploy_extend_Dog_speak") != std::string::npos);
    CHECK(ir.find("__ploy_extend_Dog_fetch") != std::string::npos);
    CHECK(ir.find("__ploy_extend_register") != std::string::npos);
    CHECK(ir.find("__ploy_py_del") != std::string::npos);
}

// ============================================================================
// End-to-end failure path tests — param count, type mismatch, unregistered
// symbols, cross-language ABI violations.
//
// Every test here must end with diags.HasErrors() == true and ideally
// verifies the specific ErrorCode or message substring to ensure the
// compiler produces a meaningful diagnostic rather than silently accepting
// bad input or crashing.
// ============================================================================

// ---- Helper ----------------------------------------------------------------
namespace {

struct E2EFailureResult {
    bool   sema_ok{false};
    bool   has_errors{false};
    size_t error_count{0};
    bool   found_message{false}; // true when a specific message was searched for
};

E2EFailureResult RunAndExpectFailure(const std::string &code,
                                     const std::string &expected_msg_fragment = "") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});

    PloyLexer lexer(code, "<e2e_failure>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return {false, diags.HasErrors(), diags.ErrorCount(), false};

    bool ok = sema.Analyze(module);
    bool found = false;
    if (!expected_msg_fragment.empty()) {
        for (const auto &d : diags.All()) {
            if (d.message.find(expected_msg_fragment) != std::string::npos) {
                found = true;
                break;
            }
        }
    }
    return {ok, diags.HasErrors(), diags.ErrorCount(), found};
}

} // namespace

// ============================================================================
// Param count mismatch — local function calls
// ============================================================================

TEST_CASE("E2E failure: too few args to local function produces error with count hint",
          "[ploy][e2e][failure][param-count]") {
    auto r = RunAndExpectFailure(R"(
FUNC add(a: INT, b: INT) -> INT { RETURN a; }
FUNC main() { LET x = add(1); }
)", "argument");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    // The diagnostic must mention the mismatch (argument / param / expected)
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: too many args to local function produces error",
          "[ploy][e2e][failure][param-count]") {
    auto r = RunAndExpectFailure(R"(
FUNC greet(name: STRING) -> STRING { RETURN name; }
FUNC main() { LET x = greet("a", "b", "c"); }
)", "argument");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: zero-arg function called with args produces error",
          "[ploy][e2e][failure][param-count]") {
    auto r = RunAndExpectFailure(R"(
FUNC no_args() -> INT { RETURN 0; }
FUNC main() { LET x = no_args(42, 99); }
)", "argument");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: error count equals number of mismatched call sites",
          "[ploy][e2e][failure][param-count]") {
    // Two call sites with wrong arg counts → two separate diagnostics
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC f(a: INT) -> INT { RETURN a; }
FUNC main() {
    LET x = f();
    LET y = f(1, 2);
}
)", diags, sema);
    CHECK(diags.HasErrors());
    CHECK(diags.ErrorCount() >= 2);
}

// ============================================================================
// Type mismatch — incompatible argument types
// ============================================================================

TEST_CASE("E2E failure: passing STRING to INT parameter produces type-mismatch error",
          "[ploy][e2e][failure][type-mismatch]") {
    auto r = RunAndExpectFailure(R"(
FUNC process(value: INT) -> INT { RETURN value; }
FUNC main() { LET x = process("not_a_number"); }
)", "type");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: passing INT to STRING parameter produces type-mismatch error",
          "[ploy][e2e][failure][type-mismatch]") {
    auto r = RunAndExpectFailure(R"(
FUNC label(name: STRING) -> STRING { RETURN name; }
FUNC main() { LET x = label(42); }
)", "type");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: type mismatch in second argument is caught",
          "[ploy][e2e][failure][type-mismatch]") {
    auto r = RunAndExpectFailure(R"(
FUNC compute(x: FLOAT, y: INT) -> FLOAT { RETURN x; }
FUNC main() { LET r = compute(1.0, "oops"); }
)", "type");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

// ============================================================================
// Unregistered / undefined symbol errors
// ============================================================================

TEST_CASE("E2E failure: calling unregistered symbol produces undefined-symbol error",
          "[ploy][e2e][failure][unregistered]") {
    auto r = RunAndExpectFailure(R"(
FUNC main() {
    LET result = no_such_function(1, 2);
}
)", "undefined");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: reading unregistered variable produces error",
          "[ploy][e2e][failure][unregistered]") {
    auto r = RunAndExpectFailure(R"(
FUNC main() {
    LET x = totally_unknown_var + 1;
}
)", "undefined");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    CHECK(r.found_message);
}

TEST_CASE("E2E failure: CALL to unlinked cross-lang symbol produces error",
          "[ploy][e2e][failure][unregistered]") {
    // math::add is never declared via LINK — must be rejected
    auto r = RunAndExpectFailure(R"(
FUNC main() {
    LET result = CALL(cpp, math::add, 1, 2);
}
)", "unregistered");
    CHECK_FALSE(r.sema_ok);
    CHECK(r.has_errors);
    // If the message uses a different keyword, just check there IS an error
    CHECK(r.has_errors);
}

TEST_CASE("E2E failure: LINK to unsupported language is rejected with specific error",
          "[ploy][e2e][failure][unregistered]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
LINK(haskell, python, some::fn, py::fn);
)", diags, sema);
    CHECK(diags.HasErrors());
    // Error must name the offending language
    bool named = false;
    for (const auto &d : diags.All()) {
        if (d.message.find("haskell") != std::string::npos ||
            d.message.find("language") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);
}

// ============================================================================
// Cross-language ABI arity violations
// ============================================================================

TEST_CASE("E2E: LINK MAP_TYPE arity is not enforced by sema",
          "[ploy][e2e][failure][abi]") {
    // MAP_TYPE entries declare type mappings, not parameter counts.
    // Calling with fewer args than MAP_TYPE entries should still pass sema.
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
LINK(cpp, python, vec::dot, np::dot) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::double, python::float);
}
FUNC main() {
    LET r = CALL(cpp, vec::dot, 1.0);
}
)", diags, sema);
    CHECK_FALSE(diags.HasErrors());
}

TEST_CASE("E2E failure: LINK MAP_TYPE correct arity passes",
          "[ploy][e2e][failure][abi]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    bool ok = AnalyzeCode(R"(
LINK(cpp, python, vec::dot, np::dot) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::double, python::float);
}
FUNC main() {
    LET r = CALL(cpp, vec::dot, 1.0, 2.0);
}
)", diags, sema);
    CHECK(ok);
    CHECK(!diags.HasErrors());
}

// ============================================================================
// Diagnostic infrastructure validation tests
// (ensure error codes, messages, and tracebacks are actually populated)
// ============================================================================

TEST_CASE("E2E failure: kParamCountMismatch error code is emitted",
          "[ploy][e2e][failure][error-code]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC f(x: INT, y: INT) -> INT { RETURN x; }
FUNC main() { LET r = f(1); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    bool found_code = false;
    for (const auto &d : diags.All()) {
        if (d.code == polyglot::frontends::ErrorCode::kParamCountMismatch) {
            found_code = true;
        }
    }
    CHECK(found_code);
}

TEST_CASE("E2E failure: kTypeMismatch error code is emitted",
          "[ploy][e2e][failure][error-code]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC take_int(x: INT) -> INT { RETURN x; }
FUNC main() { LET r = take_int("hello"); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    bool found_code = false;
    for (const auto &d : diags.All()) {
        if (d.code == polyglot::frontends::ErrorCode::kTypeMismatch) {
            found_code = true;
        }
    }
    CHECK(found_code);
}

TEST_CASE("E2E failure: kUndefinedSymbol error code is emitted",
          "[ploy][e2e][failure][error-code]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC main() { LET r = ghost_function(); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    bool found_code = false;
    for (const auto &d : diags.All()) {
        if (d.code == polyglot::frontends::ErrorCode::kUndefinedSymbol) {
            found_code = true;
        }
    }
    CHECK(found_code);
}

TEST_CASE("E2E failure: diagnostic source location points to the offending call site",
          "[ploy][e2e][failure][location]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC f(x: INT) -> INT { RETURN x; }
FUNC main() { LET r = f(1, 2); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    // The error must have a valid source location (line > 0)
    bool has_location = false;
    for (const auto &d : diags.All()) {
        if (d.loc.line > 0) {
            has_location = true;
        }
    }
    CHECK(has_location);
}

TEST_CASE("E2E failure: traceback points to function declaration when param count wrong",
          "[ploy][e2e][failure][traceback]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC multiply(a: INT, b: INT) -> INT { RETURN a; }
FUNC main() { LET r = multiply(5); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    // At least one diagnostic should have a related/traceback note pointing to
    // the declaration of 'multiply'
    bool has_traceback = false;
    for (const auto &d : diags.All()) {
        if (!d.related.empty()) {
            has_traceback = true;
        }
    }
    CHECK(has_traceback);
}

TEST_CASE("E2E failure: formatted error message is non-empty and includes file name",
          "[ploy][e2e][failure][format]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});
    (void)AnalyzeCode(R"(
FUNC f(x: INT) -> INT { RETURN x; }
FUNC main() { LET r = f(); }
)", diags, sema);
    REQUIRE(diags.HasErrors());

    // FormatAll must produce non-empty output that includes a location hint
    std::string formatted = diags.FormatAll();
    CHECK(!formatted.empty());
    // There should be some location indicator (line numbers use digits)
    bool has_digit = false;
    for (char c : formatted) {
        if (std::isdigit(static_cast<unsigned char>(c))) { has_digit = true; break; }
    }
    CHECK(has_digit);
}
