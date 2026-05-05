/**
 * @file     i18n.cpp
 * @brief    StringCatalog, Translator and missing-string scanner
 *           implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/i18n/i18n.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace polyglot::tools::ui::i18n {

using Json = nlohmann::json;

std::string LocaleName(Locale l) {
  switch (l) {
    case Locale::kZhCN: return "zh-CN";
    case Locale::kZhTW: return "zh-TW";
    case Locale::kEn:   return "en";
    case Locale::kJa:   return "ja";
    case Locale::kKo:   return "ko";
  }
  return "en";
}

std::optional<Locale> LocaleFromName(const std::string &n) {
  if (n == "zh-CN" || n == "zh_CN" || n == "zh") return Locale::kZhCN;
  if (n == "zh-TW" || n == "zh_TW")              return Locale::kZhTW;
  if (n == "en" || n == "en-US" || n == "en_US") return Locale::kEn;
  if (n == "ja" || n == "ja-JP" || n == "ja_JP") return Locale::kJa;
  if (n == "ko" || n == "ko-KR" || n == "ko_KR") return Locale::kKo;
  return std::nullopt;
}

std::vector<Locale> BuiltinLocales() {
  return {Locale::kZhCN, Locale::kZhTW, Locale::kEn,
          Locale::kJa,   Locale::kKo};
}

StringCatalog::StringCatalog() = default;

void StringCatalog::Put(const std::string &id, Locale l,
                        const std::string &text) {
  strings_[id][static_cast<int>(l)] = text;
}

bool StringCatalog::LoadLocale(Locale l, const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  for (auto it = doc.begin(); it != doc.end(); ++it) {
    if (it.value().is_string())
      Put(it.key(), l, it.value().get<std::string>());
  }
  return true;
}

bool StringCatalog::Has(const std::string &id, Locale l) const {
  auto it = strings_.find(id);
  if (it == strings_.end()) return false;
  return it->second.count(static_cast<int>(l)) > 0;
}

std::string StringCatalog::Translate(const std::string &id,
                                     Locale l) const {
  auto it = strings_.find(id);
  if (it == strings_.end()) return id;
  auto cur = it->second.find(static_cast<int>(l));
  if (cur != it->second.end()) return cur->second;
  auto fb = it->second.find(static_cast<int>(fallback_));
  if (fb != it->second.end()) return fb->second;
  return id;
}

std::vector<std::string> StringCatalog::Ids() const {
  std::vector<std::string> out;
  out.reserve(strings_.size());
  for (const auto &kv : strings_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> StringCatalog::MissingIn(Locale l) const {
  std::vector<std::string> out;
  for (const auto &kv : strings_)
    if (kv.second.count(static_cast<int>(l)) == 0) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

std::string StringCatalog::Serialize(Locale l) const {
  Json doc = Json::object();
  for (const auto &kv : strings_) {
    auto it = kv.second.find(static_cast<int>(l));
    if (it != kv.second.end()) doc[kv.first] = it->second;
  }
  return doc.dump(2);
}

std::string Translator::Translate(const std::string &id) const {
  if (!catalog_) return id;
  return catalog_->Translate(id, current_);
}

MissingStringScanner::MissingStringScanner(const StringCatalog *catalog,
                                           Locale locale)
    : catalog_(catalog), locale_(locale) {}

namespace {

bool LooksLikeId(const std::string &arg) {
  if (arg.empty()) return false;
  for (char c : arg) {
    bool ok = std::isalnum(static_cast<unsigned char>(c)) ||
              c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  // Bareword id heuristic: contains a dot ("scope.key") or all
  // upper / snake.
  if (arg.find('.') != std::string::npos) return true;
  bool snake = true;
  for (char c : arg) {
    if (!(std::isupper(static_cast<unsigned char>(c)) || c == '_' ||
          std::isdigit(static_cast<unsigned char>(c)))) {
      snake = false;
      break;
    }
  }
  return snake;
}

}  // namespace

std::vector<ScannerHit> MissingStringScanner::Scan(
    const std::string &path, const std::string &text) const {
  std::vector<ScannerHit> hits;
  std::istringstream is(text);
  std::string line;
  long long ln = 0;
  while (std::getline(is, line)) {
    ++ln;
    size_t pos = 0;
    while ((pos = line.find("tr(\"", pos)) != std::string::npos) {
      size_t end = line.find("\")", pos + 4);
      if (end == std::string::npos) break;
      std::string arg = line.substr(pos + 4, end - (pos + 4));
      ScannerHit h;
      h.path = path;
      h.line = ln;
      h.snippet = "tr(\"" + arg + "\")";
      if (LooksLikeId(arg)) {
        if (catalog_ && !catalog_->Has(arg, locale_))
          h.reason = "missing-id";
      } else {
        h.reason = "hardcoded-literal";
      }
      if (!h.reason.empty()) hits.push_back(std::move(h));
      pos = end + 2;
    }
  }
  return hits;
}

}  // namespace polyglot::tools::ui::i18n
