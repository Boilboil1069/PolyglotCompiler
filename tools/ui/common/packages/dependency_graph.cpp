/**
 * @file     dependency_graph.cpp
 * @brief    Implementation of `dependency_graph.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/packages/dependency_graph.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <unordered_set>

namespace polyglot::tools::ui::packages {

void DependencyGraph::AddNode(DependencyNode node) {
  if (node_index_.count(node.id)) return;
  node_index_[node.id] = nodes_.size();
  nodes_.push_back(std::move(node));
}

void DependencyGraph::AddEdge(DependencyEdge edge) {
  edges_.push_back(std::move(edge));
}

void DependencyGraph::MarkRoot(const std::string &id) {
  auto it = node_index_.find(id);
  if (it != node_index_.end()) nodes_[it->second].is_root = true;
}

std::vector<const DependencyNode *> DependencyGraph::Children(
    const std::string &id) const {
  std::vector<const DependencyNode *> out;
  for (const auto &e : edges_) {
    if (e.from != id) continue;
    auto it = node_index_.find(e.to);
    if (it != node_index_.end()) out.push_back(&nodes_[it->second]);
  }
  return out;
}

std::vector<VersionConflict> DependencyGraph::Conflicts() const {
  std::map<std::string, std::vector<std::string>> by_name;
  for (const auto &n : nodes_) {
    if (n.version.empty()) continue;
    auto &vec = by_name[n.name];
    if (std::find(vec.begin(), vec.end(), n.version) == vec.end())
      vec.push_back(n.version);
  }
  std::vector<VersionConflict> out;
  for (auto &kv : by_name) {
    if (kv.second.size() < 2) continue;
    VersionConflict c;
    c.name = kv.first;
    c.versions = std::move(kv.second);
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<std::pair<int, const DependencyNode *>>
DependencyGraph::TreeView() const {
  std::vector<std::pair<int, const DependencyNode *>> out;
  std::unordered_set<std::string> visited;
  std::function<void(const std::string &, int)> dfs;
  dfs = [&](const std::string &id, int depth) {
    if (!visited.insert(id).second) return;
    auto it = node_index_.find(id);
    if (it == node_index_.end()) return;
    out.emplace_back(depth, &nodes_[it->second]);
    for (const auto *c : Children(id)) dfs(c->id, depth + 1);
  };
  for (const auto &n : nodes_) {
    if (n.is_root) dfs(n.id, 0);
  }
  return out;
}

std::string DependencyGraph::ExportSvg() const {
  // Lay out nodes by their depth from any marked root using BFS;
  // unreachable nodes go in their own column at the right.
  std::unordered_map<std::string, int> depth;
  std::vector<std::string> queue;
  for (const auto &n : nodes_) {
    if (n.is_root) {
      depth[n.id] = 0;
      queue.push_back(n.id);
    }
  }
  for (std::size_t i = 0; i < queue.size(); ++i) {
    int d = depth[queue[i]];
    for (const auto *c : Children(queue[i])) {
      auto [it, inserted] = depth.emplace(c->id, d + 1);
      if (inserted) queue.push_back(c->id);
    }
  }
  int max_depth = 0;
  for (const auto &kv : depth) max_depth = std::max(max_depth, kv.second);
  for (const auto &n : nodes_) {
    if (!depth.count(n.id)) depth[n.id] = max_depth + 1;
  }

  std::map<int, std::vector<const DependencyNode *>> columns;
  for (const auto &n : nodes_) columns[depth[n.id]].push_back(&n);

  const int kNodeW = 160, kNodeH = 32;
  const int kColGap = 60, kRowGap = 18;
  std::unordered_map<std::string, std::pair<int, int>> pos;
  int width = 0;
  int height = 0;
  for (auto &kv : columns) {
    std::sort(kv.second.begin(), kv.second.end(),
              [](const DependencyNode *a, const DependencyNode *b) {
                return a->id < b->id;
              });
    int x = 20 + kv.first * (kNodeW + kColGap);
    int y = 20;
    for (const auto *n : kv.second) {
      pos[n->id] = {x, y};
      y += kNodeH + kRowGap;
    }
    width = std::max(width, x + kNodeW + 20);
    height = std::max(height, y + 20);
  }
  if (width == 0) width = 100;
  if (height == 0) height = 100;

  std::ostringstream o;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
    << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " "
    << height << "\">\n";
  o << "<style>"
       "rect{fill:#f5f7fa;stroke:#2d68a4;stroke-width:1.2}"
       ".root{fill:#dde9ff;stroke:#1d4f8f;stroke-width:2}"
       ".conflict{fill:#ffe8e0;stroke:#c4351b;stroke-width:2}"
       "text{font-family:monospace;font-size:11px;fill:#1c1c1c}"
       "line{stroke:#7c8a99;stroke-width:1}"
       "</style>\n";

  auto conflicts = Conflicts();
  std::unordered_set<std::string> conflict_names;
  for (const auto &c : conflicts) conflict_names.insert(c.name);

  for (const auto &e : edges_) {
    auto a = pos.find(e.from);
    auto b = pos.find(e.to);
    if (a == pos.end() || b == pos.end()) continue;
    int x1 = a->second.first + kNodeW;
    int y1 = a->second.second + kNodeH / 2;
    int x2 = b->second.first;
    int y2 = b->second.second + kNodeH / 2;
    o << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2
      << "\" y2=\"" << y2 << "\"/>\n";
  }
  for (const auto &n : nodes_) {
    auto p = pos.find(n.id);
    if (p == pos.end()) continue;
    const char *cls = n.is_root ? "root"
                                : (conflict_names.count(n.name) ? "conflict"
                                                               : "");
    o << "<rect class=\"" << cls << "\" x=\"" << p->second.first
      << "\" y=\"" << p->second.second << "\" width=\"" << kNodeW
      << "\" height=\"" << kNodeH << "\" rx=\"4\"/>\n";
    o << "<text x=\"" << (p->second.first + 8) << "\" y=\""
      << (p->second.second + 20) << "\">" << n.name;
    if (!n.version.empty()) o << " " << n.version;
    o << "</text>\n";
  }
  o << "</svg>\n";
  return o.str();
}

}  // namespace polyglot::tools::ui::packages
