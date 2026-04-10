/**
 * @file     reflection.h
 * @brief    Runtime service infrastructure
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace polyglot::runtime::services {

/** @brief ReflectionInfo data structure. */
struct ReflectionInfo {
  std::string type_name;
  std::size_t size{0};
};

/** @brief ReflectionRegistry class. */
class ReflectionRegistry {
 public:
  static ReflectionRegistry &Instance();

  template <typename T>
  ReflectionInfo Register(const std::string &alias = "") {
    const std::string name = alias.empty() ? typeid(T).name() : alias;
    return Register(name, sizeof(T));
  }

  ReflectionInfo Register(const std::string &type_name, std::size_t size);
  const ReflectionInfo *Get(const std::string &type_name) const;
  std::vector<ReflectionInfo> List() const;

 private:
  ReflectionRegistry() = default;
  ReflectionRegistry(const ReflectionRegistry &) = delete;
  ReflectionRegistry &operator=(const ReflectionRegistry &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ReflectionInfo> types_;
};

template <typename T>
ReflectionInfo RegisterType(const std::string &alias = "") {
  return ReflectionRegistry::Instance().Register<T>(alias);
}

inline ReflectionInfo RegisterType(const std::string &type_name, std::size_t size) {
  return ReflectionRegistry::Instance().Register(type_name, size);
}

inline const ReflectionInfo *GetReflection(const std::string &type_name) {
  return ReflectionRegistry::Instance().Get(type_name);
}

inline std::vector<ReflectionInfo> ListReflections() {
  return ReflectionRegistry::Instance().List();
}

}  // namespace polyglot::runtime::services
