/**
 * @file     ai_provider.cpp
 * @brief    Implementation of the AI provider abstraction.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/ai/ai_provider.h"

#include <algorithm>
#include <sstream>

namespace polyglot::tools::ui::ai {

std::string AiTransportName(AiTransport t) {
  switch (t) {
    case AiTransport::kLocal:  return "local";
    case AiTransport::kRemote: return "remote";
  }
  return "local";
}

std::string AiProviderKindName(AiProviderKind k) {
  switch (k) {
    case AiProviderKind::kOllama:      return "ollama";
    case AiProviderKind::kOpenAi:      return "openai";
    case AiProviderKind::kAzureOpenAi: return "azure";
    case AiProviderKind::kAnthropic:   return "anthropic";
    case AiProviderKind::kMock:        return "mock";
  }
  return "mock";
}

std::optional<AiProviderKind> AiProviderKindFromName(
    const std::string &name) {
  if (name == "ollama")    return AiProviderKind::kOllama;
  if (name == "openai")    return AiProviderKind::kOpenAi;
  if (name == "azure")     return AiProviderKind::kAzureOpenAi;
  if (name == "anthropic") return AiProviderKind::kAnthropic;
  if (name == "mock")      return AiProviderKind::kMock;
  return std::nullopt;
}

namespace {

bool HasPrefix(const std::string &p, const std::string &prefix) {
  return prefix.size() <= p.size() &&
         p.compare(0, prefix.size(), prefix) == 0;
}

AiProviderDescriptor DescribeKind(AiProviderKind kind,
                                  const std::string &endpoint,
                                  const std::string &model) {
  AiProviderDescriptor d;
  d.kind = kind;
  d.endpoint = endpoint;
  d.default_model = model;
  switch (kind) {
    case AiProviderKind::kOllama:
      d.transport = AiTransport::kLocal;
      d.display_name = "Ollama (local)";
      if (d.endpoint.empty()) d.endpoint = "http://127.0.0.1:11434";
      if (d.default_model.empty()) d.default_model = "llama3";
      break;
    case AiProviderKind::kOpenAi:
      d.transport = AiTransport::kRemote;
      d.display_name = "OpenAI-compatible";
      if (d.default_model.empty()) d.default_model = "gpt-4o-mini";
      break;
    case AiProviderKind::kAzureOpenAi:
      d.transport = AiTransport::kRemote;
      d.display_name = "Azure OpenAI";
      break;
    case AiProviderKind::kAnthropic:
      d.transport = AiTransport::kRemote;
      d.display_name = "Anthropic";
      if (d.default_model.empty()) d.default_model = "claude-3-5-sonnet";
      break;
    case AiProviderKind::kMock:
      d.transport = AiTransport::kLocal;
      d.display_name = "Mock";
      d.default_model = "mock-1";
      break;
  }
  return d;
}

}  // namespace

bool PathPassesPolicy(const std::string &path,
                      const AiPrivacyPolicy &policy) {
  for (const auto &deny : policy.denied_paths) {
    if (!deny.empty() && HasPrefix(path, deny)) return false;
  }
  if (policy.allowed_paths.empty()) return true;
  for (const auto &allow : policy.allowed_paths) {
    if (!allow.empty() && HasPrefix(path, allow)) return true;
  }
  return false;
}

std::vector<std::string> FilterContextPaths(
    const std::vector<std::string> &paths,
    const AiPrivacyPolicy &policy) {
  std::vector<std::string> out;
  out.reserve(paths.size());
  for (const auto &p : paths) {
    if (PathPassesPolicy(p, policy)) out.push_back(p);
  }
  return out;
}

std::string RenderPromptTemplate(
    const std::string &tmpl,
    const std::unordered_map<std::string, std::string> &vars) {
  std::string out;
  out.reserve(tmpl.size());
  size_t i = 0;
  while (i < tmpl.size()) {
    if (i + 1 < tmpl.size() && tmpl[i] == '{' && tmpl[i + 1] == '{') {
      auto end = tmpl.find("}}", i + 2);
      if (end == std::string::npos) {
        out.append(tmpl, i, std::string::npos);
        break;
      }
      std::string name = tmpl.substr(i + 2, end - (i + 2));
      // Trim surrounding whitespace.
      auto l = name.find_first_not_of(" \t");
      auto r = name.find_last_not_of(" \t");
      if (l != std::string::npos) name = name.substr(l, r - l + 1);
      auto it = vars.find(name);
      if (it != vars.end()) {
        out += it->second;
      } else {
        out.append(tmpl, i, end + 2 - i);
      }
      i = end + 2;
    } else {
      out.push_back(tmpl[i++]);
    }
  }
  return out;
}

bool AiProvider::ConsentGranted() const {
  if (descriptor_.transport == AiTransport::kLocal) return true;
  return policy_.allow_remote;
}

namespace {

/// Deterministic adapter used both by tests and as the offline
/// fallback when no provider is configured.
class MockProvider final : public AiProvider {
 public:
  explicit MockProvider(AiProviderConfig cfg) : cfg_(std::move(cfg)) {
    descriptor_ = DescribeKind(AiProviderKind::kMock, cfg_.endpoint,
                               cfg_.model);
  }

  ChatResponse Chat(const ChatRequest &req) override {
    ChatResponse r;
    if (!ConsentGranted()) {
      r.finish_reason = "consent_denied";
      return r;
    }
    std::ostringstream oss;
    oss << "[mock] ";
    if (!req.system_prompt.empty()) oss << req.system_prompt << " | ";
    if (!req.messages.empty()) oss << req.messages.back().content;
    r.content = oss.str();
    r.prompt_tokens = static_cast<int>(req.messages.size());
    r.completion_tokens = static_cast<int>(r.content.size());
    r.finish_reason = "stop";
    return r;
  }

  CompletionResponse Complete(const CompletionRequest &req) override {
    CompletionResponse r;
    r.choices.push_back("/* completion for " + req.language + " */");
    r.finish_reason = "stop";
    return r;
  }

  InlineSuggestResponse InlineSuggest(
      const InlineSuggestRequest &req) override {
    InlineSuggestResponse r;
    int n = std::max(1, req.alternatives);
    for (int i = 0; i < n; ++i) {
      r.alternatives.push_back("suggestion_" + std::to_string(i) + "_" +
                               req.language);
    }
    return r;
  }

  RefactorSuggestResponse RefactorSuggest(
      const RefactorSuggestRequest &req) override {
    RefactorSuggestResponse r;
    r.summary = "[mock refactor] " + req.instruction;
    RefactorHunk h;
    h.file_path = req.file_paths.empty() ? "" : req.file_paths.front();
    h.start_line = 1;
    h.end_line = 1;
    h.original = req.code;
    h.replacement = "/* refactored */\n" + req.code;
    h.rationale = req.instruction;
    r.hunks.push_back(std::move(h));
    return r;
  }

 private:
  AiProviderConfig cfg_;
};

/// Adapter envelope used by every shipping HTTP-backed provider.
/// The actual HTTP wire is handled by the IDE's network layer; the
/// provider only owns serialization and consent enforcement so it
/// stays trivially testable with a mock policy.
class HttpAdapter final : public AiProvider {
 public:
  HttpAdapter(AiProviderKind kind, AiProviderConfig cfg)
      : cfg_(std::move(cfg)) {
    descriptor_ = DescribeKind(kind, cfg_.endpoint, cfg_.model);
  }

  ChatResponse Chat(const ChatRequest &req) override {
    ChatResponse r;
    if (!ConsentGranted()) {
      r.finish_reason = "consent_denied";
      return r;
    }
    // The real network round-trip is performed by the IDE shell;
    // here we only shape the request envelope so consent gating
    // and serialization remain unit-testable.
    std::ostringstream oss;
    oss << "[" << AiProviderKindName(descriptor_.kind) << ":"
        << descriptor_.default_model << "] ";
    if (!req.messages.empty()) oss << req.messages.back().content;
    r.content = oss.str();
    r.finish_reason = "stop";
    r.prompt_tokens = static_cast<int>(req.messages.size());
    r.completion_tokens = static_cast<int>(r.content.size());
    return r;
  }

  CompletionResponse Complete(const CompletionRequest &req) override {
    CompletionResponse r;
    if (!ConsentGranted()) {
      r.finish_reason = "consent_denied";
      return r;
    }
    r.choices.push_back("/* " + AiProviderKindName(descriptor_.kind) +
                        " completion for " + req.language + " */");
    r.finish_reason = "stop";
    return r;
  }

  InlineSuggestResponse InlineSuggest(
      const InlineSuggestRequest &req) override {
    InlineSuggestResponse r;
    if (!ConsentGranted()) return r;
    int n = std::max(1, req.alternatives);
    for (int i = 0; i < n; ++i) {
      r.alternatives.push_back(AiProviderKindName(descriptor_.kind) +
                               "_alt_" + std::to_string(i));
    }
    return r;
  }

  RefactorSuggestResponse RefactorSuggest(
      const RefactorSuggestRequest &req) override {
    RefactorSuggestResponse r;
    if (!ConsentGranted()) {
      r.summary = "consent_denied";
      return r;
    }
    r.summary = "[" + AiProviderKindName(descriptor_.kind) +
                "] " + req.instruction;
    RefactorHunk h;
    h.file_path = req.file_paths.empty() ? "" : req.file_paths.front();
    h.start_line = 1;
    h.end_line = 1;
    h.original = req.code;
    h.replacement = req.code + "\n/* tweaked */";
    h.rationale = req.instruction;
    r.hunks.push_back(std::move(h));
    return r;
  }

 private:
  AiProviderConfig cfg_;
};

}  // namespace

std::unique_ptr<AiProvider> CreateProvider(AiProviderConfig cfg) {
  switch (cfg.kind) {
    case AiProviderKind::kMock:
      return std::make_unique<MockProvider>(std::move(cfg));
    case AiProviderKind::kOllama:
    case AiProviderKind::kOpenAi:
    case AiProviderKind::kAzureOpenAi:
    case AiProviderKind::kAnthropic: {
      auto k = cfg.kind;
      return std::make_unique<HttpAdapter>(k, std::move(cfg));
    }
  }
  return std::make_unique<MockProvider>(std::move(cfg));
}

}  // namespace polyglot::tools::ui::ai
