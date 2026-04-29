/**
 * @file     call_analyzer_panel.h
 * @brief    Call Analyzer dock panel for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace polyglot::tools::ui {

class ProfileSession;
struct CallGraphNode;

// ============================================================================
// CallAnalyzerPanel — Call Analyzer dock panel
// ============================================================================

/**
 * @brief Dockable panel that visualises a static + dynamic call graph.
 *
 * Layout (left → right):
 *   * Top toolbar: Load .cgjson / Compile current file / Filter / Search
 *   * Splitter:
 *     - Left: callers tree + callees tree for the currently selected node.
 *     - Centre: graph view (QGraphicsScene with layered layout) + node table.
 *     - Right: language-pair filter list + path search (src/dst inputs +
 *       results list).
 */
class CallAnalyzerPanel : public QWidget {
  Q_OBJECT

public:
  explicit CallAnalyzerPanel(QWidget *parent = nullptr);
  ~CallAnalyzerPanel() override;

  /// Inject a shared @ref ProfileSession (so the Profiler panel and this
  /// panel can use the same call-graph data).
  void SetSession(ProfileSession *session);
  ProfileSession *Session() const { return session_; }

signals:
  /// Emitted when the user double-clicks a node row that has a source
  /// location.  Wired by MainWindow to open the file in the editor.
  void OpenFileRequested(const QString &file, int line);

  void StatusMessage(const QString &message);

private slots:
  void OnLoadCgJsonClicked();
  void OnEmitForCurrentFileClicked();
  void OnNodeSelectionChanged();
  void OnNodeDoubleClicked(const QModelIndex &index);
  void OnFindPathsClicked();
  void OnLanguageFilterChanged();
  void OnCallGraphLoaded(int node_count, int edge_count);

private:
  void BuildUi();
  void WireConnections();
  void RepaintGraph();
  void RefreshLanguageFilterList();
  void RefreshCallerCalleeTrees(const QString &node_id);
  bool EdgePassesLanguageFilter(const QString &from_lang, const QString &to_lang) const;

  ProfileSession *session_{nullptr};
  bool owns_session_{true};

  // Toolbar
  QToolBar *toolbar_{nullptr};
  QPushButton *load_btn_{nullptr};
  QPushButton *emit_btn_{nullptr};
  QLineEdit *current_source_edit_{nullptr};
  QLabel *summary_label_{nullptr};

  // Splitter children
  QSplitter *splitter_{nullptr};
  QTreeWidget *callers_tree_{nullptr};
  QTreeWidget *callees_tree_{nullptr};
  QGraphicsScene *graph_scene_{nullptr};
  QGraphicsView *graph_view_{nullptr};
  QTableView *nodes_view_{nullptr};
  QListWidget *language_filter_list_{nullptr};
  QLineEdit *path_src_edit_{nullptr};
  QLineEdit *path_dst_edit_{nullptr};
  QSpinBox *path_depth_spin_{nullptr};
  QPushButton *find_paths_btn_{nullptr};
  QListWidget *path_results_list_{nullptr};
};

} // namespace polyglot::tools::ui
