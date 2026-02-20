// ============================================================================
// Macro-Benchmarks
//
// These benchmarks measure full pipeline throughput (lex → parse → sema →
// lower → IR print) and scaling behavior with increasing program size.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <vector>
#include <numeric>
#include <iostream>
#include <algorithm>
#include <iomanip>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "common/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ir::IRContext;

// ============================================================================
// Benchmark infrastructure
// ============================================================================

namespace {

struct MacroStats {
    double mean_ms;
    double min_ms;
    double max_ms;
    double median_ms;
    int iterations;
    size_t ir_size;   // bytes of generated IR
};

MacroStats RunFullPipeline(const std::string &code, int warmup, int runs) {
    // Warmup
    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(code, "<macro>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags);
        sema.Analyze(module);
        IRContext ctx;
        PloyLowering lowering(ctx, diags, sema);
        lowering.Lower(module);
    }

    std::vector<double> samples;
    size_t ir_size = 0;
    for (int i = 0; i < runs; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        Diagnostics diags;
        PloyLexer lexer(code, "<macro>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();

        PloySema sema(diags);
        sema.Analyze(module);

        IRContext ctx;
        PloyLowering lowering(ctx, diags, sema);
        lowering.Lower(module);

        std::ostringstream oss;
        for (const auto &fn : ctx.Functions()) {
            polyglot::ir::PrintFunction(*fn, oss);
        }

        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        ir_size = oss.str().size();
    }

    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {
        sum / static_cast<double>(samples.size()),
        samples.front(),
        samples.back(),
        samples[samples.size() / 2],
        static_cast<int>(samples.size()),
        ir_size
    };
}

void PrintMacroStats(const std::string &label, const MacroStats &s) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  [MACRO] " << label
              << ": mean=" << s.mean_ms << "ms"
              << " min=" << s.min_ms << "ms"
              << " max=" << s.max_ms << "ms"
              << " median=" << s.median_ms << "ms"
              << " ir_bytes=" << s.ir_size
              << " (n=" << s.iterations << ")\n";
}

// --- Program generators ---

std::string GenerateScalingProgram(int funcs) {
    std::ostringstream oss;
    oss << "IMPORT python::lib;\n";
    oss << "IMPORT cpp::proc;\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n";
    oss << "MAP_TYPE(cpp::double, python::float);\n\n";

    for (int i = 0; i < funcs; ++i) {
        oss << "FUNC f" << i << "(x: INT, y: FLOAT) -> FLOAT {\n";
        oss << "    LET a = CALL(python, lib::compute, x);\n";
        oss << "    LET b = CALL(cpp, proc::transform, y);\n";
        oss << "    IF a > 0 {\n";
        oss << "        RETURN b;\n";
        oss << "    } ELSE {\n";
        oss << "        RETURN 0.0;\n";
        oss << "    }\n";
        oss << "}\n\n";
    }
    return oss.str();
}

std::string GenerateOOPProgram(int objects) {
    std::ostringstream oss;
    oss << "IMPORT python::base;\n";
    oss << "MAP_TYPE(cpp::double, python::float);\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n\n";

    for (int i = 0; i < objects; ++i) {
        oss << "EXTEND(python, base::Widget) AS Widget" << i << " {\n";
        oss << "    FUNC action(x: INT) -> INT {\n";
        oss << "        RETURN x + " << i << ";\n";
        oss << "    }\n";
        oss << "}\n\n";
    }

    oss << "FUNC use_widgets() -> INT {\n";
    oss << "    VAR total = 0;\n";
    for (int i = 0; i < objects; ++i) {
        oss << "    LET w" << i << " = NEW(python, Widget" << i << ", \"w\");\n";
        oss << "    LET r" << i << " = METHOD(python, w" << i << ", action, " << i << ");\n";
        oss << "    total = total + r" << i << ";\n";
        oss << "    DELETE(python, w" << i << ");\n";
    }
    oss << "    RETURN total;\n";
    oss << "}\n";
    return oss.str();
}

std::string GenerateComplexPipeline(int stages, int callsPerStage) {
    std::ostringstream oss;
    oss << "IMPORT cpp::proc;\n";
    oss << "IMPORT python::ml;\n";
    oss << "IMPORT rust::data;\n";
    oss << "MAP_TYPE(cpp::double, python::float);\n";
    oss << "MAP_TYPE(cpp::int, python::int);\n";
    oss << "MAP_TYPE(rust::f64, python::float);\n\n";

    oss << "PIPELINE big {\n";
    for (int s = 0; s < stages; ++s) {
        oss << "    FUNC stage" << s << "(x: FLOAT) -> FLOAT {\n";
        oss << "        VAR v = x;\n";
        for (int c = 0; c < callsPerStage; ++c) {
            if (c % 3 == 0) {
                oss << "        LET t" << c << " = CALL(cpp, proc::transform, v);\n";
            } else if (c % 3 == 1) {
                oss << "        LET t" << c << " = CALL(python, ml::predict, v);\n";
            } else {
                oss << "        LET t" << c << " = CALL(rust, data::process, v);\n";
            }
            oss << "        v = v + t" << c << ";\n";
        }
        oss << "        RETURN v;\n";
        oss << "    }\n\n";
    }
    oss << "}\n";
    oss << "EXPORT big AS \"pipeline\";\n";
    return oss.str();
}

constexpr int kWarmup = 3;
constexpr int kRuns   = 10;

} // namespace

// ============================================================================
// Full Pipeline Throughput
// ============================================================================

TEST_CASE("Macro: full pipeline — 10 functions", "[benchmark][macro]") {
    auto code = GenerateScalingProgram(10);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/10-funcs", stats);
    REQUIRE(stats.mean_ms < 500.0);
    REQUIRE(stats.ir_size > 0);
}

TEST_CASE("Macro: full pipeline — 50 functions", "[benchmark][macro]") {
    auto code = GenerateScalingProgram(50);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/50-funcs", stats);
    REQUIRE(stats.mean_ms < 2000.0);
    REQUIRE(stats.ir_size > 0);
}

TEST_CASE("Macro: full pipeline — 100 functions", "[benchmark][macro]") {
    auto code = GenerateScalingProgram(100);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/100-funcs", stats);
    REQUIRE(stats.mean_ms < 5000.0);
    REQUIRE(stats.ir_size > 0);
}

TEST_CASE("Macro: full pipeline — 200 functions", "[benchmark][macro]") {
    auto code = GenerateScalingProgram(200);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/200-funcs", stats);
    REQUIRE(stats.mean_ms < 15000.0);
    REQUIRE(stats.ir_size > 0);
}

// ============================================================================
// Scaling: Linearity Check
// ============================================================================

TEST_CASE("Macro: scaling linearity check", "[benchmark][macro]") {
    // Measure 25, 50, 100 functions and verify roughly linear scaling
    auto s25  = RunFullPipeline(GenerateScalingProgram(25),  kWarmup, kRuns);
    auto s50  = RunFullPipeline(GenerateScalingProgram(50),  kWarmup, kRuns);
    auto s100 = RunFullPipeline(GenerateScalingProgram(100), kWarmup, kRuns);

    PrintMacroStats("Scaling/25-funcs",  s25);
    PrintMacroStats("Scaling/50-funcs",  s50);
    PrintMacroStats("Scaling/100-funcs", s100);

    // 2x input should be at most 4x time (sub-quadratic)
    double ratio_50_25  = s50.mean_ms  / s25.mean_ms;
    double ratio_100_50 = s100.mean_ms / s50.mean_ms;

    std::cout << "  [SCALING] 50/25 ratio: " << ratio_50_25 << "\n";
    std::cout << "  [SCALING] 100/50 ratio: " << ratio_100_50 << "\n";

    // Allow generous bounds: ratio should be under 5x for 2x input
    REQUIRE(ratio_50_25 < 5.0);
    REQUIRE(ratio_100_50 < 5.0);
}

// ============================================================================
// OOP Features
// ============================================================================

TEST_CASE("Macro: OOP features — 10 extended classes", "[benchmark][macro]") {
    auto code = GenerateOOPProgram(10);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("OOP/10-classes", stats);
    REQUIRE(stats.mean_ms < 2000.0);
    REQUIRE(stats.ir_size > 0);
}

TEST_CASE("Macro: OOP features — 25 extended classes", "[benchmark][macro]") {
    auto code = GenerateOOPProgram(25);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("OOP/25-classes", stats);
    REQUIRE(stats.mean_ms < 5000.0);
    REQUIRE(stats.ir_size > 0);
}

// ============================================================================
// Pipeline Complexity
// ============================================================================

TEST_CASE("Macro: pipeline — 10 stages x 5 calls", "[benchmark][macro]") {
    auto code = GenerateComplexPipeline(10, 5);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/10x5", stats);
    REQUIRE(stats.mean_ms < 3000.0);
    REQUIRE(stats.ir_size > 0);
}

TEST_CASE("Macro: pipeline — 20 stages x 10 calls", "[benchmark][macro]") {
    auto code = GenerateComplexPipeline(20, 10);
    auto stats = RunFullPipeline(code, kWarmup, kRuns);
    PrintMacroStats("Pipeline/20x10", stats);
    REQUIRE(stats.mean_ms < 10000.0);
    REQUIRE(stats.ir_size > 0);
}

// ============================================================================
// IR Output Size Sanity
// ============================================================================

TEST_CASE("Macro: IR output size grows with program size", "[benchmark][macro]") {
    auto s10  = RunFullPipeline(GenerateScalingProgram(10),  1, 1);
    auto s50  = RunFullPipeline(GenerateScalingProgram(50),  1, 1);
    auto s100 = RunFullPipeline(GenerateScalingProgram(100), 1, 1);

    std::cout << "  [IR_SIZE] 10 funcs: " << s10.ir_size << " bytes\n";
    std::cout << "  [IR_SIZE] 50 funcs: " << s50.ir_size << " bytes\n";
    std::cout << "  [IR_SIZE] 100 funcs: " << s100.ir_size << " bytes\n";

    // IR size should grow with function count
    REQUIRE(s50.ir_size > s10.ir_size);
    REQUIRE(s100.ir_size > s50.ir_size);
    // Approximately linear: 5x functions → at least 3x IR
    REQUIRE(s50.ir_size > s10.ir_size * 3);
}
