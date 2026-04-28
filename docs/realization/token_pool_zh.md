# Token Pool — 前端词素与标识符共享驻留

> `polyglot::frontends::TokenPool` 组件说明。
> 文档版本 1.2.1。作者：Manning Cyrus。

## 1. 目标

为所有语言前端提供唯一的、规范的 token 存储容器，使得：

- 词素（lexeme）一次性写入分块字节竞技场（`StringArena`），通过稳定的
  `std::string_view` 引用。
- 标识符通过开放寻址哈希表（`IdentifierTable`）去重，得到稠密的 32 位
  `SymbolId`。
- 分析器可以在词法池上做快照、推测性向前看、并通过
  `TokenPool::Save() / Restore()` 干净地回滚。
- UI 索引与并行编译流水线可通过 `SharedTokenPool` 共享同一个池，每个
  操作由 `std::shared_mutex` 守护。

## 2. 公共接口

头文件：`frontends/common/include/token_pool.h`。

```cpp
namespace polyglot::frontends {

using TokenHandle = std::uint32_t;
inline constexpr TokenHandle kInvalidTokenHandle = 0xffffffffu;

struct TokenPoolStats {
  std::size_t tokens, arena_bytes, arena_capacity;
  std::size_t unique_identifiers, intern_hits, intern_misses;
};

class TokenPool {
public:
  explicit TokenPool(std::size_t arena_chunk_bytes = StringArena::kDefaultChunkBytes);
  TokenHandle      Add(Token token);
  const Token     &Get(TokenHandle handle) const;     // 越界抛 std::out_of_range
  std::string_view InternLexeme(std::string_view text);
  SymbolId         InternIdentifier(std::string_view name);
  Token            MakeToken(TokenKind kind, std::string_view lex,
                             const core::SourceLoc &loc);

  struct Snapshot { /* tokens, arena mark, ident snapshot */ };
  Snapshot Save() const noexcept;
  void     Restore(const Snapshot &snap);
  void     Reset();          // 别名：Clear()
  TokenPoolStats Stats() const noexcept;
};

class SharedTokenPool { /* 同样接口，加上 WithExclusive/WithShared(Fn) */ };

} // namespace polyglot::frontends
```

构成模块：

| 头文件                                            | 作用                                                         |
| ------------------------------------------------- | ------------------------------------------------------------ |
| `frontends/common/include/string_arena.h`         | 分块单调字节竞技场（默认块 64 KiB）。                        |
| `frontends/common/include/identifier_table.h`     | FNV-1a + 开放寻址，0.75 负载因子，稠密 `SymbolId`。          |
| `frontends/common/include/token_pool.h`           | Token 存储 + 驻留外观 + 线程安全变体。                       |

## 3. 驱动层 / UI 层接入

- `FrontendOptions::token_pool` —— 调用方注入的可选 `SharedTokenPool*`，
  传 `nullptr` 时保持原行为。
- `FrontendOptions::dump_token_pool_stats` —— 与新增驱动开关
  `--dump-token-pool` 同义。
- `tools/polyc/src/stage_frontend.cpp` 在每次会话分配一个共享池，挂接到
  `Preprocessor`，并对 `.ploy` 同时挂到 `PloyLexer`，最后把
  `TokenPool::Stats()` 序列化到 `FrontendResult::token_pool_stats_json`。
- `tools/polyc/src/driver.cpp` 在开启 `--dump-token-pool` 时把 JSON
  写入 `<aux_dir>/<stem>.pool_stats.json`。
- `tools/ui/common/src/compiler_service.cpp` 对每次 `Compile()`
  挂接一个 `SharedTokenPool`，并把统计计数暴露在
  `CompileResult::token_pool_stats` 上。
- `tools/ui/common/resources/default_settings.json` 暴露三个键：
  `frontend.tokenPool.arenaChunkBytes`、`frontend.tokenPool.shared`、
  `frontend.tokenPool.dumpStats`。

## 4. Lexer 接入协议

`LexerBase` 现在持有一个可选的 `SharedTokenPool*`，并提供：

```cpp
void   SetTokenPool(SharedTokenPool *pool) noexcept;
SharedTokenPool *TokenPool() const noexcept;
Token  EmitToken(Token t);  // protected：镜像写入池后原样返回 t
```

某个语言前端要让 lexer 接入共享池，只需把 `NextToken()` 中每个
`return X;` 改写成 `return EmitToken(X);` 即可。`Token::lexeme` 仍是
`std::string`，因此对所有现有消费方（分析器、AST 节点、语法高亮、约
30 个测试文件）保持二进制兼容。

`PloyLexer` 已通过 `stage_frontend` 接入；其余 8 个前端可逐文件独立
迁移，无需协调破坏性修改。

## 5. 测试

`tests/unit/frontend/token_pool_test.cpp` —— 7 条强制用例：

1. `StringArena` 在跨多个块时仍返回稳定视图（1000 个字符串 + 约
   4 MiB 额外压力）。
2. `StringArena::RewindTo` 精准丢弃标记之后追加的字节。
3. `IdentifierTable` 接收 100 000 个唯一名 + 10 000 个重复名，命中/未
   命中计数与稳定 id 均正确。
4. `TokenPool::Save / Restore` 在推测性向前看后，精确回滚 token、
   arena 字节、标识符 id 三项。
5. `SharedTokenPool` 16 线程 `InternIdentifier` 压力 —— 所有 `shared_K`
   在各线程中得到同一 `SymbolId`，唯一计数符合公式预期。
6. `TokenHandle` 在 100 万次 `Add()` 之后仍稳定；采样的句柄回读到字
   节级一致的内容。
7. `TokenPool::MakeToken` 把词素驻留到 arena，且可通过 `InternLexeme`
   再次查到。

七条断言都是行为级，不出现 `REQUIRE(true)`。

## 6. 性能特征

- Arena 分配：分摊 O(1)/字节。块大小由构造参数决定（强制夹紧到
  `[4 KiB, 16 MiB]`）。
- 标识符驻留：平均 O(1) 探针（负载因子 ≤ 0.75）；增长重哈希正比于稠密
  `symbols_` 向量大小。
- `SharedTokenPool` 读操作（`Get`、`Stats`、`Size`、`FindIdentifier`、
  `IdentifierName`）持共享锁，写操作持独占锁。

## 7. 设计说明：`Token::lexeme` 保留 `std::string`

`Token::lexeme` 没有改成 `std::string_view`，原因是：(a) 约 30 个单元
测试文件用字面量字符串与 lexeme 做相等比较；(b)
`compiler_service.cpp` 与 `syntax_highlighter.cpp` 在池生命周期之外
按值持有 token；(c) 9 个前端的 AST 节点均内嵌一份 lexeme 拷贝。
arena/intern 能力仍通过 `InternLexeme` / `InternIdentifier` /
`MakeToken` 完整暴露，愿意接入的代码可走零拷贝路径，原有 API 契约
不被破坏。

## 8. Dump 文件结构

`<stem>.pool_stats.json`（每次编译一个对象）：

```json
{
  "language": "cpp",
  "tokens": 12345,
  "arena_bytes": 65536,
  "arena_capacity": 131072,
  "unique_identifiers": 678,
  "intern_hits": 9012,
  "intern_misses": 678
}
```
