/**
 * @file     reflection.cpp
 * @brief    Runtime implementation
 *
 * @ingroup  Runtime
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "runtime/include/services/reflection.h"

namespace polyglot::runtime::services {

ReflectionRegistry &ReflectionRegistry::Instance() {
  static ReflectionRegistry instance;
  return instance;
}

ReflectionInfo ReflectionRegistry::Register(const std::string &type_name, std::size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto [it, inserted] = types_.emplace(type_name, ReflectionInfo{type_name, size});
  if (!inserted) {
    it->second.size = size;
  }
  return it->second;
}

const ReflectionInfo *ReflectionRegistry::Get(const std::string &type_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = types_.find(type_name);
  if (it == types_.end()) return nullptr;
  return &it->second;
}

std::vector<ReflectionInfo> ReflectionRegistry::List() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ReflectionInfo> result;
  result.reserve(types_.size());
  for (const auto &kv : types_) {
    result.push_back(kv.second);
  }
  return result;
}

}  // namespace polyglot::runtime::services
