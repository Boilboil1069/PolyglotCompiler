// ============================================================================
// Micro-Benchmarks
//
// These benchmarks measure individual compiler stage throughput:
// lexer, parser, sema, and lowering — each in isolation.
// Results are printed as ops/sec and ms/op for analysis.
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
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ir::IRContext;

// ============================================================================
// Benchmark infrastructure
// ============================================================================

namespace {

struct BenchStats {
    double mean_ms;
    double min_ms;
    double max_ms;
    double median_ms;
    int iterations;
};

BenchStats ComputeStats(std::vector<double> &samples) {
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {
        sum / static_cast<double>(samples.size()),
        samples.front(),
        samples.back(),
        samples[samples.size() / 2],
        static_cast<int>(samples.size())
    };
}

void PrintStats(const std::string &label, const BenchStats &s) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  [BENCH] " << label
              << ": mean=" << s.mean_ms << "ms"
              << " min=" << s.min_ms << "ms"
              << " max=" << s.max_ms << "ms"
              << " median=" << s.median_ms << "ms"
              << " (n=" << s.iterations << ")\n";
}

// Standard input programs for benchmarking
const std::string kSmallProgram = R"(
    IMPORT python::lib;
    MAP_TYPE(cpp::int, python::int);

    FUNC add(a: INT, b: INT) -> INT {
        LET r = CALL(python, lib::compute, a);
        RETURN r;
    }
)";

const std::string kMediumProgram = R"(
    IMPORT cpp::engine;
    IMPORT python::model;

    LINK(cpp, python, engine::process, model::predict) {
        MAP_TYPE(cpp::double, python::float);
        MAP_TYPE(cpp::int, python::int);
    }

    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);

    STRUCT Config {
        width: INT;
        height: INT;
        scale: FLOAT;
    }

    PIPELINE data_flow {
        FUNC stage1(x: FLOAT) -> FLOAT {
            LET r = CALL(cpp, engine::process, x);
            RETURN r;
        }

        FUNC stage2(x: FLOAT) -> FLOAT {
            LET r = CALL(python, model::predict, x);
            IF r > 0.5 {
                RETURN r;
            } ELSE {
                RETURN 0.0;
            }
        }

        FUNC stage3(n: INT) -> INT {
            VAR acc = 0;
            FOR i IN 0..n {
                acc = acc + 1;
            }
            RETURN acc;
        }
    }

    FUNC main_entry(data: LIST(FLOAT), count: INT) -> FLOAT {
        LET obj = NEW(python, model::Net, 4, 8);
        SET(python, obj, lr, 0.01);
        LET pred = METHOD(python, obj, forward, data);
        LET lr = GET(python, obj, lr);
        DELETE(python, obj);
        RETURN pred;
    }

    EXPORT data_flow AS "pipeline";
    EXPORT main_entry AS "entry";
)";

std::string GenerateLargeProgram(int funcs) {
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
        oss << "        VAR result = b;\n";
        oss << "        FOR i IN 0..x {\n";
        oss << "            result = result + 1.0;\n";
        oss << "        }\n";
        oss << "        RETURN result;\n";
        oss << "    } ELSE {\n";
        oss << "        RETURN b;\n";
        oss << "    }\n";
        oss << "}\n\n";
    }
    return oss.str();
}

constexpr int kWarmupRuns  = 3;
constexpr int kBenchRuns   = 20;

} // namespace

// ============================================================================
// Lexer micro-benchmarks
// ============================================================================

TEST_CASE("Micro: lexer throughput — small program", "[benchmark][micro]") {
    // Warmup
    for (int i = 0; i < kWarmupRuns; ++i) {
        PloyLexer lexer(kSmallProgram, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        PloyLexer lexer(kSmallProgram, "<bench>");
        int tokens = 0;
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) { ++tokens; }
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    auto stats = ComputeStats(samples);
    PrintStats("Lexer/small", stats);
    REQUIRE(stats.mean_ms < 100.0);
}

TEST_CASE("Micro: lexer throughput — medium program", "[benchmark][micro]") {
    for (int i = 0; i < kWarmupRuns; ++i) {
        PloyLexer lexer(kMediumProgram, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        PloyLexer lexer(kMediumProgram, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    auto stats = ComputeStats(samples);
    PrintStats("Lexer/medium", stats);
    REQUIRE(stats.mean_ms < 200.0);
}

TEST_CASE("Micro: lexer throughput — large program (100 funcs)", "[benchmark][micro]") {
    auto code = GenerateLargeProgram(100);

    for (int i = 0; i < kWarmupRuns; ++i) {
        PloyLexer lexer(code, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        PloyLexer lexer(code, "<bench>");
        int tokens = 0;
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) { ++tokens; }
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    auto stats = ComputeStats(samples);
    PrintStats("Lexer/large-100", stats);
    REQUIRE(stats.mean_ms < 1000.0);
}

// ============================================================================
// Parser micro-benchmarks
// ============================================================================

TEST_CASE("Micro: parser throughput — small program", "[benchmark][micro]") {
    for (int i = 0; i < kWarmupRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kSmallProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        Diagnostics diags;
        PloyLexer lexer(kSmallProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(module != nullptr);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Parser/small", stats);
    REQUIRE(stats.mean_ms < 100.0);
}

TEST_CASE("Micro: parser throughput — medium program", "[benchmark][micro]") {
    for (int i = 0; i < kWarmupRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(module != nullptr);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Parser/medium", stats);
    REQUIRE(stats.mean_ms < 200.0);
}

TEST_CASE("Micro: parser throughput — large program (100 funcs)", "[benchmark][micro]") {
    auto code = GenerateLargeProgram(100);

    for (int i = 0; i < kWarmupRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(code, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        Diagnostics diags;
        PloyLexer lexer(code, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(module != nullptr);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Parser/large-100", stats);
    REQUIRE(stats.mean_ms < 2000.0);
}

// ============================================================================
// Sema micro-benchmarks
// ============================================================================

TEST_CASE("Micro: sema throughput — medium program", "[benchmark][micro]") {
    // Pre-parse once (we are benchmarking sema only)
    for (int i = 0; i < kWarmupRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags);
        sema.Analyze(module);
    }

    std::vector<double> samples;
    for (int i = 0; i < kBenchRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();

        auto start = std::chrono::high_resolution_clock::now();
        PloySema sema(diags);
        bool ok = sema.Analyze(module);
        auto end = std::chrono::high_resolution_clock::now();

        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(ok);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Sema/medium", stats);
    REQUIRE(stats.mean_ms < 200.0);
}

// ============================================================================
// Lowering micro-benchmarks
// ============================================================================

TEST_CASE("Micro: lowering throughput — medium program", "[benchmark][micro]") {
    for (int i = 0; i < kWarmupRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
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
    for (int i = 0; i < kBenchRuns; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags);
        sema.Analyze(module);

        auto start = std::chrono::high_resolution_clock::now();
        IRContext ctx;
        PloyLowering lowering(ctx, diags, sema);
        bool ok = lowering.Lower(module);
        auto end = std::chrono::high_resolution_clock::now();

        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(ok);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Lowering/medium", stats);
    REQUIRE(stats.mean_ms < 200.0);
}
