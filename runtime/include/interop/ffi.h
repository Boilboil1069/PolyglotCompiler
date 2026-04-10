/**
 * @file     ffi.h
 * @brief    Foreign Function Interface (FFI) for polyglot runtime
 * @details  Provides foreign function binding, ownership tracking, dynamic library loading,
 *           and a centralized FFI registry for cross-language interop. Ownership semantics
 *           (borrowed, owned, shared) are enforced to ensure safe resource management across
 *           language boundaries (C++, Python, Rust).
 *
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  2.0.0
 *
 * Main Classes:
 * - ForeignFunction: Describes a foreign callable with signature and ownership
 * - ForeignHandle: Unified handle wrapping any foreign resource with lifecycle tracking
 * - OwnershipTracker: Validates and records ownership state transitions
 * - FFIRegistry: Central registry for foreign functions, objects, and dynamic libraries
 *
 * @note     All registry and tracker APIs are thread-safe.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/include/interop/calling_convention.h"
#include "runtime/include/interop/memory.h"

namespace polyglot::runtime::interop {

/** @name - */
/** @{ */
// ForeignFunction
/** @} */

/** @name - */
/** @{ */

/// Describes a foreign callable function with its address, ABI signature,
/// and ownership tag indicating who is responsible for the symbol lifetime.
struct ForeignFunction {
  std::string name;
  void *address{nullptr};
  ForeignSignature signature;
  Ownership ownership{Ownership::kBorrowed};
};

/** @} */

/** @name - */
/** @{ */
// ForeignHandle
/** @} */

/** @name - */
/** @{ */

/// Unified handle wrapping a foreign resource (function, object, or library)
/// with full lifecycle and access tracking.
struct ForeignHandle {
  /** @brief Kind enumeration. */
  enum class Kind { kFunction, kObject, kLibrary };
  /** @brief State enumeration. */
  enum class State { kActive, kTransferred, kReleased, kInvalid };

  uint64_t id{0};
  std::string name;
  Kind kind{Kind::kFunction};
  Ownership ownership{Ownership::kBorrowed};
  State state{State::kActive};
  void *resource{nullptr};

  std::chrono::steady_clock::time_point created_at{std::chrono::steady_clock::now()};
  size_t access_count{0};

  /// A handle is valid when it is active and points at a non-null resource.
  bool IsValid() const { return state == State::kActive && resource != nullptr; }

  /// Only owned handles may be transferred.
  bool CanTransfer() const { return IsValid() && ownership == Ownership::kOwned; }

  /// Borrowed handles are read-only; owned and shared may mutate.
  bool CanMutate() const { return IsValid() && ownership != Ownership::kBorrowed; }
};

/** @} */

/** @name - */
/** @{ */
// OwnershipTracker
/** @} */

/** @name - */
/** @{ */

/// Tracks ownership state for every registered foreign handle and validates
/// transitions (register, borrow, transfer, release).  Thread-safe.
class OwnershipTracker {
 public:
  static OwnershipTracker &Instance();

  /// Register a new handle and return its unique id.
  uint64_t Register(const std::string &name, Ownership ownership,
                    ForeignHandle::Kind kind, void *resource);

  /// Transfer ownership of a handle to a new mode. Only valid on owned handles.
  bool Transfer(uint64_t handle_id, Ownership new_ownership);

  /// Release (invalidate) a handle.  Returns false if already released.
  bool Release(uint64_t handle_id);

  /// Record an access on a handle and return the current handle, or nullptr.
  const ForeignHandle *Access(uint64_t handle_id);

  /// Lookup a handle by id without recording an access.
  const ForeignHandle *Lookup(uint64_t handle_id) const;

  /// Lookup a handle by name.
  const ForeignHandle *LookupByName(const std::string &name) const;

  /// Validate that a handle exists and satisfies the required ownership.
  bool ValidateAccess(uint64_t handle_id, Ownership required) const;

  /// Validate that a handle is borrowable (active and not transferred).
  bool ValidateBorrow(uint64_t handle_id) const;

  /// Return all currently active handles.
  std::vector<ForeignHandle> ListActiveHandles() const;

  /// Number of active handles.
  size_t ActiveCount() const;

  /// Clear all tracked handles (intended for testing/reset).
  void Reset();

 private:
  OwnershipTracker() = default;
  OwnershipTracker(const OwnershipTracker &) = delete;
  OwnershipTracker &operator=(const OwnershipTracker &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, ForeignHandle> handles_;
  std::unordered_map<std::string, uint64_t> name_index_;
  std::atomic<uint64_t> next_id_{1};
};

/** @} */

/** @name - */
/** @{ */
// DynamicLibrary
/** @} */

/** @name - */
/** @{ */

/// RAII wrapper around dlopen / dlsym (Unix) or LoadLibrary / GetProcAddress (Windows).
class DynamicLibrary {
 public:
  DynamicLibrary() = default;
  ~DynamicLibrary();

  DynamicLibrary(const DynamicLibrary &) = delete;
  DynamicLibrary &operator=(const DynamicLibrary &) = delete;
  DynamicLibrary(DynamicLibrary &&other) noexcept;
  DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;

  /// Open a shared library at `path`.  Returns true on success.
  bool Open(const std::string &path);

  /// Close the library.  Safe to call multiple times.
  void Close();

  /// Resolve a symbol name inside the library.  Returns nullptr on failure.
  void *GetSymbol(const std::string &symbol_name) const;

  /// Return the last error string from the platform loader.
  const std::string &LastError() const { return last_error_; }

  bool IsOpen() const { return handle_ != nullptr; }
  const std::string &Path() const { return path_; }

 private:
  void *handle_{nullptr};
  std::string path_;
  std::string last_error_;
};

/** @} */

/** @name - */
/** @{ */
// FFIRegistry
/** @} */

/** @name - */
/** @{ */

/// Central registry for foreign functions, objects, and dynamic libraries.
/// Coordinates binding, lookup, ownership transfer, and release.  Thread-safe.
class FFIRegistry {
 public:
  static FFIRegistry &Instance();

  // -- Function binding -----------------------------------------------------

  /// Bind a foreign function and return its tracker handle id.
  uint64_t BindFunction(const std::string &name, void *address,
                        const ForeignSignature &sig,
                        Ownership ownership = Ownership::kBorrowed);

  /// Look up a previously bound function by name.
  const ForeignFunction *GetFunction(const std::string &name) const;

  /// Look up a previously bound function by handle id.
  const ForeignFunction *GetFunctionByHandle(uint64_t handle_id) const;

  // -- Object binding -------------------------------------------------------

  /// Bind a foreign data object and return its tracker handle id.
  uint64_t BindObject(const std::string &name, void *address, size_t size,
                      Ownership ownership,
                      std::function<void(void *)> deleter = nullptr);

  /// Look up a previously bound object by name.
  ForeignObject *GetObject(const std::string &name) const;

  /// Look up a previously bound object by handle id.
  ForeignObject *GetObjectByHandle(uint64_t handle_id) const;

  // -- Dynamic library loading ----------------------------------------------

  /// Load a dynamic library and return its tracker handle id, or 0 on failure.
  uint64_t LoadLibrary(const std::string &path);

  /// Resolve a symbol inside a previously loaded library.
  void *GetLibrarySymbol(uint64_t lib_handle, const std::string &symbol);

  /// Unload a dynamic library.
  bool UnloadLibrary(uint64_t lib_handle);

  // -- Lifecycle ------------------------------------------------------------

  /// Release any kind of handle (function, object, or library).
  bool Release(uint64_t handle_id);

  /// Transfer ownership of a handle.
  bool TransferOwnership(uint64_t handle_id, Ownership new_ownership);

  // -- Query ----------------------------------------------------------------

  size_t FunctionCount() const;
  size_t ObjectCount() const;
  size_t LibraryCount() const;
  std::vector<std::string> ListFunctions() const;
  std::vector<std::string> ListObjects() const;

  /// Clear all registrations (intended for testing).
  void Reset();

 private:
  FFIRegistry() = default;
  FFIRegistry(const FFIRegistry &) = delete;
  FFIRegistry &operator=(const FFIRegistry &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ForeignFunction> functions_;
  std::unordered_map<uint64_t, std::string> fn_handle_to_name_;

  std::unordered_map<std::string, ForeignObject *> objects_;
  std::unordered_map<uint64_t, std::string> obj_handle_to_name_;

  std::unordered_map<uint64_t, DynamicLibrary> libraries_;
};

/** @} */

/** @name - */
/** @{ */
// Free-standing convenience helpers (backward-compatible)
/** @} */

/** @name - */
/** @{ */

ForeignFunction Bind(const std::string &name, void *address);
ForeignObject *BindBorrowed(const std::string &name, void *address, size_t size);
ForeignObject *BindOwned(const std::string &name, void *address, size_t size,
                         std::function<void(void *)> deleter);
ForeignObject *BindShared(const std::string &name, void *address, size_t size,
                          std::function<void(void *)> deleter);

/// Build a ForeignFunction with a signature and address, tagging ownership on the handle.
ForeignFunction BindWithSignature(const std::string &name, void *address,
                                  const ForeignSignature &sig,
                                  Ownership ownership = Ownership::kBorrowed);

}  // namespace polyglot::runtime::interop

/** @} */