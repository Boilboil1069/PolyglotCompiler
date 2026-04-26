# Python / C++ / Rust Runtime Upgrade

> Demand: `2026-04-26-04` — "为什么 runtime 中 Python/C++/Rust 是轻量包装，而不是
> 完整实现呢？请帮我修改成完整的实现。"

## Summary

`runtime/src/libs/python_rt.{c,h}`, `runtime/src/libs/cpp_rt.{cpp,h}`, and
`runtime/src/libs/rust_rt.{c,h}` have been promoted from minimal
`print` / `strdup` / `release` shims to first-class language bridges that match
the depth of `java_rt`, `dotnet_rt`, `javascript_rt`, and `ruby_rt`.

| File                            | Old size | New size | Status                         |
| ------------------------------- | -------- | -------- | ------------------------------ |
| `runtime/src/libs/python_rt.c`  | 33 lines | ~470 lines | full CPython embedding       |
| `runtime/src/libs/cpp_rt.c` → `.cpp` | 30 lines | ~270 lines | dlopen + try/catch bridge |
| `runtime/src/libs/rust_rt.c`    | 30 lines | ~220 lines | cdylib loader + ABI helpers  |

The three pre-existing entry points
(`polyglot_<lang>_print`, `polyglot_<lang>_strdup_gc`, `polyglot_<lang>_release`)
remain ABI-compatible; all new functionality is purely additive.

## Design

All three follow the same blueprint already proven by `java_rt` and
`javascript_rt`:

1. **Loader layer** (`LoadLibraryA` on Windows, `dlopen` elsewhere) with a
   probe order: explicit `POLYGLOT_<LANG>_LIBRARY` env override, then
   well-known platform paths and version variants.
2. **Symbol table** of file-static function pointers — no host headers
   required at build time.
3. **Tagged-union value descriptors** allocated on the runtime's mimalloc
   raw heap; user-visible strings live on the GC heap and are rooted via
   `polyglot_gc_register_root`.
4. **Standalone fallback** so that even when the host runtime is absent
   (no libpython, no rustc-built cdylib, no foreign C++ shared library),
   the print and strdup helpers continue to work.

### Python (`python_rt.c`)

Embeds CPython 3.x via the stable C-API. Resolved symbols:

```
Py_Initialize / Py_InitializeEx / Py_IsInitialized / Py_Finalize
PyRun_SimpleStringFlags
PyImport_ImportModule
PyObject_GetAttrString / PyObject_SetAttrString
PyObject_CallObject / PyObject_Str
PyTuple_New / PyTuple_SetItem
PyUnicode_FromString / PyUnicode_AsUTF8
PyLong_FromLongLong / PyLong_AsLongLong
PyFloat_FromDouble / PyFloat_AsDouble
Py_IncRef / Py_DecRef
PyErr_Occurred / PyErr_Clear / PyErr_Print
```

Probe order: `POLYGLOT_PYTHON_LIBRARY`, then `PYTHONHOME`/`PYTHON_HOME`,
then `python3.dll` / `libpython3.so` / `libpython3.dylib` (and the
versioned variants for 3.8 → 3.13).

Public surface:

```c
int   polyglot_python_init(int version_hint);
void  polyglot_python_shutdown(void);
int   polyglot_python_run_string(const char *src);
void *polyglot_python_import(const char *module);
void *polyglot_python_get_attr(void *obj, const char *name);
int   polyglot_python_set_attr(void *obj, const char *name, void *v);
void *polyglot_python_call(void *callable, const void *const *argv, int n);
void *polyglot_python_call_method(void *recv, const char *m, …);
char *polyglot_python_value_to_string(void *v, void ***root_out);
long long polyglot_python_value_to_int(void *v);
double    polyglot_python_value_to_float(void *v);
void *polyglot_python_string_value(const char *utf8);
void *polyglot_python_integer_value(long long n);
void *polyglot_python_float_value(double n);
void *polyglot_python_none_value(void);
void  polyglot_python_release_value(void *v);
```

Each `PyObject*` wrap takes one strong reference (`Py_IncRef`) and releases
it on `polyglot_python_release_value` (`Py_DecRef`).  `PyTuple_SetItem`
steals references — the descriptor-to-`PyObject` converter accounts for
this automatically.

### C++ (`cpp_rt.cpp`)

C++ is the host language of the runtime, so this bridge is *not* a VM
embedder — it is the gate between extern-C consumers and arbitrary
foreign C++ shared libraries.  The file moved from `.c` to `.cpp` so that
the trampolines can use `try`/`catch` to keep stray exceptions from
unwinding through C frames.

Public surface:

```c
int   polyglot_cpp_init(int version_hint);
void  polyglot_cpp_shutdown(void);
void *polyglot_cpp_load_library(const char *path);
void  polyglot_cpp_unload_library(void *handle);
void *polyglot_cpp_resolve_symbol(void *handle, const char *symbol);
char *polyglot_cpp_demangle(const char *mangled, void ***root_out);

// Try-call trampolines (return 0 on success, -1 if the call threw)
int polyglot_cpp_try_call_void_void(fn);
int polyglot_cpp_try_call_void_str (fn, const char *arg);
int polyglot_cpp_try_call_i64_void (fn, long long *out);
int polyglot_cpp_try_call_i64_i64  (fn, long long arg, long long *out);
int polyglot_cpp_try_call_f64_f64  (fn, double arg,    double *out);

const char *polyglot_cpp_last_exception(void);
void        polyglot_cpp_clear_exception(void);
```

The "last exception" message is stored in a `thread_local std::string` so
the C ABI surface returns a stable `c_str()` between try-call calls on the
same thread.  Demangling uses Itanium `abi::__cxa_demangle` on
GCC/Clang; on MSVC the input is duplicated verbatim (UnDecorateSymbolName
would pull in dbghelp just for diagnostics).

Loaded library handles are tracked under a `std::mutex`-guarded vector so
that `polyglot_cpp_shutdown` releases everything cleanly.

### Rust (`rust_rt.c`)

Rust crates participate as `cdylib` shared libraries that expose
`extern "C"` symbols. The bridge is therefore a thin loader plus a panic-
safe ABI mirror.  Rust panics across FFI are UB by language convention,
so every richer call shape returns the explicit `polyglot_rust_result_t`
struct that the crate populates after running its body inside
`std::panic::catch_unwind`:

```c
typedef struct {
  int ok;             // 1 = success, 0 = failure
  void *value;        // success payload (opaque)
  const char *error;  // borrowed UTF-8; valid until next call
} polyglot_rust_result_t;
```

Public surface:

```c
int   polyglot_rust_init(int version_hint);
void  polyglot_rust_shutdown(void);
void *polyglot_rust_load_crate(const char *path);   // bare name OK
void  polyglot_rust_unload_crate(void *handle);
void *polyglot_rust_resolve(void *handle, const char *symbol);

polyglot_rust_result_t polyglot_rust_call(fn, const void *const *argv, int argc);
long long polyglot_rust_call_i64(fn, long long arg);
double    polyglot_rust_call_f64(fn, double arg);
char     *polyglot_rust_call_str(fn, const char *arg, void ***root_out);

void *polyglot_rust_slice_make(const void *ptr, size_t len, size_t elem);
void  polyglot_rust_slice_destroy(void *slice);
```

`polyglot_rust_load_crate` first tries the path verbatim, then falls back
to the platform-canonical decoration (`<name>.dll` / `lib<name>.so` /
`lib<name>.dylib`) so callers can hand it bare crate names.  Loaded
handles go onto a runtime-internal list and are released en masse on
shutdown.

The slice helper packages `(ptr, len, elem_size)` on the raw heap so a
Rust `extern "C"` constructor can read it back into a real
`core::slice::from_raw_parts` view on the other side.

## Build integration

Only one CMake change was required:

```diff
- src/libs/cpp_rt.c
+ src/libs/cpp_rt.cpp
```

in `runtime/CMakeLists.txt`.  All other plumbing (mimalloc-static,
`fmt::fmt-header-only`, GC bridge in `base.h`) was already in place from
demand `2026-04-26-02`.

## Backward compatibility

No call site outside `runtime/src/libs/` referenced any of the
`polyglot_python_*` / `polyglot_cpp_*` / `polyglot_rust_*` symbols
(verified via grep over the whole tree before the upgrade), and the
three legacy entry points kept their signatures intact.  Hence no other
module needed to change for this work.

## Related demands

* `2026-04-26-01` — Go / JavaScript / Ruby frontends + runtimes
  (companion runtime work for the new languages).
* `2026-04-26-02` — mimalloc + fmt orphan-dependency fix
  (provides `polyglot_raw_*` and the GC heap used by these descriptors).
