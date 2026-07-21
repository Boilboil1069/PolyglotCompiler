/**
 * @file     topology_live.h
 * @brief    Live topology tracker (follows current file/symbol).
 *
 * The static topology panel renders the full project topology;
 * the live tracker on top of it keeps a `LiveTopologyView` in sync
 * with the editor's current file and selected symbol, dispatches a
 * debounced rebuild on edits, and resolves clicks on topology
 * nodes back to source positions.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::topology {

struct Node {
  std::string id;
  std::string label;
  std::string source_file;
  int source_line{0};
  std::vector<std::string> tags;        ///< e.g. "function","module".
};

struct Edge {
  std::string from;
  std::string to;
  std::string kind;                     ///< "calls","imports","links".
};

class TopologyGraph {
 public:
  void AddNode(Node n);
  void AddEdge(Edge e);
  void Clear();

  const std::vector<Node> &nodes() const { return nodes_; }
  const std::vector<Edge> &edges() const { return edges_; }
  const Node *FindNode(const std::string &id) const;

  /// Subgraph rooted at `id` reachable in `radius` hops in either
  /// direction.  `radius=0` returns just the node.
  TopologyGraph Neighbourhood(const std::string &id, int radius) const;

 private:
  std::vector<Node> nodes_;
  std::vector<Edge> edges_;
};

/// Live view derived from a `TopologyGraph` plus the editor's
/// current focus.  The IDE updates focus on cursor moves and
/// commits source edits; the tracker decides when to rebuild.
class LiveTopologyTracker {
 public:
  explicit LiveTopologyTracker(
      TopologyGraph base,
      std::chrono::milliseconds debounce = std::chrono::milliseconds(150));

  /// Editor reports the active source file and the symbol under
  /// the caret (may be empty).  Returns the focused subgraph.
  TopologyGraph FocusOn(const std::string &file,
                        const std::string &symbol,
                        int radius = 1);

  /// Editor reports a buffer change at `now`; the tracker decides
  /// whether enough quiet time has passed to rebuild.
  bool ShouldRebuild(std::chrono::steady_clock::time_point now);

  /// Notify the tracker that an edit just happened.
  void NotifyEdit(std::chrono::steady_clock::time_point now);

  /// Replace the base graph after an incremental rebuild.
  void ReplaceGraph(TopologyGraph g);

  const TopologyGraph &base() const { return base_; }
  const std::string &current_file() const { return current_file_; }
  const std::string &current_symbol() const { return current_symbol_; }

  /// Resolve a click on a topology node back to its source.
  std::optional<std::pair<std::string, int>> NodeSource(
      const std::string &node_id) const;

 private:
  TopologyGraph base_;
  std::chrono::milliseconds debounce_;
  std::optional<std::chrono::steady_clock::time_point> last_edit_;
  std::string current_file_;
  std::string current_symbol_;
};

}  // namespace polyglot::tools::ui::topology
