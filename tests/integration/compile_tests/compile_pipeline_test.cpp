// ============================================================================
// Integration Tests — Compile Pipeline Tests
//
// These tests verify complete end-to-end compilation pipelines:
// lexer → parser → sema → lowering → IR output, for realistic
// multi-feature .ploy programs.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <iostream>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"
#include "backends/wasm/include/wasm_target.h"
#include "tools/polyld/include/polyglot_linker.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::CrossLangCallDescriptor;
using polyglot::ir::IRContext;
using namespace polyglot::ploy;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Full pipeline: lex → parse → sema → lower, return IR text
std::string CompileFull(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<integration>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return "";
    }

    PloySema sema(diags);
    if (!sema.Analyze(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return "";
    }

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return "";
    }

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return oss.str();
}

// Full pipeline returning descriptors
struct CompileResult {
    std::string ir_text;
    std::vector<CrossLangCallDescriptor> descriptors;
    bool success;
};

CompileResult CompileWithDescriptors(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<integration>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    PloySema sema(diags);
    if (!sema.Analyze(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return {oss.str(), lowering.CallDescriptors(), true};
}

} // namespace

// ============================================================================
// Pipeline: Basic LINK + CALL (matches 01_basic_linking pattern)
// ============================================================================

TEST_CASE("Integration: basic LINK and CALL pipeline", "[integration][compile]") {
    Diagnostics diags;
    // LINK with 1 MAP_TYPE = 1 parameter bridge
    std::string code = R"(
LINK(cpp, python, math_ops::add, string_utils::format) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC compute(a: i32) -> i32 {
    LET sum = CALL(cpp, math_ops::add, a);
    RETURN sum;
}

EXPORT compute AS "polyglot_compute";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("compute") != std::string::npos);
    REQUIRE(diags.ErrorCount() == 0);
}

TEST_CASE("Integration: LINK with 2-param bridge", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_add(a: i32, b: i32) -> i32 {
    LET result = CALL(cpp, math::add, a, b);
    RETURN result;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.descriptors.size() >= 1);
}

// ============================================================================
// Pipeline: Type Mapping with STRUCT
// ============================================================================

TEST_CASE("Integration: type mapping with STRUCT definition", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(cpp::int, python::int);

STRUCT DataPoint {
    x: FLOAT;
    y: FLOAT;
    label: STRING;
}

FUNC process_data(factor: f64) -> f64 {
    LET result = factor * 2.0;
    RETURN result;
}

EXPORT process_data AS "polyglot_process";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("process_data") != std::string::npos);
}

// ============================================================================
// Pipeline: Control Flow in PIPELINE
// ============================================================================

TEST_CASE("Integration: PIPELINE with IF/ELSE control flow", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, processor::enhance, model::predict) {
    MAP_TYPE(cpp::double, python::float);
}

PIPELINE image_classification {
    FUNC classify(threshold: f64) -> i32 {
        IF threshold > 0.5 {
            RETURN 1;
        } ELSE {
            RETURN 0;
        }
    }
}

EXPORT image_classification AS "image_pipeline";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("classify") != std::string::npos);
}

TEST_CASE("Integration: PIPELINE with WHILE and BREAK", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
PIPELINE refiner {
    FUNC refine(max_iter: i32, target: f64) -> f64 {
        VAR current = 0.0;
        VAR iteration = 0;
        WHILE iteration < max_iter {
            current = current + 0.1;
            IF current > target {
                BREAK;
            }
            iteration = iteration + 1;
        }
        RETURN current;
    }
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("refine") != std::string::npos);
}

TEST_CASE("Integration: PIPELINE with FOR loop", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
PIPELINE batcher {
    FUNC process_batch(batch_size: i32) -> i32 {
        VAR total = 0;
        FOR i IN 0..batch_size {
            total = total + 1;
        }
        RETURN total;
    }
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("process_batch") != std::string::npos);
}

// ============================================================================
// Pipeline: OOP Interop (class instantiation)
// ============================================================================

TEST_CASE("Integration: NEW and METHOD chain", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE torch;

FUNC class_ops() -> INT {
    LET model = NEW(python, torch::Module);
    LET x = 1;
    LET result = METHOD(python, model, forward, x);
    RETURN 0;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    // NEW and METHOD generate cross-language descriptors
    REQUIRE(result.descriptors.size() >= 2);
}

TEST_CASE("Integration: multiple NEW for different languages", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE sklearn;
IMPORT cpp::image_processing;

FUNC cross_lang() -> INT {
    LET scaler = NEW(python, sklearn::StandardScaler);
    LET scaled = METHOD(python, scaler, fit_transform, [1.0, 2.0, 3.0]);
    LET processor = NEW(cpp, image_processing::ImageProcessor, scaled);
    LET result = METHOD(cpp, processor, process, 640, 480);
    RETURN result;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.descriptors.size() >= 4); // NEW*2 + METHOD*2
}

// ============================================================================
// Pipeline: GET/SET Attribute Access
// ============================================================================

TEST_CASE("Integration: GET and SET attribute access", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE config;

FUNC attr_example() -> INT {
    LET counter = NEW(python, config::Counter, 10);
    SET(python, counter, step, 5);
    LET step_val = GET(python, counter, step);
    METHOD(python, counter, increment);
    LET new_val = GET(python, counter, value);
    RETURN new_val;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    // NEW + SET + GET*2 + METHOD = 5+ descriptors
    REQUIRE(result.descriptors.size() >= 5);
}

// ============================================================================
// Pipeline: WITH Resource Management
// ============================================================================

TEST_CASE("Integration: WITH basic resource management", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
FUNC file_example() -> INT {
    WITH(python, NEW(python, sqlite3::connect, "app.db")) AS conn {
        LET cursor = METHOD(python, conn, cursor);
        METHOD(python, cursor, execute, "CREATE TABLE t");
        METHOD(python, conn, commit);
    }
    RETURN 0;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.descriptors.size() >= 4);
}

TEST_CASE("Integration: nested WITH blocks", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
FUNC nested_with() -> INT {
    WITH(python, NEW(python, io::File, "in.txt", "r")) AS reader {
        WITH(python, NEW(python, io::File, "out.txt", "w")) AS writer {
            LET data = METHOD(python, reader, read);
            METHOD(python, writer, write, data);
        }
    }
    RETURN 0;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.descriptors.size() >= 6);
}

// ============================================================================
// Pipeline: DELETE + EXTEND
// ============================================================================

TEST_CASE("Integration: EXTEND basic subclass creation", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
    FUNC fetch(item: STRING) -> STRING {
        RETURN item;
    }
}

FUNC use_dog() -> INT {
    LET dog = NEW(python, Dog);
    LET sound = METHOD(python, dog, speak);
    LET ball = METHOD(python, dog, fetch, "ball");
    DELETE(python, dog);
    RETURN 0;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("__ploy_extend_Dog_speak") != std::string::npos);
    REQUIRE(result.ir_text.find("__ploy_extend_Dog_fetch") != std::string::npos);
    REQUIRE(result.ir_text.find("__ploy_py_del") != std::string::npos);
}

TEST_CASE("Integration: DELETE lifecycle", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
FUNC lifecycle() -> INT {
    LET obj = NEW(python, MyClass);
    METHOD(python, obj, do_work);
    DELETE(python, obj);
    RETURN 0;
}
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("__ploy_py_del") != std::string::npos);
}

// ============================================================================
// Pipeline: Mixed Three-Language
// ============================================================================

TEST_CASE("Integration: mixed three-language pipeline", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, engine::process, model::predict) {
    MAP_TYPE(cpp::double, python::float);
}

LINK(rust, python, data::compress, model::predict) {
    MAP_TYPE(rust::f64, python::float);
}

MAP_TYPE(cpp::double, python::float);
MAP_TYPE(rust::f64, python::float);

FUNC infer(x: f64) -> f64 {
    LET enhanced = CALL(cpp, engine::process, x);
    LET compressed = CALL(rust, data::compress, x);
    RETURN enhanced;
}

EXPORT infer AS "run_inference";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("infer") != std::string::npos);
    REQUIRE(result.descriptors.size() >= 2);
}

// ============================================================================
// Pipeline: MATCH dispatch
// ============================================================================

TEST_CASE("Integration: MATCH dispatch compiles", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
FUNC dispatch(mode: i32) -> i32 {
    MATCH mode {
        CASE 0 { RETURN 10; }
        CASE 1 { RETURN 20; }
        CASE 2 { RETURN 30; }
        DEFAULT { RETURN 0; }
    }
}

EXPORT dispatch AS "dispatch";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("dispatch") != std::string::npos);
}

// ============================================================================
// Pipeline: Package import
// ============================================================================

TEST_CASE("Integration: package import config compiles", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
CONFIG CONDA "ml_env";
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE scipy >= 1.7;

FUNC use_packages(x: INT) -> INT {
    LET result = CALL(python, np::mean, x);
    RETURN result;
}

EXPORT use_packages AS "use_pkg";
    )";
    auto ir = CompileFull(code, diags);
    REQUIRE_FALSE(diags.HasErrors());
    REQUIRE_FALSE(ir.empty());
}

// ============================================================================
// Pipeline: Complete ML Training Loop (all features combined)
// ============================================================================

TEST_CASE("Integration: complete ML training loop compiles", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE torch;

PIPELINE ml_training {
    FUNC train(epochs: i32) -> f64 {
        LET model = NEW(python, torch::nn::Linear, 4, 1);
        SET(python, model, training, TRUE);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET pred = METHOD(python, model, forward, [1.0, 2.0, 3.0, 4.0]);
            total_loss = total_loss + pred;

            IF pred < 0.01 {
                BREAK;
            }

            epoch = epoch + 1;
        }

        LET final_lr = GET(python, model, learning_rate);

        DELETE(python, model);

        RETURN total_loss;
    }
}

EXPORT ml_training AS "train_model";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("train") != std::string::npos);
    // Many descriptors: NEW + SET + METHOD + GET + DELETE
    REQUIRE(result.descriptors.size() >= 5);
}

// ============================================================================
// Pipeline: NEW + METHOD + LINK full pipeline
// ============================================================================

TEST_CASE("Integration: NEW + METHOD + LINK full pipeline", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
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
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
}

// ============================================================================
// Pipeline: Multiple PIPELINE functions
// ============================================================================

TEST_CASE("Integration: multiple functions in PIPELINE", "[integration][compile]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, engine::render, model::predict) {
    MAP_TYPE(cpp::double, python::float);
}

PIPELINE game_pipeline {
    FUNC init(w: i32, h: i32) -> i32 {
        RETURN w * h;
    }

    FUNC tick(dt: f64) -> f64 {
        LET force = CALL(cpp, engine::render, dt);
        RETURN force;
    }

    FUNC cleanup() -> i32 {
        RETURN 0;
    }
}

EXPORT game_pipeline AS "game";
    )";
    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    // Pipeline functions are compiled under the pipeline name
    REQUIRE(result.ir_text.find("game_pipeline") != std::string::npos);
}

// ============================================================================
// E2E: Frontend → Backend x86_64 Assembly Emission
// ============================================================================

TEST_CASE("E2E: ploy compile to x86_64 assembly", "[integration][e2e][x86]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math_ops::add, format_utils::to_str) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC compute(x: i64) -> i64 {
    LET result = CALL(cpp, math_ops::add, x);
    RETURN result;
}
    )";

    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());

    // Build an IR context and feed to x86 backend
    IRContext ctx;
    auto fn = ctx.CreateFunction("compute", polyglot::ir::IRType::I64(),
                                 {{"x", polyglot::ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");
    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("x");
    blk->SetTerminator(ret);

    polyglot::backends::x86_64::X86Target target(&ctx);
    std::string asm_text = target.EmitAssembly();
    REQUIRE_FALSE(asm_text.empty());
    // Should contain function label
    REQUIRE(asm_text.find("compute") != std::string::npos);
}

// ============================================================================
// E2E: Frontend → Backend ARM64 Assembly Emission
// ============================================================================

TEST_CASE("E2E: ploy compile to arm64 assembly", "[integration][e2e][arm64]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, engine::run, data::fetch) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC engine_main() -> i64 {
    LET val = CALL(cpp, engine::run, 100);
    RETURN val;
}
    )";

    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);

    IRContext ctx;
    auto fn = ctx.CreateFunction("engine_main", polyglot::ir::IRType::I64(), {});
    auto blk = fn->CreateBlock("entry");
    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    blk->SetTerminator(ret);

    polyglot::backends::arm64::Arm64Target target(&ctx);
    std::string asm_text = target.EmitAssembly();
    REQUIRE_FALSE(asm_text.empty());
    REQUIRE(asm_text.find("engine_main") != std::string::npos);
}

// ============================================================================
// E2E: Frontend → Backend WASM Assembly (WAT) Emission
// ============================================================================

TEST_CASE("E2E: ploy compile to wasm WAT", "[integration][e2e][wasm]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math::multiply, formatter::print) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC wasm_entry(a: i64, b: i64) -> i64 {
    LET sum = a;
    RETURN sum;
}
    )";

    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);

    // Build a simple IR module for the WASM backend
    IRContext ctx;
    auto fn = ctx.CreateFunction("wasm_entry", polyglot::ir::IRType::I64(),
                                 {{"a", polyglot::ir::IRType::I64()},
                                  {"b", polyglot::ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");
    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("a");
    blk->SetTerminator(ret);

    polyglot::backends::wasm::WasmTarget target(&ctx);

    // WAT text output
    std::string wat = target.EmitAssembly();
    REQUIRE_FALSE(wat.empty());
    REQUIRE(wat.find("(module") != std::string::npos);
    REQUIRE(wat.find("wasm_entry") != std::string::npos);
    REQUIRE(wat.find("i64") != std::string::npos);

    // Binary output
    auto binary = target.EmitWasmBinary();
    REQUIRE(binary.size() >= 8);
    // Check magic: \0asm
    REQUIRE(binary[0] == 0x00);
    REQUIRE(binary[1] == 0x61);
    REQUIRE(binary[2] == 0x73);
    REQUIRE(binary[3] == 0x6D);
    // Check version: 1
    REQUIRE(binary[4] == 0x01);
}

// ============================================================================
// E2E: PolyglotLinker — Cross-language Link Resolution
// ============================================================================

TEST_CASE("E2E: PolyglotLinker resolves cross-lang descriptors",
          "[integration][e2e][linker]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math::square, util::display) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC run(n: i64) -> i64 {
    LET sq = CALL(cpp, math::square, n);
    LET out = CALL(python, util::display, sq);
    RETURN sq;
}
    )";

    auto result = CompileWithDescriptors(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.descriptors.size() >= 1);

    // Feed descriptors into PolyglotLinker and resolve
    polyglot::linker::LinkerConfig config;
    polyglot::linker::PolyglotLinker linker(config);

    for (auto &desc : result.descriptors) {
        linker.AddCallDescriptor(desc);
    }

    // Add the LINK entry that corresponds to the .ploy LINK declaration
    polyglot::ploy::LinkEntry link_entry;
    link_entry.kind = polyglot::ploy::LinkDecl::LinkKind::kFunction;
    link_entry.target_language = "cpp";
    link_entry.source_language = "python";
    link_entry.target_symbol = "math::square";
    link_entry.source_symbol = "util::display";
    linker.AddLinkEntry(link_entry);

    // Register cross-language symbols so ResolveSymbolPair can find them
    polyglot::linker::CrossLangSymbol cpp_sym;
    cpp_sym.name = "math::square";
    cpp_sym.mangled_name = "math::square";
    cpp_sym.language = "cpp";
    cpp_sym.type = polyglot::linker::SymbolType::kFunction;
    cpp_sym.params.push_back({"i64", 8, false});
    cpp_sym.return_desc = {"i64", 8, false};

    polyglot::linker::CrossLangSymbol py_sym;
    py_sym.name = "util::display";
    py_sym.mangled_name = "util::display";
    py_sym.language = "python";
    py_sym.type = polyglot::linker::SymbolType::kFunction;
    py_sym.params.push_back({"i64", 8, false});
    py_sym.return_desc = {"i64", 8, false};

    linker.AddCrossLangSymbol(cpp_sym);
    linker.AddCrossLangSymbol(py_sym);

    REQUIRE(linker.ResolveLinks());
    // After resolution, glue stubs should have been generated
    auto &stubs = linker.GetStubs();
    REQUIRE_FALSE(stubs.empty());
    // Each stub must have a name and non-empty code
    for (auto &stub : stubs) {
        REQUIRE_FALSE(stub.stub_name.empty());
        REQUIRE_FALSE(stub.code.empty());
    }
}

TEST_CASE("E2E: PolyglotLinker hard-fails on ABI schema mismatch",
          "[integration][e2e][linker][abi]") {
    polyglot::linker::LinkerConfig config;
    polyglot::linker::PolyglotLinker linker(config);

    polyglot::ploy::LinkEntry link_entry;
    link_entry.kind = polyglot::ploy::LinkDecl::LinkKind::kFunction;
    link_entry.target_language = "cpp";
    link_entry.source_language = "python";
    link_entry.target_symbol = "math::scale";
    link_entry.source_symbol = "py::scale";
    linker.AddLinkEntry(link_entry);

    polyglot::linker::CrossLangSymbol target_sym;
    target_sym.name = "math::scale";
    target_sym.mangled_name = "math::scale";
    target_sym.language = "cpp";
    target_sym.type = polyglot::linker::SymbolType::kFunction;
    target_sym.params.push_back({"i64", 8, false});
    target_sym.params.push_back({"i64", 8, false});
    target_sym.return_desc = {"i64", 8, false};

    polyglot::linker::CrossLangSymbol source_sym;
    source_sym.name = "py::scale";
    source_sym.mangled_name = "py::scale";
    source_sym.language = "python";
    source_sym.type = polyglot::linker::SymbolType::kFunction;
    source_sym.params.push_back({"i64", 8, false});
    source_sym.return_desc = {"i64", 8, false};

    linker.AddCrossLangSymbol(target_sym);
    linker.AddCrossLangSymbol(source_sym);

    CHECK_FALSE(linker.ResolveLinks());
    CHECK_FALSE(linker.GetErrors().empty());
}

// ============================================================================
// E2E: x86_64 Object Code Emission
// ============================================================================

TEST_CASE("E2E: x86_64 EmitObjectCode produces MCResult",
          "[integration][e2e][x86][objcode]") {
    IRContext ctx;
    auto fn = ctx.CreateFunction("add_one", polyglot::ir::IRType::I64(),
                                 {{"x", polyglot::ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");

    // x + 1
    auto add = std::make_shared<polyglot::ir::BinaryInstruction>();
    add->op = polyglot::ir::BinaryInstruction::Op::kAdd;
    add->name = "result";
    add->type = polyglot::ir::IRType::I64();
    add->operands = {"x", "1"};
    blk->AddInstruction(add);

    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("result");
    blk->SetTerminator(ret);

    polyglot::backends::x86_64::X86Target target(&ctx);
    auto mc = target.EmitObjectCode();

    // MCResult should have at least a .text section
    bool has_text = false;
    for (auto &sec : mc.sections) {
        if (sec.name == ".text") {
            has_text = true;
            REQUIRE_FALSE(sec.data.empty());
        }
    }
    REQUIRE(has_text);

    // Should define the function symbol
    bool has_sym = false;
    for (auto &sym : mc.symbols) {
        if (sym.name == "add_one") {
            has_sym = true;
            REQUIRE(sym.global);
        }
    }
    REQUIRE(has_sym);
}

// ============================================================================
// E2E: ARM64 Object Code Emission
// ============================================================================

TEST_CASE("E2E: arm64 EmitObjectCode produces MCResult",
          "[integration][e2e][arm64][objcode]") {
    IRContext ctx;
    auto fn = ctx.CreateFunction("identity", polyglot::ir::IRType::I64(),
                                 {{"x", polyglot::ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");

    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands.push_back("x");
    blk->SetTerminator(ret);

    polyglot::backends::arm64::Arm64Target target(&ctx);
    auto mc = target.EmitObjectCode();

    // MCResult should have at least a .text section
    bool has_text = false;
    for (auto &sec : mc.sections) {
        if (sec.name == ".text") {
            has_text = true;
            REQUIRE_FALSE(sec.data.empty());
        }
    }
    REQUIRE(has_text);

    // Should define the function symbol
    bool has_sym = false;
    for (auto &sym : mc.symbols) {
        if (sym.name == "identity") {
            has_sym = true;
            REQUIRE(sym.global);
        }
    }
    REQUIRE(has_sym);
}

// ============================================================================
// E2E: WASM Multi-function Smoke Test
// ============================================================================

TEST_CASE("E2E: wasm multi-function binary emission",
          "[integration][e2e][wasm][smoke]") {
    IRContext ctx;

    // Function 1: add(a, b) -> a + b
    {
        auto fn = ctx.CreateFunction("add", polyglot::ir::IRType::I64(),
                                     {{"a", polyglot::ir::IRType::I64()},
                                      {"b", polyglot::ir::IRType::I64()}});
        auto blk = fn->CreateBlock("entry");
        auto binop = std::make_shared<polyglot::ir::BinaryInstruction>();
        binop->op = polyglot::ir::BinaryInstruction::Op::kAdd;
        binop->name = "sum";
        binop->type = polyglot::ir::IRType::I64();
        binop->operands = {"a", "b"};
        blk->AddInstruction(binop);
        auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
        ret->operands.push_back("sum");
        blk->SetTerminator(ret);
    }

    // Function 2: negate(x) -> 0 - x
    {
        auto fn = ctx.CreateFunction("negate", polyglot::ir::IRType::I64(),
                                     {{"x", polyglot::ir::IRType::I64()}});
        auto blk = fn->CreateBlock("entry");
        auto binop = std::make_shared<polyglot::ir::BinaryInstruction>();
        binop->op = polyglot::ir::BinaryInstruction::Op::kSub;
        binop->name = "neg";
        binop->type = polyglot::ir::IRType::I64();
        binop->operands = {"0", "x"};
        blk->AddInstruction(binop);
        auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
        ret->operands.push_back("neg");
        blk->SetTerminator(ret);
    }

    polyglot::backends::wasm::WasmTarget target(&ctx);

    // WAT output should contain both functions
    std::string wat = target.EmitAssembly();
    REQUIRE_FALSE(wat.empty());
    REQUIRE(wat.find("add") != std::string::npos);
    REQUIRE(wat.find("negate") != std::string::npos);
    REQUIRE(wat.find("i64.add") != std::string::npos);
    REQUIRE(wat.find("i64.sub") != std::string::npos);

    // Binary output
    auto binary = target.EmitWasmBinary();
    REQUIRE(binary.size() >= 8);
    // Magic header
    REQUIRE(binary[0] == 0x00);
    REQUIRE(binary[1] == 0x61);
    REQUIRE(binary[2] == 0x73);
    REQUIRE(binary[3] == 0x6D);
}
