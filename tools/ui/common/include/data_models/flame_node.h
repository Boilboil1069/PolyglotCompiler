/**
 * @file     flame_node.h
 * @brief    Flame-graph tree model shared by Profiler / Call Analyzer panels
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QAbstractItemModel>
#include <QColor>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// FlameNode — a single sample-aggregated node in the inclusive-time tree
// ============================================================================

/** @brief One aggregated frame in the flame graph. */
struct FlameNode {
  QString function;          // fully qualified function name
  QString language;          // host language id (cpp/python/rust/...)
  std::uint64_t inclusive_ns{0};
  std::uint64_t self_ns{0};
  std::uint64_t calls{0};
  std::vector<std::unique_ptr<FlameNode>> children;
  FlameNode *parent{nullptr};

  /// Human-readable inclusive time (ms with 3 fractional digits).
  QString InclusiveText() const;

  /// Average per-call inclusive time in microseconds (0 when calls == 0).
  double AvgUs() const;
};

// ============================================================================
// FlameTreeModel — Qt item model exposing a FlameNode tree
// ============================================================================

/** @brief Read-only tree model for the flame graph view. */
class FlameTreeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Column {
    kColumnFunction = 0,
    kColumnLanguage,
    kColumnInclusive,
    kColumnSelf,
    kColumnCalls,
    kColumnAvg,
    kColumnCount,
  };

  explicit FlameTreeModel(QObject *parent = nullptr);
  ~FlameTreeModel() override;

  /// Replace the entire tree.  Takes ownership of @p root.
  void SetRoot(std::unique_ptr<FlameNode> root);
  const FlameNode *Root() const { return root_.get(); }

  /// Convenience accessor used by the flame painter.
  const FlameNode *NodeAt(const QModelIndex &index) const;

  // QAbstractItemModel overrides
  QModelIndex index(int row, int column, const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  /// Stable colour for a host language (used by both tree and painter).
  static QColor LanguageColor(const QString &language);

private:
  std::unique_ptr<FlameNode> root_;
};

} // namespace polyglot::tools::ui
