// topology_graph.cpp — TopologyGraph implementation.

#include "tools/polytopo/include/topology_graph.h"

#include <algorithm>
#include <queue>
#include <stack>
#include <unordered_set>

namespace polyglot::tools::topo {

// ============================================================================
// Node management
// ============================================================================

uint64_t TopologyGraph::AddNode(TopologyNode node) {
    node.id = next_node_id_++;

    // Assign port ids
    for (auto &port : node.inputs) {
        port.id = AllocPortId();
    }
    for (auto &port : node.outputs) {
        port.id = AllocPortId();
    }

    node_index_[node.id] = nodes_.size();
    if (!node.name.empty()) {
        name_index_[node.name] = node.id;
    }
    nodes_.push_back(std::move(node));
    return nodes_.back().id;
}

const TopologyNode *TopologyGraph::GetNode(uint64_t id) const {
    auto it = node_index_.find(id);
    if (it == node_index_.end()) return nullptr;
    return &nodes_[it->second];
}

TopologyNode *TopologyGraph::GetMutableNode(uint64_t id) {
    auto it = node_index_.find(id);
    if (it == node_index_.end()) return nullptr;
    return &nodes_[it->second];
}

const TopologyNode *TopologyGraph::FindNodeByName(const std::string &name) const {
    auto it = name_index_.find(name);
    if (it == name_index_.end()) return nullptr;
    return GetNode(it->second);
}

// ============================================================================
// Edge management
// ============================================================================

uint64_t TopologyGraph::AddEdge(TopologyEdge edge) {
    edge.id = next_edge_id_++;
    edges_.push_back(std::move(edge));
    return edges_.back().id;
}

std::vector<const TopologyEdge *> TopologyGraph::OutEdges(uint64_t node_id) const {
    std::vector<const TopologyEdge *> result;
    for (const auto &edge : edges_) {
        if (edge.source_node_id == node_id) {
            result.push_back(&edge);
        }
    }
    return result;
}

std::vector<const TopologyEdge *> TopologyGraph::InEdges(uint64_t node_id) const {
    std::vector<const TopologyEdge *> result;
    for (const auto &edge : edges_) {
        if (edge.target_node_id == node_id) {
            result.push_back(&edge);
        }
    }
    return result;
}

// ============================================================================
// Query
// ============================================================================

std::vector<const TopologyNode *> TopologyGraph::Roots() const {
    std::unordered_set<uint64_t> has_incoming;
    for (const auto &edge : edges_) {
        has_incoming.insert(edge.target_node_id);
    }
    std::vector<const TopologyNode *> result;
    for (const auto &node : nodes_) {
        if (has_incoming.find(node.id) == has_incoming.end()) {
            result.push_back(&node);
        }
    }
    return result;
}

std::vector<const TopologyNode *> TopologyGraph::Leaves() const {
    std::unordered_set<uint64_t> has_outgoing;
    for (const auto &edge : edges_) {
        has_outgoing.insert(edge.source_node_id);
    }
    std::vector<const TopologyNode *> result;
    for (const auto &node : nodes_) {
        if (has_outgoing.find(node.id) == has_outgoing.end()) {
            result.push_back(&node);
        }
    }
    return result;
}

std::vector<const TopologyNode *> TopologyGraph::TopologicalSort() const {
    // Build adjacency list and in-degree counts
    std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
    std::unordered_map<uint64_t, int> in_degree;

    for (const auto &node : nodes_) {
        adj[node.id] = {};
        in_degree[node.id] = 0;
    }

    for (const auto &edge : edges_) {
        adj[edge.source_node_id].push_back(edge.target_node_id);
        in_degree[edge.target_node_id]++;
    }

    // Kahn's algorithm
    std::queue<uint64_t> queue;
    for (const auto &[id, deg] : in_degree) {
        if (deg == 0) {
            queue.push(id);
        }
    }

    std::vector<const TopologyNode *> sorted;
    sorted.reserve(nodes_.size());

    while (!queue.empty()) {
        uint64_t current = queue.front();
        queue.pop();

        const auto *node = GetNode(current);
        if (node) {
            sorted.push_back(node);
        }

        for (uint64_t neighbor : adj[current]) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    // If sorted size != node count, there is a cycle -> return empty
    if (sorted.size() != nodes_.size()) {
        return {};
    }
    return sorted;
}

std::vector<uint64_t> TopologyGraph::DetectCycles() const {
    // DFS-based cycle detection
    enum class Color { kWhite, kGray, kBlack };
    std::unordered_map<uint64_t, Color> color;
    std::unordered_map<uint64_t, uint64_t> parent;
    std::unordered_map<uint64_t, std::vector<uint64_t>> adj;

    for (const auto &node : nodes_) {
        color[node.id] = Color::kWhite;
        adj[node.id] = {};
    }
    for (const auto &edge : edges_) {
        adj[edge.source_node_id].push_back(edge.target_node_id);
    }

    std::vector<uint64_t> cycle;
    bool found = false;

    // Iterative DFS to avoid stack overflow on large graphs
    for (const auto &node : nodes_) {
        if (found) break;
        if (color[node.id] != Color::kWhite) continue;

        std::stack<std::pair<uint64_t, size_t>> stack;
        stack.push({node.id, 0});
        color[node.id] = Color::kGray;

        while (!stack.empty() && !found) {
            auto &[current, idx] = stack.top();
            if (idx < adj[current].size()) {
                uint64_t next = adj[current][idx];
                idx++;

                if (color[next] == Color::kGray) {
                    // Found a cycle — reconstruct
                    cycle.push_back(next);
                    uint64_t trace = current;
                    while (trace != next) {
                        cycle.push_back(trace);
                        // Find parent on stack
                        bool found_parent = false;
                        for (auto sit = stack.size(); sit > 0; --sit) {
                            // Walk up the DFS stack to trace back
                        }
                        if (parent.count(trace)) {
                            trace = parent[trace];
                        } else {
                            break;
                        }
                    }
                    cycle.push_back(next);
                    std::reverse(cycle.begin(), cycle.end());
                    found = true;
                } else if (color[next] == Color::kWhite) {
                    color[next] = Color::kGray;
                    parent[next] = current;
                    stack.push({next, 0});
                }
            } else {
                color[current] = Color::kBlack;
                stack.pop();
            }
        }
    }

    return cycle;
}

// ============================================================================
// Statistics
// ============================================================================

std::unordered_map<std::string, size_t> TopologyGraph::LanguageDistribution() const {
    std::unordered_map<std::string, size_t> dist;
    for (const auto &node : nodes_) {
        if (!node.language.empty()) {
            dist[node.language]++;
        }
    }
    return dist;
}

std::unordered_map<TopologyEdge::Status, size_t> TopologyGraph::EdgeStatusDistribution() const {
    std::unordered_map<TopologyEdge::Status, size_t> dist;
    for (const auto &edge : edges_) {
        dist[edge.status]++;
    }
    return dist;
}

} // namespace polyglot::tools::topo
