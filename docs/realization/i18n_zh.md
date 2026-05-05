# i18n 贡献指南

PolyUI 内建五种语言：简体中文（`zh-CN`）、繁体中文（`zh-TW`）、
英文（`en`）、日文（`ja`）、韩文（`ko`）。所有 UI 字符串按 **ID**
查询；禁止源码硬编码字面量，CI 中的 `MissingStringScanner` 会在
违规出现时失败。

## 结构

* [`tools/ui/common/i18n/i18n.h`](../../tools/ui/common/i18n/i18n.h)
  定义 `Locale`、`StringCatalog`、`Translator` 与 CI 扫描器。
  Qt 层把 `Translator` 接到 `QTranslator`，但目录本身与传输无
  关，可在不依赖 Qt 的情况下做单元测试。
* `StringCatalog::Put(id, locale, text)` 注册一条翻译。
  `Translate(id, locale)` 返回该语言的文本，依次回退到回退语言
  （默认英文）以及 id 自身。
* `StringCatalog::MissingIn(locale)` 用来生成各语言的完成度报告。

## 新增字符串

1. 选取稳定 id，遵循 `<scope>.<key>`（如
   `editor.action.save`）。id 区分大小写。
2. 先写英文源串——它是所有其他语言的回退。
3. 翻译到每种内建语言。未翻译条目会被 `MissingIn` 暴露，并使
   语言覆盖率 CI 失败。
4. C++ 中用 `tr("editor.action.save")` 引用。扫描器只接受 bare-
   word id（snake_case 或点分），传入英文句子会触发
   `hardcoded-literal` 规则。

## 新增语言

1. 在 [`i18n.h`](../../tools/ui/common/i18n/i18n.h) 的 `Locale` 末
   尾追加枚举。
2. 在 [`i18n.cpp`](../../tools/ui/common/i18n/i18n.cpp) 中更新
   `LocaleName`、`LocaleFromName` 与 `BuiltinLocales`。
3. 提供 JSON 目录并通过 `StringCatalog::LoadLocale` 加载。

## CI 校验

合并前对每个 C++ 翻译单元运行扫描器：

```cpp
MissingStringScanner scan(&catalog, Locale::kEn);
auto hits = scan.Scan(path, source);
```

`hits` 非空即失败。`reason` 字段会告诉你是硬编码字面量还是
目录尚不识别的 id。

## 测试

单元覆盖位于
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp)；
新增语言或修改扫描启发式时请同步扩充。
