// ============================================================================
// Integration Tests  -- Performance / Stress Tests
//
// These tests verify that the compiler handles larger or more complex
// programs without crashing, and that performance remains acceptable.
// They exercise the full pipeline with heavier workloads.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <iostream>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
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

struct PerfResult {
    bool success;
    std::string ir_text;
    double elapsed_ms;
};

PerfResult CompileTimed(const std::string &code) {
    auto start = std::chrono::high_resolution_clock::now();

    Diagnostics diags;
    PloyLexer lexer(code, "<perf>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) {
        for (const auto &d : diags.All()) std::cerr << "[PERF-DIAG] " << d.message << "\n";
        return {false, "", 0.0};
    }

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) {
        for (const auto &d : diags.All()) std::cerr << "[PERF-DIAG] " << d.message << "\n";
        return {false, "", 0.0};
    }

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) {
        for (const auto &d : diags.All()) std::cerr << "[PERF-DIAG] " << d.message << "\n";
        return {false, "", 0.0};
    }

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    return {true, oss.str(), elapsed};
}

// Generate a program with N functions
std::string GenerateNFunctions(int n) {
    std::ostringstream oss;
    oss << "IMPORT python::lib;\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n";
    for (int i = 0; i < n; ++i) {
        oss << "FUNC func_" << i << "(x" << i << ": INT) -> INT {\n";
        oss << "    LET r" << i << " = CALL(python, lib::compute, x" << i << ");\n";
        oss << "    RETURN r" << i << ";\n";
        oss << "}\n\n";
    }
    return oss.str();
}

// Generate a deeply nested control flow program
std::string GenerateDeepNesting(int depth) {
    std::ostringstream oss;
    oss << "IMPORT python::lib;\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n";
    oss << "FUNC deep(x: INT) -> INT {\n";
    oss << "    VAR result = x;\n";
    for (int i = 0; i < depth; ++i) {
        oss << std::string(4 * (i + 1), ' ') << "IF result > 0 {\n";
        oss << std::string(4 * (i + 2), ' ') << "result = result - 1;\n";
    }
    for (int i = depth - 1; i >= 0; --i) {
        oss << std::string(4 * (i + 1), ' ') << "}\n";
    }
    oss << "    RETURN result;\n";
    oss << "}\n";
    return oss.str();
}

// Generate a program with N CALL statements in one function
std::string GenerateManyCalls(int n) {
    std::ostringstream oss;
    oss << "IMPORT python::lib;\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n";
    oss << "FUNC many_calls(x: INT) -> INT {\n";
    oss << "    VAR acc = x;\n";
    for (int i = 0; i < n; ++i) {
        oss << "    LET v" << i << " = CALL(python, lib::compute, acc);\n";
        oss << "    acc = acc + v" << i << ";\n";
    }
    oss << "    RETURN acc;\n";
    oss << "}\n";
    return oss.str();
}

// Generate a pipeline with N stages
std::string GeneratePipeline(int stages) {
    std::ostringstream oss;
    oss << "IMPORT cpp::proc;\n";
    oss << "IMPORT python::lib;\n";
    oss << "MAP_TYPE(cpp::double, python::float);\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n\n";

    oss << "PIPELINE big_pipeline {\n";
    for (int i = 0; i < stages; ++i) {
        oss << "    FUNC stage_" << i << "(x" << i << ": FLOAT) -> FLOAT {\n";
        if (i % 2 == 0) {
            oss << "        LET r" << i << " = CALL(cpp, proc::transform, x" << i << ");\n";
        } else {
            oss << "        LET r" << i << " = CALL(python, lib::compute, x" << i << ");\n";
        }
        oss << "        RETURN r" << i << ";\n";
        oss << "    }\n\n";
    }
    oss << "}\n";
    oss << "EXPORT big_pipeline AS \"pipeline\";\n";
    return oss.str();
}

} // namespace

// ============================================================================
// Stress: Many Functions
// ============================================================================

TEST_CASE("Perf: compile 50 functions", "[integration][perf]") {
    auto code = GenerateNFunctions(50);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    // Should contain all function names
    REQUIRE(result.ir_text.find("func_0") != std::string::npos);
    REQUIRE(result.ir_text.find("func_49") != std::string::npos);
    // Reasonable time for 50 functions (under 5 seconds)
    REQUIRE(result.elapsed_ms < 5000.0);
}

TEST_CASE("Perf: compile 100 functions", "[integration][perf]") {
    auto code = GenerateNFunctions(100);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("func_99") != std::string::npos);
    REQUIRE(result.elapsed_ms < 10000.0);
}

// ============================================================================
// Stress: Deep Control Flow Nesting
// ============================================================================

TEST_CASE("Perf: 10-level nested IF", "[integration][perf]") {
    auto code = GenerateDeepNesting(10);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.elapsed_ms < 2000.0);
}

TEST_CASE("Perf: 20-level nested IF", "[integration][perf]") {
    auto code = GenerateDeepNesting(20);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.elapsed_ms < 5000.0);
}

// ============================================================================
// Stress: Many Cross-Language CALLs
// ============================================================================

TEST_CASE("Perf: 50 CALL statements in one function", "[integration][perf]") {
    auto code = GenerateManyCalls(50);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.elapsed_ms < 5000.0);
}

TEST_CASE("Perf: 100 CALL statements in one function", "[integration][perf]") {
    auto code = GenerateManyCalls(100);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.elapsed_ms < 10000.0);
}

// ============================================================================
// Stress: Large Pipeline
// ============================================================================

TEST_CASE("Perf: 20-stage pipeline", "[integration][perf]") {
    auto code = GeneratePipeline(20);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    // PIPELINE functions are emitted under the pipeline name
    REQUIRE(result.ir_text.find("__ploy_pipeline_big_pipeline") != std::string::npos);
    REQUIRE(result.elapsed_ms < 5000.0);
}

TEST_CASE("Perf: 50-stage pipeline", "[integration][perf]") {
    auto code = GeneratePipeline(50);
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("__ploy_pipeline_big_pipeline") != std::string::npos);
    REQUIRE(result.elapsed_ms < 10000.0);
}

// ============================================================================
// Stress: Complex Mixed Program
// ============================================================================

TEST_CASE("Perf: complex mixed program with all features", "[integration][perf]") {
    std::string code = R"(
        IMPORT cpp::engine;
        IMPORT python PACKAGE ai;

        LINK(cpp, python, engine::render, ai::decide) {
            MAP_TYPE(cpp::double, python::float);
        }

        MAP_TYPE(cpp::double, python::float);
        MAP_TYPE(cpp::int, python::int);

        STRUCT GameState {
            tick_gs: INT;
            fps_gs: FLOAT;
            alive_gs: BOOL;
        }

        EXTEND(python, ai::Agent) AS SmartAgent {
            FUNC think(state_t: INT) -> INT {
                IF state_t > 50 {
                    RETURN 1;
                } ELSE {
                    RETURN 0;
                }
            }

            FUNC reward_fn(score_r: FLOAT) -> FLOAT {
                RETURN score_r * 0.99;
            }
        }

        FUNC game_loop(max_ticks: INT) -> INT {
            LET world_g = NEW(cpp, engine::World, 1000, 1000);
            LET agent_g = NEW(python, SmartAgent, "bot1");

            VAR tick_g = 0;
            WHILE tick_g < max_ticks {
                LET state_g = METHOD(cpp, world_g, get_state);
                LET action_g = METHOD(python, agent_g, think, state_g);
                tick_g = tick_g + 1;

                IF tick_g > 100 {
                    BREAK;
                }
            }

            DELETE(python, agent_g);
            DELETE(cpp, world_g);
            RETURN tick_g;
        }

        EXPORT game_loop AS "loop";
    )";
    auto result = CompileTimed(code);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("game_loop") != std::string::npos);
    REQUIRE(result.ir_text.find("think") != std::string::npos);
    REQUIRE(result.ir_text.find("reward") != std::string::npos);
    // Should compile in reasonable time
    REQUIRE(result.elapsed_ms < 5000.0);
}

// ============================================================================
// Stress: Lexer token throughput
// ============================================================================

TEST_CASE("Perf: lex 200 functions quickly", "[integration][perf]") {
    auto code = GenerateNFunctions(200);
    auto start = std::chrono::high_resolution_clock::now();

    Diagnostics diags;
    PloyLexer lexer(code, "<lex_perf>");
    int count = 0;
    while (true) {
        auto tok = lexer.NextToken();
        ++count;
        if (tok.kind == TokenKind::kEndOfFile) break;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    REQUIRE(count > 1000);   // Should produce many tokens
    REQUIRE(ms < 2000.0);    // Lexing should be fast
}

// ============================================================================
// Stress: Parser throughput
// ============================================================================

TEST_CASE("Perf: parse 200 functions quickly", "[integration][perf]") {
    auto code = GenerateNFunctions(200);
    auto start = std::chrono::high_resolution_clock::now();

    Diagnostics diags;
    PloyLexer lexer(code, "<parse_perf>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    REQUIRE(module != nullptr);
    REQUIRE_FALSE(diags.HasErrors());
    REQUIRE(ms < 3000.0);
}
