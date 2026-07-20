# Poly highlighting demo

This file exercises every token family recognised by the Typora mode. Open it
in Typora after installation; both fence labels below must be highlighted.

本文件覆盖 Typora 高亮模式识别的所有主要 token 类型。安装后用 Typora
打开本文件，下面的规范 `poly` 与兼容 `ploy` 两种代码块都应出现高亮。

## Canonical `poly` name / 规范 `poly` 名称

```poly
/// A typed, documented function with attributes.
@inline @hot
PUB ASYNC FUNC describe<T: Display>(value: T) -> STRING {
    LET retries: u8 = 0x03;
    LET ratio: f64 = 1.25e-2;
    LET path = r#"C:\data\"quoted\""#;
    LET message = f"value = {value}, path = {path}";

    IF TRUE AND retries >= 0 {
        RETURN message;
    } ELSE {
        RETURN "unreachable\n";
    }
}
```

Expected categories / 预期分类：

- `PUB`, `ASYNC`, `FUNC`, `LET`, `IF`, `RETURN` → keyword / 关键字；
- `STRING`, `u8`, `f64` → type / 类型；
- `describe`, `retries`, `ratio`, `path`, `message` after declarations → definition / 定义；
- `TRUE` → atom / 原子值；
- `@inline`, `@hot` → metadata / 属性；
- `0x03`, `1.25e-2` → number / 数值；
- regular, raw, and template literals → string / 字符串。

## Legacy `ploy` alias / 兼容 `ploy` 别名

```ploy
CLASS python::widgets::Counter {
    METHOD __init__(start: i32) -> VOID;
    METHOD increment(step: i32) -> i32;
    ATTR value: i32;
}

FUNC use_counter() -> OPTION<i32> {
    LET counter: HANDLE<python::widgets::Counter> =
        NEW(python, widgets::Counter, 0);
    LET value = METHOD(python, counter, increment, 1);

    MATCH value {
        CASE 0 -> { RETURN None; }
        DEFAULT -> { RETURN Some(value); }
    }
}

LET long_text = """first line
second line""";

/* Ordinary block comment.
   It remains a comment on the next line. */
```

`CLASS`, `HANDLE`, and `ATTR` are contextual keywords in the compiler. The
editor gives them stable keyword/type colours everywhere so that schema code is
easy to scan; the parser remains the authority on where they are legal.

编译器把 `CLASS`、`HANDLE`、`ATTR` 视为上下文关键字。编辑器会在所有位置
稳定地为它们着色，便于阅读 schema；它们是否能出现在某个位置仍以 parser
为准。
