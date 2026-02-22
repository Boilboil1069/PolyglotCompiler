#include "runtime/include/gc/gc_api.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace polyglot::runtime::gc {
namespace {

struct GBlock {
  void *ptr{nullptr};
  size_t size{0};
  bool marked{false};
  unsigned age{0};
};

constexpr unsigned kPromotionAge = 2;
constexpr unsigned kOldCycle = 4;

}  // namespace

class GenerationalGC : public GC {
 public:
  void *Allocate(size_t size) override {
    std::lock_guard<std::mutex> lock(mu_);
    void *mem = std::malloc(size);
    if (!mem) return nullptr;
    young_.push_back({mem, size, false, 0});
    young_index_[mem] = young_.size() - 1;
    total_allocations_++;
    total_bytes_allocated_ += size;
    current_heap_bytes_ += size;
    if (current_heap_bytes_ > peak_heap_bytes_) {
      peak_heap_bytes_ = current_heap_bytes_;
    }
    return mem;
  }

  void Collect() override {
    std::lock_guard<std::mutex> lock(mu_);
    ++cycle_;
    for (auto &b : young_) b.marked = false;
    for (auto &b : old_) b.marked = false;
    MarkGeneration(young_, young_index_);
    if (cycle_ % kOldCycle == 0) MarkGeneration(old_, old_index_);
    PromoteSurvivors();
    SweepGeneration(young_, young_index_);
    if (cycle_ % kOldCycle == 0) SweepGeneration(old_, old_index_);
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
    stats.live_objects = young_.size() + old_.size();
    stats.root_count = roots_.size();
    return stats;
  }

 private:
  void MarkGeneration(std::vector<GBlock> &gen, std::unordered_map<void *, std::size_t> &index) {
    for (auto *slot : roots_) {
      if (!slot || !*slot) continue;
      auto it = index.find(*slot);
      if (it != index.end()) gen[it->second].marked = true;
    }
  }

  void PromoteSurvivors() {
    std::size_t i = 0;
    while (i < young_.size()) {
      if (young_[i].marked) {
        ++young_[i].age;
        if (young_[i].age >= kPromotionAge) {
          old_index_[young_[i].ptr] = old_.size();
          old_.push_back(young_[i]);
          young_index_.erase(young_[i].ptr);
          if (i + 1 != young_.size()) {
            young_[i] = young_.back();
            young_index_[young_[i].ptr] = i;
          }
          young_.pop_back();
          continue;
        }
      }
      ++i;
    }
  }

  void SweepGeneration(std::vector<GBlock> &gen, std::unordered_map<void *, std::size_t> &index) {
    std::size_t i = 0;
    while (i < gen.size()) {
      if (gen[i].marked) {
        ++i;
        continue;
      }
      current_heap_bytes_ -= gen[i].size;
      total_freed_bytes_ += gen[i].size;
      std::free(gen[i].ptr);
      index.erase(gen[i].ptr);
      if (i + 1 != gen.size()) {
        gen[i] = gen.back();
        index[gen[i].ptr] = i;
      }
      gen.pop_back();
    }
  }

  std::vector<GBlock> young_;
  std::vector<GBlock> old_;
  std::unordered_map<void *, std::size_t> young_index_;
  std::unordered_map<void *, std::size_t> old_index_;
  std::vector<void **> roots_;
  unsigned cycle_{0};
  mutable std::mutex mu_;

  // Accumulated statistics
  size_t total_allocations_{0};
  size_t total_bytes_allocated_{0};
  size_t current_heap_bytes_{0};
  size_t peak_heap_bytes_{0};
  size_t collections_{0};
  size_t total_freed_bytes_{0};
};

std::unique_ptr<GC> MakeGenerationalGC() { return std::make_unique<GenerationalGC>(); }

}  // namespace polyglot::runtime::gc
