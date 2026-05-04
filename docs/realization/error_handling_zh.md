# 错误处理实现说明

本文档说明 PolyglotCompiler v1.13.0 如何为 Ploy 实现结构化异常处理，
以及运行时桥如何把宿主语言异常映射到统一的 `Error` 句柄。

## 表层语法

Ploy v1.13.0 新增五个关键字：

| 关键字     | 作用                                       |
| --------- | ----------------------------------------- |
| `TRY`     | 受保护代码块的开始                         |
| `CATCH`   | 引入捕获到的 Error 的绑定                  |
| `FINALLY` | 无条件清理块                               |
| `THROW`   | 把一个值抛入外层处理器                     |
| `ERROR`   | 内建句柄类型的名字                         |

完整形式如下：

```ploy
TRY {
    body_statements;
}
CATCH (e: Error) {
    handler_statements;
}
FINALLY {
    cleanup_statements;
}
```

不带任何 `CATCH` 与 `FINALLY` 的裸 `TRY` 会被 sema 拒绝。允许多个
`CATCH` 子句；当前按"首个匹配胜出"派发，所有子句都按 `Error` 处理
（基于源语言的类型化派发列入未来工作）。允许只带 `FINALLY` 的形式。

## 内建 `Error` 句柄

捕获绑定的类型为内建句柄 `Error`，定义如下：

```ploy
HANDLE Error {
    message:    String;
    source_lang: String;
    stacktrace: List<String>;
}
```

存储由运行时持有；字段仅在所在 `CATCH` 块内有效。`source_lang` 取值
为 `ploy`、`python`、`cpp`、`java`、`dotnet`、`rust` 之一。

## IR 形态

`TRY` / `CATCH` / `FINALLY` 的下沉伪 IR 如下：

```
try.entry:
    %tag = call i32 __ploy_rt_try_begin()
    %thrown = icmp ne i32 %tag, 0
    br i1 %thrown, label %try.catch, label %try.body
try.body:
    <受保护体>
    call void __ploy_rt_try_end()
    br label %try.finally   ; 若无 FINALLY 则跳到 %try.merge
try.catch:
    %err = call ptr __ploy_rt_current_error()
    <把 err 绑定到声明名字>
    <catch 体>
    call void __ploy_rt_clear_error()
    br label %try.finally   ; 或 %try.merge
try.finally:
    <finally 体>
    br label %try.merge
try.merge:
    <后继>
```

`THROW <expr>;` 下沉为单次 `__ploy_rt_throw(<value>)` 调用，后接
`unreachable`。

## 运行时 ABI

`runtime/include/services/error_bridge.h` 公开如下 C ABI：

| 符号                                              | 作用                                          |
| ------------------------------------------------ | --------------------------------------------- |
| `__ploy_rt_try_begin()`                          | 推入处理器作用域；首次进入返回 0              |
| `__ploy_rt_try_end()`                            | 正常退出时弹出处理器作用域                    |
| `__ploy_rt_throw(msg)`                           | 抛出标签为 `ploy` 的 Error                    |
| `__ploy_rt_throw_from(msg, lang)`                | 抛出带宿主语言标签的 Error                    |
| `__ploy_rt_current_error()`                      | 当前 Error 的不透明指针                       |
| `__ploy_rt_current_error_message()`              | 以 NUL 结尾的消息                             |
| `__ploy_rt_current_error_source_lang()`          | 以 NUL 结尾的源语言标签                       |
| `__ploy_rt_current_error_stacktrace_count()`     | 栈帧数                                        |
| `__ploy_rt_current_error_stacktrace_at(i)`       | 第 `i` 个栈帧；超界返回 `nullptr`             |
| `__ploy_rt_clear_error()`                        | 标记当前 Error 已被消费                       |

数据平面线程局部；负载存储由运行时在所在 `CATCH` 块期间持有。

`__ploy_rt_throw` 抛出 C++ 的
`polyglot::runtime::services::RuntimeError`，外层任何原生帧均可捕获。
之所以未在 IR 站点直接采用 setjmp/longjmp，是因为下沉发出的是普通
调用（而非 returns-twice 内建）；把 IR 级条件跳转与 C++ 解栈路径
真正合并列入未来工作。

## 宿主语言映射

| 宿主语言   | 外语类型              | 适配器入口             |
| --------- | -------------------- | ---------------------- |
| Python    | `Exception`          | `pyloy_throw_python`   |
| C++       | `std::exception`     | `cppploy_throw_cxx`    |
| Java      | `Throwable`          | `jloy_throw_java`      |
| .NET      | `Exception`          | `clrloy_throw_dotnet`  |
| Rust      | `Result::Err`        | `rsloy_throw_rust`     |

每个适配器以表中的语言标签调用 `__ploy_rt_throw_from(msg, lang)`。
反向——把 Ploy 的 `Error` 交回给宿主语言代码——由各语言桥层在 Ploy
被调者经由外语调用边界返回时执行。今天该路径只实现了数据平面（外
语调用方可通过 `__ploy_rt_current_error_*` 读取当前 Error），IR 级
派发器列入未来工作。

## 未来工作

* 基于 `invoke` / `landingpad` 的 IR 级派发，使 `TRY` 体内每个可能
  抛出的调用通过真实异常解栈路由到正确的 catch。
* 类型化 catch 派发：依据 Error 的源语言或用户自定义 Error 子类型
  选择匹配的 `CATCH` 子句。
* 后缀 `?` 短路传播（`expr?`）：改写为一个隐式 `TRY`，把捕获到的
  Error 从外层函数中再次抛出。
* 跨语言反向拦截：把 Ploy 回调内部抛出的 `Error` 路由进外语调用方
  的异常体系。
