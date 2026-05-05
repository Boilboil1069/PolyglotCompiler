/**
 * @file     workspace_scanner.h
 * @brief    Background workspace diagnostics scanner
 *
 * Walks every source file under the workspace root that has a known
 * frontend, runs @ref CompilerService::Analyze in batches on the GUI
 * thread (via @ref QTimer ticks so the UI stays responsive), and
 * publishes the resulting diagnostics into the workspace-wide
 * @ref ProblemsAggregator under source label `"polyc-bg"`.  A
 * @ref QFileSystemWatcher keeps the index live: edits, creates and
 * deletes trigger an incremental rescan of just the affected paths.
 *
 * Implements demand 2026-04-28-20 §2 (real-time check / workspace
 * background scan / large workspace progress).
 *
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace polyglot::tools::ui {

class CompilerService;
class ProblemsAggregator;

/// @brief Asynchronous workspace diagnostic scanner.
///
/// One instance per MainWindow.  Lifecycle:
///   1. @ref SetWorkspaceRoot starts a fresh recursive enumeration.
///   2. Files are appended to a queue and drained at
///      @c kBatchSize files per @c kTickIntervalMs tick.
///   3. Each batched file is analysed via @ref CompilerService::Analyze
///      and the result is published to the aggregator.
///   4. A @ref QFileSystemWatcher monitors the root directory tree and
///      enqueues changed / newly created files; deleted files are
///      removed from the aggregator immediately.
class WorkspaceScanner : public QObject {
  Q_OBJECT

 public:
  /// First-scan threshold above which a status-bar progress message is
  /// surfaced via @ref ProgressUpdated.
  static constexpr int kLargeWorkspaceFiles = 2000;
  /// Files processed per tick.
  static constexpr int kBatchSize = 50;
  /// Tick interval, ms.
  static constexpr int kTickIntervalMs = 50;

  WorkspaceScanner(ProblemsAggregator *aggregator, CompilerService *compiler,
                   QObject *parent = nullptr);
  ~WorkspaceScanner() override;

  /// Switch the workspace root.  Empty string disables scanning and
  /// detaches the watcher.  Triggers a full rescan when @p root differs
  /// from the previous value.
  void SetWorkspaceRoot(const QString &root);

  /// Current workspace root (may be empty).
  QString WorkspaceRoot() const { return root_; }

 signals:
  /// Emitted while a scan is in progress (only when the total work
  /// exceeds @ref kLargeWorkspaceFiles).
  void ProgressUpdated(int done, int total);
  /// Emitted once when a scan run completes (queue drained).
  void ScanFinished(int total_files);

 private slots:
  void OnTick();
  void OnDirectoryChanged(const QString &path);
  void OnFileChanged(const QString &path);

 private:
  void StartFullScan();
  void EnqueueDirectory(const QString &dir);
  void EnqueueFile(const QString &path);
  void AnalyseOne(const QString &path);
  bool IsCandidate(const QString &path) const;
  void RewatchAll();

  ProblemsAggregator *aggregator_{nullptr};
  CompilerService *compiler_{nullptr};

  QString root_;
  QFileSystemWatcher watcher_;
  QTimer tick_;

  QQueue<QString> queue_;
  QSet<QString> queued_;       ///< Deduplicates pending entries.
  int total_in_run_{0};        ///< Total files in the current scan run.
  int processed_in_run_{0};    ///< Done so far.
  bool large_run_{false};      ///< True when ProgressUpdated should fire.
};

}  // namespace polyglot::tools::ui
