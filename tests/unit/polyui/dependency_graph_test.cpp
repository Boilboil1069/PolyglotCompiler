/**
 * @file     dependency_graph_test.cpp
 * @brief    Unit tests for `DependencyGraph`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/packages/dependency_graph.h"

using namespace polyglot::tools::ui::packages;

namespace {

DependencyGraph BuildSample() {
  DependencyGraph g;
  g.AddNode({"app@0.0.0", "app", "0.0.0", true});
  g.AddNode({"serde@1.0.197", "serde", "1.0.197", false});
  g.AddNode({"tokio@1.36.0", "tokio", "1.36.0", false});
  g.AddNode({"serde@1.0.150", "serde", "1.0.150", false});
  g.AddEdge({"app@0.0.0", "serde@1.0.197", "^1.0"});
  g.AddEdge({"app@0.0.0", "tokio@1.36.0", "^1.36"});
  g.AddEdge({"tokio@1.36.0", "serde@1.0.150", "^1.0"});
  g.MarkRoot("app@0.0.0");
  return g;
}

}  // namespace

TEST_CASE("DependencyGraph TreeView walks marked roots",
          "[polyui][packages][graph]") {
  auto g = BuildSample();
  auto tv = g.TreeView();
  REQUIRE(tv.size() == 4);
  CHECK(tv[0].second->name == "app");
  CHECK(tv[0].first == 0);
  CHECK(tv[1].first == 1);
}

TEST_CASE("DependencyGraph reports duplicate-version conflicts",
          "[polyui][packages][graph]") {
  auto g = BuildSample();
  auto c = g.Conflicts();
  REQUIRE(c.size() == 1);
  CHECK(c[0].name == "serde");
  CHECK(c[0].versions.size() == 2);
}

TEST_CASE("DependencyGraph SVG export contains nodes and edges",
          "[polyui][packages][graph]") {
  auto g = BuildSample();
  auto svg = g.ExportSvg();
  CHECK(svg.find("<svg ") == 0);
  CHECK(svg.find("serde 1.0.197") != std::string::npos);
  CHECK(svg.find("<line ") != std::string::npos);
  CHECK(svg.find("class=\"conflict\"") != std::string::npos);
  CHECK(svg.find("class=\"root\"") != std::string::npos);
}
