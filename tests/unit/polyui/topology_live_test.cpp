/**
 * @file     topology_live_test.cpp
 * @brief    Unit tests for `LiveTopologyTracker`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/topology_live/topology_live.h"

using namespace polyglot::tools::ui::topology;
using namespace std::chrono_literals;

namespace {
TopologyGraph MakeGraph() {
  TopologyGraph g;
  g.AddNode({"a", "fn a", "a.ploy", 1, {"function"}});
  g.AddNode({"b", "fn b", "b.ploy", 1, {"function"}});
  g.AddNode({"c", "fn c", "c.ploy", 1, {"function"}});
  g.AddNode({"d", "fn d", "d.ploy", 1, {"function"}});
  g.AddEdge({"a", "b", "calls"});
  g.AddEdge({"b", "c", "calls"});
  g.AddEdge({"c", "d", "calls"});
  return g;
}
}  // namespace

TEST_CASE("Neighbourhood respects radius in either direction",
          "[polyui][topology]") {
  auto g = MakeGraph();
  auto sub = g.Neighbourhood("b", 1);
  CHECK(sub.nodes().size() == 3);     // b + a (incoming) + c (outgoing).
  CHECK(sub.edges().size() == 2);
  CHECK(g.Neighbourhood("b", 0).nodes().size() == 1);
  CHECK(g.Neighbourhood("missing", 1).nodes().empty());
}

TEST_CASE("FocusOn by symbol returns the symbol's neighbourhood",
          "[polyui][topology]") {
  LiveTopologyTracker tracker(MakeGraph());
  auto v = tracker.FocusOn("b.ploy", "b", 1);
  CHECK(v.nodes().size() == 3);
  CHECK(tracker.current_symbol() == "b");
  CHECK(tracker.current_file() == "b.ploy");
}

TEST_CASE("FocusOn falls back to file anchor when symbol unknown",
          "[polyui][topology]") {
  LiveTopologyTracker tracker(MakeGraph());
  auto v = tracker.FocusOn("c.ploy", "", 1);
  // c plus its incoming b and outgoing d.
  CHECK(v.nodes().size() == 3);
}

TEST_CASE("Debounced rebuild only fires after quiet interval",
          "[polyui][topology]") {
  LiveTopologyTracker tracker(MakeGraph(), 100ms);
  auto t0 = std::chrono::steady_clock::now();
  tracker.NotifyEdit(t0);
  CHECK_FALSE(tracker.ShouldRebuild(t0 + 50ms));
  CHECK(tracker.ShouldRebuild(t0 + 150ms));
  // Rebuild consumes the pending state.
  CHECK_FALSE(tracker.ShouldRebuild(t0 + 200ms));
}

TEST_CASE("NodeSource returns source position or nullopt",
          "[polyui][topology]") {
  LiveTopologyTracker tracker(MakeGraph());
  auto src = tracker.NodeSource("a");
  REQUIRE(src);
  CHECK(src->first == "a.ploy");
  CHECK(src->second == 1);
  CHECK_FALSE(tracker.NodeSource("missing"));
}
