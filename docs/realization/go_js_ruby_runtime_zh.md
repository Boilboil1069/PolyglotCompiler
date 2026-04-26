# Go / JavaScript / Ruby 运行时桥接

本文档描述了 polyglot 运行时如何为 `2026-04-26-01` 引入的三个新前端提供运行时支持。每个运行时位于 `runtime/src/libs/<lang>_rt.c`，对应头文件位于 `runtime/include/libs/`，并与现有 Python / C++ / Rust / Java / .NET 桥接一起编译进 `runtime` 共享库。

三个运行时遵循与 `java_rt`、`dotnet_rt` 一致的整体模式：

1. **加载层** — 通过 `LoadLibrary` / `dlopen` 懒加载宿主引擎（libnode、libruby 等），缺失时退化为独立模式（仍保留 `print` / `strdup` 能力）。
2. **符号表** — 一组文件作用域函数指针，对应宿主引擎的 C ABI；构建期不需要任何宿主引擎头文件，因此可在最小 CI 镜像上编译。
3. **值描述符** — 由 mimalloc（`polyglot_raw_*`）支持的 tagged-union 堆对象，封装字符串、标量与引擎侧引用。
4. **GC 集成** — 提供给调用方的字符串放在 `polyglot_alloc` 的 GC 堆上，并通过 `polyglot_gc_register_root` 注册根，与现有语言桥接完全一致。

## Go 运行时 (`go_rt`)

Go 的 GC、goroutine 调度器与 channel 原语无法在不强依赖 Go 工具链（`-buildmode=c-archive`）的前提下嵌入到宿主 C/C++ 进程，因此 PolyglotCompiler 重新实现了下列**最小必要**运行时面：

| 符号 | 用途 |
| --- | --- |
| `polyglot_go_print` | `fmt.Println` 语义。 |
| `polyglot_go_strdup_gc` / `polyglot_go_release` | GC-rooted 字符串复制。 |
| `polyglot_go_spawn` / `polyglot_go_join` / `polyglot_go_detach` | 启动 goroutine（映射到 OS 线程）。 |
| `polyglot_go_yield` / `polyglot_go_num_cpu` | `runtime.Gosched` / `runtime.NumCPU`。 |
| `polyglot_go_chan_make` / `polyglot_go_chan_send` / `polyglot_go_chan_recv` / `polyglot_go_chan_close` / `polyglot_go_chan_destroy` | 有界 MPMC channel（`capacity == 0` 即 rendezvous）。 |
| `polyglot_go_defer_push` / `polyglot_go_defer_run` | 按帧 LIFO defer 栈。 |

所有元数据（channel 环形缓冲、defer 节点、goroutine 句柄）都在 mimalloc 原始堆上分配；channel 载荷使用 `polyglot_memcpy` 按字节复制，从而保证与 Go 一致的值语义。

## JavaScript 运行时 (`javascript_rt`)

JavaScript 桥接通过 Node.js / libnode 自带的 `node.dll` / `libnode.so` / `libnode.dylib` 动态加载引擎，搜索顺序：

1. `POLYGLOT_JS_LIBRARY` 环境变量覆盖（测试夹具使用）。
2. 按平台拼接 `NODE_HOME/<lib-or-bin>/<libnode|node>`。
3. 默认加载器搜索路径。

若未找到引擎库，所有需要 VM 的入口返回 `NULL`，但 `polyglot_js_print` 与 strdup 辅助函数仍可通过 libc 工作，确保下沉后只调用 `console.log` 的程序在缺少 Node 的主机上仍可运行。

桥接暴露了下沉 IR 实际需要的 N-API 接口：

| 符号 | 用途 |
| --- | --- |
| `polyglot_js_init` / `polyglot_js_shutdown` | 引擎生命周期。 |
| `polyglot_js_print` / `polyglot_js_strdup_gc` / `polyglot_js_release` | 永远可用的辅助函数。 |
| `polyglot_js_eval` | 在 global 对象上调用 `eval(source)`。 |
| `polyglot_js_get_global` / `polyglot_js_get_property` | 属性查找。 |
| `polyglot_js_call_function` | 通用调用。 |
| `polyglot_js_value_to_string` / `polyglot_js_value_to_number` | 值转换。 |
| `polyglot_js_string_value` / `polyglot_js_number_value` | 装箱原语。 |
| `polyglot_js_release_value` | 释放任意值句柄。 |

`polyrt --js-host`（规划中）模式负责引导 Node 嵌入器 API 并把 `napi_env` 传给 `polyglot_js_register_env`；启用前，eval / call 路径会按上文规则退化为独立模式。

## Ruby 运行时 (`ruby_rt`)

Ruby 桥接通过 libruby 稳定 C ABI 加载 MRI / CRuby（2.7、3.0、3.1、3.2、3.3）：`ruby_setup`、`rb_eval_string_protect`、`rb_funcallv`、`rb_str_new_cstr`、`rb_string_value_cstr`、`rb_int2inum`、`rb_float_new`、`rb_const_get`、`rb_intern`、`rb_gc_register_address`、`rb_gc_unregister_address`，外加用于顶层常量解析的数据符号 `rb_cObject`。

搜索顺序与 JS 桥接相同（override → `RUBY_ROOT` → 默认路径）。引擎返回的 Ruby `VALUE` 在包装句柄存活期间通过 `rb_gc_register_address` 注册根；释放包装时反注册并释放描述符。

| 符号 | 用途 |
| --- | --- |
| `polyglot_ruby_init` / `polyglot_ruby_shutdown` | VM 生命周期。 |
| `polyglot_ruby_print` / `polyglot_ruby_strdup_gc` / `polyglot_ruby_release` | 永远可用的辅助函数。 |
| `polyglot_ruby_require` | `require "<feature>"`。 |
| `polyglot_ruby_eval` | 包装 `rb_eval_string_protect`。 |
| `polyglot_ruby_get_constant` | 解析 `A::B::C` 路径。 |
| `polyglot_ruby_call_method` | 在 receiver 上调用方法（`receiver == NULL` 时调用顶层 Kernel 方法）。 |
| `polyglot_ruby_value_to_string` | `obj.to_s`，返回 GC-rooted UTF-8 缓冲。 |
| `polyglot_ruby_string_value` / `polyglot_ruby_integer_value` / `polyglot_ruby_float_value` | 装箱原语。 |
| `polyglot_ruby_release_value` | 释放任意值句柄（同时反注册 VM 值的根）。 |

## 构建接入

三份源码均已在 `runtime/CMakeLists.txt` 列出：

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

它们与现有的 `mimalloc-static`、`fmt::fmt-header-only` 共用同一组链接依赖，并作为同一份 `runtime` 共享库的一部分对外导出，下游工具（`polyc`、`polyrt`、所有测试二进制）无需任何额外接入。
