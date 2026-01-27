#include "runtime/include/gc/gc_api.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>

namespace polyglot::runtime::gc {
namespace {

struct Block {
  void *ptr{nullptr};
  size_t size{0};
  bool marked{false};
};

}  // namespace

class MarkSweepGC : public GC {
 public:
  void *Allocate(size_t size) override {
    void *mem = std::malloc(size);
    if (!mem) return nullptr;
    blocks_.push_back({mem, size, false});
    index_[mem] = blocks_.size() - 1;
    return mem;
  }

  void Collect() override {
    for (auto &b : blocks_) b.marked = false;
    for (auto *slot : roots_) {
      if (!slot || !*slot) continue;
      auto it = index_.find(*slot);
      if (it != index_.end()) blocks_[it->second].marked = true;
    }
    Sweep();
  }

  void RegisterRoot(void **slot) override {
    if (!slot) return;
    roots_.push_back(slot);
  }

  void UnregisterRoot(void **slot) override {
    if (!slot) return;
    roots_.erase(std::remove(roots_.begin(), roots_.end(), slot), roots_.end());
  }

 private:
  void Sweep() {
    std::size_t i = 0;
    while (i < blocks_.size()) {
      if (blocks_[i].marked) {
        ++i;
        continue;
      }
      std::free(blocks_[i].ptr);
      index_.erase(blocks_[i].ptr);
      if (i + 1 != blocks_.size()) {
        blocks_[i] = blocks_.back();
        index_[blocks_[i].ptr] = i;
      }
      blocks_.pop_back();
    }
  }

  std::vector<Block> blocks_;
  std::unordered_map<void *, std::size_t> index_;
  std::vector<void **> roots_;
};

std::unique_ptr<GC> MakeMarkSweepGC() { return std::make_unique<MarkSweepGC>(); }

}  // namespace polyglot::runtime::gc
