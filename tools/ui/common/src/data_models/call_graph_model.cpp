/**
 * @file     call_graph_model.cpp
 * @brief    CallGraphModel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/data_models/call_graph_model.h"

#include <QSet>
#include <algorithm>
#include <utility>

namespace polyglot::tools::ui {

CallGraphModel::CallGraphModel(QObject *parent) : QAbstractItemModel(parent) {}
CallGraphModel::~CallGraphModel() = default;

void CallGraphModel::Replace(std::vector<CallGraphNode> nodes, std::vector<CallGraphEdge> edges) {
  beginResetModel();
  nodes_ = std::move(nodes);
  edges_ = std::move(edges);
  id_to_row_.clear();
  id_to_row_.reserve(static_cast<int>(nodes_.size()));
  for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
    id_to_row_.insert(nodes_[i].id, i);
  }
  RebuildAdjacency();
  endResetModel();
}

void CallGraphModel::Clear() {
  beginResetModel();
  nodes_.clear();
  edges_.clear();
  id_to_row_.clear();
  adjacency_.clear();
  reverse_.clear();
  endResetModel();
}

int CallGraphModel::RowForId(const QString &id) const {
  auto it = id_to_row_.find(id);
  return it == id_to_row_.end() ? -1 : it.value();
}

void CallGraphModel::ApplyRuntimeCounts(const QHash<QString, std::uint64_t> &call_counts,
                                        const QHash<QString, std::uint64_t> &inclusive_ns) {
  if (nodes_.empty()) {
    return;
  }
  int first_changed = -1;
  int last_changed = -1;
  for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
    auto &node = nodes_[i];
    bool dirty = false;
    if (auto it = call_counts.find(node.id); it != call_counts.end()) {
      node.calls = it.value();
      dirty = true;
    } else if (auto it_name = call_counts.find(node.name); it_name != call_counts.end()) {
      node.calls = it_name.value();
      dirty = true;
    }
    if (auto it = inclusive_ns.find(node.id); it != inclusive_ns.end()) {
      node.inclusive_ns = it.value();
      dirty = true;
    } else if (auto it_name = inclusive_ns.find(node.name); it_name != inclusive_ns.end()) {
      node.inclusive_ns = it_name.value();
      dirty = true;
    }
    if (dirty) {
      if (first_changed < 0) {
        first_changed = i;
      }
      last_changed = i;
    }
  }
  if (first_changed >= 0) {
    emit dataChanged(index(first_changed, 0, {}),
                     index(last_changed, kColumnCount - 1, {}));
  }
  // Edges: aggregate by from->to pair when call_counts contains the joined id.
  for (auto &edge : edges_) {
    const QString joined = edge.from + QStringLiteral("->") + edge.to;
    if (auto it = call_counts.find(joined); it != call_counts.end()) {
      edge.calls = it.value();
    }
  }
}

QStringList CallGraphModel::DirectCallers(const QString &id) const {
  auto it = reverse_.find(id);
  return it == reverse_.end() ? QStringList{} : it.value();
}

QStringList CallGraphModel::DirectCallees(const QString &id) const {
  auto it = adjacency_.find(id);
  return it == adjacency_.end() ? QStringList{} : it.value();
}

std::vector<QStringList> CallGraphModel::FindPaths(const QString &src, const QString &dst,
                                                   int max_depth) const {
  std::vector<QStringList> results;
  if (src.isEmpty() || dst.isEmpty() || max_depth <= 0) {
    return results;
  }
  if (!id_to_row_.contains(src) || !id_to_row_.contains(dst)) {
    return results;
  }
  QStringList stack;
  QSet<QString> on_stack;
  stack.append(src);
  on_stack.insert(src);

  // Iterative DFS bookkeeping: per-frame next-child cursor.
  std::vector<int> cursor;
  cursor.push_back(0);

  while (!stack.isEmpty()) {
    const QString &top = stack.last();
    if (top == dst && stack.size() > 1) {
      results.push_back(stack);
      // Pop and continue searching for siblings.
      on_stack.remove(stack.takeLast());
      cursor.pop_back();
      if (!cursor.empty()) {
        ++cursor.back();
      }
      continue;
    }
    if (stack.size() >= max_depth) {
      on_stack.remove(stack.takeLast());
      cursor.pop_back();
      if (!cursor.empty()) {
        ++cursor.back();
      }
      continue;
    }
    const QStringList &children = adjacency_.value(top);
    int &next = cursor.back();
    bool descended = false;
    while (next < children.size()) {
      const QString &child = children[next];
      if (!on_stack.contains(child)) {
        stack.append(child);
        on_stack.insert(child);
        cursor.push_back(0);
        descended = true;
        break;
      }
      ++next;
    }
    if (!descended) {
      on_stack.remove(stack.takeLast());
      cursor.pop_back();
      if (!cursor.empty()) {
        ++cursor.back();
      }
    }
  }
  return results;
}

QModelIndex CallGraphModel::index(int row, int column, const QModelIndex &parent) const {
  if (parent.isValid() || row < 0 || row >= static_cast<int>(nodes_.size()) || column < 0 ||
      column >= kColumnCount) {
    return {};
  }
  return createIndex(row, column);
}

QModelIndex CallGraphModel::parent(const QModelIndex & /*index*/) const { return {}; }

int CallGraphModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(nodes_.size());
}

int CallGraphModel::columnCount(const QModelIndex & /*parent*/) const { return kColumnCount; }

QVariant CallGraphModel::data(const QModelIndex &idx, int role) const {
  if (!idx.isValid() || idx.row() < 0 || idx.row() >= static_cast<int>(nodes_.size())) {
    return {};
  }
  const auto &node = nodes_[idx.row()];
  if (role == Qt::DisplayRole) {
    switch (idx.column()) {
    case kColumnId:
      return node.id;
    case kColumnName:
      return node.name;
    case kColumnLanguage:
      return node.language;
    case kColumnFile:
      return node.file;
    case kColumnLine:
      return node.line > 0 ? QString::number(node.line) : QString();
    case kColumnCalls:
      return QString::number(static_cast<qulonglong>(node.calls));
    case kColumnInclusive:
      return QString::number(static_cast<double>(node.inclusive_ns) / 1.0e6, 'f', 3) + " ms";
    default:
      return {};
    }
  }
  return {};
}

QVariant CallGraphModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
  case kColumnId:
    return tr("Id");
  case kColumnName:
    return tr("Function");
  case kColumnLanguage:
    return tr("Language");
  case kColumnFile:
    return tr("File");
  case kColumnLine:
    return tr("Line");
  case kColumnCalls:
    return tr("Calls");
  case kColumnInclusive:
    return tr("Inclusive");
  default:
    return {};
  }
}

void CallGraphModel::RebuildAdjacency() {
  adjacency_.clear();
  reverse_.clear();
  for (const auto &edge : edges_) {
    adjacency_[edge.from].append(edge.to);
    reverse_[edge.to].append(edge.from);
  }
  // Sort outgoing/incoming lists for deterministic enumeration.
  for (auto it = adjacency_.begin(); it != adjacency_.end(); ++it) {
    std::sort(it.value().begin(), it.value().end());
  }
  for (auto it = reverse_.begin(); it != reverse_.end(); ++it) {
    std::sort(it.value().begin(), it.value().end());
  }
}

} // namespace polyglot::tools::ui
