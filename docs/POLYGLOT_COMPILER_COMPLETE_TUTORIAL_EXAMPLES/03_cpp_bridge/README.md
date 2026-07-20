# 03 C++ bridge — Chapters 5 and 15–17 — `LAYERED`

该示例与教材第 5 章一致：Poly 通过当前可工作的兼容 `LINK` 形式调用两个稳定的 `extern "C"` 函数。前端与 C++ 翻译单元可独立验证；跨语言 lowering 当前仍是分层能力，因此 runner 不把最终 stdout 当作已实测结果。

This matches Chapter 5: Poly calls two stable `extern "C"` functions through the currently working compatibility LINK form. The frontend and C++ translation unit are independently verifiable; cross-language lowering remains layered, so the runner does not claim final stdout as observed.

成功接线后的 stdout 必须与 `expected_stdout.txt` 完全相同；`expected_result.md` 还记录静态产物。

After successful wiring, stdout must match `expected_stdout.txt`; `expected_result.md` also records static artifacts.
