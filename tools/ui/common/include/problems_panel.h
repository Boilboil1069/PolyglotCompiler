/**
 * @file     problems_panel.h
 * @brief    Diagnostics dock panel ("Problems") for the PolyglotCompiler IDE
 *
 * Companion widget to @ref ProblemsAggregator: every diagnostic that the
 * IDE currently knows about — from the LSP client, the in-process
 * @ref CompilerService, or the polyc `--check` fallback — is funnelled
 * into the aggregator and rendered here, grouped by file with severity /
 * file / source / regex filters along the top.  Double-clicking a row
 * emits @ref OpenFileRequested so the host MainWindow can navigate to
 * the corresponding line and column.
 *
 * Implements demand 2026-04-28-20 §1.
 *
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "tools/ui/common/include/problems_aggregator.h"

namespace polyglot::tools::ui {

/// Dockable Problems panel.  Lifetime of the @ref ProblemsAggregator is
/// owned by the host (MainWindow) — the panel only borrows a pointer.
class ProblemsPanel : public QWidget {
  Q_OBJECT

 public:
  explicit ProblemsPanel(ProblemsAggregator *aggregator, QWidget *parent = nullptr);
  ~ProblemsPanel() override;

  /// Force a refresh of the tree from the aggregator snapshot.  Called
  /// automatically on the aggregator change callback (marshalled to the
  /// GUI thread).
  void Refresh();

 signals:
  /// Emitted when the user activates a row.  Coordinates are 1-based.
  void OpenFileRequested(const QString &file, int line, int column);

  /// Emitted after every aggregator-driven refresh so external observers
  /// (e.g. the status-bar problems counter) can stay in sync without
  /// each subscribing to the aggregator directly.
  void AggregatorChanged();

 private slots:
  void OnSeverityToggled();
  void OnFileFilterChanged(const QString &text);
  void OnSourceFilterChanged(int index);
  void OnRegexFilterChanged(const QString &text);
  void OnClearFiltersClicked();
  void OnItemActivated(QTreeWidgetItem *item, int column);

 private:
  void BuildUi();
  void RebuildTree();
  void RebuildSourceComboKeepingSelection();
  ProblemFilter CurrentFilter() const;

  ProblemsAggregator *aggregator_{nullptr};

  // Filter row
  QToolButton *btn_errors_{nullptr};
  QToolButton *btn_warnings_{nullptr};
  QToolButton *btn_info_{nullptr};
  QToolButton *btn_hints_{nullptr};
  QLineEdit *file_edit_{nullptr};
  QComboBox *source_combo_{nullptr};
  QLineEdit *regex_edit_{nullptr};
  QToolButton *btn_clear_{nullptr};
  QLabel *summary_label_{nullptr};

  // Tree
  QTreeWidget *tree_{nullptr};
};

}  // namespace polyglot::tools::ui
