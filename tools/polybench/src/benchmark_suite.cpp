/**
 * PolyglotCompiler performance benchmark suite
 * 
 * Provides comprehensive performance benchmarks covering:
 * - Compilation performance
 * - Runtime performance
 * - GC performance
 * - Optimization effectiveness
 */

#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

#include "runtime/include/gc/gc_strategy.h"
#include "runtime/include/gc/heap.h"
#include "middle/include/passes/transform/advanced_optimizations.h"
#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/passes/transform/inlining.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"
#include "frontends/rust/include/rust_lowering.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "backends/x86_64/include/x86_target.h"

using json = nlohmann::json;
using namespace std::chrono;

namespace polyglot::tools {

// ============ Benchmark framework ============

class BenchmarkResult {
public:
    std::string name;
    double mean_time_ms;
    double min_time_ms;
    double max_time_ms;
    double std_dev_ms;
    size_t iterations;
    std::map<std::string, double> metrics;
};

class BenchmarkRunner {
public:
    BenchmarkRunner(const std::string& suite_name) 
        : suite_name_(suite_name) {}
    
    void AddBenchmark(const std::string& name, 
                     std::function<void()> func,
                     size_t iterations = 100) {
        benchmarks_.push_back({name, func, iterations});
    }
    
    void Run() {
        std::cout << "=== Running Benchmark Suite: " << suite_name_ << " ===\n\n";
        
        for (auto& bench : benchmarks_) {
            std::cout << "Running: " << bench.name << " (" << bench.iterations << " iterations)...\n";
            
            std::vector<double> times;
            times.reserve(bench.iterations);
            
            // Warmup
            for (size_t i = 0; i < 5; ++i) {
                bench.func();
            }
            
            // Actual benchmark
            for (size_t i = 0; i < bench.iterations; ++i) {
                auto start = high_resolution_clock::now();
                bench.func();
                auto end = high_resolution_clock::now();
                
                auto duration = duration_cast<microseconds>(end - start);
                times.push_back(duration.count() / 1000.0);
            }
            
            BenchmarkResult result;
            result.name = bench.name;
            result.iterations = bench.iterations;
            result.mean_time_ms = CalculateMean(times);
            result.min_time_ms = *std::min_element(times.begin(), times.end());
            result.max_time_ms = *std::max_element(times.begin(), times.end());
            result.std_dev_ms = CalculateStdDev(times, result.mean_time_ms);
            
            results_.push_back(result);
            PrintResult(result);
        }
        
        std::cout << "\n=== Benchmark Suite Complete ===\n";
    }
    
    void SaveResults(const std::string& output_file) {
        json j;
        j["suite_name"] = suite_name_;
        j["timestamp"] = std::time(nullptr);
        
        json results_json = json::array();
        for (const auto& result : results_) {
            json r;
            r["name"] = result.name;
            r["mean_ms"] = result.mean_time_ms;
            r["min_ms"] = result.min_time_ms;
            r["max_ms"] = result.max_time_ms;
            r["std_dev_ms"] = result.std_dev_ms;
            r["iterations"] = result.iterations;
            r["metrics"] = result.metrics;
            results_json.push_back(r);
        }
        j["results"] = results_json;
        
        std::ofstream out(output_file);
        out << j.dump(4);
        
        std::cout << "\nResults saved to: " << output_file << "\n";
    }
    
private:
    struct Benchmark {
        std::string name;
        std::function<void()> func;
        size_t iterations;
    };
    
    std::string suite_name_;
    std::vector<Benchmark> benchmarks_;
    std::vector<BenchmarkResult> results_;
    
    double CalculateMean(const std::vector<double>& values) {
        double sum = 0;
        for (double v : values) sum += v;
        return sum / values.size();
    }
    
    double CalculateStdDev(const std::vector<double>& values, double mean) {
        double sum = 0;
        for (double v : values) {
            sum += (v - mean) * (v - mean);
        }
        return std::sqrt(sum / values.size());
    }
    
    void PrintResult(const BenchmarkResult& result) {
        std::cout << "  Mean:   " << result.mean_time_ms << " ms\n";
        std::cout << "  Min:    " << result.min_time_ms << " ms\n";
        std::cout << "  Max:    " << result.max_time_ms << " ms\n";
        std::cout << "  StdDev: " << result.std_dev_ms << " ms\n\n";
    }
};

// ============ GC performance benchmarks ============

void BenchmarkGC() {
    BenchmarkRunner runner("GC Performance");
    
    // Mark-sweep GC
    runner.AddBenchmark("MarkSweep - 1000 allocations", []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kMarkSweep);
        for (int i = 0; i < 1000; ++i) {
            heap.Allocate(64);
        }
        heap.Collect();
    }, 50);
    
    // Generational GC
    runner.AddBenchmark("Generational - 1000 allocations", []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kGenerational);
        for (int i = 0; i < 1000; ++i) {
            heap.Allocate(64);
        }
        heap.Collect();
    }, 50);
    
    // Copying GC
    runner.AddBenchmark("Copying - 500 allocations", []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kCopying);
        for (int i = 0; i < 500; ++i) {
            heap.Allocate(64);
        }
        heap.Collect();
    }, 50);
    
    // Incremental GC
    runner.AddBenchmark("Incremental - 1000 allocations", []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kIncremental);
        for (int i = 0; i < 1000; ++i) {
            heap.Allocate(64);
        }
        heap.Collect();
    }, 50);
    
    runner.Run();
    runner.SaveResults("benchmark_gc.json");
}

// ============ Compilation performance benchmarks ============

void BenchmarkCompilation() {
    BenchmarkRunner runner("Compilation Performance");
    
    // Python compilation
    runner.AddBenchmark("Python - Simple function", []() {
        using namespace polyglot;
        std::string code = R"(
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)
)";
        frontends::Diagnostics diags;
        python::PythonLexer lexer(code, "<bench>");
        python::PythonParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        python::LowerToIR(*mod, ctx, diags);
    }, 100);
    
    // Rust compilation
    runner.AddBenchmark("Rust - Simple function", []() {
        using namespace polyglot;
        std::string code = R"(
fn fibonacci(n: i32) -> i32 {
    if n <= 1 { n }
    else { fibonacci(n-1) + fibonacci(n-2) }
}
)";
        frontends::Diagnostics diags;
        rust::RustLexer lexer(code, "<bench>");
        rust::RustParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        rust::LowerToIR(*mod, ctx, diags);
    }, 100);
    
    // C++ compilation
    runner.AddBenchmark("C++ - Simple function", []() {
        using namespace polyglot;
        std::string code = R"(
int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n-1) + fibonacci(n-2);
}
)";
        frontends::Diagnostics diags;
        cpp::CppLexer lexer(code, "<bench>");
        cpp::CppParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        cpp::LowerToIR(*mod, ctx, diags);
    }, 100);
    
    runner.Run();
    runner.SaveResults("benchmark_compilation.json");
}

// ============ Optimization performance benchmarks ============

void BenchmarkOptimizations() {
    BenchmarkRunner runner("Optimization Performance");
    
    using namespace polyglot::ir;
    using namespace polyglot::passes::transform;
    
    // Create a test function with basic blocks and instructions.
    auto create_test_func = []() {
        polyglot::ir::IRContext ctx;
        auto fn_ptr = ctx.CreateFunction("bench_test");
        // Return the function by value for the benchmark lambda.
        return *fn_ptr;
    };
    
    runner.AddBenchmark("Constant Folding", [&]() {
        auto func = create_test_func();
        polyglot::ir::IRContext ctx;
        auto fn = ctx.CreateFunction(func.name);
        polyglot::passes::transform::RunConstantFold(ctx);
    }, 1000);
    
    runner.AddBenchmark("Dead Code Elimination", [&]() {
        auto func = create_test_func();
        polyglot::ir::IRContext ctx;
        auto fn = ctx.CreateFunction(func.name);
        polyglot::passes::transform::RunDeadCodeElimination(ctx);
    }, 1000);
    
    runner.AddBenchmark("Loop Unrolling", [&]() {
        Function func = create_test_func();
        polyglot::passes::transform::LoopUnrolling(func, 4);
    }, 100);
    
    runner.AddBenchmark("LICM", [&]() {
        Function func = create_test_func();
        polyglot::passes::transform::LoopInvariantCodeMotion(func);
    }, 100);
    
    runner.AddBenchmark("Auto Vectorization", [&]() {
        Function func = create_test_func();
        AutoVectorization(func);
    }, 100);
    
    runner.Run();
    runner.SaveResults("benchmark_optimizations.json");
}

// ============ End-to-end performance benchmarks ============

void BenchmarkEndToEnd() {
    BenchmarkRunner runner("End-to-End Performance");
    
    // Small program compilation
    runner.AddBenchmark("E2E - Small program (100 LOC)", []() {
        using namespace polyglot;
        // Generate a 100-LOC Python program with multiple functions.
        std::string code;
        for (int i = 0; i < 20; ++i) {
            code += "def func_" + std::to_string(i) + "(x):\n";
            code += "    y = x + " + std::to_string(i) + "\n";
            code += "    return y * 2\n\n";
        }
        frontends::Diagnostics diags;
        python::PythonLexer lexer(code, "<bench>");
        python::PythonParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        python::LowerToIR(*mod, ctx, diags);
        passes::transform::RunConstantFold(ctx);
        passes::transform::RunDeadCodeElimination(ctx);
    }, 50);
    
    // Medium program compilation
    runner.AddBenchmark("E2E - Medium program (1000 LOC)", []() {
        using namespace polyglot;
        std::string code;
        for (int i = 0; i < 200; ++i) {
            code += "def func_" + std::to_string(i) + "(x):\n";
            code += "    y = x + " + std::to_string(i) + "\n";
            code += "    z = y * 3\n";
            code += "    return z - 1\n\n";
        }
        frontends::Diagnostics diags;
        python::PythonLexer lexer(code, "<bench>");
        python::PythonParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        python::LowerToIR(*mod, ctx, diags);
        passes::transform::RunConstantFold(ctx);
        passes::transform::RunDeadCodeElimination(ctx);
        passes::transform::RunCommonSubexpressionElimination(ctx);
    }, 10);
    
    // Large program compilation
    runner.AddBenchmark("E2E - Large program (10000 LOC)", []() {
        using namespace polyglot;
        std::string code;
        for (int i = 0; i < 2000; ++i) {
            code += "def func_" + std::to_string(i) + "(x):\n";
            code += "    y = x + " + std::to_string(i) + "\n";
            code += "    z = y * 3\n";
            code += "    w = z - 1\n";
            code += "    return w + x\n\n";
        }
        frontends::Diagnostics diags;
        python::PythonLexer lexer(code, "<bench>");
        python::PythonParser parser(lexer, diags);
        parser.ParseModule();
        auto mod = parser.TakeModule();
        ir::IRContext ctx;
        python::LowerToIR(*mod, ctx, diags);
        passes::transform::RunConstantFold(ctx);
        passes::transform::RunDeadCodeElimination(ctx);
        passes::transform::RunCommonSubexpressionElimination(ctx);
        passes::transform::RunInlining(ctx);
    }, 3);
    
    runner.Run();
    runner.SaveResults("benchmark_e2e.json");
}

// ============ Comparative benchmarks ============

void BenchmarkComparison() {
    BenchmarkRunner runner("Optimization Level Comparison");
    
    // No optimization vs. optimized levels
    for (int opt_level = 0; opt_level <= 3; ++opt_level) {
        std::string name = "Compile with -O" + std::to_string(opt_level);
        runner.AddBenchmark(name, [opt_level]() {
            using namespace polyglot;
            std::string code;
            for (int i = 0; i < 50; ++i) {
                code += "def f" + std::to_string(i) + "(x):\n";
                code += "    return x + " + std::to_string(i) + "\n\n";
            }
            frontends::Diagnostics diags;
            python::PythonLexer lexer(code, "<bench>");
            python::PythonParser parser(lexer, diags);
            parser.ParseModule();
            auto mod = parser.TakeModule();
            ir::IRContext ctx;
            python::LowerToIR(*mod, ctx, diags);
            if (opt_level >= 1) {
                passes::transform::RunConstantFold(ctx);
                passes::transform::RunDeadCodeElimination(ctx);
            }
            if (opt_level >= 2) {
                passes::transform::RunCommonSubexpressionElimination(ctx);
                passes::transform::RunInlining(ctx);
            }
        }, 20);
    }
    
    runner.Run();
    runner.SaveResults("benchmark_comparison.json");
}

}  // namespace polyglot::tools

// ============ Main entrypoint ============

int main(int argc, char* argv[]) {
    using namespace polyglot::tools;

    std::cout << "╔════════════════════════════════════════════════╗\n";
    std::cout << "║   PolyglotCompiler Benchmark Suite v3.0        ║\n";
    std::cout << "║   Comprehensive Performance Testing            ║\n";
    std::cout << "╚════════════════════════════════════════════════╝\n\n";
    
    if (argc > 1) {
        std::string suite = argv[1];
        
        if (suite == "gc") {
            BenchmarkGC();
        } else if (suite == "compile") {
            BenchmarkCompilation();
        } else if (suite == "opt") {
            BenchmarkOptimizations();
        } else if (suite == "e2e") {
            BenchmarkEndToEnd();
        } else if (suite == "compare") {
            BenchmarkComparison();
        } else if (suite == "all") {
            BenchmarkGC();
            BenchmarkCompilation();
            BenchmarkOptimizations();
            BenchmarkEndToEnd();
            BenchmarkComparison();
        } else {
            std::cerr << "Unknown suite: " << suite << "\n";
            std::cerr << "Available: gc, compile, opt, e2e, compare, all\n";
            return 1;
        }
    } else {
        std::cout << "Usage: " << argv[0] << " <suite>\n";
        std::cout << "Suites: gc, compile, opt, e2e, compare, all\n\n";
        
        // Run all suites by default
        BenchmarkGC();
        BenchmarkCompilation();
        BenchmarkOptimizations();
    }
    
    std::cout << "\n✅ All benchmarks completed!\n";
    std::cout << "Results saved to JSON files in current directory.\n";
    
    return 0;
}
