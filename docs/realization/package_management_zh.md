# .ploy 包管理

## 概述

`.ploy` 语言提供了全面的包管理功能，允许从不同语言生态系统（Python、Rust、C++、Java、.NET）导入包，
支持版本约束、选择性导入、包自动发现和虚拟环境配置。

## 功能

### 1. 版本约束

指定项目所需包的最低、最高或精确版本。

**语法：**

```ploy
IMPORT <语言> PACKAGE <包名> <版本运算符> <版本号>;
```

**支持的版本运算符：**

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `>=`   | 大于等于 | `IMPORT python PACKAGE numpy >= 1.20;` |
| `<=`   | 小于等于 | `IMPORT python PACKAGE scipy <= 1.10.0;` |
| `==`   | 精确匹配 | `IMPORT python PACKAGE torch == 2.0.0;` |
| `>`    | 严格大于 | `IMPORT python PACKAGE flask > 2.0;` |
| `<`    | 严格小于 | `IMPORT python PACKAGE django < 5.0;` |
| `~=`   | 兼容发布 | `IMPORT python PACKAGE requests ~= 2.28;` |

**兼容发布（`~=`）** 遵循 PEP 440 语义：
- `~= 1.20` 表示 `>= 1.20, < 2.0`
- `~= 1.20.3` 表示 `>= 1.20.3, < 1.21.0`

**示例：**

```ploy
// 要求 NumPy 版本 1.20 或更高
IMPORT python PACKAGE numpy >= 1.20;

// 要求精确的 PyTorch 版本
IMPORT python PACKAGE torch == 2.0.0;

// 对 Rust crate 使用版本约束
IMPORT rust PACKAGE serde >= 1.0;

// 与别名组合使用
IMPORT python PACKAGE numpy >= 1.20 AS np;
```

**验证：** 语义分析器在编译时验证版本字符串。如果启用了包自动发现，且已安装的包版本
不满足约束条件，将报告编译时错误。

### 2. 选择性导入

仅从包中导入特定的函数、类或符号，而不是整个模块。

**语法：**

```ploy
IMPORT <语言> PACKAGE <包名>::(<符号1>, <符号2>, ...);
```

**示例：**

```ploy
// 仅从 numpy 导入 array、mean 和 std
IMPORT python PACKAGE numpy::(array, mean, std);

// 从子模块导入特定函数
IMPORT python PACKAGE numpy.linalg::(solve, inv);

// 单符号导入
IMPORT python PACKAGE os::(path);
```

**与版本约束和别名的组合：**

完整语法的解析顺序为：

```
IMPORT <语言> PACKAGE <包名>[::(<符号列表>)] [<版本运算符> <版本号>] [AS <别名>];
                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~~~
                      ①包名+可选选择性导入          ②可选版本约束(修饰包)    ③可选别名
```

> **注意：** 版本约束 `>= 1.20` 修饰的是**整个包**（如 numpy），而非选择性导入的符号。
> 选择性导入 `::()` 紧跟包名，表示"从该包中仅导入这些符号"；版本约束在其后，表示"该包的版本要求"。

```ploy
// 选择性导入 + 版本约束：要求 numpy >= 1.20，且仅导入 array 和 mean
IMPORT python PACKAGE numpy::(array, mean) >= 1.20;

// 选择性导入 + 版本约束：要求 torch >= 2.0，且仅导入 tensor 和 no_grad
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// 整包导入 + 别名：将 torch 包别名为 pt
IMPORT python PACKAGE torch >= 2.0 AS pt;
```

> **限制：** 选择性导入 `::()` 与 `AS` 别名**不能同时使用**。
> 例如 `IMPORT python PACKAGE torch::(tensor, no_grad) AS pt;` 是非法的，
> 因为 `pt` 无法确定指代的是 `torch::tensor` 还是 `torch::no_grad`。
> 如需别名，请使用整包导入（不带 `::()`）；如需选择性导入，请省略 `AS`。

**行为说明：**
- 包本身被注册为符号（例如 `numpy`）
- 每个选择的符号被单独注册（例如 `array`、`mean`）
- 链接器仅为选择的符号生成目标绑定
- 选择列表中的重复符号会导致编译时错误
- 选择性导入与 AS 别名的组合会导致编译时错误

### 3. 包自动发现

编译器自动检测本地环境中已安装的包，并验证导入的包是否存在。

**支持的发现机制：**

| 语言   | 发现方法      | 使用的命令 |
|--------|-------------|------------|
| Python | pip freeze  | `python -m pip list --format=freeze` |
| Rust   | cargo install | `cargo install --list` |
| C/C++  | pkg-config  | `pkg-config --list-all` |

**行为说明：**
- 发现在遇到 `PACKAGE` 导入时自动触发
- 每种语言每个编译单元最多运行一次发现
- 如果配置了虚拟环境（通过 `CONFIG VENV`），发现将使用虚拟环境的 Python 解释器
  而不是系统 Python
- 发现是**尽力而为**的：如果未找到包，编译器**不会**报错 —— 该包可能在链接时
  在不同的环境中可用

### 4. 虚拟环境支持

配置用于包解析和发现的特定虚拟环境。

**语法：**

```ploy
CONFIG VENV [<语言>] "<虚拟环境路径>";
```

**示例：**

```ploy
// 配置 Python 虚拟环境（语言默认为 "python"）
CONFIG VENV python "C:/Users/me/envs/data_science";

// 省略语言时默认使用 Python
CONFIG VENV "/home/user/.virtualenvs/ml";

// 必须出现在使用它的 IMPORT 语句之前
CONFIG VENV python "/opt/envs/production";
IMPORT python PACKAGE numpy >= 1.20;
```

**规则：**
- 每个编译单元每种语言只允许一个虚拟环境配置
- 同一语言的重复 `CONFIG VENV` 会产生编译时错误
- 语言必须是有效的支持语言（cpp、python、rust、c、ploy）
- 虚拟环境路径用于运行包发现命令
- 在 Windows 上，虚拟环境的 Python 位于 `<venv_path>\Scripts\python.exe`
- 在 Unix/macOS 上，虚拟环境的 Python 位于 `<venv_path>/bin/python`

## 完整示例

```ploy
// 配置 Python 虚拟环境
CONFIG VENV python "C:/Users/me/envs/data_science";

// 带版本约束的导入
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE scipy.optimize >= 1.8 AS opt;
IMPORT python PACKAGE pandas >= 1.5.0;

// 选择性导入
IMPORT python PACKAGE numpy::(array, mean, std);

// 选择性导入与版本约束组合
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// Rust crate 带版本约束
IMPORT rust PACKAGE rayon >= 1.7;

// 标准限定导入
IMPORT cpp::math;
IMPORT rust::serde;

// 在 LINK 声明中使用导入的包
LINK(cpp, python, compute_mean, np::mean);
LINK(cpp, python, compute_std, np::std);

MAP_TYPE(cpp::double, python::float);

// 使用跨语言调用的管道
PIPELINE data_analysis {
    FUNC statistics(data: LIST(f64)) -> TUPLE(f64, f64) {
        LET avg = CALL(python, np::mean, data);
        LET sd = CALL(python, np::std, data);
        RETURN (avg, sd);
    }
}

EXPORT data_analysis AS "data_analysis_pipeline";
```

## IR 元数据生成

对于每个使用扩展功能的导入，降低阶段会生成 IR 元数据：

- **版本约束**：一个全局符号 `__ploy_module_<lang>_<pkg>_version_constraint`，
  包含运算符和版本字符串（例如 `">= 1.20"`）
- **选择的符号**：一个全局符号 `__ploy_module_<lang>_<pkg>_selected_symbols`，
  包含逗号分隔的选择符号列表，以及每个选择符号的单独外部符号声明

此元数据由多语言链接器使用，用于：
1. 在链接时验证包兼容性
2. 仅为选择的符号生成目标绑定代码
3. 为运行时执行配置正确的虚拟环境
