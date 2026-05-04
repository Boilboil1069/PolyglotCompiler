# 示例 39 — 可见性（PUB / PRIVATE）与属性（@name）

演示 v1.16.0 模块边界可见性与内建声明属性的表层语法。

## 表层语法

```
@<attr1> @<attr2>(<arg>, ...)  PUB | PRIVATE  FUNC | STRUCT  ...
```

* `PUB` — 跨模块导出，是 `EXPORT` 的合法目标。
* `PRIVATE` — 模块本地（默认），显式 `PRIVATE` 会阻止 `EXPORT`。
* 默认可见性（未显式声明）视为 private，但 `EXPORT` 出于向后兼容
  会自动升级为 `PUB` 并发出弃用警告。
* 注解（`@inline`、`@hot` 等）位于可见性关键字之前。

## 内建属性目录

| 属性                  | 含义                                 |
|----------------------|--------------------------------------|
| `@inline`            | 提示：每个调用点优先内联。            |
| `@noinline`          | 提示：禁止内联。                      |
| `@always_inline`     | 强制内联。                            |
| `@hot`               | 热路径（性能/体积平衡偏向性能）。     |
| `@cold`              | 冷路径（偏向体积）。                  |
| `@profile`           | 始终生成 profile 采样桥。             |
| `@no_profile`        | 抑制 profile 采集。                   |
| `@deprecated("msg")` | 标记符号已弃用，sema 发出警告。       |
| `@link_name("sym")`  | 覆盖 mangle 后的导出名。              |
| `@target("a,b,...")` | 限定符号可用的架构。                  |

未知注解仅产生 sema 警告，便于第三方工具在不改动编译器的情况下扩展目录。

## 构建

```bash
./build/polyc tests/samples/39_visibility_attrs/visibility_attrs.ploy \
    -o /tmp/sample39.o
```

## 后续工作

* 将各属性接入优化器 / Profiler 流水线（当前属性能够穿过解析与 sema，
  但尚未驱动 lowering 决策）。
* 把可见性 / 属性前缀扩展到模块作用域的 `CLASS`、`CONST`、`TYPE`、`LET`。
* 引入嵌套 `MODULE name { ... }` 以提供更细粒度的可见性作用域。
