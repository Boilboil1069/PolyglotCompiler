/**
 * @file     dependency_graph.h
 * @brief    Cross-ecosystem dependency graph + conflict detection.
 *
 * The IDE renders this graph in two modes — a hierarchical tree
 * (one node per direct dependency, children for transitives) and
 * a force-directed graph (every edge in the resolution).  Version
 * conflicts (the same logical package pinned to different versions
 * along disjoint paths) are highlighted; the workspace can export
 * the current view as SVG for sharing.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace polyglot::tools::ui::packages {

struct DependencyNode {
  std::string id;            ///< "name@version" (or just name for roots).
  std::string name;
  std::string version;
  bool is_root{false};
};

struct DependencyEdge {
  std::string from;          ///< parent node id
  std::string to;            ///< child node id
  std::string requirement;   ///< e.g. "^1.2", ">=2.0,<3.0".
};

struct VersionConflict {
  std::string name;
  std::vector<std::string> versions;
};

class DependencyGraph {
 public:
  void AddNode(DependencyNode node);
  void AddEdge(DependencyEdge edge);

  /// Mark `id` as a root dependency (direct, declared by the user).
  void MarkRoot(const std::string &id);

  const std::vector<DependencyNode> &nodes() const { return nodes_; }
  const std::vector<DependencyEdge> &edges() const { return edges_; }

  /// Children (direct out-edges) of `id`, in insertion order.
  std::vector<const DependencyNode *> Children(const std::string &id) const;

  /// All packages that resolve to more than one distinct version.
  std::vector<VersionConflict> Conflicts() const;

  /// Render the graph as a self-contained SVG document.  The
  /// layout is a deterministic, columnar Sugiyama-lite suitable
  /// for embedding in documentation and PR comments.
  std::string ExportSvg() const;

  /// Render only the tree projection rooted at the marked roots.
  /// Cycles are broken at first revisit.
  std::vector<std::pair<int, const DependencyNode *>> TreeView() const;

 private:
  std::vector<DependencyNode> nodes_;
  std::vector<DependencyEdge> edges_;
  std::unordered_map<std::string, std::size_t> node_index_;
};

}  // namespace polyglot::tools::ui::packages
