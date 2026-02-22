# 运行时 C ABI 参考

本文档规定了 PolyglotCompiler 运行时导出的 **C 链接 ABI**。以下列出的
所有符号均使用 `extern "C"` 链接，使生成的代码（无论源语言如何）都可以
通过扁平、稳定的二进制接口调用它们。

C++ API（类、模板、命名空间）是*实现*层；C ABI 是生成的目标代码和
polyglot 链接器所消费的*契约*层。

---

## 1 设计原则

| 原则 | 详情 |
|------|------|
| **扁平 C 符号** | 每个运行时入口点都是带有明确名称的 `extern "C"` |
| **不透明指针** | 跨语言对象以 `void*` 传递；运行时负责重新解释 |
| **GC 集成** | 分配通过 `polyglot_alloc`；根必须注册 |
| **无异常** | C ABI 不抛出异常；错误通过返回值（`nullptr`、`false`）指示 |

---

## 2 核心服务 (`runtime/include/libs/base.h`)

| 符号 | 签名 | 描述 |
|------|------|------|
| `polyglot_alloc` | `void*(size_t)` | GC 支持的分配 |
| `polyglot_gc_collect` | `void()` | 触发垃圾回收 |
| `polyglot_gc_register_root` | `void(void**)` | 注册 GC 根槽 |
| `polyglot_gc_unregister_root` | `void(void**)` | 注销 GC 根槽 |
| `polyglot_alloc_rooted` | `void*(size_t)` | 分配 + 注册根（便捷函数） |
| `polyglot_memcpy` | `void*(void*,const void*,size_t)` | 内存复制 |
| `polyglot_memcmp` | `int(const void*,const void*,size_t)` | 内存比较 |
| `polyglot_memset` | `void*(void*,int,size_t)` | 内存设置 |
| `polyglot_strlen` | `size_t(const char*)` | 字符串长度 |
| `polyglot_strcpy` | `char*(char*,const char*)` | 字符串复制 |
| `polyglot_strncpy` | `char*(char*,const char*,size_t)` | 有界字符串复制 |
| `polyglot_strcmp` | `int(const char*,const char*)` | 字符串比较 |
| `polyglot_strncmp` | `int(const char*,const char*,size_t)` | 有界字符串比较 |
| `polyglot_println` | `void(const char*)` | 向 stdout 输出一行 |
| `polyglot_read_file` | `bool(const char*,char**,size_t*)` | 读取文件到缓冲区 |
| `polyglot_write_file` | `bool(const char*,const char*,size_t)` | 写入缓冲区到文件 |
| `polyglot_free_file_buffer` | `void(char*)` | 释放 `polyglot_read_file` 的缓冲区 |

---

## 3 语言桥接符号

### 3.1 C++ 桥接 (`runtime/include/libs/cpp_rt.h`)

| 符号 | 签名 | 描述 |
|------|------|------|
| `polyglot_cpp_print` | `void(const char*)` | 从 C++ 上下文打印 |
| `polyglot_cpp_strdup_gc` | `char*(const char*,void***)` | 复制字符串到 GC 内存 |
| `polyglot_cpp_release` | `void(char**,void***)` | 释放 GC 根 |

### 3.2 Python 桥接 (`runtime/include/libs/python_rt.h`)

| 符号 | 签名 | 描述 |
|------|------|------|
| `polyglot_python_strdup_gc` | `char*(const char*,void***)` | 复制字符串到 GC 内存 |
| `polyglot_python_release` | `void(char**,void***)` | 释放 GC 根 |

### 3.3 Java 桥接 (`runtime/include/libs/java_rt.h`)

提供 JNI 初始化、方法调用和对象生命周期管理。

### 3.4 .NET 桥接 (`runtime/include/libs/dotnet_rt.h`)

提供 CoreCLR 主机初始化、托管方法调用和对象生命周期管理。

### 3.5 Rust 桥接 (`runtime/include/libs/rust_rt.h`)

提供与 FFI 兼容的入口点，用于调用 Rust 代码。

---

## 4 互操作 / 对象生命周期 (`runtime/include/interop/object_lifecycle.h`)

这些 `__ploy_*` 符号由 `.ploy` IR 降低阶段发出，在链接时解析：

| 符号 | 签名 | 描述 |
|------|------|------|
| `__ploy_py_del` | `void(void*)` | 释放 Python 对象（减少引用计数） |
| `__ploy_cpp_delete` | `void(void*)` | 删除 C++ 对象 |
| `__ploy_rust_drop` | `void(void*)` | 销毁 Rust 值 |
| `__ploy_java_release` | `void(void*)` | 释放 JNI 全局引用 |
| `__ploy_dotnet_dispose` | `void(void*)` | 释放 .NET 对象 |
| `__ploy_extend_register` | `void(const char*,const char*,const char*)` | 注册跨语言类扩展 |
| `__ploy_rt_string_convert` | `char*(const void*,size_t)` | 跨语言字符串转换 |
| `__ploy_rt_memcpy` | `void*(void*,const void*,size_t)` | 运行时级 memcpy |
| `__ploy_rt_convert_tuple` | `void*(void*)` | 元组布局转换 |
| `__ploy_rt_dict_convert` | `void*(void*)` | 字典布局转换 |
| `__ploy_rt_convert_struct` | `void*(void*)` | 结构体布局转换 |

---

## 5 容器编组 (`runtime/include/interop/container_marshal.h`)

### 5.1 数据结构

| 类型 | 字段 | 描述 |
|------|------|------|
| `RuntimeList` | `count`、`capacity`、`elem_size`、`data` | 动态列表 (`LIST[T]`) |
| `RuntimeTuple` | `num_elements`、`offsets`、`data` | 异构元组 |
| `RuntimeDict` | `count`、`bucket_count`、`key_size`、`value_size`、`buckets` | 哈希映射 (`DICT[K,V]`) |

### 5.2 List API

| 符号 | 签名 |
|------|------|
| `__ploy_rt_list_create` | `void*(size_t elem_size, size_t capacity)` |
| `__ploy_rt_list_push` | `void(void*,const void*)` |
| `__ploy_rt_list_get` | `void*(void*,size_t)` |
| `__ploy_rt_list_len` | `size_t(void*)` |
| `__ploy_rt_list_free` | `void(void*)` |

### 5.3 Tuple API

| 符号 | 签名 |
|------|------|
| `__ploy_rt_tuple_create` | `void*(size_t,const size_t*)` |
| `__ploy_rt_tuple_get` | `void*(void*,size_t)` |
| `__ploy_rt_tuple_free` | `void(void*)` |

### 5.4 Dict API

| 符号 | 签名 |
|------|------|
| `__ploy_rt_dict_create` | `void*(size_t,size_t)` |
| `__ploy_rt_dict_insert` | `void(void*,const void*,const void*)` |
| `__ploy_rt_dict_lookup` | `void*(void*,const void*)` |
| `__ploy_rt_dict_len` | `size_t(void*)` |
| `__ploy_rt_dict_free` | `void(void*)` |

---

## 6 GC C++ API 与 C ABI 对比

| 关注点 | C++ API (`polyglot::runtime::gc::GC`) | C ABI (`polyglot_*`) |
|--------|---------------------------------------|----------------------|
| 分配 | `GC::Allocate(size)` | `polyglot_alloc(size)` |
| 回收 | `GC::Collect()` | `polyglot_gc_collect()` |
| 根注册 | `GC::RegisterRoot(slot)` | `polyglot_gc_register_root(slot)` |
| 统计 | `GC::GetStats()` → `GCStats` | 未通过 C ABI 暴露 |

C ABI 函数是委托给 GC 单例的薄包装。生成的代码总是调用 C ABI；C++ API
用于宿主端工具（`polyrt`、基准测试）。

---

## 7 稳定性保证

- **C ABI 符号** 在次要版本间保持稳定；移除需要经过弃用周期。
- **C++ API** 是内部的，可能在不通知的情况下更改。
- **`__ploy_*` 符号** 由降低层发出并由链接器消费；遵循与 C ABI 符号
  相同的稳定性规则。
