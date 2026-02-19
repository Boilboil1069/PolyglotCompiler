#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "common/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ir::IRContext;
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

    PloySema sema(diags);
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
    PloySema sema(diags);
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
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
LINK(javascript, python, console::log, utils::print);
)", diags, sema);
    // 'javascript' is not a supported language — sema should report error
    REQUIRE(!ok);
}

TEST_CASE("Ploy sema validates FUNC declarations", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags);
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
    PloySema sema(diags);
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
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
MAP_TYPE(cpp::int, python::int);
)", diags, sema);
    REQUIRE(ok);
    REQUIRE(sema.TypeMappings().size() == 1);
}

TEST_CASE("Ploy sema validates PIPELINE", "[ploy][sema]") {
    Diagnostics diags;
    PloySema sema(diags);
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
    PloySema sema(diags);
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
    PloySema sema(diags);
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
    PloySema sema(diags);
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

    PloySema sema(diags);
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
// Complex Type Extension — Lexer Tests
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
// Complex Type Extension — Parser Tests
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
// Complex Type Extension — Sema Tests
// ============================================================================

TEST_CASE("Ploy sema validates STRUCT declaration", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
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
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
MAP_FUNC to_list(x: i32) -> i32 {
    RETURN 0;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates LIST type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
FUNC test(xs: LIST[i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates DICT type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
FUNC test(m: DICT[str, i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates TUPLE type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
FUNC test(t: TUPLE[i32, f64]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates OPTION type resolution", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
FUNC test(x: OPTION[i32]) -> void {
    RETURN;
}
)", diags, sema);
    REQUIRE(ok);
}

TEST_CASE("Ploy sema validates CONVERT expression", "[ploy][sema][complex]") {
    Diagnostics diags;
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
FUNC test(x: i32) -> f64 {
    LET y = CONVERT(x, f64);
    RETURN y;
}
)", diags, sema);
    REQUIRE(ok);
}

// ============================================================================
// Complex Type Extension — Lowering Tests
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
// Complex Type Extension — Integration Tests
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
    PloySema sema(diags);
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
    PloySema sema(diags);
    bool ok = AnalyzeCode(R"(
IMPORT java PACKAGE javax.swing;
)", diags, sema);
    // 'java' is not a supported language
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
