# 可运行的原生运行时示例

这些程序调用 polyc 的显式原生 API。实际验证平台为 macOS ARM64；不需要对应语言的解释器、JVM 或 CLR 来运行生成的文件。

## 排序与文件统计

```bash
cmake --build build-release --target polyc polyld -j8
build-release/polyc --strict --no-package-index --no-aux -O2 \
  examples/native_runtime/number_stats.poly -o build-release/number-stats
build-release/number-stats examples/native_runtime/numbers.txt build-release/sorted.txt
```

预期输出：

```text
count: 8
sum: 1047
min: -21
max: 1000
```

`sorted.txt` 每行一个整数：`-21, -7, 0, 5, 9, 19, 42, 1000`。

示例最多处理 1024 个整数，使用插入排序，保留 `-2147483648` 作为结束标记；求和输入应避免有符号溢出。这些限制属于示例，不是数组 API 的容量上限。

## 九种语言的 API 示例

`runtime.poly/cpp/py/rs/go/java/cs/js/rb` 分别是独立输入，可单独编译：

```bash
build-release/polyc --strict --no-package-index --no-aux \
  examples/native_runtime/runtime.py -o build-release/runtime-demo
build-release/runtime-demo hello -9223372036854775808 9223372036854775808 12x
```

会验证参数解析、整数数组、越界返回值，并在当前目录创建 `native-values.txt`。其他扩展名使用相同命令。

## 原生 API 约定

| API | 行为 |
| --- | --- |
| `print_i64(value)` | 输出完整的有符号 64 位十进制整数及换行 |
| `print_f64(value)` | 输出精确的十六进制浮点数及换行，保留负零 |
| `print_text(text)` | 输出以 NUL 结尾的 UTF-8 字节，不自动换行 |
| `args_count()` | 参数总数，包含程序名 |
| `arg_text(index)` | 返回指定参数；无效索引返回空字符串 |
| `arg_int(index, fallback)` | 解析完整十进制参数；格式错误、溢出或缺失返回 fallback |
| `array_new(length)` | 创建清零的 i64 数组，返回整数句柄；无效长度或分配失败返回 0 |
| `array_len(handle)` | 返回长度；空句柄返回 0 |
| `array_get(handle, index, fallback)` | 读取元素；越界或空句柄返回 fallback |
| `array_set(handle, index, value)` | 写元素，成功返回 0；越界或空句柄返回 -1 |
| `array_free(handle)` | 释放数组；空句柄可安全传入 |
| `file_open_ints(path)` | 只读打开文件，返回描述符；失败返回 -1 |
| `file_open_write(path)` | 创建或截断文件，返回描述符；失败返回 -1 |
| `file_next_int(fd, fallback)` | 扫描下一个有符号十进制整数；结束、错误或溢出返回 fallback |
| `file_write_text(fd, text)` | 写文本，处理短写入；成功返回字节数，失败返回 -1 |
| `file_write_int(fd, value, separator)` | 写整数并附上非零的 ASCII 分隔字符；成功返回字节数，失败返回 -1 |
| `file_close(fd)` | 关闭描述符 |

数组句柄只能来自 `array_new`，释放后不能复用或再次释放。最大长度为 2^27；分配还受系统资源限制。它是显式内存管理 API，不能把任意整数当作有效指针。

原生字符串桥接用于这些 API 的字面量和 `arg_text` 结果，不代表实现了各语言完整的字符串、集合、异常或标准库。JavaScript 的整数 API 接受安全整数常量及返回的 i64 句柄；普通 Number 变量需要进一步的类型转换支持，BigInt 的任意精度语义尚未实现。

## 自动验证

```bash
python3 tests/native_programs/runtime_regression.py \
  --polyc build-release/polyc --output-root build-release/native-runtime
```

默认运行九种语言 × 四档优化 × 两种寄存器分配，共 72 个配置。每次检查进程退出码、完整 stdout、stderr 及文件内容；源码、命令和日志保留在独立结果目录。
