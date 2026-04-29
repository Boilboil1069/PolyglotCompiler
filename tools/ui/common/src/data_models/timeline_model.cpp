/**
 * @file     timeline_model.cpp
 * @brief    TimelineModel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/data_models/timeline_model.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace polyglot::tools::ui {

TimelineModel::TimelineModel(QObject *parent) : QAbstractListModel(parent) {}
TimelineModel::~TimelineModel() = default;

void TimelineModel::Replace(std::vector<TimelineEvent> events) {
  beginResetModel();
  events_ = std::move(events);
  lanes_.clear();
  for (const auto &e : events_) {
    RegisterLane(e.thread);
  }
  Recompute();
  endResetModel();
}

void TimelineModel::Append(const TimelineEvent &event) {
  const int row = static_cast<int>(events_.size());
  beginInsertRows({}, row, row);
  events_.push_back(event);
  RegisterLane(event.thread);
  if (events_.size() == 1) {
    min_start_ns_ = event.start_ns;
    max_end_ns_ = event.start_ns + event.duration_ns;
  } else {
    if (event.start_ns < min_start_ns_) {
      min_start_ns_ = event.start_ns;
    }
    const std::uint64_t end = event.start_ns + event.duration_ns;
    if (end > max_end_ns_) {
      max_end_ns_ = end;
    }
  }
  endInsertRows();
}

void TimelineModel::Clear() {
  beginResetModel();
  events_.clear();
  lanes_.clear();
  min_start_ns_ = 0;
  max_end_ns_ = 0;
  endResetModel();
}

int TimelineModel::LaneIndex(const QString &thread) const {
  for (int i = 0; i < static_cast<int>(lanes_.size()); ++i) {
    if (lanes_[i] == thread) {
      return i;
    }
  }
  return -1;
}

int TimelineModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(events_.size());
}

int TimelineModel::columnCount(const QModelIndex & /*parent*/) const { return kColumnCount; }

QVariant TimelineModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(events_.size())) {
    return {};
  }
  const auto &e = events_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case kColumnFunction:
      return e.function;
    case kColumnThread:
      return e.thread;
    case kColumnLanguage:
      return e.language;
    case kColumnStart:
      return QString::number(static_cast<double>(e.start_ns) / 1.0e6, 'f', 3) + " ms";
    case kColumnDuration:
      return QString::number(static_cast<double>(e.duration_ns) / 1.0e3, 'f', 2) + " us";
    case kColumnCalls:
      return QString::number(static_cast<qulonglong>(e.calls));
    default:
      return {};
    }
  }
  return {};
}

QVariant TimelineModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
  case kColumnFunction:
    return tr("Function");
  case kColumnThread:
    return tr("Thread");
  case kColumnLanguage:
    return tr("Language");
  case kColumnStart:
    return tr("Start");
  case kColumnDuration:
    return tr("Duration");
  case kColumnCalls:
    return tr("Calls");
  default:
    return {};
  }
}

void TimelineModel::RegisterLane(const QString &thread) {
  if (std::find(lanes_.begin(), lanes_.end(), thread) == lanes_.end()) {
    lanes_.push_back(thread);
  }
}

void TimelineModel::Recompute() {
  if (events_.empty()) {
    min_start_ns_ = 0;
    max_end_ns_ = 0;
    return;
  }
  min_start_ns_ = std::numeric_limits<std::uint64_t>::max();
  max_end_ns_ = 0;
  for (const auto &e : events_) {
    min_start_ns_ = std::min(min_start_ns_, e.start_ns);
    max_end_ns_ = std::max(max_end_ns_, e.start_ns + e.duration_ns);
  }
}

} // namespace polyglot::tools::ui
