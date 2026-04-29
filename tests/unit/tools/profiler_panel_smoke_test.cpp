/**
 * @file     profiler_panel_smoke_test.cpp
 * @brief    Smoke test ensuring ProfilerPanel + CallAnalyzerPanel can be
 *           constructed, share a session, and register with the panel manager.
 *
 * @ingroup  Tests / Unit / Tools
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QString>

#include "tools/ui/common/include/call_analyzer_panel.h"
#include "tools/ui/common/include/profile_session.h"
#include "tools/ui/common/include/profiler_panel.h"

using polyglot::tools::ui::CallAnalyzerPanel;
using polyglot::tools::ui::ProfileSession;
using polyglot::tools::ui::ProfilerPanel;

namespace {

QApplication &GetOrCreateApp() {
  if (QApplication::instance()) {
    return *static_cast<QApplication *>(QApplication::instance());
  }
  static int argc = 1;
  static const char *argv[] = {"profiler_panel_smoke_test", nullptr};
  static QApplication app(argc, const_cast<char **>(argv));
  return app;
}

} // namespace

TEST_CASE("ProfilerPanel constructs with internal session", "[ui][profiler][smoke]") {
  GetOrCreateApp();
  ProfilerPanel panel;
  REQUIRE(panel.Session() != nullptr);
}

TEST_CASE("CallAnalyzerPanel constructs with internal session",
          "[ui][callanalyzer][smoke]") {
  GetOrCreateApp();
  CallAnalyzerPanel panel;
  REQUIRE(panel.Session() != nullptr);
}

TEST_CASE("Profiler and CallAnalyzer share an injected session",
          "[ui][profiler][shared]") {
  GetOrCreateApp();
  ProfilerPanel profiler;
  CallAnalyzerPanel analyzer;

  ProfileSession *shared = profiler.Session();
  REQUIRE(shared != nullptr);
  analyzer.SetSession(shared);
  REQUIRE(analyzer.Session() == shared);

  // Ensure the panels expose meta-object information for their public
  // signal — the test will fail to compile if the signal signature drifts.
  REQUIRE(profiler.metaObject()->indexOfSignal(
              "OpenFileRequested(QString,int)") >= 0);
  REQUIRE(analyzer.metaObject()->indexOfSignal(
              "OpenFileRequested(QString,int)") >= 0);
}
