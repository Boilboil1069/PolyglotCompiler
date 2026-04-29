# 示例 `26_state_machine`

> 有限状态机。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Java |
| 入口 | `state_machine.ploy` |
| 主题 | 有限状态机 |
| 预期 stdout | `26_state_machine: ok\r\n` |

## 文件

- `state_machine.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `transition_table.cpp` — 宿主语言源文件
- `FsmRunner.java` — 宿主语言源文件

## 构建

```powershell
polyc state_machine.ploy --emit-obj=build/state_machine.obj --quiet
polyld build/state_machine.obj -o build/state_machine.exe
./build/state_machine.exe
```

## 预期运行输出

```
26_state_machine: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
