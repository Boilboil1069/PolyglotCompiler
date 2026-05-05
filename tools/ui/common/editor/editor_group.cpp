/**
 * @file     editor_group.cpp
 * @brief    Implementation of `EditorGroup` (demand 2026-04-28-25 §1).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editor/editor_group.h"

#include <algorithm>
#include <limits>

namespace polyglot::tools::ui {

std::size_t EditorGroup::PinnedCount() const {
  std::size_t n = 0;
  for (const auto &t : tabs_) {
    if (t.pinned) ++n;
    else break;
  }
  return n;
}

std::size_t EditorGroup::Find(const std::string &path) const {
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].path == path) return i;
  }
  return std::numeric_limits<std::size_t>::max();
}

std::size_t EditorGroup::Open(EditorTab tab) {
  const std::size_t found = Find(tab.path);
  if (found != std::numeric_limits<std::size_t>::max()) {
    Activate(found);
    return found;
  }
  // Insert at the end of the appropriate region.
  const std::size_t insert_at =
      tab.pinned ? PinnedCount() : tabs_.size();
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(insert_at),
               std::move(tab));
  active_ = insert_at;
  return insert_at;
}

void EditorGroup::Close(std::size_t index) {
  if (index >= tabs_.size()) return;
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
  if (tabs_.empty()) {
    active_.reset();
    return;
  }
  // Move focus left when possible, else stay on the new tab at this
  // index (which used to be at index + 1).
  std::size_t next =
      (active_ && *active_ > index) ? *active_ - 1 : index;
  if (next >= tabs_.size()) next = tabs_.size() - 1;
  active_ = next;
}

void EditorGroup::CloseOthers(std::size_t keep_index) {
  if (keep_index >= tabs_.size()) return;
  EditorTab keep = tabs_[keep_index];
  std::vector<EditorTab> kept;
  kept.reserve(tabs_.size());
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (i == keep_index || tabs_[i].pinned) kept.push_back(tabs_[i]);
  }
  tabs_ = std::move(kept);
  // Re-locate the kept tab.
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].path == keep.path) {
      active_ = i;
      break;
    }
  }
}

void EditorGroup::CloseToRight(std::size_t index) {
  if (index >= tabs_.size()) return;
  std::vector<EditorTab> kept;
  kept.reserve(tabs_.size());
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (i <= index || tabs_[i].pinned) kept.push_back(tabs_[i]);
  }
  tabs_ = std::move(kept);
  if (active_ && *active_ >= tabs_.size()) {
    active_ = tabs_.empty() ? std::optional<std::size_t>()
                            : std::optional<std::size_t>(tabs_.size() - 1);
  }
}

void EditorGroup::TogglePin(std::size_t index) {
  if (index >= tabs_.size()) return;
  EditorTab tab = tabs_[index];
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
  tab.pinned = !tab.pinned;
  const std::size_t insert_at =
      tab.pinned ? PinnedCount() : tabs_.size();
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(insert_at), tab);
  active_ = insert_at;
}

void EditorGroup::Reorder(std::size_t from, std::size_t to) {
  if (from >= tabs_.size() || to >= tabs_.size() || from == to) return;
  const bool from_pinned = tabs_[from].pinned;
  const std::size_t pinned = PinnedCount();
  // Clamp `to` to the appropriate region so the pinned/unpinned
  // partition invariant is preserved.
  std::size_t clamped_to = to;
  if (from_pinned && clamped_to >= pinned) clamped_to = pinned - 1;
  if (!from_pinned && clamped_to < pinned) clamped_to = pinned;
  EditorTab moved = tabs_[from];
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(clamped_to),
               std::move(moved));
  active_ = clamped_to;
}

void EditorGroup::Activate(std::size_t index) {
  if (index >= tabs_.size()) return;
  active_ = index;
}

}  // namespace polyglot::tools::ui
