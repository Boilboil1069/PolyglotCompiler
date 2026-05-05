/**
 * @file     recent.h
 * @brief    Recent files / workspaces list (Ctrl+R / Ctrl+E).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::shell {

struct RecentEntry {
  std::string path;
  std::string label;
  long long last_opened_unix{0};
  bool pinned{false};
};

/// Bounded recent list with pinning.  `Touch` moves an entry to
/// the top; pinned entries always sort above unpinned ones while
/// preserving relative recency.
class RecentList {
 public:
  explicit RecentList(size_t capacity = 32) : capacity_(capacity) {}

  void Touch(const RecentEntry &e);
  bool Pin(const std::string &path, bool pinned);
  bool Remove(const std::string &path);
  void Clear();
  std::vector<RecentEntry> Items() const;
  std::optional<RecentEntry> Find(const std::string &path) const;
  size_t capacity() const { return capacity_; }

  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  size_t capacity_;
  std::vector<RecentEntry> items_;
  void Trim();
};

}  // namespace polyglot::tools::ui::shell
