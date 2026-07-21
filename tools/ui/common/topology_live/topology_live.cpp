/**
 * @file     topology_live.cpp
 * @brief    Implementation of `topology_live.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/topology_live/topology_live.h"

#include <queue>
#include <unordered_set>

namespace polyglot::tools::ui::topology {

void TopologyGraph::AddNode(Node n) {
  for (auto &existing : nodes_) {
    if (existing.id == n.id) {
      existing = std::move(n);
      return;
    }
  }
  nodes_.push_back(std::move(n));
}

void TopologyGraph::AddEdge(Edge e) { edges_.push_back(std::move(e)); }
void TopologyGraph::Clear() { nodes_.clear(); edges_.clear(); }

const Node *TopologyGraph::FindNode(const std::string &id) const {
  for (const auto &n : nodes_)
    if (n.id == id) return &n;
  return nullptr;
}

TopologyGraph TopologyGraph::Neighbourhood(const std::string &id,
                                           int radius) const {
  TopologyGraph out;
  if (!FindNode(id)) return out;

  std::unordered_set<std::string> kept{id};
  std::queue<std::pair<std::string, int>> queue;
  queue.emplace(id, 0);
  while (!queue.empty()) {
    auto [cur, d] = queue.front();
    queue.pop();
    if (d >= radius) continue;
    for (const auto &e : edges_) {
      if (e.from == cur && !kept.count(e.to)) {
        kept.insert(e.to);
        queue.emplace(e.to, d + 1);
      } else if (e.to == cur && !kept.count(e.from)) {
        kept.insert(e.from);
        queue.emplace(e.from, d + 1);
      }
    }
  }

  for (const auto &n : nodes_)
    if (kept.count(n.id)) out.AddNode(n);
  for (const auto &e : edges_)
    if (kept.count(e.from) && kept.count(e.to)) out.AddEdge(e);
  return out;
}

LiveTopologyTracker::LiveTopologyTracker(
    TopologyGraph base, std::chrono::milliseconds debounce)
    : base_(std::move(base)), debounce_(debounce) {}

TopologyGraph LiveTopologyTracker::FocusOn(const std::string &file,
                                           const std::string &symbol,
                                           int radius) {
  current_file_ = file;
  current_symbol_ = symbol;
  if (!symbol.empty()) {
    if (base_.FindNode(symbol))
      return base_.Neighbourhood(symbol, radius);
  }
  // Fall back: any node anchored at this file.
  TopologyGraph out;
  std::unordered_set<std::string> seeds;
  for (const auto &n : base_.nodes()) {
    if (n.source_file == file) {
      seeds.insert(n.id);
    }
  }
  for (const auto &id : seeds) {
    auto sub = base_.Neighbourhood(id, radius);
    for (const auto &n : sub.nodes()) out.AddNode(n);
    for (const auto &e : sub.edges()) out.AddEdge(e);
  }
  return out;
}

void LiveTopologyTracker::NotifyEdit(
    std::chrono::steady_clock::time_point now) {
  last_edit_ = now;
}

bool LiveTopologyTracker::ShouldRebuild(
    std::chrono::steady_clock::time_point now) {
  if (!last_edit_) return false;
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_edit_);
  if (elapsed >= debounce_) {
    last_edit_.reset();
    return true;
  }
  return false;
}

void LiveTopologyTracker::ReplaceGraph(TopologyGraph g) {
  base_ = std::move(g);
}

std::optional<std::pair<std::string, int>>
LiveTopologyTracker::NodeSource(const std::string &node_id) const {
  const auto *n = base_.FindNode(node_id);
  if (!n) return std::nullopt;
  if (n->source_file.empty() && n->source_line == 0) return std::nullopt;
  return std::make_pair(n->source_file, n->source_line);
}

}  // namespace polyglot::tools::ui::topology
