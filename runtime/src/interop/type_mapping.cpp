#include "runtime/include/interop/type_mapping.h"

#include <unordered_map>

namespace polyglot::runtime::interop {

namespace {
const std::unordered_map<std::string, ABIType> kBuiltin = {
    {"c:int", {"i32", 4, 4, false, false}},
    {"c:long", {"i64", 8, 8, false, false}},
    {"c:float", {"f32", 4, 4, true, false}},
    {"c:double", {"f64", 8, 8, true, false}},
    {"c:void*", {"ptr", sizeof(void *), alignof(void *), false, true}},
    {"python:int", {"py_object", sizeof(void *), alignof(void *), false, true}},
    {"python:str", {"py_object", sizeof(void *), alignof(void *), false, true}},
};
}  // namespace

const std::unordered_map<std::string, ABIType> &BuiltinTypeTable() { return kBuiltin; }

ABIType MapBuiltinType(const std::string &lang, const std::string &type_name) {
  auto it = kBuiltin.find(lang + ":" + type_name);
  if (it != kBuiltin.end()) return it->second;
  return ABIType{"unknown", 0, 1, false, false};
}

}  // namespace polyglot::runtime::interop
