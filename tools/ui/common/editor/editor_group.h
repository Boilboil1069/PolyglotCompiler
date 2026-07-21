/**
 * @file     editor_group.h
 * @brief    Tab + group model for the multi-tab editor surface
 *           (demand 2026-04-28-25 §1).
 *
 * The model is intentionally Qt-free so it can be exercised by
 * Catch2 unit tests without a `QApplication`.  The Qt widget
 * (`EditorGroupWidget`) is a thin adapter that mirrors `EditorGroup`
 * mutations into a `QTabBar` — see `tools/ui/common/src/`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui {

/// A single editor tab.  `path` is the unique identity used for
/// look-ups; `pinned` tabs are kept at the front of the tab strip and
/// survive `CloseOthers` / `CloseToRight`.
struct EditorTab {
  std::string path;     ///< Absolute or workspace-relative path.
  std::string title;    ///< Display label (usually the file name).
  bool pinned{false};
  bool dirty{false};
};

/// Ordered collection of `EditorTab`s with a single active tab.
class EditorGroup {
 public:
  /// Returns the current tabs in display order: pinned first, then
  /// unpinned, both preserving insertion order.
  const std::vector<EditorTab> &Tabs() const { return tabs_; }

  /// Index of the currently active tab, or `std::nullopt` when the
  /// group is empty.
  std::optional<std::size_t> ActiveIndex() const { return active_; }

  /// Append a tab and make it active.  If a tab with the same path
  /// already exists, it is brought into focus instead of being
  /// duplicated.
  std::size_t Open(EditorTab tab);

  /// Close the tab at `index`.  When the active tab is closed, focus
  /// moves to the previous tab (or the first remaining one).  No-op
  /// when `index` is out of range.
  void Close(std::size_t index);

  /// Close every tab except the one at `keep_index` and the pinned
  /// tabs.  Pinned tabs are always preserved.
  void CloseOthers(std::size_t keep_index);

  /// Close every tab strictly to the right of `index`.  Pinned tabs
  /// to the right are preserved.
  void CloseToRight(std::size_t index);

  /// Toggle the pinned flag.  Pinning re-orders the tab to the end of
  /// the pinned prefix; unpinning moves it to the start of the
  /// unpinned suffix.
  void TogglePin(std::size_t index);

  /// Re-order a tab inside the group.  If `from` is pinned, `to` is
  /// clamped to the pinned prefix; analogous for unpinned.
  void Reorder(std::size_t from, std::size_t to);

  /// Find the tab with the given path; returns `npos` (`SIZE_MAX`)
  /// when no match is found.
  std::size_t Find(const std::string &path) const;

  /// Update the active tab.  Out-of-range indices are ignored.
  void Activate(std::size_t index);

 private:
  std::vector<EditorTab> tabs_;
  std::optional<std::size_t> active_;

  std::size_t PinnedCount() const;
};

}  // namespace polyglot::tools::ui
