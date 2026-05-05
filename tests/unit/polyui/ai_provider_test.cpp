/**
 * @file     ai_provider_test.cpp
 * @brief    Unit tests for the AI provider abstraction.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/ai/ai_provider.h"

using namespace polyglot::tools::ui::ai;

TEST_CASE("Provider kind names round-trip", "[polyui][ai]") {
  for (auto k : {AiProviderKind::kOllama, AiProviderKind::kOpenAi,
                 AiProviderKind::kAzureOpenAi, AiProviderKind::kAnthropic,
                 AiProviderKind::kMock})
    CHECK(*AiProviderKindFromName(AiProviderKindName(k)) == k);
  CHECK_FALSE(AiProviderKindFromName("nope"));
}

TEST_CASE("Path policy honours allow / deny", "[polyui][ai]") {
  AiPrivacyPolicy p;
  p.allowed_paths = {"src/", "tests/"};
  p.denied_paths  = {"src/secret/"};
  CHECK(PathPassesPolicy("src/main.ploy", p));
  CHECK_FALSE(PathPassesPolicy("src/secret/keys.ploy", p));
  CHECK(PathPassesPolicy("tests/unit.ploy", p));
  CHECK_FALSE(PathPassesPolicy("docs/readme.md", p));

  AiPrivacyPolicy empty;
  CHECK(PathPassesPolicy("anywhere", empty));
}

TEST_CASE("FilterContextPaths drops disallowed paths",
          "[polyui][ai]") {
  AiPrivacyPolicy p;
  p.denied_paths = {".env"};
  auto out = FilterContextPaths({"src/a.ploy", ".env", "src/b.ploy"}, p);
  REQUIRE(out.size() == 2);
  CHECK(out[0] == "src/a.ploy");
  CHECK(out[1] == "src/b.ploy");
}

TEST_CASE("RenderPromptTemplate substitutes known names",
          "[polyui][ai]") {
  std::unordered_map<std::string, std::string> vars = {
      {"name", "Polyglot"}, {"lang", "ploy"}};
  auto out = RenderPromptTemplate("hi {{name}}, edit {{lang}} files; "
                                  "{{ unknown }} stays.", vars);
  CHECK(out ==
        "hi Polyglot, edit ploy files; {{ unknown }} stays.");
}

TEST_CASE("Mock provider returns a deterministic chat reply",
          "[polyui][ai]") {
  AiProviderConfig cfg;
  cfg.kind = AiProviderKind::kMock;
  auto p = CreateProvider(cfg);
  REQUIRE(p);
  CHECK(p->descriptor().transport == AiTransport::kLocal);

  ChatRequest req;
  req.system_prompt = "be helpful";
  req.messages.push_back({"user", "hello"});
  auto r = p->Chat(req);
  CHECK(r.finish_reason == "stop");
  CHECK(r.content.find("hello") != std::string::npos);

  auto suggest = p->InlineSuggest({"ploy", "FN ", "}", "f.ploy", 4});
  CHECK(suggest.alternatives.size() == 4);

  RefactorSuggestRequest rs;
  rs.instruction = "extract function";
  rs.file_paths = {"src/a.ploy"};
  rs.code = "FN x() { RETURN 1; }\n";
  auto rr = p->RefactorSuggest(rs);
  CHECK(rr.hunks.size() == 1);
  CHECK(rr.hunks.front().rationale == "extract function");
}

TEST_CASE("Remote adapter blocks calls without consent",
          "[polyui][ai]") {
  AiProviderConfig cfg;
  cfg.kind = AiProviderKind::kOpenAi;
  cfg.api_key = "test-key";
  auto p = CreateProvider(cfg);
  REQUIRE(p);
  CHECK(p->descriptor().transport == AiTransport::kRemote);

  ChatRequest req;
  req.messages.push_back({"user", "explain this code"});
  auto denied = p->Chat(req);
  CHECK(denied.finish_reason == "consent_denied");

  AiPrivacyPolicy policy;
  policy.allow_remote = true;
  p->set_policy(policy);
  auto allowed = p->Chat(req);
  CHECK(allowed.finish_reason == "stop");
  CHECK(allowed.content.find("openai") != std::string::npos);
}

TEST_CASE("Local adapter (ollama) needs no consent",
          "[polyui][ai]") {
  AiProviderConfig cfg;
  cfg.kind = AiProviderKind::kOllama;
  auto p = CreateProvider(cfg);
  REQUIRE(p);
  CHECK(p->descriptor().transport == AiTransport::kLocal);
  CHECK(p->descriptor().endpoint == "http://127.0.0.1:11434");

  ChatRequest req;
  req.messages.push_back({"user", "ping"});
  auto r = p->Chat(req);
  CHECK(r.finish_reason == "stop");
}
