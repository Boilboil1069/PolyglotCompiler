/**
 * @file     profile_session.cpp
 * @brief    ProfileSession implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "tools/ui/common/include/profile_session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <algorithm>

#include "tools/ui/common/include/data_models/call_graph_model.h"
#include "tools/ui/common/include/data_models/flame_node.h"
#include "tools/ui/common/include/data_models/timeline_model.h"

namespace polyglot::tools::ui {

namespace {

#if defined(_WIN32)
constexpr const char *kExecSuffix = ".exe";
#else
constexpr const char *kExecSuffix = "";
#endif

} // namespace

// ============================================================================
// Construction / accessors
// ============================================================================

ProfileSession::ProfileSession(QObject *parent) : QObject(parent) {
  flame_ = new FlameTreeModel(this);
  call_graph_ = new CallGraphModel(this);
  timeline_ = new TimelineModel(this);
}

ProfileSession::~ProfileSession() { StopProfileStream(); }

QString ProfileSession::PolyrtPath() const {
  return ResolveSiblingTool(polyrt_path_, QString("polyrt") + kExecSuffix);
}

QString ProfileSession::PolybenchPath() const {
  return ResolveSiblingTool(polybench_path_, QString("polybench") + kExecSuffix);
}

QString ProfileSession::PolycPath() const {
  return ResolveSiblingTool(polyc_path_, QString("polyc") + kExecSuffix);
}

bool ProfileSession::IsRunning() const {
  auto active = [](QProcess *p) {
    return p && p->state() != QProcess::NotRunning;
  };
  return active(bench_process_) || active(profile_process_) ||
         active(callgraph_process_) || active(stream_process_);
}

bool ProfileSession::IsStreaming() const {
  return stream_process_ && stream_process_->state() != QProcess::NotRunning;
}

QString ProfileSession::ResolveSiblingTool(const QString &cached,
                                           const QString &executable_name) const {
  if (!cached.isEmpty() && QFileInfo(cached).exists()) {
    return cached;
  }
  // Look next to the running IDE binary first.
  const QString app_dir = QCoreApplication::applicationDirPath();
  const QString candidate = QDir(app_dir).filePath(executable_name);
  if (QFileInfo::exists(candidate)) {
    return candidate;
  }
  return executable_name; // Rely on PATH.
}

QString ProfileSession::MakeTempPath(const QString &suffix) const {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const QString name = QStringLiteral("polyui-%1.%2")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                           .arg(suffix);
  return QDir(base).filePath(name);
}

// ============================================================================
// One-shot benchmark (polybench --json)
// ============================================================================

void ProfileSession::RunBenchmark() {
  if (bench_process_ && bench_process_->state() != QProcess::NotRunning) {
    return;
  }
  delete bench_process_;
  bench_process_ = new QProcess(this);
  last_bench_json_path_ = MakeTempPath(QStringLiteral("bench.json"));

  QStringList args;
  QString program;
  const QString polybench = PolybenchPath();
  if (QFileInfo::exists(polybench)) {
    program = polybench;
    args << QStringLiteral("--json") << last_bench_json_path_;
  } else {
    program = PolyrtPath();
    args << QStringLiteral("bench") << QStringLiteral("--json")
         << QStringLiteral("--out=") + last_bench_json_path_;
  }

  connect(bench_process_, &QProcess::readyReadStandardError, this, [this]() {
    if (!bench_process_) {
      return;
    }
    const QString text = QString::fromUtf8(bench_process_->readAllStandardError());
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
      emit ToolErrorOutput(QStringLiteral("bench"), line);
    }
  });
  connect(bench_process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &ProfileSession::OnBenchmarkFinished);
  bench_process_->start(program, args);
}

void ProfileSession::OnBenchmarkFinished(int exit_code, QProcess::ExitStatus status) {
  const bool ok = (status == QProcess::NormalExit && exit_code == 0);
  QString message;
  if (ok) {
    if (LoadProfileJson(last_bench_json_path_)) {
      message = tr("Benchmark complete (%1 events).").arg(timeline_->rowCount({}));
    } else {
      message = tr("Benchmark exited successfully but JSON could not be parsed.");
    }
  } else {
    message = tr("Benchmark process exited with code %1.").arg(exit_code);
  }
  emit BenchmarkFinished(ok, message);
}

// ============================================================================
// One-shot profile (polyrt profile --json)
// ============================================================================

void ProfileSession::RunProfile(int duration_ms, int interval_ms) {
  if (profile_process_ && profile_process_->state() != QProcess::NotRunning) {
    return;
  }
  delete profile_process_;
  profile_process_ = new QProcess(this);
  last_profile_json_path_ = MakeTempPath(QStringLiteral("profile.json"));
  QStringList args = {QStringLiteral("profile"),
                      QStringLiteral("--json"),
                      QStringLiteral("--out=") + last_profile_json_path_,
                      QStringLiteral("--duration-ms=") + QString::number(duration_ms),
                      QStringLiteral("--interval-ms=") + QString::number(interval_ms),
                      QStringLiteral("--enable")};
  connect(profile_process_, &QProcess::readyReadStandardError, this, [this]() {
    if (!profile_process_) {
      return;
    }
    const QString text = QString::fromUtf8(profile_process_->readAllStandardError());
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
      emit ToolErrorOutput(QStringLiteral("profile"), line);
    }
  });
  connect(profile_process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &ProfileSession::OnProfileFinished);
  profile_process_->start(PolyrtPath(), args);
}

void ProfileSession::OnProfileFinished(int exit_code, QProcess::ExitStatus status) {
  const bool ok = (status == QProcess::NormalExit && exit_code == 0);
  QString message;
  if (ok && LoadProfileJson(last_profile_json_path_)) {
    message = tr("Profile complete (%1 events).").arg(timeline_->rowCount({}));
  } else {
    message = tr("Profile process exited with code %1.").arg(exit_code);
  }
  emit ProfileFinished(ok, message);
}

// ============================================================================
// Streaming profile (polyrt profile --stream)
// ============================================================================

void ProfileSession::StartProfileStream(int interval_ms) {
  StopProfileStream();
  stream_process_ = new QProcess(this);
  stream_buffer_.clear();
  stream_sample_count_ = 0;
  const QString stream_path = MakeTempPath(QStringLiteral("stream.ndjson"));
  QStringList args = {QStringLiteral("profile"),
                      QStringLiteral("--json"),
                      QStringLiteral("--stream=") + stream_path,
                      QStringLiteral("--interval-ms=") + QString::number(interval_ms),
                      QStringLiteral("--duration-ms=0"), // run until terminated
                      QStringLiteral("--enable")};
  connect(stream_process_, &QProcess::readyReadStandardOutput, this,
          &ProfileSession::OnStreamReadyRead);
  connect(stream_process_, &QProcess::readyReadStandardError, this, [this]() {
    if (!stream_process_) {
      return;
    }
    const QString text = QString::fromUtf8(stream_process_->readAllStandardError());
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
      emit ToolErrorOutput(QStringLiteral("profile-stream"), line);
    }
  });
  connect(stream_process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &ProfileSession::OnStreamFinished);
  stream_process_->start(PolyrtPath(), args);
}

void ProfileSession::StopProfileStream() {
  if (!stream_process_) {
    return;
  }
  if (stream_process_->state() != QProcess::NotRunning) {
    stream_process_->terminate();
    if (!stream_process_->waitForFinished(1000)) {
      stream_process_->kill();
      stream_process_->waitForFinished(500);
    }
  }
  stream_process_->deleteLater();
  stream_process_ = nullptr;
}

void ProfileSession::OnStreamReadyRead() {
  if (!stream_process_) {
    return;
  }
  stream_buffer_ += QString::fromUtf8(stream_process_->readAllStandardOutput());
  while (true) {
    const int newline = stream_buffer_.indexOf('\n');
    if (newline < 0) {
      break;
    }
    const QString line = stream_buffer_.left(newline).trimmed();
    stream_buffer_.remove(0, newline + 1);
    if (!line.isEmpty()) {
      HandleStreamLine(line);
    }
  }
}

void ProfileSession::OnStreamFinished(int /*exit_code*/, QProcess::ExitStatus /*status*/) {
  emit ToolErrorOutput(QStringLiteral("profile-stream"),
                       tr("Stream process exited."));
}

void ProfileSession::HandleStreamLine(const QString &line) {
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return;
  }
  const QJsonObject obj = doc.object();
  TimelineEvent event;
  event.function = obj.value(QStringLiteral("function")).toString(QStringLiteral("<sample>"));
  event.language = obj.value(QStringLiteral("language")).toString(QStringLiteral("ploy"));
  event.thread = obj.value(QStringLiteral("thread")).toString(QStringLiteral("main"));
  event.start_ns =
      static_cast<std::uint64_t>(obj.value(QStringLiteral("timestamp_ns")).toDouble(0.0));
  event.duration_ns =
      static_cast<std::uint64_t>(obj.value(QStringLiteral("window_ns")).toDouble(0.0));
  event.calls = static_cast<std::uint64_t>(obj.value(QStringLiteral("calls")).toDouble(0.0));
  timeline_->Append(event);
  ++stream_sample_count_;
  emit StreamSampleReceived(static_cast<int>(stream_sample_count_));
}

// ============================================================================
// Call-graph emission (polyc --emit call-graph)
// ============================================================================

void ProfileSession::EmitAndLoadCallGraph(const QString &source_path,
                                          const QStringList &extra_args) {
  if (callgraph_process_ && callgraph_process_->state() != QProcess::NotRunning) {
    return;
  }
  delete callgraph_process_;
  callgraph_process_ = new QProcess(this);
  last_callgraph_json_path_ = MakeTempPath(QStringLiteral("callgraph.cgjson"));

  QStringList args = extra_args;
  args << QStringLiteral("--emit=call-graph:") + last_callgraph_json_path_;
  if (!source_path.isEmpty()) {
    args << source_path;
  }

  connect(callgraph_process_, &QProcess::readyReadStandardError, this, [this]() {
    if (!callgraph_process_) {
      return;
    }
    const QString text = QString::fromUtf8(callgraph_process_->readAllStandardError());
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
      emit ToolErrorOutput(QStringLiteral("polyc"), line);
    }
  });
  connect(callgraph_process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &ProfileSession::OnCallGraphFinished);
  callgraph_process_->start(PolycPath(), args);
}

void ProfileSession::OnCallGraphFinished(int exit_code, QProcess::ExitStatus status) {
  const bool ok = (status == QProcess::NormalExit && exit_code == 0);
  if (!ok) {
    emit CallGraphLoaded(0, 0);
    return;
  }
  LoadCallGraphJson(last_callgraph_json_path_);
}

bool ProfileSession::LoadCallGraphJson(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    emit CallGraphLoaded(0, 0);
    return false;
  }
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError) {
    emit CallGraphLoaded(0, 0);
    return false;
  }
  QString parse_err;
  if (!ParseCallGraphDocument(doc, &parse_err)) {
    emit ToolErrorOutput(QStringLiteral("call-graph"), parse_err);
    emit CallGraphLoaded(0, 0);
    return false;
  }
  emit CallGraphLoaded(static_cast<int>(call_graph_->Nodes().size()),
                        static_cast<int>(call_graph_->Edges().size()));
  return true;
}

bool ProfileSession::LoadProfileJson(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError) {
    return false;
  }
  QString parse_err;
  return ParseProfileDocument(doc, &parse_err);
}

// ============================================================================
// JSON parsing
// ============================================================================

bool ProfileSession::ParseCallGraphDocument(const QJsonDocument &doc, QString *err) {
  if (!doc.isObject()) {
    if (err) {
      *err = QStringLiteral("Call-graph document root is not an object.");
    }
    return false;
  }
  const QJsonObject obj = doc.object();
  std::vector<CallGraphNode> nodes;
  std::vector<CallGraphEdge> edges;

  const QJsonArray nodes_array = obj.value(QStringLiteral("nodes")).toArray();
  nodes.reserve(static_cast<std::size_t>(nodes_array.size()));
  for (const QJsonValue &v : nodes_array) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject nobj = v.toObject();
    CallGraphNode node;
    node.id = nobj.value(QStringLiteral("id")).toString();
    node.name = nobj.value(QStringLiteral("name")).toString();
    node.language = nobj.value(QStringLiteral("language")).toString();
    node.file = nobj.value(QStringLiteral("file")).toString();
    node.line = nobj.value(QStringLiteral("line")).toInt(0);
    node.is_external = nobj.value(QStringLiteral("is_external")).toBool(false);
    node.is_bridge_stub = nobj.value(QStringLiteral("is_bridge_stub")).toBool(false);
    node.block_count = nobj.value(QStringLiteral("block_count")).toInt(0);
    if (node.id.isEmpty()) {
      node.id = node.name;
    }
    nodes.push_back(std::move(node));
  }

  const QJsonArray edges_array = obj.value(QStringLiteral("edges")).toArray();
  edges.reserve(static_cast<std::size_t>(edges_array.size()));
  for (const QJsonValue &v : edges_array) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject eobj = v.toObject();
    CallGraphEdge edge;
    edge.from = eobj.value(QStringLiteral("from")).toString();
    edge.to = eobj.value(QStringLiteral("to")).toString();
    if (edge.from.isEmpty() || edge.to.isEmpty()) {
      continue;
    }
    edge.from_language = eobj.value(QStringLiteral("from_language")).toString();
    edge.to_language = eobj.value(QStringLiteral("to_language")).toString();
    edges.push_back(std::move(edge));
  }

  // Backfill from/to language from node table when not present in edge.
  QHash<QString, QString> id_to_lang;
  id_to_lang.reserve(static_cast<int>(nodes.size()));
  for (const auto &n : nodes) {
    id_to_lang.insert(n.id, n.language);
  }
  for (auto &edge : edges) {
    if (edge.from_language.isEmpty()) {
      edge.from_language = id_to_lang.value(edge.from);
    }
    if (edge.to_language.isEmpty()) {
      edge.to_language = id_to_lang.value(edge.to);
    }
  }

  call_graph_->Replace(std::move(nodes), std::move(edges));
  return true;
}

bool ProfileSession::ParseProfileDocument(const QJsonDocument &doc, QString *err) {
  if (!doc.isObject()) {
    if (err) {
      *err = QStringLiteral("Profile document root is not an object.");
    }
    return false;
  }
  const QJsonObject obj = doc.object();

  // Timeline events --------------------------------------------------------
  std::vector<TimelineEvent> events;
  const QJsonArray samples = obj.value(QStringLiteral("samples")).toArray();
  events.reserve(static_cast<std::size_t>(samples.size()));
  for (const QJsonValue &v : samples) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject sobj = v.toObject();
    TimelineEvent event;
    event.function = sobj.value(QStringLiteral("function"))
                          .toString(QStringLiteral("<sample>"));
    event.language = sobj.value(QStringLiteral("language"))
                          .toString(QStringLiteral("ploy"));
    event.thread = sobj.value(QStringLiteral("thread"))
                        .toString(QStringLiteral("main"));
    event.start_ns =
        static_cast<std::uint64_t>(sobj.value(QStringLiteral("timestamp_ns")).toDouble(0.0));
    event.duration_ns =
        static_cast<std::uint64_t>(sobj.value(QStringLiteral("window_ns")).toDouble(0.0));
    event.calls =
        static_cast<std::uint64_t>(sobj.value(QStringLiteral("calls")).toDouble(0.0));
    event.is_bridge = sobj.value(QStringLiteral("is_bridge")).toBool(false);
    events.push_back(event);
  }
  timeline_->Replace(std::move(events));

  // Flame tree -------------------------------------------------------------
  flame_->SetRoot(BuildFlameTreeFromSamples(obj));

  // Apply runtime counts onto the call-graph (best-effort — only when the
  // call-graph has already been populated by EmitAndLoadCallGraph()).
  QHash<QString, std::uint64_t> call_counts;
  QHash<QString, std::uint64_t> inclusive_ns;
  const QJsonArray hot = obj.value(QStringLiteral("hotspots")).toArray();
  for (const QJsonValue &v : hot) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject hobj = v.toObject();
    const QString key = hobj.value(QStringLiteral("function")).toString();
    if (key.isEmpty()) {
      continue;
    }
    call_counts.insert(key, static_cast<std::uint64_t>(
                                 hobj.value(QStringLiteral("calls")).toDouble(0.0)));
    inclusive_ns.insert(key,
                        static_cast<std::uint64_t>(
                            hobj.value(QStringLiteral("inclusive_ns")).toDouble(0.0)));
  }
  if (!call_counts.isEmpty() || !inclusive_ns.isEmpty()) {
    call_graph_->ApplyRuntimeCounts(call_counts, inclusive_ns);
  }
  return true;
}

std::unique_ptr<FlameNode>
ProfileSession::BuildFlameTreeFromSamples(const QJsonObject &doc) {
  auto root = std::make_unique<FlameNode>();
  root->function = QStringLiteral("<root>");
  root->language = QStringLiteral("ploy");

  const QJsonArray frames = doc.value(QStringLiteral("frames")).toArray();
  if (frames.isEmpty()) {
    // Synthesise a flat tree from hotspots when explicit frames are absent.
    const QJsonArray hot = doc.value(QStringLiteral("hotspots")).toArray();
    for (const QJsonValue &v : hot) {
      if (!v.isObject()) {
        continue;
      }
      const QJsonObject hobj = v.toObject();
      auto child = std::make_unique<FlameNode>();
      child->function = hobj.value(QStringLiteral("function")).toString();
      child->language =
          hobj.value(QStringLiteral("language")).toString(QStringLiteral("ploy"));
      child->inclusive_ns = static_cast<std::uint64_t>(
          hobj.value(QStringLiteral("inclusive_ns")).toDouble(0.0));
      child->self_ns = static_cast<std::uint64_t>(
          hobj.value(QStringLiteral("self_ns")).toDouble(0.0));
      child->calls = static_cast<std::uint64_t>(
          hobj.value(QStringLiteral("calls")).toDouble(0.0));
      child->parent = root.get();
      root->inclusive_ns += child->inclusive_ns;
      root->calls += child->calls;
      root->children.push_back(std::move(child));
    }
    return root;
  }

  // Each "frame" entry: {function, language, inclusive_ns, self_ns, calls,
  // stack:[fn0, fn1, ...]}.  We aggregate into a tree keyed by stack prefix.
  for (const QJsonValue &v : frames) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject fobj = v.toObject();
    const QJsonArray stack = fobj.value(QStringLiteral("stack")).toArray();
    if (stack.isEmpty()) {
      continue;
    }
    FlameNode *cursor = root.get();
    for (int i = 0; i < stack.size(); ++i) {
      const QString frame_name = stack[i].toString();
      FlameNode *match = nullptr;
      for (auto &child : cursor->children) {
        if (child->function == frame_name) {
          match = child.get();
          break;
        }
      }
      if (!match) {
        auto child = std::make_unique<FlameNode>();
        child->function = frame_name;
        child->language =
            fobj.value(QStringLiteral("language")).toString(QStringLiteral("ploy"));
        child->parent = cursor;
        match = child.get();
        cursor->children.push_back(std::move(child));
      }
      cursor = match;
    }
    cursor->inclusive_ns += static_cast<std::uint64_t>(
        fobj.value(QStringLiteral("inclusive_ns")).toDouble(0.0));
    cursor->self_ns += static_cast<std::uint64_t>(
        fobj.value(QStringLiteral("self_ns")).toDouble(0.0));
    cursor->calls += static_cast<std::uint64_t>(
        fobj.value(QStringLiteral("calls")).toDouble(0.0));
  }

  // Roll up inclusive time / call counts onto the synthetic root for
  // convenience to the painter.
  for (const auto &child : root->children) {
    root->inclusive_ns += child->inclusive_ns;
    root->calls += child->calls;
  }
  return root;
}

} // namespace polyglot::tools::ui
