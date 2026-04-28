# 调试信息发射规范化

> 状态：1.4.0 起生效；影响 `backends/common/src/debug_emitter.cpp`、
> `backends/common/src/dwarf_builder.cpp` 与公开头
> `backends/common/include/debug_info.h`。

## 1. 为什么有这份文档

之前的 DWARF / PDB 发射器里散落着 `// Placeholder`、`// (placeholder)`、
`// Simplified` 之类的注释。其中 9 处其实并不是占位：它们标记的是 DWARF 规
格里那些"长度前缀先写 0、节末再回填"的字段（`unit_length`、`header_length`、
CIE / FDE 长度，以及 ELF 的 `e_shoff`），其最终值只有在外层段全部追加完成后
才能算出。代码本来就在末尾用一对 `Patch32(..., position, value)` 把这几
个槽位填好。

剩下 3 处则是真正需要打磨或换标签的地方：

| 位置                                                               | 旧注释                               | 实际情况                                                                                                           |
| ------------------------------------------------------------------ | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------ |
| `DwarfSectionBuilder::EncodeLineStatements`（debug_emitter.cpp）   | `address++; // Simplified address tracking` | 行为捷径——行号程序在每行只把 PC 推 1，根本不看 `DebugLineInfo::address`。                                          |
| `PdbSectionBuilder::GenerateGuid` 声明（debug_emitter.cpp）        | `// GUID generation (simplified)`    | 实现已经符合 RFC 4122 v4，注释误导。                                                                               |
| `DwarfBuilder::BuildLineNumberProgram`（dwarf_builder.cpp）        | `// Line number program (simplified)` | 遗留 fallback 路径，刻意比生产路径 `DwarfSectionBuilder::EncodeLineStatements` 简化。                              |

## 2. 长度前缀的"先占位、后回填"契约

每一处"reserved length, patched at section close"注释都对应以下不变量，
并由 `tests/unit/backends/debug_emitter_normalization_test.cpp` 守住：

* 该偏移处的字节最初是 0。
* 在外层 builder 方法返回前，必有一次 `Patch32(buffer, position, computed_length)`
  把它替换成正确值。
* 回填后的值等于 `区域字节数 − 长度前缀本身的字节数`（DWARF v5 §7.4 / §7.5 /
  §6.4.1；LSB §10.6.1；ELF gABI §1.4 中 `e_shoff` 的处理）。

适用范围：

| 段                       | 前缀                              | 规范引用                  |
| ------------------------ | --------------------------------- | ------------------------- |
| `.debug_info`            | `unit_length`（4 字节）           | DWARF v5 §7.4             |
| `.debug_line`            | `unit_length`、`header_length`    | DWARF v5 §6.2.4           |
| `.debug_frame`           | CIE 长度、FDE 长度                | DWARF v5 §6.4.1           |
| `.eh_frame`              | CIE 长度、FDE 长度                | System V ABI §10.6.1      |
| `.debug_aranges`         | unit 长度                         | DWARF v5 §6.1.2           |
| ELF64 文件头             | `e_shoff`（8 字节）               | ELF gABI Fig. 4-3         |

阅读源码时，找 `_pos = result.size()` 和 `WriteLE<uint32_t>(result, 0)` 这一对
紧接着出现的语句，再找匹配的 `Patch32` 调用——这就是契约的代码形态。

## 3. `DebugLineInfo::address` 与显式 PC 推进

`DebugLineInfo` 现在多了一个 `address` 成员：

```cpp
struct DebugLineInfo {
  std::string file;
  int line{0};
  int column{0};
  std::uint64_t address{0};
};
```

`DwarfSectionBuilder::EncodeLineStatements` 按以下规则使用该字段：

1. 第一行发射 `DW_LNE_set_address(entry.address)`（调用方未填则为 `0`，由
   链接器重定位），并把状态机 PC 寄存器同步到该地址。
2. 后续每行计算 `delta = max(entry.address, address + 1) − address`，发射
   `DW_LNS_advance_pc ULEB128(delta)`。即使调用方完全没有填入真实机器地址，
   行号程序也保证严格单调、每行可寻址。
3. `DW_LNS_copy` 之后把状态机 PC 更新成新值，确保下一行 delta 计算正确。

`.debug_line line program advances PC monotonically` 用例覆盖该行为：喂三
行地址递增的 entry，断言发射的字节流中至少出现一次 `DW_LNS_advance_pc`
（`0x02`）操作码。

## 4. PDB GUID 生成

`PdbSectionBuilder::GenerateGuid` 使用 `std::random_device` 提供熵，按
RFC 4122 §4.4 设定 version / variant 位：

```text
guid[6] = (guid[6] & 0x0F) | 0x40;   // Version 4
guid[8] = (guid[8] & 0x3F) | 0x80;   // Variant 1
```

连续两次发射结果一定不同——`PDB Info stream GUID has RFC 4122 v4 + variant 1 bits`
用例会连发两份 PDB，通过 VC70 magic 版本号（`20000404`）定位 GUID 并比较。

## 5. 测试不变量小结

`tests/unit/backends/debug_emitter_normalization_test.cpp` 新增 4 个用例：

| # | 标题                                                                  | 断言                                                                                                                       |
| - | --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 1 | `.debug_info unit_length is reserved-then-patched`                    | `LE32(body, 0) == body.size() − 4`，body ≥ 11 字节。                                                                       |
| 2 | `.debug_line unit_length and header_length are reserved-then-patched` | `LE32(body, 0) == body.size() − 4`；`LE32(body, 8) ∈ (0, body.size())`。                                                   |
| 3 | `.debug_line line program advances PC monotonically`                  | 喂三行地址递增的 entry 后，段载荷里至少出现一次 `DW_LNS_advance_pc` 操作码（`0x02`）。                                       |
| 4 | `PDB Info stream GUID has RFC 4122 v4 + variant 1 bits`               | `(guid[6] & 0xF0) == 0x40`、`(guid[8] & 0xC0) == 0x80`，且两次连续 GUID 不同。                                              |

## 6. 迁移说明

* `DebugLineInfo` 多了第四个带默认初始化的成员。原来 `{file, line, column}`
  这种 aggregate 写法继续编译并且行为等价（新字段默认为 `0`，与旧发射器
  用的值一致）。
* `DwarfSectionBuilder` 与 `PdbSectionBuilder` 仍是
  `backends/common/src/debug_emitter.cpp` 内的实现细节；对外公开面仍是
  `DebugEmitter::EmitDWARF` / `EmitPDB` / `EmitSourceMap`，对外 API 无变化。
* CMake 目标边界没动。新测试文件已经接到 `tests/CMakeLists.txt` 的
  `UNIT_TEST_BACKENDS` 列表里。
