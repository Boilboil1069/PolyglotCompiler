/**
 * @file     benchmark_suite.cpp
 * @brief    Benchmark driver
 *
 * @ingroup  Tool / polybench
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
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
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "tools/common/include/effective_settings_loader.h"
#include "common/include/target_triple.h"
#include <vector>#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/passes/transform/advanced_optimizations.h"
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/passes/transform/inlining.h"

#include "backends/x86_64/include/x86_target.h"
#include "common/include/version.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_lowering.h"
#include "frontends/rust/include/rust_parser.h"
#include "runtime/include/gc/gc_strategy.h"
#include "runtime/include/gc/heap.h"

using json = nlohmann::json;
using namespace std::chrono;

namespace polyglot::tools {

// BIN-7: process-wide target triple chosen by `--target=<spec>`.  Each
// `BenchmarkRunner` reads this on construction so individual suite
// functions do not need to thread the value through their signatures.
::polyglot::common::TargetTriple g_polybench_target_triple =
    ::polyglot::common::HostTriple();

/** @name Benchmark framework */
/** @{ */

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
  BenchmarkRunner(const std::string &suite_name)
      : suite_name_(suite_name), target_triple_(g_polybench_target_triple) {}

  void AddBenchmark(const std::string &name, std::function<void()> func, size_t iterations = 100) {
    benchmarks_.push_back({name, func, iterations});
  }

  void Run() {
    std::cout << "=== Running Benchmark Suite: " << suite_name_ << " ===\n\n";

    for (auto &bench : benchmarks_) {
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

  void SaveResults(const std::string &output_file) {
    json j;
    j["suite_name"] = suite_name_;
    j["timestamp"] = std::time(nullptr);
    // BIN-7: stamp every results file with the resolved target triple
    // so consumers can correlate measurements across host/target pairs.
    j["target_triple"] = target_triple_.str();

    json results_json = json::array();
    for (const auto &result : results_) {
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
  // BIN-7: target triple recorded in the JSON results header.  Process-
  // wide so every Runner instance picks up the user's --target= choice.
  ::polyglot::common::TargetTriple target_triple_;

public:
  void SetTargetTriple(const ::polyglot::common::TargetTriple &t) {
    target_triple_ = t;
  }

private:

  double CalculateMean(const std::vector<double> &values) {
    double sum = 0;
    for (double v : values)
      sum += v;
    return sum / values.size();
  }

  double CalculateStdDev(const std::vector<double> &values, double mean) {
    double sum = 0;
    for (double v : values) {
      sum += (v - mean) * (v - mean);
    }
    return std::sqrt(sum / values.size());
  }

  void PrintResult(const BenchmarkResult &result) {
    std::cout << "  Mean:   " << result.mean_time_ms << " ms\n";
    std::cout << "  Min:    " << result.min_time_ms << " ms\n";
    std::cout << "  Max:    " << result.max_time_ms << " ms\n";
    std::cout << "  StdDev: " << result.std_dev_ms << " ms\n\n";
  }
};

/** @} */

/** @name GC performance benchmarks */
/** @{ */

void BenchmarkGC() {
  BenchmarkRunner runner("GC Performance");

  // Mark-sweep GC
  runner.AddBenchmark(
      "MarkSweep - 1000 allocations",
      []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kMarkSweep);
        for (int i = 0; i < 1000; ++i) {
          heap.Allocate(64);
        }
        heap.Collect();
      },
      50);

  // Generational GC
  runner.AddBenchmark(
      "Generational - 1000 allocations",
      []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kGenerational);
        for (int i = 0; i < 1000; ++i) {
          heap.Allocate(64);
        }
        heap.Collect();
      },
      50);

  // Copying GC
  runner.AddBenchmark(
      "Copying - 500 allocations",
      []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kCopying);
        for (int i = 0; i < 500; ++i) {
          heap.Allocate(64);
        }
        heap.Collect();
      },
      50);

  // Incremental GC
  runner.AddBenchmark(
      "Incremental - 1000 allocations",
      []() {
        using namespace polyglot::runtime::gc;
        Heap heap(Strategy::kIncremental);
        for (int i = 0; i < 1000; ++i) {
          heap.Allocate(64);
        }
        heap.Collect();
      },
      50);

  runner.Run();
  runner.SaveResults("benchmark_gc.json");
}

/** @} */

/** @name Compilation performance benchmarks */
/** @{ */

void BenchmarkCompilation() {
  BenchmarkRunner runner("Compilation Performance");

  // Python compilation
  runner.AddBenchmark(
      "Python - Simple function",
      []() {
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
      },
      100);

  // Rust compilation
  runner.AddBenchmark(
      "Rust - Simple function",
      []() {
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
      },
      100);

  // C++ compilation
  runner.AddBenchmark(
      "C++ - Simple function",
      []() {
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
      },
      100);

  runner.Run();
  runner.SaveResults("benchmark_compilation.json");
}

/** @} */

/** @name Optimization performance benchmarks */
/** @{ */

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

  runner.AddBenchmark(
      "Constant Folding",
      [&]() {
        auto func = create_test_func();
        polyglot::ir::IRContext ctx;
        auto fn = ctx.CreateFunction(func.name);
        polyglot::passes::transform::RunConstantFold(ctx);
      },
      1000);

  runner.AddBenchmark(
      "Dead Code Elimination",
      [&]() {
        auto func = create_test_func();
        polyglot::ir::IRContext ctx;
        auto fn = ctx.CreateFunction(func.name);
        polyglot::passes::transform::RunDeadCodeElimination(ctx);
      },
      1000);

  runner.AddBenchmark(
      "Loop Unrolling",
      [&]() {
        Function func = create_test_func();
        polyglot::passes::transform::LoopUnrolling(func, 4);
      },
      100);

  runner.AddBenchmark(
      "LICM",
      [&]() {
        Function func = create_test_func();
        polyglot::passes::transform::LoopInvariantCodeMotion(func);
      },
      100);

  runner.AddBenchmark(
      "Auto Vectorization",
      [&]() {
        Function func = create_test_func();
        AutoVectorization(func);
      },
      100);

  runner.Run();
  runner.SaveResults("benchmark_optimizations.json");
}

/** @} */

/** @name End-to-end performance benchmarks */
/** @{ */

void BenchmarkEndToEnd() {
  BenchmarkRunner runner("End-to-End Performance");

  // Small program compilation
  runner.AddBenchmark(
      "E2E - Small program (100 LOC)",
      []() {
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
      },
      50);

  // Medium program compilation
  runner.AddBenchmark(
      "E2E - Medium program (1000 LOC)",
      []() {
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
      },
      10);

  // Large program compilation
  runner.AddBenchmark(
      "E2E - Large program (10000 LOC)",
      []() {
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
      },
      3);

  runner.Run();
  runner.SaveResults("benchmark_e2e.json");
}

/** @} */

/** @name Container linker timing benchmarks */
/** @{ */

namespace bench_link {

// Helpers that emit a tiny but format-legal byte image for each of the
// four binary containers polyld supports.  These mirror the canonical
// wire layout used by `tools/polyld` so the timing numbers reflect the
// same per-byte work the production writers do (header / section table /
// payload assembly + checksum updates).  They intentionally do not call
// into the linker_lib targets so polybench keeps its lean dependency
// surface (no transitive frontend / backend pulls beyond what it
// already needs for the IR-level suites).

inline void Put8(std::vector<std::uint8_t> &b, std::uint8_t v) { b.push_back(v); }
inline void Put16(std::vector<std::uint8_t> &b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v & 0xFF));
  b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
inline void Put32(std::vector<std::uint8_t> &b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
inline void Put64(std::vector<std::uint8_t> &b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
inline void PutBytes(std::vector<std::uint8_t> &b, const void *p, std::size_t n) {
  const auto *bp = static_cast<const std::uint8_t *>(p);
  b.insert(b.end(), bp, bp + n);
}
inline void PadTo(std::vector<std::uint8_t> &b, std::size_t target) {
  if (b.size() < target) b.resize(target, 0);
}

// PE32+ skeleton: DOS stub + PE\0\0 + COFF header + Optional Header
// (PE32+ magic 0x20B) + a single .text section containing `text_size`
// bytes of NOP-equivalent code.  All offsets are computed as the real
// writer does (raw alignment 512, virtual alignment 4096).
std::vector<std::uint8_t> EmitPESkeleton(std::size_t text_size) {
  std::vector<std::uint8_t> img;
  // DOS header.  e_lfanew at offset 0x3C points to the PE signature.
  PutBytes(img, "MZ", 2);
  PadTo(img, 0x3C);
  Put32(img, 0x80);                // e_lfanew → PE sig at 0x80.
  PadTo(img, 0x80);
  PutBytes(img, "PE\0\0", 4);
  // COFF file header (20 bytes).
  Put16(img, 0x8664);              // Machine = AMD64.
  Put16(img, 1);                   // NumberOfSections.
  Put32(img, 0);                   // TimeDateStamp.
  Put32(img, 0);                   // PointerToSymbolTable.
  Put32(img, 0);                   // NumberOfSymbols.
  Put16(img, 0xF0);                // SizeOfOptionalHeader (PE32+).
  Put16(img, 0x0022);              // Characteristics (EXECUTABLE | LARGE_ADDR).
  // Optional Header (PE32+) — 240 bytes total.
  Put16(img, 0x020B);              // Magic = PE32+.
  Put8(img, 14); Put8(img, 0);     // LinkerVersion 14.0.
  Put32(img, static_cast<std::uint32_t>(text_size));   // SizeOfCode.
  Put32(img, 0); Put32(img, 0);    // SizeOfInitData / Uninit.
  Put32(img, 0x1000);              // AddressOfEntryPoint.
  Put32(img, 0x1000);              // BaseOfCode.
  Put64(img, 0x140000000ULL);      // ImageBase.
  Put32(img, 0x1000);              // SectionAlignment.
  Put32(img, 0x200);               // FileAlignment.
  Put16(img, 6); Put16(img, 0);    // OS major/minor.
  Put16(img, 0); Put16(img, 0);    // Image major/minor.
  Put16(img, 6); Put16(img, 0);    // Subsystem major/minor.
  Put32(img, 0);                   // Win32VersionValue.
  Put32(img, 0x2000);              // SizeOfImage.
  Put32(img, 0x400);               // SizeOfHeaders.
  Put32(img, 0);                   // CheckSum (writer would patch later).
  Put16(img, 3);                   // Subsystem = WINDOWS_CUI.
  Put16(img, 0x8160);              // DllCharacteristics.
  Put64(img, 0x100000ULL);         // SizeOfStackReserve.
  Put64(img, 0x1000ULL);           // SizeOfStackCommit.
  Put64(img, 0x100000ULL);         // SizeOfHeapReserve.
  Put64(img, 0x1000ULL);           // SizeOfHeapCommit.
  Put32(img, 0);                   // LoaderFlags.
  Put32(img, 16);                  // NumberOfRvaAndSizes.
  for (int i = 0; i < 16; ++i) { Put32(img, 0); Put32(img, 0); }
  // Section table: one .text entry.
  PutBytes(img, ".text\0\0\0", 8);
  Put32(img, static_cast<std::uint32_t>(text_size));   // VirtualSize.
  Put32(img, 0x1000);              // VirtualAddress.
  Put32(img, static_cast<std::uint32_t>(((text_size + 0x1FF) / 0x200) * 0x200)); // SizeOfRawData.
  Put32(img, 0x400);               // PointerToRawData.
  Put32(img, 0); Put32(img, 0); Put16(img, 0); Put16(img, 0);
  Put32(img, 0x60000020);          // Characteristics: CODE | EXEC | READ.
  // Pad headers to FileAlignment then append .text payload.
  PadTo(img, 0x400);
  for (std::size_t i = 0; i < text_size; ++i) Put8(img, 0x90);   // NOPs.
  PadTo(img, 0x400 + ((text_size + 0x1FF) / 0x200) * 0x200);
  return img;
}

// Mach-O64 skeleton: MH_EXECUTE header + LC_SEGMENT_64(__TEXT/__text)
// containing `text_size` NOP-equivalent bytes.  Mirrors the layout the
// real Mach-O writer emits before LINKEDIT and load-command tail-padding.
std::vector<std::uint8_t> EmitMachOSkeleton(std::size_t text_size) {
  std::vector<std::uint8_t> img;
  const std::uint32_t lc_size  = 72 /*seg64*/ + 80 /*sect64*/;
  const std::uint32_t hdr_size = 32 + lc_size;
  Put32(img, 0xFEEDFACFu);         // MH_MAGIC_64.
  Put32(img, 0x01000007u);         // CPU_TYPE_X86_64.
  Put32(img, 0x00000003u);         // CPU_SUBTYPE_ALL.
  Put32(img, 2);                   // MH_EXECUTE.
  Put32(img, 1);                   // ncmds.
  Put32(img, lc_size);             // sizeofcmds.
  Put32(img, 0x00200085u);         // flags: NOUNDEFS|DYLDLINK|TWOLEVEL|PIE.
  Put32(img, 0);                   // reserved.
  // LC_SEGMENT_64.
  Put32(img, 0x19u);               // LC_SEGMENT_64.
  Put32(img, lc_size);
  char segname[16] = "__TEXT";
  PutBytes(img, segname, 16);
  Put64(img, 0x100000000ULL);      // vmaddr.
  Put64(img, 0x1000ULL);           // vmsize.
  Put64(img, 0);                   // fileoff.
  Put64(img, hdr_size + text_size);// filesize.
  Put32(img, 0x5);                 // maxprot R|X.
  Put32(img, 0x5);                 // initprot R|X.
  Put32(img, 1);                   // nsects.
  Put32(img, 0);                   // flags.
  // Section64.
  char sectname[16] = "__text";
  PutBytes(img, sectname, 16);
  PutBytes(img, segname,  16);
  Put64(img, 0x100000000ULL + hdr_size);  // addr.
  Put64(img, text_size);                  // size.
  Put32(img, hdr_size);                   // offset.
  Put32(img, 4);                          // align.
  Put32(img, 0); Put32(img, 0);
  Put32(img, 0x80000400u);                // PURE_INSTR | SOME_INSTR.
  Put32(img, 0); Put32(img, 0); Put32(img, 0);
  // Payload.
  for (std::size_t i = 0; i < text_size; ++i) Put8(img, 0x90);
  return img;
}

// ELF64 LE skeleton: Ehdr + one PT_LOAD Phdr + payload.  Matches the
// shape that `Linker::GenerateELFExecutable` writes for a stripped
// minimal binary on x86_64 Linux.
std::vector<std::uint8_t> EmitELFSkeleton(std::size_t text_size) {
  std::vector<std::uint8_t> img;
  // Ehdr (64 bytes).
  PutBytes(img, "\x7F" "ELF", 4);
  Put8(img, 2);                    // ELFCLASS64.
  Put8(img, 1);                    // ELFDATA2LSB.
  Put8(img, 1);                    // EV_CURRENT.
  Put8(img, 0);                    // OSABI.
  Put64(img, 0);                   // padding.
  Put16(img, 2);                   // ET_EXEC.
  Put16(img, 0x3E);                // EM_X86_64.
  Put32(img, 1);                   // e_version.
  Put64(img, 0x401000ULL);         // e_entry.
  Put64(img, 64);                  // e_phoff.
  Put64(img, 0);                   // e_shoff.
  Put32(img, 0);                   // e_flags.
  Put16(img, 64);                  // e_ehsize.
  Put16(img, 56);                  // e_phentsize.
  Put16(img, 1);                   // e_phnum.
  Put16(img, 0);                   // e_shentsize.
  Put16(img, 0);                   // e_shnum.
  Put16(img, 0);                   // e_shstrndx.
  // Phdr (56 bytes) — PT_LOAD R|X covering Ehdr+Phdr+payload.
  Put32(img, 1);                   // PT_LOAD.
  Put32(img, 5);                   // PF_R|PF_X.
  Put64(img, 0);                   // p_offset.
  Put64(img, 0x400000ULL);         // p_vaddr.
  Put64(img, 0x400000ULL);         // p_paddr.
  Put64(img, 120 + text_size);     // p_filesz.
  Put64(img, 120 + text_size);     // p_memsz.
  Put64(img, 0x1000ULL);           // p_align.
  // Payload.
  for (std::size_t i = 0; i < text_size; ++i) Put8(img, 0x90);
  return img;
}

// Wasm MVP skeleton: 8-byte preamble + type/func/export/code sections
// describing a single `_start: () -> ()` whose body is `nop` × `text_size`
// then `end`.  Mirrors `EmitWasmModule` from `linker_wasm` byte-for-byte
// for the no-import / no-memory / no-table fast path the real writer
// takes when the user gives it an empty `Module`.
std::vector<std::uint8_t> EmitWasmSkeleton(std::size_t text_size) {
  std::vector<std::uint8_t> img;
  // Preamble.
  PutBytes(img, "\0asm", 4);
  Put32(img, 1);
  // Helper for ULEB128.
  auto leb = [](std::vector<std::uint8_t> &b, std::uint32_t v) {
    do { std::uint8_t byte = v & 0x7F; v >>= 7; if (v) byte |= 0x80; b.push_back(byte); } while (v);
  };
  // Type section: one [] -> [] func type.
  std::vector<std::uint8_t> type_payload; leb(type_payload, 1);
  type_payload.push_back(0x60); leb(type_payload, 0); leb(type_payload, 0);
  Put8(img, 1); leb(img, static_cast<std::uint32_t>(type_payload.size()));
  img.insert(img.end(), type_payload.begin(), type_payload.end());
  // Function section: one func of type 0.
  std::vector<std::uint8_t> func_payload; leb(func_payload, 1); leb(func_payload, 0);
  Put8(img, 3); leb(img, static_cast<std::uint32_t>(func_payload.size()));
  img.insert(img.end(), func_payload.begin(), func_payload.end());
  // Export section: "_start" → func 0.
  std::vector<std::uint8_t> exp_payload; leb(exp_payload, 1);
  leb(exp_payload, 6); PutBytes(exp_payload, "_start", 6);
  Put8(exp_payload, 0); leb(exp_payload, 0);
  Put8(img, 7); leb(img, static_cast<std::uint32_t>(exp_payload.size()));
  img.insert(img.end(), exp_payload.begin(), exp_payload.end());
  // Code section: one body = (locals=0, nop * text_size, end).
  std::vector<std::uint8_t> body; leb(body, 0);
  for (std::size_t i = 0; i < text_size; ++i) body.push_back(0x01);
  body.push_back(0x0B);
  std::vector<std::uint8_t> code_payload; leb(code_payload, 1);
  leb(code_payload, static_cast<std::uint32_t>(body.size()));
  code_payload.insert(code_payload.end(), body.begin(), body.end());
  Put8(img, 10); leb(img, static_cast<std::uint32_t>(code_payload.size()));
  img.insert(img.end(), code_payload.begin(), code_payload.end());
  return img;
}

}  // namespace bench_link

void BenchmarkLinkTimes() {
  using namespace bench_link;
  BenchmarkRunner runner("Container Linker Timings");

  // Per-format text payload kept identical so the four numbers are
  // directly comparable.  4 KiB matches the smallest realistic .text
  // segment polyld emits for a hello-world image.
  constexpr std::size_t kPayload = 4096;

  runner.AddBenchmark(
      "pe_link_time",
      [&]() {
        auto img = EmitPESkeleton(kPayload);
        // Force the optimiser to keep the bytes around.
        if (img.empty() || img.front() != 'M') std::abort();
      },
      200);
  runner.AddBenchmark(
      "macho_link_time",
      [&]() {
        auto img = EmitMachOSkeleton(kPayload);
        if (img.size() < 32 || img[0] != 0xCFu) std::abort();
      },
      200);
  runner.AddBenchmark(
      "elf_link_time",
      [&]() {
        auto img = EmitELFSkeleton(kPayload);
        if (img.size() < 64 || img[0] != 0x7Fu) std::abort();
      },
      200);
  runner.AddBenchmark(
      "wasm_link_time",
      [&]() {
        auto img = EmitWasmSkeleton(kPayload);
        if (img.size() < 8 || img[0] != 0x00u) std::abort();
      },
      200);

  runner.Run();
  runner.SaveResults("benchmark_link_times.json");
}

/** @} */

/** @name Comparative benchmarks */
/** @{ */

void BenchmarkComparison() {
  BenchmarkRunner runner("Optimization Level Comparison");

  // No optimization vs. optimized levels
  for (int opt_level = 0; opt_level <= 3; ++opt_level) {
    std::string name = "Compile with -O" + std::to_string(opt_level);
    runner.AddBenchmark(
        name,
        [opt_level]() {
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
        },
        20);
  }

  runner.Run();
  runner.SaveResults("benchmark_comparison.json");
}

} // namespace polyglot::tools

/** @} */

/** @name Main entrypoint */
/** @{ */

int main(int argc, char *argv[]) {
  if (auto rc = polyglot::tools::common::HandleSettingsCliFlags(argc, argv); rc.has_value()) {
    return *rc;
  }
  using namespace polyglot::tools;

  // BIN-7: collect a process-wide target triple before dispatching to
  // the suite functions.  Any unrecognised positional is treated as the
  // suite name (preserving the historical CLI shape).
  ::polyglot::common::TargetTriple bench_triple = ::polyglot::common::HostTriple();
  std::string suite;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--target=", 0) == 0) {
      auto parsed = ::polyglot::common::ParseTargetTriple(a.substr(9));
      if (!parsed.ok()) {
        std::cerr << "[error] --target=" << a.substr(9)
                  << ": polybench-err-E1100 "
                  << (parsed.error ? parsed.error->message : "invalid triple") << "\n";
        return 1;
      }
      bench_triple = *parsed.triple;
    } else if (suite.empty() && !a.empty() && a[0] != '-') {
      suite = a;
    }
  }
  g_polybench_target_triple = bench_triple;

  std::cout << "╔════════════════════════════════════════════════╗\n";
  std::cout << "║   " << POLYGLOT_PROJECT_NAME << " Benchmark Suite v" << POLYGLOT_VERSION_STRING
            << "        ║\n";
  std::cout << "║   Comprehensive Performance Testing            ║\n";
  std::cout << "╚════════════════════════════════════════════════╝\n\n";

  if (!suite.empty()) {
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
    } else if (suite == "link") {
      BenchmarkLinkTimes();
    } else if (suite == "all") {
      BenchmarkGC();
      BenchmarkCompilation();
      BenchmarkOptimizations();
      BenchmarkEndToEnd();
      BenchmarkComparison();
      BenchmarkLinkTimes();
    } else {
      std::cerr << "Unknown suite: " << suite << "\n";
      std::cerr << "Available: gc, compile, opt, e2e, compare, link, all\n";
      return 1;
    }
  } else {
    std::cout << "Usage: " << argv[0] << " <suite> [--target=<triple>]\n";
    std::cout << "Suites: gc, compile, opt, e2e, compare, link, all\n\n";

    // Run all suites by default
    BenchmarkGC();
    BenchmarkCompilation();
    BenchmarkOptimizations();
  }

  std::cout << "\n✅ All benchmarks completed!\n";
  std::cout << "Results saved to JSON files in current directory.\n";

  return 0;
}

/** @} */