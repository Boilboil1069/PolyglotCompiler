# PolyglotCompiler 编译模型 — 常见问题

## 问题

> 现有 ploy 代码内部有 cpp 与 python 代码，编译后的二进制文件是编译了 cpp 与 python 后混合而成的吗？

---

## 回答

**是的 — 最终的二进制文件是由 PolyglotCompiler 自己的前端完全编译生成的统一原生二进制文件。**

PolyglotCompiler 在编译过程中**不会**调用任何外部编译器或解释器（MSVC、GCC、rustc、CPython）。具体流程如下：

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

6. **链接器生成统一二进制文件**
   - 所有目标文件（来自所有语言 + 粘合代码）链接在一起
   - 结果是**单个原生可执行文件** — 不是字节码和机器码的混合体

### 这意味着什么

| 方面 | 说明 |
|------|------|
| **无外部编译器** | PolyglotCompiler 在编译过程中不调用 `g++`、`msvc`、`rustc` 或 `python`。所有语言都由项目自己的前端编译。 |
| **运行时无解释器** | Python 代码被编译为原生机器码，而非由 CPython 解释执行。生成的二进制文件运行时不需要安装 Python。 |
| **单一二进制输出** | 输出是一个统一的原生可执行文件（或库），包含来自所有源语言的编译代码，通过共享 IR 统一。 |
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

PolyglotCompiler 支持直接编译 `.ploy` 文件 — 它会自动发现并编译所有引用的源文件：

```bash
polyc pipeline.ploy -o program          # 自动编译所有引用的源文件
```

这等效于手动的多步编译过程：

```bash
polyc --lang=cpp image_processor.cpp -o image_processor.o    # frontend_cpp → IR → x86_64 → .o
polyc --lang=python ml_model.py -o ml_model.o                # frontend_python → IR → x86_64 → .o
polyc --lang=ploy pipeline.ploy -o pipeline.o                # frontend_ploy → 描述符 → 粘合代码 → .o
polyld -o program image_processor.o ml_model.o pipeline.o    # 链接为单个二进制文件
```

最终的 `program` 是一个**单一的原生 x86_64（或 ARM64 / WebAssembly）可执行文件** — 运行时不需要 Python 解释器，不需要 C++ 编译器，不需要任何外部依赖。

### 与传统方案的对比

| 方案 | PolyglotCompiler | 传统 FFI (如 ctypes/pybind11) |
|------|-----------------|-------------------------------|
| 编译方式 | 所有语言由自有前端编译到共享 IR | 各语言各自编译，运行时动态绑定 |
| 运行时依赖 | 无外部依赖 | 需要 Python 解释器 + C++ 运行时 |
| 性能 | 原生代码，跨语言调用仅需编组 | 解释器开销 + FFI 开销 |
| 类型安全 | 编译时检查 | 运行时检查 |
| 部署 | 单个可执行文件 | 需要部署多个运行时 |

---

*最后更新: 2026-02-20*
