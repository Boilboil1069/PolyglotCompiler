/**
 * @file     call_graph_model_test.cpp
 * @brief    Unit tests for CallGraphModel (id lookup, edges, DFS, runtime overlay)
 *
 * @ingroup  Tests / Unit / Tools
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include <catch2/catch_test_macros.hpp>

#include <QHash>
#include <QString>
#include <QStringList>
#include <algorithm>

#include "tools/ui/common/include/data_models/call_graph_model.h"

using polyglot::tools::ui::CallGraphEdge;
using polyglot::tools::ui::CallGraphModel;
using polyglot::tools::ui::CallGraphNode;

namespace {

CallGraphNode MakeNode(const char *id, const char *lang = "ploy") {
  CallGraphNode n;
  n.id = QString::fromLatin1(id);
  n.name = QString::fromLatin1(id);
  n.language = QString::fromLatin1(lang);
  return n;
}

CallGraphEdge MakeEdge(const char *from, const char *to) {
  CallGraphEdge e;
  e.from = QString::fromLatin1(from);
  e.to = QString::fromLatin1(to);
  return e;
}

} // namespace

TEST_CASE("CallGraphModel populates rows and id lookup", "[ui][callgraph]") {
  CallGraphModel model;
  std::vector<CallGraphNode> nodes{MakeNode("a"), MakeNode("b", "python"),
                                   MakeNode("c")};
  std::vector<CallGraphEdge> edges{MakeEdge("a", "b"), MakeEdge("b", "c"),
                                   MakeEdge("a", "c")};
  model.Replace(std::move(nodes), std::move(edges));

  REQUIRE(model.rowCount(QModelIndex()) == 3);
  REQUIRE(model.RowForId("a") == 0);
  REQUIRE(model.RowForId("b") == 1);
  REQUIRE(model.RowForId("c") == 2);
  REQUIRE(model.RowForId("missing") == -1);
}

TEST_CASE("CallGraphModel exposes direct callers and callees",
          "[ui][callgraph][adjacency]") {
  CallGraphModel model;
  model.Replace({MakeNode("a"), MakeNode("b"), MakeNode("c"), MakeNode("d")},
                {MakeEdge("a", "b"), MakeEdge("a", "c"),
                 MakeEdge("b", "d"), MakeEdge("c", "d")});

  auto callers_of_d = model.DirectCallers("d");
  std::sort(callers_of_d.begin(), callers_of_d.end());
  REQUIRE(callers_of_d == QStringList{"b", "c"});

  auto callees_of_a = model.DirectCallees("a");
  std::sort(callees_of_a.begin(), callees_of_a.end());
  REQUIRE(callees_of_a == QStringList{"b", "c"});

  REQUIRE(model.DirectCallees("d").isEmpty());
  REQUIRE(model.DirectCallers("a").isEmpty());
}

TEST_CASE("CallGraphModel finds bounded DFS paths", "[ui][callgraph][dfs]") {
  CallGraphModel model;
  model.Replace({MakeNode("a"), MakeNode("b"), MakeNode("c"), MakeNode("d")},
                {MakeEdge("a", "b"), MakeEdge("a", "c"),
                 MakeEdge("b", "d"), MakeEdge("c", "d")});

  auto paths = model.FindPaths("a", "d", /*max_depth=*/8);
  REQUIRE(paths.size() == 2);
  bool saw_b = false;
  bool saw_c = false;
  for (const auto &p : paths) {
    REQUIRE(p.front() == "a");
    REQUIRE(p.back() == "d");
    if (p.contains(QStringLiteral("b"))) saw_b = true;
    if (p.contains(QStringLiteral("c"))) saw_c = true;
  }
  REQUIRE(saw_b);
  REQUIRE(saw_c);
}

TEST_CASE("CallGraphModel DFS avoids infinite loops on cycles",
          "[ui][callgraph][cycles]") {
  CallGraphModel model;
  model.Replace({MakeNode("a"), MakeNode("b"), MakeNode("c")},
                {MakeEdge("a", "b"), MakeEdge("b", "c"), MakeEdge("c", "a"),
                 MakeEdge("b", "a")});

  // Bounded DFS — must terminate without exhausting the stack.
  auto paths = model.FindPaths("a", "c", /*max_depth=*/16);
  REQUIRE_FALSE(paths.empty());
  for (const auto &p : paths) {
    REQUIRE(p.front() == "a");
    REQUIRE(p.back() == "c");
    // No id may repeat inside a single DFS path.
    QStringList sorted = p;
    std::sort(sorted.begin(), sorted.end());
    auto unique_end = std::unique(sorted.begin(), sorted.end());
    REQUIRE(unique_end == sorted.end());
  }
}

TEST_CASE("CallGraphModel ApplyRuntimeCounts updates matched rows only",
          "[ui][callgraph][overlay]") {
  CallGraphModel model;
  model.Replace({MakeNode("a"), MakeNode("b")},
                {MakeEdge("a", "b")});

  QHash<QString, std::uint64_t> calls;
  calls.insert(QStringLiteral("a"), 7);
  calls.insert(QStringLiteral("missing"), 99);
  QHash<QString, std::uint64_t> incl;
  incl.insert(QStringLiteral("a"), 1'234'000);
  model.ApplyRuntimeCounts(calls, incl);

  REQUIRE(model.Nodes().at(0).calls == 7);
  REQUIRE(model.Nodes().at(0).inclusive_ns == 1'234'000);
  REQUIRE(model.Nodes().at(1).calls == 0);
  REQUIRE(model.Nodes().at(1).inclusive_ns == 0);
}

TEST_CASE("CallGraphModel Clear empties state", "[ui][callgraph][clear]") {
  CallGraphModel model;
  model.Replace({MakeNode("a"), MakeNode("b")}, {MakeEdge("a", "b")});
  REQUIRE(model.rowCount(QModelIndex()) == 2);
  model.Clear();
  REQUIRE(model.rowCount(QModelIndex()) == 0);
  REQUIRE(model.RowForId("a") == -1);
  REQUIRE(model.Edges().empty());
}
