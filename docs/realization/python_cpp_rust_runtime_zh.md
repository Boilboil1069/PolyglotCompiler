# Python / C++ / Rust 运行时升级

> 需求条目：`2026-04-26-04` — "为什么 runtime 中 Python/C++/Rust 是轻量包装，
> 而不是完整实现呢？请帮我修改成完整的实现。"

## 摘要

`runtime/src/libs/python_rt.{c,h}`、`runtime/src/libs/cpp_rt.{cpp,h}`、
`runtime/src/libs/rust_rt.{c,h}` 已从仅有 `print`/`strdup`/`release` 三个函数
的轻量包装升级为与 `java_rt`、`dotnet_rt`、`javascript_rt`、`ruby_rt` 同档次的
完整语言桥。

| 文件                                  | 升级前 | 升级后    | 状态                            |
| ------------------------------------- | ------ | --------- | ------------------------------- |
| `runtime/src/libs/python_rt.c`        | 33 行  | 约 470 行 | 完整 CPython 嵌入               |
| `runtime/src/libs/cpp_rt.c` → `.cpp`  | 30 行  | 约 270 行 | dlopen + try/catch 异常网关     |
| `runtime/src/libs/rust_rt.c`          | 30 行  | 约 220 行 | cdylib 加载器 + ABI 帮手        |

原有的三个公开符号
（`polyglot_<lang>_print` / `polyglot_<lang>_strdup_gc` / `polyglot_<lang>_release`）
ABI 保持不变；所有新功能均为纯增量。

## 设计

三个桥统一遵循 `java_rt` / `javascript_rt` 沉淀下来的范式：

1. **加载层**：Windows 用 `LoadLibraryA`，其它平台用 `dlopen`，探测顺序为：
   显式 `POLYGLOT_<LANG>_LIBRARY` 环境变量覆盖 → 平台习惯路径 → 各版本变体。
2. **符号表**：文件级 `static` 函数指针，编译期不依赖宿主头文件。
3. **带标签联合的值描述符**：分配在 mimalloc 原始堆上；用户可见字符串放在
   GC 堆上并通过 `polyglot_gc_register_root` 加根。
4. **离线降级**：宿主运行时缺失时（无 libpython、无外部 cdylib、无外部 C++
   动态库）`print` 与 `strdup` 仍可工作，仅嵌入相关接口返回 NULL/-1。

### Python (`python_rt.c`)

通过稳定 C API 嵌入 CPython 3.x，解析的符号包括：

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

探测顺序：`POLYGLOT_PYTHON_LIBRARY` → `PYTHONHOME`/`PYTHON_HOME` →
`python3.dll` / `libpython3.so` / `libpython3.dylib`（含 3.8 ~ 3.13 版本变体）。

公开接口：

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

每次 wrap `PyObject*` 都会 `Py_IncRef` 一次，`polyglot_python_release_value` 时
`Py_DecRef` 释放。`PyTuple_SetItem` 会偷引用，描述符 → PyObject 的转换函数已
针对此规则正确处理。

### C++ (`cpp_rt.cpp`)

C++ 是运行时本身的宿主语言，因此这层不是嵌入式 VM，而是 extern-C 调用方与
任意外部 C++ 共享库之间的网关。源文件由 `.c` 改为 `.cpp` 是为了在调用 trampoline
中使用 `try`/`catch`，避免散逸异常穿透 C 框架。

公开接口：

```c
int   polyglot_cpp_init(int version_hint);
void  polyglot_cpp_shutdown(void);
void *polyglot_cpp_load_library(const char *path);
void  polyglot_cpp_unload_library(void *handle);
void *polyglot_cpp_resolve_symbol(void *handle, const char *symbol);
char *polyglot_cpp_demangle(const char *mangled, void ***root_out);

// 异常安全调用 trampoline（成功返回 0，抛异常返回 -1）
int polyglot_cpp_try_call_void_void(fn);
int polyglot_cpp_try_call_void_str (fn, const char *arg);
int polyglot_cpp_try_call_i64_void (fn, long long *out);
int polyglot_cpp_try_call_i64_i64  (fn, long long arg, long long *out);
int polyglot_cpp_try_call_f64_f64  (fn, double arg,    double *out);

const char *polyglot_cpp_last_exception(void);
void        polyglot_cpp_clear_exception(void);
```

"最近一次异常" 的文本以 `thread_local std::string` 保存，C ABI 在两次 try-call
之间返回稳定的 `c_str()`。退化逻辑：GCC/Clang 走 Itanium ABI 的
`abi::__cxa_demangle`；MSVC 直接复制原文（不引入 dbghelp 仅为诊断）。
所有载入的 handle 由互斥保护的 `std::vector` 跟踪，`polyglot_cpp_shutdown`
统一回收。

### Rust (`rust_rt.c`)

Rust crate 以 `cdylib` 形式参与 polyglot 程序，导出 `extern "C"` 符号。该桥因此
做成"加载器 + panic 安全 ABI 镜像"。Rust panic 跨 FFI 属语言级 UB，因此较富语义
的调用入口统一返回如下结构，由 crate 自身在 `std::panic::catch_unwind` 内填写：

```c
typedef struct {
  int ok;             // 1 成功 / 0 失败
  void *value;        // 成功载荷（不透明）
  const char *error;  // 借用 UTF-8 字符串，至下次调用前有效
} polyglot_rust_result_t;
```

公开接口：

```c
int   polyglot_rust_init(int version_hint);
void  polyglot_rust_shutdown(void);
void *polyglot_rust_load_crate(const char *path);   // 也支持只传 crate 名
void  polyglot_rust_unload_crate(void *handle);
void *polyglot_rust_resolve(void *handle, const char *symbol);

polyglot_rust_result_t polyglot_rust_call(fn, const void *const *argv, int argc);
long long polyglot_rust_call_i64(fn, long long arg);
double    polyglot_rust_call_f64(fn, double arg);
char     *polyglot_rust_call_str(fn, const char *arg, void ***root_out);

void *polyglot_rust_slice_make(const void *ptr, size_t len, size_t elem);
void  polyglot_rust_slice_destroy(void *slice);
```

`polyglot_rust_load_crate` 会先按原路径尝试，失败后再用平台前后缀（`.dll` /
`lib*.so` / `lib*.dylib`）组装一次再试，方便上层只传 crate 名。所有 handle
进入运行时内部链表，`polyglot_rust_shutdown` 统一释放。

切片帮手把 `(ptr, len, elem_size)` 三元组打包到原始堆上，对端 Rust 函数可
读回去并交给 `core::slice::from_raw_parts` 还原视图。

## 构建集成

`runtime/CMakeLists.txt` 仅一处改动：

```diff
- src/libs/cpp_rt.c
+ src/libs/cpp_rt.cpp
```

其它依赖（mimalloc-static、`fmt::fmt-header-only`、`base.h` 中的 GC 桥）已在
`2026-04-26-02` 中就绪。

## 兼容性

升级前对全树执行 `polyglot_python_*` / `polyglot_cpp_*` / `polyglot_rust_*` 的
全文搜索，确认所有引用都仅出现在 `runtime/src/libs/` 内部；同时保留了三个旧函数
的签名，故无任何下游模块需要随之改动。

## 相关需求

* `2026-04-26-01`：Go / JavaScript / Ruby 前端及运行时（新语言的运行时同伴工作）。
* `2026-04-26-02`：mimalloc + fmt 孤儿依赖修复（提供 `polyglot_raw_*` 与本文使用
  的 GC 堆基础设施）。
