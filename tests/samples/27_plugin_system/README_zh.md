# 示例 `27_plugin_system`

> 插件系统。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Python |
| 入口 | `plugin_system.ploy` |
| 主题 | 插件系统 |
| 预期 stdout | `27_plugin_system: ok\r\n` |

## 文件

- `plugin_system.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `plugin_host.cpp` — 宿主语言源文件
- `sample_plugin.py` — 宿主语言源文件

## 构建

```powershell
polyc plugin_system.ploy --emit-obj=build/plugin_system.obj --quiet
polyld build/plugin_system.obj -o build/plugin_system.exe
./build/plugin_system.exe
```

## 预期运行输出

```
27_plugin_system: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
