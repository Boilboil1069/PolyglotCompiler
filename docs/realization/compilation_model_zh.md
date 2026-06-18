# PolyglotCompiler 编译模型 — 常见问题

## 问题

> 现有 ploy 代码内部有 cpp 与 python 代码，编译后的二进制文件是编译了 cpp 与 python 后混合而成的吗？

---

## 回答

**只有在所有引用的实现符号和运行时符号都能被链接器解析时，`.ploy` 构建才会生成一个最终原生输出。**

PolyglotCompiler 在前端阶段不会调用 MSVC、GCC、rustc 或 CPython。每种受支持源码在被显式编译时，会由项目自己的前端解析、类型检查并降级为 IR。链接阶段会使用内置 `polyld` 或平台链接器生成最终可执行文件。对于 `.ploy` 跨语言构建，描述符现在会参与严格链接校验：缺失外部实现符号或运行时符号时，构建会失败。

### 编译流程

```
┌─────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  C++ 源代码  │   │ Python 源代码 │   │  Rust 源代码  │   │  .ploy 源代码 │
│  (*.cpp)     │   │  (*.py)      │   │  (*.rs)      │   │  (*.ploy)    │
└──────┬──────┘   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
       │                 │                  │                  │
       ▼                 ▼                  ▼                  ▼
  ┌──────────┐    ┌──────────┐      ┌──────────┐       ┌──────────┐
  │frontend_ │    │frontend_ │      │frontend_ │       │frontend_ │
  │  cpp     │    │ python   │      │  rust    │       │  ploy    │
  │(polyglot)│    │(polyglot)│      │(polyglot)│       │(polyglot)│
  └────┬─────┘    └────┬─────┘      └────┬─────┘       └────┬─────┘
       │               │                 │                   │
       └───────────┬───┴─────────────┬───┘                   │
                   │                 │                        │
                   ▼                 │                        ▼
            ┌────────────┐          │              ┌────────────────┐
            │ 共享 IR    │          │              │ 跨语言调用      │
            │ (SSA 形式) │◄─────────┘              │ 描述符          │
            └─────┬──────┘                         └───────┬────────┘
                  │                                        │
                  ▼                                        ▼
           ┌────────────┐                          ┌───────────────┐
           │ 优化器      │                          │PolyglotLinker │
           │(25+ Pass)  │                          │ (粘合代码)     │
           └─────┬──────┘                          └───────┬───────┘
                 │                                         │
                 ▼                                         │
         ┌────────────────────┐                              │
         │      后端          │                              │
         │(x86_64/ARM64/WASM) │◄─────────────────────────────┘
         └────────┬───────────┘
                │
                ▼
       ┌────────────────┐
       │  目标文件      │
       │ (ELF/Mach-O/   │
       │  POBJ)         │
       └───────┬────────┘
               │
               ▼
       ┌────────────────┐
       │ 统一原生       │
       │    二进制      │
       └────────────────┘
```

### 详细步骤

1. **每个源文件由 PolyglotCompiler 自己的前端编译**
   - C++ 文件 → `frontend_cpp`（词法分析 → 语法分析 → 语义分析 → IR 生成）
   - Python 文件 → `frontend_python`（词法分析 → 语法分析 → 语义分析 → IR 生成）
   - Rust 文件 → `frontend_rust`（词法分析 → 语法分析 → 语义分析 → IR 生成）
   - 三个前端都生成**相同的共享 IR**（SSA 形式）

2. **`.ploy` 文件描述跨语言连接关系**
   - `LINK`、`IMPORT`、`CALL`、`NEW`、`METHOD`、`GET`、`SET`、`WITH` — 声明不同语言的函数/类/属性如何交互
   - `.ploy` 前端生成**跨语言调用描述符**（`CrossLangCallDescriptor`）

3. **PolyglotLinker 生成粘合代码**
   - 读取 `.ploy` 前端的跨语言调用描述符
   - 自动从 LINK 声明和已知函数签名合成跨语言符号条目 — 无需手动注册符号
   - 生成 **FFI 桥接桩函数**（如 `__ploy_bridge_ploy_python___init__`、`__ploy_bridge_ploy_python___getattr__weight`）
   - 生成**类型编组代码**（在不同语言的数据表示之间转换）
   - 生成**所有权跟踪代码**（管理跨语言对象生命周期）

4. **优化器在合并后的 IR 上运行**
   - 25+ 优化 Pass（常量折叠、死代码消除、内联、去虚化、循环优化等）
   - 支持 PGO 和 LTO 进行跨模块优化

5. **后端生成原生代码**
   - 针对 x86_64、ARM64 或 WebAssembly 的指令选择
   - 寄存器分配（图着色或线性扫描）
   - 汇编发射
   - 目标文件生成（ELF/Mach-O/POBJ）

6. **链接器在所有符号解析后生成统一二进制文件**
   - 目标文件、描述符生成的粘合代码以及所需运行时符号会一起链接
   - 如果引用的外部函数或运行时导入缺失，构建会以未解析符号诊断失败

### 这意味着什么

| 方面 | 说明 |
|------|------|
| **无外部编译器** | PolyglotCompiler 在编译过程中不调用 `g++`、`msvc`、`rustc` 或 `python`。所有语言都由项目自己的前端编译。 |
| **运行时导入必须解析** | 面向 Python 的桥接可能需要 GIL 辅助函数或包调用适配器等运行时符号，这些符号必须由链接的运行时目标文件或库提供。 |
| **单一二进制输出** | 只有在所有已编译源码、桥接桩函数和运行时符号都解析成功后，输出才是一个统一的原生可执行文件或库。 |
| **跨语言类型安全** | 类型编组在编译时通过 PolyglotLinker 的粘合代码生成完成，而非运行时动态分发。 |
| **跨语言开销** | 唯一的开销是语言边界处的编组代码（参数转换、所有权跟踪），与标准 FFI 调用相当。 |

### 示例

给定以下源文件：

```cpp
// image_processor.cpp
void enhance(double* data, int size) { /* 应用图像增强处理 */ }
```

```python
# ml_model.py
def predict(data: list) -> float:
    return sum(data) / len(data)
```

```ploy
// pipeline.ploy
IMPORT cpp::image_processor;
IMPORT python PACKAGE ml_model;

LINK(cpp, python, image_processor::enhance, ml_model::preprocess) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}

PIPELINE process {
    FUNC run(input: ARRAY[FLOAT], size: INT) -> FLOAT {
        LET enhance_result = CALL(cpp, image_processor::enhance, input, size);
        LET result = CALL(python, ml_model::predict, enhance_result);
        RETURN result;
    }
}

EXPORT process AS "run_pipeline";
```

#### 一步编译

PolyglotCompiler 支持直接编译 `.ploy` 文件。对于 `IMPORT cpp::image_processor;` 或 `IMPORT python::ml_model;` 这类本地源码导入，`polyc` 会在 `.ploy` 文件同目录和 `-I` 搜索根中查找源码，自动编译为目标文件，并写入 `aux/`。构建还会生成 `<stem>_foreign_aliases.pobj`，导出 `image_processor::enhance`、`image_processor__enhance` 等模块限定桥接符号。

```bash
polyc pipeline.ploy -o program
```

用于调试时，仍可使用等价的显式多源码流程：

```bash
polyc --lang=cpp -c image_processor.cpp -o image_processor.o
polyc --lang=python -c ml_model.py -o ml_model.o
polyc --lang=ploy -c pipeline.ploy -o pipeline.o
polyld -o program image_processor.o ml_model.o pipeline.o --ploy-desc aux/pipeline_link_descriptors.paux
```

当所有引用的实现符号和运行时符号都解析成功时，最终的 `program` 是一个原生 x86_64、ARM64 或 WebAssembly 输出。本地源码导入现在属于自动闭包；包导入和外部运行时库仍需要通过对应包/运行时路径解析。

### 与传统方案的对比

| 方案 | PolyglotCompiler | 传统 FFI (如 ctypes/pybind11) |
|------|-----------------|-------------------------------|
| 编译方式 | 所有语言由自有前端编译到共享 IR | 各语言各自编译，运行时动态绑定 |
| 运行时依赖 | 无外部依赖 | 需要 Python 解释器 + C++ 运行时 |
| 性能 | 原生代码，跨语言调用仅需编组 | 解释器开销 + FFI 开销 |
| 类型安全 | 编译时检查 | 运行时检查 |
| 部署 | 单个可执行文件 | 需要部署多个运行时 |

---

*最后更新: 2026-06-18*
