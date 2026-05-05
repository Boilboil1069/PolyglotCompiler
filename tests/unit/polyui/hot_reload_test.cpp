/**
 * @file     hot_reload_test.cpp
 * @brief    Unit tests for `HotReloadEngine`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/runtime/hot_reload.h"

using namespace polyglot::tools::ui::runtime;

TEST_CASE("DetectLanguage maps common extensions",
          "[polyui][runtime][reload]") {
  CHECK(HotReloadEngine::DetectLanguage("a.ploy") == "ploy");
  CHECK(HotReloadEngine::DetectLanguage("a.py")   == "python");
  CHECK(HotReloadEngine::DetectLanguage("a.cpp")  == "cpp");
  CHECK(HotReloadEngine::DetectLanguage("a.rs")   == "rust");
  CHECK(HotReloadEngine::DetectLanguage("Foo.java") == "java");
  CHECK(HotReloadEngine::DetectLanguage("Foo.cs") == "dotnet");
  CHECK(HotReloadEngine::DetectLanguage("README") == "");
}

TEST_CASE("Engine routes by language and reports unsupported",
          "[polyui][runtime][reload]") {
  HotReloadEngine e;
  e.RegisterHandler("python", [](const ReloadRequest &r) {
    ReloadResult res;
    res.replaced_symbols = {"mod." + r.file};
    return res;
  });
  ReloadRequest req;  req.language = "python";  req.file = "x.py";
  auto r = e.Notify(req);
  CHECK(r.status == ReloadStatus::kSuccess);
  REQUIRE(r.replaced_symbols.size() == 1);

  ReloadRequest unknown;  unknown.language = "fortran"; unknown.file = "x.f90";
  CHECK(e.Notify(unknown).status == ReloadStatus::kUnsupported);
}

TEST_CASE("Engine coalesces overlapping reloads",
          "[polyui][runtime][reload]") {
  HotReloadEngine e;
  bool reentrant_seen = false;
  e.RegisterHandler("ploy", [&](const ReloadRequest &r) {
    // Issue a re-entrant Notify for the same file while we're
    // still "running".  It must be queued, not executed in line.
    if (!reentrant_seen) {
      reentrant_seen = true;
      ReloadRequest dup = r;
      auto inner = e.Notify(dup);
      CHECK(inner.status == ReloadStatus::kPartial);
      CHECK(inner.message.find("queued") != std::string::npos);
    }
    return ReloadResult{};
  });
  ReloadRequest req;  req.language = "ploy"; req.file = "main.ploy";
  e.Notify(req);
  CHECK(e.pending_count() == 1);
  CHECK(e.DrainPending("main.ploy"));
  CHECK(e.pending_count() == 0);
}
