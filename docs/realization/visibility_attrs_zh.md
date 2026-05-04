# 可见性与属性（v1.16.0 起）

模块边界可见性（`PUB` / `PRIVATE`）与 `@name(args)` 注解前缀是叠加在
现有顶层声明语法之上的表层特性。v1.16.0 MVP 不改动下沉与运行时 ABI；
新增状态以元数据形式穿过解析与 sema，供后续阶段在接入后消费。

## 表层语法

```
@<attr>(<arg>, ...)   @<attr2>   PUB | PRIVATE   FUNC | STRUCT  ...
```

* 属性名为普通标识符（`inline`、`hot` 等）；参数按词法器原样字串
  捕获（保留任何包围引号）。
* 每个声明最多一个 `PUB` / `PRIVATE`。
* v1.16.0 MVP 仅在 `FUNC`、`ASYNC FUNC`、`STRUCT` 之前接受该前缀；
  其他顶层形式由解析器报告诊断。

## 内建属性注册表

sema 接受下列名称而不警告；其余名称发出 `kGenericWarning`，因此未知
属性不会破坏编译。

| 名称              | 含义                                  |
|-------------------|---------------------------------------|
| `inline`          | 提示：每个调用点优先内联。             |
| `noinline`        | 提示：禁止内联。                       |
| `always_inline`   | 强制内联。                             |
| `hot`             | 热路径优化。                           |
| `cold`            | 冷路径优化。                           |
| `profile`         | 始终生成 profile 采样桥。              |
| `no_profile`      | 抑制 profile 采集。                    |
| `deprecated`      | 标记符号已弃用。                       |
| `link_name`       | 覆盖 mangle 后的导出名。               |
| `target`          | 限定符号可用的架构。                   |

## Sema 集成

* `FuncDecl` 与 `StructDecl` 持有 `Visibility visibility`、
  `bool visibility_explicit` 与 `std::vector<Attribute> attributes`。
* `PloySymbol` 同步持有 `visibility` / `visibility_explicit`，供
  `EXPORT` 分析查询。
* `PloySema::ValidateAttributes` 遍历属性列表，对内建注册表外的名称
  发出警告。
* `PloySema::AnalyzeExportDecl` 要求目标符号持有 `Visibility::kPub`。
  显式 `PRIVATE` 为硬错误；仍带默认 `kPrivate` 的符号（源代码中
  未写 PUB / PRIVATE）会被自动升级为 `kPub` 并发出弃用警告，以保持
  v1.16.0 之前源码可编译。

## 下沉

MVP 把属性与可见性以惰性元数据形式留在已解析的 AST 上。v1.16.0 不
改动 IR 构建器、优化器与运行时。把 `@inline` / `@hot` / `@profile`
接入优化器，以及把 `@link_name` / `@target` 接入链接器，记入后续
工作。

## 跨语言宿主映射

`PUB` 控制 polyglot 二进制内部的模块边界可见性；宿主绑定继续仅接收
`EXPORT` 列出的符号——而 `EXPORT` 现在要求 `PUB`。属性目前未传播至
宿主语言绑定（例如 C++ 的 `[[gnu::always_inline]]` 或 Rust 的
`#[inline(always)]`）；干净地做这件事属于上述后续工作。

## 后续工作

* 将 `@inline` / `@noinline` / `@always_inline` / `@hot` / `@cold`
  接入优化器与后端 mangling。
* 让链接器尊重 `@link_name`，将 IR 符号名重命名为请求的外部名而无需
  额外的别名全局。
* 让 `@target("x86_64,arm64")` 在不支持架构上跳过代码生成。
* 把可见性 / 属性前缀扩展到 `CLASS`、`CONST`、`TYPE`、`MAP_FUNC` 与
  模块作用域 `LET`。
* 引入嵌套 `MODULE name { ... }` 以提供更细粒度的可见性作用域。
* 在未来主版本中将 `EXPORT` 缺少 `PUB` 的弃用警告升级为硬错误。
