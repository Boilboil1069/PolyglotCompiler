# 示例 `22_database_access`

> 数据库访问层。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Java |
| 入口 | `database_access.ploy` |
| 主题 | 数据库访问层 |
| 预期 stdout | `22_database_access: ok\r\n` |

## 文件

- `database_access.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `db_connection.py` — 宿主语言源文件
- `UserDao.java` — 宿主语言源文件

## 构建

```powershell
polyc database_access.ploy --emit-obj=build/database_access.obj --quiet
polyld build/database_access.obj -o build/database_access.exe
./build/database_access.exe
```

## 预期运行输出

```
22_database_access: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
