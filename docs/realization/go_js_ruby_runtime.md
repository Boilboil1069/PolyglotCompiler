# Go / JavaScript / Ruby Runtime Bridges

This document describes how the polyglot runtime supports the three additional
front-ends introduced by demand `2026-04-26-01`.  Each runtime lives under
`runtime/src/libs/<lang>_rt.c` with a matching `.h` header in
`runtime/include/libs/`, and is compiled into the shared `runtime` library
together with the existing Python / C++ / Rust / Java / .NET bridges.

The three runtimes follow the same overall pattern as `java_rt` and
`dotnet_rt`:

1. **Loader layer** — Resolves the host engine (libnode, libruby, …) lazily
   through `LoadLibrary` / `dlopen`, falling back to a stand-alone mode that
   keeps `print` / `strdup` working without a host runtime.
2. **Symbol table** — A set of file-static function pointers that mirror the
   C ABI of the host engine.  No headers from the host engine are required at
   build time, so the bridges compile on minimal CI images.
3. **Value descriptors** — A tagged-union heap object backed by mimalloc
   (`polyglot_raw_*`) that wraps strings, scalars and engine-side references.
4. **GC integration** — Strings allocated for callers are placed on the
   `polyglot_alloc` (GC) heap and rooted via `polyglot_gc_register_root`,
   exactly like the existing language bridges.

## Go runtime (`go_rt`)

Go's GC, goroutine scheduler and channel primitives cannot be embedded into a
host C/C++ process without a hard build-time dependency on the Go toolchain
(`-buildmode=c-archive`).  PolyglotCompiler therefore re-implements the
*minimum* runtime surface needed by lowered Go IR:

| Symbol | Purpose |
| --- | --- |
| `polyglot_go_print` | `fmt.Println` semantic. |
| `polyglot_go_strdup_gc` / `polyglot_go_release` | GC-rooted string duplication. |
| `polyglot_go_spawn` / `polyglot_go_join` / `polyglot_go_detach` | Goroutine launch (mapped to OS threads). |
| `polyglot_go_yield` / `polyglot_go_num_cpu` | `runtime.Gosched` / `runtime.NumCPU`. |
| `polyglot_go_chan_make` / `polyglot_go_chan_send` / `polyglot_go_chan_recv` / `polyglot_go_chan_close` / `polyglot_go_chan_destroy` | Bounded MPMC channel (`capacity == 0` is rendezvous). |
| `polyglot_go_defer_push` / `polyglot_go_defer_run` | Per-frame LIFO defer stack. |

All bookkeeping (channel ring buffers, defer nodes, routine handles) lives on
the raw mimalloc heap.  Channel payloads are byte-copied with `polyglot_memcpy`
so value semantics match Go.

## JavaScript runtime (`javascript_rt`)

The JavaScript bridge dynamically loads a Node.js / libnode build through the
`node.dll` / `libnode.so` / `libnode.dylib` symbols that ship with every Node
installation.  Probe order is:

1. `POLYGLOT_JS_LIBRARY` environment override (used by tests).
2. `NODE_HOME/<lib-or-bin>/<libnode|node>` per platform.
3. Default loader search path.

If no engine library is found, every entry point that requires a live VM
returns `NULL` while `polyglot_js_print` and the strdup helpers continue to
work via libc.  This keeps simple lowered `console.log` programs functional on
hosts that do not have Node installed.

The bridge exposes the N-API surface that lowered IR actually needs:

| Symbol | Purpose |
| --- | --- |
| `polyglot_js_init` / `polyglot_js_shutdown` | Engine lifecycle. |
| `polyglot_js_print` / `polyglot_js_strdup_gc` / `polyglot_js_release` | Always-available helpers. |
| `polyglot_js_eval` | Calls `eval(source)` on the global object. |
| `polyglot_js_get_global` / `polyglot_js_get_property` | Property look-up. |
| `polyglot_js_call_function` | Generic call site. |
| `polyglot_js_value_to_string` / `polyglot_js_value_to_number` | Value coercion. |
| `polyglot_js_string_value` / `polyglot_js_number_value` | Boxing primitives. |
| `polyglot_js_release_value` | Releases any value handle. |

The `polyrt --js-host` mode (planned) is responsible for bootstrapping the
Node embedder API and handing the resulting `napi_env` to
`polyglot_js_register_env`.  Until that mode is enabled, eval / call paths
fall back to standalone behaviour as documented above.

## Ruby runtime (`ruby_rt`)

The Ruby bridge loads MRI / CRuby (2.7, 3.0, 3.1, 3.2, 3.3) through libruby's
stable C ABI: `ruby_setup`, `rb_eval_string_protect`, `rb_funcallv`,
`rb_str_new_cstr`, `rb_string_value_cstr`, `rb_int2inum`, `rb_float_new`,
`rb_const_get`, `rb_intern`, `rb_gc_register_address`,
`rb_gc_unregister_address`, plus the data symbol `rb_cObject` for top-level
constant lookup.

The probe order is identical to the JS bridge (override → `RUBY_ROOT` →
default search path) and Ruby `VALUE`s returned from the engine are rooted via
`rb_gc_register_address` for as long as the wrapper handle lives.  Releasing
the wrapper unregisters the root before freeing the descriptor.

| Symbol | Purpose |
| --- | --- |
| `polyglot_ruby_init` / `polyglot_ruby_shutdown` | VM lifecycle. |
| `polyglot_ruby_print` / `polyglot_ruby_strdup_gc` / `polyglot_ruby_release` | Always-available helpers. |
| `polyglot_ruby_require` | `require "<feature>"`. |
| `polyglot_ruby_eval` | Wraps `rb_eval_string_protect`. |
| `polyglot_ruby_get_constant` | Resolves `A::B::C` paths. |
| `polyglot_ruby_call_method` | Invokes a method on a receiver (or top-level Kernel methods when `receiver == NULL`). |
| `polyglot_ruby_value_to_string` | `obj.to_s` returning a GC-rooted UTF-8 buffer. |
| `polyglot_ruby_string_value` / `polyglot_ruby_integer_value` / `polyglot_ruby_float_value` | Boxing primitives. |
| `polyglot_ruby_release_value` | Releases any value handle (and unroots VM values). |

## Build wiring

All three sources are listed in `runtime/CMakeLists.txt`:

```cmake
add_library(runtime
    ...
    src/libs/go_rt.c
    src/libs/javascript_rt.c
    src/libs/ruby_rt.c
    src/memory/polyglot_alloc.cpp
    ...
)
```

They share the existing `mimalloc-static` and `fmt::fmt-header-only` link
dependencies and are exported as part of the same shared `runtime` library
that downstream tools (`polyc`, `polyrt`, all test binaries) already consume,
so no further wiring is needed in the build system.
