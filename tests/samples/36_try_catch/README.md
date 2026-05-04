# 36_try_catch — Structured exception handling

`try_catch.ploy` demonstrates the `TRY` / `CATCH` / `FINALLY` / `THROW`
syntax introduced by the v1.13.0 release.  The protected body raises
an Error with `THROW`; the `CATCH (e: Error)` clause binds the caught
handle to a name and runs its body; the `FINALLY` block runs
unconditionally as a guaranteed cleanup point.

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

## Built-in `Error` handle

The catch binding has type `Error`, a built-in handle exposing:

| field        | type           | description                                  |
| ------------ | -------------- | -------------------------------------------- |
| `message`    | `String`       | human-readable description                   |
| `source_lang`| `String`       | originating language tag (`ploy`, `python`,  |
|              |                | `cpp`, `java`, `dotnet`, `rust`)             |
| `stacktrace` | `List<String>` | best-effort stack trace at the throw site    |

The Ploy runtime owns the storage; reading these fields after the
enclosing CATCH block returns is undefined.

## Cross-language interception

The runtime bridge (`runtime/include/services/error_bridge.h`) exposes
the C ABI used by per-language adapters to forward foreign exceptions
into the unified `Error` handle:

* Python `Exception`
* C++ `std::exception`
* Java `Throwable`
* .NET `Exception`
* Rust `Result::Err`

The reverse path — IR-level dispatch of foreign exceptions into a Ploy
`CATCH` clause — wires the data plane into `__ploy_rt_throw_from`.  See
`docs/realization/error_handling.md` for the model and the future-work
notes.

## Output

Running the compiled program prints:

```
caught
cleanup
36_try_catch: ok
```
