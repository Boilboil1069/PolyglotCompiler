/**
 * @file     ffi_interop_test.cpp
 * @brief    Comprehensive unit tests for the FFI interop subsystem
 * @details  Tests ownership tracking, FFI registry, dynamic library loading,
 *           foreign handle lifecycle, and backward-compatible binding helpers.
 *
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  1.0.0
 */
#include <deps/catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "runtime/include/interop/ffi.h"
#include "runtime/include/interop/memory.h"
#include "runtime/include/interop/marshalling.h"
#include "runtime/include/interop/type_mapping.h"
#include "runtime/include/interop/calling_convention.h"

using namespace polyglot::runtime::interop;

// ===========================================================================
// Helper fixtures
// ===========================================================================

namespace {

// Reset global singletons before each test section that touches them.
struct RegistryGuard {
  RegistryGuard() {
    FFIRegistry::Instance().Reset();
    OwnershipTracker::Instance().Reset();
  }
  ~RegistryGuard() {
    FFIRegistry::Instance().Reset();
    OwnershipTracker::Instance().Reset();
  }
};

// A dummy foreign function to use as a symbol address.
int dummy_add(int a, int b) { return a + b; }
int dummy_mul(int a, int b) { return a * b; }

}  // namespace

// ===========================================================================
// 1. ForeignHandle basic properties
// ===========================================================================

TEST_CASE("ForeignHandle validity checks", "[runtime][ffi][handle]") {
  ForeignHandle h;
  h.id = 1;
  h.resource = reinterpret_cast<void *>(0x1234);
  h.state = ForeignHandle::State::kActive;
  h.ownership = Ownership::kOwned;

  REQUIRE(h.IsValid() == true);
  REQUIRE(h.CanTransfer() == true);
  REQUIRE(h.CanMutate() == true);

  SECTION("Borrowed handle cannot transfer or mutate") {
    h.ownership = Ownership::kBorrowed;
    REQUIRE(h.IsValid() == true);
    REQUIRE(h.CanTransfer() == false);
    REQUIRE(h.CanMutate() == false);
  }

  SECTION("Shared handle can mutate but not transfer") {
    h.ownership = Ownership::kShared;
    REQUIRE(h.IsValid() == true);
    REQUIRE(h.CanTransfer() == false);
    REQUIRE(h.CanMutate() == true);
  }

  SECTION("Released handle is not valid") {
    h.state = ForeignHandle::State::kReleased;
    REQUIRE(h.IsValid() == false);
    REQUIRE(h.CanTransfer() == false);
    REQUIRE(h.CanMutate() == false);
  }

  SECTION("Null resource is not valid") {
    h.resource = nullptr;
    REQUIRE(h.IsValid() == false);
  }
}

// ===========================================================================
// 2. OwnershipTracker
// ===========================================================================

TEST_CASE("OwnershipTracker register and lookup", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("sym_a", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0xA));
  REQUIRE(id > 0);
  REQUIRE(tracker.ActiveCount() == 1);

  const ForeignHandle *h = tracker.Lookup(id);
  REQUIRE(h != nullptr);
  REQUIRE(h->name == "sym_a");
  REQUIRE(h->ownership == Ownership::kOwned);
  REQUIRE(h->state == ForeignHandle::State::kActive);

  const ForeignHandle *by_name = tracker.LookupByName("sym_a");
  REQUIRE(by_name != nullptr);
  REQUIRE(by_name->id == id);
}

TEST_CASE("OwnershipTracker access increments counter", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("counter_test", Ownership::kBorrowed,
                                 ForeignHandle::Kind::kObject,
                                 reinterpret_cast<void *>(0xB));
  REQUIRE(tracker.Lookup(id)->access_count == 0);

  tracker.Access(id);
  tracker.Access(id);
  tracker.Access(id);
  REQUIRE(tracker.Lookup(id)->access_count == 3);
}

TEST_CASE("OwnershipTracker transfer semantics", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t owned_id = tracker.Register("owned_fn", Ownership::kOwned,
                                       ForeignHandle::Kind::kFunction,
                                       reinterpret_cast<void *>(0x1));
  uint64_t borrowed_id = tracker.Register("borrowed_fn", Ownership::kBorrowed,
                                          ForeignHandle::Kind::kFunction,
                                          reinterpret_cast<void *>(0x2));

  // Owned can be transferred to shared.
  REQUIRE(tracker.Transfer(owned_id, Ownership::kShared) == true);
  REQUIRE(tracker.Lookup(owned_id)->ownership == Ownership::kShared);

  // Borrowed cannot be transferred.
  REQUIRE(tracker.Transfer(borrowed_id, Ownership::kOwned) == false);

  // Non-existent handle cannot be transferred.
  REQUIRE(tracker.Transfer(9999, Ownership::kOwned) == false);
}

TEST_CASE("OwnershipTracker transfer to borrowed marks transferred state",
          "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("xfer_borrow", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0x1));
  REQUIRE(tracker.Transfer(id, Ownership::kBorrowed) == true);
  REQUIRE(tracker.Lookup(id)->state == ForeignHandle::State::kTransferred);
}

TEST_CASE("OwnershipTracker release lifecycle", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("rel_fn", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0x1));
  REQUIRE(tracker.ActiveCount() == 1);

  REQUIRE(tracker.Release(id) == true);
  REQUIRE(tracker.Lookup(id)->state == ForeignHandle::State::kReleased);
  REQUIRE(tracker.ActiveCount() == 0);

  // Double release returns false.
  REQUIRE(tracker.Release(id) == false);
}

TEST_CASE("OwnershipTracker validate access with ownership levels",
          "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t owned = tracker.Register("v_owned", Ownership::kOwned,
                                    ForeignHandle::Kind::kFunction,
                                    reinterpret_cast<void *>(0x1));
  uint64_t shared = tracker.Register("v_shared", Ownership::kShared,
                                     ForeignHandle::Kind::kObject,
                                     reinterpret_cast<void *>(0x2));
  uint64_t borrowed = tracker.Register("v_borrowed", Ownership::kBorrowed,
                                       ForeignHandle::Kind::kObject,
                                       reinterpret_cast<void *>(0x3));

  // Owned satisfies any requirement.
  REQUIRE(tracker.ValidateAccess(owned, Ownership::kOwned) == true);
  REQUIRE(tracker.ValidateAccess(owned, Ownership::kShared) == true);
  REQUIRE(tracker.ValidateAccess(owned, Ownership::kBorrowed) == true);

  // Shared satisfies shared and borrowed but not owned.
  REQUIRE(tracker.ValidateAccess(shared, Ownership::kOwned) == false);
  REQUIRE(tracker.ValidateAccess(shared, Ownership::kShared) == true);
  REQUIRE(tracker.ValidateAccess(shared, Ownership::kBorrowed) == true);

  // Borrowed only satisfies borrowed.
  REQUIRE(tracker.ValidateAccess(borrowed, Ownership::kOwned) == false);
  REQUIRE(tracker.ValidateAccess(borrowed, Ownership::kShared) == false);
  REQUIRE(tracker.ValidateAccess(borrowed, Ownership::kBorrowed) == true);
}

TEST_CASE("OwnershipTracker validate borrow", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("borrow_check", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0x1));
  REQUIRE(tracker.ValidateBorrow(id) == true);

  tracker.Release(id);
  REQUIRE(tracker.ValidateBorrow(id) == false);
  REQUIRE(tracker.ValidateBorrow(9999) == false);
}

TEST_CASE("OwnershipTracker list active handles", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  tracker.Register("a", Ownership::kOwned, ForeignHandle::Kind::kFunction,
                   reinterpret_cast<void *>(0x1));
  tracker.Register("b", Ownership::kBorrowed, ForeignHandle::Kind::kObject,
                   reinterpret_cast<void *>(0x2));
  uint64_t c = tracker.Register("c", Ownership::kShared,
                                ForeignHandle::Kind::kLibrary,
                                reinterpret_cast<void *>(0x3));
  tracker.Release(c);

  auto active = tracker.ListActiveHandles();
  REQUIRE(active.size() == 2);
  REQUIRE(tracker.ActiveCount() == 2);
}

TEST_CASE("OwnershipTracker reset clears all", "[runtime][ffi][tracker]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  tracker.Register("x", Ownership::kOwned, ForeignHandle::Kind::kFunction,
                   reinterpret_cast<void *>(0x1));
  tracker.Register("y", Ownership::kOwned, ForeignHandle::Kind::kFunction,
                   reinterpret_cast<void *>(0x2));
  REQUIRE(tracker.ActiveCount() == 2);

  tracker.Reset();
  REQUIRE(tracker.ActiveCount() == 0);
  REQUIRE(tracker.LookupByName("x") == nullptr);
}

// ===========================================================================
// 3. FFIRegistry — function binding
// ===========================================================================

TEST_CASE("FFIRegistry bind and query functions", "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.args = {{"i32", 4, 4, false, false}, {"i32", 4, 4, false, false}};
  sig.result = {"i32", 4, 4, false, false};

  uint64_t hid = reg.BindFunction("add", reinterpret_cast<void *>(&dummy_add),
                                  sig, Ownership::kBorrowed);
  REQUIRE(hid > 0);
  REQUIRE(reg.FunctionCount() == 1);

  const ForeignFunction *fn = reg.GetFunction("add");
  REQUIRE(fn != nullptr);
  REQUIRE(fn->name == "add");
  REQUIRE(fn->address == reinterpret_cast<void *>(&dummy_add));
  REQUIRE(fn->ownership == Ownership::kBorrowed);
  REQUIRE(fn->signature.args.size() == 2);

  // Lookup by handle id.
  const ForeignFunction *fn2 = reg.GetFunctionByHandle(hid);
  REQUIRE(fn2 != nullptr);
  REQUIRE(fn2->name == "add");

  // Unknown function returns nullptr.
  REQUIRE(reg.GetFunction("nonexistent") == nullptr);
  REQUIRE(reg.GetFunctionByHandle(9999) == nullptr);
}

TEST_CASE("FFIRegistry bind multiple functions", "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kSysV, Endianness::kLittle, false};
  sig.args = {{"i32", 4, 4, false, false}, {"i32", 4, 4, false, false}};
  sig.result = {"i32", 4, 4, false, false};

  reg.BindFunction("add", reinterpret_cast<void *>(&dummy_add), sig, Ownership::kBorrowed);
  reg.BindFunction("mul", reinterpret_cast<void *>(&dummy_mul), sig, Ownership::kOwned);

  REQUIRE(reg.FunctionCount() == 2);

  auto names = reg.ListFunctions();
  REQUIRE(names.size() == 2);

  // Verify both are resolvable.
  REQUIRE(reg.GetFunction("add") != nullptr);
  REQUIRE(reg.GetFunction("mul") != nullptr);
  REQUIRE(reg.GetFunction("mul")->ownership == Ownership::kOwned);
}

// ===========================================================================
// 4. FFIRegistry — object binding
// ===========================================================================

TEST_CASE("FFIRegistry bind and query objects", "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  int data = 42;
  int deleted = 0;
  auto deleter = [&](void *) { ++deleted; };

  uint64_t hid = reg.BindObject("my_obj", &data, sizeof(data),
                                Ownership::kOwned, deleter);
  REQUIRE(hid > 0);
  REQUIRE(reg.ObjectCount() == 1);

  ForeignObject *obj = reg.GetObject("my_obj");
  REQUIRE(obj != nullptr);
  REQUIRE(obj->ptr == &data);
  REQUIRE(obj->size == sizeof(int));
  REQUIRE(obj->ownership == Ownership::kOwned);

  ForeignObject *obj2 = reg.GetObjectByHandle(hid);
  REQUIRE(obj2 != nullptr);
  REQUIRE(obj2->ptr == &data);

  REQUIRE(reg.GetObject("no_such") == nullptr);
  REQUIRE(reg.GetObjectByHandle(9999) == nullptr);

  auto names = reg.ListObjects();
  REQUIRE(names.size() == 1);
  REQUIRE(names[0] == "my_obj");
}

// ===========================================================================
// 5. FFIRegistry — lifecycle (release, transfer)
// ===========================================================================

TEST_CASE("FFIRegistry release function handle", "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"void", 0, 1, false, false};

  uint64_t hid = reg.BindFunction("fn_to_release",
                                  reinterpret_cast<void *>(0xDEAD),
                                  sig, Ownership::kOwned);
  REQUIRE(reg.FunctionCount() == 1);

  REQUIRE(reg.Release(hid) == true);
  REQUIRE(reg.FunctionCount() == 0);
  REQUIRE(reg.GetFunction("fn_to_release") == nullptr);

  // Double release returns false.
  REQUIRE(reg.Release(hid) == false);
}

TEST_CASE("FFIRegistry release object handle invokes deleter",
          "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  int deleted = 0;
  auto deleter = [&](void *) { ++deleted; };

  uint64_t hid = reg.BindObject("del_obj", reinterpret_cast<void *>(0x1), 8,
                                Ownership::kOwned, deleter);
  REQUIRE(reg.ObjectCount() == 1);

  REQUIRE(reg.Release(hid) == true);
  REQUIRE(reg.ObjectCount() == 0);
  REQUIRE(deleted == 1);
}

TEST_CASE("FFIRegistry transfer ownership of function",
          "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"i32", 4, 4, false, false};

  uint64_t hid = reg.BindFunction("transfer_fn",
                                  reinterpret_cast<void *>(&dummy_add),
                                  sig, Ownership::kOwned);

  // Transfer from owned to shared.
  REQUIRE(reg.TransferOwnership(hid, Ownership::kShared) == true);
  REQUIRE(reg.GetFunction("transfer_fn")->ownership == Ownership::kShared);
}

TEST_CASE("FFIRegistry reset clears everything", "[runtime][ffi][registry]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"void", 0, 1, false, false};

  reg.BindFunction("f1", reinterpret_cast<void *>(0x1), sig, Ownership::kBorrowed);
  reg.BindFunction("f2", reinterpret_cast<void *>(0x2), sig, Ownership::kOwned);
  reg.BindObject("o1", reinterpret_cast<void *>(0x3), 4, Ownership::kBorrowed);

  REQUIRE(reg.FunctionCount() == 2);
  REQUIRE(reg.ObjectCount() == 1);

  reg.Reset();
  REQUIRE(reg.FunctionCount() == 0);
  REQUIRE(reg.ObjectCount() == 0);
  REQUIRE(reg.LibraryCount() == 0);
}

// ===========================================================================
// 6. DynamicLibrary
// ===========================================================================

TEST_CASE("DynamicLibrary open nonexistent library fails", "[runtime][ffi][dynlib]") {
  DynamicLibrary lib;
  REQUIRE(lib.IsOpen() == false);
  REQUIRE(lib.Open("/nonexistent/path/libfake.so") == false);
  REQUIRE(lib.IsOpen() == false);
  REQUIRE(lib.LastError().empty() == false);
}

TEST_CASE("DynamicLibrary open system library", "[runtime][ffi][dynlib]") {
  DynamicLibrary lib;
#ifdef __APPLE__
  bool ok = lib.Open("/usr/lib/libSystem.B.dylib");
#elif defined(__linux__)
  bool ok = lib.Open("libm.so.6");
  if (!ok) ok = lib.Open("libm.so");  // fallback
#else
  bool ok = false;  // skip on unsupported platforms
#endif

  if (ok) {
    REQUIRE(lib.IsOpen() == true);
    // Resolve a known symbol.
    void *sym = lib.GetSymbol("strlen");
    // strlen may or may not be in the library; just check that GetSymbol doesn't crash.
    (void)sym;
    lib.Close();
    REQUIRE(lib.IsOpen() == false);
  }
}

TEST_CASE("DynamicLibrary move semantics", "[runtime][ffi][dynlib]") {
  DynamicLibrary lib1;
  // Even an unopened library should move safely.
  DynamicLibrary lib2(std::move(lib1));
  REQUIRE(lib2.IsOpen() == false);

  DynamicLibrary lib3;
  lib3 = std::move(lib2);
  REQUIRE(lib3.IsOpen() == false);
}

TEST_CASE("DynamicLibrary GetSymbol on closed library returns null",
          "[runtime][ffi][dynlib]") {
  DynamicLibrary lib;
  REQUIRE(lib.GetSymbol("anything") == nullptr);
}

// ===========================================================================
// 7. FFIRegistry — dynamic library loading
// ===========================================================================

TEST_CASE("FFIRegistry load nonexistent library returns 0", "[runtime][ffi][registry][dynlib]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  uint64_t hid = reg.LoadLibrary("/no/such/library.so");
  REQUIRE(hid == 0);
  REQUIRE(reg.LibraryCount() == 0);
}

TEST_CASE("FFIRegistry unload unknown library returns false",
          "[runtime][ffi][registry][dynlib]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();
  REQUIRE(reg.UnloadLibrary(9999) == false);
}

// ===========================================================================
// 8. Backward-compatible free-standing helpers
// ===========================================================================

TEST_CASE("Bind creates ForeignFunction with default convention", "[runtime][ffi][compat]") {
  RegistryGuard guard;
  auto fn = Bind("legacy", reinterpret_cast<void *>(0x100));
  REQUIRE(fn.name == "legacy");
  REQUIRE(fn.address == reinterpret_cast<void *>(0x100));
  REQUIRE(fn.ownership == Ownership::kBorrowed);
  REQUIRE(fn.signature.convention.kind == CallingConventionKind::kCDecl);
}

TEST_CASE("BindWithSignature sets ownership on function", "[runtime][ffi][compat]") {
  RegistryGuard guard;
  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kFastCall, Endianness::kLittle, false};
  sig.result = {"i32", 4, 4, false, false};

  auto fn = BindWithSignature("owned_sym", reinterpret_cast<void *>(0x200),
                              sig, Ownership::kOwned);
  REQUIRE(fn.name == "owned_sym");
  REQUIRE(fn.ownership == Ownership::kOwned);

  // The ownership tracker should have recorded the handle.
  const ForeignHandle *h = OwnershipTracker::Instance().LookupByName("owned_sym");
  REQUIRE(h != nullptr);
  REQUIRE(h->ownership == Ownership::kOwned);
  REQUIRE(h->kind == ForeignHandle::Kind::kFunction);
}

TEST_CASE("BindBorrowed/Owned/Shared create correct ForeignObjects", "[runtime][ffi][compat]") {
  int destroyed = 0;
  auto deleter = [&](void *) { ++destroyed; };
  void *addr = reinterpret_cast<void *>(0x1234);

  ForeignObject *borrowed = BindBorrowed("b", addr, 0);
  ForeignObject *owned = BindOwned("o", addr, 0, deleter);
  ForeignObject *shared = BindShared("s", addr, 0, deleter);

  REQUIRE(borrowed->ownership == Ownership::kBorrowed);
  REQUIRE(owned->ownership == Ownership::kOwned);
  REQUIRE(shared->ownership == Ownership::kShared);

  // Retain shared and release twice — deleter should fire once.
  RetainForeign(shared);
  ReleaseForeign(shared);
  ReleaseForeign(shared);
  REQUIRE(destroyed == 1);

  ReleaseForeign(borrowed);
  ReleaseForeign(owned);
  REQUIRE(destroyed == 2);
}

// ===========================================================================
// 9. Enhanced ForeignObject helpers
// ===========================================================================

TEST_CASE("ForeignRefCount and ForeignIsAlive", "[runtime][ffi][memory]") {
  int deleted = 0;
  auto deleter = [&](void *) { ++deleted; };

  ForeignObject *obj = AcquireOwned(reinterpret_cast<void *>(0x1), 4, deleter);
  REQUIRE(ForeignRefCount(obj) == 1);
  REQUIRE(ForeignIsAlive(obj) == true);

  RetainForeign(obj);
  REQUIRE(ForeignRefCount(obj) == 2);

  ReleaseForeign(obj);
  REQUIRE(ForeignRefCount(obj) == 1);

  ReleaseForeign(obj);
  // obj is now deleted — we shouldn't dereference it, but
  // we can verify the deleter fired.
  REQUIRE(deleted == 1);

  // Null checks.
  REQUIRE(ForeignRefCount(nullptr) == 0);
  REQUIRE(ForeignIsAlive(nullptr) == false);
}

TEST_CASE("OwnershipName utility", "[runtime][ffi][memory]") {
  REQUIRE(std::string(OwnershipName(Ownership::kBorrowed)) == "borrowed");
  REQUIRE(std::string(OwnershipName(Ownership::kOwned)) == "owned");
  REQUIRE(std::string(OwnershipName(Ownership::kShared)) == "shared");
}

// ===========================================================================
// 10. Thread safety of OwnershipTracker
// ===========================================================================

TEST_CASE("OwnershipTracker concurrent register and release",
          "[runtime][ffi][tracker][threading]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  constexpr int kPerThread = 50;
  constexpr int kThreads = 4;

  auto worker = [&](int thread_idx) {
    for (int i = 0; i < kPerThread; ++i) {
      std::string name = "t" + std::to_string(thread_idx) + "_" + std::to_string(i);
      uint64_t id = tracker.Register(name, Ownership::kOwned,
                                     ForeignHandle::Kind::kFunction,
                                     reinterpret_cast<void *>(static_cast<uintptr_t>(i + 1)));
      REQUIRE(id > 0);
      tracker.Access(id);
      tracker.Release(id);
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back(worker, t);
  }
  for (auto &th : threads) th.join();

  // All handles should have been released.
  REQUIRE(tracker.ActiveCount() == 0);
}

// ===========================================================================
// 11. Thread safety of FFIRegistry
// ===========================================================================

TEST_CASE("FFIRegistry concurrent function binding",
          "[runtime][ffi][registry][threading]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"i32", 4, 4, false, false};

  constexpr int kPerThread = 25;
  constexpr int kThreads = 4;

  auto worker = [&](int thread_idx) {
    for (int i = 0; i < kPerThread; ++i) {
      std::string name = "fn_" + std::to_string(thread_idx) + "_" + std::to_string(i);
      reg.BindFunction(name, reinterpret_cast<void *>(&dummy_add),
                       sig, Ownership::kBorrowed);
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back(worker, t);
  }
  for (auto &th : threads) th.join();

  REQUIRE(reg.FunctionCount() == kPerThread * kThreads);
}

// ===========================================================================
// 12. Integration: registry + tracker coordination
// ===========================================================================

TEST_CASE("FFIRegistry BindFunction registers in OwnershipTracker",
          "[runtime][ffi][integration]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();
  auto &tracker = OwnershipTracker::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"i32", 4, 4, false, false};

  uint64_t hid = reg.BindFunction("integrated_fn",
                                  reinterpret_cast<void *>(&dummy_add),
                                  sig, Ownership::kOwned);

  // The tracker should know about this handle.
  const ForeignHandle *h = tracker.Lookup(hid);
  REQUIRE(h != nullptr);
  REQUIRE(h->name == "integrated_fn");
  REQUIRE(h->kind == ForeignHandle::Kind::kFunction);
  REQUIRE(h->ownership == Ownership::kOwned);
  REQUIRE(tracker.ActiveCount() == 1);

  // Release through the registry should propagate.
  reg.Release(hid);
  REQUIRE(tracker.Lookup(hid)->state == ForeignHandle::State::kReleased);
}

TEST_CASE("FFIRegistry BindObject registers in OwnershipTracker",
          "[runtime][ffi][integration]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();
  auto &tracker = OwnershipTracker::Instance();

  int data = 7;
  uint64_t hid = reg.BindObject("int_obj", &data, sizeof(data),
                                Ownership::kShared);

  const ForeignHandle *h = tracker.Lookup(hid);
  REQUIRE(h != nullptr);
  REQUIRE(h->kind == ForeignHandle::Kind::kObject);
  REQUIRE(h->ownership == Ownership::kShared);
}

TEST_CASE("FFIRegistry transfer propagates to function record",
          "[runtime][ffi][integration]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"void", 0, 1, false, false};

  uint64_t hid = reg.BindFunction("xfer_fn", reinterpret_cast<void *>(0xABC),
                                  sig, Ownership::kOwned);

  REQUIRE(reg.TransferOwnership(hid, Ownership::kShared) == true);

  // Both the registry record and the tracker should reflect the new ownership.
  REQUIRE(reg.GetFunction("xfer_fn")->ownership == Ownership::kShared);
  REQUIRE(OwnershipTracker::Instance().Lookup(hid)->ownership == Ownership::kShared);
}

// ===========================================================================
// 13. Shared object deleter fires on last release via registry
// ===========================================================================

TEST_CASE("FFIRegistry shared object deleter fires on release",
          "[runtime][ffi][registry][shared]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  int deleted = 0;
  auto deleter = [&](void *) { ++deleted; };

  uint64_t hid = reg.BindObject("shared_obj", reinterpret_cast<void *>(0x1), 8,
                                Ownership::kShared, deleter);

  // The object starts with refcount 1.
  ForeignObject *obj = reg.GetObject("shared_obj");
  REQUIRE(obj != nullptr);
  REQUIRE(ForeignRefCount(obj) == 1);

  // Retain to simulate a second reference.
  RetainForeign(obj);
  REQUIRE(ForeignRefCount(obj) == 2);

  // Release through registry drops one ref (the registry's) and removes the record.
  reg.Release(hid);
  // The object is still alive because we retained it.
  REQUIRE(deleted == 0);

  // Now the final release triggers the deleter.
  ReleaseForeign(obj);
  REQUIRE(deleted == 1);
}

// ===========================================================================
// 14. Edge cases
// ===========================================================================

TEST_CASE("OwnershipTracker access on released handle returns null",
          "[runtime][ffi][tracker][edge]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("edge", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0x1));
  tracker.Release(id);
  REQUIRE(tracker.Access(id) == nullptr);
}

TEST_CASE("OwnershipTracker validate access on released returns false",
          "[runtime][ffi][tracker][edge]") {
  RegistryGuard guard;
  auto &tracker = OwnershipTracker::Instance();

  uint64_t id = tracker.Register("edge2", Ownership::kOwned,
                                 ForeignHandle::Kind::kFunction,
                                 reinterpret_cast<void *>(0x1));
  tracker.Release(id);
  REQUIRE(tracker.ValidateAccess(id, Ownership::kBorrowed) == false);
}

TEST_CASE("FFIRegistry release nonexistent handle returns false",
          "[runtime][ffi][registry][edge]") {
  RegistryGuard guard;
  REQUIRE(FFIRegistry::Instance().Release(99999) == false);
}

TEST_CASE("FFIRegistry transfer on borrowed function fails",
          "[runtime][ffi][registry][edge]") {
  RegistryGuard guard;
  auto &reg = FFIRegistry::Instance();

  ForeignSignature sig;
  sig.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  sig.result = {"void", 0, 1, false, false};

  uint64_t hid = reg.BindFunction("borrowed_fn", reinterpret_cast<void *>(0x1),
                                  sig, Ownership::kBorrowed);
  // Cannot transfer a borrowed handle.
  REQUIRE(reg.TransferOwnership(hid, Ownership::kOwned) == false);
}
