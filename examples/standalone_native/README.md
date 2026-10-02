# 九种语言的独立原生程序

每个 `calls.*` 都是独立编译单元：先定义两个辅助函数，再由入口调用它们计算 `2 × 13 + 2 × 8`。使用仓库内构建的 `polyc` 和同目录的 `polyld`：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target polyc polyld -j 8
mkdir -p build-release/native-examples
build-release/polyc --strict --no-package-index -O2 \
  examples/standalone_native/calls.cpp -o build-release/native-examples/calls
./build-release/native-examples/calls
echo $? # 42（计算结果，不是编译失败）
```

将 `.cpp` 换成 `.poly`、`.py`、`.rs`、`.go`、`.java`、`.cs`、`.js` 或 `.rb` 即可编译其余示例。JavaScript 的 `number` 使用浮点 ABI，入口返回 `结果 === 42`，所以正确退出码为 **1**；其余示例为 **42**。

这些程序使用 polyc 的独立原生入口约定：无参数，返回整数、布尔或 void。Java 为 `static main`，C# 为 `static Main`。C++ 示例的 `long main`、Rust/Go 示例的整数返回 `main` 是 polyc 原生入口约定；标准 C++ `int main()` 也受支持。Java 示例使用无参数静态入口。这些入口与对应官方工具链的标准程序入口存在差异。Python 需要类型注解，JavaScript 使用 JSDoc，Ruby 使用 YARD。这里没有启动 Python、JVM、CLR、Node 或 Ruby 解释器。

当前执行验证平台为 macOS ARM64；详见 [原生编译与性能评估](../../docs/NATIVE_COMPILATION_EVALUATION_zh.md)。
