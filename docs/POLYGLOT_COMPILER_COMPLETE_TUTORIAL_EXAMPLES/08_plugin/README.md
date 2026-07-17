# 08 Native plugin — Chapters 39 and 41 — `RUNNABLE`

这个最小 C plugin 实现 mandatory exports 和 optional activate/deactivate hooks，不注册 provider。`host_smoke.c` 提供最小 host service table 并执行完整 lifecycle，因此 public header、shared-library exports 和 activation output 都能验证。

This minimal C plugin implements mandatory exports and optional activation hooks without registering a provider. `host_smoke.c` supplies a minimal host service table and executes the lifecycle, validating the public header, exports, and activation output.

```sh
cmake -S docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/08_plugin \
  -B build/tutorial-examples/plugin
cmake --build build/tutorial-examples/plugin
build/tutorial-examples/plugin/tutorial_plugin_host
```

构建本身不产生业务 stdout。Host 的实测输出保存在 `expected_stdout.txt`；导出检查应找到三个 mandatory symbols，见 `expected_result.md`。

The build itself produces no business stdout. The host's observed output is in `expected_stdout.txt`; export inspection must find the mandatory symbols listed in `expected_result.md`.
