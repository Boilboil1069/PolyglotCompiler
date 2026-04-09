// ============================================================================
// Micro-Benchmarks
//
// These benchmarks measure individual compiler stage throughput:
// lexer, parser, sema, and lowering — each in isolation.
// Results are printed as ops/sec and ms/op for analysis.
//
// Environment variable POLYBENCH_MODE controls iteration counts:
//   "fast"  — 1 warmup, 5 runs   (CI / quick sanity)
//   "full"  — 5 warmup, 50 runs  (detailed profiling)
//   default — 3 warmup, 20 runs  (normal)
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
#include <cstdlib>

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
using polyglot::ploy::PloySemaOptions;
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
    IMPORT python PACKAGE model;

    LINK(cpp, python, engine::process, model::predict) {
        MAP_TYPE(cpp::double, python::float);
    }

    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);

    STRUCT Config {
        width_c: INT;
        height_c: INT;
        scale_c: FLOAT;
    }

    PIPELINE data_flow {
        FUNC stage1(x1: FLOAT) -> FLOAT {
            LET r1 = CALL(cpp, engine::process, x1);
            RETURN r1;
        }

        FUNC stage2(x2: FLOAT) -> FLOAT {
            LET r2 = CALL(python, model::predict, x2);
            IF r2 > 0.5 {
                RETURN r2;
            } ELSE {
                RETURN 0.0;
            }
        }

        FUNC stage3(n3: INT) -> INT {
            VAR acc3 = 0;
            FOR i3 IN 0..n3 {
                acc3 = acc3 + 1;
            }
            RETURN acc3;
        }
    }

    FUNC main_entry(data_me: FLOAT, count_me: INT) -> FLOAT {
        LET obj_me = NEW(python, model::Net, 4, 8);
        SET(python, obj_me, lr, 0.01);
        LET pred_me = METHOD(python, obj_me, forward, data_me);
        LET lr_me = GET(python, obj_me, lr);
        DELETE(python, obj_me);
        RETURN pred_me;
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
        oss << "FUNC f" << i << "(x" << i << ": INT, y" << i << ": FLOAT) -> FLOAT {\n";
        oss << "    LET a" << i << " = CALL(python, lib::compute, x" << i << ");\n";
        oss << "    LET b" << i << " = CALL(cpp, proc::transform, y" << i << ");\n";
        oss << "    IF a" << i << " > 0 {\n";
        oss << "        VAR result" << i << " = b" << i << ";\n";
        oss << "        FOR i" << i << " IN 0..x" << i << " {\n";
        oss << "            result" << i << " = result" << i << " + 1.0;\n";
        oss << "        }\n";
        oss << "        RETURN result" << i << ";\n";
        oss << "    } ELSE {\n";
        oss << "        RETURN b" << i << ";\n";
        oss << "    }\n";
        oss << "}\n\n";
    }
    return oss.str();
}

constexpr int kDefaultWarmup = 3;
constexpr int kDefaultRuns   = 20;
constexpr int kFastWarmup    = 1;
constexpr int kFastRuns      = 5;
constexpr int kFullWarmup    = 5;
constexpr int kFullRuns      = 50;

// Read POLYBENCH_MODE environment variable and return (warmup, runs).
static std::pair<int,int> GetBenchConfig() {
#pragma warning(suppress: 4996)
    const char *mode = std::getenv("POLYBENCH_MODE");
    if (mode) {
        std::string m(mode);
        if (m == "fast") return {kFastWarmup, kFastRuns};
        if (m == "full") return {kFullWarmup, kFullRuns};
    }
    return {kDefaultWarmup, kDefaultRuns};
}

// PloySemaOptions with discovery disabled for benchmarks.
static PloySemaOptions BenchSemaOptions() {
    PloySemaOptions opts;
    opts.enable_package_discovery = false;
    opts.strict_mode = false;
    return opts;
}

} // namespace

// ============================================================================
// Lexer micro-benchmarks
// ============================================================================

TEST_CASE("Micro: lexer throughput — small program", "[benchmark][micro]") {
    auto [warmup, runs] = GetBenchConfig();
    // Warmup
    for (int i = 0; i < warmup; ++i) {
        PloyLexer lexer(kSmallProgram, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        PloyLexer lexer(kSmallProgram, "<bench>");
        int tokens = 0;
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) { ++tokens; }
        (void)tokens;  // Suppress unused variable warning
        auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    auto stats = ComputeStats(samples);
    PrintStats("Lexer/small", stats);
    REQUIRE(stats.mean_ms < 100.0);
}

TEST_CASE("Micro: lexer throughput — medium program", "[benchmark][micro]") {
    auto [warmup, runs] = GetBenchConfig();
    for (int i = 0; i < warmup; ++i) {
        PloyLexer lexer(kMediumProgram, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
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
    auto [warmup, runs] = GetBenchConfig();
    auto code = GenerateLargeProgram(100);

    for (int i = 0; i < warmup; ++i) {
        PloyLexer lexer(code, "<bench>");
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) {}
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        PloyLexer lexer(code, "<bench>");
        int tokens = 0;
        while (lexer.NextToken().kind != TokenKind::kEndOfFile) { ++tokens; }
        (void)tokens;
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
    auto [warmup, runs] = GetBenchConfig();
    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kSmallProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
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
    auto [warmup, runs] = GetBenchConfig();
    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
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
    auto [warmup, runs] = GetBenchConfig();
    auto code = GenerateLargeProgram(100);

    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(code, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
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
    auto [warmup, runs] = GetBenchConfig();
    auto bench_opts = BenchSemaOptions();

    // Pre-parse once (we are benchmarking sema only)
    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags, bench_opts);
        sema.Analyze(module);
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();

        auto start = std::chrono::high_resolution_clock::now();
        PloySema sema(diags, bench_opts);
        bool ok = sema.Analyze(module);
        auto end = std::chrono::high_resolution_clock::now();

        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        REQUIRE(ok);
    }

    auto stats = ComputeStats(samples);
    PrintStats("Sema/medium", stats);
    REQUIRE(stats.mean_ms < 5000.0);
}

// ============================================================================
// Lowering micro-benchmarks
//
// The timing boundary starts AFTER parse+sema (which are prerequisites)
// and covers only the lowering and IR generation phase.
// ============================================================================

TEST_CASE("Micro: lowering throughput — medium program", "[benchmark][micro]") {
    auto [warmup, runs] = GetBenchConfig();
    auto bench_opts = BenchSemaOptions();

    for (int i = 0; i < warmup; ++i) {
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags, bench_opts);
        sema.Analyze(module);
        IRContext ctx;
        PloyLowering lowering(ctx, diags, sema);
        lowering.Lower(module);
    }

    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        // Setup phase: parse + sema (outside timing boundary)
        Diagnostics diags;
        PloyLexer lexer(kMediumProgram, "<bench>");
        PloyParser parser(lexer, diags);
        parser.ParseModule();
        auto module = parser.TakeModule();
        PloySema sema(diags, bench_opts);
        sema.Analyze(module);

        // Timed phase: lowering only
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
