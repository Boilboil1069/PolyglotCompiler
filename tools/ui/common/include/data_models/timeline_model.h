/**
 * @file     timeline_model.h
 * @brief    Timeline (swimlane) model for the Profiler panel
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <cstdint>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// TimelineEvent — a single bar in the Timeline view
// ============================================================================

/** @brief One execution-window event drawn on the timeline. */
struct TimelineEvent {
  QString function;
  QString language;
  QString thread;            // logical thread / lane id
  std::uint64_t start_ns{0}; // monotonic timestamp
  std::uint64_t duration_ns{0};
  std::uint64_t calls{0};
  bool is_bridge{false};     // cross-language marshalling event
};

// ============================================================================
// TimelineModel — list of timeline events with lane tracking
// ============================================================================

/**
 * @brief List model exposing TimelineEvent rows.
 *
 * The Profiler panel paints each row according to its lane (one swimlane
 * per unique thread id, in first-appearance order).  The model maintains
 * the lane list and time-range aggregates so views can scale axes without
 * walking the event vector.
 */
class TimelineModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Column {
    kColumnFunction = 0,
    kColumnThread,
    kColumnLanguage,
    kColumnStart,
    kColumnDuration,
    kColumnCalls,
    kColumnCount,
  };

  explicit TimelineModel(QObject *parent = nullptr);
  ~TimelineModel() override;

  /// Replace the entire event vector.
  void Replace(std::vector<TimelineEvent> events);
  void Append(const TimelineEvent &event);
  void Clear();

  const std::vector<TimelineEvent> &Events() const { return events_; }
  const std::vector<QString> &Lanes() const { return lanes_; }

  std::uint64_t MinStartNs() const { return min_start_ns_; }
  std::uint64_t MaxEndNs() const { return max_end_ns_; }

  /// Return the lane index (0-based, in first-appearance order) for a thread id.
  int LaneIndex(const QString &thread) const;

  // QAbstractListModel overrides
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  void RegisterLane(const QString &thread);
  void Recompute();

  std::vector<TimelineEvent> events_;
  std::vector<QString> lanes_;
  std::uint64_t min_start_ns_{0};
  std::uint64_t max_end_ns_{0};
};

} // namespace polyglot::tools::ui
