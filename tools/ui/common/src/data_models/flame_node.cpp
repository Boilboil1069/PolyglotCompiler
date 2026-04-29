/**
 * @file     flame_node.cpp
 * @brief    FlameNode + FlameTreeModel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/data_models/flame_node.h"

#include <QHash>

namespace polyglot::tools::ui {

// ============================================================================
// FlameNode helpers
// ============================================================================

QString FlameNode::InclusiveText() const {
  const double ms = static_cast<double>(inclusive_ns) / 1.0e6;
  return QString::number(ms, 'f', 3) + " ms";
}

double FlameNode::AvgUs() const {
  if (calls == 0) {
    return 0.0;
  }
  return static_cast<double>(inclusive_ns) / 1.0e3 / static_cast<double>(calls);
}

// ============================================================================
// FlameTreeModel
// ============================================================================

FlameTreeModel::FlameTreeModel(QObject *parent) : QAbstractItemModel(parent) {}
FlameTreeModel::~FlameTreeModel() = default;

void FlameTreeModel::SetRoot(std::unique_ptr<FlameNode> root) {
  beginResetModel();
  root_ = std::move(root);
  endResetModel();
}

const FlameNode *FlameTreeModel::NodeAt(const QModelIndex &index) const {
  if (!index.isValid()) {
    return root_.get();
  }
  return static_cast<const FlameNode *>(index.internalPointer());
}

QModelIndex FlameTreeModel::index(int row, int column, const QModelIndex &parent) const {
  if (!root_ || row < 0 || column < 0 || column >= kColumnCount) {
    return {};
  }
  const FlameNode *parent_node = parent.isValid()
                                      ? static_cast<const FlameNode *>(parent.internalPointer())
                                      : root_.get();
  if (!parent_node || row >= static_cast<int>(parent_node->children.size())) {
    return {};
  }
  return createIndex(row, column,
                     const_cast<FlameNode *>(parent_node->children[row].get()));
}

QModelIndex FlameTreeModel::parent(const QModelIndex &index) const {
  if (!index.isValid() || !root_) {
    return {};
  }
  const auto *node = static_cast<const FlameNode *>(index.internalPointer());
  if (!node || !node->parent || node->parent == root_.get()) {
    return {};
  }
  const FlameNode *grand = node->parent->parent;
  if (!grand) {
    return {};
  }
  for (std::size_t i = 0; i < grand->children.size(); ++i) {
    if (grand->children[i].get() == node->parent) {
      return createIndex(static_cast<int>(i), 0, const_cast<FlameNode *>(node->parent));
    }
  }
  return {};
}

int FlameTreeModel::rowCount(const QModelIndex &parent) const {
  if (!root_) {
    return 0;
  }
  const FlameNode *node = parent.isValid()
                              ? static_cast<const FlameNode *>(parent.internalPointer())
                              : root_.get();
  return node ? static_cast<int>(node->children.size()) : 0;
}

int FlameTreeModel::columnCount(const QModelIndex & /*parent*/) const { return kColumnCount; }

QVariant FlameTreeModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  const auto *node = static_cast<const FlameNode *>(index.internalPointer());
  if (!node) {
    return {};
  }
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case kColumnFunction:
      return node->function;
    case kColumnLanguage:
      return node->language;
    case kColumnInclusive:
      return node->InclusiveText();
    case kColumnSelf:
      return QString::number(static_cast<double>(node->self_ns) / 1.0e6, 'f', 3) + " ms";
    case kColumnCalls:
      return QString::number(static_cast<qulonglong>(node->calls));
    case kColumnAvg:
      return QString::number(node->AvgUs(), 'f', 2) + " us";
    default:
      break;
    }
  } else if (role == Qt::DecorationRole && index.column() == kColumnLanguage) {
    return LanguageColor(node->language);
  }
  return {};
}

QVariant FlameTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
  case kColumnFunction:
    return tr("Function");
  case kColumnLanguage:
    return tr("Language");
  case kColumnInclusive:
    return tr("Inclusive");
  case kColumnSelf:
    return tr("Self");
  case kColumnCalls:
    return tr("Calls");
  case kColumnAvg:
    return tr("Avg");
  default:
    return {};
  }
}

QColor FlameTreeModel::LanguageColor(const QString &language) {
  // Stable, theme-agnostic palette.  Picked to remain readable on both
  // light and dark backgrounds.
  static const QHash<QString, QColor> kPalette = {
      {QStringLiteral("cpp"), QColor(0x65, 0x9A, 0xD2)},
      {QStringLiteral("c"), QColor(0x55, 0x88, 0xA3)},
      {QStringLiteral("python"), QColor(0xF1, 0xC4, 0x0F)},
      {QStringLiteral("rust"), QColor(0xCE, 0x42, 0x2B)},
      {QStringLiteral("java"), QColor(0xE7, 0x6F, 0x00)},
      {QStringLiteral("csharp"), QColor(0x68, 0x21, 0x7A)},
      {QStringLiteral("javascript"), QColor(0xF0, 0xDB, 0x4F)},
      {QStringLiteral("go"), QColor(0x00, 0xAD, 0xD8)},
      {QStringLiteral("ruby"), QColor(0xCC, 0x34, 0x2D)},
      {QStringLiteral("ploy"), QColor(0x57, 0xB0, 0x7B)},
      {QStringLiteral("bridge"), QColor(0x9E, 0x9E, 0x9E)},
  };
  auto it = kPalette.find(language);
  if (it != kPalette.end()) {
    return it.value();
  }
  return QColor(0x80, 0x80, 0x80);
}

} // namespace polyglot::tools::ui
