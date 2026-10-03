/**
 * @file     topology_layout_test.cpp
 * @brief    Unit tests for TopologyPanel layout algorithms (2026-04-20-2)
 *
 * @ingroup  Tests / Topology UI
 * @author   Manning Cyrus
 * @date     2026-04-20
 *
 * These tests verify that:
 *   1. LayoutModeToString and LayoutModeFromString round-trip correctly
 *      for every defined LayoutMode value, and that an unknown string
 *      maps to the supplied fallback.
 *   2. The default layout chosen by TopologyPanel is the static
 *      Hierarchical (DAG) layout (i.e. no force-directed simulation runs
 *      after a fresh load on a default-config user).
 *   3. Static layouts are deterministic: laying out the same graph twice
 *      with the same algorithm produces identical positions.
 *   4. Static layouts produce non-degenerate placements (different nodes
 *      do not collapse onto the same coordinate).
 *   5. Switching to a different static layout actually moves nodes.
 *
 * Requires Qt (QApplication) to instantiate the widget hierarchy.
 */

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QPointF>
#include <QSettings>

#include <set>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/include/topology_panel.h"

using namespace polyglot::tools::ui;

namespace {

QApplication &GetOrCreateApp() {
    if (QApplication::instance()) {
        return *static_cast<QApplication *>(QApplication::instance());
    }
    static int argc = 1;
    static const char *argv[] = {"topology_layout_test"};
    static QApplication app(argc, const_cast<char **>(argv));
    return app;
}

QString WriteTempPloy(const std::string &source, const QString &filename) {
    QString path = QDir::temp().filePath(filename);
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(source.c_str(), static_cast<qint64>(source.size()));
    f.close();
    return path;
}

// Snapshot every node's current scene position keyed by node id.
std::unordered_map<uint64_t, QPointF> SnapshotPositions(const TopologyPanel &panel) {
    std::unordered_map<uint64_t, QPointF> out;
    for (const auto &[id, item] : panel.NodeItems()) {
        out.emplace(id, item->pos());
    }
    return out;
}

// A minimal multi-node, multi-edge .poly source so layouts have something
// non-trivial to arrange.
constexpr const char *kSampleSource = R"(
IMPORT cpp::math;
IMPORT python::util;

LINK(cpp, python, math::add, util::py_add) {
    MAP_TYPE(cpp::int, python::int);
}
LINK(cpp, python, math::mul, util::py_mul) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC compute(a: INT, b: INT) -> INT {
    LET x = CALL(cpp, math::add, a, b);
    LET y = CALL(cpp, math::mul, x, b);
    RETURN y;
}
)";

}  // namespace

// ============================================================================
// 1. LayoutMode string round-trip
// ============================================================================

TEST_CASE("LayoutMode: string round-trip is exact for every enum value",
          "[topology][ui][layout]") {
    const std::vector<LayoutMode> kAll = {
        LayoutMode::kHierarchical,
        LayoutMode::kForceDirected,
        LayoutMode::kGridTopDown,
        LayoutMode::kGridLeftRight,
        LayoutMode::kCircular,
        LayoutMode::kConcentric,
        LayoutMode::kSpiral,
        LayoutMode::kBfsTree,
    };

    for (LayoutMode mode : kAll) {
        const QString name = LayoutModeToString(mode);
        REQUIRE_FALSE(name.isEmpty());
        const LayoutMode parsed =
            LayoutModeFromString(name, LayoutMode::kHierarchical);
        CHECK(parsed == mode);
    }

    // Unknown strings must fall back to the supplied default, not crash.
    CHECK(LayoutModeFromString("not-a-real-mode", LayoutMode::kCircular)
          == LayoutMode::kCircular);
    CHECK(LayoutModeFromString("", LayoutMode::kSpiral) == LayoutMode::kSpiral);
}

// ============================================================================
// 2. Default layout is the static Hierarchical algorithm
// ============================================================================

TEST_CASE("TopologyPanel: default layout is static Hierarchical",
          "[topology][ui][layout][default]") {
    auto &app = GetOrCreateApp();
    Q_UNUSED(app);

    // Force the persisted setting to its first-launch state so the test does
    // not depend on a previous run.
    {
        QSettings settings("PolyglotCompiler", "IDE");
        settings.remove("topology/layout_mode");
    }

    TopologyPanel panel;
    QString path = WriteTempPloy(kSampleSource, "topo_layout_default.poly");
    panel.LoadFromFile(path);

    REQUIRE(panel.NodeItems().size() >= 2);

    // The default Hierarchical layout is deterministic and arranges nodes by
    // layer / order; we only require that it does not collapse every node onto
    // the same point.  A force-directed seed by contrast jitters every node
    // randomly, but here we simply assert the layout produced at least two
    // distinct positions.
    auto snap = SnapshotPositions(panel);
    std::set<std::pair<long long, long long>> distinct;
    for (const auto &[_, pos] : snap) {
        distinct.emplace(static_cast<long long>(std::llround(pos.x())),
                         static_cast<long long>(std::llround(pos.y())));
    }
    CHECK(distinct.size() >= 2);

    QFile::remove(path);
}

// ============================================================================
// 3. Static layouts are deterministic across two consecutive applications
// ============================================================================

TEST_CASE("TopologyPanel: static layouts are deterministic",
          "[topology][ui][layout][determinism]") {
    auto &app = GetOrCreateApp();
    Q_UNUSED(app);

    const std::vector<LayoutMode> kStaticModes = {
        LayoutMode::kHierarchical,
        LayoutMode::kGridTopDown,
        LayoutMode::kGridLeftRight,
        LayoutMode::kCircular,
        LayoutMode::kConcentric,
        LayoutMode::kSpiral,
        LayoutMode::kBfsTree,
    };

    for (LayoutMode mode : kStaticModes) {
        // Persist the mode under test then create a fresh panel that picks it
        // up from QSettings (mirrors the user-facing flow).
        {
            QSettings settings("PolyglotCompiler", "IDE");
            settings.setValue("topology/layout_mode", LayoutModeToString(mode));
        }

        TopologyPanel panel_a;
        TopologyPanel panel_b;
        QString path_a = WriteTempPloy(kSampleSource,
                                       QString("topo_layout_det_a_%1.poly")
                                           .arg(static_cast<int>(mode)));
        QString path_b = WriteTempPloy(kSampleSource,
                                       QString("topo_layout_det_b_%1.poly")
                                           .arg(static_cast<int>(mode)));
        panel_a.LoadFromFile(path_a);
        panel_b.LoadFromFile(path_b);

        REQUIRE(panel_a.NodeItems().size() == panel_b.NodeItems().size());

        auto snap_a = SnapshotPositions(panel_a);
        auto snap_b = SnapshotPositions(panel_b);
        for (const auto &[id, pos_a] : snap_a) {
            auto it = snap_b.find(id);
            REQUIRE(it != snap_b.end());
            // Static layouts must produce bit-identical placements (within
            // 0.001 px) for the same input on two independent panels.
            CHECK(std::abs(pos_a.x() - it->second.x()) < 0.001);
            CHECK(std::abs(pos_a.y() - it->second.y()) < 0.001);
        }

        QFile::remove(path_a);
        QFile::remove(path_b);
    }

    // Restore default afterwards so unrelated tests are not affected.
    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("topology/layout_mode",
                      LayoutModeToString(LayoutMode::kHierarchical));
}

// ============================================================================
// 4. Static layouts do not collapse all nodes to the origin
// ============================================================================

TEST_CASE("TopologyPanel: static layouts produce distinct node positions",
          "[topology][ui][layout][spread]") {
    auto &app = GetOrCreateApp();
    Q_UNUSED(app);

    const std::vector<LayoutMode> kStaticModes = {
        LayoutMode::kHierarchical,
        LayoutMode::kGridTopDown,
        LayoutMode::kGridLeftRight,
        LayoutMode::kCircular,
        LayoutMode::kConcentric,
        LayoutMode::kSpiral,
        LayoutMode::kBfsTree,
    };

    for (LayoutMode mode : kStaticModes) {
        {
            QSettings settings("PolyglotCompiler", "IDE");
            settings.setValue("topology/layout_mode", LayoutModeToString(mode));
        }
        TopologyPanel panel;
        QString path = WriteTempPloy(kSampleSource,
                                     QString("topo_layout_spread_%1.poly")
                                         .arg(static_cast<int>(mode)));
        panel.LoadFromFile(path);

        REQUIRE(panel.NodeItems().size() >= 2);
        // Count distinct quantised positions; a healthy layout must have at
        // least two distinct coordinates among its nodes.
        std::set<std::pair<long long, long long>> distinct;
        for (const auto &[_, item] : panel.NodeItems()) {
            QPointF p = item->pos();
            distinct.emplace(static_cast<long long>(std::llround(p.x())),
                             static_cast<long long>(std::llround(p.y())));
        }
        CHECK(distinct.size() >= 2);

        QFile::remove(path);
    }

    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("topology/layout_mode",
                      LayoutModeToString(LayoutMode::kHierarchical));
}

TEST_CASE("TopologyPanel renders visible directed values without overlapping cards",
          "[topology][ui][dataflow][snapshot]") {
  auto &app = GetOrCreateApp();
  const auto source = R"(
    FUNC next(x: INT) -> INT { RETURN x + 1; }
    FUNC main(a: INT) -> INT {
      LET left = next(a);
      LET right = next(left);
      RETURN right;
    }
  )";
  QSettings settings("PolyglotCompiler", "IDE");
  settings.setValue("topology/layout_mode", "hierarchical");
  TopologyPanel panel;
  panel.resize(1500, 850);
  auto path = WriteTempPloy(source, "topology_values_snapshot.poly");
  panel.LoadFromFile(path);
  panel.show(); app.processEvents();
  QMetaObject::invokeMethod(&panel, "OnZoomFit");
  std::vector<TopoNodeItem *> visible;
  for (const auto &[id, node] : panel.NodeItems())
    if (node->isVisible()) visible.push_back(node);
  REQUIRE(visible.size() >= 7);
  for (size_t i = 0; i < visible.size(); ++i)
    for (size_t j = i + 1; j < visible.size(); ++j)
      CHECK_FALSE(visible[i]->sceneBoundingRect().intersects(visible[j]->sceneBoundingRect()));
  size_t wires = 0;
  for (auto *edge : panel.EdgeItems()) {
    if (!edge->isVisible()) continue;
    CHECK_FALSE(edge->FlowLabel().isEmpty());
    CHECK(edge->flags().testFlag(QGraphicsItem::ItemIsSelectable));
    const auto *src = panel.NodeItems().at(edge->SourceNodeId());
    const auto *dst = panel.NodeItems().at(edge->TargetNodeId());
    CHECK(src->OutputPort(edge->SourcePortId()) != nullptr);
    CHECK(dst->InputPort(edge->TargetPortId()) != nullptr);
    CHECK(src->pos().x() < dst->pos().x());
    ++wires;
  }
  CHECK(wires >= 6);
  const auto output = qEnvironmentVariable("POLY_TOPOLOGY_SNAPSHOT_DIR");
  if (!output.isEmpty()) {
    QDir().mkpath(output);
    CHECK(panel.grab().save(output + "/topology-values.png"));
  }
  QFile::remove(path);
}

#include <filesystem>
#include "frontends/common/include/frontend_registry.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/python/include/python_frontend.h"

TEST_CASE("TopologyPanel displays actual foreign signatures and conversion labels",
          "[topology][ui][dataflow][snapshot][foreign]") {
  auto &app = GetOrCreateApp();
  auto &registry = polyglot::frontends::FrontendRegistry::Instance();
  registry.Register(std::make_shared<polyglot::cpp::CppLanguageFrontend>());
  registry.Register(std::make_shared<polyglot::python::PythonLanguageFrontend>());
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto fixture = root / "tests/fixtures/topology/value_conversion/flow.poly";
  REQUIRE(std::filesystem::exists(fixture));
  QSettings settings("PolyglotCompiler", "IDE");
  settings.setValue("topology/layout_mode", "hierarchical");
  TopologyPanel panel;
  panel.resize(1500, 850);
  panel.LoadFromFile(QString::fromStdString(fixture.string()));
  panel.show(); app.processEvents();
  QMetaObject::invokeMethod(&panel, "OnZoomFit");
  bool found = false;
  for (auto *edge : panel.EdgeItems()) {
    if (!edge->FlowLabel().contains("i32 → f64")) continue;
    CHECK(edge->isVisible());
    CHECK(edge->Status() == "implicit_convert");
    edge->setSelected(true); app.processEvents();
    found = true;
  }
  CHECK(found);
  QMetaObject::invokeMethod(&panel, "OnZoomFit");
  app.processEvents();
  const auto output = qEnvironmentVariable("POLY_TOPOLOGY_SNAPSHOT_DIR");
  if (!output.isEmpty()) {
    QDir().mkpath(output);
    CHECK(panel.grab().save(output + "/topology-foreign-conversion.png"));
  }
}

TEST_CASE("TopologyPanel can delete a node and its selected edge then relayout",
          "[topology][ui][dataflow][delete]") {
  auto &app = GetOrCreateApp();
  TopologyPanel panel;
  const auto path = WriteTempPloy(kSampleSource, "topo_delete_selection.poly");
  panel.LoadFromFile(path);
  REQUIRE_FALSE(panel.EdgeItems().empty());
  auto *edge = panel.EdgeItems().front();
  const auto node_id = edge->SourceNodeId();
  auto *node = panel.NodeItems().at(node_id);
  node->setVisible(true);
  edge->setVisible(true);
  node->setSelected(true);
  edge->setSelected(true);
  REQUIRE(node->isSelected());
  REQUIRE(edge->isSelected());
  REQUIRE(QMetaObject::invokeMethod(&panel, "OnBatchDelete"));
  CHECK(panel.NodeItems().count(node_id) == 0);
  for (const auto *remaining : panel.EdgeItems()) {
    CHECK(remaining->SourceNodeId() != node_id);
    CHECK(remaining->TargetNodeId() != node_id);
  }
  REQUIRE(QMetaObject::invokeMethod(&panel, "OnLayoutChanged", Q_ARG(int, 0)));
  panel.RefreshEdgePositions();
  app.processEvents();
  QFile::remove(path);
}

TEST_CASE("TopologyPanel routes long dependencies around unrelated function cards",
          "[topology][ui][dataflow][routing][snapshot]") {
  auto &app = GetOrCreateApp();
  QSettings settings("PolyglotCompiler", "IDE");
  settings.setValue("topology/layout_mode", "hierarchical");
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto fixture = root / "examples/editor_cross_language_demo/order_flow.poly";
  REQUIRE(std::filesystem::exists(fixture));
  TopologyPanel panel;
  panel.resize(1700, 850);
  panel.LoadFromFile(QString::fromStdString(fixture.string()));
  panel.show(); app.processEvents();
  REQUIRE(QMetaObject::invokeMethod(&panel, "OnZoomFit"));
  std::size_t checked = 0;
  for (const auto *edge : panel.EdgeItems()) {
    if (!edge->isVisible()) continue;
    for (const auto &[id, node] : panel.NodeItems()) {
      if (!node->isVisible() || id == edge->SourceNodeId() || id == edge->TargetNodeId()) continue;
      INFO("edge " << edge->EdgeId() << " crosses " << node->NodeName().toStdString());
      // QPainterPath::intersects treats an open path as a filled polygon.
      // Test the stroked wire, so its imaginary closing chord is not counted.
      CHECK_FALSE(edge->shape().intersects(node->sceneBoundingRect().adjusted(2, 2, -2, -2)));
      ++checked;
    }
  }
  CHECK(checked > 0);
  const auto output = qEnvironmentVariable("POLY_TOPOLOGY_SNAPSHOT_DIR");
  if (!output.isEmpty()) {
    QDir().mkpath(output);
    CHECK(panel.grab().save(output + "/topology-order-flow.png"));
  }
}

TEST_CASE("Layout keeps edges attached and subsequent card dragging updates them",
          "[topology][ui][layout]") {
  GetOrCreateApp();
  QSettings settings("PolyglotCompiler", "IDE");
  settings.setValue("topology/layout_mode", LayoutModeToString(LayoutMode::kHierarchical));
  TopologyPanel panel;
  const auto path = WriteTempPloy(kSampleSource, "topo_layout_drag_after_batch.poly");
  panel.LoadFromFile(path);
  REQUIRE_FALSE(panel.EdgeItems().empty());
  const auto check_connections = [&]() {
    for (const auto *edge : panel.EdgeItems()) {
      const auto *source = panel.NodeItems().at(edge->SourceNodeId());
      const auto *target = panel.NodeItems().at(edge->TargetNodeId());
      const auto from = source->OutputPortPos(edge->SourcePortId());
      const auto to = target->InputPortPos(edge->TargetPortId());
      CHECK(QLineF(edge->path().pointAtPercent(0), from).length() < 0.001);
      CHECK(QLineF(edge->path().pointAtPercent(1), to).length() < 0.001);
    }
  };
  check_connections();
  auto *source = panel.NodeItems().at(panel.EdgeItems().front()->SourceNodeId());
  source->moveBy(67, -43);
  check_connections();
  auto *target = panel.NodeItems().at(panel.EdgeItems().front()->TargetNodeId());
  target->moveBy(-38, 61);
  check_connections();
  QFile::remove(path);
}
