# 37_async_await — Cooperative async / await

`async_await.ploy` demonstrates the `ASYNC` / `AWAIT` syntax
introduced by the v1.14.0 release.  An `ASYNC FUNC` declares a
function whose return value is implicitly wrapped as `Future<T>`;
`AWAIT <expr>` suspends the surrounding async frame until the
operand future resolves and yields the resolved payload.

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC chained() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN 0;
}
```

## Runtime model

The cooperative scheduler is implemented by:

* `runtime/include/services/async_bridge.h` /
  `runtime/src/services/async_bridge.cpp` — the C ABI consumed by
  code lowered from `ASYNC` / `AWAIT` and the C++ helpers used by
  language adapters and tests.
* `runtime/include/services/event_loop.h` /
  `runtime/src/services/event_loop.cpp` — the single-threaded
  ready/suspended queue that backs the bridge.

The lowered IR emits `__ploy_rt_async_enter` at every async function
prologue, `__ploy_rt_async_complete` at the implicit return path, and
`__ploy_rt_await(handle) -> i8*` at every AWAIT site.

## Cross-language host mapping

| host language | native awaitable          | adapter symbol               |
| ------------- | ------------------------- | ---------------------------- |
| Python        | `asyncio` coroutine       | `pyloy_async_resolve`        |
| C++           | `std::coroutine` / `co_await` | `cppploy_async_resolve`  |
| Java          | `CompletableFuture`       | `jloy_async_resolve`         |
| .NET          | `Task<T>`                 | `clrloy_async_resolve`       |
| Rust          | `Future`                  | `rsloy_async_resolve`        |

Each adapter resolves a Ploy `Future<T>` from its native awaitable
through `__ploy_rt_future_resolve`; the cooperative event loop then
wakes any task suspended on that handle.

## Inspecting the scheduler

`polyrt async` prints a snapshot of the loop:

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
object suitable for piping into tooling.  `polyrt async --run[=N]`
drives the loop for at most `N` ticks before reporting.

## Output

```
37_async_await: ok
```

## Future work

* Fully cooperative coroutine suspension (current `__ploy_rt_await`
  polls the resolved-payload table; true frame suspension is tracked
  alongside the IR-level `invoke` / `landingpad` work for v1.13.0).
* Multi-thread work-stealing pool layered over `runtime/threading.{h,cpp}`.
* Cancellation propagation across the cooperative scheduler boundary.
* Cross-language reverse-path adapters for Python `asyncio`, Rust
  `Future`, C++20 coroutines, Java `CompletableFuture`, .NET `Task<T>`.
