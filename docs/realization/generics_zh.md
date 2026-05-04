# 泛型 —— 带 Bound 的类型参数与类型擦除下沉 (since v1.15.0)

## 表层语法

```ploy
FUNC max<T: Comparable>(a: T, b: T) -> T {
    IF (a > b) { RETURN a; } ELSE { RETURN b; }
}

STRUCT Pair<A, B> { first: A, second: B }

FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric {
    RETURN a + b;
}
```

* **类型参数列表** —— `<T: Bound1 + Bound2, U>` 紧随函数或结构体名出现。
  Bound 为普通标识符，由 sema 对照内建 trait 注册表校验。
* **WHERE 子句** —— 位于返回类型与函数体之间的
  `WHERE T: Bound1 + Bound2, U: Bound3` 把约束并入对应参数的
  bound 列表。引用未声明参数会在解析阶段被拒绝。
* **泛型实例化** —— 类型位置中的 `Pair<i32, String>` 解析为
  携带类型实参的 `ParameterizedType`，sema 解析所有实参并坍缩到底层
  结构体身份。

## 内建 trait 注册表

| Bound        | 宿主语言契约                                                            |
| ------------ | ----------------------------------------------------------------------- |
| `Comparable` | C++ `std::totally_ordered`、Rust `Ord`、Java `Comparable`、.NET `IComparable`、Python `__lt__` / `__eq__` |
| `Hashable`   | C++ `std::hash`、Rust `Hash`、Java `hashCode`、.NET `GetHashCode`、Python `__hash__` |
| `Numeric`    | C++ `std::is_arithmetic`、Rust `Num`、Java `Number`、.NET `INumber`、Python `numbers.Number` |
| `Iterable`   | C++ ranges、Rust `IntoIterator`、Java `Iterable`、.NET `IEnumerable`、Python `__iter__` |
| `Display`    | C++ `operator<<`、Rust `Display`、Java `toString`、.NET `ToString`、Python `__str__` |

注册表实现位于 `frontends/ploy/src/sema/sema.cpp`
（`PloySema::ValidateTypeParamBounds`）。未知 bound 触发
`kTypeMismatch` 诊断。

## 下沉形态（MVP）

v1.15.0 把每个泛型声明仅下沉一次，所有类型参数被解析为
`core::Type::Any()`。函数体内对 `T` 的引用因而表现为不透明指针式值，
IR 构建器为每个泛型函数（或结构体布局）发射单一实体，服务所有调用点：

```text
function max(any %a, any %b) -> any { ... }
```

这样在保持诊断面稳定的同时，为后续按实例化生成独立 IR 函数留出空间。

## Sema 集成

* `FuncDecl::type_params` 与 `StructDecl::type_params` 持有
  `TypeParam { name, bounds }` 向量。WHERE 子句在解析阶段并入对应
  参数的 `bounds`。
* `PloySema::active_type_params_` 是 `ResolveType(SimpleType)` 优先
  查询的名字集合；命中即解析为 `Any`。该集合在 `AnalyzeFuncDecl` 与
  `AnalyzeStructDecl` 期间填充，所有退出路径都会弹出。
* `PloySema::ValidateTypeParamBounds` 对照内建注册表校验每个
  声明的 bound。

## 跨语言宿主映射

MVP 路径把每个泛型实例化视为一个跨语言 stub。例如从 Python 调用
`max(10, 20)` 会下沉为单一 `max` 符号，其参数类型退化为 `Any`；
宿主适配器走与其它返回 `Any` 的函数相同的装箱值通道。单态化落地后，
每个具体实例化将发布自己的 stub，并在上方注册表里逐宿主语言写明
类型契约。

## 未来工作

* **单态化 (monomorphization)。** 按调用点参数类型推断出的具体类型
  元组生成独立 IR 函数；每个实例化生成一个跨语言 stub。
* **针对具体类型的 bound 强校验。** 对非有序类型拒绝 `Comparable`；
  对非算术类型拒绝 `Numeric`；在调用点而非声明点报错。
* **`LINK` 声明中的参数化泛型。** 单态化就绪后允许
  `LINK rust::vec::Vec<T> AS ...`。
* **类的泛型方法。** 把 `CLASS` 块（demand 2026-04-28-9）扩展为
  支持 `METHOD push<T>(item: T)` 行。
* **高阶 bound。** 把注册表扩展为用户自定义 trait 与跨参数的
  `WHERE` 约束。
