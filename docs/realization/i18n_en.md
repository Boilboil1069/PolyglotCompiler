# i18n Contribution Guide

PolyUI ships with five built-in locales: Simplified Chinese
(`zh-CN`), Traditional Chinese (`zh-TW`), English (`en`),
Japanese (`ja`) and Korean (`ko`).  All UI strings are looked up
by **id**; hardcoded literals are forbidden and a CI scanner
(`MissingStringScanner`) fails the build when one slips in.

## Anatomy

* [`tools/ui/common/i18n/i18n.h`](../../tools/ui/common/i18n/i18n.h)
  defines `Locale`, `StringCatalog`, `Translator` and the CI
  scanner.  The Qt layer wires `Translator` into `QTranslator`
  but the catalog itself is transport-free and unit-tested
  without Qt.
* `StringCatalog::Put(id, locale, text)` registers a translation.
  `Translate(id, locale)` returns the requested text, falling
  back to the configured fallback locale (English by default)
  and finally to the id itself.
* `StringCatalog::MissingIn(locale)` powers the per-locale
  completeness reports.

## Adding a string

1. Pick a stable id following `<scope>.<key>` (e.g.
   `editor.action.save`).  Ids are case-sensitive.
2. Add the English source first; that is the fallback for every
   other locale.
3. Translate into every built-in locale.  Untranslated entries
   are surfaced by `MissingIn` and break the locale-coverage CI
   check.
4. Reference it from C++ via `tr("editor.action.save")`.  The
   scanner accepts only bareword ids (snake-case or
   dot-separated) — passing a literal English sentence trips the
   `hardcoded-literal` rule.

## Adding a new locale

1. Append the enumerator to `Locale` in
   [`i18n.h`](../../tools/ui/common/i18n/i18n.h).
2. Update `LocaleName`, `LocaleFromName` and `BuiltinLocales` in
   [`i18n.cpp`](../../tools/ui/common/i18n/i18n.cpp).
3. Provide a JSON catalog and load it through
   `StringCatalog::LoadLocale`.

## CI gate

Run the scanner against every C++ translation unit before
merging:

```cpp
MissingStringScanner scan(&catalog, Locale::kEn);
auto hits = scan.Scan(path, source);
```

A non-empty `hits` vector fails the build.  The hit's `reason`
field tells you whether the offending call passed a hardcoded
literal or referenced an id that the catalog does not yet know.

## Testing

Unit coverage lives in
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp);
extend it whenever you add a locale or change the scanner
heuristics.
