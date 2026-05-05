# 文件类型查看器教程

PolyUI 内置图像、Hex / 二进制以及 SQLite 数据库的专用查看器。

## 1. 图像查看器

支持格式：PNG、JPEG、WebP、GIF、SVG、BMP。先看魔数，文本类
格式回退到扩展名。

* **缩放**：滚轮或 `+` / `-`，夹紧到 5 % – 6400 %。
* **平移**：中键拖动或方向键。
* **像素拾取**：点击任意像素，状态栏显示其 RGBA。
* **通道分离**：循环 Red / Green / Blue / Alpha，单独查看某通道。

## 2. Hex 查看器

面向超大文件（≥ 1 GiB）。查看器从不一次性载入整文件：通过分
块 I/O 回调读取，搜索时仅保留 `needle.size() - 1` 字节作为相
邻分块的重叠，跨块匹配也能命中。

* **跳转**：`Ctrl+G` 跳到按行对齐的偏移。
* **查找**：`Ctrl+F` 从光标开始按 hex 模式查找。
* **高亮区段**：链路分析（如 `polyld` 段映射）推送命名高亮，
  查看器内联渲染。

IDE 为已知 PolyglotCompiler 产物（`.ir.bin`、`.asm.bin`、
`.obj`）接入了 schema 渲染器，右栏在原始字节旁展示解码字段。

## 3. 二进制识别

`IdentifyBinary` 报告容器类型（ELF / PE / Mach-O / WASM）、位宽、
架构（`x86_64`、`aarch64`、`wasm32` …）、字节序与 subsystem。
反汇编面板在支持的架构上委托给 `polyasm`；不支持时回退到
`.byte` 占位渲染，面板始终有内容。

## 4. SQLite 客户端与 SQL Console

首批支持 SQLite（与 `samples/22_database_access` 联动）。

* **Schema 浏览器**：列出当前连接的表与列。
* **编辑器**：基于 schema 浏览器的补全；`Ctrl+Enter` 运行。
* **结果分页**：固定页大小、表头排序，通过 `ExportCsv` 导出
  CSV（值中的逗号、引号、换行均被正确转义）。
* **历史**：每条已执行语句连同结果记录在有界缓冲中。

其他驱动（PostgreSQL、MySQL）通过同一 `SqlDriver` 接口接入。
