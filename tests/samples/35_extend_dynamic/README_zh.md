# 35_extend_dynamic —— `EXTEND` 仅限动态宿主语言

`extend_dynamic.ploy` 展示需求 2026-04-28-11 之后 `EXTEND` 的**合法**
形式：language 参数只能是 `python`、`ruby`、`javascript`（及其标签
别名 `rb`、`js`、`ts`）。其语义是"宿主语言层 monkey-patch"，覆写在
程序加载时被安装到外部运行时的方法派发表里。外部对象**不会**进入
ploy 的静态类型系统。

```ploy
EXTEND(python, torch::nn::Module) AS LinearReLU {
    FUNC forward(x: f64) -> f64 { RETURN x; }
}
```

## 被拒绝的写法

sema 会对所有静态类型语言上的 `EXTEND` 报告以下诊断：

```
EXTEND is not allowed on statically-typed language 'rust'
  — its type system cannot accept an out-of-source subclass without
    breaking soundness
suggestion: wrap the foreign API in a local Ploy FUNC and use
            CALL / METHOD instead, or move the EXTEND target to a
            dynamic host (python / ruby / javascript)
```

被拒绝的语言集合为 `cpp`、`c`、`rust`、`java`、`dotnet`、`csharp`、
`go`、`golang`。

## 迁移路径

如果原本想在静态语言上做扩展点，请把 `EXTEND` 替换为一个本地 ploy
FUNC，再加上 `CALL` / `METHOD`：

```ploy
// 之前（会被拒绝）：
//   EXTEND(rust, tokio::Task) AS MyTask { FUNC run(id: i32) -> i32 { ... } }

// 之后：
FUNC my_task_run(id: i32) -> i32 {
    RETURN CALL(rust, tokio_task_run, id);
}
```

英文版位于 [`README.md`](README.md)。
