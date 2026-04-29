# 示例 `30_game_loop_demo`

> 游戏循环骨架。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Rust |
| 入口 | `game_loop_demo.ploy` |
| 主题 | 游戏循环骨架 |
| 预期 stdout | `30_game_loop_demo: ok\r\n` |

## 文件

- `game_loop_demo.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `tick_scheduler.cpp` — 宿主语言源文件
- `physics_step.rs` — 宿主语言源文件

## 构建

```powershell
polyc game_loop_demo.ploy --emit-obj=build/game_loop_demo.obj --quiet
polyld build/game_loop_demo.obj -o build/game_loop_demo.exe
./build/game_loop_demo.exe
```

## 预期运行输出

```
30_game_loop_demo: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
