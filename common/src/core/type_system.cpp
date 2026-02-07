/**
 * @file     type_system.cpp
 * @brief    Implementation of the core type system for the polyglot compiler.
 * @details  Provides the full implementation of Type member functions, TypeSystem
 *           (primitive maps, compatibility, conversions, size/alignment, aliases),
 *           and TypeRegistry (named type storage, cross-language equivalence).
 *
 * @author   Manning Cyrus
 * @date     2026-02-07
 * @version  2.0.0
 */

#include "common/include/core/types.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace polyglot::core {

// ============================================================================
// Type member function implementations
// ============================================================================

Type Type::GetElementType() const {
  // For pointer, reference, array, optional, slice — the inner type is the
  // first element of type_args.
  switch (kind) {
    case TypeKind::kPointer:
    case TypeKind::kReference:
    case TypeKind::kArray:
    case TypeKind::kOptional:
    case TypeKind::kSlice:
      if (!type_args.empty()) return type_args[0];
      break;
    default:
      break;
  }
  return Type::Invalid();
}

Type Type::GetReturnType() const {
  // Function types store [return_type, param0, param1, ...] in type_args.
  if (kind == TypeKind::kFunction && !type_args.empty()) {
    return type_args[0];
  }
  return Type::Invalid();
}

std::vector<Type> Type::GetParamTypes() const {
  if (kind == TypeKind::kFunction && type_args.size() > 1) {
    return std::vector<Type>(type_args.begin() + 1, type_args.end());
  }
  return {};
}

size_t Type::GetParamCount() const {
  if (kind == TypeKind::kFunction && !type_args.empty()) {
    return type_args.size() - 1;
  }
  return 0;
}

Type Type::WithConst(bool c) const {
  Type copy = *this;
  copy.is_const = c;
  return copy;
}

Type Type::WithVolatile(bool v) const {
  Type copy = *this;
  copy.is_volatile = v;
  return copy;
}

Type Type::StripQualifiers() const {
  Type copy = *this;
  copy.is_const = false;
  copy.is_volatile = false;
  copy.is_rvalue_ref = false;
  return copy;
}

std::string Type::ToString() const {
  std::ostringstream os;

  // CV qualifiers prefix
  if (is_const) os << "const ";
  if (is_volatile) os << "volatile ";

  switch (kind) {
    case TypeKind::kInvalid:
      os << "<invalid>";
      break;

    case TypeKind::kVoid:
      os << "void";
      break;

    case TypeKind::kBool:
      os << "bool";
      break;

    case TypeKind::kInt:
      if (bit_width > 0) {
        os << (is_signed ? "i" : "u") << bit_width;
      } else {
        os << "int";
      }
      break;

    case TypeKind::kFloat:
      if (bit_width > 0) {
        os << "f" << bit_width;
      } else {
        os << "float";
      }
      break;

    case TypeKind::kString:
      os << "string";
      break;

    case TypeKind::kPointer:
      if (!type_args.empty()) {
        os << type_args[0].ToString() << "*";
      } else {
        os << "void*";
      }
      break;

    case TypeKind::kReference:
      if (!type_args.empty()) {
        os << type_args[0].ToString();
      }
      os << (is_rvalue_ref ? "&&" : "&");
      break;

    case TypeKind::kFunction: {
      os << "fn(";
      // Parameters are type_args[1..N]
      for (size_t i = 1; i < type_args.size(); ++i) {
        if (i > 1) os << ", ";
        os << type_args[i].ToString();
      }
      os << ") -> ";
      if (!type_args.empty()) {
        os << type_args[0].ToString();
      } else {
        os << "void";
      }
      break;
    }

    case TypeKind::kClass:
    case TypeKind::kStruct:
    case TypeKind::kUnion:
    case TypeKind::kEnum:
    case TypeKind::kModule:
      if (!language.empty()) {
        os << language << "::";
      }
      os << name;
      break;

    case TypeKind::kAny:
      os << "any";
      break;

    case TypeKind::kTuple: {
      os << "(";
      for (size_t i = 0; i < type_args.size(); ++i) {
        if (i > 0) os << ", ";
        os << type_args[i].ToString();
      }
      os << ")";
      break;
    }

    case TypeKind::kGenericParam:
      os << name;
      break;

    case TypeKind::kGenericInstance: {
      if (!language.empty()) {
        os << language << "::";
      }
      os << name << "<";
      for (size_t i = 0; i < type_args.size(); ++i) {
        if (i > 0) os << ", ";
        os << type_args[i].ToString();
      }
      os << ">";
      break;
    }

    case TypeKind::kArray:
      if (!type_args.empty()) {
        os << "[" << type_args[0].ToString();
        if (array_size > 0) {
          os << "; " << array_size;
        }
        os << "]";
      } else {
        os << "[]";
      }
      break;

    case TypeKind::kOptional:
      if (!type_args.empty()) {
        os << type_args[0].ToString() << "?";
      } else {
        os << "optional";
      }
      break;

    case TypeKind::kSlice:
      if (!type_args.empty()) {
        os << "&[" << type_args[0].ToString() << "]";
      } else {
        os << "&[]";
      }
      break;
  }

  if (!lifetime.empty()) {
    os << " '" << lifetime;
  }

  return os.str();
}

// ============================================================================
// TypeSystem implementation
// ============================================================================

/// Helper: lowercase a string for case-insensitive lookup.
static std::string ToLower(const std::string &s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

TypeSystem::TypeSystem() {
  // -----------------------------------------------------------------------
  // Python primitive map
  // -----------------------------------------------------------------------
  auto &py = primitive_maps_["python"];
  py["int"]       = Type::Int(64, true);   // Python int is arbitrary precision; map to i64
  py["float"]     = Type::Float(64);       // Python float is always double precision
  py["bool"]      = Type::Bool();
  py["str"]       = Type::String();
  py["none"]      = Type::Void();
  py["nonetype"]  = Type::Void();
  py["bytes"]     = Type::Array(Type::Int(8, false));
  py["bytearray"] = Type::Array(Type::Int(8, false));
  py["complex"]   = Type::Struct("complex", "python");
  py["object"]    = Type::Any();

  // -----------------------------------------------------------------------
  // C++ primitive map
  // -----------------------------------------------------------------------
  auto &cpp = primitive_maps_["cpp"];
  // Void
  cpp["void"]     = Type::Void();
  // Boolean
  cpp["bool"]     = Type::Bool();
  // Character types
  cpp["char"]     = Type::Int(8, true);
  cpp["wchar_t"]  = Type::Int(32, true);
  cpp["char8_t"]  = Type::Int(8, false);
  cpp["char16_t"] = Type::Int(16, false);
  cpp["char32_t"] = Type::Int(32, false);
  // Signed integer types
  cpp["short"]               = Type::Int(16, true);
  cpp["int"]                 = Type::Int(32, true);
  cpp["long"]                = Type::Int(64, true);
  cpp["long long"]           = Type::Int(64, true);
  cpp["signed"]              = Type::Int(32, true);
  cpp["signed char"]         = Type::Int(8, true);
  cpp["signed short"]        = Type::Int(16, true);
  cpp["signed int"]          = Type::Int(32, true);
  cpp["signed long"]         = Type::Int(64, true);
  cpp["signed long long"]    = Type::Int(64, true);
  // Unsigned integer types
  cpp["unsigned"]            = Type::Int(32, false);
  cpp["unsigned char"]       = Type::Int(8, false);
  cpp["unsigned short"]      = Type::Int(16, false);
  cpp["unsigned int"]        = Type::Int(32, false);
  cpp["unsigned long"]       = Type::Int(64, false);
  cpp["unsigned long long"]  = Type::Int(64, false);
  // Floating point
  cpp["float"]    = Type::Float(32);
  cpp["double"]   = Type::Float(64);
  cpp["long double"] = Type::Float(128);
  // Size types
  cpp["size_t"]   = Type::Int(64, false);
  cpp["ssize_t"]  = Type::Int(64, true);
  cpp["ptrdiff_t"]= Type::Int(64, true);
  cpp["intptr_t"] = Type::Int(64, true);
  cpp["uintptr_t"]= Type::Int(64, false);
  // Fixed-width integer types
  cpp["int8_t"]   = Type::Int(8, true);
  cpp["int16_t"]  = Type::Int(16, true);
  cpp["int32_t"]  = Type::Int(32, true);
  cpp["int64_t"]  = Type::Int(64, true);
  cpp["uint8_t"]  = Type::Int(8, false);
  cpp["uint16_t"] = Type::Int(16, false);
  cpp["uint32_t"] = Type::Int(32, false);
  cpp["uint64_t"] = Type::Int(64, false);
  // Auto (treated as Any for type deduction)
  cpp["auto"]     = Type::Any();
  // String types
  cpp["std::string"] = Type::String();
  cpp["string"]      = Type::String();

  // -----------------------------------------------------------------------
  // Rust primitive map
  // -----------------------------------------------------------------------
  auto &rs = primitive_maps_["rust"];
  // Boolean
  rs["bool"]   = Type::Bool();
  // Character
  rs["char"]   = Type::Int(32, false);  // Rust char is 4-byte Unicode scalar
  // Signed integers
  rs["i8"]     = Type::Int(8, true);
  rs["i16"]    = Type::Int(16, true);
  rs["i32"]    = Type::Int(32, true);
  rs["i64"]    = Type::Int(64, true);
  rs["i128"]   = Type::Int(128, true);
  rs["isize"]  = Type::Int(64, true);   // Assume 64-bit platform
  // Unsigned integers
  rs["u8"]     = Type::Int(8, false);
  rs["u16"]    = Type::Int(16, false);
  rs["u32"]    = Type::Int(32, false);
  rs["u64"]    = Type::Int(64, false);
  rs["u128"]   = Type::Int(128, false);
  rs["usize"]  = Type::Int(64, false);  // Assume 64-bit platform
  // Floating point
  rs["f32"]    = Type::Float(32);
  rs["f64"]    = Type::Float(64);
  // String types
  rs["str"]    = Type::String();
  rs["String"] = Type::String();
  rs["&str"]   = Type::String();
  // Unit type
  rs["()"]     = Type::Void();

  // -----------------------------------------------------------------------
  // Default type aliases common across languages
  // -----------------------------------------------------------------------
  aliases_["size_t"]    = Type::Int(64, false);
  aliases_["ssize_t"]   = Type::Int(64, true);
  aliases_["ptrdiff_t"] = Type::Int(64, true);
  aliases_["intptr_t"]  = Type::Int(64, true);
  aliases_["uintptr_t"] = Type::Int(64, false);
}

Type TypeSystem::MapFromLanguage(const std::string &lang, const std::string &name) const {
  std::string lower_lang = ToLower(lang);
  std::string lower_name = ToLower(name);

  // Check for container / generic type patterns before primitive lookup.
  // Python: List[int], Dict[str, int], Set[str], Tuple[int, str], Optional[int]
  // C++: std::vector<int>, std::map<K,V>, etc.
  // Rust: Vec<T>, HashMap<K,V>, Option<T>, Result<T,E>

  // Detect Python container annotations: "list[T]", "dict[K,V]", etc.
  if (lower_lang == "python" || lower_lang == "py") {
    auto bracket = name.find('[');
    if (bracket != std::string::npos && name.back() == ']') {
      std::string outer = ToLower(name.substr(0, bracket));
      std::string inner = name.substr(bracket + 1, name.size() - bracket - 2);
      if (outer == "list") {
        Type elem = MapFromLanguage(lang, inner);
        return Type::GenericInstance("list", {elem}, "python");
      }
      if (outer == "set") {
        Type elem = MapFromLanguage(lang, inner);
        return Type::GenericInstance("set", {elem}, "python");
      }
      if (outer == "optional") {
        Type elem = MapFromLanguage(lang, inner);
        return Type::Optional(elem);
      }
      if (outer == "dict") {
        // Split on first comma (simple heuristic; does not handle nested commas).
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
          std::string key_str = inner.substr(0, comma);
          std::string val_str = inner.substr(comma + 1);
          while (!val_str.empty() && val_str[0] == ' ') val_str.erase(val_str.begin());
          Type key = MapFromLanguage(lang, key_str);
          Type val = MapFromLanguage(lang, val_str);
          return Type::GenericInstance("dict", {key, val}, "python");
        }
      }
      if (outer == "tuple") {
        std::vector<Type> elems;
        std::string remaining = inner;
        while (!remaining.empty()) {
          auto c = remaining.find(',');
          std::string part = (c != std::string::npos) ? remaining.substr(0, c) : remaining;
          while (!part.empty() && part[0] == ' ') part.erase(part.begin());
          while (!part.empty() && part.back() == ' ') part.pop_back();
          elems.push_back(MapFromLanguage(lang, part));
          if (c == std::string::npos) break;
          remaining = remaining.substr(c + 1);
        }
        return Type::Tuple(elems);
      }
    }
  }

  // Detect C++ template types: "std::vector<T>", etc.
  if (lower_lang == "cpp" || lower_lang == "c++") {
    auto angle = name.find('<');
    if (angle != std::string::npos && name.back() == '>') {
      std::string outer = name.substr(0, angle);
      std::string inner = name.substr(angle + 1, name.size() - angle - 2);
      // Split inner by comma respecting nested angle brackets.
      std::vector<Type> args;
      int depth = 0;
      std::string current;
      for (char c : inner) {
        if (c == '<') { depth++; current += c; }
        else if (c == '>') { depth--; current += c; }
        else if (c == ',' && depth == 0) {
          while (!current.empty() && current[0] == ' ') current.erase(current.begin());
          while (!current.empty() && current.back() == ' ') current.pop_back();
          args.push_back(MapFromLanguage(lang, current));
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        while (!current.empty() && current[0] == ' ') current.erase(current.begin());
        while (!current.empty() && current.back() == ' ') current.pop_back();
        args.push_back(MapFromLanguage(lang, current));
      }
      return Type::GenericInstance(outer, args, "cpp");
    }
  }

  // Detect Rust generic types: "Vec<T>", "Option<T>", etc.
  if (lower_lang == "rust" || lower_lang == "rs") {
    auto angle = name.find('<');
    if (angle != std::string::npos && name.back() == '>') {
      std::string outer = name.substr(0, angle);
      std::string inner = name.substr(angle + 1, name.size() - angle - 2);
      // Option<T> maps to Optional
      if (outer == "Option") {
        Type elem = MapFromLanguage(lang, inner);
        return Type::Optional(elem);
      }
      // Parse as generic instance
      std::vector<Type> args;
      int depth = 0;
      std::string current;
      for (char c : inner) {
        if (c == '<') { depth++; current += c; }
        else if (c == '>') { depth--; current += c; }
        else if (c == ',' && depth == 0) {
          while (!current.empty() && current[0] == ' ') current.erase(current.begin());
          while (!current.empty() && current.back() == ' ') current.pop_back();
          args.push_back(MapFromLanguage(lang, current));
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        while (!current.empty() && current[0] == ' ') current.erase(current.begin());
        while (!current.empty() && current.back() == ' ') current.pop_back();
        args.push_back(MapFromLanguage(lang, current));
      }
      return Type::GenericInstance(outer, args, "rust");
    }
  }

  // Normalize the language key for primitive map lookup.
  std::string map_key = lower_lang;
  if (map_key == "c++" || map_key == "cxx") map_key = "cpp";
  if (map_key == "rs") map_key = "rust";
  if (map_key == "py") map_key = "python";

  auto lang_it = primitive_maps_.find(map_key);
  if (lang_it != primitive_maps_.end()) {
    // First try exact case-insensitive match
    auto type_it = lang_it->second.find(lower_name);
    if (type_it != lang_it->second.end()) {
      return type_it->second;
    }
    // For Rust, also try the original case (e.g. "String" vs "string")
    if (map_key == "rust") {
      type_it = lang_it->second.find(name);
      if (type_it != lang_it->second.end()) {
        return type_it->second;
      }
    }
  }

  // Check aliases
  auto alias_it = aliases_.find(name);
  if (alias_it != aliases_.end()) {
    return alias_it->second;
  }
  alias_it = aliases_.find(lower_name);
  if (alias_it != aliases_.end()) {
    return alias_it->second;
  }

  // Fall back to a user-defined class/struct type.
  return Type::Struct(name, map_key);
}

bool TypeSystem::CanImplicitlyConvert(const Type &from, const Type &to) const {
  // Identical types always convert.
  if (from == to) return true;

  // Any type is universally compatible.
  if (from.kind == TypeKind::kAny || to.kind == TypeKind::kAny) return true;

  // Numeric widening: int -> int (wider), int -> float, float -> float (wider).
  if (from.IsNumeric() && to.IsNumeric()) {
    // int -> float is always allowed
    if (from.IsInteger() && to.IsFloatingPoint()) return true;
    // Same kind: allow widening (bit_width increases)
    if (from.kind == to.kind) {
      int fw = from.bit_width > 0 ? from.bit_width : 32;
      int tw = to.bit_width > 0 ? to.bit_width : 32;
      if (tw >= fw) {
        return true;
      }
    }
  }

  // bool -> int/float
  if (from.IsBool() && to.IsNumeric()) return true;

  // Add const qualification: T -> const T
  if (from.StripQualifiers() == to.StripQualifiers()) {
    if (!from.is_const && to.is_const) return true;
  }

  // Pointer to void*: T* -> void*
  if (from.IsPointer() && to.IsPointer()) {
    if (!to.type_args.empty() && to.type_args[0].IsVoid()) return true;
    // Also allow adding const to pointed-to type
    if (!from.type_args.empty() && !to.type_args.empty()) {
      Type from_elem = from.type_args[0].StripQualifiers();
      Type to_elem = to.type_args[0].StripQualifiers();
      if (from_elem == to_elem && !from.type_args[0].is_const && to.type_args[0].is_const) {
        return true;
      }
    }
  }

  // Reference bindings: T -> T& (lvalue ref), T -> const T& (any)
  if (to.IsReference() && !to.type_args.empty()) {
    Type ref_inner = to.type_args[0];
    if (from.StripQualifiers() == ref_inner.StripQualifiers()) {
      // Binding to const ref is always ok
      if (to.is_const) return true;
      // Binding to non-const lvalue ref requires non-rvalue source
      if (!to.is_rvalue_ref) return true;
    }
  }

  // T -> Optional<T>
  if (to.IsOptional() && !to.type_args.empty()) {
    if (CanImplicitlyConvert(from, to.type_args[0])) return true;
  }

  // Array -> Slice: [T; N] -> &[T]
  if (from.IsArray() && to.IsSlice()) {
    if (!from.type_args.empty() && !to.type_args.empty()) {
      if (IsCompatible(from.type_args[0], to.type_args[0])) return true;
    }
  }

  return false;
}

bool TypeSystem::IsCompatible(const Type &lhs, const Type &rhs) const {
  // Exact match.
  if (lhs == rhs) return true;

  // Any is compatible with everything.
  if (lhs.kind == TypeKind::kAny || rhs.kind == TypeKind::kAny) return true;

  // Both numeric types are broadly compatible (for binary ops, etc.)
  if (lhs.IsNumeric() && rhs.IsNumeric()) return true;

  // Bool is compatible with numeric (C-style)
  if ((lhs.IsBool() && rhs.IsNumeric()) || (lhs.IsNumeric() && rhs.IsBool())) return true;

  // Same kind, ignore qualifiers
  if (lhs.kind == rhs.kind) {
    Type a = lhs.StripQualifiers();
    Type b = rhs.StripQualifiers();
    if (a == b) return true;

    // Structural compatibility for compound types
    switch (lhs.kind) {
      case TypeKind::kTuple:
        if (a.type_args.size() == b.type_args.size()) {
          bool all_compat = true;
          for (size_t i = 0; i < a.type_args.size(); ++i) {
            if (!IsCompatible(a.type_args[i], b.type_args[i])) {
              all_compat = false;
              break;
            }
          }
          if (all_compat) return true;
        }
        break;

      case TypeKind::kGenericInstance:
        if (a.name == b.name && a.type_args.size() == b.type_args.size()) {
          bool all_compat = true;
          for (size_t i = 0; i < a.type_args.size(); ++i) {
            if (!IsCompatible(a.type_args[i], b.type_args[i])) {
              all_compat = false;
              break;
            }
          }
          if (all_compat) return true;
        }
        break;

      case TypeKind::kArray:
        if (!a.type_args.empty() && !b.type_args.empty()) {
          if (IsCompatible(a.type_args[0], b.type_args[0])) {
            // If both have fixed sizes, they must match; otherwise compatible.
            if (a.array_size == 0 || b.array_size == 0 || a.array_size == b.array_size) {
              return true;
            }
          }
        }
        break;

      case TypeKind::kOptional:
        if (!a.type_args.empty() && !b.type_args.empty()) {
          return IsCompatible(a.type_args[0], b.type_args[0]);
        }
        break;

      case TypeKind::kSlice:
        if (!a.type_args.empty() && !b.type_args.empty()) {
          return IsCompatible(a.type_args[0], b.type_args[0]);
        }
        break;

      case TypeKind::kPointer:
      case TypeKind::kReference:
        if (!a.type_args.empty() && !b.type_args.empty()) {
          return IsCompatible(a.type_args[0], b.type_args[0]);
        }
        break;

      case TypeKind::kFunction:
        // Functions are compatible if return types and all param types match.
        if (a.type_args.size() == b.type_args.size()) {
          bool all_compat = true;
          for (size_t i = 0; i < a.type_args.size(); ++i) {
            if (!IsCompatible(a.type_args[i], b.type_args[i])) {
              all_compat = false;
              break;
            }
          }
          if (all_compat) return true;
        }
        break;

      default:
        break;
    }
  }

  return false;
}

size_t TypeSystem::SizeOf(const Type &t) const {
  switch (t.kind) {
    case TypeKind::kVoid:
      return 0;

    case TypeKind::kBool:
      return 1;

    case TypeKind::kInt:
      if (t.bit_width > 0) return static_cast<size_t>(t.bit_width) / 8;
      return 4;  // Default int is 32-bit

    case TypeKind::kFloat:
      if (t.bit_width > 0) return static_cast<size_t>(t.bit_width) / 8;
      return 4;  // Default float is 32-bit

    case TypeKind::kString:
      // String is typically a fat pointer (ptr + length) on 64-bit: 16 bytes.
      return 16;

    case TypeKind::kPointer:
    case TypeKind::kReference:
      return 8;  // 64-bit pointer

    case TypeKind::kFunction:
      return 8;  // Function pointer

    case TypeKind::kArray:
      if (!t.type_args.empty() && t.array_size > 0) {
        return SizeOf(t.type_args[0]) * t.array_size;
      }
      // Dynamic array: pointer + length
      return 16;

    case TypeKind::kOptional:
      // Optional<T> = T + 1 byte discriminant, aligned to T's alignment
      if (!t.type_args.empty()) {
        size_t inner = SizeOf(t.type_args[0]);
        size_t align = AlignOf(t.type_args[0]);
        // Round up (inner + 1) to alignment boundary
        size_t total = inner + 1;
        return (total + align - 1) & ~(align - 1);
      }
      return 8;

    case TypeKind::kSlice:
      // Slice: pointer + length (fat pointer)
      return 16;

    case TypeKind::kTuple: {
      // Tuple: sum of element sizes with padding for alignment
      size_t total = 0;
      size_t max_align = 1;
      for (const auto &elem : t.type_args) {
        size_t align = AlignOf(elem);
        max_align = std::max(max_align, align);
        // Align current offset
        total = (total + align - 1) & ~(align - 1);
        total += SizeOf(elem);
      }
      // Pad to overall alignment
      if (max_align > 1) {
        total = (total + max_align - 1) & ~(max_align - 1);
      }
      return total;
    }

    case TypeKind::kStruct:
    case TypeKind::kClass:
    case TypeKind::kUnion:
    case TypeKind::kEnum:
      // Opaque types: return a sensible default.
      // Actual sizes depend on struct field layout computed elsewhere.
      return 0;

    case TypeKind::kAny:
      // Boxed any: pointer + type tag
      return 16;

    case TypeKind::kGenericParam:
    case TypeKind::kGenericInstance:
    case TypeKind::kModule:
    case TypeKind::kInvalid:
      return 0;
  }
  return 0;
}

size_t TypeSystem::AlignOf(const Type &t) const {
  switch (t.kind) {
    case TypeKind::kVoid:
      return 1;

    case TypeKind::kBool:
      return 1;

    case TypeKind::kInt:
      if (t.bit_width > 0) {
        size_t bytes = static_cast<size_t>(t.bit_width) / 8;
        return std::min(bytes, static_cast<size_t>(8));  // Cap at 8-byte alignment
      }
      return 4;

    case TypeKind::kFloat:
      if (t.bit_width > 0) {
        size_t bytes = static_cast<size_t>(t.bit_width) / 8;
        return std::min(bytes, static_cast<size_t>(16));
      }
      return 4;

    case TypeKind::kString:
      return 8;  // Pointer alignment

    case TypeKind::kPointer:
    case TypeKind::kReference:
    case TypeKind::kFunction:
    case TypeKind::kSlice:
      return 8;

    case TypeKind::kArray:
      if (!t.type_args.empty()) {
        return AlignOf(t.type_args[0]);
      }
      return 8;

    case TypeKind::kOptional:
      if (!t.type_args.empty()) {
        return AlignOf(t.type_args[0]);
      }
      return 8;

    case TypeKind::kTuple: {
      size_t max_align = 1;
      for (const auto &elem : t.type_args) {
        max_align = std::max(max_align, AlignOf(elem));
      }
      return max_align;
    }

    case TypeKind::kAny:
      return 8;

    default:
      return 1;
  }
}

Type TypeSystem::CommonType(const Type &a, const Type &b) const {
  // If types are identical, return either.
  if (a == b) return a;

  // Any absorbs all types.
  if (a.kind == TypeKind::kAny) return b;
  if (b.kind == TypeKind::kAny) return a;

  // Bool promotes to the other numeric type.
  if (a.IsBool() && b.IsNumeric()) return b;
  if (b.IsBool() && a.IsNumeric()) return a;

  // Numeric promotion: int < float, narrower < wider.
  if (a.IsNumeric() && b.IsNumeric()) {
    // If one is float and the other is int, promote to float.
    if (a.IsFloatingPoint() && b.IsInteger()) return a;
    if (b.IsFloatingPoint() && a.IsInteger()) return b;

    // Both same numeric kind: pick the wider one.
    int aw = a.bit_width > 0 ? a.bit_width : 32;
    int bw = b.bit_width > 0 ? b.bit_width : 32;
    if (aw >= bw) return a;
    return b;
  }

  // Optional promotion: T and Optional<T> -> Optional<T>
  if (a.IsOptional() && !b.IsOptional()) {
    if (!a.type_args.empty() && IsCompatible(a.type_args[0], b)) {
      return a;
    }
  }
  if (b.IsOptional() && !a.IsOptional()) {
    if (!b.type_args.empty() && IsCompatible(b.type_args[0], a)) {
      return b;
    }
  }

  // Pointer types: T* and void* -> void*
  if (a.IsPointer() && b.IsPointer()) {
    if (!a.type_args.empty() && a.type_args[0].IsVoid()) return a;
    if (!b.type_args.empty() && b.type_args[0].IsVoid()) return b;
  }

  // Cannot determine common type; return Any as a fallback.
  return Type::Any();
}

bool TypeSystem::AreLayoutCompatible(const Type &a, const Type &b) const {
  // Two types are layout-compatible if they have the same size and alignment.
  if (a == b) return true;

  // Numeric types with same bit width are layout-compatible.
  if (a.IsNumeric() && b.IsNumeric()) {
    int aw = a.bit_width > 0 ? a.bit_width : 32;
    int bw = b.bit_width > 0 ? b.bit_width : 32;
    return aw == bw;
  }

  // Pointer-sized types are all layout-compatible.
  if ((a.IsPointer() || a.IsReference()) && (b.IsPointer() || b.IsReference())) {
    return true;
  }

  // Arrays of same element type and size are layout-compatible.
  if (a.IsArray() && b.IsArray()) {
    if (!a.type_args.empty() && !b.type_args.empty()) {
      if (AreLayoutCompatible(a.type_args[0], b.type_args[0])) {
        return a.array_size == b.array_size;
      }
    }
  }

  // Tuples with same layout are compatible.
  if (a.kind == TypeKind::kTuple && b.kind == TypeKind::kTuple) {
    if (a.type_args.size() != b.type_args.size()) return false;
    for (size_t i = 0; i < a.type_args.size(); ++i) {
      if (!AreLayoutCompatible(a.type_args[i], b.type_args[i])) return false;
    }
    return true;
  }

  // Fall back to size and alignment comparison for concrete types.
  size_t sa = SizeOf(a), sb = SizeOf(b);
  size_t aa_align = AlignOf(a), ab_align = AlignOf(b);
  return sa > 0 && sb > 0 && sa == sb && aa_align == ab_align;
}

Type TypeSystem::Normalize(const Type &t) const {
  // Strip qualifiers and resolve aliases.
  Type result = t.StripQualifiers();

  // Resolve alias if the name matches.
  auto alias_it = aliases_.find(result.name);
  if (alias_it != aliases_.end()) {
    // Preserve original language tag if the alias doesn't have one.
    Type resolved = alias_it->second;
    if (resolved.language.empty() && !result.language.empty()) {
      resolved.language = result.language;
    }
    result = resolved;
  }

  // Recursively normalize type arguments.
  for (auto &arg : result.type_args) {
    arg = Normalize(arg);
  }

  return result;
}

bool TypeSystem::IsWidening(const Type &from, const Type &to) {
  if (!from.IsNumeric() || !to.IsNumeric()) return false;

  // int -> float is considered widening (loss of precision is accepted).
  if (from.IsInteger() && to.IsFloatingPoint()) return true;

  // Same kind: wider bit width is widening.
  if (from.kind == to.kind) {
    int fw = from.bit_width > 0 ? from.bit_width : 32;
    int tw = to.bit_width > 0 ? to.bit_width : 32;
    return tw > fw;
  }

  return false;
}

bool TypeSystem::IsNarrowing(const Type &from, const Type &to) {
  if (!from.IsNumeric() || !to.IsNumeric()) return false;

  // float -> int is always narrowing.
  if (from.IsFloatingPoint() && to.IsInteger()) return true;

  // Same kind: narrower bit width is narrowing.
  if (from.kind == to.kind) {
    int fw = from.bit_width > 0 ? from.bit_width : 32;
    int tw = to.bit_width > 0 ? to.bit_width : 32;
    return tw < fw;
  }

  return false;
}

int TypeSystem::ConversionRank(const Type &t) const {
  // Numeric rank for overload resolution scoring.
  // Lower = narrower type.
  switch (t.kind) {
    case TypeKind::kBool:
      return 0;
    case TypeKind::kInt: {
      int w = t.bit_width > 0 ? t.bit_width : 32;
      // i8=1, i16=2, i32=3, i64=4, i128=5
      if (w <= 8) return 1;
      if (w <= 16) return 2;
      if (w <= 32) return 3;
      if (w <= 64) return 4;
      return 5;
    }
    case TypeKind::kFloat: {
      int w = t.bit_width > 0 ? t.bit_width : 32;
      // f32=6, f64=7, f128=8
      if (w <= 32) return 6;
      if (w <= 64) return 7;
      return 8;
    }
    case TypeKind::kString:
      return 10;
    case TypeKind::kPointer:
    case TypeKind::kReference:
      return 11;
    default:
      return 100;
  }
}

void TypeSystem::RegisterAlias(const std::string &alias, Type target) {
  aliases_[alias] = std::move(target);
}

Type TypeSystem::ResolveAlias(const std::string &name) const {
  auto it = aliases_.find(name);
  if (it != aliases_.end()) return it->second;
  return Type::Invalid();
}

bool TypeSystem::HasAlias(const std::string &name) const {
  return aliases_.find(name) != aliases_.end();
}

std::string TypeSystem::KindToString(TypeKind kind) {
  switch (kind) {
    case TypeKind::kInvalid:         return "Invalid";
    case TypeKind::kVoid:            return "Void";
    case TypeKind::kBool:            return "Bool";
    case TypeKind::kInt:             return "Int";
    case TypeKind::kFloat:           return "Float";
    case TypeKind::kString:          return "String";
    case TypeKind::kPointer:         return "Pointer";
    case TypeKind::kFunction:        return "Function";
    case TypeKind::kReference:       return "Reference";
    case TypeKind::kClass:           return "Class";
    case TypeKind::kModule:          return "Module";
    case TypeKind::kAny:             return "Any";
    case TypeKind::kStruct:          return "Struct";
    case TypeKind::kUnion:           return "Union";
    case TypeKind::kEnum:            return "Enum";
    case TypeKind::kTuple:           return "Tuple";
    case TypeKind::kGenericParam:    return "GenericParam";
    case TypeKind::kGenericInstance: return "GenericInstance";
    case TypeKind::kArray:           return "Array";
    case TypeKind::kOptional:        return "Optional";
    case TypeKind::kSlice:           return "Slice";
  }
  return "Unknown";
}

// ============================================================================
// TypeRegistry implementation
// ============================================================================

void TypeRegistry::Register(const std::string &name, Type type) {
  types_[name] = std::move(type);
}

const Type *TypeRegistry::Find(const std::string &name) const {
  auto it = types_.find(name);
  if (it != types_.end()) return &it->second;
  return nullptr;
}

bool TypeRegistry::Contains(const std::string &name) const {
  return types_.find(name) != types_.end();
}

size_t TypeRegistry::Size() const {
  return types_.size();
}

void TypeRegistry::Clear() {
  types_.clear();
  equivalences_.clear();
}

void TypeRegistry::RegisterEquivalence(const std::string &lang_a, const std::string &type_a,
                                       const std::string &lang_b, const std::string &type_b) {
  std::string key_a = lang_a + ":" + type_a;
  std::string key_b = lang_b + ":" + type_b;

  // Build transitive closure: merge the equivalence sets of both keys.
  // Collect all existing members from both sets.
  std::unordered_set<std::string> merged;
  merged.insert(key_a);
  merged.insert(key_b);

  auto collect = [&](const std::string &key) {
    auto it = equivalences_.find(key);
    if (it != equivalences_.end()) {
      for (const auto &eq : it->second) {
        merged.insert(eq);
      }
    }
  };
  collect(key_a);
  collect(key_b);

  // Update each member's equivalence list to contain all other members.
  for (const auto &member : merged) {
    auto &eq_list = equivalences_[member];
    eq_list.clear();
    for (const auto &other : merged) {
      if (other != member) {
        eq_list.push_back(other);
      }
    }
  }
}

std::vector<std::pair<std::string, std::string>> TypeRegistry::GetEquivalences(
    const std::string &lang, const std::string &type_name) const {
  std::string key = lang + ":" + type_name;
  std::vector<std::pair<std::string, std::string>> result;
  auto it = equivalences_.find(key);
  if (it != equivalences_.end()) {
    for (const auto &eq : it->second) {
      auto colon = eq.find(':');
      if (colon != std::string::npos) {
        result.emplace_back(eq.substr(0, colon), eq.substr(colon + 1));
      }
    }
  }
  return result;
}

bool TypeRegistry::AreEquivalent(const std::string &lang_a, const std::string &type_a,
                                 const std::string &lang_b, const std::string &type_b) const {
  if (lang_a == lang_b && type_a == type_b) return true;
  std::string key_a = lang_a + ":" + type_a;
  std::string key_b = lang_b + ":" + type_b;
  auto it = equivalences_.find(key_a);
  if (it != equivalences_.end()) {
    for (const auto &eq : it->second) {
      if (eq == key_b) return true;
    }
  }
  return false;
}

std::vector<std::pair<std::string, Type>> TypeRegistry::AllTypes() const {
  std::vector<std::pair<std::string, Type>> result;
  result.reserve(types_.size());
  for (const auto &entry : types_) {
    result.emplace_back(entry.first, entry.second);
  }
  return result;
}

}  // namespace polyglot::core
