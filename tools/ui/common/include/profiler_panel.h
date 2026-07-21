/**
 * @file     profiler_panel.h
 * @brief    Performance Profiler dock panel for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

namespace polyglot::tools::ui {

class ProfileSession;

// ============================================================================
// ProfilerPanel — Performance Profiler dock panel
// ============================================================================

/**
 * @brief Dockable panel that drives a @ref ProfileSession and visualises
 *        flame graph / hotspots / timeline / language breakdown / GC.
 *
 * The panel owns its @ref ProfileSession by default but external code can
 * inject one via @ref SetSession (used by the IDE so the Call Analyzer
 * panel can share the same call-graph model).
 */
class ProfilerPanel : public QWidget {
  Q_OBJECT

public:
  explicit ProfilerPanel(QWidget *parent = nullptr);
  ~ProfilerPanel() override;

  /// Replace the underlying session.  Takes ownership of @p session.
  void SetSession(ProfileSession *session);
  ProfileSession *Session() const { return session_; }

signals:
  /// Emitted when the user double-clicks a function row that has source
  /// location metadata.  Wired by MainWindow to open the file.
  void OpenFileRequested(const QString &file, int line);

  /// Status messages bubbled up to the main status bar.
  void StatusMessage(const QString &message);

private slots:
  void OnRunBenchmarkClicked();
  void OnRunProfileClicked();
  void OnStartStreamClicked();
  void OnStopStreamClicked();
  void OnHotspotDoubleClicked(const QModelIndex &index);
  void OnFlameDoubleClicked(const QModelIndex &index);
  void OnSessionFinished(bool ok, const QString &message);
  void OnStreamSampleReceived(int total_samples);
  void OnToolErrorOutput(const QString &tool, const QString &line);

private:
  void BuildUi();
  void WireConnections();
  void UpdateButtons();
  void RefreshHotspotTable();
  void RefreshLanguageBreakdown();

  ProfileSession *session_{nullptr};
  bool owns_session_{true};

  // Toolbar / inputs
  QToolBar *toolbar_{nullptr};
  QPushButton *run_bench_btn_{nullptr};
  QPushButton *run_profile_btn_{nullptr};
  QPushButton *start_stream_btn_{nullptr};
  QPushButton *stop_stream_btn_{nullptr};
  QSpinBox *duration_spin_{nullptr};
  QSpinBox *interval_spin_{nullptr};
  QLabel *status_label_{nullptr};

  // Tabs
  QTabWidget *tabs_{nullptr};
  QTreeView *flame_view_{nullptr};
  QTableView *hotspots_view_{nullptr};
  QTableView *timeline_view_{nullptr};
  QPlainTextEdit *language_view_{nullptr};
  QPlainTextEdit *log_view_{nullptr};
};

} // namespace polyglot::tools::ui
