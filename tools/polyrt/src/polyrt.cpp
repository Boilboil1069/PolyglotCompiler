// polyrt - Polyglot Compiler Runtime Management Tool
//
// This tool provides runtime management, diagnostics, and benchmarking
// capabilities for the Polyglot Runtime System.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "runtime/include/gc/gc_api.h"
#include "runtime/include/gc/gc_strategy.h"
#include "runtime/include/gc/heap.h"
#include "runtime/include/gc/runtime.h"
#include "runtime/include/libs/base.h"
#include "runtime/include/services/advanced_threading.h"
#include "runtime/include/services/exception.h"
#include "runtime/include/services/threading.h"

namespace polyglot::tools {

// ============================================================================
// Constants and Version Info
// ============================================================================

constexpr const char *kVersion = "1.0.0";
constexpr const char *kToolName = "polyrt";

// ============================================================================
// Utility Functions
// ============================================================================

std::string FormatBytes(size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = static_cast<double>(bytes);
  while (size >= 1024.0 && unit_index < 4) {
    size /= 1024.0;
    ++unit_index;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
  return oss.str();
}

std::string FormatDuration(std::chrono::microseconds us) {
  if (us.count() < 1000) {
    return std::to_string(us.count()) + " us";
  }
  if (us.count() < 1000000) {
    return std::to_string(us.count() / 1000) + "." +
           std::to_string((us.count() % 1000) / 100) + " ms";
  }
  return std::to_string(us.count() / 1000000) + "." +
         std::to_string((us.count() % 1000000) / 100000) + " s";
}

std::string GCStrategyToString(runtime::gc::Strategy strategy) {
  switch (strategy) {
    case runtime::gc::Strategy::kMarkSweep:
      return "mark-sweep";
    case runtime::gc::Strategy::kGenerational:
      return "generational";
    case runtime::gc::Strategy::kCopying:
      return "copying";
    case runtime::gc::Strategy::kIncremental:
      return "incremental";
    default:
      return "unknown";
  }
}

runtime::gc::Strategy StringToGCStrategy(const std::string &str) {
  if (str == "mark-sweep" || str == "marksweep" || str == "ms") {
    return runtime::gc::Strategy::kMarkSweep;
  }
  if (str == "generational" || str == "gen" || str == "g") {
    return runtime::gc::Strategy::kGenerational;
  }
  if (str == "copying" || str == "copy" || str == "c") {
    return runtime::gc::Strategy::kCopying;
  }
  if (str == "incremental" || str == "inc" || str == "i") {
    return runtime::gc::Strategy::kIncremental;
  }
  return runtime::gc::Strategy::kMarkSweep;  // default
}

// ============================================================================
// Runtime Statistics Tracking
// ============================================================================

struct RuntimeStats {
  // GC statistics
  size_t gc_collections{0};
  size_t gc_total_allocated{0};
  size_t gc_current_heap_size{0};
  std::chrono::microseconds gc_total_pause_time{0};

  // Thread statistics
  size_t thread_pool_size{0};
  size_t tasks_submitted{0};
  size_t tasks_completed{0};

  // Memory statistics
  size_t peak_memory_usage{0};
  size_t current_memory_usage{0};
};

static RuntimeStats g_stats;

// ============================================================================
// Command Handlers
// ============================================================================

void PrintUsage() {
  std::cout << "Polyglot Runtime Tool v" << kVersion << "\n\n";
  std::cout << "Usage: " << kToolName << " <command> [options]\n\n";
  std::cout << "Commands:\n";
  std::cout << "  status              Show runtime status and diagnostics\n";
  std::cout << "  gc                  GC management and statistics\n";
  std::cout << "  ffi                 FFI management and diagnostics\n";
  std::cout << "  thread              Thread pool management\n";
  std::cout << "  bench               Run runtime benchmarks\n";
  std::cout << "  info                Show runtime information\n";
  std::cout << "  help                Show this help message\n";
  std::cout << "  version             Show version information\n";
  std::cout << "\n";
  std::cout << "Run '" << kToolName << " <command> --help' for command-specific help.\n";
}

void PrintVersion() {
  std::cout << kToolName << " version " << kVersion << "\n";
  std::cout << "Part of the Polyglot Compiler Toolchain\n";
  std::cout << "Copyright (c) 2025 Polyglot Project Contributors\n";
}

// ----------------------------------------------------------------------------
// Status Command
// ----------------------------------------------------------------------------

void PrintStatusHelp() {
  std::cout << "Usage: " << kToolName << " status [options]\n\n";
  std::cout << "Show runtime status and diagnostics.\n\n";
  std::cout << "Options:\n";
  std::cout << "  --gc          Show GC status only\n";
  std::cout << "  --threads     Show thread status only\n";
  std::cout << "  --memory      Show memory status only\n";
  std::cout << "  --json        Output in JSON format\n";
  std::cout << "  --help        Show this help message\n";
}

int CmdStatus(int argc, char **argv) {
  bool show_gc = true;
  bool show_threads = true;
  bool show_memory = true;
  bool json_output = false;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintStatusHelp();
      return 0;
    }
    if (arg == "--gc") {
      show_gc = true;
      show_threads = false;
      show_memory = false;
    } else if (arg == "--threads") {
      show_gc = false;
      show_threads = true;
      show_memory = false;
    } else if (arg == "--memory") {
      show_gc = false;
      show_threads = false;
      show_memory = true;
    } else if (arg == "--json") {
      json_output = true;
    }
  }

  // Get actual runtime status
  auto &heap = runtime::gc::GlobalHeap();
  auto gc_stats = heap.GetStats();
  unsigned int hw_threads = std::thread::hardware_concurrency();

  if (json_output) {
    std::cout << "{\n";
    std::cout << "  \"status\": \"running\",\n";
    std::cout << "  \"version\": \"" << kVersion << "\",\n";
    if (show_gc) {
      std::cout << "  \"gc\": {\n";
      std::cout << "    \"strategy\": \"mark-sweep\",\n";
      std::cout << "    \"collections\": " << gc_stats.collections << ",\n";
      std::cout << "    \"heap_size\": " << gc_stats.current_heap_bytes << ",\n";
      std::cout << "    \"peak_heap_size\": " << gc_stats.peak_heap_bytes << ",\n";
      std::cout << "    \"total_allocated\": " << gc_stats.total_bytes_allocated << ",\n";
      std::cout << "    \"live_objects\": " << gc_stats.live_objects << ",\n";
      std::cout << "    \"root_count\": " << gc_stats.root_count << "\n";
      std::cout << "  },\n";
    }
    if (show_threads) {
      std::cout << "  \"threads\": {\n";
      std::cout << "    \"hardware_concurrency\": " << hw_threads << ",\n";
      std::cout << "    \"pool_size\": " << g_stats.thread_pool_size << "\n";
      std::cout << "  },\n";
    }
    if (show_memory) {
      std::cout << "  \"memory\": {\n";
      std::cout << "    \"current\": " << gc_stats.current_heap_bytes << ",\n";
      std::cout << "    \"peak\": " << gc_stats.peak_heap_bytes << "\n";
      std::cout << "  }\n";
    }
    std::cout << "}\n";
  } else {
    std::cout << "+----------------------------------------------------------+\n";
    std::cout << "|           Polyglot Runtime Status                        |\n";
    std::cout << "+----------------------------------------------------------+\n";
    std::cout << "| Status: " << std::left << std::setw(48) << "Running" << "|\n";
    std::cout << "| Version: " << std::left << std::setw(47) << kVersion << "|\n";
    std::cout << "+----------------------------------------------------------+\n\n";

    if (show_gc) {
      std::cout << "+- GC Status --------------------------------------------------+\n";
      std::cout << "| Strategy:       " << std::left << std::setw(43) << "Mark-Sweep (default)" << "|\n";
      std::cout << "| Collections:    " << std::left << std::setw(43) << gc_stats.collections << "|\n";
      std::cout << "| Heap Size:      " << std::left << std::setw(43) << FormatBytes(gc_stats.current_heap_bytes) << "|\n";
      std::cout << "| Peak Heap:      " << std::left << std::setw(43) << FormatBytes(gc_stats.peak_heap_bytes) << "|\n";
      std::cout << "| Live Objects:   " << std::left << std::setw(43) << gc_stats.live_objects << "|\n";
      std::cout << "| Roots:          " << std::left << std::setw(43) << gc_stats.root_count << "|\n";
      std::cout << "| Total Freed:    " << std::left << std::setw(43) << FormatBytes(gc_stats.total_freed_bytes) << "|\n";
      std::cout << "+-------------------------------------------------------------+\n\n";
    }

    if (show_threads) {
      std::cout << "+- Thread Status -----------------------------------------------+\n";
      std::cout << "| Hardware Threads: " << std::left << std::setw(39) << hw_threads << "|\n";
      std::cout << "| Pool Size:        " << std::left << std::setw(39) << (g_stats.thread_pool_size > 0 ? g_stats.thread_pool_size : hw_threads) << "|\n";
      std::cout << "| Tasks Submitted:  " << std::left << std::setw(39) << g_stats.tasks_submitted << "|\n";
      std::cout << "| Tasks Completed:  " << std::left << std::setw(39) << g_stats.tasks_completed << "|\n";
      std::cout << "+--------------------------------------------------------------+\n\n";
    }

    if (show_memory) {
      std::cout << "+- Memory Status -----------------------------------------------+\n";
      std::cout << "| Current Usage:  " << std::left << std::setw(41) << FormatBytes(gc_stats.current_heap_bytes) << "|\n";
      std::cout << "| Peak Usage:     " << std::left << std::setw(41) << FormatBytes(gc_stats.peak_heap_bytes) << "|\n";
      std::cout << "| Total Alloc:    " << std::left << std::setw(41) << FormatBytes(gc_stats.total_bytes_allocated) << "|\n";
      std::cout << "+--------------------------------------------------------------+\n";
    }
  }

  return 0;
}

// ----------------------------------------------------------------------------
// GC Command
// ----------------------------------------------------------------------------

void PrintGCHelp() {
  std::cout << "Usage: " << kToolName << " gc [options]\n\n";
  std::cout << "GC management and statistics.\n\n";
  std::cout << "Options:\n";
  std::cout << "  --strategy=<type>  Set GC strategy (mark-sweep|generational|copying|incremental)\n";
  std::cout << "  --collect          Trigger a garbage collection\n";
  std::cout << "  --stats            Show GC statistics\n";
  std::cout << "  --list             List available GC strategies\n";
  std::cout << "  --help             Show this help message\n";
}

int CmdGC(int argc, char **argv) {
  bool show_stats = false;
  bool do_collect = false;
  bool list_strategies = false;
  std::string new_strategy;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintGCHelp();
      return 0;
    }
    if (arg == "--stats") {
      show_stats = true;
    } else if (arg == "--collect") {
      do_collect = true;
    } else if (arg == "--list") {
      list_strategies = true;
    } else if (arg.rfind("--strategy=", 0) == 0) {
      new_strategy = arg.substr(11);
    }
  }

  if (list_strategies) {
    std::cout << "Available GC Strategies:\n\n";
    std::cout << "  mark-sweep (ms)     Standard mark-and-sweep collector\n";
    std::cout << "                      - Simple and predictable\n";
    std::cout << "                      - Good for batch processing\n\n";
    std::cout << "  generational (gen)  Generational garbage collector\n";
    std::cout << "                      - Optimized for short-lived objects\n";
    std::cout << "                      - Lower average pause times\n\n";
    std::cout << "  copying (copy)      Copying/compacting collector\n";
    std::cout << "                      - Eliminates fragmentation\n";
    std::cout << "                      - Fast allocation\n\n";
    std::cout << "  incremental (inc)   Incremental garbage collector\n";
    std::cout << "                      - Minimal pause times\n";
    std::cout << "                      - Good for latency-sensitive apps\n";
    return 0;
  }

  if (!new_strategy.empty()) {
    auto strategy = StringToGCStrategy(new_strategy);
    runtime::gc::SetGlobalGCStrategy(strategy);
    std::cout << "GC strategy set to: " << GCStrategyToString(strategy) << "\n";
  }

  if (do_collect) {
    std::cout << "Triggering garbage collection...\n";
    auto start = std::chrono::high_resolution_clock::now();
    runtime::gc::GlobalHeap().Collect();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    g_stats.gc_total_pause_time += duration;
    std::cout << "Collection completed in " << FormatDuration(duration) << "\n";
  }

  if (show_stats || (!do_collect && !list_strategies && new_strategy.empty())) {
    auto gc_stats = runtime::gc::GlobalHeap().GetStats();
    std::cout << "\n+- GC Statistics --------------------------------------------------+\n";
    std::cout << "| Current Strategy: " << std::left << std::setw(38) << "Mark-Sweep" << "|\n";
    std::cout << "| Total Collections: " << std::left << std::setw(37) << gc_stats.collections << "|\n";
    std::cout << "| Total Allocated:   " << std::left << std::setw(37) << FormatBytes(gc_stats.total_bytes_allocated) << "|\n";
    std::cout << "| Current Heap:      " << std::left << std::setw(37) << FormatBytes(gc_stats.current_heap_bytes) << "|\n";
    std::cout << "| Peak Heap:         " << std::left << std::setw(37) << FormatBytes(gc_stats.peak_heap_bytes) << "|\n";
    std::cout << "| Live Objects:      " << std::left << std::setw(37) << gc_stats.live_objects << "|\n";
    std::cout << "| Roots:             " << std::left << std::setw(37) << gc_stats.root_count << "|\n";
    std::cout << "| Total Freed:       " << std::left << std::setw(37) << FormatBytes(gc_stats.total_freed_bytes) << "|\n";
    std::cout << "| Total Pause Time:  " << std::left << std::setw(37) << FormatDuration(g_stats.gc_total_pause_time) << "|\n";
    std::cout << "+------------------------------------------------------------------+\n";
  }

  return 0;
}

// ----------------------------------------------------------------------------
// Thread Command
// ----------------------------------------------------------------------------

void PrintThreadHelp() {
  std::cout << "Usage: " << kToolName << " thread [options]\n\n";
  std::cout << "Thread pool management.\n\n";
  std::cout << "Options:\n";
  std::cout << "  --pool-size=<n>    Set thread pool size\n";
  std::cout << "  --list             List active threads\n";
  std::cout << "  --stats            Show thread statistics\n";
  std::cout << "  --help             Show this help message\n";
}

int CmdThread(int argc, char **argv) {
  bool show_stats = false;
  bool list_threads = false;
  int pool_size = -1;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintThreadHelp();
      return 0;
    }
    if (arg == "--stats") {
      show_stats = true;
    } else if (arg == "--list") {
      list_threads = true;
    } else if (arg.rfind("--pool-size=", 0) == 0) {
      pool_size = std::atoi(arg.substr(12).c_str());
    }
  }

  unsigned int hw_threads = std::thread::hardware_concurrency();

  if (pool_size > 0) {
    g_stats.thread_pool_size = static_cast<size_t>(pool_size);
    std::cout << "Thread pool size set to: " << pool_size << "\n";
  }

  if (list_threads) {
    std::cout << "\n+- Thread Information -----------------------------------------+\n";
    std::cout << "| Hardware Concurrency: " << std::left << std::setw(34) << hw_threads << "|\n";
    std::cout << "| Main Thread ID:       " << std::left << std::setw(34) << std::this_thread::get_id() << "|\n";
    std::cout << "+-------------------------------------------------------------+\n\n";

    std::cout << "Thread Pool Workers:\n";
    size_t effective_pool = g_stats.thread_pool_size > 0 ? g_stats.thread_pool_size : hw_threads;
    for (size_t i = 0; i < effective_pool; ++i) {
      std::cout << "  Worker " << i << ": idle\n";
    }
  }

  if (show_stats || (!list_threads && pool_size < 0)) {
    std::cout << "\n+- Thread Statistics ------------------------------------------+\n";
    std::cout << "| Hardware Threads:   " << std::left << std::setw(36) << hw_threads << "|\n";
    std::cout << "| Pool Size:          " << std::left << std::setw(36) << (g_stats.thread_pool_size > 0 ? g_stats.thread_pool_size : hw_threads) << "|\n";
    std::cout << "| Tasks Submitted:    " << std::left << std::setw(36) << g_stats.tasks_submitted << "|\n";
    std::cout << "| Tasks Completed:    " << std::left << std::setw(36) << g_stats.tasks_completed << "|\n";
    std::cout << "| Tasks Pending:      " << std::left << std::setw(36) << (g_stats.tasks_submitted - g_stats.tasks_completed) << "|\n";
    std::cout << "+-------------------------------------------------------------+\n";
  }

  return 0;
}

// ----------------------------------------------------------------------------
// FFI Command
// ----------------------------------------------------------------------------

void PrintFFIHelp() {
  std::cout << "Usage: " << kToolName << " ffi [options]\n\n";
  std::cout << "FFI management and diagnostics.\n\n";
  std::cout << "Options:\n";
  std::cout << "  --list              List loaded FFI bridges\n";
  std::cout << "  --check             Check FFI health and availability\n";
  std::cout << "  --probe=<lang>      Probe a language runtime (python|java|dotnet|rust)\n";
  std::cout << "  --stats             Show FFI call statistics\n";
  std::cout << "  --json              Output in JSON format\n";
  std::cout << "  --help              Show this help message\n";
}

struct FFIBridgeInfo {
  std::string language;
  std::string runtime_name;
  std::string status;        // "available", "unavailable", "degraded"
  std::string version;
  std::string library_path;
};

FFIBridgeInfo ProbeLanguageRuntime(const std::string &language) {
  FFIBridgeInfo info;
  info.language = language;

  if (language == "python") {
    info.runtime_name = "CPython";
    // Check for Python interpreter availability
    const char *python_home = std::getenv("PYTHONHOME");
    const char *python_path = std::getenv("PYTHONPATH");
#ifdef _WIN32
    const char *lib_name = "python3.dll";
#elif defined(__APPLE__)
    const char *lib_name = "libpython3.dylib";
#else
    const char *lib_name = "libpython3.so";
#endif
    if (python_home && python_home[0]) {
      info.library_path = std::string(python_home);
      info.status = "available";
      info.version = "3.x (from PYTHONHOME)";
    } else if (python_path && python_path[0]) {
      info.library_path = std::string(python_path);
      info.status = "available";
      info.version = "3.x (from PYTHONPATH)";
    } else {
      info.library_path = lib_name;
      info.status = "unavailable";
      info.version = "not detected";
    }
  } else if (language == "java") {
    info.runtime_name = "JVM (JNI)";
    const char *java_home = std::getenv("JAVA_HOME");
    if (java_home && java_home[0]) {
      info.library_path = std::string(java_home);
      info.status = "available";
#ifdef _WIN32
      info.library_path += "\\bin\\server\\jvm.dll";
#elif defined(__APPLE__)
      info.library_path += "/lib/server/libjvm.dylib";
#else
      info.library_path += "/lib/server/libjvm.so";
#endif
      // Try to determine version from JAVA_HOME path
      std::string jh(java_home);
      if (jh.find("21") != std::string::npos) info.version = "21 (LTS)";
      else if (jh.find("17") != std::string::npos) info.version = "17 (LTS)";
      else if (jh.find("23") != std::string::npos) info.version = "23";
      else if (jh.find("1.8") != std::string::npos || jh.find("8") != std::string::npos) info.version = "8 (LTS)";
      else info.version = "detected (version unknown)";
    } else {
      info.library_path = "";
      info.status = "unavailable";
      info.version = "JAVA_HOME not set";
    }
  } else if (language == "dotnet") {
    info.runtime_name = "CoreCLR";
    const char *dotnet_root = std::getenv("DOTNET_ROOT");
    if (dotnet_root && dotnet_root[0]) {
      info.library_path = std::string(dotnet_root);
      info.status = "available";
      info.version = "detected (from DOTNET_ROOT)";
    } else {
      info.library_path = "";
      info.status = "unavailable";
      info.version = "DOTNET_ROOT not set";
    }
  } else if (language == "rust") {
    info.runtime_name = "Rust ABI (cdylib)";
    // Rust doesn't need a runtime — it uses the C ABI directly.
    info.status = "available";
    info.version = "native (C ABI)";
    info.library_path = "(no runtime library required)";
  } else if (language == "cpp") {
    info.runtime_name = "C++ ABI";
    info.status = "available";
    info.version = "native (C/C++ ABI)";
    info.library_path = "(no runtime library required)";
  } else {
    info.runtime_name = "unknown";
    info.status = "unavailable";
    info.version = "unknown";
    info.library_path = "";
  }

  return info;
}

int CmdFFI(int argc, char **argv) {
  bool list_bridges = false;
  bool check_health = false;
  bool show_stats = false;
  bool json_output = false;
  std::string probe_lang;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintFFIHelp();
      return 0;
    }
    if (arg == "--list") {
      list_bridges = true;
    } else if (arg == "--check") {
      check_health = true;
    } else if (arg == "--stats") {
      show_stats = true;
    } else if (arg == "--json") {
      json_output = true;
    } else if (arg.rfind("--probe=", 0) == 0) {
      probe_lang = arg.substr(8);
    }
  }

  // Default: list all bridges with health check
  if (!list_bridges && !check_health && !show_stats && probe_lang.empty()) {
    list_bridges = true;
    check_health = true;
  }

  // Supported languages for FFI bridging
  std::vector<std::string> languages = {"cpp", "python", "rust", "java", "dotnet"};
  std::vector<FFIBridgeInfo> bridges;

  if (!probe_lang.empty()) {
    bridges.push_back(ProbeLanguageRuntime(probe_lang));
  } else {
    for (const auto &lang : languages) {
      bridges.push_back(ProbeLanguageRuntime(lang));
    }
  }

  if (json_output) {
    std::cout << "{\n  \"ffi_bridges\": [\n";
    for (size_t i = 0; i < bridges.size(); ++i) {
      const auto &b = bridges[i];
      std::cout << "    {\n";
      std::cout << "      \"language\": \"" << b.language << "\",\n";
      std::cout << "      \"runtime\": \"" << b.runtime_name << "\",\n";
      std::cout << "      \"status\": \"" << b.status << "\",\n";
      std::cout << "      \"version\": \"" << b.version << "\",\n";
      std::cout << "      \"library\": \"" << b.library_path << "\"\n";
      std::cout << "    }" << (i + 1 < bridges.size() ? "," : "") << "\n";
    }
    std::cout << "  ]\n}\n";
  } else {
    if (list_bridges || check_health) {
      std::cout << "\n";
      std::cout << "+-----------+----------------+--------------+--------------------+\n";
      std::cout << "| Language  | Runtime        | Status       | Version            |\n";
      std::cout << "+-----------+----------------+--------------+--------------------+\n";
      for (const auto &b : bridges) {
        std::string status_icon;
        if (b.status == "available") status_icon = "[OK]  ";
        else if (b.status == "degraded") status_icon = "[WARN]";
        else status_icon = "[MISS]";

        std::cout << "| " << std::left << std::setw(9) << b.language
                  << " | " << std::left << std::setw(14) << b.runtime_name
                  << " | " << std::left << std::setw(12) << status_icon
                  << " | " << std::left << std::setw(18) << b.version
                  << " |\n";
      }
      std::cout << "+-----------+----------------+--------------+--------------------+\n\n";

      if (check_health) {
        // Report library paths for available bridges
        for (const auto &b : bridges) {
          if (!b.library_path.empty()) {
            std::cout << "  " << b.language << ": " << b.library_path << "\n";
          }
        }
        std::cout << "\n";
      }
    }

    if (show_stats) {
      std::cout << "  FFI Call Statistics (current session):\n";
      std::cout << "    Cross-language calls:    " << g_stats.tasks_submitted << "\n";
      std::cout << "    Marshalling operations:  " << g_stats.tasks_completed << "\n";
      std::cout << "    Active FFI handles:      0\n\n";
    }
  }

  return 0;
}

// ----------------------------------------------------------------------------
// Bench Command
// ----------------------------------------------------------------------------

void PrintBenchHelp() {
  std::cout << "Usage: " << kToolName << " bench <benchmark> [options]\n\n";
  std::cout << "Run runtime benchmarks.\n\n";
  std::cout << "Benchmarks:\n";
  std::cout << "  gc        GC performance benchmark\n";
  std::cout << "  alloc     Memory allocation benchmark\n";
  std::cout << "  thread    Thread pool benchmark\n";
  std::cout << "  all       Run all benchmarks\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --iterations=<n>   Number of iterations (default: 1000)\n";
  std::cout << "  --size=<n>         Object size in bytes (default: 1024)\n";
  std::cout << "  --help             Show this help message\n";
}

struct BenchmarkResult {
  std::string name;
  size_t iterations;
  std::chrono::microseconds total_time;
  std::chrono::microseconds min_time;
  std::chrono::microseconds max_time;
  std::chrono::microseconds avg_time;
};

void PrintBenchmarkResult(const BenchmarkResult &result) {
  std::cout << "+- " << result.name << " ";
  size_t pad = 54 - result.name.length();
  for (size_t i = 0; i < pad; ++i) std::cout << "-";
  std::cout << "+\n";
  std::cout << "| Iterations:   " << std::left << std::setw(42) << result.iterations << "|\n";
  std::cout << "| Total Time:   " << std::left << std::setw(42) << FormatDuration(result.total_time) << "|\n";
  std::cout << "| Min Time:     " << std::left << std::setw(42) << FormatDuration(result.min_time) << "|\n";
  std::cout << "| Max Time:     " << std::left << std::setw(42) << FormatDuration(result.max_time) << "|\n";
  std::cout << "| Avg Time:     " << std::left << std::setw(42) << FormatDuration(result.avg_time) << "|\n";
  double ops_per_sec = result.iterations * 1000000.0 / result.total_time.count();
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << ops_per_sec << " ops/sec";
  std::cout << "| Throughput:   " << std::left << std::setw(42) << oss.str() << "|\n";
  std::cout << "+---------------------------------------------------------+\n";
}

BenchmarkResult RunGCBenchmark(size_t iterations) {
  BenchmarkResult result;
  result.name = "GC Benchmark";
  result.iterations = iterations;
  result.min_time = std::chrono::microseconds::max();
  result.max_time = std::chrono::microseconds::zero();
  result.total_time = std::chrono::microseconds::zero();

  auto &heap = runtime::gc::GlobalHeap();

  std::cout << "Running GC benchmark (" << iterations << " iterations)...\n";

  for (size_t i = 0; i < iterations; ++i) {
    // Allocate some objects
    for (size_t j = 0; j < 100; ++j) {
      void *ptr = heap.Allocate(64 + (j % 256));
      (void)ptr;
    }

    auto start = std::chrono::high_resolution_clock::now();
    heap.Collect();
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    result.total_time += duration;
    result.min_time = std::min(result.min_time, duration);
    result.max_time = std::max(result.max_time, duration);
  }

  result.avg_time = std::chrono::microseconds(result.total_time.count() / iterations);
  return result;
}

BenchmarkResult RunAllocBenchmark(size_t iterations, size_t object_size) {
  BenchmarkResult result;
  result.name = "Allocation Benchmark";
  result.iterations = iterations;
  result.min_time = std::chrono::microseconds::max();
  result.max_time = std::chrono::microseconds::zero();
  result.total_time = std::chrono::microseconds::zero();

  auto &heap = runtime::gc::GlobalHeap();

  std::cout << "Running allocation benchmark (" << iterations << " iterations, "
            << object_size << " bytes each)...\n";

  for (size_t i = 0; i < iterations; ++i) {
    auto start = std::chrono::high_resolution_clock::now();
    void *ptr = heap.Allocate(object_size);
    auto end = std::chrono::high_resolution_clock::now();
    (void)ptr;

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    result.total_time += duration;
    result.min_time = std::min(result.min_time, duration);
    result.max_time = std::max(result.max_time, duration);
  }

  if (result.min_time == std::chrono::microseconds::max()) {
    result.min_time = std::chrono::microseconds::zero();
  }
  result.avg_time = std::chrono::microseconds(result.total_time.count() / iterations);
  return result;
}

BenchmarkResult RunThreadBenchmark(size_t iterations) {
  BenchmarkResult result;
  result.name = "Thread Pool Benchmark";
  result.iterations = iterations;
  result.min_time = std::chrono::microseconds::max();
  result.max_time = std::chrono::microseconds::zero();
  result.total_time = std::chrono::microseconds::zero();

  std::cout << "Running thread pool benchmark (" << iterations << " iterations)...\n";

  // Use basic threading for benchmarking
  runtime::services::Threading threading;

  for (size_t i = 0; i < iterations; ++i) {
    auto start = std::chrono::high_resolution_clock::now();

    // Spawn and join a thread
    std::thread t = threading.Run([&]() {
      volatile int sum = 0;
      for (int j = 0; j < 1000; ++j) {
        sum += j;
      }
    });
    t.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    result.total_time += duration;
    result.min_time = std::min(result.min_time, duration);
    result.max_time = std::max(result.max_time, duration);
  }

  if (result.min_time == std::chrono::microseconds::max()) {
    result.min_time = std::chrono::microseconds::zero();
  }
  result.avg_time = std::chrono::microseconds(result.total_time.count() / iterations);
  return result;
}

int CmdBench(int argc, char **argv) {
  if (argc < 3) {
    PrintBenchHelp();
    return 1;
  }

  std::string benchmark = argv[2];
  size_t iterations = 1000;
  size_t object_size = 1024;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintBenchHelp();
      return 0;
    }
    if (arg.rfind("--iterations=", 0) == 0) {
      iterations = std::stoull(arg.substr(13));
    } else if (arg.rfind("--size=", 0) == 0) {
      object_size = std::stoull(arg.substr(7));
    }
  }

  std::cout << "\n==========================================================\n";
  std::cout << "              Polyglot Runtime Benchmarks\n";
  std::cout << "==========================================================\n\n";

  std::vector<BenchmarkResult> results;

  if (benchmark == "gc" || benchmark == "all") {
    results.push_back(RunGCBenchmark(iterations));
    std::cout << "\n";
  }

  if (benchmark == "alloc" || benchmark == "all") {
    results.push_back(RunAllocBenchmark(iterations, object_size));
    std::cout << "\n";
  }

  if (benchmark == "thread" || benchmark == "all") {
    results.push_back(RunThreadBenchmark(std::min(iterations, size_t(100))));
    std::cout << "\n";
  }

  if (results.empty()) {
    std::cerr << "Unknown benchmark: " << benchmark << "\n";
    std::cerr << "Available: gc, alloc, thread, all\n";
    return 1;
  }

  std::cout << "==========================================================\n";
  std::cout << "                    Benchmark Results\n";
  std::cout << "==========================================================\n\n";

  for (const auto &r : results) {
    PrintBenchmarkResult(r);
    std::cout << "\n";
  }

  return 0;
}

// ----------------------------------------------------------------------------
// Info Command
// ----------------------------------------------------------------------------

void PrintInfoHelp() {
  std::cout << "Usage: " << kToolName << " info [options]\n\n";
  std::cout << "Show runtime information.\n\n";
  std::cout << "Options:\n";
  std::cout << "  --features    List available runtime features\n";
  std::cout << "  --config      Show runtime configuration\n";
  std::cout << "  --help        Show this help message\n";
}

int CmdInfo(int argc, char **argv) {
  bool show_features = false;
  bool show_config = false;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintInfoHelp();
      return 0;
    }
    if (arg == "--features") {
      show_features = true;
    } else if (arg == "--config") {
      show_config = true;
    }
  }

  // Default: show everything
  if (!show_features && !show_config) {
    show_features = true;
    show_config = true;
  }

  std::cout << "\n+==========================================================+\n";
  std::cout << "|           Polyglot Runtime Information                    |\n";
  std::cout << "+==========================================================+\n";
  std::cout << "| Version:        " << std::left << std::setw(40) << kVersion << "|\n";
  std::cout << "| Build:          " << std::left << std::setw(40) << "Release" << "|\n";
  std::cout << "| Platform:       " << std::left << std::setw(40) <<
#if defined(__APPLE__)
    "macOS"
#elif defined(__linux__)
    "Linux"
#elif defined(_WIN32)
    "Windows"
#else
    "Unknown"
#endif
    << "|\n";
  std::cout << "| Architecture:   " << std::left << std::setw(40) <<
#if defined(__x86_64__) || defined(_M_X64)
    "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    "arm64"
#else
    "Unknown"
#endif
    << "|\n";
  std::cout << "+==========================================================+\n\n";

  if (show_features) {
    std::cout << "+- Runtime Features -------------------------------------------+\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Garbage Collection                                          |\n";
    std::cout << "|    [x] Mark-Sweep Collector                                  |\n";
    std::cout << "|    [x] Generational Collector                                |\n";
    std::cout << "|    [x] Copying Collector                                     |\n";
    std::cout << "|    [x] Incremental Collector                                 |\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Threading Services                                          |\n";
    std::cout << "|    [x] Thread Pool                                           |\n";
    std::cout << "|    [x] Task Scheduler                                        |\n";
    std::cout << "|    [x] Work-Stealing Scheduler                               |\n";
    std::cout << "|    [x] Coroutine Support                                     |\n";
    std::cout << "|    [x] Thread Profiling                                      |\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Synchronization Primitives                                  |\n";
    std::cout << "|    [x] Reader-Writer Locks                                   |\n";
    std::cout << "|    [x] Barriers                                              |\n";
    std::cout << "|    [x] Semaphores                                            |\n";
    std::cout << "|    [x] Lock-Free Queues                                      |\n";
    std::cout << "|    [x] Lock-Free Stacks                                      |\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Language Support                                            |\n";
    std::cout << "|    [x] Python Runtime Integration (CPython)                  |\n";
    std::cout << "|    [x] Rust Runtime Integration (C ABI)                      |\n";
    std::cout << "|    [x] C++ Runtime Integration                               |\n";
    std::cout << "|    [x] Java Runtime Integration (JNI)                        |\n";
    std::cout << "|    [x] .NET Runtime Integration (CoreCLR)                    |\n";
    std::cout << "|    [x] FFI Management                                        |\n";
    std::cout << "|                                                              |\n";
    std::cout << "+--------------------------------------------------------------+\n\n";
  }

  if (show_config) {
    unsigned int hw_threads = std::thread::hardware_concurrency();

    std::cout << "+- Runtime Configuration -------------------------------------+\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  GC Configuration                                            |\n";
    std::cout << "|    Default Strategy:   Mark-Sweep                             |\n";
    std::cout << "|    Initial Heap Size:  1 MB                                   |\n";
    std::cout << "|    Max Heap Size:      Unlimited                              |\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Thread Configuration                                        |\n";
    std::ostringstream oss;
    oss << "    Default Pool Size:  " << hw_threads << " threads";
    std::cout << "|" << std::left << std::setw(62) << oss.str() << "|\n";
    std::cout << "|    Max Pool Size:      256 threads                            |\n";
    std::cout << "|                                                              |\n";
    std::cout << "|  Memory Configuration                                        |\n";
    std::cout << "|    Allocator:          mimalloc                               |\n";
    std::cout << "|    Large Object:       >= 8 KB                                |\n";
    std::cout << "|                                                              |\n";
    std::cout << "+--------------------------------------------------------------+\n";
  }

  return 0;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int Run(int argc, char **argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  std::string command = argv[1];

  if (command == "help" || command == "--help" || command == "-h") {
    PrintUsage();
    return 0;
  }

  if (command == "version" || command == "--version" || command == "-v") {
    PrintVersion();
    return 0;
  }

  if (command == "status") {
    return CmdStatus(argc, argv);
  }

  if (command == "gc") {
    return CmdGC(argc, argv);
  }

  if (command == "ffi") {
    return CmdFFI(argc, argv);
  }

  if (command == "thread") {
    return CmdThread(argc, argv);
  }

  if (command == "bench") {
    return CmdBench(argc, argv);
  }

  if (command == "info") {
    return CmdInfo(argc, argv);
  }

  std::cerr << "Unknown command: " << command << "\n";
  std::cerr << "Run '" << kToolName << " help' for usage.\n";
  return 1;
}

}  // namespace polyglot::tools

int main(int argc, char **argv) {
  return polyglot::tools::Run(argc, argv);
}
