/**
 * @file     profile_session.h
 * @brief    Orchestrates polybench / polyrt / polyc invocations and parses
 *           their JSON output into the shared Profiler data models.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>
#include <memory>
#include <vector>

namespace polyglot::tools::ui {

class FlameTreeModel;
class CallGraphModel;
class TimelineModel;
struct FlameNode;

// ============================================================================
// ProfileSession — runs profiling tools and feeds the data models
// ============================================================================

/**
 * @brief Owns FlameTreeModel / CallGraphModel / TimelineModel instances and
 *        keeps them in sync with profiling subprocess output.
 *
 * Three modes are supported:
 *   * @ref RunBenchmark — one-shot @c polybench @c polyrt @c bench JSON capture.
 *   * @ref RunProfile — bounded @c polyrt @c profile sampling session.
 *   * @ref StartProfileStream — long-running @c polyrt @c profile @c --stream
 *     pipe consumed line-by-line until @ref StopProfileStream.
 *
 * Tool paths default to the corresponding sibling executables in the IDE's
 * application directory and may be overridden via SettingsService keys
 * @c profiler.polyrtPath / @c profiler.polybenchPath / @c profiler.polycPath.
 */
class ProfileSession : public QObject {
  Q_OBJECT

public:
  explicit ProfileSession(QObject *parent = nullptr);
  ~ProfileSession() override;

  FlameTreeModel *Flame() const { return flame_; }
  CallGraphModel *CallGraph() const { return call_graph_; }
  TimelineModel *Timeline() const { return timeline_; }

  /// Fully qualified executable paths.  Empty strings fall back to the
  /// auto-detected sibling executables in the IDE's application directory.
  void SetPolyrtPath(const QString &path) { polyrt_path_ = path; }
  void SetPolybenchPath(const QString &path) { polybench_path_ = path; }
  void SetPolycPath(const QString &path) { polyc_path_ = path; }

  QString PolyrtPath() const;
  QString PolybenchPath() const;
  QString PolycPath() const;

  /// True while any polyrt / polybench process is currently active.
  bool IsRunning() const;

  // One-shot operations -------------------------------------------------

  /// Invoke `polybench --json <tmp>` (or polyrt bench when polybench is
  /// unavailable), then re-populate the flame / timeline models.  Emits
  /// @ref BenchmarkFinished on completion.
  void RunBenchmark();

  /// Invoke `polyrt profile --json <tmp> --duration-ms <d>`.
  void RunProfile(int duration_ms = 2000, int interval_ms = 200);

  /// Invoke `polyc --emit call-graph <tmp.cgjson>` for the given source
  /// file.  Re-populates the call-graph model and emits
  /// @ref CallGraphLoaded.  When @p source_path is empty, an attempt is
  /// made to load @p out_path directly from disk if it already exists.
  void EmitAndLoadCallGraph(const QString &source_path, const QStringList &extra_args = {});

  /// Load a previously generated call-graph JSON document directly.
  bool LoadCallGraphJson(const QString &path);

  /// Load a previously generated profile JSON document directly.
  bool LoadProfileJson(const QString &path);

  // Streaming -----------------------------------------------------------

  /// Spawn `polyrt profile --stream <pipe>` and incrementally consume
  /// NDJSON records, repainting the timeline / flame models at ≥5 Hz.
  void StartProfileStream(int interval_ms = 200);
  void StopProfileStream();
  bool IsStreaming() const;

signals:
  void BenchmarkFinished(bool ok, const QString &message);
  void ProfileFinished(bool ok, const QString &message);
  void CallGraphLoaded(int node_count, int edge_count);
  void StreamSampleReceived(int total_samples);
  void ToolErrorOutput(const QString &tool, const QString &line);

private slots:
  void OnBenchmarkFinished(int exit_code, QProcess::ExitStatus status);
  void OnProfileFinished(int exit_code, QProcess::ExitStatus status);
  void OnCallGraphFinished(int exit_code, QProcess::ExitStatus status);
  void OnStreamReadyRead();
  void OnStreamFinished(int exit_code, QProcess::ExitStatus status);

private:
  bool ParseProfileDocument(const QJsonDocument &doc, QString *err);
  bool ParseCallGraphDocument(const QJsonDocument &doc, QString *err);
  void HandleStreamLine(const QString &line);

  /// Build a flame tree from a profile sample list.  The returned root is
  /// always non-null (a synthetic "<root>" frame).
  std::unique_ptr<FlameNode> BuildFlameTreeFromSamples(const QJsonObject &doc);

  QString ResolveSiblingTool(const QString &cached, const QString &executable_name) const;
  QString MakeTempPath(const QString &suffix) const;

  // Models — owned, reparented to @c this so Qt destroys them with us.
  FlameTreeModel *flame_{nullptr};
  CallGraphModel *call_graph_{nullptr};
  TimelineModel *timeline_{nullptr};

  // Active processes (one of each kind at most).
  QProcess *bench_process_{nullptr};
  QProcess *profile_process_{nullptr};
  QProcess *callgraph_process_{nullptr};
  QProcess *stream_process_{nullptr};
  QString stream_buffer_;
  QString last_bench_json_path_;
  QString last_profile_json_path_;
  QString last_callgraph_json_path_;

  QString polyrt_path_;
  QString polybench_path_;
  QString polyc_path_;

  std::uint64_t stream_sample_count_{0};
};

} // namespace polyglot::tools::ui
