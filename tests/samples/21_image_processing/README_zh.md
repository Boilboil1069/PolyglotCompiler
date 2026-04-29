# 示例 `21_image_processing`

> 图像处理内核。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Rust |
| 入口 | `image_processing.ploy` |
| 主题 | 图像处理内核 |
| 预期 stdout | `21_image_processing: ok\r\n` |

## 文件

- `image_processing.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `greyscale_kernel.cpp` — 宿主语言源文件
- `box_blur.rs` — 宿主语言源文件

## 构建

```powershell
polyc image_processing.ploy --emit-obj=build/image_processing.obj --quiet
polyld build/image_processing.obj -o build/image_processing.exe
./build/image_processing.exe
```

## 预期运行输出

```
21_image_processing: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
