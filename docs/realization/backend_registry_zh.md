# 后端注册中心与 ITargetBackend

> 状态：随 PolyglotCompiler 1.3.3 发布（后端层重构子需求 2026-04-28-2a）。
> 受众：后端实现者、工具链集成方，以及任何动到 `polyc` / `polyasm` 分发逻辑的同学。

## 1. 这个东西为什么存在

1.3.2 之前，每一个需要代码生成的工具（`polyc`、`polyasm`、未来的 `polyld` 插件）都在
`--arch` 字符串上写死 `if/else`，直接 `new` 出 `X86Target` / `Arm64Target` / `WasmTarget`。
这个布局有三个具体问题：

1. 新增一个后端（RISC-V、LoongArch…）需要改动所有消费者。
2. 后端的能力（能否生成 object、是否支持 debug info、bitcode、可选哪种寄存器分配器）
   完全无法被外部探查，用户没有办法问"我现在有哪些目标？"。
3. 前端层早就用 `polyglot::frontends::FrontendRegistry` 解决了同样的问题，
   后端却长期不对称。

`polyglot::backends::ITargetBackend` + `BackendRegistry` 把这道缺口补上了，整体形状刻意
对齐前端注册中心，熟悉 `REGISTER_FRONTEND` 的同学不需要任何新概念就能注册一个后端。

## 2. 公共接口一览

| 头文件 | 提供的内容 |
|---|---|
| `backends/common/include/target_backend.h` | `ITargetBackend`、`TargetOptions`、`TargetArtifacts`、`BackendCapabilities`、`BackendInfo`、`MCRelocation`、`MCSymbol`、`MCSection`、`CompileStats`、`BackendDiagnostic`、`CompileResult`、`MakeBackendInfo`、`AsciiToLower` |
| `backends/common/include/backend_registry.h` | `BackendRegistry`、`RegisterStatus`、`BackendRegistrar`、`REGISTER_TARGET_BACKEND`、`ToJson`、`ToHumanReadable` |

所有类型位于 `polyglot::backends`。三个内置后端的 adapter 放在嵌套命名空间：
`polyglot::backends::x86_64`、`polyglot::backends::arm64`、`polyglot::backends::wasm`。

## 3. `ITargetBackend` 契约

```cpp
class ITargetBackend {
 public:
    virtual ~ITargetBackend() = default;

    virtual std::string TargetTriple() const = 0;
    virtual std::string Description() const = 0;
    virtual std::vector<std::string> Aliases() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual BackendCapabilities Capabilities() const = 0;

    virtual CompileResult Compile(const middle::ir::Module& module,
                                  const TargetOptions& options) = 0;

    virtual CompileResult EmitAssembly(const middle::ir::Module& module,
                                       const TargetOptions& options);
    virtual CompileResult EmitObject  (const middle::ir::Module& module,
                                       const TargetOptions& options);
    virtual CompileResult EmitBitcode (const middle::ir::Module& module,
                                       const TargetOptions& options);
};
```

* `TargetTriple()` —— 规范 triple，例如 `x86_64-unknown-elf`。注册表里必须唯一；别名查询走另一条路径。
* `Aliases()` —— 所有被接受的写法（大小写不敏感）。约定：
  * 索引 0 必须是规范 triple；
  * 列出用户实际会敲的短写法（`amd64`、`x64`、`aarch64`、`wasm32`）；
  * 列出常见平台 triple（`x86_64-pc-windows-msvc`、`aarch64-apple-darwin`、`wasm32-wasi`…）。
* `Capabilities()` —— 能力位，被 `polyc`、设置 UI 与 `--print-targets` 消费。三个内置后端的能力矩阵：

  | 能力 | x86_64 | arm64 | wasm |
  |---|:---:|:---:|:---:|
  | `emits_object` | ✅ | ✅ | ✅ |
  | `emits_assembly` | ✅ | ✅ | ✅（文本 `.wat` 占位） |
  | `emits_bitcode` | — | — | — |
  | `supports_debug_info` | ✅ | ✅ | — |
  | `supports_pic` | ✅ | ✅ | n/a |
  | `register_allocators` | linear-scan, graph-coloring | linear-scan, graph-coloring | n/a |

* `Compile()` 是规范入口。`EmitAssembly` / `EmitObject` 默认调用 `Compile()` 并取
  `TargetArtifacts` 中对应字段。`EmitBitcode` 默认返回单条
  `BackendDiagnostic{Severity::kError, "bitcode emission unsupported"}`，调用方永远拿到一条
  类型化错误，而不是静默吐零字节（这一默认行为将在子需求 2026-04-28-2e 上线 LLVM bitcode 发射器后被解除）。

### `TargetOptions`

值语义结构体，每次 `Compile` 都按值传入。稳定字段（只增不删）：

* `EmitKind emit` —— `kObject`、`kAssembly`、`kBitcode`、`kLLVMText`。
* `RegAllocStrategy reg_alloc` —— `kLinearScan`、`kGraphColoring`。
* `SchedulerStrategy scheduler` —— `kList`、`kNone`。
* `VerifyLevel verify` —— `kOff`、`kOn`、`kStrict`。
* `DebugInfoLevel debug_info` —— `kNone`、`kLine`、`kFull`。
* `optimization_level`（0–3）、`position_independent`、`relocation_model`、`cpu`、`features`。
* `module_name`、`source_path` —— 仅用于诊断溯源。

### `TargetArtifacts`

成功时挂在 `CompileResult::artifacts` 上。字段：

* `assembly_text` —— `emit==kAssembly` 时的 UTF-8 字符串，或作为副产物出现。
* `object_bytes` —— 产物字节流（ELF/Mach-O/COFF/wasm/bitcode）。
* `relocations` —— `vector<MCRelocation>`（symbol、section、offset、type、addend、is_pcrel）。
* `symbols` —— `vector<MCSymbol>`，含 `is_global` / `is_defined` / `is_weak`。
* `sections` —— `vector<MCSection>`，含 size/align/flags。
* `debug_sections` —— 不透明的目标特定调试负载（DWARF/PDB/无）。
* `stats` —— `CompileStats`（isel/regalloc/sched/emit 各阶段耗时 + 峰值内存 + 计数器）。

> 字段命名约定：布尔字段一律 `is_` 前缀（`is_global` / `is_defined` / `is_bss` / `is_weak`），
> 避免与工具链中其它保留字冲突。

### `CompileResult` 与诊断

```cpp
struct CompileResult {
    std::optional<TargetArtifacts>    artifacts;
    std::vector<BackendDiagnostic>    diagnostics;
    bool ok() const { return artifacts.has_value(); }
};
```

后端可以在成功时附带诊断（warning/note），失败时附带错误。映射到前端 `Diagnostics` 的规则：

| `BackendDiagnostic::Severity` | 前端通道 |
|---|---|
| `kError` | `Diagnostics::ReportError` |
| `kWarning` | `Diagnostics::ReportWarning` |
| `kInfo` | `Diagnostics::ReportWarning`（降级 —— 当前 `Diagnostics` 暂无 info 通道） |

## 4. 注册表语义

### 4.1 生命周期

`BackendRegistry::Instance()` 返回 Meyers 单例，由 `std::mutex` 守护，所有公开方法线程安全。
注册顺序不影响任何外部行为：`List()` 始终按规范 triple 排序，保证可复现输出。

### 4.2 注册

```cpp
namespace polyglot::backends::arm64 {
REGISTER_TARGET_BACKEND([] { return std::make_unique<Arm64TargetBackend>(); });
}
```

`REGISTER_TARGET_BACKEND` 展开为一个翻译单元局部的 `BackendRegistrar`，构造函数里调用
`BackendRegistry::Instance().Register(...)`。这是**唯一**被授权的注册路径 —— 直接调用
`Register()` 仅用于测试与 `Clear()` 流程。

注册返回值：

| `RegisterStatus` | 含义 |
|---|---|
| `kOk` | 已接受；规范 triple 与所有别名已索引。 |
| `kNullBackend` | 调用者传入空 `unique_ptr`。 |
| `kDuplicateTriple` | 已有后端占用此规范 triple。 |
| `kAliasConflict` | 别名与已存在后端的 triple 或别名冲突。 |

宏在 debug 构建下对非 `kOk` 直接 assert，release 构建下打 warning。四条路径均有单元测试覆盖。

### 4.3 查询

```cpp
ITargetBackend* be = BackendRegistry::Instance().Find("amd64");           // 大小写不敏感
ITargetBackend* be = BackendRegistry::Instance().FindOrDiagnose(triple, &diag);
```

`Find` 在未知 triple 时返回 `nullptr`。`FindOrDiagnose` 是 `polyc` / `polyasm` 走的生产路径；
未命中时会追加一条形如下面的错误诊断：

```
no backend registered for target 'mips'. Available backends:
  arm64-unknown-elf (aliases: arm64, aarch64, armv8, …)
  wasm32-unknown-unknown (aliases: wasm, wasm32, wasm64, …)
  x86_64-unknown-elf (aliases: x86_64, x86-64, amd64, x64, …)
```

用户不需要再开 verbose 标志就能拿到可操作的提示。

### 4.4 枚举

`List()` 返回排序后的 `std::vector<BackendInfo>`，每条携带 triple、描述、别名、能力矩阵
与 `is_available`。两个自由函数负责序列化：

* `ToHumanReadable(const std::vector<BackendInfo>&) -> std::string`
* `ToJson(const std::vector<BackendInfo>&) -> std::string`

JSON 形状稳定、可机读：

```json
[
  {
    "triple": "arm64-unknown-elf",
    "description": "ARM64 (AArch64) backend",
    "aliases": ["arm64", "aarch64", "armv8", "..."],
    "available": true,
    "capabilities": {
      "emits_object": true,
      "emits_assembly": true,
      "emits_bitcode": false,
      "supports_debug_info": true,
      "supports_pic": true,
      "register_allocators": ["linear-scan", "graph-coloring"]
    }
  }
]
```

## 5. 工具集成

### 5.1 `polyc`

`tools/polyc/src/compilation_pipeline.cpp` 不再按架构字符串分支：

```cpp
auto* backend =
    backends::BackendRegistry::Instance().FindOrDiagnose(config.target_arch, &lookup_diag);
if (!backend) {
    diagnostics.ReportError(lookup_diag.message);
    return Result::Failure;
}

backends::TargetOptions opts;
opts.emit          = backends::EmitKind::kObject;
opts.reg_alloc     = config.reg_alloc;
opts.scheduler     = config.scheduler;
opts.debug_info    = config.debug_info;
opts.module_name   = ir_module.name();
opts.source_path   = config.source_path;

auto compile_result = backend->Compile(ir_module, opts);
absorb_artifacts(compile_result, &compiled_object);   // 适配进旧类型
```

`absorb_artifacts` 把 `TargetArtifacts` 翻译成既有的 `CompiledObject` /
`linker::Symbol` / `linker::Relocation`，以便链接器/驱动管道在 2a 阶段不发生横向变更（后续子
需求会再做形状收口）。

### 5.2 `polyasm`

`tools/polyasm/src/assembler.cpp` 走同一模式。WASM 二进制直接落盘的快路径被保留 ——
仅当 triple 以 `wasm` 开头且用户请求原始 `.wasm` 输出时才绕过注册表。

### 5.3 `polyc` 新增 CLI

| 标志 | 行为 |
|---|---|
| `--print-targets` | 打印人类可读的后端清单后退出 0。 |
| `--print-targets=text` | 同上（显式形式）。 |
| `--print-targets=json` | 打印注册表 JSON 快照后退出 0。 |
| `--print-target-info=<triple>` | 打印单个后端的人类可读信息。 |
| `--print-target-info=<triple>:json` | 同上，JSON 形式。 |

查询走别名解析：`--print-target-info=amd64` 会命中 x86_64 后端。

退出码：

* `0` —— 命中并打印。
* `2` —— 别名未注册，"available backends" 诊断写到 stderr。

这两个标志在 `main()` 最顶部解析，先于普通参数解析，所以即便其它参数有问题也能正常工作。

## 6. 从 1.3.3 之前的分发迁移

如果你在外部工具里这么写过：

```cpp
if (arch == "wasm") { /* WasmTarget */ }
else if (arch == "arm64" || arch == "aarch64") { /* Arm64Target */ }
else { /* X86Target */ }
```

请改写为：

```cpp
auto* be = polyglot::backends::BackendRegistry::Instance().FindOrDiagnose(arch, &diag);
if (!be) { /* 透传 diag.message */ return; }
auto result = be->Compile(ir_module, options);
```

仍允许直接 new 出 `X86Target` / `Arm64Target` / `WasmTarget`（2a 阶段它们没有变化），但
**不推荐**。后续子需求（2b–2d）会重塑这些类；走注册表的代码不受影响。

## 7. 新增一个后端

最小清单：

1. 实现 `ITargetBackend`（通常作为 adapter 包装现有代码生成器）。
2. 在命名空间作用域注册：
   ```cpp
   namespace polyglot::backends::myarch {
   REGISTER_TARGET_BACKEND([] { return std::make_unique<MyArchBackend>(); });
   }
   ```
3. 把 `.cpp` 加入对应 CMake target，使静态初始化器被链入。`BUILD_SHARED_LIBS=ON`（默认）下
   意味着把库加到 `polyc` / `polyasm` / `test_backends` 的链接列表里 —— 与
   `backend_x86_64` / `backend_arm64` / `backend_wasm` 完全一致。
4. 在 `tests/unit/backends/<myarch>_target_backend_test.cpp` 加单元测试，断言后端可通过规范
   triple 与至少一个别名命中，并且 `Capabilities()` 与文档一致。
5. 如果你给内置集合新增了一项，把 `--print-targets` 的基线测试同步更新（测试会断言注册表大小）。

## 8. 故障排查

* **`no backend registered for target 'X'`** —— 别名拼错，或者后端的翻译单元在链接时被剥离
  （缺 `whole-archive`）。检查对应 `.cpp` 是否在链接行里；用 shared libs 时是自动的。
* **`backend registration rejected: alias conflict`** —— 两个后端声明了同一个别名。
  别名必须全局唯一，请加上目标前缀。
* **`bitcode emission unsupported`** —— 1.3.3 在 x86_64/arm64/wasm 上属预期行为，由子需求
  2026-04-28-2e 解除。
* **大小写混合的 triple 没命中** —— 注册表用 `AsciiToLower` 做小写化。如果你在外部手工比较
  triple，请同样小写化。

## 9. 关联工作

* 子需求 2026-04-28-2b 会把 `MachineFunction` / `LinearScanAllocate` /
  `GraphColoringAllocate` / `ScheduleFunction` 上提到 `backends/common/`，并加入会经过本注册
  表运行的 MachineIR Verifier。
* 子需求 2026-04-28-2c 重写 ABI 与重定位模型；`RelocationTraits` 会在那时通过注册表暴露。
* 子需求 2026-04-28-2e 引入受 `POLYGLOT_ENABLE_LLVM_BITCODE` 守护的真正 `EmitBitcode`。

注册表接口与宏的形状视为稳定，上述任何子需求都不会修改它们；只会增加带默认实现的可选方法。
