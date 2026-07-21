/**
 * @file     i18n.h
 * @brief    Locale enumeration, string catalog and missing-string
 *           scanner.  The IDE's Qt layer plugs `Translator` into
 *           `QTranslator`; the catalog itself is transport free.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::i18n {

enum class Locale {
  kZhCN,    ///< Simplified Chinese.
  kZhTW,    ///< Traditional Chinese.
  kEn,      ///< English (en-US).
  kJa,      ///< Japanese.
  kKo,      ///< Korean.
};

std::string LocaleName(Locale l);                    ///< "zh-CN" etc.
std::optional<Locale> LocaleFromName(const std::string &name);
std::vector<Locale> BuiltinLocales();

/// Per-id, per-locale string store. Lookups fall back to the
/// fallback locale (English by default) and finally to the id
/// itself, so the UI never blanks out on a missing string.
class StringCatalog {
 public:
  StringCatalog();

  void set_fallback(Locale l) { fallback_ = l; }
  Locale fallback() const { return fallback_; }

  /// Insert / overwrite a translation for `(id, locale)`.
  void Put(const std::string &id, Locale locale, const std::string &text);
  /// Bulk-load every string for `locale` from a JSON document of
  /// the form `{ "id1": "text1", "id2": "text2", ... }`.
  bool LoadLocale(Locale locale, const std::string &json);

  std::string Translate(const std::string &id, Locale locale) const;
  bool Has(const std::string &id, Locale locale) const;
  std::vector<std::string> Ids() const;
  /// Ids whose `locale` translation is missing.
  std::vector<std::string> MissingIn(Locale locale) const;

  std::string Serialize(Locale locale) const;

 private:
  Locale fallback_{Locale::kEn};
  // strings_[id][locale_int] = text
  std::unordered_map<std::string,
                     std::unordered_map<int, std::string>> strings_;
};

/// Bound to a current locale; the IDE swaps these out on locale
/// change. `Translate` always returns a non-empty string.
class Translator {
 public:
  Translator(const StringCatalog *catalog, Locale current)
      : catalog_(catalog), current_(current) {}

  void set_locale(Locale l) { current_ = l; }
  Locale locale() const { return current_; }

  std::string Translate(const std::string &id) const;

 private:
  const StringCatalog *catalog_;
  Locale current_;
};

struct ScannerHit {
  std::string path;
  long long line{0};
  std::string snippet;        ///< Raw matched literal.
  std::string reason;         ///< "hardcoded-literal" / "missing-id".
};

/// CI gate: scan source for `tr("...")` calls and either flag
/// hardcoded literals or check that the id (the first argument
/// when it looks like a bareword id) is present in the catalog.
class MissingStringScanner {
 public:
  explicit MissingStringScanner(const StringCatalog *catalog,
                                Locale locale = Locale::kEn);

  std::vector<ScannerHit> Scan(const std::string &path,
                               const std::string &text) const;

 private:
  const StringCatalog *catalog_;
  Locale locale_;
};

}  // namespace polyglot::tools::ui::i18n
