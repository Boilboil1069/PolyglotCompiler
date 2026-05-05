# AI Assistant Integration

## Goal

Provide a single value-layer interface every IDE-side AI feature
(chat, completion, inline suggestion, refactor) talks to so the
shell, settings UI and tests share one model and never branch on
the backing provider.  Privacy is enforced inside the provider —
no remote request is dispatched until the user has explicitly
opted in.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `AiProvider` | [`tools/ui/common/ai/ai_provider.h`](../../tools/ui/common/ai/ai_provider.h) | Abstract base: `Chat`, `Complete`, `InlineSuggest`, `RefactorSuggest`. |
| `MockProvider` / `HttpAdapter` | same | Deterministic offline adapter + shared HTTP envelope used by Ollama / OpenAI / Azure / Anthropic.  Each adapter publishes an `AiTransport` (`kLocal` / `kRemote`) and short-circuits with `consent_denied` when the policy disallows the call. |
| `AiPrivacyPolicy` + `PathPassesPolicy` + `FilterContextPaths` | same | Master switch, allow/deny path lists and diagnostics / open-files toggles applied to every project-context request. |
| `RenderPromptTemplate` | same | `{{name}}` substitution; unknown placeholders are left untouched so missing variables surface during review. |
| `InlineSuggestionSession` | [`inline_suggestion.h`](../../tools/ui/common/ai/inline_suggestion.h) | State machine for ghost-text: `Show` → `Showing`, `Accept` → `Accepted`, `Dismiss` → `Dismissed`; `Next` / `Prev` cycle the alternative list. |
| `RefactorReviewSession` | [`refactor_diff.h`](../../tools/ui/common/ai/refactor_diff.h) | Per-hunk `Accept` / `Reject` tracking; emits a unified diff containing only the accepted hunks. |

## Pipelines

* **Provider construction.** The settings layer hands an
  `AiProviderConfig` (kind + endpoint + model + headers + optional
  API key) to `CreateProvider`.  `kMock` builds the deterministic
  in-memory provider; everything else is built on a shared HTTP
  adapter envelope that owns serialization and consent gating.
* **Privacy gate.** Every adapter inherits `ConsentGranted()` from
  `AiProvider`.  Local providers always return true; remote
  providers return `policy_.allow_remote`.  When the gate denies a
  call, chat / completion / refactor responses are returned with
  `finish_reason = "consent_denied"` and inline-suggest returns an
  empty alternative list so the UI can surface the consent prompt
  in-place.
* **Project context collection.** Files attached to a request go
  through `FilterContextPaths(paths, policy)`; deny rules win over
  allow rules; an empty allow-list means "everything except deny".
* **Inline suggestions.** The IDE binds Tab / Esc / Alt+] / Alt+[
  to `Accept` / `Dismiss` / `Next` / `Prev`.  Empty alternative
  lists keep the session in `kIdle` so the bindings become no-ops.
* **Refactor review.** `RefactorReviewSession::Load` consumes a
  `RefactorSuggestResponse`; the UI shows each hunk with its
  rationale.  When the user is done, `RenderUnifiedDiff()`
  produces a `patch -p0`-compatible blob containing only the
  accepted hunks.

## Tests

* [`tests/unit/polyui/ai_provider_test.cpp`](../../tests/unit/polyui/ai_provider_test.cpp)
  — kind-name round-trips, allow / deny path policy, context
  filtering, prompt templating, mock provider chat / inline /
  refactor, remote consent gating, local-adapter defaults.
* [`tests/unit/polyui/inline_suggestion_test.cpp`](../../tests/unit/polyui/inline_suggestion_test.cpp)
  — empty list keeps the session idle; Next / Prev wrap; Accept
  commits exactly once; Dismiss leaves no inserted text.
* [`tests/unit/polyui/refactor_diff_test.cpp`](../../tests/unit/polyui/refactor_diff_test.cpp)
  — per-hunk accept / reject; bulk accept / reject; unified diff
  emits accepted hunks only with the right `@@` ranges.

The full polyui suite runs at 986 assertions across 199 cases.
