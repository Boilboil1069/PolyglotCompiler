/**
 * @file     hex_viewer.cpp
 * @brief    Chunked hex viewer implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/viewer/hex_viewer.h"

#include <algorithm>

namespace polyglot::tools::ui::viewer {

HexViewer::HexViewer(uint64_t total, uint64_t chunk, HexReader r)
    : total_size_(total),
      chunk_size_(chunk == 0 ? 4096 : chunk),
      reader_(std::move(r)) {}

std::vector<uint8_t> HexViewer::Read(uint64_t offset,
                                     uint64_t length) const {
  std::vector<uint8_t> out;
  if (offset >= total_size_ || length == 0) return out;
  uint64_t want = std::min(length, total_size_ - offset);
  reader_(offset, want, out);
  if (out.size() > want) out.resize(static_cast<size_t>(want));
  return out;
}

std::optional<uint64_t> HexViewer::Find(
    const std::vector<uint8_t> &needle, uint64_t from) const {
  if (needle.empty() || from >= total_size_) return std::nullopt;
  uint64_t cursor = from;
  std::vector<uint8_t> tail;          // overlap from previous chunk
  while (cursor < total_size_) {
    uint64_t want = std::min(chunk_size_, total_size_ - cursor);
    std::vector<uint8_t> chunk;
    reader_(cursor, want, chunk);
    if (chunk.empty()) break;
    std::vector<uint8_t> buf;
    buf.reserve(tail.size() + chunk.size());
    buf.insert(buf.end(), tail.begin(), tail.end());
    buf.insert(buf.end(), chunk.begin(), chunk.end());
    auto it = std::search(buf.begin(), buf.end(),
                          needle.begin(), needle.end());
    if (it != buf.end()) {
      uint64_t local = static_cast<uint64_t>(it - buf.begin());
      uint64_t base = cursor - tail.size();
      return base + local;
    }
    // Keep the last (needle.size()-1) bytes for the next round.
    size_t keep = std::min<size_t>(needle.size() - 1, buf.size());
    tail.assign(buf.end() - keep, buf.end());
    cursor += chunk.size();
  }
  return std::nullopt;
}

uint64_t HexViewer::JumpTo(uint64_t offset, uint64_t row) const {
  if (row == 0) row = 16;
  uint64_t clamped = std::min(offset, total_size_);
  return (clamped / row) * row;
}

void HexViewer::AddHighlight(HexHighlight h) {
  highlights_.push_back(std::move(h));
}

void HexViewer::ClearHighlights() { highlights_.clear(); }

std::vector<HexHighlight> HexViewer::HighlightsCovering(
    uint64_t offset, uint64_t length) const {
  std::vector<HexHighlight> out;
  uint64_t end = offset + length;
  for (const auto &h : highlights_) {
    uint64_t he = h.offset + h.length;
    if (he <= offset || h.offset >= end) continue;
    out.push_back(h);
  }
  return out;
}

}  // namespace polyglot::tools::ui::viewer
