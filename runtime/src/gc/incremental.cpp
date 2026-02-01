#include "runtime/include/gc/gc_api.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::runtime::gc {
namespace {

// 增量式GC（Incremental GC）- 三色标记算法
// 白色：未访问的对象
// 灰色：已访问但未扫描其引用的对象
// 黑色：已访问且扫描完成的对象

struct IBlock {
  void *ptr{nullptr};
  size_t size{0};
  enum Color { WHITE, GRAY, BLACK } color{WHITE};
};

constexpr size_t kIncrementalStepSize = 100;  // 每次增量处理的对象数

}  // namespace

class IncrementalGC : public GC {
 public:
  void *Allocate(size_t size) override {
    void *mem = std::malloc(size);
    if (!mem) return nullptr;

    blocks_.push_back({mem, size, IBlock::WHITE});
    index_[mem] = blocks_.size() - 1;

    // 每次分配后执行一小步增量GC
    if (!gc_running_) {
      StartGC();
    }
    IncrementalStep();

    return mem;
  }

  void Collect() override {
    // 执行完整的GC周期
    StartGC();
    while (gc_running_) {
      IncrementalStep();
    }
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
  void StartGC() {
    // 将所有对象标记为白色
    for (auto &b : blocks_) {
      b.color = IBlock::WHITE;
    }

    // 将所有根对象标记为灰色
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

    // 处理一批灰色对象
    size_t processed = 0;
    std::unordered_set<size_t> new_gray;

    for (auto idx : gray_set_) {
      if (processed >= kIncrementalStepSize) {
        new_gray.insert(idx);
        continue;
      }

      // 将灰色对象标记为黑色
      blocks_[idx].color = IBlock::BLACK;
      ++processed;

      // 在完整实现中，这里需要扫描对象内的引用
      // 并将引用的白色对象标记为灰色
      // 简化版本跳过这一步
    }

    gray_set_ = std::move(new_gray);

    // 如果没有灰色对象了，进入清除阶段
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

      // 清除白色和灰色对象（灰色对象理论上不应该存在）
      std::free(blocks_[i].ptr);
      index_.erase(blocks_[i].ptr);

      if (i + 1 != blocks_.size()) {
        blocks_[i] = blocks_.back();
        index_[blocks_[i].ptr] = i;

        // 更新灰色集合中的索引
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
};

std::unique_ptr<GC> MakeIncrementalGC() { return std::make_unique<IncrementalGC>(); }

}  // namespace polyglot::runtime::gc
