/**
 * @file     remote_session_test.cpp
 * @brief    Unit tests for `RemoteSession` backends.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/remote/remote_session.h"

using namespace polyglot::tools::ui::remote;

TEST_CASE("RemoteKind names round-trip", "[polyui][remote]") {
  for (auto k : {RemoteKind::kLocal, RemoteKind::kSsh, RemoteKind::kWsl,
                 RemoteKind::kContainer})
    CHECK(*RemoteKindFromName(RemoteKindName(k)) == k);
  CHECK_FALSE(RemoteKindFromName("nope"));
}

TEST_CASE("ParseConnectionString recognises every scheme",
          "[polyui][remote]") {
  auto local = ParseConnectionString("local:/tmp/work");
  REQUIRE(local);
  CHECK(local->kind == RemoteKind::kLocal);
  CHECK(local->workspace == "/tmp/work");

  auto ssh = ParseConnectionString("ssh://alice@host.example:2222/srv/repo");
  REQUIRE(ssh);
  CHECK(ssh->kind == RemoteKind::kSsh);
  CHECK(ssh->user == "alice");
  CHECK(ssh->host == "host.example");
  CHECK(ssh->port == 2222);
  CHECK(ssh->workspace == "/srv/repo");

  auto wsl = ParseConnectionString("wsl://Ubuntu/home/me/repo");
  REQUIRE(wsl);
  CHECK(wsl->kind == RemoteKind::kWsl);
  CHECK(wsl->host == "Ubuntu");
  CHECK(wsl->workspace == "/home/me/repo");

  auto cont = ParseConnectionString("container://podman/poly-dev/work");
  REQUIRE(cont);
  CHECK(cont->kind == RemoteKind::kContainer);
  CHECK(cont->runtime == "podman");
  CHECK(cont->host == "poly-dev");
  CHECK(cont->workspace == "/work");

  auto cont2 = ParseConnectionString("container://poly-dev/work");
  REQUIRE(cont2);
  CHECK(cont2->runtime == "docker");
  CHECK(cont2->host == "poly-dev");

  CHECK_FALSE(ParseConnectionString("badscheme:foo"));
  CHECK_FALSE(ParseConnectionString("frob://x/y"));
}

TEST_CASE("CreateSession dispatches to the matching backend",
          "[polyui][remote]") {
  for (auto k : {RemoteKind::kLocal, RemoteKind::kSsh, RemoteKind::kWsl,
                 RemoteKind::kContainer}) {
    RemoteDescriptor d;
    d.kind = k;
    d.host = "h";
    d.workspace = "/w";
    auto s = CreateSession(d);
    REQUIRE(s);
    CHECK(s->descriptor().kind == k);
    REQUIRE(s->Connect());
    CHECK(s->IsConnected());
    s->Disconnect();
    CHECK_FALSE(s->IsConnected());
  }
}

TEST_CASE("Filesystem operations round-trip on every backend",
          "[polyui][remote]") {
  for (auto k : {RemoteKind::kLocal, RemoteKind::kSsh, RemoteKind::kWsl,
                 RemoteKind::kContainer}) {
    RemoteDescriptor d;
    d.kind = k;
    d.host = "h";
    auto s = CreateSession(d);
    REQUIRE(s);
    s->Connect();
    REQUIRE(s->WriteFile("/srv/main.ploy", "FN main() {}\n"));
    REQUIRE(s->WriteFile("/srv/lib.ploy", "FN add(a,b) { RETURN a+b; }\n"));
    auto contents = s->ReadFile("/srv/main.ploy");
    REQUIRE(contents);
    CHECK(*contents == "FN main() {}\n");
    auto entries = s->ListDir("/srv");
    CHECK(entries.size() == 2);
    REQUIRE(s->RemoveFile("/srv/main.ploy"));
    CHECK_FALSE(s->ReadFile("/srv/main.ploy"));
  }
}

TEST_CASE("Spawn wraps commands per transport", "[polyui][remote]") {
  RemoteDescriptor ssh;
  ssh.kind = RemoteKind::kSsh;
  ssh.host = "host.example";
  ssh.user = "alice";
  ssh.port = 2222;
  auto s = CreateSession(ssh);
  s->Connect();
  auto p = s->Spawn("polyc build", {});
  CHECK(p.command.find("ssh -p 2222 alice@host.example -- polyc build") !=
        std::string::npos);

  RemoteDescriptor wsl;
  wsl.kind = RemoteKind::kWsl;
  wsl.host = "Ubuntu";
  auto w = CreateSession(wsl);
  w->Connect();
  auto pw = w->Spawn("polyc build", {});
  CHECK(pw.command == "wsl -d Ubuntu -- polyc build");

  RemoteDescriptor cont;
  cont.kind = RemoteKind::kContainer;
  cont.runtime = "podman";
  cont.host = "poly-dev";
  cont.user = "dev";
  auto c = CreateSession(cont);
  c->Connect();
  auto pc = c->Spawn("polyc build", {});
  CHECK(pc.command == "podman exec -u dev poly-dev polyc build");
}

TEST_CASE("Process lifecycle: spawn / wait / kill", "[polyui][remote]") {
  RemoteDescriptor d;
  d.kind = RemoteKind::kLocal;
  auto s = CreateSession(d);
  s->Connect();
  auto p = s->Spawn("sleep 5", {});
  CHECK(p.running);
  REQUIRE(s->Kill(p.id));
  auto after = s->Wait(p.id);
  REQUIRE(after);
  CHECK_FALSE(after->running);
  CHECK_FALSE(s->Kill("missing"));
  CHECK_FALSE(s->Wait("missing"));
}

TEST_CASE("Port forwarding registry", "[polyui][remote]") {
  RemoteDescriptor d;
  d.kind = RemoteKind::kSsh;
  d.host = "h";
  auto s = CreateSession(d);
  s->Connect();
  auto f = s->Forward(13000, 3000);
  CHECK(f.active);
  CHECK(f.local_port == 13000);
  CHECK(f.remote_port == 3000);
  s->Forward(18080, 80, "service");
  CHECK(s->ActiveForwards().size() == 2);
  REQUIRE(s->Unforward(13000));
  CHECK(s->ActiveForwards().size() == 1);
  CHECK_FALSE(s->Unforward(99999));
}

TEST_CASE("OpenTerminal spawns the default shell when command empty",
          "[polyui][remote]") {
  RemoteDescriptor d;
  d.kind = RemoteKind::kWsl;
  d.host = "Ubuntu";
  auto s = CreateSession(d);
  s->Connect();
  auto t = s->OpenTerminal();
  CHECK(t.command == "wsl -d Ubuntu -- /bin/bash");
  auto t2 = s->OpenTerminal("htop");
  CHECK(t2.command == "wsl -d Ubuntu -- htop");
}
