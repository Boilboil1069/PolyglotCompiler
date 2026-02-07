/**
 * @file     ffi.cpp
 * @brief    Implementation of the FFI binding, ownership tracking, dynamic library,
 *           and registry subsystems.
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  2.0.0
 */
#include "runtime/include/interop/ffi.h"

#include <algorithm>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace polyglot::runtime::interop {

// ===========================================================================
// OwnershipTracker
// ===========================================================================

OwnershipTracker &OwnershipTracker::Instance() {
  static OwnershipTracker instance;
  return instance;
}

uint64_t OwnershipTracker::Register(const std::string &name, Ownership ownership,
                                    ForeignHandle::Kind kind, void *resource) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

  ForeignHandle handle;
  handle.id = id;
  handle.name = name;
  handle.kind = kind;
  handle.ownership = ownership;
  handle.state = ForeignHandle::State::kActive;
  handle.resource = resource;
  handle.created_at = std::chrono::steady_clock::now();
  handle.access_count = 0;

  handles_.emplace(id, std::move(handle));
  if (!name.empty()) {
    name_index_[name] = id;
  }
  return id;
}

bool OwnershipTracker::Transfer(uint64_t handle_id, Ownership new_ownership) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return false;

  ForeignHandle &h = it->second;
  // Only active, owned handles may be transferred.
  if (h.state != ForeignHandle::State::kActive) return false;
  if (h.ownership != Ownership::kOwned) return false;

  h.ownership = new_ownership;
  // If ownership becomes borrowed it logically marks the handle as transferred
  // from the caller's perspective, but the resource remains active.
  if (new_ownership == Ownership::kBorrowed) {
    h.state = ForeignHandle::State::kTransferred;
  }
  return true;
}

bool OwnershipTracker::Release(uint64_t handle_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return false;

  ForeignHandle &h = it->second;
  if (h.state == ForeignHandle::State::kReleased ||
      h.state == ForeignHandle::State::kInvalid) {
    return false;
  }

  h.state = ForeignHandle::State::kReleased;
  h.resource = nullptr;
  return true;
}

const ForeignHandle *OwnershipTracker::Access(uint64_t handle_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return nullptr;
  if (it->second.state != ForeignHandle::State::kActive) return nullptr;
  ++it->second.access_count;
  return &it->second;
}

const ForeignHandle *OwnershipTracker::Lookup(uint64_t handle_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return nullptr;
  return &it->second;
}

const ForeignHandle *OwnershipTracker::LookupByName(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto nit = name_index_.find(name);
  if (nit == name_index_.end()) return nullptr;
  auto it = handles_.find(nit->second);
  if (it == handles_.end()) return nullptr;
  return &it->second;
}

bool OwnershipTracker::ValidateAccess(uint64_t handle_id, Ownership required) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return false;
  const ForeignHandle &h = it->second;
  if (h.state != ForeignHandle::State::kActive) return false;

  // Owned satisfies any requirement.
  if (h.ownership == Ownership::kOwned) return true;
  // Shared satisfies shared or borrowed.
  if (h.ownership == Ownership::kShared &&
      (required == Ownership::kShared || required == Ownership::kBorrowed))
    return true;
  // Borrowed only satisfies borrowed.
  if (h.ownership == Ownership::kBorrowed && required == Ownership::kBorrowed)
    return true;

  return false;
}

bool OwnershipTracker::ValidateBorrow(uint64_t handle_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(handle_id);
  if (it == handles_.end()) return false;
  const ForeignHandle &h = it->second;
  return h.state == ForeignHandle::State::kActive;
}

std::vector<ForeignHandle> OwnershipTracker::ListActiveHandles() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ForeignHandle> result;
  for (const auto &[id, h] : handles_) {
    if (h.state == ForeignHandle::State::kActive) {
      result.push_back(h);
    }
  }
  return result;
}

size_t OwnershipTracker::ActiveCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const auto &[id, h] : handles_) {
    if (h.state == ForeignHandle::State::kActive) ++count;
  }
  return count;
}

void OwnershipTracker::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  handles_.clear();
  name_index_.clear();
  next_id_.store(1, std::memory_order_relaxed);
}

// ===========================================================================
// DynamicLibrary
// ===========================================================================

DynamicLibrary::~DynamicLibrary() { Close(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept
    : handle_(other.handle_),
      path_(std::move(other.path_)),
      last_error_(std::move(other.last_error_)) {
  other.handle_ = nullptr;
}

DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    path_ = std::move(other.path_);
    last_error_ = std::move(other.last_error_);
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Open(const std::string &path) {
  Close();
  path_ = path;

#ifdef _WIN32
  handle_ = static_cast<void *>(::LoadLibraryA(path.c_str()));
  if (!handle_) {
    DWORD err = ::GetLastError();
    last_error_ = "LoadLibrary failed with error code " + std::to_string(err);
    return false;
  }
#else
  handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle_) {
    const char *err = ::dlerror();
    last_error_ = err ? err : "dlopen failed with unknown error";
    return false;
  }
#endif
  last_error_.clear();
  return true;
}

void DynamicLibrary::Close() {
  if (!handle_) return;

#ifdef _WIN32
  ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
  ::dlclose(handle_);
#endif
  handle_ = nullptr;
}

void *DynamicLibrary::GetSymbol(const std::string &symbol_name) const {
  if (!handle_) return nullptr;

#ifdef _WIN32
  void *sym = reinterpret_cast<void *>(
      ::GetProcAddress(static_cast<HMODULE>(handle_), symbol_name.c_str()));
  if (!sym) {
    DWORD err = ::GetLastError();
    const_cast<DynamicLibrary *>(this)->last_error_ =
        "GetProcAddress failed with error code " + std::to_string(err);
  }
#else
  ::dlerror();  // clear previous errors
  void *sym = ::dlsym(handle_, symbol_name.c_str());
  const char *err = ::dlerror();
  if (err) {
    const_cast<DynamicLibrary *>(this)->last_error_ = err;
    return nullptr;
  }
#endif
  return sym;
}

// ===========================================================================
// FFIRegistry
// ===========================================================================

FFIRegistry &FFIRegistry::Instance() {
  static FFIRegistry instance;
  return instance;
}

// -- Function binding -------------------------------------------------------

uint64_t FFIRegistry::BindFunction(const std::string &name, void *address,
                                   const ForeignSignature &sig,
                                   Ownership ownership) {
  std::lock_guard<std::mutex> lock(mutex_);
  ForeignFunction fn;
  fn.name = name;
  fn.address = address;
  fn.signature = sig;
  fn.ownership = ownership;

  functions_[name] = fn;

  uint64_t hid = OwnershipTracker::Instance().Register(
      name, ownership, ForeignHandle::Kind::kFunction, address);
  fn_handle_to_name_[hid] = name;
  return hid;
}

const ForeignFunction *FFIRegistry::GetFunction(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = functions_.find(name);
  if (it == functions_.end()) return nullptr;
  return &it->second;
}

const ForeignFunction *FFIRegistry::GetFunctionByHandle(uint64_t handle_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto nit = fn_handle_to_name_.find(handle_id);
  if (nit == fn_handle_to_name_.end()) return nullptr;
  auto it = functions_.find(nit->second);
  if (it == functions_.end()) return nullptr;
  return &it->second;
}

// -- Object binding ---------------------------------------------------------

uint64_t FFIRegistry::BindObject(const std::string &name, void *address,
                                 size_t size, Ownership ownership,
                                 std::function<void(void *)> deleter) {
  std::lock_guard<std::mutex> lock(mutex_);
  ForeignObject *obj = AcquireForeign(address, size, std::move(deleter), ownership);
  objects_[name] = obj;

  uint64_t hid = OwnershipTracker::Instance().Register(
      name, ownership, ForeignHandle::Kind::kObject, address);
  obj_handle_to_name_[hid] = name;
  return hid;
}

ForeignObject *FFIRegistry::GetObject(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = objects_.find(name);
  if (it == objects_.end()) return nullptr;
  return it->second;
}

ForeignObject *FFIRegistry::GetObjectByHandle(uint64_t handle_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto nit = obj_handle_to_name_.find(handle_id);
  if (nit == obj_handle_to_name_.end()) return nullptr;
  auto it = objects_.find(nit->second);
  if (it == objects_.end()) return nullptr;
  return it->second;
}

// -- Dynamic library loading ------------------------------------------------

uint64_t FFIRegistry::LoadLibrary(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  DynamicLibrary lib;
  if (!lib.Open(path)) return 0;

  uint64_t hid = OwnershipTracker::Instance().Register(
      path, Ownership::kOwned, ForeignHandle::Kind::kLibrary,
      nullptr /* the library handle is internal */);
  libraries_.emplace(hid, std::move(lib));
  return hid;
}

void *FFIRegistry::GetLibrarySymbol(uint64_t lib_handle,
                                    const std::string &symbol) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = libraries_.find(lib_handle);
  if (it == libraries_.end()) return nullptr;
  return it->second.GetSymbol(symbol);
}

bool FFIRegistry::UnloadLibrary(uint64_t lib_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = libraries_.find(lib_handle);
  if (it == libraries_.end()) return false;

  it->second.Close();
  libraries_.erase(it);
  OwnershipTracker::Instance().Release(lib_handle);
  return true;
}

// -- Lifecycle --------------------------------------------------------------

bool FFIRegistry::Release(uint64_t handle_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Try to find in functions.
  {
    auto nit = fn_handle_to_name_.find(handle_id);
    if (nit != fn_handle_to_name_.end()) {
      functions_.erase(nit->second);
      fn_handle_to_name_.erase(nit);
      OwnershipTracker::Instance().Release(handle_id);
      return true;
    }
  }

  // Try objects.
  {
    auto nit = obj_handle_to_name_.find(handle_id);
    if (nit != obj_handle_to_name_.end()) {
      auto oit = objects_.find(nit->second);
      if (oit != objects_.end()) {
        ReleaseForeign(oit->second);
        objects_.erase(oit);
      }
      obj_handle_to_name_.erase(nit);
      OwnershipTracker::Instance().Release(handle_id);
      return true;
    }
  }

  // Try libraries.
  {
    auto lit = libraries_.find(handle_id);
    if (lit != libraries_.end()) {
      lit->second.Close();
      libraries_.erase(lit);
      OwnershipTracker::Instance().Release(handle_id);
      return true;
    }
  }

  return false;
}

bool FFIRegistry::TransferOwnership(uint64_t handle_id, Ownership new_ownership) {
  // Delegate to ownership tracker.  The tracker validates the transition.
  // Also update the local function/object record.
  std::lock_guard<std::mutex> lock(mutex_);

  bool ok = OwnershipTracker::Instance().Transfer(handle_id, new_ownership);
  if (!ok) return false;

  // Update the cached ownership on the function record.
  {
    auto nit = fn_handle_to_name_.find(handle_id);
    if (nit != fn_handle_to_name_.end()) {
      auto it = functions_.find(nit->second);
      if (it != functions_.end()) {
        it->second.ownership = new_ownership;
      }
    }
  }

  // Update on the object record.
  {
    auto nit = obj_handle_to_name_.find(handle_id);
    if (nit != obj_handle_to_name_.end()) {
      auto it = objects_.find(nit->second);
      if (it != objects_.end() && it->second) {
        it->second->ownership = new_ownership;
      }
    }
  }

  return true;
}

// -- Query ------------------------------------------------------------------

size_t FFIRegistry::FunctionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return functions_.size();
}

size_t FFIRegistry::ObjectCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return objects_.size();
}

size_t FFIRegistry::LibraryCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return libraries_.size();
}

std::vector<std::string> FFIRegistry::ListFunctions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(functions_.size());
  for (const auto &[n, _] : functions_) names.push_back(n);
  return names;
}

std::vector<std::string> FFIRegistry::ListObjects() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(objects_.size());
  for (const auto &[n, _] : objects_) names.push_back(n);
  return names;
}

void FFIRegistry::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  functions_.clear();
  fn_handle_to_name_.clear();

  // Release all tracked objects.
  for (auto &[name, obj] : objects_) {
    ReleaseForeign(obj);
  }
  objects_.clear();
  obj_handle_to_name_.clear();

  // Close all libraries.
  for (auto &[id, lib] : libraries_) {
    lib.Close();
  }
  libraries_.clear();

  OwnershipTracker::Instance().Reset();
}

// ===========================================================================
// Free-standing convenience helpers (backward-compatible API)
// ===========================================================================

ForeignFunction Bind(const std::string &name, void *address) {
  ForeignFunction fn;
  fn.name = name;
  fn.address = address;
  fn.signature.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  fn.ownership = Ownership::kBorrowed;
  return fn;
}

ForeignObject *BindBorrowed(const std::string &name, void *address, size_t size) {
  (void)name;
  return AcquireBorrowed(address, size);
}

ForeignObject *BindOwned(const std::string &name, void *address, size_t size,
                         std::function<void(void *)> deleter) {
  (void)name;
  return AcquireOwned(address, size, std::move(deleter));
}

ForeignObject *BindShared(const std::string &name, void *address, size_t size,
                          std::function<void(void *)> deleter) {
  (void)name;
  return AcquireShared(address, size, std::move(deleter));
}

ForeignFunction BindWithSignature(const std::string &name, void *address,
                                  const ForeignSignature &sig,
                                  Ownership ownership) {
  ForeignFunction fn;
  fn.name = name;
  fn.address = address;
  fn.signature = sig;
  fn.ownership = ownership;

  // Register with the global ownership tracker so that the handle's lifecycle
  // is observable by the runtime.
  OwnershipTracker::Instance().Register(name, ownership,
                                        ForeignHandle::Kind::kFunction, address);
  return fn;
}

}  // namespace polyglot::runtime::interop
