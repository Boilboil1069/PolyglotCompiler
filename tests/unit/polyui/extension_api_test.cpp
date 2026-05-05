/**
 * @file     extension_api_test.cpp
 * @brief    Unit tests for the extension manifest parser, the
 *           capability gate and the host registry.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/ext/extension_api.h"

using namespace polyglot::tools::ui::ext;

TEST_CASE("Loader / activation / capability names round-trip",
          "[polyui][ext]") {
  for (auto l : {ExtensionLoader::kNative, ExtensionLoader::kJavaScript})
    CHECK(*ExtensionLoaderFromName(ExtensionLoaderName(l)) == l);
  for (auto e : {ActivationEvent::kOnStartup, ActivationEvent::kOnLanguage,
                 ActivationEvent::kOnCommand, ActivationEvent::kOnView,
                 ActivationEvent::kOnDebug, ActivationEvent::kOnFileOpen})
    CHECK(*ActivationEventFromName(ActivationEventName(e)) == e);
  for (auto c : {Capability::kFilesystem, Capability::kNetwork,
                 Capability::kProcess, Capability::kClipboard,
                 Capability::kSecrets})
    CHECK(*CapabilityFromName(CapabilityName(c)) == c);
}

TEST_CASE("Semver comparison handles uneven part counts",
          "[polyui][ext]") {
  CHECK(CompareVersion("1.2.3", "1.2.3") == 0);
  CHECK(CompareVersion("1.2.3", "1.10.0") < 0);
  CHECK(CompareVersion("2.0", "1.99.99") > 0);
  CHECK(CompareVersion("1.0", "1.0.0") == 0);
}

TEST_CASE("ParseManifest extracts every interesting field",
          "[polyui][ext]") {
  std::string doc = R"({
    "id":"polyglot.sample",
    "name":"Sample",
    "version":"0.2.1",
    "publisher":"polyglot",
    "main":"out/extension.js",
    "loader":"javascript",
    "activation":["onStartup",
                  {"event":"onLanguage","argument":"ploy"}],
    "capabilities":["filesystem","network"],
    "contributes":{
      "commands":[{"id":"sample.hello","title":"Hello"}],
      "keybindings":[{"id":"sample.hello.key","key":"Ctrl+H"}]
    }
  })";
  auto m = ParseManifest(doc);
  REQUIRE(m);
  CHECK(m->id == "polyglot.sample");
  CHECK(m->loader == ExtensionLoader::kJavaScript);
  CHECK(m->entry_point == "out/extension.js");
  REQUIRE(m->activation.size() == 2);
  CHECK(m->activation[1].event == ActivationEvent::kOnLanguage);
  CHECK(m->activation[1].argument == "ploy");
  CHECK(m->required_capabilities.size() == 2);
  CHECK(m->contributes.size() == 2);
  CHECK_FALSE(ParseManifest("{}"));
  CHECK_FALSE(ParseManifest("not json"));
}

TEST_CASE("CapabilityGate grants and revokes",
          "[polyui][ext][capability]") {
  CapabilityGate g;
  g.Grant("ext.a", Capability::kFilesystem);
  CHECK(g.IsGranted("ext.a", Capability::kFilesystem));
  CHECK_FALSE(g.IsGranted("ext.a", Capability::kNetwork));
  CHECK(g.AllGranted("ext.a", {Capability::kFilesystem}));
  CHECK_FALSE(g.AllGranted("ext.a",
                           {Capability::kFilesystem, Capability::kNetwork}));
  g.Revoke("ext.a", Capability::kFilesystem);
  CHECK_FALSE(g.IsGranted("ext.a", Capability::kFilesystem));
}

namespace {

ExtensionManifest MakeManifest(const std::string &id, const std::string &v,
                               std::vector<Capability> caps = {}) {
  ExtensionManifest m;
  m.id = id;
  m.name = id;
  m.version = v;
  m.entry_point = id + ".so";
  m.loader = ExtensionLoader::kNative;
  m.required_capabilities = std::move(caps);
  Contribution c;
  c.kind = ContributionKind::kCommand;
  c.id = id + ".run";
  c.title = "Run";
  m.contributes.push_back(c);
  Trigger t;
  t.event = ActivationEvent::kOnCommand;
  t.argument = id + ".run";
  m.activation.push_back(t);
  return m;
}

}  // namespace

TEST_CASE("Host install / activate / contribution registry",
          "[polyui][ext][host]") {
  CapabilityGate gate;
  ExtensionHost host;
  host.set_capability_gate(&gate);

  CHECK(host.Install(MakeManifest("a.ext", "1.0.0",
                                  {Capability::kFilesystem})));
  CHECK_FALSE(host.Activate("a.ext"));    // missing grant
  gate.Grant("a.ext", Capability::kFilesystem);
  CHECK(host.Activate("a.ext"));
  CHECK(host.Contributions().size() == 1);

  // Reactivating must not duplicate.
  CHECK(host.Reload("a.ext"));
  CHECK(host.Contributions().size() == 1);

  // Newer version replaces older without leaving stale contributions.
  CHECK(host.Install(MakeManifest("a.ext", "1.1.0",
                                  {Capability::kFilesystem})));
  CHECK(host.Activate("a.ext"));
  CHECK(host.Contributions().size() == 1);

  // Deactivate removes its contributions.
  CHECK(host.Deactivate("a.ext"));
  CHECK(host.Contributions().empty());
}

TEST_CASE("Contribution dedup across two extensions",
          "[polyui][ext][host]") {
  CapabilityGate gate;
  ExtensionHost host;
  host.set_capability_gate(&gate);
  host.Install(MakeManifest("a.ext", "1.0.0"));
  host.Install(MakeManifest("b.ext", "1.0.0"));
  // Force a collision: rename b's contribution id to a.ext.run.
  auto b = host.Get("b.ext");
  REQUIRE(b);
  ExtensionManifest m = b->manifest;
  m.version = "2.0.0";
  m.contributes.front().id = "a.ext.run";
  REQUIRE(host.Install(m));
  host.Activate("a.ext");
  host.Activate("b.ext");
  CHECK(host.Contributions().size() == 1);
  CHECK(host.ContributionsOfKind(ContributionKind::kCommand).size() == 1);
}

TEST_CASE("Activation event matching", "[polyui][ext][host]") {
  ExtensionHost host;
  ExtensionManifest m = MakeManifest("c.ext", "1.0.0");
  m.activation.clear();
  Trigger lang;
  lang.event = ActivationEvent::kOnLanguage;
  lang.argument = "ploy";
  m.activation.push_back(lang);
  Trigger any_startup;
  any_startup.event = ActivationEvent::kOnStartup;
  m.activation.push_back(any_startup);
  host.Install(std::move(m));

  CHECK(host.MatchesActivationEvent("c.ext", ActivationEvent::kOnLanguage,
                                    "ploy"));
  CHECK_FALSE(host.MatchesActivationEvent(
      "c.ext", ActivationEvent::kOnLanguage, "rust"));
  CHECK(host.MatchesActivationEvent("c.ext",
                                    ActivationEvent::kOnStartup));
}
