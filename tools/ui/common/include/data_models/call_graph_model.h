/**
 * @file     call_graph_model.h
 * @brief    Call-graph data model shared by Profiler / Call Analyzer panels
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <cstdint>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// Call-graph value types
// ============================================================================

/** @brief A single function node in the static / dynamic call graph. */
struct CallGraphNode {
  QString id;        // stable unique id (matches edge endpoints)
  QString name;      // display name
  QString language;  // host language id
  QString file;      // source file path (may be empty)
  int line{0};       // 1-based source line (0 when unknown)
  bool is_external{false};
  bool is_bridge_stub{false};
  int block_count{0};
  std::uint64_t calls{0};       // populated from runtime overlay
  std::uint64_t inclusive_ns{0};
};

/** @brief A directed call edge between two CallGraphNode entries. */
struct CallGraphEdge {
  QString from;
  QString to;
  QString from_language;
  QString to_language;
  std::uint64_t calls{0};
};

// ============================================================================
// CallGraphModel — flat node list with index lookup + edge container
// ============================================================================

/**
 * @brief Item model exposing every call-graph node as a row.
 *
 * Columns: id / name / language / file / line / calls / inclusive(ms).
 * Row order matches insertion order so the painter can use indices as
 * stable handles.
 */
class CallGraphModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Column {
    kColumnId = 0,
    kColumnName,
    kColumnLanguage,
    kColumnFile,
    kColumnLine,
    kColumnCalls,
    kColumnInclusive,
    kColumnCount,
  };

  explicit CallGraphModel(QObject *parent = nullptr);
  ~CallGraphModel() override;

  /// Replace nodes and edges atomically.
  void Replace(std::vector<CallGraphNode> nodes, std::vector<CallGraphEdge> edges);
  void Clear();

  const std::vector<CallGraphNode> &Nodes() const { return nodes_; }
  const std::vector<CallGraphEdge> &Edges() const { return edges_; }

  /// Map a stable node id to its row index, or -1 when absent.
  int RowForId(const QString &id) const;

  /// Apply runtime call counts onto matching nodes / edges.  Unknown ids
  /// are silently skipped; matched rows emit dataChanged().
  void ApplyRuntimeCounts(const QHash<QString, std::uint64_t> &call_counts,
                          const QHash<QString, std::uint64_t> &inclusive_ns);

  // Path queries ----------------------------------------------------------

  /// Direct callers of @p id (edges where @c to == id).
  QStringList DirectCallers(const QString &id) const;
  /// Direct callees of @p id (edges where @c from == id).
  QStringList DirectCallees(const QString &id) const;

  /// Enumerate every static path from @p src to @p dst (bounded DFS).
  /// Each result is the full id sequence, inclusive of endpoints.
  std::vector<QStringList> FindPaths(const QString &src, const QString &dst,
                                     int max_depth) const;

  // QAbstractItemModel overrides
  QModelIndex index(int row, int column, const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  void RebuildAdjacency();

  std::vector<CallGraphNode> nodes_;
  std::vector<CallGraphEdge> edges_;
  QHash<QString, int> id_to_row_;
  QHash<QString, QStringList> adjacency_; // id -> outgoing target ids
  QHash<QString, QStringList> reverse_;   // id -> incoming source ids
};

} // namespace polyglot::tools::ui
