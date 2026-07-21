/**
 * @file     hex_viewer.h
 * @brief    Chunked hex viewer for very large files (>= 1 GiB).
 *           Locates / searches / jumps without ever materialising
 *           the full file in memory.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::viewer {

/// Reader callback: fill `out` with up to `length` bytes starting
/// at `offset`; return the number of bytes actually read.  The
/// IDE backs this with a memory-mapped file or chunked I/O.
using HexReader =
    std::function<size_t(uint64_t offset, uint64_t length,
                         std::vector<uint8_t> &out)>;

struct HexHighlight {
  uint64_t offset{0};
  uint64_t length{0};
  std::string label;
};

class HexViewer {
 public:
  HexViewer(uint64_t total_size, uint64_t chunk_size, HexReader reader);

  uint64_t total_size() const { return total_size_; }
  uint64_t chunk_size() const { return chunk_size_; }

  /// Read `length` bytes from `offset`.  Result is clipped to
  /// the file end.
  std::vector<uint8_t> Read(uint64_t offset, uint64_t length) const;

  /// Forward search for `needle` starting at `from`.  Scans in
  /// chunks of `chunk_size_` so we never load more than one
  /// chunk plus a `needle.size()` overlap at a time.  Returns
  /// the absolute offset of the first match.
  std::optional<uint64_t> Find(const std::vector<uint8_t> &needle,
                                uint64_t from = 0) const;

  /// Clamp `offset` into the file and snap it down to a
  /// `bytes_per_row` multiple so the viewport always starts on a
  /// row boundary.
  uint64_t JumpTo(uint64_t offset, uint64_t bytes_per_row = 16) const;

  void AddHighlight(HexHighlight h);
  void ClearHighlights();
  std::vector<HexHighlight> HighlightsCovering(uint64_t offset,
                                                uint64_t length) const;

 private:
  uint64_t total_size_;
  uint64_t chunk_size_;
  HexReader reader_;
  std::vector<HexHighlight> highlights_;
};

}  // namespace polyglot::tools::ui::viewer
