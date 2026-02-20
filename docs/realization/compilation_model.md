# PolyglotCompiler Compilation Model — FAQ

## Question / 问题

> 现有 ploy 代码内部有 cpp 与 python 代码，编译后的二进制文件是编译了 cpp 与 python 后混合而成的吗？

> If a `.ploy` file references both C++ and Python code, is the resulting binary a mix of compiled C++ and Python?

---

## Answer / 回答

**Yes — the final binary is a unified native binary produced entirely by PolyglotCompiler's own frontends.**

PolyglotCompiler does **NOT** invoke any external compilers or interpreters (MSVC, GCC, rustc, CPython) during compilation. Instead, the following process takes place:

### Compilation Flow / 编译流程

```
┌─────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  C++ Source  │   │ Python Source │   │  Rust Source  │   │  .ploy Source │
│  (*.cpp)     │   │  (*.py)      │   │  (*.rs)      │   │  (*.ploy)    │
└──────┬──────┘   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
       │                 │                   │                   │
       ▼                 ▼                   ▼                   ▼
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
            │ Shared IR  │          │              │ Cross-Language  │
            │ (SSA Form) │◄─────────┘              │ Call Descriptors│
            └─────┬──────┘                         └───────┬────────┘
                  │                                        │
                  ▼                                        ▼
           ┌────────────┐                          ┌───────────────┐
           │ Optimiser   │                          │ PolyglotLinker│
           │ (25+ Passes)│                          │ (Glue Code)  │
           └─────┬──────┘                          └───────┬───────┘
                 │                                         │
                 ▼                                         │
         ┌──────────────┐                                  │
         │   Backend    │                                  │
         │(x86_64/ARM64)│◄─────────────────────────────────┘
         └──────┬───────┘
                │
                ▼
       ┌────────────────┐
       │  Object Files  │
       │ (ELF/Mach-O/   │
       │  POBJ)         │
       └───────┬────────┘
               │
               ▼
       ┌────────────────┐
       │ Unified Native │
       │    Binary      │
       └────────────────┘
```

### Step-by-Step / 详细步骤

1. **Each source file is compiled by PolyglotCompiler's own frontend**  
   每个源文件由 PolyglotCompiler 自己的前端编译：
   - C++ files → `frontend_cpp` (Lexer → Parser → Sema → Lowering)
   - Python files → `frontend_python` (Lexer → Parser → Sema → Lowering)
   - Rust files → `frontend_rust` (Lexer → Parser → Sema → Lowering)
   - All three frontends produce the **same shared IR** (SSA form)

2. **The `.ploy` file describes cross-language connections**  
   `.ploy` 文件描述跨语言连接关系：
   - `LINK`, `IMPORT`, `CALL`, `NEW`, `METHOD`, `GET`, `SET`, `WITH` — declare how functions/classes/attributes from different languages interact
   - The `.ploy` frontend produces **cross-language call descriptors** (`CrossLangCallDescriptor`)

3. **The PolyglotLinker generates glue code**  
   PolyglotLinker 生成粘合代码：
   - Reads the cross-language call descriptors from the `.ploy` frontend
   - Generates **FFI bridge stubs** (e.g., `__ploy_bridge_ploy_python___init__`, `__ploy_bridge_ploy_python___getattr__weight`)
   - Generates **type marshalling code** (converting between language-specific representations)
   - Generates **ownership tracking code** (managing cross-language object lifetimes)

4. **The optimiser runs on the combined IR**  
   优化器在合并后的 IR 上运行：
   - 25+ optimisation passes (constant folding, DCE, inlining, devirtualisation, loop optimisation, etc.)
   - PGO and LTO support for cross-module optimisation

5. **The backend generates native code**  
   后端生成原生代码：
   - Instruction selection for x86_64 or ARM64
   - Register allocation (graph colouring or linear scan)
   - Assembly emission
   - Object file generation (ELF/Mach-O/POBJ)

6. **The linker produces a unified binary**  
   链接器生成统一二进制文件：
   - All object files (from all languages + glue code) are linked together
   - The result is a **single native executable** — not a mix of bytecode and machine code

### What This Means / 这意味着什么

| Aspect | Description |
|--------|-------------|
| **No external compilers** | PolyglotCompiler does NOT call `g++`, `msvc`, `rustc`, or `python` during compilation. All languages are compiled by the project's own frontends. |
| **No interpreters at runtime** | The Python code is compiled to native machine code, not interpreted by CPython. The resulting binary does not require a Python installation to run. |
| **Single binary output** | The output is a single native executable (or library) that contains compiled code from all source languages, unified through a shared IR. |
| **Type safety across languages** | Type marshalling is done at compile time via the PolyglotLinker's glue code generation, not at runtime via dynamic dispatch. |
| **Cross-language overhead** | The only overhead is the marshalling code at language boundaries (argument conversion, ownership tracking), which is comparable to a standard FFI call. |

### 不使用外部编译器

| 方面 | 说明 |
|------|------|
| **无外部编译器** | PolyglotCompiler 不调用 `g++`、`msvc`、`rustc` 或 `python`。所有语言都由项目自己的前端编译。 |
| **运行时无解释器** | Python 代码被编译为原生机器码，而非由 CPython 解释执行。生成的二进制文件运行时不需要安装 Python。 |
| **单一二进制输出** | 输出是一个统一的原生可执行文件（或库），包含来自所有源语言的编译代码，通过共享 IR 统一。 |
| **跨语言类型安全** | 类型编组在编译时通过 PolyglotLinker 的粘合代码生成完成，而非运行时动态分发。 |
| **跨语言开销** | 唯一的开销是语言边界处的编组代码（参数转换、所有权跟踪），与标准 FFI 调用相当。 |

### Example / 示例

Given these source files:

```cpp
// image_processor.cpp
void enhance(double* data, int size) { /* applies image enhancement */ }
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

#### Single-Command Compilation / 一步编译

PolyglotCompiler supports compiling a `.ploy` file directly — it will automatically discover and compile all referenced source files:

```bash
polyc pipeline.ploy -o program          # Auto-compiles all referenced sources
```

This is equivalent to the multi-step manual process:

```bash
polyc --lang=cpp image_processor.cpp -o image_processor.o    # frontend_cpp → IR → x86_64 → .o
polyc --lang=python ml_model.py -o ml_model.o                # frontend_python → IR → x86_64 → .o
polyc --lang=ploy pipeline.ploy -o pipeline.o                # frontend_ploy → descriptors → glue → .o
polyld -o program image_processor.o ml_model.o pipeline.o    # Link into single binary
```

The final `program` is a **single native x86_64 (or ARM64) executable** — no Python interpreter, no C++ compiler, no external dependencies required at runtime.

---

*Last updated / 最后更新: 2026-02-20*
