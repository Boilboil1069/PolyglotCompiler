#include "runtime/include/gc/gc_api.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace polyglot::runtime::gc {
namespace {

// 复制式GC（Copying GC）- 半空间收集器
// 将堆分为两个半空间：from-space 和 to-space
// 分配总是在 from-space 进行，GC 时将存活对象复制到 to-space，然后交换空间

constexpr size_t kSemiSpaceSize = 1024 * 1024 * 8;  // 8MB per semi-space

struct ObjectHeader {
  size_t size;
  void *forwarding_addr;  // 用于复制时记录新地址
  bool forwarded;
};

}  // namespace

class CopyingGC : public GC {
 public:
  CopyingGC() {
    from_space_ = std::malloc(kSemiSpaceSize);
    to_space_ = std::malloc(kSemiSpaceSize);
    from_ptr_ = static_cast<char *>(from_space_);
    from_limit_ = from_ptr_ + kSemiSpaceSize;
    to_ptr_ = static_cast<char *>(to_space_);
  }

  ~CopyingGC() override {
    std::free(from_space_);
    std::free(to_space_);
  }

  void *Allocate(size_t size) override {
    std::lock_guard<std::mutex> lock(mu_);
    // Align to 8 bytes
    size = (size + 7) & ~7;
    size_t total_size = sizeof(ObjectHeader) + size;

    // Check if there is enough space
    if (from_ptr_ + total_size > from_limit_) {
      CollectImpl();
      // If still not enough after collection, return nullptr
      if (from_ptr_ + total_size > from_limit_) {
        return nullptr;
      }
    }

    // Allocate object
    auto *header = reinterpret_cast<ObjectHeader *>(from_ptr_);
    header->size = size;
    header->forwarding_addr = nullptr;
    header->forwarded = false;

    void *obj = from_ptr_ + sizeof(ObjectHeader);
    from_ptr_ += total_size;

    total_allocations_++;
    total_bytes_allocated_ += size;
    current_heap_bytes_ += size;
    live_objects_++;
    if (current_heap_bytes_ > peak_heap_bytes_) {
      peak_heap_bytes_ = current_heap_bytes_;
    }

    return obj;
  }

  void Collect() override {
    std::lock_guard<std::mutex> lock(mu_);
    CollectImpl();
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
    stats.live_objects = live_objects_;
    stats.root_count = roots_.size();
    return stats;
  }

 private:
  // Internal collect without locking (caller must hold mu_).
  void CollectImpl() {
    // Reset to-space pointer
    to_ptr_ = static_cast<char *>(to_space_);

    // Copy all root objects
    for (auto **root : roots_) {
      if (!root || !*root) continue;
      *root = Copy(*root);
    }

    // Swap spaces
    std::swap(from_space_, to_space_);
    std::swap(from_ptr_, to_ptr_);
    from_limit_ = static_cast<char *>(from_space_) + kSemiSpaceSize;
  }
  void *Copy(void *obj) {
    if (!obj) return nullptr;

    // 获取对象头
    auto *header = reinterpret_cast<ObjectHeader *>(static_cast<char *>(obj) - sizeof(ObjectHeader));

    // 如果已经被复制，返回转发地址
    if (header->forwarded) {
      return header->forwarding_addr;
    }

    // 复制到 to-space
    size_t total_size = sizeof(ObjectHeader) + header->size;
    void *new_obj = to_ptr_ + sizeof(ObjectHeader);

    std::memcpy(to_ptr_, header, total_size);
    to_ptr_ += total_size;

    // 设置转发地址
    header->forwarded = true;
    header->forwarding_addr = new_obj;

    return new_obj;
  }

  void *from_space_{nullptr};
  void *to_space_{nullptr};
  char *from_ptr_{nullptr};
  char *from_limit_{nullptr};
  char *to_ptr_{nullptr};

  std::vector<void **> roots_;
  mutable std::mutex mu_;

  // Accumulated statistics
  size_t total_allocations_{0};
  size_t total_bytes_allocated_{0};
  size_t current_heap_bytes_{0};
  size_t peak_heap_bytes_{0};
  size_t collections_{0};
  size_t total_freed_bytes_{0};
  size_t live_objects_{0};
};

std::unique_ptr<GC> MakeCopyingGC() { return std::make_unique<CopyingGC>(); }

}  // namespace polyglot::runtime::gc
