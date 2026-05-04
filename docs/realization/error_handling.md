# Error Handling Realization

This note describes how the PolyglotCompiler v1.13.0 release realises
structured exception handling for Ploy and how the runtime bridge maps
host-language exceptions onto the unified `Error` handle.

## Surface syntax

Ploy v1.13.0 adds five keywords:

| keyword   | role                                                           |
| --------- | -------------------------------------------------------------- |
| `TRY`     | starts a protected body                                        |
| `CATCH`   | introduces a binding for a caught Error                        |
| `FINALLY` | unconditional cleanup block                                    |
| `THROW`   | raises a value into the surrounding handler                    |
| `ERROR`   | name of the built-in handle type                               |

A complete form is:

```ploy
TRY {
    body_statements;
}
CATCH (e: Error) {
    handler_statements;
}
FINALLY {
    cleanup_statements;
}
```

A bare `TRY` without any `CATCH` or `FINALLY` is rejected by sema.
Multiple `CATCH` clauses are accepted; the first one whose binding
type matches wins (typed-catch dispatch is currently nominal-only and
treats every clause as `Error`).  A standalone `FINALLY` (no `CATCH`)
is allowed.

## Built-in `Error` handle

The catch binding has the built-in handle type `Error`, defined as

```ploy
HANDLE Error {
    message:    String;
    source_lang: String;
    stacktrace: List<String>;
}
```

The runtime owns the storage; the fields are valid only inside the
enclosing `CATCH` body.  `source_lang` carries one of `ploy`,
`python`, `cpp`, `java`, `dotnet`, `rust`.

## IR shape

The lowering emits the following pseudo-IR for a `TRY` / `CATCH` /
`FINALLY`:

```
try.entry:
    %tag = call i32 __ploy_rt_try_begin()
    %thrown = icmp ne i32 %tag, 0
    br i1 %thrown, label %try.catch, label %try.body
try.body:
    <protected body>
    call void __ploy_rt_try_end()
    br label %try.finally   ; or %try.merge if no FINALLY
try.catch:
    %err = call ptr __ploy_rt_current_error()
    <bind err to declared name>
    <catch body>
    call void __ploy_rt_clear_error()
    br label %try.finally   ; or %try.merge
try.finally:
    <finally body>
    br label %try.merge
try.merge:
    <continuation>
```

`THROW <expr>;` lowers to a single call to `__ploy_rt_throw(<value>)`
followed by `unreachable`.

## Runtime ABI

`runtime/include/services/error_bridge.h` exposes the C ABI:

| symbol                                          | role                                                    |
| ----------------------------------------------- | ------------------------------------------------------- |
| `__ploy_rt_try_begin()`                         | push handler scope; returns 0 on first entry            |
| `__ploy_rt_try_end()`                           | pop handler scope on normal exit                        |
| `__ploy_rt_throw(msg)`                          | raise an Error tagged `ploy`                            |
| `__ploy_rt_throw_from(msg, lang)`               | raise an Error tagged with a host-language label        |
| `__ploy_rt_current_error()`                     | opaque pointer to the live Error                        |
| `__ploy_rt_current_error_message()`             | NUL-terminated message                                  |
| `__ploy_rt_current_error_source_lang()`         | NUL-terminated source language                          |
| `__ploy_rt_current_error_stacktrace_count()`    | frame count                                             |
| `__ploy_rt_current_error_stacktrace_at(i)`      | frame `i`, or `nullptr` if `i` is out of range          |
| `__ploy_rt_clear_error()`                       | mark the current Error as consumed                      |

The data plane is thread-local; the storage holding a payload is owned
by the runtime for the duration of the enclosing `CATCH` body.

`__ploy_rt_throw` raises a C++ `polyglot::runtime::services::RuntimeError`
that any enclosing native frame can catch.  Direct setjmp/longjmp at
the IR site is not used because the lowering emits a regular call,
not a returns-twice intrinsic; integrating the IR-level conditional
branch with the C++ unwind path is tracked under future work.

## Host-language mapping

| host language | foreign type        | adapter entry point     |
| ------------- | ------------------- | ----------------------- |
| Python        | `Exception`         | `pyloy_throw_python`    |
| C++           | `std::exception`    | `cppploy_throw_cxx`     |
| Java          | `Throwable`         | `jloy_throw_java`       |
| .NET          | `Exception`         | `clrloy_throw_dotnet`   |
| Rust          | `Result::Err`       | `rsloy_throw_rust`      |

Each adapter calls `__ploy_rt_throw_from(msg, lang)` with the
language tag from the table above.  The reverse direction — handing a
Ploy `Error` back to host-language code — is performed by the
language-specific bridge layers when a Ploy callee returns through a
foreign call boundary; today this is implemented for the data plane
only (the foreign caller can read the current Error via
`__ploy_rt_current_error_*`) with the IR-level dispatcher tracked
under future work.

## Future work

* IR-level dispatch using `invoke` / `landingpad` so that every
  potentially-throwing call inside a `TRY` body routes to the correct
  catch via real exception unwinding.
* Typed-catch dispatch: select the matching `CATCH` clause based on
  the Error's source language or a user-defined Error subtype.
* Postfix `?` short-circuit propagation (`expr?`): rewritten to a
  synthetic `TRY` that re-raises a caught Error from the enclosing
  function.
* Cross-language reverse-path interception: routing a Ploy `Error`
  raised inside a Ploy callback into the host-language exception
  hierarchy of the foreign caller.
