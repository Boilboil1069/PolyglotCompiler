# PolyglotCompiler Compilation Model — FAQ

## Question / 问题

> 现有 ploy 代码内部有 cpp 与 python 代码，编译后的二进制文件是编译了 cpp 与 python 后混合而成的吗？

> If a `.ploy` file references both C++ and Python code, is the resulting binary a mix of compiled C++ and Python?

---

## Answer / 回答

**A `.ploy` build produces one final native output only when every referenced implementation and runtime symbol is available to the linker.**

PolyglotCompiler does **not** invoke MSVC, GCC, rustc, or CPython during its frontend stages. Each supported source language is parsed, type-checked, and lowered by the project's own frontends when that source is explicitly compiled. During the link stage, the built-in `polyld` linker or a platform linker may be invoked to produce the final executable. For `.ploy` cross-language builds, descriptors now participate in strict link validation: missing external implementation symbols or runtime symbols stop the build.

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
         ┌────────────────────┐                              │
         │      Backend       │                              │
         │(x86_64/ARM64/WASM) │◄─────────────────────────────┘
         └────────┬───────────┘
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
   - Automatically synthesizes cross-language symbol entries from LINK declarations and known function signatures — no manual symbol registration required
   - Generates **FFI bridge stubs** (e.g., `__ploy_bridge_ploy_python___init__`, `__ploy_bridge_ploy_python___getattr__weight`)
   - Generates **type marshalling code** (converting between language-specific representations)
   - Generates **ownership tracking code** (managing cross-language object lifetimes)

4. **The optimiser runs on the combined IR**  
   优化器在合并后的 IR 上运行：
   - 25+ optimisation passes (constant folding, DCE, inlining, devirtualisation, loop optimisation, etc.)
   - PGO and LTO support for cross-module optimisation

5. **The backend generates native code**  
   后端生成原生代码：
   - Instruction selection for x86_64, ARM64, or WebAssembly
   - Register allocation (graph colouring or linear scan)
   - Assembly emission
   - Object file generation (ELF/Mach-O/POBJ)

6. **The linker produces a unified binary only after all symbols resolve**  
   链接器生成统一二进制文件：
   - Object files, descriptor-generated glue code, and required runtime symbols are linked together
   - If a referenced foreign function or runtime import is missing, the build fails with unresolved-symbol diagnostics

### What This Means / 这意味着什么

| Aspect | Description |
|--------|-------------|
| **No external compilers for frontends** | PolyglotCompiler does NOT call `g++`, `msvc`, `rustc`, or `python` for compilation. All languages are compiled by the project's own frontends. The link stage may invoke a system linker (e.g., `clang`, `lld-link`) when needed. |
| **Runtime imports must resolve** | Python-oriented bridges may require runtime symbols such as GIL helpers or package-call adapters; those symbols must be supplied by the linked runtime object or library. |
| **Single binary output** | The output is a single native executable or library only after all compiled sources, bridge stubs, and runtime symbols resolve. |
| **Type safety across languages** | Type marshalling is done at compile time via the PolyglotLinker's glue code generation, not at runtime via dynamic dispatch. |
| **Cross-language overhead** | The only overhead is the marshalling code at language boundaries (argument conversion, ownership tracking), which is comparable to a standard FFI call. |

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

PolyglotCompiler supports compiling a `.ploy` file directly. For local source imports such as `IMPORT cpp::image_processor;` or `IMPORT python::ml_model;`, `polyc` searches next to the `.ploy` file and in `-I` include roots, compiles the discovered source files automatically, and writes the generated objects into `aux/`. The build also emits a `<stem>_foreign_aliases.pobj` object that exports module-qualified bridge symbols such as `image_processor::enhance` and `image_processor__enhance`.

```bash
polyc pipeline.ploy -o program
```

The equivalent explicit multi-source flow is still available for debugging:

```bash
polyc --lang=cpp -c image_processor.cpp -o image_processor.o
polyc --lang=python -c ml_model.py -o ml_model.o
polyc --lang=ploy -c pipeline.ploy -o pipeline.o
polyld -o program image_processor.o ml_model.o pipeline.o --ploy-desc aux/pipeline_link_descriptors.paux
```

The final `program` is a native x86_64, ARM64, or WebAssembly output when every referenced implementation and runtime symbol resolves. Local source imports are now part of that closure; package imports and external runtime libraries still have to resolve through their configured package/runtime paths.

### Compared with Traditional Toolchains / 与传统方案的对比

| Aspect | PolyglotCompiler | Traditional FFI (e.g. ctypes / pybind11) |
|--------|------------------|------------------------------------------|
| Compilation | Every language is compiled by its own frontend into a shared IR | Each language is compiled separately and bound at runtime |
| Runtime dependencies | None | Requires a Python interpreter + C++ runtime |
| Performance | Native code; cross-language calls only pay marshalling cost | Interpreter overhead + FFI overhead |
| Type safety | Checked at compile time | Checked at runtime |
| Deployment | Single executable | Multiple runtimes must be deployed |

---

*Last updated / 最后更新: 2026-06-18*
