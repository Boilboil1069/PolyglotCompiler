/**
 * @file     incremental.cpp
 * @brief    Runtime implementation
 *
 * @ingroup  Runtime
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "runtime/include/gc/gc_api.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::runtime::gc {
namespace {

// Incremental GC - tri-color marking
// White: object not yet visited
// Gray: visited but references not yet scanned
// Black: visited and fully scanned

struct IBlock {
  void *ptr{nullptr};
  size_t size{0};
  enum Color { WHITE, GRAY, BLACK } color{WHITE};
};

constexpr size_t kIncrementalStepSize = 100;  // Number of objects processed per incremental step

}  // namespace

class IncrementalGC : public GC {
 public:
  void *Allocate(size_t size) override {
    std::lock_guard<std::mutex> lock(mu_);
    void *mem = std::malloc(size);
    if (!mem) return nullptr;

    blocks_.push_back({mem, size, IBlock::WHITE});
    index_[mem] = blocks_.size() - 1;

    total_allocations_++;
    total_bytes_allocated_ += size;
    current_heap_bytes_ += size;
    if (current_heap_bytes_ > peak_heap_bytes_) {
      peak_heap_bytes_ = current_heap_bytes_;
    }

    // Perform a small incremental GC step after each allocation
    if (!gc_running_) {
      StartGC();
    }
    IncrementalStep();

    return mem;
  }

  void Collect() override {
    std::lock_guard<std::mutex> lock(mu_);
    // Run a full GC cycle
    StartGC();
    while (gc_running_) {
      IncrementalStep();
    }
    collections_++;
  }

  void RegisterRoot(void **slot) override {
    std::lock_guard<std::mutex> lock(mu_);
    if (!slot) return;
    roots_.push_back(slot);
  }

  void UnregisterRoot(void **slot) override {
    std::lock_guard<std::mutex> lock(mu_);
    if (!slot) return;
    roots_.erase(std::remove(roots_.begin(), roots_.end(), slot), roots_.end());
  }

  GCStats GetStats() const override {
    std::lock_guard<std::mutex> lock(mu_);
    GCStats stats;
    stats.total_allocations = total_allocations_;
    stats.total_bytes_allocated = total_bytes_allocated_;
    stats.current_heap_bytes = current_heap_bytes_;
    stats.peak_heap_bytes = peak_heap_bytes_;
    stats.collections = collections_;
    stats.total_freed_bytes = total_freed_bytes_;
    stats.live_objects = blocks_.size();
    stats.root_count = roots_.size();
    return stats;
  }

 private:
  void StartGC() {
    // Mark all objects white
    for (auto &b : blocks_) {
      b.color = IBlock::WHITE;
    }

    // Mark all roots gray
    gray_set_.clear();
    for (auto **root : roots_) {
      if (!root || !*root) continue;
      auto it = index_.find(*root);
      if (it != index_.end()) {
        blocks_[it->second].color = IBlock::GRAY;
        gray_set_.insert(it->second);
      }
    }

    gc_running_ = true;
  }

  void IncrementalStep() {
    if (!gc_running_) return;

    // Process a batch of gray objects
    size_t processed = 0;
    std::unordered_set<size_t> new_gray;

    for (auto idx : gray_set_) {
      if (processed >= kIncrementalStepSize) {
        new_gray.insert(idx);
        continue;
      }

      // Mark the gray object black
      blocks_[idx].color = IBlock::BLACK;
      ++processed;

      // In a full implementation, scan references
      // and move referenced white objects to gray.
      // Simplified version omits this.
    }

    gray_set_ = std::move(new_gray);

    // If no gray objects remain, enter sweep phase
    if (gray_set_.empty()) {
      Sweep();
      gc_running_ = false;
    }
  }

  void Sweep() {
    size_t i = 0;
    while (i < blocks_.size()) {
      if (blocks_[i].color == IBlock::BLACK) {
        ++i;
        continue;
      }

      // Free white and any stray gray objects (gray should not remain)
      current_heap_bytes_ -= blocks_[i].size;
      total_freed_bytes_ += blocks_[i].size;
      std::free(blocks_[i].ptr);
      index_.erase(blocks_[i].ptr);

      if (i + 1 != blocks_.size()) {
        blocks_[i] = blocks_.back();
        index_[blocks_[i].ptr] = i;

        // Update indices inside the gray set
        if (gray_set_.count(blocks_.size() - 1)) {
          gray_set_.erase(blocks_.size() - 1);
          gray_set_.insert(i);
        }
      }
      blocks_.pop_back();
    }
  }

  std::vector<IBlock> blocks_;
  std::unordered_map<void *, size_t> index_;
  std::vector<void **> roots_;
  std::unordered_set<size_t> gray_set_;
  bool gc_running_{false};
  mutable std::mutex mu_;

  // Accumulated statistics
  size_t total_allocations_{0};
  size_t total_bytes_allocated_{0};
  size_t current_heap_bytes_{0};
  size_t peak_heap_bytes_{0};
  size_t collections_{0};
  size_t total_freed_bytes_{0};
};

std::unique_ptr<GC> MakeIncrementalGC() { return std::make_unique<IncrementalGC>(); }

}  // namespace polyglot::runtime::gc
