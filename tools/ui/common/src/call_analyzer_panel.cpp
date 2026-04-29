/**
 * @file     call_analyzer_panel.cpp
 * @brief    CallAnalyzerPanel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/call_analyzer_panel.h"

#include <QBrush>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPen>
#include <QSet>
#include <QTreeWidgetItem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tools/ui/common/include/data_models/call_graph_model.h"
#include "tools/ui/common/include/data_models/flame_node.h"
#include "tools/ui/common/include/profile_session.h"

namespace polyglot::tools::ui {

CallAnalyzerPanel::CallAnalyzerPanel(QWidget *parent) : QWidget(parent) {
  session_ = new ProfileSession(this);
  owns_session_ = true;
  BuildUi();
  WireConnections();
}

CallAnalyzerPanel::~CallAnalyzerPanel() = default;

void CallAnalyzerPanel::SetSession(ProfileSession *session) {
  if (session_ == session) {
    return;
  }
  if (owns_session_ && session_) {
    session_->deleteLater();
  }
  session_ = session;
  owns_session_ = false;
  if (session_) {
    nodes_view_->setModel(session_->CallGraph());
    WireConnections();
  }
}

void CallAnalyzerPanel::BuildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  // ── Toolbar ────────────────────────────────────────────────────────────
  toolbar_ = new QToolBar(this);
  load_btn_ = new QPushButton(tr("Load .cgjson…"), this);
  emit_btn_ = new QPushButton(tr("Emit for…"), this);
  current_source_edit_ = new QLineEdit(this);
  current_source_edit_->setPlaceholderText(tr("Source file (optional, used by Emit)"));
  summary_label_ = new QLabel(tr("No graph loaded."), this);
  summary_label_->setStyleSheet(QStringLiteral("color: gray;"));
  toolbar_->addWidget(load_btn_);
  toolbar_->addWidget(emit_btn_);
  toolbar_->addWidget(current_source_edit_);
  toolbar_->addSeparator();
  toolbar_->addWidget(summary_label_);
  layout->addWidget(toolbar_);

  // ── Splitter ───────────────────────────────────────────────────────────
  splitter_ = new QSplitter(Qt::Horizontal, this);

  // Left: callers + callees trees stacked vertically.
  auto *left = new QWidget(this);
  auto *left_layout = new QVBoxLayout(left);
  left_layout->setContentsMargins(2, 2, 2, 2);
  callers_tree_ = new QTreeWidget(left);
  callers_tree_->setHeaderLabels({tr("Callers")});
  callees_tree_ = new QTreeWidget(left);
  callees_tree_->setHeaderLabels({tr("Callees")});
  left_layout->addWidget(callers_tree_);
  left_layout->addWidget(callees_tree_);
  splitter_->addWidget(left);

  // Centre: graph view + node table.
  auto *centre = new QSplitter(Qt::Vertical, this);
  graph_scene_ = new QGraphicsScene(this);
  graph_view_ = new QGraphicsView(graph_scene_, centre);
  graph_view_->setRenderHint(QPainter::Antialiasing, true);
  graph_view_->setDragMode(QGraphicsView::ScrollHandDrag);
  centre->addWidget(graph_view_);

  nodes_view_ = new QTableView(centre);
  nodes_view_->setModel(session_->CallGraph());
  nodes_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  nodes_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  nodes_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  nodes_view_->horizontalHeader()->setStretchLastSection(true);
  centre->addWidget(nodes_view_);
  centre->setStretchFactor(0, 3);
  centre->setStretchFactor(1, 2);
  splitter_->addWidget(centre);

  // Right: language filter + path search.
  auto *right = new QWidget(this);
  auto *right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(2, 2, 2, 2);

  auto *filter_group = new QGroupBox(tr("Language pairs (uncheck to hide edges)"), right);
  auto *filter_layout = new QVBoxLayout(filter_group);
  language_filter_list_ = new QListWidget(filter_group);
  filter_layout->addWidget(language_filter_list_);
  right_layout->addWidget(filter_group);

  auto *path_group = new QGroupBox(tr("Path search"), right);
  auto *path_layout = new QFormLayout(path_group);
  path_src_edit_ = new QLineEdit(path_group);
  path_dst_edit_ = new QLineEdit(path_group);
  path_depth_spin_ = new QSpinBox(path_group);
  path_depth_spin_->setRange(1, 32);
  path_depth_spin_->setValue(8);
  find_paths_btn_ = new QPushButton(tr("Find"), path_group);
  path_layout->addRow(tr("From:"), path_src_edit_);
  path_layout->addRow(tr("To:"), path_dst_edit_);
  path_layout->addRow(tr("Max depth:"), path_depth_spin_);
  path_layout->addRow(find_paths_btn_);
  path_results_list_ = new QListWidget(path_group);
  path_layout->addRow(path_results_list_);
  right_layout->addWidget(path_group);

  splitter_->addWidget(right);
  splitter_->setStretchFactor(0, 1);
  splitter_->setStretchFactor(1, 3);
  splitter_->setStretchFactor(2, 1);

  layout->addWidget(splitter_, /*stretch=*/1);
}

void CallAnalyzerPanel::WireConnections() {
  connect(load_btn_, &QPushButton::clicked, this, &CallAnalyzerPanel::OnLoadCgJsonClicked);
  connect(emit_btn_, &QPushButton::clicked, this,
          &CallAnalyzerPanel::OnEmitForCurrentFileClicked);
  connect(find_paths_btn_, &QPushButton::clicked, this,
          &CallAnalyzerPanel::OnFindPathsClicked);
  connect(language_filter_list_, &QListWidget::itemChanged, this,
          [this](QListWidgetItem *) { OnLanguageFilterChanged(); });
  connect(nodes_view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
          [this](const QModelIndex &, const QModelIndex &) { OnNodeSelectionChanged(); });
  connect(nodes_view_, &QAbstractItemView::doubleClicked, this,
          &CallAnalyzerPanel::OnNodeDoubleClicked);
  if (session_) {
    connect(session_, &ProfileSession::CallGraphLoaded, this,
            &CallAnalyzerPanel::OnCallGraphLoaded);
  }
}

void CallAnalyzerPanel::OnLoadCgJsonClicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Load call-graph JSON"), {}, tr("Call-graph JSON (*.cgjson *.json)"));
  if (path.isEmpty() || !session_) {
    return;
  }
  if (!session_->LoadCallGraphJson(path)) {
    summary_label_->setText(tr("Failed to load %1").arg(path));
    summary_label_->setStyleSheet(QStringLiteral("color: #c93838;"));
  }
}

void CallAnalyzerPanel::OnEmitForCurrentFileClicked() {
  if (!session_) {
    return;
  }
  const QString src = current_source_edit_->text().trimmed();
  session_->EmitAndLoadCallGraph(src);
  summary_label_->setText(tr("Emitting call-graph…"));
}

void CallAnalyzerPanel::OnCallGraphLoaded(int node_count, int edge_count) {
  summary_label_->setText(
      tr("%1 nodes, %2 edges").arg(node_count).arg(edge_count));
  summary_label_->setStyleSheet(QStringLiteral("color: #2e8b57;"));
  RefreshLanguageFilterList();
  RepaintGraph();
  emit StatusMessage(summary_label_->text());
}

void CallAnalyzerPanel::OnNodeSelectionChanged() {
  if (!session_) {
    return;
  }
  const QModelIndex idx = nodes_view_->currentIndex();
  if (!idx.isValid()) {
    return;
  }
  const auto *graph = session_->CallGraph();
  if (idx.row() < 0 || idx.row() >= static_cast<int>(graph->Nodes().size())) {
    return;
  }
  RefreshCallerCalleeTrees(graph->Nodes()[idx.row()].id);
}

void CallAnalyzerPanel::OnNodeDoubleClicked(const QModelIndex &index) {
  if (!index.isValid() || !session_) {
    return;
  }
  const auto *graph = session_->CallGraph();
  if (index.row() < 0 || index.row() >= static_cast<int>(graph->Nodes().size())) {
    return;
  }
  const auto &node = graph->Nodes()[index.row()];
  if (!node.file.isEmpty()) {
    emit OpenFileRequested(node.file, node.line > 0 ? node.line : 1);
  }
}

void CallAnalyzerPanel::OnFindPathsClicked() {
  if (!session_) {
    return;
  }
  const QString src = path_src_edit_->text().trimmed();
  const QString dst = path_dst_edit_->text().trimmed();
  const int depth = path_depth_spin_->value();
  path_results_list_->clear();
  const auto results = session_->CallGraph()->FindPaths(src, dst, depth);
  if (results.empty()) {
    path_results_list_->addItem(tr("(no path within depth %1)").arg(depth));
    return;
  }
  for (const auto &path : results) {
    path_results_list_->addItem(path.join(QStringLiteral(" -> ")));
  }
}

void CallAnalyzerPanel::OnLanguageFilterChanged() { RepaintGraph(); }

bool CallAnalyzerPanel::EdgePassesLanguageFilter(const QString &from_lang,
                                                 const QString &to_lang) const {
  // When the list is empty (e.g. before first paint) accept everything.
  if (language_filter_list_->count() == 0) {
    return true;
  }
  const QString key = from_lang + QStringLiteral("->") + to_lang;
  for (int i = 0; i < language_filter_list_->count(); ++i) {
    QListWidgetItem *item = language_filter_list_->item(i);
    if (item->text() == key) {
      return item->checkState() == Qt::Checked;
    }
  }
  return true;
}

void CallAnalyzerPanel::RefreshLanguageFilterList() {
  if (!session_) {
    return;
  }
  language_filter_list_->blockSignals(true);
  QSet<QString> seen;
  for (const auto &edge : session_->CallGraph()->Edges()) {
    seen.insert(edge.from_language + QStringLiteral("->") + edge.to_language);
  }
  // Preserve previously checked state where possible.
  std::unordered_map<std::string, bool> previous;
  for (int i = 0; i < language_filter_list_->count(); ++i) {
    QListWidgetItem *item = language_filter_list_->item(i);
    previous[item->text().toStdString()] = (item->checkState() == Qt::Checked);
  }
  language_filter_list_->clear();
  QStringList sorted_keys(seen.begin(), seen.end());
  std::sort(sorted_keys.begin(), sorted_keys.end());
  for (const QString &key : sorted_keys) {
    auto *item = new QListWidgetItem(key, language_filter_list_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    auto it = previous.find(key.toStdString());
    item->setCheckState(it == previous.end()
                            ? Qt::Checked
                            : (it->second ? Qt::Checked : Qt::Unchecked));
  }
  language_filter_list_->blockSignals(false);
}

void CallAnalyzerPanel::RefreshCallerCalleeTrees(const QString &node_id) {
  callers_tree_->clear();
  callees_tree_->clear();
  if (!session_) {
    return;
  }
  const auto *graph = session_->CallGraph();
  for (const QString &caller : graph->DirectCallers(node_id)) {
    auto *item = new QTreeWidgetItem(callers_tree_);
    item->setText(0, caller);
  }
  for (const QString &callee : graph->DirectCallees(node_id)) {
    auto *item = new QTreeWidgetItem(callees_tree_);
    item->setText(0, callee);
  }
}

void CallAnalyzerPanel::RepaintGraph() {
  graph_scene_->clear();
  if (!session_) {
    return;
  }
  const auto *graph = session_->CallGraph();
  const auto &nodes = graph->Nodes();
  if (nodes.empty()) {
    return;
  }

  // Simple layered layout: longest-path layering by BFS distance from any
  // node with no incoming edges.  Nodes inside the same layer are placed
  // along a horizontal row; layers are stacked vertically.
  std::unordered_map<std::string, int> indeg;
  for (const auto &n : nodes) {
    indeg[n.id.toStdString()] = 0;
  }
  for (const auto &edge : graph->Edges()) {
    if (!EdgePassesLanguageFilter(edge.from_language, edge.to_language)) {
      continue;
    }
    auto it = indeg.find(edge.to.toStdString());
    if (it != indeg.end()) {
      ++it->second;
    }
  }
  std::unordered_map<std::string, int> layer;
  std::vector<std::string> queue;
  for (const auto &n : nodes) {
    if (indeg[n.id.toStdString()] == 0) {
      layer[n.id.toStdString()] = 0;
      queue.push_back(n.id.toStdString());
    }
  }
  std::size_t qi = 0;
  while (qi < queue.size()) {
    const std::string current = queue[qi++];
    const QString cur_q = QString::fromStdString(current);
    const int cur_layer = layer[current];
    for (const auto &edge : graph->Edges()) {
      if (edge.from != cur_q) {
        continue;
      }
      if (!EdgePassesLanguageFilter(edge.from_language, edge.to_language)) {
        continue;
      }
      const std::string to = edge.to.toStdString();
      auto it = layer.find(to);
      const int candidate = cur_layer + 1;
      if (it == layer.end() || candidate > it->second) {
        layer[to] = candidate;
        queue.push_back(to);
      }
    }
  }

  // Bucket per-layer for placement.
  std::unordered_map<int, std::vector<int>> by_layer; // layer -> node row indices
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    int l = 0;
    auto it = layer.find(nodes[i].id.toStdString());
    if (it != layer.end()) {
      l = it->second;
    }
    by_layer[l].push_back(i);
  }

  constexpr qreal kColWidth = 220.0;
  constexpr qreal kRowHeight = 70.0;
  constexpr qreal kNodeWidth = 160.0;
  constexpr qreal kNodeHeight = 40.0;

  std::unordered_map<std::string, QPointF> centres;
  for (const auto &kv : by_layer) {
    const int l = kv.first;
    const auto &rows = kv.second;
    for (std::size_t r = 0; r < rows.size(); ++r) {
      const auto &node = nodes[rows[r]];
      const qreal x = l * kColWidth;
      const qreal y = static_cast<qreal>(r) * kRowHeight;
      auto *rect = graph_scene_->addRect(x, y, kNodeWidth, kNodeHeight,
                                          QPen(Qt::black),
                                          QBrush(FlameTreeModel::LanguageColor(node.language)));
      rect->setToolTip(node.name + QStringLiteral("\n[") + node.language +
                        QStringLiteral("] calls=") +
                        QString::number(static_cast<qulonglong>(node.calls)));
      auto *label = graph_scene_->addText(node.name);
      label->setPos(x + 4, y + 4);
      centres[node.id.toStdString()] = QPointF(x + kNodeWidth / 2, y + kNodeHeight / 2);
    }
  }

  for (const auto &edge : graph->Edges()) {
    if (!EdgePassesLanguageFilter(edge.from_language, edge.to_language)) {
      continue;
    }
    auto from_it = centres.find(edge.from.toStdString());
    auto to_it = centres.find(edge.to.toStdString());
    if (from_it == centres.end() || to_it == centres.end()) {
      continue;
    }
    QPen pen(edge.from_language == edge.to_language ? Qt::darkGray : Qt::darkRed);
    pen.setWidthF(edge.from_language == edge.to_language ? 1.0 : 1.6);
    graph_scene_->addLine(QLineF(from_it->second, to_it->second), pen);
  }

  graph_scene_->setSceneRect(graph_scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
}

} // namespace polyglot::tools::ui
