# Async Model — Cooperative `Future<T>` Bridge (since v1.14.0)

## Surface syntax

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC chained() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN 0;
}
```

* `ASYNC FUNC name(...) -> T { ... }` — declares an asynchronous
  function whose return type `T` is implicitly wrapped as
  `Future<T>` at the ABI boundary; the developer-facing type stays
  `T`.
* `AWAIT <expr>` — suspends the surrounding `ASYNC FUNC` until the
  awaited future resolves, then yields the resolved payload.  Only
  legal inside an `ASYNC FUNC` body; sema rejects any other use.

The lexer registers `ASYNC` and `AWAIT` as unconditionally reserved
keywords.  The parser admits `ASYNC FUNC` at top-level, inside class
methods, and as a statement; the resulting `FuncDecl` AST node carries
`is_async = true`.  `AwaitExpression` is a dedicated AST node placed
in the unary-precedence layer (peer to `NOT`).

## Lowering shape

For an `ASYNC FUNC` the lowering pass emits a normal IR function
bracketed by two cooperative-scheduler markers:

```text
function fn(...) -> ret_type {
entry:
    call void @__ploy_rt_async_enter()
    ; ... body ...
    call void @__ploy_rt_async_complete()  ; on the implicit return path
    ret ...
}
```

For every `AWAIT <expr>` site the operand is lowered to an opaque
future handle (currently a 64-bit id reinterpreted as `i8*`) and
passed to the bridge:

```text
%await.value = call i8* @__ploy_rt_await(i8* %handle)
```

The handle round-trips through `__ploy_rt_future_resolve` when an
adapter observes a native awaitable becoming ready.

## Runtime ABI

| symbol                              | purpose                                            |
| ----------------------------------- | -------------------------------------------------- |
| `__ploy_rt_async_enter`             | bump active-frame counter at function entry        |
| `__ploy_rt_async_complete`          | decrement counter at the implicit return path     |
| `__ploy_rt_async_spawn(fn, data)`   | enqueue a callable; returns a future-handle id     |
| `__ploy_rt_await(handle)`           | suspend until the future resolves; yields payload |
| `__ploy_rt_future_resolve(id, ptr)` | wake any task suspended on this id                 |
| `__ploy_rt_async_run(max_ticks)`    | drive the loop until quiescent or `max_ticks`     |
| `__ploy_rt_async_pending`           | snapshot — pending tasks                           |
| `__ploy_rt_async_suspended`         | snapshot — suspended tasks                         |
| `__ploy_rt_async_completed`         | snapshot — completed tasks                         |
| `__ploy_rt_async_active_frames`     | snapshot — frames currently inside `async_enter`  |

The C++ surface in `runtime/include/services/async_bridge.h`
(`SpawnPloyTask`, `ResolveFuture`, `RunUntilIdle`,
`SnapshotScheduler`, `ResetScheduler`) is what tests, the
`polyrt async` CLI, and language adapters consume.

## Cross-language host mapping

| host language | native awaitable          | adapter symbol               |
| ------------- | ------------------------- | ---------------------------- |
| Python        | `asyncio` coroutine       | `pyloy_async_resolve`        |
| C++           | `std::coroutine` / `co_await` | `cppploy_async_resolve`  |
| Java          | `CompletableFuture`       | `jloy_async_resolve`         |
| .NET          | `Task<T>`                 | `clrloy_async_resolve`       |
| Rust          | `Future` (executor pump)  | `rsloy_async_resolve`        |

Each adapter wraps its native awaitable, drives it to readiness on
the host side, and surfaces the resolved payload through
`__ploy_rt_future_resolve`.  The cooperative event loop wakes the
suspended Ploy task on the next tick.

## Inspecting the scheduler

`polyrt async` prints a snapshot of the cooperative event loop:

```
$ polyrt async
polyrt async: cooperative scheduler snapshot
  pending tasks       : 0
  suspended tasks     : 0
  completed tasks     : 0
  loop iterations     : 0
  active async frames : 0
```

`polyrt async --json` emits the same payload as a single-line JSON
object.  `polyrt async --run[=N]` drives the loop for at most `N`
ticks before reporting.

## Future work

* Replace the polled `__ploy_rt_await` with a true coroutine
  suspension point, sharing the IR-level `invoke` / `landingpad`
  machinery scheduled for the v1.13.0 follow-up.
* Multi-thread work-stealing pool layered on top of
  `runtime/threading.{h,cpp}`.
* Cancellation propagation across the cooperative scheduler boundary.
* Cross-language reverse-path adapters for every supported host
  awaitable (Python `asyncio`, Rust `Future`, C++20 coroutine, Java
  `CompletableFuture`, .NET `Task<T>`).
* `Future<T>` parametric typing once generics (demand
  2026-04-28-15) land in sema.
