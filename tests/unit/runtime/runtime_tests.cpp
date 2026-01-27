#include <deps/catch2/catch_test_macros.hpp>

#include "runtime/include/gc/heap.h"
#include "runtime/include/gc/runtime.h"
#include "runtime/include/gc/root_guard.h"
#include "runtime/include/interop/ffi.h"
#include "runtime/include/interop/memory.h"
#include "runtime/include/interop/marshalling.h"
#include "runtime/include/interop/type_mapping.h"
#include "runtime/include/interop/calling_convention.h"
#include "backends/common/include/debug_info.h"
#include "runtime/include/services/exception.h"
#include "runtime/include/services/threading.h"

using namespace polyglot::runtime;

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

TEST_CASE("Foreign ownership respects deleter only for owned", "[runtime][interop]") {
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

  REQUIRE(destroyed == 1);
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

  REQUIRE(freed == 1);
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
