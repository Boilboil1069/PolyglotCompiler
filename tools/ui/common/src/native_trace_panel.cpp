#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QResizeEvent>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "tools/ui/common/include/native_trace_panel.h"

namespace polyglot::tools::ui {

NativeTracePanel::NativeTracePanel(QWidget *parent) : QWidget(parent) {
  setObjectName("nativeTracePanel");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  overlay_ = new QCheckBox(tr("Show sample on source and graph"));
  overlay_->setChecked(true);
  overlay_->setObjectName("runtimeOverlayToggle");
  layout->addWidget(overlay_);
  auto *scope = new QLabel(tr("Recorded foreign CALLs · single thread"));
  scope->setWordWrap(true);
  scope->setToolTip(
      tr("Each row is a real invocation. Parent IDs only describe other recorded foreign CALLs."));
  layout->addWidget(scope);
  status_ = new QLabel(tr("No runtime session. Enable Trace calls, then Compile & Run."));
  status_->setWordWrap(true);
  status_->setObjectName("runtimeTraceStatus");
  layout->addWidget(status_);
  auto *split = new QSplitter(Qt::Horizontal);
  values_split_ = split;
  samples_ = new QTreeWidget();
  samples_->setObjectName("runtimeTraceSamples");
  samples_->setColumnCount(4);
  samples_->setHeaderLabels({tr("#"), tr("Call"), tr("Return"), tr("µs")});
  samples_->setRootIsDecorated(false);
  samples_->setUniformRowHeights(true);
  samples_->header()->setSectionResizeMode(QHeaderView::Interactive);
  samples_->header()->setStretchLastSection(false);
  for (int column : {0, 2, 3})
    samples_->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  samples_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  samples_->setMinimumHeight(60);
  samples_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  values_ = new QTreeWidget();
  values_->setObjectName("runtimeTraceValues");
  values_->setColumnCount(3);
  values_->setHeaderLabels({tr("Parameter"), tr("Type"), tr("Value")});
  values_->header()->setSectionResizeMode(QHeaderView::Interactive);
  values_->setColumnWidth(0, 105);
  values_->setColumnWidth(1, 48);
  values_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  values_->setMinimumHeight(60);
  values_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  split->addWidget(samples_);
  split->addWidget(values_);
  layout->addWidget(split, 1);
  timer_ = new QTimer(this);
  timer_->setInterval(150);
  connect(timer_, &QTimer::timeout, this, &NativeTracePanel::Poll);
  connect(samples_, &QTreeWidget::itemSelectionChanged, this, &NativeTracePanel::UpdateSelection);
  connect(overlay_, &QCheckBox::toggled, this, &NativeTracePanel::OverlayChanged);
}

void NativeTracePanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  const auto orientation = width() >= 480 ? Qt::Horizontal : Qt::Vertical;
  if (values_split_->orientation() != orientation) {
    values_split_->setOrientation(orientation);
    values_split_->setSizes({1, 1});
  }
}

bool NativeTracePanel::OverlayEnabled() const {
  return overlay_->isChecked();
}

void NativeTracePanel::ClearSession() {
  timer_->stop();
  following_ = false;
  saw_file_ = false;
  trace_.Clear();
  trace_file_.clear();
  error_.clear();
  pending_.clear();
  offset_ = 0;
  samples_->clear();
  values_->clear();
  status_->setText(tr("No runtime samples."));
  emit OverlayChanged(overlay_->isChecked());
}

void NativeTracePanel::Fail(const QString &message) {
  error_ = message;
  timer_->stop();
  following_ = false;
  status_->setText(tr("Trace error: %1").arg(message));
  emit TraceError(message);
}

bool NativeTracePanel::BeginSession(const QString &trace_file, bool follow) {
  ClearSession();
  trace_file_ = trace_file;
  QFile metadata(trace_file + ".sites.json");
  if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 8 * 1024 * 1024) {
    Fail(tr("Cannot read compiler call-site metadata: %1").arg(metadata.fileName()));
    return false;
  }
  std::string error;
  if (!trace_.LoadSites(metadata.readAll().toStdString(), error)) {
    Fail(QString::fromStdString(error));
    return false;
  }
  following_ = follow;
  status_->setText(
      tr("Waiting for recorded calls · %1 compiler call sites").arg(trace_.Sites().size()));
  Poll();
  if (following_ && error_.isEmpty())
    timer_->start();
  if (!follow)
    FinishSession();
  return error_.isEmpty();
}

void NativeTracePanel::Poll() {
  if (!error_.isEmpty() || trace_file_.isEmpty())
    return;
  QFile file(trace_file_);
  if (!file.exists())
    return; // The process has not reached a traced call yet.
  if (!file.open(QIODevice::ReadOnly)) {
    Fail(tr("Cannot read trace file: %1").arg(trace_file_));
    return;
  }
  saw_file_ = true;
  if (file.size() < offset_) {
    Fail(tr("Trace file was truncated during this session"));
    return;
  }
  if (file.size() > 32 * 1024 * 1024) {
    Fail(tr("Trace exceeds the 32 MiB inspection limit"));
    return;
  }
  file.seek(offset_);
  pending_ += file.read(4 * 1024 * 1024);
  offset_ = file.pos();
  const auto previous_count = trace_.Samples().size();
  while (true) {
    const auto newline = pending_.indexOf('\n');
    if (newline < 0)
      break;
    const auto line = pending_.left(newline).trimmed();
    pending_.remove(0, newline + 1);
    if (line.isEmpty())
      continue;
    std::string error;
    if (!trace_.AppendEvent(line.toStdString(), error)) {
      UpdateRows(previous_count);
      Fail(QString::fromStdString(error));
      return;
    }
  }
  UpdateRows(previous_count);
  status_->setText(tr("%1 recorded calls · %2")
                       .arg(trace_.Samples().size())
                       .arg(following_ ? tr("recording") : tr("completed session")));
}

void NativeTracePanel::FinishSession() {
  timer_->stop();
  following_ = false;
  do {
    const auto previous_offset = offset_;
    Poll();
    if (!error_.isEmpty() || previous_offset == offset_)
      break;
  } while (QFileInfo(trace_file_).size() > offset_);
  if (!error_.isEmpty())
    return;
  if (!saw_file_) {
    Fail(tr(
        "No trace file was produced. Check program errors and whether a traced CALL was reached."));
    return;
  }
  if (!pending_.trimmed().isEmpty()) {
    const auto previous_count = trace_.Samples().size();
    std::string error;
    if (!trace_.AppendEvent(pending_.toStdString(), error)) {
      Fail(tr("Incomplete final trace record: %1").arg(QString::fromStdString(error)));
      return;
    }
    pending_.clear();
    UpdateRows(previous_count);
  }
  status_->setText(tr("%1 recorded calls · completed session").arg(trace_.Samples().size()));
}

void NativeTracePanel::UpdateRows(std::size_t previous_count) {
  for (std::size_t index = previous_count; index < trace_.Samples().size(); ++index) {
    const auto &sample = trace_.Samples()[index];
    const auto *site = trace_.Site(sample.site_id);
    auto *row = new QTreeWidgetItem(
        samples_, {QString::fromStdString(sample.instance_id), QString::fromStdString(site->callee),
                   sample.result.available ? QString::fromStdString(sample.result.display)
                                           : tr("unavailable"),
                   QString::number(static_cast<double>(sample.duration_ns / 1000.0L), 'f', 3)});
    row->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
    row->setToolTip(1, QString::fromStdString(site->callee) + "\n" +
                           QString::fromStdString(site->file) + ":" + QString::number(site->line) +
                           ":" + QString::number(site->column));
  }
  if (!samples_->currentItem() && samples_->topLevelItemCount())
    SelectSample(0);
}

const runtime::NativeTraceSample *NativeTracePanel::SelectedSample() const {
  if (!samples_->currentItem())
    return nullptr;
  const auto index = samples_->currentItem()->data(0, Qt::UserRole).toULongLong();
  return index < trace_.Samples().size() ? &trace_.Samples()[index] : nullptr;
}

bool NativeTracePanel::SelectSample(std::size_t index) {
  if (index >= static_cast<std::size_t>(samples_->topLevelItemCount()))
    return false;
  samples_->setCurrentItem(samples_->topLevelItem(static_cast<int>(index)));
  return true;
}

void NativeTracePanel::UpdateSelection() {
  values_->clear();
  const auto *sample = SelectedSample();
  if (!sample)
    return;
  const auto append = [&](const runtime::NativeTraceValue &value) {
    auto *row = new QTreeWidgetItem(
        values_,
        {QString::fromStdString(value.name), QString::fromStdString(value.type),
         value.available
             ? QString::fromStdString(value.display)
             : tr("Unavailable: %1").arg(QString::fromStdString(value.unavailable_reason))});
    row->setToolTip(0, QString::fromStdString(value.name));
    row->setToolTip(1, QString::fromStdString(value.type));
    row->setToolTip(2, tr("Captured ABI bits: %1").arg(QString::fromStdString(value.raw_bits)));
  };
  for (const auto &argument : sample->arguments)
    append(argument);
  append(sample->result);
  new QTreeWidgetItem(
      values_,
      {tr("Parent traced instance"), {}, QString::fromStdString(sample->parent_instance_id)});
  emit SampleSelected();
}

} // namespace polyglot::tools::ui
