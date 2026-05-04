# 38_generics —— 泛型 FUNC / STRUCT (since v1.15.0)

本示例演示 [demand 2026-04-28-15](../../docs/demand/demand.md) 引入的
表层语法：

```ploy
STRUCT Pair<A, B> { first: A, second: B }
FUNC max<T: Comparable>(a: T, b: T) -> T { ... }
FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric { ... }
```

## 表层语法

* **类型参数列表。** `<T: Bound1 + Bound2, U>` 紧随函数或结构体名出现。
  Bound 为普通标识符，由 sema 对照下方内建 trait 表校验。空列表
  （没有 `<...>`）表示非泛型声明。
* **WHERE 子句。** 位于返回类型与函数体之间的
  `WHERE T: Bound1 + Bound2, U: Bound3` 会把约束并入对应参数的
  bound 列表。引用未声明参数会在解析阶段被拒绝。
* **类型位置的泛型实例化。** 字段类型或变量注解中的
  `Pair<i32, String>` 会被解析为参数化类型，sema 校验类型实参解析。

## 内建 trait 注册表

| Bound        | 宿主语言契约                                                            |
| ------------ | ----------------------------------------------------------------------- |
| `Comparable` | C++ `std::totally_ordered`、Rust `Ord`、Java `Comparable`、.NET `IComparable`、Python `__lt__`/`__eq__` |
| `Hashable`   | C++ `std::hash`、Rust `Hash`、Java `hashCode`、.NET `GetHashCode`、Python `__hash__` |
| `Numeric`    | C++ `std::is_arithmetic`、Rust `Num`、Java `Number`、.NET `INumber`、Python `numbers.Number` |
| `Iterable`   | C++ ranges、Rust `IntoIterator`、Java `Iterable`、.NET `IEnumerable`、Python `__iter__` |
| `Display`    | C++ `operator<<`、Rust `Display`、Java `toString`、.NET `ToString`、Python `__str__` |

未知 bound 触发 sema `kTypeMismatch` 诊断。

## 下沉模型（MVP）

v1.15.0 的实现把每个类型参数解析为 `Any`（类型擦除），泛型函数与
结构体仅下沉一次。IR 把 `T` 视作不透明值，单个函数体服务所有调用点。
这样在保持诊断面稳定的同时，为后续按实例化生成独立 IR 函数留出
空间（见**未来工作**）。

## 构建与运行

```bash
polyc 38_generics/generics.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
./build/sample.exe
# 38_generics: ok
```

## 未来工作

* **单态化 (monomorphization)。** 按调用点参数类型推断出的具体类型
  元组生成独立 IR 函数；每个实例化生成一个跨语言 stub。
* **Bound 强校验。** 把内建 trait 检查对照具体类型下沉
  （如对非有序结构体拒绝 `Comparable`）。
* **`LINK` 声明中的参数化泛型。** 单态化就绪后，允许
  `LINK rust::vec::Vec<T> AS ...`。
* **高阶 bound。** 将注册表扩展为用户自定义 trait 与跨参数的 `WHERE`
  约束。
