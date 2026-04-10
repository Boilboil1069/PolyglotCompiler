/**
 * @file     type_mapping.h
 * @brief    Cross-language interoperability
 *
 * @ingroup  Runtime / Interop
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>
#include <unordered_map>

#include "runtime/include/interop/marshalling.h"

namespace polyglot::runtime::interop {

/** @brief ABIType data structure. */
struct ABIType {
  std::string name;
  size_t size{0};
  size_t alignment{1};
  bool is_float{false};
  bool is_pointer{false};
};

/** @brief TypeMapping data structure. */
struct TypeMapping {
  std::string source_lang;
  std::string source_type;
  ABIType abi;
};

// Returns an ABIType for a builtin source type; falls back to {"unknown",0,1} if unmapped.
ABIType MapBuiltinType(const std::string &lang, const std::string &type_name);

// Convenience cache for mapping lookups; thread-safe reads after initialization.
const std::unordered_map<std::string, ABIType> &BuiltinTypeTable();

}  // namespace polyglot::runtime::interop
