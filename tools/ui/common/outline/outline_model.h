/**
 * @file     outline_model.h
 * @brief    Tree model for the Outline panel and Breadcrumbs
 *           (demand 2026-04-28-25 §5).
 *
 * Built from the LSP `textDocument/documentSymbol` response (or the
 * polyls in-process equivalent).  Operations are pure C++ so they are
 * unit-testable; the QAbstractItemModel adapter sits in the editor
 * sources.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace polyglot::tools::ui {

struct OutlineNode {
  std::string name;
  std::string kind;            ///< "function" / "class" / "variable" / …
  std::uint32_t line{0};
  std::uint32_t character{0};
  std::vector<std::unique_ptr<OutlineNode>> children;
};

class OutlineModel {
 public:
  /// Replace the model contents.  The vector is consumed.
  void SetRoots(std::vector<std::unique_ptr<OutlineNode>> roots);

  const std::vector<std::unique_ptr<OutlineNode>> &Roots() const {
    return roots_;
  }

  /// Re-filter the tree using a case-insensitive substring match.
  /// Nodes whose name does not contain @p needle and whose
  /// descendants also do not match are hidden from @ref Visible.
  /// An empty needle restores the full tree.
  void SetFilter(std::string needle);

  /// Returns top-level nodes that survived the current filter.  The
  /// returned pointers reference nodes owned by the model.
  std::vector<const OutlineNode *> Visible() const;

  /// Build the breadcrumb chain for the symbol enclosing the given
  /// document position (line / character are 0-based).  Order: root
  /// → leaf.  Empty when no symbol contains the position.
  std::vector<const OutlineNode *> Breadcrumbs(std::uint32_t line,
                                               std::uint32_t character) const;

 private:
  std::vector<std::unique_ptr<OutlineNode>> roots_;
  std::string filter_;

  bool Matches(const OutlineNode &n) const;
};

}  // namespace polyglot::tools::ui
