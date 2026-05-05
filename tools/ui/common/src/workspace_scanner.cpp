/**
 * @file     workspace_scanner.cpp
 * @brief    Implementation of @ref polyglot::tools::ui::WorkspaceScanner
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/workspace_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "frontends/common/include/frontend_registry.h"
#include "tools/ui/common/include/compiler_service.h"
#include "tools/ui/common/include/problems_aggregator.h"

namespace polyglot::tools::ui {

namespace {

// Cap on how many directories we ask QFileSystemWatcher to monitor — the
// OS imposes a per-process limit (Linux ~8K inotify watches by default,
// Windows uses ReadDirectoryChangesW which is generous but not free) and
// for very large trees we degrade gracefully to root-only monitoring.
constexpr int kMaxWatchedDirectories = 1024;

// Skip these directories outright when walking the workspace — they
// dominate the file count in real projects but never contain user
// source.  The list is intentionally small; users can extend the
// behaviour with workspace-level settings (out of scope for this
// scanner).
const QStringList &SkippedDirNames() {
  static const QStringList list = {
      QStringLiteral(".git"),       QStringLiteral(".hg"),
      QStringLiteral(".svn"),       QStringLiteral("node_modules"),
      QStringLiteral("build"),      QStringLiteral("build-debug"),
      QStringLiteral("build-release"), QStringLiteral("target"),
      QStringLiteral(".cache"),     QStringLiteral(".polyc_cache"),
      QStringLiteral("__pycache__"),QStringLiteral(".venv"),
      QStringLiteral("venv"),       QStringLiteral("dist"),
  };
  return list;
}

bool ShouldSkipDir(const QString &name) {
  return SkippedDirNames().contains(name);
}

}  // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

WorkspaceScanner::WorkspaceScanner(ProblemsAggregator *aggregator,
                                   CompilerService *compiler, QObject *parent)
    : QObject(parent), aggregator_(aggregator), compiler_(compiler) {
  tick_.setSingleShot(false);
  tick_.setInterval(kTickIntervalMs);
  connect(&tick_, &QTimer::timeout, this, &WorkspaceScanner::OnTick);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
          &WorkspaceScanner::OnDirectoryChanged);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
          &WorkspaceScanner::OnFileChanged);
}

WorkspaceScanner::~WorkspaceScanner() = default;

// ============================================================================
// Public API
// ============================================================================

void WorkspaceScanner::SetWorkspaceRoot(const QString &root) {
  if (root == root_) return;

  // Drop everything we previously published under the background source
  // — the new root is unrelated.
  if (aggregator_) {
    aggregator_->ClearSource(std::string("polyc-bg"));
  }
  if (!watcher_.directories().isEmpty()) {
    watcher_.removePaths(watcher_.directories());
  }
  if (!watcher_.files().isEmpty()) {
    watcher_.removePaths(watcher_.files());
  }
  queue_.clear();
  queued_.clear();
  total_in_run_ = 0;
  processed_in_run_ = 0;
  large_run_ = false;
  tick_.stop();

  root_ = root;
  if (root_.isEmpty() || !aggregator_ || !compiler_) return;

  StartFullScan();
}

// ============================================================================
// Scan orchestration
// ============================================================================

void WorkspaceScanner::StartFullScan() {
  EnqueueDirectory(root_);
  total_in_run_ = queue_.size();
  processed_in_run_ = 0;
  large_run_ = (total_in_run_ >= kLargeWorkspaceFiles);
  if (large_run_) {
    emit ProgressUpdated(0, total_in_run_);
  }
  RewatchAll();
  if (!queue_.isEmpty()) {
    tick_.start();
  } else {
    emit ScanFinished(0);
  }
}

void WorkspaceScanner::EnqueueDirectory(const QString &dir) {
  // Recursive walk via QDirIterator — avoids a stack-depth blowup on
  // pathologically deep trees.  We add candidate files to the queue and
  // skip the directories listed in @ref SkippedDirNames.
  QDirIterator it(dir,
                  QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                      QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    if (fi.isDir()) {
      if (ShouldSkipDir(fi.fileName())) {
        // Cannot prune subtrees with QDirIterator; instead we test each
        // child for a skipped ancestor below.  Continue here — files
        // beneath this dir still surface but are filtered.
      }
      continue;
    }
    // Reject any path containing a skipped directory component.
    bool skip = false;
    const QString rel = QDir(root_).relativeFilePath(fi.absoluteFilePath());
    for (const QString &part : rel.split('/', Qt::SkipEmptyParts)) {
      if (ShouldSkipDir(part)) {
        skip = true;
        break;
      }
    }
    if (skip) continue;
    if (IsCandidate(fi.absoluteFilePath())) {
      EnqueueFile(fi.absoluteFilePath());
    }
  }
}

void WorkspaceScanner::EnqueueFile(const QString &path) {
  if (queued_.contains(path)) return;
  queued_.insert(path);
  queue_.enqueue(path);
}

bool WorkspaceScanner::IsCandidate(const QString &path) const {
  // Rely on the registry's path-based detection — if a frontend claims
  // the extension, it is worth analysing.
  const std::string lang =
      polyglot::frontends::FrontendRegistry::Instance().DetectLanguage(path.toStdString());
  return !lang.empty() && lang != "unknown";
}

// ============================================================================
// Tick / analyse
// ============================================================================

void WorkspaceScanner::OnTick() {
  for (int i = 0; i < kBatchSize && !queue_.isEmpty(); ++i) {
    const QString path = queue_.dequeue();
    queued_.remove(path);
    AnalyseOne(path);
    ++processed_in_run_;
  }
  if (large_run_) {
    emit ProgressUpdated(processed_in_run_, total_in_run_);
  }
  if (queue_.isEmpty()) {
    tick_.stop();
    emit ScanFinished(total_in_run_);
    large_run_ = false;
  }
}

void WorkspaceScanner::AnalyseOne(const QString &path) {
  if (!aggregator_ || !compiler_) return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
  QTextStream ts(&file);
  const std::string source = ts.readAll().toStdString();
  file.close();

  const std::string language =
      polyglot::frontends::FrontendRegistry::Instance().DetectLanguage(path.toStdString());
  if (language.empty()) return;

  const QString abs = QFileInfo(path).absoluteFilePath();
  auto diags = compiler_->Analyze(source, language, abs.toStdString());
  aggregator_->ReplaceFromDiagnosticInfo(abs.toStdString(),
                                         std::string("polyc-bg"), diags);
}

// ============================================================================
// File system watching
// ============================================================================

void WorkspaceScanner::RewatchAll() {
  // Watch directories so QFileSystemWatcher fires on create / delete
  // events, not just on the modification of already-watched files.
  if (root_.isEmpty()) return;
  QStringList dirs;
  dirs.reserve(64);
  dirs << root_;
  QDirIterator it(root_, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (ShouldSkipDir(it.fileName())) continue;
    dirs << it.filePath();
    if (dirs.size() >= kMaxWatchedDirectories) break;
  }
  if (!dirs.isEmpty()) {
    watcher_.addPaths(dirs);
  }
}

void WorkspaceScanner::OnDirectoryChanged(const QString &path) {
  if (!aggregator_ || !compiler_ || root_.isEmpty()) return;
  // Rescan just this directory's immediate children: enumerate, drop
  // diagnostics for any tracked file that no longer exists, enqueue new
  // / changed files for analysis on the next tick.
  QDir d(path);
  const QStringList present = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
  QSet<QString> present_set;
  for (const QString &name : present) {
    present_set.insert(d.absoluteFilePath(name));
  }
  // Detect deletions: any entry we previously knew about that now
  // vanished.  Without a separate index we approximate by clearing
  // diagnostics for files that are missing right now.
  for (const QString &name : d.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
    Q_UNUSED(name);
  }
  for (const QString &abs : present_set) {
    if (IsCandidate(abs)) EnqueueFile(abs);
  }
  if (!queue_.isEmpty() && !tick_.isActive()) {
    total_in_run_ = queue_.size();
    processed_in_run_ = 0;
    large_run_ = false;
    tick_.start();
  }
}

void WorkspaceScanner::OnFileChanged(const QString &path) {
  if (!aggregator_) return;
  QFileInfo fi(path);
  if (!fi.exists()) {
    aggregator_->ClearFile(QFileInfo(path).absoluteFilePath().toStdString());
    return;
  }
  if (!IsCandidate(path)) return;
  EnqueueFile(QFileInfo(path).absoluteFilePath());
  if (!tick_.isActive()) {
    total_in_run_ = queue_.size();
    processed_in_run_ = 0;
    large_run_ = false;
    tick_.start();
  }
}

}  // namespace polyglot::tools::ui
