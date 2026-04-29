# 静态类型化的跨语言对象互操作

> **需求**：2026-04-28-9 —— *跨语言对象作为 `HANDLE<lang::Class>` 进入静态类型系统*。
> **引入版本**：1.9.0。

## 1. 动机

在本特性之前，`.ploy` 中所有跨语言对象表达式都坍缩为不透明的
`Any` 类型：

| 表达式                                              | 1.9.0 之前 | 现在                                |
| --------------------------------------------------- | ---------- | ----------------------------------- |
| `NEW(python, torch::nn::Linear, 10, 5)`             | `Any`      | `HANDLE<python::torch::nn::Linear>` |
| `METHOD(python, model, forward, x)`                 | `Any`      | 声明的返回类型                      |
| `GET(python, model, in_features)`                   | `Any`      | 声明的 `ATTR` 类型                  |
| `SET(python, model, in_features, 5)`                | `Any`      | 与 `ATTR` 类型核对                  |

`Any` 的代价是：**每一个**跨语言调用点都成为运行期赌博 ——
参数个数、参数类型与返回类型都要等到外语解释器抛错时才被发现，而错误
栈通常深埋在 `__pyx_call` 之类的帧里，看不到任何 `.ploy` 源位置。

`HANDLE<lang::Class>` 把这份契约重新交还静态类型系统。一旦类有了已注
册的模式，sema 就会：

* **拒绝** `NEW` 构造与 `METHOD` 调用的实参个数错误；
* **拒绝**实参与 `SET` 写入值的类型错误；
* **解析** `METHOD` 与 `GET` 的返回类型，使下游（lowering、去虚拟化、
  ABI 编组）看到的是具体类型而非 `Any`；
* **禁止**隐式跨语言转换 —— `HANDLE<a::T>` 与 `HANDLE<b::U>` 静态不等，
  即便类名相同也是如此。

## 2. 表层语法

### 2.1 `CLASS` 模式声明

```ploy
CLASS python::torch::nn::Linear {
    METHOD __init__(in_features: i32, out_features: i32);
    METHOD forward(x: f32) -> f32;
    ATTR in_features: i32;
    ATTR out_features: i32;
}
```

文法（非形式化）：

```
class_decl   ::= "CLASS" lang "::" class_path "{" class_row* "}"
class_row    ::= "METHOD" name "(" param_list? ")" ("->" type)? ";"
              |  "ATTR"   name ":" type ";"
param_list   ::= param ("," param)*
param        ::= (name ":")? type
class_path   ::= ident ("::" ident)*
```

* `CLASS`、`HANDLE` 与 `ATTR` 是**上下文关键字**：拼写相同的普通标识符
  仍然以标识符身份返回词法层，因此把 `class`、`handle`、`attr` 用作
  变量名的现有样例继续可编译。`METHOD` 在本需求之前就已是规范关键
  字（跨语言方法调用形式），不受影响。
* 名为 `__init__`、`new`、`ctor` 的方法会被记入
  `ForeignClassSchema::constructor_sig`，被 `NEW` 取用。
* 模式按模块全局有效；同一 `lang::path` 重复声明属于硬错误。

### 2.2 `HANDLE<lang::class_path>` 类型表达式

```ploy
LET model: HANDLE<python::torch::nn::Linear> = NEW(python, torch::nn::Linear, 128, 10);
```

* 只要是 `ParseQualifiedOrSimpleType` 处理的类型位置，都能识别
  `HANDLE<...>`。
* 尖括号形式仅限 `HANDLE` 上下文关键字，避免与表达式位置上的
  `<` / `>` 比较运算符冲突。
* Sema 把 `HandleType { language, class_path }` 降到
  `core::Type::Class(class_path, language)`。两者相等当且仅当两个字段
  都相同。

### 2.3 向后兼容

对一个**未**注册模式的目标使用 `NEW(...)` 仍可解析，并返回
`core::Type::Unknown()` —— 即既有的动态分发路径。同样地，对非类型化
接收者的 `METHOD` / `GET` / `SET` 走的仍然是 1.9.0 之前的 LINK 签名
查找。现有样例无需改动即可继续构建。

## 3. Sema 实现

| 组件                                                  | 位置（文件:行，约略）                          |
| ----------------------------------------------------- | ---------------------------------------------- |
| `core::Type::Class(name, lang)` 工厂                  | `common/include/core/types.h:151`              |
| `HandleType` / `ClassDecl` / `ClassMethodSig` AST     | `frontends/ploy/include/ploy_ast.h:74` 与 `218` |
| `ParseClassDecl` + HANDLE 文法                        | `frontends/ploy/src/parser/parser.cpp`         |
| `AnalyzeClassDecl` + 模式注册辅助函数                 | `frontends/ploy/src/sema/sema.cpp`             |
| `ResolveType(HandleType)` 分支                        | 同文件，`ResolveType`                          |
| 模式感知的 `AnalyzeNewExpression`                     | 同文件，约第 1149 行                           |
| 模式感知的 `AnalyzeMethodCallExpression`              | 同文件，约第 1225 行                           |
| 模式感知的 `AnalyzeGetAttrExpression`                 | 同文件，约第 1322 行                           |
| 模式感知的 `AnalyzeSetAttrExpression`                 | 同文件，约第 1398 行                           |
| `AreTypesCompatible` 中的跨语言不兼容规则             | 同文件，约第 1597 行                           |

每个模式感知路径都在传统 `LookupSignature()` 回退**之前**执行，因此
当显式 `CLASS` 块与 LINK 派生签名同时存在时，前者总是胜出。

## 4. 诊断策略

| 情形                                       | 严重度 | 理由                                                                                                                                  |
| ------------------------------------------ | ------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| `NEW` / `METHOD` 实参个数不匹配            | 错误   | 与普通函数调用 sema 一致。                                                                                                            |
| 实参类型不匹配                             | 错误   | 与普通函数调用 sema 一致。                                                                                                            |
| `SET` 写入值类型不匹配                     | 错误   | 违反声明类型的属性写入会破坏外语对象的不变量。                                                                                        |
| 在类型化 handle 上调用未知 `METHOD`        | 警告   | 外语对象常用 Python `__getattr__`、C# `dynamic` 等机制动态挂方法。我们用警告告知静态分发已退化，但允许编译继续。                    |
| 在类型化 handle 上访问未知 `ATTR`          | 警告   | 同上。                                                                                                                                |
| 跨语言 handle 赋值                         | 错误   | `HANDLE<a::T>` 是外语运行时指针，将其重解释为 `HANDLE<b::U>` 一定会摧毁对方运行时。                                                   |

如需显式转换，可使用 `CONVERT` + `MAP_FUNC`：

```ploy
LET py_obj: HANDLE<python::A> = ...;
LET cpp_obj: HANDLE<cpp::A> = CONVERT(py_obj, MAP_FUNC bridge::py_to_cpp);
```

## 5. 测试与样例

* **单测**：`tests/unit/frontends/ploy/typed_handle_test.cpp` 覆盖
  模式注册、`NEW`/`METHOD`/`GET`/`SET` 解析、实参个数与类型错误、
  未知方法警告路径、跨语言混用错误路径、向后兼容回退，以及小写
  `handle` 标识符不被遮蔽。
* **样例**：`tests/samples/32_typed_handles/` —— 含双语 README、期望
  stdout 与回归脚本所需的确定性 PRINTLN 标记的可运行示例。

## 6. 未尽事项（已搁置）

* `HANDLE<self>` 与泛型类模式。
* 按参数类型重载方法。
* 由 `IMPORT` 自省自动生成 `CLASS` 块（Python `inspect.signature`、
  C++ `[[clang::reflect]]`）。
