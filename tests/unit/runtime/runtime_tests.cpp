#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>

#include "runtime/include/gc/heap.h"
#include "runtime/include/gc/runtime.h"
#include "runtime/include/gc/root_guard.h"
#include "runtime/include/interop/ffi.h"
#include "runtime/include/interop/memory.h"
#include "runtime/include/interop/marshalling.h"
#include "runtime/include/interop/type_mapping.h"
#include "runtime/include/interop/calling_convention.h"
#include "backends/common/include/debug_info.h"
#include "backends/common/include/debug_emitter.h"
#include "runtime/include/services/exception.h"
#include "runtime/include/services/threading.h"

extern "C" {
#include "runtime/include/libs/base.h"
}

using namespace polyglot::runtime;

// Helper to build a cross-platform temporary file path.
static std::string TmpPath(const char *name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

TEST_CASE("Runtime error captures stack trace", "[runtime][exception]") {
  try {
    services::ThrowRuntimeError("boom");
    FAIL("Expected ThrowRuntimeError to throw");
  } catch (const services::RuntimeError &err) {
    REQUIRE(err.StackTrace().size() > 0);
  }
}

TEST_CASE("C root guard keeps allocation alive", "[runtime][gc][root]") {
  void *ptr = polyglot_alloc(16);
  POLYGLOT_WITH_ROOT(guard, &ptr);
  REQUIRE(ptr != nullptr);
  polyglot_gc_collect();
  REQUIRE(ptr != nullptr);
}

TEST_CASE("Thread-local storage isolates values", "[runtime][threading][tls]") {
  services::ThreadLocalStorage tls;
  tls.Set("key", reinterpret_cast<void *>(0x1));
  REQUIRE(tls.Get("key") == reinterpret_cast<void *>(0x1));

  services::Threading threading;
  void *thread_value = reinterpret_cast<void *>(0x2);
  auto worker = threading.Run([&]() {
    services::ThreadLocalStorage local_tls;
    local_tls.Set("key", thread_value);
    REQUIRE(local_tls.Get("key") == thread_value);
    REQUIRE(tls.Get("key") == reinterpret_cast<void *>(0x1));
  });
  worker.join();
  REQUIRE(tls.Get("key") == reinterpret_cast<void *>(0x1));
}

TEST_CASE("GC allocations survive across threads with roots", "[runtime][gc][threading]") {
  gc::Heap heap(gc::Strategy::kMarkSweep);
  services::Threading threading;

  auto task = [&](int index) {
    void *ptr = heap.Allocate(64);
    auto handle = heap.Track(&ptr);
    REQUIRE(ptr != nullptr);
    (void)handle;
    heap.Collect();
    // Pointer should remain valid while root is registered; we only check non-null.
    REQUIRE(ptr != nullptr);
  };

  std::thread t1 = threading.Run([&]() { task(1); });
  std::thread t2 = threading.Run([&]() { task(2); });
  t1.join();
  t2.join();
}

TEST_CASE("Foreign ownership respects deleter for owned and shared", "[runtime][interop]") {
  using interop::ForeignObject;
  using interop::Ownership;
  int destroyed = 0;
  auto deleter = [&](void *) { ++destroyed; };

  ForeignObject *owned = interop::AcquireForeign(reinterpret_cast<void *>(0x1), 1, deleter, Ownership::kOwned);
  ForeignObject *borrowed = interop::AcquireForeign(reinterpret_cast<void *>(0x2), 1, deleter, Ownership::kBorrowed);
  ForeignObject *shared = interop::AcquireForeign(reinterpret_cast<void *>(0x3), 1, deleter, Ownership::kShared);
  interop::RetainForeign(shared);

  interop::ReleaseForeign(owned);
  interop::ReleaseForeign(borrowed);
  interop::ReleaseForeign(shared);
  interop::ReleaseForeign(shared);

  // Owned and shared both fire their deleter on final release; borrowed does not.
  REQUIRE(destroyed == 2);
}

TEST_CASE("FFI binding helpers tag ownership", "[runtime][interop][ffi]") {
  int freed = 0;
  auto deleter = [&](void *ptr) {
    (void)ptr;
    ++freed;
  };

  void *addr = reinterpret_cast<void *>(0x1234);
  interop::ForeignObject *borrowed = interop::BindBorrowed("f", addr, 0);
  interop::ForeignObject *owned = interop::BindOwned("g", addr, 0, deleter);
  interop::ForeignObject *shared = interop::BindShared("h", addr, 0, deleter);
  interop::RetainForeign(shared);

  interop::ReleaseForeign(borrowed);
  interop::ReleaseForeign(owned);
  interop::ReleaseForeign(shared);
  interop::ReleaseForeign(shared);

  // Both owned and shared objects invoke their deleter on final release.
  REQUIRE(freed == 2);
}

TEST_CASE("Marshalling handles endianness", "[runtime][interop][marshal]") {
  auto le = interop::MarshalInt(0x1122334455667788ULL, 4, interop::Endianness::kLittle);
  auto be = interop::MarshalInt(0x11223344ULL, 4, interop::Endianness::kBig);
  REQUIRE(le.size() == 4);
  REQUIRE(be.size() == 4);
  REQUIRE(le[0] == 0x88);
  REQUIRE(le[3] == 0x55);
  REQUIRE(be[0] == 0x11);
  REQUIRE(be[3] == 0x44);
  REQUIRE(interop::UnmarshalInt(le, interop::Endianness::kLittle) == 0x55667788ULL);
  REQUIRE(interop::UnmarshalInt(be, interop::Endianness::kBig) == 0x11223344ULL);
}

TEST_CASE("Type mapping resolves builtin types", "[runtime][interop][types]") {
  auto i32 = interop::MapBuiltinType("c", "int");
  REQUIRE(i32.size == 4);
  REQUIRE(!i32.is_float);
  auto f64 = interop::MapBuiltinType("c", "double");
  REQUIRE(f64.is_float);
  auto unknown = interop::MapBuiltinType("rust", "u128");
  REQUIRE(unknown.size == 0);
}

TEST_CASE("Calling convention validation checks sizes", "[runtime][interop][callconv]") {
  interop::ForeignSignature sig;
  sig.convention = {interop::CallingConventionKind::kSysV, interop::Endianness::kLittle, false};
  sig.args.push_back({"i32", 4, 4, false, false});
  sig.result = {"i64", 8, 8, false, false};
  REQUIRE(interop::ValidateSignature(sig));
  sig.args[0].size = 0;
  REQUIRE_FALSE(interop::ValidateSignature(sig));
}

TEST_CASE("DebugInfoBuilder emits source map json", "[debug][sourcemap]") {
  polyglot::backends::DebugInfoBuilder dbg;
  dbg.AddLine({"main.c", 1, 1});
  dbg.AddVariable({"x", "i32", "main.c", 1, 0});
  dbg.AddType({"i32", "int", 4, 4});
  dbg.AddSymbol({"main", ".text", 0x1000, 10, true});
  auto json = dbg.EmitSourceMapJSON();
  REQUIRE(json.find("\"main.c\"") != std::string::npos);
  REQUIRE(json.find("\"main\"") != std::string::npos);
}

TEST_CASE("DebugEmitter emits DWARF debug info", "[debug][dwarf]") {
  polyglot::backends::DebugInfoBuilder dbg;
  
  // Add comprehensive debug information
  dbg.AddLine({"main.cpp", 5, 1});
  dbg.AddLine({"main.cpp", 6, 5});
  dbg.AddLine({"main.cpp", 7, 5});
  dbg.AddLine({"main.cpp", 8, 1});
  
  dbg.AddVariable({"argc", "int", "main.cpp", 5, 0});
  dbg.AddVariable({"argv", "char**", "main.cpp", 5, 0});
  dbg.AddVariable({"result", "int", "main.cpp", 6, 1});
  
  dbg.AddType({"int", "int", 4, 4});
  dbg.AddType({"char", "char", 1, 1});
  dbg.AddType({"char*", "pointer", 8, 8});
  dbg.AddType({"char**", "pointer", 8, 8});
  
  dbg.AddSymbol({"main", ".text", 0x1000, 0x100, true});
  dbg.AddSymbol({"helper_func", ".text", 0x1100, 0x50, true});
  
  // Test DWARF emission
  auto tmp_dwarf_s = TmpPath("polyglot_test_debug.o");
  const char *tmp_dwarf = tmp_dwarf_s.c_str();
  bool dwarf_result = polyglot::backends::DebugEmitter::EmitDWARF(dbg, tmp_dwarf);
  REQUIRE(dwarf_result == true);
  
  // Verify file was created and has content
  std::ifstream dwarf_file(tmp_dwarf, std::ios::binary | std::ios::ate);
  REQUIRE(dwarf_file.is_open());
  auto size = dwarf_file.tellg();
  REQUIRE(size > 0);
  
  // Read and verify ELF magic number
  dwarf_file.seekg(0);
  char magic[4];
  dwarf_file.read(magic, 4);
  REQUIRE(magic[0] == 0x7F);
  REQUIRE(magic[1] == 'E');
  REQUIRE(magic[2] == 'L');
  REQUIRE(magic[3] == 'F');
  
  // Cleanup
  std::remove(tmp_dwarf);
}

TEST_CASE("DebugEmitter emits PDB debug info", "[debug][pdb]") {
  polyglot::backends::DebugInfoBuilder dbg;
  
  // Add debug information
  dbg.AddLine({"main.cpp", 1, 1});
  dbg.AddSymbol({"WinMain", ".text", 0x1000, 0x100, true});
  dbg.AddType({"int", "int", 4, 4});
  
  // Test PDB emission
  auto tmp_pdb_s = TmpPath("polyglot_test_debug.pdb");
  const char *tmp_pdb = tmp_pdb_s.c_str();
  bool pdb_result = polyglot::backends::DebugEmitter::EmitPDB(dbg, tmp_pdb);
  REQUIRE(pdb_result == true);
  
  // Verify file was created and has content
  std::ifstream pdb_file(tmp_pdb, std::ios::binary | std::ios::ate);
  REQUIRE(pdb_file.is_open());
  auto size = pdb_file.tellg();
  REQUIRE(size > 0);
  
  // Read and verify PDB magic header (starts with "Microsoft C/C++ MSF")
  pdb_file.seekg(0);
  char magic[20];
  pdb_file.read(magic, 20);
  std::string magic_str(magic, 20);
  REQUIRE(magic_str.find("Microsoft C/C++ MSF") == 0);
  
  // Cleanup
  std::remove(tmp_pdb);
}

TEST_CASE("DebugEmitter source map JSON format", "[debug][sourcemap]") {
  polyglot::backends::DebugInfoBuilder dbg;
  
  dbg.AddLine({"test.cpp", 10, 5});
  dbg.AddVariable({"x", "double", "test.cpp", 10, 0});
  dbg.AddType({"double", "float", 8, 8});
  dbg.AddSymbol({"compute", ".text", 0x2000, 0x200, true});
  
  // Test source map emission
  auto tmp_map_s = TmpPath("polyglot_test.map");
  const char *tmp_map = tmp_map_s.c_str();
  bool map_result = polyglot::backends::DebugEmitter::EmitSourceMap(dbg, tmp_map);
  REQUIRE(map_result == true);
  
  // Read and verify JSON content
  std::ifstream map_file(tmp_map);
  REQUIRE(map_file.is_open());
  
  std::string content((std::istreambuf_iterator<char>(map_file)),
                       std::istreambuf_iterator<char>());
  
  REQUIRE(content.find("\"lines\"") != std::string::npos);
  REQUIRE(content.find("\"variables\"") != std::string::npos);
  REQUIRE(content.find("\"types\"") != std::string::npos);
  REQUIRE(content.find("\"symbols\"") != std::string::npos);
  REQUIRE(content.find("\"test.cpp\"") != std::string::npos);
  REQUIRE(content.find("\"compute\"") != std::string::npos);
  
  // Cleanup
  std::remove(tmp_map);
}

TEST_CASE("Base library memory and IO", "[runtime][libbase]") {
  char buf[8];
  polyglot_memset(buf, 'a', 7);
  buf[7] = '\0';
  REQUIRE(polyglot_strlen(buf) == 7);
  REQUIRE(polyglot_strncmp(buf, "aaaaaaa", 7) == 0);

  // file IO using GC buffer
  auto tmp_s = TmpPath("polyglot_runtime_test.txt");
  const char *tmp = tmp_s.c_str();
  REQUIRE(polyglot_write_file(tmp, "hi", 2));
  char *file_buf = NULL;
  size_t file_size = 0;
  REQUIRE(polyglot_read_file(tmp, &file_buf, &file_size));
  REQUIRE(file_size == 2);
  REQUIRE(polyglot_strncmp(file_buf, "hi", 2) == 0);
  polyglot_free_file_buffer(file_buf);
}

// ============ PGO Optimizer Tests ============

#include "middle/include/pgo/profile_data.h"

using namespace polyglot::pgo;

TEST_CASE("PGO BranchProfile prediction analysis", "[pgo][branch]") {
  BranchProfile branch;
  branch.branch_id = 1;
  branch.source_block = 0;
  branch.taken_count = 950;
  branch.not_taken_count = 50;
  
  REQUIRE(branch.TakenProbability() == Catch::Approx(0.95).epsilon(0.01));
  REQUIRE(branch.IsHighlyPredictable() == true);
  REQUIRE(branch.TotalCount() == 1000);
  
  // Test uncertain branch
  BranchProfile uncertain;
  uncertain.branch_id = 2;
  uncertain.source_block = 1;
  uncertain.taken_count = 500;
  uncertain.not_taken_count = 500;
  
  REQUIRE(uncertain.TakenProbability() == Catch::Approx(0.5).epsilon(0.01));
  REQUIRE(uncertain.IsHighlyPredictable() == false);
}

TEST_CASE("PGO CallSiteProfile virtual target analysis", "[pgo][callsite]") {
  CallSiteProfile cs;
  cs.call_site_id = 1;
  cs.callee_name = "foo";
  cs.call_count = 1000;
  cs.callee_size = 50;
  cs.is_hot = true;
  
  // No virtual targets - monomorphic
  REQUIRE(cs.has_single_target() == true);
  auto target = cs.GetMostLikelyTarget();
  REQUIRE(target.first == "foo");
  REQUIRE(target.second == 1000);
  
  // Add virtual targets
  cs.target_distribution["DerivedA::foo"] = 800;
  cs.target_distribution["DerivedB::foo"] = 200;
  
  REQUIRE(cs.has_single_target() == false);
  auto likely = cs.GetMostLikelyTarget();
  REQUIRE(likely.first == "DerivedA::foo");
  REQUIRE(likely.second == 800);
  REQUIRE(cs.GetMostLikelyTargetProbability() == Catch::Approx(0.8).epsilon(0.01));
}

TEST_CASE("PGO FunctionProfile hot/cold block analysis", "[pgo][function]") {
  FunctionProfile func;
  func.function_name = "test_func";
  func.invocation_count = 100;
  func.total_time_ns = 10000.0;
  
  // Add basic blocks with varying execution counts
  for (size_t i = 0; i < 10; ++i) {
    BasicBlockProfile bb;
    bb.block_id = i;
    bb.execution_count = (i == 0) ? 10000 : (i < 3 ? 1000 : 10);
    bb.execution_time_ns = bb.execution_count * 10.0;
    func.basic_blocks.push_back(bb);
  }
  
  auto hot = func.GetHotBlocks();
  REQUIRE(hot.size() >= 1);
  REQUIRE(hot[0] == 0);  // Block 0 should be hottest
  
  auto cold = func.GetColdBlocks();
  REQUIRE(cold.size() >= 1);
}

TEST_CASE("PGO ProfileData storage and retrieval", "[pgo][profiledata]") {
  ProfileData data;
  
  FunctionProfile func1;
  func1.function_name = "func1";
  func1.invocation_count = 1000;
  func1.total_time_ns = 5000.0;
  data.AddFunctionProfile(func1);
  
  FunctionProfile func2;
  func2.function_name = "func2";
  func2.invocation_count = 500;
  func2.total_time_ns = 2500.0;
  data.AddFunctionProfile(func2);
  
  REQUIRE(data.GetFunctionCount() == 2);
  REQUIRE(data.GetTotalInvocationCount() == 1500);
  REQUIRE(data.GetAverageInvocationCount() == Catch::Approx(750.0).epsilon(0.01));
  
  auto names = data.GetFunctionNames();
  REQUIRE(names.size() == 2);
  
  const auto* retrieved = data.GetFunctionProfile("func1");
  REQUIRE(retrieved != nullptr);
  REQUIRE(retrieved->invocation_count == 1000);
  
  // Hot functions
  auto hot = data.GetHotFunctions(1);
  REQUIRE(hot.size() == 1);
  REQUIRE(hot[0] == "func1");
}

TEST_CASE("PGO ProfileData serialization", "[pgo][serialization]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "serialized_func";
  func.invocation_count = 12345;
  func.total_time_ns = 67890.0;
  
  BasicBlockProfile bb;
  bb.block_id = 0;
  bb.execution_count = 1000;
  bb.execution_time_ns = 500.0;
  func.basic_blocks.push_back(bb);
  
  BranchProfile br;
  br.branch_id = 0;
  br.taken_count = 900;
  br.not_taken_count = 100;
  func.branches.push_back(br);
  
  data.AddFunctionProfile(func);
  
  auto tmp_s = TmpPath("polyglot_pgo_test.json");
  const char* tmp = tmp_s.c_str();
  REQUIRE(data.SaveToFile(tmp) == true);
  
  ProfileData loaded;
  REQUIRE(loaded.LoadFromFile(tmp) == true);
  REQUIRE(loaded.GetFunctionCount() == 1);
  
  const auto* loaded_func = loaded.GetFunctionProfile("serialized_func");
  REQUIRE(loaded_func != nullptr);
  REQUIRE(loaded_func->invocation_count == 12345);
  REQUIRE(loaded_func->basic_blocks.size() == 1);
  REQUIRE(loaded_func->branches.size() == 1);
  
  std::remove(tmp);
}

TEST_CASE("PGOOptimizer inlining decisions", "[pgo][optimizer][inlining]") {
  ProfileData data;
  
  // Create caller function with hot call site
  FunctionProfile caller;
  caller.function_name = "caller";
  caller.invocation_count = 1000;
  caller.estimated_size = 200;
  
  CallSiteProfile cs;
  cs.call_site_id = 1;
  cs.callee_name = "small_callee";
  cs.call_count = 5000;  // Hot
  cs.callee_size = 20;   // Small
  cs.is_hot = true;
  caller.call_sites.push_back(cs);
  
  data.AddFunctionProfile(caller);
  
  // Create small callee
  FunctionProfile callee;
  callee.function_name = "small_callee";
  callee.invocation_count = 5000;
  callee.estimated_size = 20;
  data.AddFunctionProfile(callee);
  
  PGOOptimizer optimizer(data);
  auto decisions = optimizer.MakeInliningDecisions();
  
  REQUIRE(decisions.size() >= 1);
  
  // Find the decision for our call site
  bool found = false;
  for (const auto& d : decisions) {
    if (d.caller == "caller" && d.callee == "small_callee") {
      found = true;
      REQUIRE(d.should_inline == true);  // Hot + small should be inlined
      break;
    }
  }
  REQUIRE(found == true);
}

TEST_CASE("PGOOptimizer branch prediction hints", "[pgo][optimizer][branch]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "branch_func";
  func.invocation_count = 1000;
  
  // Highly predictable branch (99% taken)
  // Confidence = |0.99 - 0.5| * 2 = 0.98, well above 0.9 threshold
  BranchProfile br1;
  br1.branch_id = 1;
  br1.source_block = 0;
  br1.taken_count = 9900;
  br1.not_taken_count = 100;
  func.branches.push_back(br1);
  
  // Unpredictable branch (50/50)
  // Confidence = |0.5 - 0.5| * 2 = 0.0, below threshold
  BranchProfile br2;
  br2.branch_id = 2;
  br2.source_block = 1;
  br2.taken_count = 5000;
  br2.not_taken_count = 5000;
  func.branches.push_back(br2);
  
  data.AddFunctionProfile(func);
  
  PGOOptimizer optimizer(data);
  auto hints = optimizer.GetBranchPredictions();
  
  // Should only include the predictable branch (br1)
  bool found_predictable = false;
  for (const auto& h : hints) {
    if (h.branch_id == 1) {
      found_predictable = true;
      REQUIRE(h.likely_taken == true);
      REQUIRE(h.confidence >= 0.9);
    }
  }
  REQUIRE(found_predictable == true);
}

TEST_CASE("PGOOptimizer devirtualization hints", "[pgo][optimizer][devirt]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "virtual_caller";
  func.invocation_count = 1000;
  
  CallSiteProfile cs;
  cs.call_site_id = 1;
  cs.callee_name = "Base::method";
  cs.call_count = 1000;
  cs.callee_size = 50;
  cs.is_hot = true;
  
  // 85% of calls go to one target
  cs.target_distribution["DerivedA::method"] = 850;
  cs.target_distribution["DerivedB::method"] = 150;
  func.call_sites.push_back(cs);
  
  data.AddFunctionProfile(func);
  
  PGOOptimizer optimizer(data);
  auto hints = optimizer.FindDevirtualizationOpportunities();
  
  REQUIRE(hints.size() >= 1);
  REQUIRE(hints[0].likely_target == "DerivedA::method");
  REQUIRE(hints[0].confidence >= 0.8);
}

TEST_CASE("PGOOptimizer code layout hints", "[pgo][optimizer][layout]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "layout_func";
  func.invocation_count = 1000;
  
  // Add blocks with varying hotness
  for (size_t i = 0; i < 5; ++i) {
    BasicBlockProfile bb;
    bb.block_id = i;
    bb.execution_count = (i == 0 || i == 1) ? 10000 : 100;
    bb.execution_time_ns = bb.execution_count * 5.0;
    func.basic_blocks.push_back(bb);
  }
  
  data.AddFunctionProfile(func);
  
  PGOOptimizer optimizer(data);
  auto hints = optimizer.OptimizeCodeLayout();
  
  REQUIRE(hints.size() >= 1);
  REQUIRE(hints[0].function == "layout_func");
  REQUIRE(hints[0].hot_blocks.size() >= 1);
  
  // Block 0 should be in hot blocks
  bool has_hot_block_0 = false;
  for (size_t b : hints[0].hot_blocks) {
    if (b == 0) has_hot_block_0 = true;
  }
  REQUIRE(has_hot_block_0 == true);
}

TEST_CASE("PGOOptimizer loop optimization hints", "[pgo][optimizer][loop]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "loop_func";
  func.invocation_count = 100;
  
  // Block 0 is a loop header (executes 1000x per invocation)
  BasicBlockProfile loop_header;
  loop_header.block_id = 0;
  loop_header.execution_count = 100000;  // 100 invocations * 1000 iterations
  loop_header.execution_time_ns = 1000000.0;
  func.basic_blocks.push_back(loop_header);
  
  data.AddFunctionProfile(func);
  
  PGOOptimizer optimizer(data);
  auto hints = optimizer.OptimizeLoops();
  
  // Should suggest vectorization for this high-iteration loop
  bool found_hint = false;
  for (const auto& h : hints) {
    if (h.function == "loop_func") {
      found_hint = true;
      REQUIRE(h.should_vectorize == true);
    }
  }
  REQUIRE(found_hint == true);
}

TEST_CASE("PGOOptimizer optimization report", "[pgo][optimizer][report]") {
  ProfileData data;
  
  // Add some functions with profile data
  for (int i = 0; i < 5; ++i) {
    FunctionProfile func;
    func.function_name = "func_" + std::to_string(i);
    func.invocation_count = (i < 2) ? 10000 : 10;  // 2 hot, 3 cold
    func.total_time_ns = func.invocation_count * 100.0;
    func.estimated_size = 50;
    
    // Add a branch with 99% probability for even functions (above 0.9 threshold)
    BranchProfile br;
    br.branch_id = 0;
    br.source_block = 0;
    br.taken_count = (i % 2 == 0) ? 9900 : 5000;
    br.not_taken_count = (i % 2 == 0) ? 100 : 5000;
    func.branches.push_back(br);
    
    data.AddFunctionProfile(func);
  }
  
  PGOOptimizer optimizer(data);
  auto report = optimizer.GenerateReport();
  
  REQUIRE(report.total_functions == 5);
  REQUIRE(report.hot_functions > 0);
  REQUIRE(report.branch_predictions.size() > 0);
}

TEST_CASE("PGOOptimizer JSON report export", "[pgo][optimizer][json]") {
  ProfileData data;
  
  FunctionProfile func;
  func.function_name = "export_test";
  func.invocation_count = 1000;
  func.total_time_ns = 5000.0;
  
  BranchProfile br;
  br.branch_id = 0;
  br.source_block = 0;
  br.taken_count = 990;     // 99% taken = 0.98 confidence, above 0.9 threshold
  br.not_taken_count = 10;
  func.branches.push_back(br);
  
  data.AddFunctionProfile(func);
  
  PGOOptimizer optimizer(data);
  
  auto tmp_s = TmpPath("polyglot_pgo_report.json");
  const char* tmp = tmp_s.c_str();
  REQUIRE(optimizer.ExportReportToJson(tmp) == true);
  
  // Verify file was created and contains expected data
  std::ifstream report_file(tmp);
  REQUIRE(report_file.is_open());
  
  std::string content((std::istreambuf_iterator<char>(report_file)),
                       std::istreambuf_iterator<char>());
  
  REQUIRE(content.find("\"version\"") != std::string::npos);
  REQUIRE(content.find("\"summary\"") != std::string::npos);
  REQUIRE(content.find("\"branch_predictions\"") != std::string::npos);
  REQUIRE(content.find("\"export_test\"") != std::string::npos);
  
  std::remove(tmp);
}

TEST_CASE("RuntimeProfiler data collection", "[pgo][profiler]") {
  RuntimeProfiler& profiler = RuntimeProfiler::Instance();
  profiler.Reset();
  profiler.Enable();
  
  // Record some profile data
  profiler.RecordBasicBlock("test_func", 0);
  profiler.RecordBasicBlock("test_func", 0);
  profiler.RecordBasicBlock("test_func", 1);
  
  profiler.RecordBranch("test_func", 0, true);
  profiler.RecordBranch("test_func", 0, true);
  profiler.RecordBranch("test_func", 0, false);
  
  // Get collected data
  ProfileData data = profiler.GetProfileData();
  REQUIRE(data.GetFunctionCount() == 1);
  
  const auto* func = data.GetFunctionProfile("test_func");
  REQUIRE(func != nullptr);
  REQUIRE(func->basic_blocks.size() == 2);
  REQUIRE(func->branches.size() == 1);
  
  // Verify branch data
  const auto* br = func->GetBranchProfile(0);
  REQUIRE(br != nullptr);
  REQUIRE(br->taken_count == 2);
  REQUIRE(br->not_taken_count == 1);
  
  profiler.Reset();
}
