# 发布打包指南

本文档描述如何在各支持平台上构建 PolyglotCompiler 的发布版本包。

## 概述

`scripts/` 目录包含各平台的打包脚本，自动完成：

1. 以 **Release** 模式构建项目
2. 收集二进制文件、Qt 运行时依赖和文档
3. 生成可分发的压缩包

| 平台 | 脚本 | 输出 |
|------|------|------|
| **Windows** | `scripts/package_windows.ps1` | 免安装 `.zip` + NSIS 安装程序 `.exe` |
| **Linux** | `scripts/package_linux.sh` | 免安装 `.tar.gz` |
| **macOS** | `scripts/package_macos.sh` | 免安装 `.tar.gz`（含 `.app` 包） |

## 前置要求

### 所有平台
- CMake >= 3.20
- Ninja（推荐）或平台原生构建工具
- 支持 C++20 的编译器

### Windows
- MSVC 2022（Visual Studio 或 Build Tools）
- 已安装 Qt6（默认路径：`D:\Qt`）
- [NSIS 3.x](https://nsis.sourceforge.io/) — 仅生成安装程序时需要

### Linux
- GCC 12+ 或 Clang 15+（C++20 支持）
- Qt6 开发库（可选，用于 polyui）

### macOS
- Xcode Command Line Tools（Apple Clang）
- Qt6（可选，用于 polyui）

## 使用方法

### Windows

在项目根目录打开 PowerShell 终端：

```powershell
# 完整构建 + 免安装 zip + NSIS 安装程序
.\scripts\package_windows.ps1

# 自定义 Qt 路径
.\scripts\package_windows.ps1 -QtRoot "C:\Qt"

# 跳过构建（打包已有的构建产物）
.\scripts\package_windows.ps1 -SkipBuild

# 仅生成免安装 zip（不生成 NSIS 安装程序）
.\scripts\package_windows.ps1 -SkipInstaller
```

**输出：**
```
dist/
├── PolyglotCompiler-1.0.0-windows-x64-portable.zip
└── PolyglotCompiler-1.0.0-windows-x64-setup.exe
```

### Linux

```bash
chmod +x scripts/package_linux.sh

# 完整构建 + 免安装 tar.gz
./scripts/package_linux.sh

# 指定 Qt 路径
./scripts/package_linux.sh --qt-root /opt/Qt

# 跳过构建
./scripts/package_linux.sh --skip-build
```

**输出：**
```
dist/
└── PolyglotCompiler-1.0.0-linux-x86_64-portable.tar.gz
```

### macOS

```bash
chmod +x scripts/package_macos.sh

# 完整构建 + 免安装 tar.gz
./scripts/package_macos.sh

# 指定 Qt 路径
./scripts/package_macos.sh --qt-root ~/Qt

# 跳过构建
./scripts/package_macos.sh --skip-build
```

**输出：**
```
dist/
└── PolyglotCompiler-1.0.0-macos-arm64-portable.tar.gz
```

## 包内容

### 免安装版（所有平台）

```
PolyglotCompiler-1.0.0-<平台>/
├── bin/
│   ├── polyc(.exe)         # 编译器
│   ├── polyld(.exe)        # 链接器
│   ├── polyasm(.exe)       # 汇编器
│   ├── polyopt(.exe)       # 优化器
│   ├── polyrt(.exe)        # 运行时工具
│   ├── polybench(.exe)     # 基准测试工具
│   ├── polyui(.exe)        # IDE（需构建时有 Qt）
│   └── (Qt DLL/SO)         # Qt 运行时（仅 Windows/Linux）
├── docs/                   # 文档
├── README.md
└── LICENSE
```

### Windows 安装程序

NSIS 安装程序提供：
- 标准 Windows 安装/卸载流程
- 可选的 PATH 注册（将 `bin/` 添加到系统 PATH）
- IDE 的开始菜单快捷方式
- 卸载程序（清理注册表）

## NSIS 安装脚本

安装程序定义在 `scripts/installer.nsi`。`package_windows.ps1` 会自动以正确参数调用它。手动构建：

```cmd
makensis /DPRODUCT_VERSION=1.0.0 ^
         /DSTAGE_DIR=dist\stage\PolyglotCompiler-1.0.0-windows-x64 ^
         /DOUTPUT_FILE=dist\PolyglotCompiler-1.0.0-windows-x64-setup.exe ^
         scripts\installer.nsi
```

## 版本管理

项目版本（`1.0.0`）定义在以下位置：

| 位置 | 用途 |
|------|------|
| `CMakeLists.txt`（`project(... VERSION 1.0.0)`） | 构建系统版本号 |
| `tools/ui/*/main.cpp` | IDE 版本显示 |
| `tools/polyc/src/driver.cpp` | 编译器横幅 |
| `tools/polyrt/src/polyrt.cpp`（`kVersion`） | 运行时工具版本 |
| `scripts/package_*.ps1/sh` | 打包脚本 |
| `scripts/installer.nsi` | 安装程序元数据 |
| `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md` | 文档头部 |
| `README.md` | 仓库徽章 |

升级版本时，请更新以上所有位置。CMake 的 `PROJECT_VERSION` 变量自动用于 macOS bundle 元数据。
