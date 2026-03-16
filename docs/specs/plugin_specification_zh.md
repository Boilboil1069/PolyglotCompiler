# PolyglotCompiler 插件规范

版本: 1.0.0 | API 版本: 1.0.0 | 日期: 2026-03-15

---

## 1. 概述

PolyglotCompiler 支持通过稳定的 **C ABI** 共享库接口在运行时加载插件。插件可以为编译器和 IDE 扩展新的语言前端、优化 Pass、代码格式化器、代码检查器、调试器集成和 UI 面板。

### 核心属性

| 属性 | 值 |
|------|-----|
| API 版本 | 1.0.0 |
| 库格式 | `.dll` (Windows) / `.so` (Linux) / `.dylib` (macOS) |
| 命名约定 | `polyplug_<名称>` (例: `polyplug_go.dll`) |
| ABI | C (extern "C")，无 C++ 名称修饰 |
| 内存模型 | 插件分配、插件拥有；宿主按需复制 |
| 线程安全 | 宿主从单线程调用插件，除非另有说明 |

---

## 2. 架构

```
┌─────────────────────────────────────────────────────┐
│                 PolyglotCompiler 宿主                │
│                                                     │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │PluginManager│  │CompilerService│  │  polyui   │ │
│  └──────┬──────┘  └──────┬───────┘  └─────┬──────┘ │
│         │                │                │         │
│         ▼                ▼                ▼         │
│  ┌─────────────────────────────────────────────┐    │
│  │           宿主服务 (C ABI)                   │    │
│  │  log · emit_diagnostic · get/set_setting    │    │
│  │  open_file · register_file_type             │    │
│  └──────────────────┬──────────────────────────┘    │
│                     │                               │
└─────────────────────┼───────────────────────────────┘
                      │ dlopen / LoadLibrary
          ┌───────────┼───────────┐
          ▼           ▼           ▼
    ┌───────────┐┌───────────┐┌───────────┐
    │  插件 A   ││  插件 B   ││  插件 C   │
    │ (语言)    ││ (检查器)  ││(格式化器) │
    └───────────┘└───────────┘└───────────┘
```

### 生命周期

```
加载 → get_info() → create() → activate() → [使用] → deactivate() → destroy() → 卸载
```

1. **加载**: 宿主调用 `dlopen` / `LoadLibrary` 加载共享库。
2. **get_info()**: 宿主读取静态元数据（id、名称、版本、能力）。
3. **create()**: 宿主分配实例，传入宿主上下文和服务表。
4. **activate()**: 宿主通知插件初始化资源。返回 0 表示成功。
5. **使用**: 宿主查询能力提供者并调用插件回调。
6. **deactivate()**: 宿主通知插件释放资源。
7. **destroy()**: 宿主释放插件实例。
8. **卸载**: 宿主调用 `dlclose` / `FreeLibrary`。

---

## 3. 必须导出的函数

每个插件共享库 **必须** 导出以下三个函数：

### 3.1 `polyglot_plugin_get_info`

```c
const PolyglotPluginInfo *polyglot_plugin_get_info(void);
```

返回指向静态元数据的指针。返回的指针在库的整个生命周期内必须保持有效。`api_version` 字段必须等于 `POLYGLOT_PLUGIN_API_VERSION`（当前为 1）。

### 3.2 `polyglot_plugin_create`

```c
PolyglotPlugin *polyglot_plugin_create(
    const PolyglotHostContext  *ctx,
    const PolyglotHostServices *host);
```

分配并返回新的插件实例。宿主传入上下文句柄和服务函数表。插件应保存这些供后续使用。失败时返回 `NULL`。

### 3.3 `polyglot_plugin_destroy`

```c
void polyglot_plugin_destroy(PolyglotPlugin *plugin);
```

释放之前由 `polyglot_plugin_create` 创建的实例。

---

## 4. 可选导出函数

仅当插件声明了对应的能力标志时才被调用。

### 4.1 生命周期钩子

```c
int  polyglot_plugin_activate(PolyglotPlugin *plugin);    // 返回 0 表示成功
void polyglot_plugin_deactivate(PolyglotPlugin *plugin);
```

### 4.2 语言提供者 (`POLYGLOT_CAP_LANGUAGE`)

```c
const PolyglotLanguageProvider *polyglot_plugin_get_language(PolyglotPlugin *plugin);
```

返回包含以下内容的静态结构体：
- `language_name` — 唯一的语言标识符 (例: `"go"`)
- `file_extensions` — NULL 结尾的数组 (例: `{".go", NULL}`)
- `tokenize()` — 用于语法高亮的词法分析
- `analyze()` — 通过 `host->emit_diagnostic` 发出诊断信息
- `compile_to_ir()` — 将源码编译为 IR 文本

### 4.3 优化 Pass (`POLYGLOT_CAP_OPTIMIZER`)

```c
const PolyglotOptimizerPass **polyglot_plugin_get_passes(
    PolyglotPlugin *plugin, uint32_t *out_count);
```

返回 Pass 描述符数组。每个 Pass 有一个 `run()` 回调用于变换 IR 文本。

### 4.4 代码操作 (`POLYGLOT_CAP_CODE_ACTION`)

```c
const PolyglotCodeAction **polyglot_plugin_get_code_actions(
    PolyglotPlugin *plugin, uint32_t *out_count);
```

### 4.5 格式化器 (`POLYGLOT_CAP_FORMATTER`)

```c
const PolyglotFormatter *polyglot_plugin_get_formatter(PolyglotPlugin *plugin);
```

### 4.6 代码检查器 (`POLYGLOT_CAP_LINTER`)

```c
const PolyglotLinter *polyglot_plugin_get_linter(PolyglotPlugin *plugin);
```

---

## 5. 能力标志

| 标志 | 值 | 描述 |
|------|-----|------|
| `POLYGLOT_CAP_LANGUAGE` | `1 << 0` | 添加新的语言前端 |
| `POLYGLOT_CAP_OPTIMIZER` | `1 << 1` | 添加优化 Pass |
| `POLYGLOT_CAP_BACKEND` | `1 << 2` | 添加代码生成后端 |
| `POLYGLOT_CAP_TOOL` | `1 << 3` | 添加 CLI 工具或子命令 |
| `POLYGLOT_CAP_UI_PANEL` | `1 << 4` | 添加 IDE 停靠面板 |
| `POLYGLOT_CAP_SYNTAX_THEME` | `1 << 5` | 添加语法配色方案 |
| `POLYGLOT_CAP_FILE_TYPE` | `1 << 6` | 添加文件类型关联 |
| `POLYGLOT_CAP_CODE_ACTION` | `1 << 7` | 添加重构 / 代码操作 |
| `POLYGLOT_CAP_FORMATTER` | `1 << 8` | 添加代码格式化器 |
| `POLYGLOT_CAP_LINTER` | `1 << 9` | 添加代码检查 Pass |
| `POLYGLOT_CAP_DEBUGGER` | `1 << 10` | 添加调试器集成 |

可以组合多个能力：`POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_LINTER`。

---

## 6. 宿主服务

宿主提供 `PolyglotHostServices` 结构体，包含以下回调：

| 服务 | 签名 | 描述 |
|------|------|------|
| `log` | `void (*)(ctx, level, message)` | 写入宿主日志输出 |
| `emit_diagnostic` | `void (*)(ctx, diag)` | 向 IDE 报告错误/警告/注释 |
| `get_setting` | `const char *(*)(ctx, key)` | 读取插件作用域的持久化设置 |
| `set_setting` | `void (*)(ctx, key, value)` | 写入插件作用域的持久化设置 |
| `open_file` | `void (*)(ctx, path, line)` | 在 IDE 编辑器中打开文件 |
| `register_file_type` | `void (*)(ctx, ext, language)` | 将文件扩展名与语言关联 |
| `get_workspace_root` | `const char *(*)(ctx)` | 查询当前工作区目录 |

### 设置作用域

每个插件拥有独立的键命名空间。id 为 `com.example.myplugin` 的插件调用 `set_setting(ctx, "indent_size", "4")` 不会与其他插件的 `indent_size` 设置冲突。

---

## 7. 插件发现

宿主在以下目录中搜索插件库：

1. `<程序目录>/plugins/` — 与 `polyui` / `polyc` 可执行文件同级
2. **Linux**: `$XDG_DATA_HOME/polyglot/plugins/`（默认: `~/.local/share/polyglot/plugins/`）
3. **macOS**: `~/Library/Application Support/PolyglotCompiler/plugins/`
4. **Windows**: `%APPDATA%/PolyglotCompiler/plugins/`
5. 用户通过 设置 → 插件 页面配置的额外目录

仅加载匹配 `polyplug_*` 命名前缀的共享库。

---

## 8. 示例：最小插件 (C)

```c
#include "common/include/plugins/plugin_api.h"
#include <stdlib.h>

typedef struct {
    const PolyglotHostContext  *ctx;
    const PolyglotHostServices *host;
} MyPlugin;

static const PolyglotPluginInfo kInfo = {
    .api_version     = POLYGLOT_PLUGIN_API_VERSION,
    .id              = "com.example.hello",
    .name            = "Hello Plugin",
    .version         = "1.0.0",
    .author          = "示例作者",
    .description     = "一个在激活时输出日志的最小示例插件。",
    .license         = "MIT",
    .homepage        = NULL,
    .capabilities    = POLYGLOT_CAP_NONE,
    .min_host_version = "1.0.0",
};

POLYGLOT_EXPORT
const PolyglotPluginInfo *polyglot_plugin_get_info(void) {
    return &kInfo;
}

POLYGLOT_EXPORT
PolyglotPlugin *polyglot_plugin_create(
        const PolyglotHostContext  *ctx,
        const PolyglotHostServices *host) {
    MyPlugin *p = (MyPlugin *)calloc(1, sizeof(MyPlugin));
    p->ctx  = ctx;
    p->host = host;
    return (PolyglotPlugin *)p;
}

POLYGLOT_EXPORT
void polyglot_plugin_destroy(PolyglotPlugin *plugin) {
    free(plugin);
}

POLYGLOT_EXPORT
int polyglot_plugin_activate(PolyglotPlugin *plugin) {
    MyPlugin *p = (MyPlugin *)plugin;
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Hello from the plugin!");
    return 0;
}

POLYGLOT_EXPORT
void polyglot_plugin_deactivate(PolyglotPlugin *plugin) {
    MyPlugin *p = (MyPlugin *)plugin;
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Goodbye from the plugin!");
}
```

构建命令：
```bash
# Linux
gcc -shared -fPIC -o polyplug_hello.so hello_plugin.c -I /path/to/PolyglotCompiler

# macOS
clang -shared -fPIC -o polyplug_hello.dylib hello_plugin.c -I /path/to/PolyglotCompiler

# Windows (MSVC)
cl /LD hello_plugin.c /I C:\path\to\PolyglotCompiler /Fe:polyplug_hello.dll
```

---

## 9. 示例：语言插件 (C++)

```cpp
#include "common/include/plugins/plugin_api.h"
#include <cstring>
#include <cstdlib>
#include <string>

struct GoPlugin {
    const PolyglotHostContext  *ctx;
    const PolyglotHostServices *host;
};

// -- 词法分析器 --
static uint32_t GoTokenize(const char *source, size_t len,
                           PolyglotToken *out, uint32_t max) {
    // 简化的词法分析 — 实际实现应使用完整的 lexer。
    uint32_t count = 0;
    // ... 词法分析逻辑 ...
    (void)source; (void)len; (void)out; (void)max;
    return count;
}

// -- 语义分析器 --
static void GoAnalyze(const PolyglotHostContext *ctx,
                      const PolyglotHostServices *host,
                      const char *source, size_t len,
                      const char *filename) {
    // ... 分析逻辑，通过 host->emit_diagnostic 发出诊断 ...
    (void)ctx; (void)host; (void)source; (void)len; (void)filename;
}

// -- 编译器 --
static char *GoCompileToIR(const PolyglotHostContext *ctx,
                           const PolyglotHostServices *host,
                           const char *source, size_t len,
                           const char *filename) {
    // ... 编译为 IR 文本 ...
    (void)ctx; (void)host; (void)source; (void)len; (void)filename;
    return nullptr;
}

static const char *kGoExtensions[] = {".go", nullptr};

static const PolyglotLanguageProvider kGoProvider = {
    .language_name  = "go",
    .file_extensions = kGoExtensions,
    .tokenize       = GoTokenize,
    .analyze        = GoAnalyze,
    .compile_to_ir  = GoCompileToIR,
};

static const PolyglotPluginInfo kInfo = {
    .api_version     = POLYGLOT_PLUGIN_API_VERSION,
    .id              = "com.example.go-frontend",
    .name            = "Go 语言支持",
    .version         = "0.1.0",
    .author          = "示例作者",
    .description     = "为 PolyglotCompiler 添加 Go 语言支持。",
    .license         = "Apache-2.0",
    .homepage        = nullptr,
    .capabilities    = POLYGLOT_CAP_LANGUAGE | POLYGLOT_CAP_FILE_TYPE,
    .min_host_version = "1.0.0",
};

extern "C" {

POLYGLOT_EXPORT const PolyglotPluginInfo *polyglot_plugin_get_info(void) {
    return &kInfo;
}

POLYGLOT_EXPORT PolyglotPlugin *polyglot_plugin_create(
        const PolyglotHostContext *ctx, const PolyglotHostServices *host) {
    auto *p = new GoPlugin{ctx, host};
    return reinterpret_cast<PolyglotPlugin *>(p);
}

POLYGLOT_EXPORT void polyglot_plugin_destroy(PolyglotPlugin *plugin) {
    delete reinterpret_cast<GoPlugin *>(plugin);
}

POLYGLOT_EXPORT int polyglot_plugin_activate(PolyglotPlugin *plugin) {
    auto *p = reinterpret_cast<GoPlugin *>(plugin);
    p->host->log(p->ctx, POLYGLOT_LOG_INFO, "Go frontend activated");
    return 0;
}

POLYGLOT_EXPORT void polyglot_plugin_deactivate(PolyglotPlugin *) {}

POLYGLOT_EXPORT const PolyglotLanguageProvider *polyglot_plugin_get_language(
        PolyglotPlugin *) {
    return &kGoProvider;
}

}  // extern "C"
```

---

## 10. 安全注意事项

- 插件在与宿主 **相同的进程** 中运行。恶意插件拥有对内存的完全访问权限。
- 仅从受信任的来源加载插件。
- 宿主在创建实例之前会验证 `api_version`。
- 插件作用域的设置按插件 id 进行沙箱隔离。
- 未来版本可能会添加插件二进制文件的签名验证。

---

## 11. 版本策略

- 当 C ABI 发生 **破坏性变更** 时，`api_version` 字段会递增。
- 添加性变更（新的可选导出）**不会** 提升 API 版本。
- 针对 API 版本 N 构建的插件保证能在支持版本 N 的宿主上运行。
- 宿主会拒绝 `api_version` 不匹配 `POLYGLOT_PLUGIN_API_VERSION` 的插件。

---

## 12. 文件位置

| 文件 | 用途 |
|------|------|
| `common/include/plugins/plugin_api.h` | 公共 C API（稳定 ABI） |
| `common/include/plugins/plugin_manager.h` | C++ 插件管理器接口 |
| `common/src/plugins/plugin_manager.cpp` | 插件管理器实现 |
| `tests/unit/plugins/plugin_manager_test.cpp` | 单元测试 |
