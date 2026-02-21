/**
 * @file     types.h
 * @brief    Core type system for the polyglot compiler
 * @details  Provides a unified type representation across Python, C++, and Rust frontends.
 *           Includes primitive type mapping with bit-width tracking, type compatibility
 *           checking, implicit conversion rules, generic type unification, a type
 *           registry for cross-language type equivalence, and size/alignment computation.
 *
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  2.0.0
 *
 * Change History:
 * Version      Date          Author          Description
 * 1.0.0        2025-01-01    Manning Cyrus   Initial version with basic type system
 * 2.0.0        2026-02-06    Manning Cyrus   Extended primitive maps with bit-width tracking,
 *                                            array/optional/slice types, type registry,
 *                                            size/alignment computation, cross-language bridge
 *
 * Dependencies:
 * - C++20 or higher
 *
 * Main Classes:
 * - Type:          Unified type descriptor for all source languages
 * - TypeSystem:    Central type mapping, compatibility, and size computation
 * - TypeUnifier:   Hindley-Milner style unifier for generic type parameters
 * - TypeRegistry:  Named type registration and cross-language equivalence tracking
 */
#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace polyglot::core {

/// Enumeration of all supported type kinds in the unified type system.
enum class TypeKind {
  kInvalid,
  kVoid,
  kBool,
  kInt,
  kFloat,
  kString,
  kPointer,
  kFunction,
  kReference,
  kClass,
  kModule,
  kAny,
  kStruct,
  kUnion,
  kEnum,
  kTuple,
  kGenericParam,
  kGenericInstance,
  kArray,     ///< Fixed-size or dynamic array type.
  kOptional,  ///< Optional/nullable type (std::optional, Option<T>, Optional).
  kSlice,     ///< Slice type (Rust-style reference to contiguous memory).
};

/// Unified type representation for all supported source languages.
///
/// Every type in the polyglot compiler is represented as an instance of this
/// struct.  Factory methods produce the common primitive and compound types.
/// The optional @c bit_width and @c is_signed fields allow the type system
/// to distinguish between e.g. i32 and i64 while still treating them as kInt.
struct Type {
  TypeKind kind{TypeKind::kInvalid};
  std::string name;
  std::string language;          ///< Optional language tag for cross-language mapping.
  std::vector<Type> type_args;   ///< For generics, tuples, and compound types.
  std::string lifetime;          ///< For borrow tracking (Rust-style).

  bool is_const{false};
  bool is_volatile{false};
  bool is_rvalue_ref{false};
  int bit_width{0};              ///< 0 = unspecified; 8/16/32/64/128 for integers and floats.
  bool is_signed{true};          ///< Sign flag for integer types.
  size_t array_size{0};          ///< Element count for fixed-size arrays (0 = dynamic).

  // --- Factory methods for common types ---

  static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
  static Type Void() { return Type{TypeKind::kVoid, "void"}; }
  static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
  static Type Int() { return Type{TypeKind::kInt, "int"}; }
  static Type Float() { return Type{TypeKind::kFloat, "float"}; }
  static Type String() { return Type{TypeKind::kString, "string"}; }
  static Type Any() { return Type{TypeKind::kAny, "any"}; }

  /// Create an integer type with specified bit width and signedness.
  static Type Int(int bits, bool sign) {
    Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
    t.bit_width = bits;
    t.is_signed = sign;
    return t;
  }

  /// Create a floating-point type with specified bit width.
  static Type Float(int bits) {
    Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
    t.bit_width = bits;
    return t;
  }

  /// Create a fixed-size or dynamic array type.
  static Type Array(Type element, size_t count = 0) {
    Type t{TypeKind::kArray, "array"};
    t.type_args.push_back(std::move(element));
    t.array_size = count;
    return t;
  }

  /// Create an optional (nullable) type wrapping an inner type.
  static Type Optional(Type inner) {
    Type t{TypeKind::kOptional, "optional"};
    t.type_args.push_back(std::move(inner));
    return t;
  }

  /// Create a slice type (reference to contiguous memory).
  static Type Slice(Type element) {
    Type t{TypeKind::kSlice, "slice"};
    t.type_args.push_back(std::move(element));
    return t;
  }

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

  // --- Type query predicates ---

  bool IsNumeric() const { return kind == TypeKind::kInt || kind == TypeKind::kFloat; }
  bool IsInteger() const { return kind == TypeKind::kInt; }
  bool IsFloatingPoint() const { return kind == TypeKind::kFloat; }
  bool IsPointer() const { return kind == TypeKind::kPointer; }
  bool IsReference() const { return kind == TypeKind::kReference; }
  bool IsArray() const { return kind == TypeKind::kArray; }
  bool IsOptional() const { return kind == TypeKind::kOptional; }
  bool IsSlice() const { return kind == TypeKind::kSlice; }
  bool IsVoid() const { return kind == TypeKind::kVoid; }
  bool IsBool() const { return kind == TypeKind::kBool; }
  bool IsString() const { return kind == TypeKind::kString; }
  bool IsCallable() const { return kind == TypeKind::kFunction; }
  bool IsGeneric() const { return kind == TypeKind::kGenericParam || kind == TypeKind::kGenericInstance; }
  bool IsAggregate() const { return kind == TypeKind::kStruct || kind == TypeKind::kClass || kind == TypeKind::kUnion || kind == TypeKind::kArray || kind == TypeKind::kTuple; }
  bool HasTypeArgs() const { return !type_args.empty(); }
  bool IsConcrete() const {
    if (kind == TypeKind::kAny || kind == TypeKind::kGenericParam) return false;
    for (const auto &arg : type_args) {
      if (!arg.IsConcrete()) return false;
    }
    return true;
  }

  // --- Element access helpers (implemented in type_system.cpp) ---

  /// Get the element/pointee type for pointer, array, optional, slice, or reference types.
  Type GetElementType() const;

  /// Get the return type of a function type (first element of type_args).
  Type GetReturnType() const;

  /// Get parameter types of a function type (all type_args after the first).
  std::vector<Type> GetParamTypes() const;

  /// Get parameter count of a function type.
  size_t GetParamCount() const;

  // --- Qualifier helpers (implemented in type_system.cpp) ---

  /// Return a copy with the const qualifier set.
  Type WithConst(bool c = true) const;

  /// Return a copy with the volatile qualifier set.
  Type WithVolatile(bool v = true) const;

  /// Return a copy with all qualifiers stripped.
  Type StripQualifiers() const;

  // --- String representation (implemented in type_system.cpp) ---

  /// Produce a human-readable string for this type.
  std::string ToString() const;

  bool operator==(const Type &other) const;
};


/// The central type system providing primitive type mapping, compatibility checking,
/// implicit conversion rules, size/alignment computation, and alias management.
///
/// Primitive maps are initialised per language (Python, C++, Rust) in the
/// constructor with optional bit-width information so that frontend sema passes
/// can distinguish narrow and wide numeric types while keeping the general
/// compatibility checks broad enough for cross-language interop.
class TypeSystem {
 public:
  /// Constructor initialises extended primitive maps for all supported languages.
  TypeSystem();

  Type PointerTo(Type element) const {
    Type t{TypeKind::kPointer, element.name + "*"};
    t.type_args.push_back(std::move(element));
    return t;
  }
  Type PointerToWithCV(Type element, bool is_const, bool is_volatile) const {
    Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
    t.type_args.push_back(std::move(element));
    t.is_const = is_const;
    t.is_volatile = is_volatile;
    return t;
  }

  Type ReferenceTo(Type element, bool is_rvalue, bool is_const = false, bool is_volatile = false) const {
    Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
    t.type_args.push_back(std::move(element));
    t.is_const = is_const;
    t.is_volatile = is_volatile;
    t.is_rvalue_ref = is_rvalue;
    return t;
  }

  Type FunctionType(const std::string &name, Type return_type = Type::Any(),
                    std::vector<Type> params = {}) const {
    Type t{TypeKind::kFunction, name};
    t.type_args.reserve(params.size() + 1);
    t.type_args.push_back(std::move(return_type));
    t.type_args.insert(t.type_args.end(), params.begin(), params.end());
    return t;
  }

  /// Map a language-specific primitive name into a core type.
  ///
  /// The method lowercases the input name, performs language-specific container
  /// detection, then looks up the extended primitive maps built in the
  /// constructor.  Falls back to a user-defined class type if nothing matches.
  Type MapFromLanguage(const std::string &lang, const std::string &name) const;

  /// Coarse implicit conversion: numeric widening, add-const, pointer to void*,
  /// reference bindings, T -> Optional<T>, array -> slice.
  bool CanImplicitlyConvert(const Type &from, const Type &to) const;

  /// Basic compatibility: allow Any, exact match, or both numeric.
  /// Also checks structural compatibility for tuples, generic instances,
  /// arrays, optionals, and slices.
  bool IsCompatible(const Type &lhs, const Type &rhs) const;

  Type UserType(std::string name, std::string lang, TypeKind kind = TypeKind::kStruct) const {
    Type t{kind, std::move(name)};
    t.language = std::move(lang);
    return t;
  }

  Type TupleOf(std::vector<Type> elems) const { return Type::Tuple(std::move(elems)); }

  Type Generic(std::string name, std::vector<Type> args, std::string lang) const {
    return Type::GenericInstance(std::move(name), std::move(args), std::move(lang));
  }

  // --- Size and alignment computation for core types ---

  /// Compute the size in bytes for a core type on a typical 64-bit platform.
  size_t SizeOf(const Type &t) const;

  /// Compute the alignment in bytes for a core type on a typical 64-bit platform.
  size_t AlignOf(const Type &t) const;

  // --- Type relationship utilities ---

  /// Compute the common (promoted) type for a binary operation.
  /// Follows C-like promotion: int widens to float, narrower widens to wider.
  Type CommonType(const Type &a, const Type &b) const;

  /// Check whether two types have compatible memory layouts for cross-language interop.
  bool AreLayoutCompatible(const Type &a, const Type &b) const;

  /// Normalise a type by stripping qualifiers and resolving aliases.
  Type Normalize(const Type &t) const;

  /// Check if a conversion from @p from to @p to is a widening conversion.
  static bool IsWidening(const Type &from, const Type &to);

  /// Check if a conversion from @p from to @p to is a narrowing conversion.
  static bool IsNarrowing(const Type &from, const Type &to);

  /// Return a numeric rank for a type used in overload resolution scoring.
  /// Lower rank = narrower type.
  int ConversionRank(const Type &t) const;

  // --- Alias management ---

  /// Register a type alias (e.g., "size_t" -> Int(64, false)).
  void RegisterAlias(const std::string &alias, Type target);

  /// Resolve a previously registered alias; returns Invalid if not found.
  Type ResolveAlias(const std::string &name) const;

  /// Check whether an alias has been registered.
  bool HasAlias(const std::string &name) const;

  // --- String representation ---

  /// Return the TypeKind as a human-readable string.
  static std::string KindToString(TypeKind kind);

 private:
  /// Extended primitive maps per language with bit-width information.
  std::unordered_map<std::string, std::unordered_map<std::string, Type>> primitive_maps_;

  /// Type alias table.
  std::unordered_map<std::string, Type> aliases_;
};

/// Simple constraint representation for generics/traits.
struct TypeConstraint {
  enum class Kind { kEquals, kTrait } kind{Kind::kEquals};
  Type lhs;
  Type rhs;
  std::string trait;  ///< Used when kind == kTrait.
};

/// A lightweight unifier for Type that treats kGenericParam as variables.
///
/// Implements a standard union-find style algorithm with an occurs check to
/// prevent infinite types.  Trait constraints are recorded and can be queried
/// but are not enforced during unification itself.
class TypeUnifier {
 public:
  bool Unify(const Type &a, const Type &b) {
    Type aa = Apply(a);
    Type bb = Apply(b);
    if (IsVar(aa)) return Bind(aa, bb);
    if (IsVar(bb)) return Bind(bb, aa);
    if (aa.kind != bb.kind) return false;
    // For numeric types, unify by kind alone (ignore name differences
    // such as "int" vs "i32" that arise from generic vs language-specific
    // type constructors).
    if (aa.kind == TypeKind::kInt || aa.kind == TypeKind::kFloat ||
        aa.kind == TypeKind::kBool || aa.kind == TypeKind::kString ||
        aa.kind == TypeKind::kVoid || aa.kind == TypeKind::kAny) {
        return true;
    }
    if (aa.name != bb.name || aa.language != bb.language) return false;
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

  /// Check whether a trait constraint has been registered for a given type.
  bool HasTraitConstraint(const Type &t, const std::string &trait) const {
    Type applied = Apply(t);
    for (const auto &c : trait_constraints_) {
      if (c.kind == TypeConstraint::Kind::kTrait && c.trait == trait) {
        Type ct = Apply(c.lhs);
        if (ct == applied) return true;
      }
    }
    return false;
  }

  /// Return all trait names constrained on the given type.
  std::vector<std::string> TraitsFor(const Type &t) const {
    std::vector<std::string> result;
    Type applied = Apply(t);
    for (const auto &c : trait_constraints_) {
      if (c.kind == TypeConstraint::Kind::kTrait) {
        Type ct = Apply(c.lhs);
        if (ct == applied) result.push_back(c.trait);
      }
    }
    return result;
  }

  const std::unordered_map<std::string, Type> &Substitutions() const { return subst_; }

  /// Reset all substitutions and constraints.
  void Reset() {
    subst_.clear();
    trait_constraints_.clear();
  }

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
  std::vector<TypeConstraint> trait_constraints_{};
};

/// Registry for named types and cross-language type equivalence tracking.
///
/// Use this to store user-defined or language-specific types by name, and to
/// declare cross-language equivalences (e.g., Python's @c int is equivalent
/// to Rust's @c i64 on a 64-bit platform).
class TypeRegistry {
 public:
  TypeRegistry() = default;

  /// Register a named type in the registry.
  void Register(const std::string &name, Type type);

  /// Find a previously registered type by name; returns nullptr if not found.
  const Type *Find(const std::string &name) const;

  /// Check whether a type with the given name is registered.
  bool Contains(const std::string &name) const;

  /// Return the number of registered types.
  size_t Size() const;

  /// Clear all registered types and equivalences.
  void Clear();

  // --- Cross-language equivalence ---

  /// Register two types from different languages as equivalent.
  void RegisterEquivalence(const std::string &lang_a, const std::string &type_a,
                           const std::string &lang_b, const std::string &type_b);

  /// Get all types equivalent to the given (lang, type_name) pair.
  std::vector<std::pair<std::string, std::string>> GetEquivalences(
      const std::string &lang, const std::string &type_name) const;

  /// Check whether two types from different languages are registered as equivalent.
  bool AreEquivalent(const std::string &lang_a, const std::string &type_a,
                     const std::string &lang_b, const std::string &type_b) const;

  // --- Iteration ---

  /// Return all registered (name, type) pairs.
  std::vector<std::pair<std::string, Type>> AllTypes() const;

 private:
  std::unordered_map<std::string, Type> types_;
  /// Equivalence map: key = "lang:type", value = list of equivalent "lang:type" entries.
  std::unordered_map<std::string, std::vector<std::string>> equivalences_;
};

inline bool Type::operator==(const Type &other) const {
  return kind == other.kind && name == other.name && language == other.language &&
         type_args == other.type_args && lifetime == other.lifetime && is_const == other.is_const &&
         is_volatile == other.is_volatile && is_rvalue_ref == other.is_rvalue_ref &&
         bit_width == other.bit_width && is_signed == other.is_signed &&
         array_size == other.array_size;
}

}  // namespace polyglot::core
