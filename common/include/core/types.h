#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace polyglot::core {

enum class TypeKind {
  kInvalid,
  kVoid,
  kBool,
  kInt,
  kFloat,
  kString,
  kPointer,
  kFunction,
  kClass,
  kModule,
  kAny,
  kStruct,
  kUnion,
  kEnum,
  kTuple,
  kGenericParam,
  kGenericInstance
};

struct Type {
  TypeKind kind{TypeKind::kInvalid};
  std::string name;
  std::string language;  // optional language tag for cross-language mapping
  std::vector<Type> type_args;  // for generics/tuples/compound
  std::string lifetime;         // for borrow tracking (Rust-style)

  static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
  static Type Void() { return Type{TypeKind::kVoid, "void"}; }
  static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
  static Type Int() { return Type{TypeKind::kInt, "int"}; }
  static Type Float() { return Type{TypeKind::kFloat, "float"}; }
  static Type String() { return Type{TypeKind::kString, "string"}; }
  static Type Any() { return Type{TypeKind::kAny, "any"}; }
  static Type Struct(std::string name, std::string lang = {}) {
    Type t{TypeKind::kStruct, std::move(name)};
    t.language = std::move(lang);
    return t;
  }
  static Type Union(std::string name, std::string lang = {}) {
    Type t{TypeKind::kUnion, std::move(name)};
    t.language = std::move(lang);
    return t;
  }
  static Type Enum(std::string name, std::string lang = {}) {
    Type t{TypeKind::kEnum, std::move(name)};
    t.language = std::move(lang);
    return t;
  }
  static Type Module(std::string name, std::string lang = {}) {
    Type t{TypeKind::kModule, std::move(name)};
    t.language = std::move(lang);
    return t;
  }
  static Type GenericParam(std::string name, std::string lang = {}) {
    Type t{TypeKind::kGenericParam, std::move(name)};
    t.language = std::move(lang);
    return t;
  }
  static Type Tuple(std::vector<Type> elems) {
    Type t{TypeKind::kTuple, "tuple"};
    t.type_args = std::move(elems);
    return t;
  }
  static Type GenericInstance(std::string name, std::vector<Type> args, std::string lang = {}) {
    Type t{TypeKind::kGenericInstance, std::move(name)};
    t.type_args = std::move(args);
    t.language = std::move(lang);
    return t;
  }

  bool IsNumeric() const {
    return kind == TypeKind::kInt || kind == TypeKind::kFloat;
  }

  bool IsConcrete() const {
    if (kind == TypeKind::kAny || kind == TypeKind::kGenericParam) return false;
    for (const auto &arg : type_args) {
      if (!arg.IsConcrete()) return false;
    }
    return true;
  }

  bool operator==(const Type &other) const;
};


class TypeSystem {
 public:
  TypeSystem() = default;

  Type PointerTo(Type element) const {
    return Type{TypeKind::kPointer, element.name + "*"};
  }

  Type FunctionType(const std::string &name, Type return_type = Type::Any(),
                    std::vector<Type> params = {}) const {
    Type t{TypeKind::kFunction, name};
    t.type_args.reserve(params.size() + 1);
    t.type_args.push_back(std::move(return_type));
    t.type_args.insert(t.type_args.end(), params.begin(), params.end());
    return t;
  }

  // Map a language-specific primitive name into a core type.
  Type MapFromLanguage(const std::string &lang, const std::string &name) const {
    std::string lowered;
    lowered.reserve(name.size());
    for (char c : name) {
      lowered.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
    }
    // language-specific special handling for container-ish builtins
    if (lang == "python") {
      if (lowered == "list") return Generic("list", {Type::Any()}, lang);
      if (lowered == "dict") return Generic("dict", {Type::Any(), Type::Any()}, lang);
      if (lowered == "tuple") return TupleOf({});
    }

    auto it = primitive_maps_.find(lang);
    if (it != primitive_maps_.end()) {
      auto found = it->second.find(lowered);
      if (found != it->second.end()) {
        Type t = found->second;
        t.language = lang;
        return t;
      }
    }
    // fallback: treat as opaque but keep language tag for cross-language mapping
    return UserType(name, lang, TypeKind::kClass);
  }

  // Basic compatibility: allow Any, exact match, or both numeric.
  bool IsCompatible(const Type &lhs, const Type &rhs) const {
    if (lhs.kind == TypeKind::kAny || rhs.kind == TypeKind::kAny)
      return true;
    if (lhs == rhs)
      return true;
    if (lhs.IsNumeric() && rhs.IsNumeric())
      return true;
    if (lhs.kind == TypeKind::kTuple && rhs.kind == TypeKind::kTuple &&
        lhs.type_args.size() == rhs.type_args.size()) {
      for (size_t i = 0; i < lhs.type_args.size(); ++i) {
        if (!IsCompatible(lhs.type_args[i], rhs.type_args[i])) return false;
      }
      return true;
    }
    if (lhs.kind == TypeKind::kGenericInstance && rhs.kind == TypeKind::kGenericInstance &&
        lhs.name == rhs.name && lhs.type_args.size() == rhs.type_args.size()) {
      for (size_t i = 0; i < lhs.type_args.size(); ++i) {
        if (!IsCompatible(lhs.type_args[i], rhs.type_args[i])) return false;
      }
      return true;
    }
    return false;
  }

  Type UserType(std::string name, std::string lang, TypeKind kind = TypeKind::kStruct) const {
    Type t{kind, std::move(name)};
    t.language = std::move(lang);
    return t;
  }

  Type TupleOf(std::vector<Type> elems) const { return Type::Tuple(std::move(elems)); }

  Type Generic(std::string name, std::vector<Type> args, std::string lang) const {
    return Type::GenericInstance(std::move(name), std::move(args), std::move(lang));
  }

 private:
  // Minimal primitive maps; extend per language as needed.
  const std::unordered_map<std::string, std::unordered_map<std::string, Type>> primitive_maps_{
  {"python",
   {{"int", Type::Int()},
    {"float", Type::Float()},
    {"bool", Type::Bool()},
    {"str", Type::String()},
    {"none", Type::Void()},
    {"list", Generic("list", {Type::Any()}, "python")},
    {"dict", Generic("dict", {Type::Any(), Type::Any()}, "python")},
    {"tuple", TupleOf({})}}},
  {"cpp",
   {{"int", Type::Int()},
    {"short", Type::Int()},
    {"long", Type::Int()},
    {"long long", Type::Int()},
    {"unsigned", Type::Int()},
    {"size_t", Type::Int()},
    {"double", Type::Float()},
    {"float", Type::Float()},
    {"bool", Type::Bool()},
    {"void", Type::Void()},
    {"char*", Type{TypeKind::kPointer, "char*"}},
    {"std::string", Type::String()},
    {"string", Type::String()},
    {"std::nullptr_t", Type::Void()}}},
  {"rust",
   {{"i8", Type::Int()},
    {"i16", Type::Int()},
    {"i32", Type::Int()},
    {"i64", Type::Int()},
    {"i128", Type::Int()},
    {"isize", Type::Int()},
    {"u8", Type::Int()},
    {"u16", Type::Int()},
    {"u32", Type::Int()},
    {"u64", Type::Int()},
    {"u128", Type::Int()},
    {"usize", Type::Int()},
    {"f32", Type::Float()},
    {"f64", Type::Float()},
    {"bool", Type::Bool()},
    {"char", Type::Int()},
    {"str", Type::String()},
    {"string", Type::String()},
    {"()", Type::Void()}}}};
};

// Simple constraint representation for generics/traits.
struct TypeConstraint {
  enum class Kind { kEquals, kTrait } kind{Kind::kEquals};
  Type lhs;
  Type rhs;
  std::string trait;  // used when kind == kTrait
};

// A lightweight unifier for Type that treats kGenericParam as variables.
class TypeUnifier {
 public:
  bool Unify(const Type &a, const Type &b) {
    Type aa = Apply(a);
    Type bb = Apply(b);
    if (IsVar(aa)) return Bind(aa, bb);
    if (IsVar(bb)) return Bind(bb, aa);
    if (aa.kind != bb.kind || aa.name != bb.name || aa.language != bb.language) return false;
    if (aa.type_args.size() != bb.type_args.size()) return false;
    for (size_t i = 0; i < aa.type_args.size(); ++i) {
      if (!Unify(aa.type_args[i], bb.type_args[i])) return false;
    }
    if (aa.lifetime != bb.lifetime) return false;
    return true;
  }

  Type Apply(const Type &t) const {
    if (IsVar(t)) {
      auto it = subst_.find(VarKey(t));
      if (it != subst_.end()) return Apply(it->second);
      return t;
    }
    Type out = t;
    for (auto &arg : out.type_args) {
      arg = Apply(arg);
    }
    return out;
  }

  void AddTraitConstraint(const Type &t, std::string trait) {
    trait_constraints_.push_back(TypeConstraint{TypeConstraint::Kind::kTrait, t, Type::Invalid(),
                                                std::move(trait)});
  }

  const std::unordered_map<std::string, Type> &Substitutions() const { return subst_; }

 private:
  static bool IsVar(const Type &t) { return t.kind == TypeKind::kGenericParam; }
  static std::string VarKey(const Type &t) { return t.language + ":" + t.name; }

  bool Occurs(const std::string &key, const Type &t) {
    if (IsVar(t) && VarKey(t) == key) return true;
    for (const auto &arg : t.type_args) {
      if (Occurs(key, arg)) return true;
    }
    return false;
  }

  bool Bind(const Type &var, const Type &value) {
    std::string key = VarKey(var);
    Type applied = Apply(value);
    if (Occurs(key, applied)) return false;
    subst_[key] = applied;
    return true;
  }

  std::unordered_map<std::string, Type> subst_{};
  std::vector<TypeConstraint> trait_constraints_{};  // currently informational
};

inline bool Type::operator==(const Type &other) const {
  return kind == other.kind && name == other.name && language == other.language &&
         type_args == other.type_args && lifetime == other.lifetime;
}

}  // namespace polyglot::core
