#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace polyglot::utils {

class Arena {
 public:
  explicit Arena(size_t block_size = 4096) : block_size_(block_size) {}

  void *Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    size_t current = Align(current_offset_, alignment);
    if (current + size > current_block_.size()) {
      AllocateBlock(std::max(block_size_, size + alignment));
      current = Align(current_offset_, alignment);
    }
    void *ptr = current_block_.data() + current;
    current_offset_ = current + size;
    return ptr;
  }

  void Reset() {
    blocks_.clear();
    current_block_.clear();
    current_offset_ = 0;
  }

 private:
  size_t Align(size_t offset, size_t alignment) const {
    size_t mask = alignment - 1;
    return (offset + mask) & ~mask;
  }

  void AllocateBlock(size_t size) {
    blocks_.push_back(std::move(current_block_));
    current_block_.assign(size, 0);
    current_offset_ = 0;
  }

  size_t block_size_;
  std::vector<std::vector<char>> blocks_{};
  std::vector<char> current_block_{};
  size_t current_offset_{0};
};

}  // namespace polyglot::utils
