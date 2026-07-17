# Expected result

动态库后缀随平台变化，但 basename 保持 `polyplug_tutorial_hello`。导出检查必须找到三个 mandatory symbols；实现还提供两个 optional lifecycle hooks。

The shared library name is platform-dependent but keeps the base name `polyplug_tutorial_hello`.

Export inspection must find:

```text
polyglot_plugin_get_info
polyglot_plugin_create
polyglot_plugin_destroy
```

The implementation also exports optional lifecycle hooks:

```text
polyglot_plugin_activate
polyglot_plugin_deactivate
```

配套 host 按 get-info → create → activate → deactivate → destroy 执行。完整实测 stdout 在 `expected_stdout.txt`；其中 activation log 是：

The companion host runs get-info → create → activate → deactivate → destroy. Its complete observed stdout is stored in `expected_stdout.txt`; the activation log line is:

```text
tutorial plugin activated
```

Activation 返回 0，host 进程 exit 0。Destruction 释放实例；library unload 后不能残留 callback 或指针。

Activation returns 0 and the process exits 0. Destruction releases the allocated instance, and no callback or pointer may survive library unload.
