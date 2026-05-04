# 异步模型 —— 协作式 `Future<T>` 桥 (since v1.14.0)

## 表层语法

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC chained() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN 0;
}
```

* `ASYNC FUNC name(...) -> T { ... }` —— 声明返回值在 ABI 边界被
  隐式包装为 `Future<T>` 的异步函数；面向开发者的类型仍为 `T`。
* `AWAIT <expr>` —— 暂停所在 `ASYNC FUNC` 直至 future 解析完成，
  随后产出已解析的负载。仅允许出现在 `ASYNC FUNC` 体内，
  其它位置由 sema 拒绝。

词法器将 `ASYNC` 与 `AWAIT` 注册为无条件保留字。
解析器在顶层、类方法及语句位置接受 `ASYNC FUNC`，
所得 `FuncDecl` AST 节点带有 `is_async = true`。
`AwaitExpression` 是位于一元优先级层 (与 `NOT` 同级) 的独立 AST 节点。

## 下沉形态

对每个 `ASYNC FUNC`，下沉阶段产出一个被两个调度器标记包夹的普通 IR 函数：

```text
function fn(...) -> ret_type {
entry:
    call void @__ploy_rt_async_enter()
    ; ... body ...
    call void @__ploy_rt_async_complete()  ; 隐式 return 路径
    ret ...
}
```

每个 `AWAIT <expr>` 处，操作数被下沉为不透明的 future 句柄
(当前是被重解释为 `i8*` 的 64-bit id) 并传入桥接函数：

```text
%await.value = call i8* @__ploy_rt_await(i8* %handle)
```

当宿主语言适配器观察到原生 awaitable 就绪时，
通过 `__ploy_rt_future_resolve` 回环句柄。

## 运行时 ABI

| 符号                                | 用途                                              |
| ----------------------------------- | ------------------------------------------------- |
| `__ploy_rt_async_enter`             | 函数入口处递增活动帧计数                          |
| `__ploy_rt_async_complete`          | 隐式 return 处递减计数                            |
| `__ploy_rt_async_spawn(fn, data)`   | 入队回调，返回 future 句柄 id                     |
| `__ploy_rt_await(handle)`           | 阻塞至 future 解析完成，返回负载                  |
| `__ploy_rt_future_resolve(id, ptr)` | 唤醒挂在该 id 上的任务                            |
| `__ploy_rt_async_run(max_ticks)`    | 驱动事件循环至空闲或达到 `max_ticks`              |
| `__ploy_rt_async_pending`           | 快照 —— 待执行任务数                              |
| `__ploy_rt_async_suspended`         | 快照 —— 挂起任务数                                |
| `__ploy_rt_async_completed`         | 快照 —— 已完成任务数                              |
| `__ploy_rt_async_active_frames`     | 快照 —— 当前位于 `async_enter` 内的帧数           |

`runtime/include/services/async_bridge.h` 中的 C++ 接口
(`SpawnPloyTask`、`ResolveFuture`、`RunUntilIdle`、
`SnapshotScheduler`、`ResetScheduler`) 是测试、`polyrt async`
CLI 与宿主适配器使用的入口。

## 跨语言宿主映射

| 宿主语言 | 原生 awaitable           | 适配器符号                   |
| -------- | ------------------------ | ---------------------------- |
| Python   | `asyncio` 协程           | `pyloy_async_resolve`        |
| C++      | `std::coroutine` / `co_await` | `cppploy_async_resolve` |
| Java     | `CompletableFuture`      | `jloy_async_resolve`         |
| .NET     | `Task<T>`                | `clrloy_async_resolve`       |
| Rust     | `Future` (executor 驱动) | `rsloy_async_resolve`        |

每个适配器封装其原生 awaitable，在宿主侧驱动至就绪态，
通过 `__ploy_rt_future_resolve` 上交解析后的负载。
事件循环在下一个 tick 唤醒挂起的 Ploy 任务。

## 调度器观测

`polyrt async` 输出协作式事件循环的快照：

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

## 未来工作

* 将轮询式 `__ploy_rt_await` 替换为真正的协程挂起点，
  与 v1.13.0 后续的 `invoke` / `landingpad` 工作共享机制。
* 基于 `runtime/threading.{h,cpp}` 的多线程 work-stealing 线程池。
* 跨调度边界的取消传播。
* 为每种受支持的宿主 awaitable 实现反向适配器
  (Python `asyncio`、Rust `Future`、C++20 协程、Java
  `CompletableFuture`、.NET `Task<T>`)。
* 待泛型 (demand 2026-04-28-15) 落地后，赋予 `Future<T>` 参数化类型。
