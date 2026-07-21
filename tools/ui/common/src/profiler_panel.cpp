/**
 * @file     profiler_panel.cpp
 * @brief    ProfilerPanel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/profiler_panel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QtGlobal>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/include/data_models/call_graph_model.h"
#include "tools/ui/common/include/data_models/flame_node.h"
#include "tools/ui/common/include/data_models/timeline_model.h"
#include "tools/ui/common/include/profile_session.h"

namespace polyglot::tools::ui {

ProfilerPanel::ProfilerPanel(QWidget *parent) : QWidget(parent) {
  session_ = new ProfileSession(this);
  owns_session_ = true;
  BuildUi();
  WireConnections();
  UpdateButtons();
}

ProfilerPanel::~ProfilerPanel() = default;

void ProfilerPanel::SetSession(ProfileSession *session) {
  if (session_ == session) {
    return;
  }
  if (owns_session_ && session_) {
    session_->deleteLater();
  }
  session_ = session;
  owns_session_ = false;
  if (session_) {
    flame_view_->setModel(session_->Flame());
    hotspots_view_->setModel(session_->Flame());
    timeline_view_->setModel(session_->Timeline());
    WireConnections();
  }
}

void ProfilerPanel::BuildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  toolbar_ = new QToolBar(this);
  run_bench_btn_ = new QPushButton(tr("Bench"), this);
  run_profile_btn_ = new QPushButton(tr("Profile"), this);
  start_stream_btn_ = new QPushButton(tr("Start Stream"), this);
  stop_stream_btn_ = new QPushButton(tr("Stop Stream"), this);

  duration_spin_ = new QSpinBox(this);
  duration_spin_->setRange(100, 600000);
  duration_spin_->setValue(2000);
  duration_spin_->setSuffix(tr(" ms"));
  duration_spin_->setToolTip(tr("Profile duration"));

  interval_spin_ = new QSpinBox(this);
  interval_spin_->setRange(20, 5000);
  interval_spin_->setValue(200);
  interval_spin_->setSuffix(tr(" ms"));
  interval_spin_->setToolTip(tr("Sampling interval (>=200 ms = 5 Hz)"));

  status_label_ = new QLabel(tr("Idle"), this);
  status_label_->setStyleSheet(QStringLiteral("color: gray;"));

  toolbar_->addWidget(run_bench_btn_);
  toolbar_->addWidget(run_profile_btn_);
  toolbar_->addSeparator();
  toolbar_->addWidget(new QLabel(tr("Duration:"), this));
  toolbar_->addWidget(duration_spin_);
  toolbar_->addWidget(new QLabel(tr("Interval:"), this));
  toolbar_->addWidget(interval_spin_);
  toolbar_->addSeparator();
  toolbar_->addWidget(start_stream_btn_);
  toolbar_->addWidget(stop_stream_btn_);
  toolbar_->addSeparator();
  toolbar_->addWidget(status_label_);

  layout->addWidget(toolbar_);

  tabs_ = new QTabWidget(this);

  flame_view_ = new QTreeView(this);
  flame_view_->setModel(session_->Flame());
  flame_view_->setUniformRowHeights(true);
  flame_view_->setAlternatingRowColors(true);
  flame_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tabs_->addTab(flame_view_, tr("Flame"));

  hotspots_view_ = new QTableView(this);
  hotspots_view_->setModel(session_->Flame());
  hotspots_view_->setSortingEnabled(true);
  hotspots_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  hotspots_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  hotspots_view_->horizontalHeader()->setStretchLastSection(true);
  tabs_->addTab(hotspots_view_, tr("Hotspots"));

  timeline_view_ = new QTableView(this);
  timeline_view_->setModel(session_->Timeline());
  timeline_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  timeline_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  timeline_view_->horizontalHeader()->setStretchLastSection(true);
  tabs_->addTab(timeline_view_, tr("Timeline"));

  language_view_ = new QPlainTextEdit(this);
  language_view_->setReadOnly(true);
  tabs_->addTab(language_view_, tr("Languages"));

  log_view_ = new QPlainTextEdit(this);
  log_view_->setReadOnly(true);
  log_view_->setMaximumBlockCount(2000);
  tabs_->addTab(log_view_, tr("Log"));

  layout->addWidget(tabs_, /*stretch=*/1);
}

void ProfilerPanel::WireConnections() {
  connect(run_bench_btn_, &QPushButton::clicked, this, &ProfilerPanel::OnRunBenchmarkClicked);
  connect(run_profile_btn_, &QPushButton::clicked, this,
          &ProfilerPanel::OnRunProfileClicked);
  connect(start_stream_btn_, &QPushButton::clicked, this,
          &ProfilerPanel::OnStartStreamClicked);
  connect(stop_stream_btn_, &QPushButton::clicked, this,
          &ProfilerPanel::OnStopStreamClicked);
  connect(hotspots_view_, &QAbstractItemView::doubleClicked, this,
          &ProfilerPanel::OnHotspotDoubleClicked);
  connect(flame_view_, &QAbstractItemView::doubleClicked, this,
          &ProfilerPanel::OnFlameDoubleClicked);

  if (!session_) {
    return;
  }
  connect(session_, &ProfileSession::BenchmarkFinished, this,
          &ProfilerPanel::OnSessionFinished);
  connect(session_, &ProfileSession::ProfileFinished, this,
          &ProfilerPanel::OnSessionFinished);
  connect(session_, &ProfileSession::StreamSampleReceived, this,
          &ProfilerPanel::OnStreamSampleReceived);
  connect(session_, &ProfileSession::ToolErrorOutput, this,
          &ProfilerPanel::OnToolErrorOutput);
}

void ProfilerPanel::UpdateButtons() {
  const bool busy = session_ && session_->IsRunning();
  const bool streaming = session_ && session_->IsStreaming();
  run_bench_btn_->setEnabled(!busy);
  run_profile_btn_->setEnabled(!busy);
  start_stream_btn_->setEnabled(!streaming);
  stop_stream_btn_->setEnabled(streaming);
}

void ProfilerPanel::OnRunBenchmarkClicked() {
  if (!session_) {
    return;
  }
  status_label_->setText(tr("Running benchmark…"));
  session_->RunBenchmark();
  UpdateButtons();
}

void ProfilerPanel::OnRunProfileClicked() {
  if (!session_) {
    return;
  }
  status_label_->setText(tr("Profiling…"));
  session_->RunProfile(duration_spin_->value(), interval_spin_->value());
  UpdateButtons();
}

void ProfilerPanel::OnStartStreamClicked() {
  if (!session_) {
    return;
  }
  status_label_->setText(tr("Streaming…"));
  session_->StartProfileStream(interval_spin_->value());
  UpdateButtons();
}

void ProfilerPanel::OnStopStreamClicked() {
  if (!session_) {
    return;
  }
  session_->StopProfileStream();
  status_label_->setText(tr("Stream stopped"));
  UpdateButtons();
}

void ProfilerPanel::OnSessionFinished(bool ok, const QString &message) {
  status_label_->setText(message);
  status_label_->setStyleSheet(ok ? QStringLiteral("color: #2e8b57;")
                                  : QStringLiteral("color: #c93838;"));
  RefreshHotspotTable();
  RefreshLanguageBreakdown();
  emit StatusMessage(message);
  UpdateButtons();
}

void ProfilerPanel::OnStreamSampleReceived(int total_samples) {
  status_label_->setText(tr("Streaming — %1 samples").arg(total_samples));
  if ((total_samples & 0x07) == 0) {
    RefreshLanguageBreakdown();
  }
}

void ProfilerPanel::OnToolErrorOutput(const QString &tool, const QString &line) {
  log_view_->appendPlainText(QStringLiteral("[%1] %2").arg(tool, line));
}

void ProfilerPanel::OnHotspotDoubleClicked(const QModelIndex &index) {
  if (!index.isValid() || !session_) {
    return;
  }
  const auto *node = session_->Flame()->NodeAt(index);
  if (!node || node->function.isEmpty()) {
    return;
  }
  // Best-effort cross-reference into the call-graph model for source info.
  if (auto *graph = session_->CallGraph()) {
    const int row = graph->RowForId(node->function);
    if (row >= 0) {
      const auto &n = graph->Nodes()[row];
      if (!n.file.isEmpty()) {
        emit OpenFileRequested(n.file, n.line > 0 ? n.line : 1);
      }
    }
  }
}

void ProfilerPanel::OnFlameDoubleClicked(const QModelIndex &index) {
  OnHotspotDoubleClicked(index);
}

void ProfilerPanel::RefreshHotspotTable() {
  if (!session_) {
    return;
  }
  hotspots_view_->resizeColumnsToContents();
  flame_view_->expandToDepth(2);
}

void ProfilerPanel::RefreshLanguageBreakdown() {
  if (!session_) {
    return;
  }
  // Walk the flame tree to bucket inclusive_ns by language.
  std::unordered_map<std::string, std::uint64_t> by_lang;
  std::uint64_t total = 0;
  std::vector<const FlameNode *> stack;
  if (const auto *root = session_->Flame()->Root()) {
    for (const auto &child : root->children) {
      stack.push_back(child.get());
    }
  }
  while (!stack.empty()) {
    const FlameNode *n = stack.back();
    stack.pop_back();
    by_lang[n->language.toStdString()] += n->self_ns;
    total += n->self_ns;
    for (const auto &child : n->children) {
      stack.push_back(child.get());
    }
  }
  // Render simple text breakdown.
  language_view_->clear();
  if (total == 0) {
    language_view_->setPlainText(tr("(no profile data yet)"));
    return;
  }
  std::vector<std::pair<std::string, std::uint64_t>> entries(by_lang.begin(), by_lang.end());
  std::sort(entries.begin(), entries.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  QString out;
  out += tr("Cross-language self-time breakdown\n");
  out += QStringLiteral("================================\n");
  for (const auto &kv : entries) {
    const double pct = 100.0 * static_cast<double>(kv.second) /
                        static_cast<double>(total);
    out += QStringLiteral("%1  %2 ms  (%3 %)\n")
                .arg(QString::fromStdString(kv.first), -12)
                .arg(QString::number(static_cast<double>(kv.second) / 1.0e6, 'f', 3), 10)
                .arg(QString::number(pct, 'f', 1), 5);
  }
  language_view_->setPlainText(out);
}

} // namespace polyglot::tools::ui
