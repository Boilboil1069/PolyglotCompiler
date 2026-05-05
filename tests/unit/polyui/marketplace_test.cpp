/**
 * @file     marketplace_test.cpp
 * @brief    Unit tests for the local marketplace.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/ext/marketplace.h"

using namespace polyglot::tools::ui::ext;

namespace {

MarketplaceEntry MakeEntry(const std::string &id, const std::string &v,
                           const std::string &sig = "") {
  MarketplaceEntry e;
  e.id = id;
  e.name = id;
  e.version = v;
  e.publisher = "polyglot";
  e.download_url = id + "@" + v;
  e.signature = sig;
  return e;
}

}  // namespace

TEST_CASE("ParseIndex round-trips the canonical document",
          "[polyui][ext][market]") {
  std::string json = R"({
    "extensions":[
      {"id":"a.ext","version":"1.0.0","name":"A","publisher":"p",
       "download_url":"a.tgz","capabilities":["filesystem"]},
      {"id":"a.ext","version":"1.1.0","download_url":"a-1.1.tgz"},
      {"id":"b.ext","version":"0.5.0"}
    ]
  })";
  auto idx = ParseIndex(json);
  REQUIRE(idx);
  auto list = idx->List();
  REQUIRE(list.size() == 2);
  // Highest version wins for the canonical lookup.
  auto a = idx->Find("a.ext");
  REQUIRE(a);
  CHECK(a->version == "1.1.0");
  auto a10 = idx->FindVersion("a.ext", "1.0.0");
  REQUIRE(a10);
  CHECK(a10->required_capabilities.size() == 1);
  CHECK_FALSE(ParseIndex("{}"));
}

TEST_CASE("Install / Update / Rollback flow",
          "[polyui][ext][market]") {
  ExtensionHost host;
  Marketplace m(&host);
  m.index().Add(MakeEntry("a.ext", "1.0.0"));
  m.index().Add(MakeEntry("a.ext", "1.1.0"));

  auto r = m.Install("a.ext", "1.0.0");
  CHECK(r.ok);
  CHECK(host.Get("a.ext")->manifest.version == "1.0.0");
  CHECK(m.History("a.ext").size() == 1);

  auto up = m.Update("a.ext");
  CHECK(up.ok);
  CHECK(up.previous_version == "1.0.0");
  CHECK(host.Get("a.ext")->manifest.version == "1.1.0");
  CHECK(m.History("a.ext").size() == 2);

  // Update again with no newer version is a no-op.
  auto noop = m.Update("a.ext");
  CHECK_FALSE(noop.ok);
  CHECK(noop.message.find("up to date") != std::string::npos);

  // Rollback to the previous version.
  auto back = m.Rollback("a.ext");
  CHECK(back.ok);
  CHECK(host.Get("a.ext")->manifest.version == "1.0.0");

  // Uninstall.
  CHECK(m.Uninstall("a.ext"));
  CHECK_FALSE(host.Get("a.ext"));
  CHECK(m.History("a.ext").empty());
}

TEST_CASE("Install fails when index lookup misses",
          "[polyui][ext][market]") {
  ExtensionHost host;
  Marketplace m(&host);
  auto r = m.Install("ghost.ext");
  CHECK_FALSE(r.ok);
  CHECK(r.message == "not in index");
}

TEST_CASE("Signature policy gates installs",
          "[polyui][ext][market]") {
  ExtensionHost host;
  Marketplace m(&host);
  m.signing().set_required(true);
  m.signing().Trust("a.ext", "good-sig");
  m.index().Add(MakeEntry("a.ext", "1.0.0", "bad-sig"));
  auto bad = m.Install("a.ext");
  CHECK_FALSE(bad.ok);
  CHECK(bad.message.find("signature") != std::string::npos);

  m.index().Add(MakeEntry("a.ext", "1.1.0", "good-sig"));
  auto good = m.Install("a.ext", "1.1.0");
  CHECK(good.ok);
}

TEST_CASE("Source name covers both variants",
          "[polyui][ext][market]") {
  CHECK(MarketplaceSourceName(MarketplaceSource::kFilesystem) ==
        "filesystem");
  CHECK(MarketplaceSourceName(MarketplaceSource::kHttp) == "http");
}
