/**
 * @file     localization_test.cpp
 * @brief    Unit tests for i18n catalog, accessibility model and
 *           telemetry / crash-report store.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/a11y/accessibility.h"
#include "tools/ui/common/i18n/i18n.h"
#include "tools/ui/common/telemetry/telemetry.h"

using namespace polyglot::tools::ui;

TEST_CASE("Locale name round-trip + builtin set",
          "[polyui][i18n][locale]") {
  for (auto l : i18n::BuiltinLocales())
    CHECK(*i18n::LocaleFromName(i18n::LocaleName(l)) == l);
  CHECK_FALSE(i18n::LocaleFromName("xx-YY"));
  CHECK(i18n::BuiltinLocales().size() == 5);
}

TEST_CASE("StringCatalog translate / fallback / missing",
          "[polyui][i18n][catalog]") {
  i18n::StringCatalog cat;
  cat.Put("file.open", i18n::Locale::kEn,   "Open File");
  cat.Put("file.open", i18n::Locale::kZhCN, "打开文件");
  cat.Put("file.save", i18n::Locale::kEn,   "Save File");
  CHECK(cat.Translate("file.open", i18n::Locale::kZhCN) == "打开文件");
  CHECK(cat.Translate("file.save", i18n::Locale::kZhCN) == "Save File");
  CHECK(cat.Translate("missing.id", i18n::Locale::kEn) == "missing.id");

  auto missing = cat.MissingIn(i18n::Locale::kZhCN);
  REQUIRE(missing.size() == 1);
  CHECK(missing[0] == "file.save");

  auto json = cat.Serialize(i18n::Locale::kEn);
  i18n::StringCatalog other;
  REQUIRE(other.LoadLocale(i18n::Locale::kEn, json));
  CHECK(other.Translate("file.open", i18n::Locale::kEn) == "Open File");
}

TEST_CASE("Translator wraps catalog with current locale",
          "[polyui][i18n][translator]") {
  i18n::StringCatalog cat;
  cat.Put("hello", i18n::Locale::kEn, "Hello");
  cat.Put("hello", i18n::Locale::kJa, "こんにちは");
  i18n::Translator t(&cat, i18n::Locale::kJa);
  CHECK(t.Translate("hello") == "こんにちは");
  t.set_locale(i18n::Locale::kEn);
  CHECK(t.Translate("hello") == "Hello");
}

TEST_CASE("MissingStringScanner flags hardcoded literals + missing ids",
          "[polyui][i18n][scanner]") {
  i18n::StringCatalog cat;
  cat.Put("welcome.title", i18n::Locale::kEn, "Welcome");
  i18n::MissingStringScanner scan(&cat, i18n::Locale::kEn);
  std::string src =
      "label = tr(\"welcome.title\");\n"
      "btn   = tr(\"missing.label\");\n"
      "msg   = tr(\"Hello, world!\");\n";
  auto hits = scan.Scan("ui.cpp", src);
  REQUIRE(hits.size() == 2);
  CHECK(hits[0].reason == "missing-id");
  CHECK(hits[1].reason == "hardcoded-literal");
}

TEST_CASE("FocusOrder respects tab_index + skips non-focusable",
          "[polyui][a11y][focus]") {
  a11y::FocusOrder fo;
  CHECK(fo.Register({"a", "button", "A", 1, true}));
  CHECK(fo.Register({"b", "button", "B", 0, true}));
  CHECK(fo.Register({"c", "button", "C", 2, true}));
  CHECK_FALSE(fo.Register({"a", "x", "x", 99, true}));   // dup
  auto order = fo.Order();
  REQUIRE(order.size() == 3);
  CHECK(order[0].id == "b");
  CHECK(order[2].id == "c");

  CHECK(*fo.Next("b") == "a");
  CHECK(*fo.Next("c") == "b");                            // wraps
  CHECK(*fo.Previous("b") == "c");                        // wraps
  CHECK(fo.SetFocusable("a", false));
  CHECK(*fo.Next("b") == "c");                            // skips disabled
}

TEST_CASE("ScreenReaderQueue drains assertive before polite",
          "[polyui][a11y][reader]") {
  a11y::ScreenReaderQueue q;
  q.Post({"p1", "polite 1", a11y::AnnouncementPriority::kPolite});
  q.Post({"a1", "alert!",   a11y::AnnouncementPriority::kAssertive});
  q.Post({"p2", "polite 2", a11y::AnnouncementPriority::kPolite});
  q.Post({"a2", "alert 2",  a11y::AnnouncementPriority::kAssertive});
  auto drained = q.Drain();
  REQUIRE(drained.size() == 4);
  CHECK(drained[0].id == "a1");
  CHECK(drained[1].id == "a2");
  CHECK(drained[2].id == "p1");
  CHECK(drained[3].id == "p2");
  CHECK(q.size() == 0);
}

TEST_CASE("AccessibilityProfile clamps + round-trips",
          "[polyui][a11y][profile]") {
  a11y::AccessibilityProfile p;
  p.high_contrast = true;
  p.large_font = true;
  p.reduce_motion = true;
  p.font_scale_percent = 500;
  p.preferred_theme = "high-contrast-dark";
  auto json = a11y::SerializeProfile(p);
  auto back = a11y::DeserializeProfile(json);
  REQUIRE(back);
  CHECK(back->high_contrast);
  CHECK(back->large_font);
  CHECK(back->reduce_motion);
  CHECK(back->font_scale_percent == 300);                 // clamped
  CHECK(back->preferred_theme == "high-contrast-dark");
}

TEST_CASE("Telemetry: default off, opt-in, allow-list, drain",
          "[polyui][telemetry]") {
  telemetry::ConsentManager c;
  CHECK(c.state() == telemetry::ConsentState::kUnknown);
  CHECK_FALSE(c.may_collect());

  telemetry::FieldAllowList allow;
  allow.Allow("language");
  allow.Allow("duration_ms");

  telemetry::TelemetryBuffer buf(8);
  telemetry::TelemetryEvent ev;
  ev.id = "editor.opened";
  ev.component = "editor";
  ev.fields["language"] = "ploy";
  ev.fields["secret"] = "should-be-stripped";

  CHECK_FALSE(buf.Record(c, allow, ev));                  // off by default

  c.Grant();
  auto rid = buf.Record(c, allow, ev);
  REQUIRE(rid);
  REQUIRE(buf.size() == 1);
  auto stored = buf.List().front();
  CHECK(stored.fields.count("secret") == 0);              // stripped
  CHECK(stored.fields.at("language") == "ploy");

  auto drained = buf.DrainForUpload(c);
  CHECK(drained.size() == 1);
  CHECK(buf.size() == 0);

  buf.Record(c, allow, ev);
  c.Revoke();
  CHECK(buf.DrainForUpload(c).empty());                   // upload denied
  CHECK(buf.size() == 1);                                  // events stay
}

TEST_CASE("CrashReportStore captures + marks uploaded + round-trip",
          "[polyui][telemetry][crash]") {
  telemetry::CrashReportStore store;
  telemetry::CrashReport r;
  r.version = "1.40.0";
  r.platform = "macOS-arm64";
  r.signal_name = "SIGSEGV";
  r.stack = "polyc::Compile()\nmain()\n";
  auto id = store.Capture(r);
  CHECK(id > 0);
  CHECK(store.Pending().size() == 1);
  CHECK(store.MarkUploaded(id));
  CHECK(store.Pending().empty());
  CHECK(store.All().size() == 1);

  auto json = store.Serialize();
  telemetry::CrashReportStore copy;
  REQUIRE(copy.Load(json));
  CHECK(copy.All().size() == 1);
  CHECK(copy.All().front().uploaded);
  CHECK(copy.Remove(id));
  CHECK(copy.All().empty());
}
