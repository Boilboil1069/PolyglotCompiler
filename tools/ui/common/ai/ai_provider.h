/**
 * @file     ai_provider.h
 * @brief    AI assistant provider abstraction.
 *
 * `AiProvider` is the unified interface every IDE-side AI feature
 * (chat panel, inline completion, refactor-suggest) talks to.  The
 * IDE never branches on which model backs the request — local
 * ollama, OpenAI-compatible HTTP, Azure OpenAI and Anthropic are
 * just adapters that share the same surface.
 *
 * No API key is ever embedded in the binary.  All credentials are
 * supplied through `AiProviderConfig` at construction time.
 *
 * Privacy is enforced through `AiPrivacyPolicy`: by default every
 * adapter declared as "remote" requires explicit user consent
 * before any request is dispatched, and the project-context
 * collector filters paths against an allow / deny list before any
 * snippet leaves the workspace.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::ai {

/// Which transport an adapter uses.  `kLocal` adapters never
/// require user consent; everything else does by default.
enum class AiTransport {
  kLocal,    ///< In-process or loopback (e.g. local ollama).
  kRemote,   ///< Any third-party HTTP endpoint.
};

std::string AiTransportName(AiTransport t);

/// Provider kinds shipped out of the box.
enum class AiProviderKind {
  kOllama,
  kOpenAi,
  kAzureOpenAi,
  kAnthropic,
  kMock,
};

std::string AiProviderKindName(AiProviderKind k);
std::optional<AiProviderKind> AiProviderKindFromName(const std::string &name);

/// Static description of an adapter — used by the settings UI and
/// by the privacy gate.
struct AiProviderDescriptor {
  AiProviderKind kind{AiProviderKind::kMock};
  AiTransport transport{AiTransport::kLocal};
  std::string display_name;
  std::string endpoint;        ///< Base URL or socket.
  std::string default_model;
};

/// Construction-time configuration.  `api_key` is only honoured
/// for adapters whose descriptor declares `kRemote` — local
/// adapters silently ignore it.
struct AiProviderConfig {
  AiProviderKind kind{AiProviderKind::kMock};
  std::string endpoint;
  std::string model;
  std::string api_key;
  std::unordered_map<std::string, std::string> headers;
};

/// Privacy / consent state owned by the IDE.  An `AiProvider`
/// consults its `policy` before every remote call and refuses to
/// execute when consent is missing.
struct AiPrivacyPolicy {
  bool allow_remote{false};                    ///< Master switch.
  std::vector<std::string> allowed_paths;      ///< Workspace allow-list.
  std::vector<std::string> denied_paths;       ///< Workspace deny-list.
  bool include_diagnostics{true};              ///< Send diagnostics with chat.
  bool include_open_files{false};              ///< Send open-file contents.
};

/// True when `path` may be sent to a remote endpoint under
/// `policy`.  Deny rules win over allow rules; an empty allow-list
/// means everything is allowed (subject to the deny-list).
bool PathPassesPolicy(const std::string &path,
                      const AiPrivacyPolicy &policy);

/// One message in a chat conversation.  Roles follow the
/// OpenAI / Anthropic convention: "system", "user", "assistant",
/// "tool".
struct ChatMessage {
  std::string role;
  std::string content;
};

struct ChatRequest {
  std::vector<ChatMessage> messages;
  std::string system_prompt;
  std::vector<std::string> attached_paths;
  std::vector<std::string> diagnostics;
  double temperature{0.2};
  int max_tokens{1024};
};

struct ChatResponse {
  std::string content;
  int prompt_tokens{0};
  int completion_tokens{0};
  std::string finish_reason;   ///< "stop", "length", "consent_denied", ...
};

struct CompletionRequest {
  std::string language;
  std::string prefix;          ///< Source up to the cursor.
  std::string suffix;          ///< Source after the cursor (FIM).
  std::string file_path;
  int max_tokens{128};
};

struct CompletionResponse {
  std::vector<std::string> choices;
  std::string finish_reason;
};

struct InlineSuggestRequest {
  std::string language;
  std::string prefix;
  std::string suffix;
  std::string file_path;
  int alternatives{3};
};

struct InlineSuggestResponse {
  std::vector<std::string> alternatives;
};

/// A refactor proposal targets a file region (`start_line` ..
/// `end_line` inclusive, 1-based) and replaces it with
/// `replacement`.  `rationale` is shown to the user.
struct RefactorHunk {
  std::string file_path;
  int start_line{0};
  int end_line{0};
  std::string original;
  std::string replacement;
  std::string rationale;
};

struct RefactorSuggestRequest {
  std::string instruction;
  std::vector<std::string> file_paths;
  std::string code;            ///< Selected code, optional.
  std::string language;
};

struct RefactorSuggestResponse {
  std::vector<RefactorHunk> hunks;
  std::string summary;
};

class AiProvider {
 public:
  virtual ~AiProvider() = default;

  const AiProviderDescriptor &descriptor() const { return descriptor_; }
  const AiPrivacyPolicy &policy() const { return policy_; }
  void set_policy(AiPrivacyPolicy p) { policy_ = std::move(p); }

  virtual ChatResponse Chat(const ChatRequest &req) = 0;
  virtual CompletionResponse Complete(const CompletionRequest &req) = 0;
  virtual InlineSuggestResponse InlineSuggest(
      const InlineSuggestRequest &req) = 0;
  virtual RefactorSuggestResponse RefactorSuggest(
      const RefactorSuggestRequest &req) = 0;

 protected:
  AiProviderDescriptor descriptor_{};
  AiPrivacyPolicy policy_{};

  /// Returns true when remote calls are allowed.  Concrete
  /// adapters call this first and short-circuit with
  /// `finish_reason = "consent_denied"` when it returns false.
  bool ConsentGranted() const;
};

/// Build an adapter from `cfg`.  `cfg.kind == kMock` produces a
/// deterministic in-memory provider used by tests and offline
/// rendering.
std::unique_ptr<AiProvider> CreateProvider(AiProviderConfig cfg);

/// Filter `paths` through `policy` and return the allowed subset.
std::vector<std::string> FilterContextPaths(
    const std::vector<std::string> &paths, const AiPrivacyPolicy &policy);

/// Render a prompt template by substituting `{{name}}` against
/// `vars`.  Unknown placeholders are left untouched so missing
/// variables surface during review rather than being silently
/// dropped.
std::string RenderPromptTemplate(
    const std::string &tmpl,
    const std::unordered_map<std::string, std::string> &vars);

}  // namespace polyglot::tools::ui::ai
