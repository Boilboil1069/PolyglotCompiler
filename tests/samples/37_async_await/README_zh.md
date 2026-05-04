# 37_async_await — 协作式异步 / await

`async_await.ploy` 演示 v1.14.0 引入的 `ASYNC` / `AWAIT` 语法。
`ASYNC FUNC` 声明的函数返回值会被隐式包装为 `Future<T>`；
`AWAIT <expr>` 暂停当前异步帧直至操作数的 future 解析完成，
随后产出已解析的负载。

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC chained() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN 0;
}
```

## 运行时模型

协作式调度器由以下文件实现：

* `runtime/include/services/async_bridge.h` /
  `runtime/src/services/async_bridge.cpp` —— 由 `ASYNC` / `AWAIT`
  下沉得到的 IR 调用的 C ABI，以及供宿主语言适配器与测试使用的
  C++ 辅助函数。
* `runtime/include/services/event_loop.h` /
  `runtime/src/services/event_loop.cpp` —— 单线程就绪 / 挂起队列。

下沉后的 IR 在每个异步函数序言处发出 `__ploy_rt_async_enter`，
在隐式返回处发出 `__ploy_rt_async_complete`，
在每个 AWAIT 处发出 `__ploy_rt_await(handle) -> i8*`。

## 跨语言宿主映射

| 宿主语言 | 原生 awaitable           | 适配器符号                   |
| -------- | ------------------------ | ---------------------------- |
| Python   | `asyncio` 协程           | `pyloy_async_resolve`        |
| C++      | `std::coroutine` / `co_await` | `cppploy_async_resolve` |
| Java     | `CompletableFuture`      | `jloy_async_resolve`         |
| .NET     | `Task<T>`                | `clrloy_async_resolve`       |
| Rust     | `Future`                 | `rsloy_async_resolve`        |

每个适配器通过 `__ploy_rt_future_resolve` 将原生 awaitable 解析为
Ploy `Future<T>`，事件循环随后唤醒挂在该句柄上的任务。

## 调度器观测

`polyrt async` 输出当前调度器快照：

```
$ polyrt async
polyrt async: cooperative scheduler snapshot
  pending tasks       : 0
  suspended tasks     : 0
  completed tasks     : 0
  loop iterations     : 0
  active async frames : 0
```

`polyrt async --json` 以单行 JSON 输出同一负载；
`polyrt async --run[=N]` 在汇报前最多驱动 `N` 个 tick。

## 输出

```
37_async_await: ok
```

## 未来工作

* 完整的协作式协程挂起 (当前 `__ploy_rt_await` 轮询已解析负载表；
  真正的帧挂起与 v1.13.0 的 `invoke` / `landingpad` 工作合并推进)。
* 基于 `runtime/threading.{h,cpp}` 的多线程 work-stealing 线程池。
* 跨调度边界的取消传播。
* Python `asyncio`、Rust `Future`、C++20 coroutine、Java
  `CompletableFuture`、.NET `Task<T>` 的反向适配器。
