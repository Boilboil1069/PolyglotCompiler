# 依赖缓存（离线优先构建）

> 状态：随 PolyglotCompiler 1.4.1 发布。

## 背景

1.4.1 之前，每次 CMake configure（也就是每次执行
`scripts/package_*.{ps1,sh}`）都会通过 `FetchContent` 重新 `git clone`
`fmt`、`nlohmann/json`、`Catch2`、`mimalloc` 这四个依赖。在网络抖动或
受限的环境下，clone 一旦超时，整个打包流程就会失败。

1.4.1 为这四个依赖引入了**持久化的本地缓存**：由专门的脚本一次性下载，
之后由 `Dependencies.cmake` 透明地复用。

## 目录结构

```
<repo-root>/
  .cache/
    deps/
      fmt/                # 已解压的源码树（包含顶层 CMakeLists.txt）
      nlohmann_json/
      Catch2/
      mimalloc/
      manifest.json       # 每个依赖的 tag/来源/抓取时间
```

可以通过 `--cache-root`（Bash）或 `-CacheRoot`（PowerShell）改变缓存
根目录；后续会接入 `POLYGLOT_DEPS_CACHE_ROOT` 环境变量。

`.cache/` 已加入 `.gitignore`，缓存仅属于本机。

## 工作原理

`Dependencies.cmake` 中新增了 `_polyglot_use_cached_dep(<dir> <name>)`
辅助函数。**对每个声明的依赖**，只要缓存目录存在且其中含有顶层
`CMakeLists.txt`，该函数就会把 CMake 标准变量
`FETCHCONTENT_SOURCE_DIR_<UPPER>` 指向缓存目录。被这个变量指向后，
`FetchContent_Declare` 会**完全跳过** git/HTTPS 访问，直接使用本地副本。

如果缓存目录为空，则该变量保持未设置，CMake 退回到原本的联网行为——
也就是说，离线缓存机制**纯粹是叠加性的**，不会破坏现有的在线工作流。

如果想强制走严格离线模式（缺依赖时直接失败而不是悄悄联网）：

```bash
cmake -S . -B build -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

打包脚本通过 `--offline` / `-Offline` 选项暴露这一开关。

## 填充缓存

### Windows / PowerShell

```powershell
# 默认：只抓缺失项；每种通道最多重试 3 次。
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1

# 强制重新抓取所有依赖（例如升级 tag 之后）。
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 -Refresh

# 当 github.com 不可达时使用 GitHub 镜像。
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 `
    -Mirror "https://gitclone.com/github.com/"
```

### Linux / macOS

```bash
scripts/fetch_deps.sh
scripts/fetch_deps.sh --refresh
scripts/fetch_deps.sh --mirror https://gitclone.com/github.com/
```

两个脚本依次尝试：

1. `git clone --depth 1 --branch <tag>`（3 次重试，5s/15s/45s 退避）
2. 通过 HTTPS 下载
   `https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.tar.gz`
   后在本地 `tar -xzf` 解压（3 次重试，相同退避）

成功后会把所选通道与解析得到的 tag 写入 `manifest.json`，下一次
使用同一 tag 再次运行就是 no-op。

如果缓存根目录为空，但上一次的 `build/_deps/<name>-src/` 还在，
脚本会先**导入**那份源码（无需联网）——非常方便老 checkout 升级到 1.4.1。

## 与打包脚本的整合

三个打包脚本同步新增了四组对等选项：

| Bash 选项 | PowerShell 选项 | 含义 |
|---|---|---|
| `--refresh-deps` | `-RefreshDeps` | 强制 `fetch_deps` 重新抓取所有依赖 |
| `--offline` | `-Offline` | 给 CMake 传 `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` |
| `--skip-deps` | `-SkipDeps` | 不调用 `fetch_deps`（CI 中已预先填好缓存） |
| `--deps-mirror <url>` | `-DepsMirror <url>` | 转发一个 GitHub 镜像 URL |

默认情况下，打包脚本会在 CMake configure 之前调用一次 `fetch_deps`。
缓存已就绪时这一调用几乎是零开销（每个依赖都打印 `[OK] cache hit`），
不会触发任何网络。

## 钉死的版本

依赖清单分布在三个文件中，**必须保持同步**：

- `Dependencies.cmake`（权威来源）
- `scripts/fetch_deps.ps1`（`$DepSpecs`）
- `scripts/fetch_deps.sh`（`DEPS=()`）

当前 1.4.1 的 pin：

| 名称 | Tag |
|---|---|
| `fmt` | `11.2.0` |
| `nlohmann_json` | `v3.11.3` |
| `Catch2` | `v3.5.4` |
| `mimalloc` | `v2.1.7` |

升级 tag 时，请同时更新三处，并执行 `fetch_deps -Refresh` 一次。

## 排错

- **两种通道都超时。** 用 `-Mirror` / `--mirror` 重新指向一个区域镜像；
  或者从其他机器手工把目录拷到 `<repo>/.cache/deps/<name>/`。
- **Windows 上找不到 `tar`。** Win10/11 的 `System32` 自带 BSD
  `tar.exe`。若没有，安装 Git for Windows（自带 `tar`）或使用 WSL。
- **严格离线构建依然访问了网络。** 确认每个缓存目录里都有顶层
  `CMakeLists.txt`；否则辅助函数会拒绝重定向 FetchContent，从而退回到
  联网通道。
