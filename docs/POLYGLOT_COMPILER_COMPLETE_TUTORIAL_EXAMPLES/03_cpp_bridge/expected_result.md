# Expected result

当前可验证结果：C++ 编译生成含未改名 `read_count`/`print_count` 的 object；Ploy 兼容 LINK 前端以 exit 0 完成并产生已知 deprecation/ABI warnings；正规化 payload 在 `expected_diagnostics.json`。IR、object 与 call-graph 是下一层 artifact。只有 bridge 最终接通后，executable 才应输出 `expected_stdout.txt` 并 exit 0。

Currently verifiable results:

- C++ compilation creates `reader.o` containing unmangled `read_count` and `print_count` symbols.
- Ploy frontend analysis accepts the compatibility LINK form with exit 0 and emits known deprecation/ABI warnings rather than unknown-language or empty-symbol errors.
- The complete normalised frontend payload is stored in `expected_diagnostics.json`.
- Compiler artifact emission creates IR, object, and `polyglot.callgraph.v1` JSON.
- A correctly linked executable prints the bytes in `expected_stdout.txt` and exits 0.

当前 signed LINK 形式会在 Sema 丢失 source/target language 与 source symbol；不要为了消除兼容 warning 而把本示例机械改成尚未打通的形式。

The signed LINK form currently loses source/target language and source-symbol fields in Sema; do not mechanically migrate this example merely to remove compatibility warnings.
