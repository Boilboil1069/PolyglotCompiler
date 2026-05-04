# 36_try_catch — 结构化异常处理

`try_catch.ploy` 演示 v1.13.0 新增的 `TRY` / `CATCH` / `FINALLY` /
`THROW` 语法。受保护的代码块通过 `THROW` 抛出一个 Error，`CATCH (e:
Error)` 子句把捕获到的句柄绑定到名字 `e` 并执行处理体；`FINALLY`
块无条件执行，作为有保证的清理点。

```ploy
TRY {
    THROW "boom";
}
CATCH (e: Error) {
    PRINTLN "caught\r\n";
}
FINALLY {
    PRINTLN "cleanup\r\n";
}
```

## 内建 `Error` 句柄

捕获绑定的类型为 `Error`——一个内建句柄，公开如下字段：

| 字段          | 类型           | 说明                                       |
| ------------ | -------------- | ------------------------------------------ |
| `message`    | `String`       | 人类可读的描述                             |
| `source_lang`| `String`       | 源语言标签（`ploy`、`python`、`cpp`、     |
|              |                | `java`、`dotnet`、`rust`）                 |
| `stacktrace` | `List<String>` | 抛出点的尽力而为栈回溯                     |

存储由 Ploy 运行时持有；离开所在 CATCH 块后再读取这些字段属于未定义
行为。

## 跨语言拦截

运行时桥（`runtime/include/services/error_bridge.h`）暴露 C ABI，供
各语言适配器把宿主语言异常转发为统一的 `Error` 句柄：

* Python `Exception`
* C++ `std::exception`
* Java `Throwable`
* .NET `Exception`
* Rust `Result::Err`

反向路径——把宿主语言异常通过 IR 级派发引导进 Ploy 的 `CATCH` 子句——
通过 `__ploy_rt_throw_from` 接入数据平面。模型与未来工作详见
`docs/realization/error_handling_zh.md`。

## 输出

编译并运行后输出：

```
caught
cleanup
36_try_catch: ok
```
