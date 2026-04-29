/**
 * @file     profile_session_test.cpp
 * @brief    Unit tests for ProfileSession's JSON loaders
 *
 * @ingroup  Tests / Unit / Tools
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>

#include "tools/ui/common/include/data_models/call_graph_model.h"
#include "tools/ui/common/include/data_models/flame_node.h"
#include "tools/ui/common/include/data_models/timeline_model.h"
#include "tools/ui/common/include/profile_session.h"

using polyglot::tools::ui::CallGraphModel;
using polyglot::tools::ui::FlameTreeModel;
using polyglot::tools::ui::ProfileSession;
using polyglot::tools::ui::TimelineModel;

namespace {

QApplication &GetOrCreateApp() {
  if (QApplication::instance()) {
    return *static_cast<QApplication *>(QApplication::instance());
  }
  static int argc = 1;
  static const char *argv[] = {"profile_session_test", nullptr};
  static QApplication app(argc, const_cast<char **>(argv));
  return app;
}

QString WriteTempFile(const QString &basename, const QString &payload) {
  const QString path = QDir::tempPath() + QStringLiteral("/") + basename;
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
  QTextStream(&f) << payload;
  f.close();
  return path;
}

} // namespace

TEST_CASE("ProfileSession loads call-graph JSON into model",
          "[ui][profile_session][callgraph]") {
  GetOrCreateApp();
  ProfileSession session;

  const QString payload = R"({
    "schema": "polyglot.callgraph.v1",
    "nodes": [
      {"id": "main",  "name": "main",  "language": "ploy",   "file": "a.ploy", "line": 1, "block_count": 3},
      {"id": "calc",  "name": "calc",  "language": "python", "file": "a.py",   "line": 7},
      {"id": "stub",  "name": "stub",  "language": "cpp",    "is_bridge_stub": true}
    ],
    "edges": [
      {"from": "main", "to": "calc"},
      {"from": "calc", "to": "stub"}
    ]
  })";
  const QString path = WriteTempFile(QStringLiteral("polyglot_cg_test.json"), payload);

  REQUIRE(session.LoadCallGraphJson(path));
  CallGraphModel *model = session.CallGraph();
  REQUIRE(model != nullptr);
  REQUIRE(model->Nodes().size() == 3);
  REQUIRE(model->Edges().size() == 2);
  REQUIRE(model->RowForId("main") == 0);
  REQUIRE(model->Nodes().at(2).is_bridge_stub);
  // Edge language back-fill from node table.
  REQUIRE(model->Edges().at(0).from_language == QStringLiteral("ploy"));
  REQUIRE(model->Edges().at(0).to_language == QStringLiteral("python"));

  QFile::remove(path);
}

TEST_CASE("ProfileSession loads profile JSON and builds flame tree",
          "[ui][profile_session][profile]") {
  GetOrCreateApp();
  ProfileSession session;

  const QString payload = R"({
    "schema": "polyglot.profile.v1",
    "samples": [
      {"function": "main", "language": "ploy",   "thread": "T0", "timestamp_ns": 0,    "window_ns": 1000, "calls": 1},
      {"function": "calc", "language": "python", "thread": "T0", "timestamp_ns": 1000, "window_ns": 2000, "calls": 5}
    ],
    "frames": [
      {"language": "ploy",   "stack": ["main"],          "inclusive_ns": 5000, "self_ns": 1000, "calls": 1},
      {"language": "python", "stack": ["main", "calc"],  "inclusive_ns": 4000, "self_ns": 4000, "calls": 5}
    ],
    "hotspots": [
      {"function": "calc", "language": "python", "calls": 5, "inclusive_ns": 4000}
    ]
  })";
  const QString path = WriteTempFile(QStringLiteral("polyglot_profile_test.json"), payload);

  REQUIRE(session.LoadProfileJson(path));
  TimelineModel *tl = session.Timeline();
  FlameTreeModel *flame = session.Flame();
  REQUIRE(tl != nullptr);
  REQUIRE(flame != nullptr);

  REQUIRE(tl->rowCount(QModelIndex()) == 2);
  REQUIRE(tl->Lanes().size() == 1);
  REQUIRE(tl->LaneIndex(QStringLiteral("T0")) == 0);

  const auto *root = flame->Root();
  REQUIRE(root != nullptr);
  REQUIRE(root->children.size() == 1);
  REQUIRE(root->children.front()->function == QStringLiteral("main"));
  REQUIRE(root->children.front()->children.size() == 1);
  REQUIRE(root->children.front()->children.front()->function ==
          QStringLiteral("calc"));

  QFile::remove(path);
}

TEST_CASE("ProfileSession reports failure on malformed JSON",
          "[ui][profile_session][error]") {
  GetOrCreateApp();
  ProfileSession session;
  const QString path =
      WriteTempFile(QStringLiteral("polyglot_bad_test.json"),
                    QStringLiteral("not-json-at-all"));
  REQUIRE_FALSE(session.LoadProfileJson(path));
  REQUIRE_FALSE(session.LoadCallGraphJson(path));
  QFile::remove(path);
}
