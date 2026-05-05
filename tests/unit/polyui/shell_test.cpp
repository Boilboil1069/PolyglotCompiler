/**
 * @file     shell_test.cpp
 * @brief    Unit tests for the welcome page, notification centre,
 *           customisable status bar, recent list, session
 *           serialiser, bookmarks and TODO index.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/shell/bookmarks.h"
#include "tools/ui/common/shell/notifications.h"
#include "tools/ui/common/shell/recent.h"
#include "tools/ui/common/shell/session.h"
#include "tools/ui/common/shell/status_bar.h"
#include "tools/ui/common/shell/todo_index.h"
#include "tools/ui/common/shell/welcome.h"

using namespace polyglot::tools::ui::shell;

TEST_CASE("WelcomePage dedupe + tip filter + round-trip",
          "[polyui][shell][welcome]") {
  WelcomePage p;
  p.AddWorkspace({"a", "/repo/a", 100});
  p.AddWorkspace({"b", "/repo/b", 200});
  p.AddWorkspace({"a-bis", "/repo/a", 300});      // dedupes by path
  CHECK(p.workspaces().size() == 2);
  CHECK(p.workspaces().front().path == "/repo/a");

  p.AddTutorial({"t1", "Hello", "/docs/t1"});
  p.AddSample({"s1", "Echo", "/samples/echo"});
  p.AddTip({"tip1", "T1", "body1", "1.39.0"});
  p.AddTip({"tip2", "T2", "body2", "1.30.0"});
  CHECK(p.TipsFor("1.39.0").size() == 1);

  p.set_pinned(true);
  auto round = p.Serialize();
  WelcomePage q;
  REQUIRE(q.Load(round));
  CHECK(q.pinned());
  CHECK(q.workspaces().size() == 2);
  CHECK(q.tutorials().size() == 1);
  CHECK(q.samples().size() == 1);
}

TEST_CASE("NotificationCenter unread / dismiss / DnD",
          "[polyui][shell][notifications]") {
  NotificationCenter c;
  Notification n;
  n.severity = NotificationSeverity::kInfo;
  n.title = "hello";
  auto id1 = c.Post(n);
  REQUIRE(id1 > 0);
  CHECK(c.UnreadCount() == 1);
  CHECK(c.MarkRead(id1));
  CHECK(c.UnreadCount() == 0);

  n.title = "boom";
  n.severity = NotificationSeverity::kError;
  c.Post(n);
  c.set_do_not_disturb(true);
  Notification quiet;
  quiet.severity = NotificationSeverity::kInfo;
  CHECK(c.Post(quiet) == 0);                     // suppressed
  Notification loud;
  loud.severity = NotificationSeverity::kError;
  CHECK(c.Post(loud) > 0);                       // errors break DnD
  CHECK(c.UnreadCount() == 2);

  c.DismissAll();
  CHECK(c.UnreadCount() == 0);
  CHECK(c.List().empty());
  CHECK(c.List(true).size() == 3);

  auto round = c.Serialize();
  NotificationCenter d;
  REQUIRE(d.Load(round));
  CHECK(d.List(true).size() == 3);
  CHECK(d.do_not_disturb());
}

TEST_CASE("StatusBar builtins / move / visibility / round-trip",
          "[polyui][shell][statusbar]") {
  StatusBar bar;
  bar.RegisterBuiltins();
  CHECK(bar.items().size() == 9);
  CHECK(bar.Find("branch"));

  StatusBarItem ext;
  ext.id = "ext.coverage";
  ext.label = "cov: 87%";
  ext.alignment = StatusAlignment::kRight;
  ext.priority = 30;
  ext.owner = "polyglot.coverage";
  CHECK(bar.Register(ext));
  CHECK_FALSE(bar.Register(ext));

  CHECK(bar.SetVisible("eol", false));
  auto right = bar.Visible(StatusAlignment::kRight);
  for (const auto &i : right) CHECK(i.id != "eol");

  CHECK(bar.Move("ext.coverage", StatusAlignment::kLeft, 200));
  auto left = bar.Visible(StatusAlignment::kLeft);
  REQUIRE(!left.empty());
  CHECK(left.front().id == "ext.coverage");

  auto round = bar.Serialize();
  StatusBar copy;
  REQUIRE(copy.Load(round));
  CHECK(copy.items().size() == bar.items().size());
}

TEST_CASE("RecentList touch / pin / capacity / round-trip",
          "[polyui][shell][recent]") {
  RecentList list(3);
  list.Touch({"/p/a", "a", 1, false});
  list.Touch({"/p/b", "b", 2, false});
  list.Touch({"/p/c", "c", 3, false});
  list.Touch({"/p/a", "a", 4, false});           // dedupe + bump
  CHECK(list.Items().size() == 3);
  CHECK(list.Items().front().path == "/p/a");

  CHECK(list.Pin("/p/b", true));
  list.Touch({"/p/d", "d", 5, false});           // evicts c (oldest unpinned)
  auto items = list.Items();
  CHECK(items.size() == 3);
  CHECK(items.front().path == "/p/b");           // pinned at top
  CHECK(list.Find("/p/c") == std::nullopt);

  CHECK(list.Remove("/p/d"));
  CHECK_FALSE(list.Remove("/p/missing"));

  auto round = list.Serialize();
  RecentList copy;
  REQUIRE(copy.Load(round));
  CHECK(copy.Items().size() == 2);
}

TEST_CASE("SessionStore serialises and re-parses",
          "[polyui][shell][session]") {
  Session s;
  s.split.orientation = SplitOrientation::kVertical;
  SessionPane left;
  left.id = "left";
  left.tabs.push_back({"/repo/main.ploy", 12, 4, 0,
                       {{1, 5}, {10, 20}}, true});
  s.split.panes.push_back(std::move(left));
  s.panels.sidebar_width = 320;
  s.panels.bottom_visible = false;
  s.debug.active = true;
  s.debug.configuration = "Launch";
  s.debug.watch_expressions = {"x", "obj->y"};
  s.debug.open_views = {"variables", "callstack"};
  s.extras["polyglot.last_search"] = "FN main";

  SessionStore store;
  auto json = store.Serialize(s);
  auto back = store.Deserialize(json);
  REQUIRE(back);
  CHECK(back->split.orientation == SplitOrientation::kVertical);
  REQUIRE(back->split.panes.size() == 1);
  REQUIRE(back->split.panes[0].tabs.size() == 1);
  CHECK(back->split.panes[0].tabs[0].cursor_line == 12);
  CHECK(back->split.panes[0].tabs[0].folds.size() == 2);
  CHECK(back->panels.sidebar_width == 320);
  CHECK_FALSE(back->panels.bottom_visible);
  CHECK(back->debug.watch_expressions.size() == 2);
  CHECK(back->extras.at("polyglot.last_search") == "FN main");
  CHECK_FALSE(store.Deserialize("not json"));
}

TEST_CASE("BookmarkStore toggle / relabel / lookups / round-trip",
          "[polyui][shell][bookmarks]") {
  BookmarkStore bm;
  auto a = bm.Toggle("/repo/main.ploy", 10, "entry", "#ff0");
  REQUIRE(a);
  CHECK(a->id > 0);
  auto a2 = bm.Toggle("/repo/main.ploy", 10);    // toggles off
  CHECK_FALSE(a2);
  CHECK(bm.All().empty());

  auto b = bm.Toggle("/repo/main.ploy", 20);
  REQUIRE(b);
  CHECK(bm.Relabel(b->id, "loop"));
  CHECK(bm.Recolor(b->id, "#0f0"));
  auto fetched = bm.AtLine("/repo/main.ploy", 20);
  REQUIRE(fetched);
  CHECK(fetched->label == "loop");
  CHECK(fetched->color == "#0f0");

  bm.Toggle("/repo/lib.ploy", 5);
  CHECK(bm.All().size() == 2);
  CHECK(bm.InFile("/repo/main.ploy").size() == 1);

  auto round = bm.Serialize();
  BookmarkStore copy;
  REQUIRE(copy.Load(round));
  CHECK(copy.All().size() == 2);
}

TEST_CASE("TodoIndex scans default and custom keywords",
          "[polyui][shell][todo]") {
  TodoIndex idx;
  std::string src =
      "// TODO: implement parser\n"
      "int x = 1;            // FIXME off-by-one\n"
      "/* not a TODOMARKER */\n"
      "// XXX revisit\n";
  CHECK(idx.Scan("/repo/a.cpp", src) == 2);     // TODO + FIXME
  auto counts = idx.CountsByKeyword();
  CHECK(counts["TODO"] == 1);
  CHECK(counts["FIXME"] == 1);

  idx.set_keywords({"TODO", "FIXME", "XXX", "HACK"});
  CHECK(idx.Scan("/repo/a.cpp", src) == 3);     // + XXX
  auto kw = idx.ForKeyword("XXX");
  REQUIRE(kw.size() == 1);
  CHECK(kw[0].line == 4);

  CHECK(idx.Scan("/repo/b.cpp",
                 "// HACK: cache leak\n// TODO note\n") == 2);
  CHECK(idx.All().size() == 5);
  idx.Forget("/repo/a.cpp");
  CHECK(idx.All().size() == 2);
  CHECK(idx.InFile("/repo/a.cpp").empty());

  // The "TODO" inside "TODOMARKER" must not match (word boundary).
  TodoIndex strict;
  CHECK(strict.Scan("/x", "// TODOMARKER\n") == 0);
}
