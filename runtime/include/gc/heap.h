#pragma once

#include <memory>

#include "runtime/include/gc/gc_strategy.h"

namespace polyglot::runtime::gc {

class RootHandle {
 public:
  RootHandle() = default;
  RootHandle(GC *gc, void **slot) : gc_(gc), slot_(slot) { if (gc_ && slot_) gc_->RegisterRoot(slot_); }
  RootHandle(const RootHandle &) = delete;
  RootHandle &operator=(const RootHandle &) = delete;
  RootHandle(RootHandle &&other) noexcept { MoveFrom(other); }
  RootHandle &operator=(RootHandle &&other) noexcept {
    if (this != &other) {
      Release();
      MoveFrom(other);
    }
    return *this;
  }
  ~RootHandle() { Release(); }

  void Reset(void **slot) {
    if (gc_ && slot_) gc_->UnregisterRoot(slot_);
    slot_ = slot;
    if (gc_ && slot_) gc_->RegisterRoot(slot_);
  }

 private:
  void Release() {
    if (gc_ && slot_) gc_->UnregisterRoot(slot_);
    gc_ = nullptr;
    slot_ = nullptr;
  }
  void MoveFrom(RootHandle &other) {
    gc_ = other.gc_;
    slot_ = other.slot_;
    other.gc_ = nullptr;
    other.slot_ = nullptr;
  }

  GC *gc_{nullptr};
  void **slot_{nullptr};
};

class Heap {
 public:
  explicit Heap(Strategy strategy = Strategy::kMarkSweep) : gc_(MakeGC(strategy)) {}

  void *Allocate(size_t size) { return gc_ ? gc_->Allocate(size) : nullptr; }
  void Collect() { if (gc_) gc_->Collect(); }
  RootHandle Track(void **slot) { return RootHandle(gc_.get(), slot); }
  GC *Raw() { return gc_.get(); }

  // Query runtime statistics from the underlying collector.
  GCStats GetStats() const { return gc_ ? gc_->GetStats() : GCStats{}; }

 private:
  std::unique_ptr<GC> gc_;
};

}  // namespace polyglot::runtime::gc
