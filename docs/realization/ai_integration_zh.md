# AI 助手集成

## 目标

为 IDE 内所有 AI 能力（聊天、补全、行内建议、重构）提供同一
套值模型接口，IDE shell、设置面板与测试共享同一模型，永远不在
后端模型层面分支。隐私控制由 provider 内部强制——在用户显式
同意之前，任何远程请求都不会真正发出。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `AiProvider` | [`tools/ui/common/ai/ai_provider.h`](../../tools/ui/common/ai/ai_provider.h) | 抽象基类：`Chat`、`Complete`、`InlineSuggest`、`RefactorSuggest`。 |
| `MockProvider` / `HttpAdapter` | 同上 | 确定性离线适配器 + Ollama / OpenAI / Azure / Anthropic 共用的 HTTP 信封。每个适配器声明 `AiTransport`（`kLocal` / `kRemote`），在策略禁止时以 `consent_denied` 短路。 |
| `AiPrivacyPolicy` + `PathPassesPolicy` + `FilterContextPaths` | 同上 | 总开关、工作区允许 / 拒绝名单，以及 diagnostics / 打开文件等上下文开关，作用于所有携带项目上下文的请求。 |
| `RenderPromptTemplate` | 同上 | `{{name}}` 占位符替换；未知占位符保持原样以便缺失变量在评审时显现。 |
| `InlineSuggestionSession` | [`inline_suggestion.h`](../../tools/ui/common/ai/inline_suggestion.h) | 灰字建议状态机：`Show` → `Showing`，`Accept` → `Accepted`，`Dismiss` → `Dismissed`；`Next` / `Prev` 在备选间循环。 |
| `RefactorReviewSession` | [`refactor_diff.h`](../../tools/ui/common/ai/refactor_diff.h) | 逐 hunk 接受 / 拒绝；输出仅包含已接受 hunk 的 unified diff。 |

## 流程

* **Provider 构造。** 设置层把 `AiProviderConfig`（kind + endpoint
  + model + headers + 可选 API key）交给 `CreateProvider`：
  `kMock` 构造内存中的确定性 provider；其余皆基于共用的 HTTP
  信封，由其负责序列化与同意门。
* **隐私门。** 所有适配器继承 `AiProvider::ConsentGranted()`。
  本地 provider 始终返回 true；远程 provider 返回
  `policy_.allow_remote`。被拒绝时，chat / completion / refactor
  返回 `finish_reason = "consent_denied"`，inline-suggest 返回空
  备选，UI 借此就地显示授权提示。
* **项目上下文采集。** 请求附带的文件会先经过
  `FilterContextPaths(paths, policy)`；拒绝规则优先于允许规则；
  允许列表为空表示"除拒绝外一切允许"。
* **行内建议。** IDE 将 Tab / Esc / Alt+] / Alt+[ 绑定到
  `Accept` / `Dismiss` / `Next` / `Prev`；空备选列表会让会话停在
  `kIdle`，使快捷键自然成为空操作。
* **重构评审。** `RefactorReviewSession::Load` 接收
  `RefactorSuggestResponse`；UI 逐 hunk 展示其 rationale。完成后
  `RenderUnifiedDiff()` 产出仅含已接受 hunk 的 `patch -p0` 兼容
  blob。

## 测试

* [`tests/unit/polyui/ai_provider_test.cpp`](../../tests/unit/polyui/ai_provider_test.cpp)
  ——kind 名往返、允许 / 拒绝路径策略、上下文过滤、提示词模板、
  mock provider chat / inline / refactor、远程同意门、本地适配器
  默认值。
* [`tests/unit/polyui/inline_suggestion_test.cpp`](../../tests/unit/polyui/inline_suggestion_test.cpp)
  ——空备选保持 idle；Next / Prev 循环；Accept 仅生效一次；
  Dismiss 不插入任何文本。
* [`tests/unit/polyui/refactor_diff_test.cpp`](../../tests/unit/polyui/refactor_diff_test.cpp)
  ——逐 hunk 接受 / 拒绝；批量接受 / 拒绝；unified diff 只包含
  已接受 hunk 且 `@@` 区间正确。

polyui 全套合计 199 例 986 条断言全部通过。
