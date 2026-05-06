# 示例 `23_http_client`

> HTTP 客户端示例。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Go |
| 入口 | `http_client.ploy` |
| 主题 | HTTP 客户端示例 |
| 预期 stdout | `23_http_client: ok\r\n` |

## 文件

- `http_client.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `HttpTransport.go` — 宿主语言源文件
- `response_decoder.py` — 宿主语言源文件

## 运行方式

> **当前版本 skip**：依赖 HTTP 客户端运行时适配。脚本读取 `expected_output.skip`，把该样例归入 SKIP 桶；待依赖能力落地后重命名回 `expected_output.txt` 即可启用。

## 构建

```powershell
polyc http_client.ploy --emit-obj=build/http_client.obj --quiet
polyld build/http_client.obj -o build/http_client.exe
./build/http_client.exe
```

## 预期运行输出

```
23_http_client: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
