# 32 — 静态类型化的跨语言对象句柄

演示 **需求 2026-04-28-9**：跨语言对象作为 `HANDLE<lang::Class>`
进入静态类型系统，使 `NEW` / `METHOD` / `GET` / `SET` 在编译期被
检查，而不是直接转交给外语言运行时。

## 关键点

* `CLASS python::torch::nn::Linear { METHOD ...; ATTR ...; }`
  ——显式声明 Python 类的方法与属性签名。
* `CLASS cpp::matrix::Matrix { ... }`——C++ 类同理。
* `LET model: HANDLE<python::torch::nn::Linear> = NEW(python, ...)`
  ——构造表达式返回类型化句柄，而非不透明的 `Any`。
* `METHOD(python, model, forward, 1.0)`
  ——参数个数与类型按已注册的模式校验。
* `GET(python, model, in_features)`
  ——属性类型来自匹配的 `ATTR` 行。

## 运行

```powershell
polyc 32_typed_handles/typed_handles.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
.\build\sample.exe
```

期望 stdout：`32_typed_handles: ok`（末尾 `\r\n`）。

## 价值

* **编译期安全性。** `METHOD(...)` 参数数错或 `ATTR` 名称拼错会被 sema
  当场报错，而不是在外语言解释器内崩溃。
* **禁止跨语言隐式转换。** `HANDLE<python::A>` 与 `HANDLE<cpp::A>`
  在静态语义上互不相等——即便类名一致——必须经由 `CONVERT` +
  `MAP_FUNC` 显式转换。
* **向后兼容。** 没有对应 `CLASS` 块的 `NEW(...)` 仍然可解析，按动态
  调用降级（仅警告）；既有样例无需改写。
