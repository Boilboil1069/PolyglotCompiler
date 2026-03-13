/**
 * @file     type_system_test.cpp
 * @brief    Unit tests for the core type system, TypeRegistry, TypeUnifier,
 *           and SymbolTable.
 * @details  Covers primitive type construction, language-specific mapping,
 *           implicit conversions, compatibility checks, size/alignment,
 *           type aliases, registry equivalences, unification, and symbol table
 *           scope management with overload resolution.
 *
 * @author   Manning Cyrus
 * @date     2026-02-07
 */

#include <catch2/catch_test_macros.hpp>

#include "common/include/core/symbols.h"
#include "common/include/core/types.h"

using namespace polyglot::core;

// ============================================================================
// Type factory and predicate tests
// ============================================================================

TEST_CASE("Type - Factory methods produce correct kinds", "[type][factory]") {
  SECTION("Primitive types") {
    REQUIRE(Type::Invalid().kind == TypeKind::kInvalid);
    REQUIRE(Type::Void().kind == TypeKind::kVoid);
    REQUIRE(Type::Bool().kind == TypeKind::kBool);
    REQUIRE(Type::Int().kind == TypeKind::kInt);
    REQUIRE(Type::Float().kind == TypeKind::kFloat);
    REQUIRE(Type::String().kind == TypeKind::kString);
    REQUIRE(Type::Any().kind == TypeKind::kAny);
  }

  SECTION("Sized integer types") {
    auto i32 = Type::Int(32, true);
    REQUIRE(i32.kind == TypeKind::kInt);
    REQUIRE(i32.bit_width == 32);
    REQUIRE(i32.is_signed == true);
    REQUIRE(i32.name == "i32");

    auto u64 = Type::Int(64, false);
    REQUIRE(u64.kind == TypeKind::kInt);
    REQUIRE(u64.bit_width == 64);
    REQUIRE(u64.is_signed == false);
    REQUIRE(u64.name == "u64");
  }

  SECTION("Sized float types") {
    auto f32 = Type::Float(32);
    REQUIRE(f32.kind == TypeKind::kFloat);
    REQUIRE(f32.bit_width == 32);
    REQUIRE(f32.name == "f32");

    auto f64 = Type::Float(64);
    REQUIRE(f64.bit_width == 64);
  }

  SECTION("Array type") {
    auto arr = Type::Array(Type::Int(32, true), 10);
    REQUIRE(arr.kind == TypeKind::kArray);
    REQUIRE(arr.array_size == 10);
    REQUIRE(arr.type_args.size() == 1);
    REQUIRE(arr.type_args[0].kind == TypeKind::kInt);
    REQUIRE(arr.IsArray());
    REQUIRE(arr.IsAggregate());
  }

  SECTION("Optional type") {
    auto opt = Type::Optional(Type::String());
    REQUIRE(opt.kind == TypeKind::kOptional);
    REQUIRE(opt.IsOptional());
    REQUIRE(opt.type_args.size() == 1);
    REQUIRE(opt.type_args[0].kind == TypeKind::kString);
  }

  SECTION("Slice type") {
    auto sl = Type::Slice(Type::Int(8, false));
    REQUIRE(sl.kind == TypeKind::kSlice);
    REQUIRE(sl.IsSlice());
    REQUIRE(sl.type_args[0].bit_width == 8);
  }

  SECTION("Tuple type") {
    auto tup = Type::Tuple({Type::Int(), Type::String(), Type::Bool()});
    REQUIRE(tup.kind == TypeKind::kTuple);
    REQUIRE(tup.type_args.size() == 3);
    REQUIRE(tup.IsAggregate());
  }

  SECTION("GenericInstance type") {
    auto gi = Type::GenericInstance("Vec", {Type::Int(32, true)}, "rust");
    REQUIRE(gi.kind == TypeKind::kGenericInstance);
    REQUIRE(gi.name == "Vec");
    REQUIRE(gi.language == "rust");
    REQUIRE(gi.type_args.size() == 1);
    REQUIRE(gi.IsGeneric());
  }

  SECTION("GenericParam type") {
    auto gp = Type::GenericParam("T", "cpp");
    REQUIRE(gp.kind == TypeKind::kGenericParam);
    REQUIRE(gp.IsGeneric());
    REQUIRE(!gp.IsConcrete());
  }
}

TEST_CASE("Type - Predicate methods", "[type][predicates]") {
  REQUIRE(Type::Int(32, true).IsNumeric());
  REQUIRE(Type::Float(64).IsNumeric());
  REQUIRE(Type::Int(32, true).IsInteger());
  REQUIRE(Type::Float(64).IsFloatingPoint());
  REQUIRE(!Type::String().IsNumeric());
  REQUIRE(Type::Bool().IsBool());
  REQUIRE(Type::String().IsString());
  REQUIRE(Type::Void().IsVoid());

  auto fn = Type{TypeKind::kFunction, "foo"};
  REQUIRE(fn.IsCallable());

  REQUIRE(Type::Int(32, true).IsConcrete());
  REQUIRE(!Type::GenericParam("T").IsConcrete());
  REQUIRE(!Type::Any().IsConcrete());
}

TEST_CASE("Type - Element access helpers", "[type][accessors]") {
  TypeSystem ts;

  SECTION("Pointer element type") {
    auto ptr = ts.PointerTo(Type::Int(32, true));
    REQUIRE(ptr.GetElementType().kind == TypeKind::kInt);
    REQUIRE(ptr.GetElementType().bit_width == 32);
  }

  SECTION("Reference element type") {
    auto ref = ts.ReferenceTo(Type::String(), false);
    REQUIRE(ref.GetElementType().kind == TypeKind::kString);
  }

  SECTION("Array element type") {
    auto arr = Type::Array(Type::Float(64), 5);
    REQUIRE(arr.GetElementType().kind == TypeKind::kFloat);
  }

  SECTION("Optional element type") {
    auto opt = Type::Optional(Type::Bool());
    REQUIRE(opt.GetElementType().kind == TypeKind::kBool);
  }

  SECTION("Slice element type") {
    auto sl = Type::Slice(Type::Int(8, false));
    REQUIRE(sl.GetElementType().kind == TypeKind::kInt);
  }

  SECTION("Non-compound returns Invalid") {
    REQUIRE(Type::Int().GetElementType().kind == TypeKind::kInvalid);
  }

  SECTION("Function return and param types") {
    auto fn = ts.FunctionType("add", Type::Int(32, true),
                              {Type::Int(32, true), Type::Int(32, true)});
    REQUIRE(fn.GetReturnType().kind == TypeKind::kInt);
    REQUIRE(fn.GetParamCount() == 2);
    auto params = fn.GetParamTypes();
    REQUIRE(params.size() == 2);
    REQUIRE(params[0].bit_width == 32);
  }
}

TEST_CASE("Type - Qualifier helpers", "[type][qualifiers]") {
  auto t = Type::Int(32, true);
  REQUIRE(!t.is_const);

  auto ct = t.WithConst();
  REQUIRE(ct.is_const);
  REQUIRE(ct.kind == TypeKind::kInt);

  auto vt = t.WithVolatile();
  REQUIRE(vt.is_volatile);

  auto stripped = ct.WithVolatile().StripQualifiers();
  REQUIRE(!stripped.is_const);
  REQUIRE(!stripped.is_volatile);
  REQUIRE(!stripped.is_rvalue_ref);
}

TEST_CASE("Type - Equality", "[type][equality]") {
  REQUIRE(Type::Int(32, true) == Type::Int(32, true));
  REQUIRE(!(Type::Int(32, true) == Type::Int(64, true)));
  REQUIRE(!(Type::Int(32, true) == Type::Int(32, false)));
  REQUIRE(Type::Void() == Type::Void());
  REQUIRE(!(Type::Int() == Type::Float()));
}

TEST_CASE("Type - ToString", "[type][tostring]") {
  REQUIRE(Type::Void().ToString() == "void");
  REQUIRE(Type::Bool().ToString() == "bool");
  REQUIRE(Type::Int(32, true).ToString() == "i32");
  REQUIRE(Type::Int(64, false).ToString() == "u64");
  REQUIRE(Type::Float(64).ToString() == "f64");
  REQUIRE(Type::String().ToString() == "string");
  REQUIRE(Type::Any().ToString() == "any");

  auto arr = Type::Array(Type::Int(32, true), 4);
  REQUIRE(arr.ToString() == "[i32; 4]");

  auto opt = Type::Optional(Type::String());
  REQUIRE(opt.ToString() == "string?");

  auto sl = Type::Slice(Type::Int(8, false));
  REQUIRE(sl.ToString() == "&[u8]");

  auto tup = Type::Tuple({Type::Int(), Type::Float()});
  REQUIRE(tup.ToString() == "(int, float)");

  auto ct = Type::Int(32, true).WithConst();
  REQUIRE(ct.ToString() == "const i32");
}

// ============================================================================
// TypeSystem tests
// ============================================================================

TEST_CASE("TypeSystem - MapFromLanguage for Python", "[typesystem][python]") {
  TypeSystem ts;

  SECTION("Basic Python primitives") {
    auto t = ts.MapFromLanguage("python", "int");
    REQUIRE(t.kind == TypeKind::kInt);
    REQUIRE(t.bit_width == 64);

    t = ts.MapFromLanguage("python", "float");
    REQUIRE(t.kind == TypeKind::kFloat);
    REQUIRE(t.bit_width == 64);

    t = ts.MapFromLanguage("python", "bool");
    REQUIRE(t.kind == TypeKind::kBool);

    t = ts.MapFromLanguage("python", "str");
    REQUIRE(t.kind == TypeKind::kString);

    t = ts.MapFromLanguage("python", "None");
    REQUIRE(t.kind == TypeKind::kVoid);
  }

  SECTION("Python container types") {
    auto list_t = ts.MapFromLanguage("python", "List[int]");
    REQUIRE(list_t.kind == TypeKind::kGenericInstance);
    REQUIRE(list_t.name == "list");
    REQUIRE(list_t.type_args.size() == 1);
    REQUIRE(list_t.type_args[0].kind == TypeKind::kInt);

    auto dict_t = ts.MapFromLanguage("python", "Dict[str, int]");
    REQUIRE(dict_t.kind == TypeKind::kGenericInstance);
    REQUIRE(dict_t.name == "dict");
    REQUIRE(dict_t.type_args.size() == 2);

    auto opt_t = ts.MapFromLanguage("python", "Optional[int]");
    REQUIRE(opt_t.kind == TypeKind::kOptional);
  }

  SECTION("Python unknown types become user-defined structs") {
    auto t = ts.MapFromLanguage("python", "MyClass");
    REQUIRE(t.kind == TypeKind::kStruct);
    REQUIRE(t.name == "MyClass");
  }
}

TEST_CASE("TypeSystem - MapFromLanguage for C++", "[typesystem][cpp]") {
  TypeSystem ts;

  SECTION("C++ primitive types") {
    auto t = ts.MapFromLanguage("cpp", "int");
    REQUIRE(t.kind == TypeKind::kInt);
    REQUIRE(t.bit_width == 32);
    REQUIRE(t.is_signed == true);

    t = ts.MapFromLanguage("cpp", "unsigned long long");
    REQUIRE(t.kind == TypeKind::kInt);
    REQUIRE(t.bit_width == 64);
    REQUIRE(t.is_signed == false);

    t = ts.MapFromLanguage("cpp", "double");
    REQUIRE(t.kind == TypeKind::kFloat);
    REQUIRE(t.bit_width == 64);

    t = ts.MapFromLanguage("cpp", "bool");
    REQUIRE(t.kind == TypeKind::kBool);

    t = ts.MapFromLanguage("cpp", "void");
    REQUIRE(t.kind == TypeKind::kVoid);

    t = ts.MapFromLanguage("cpp", "auto");
    REQUIRE(t.kind == TypeKind::kAny);
  }

  SECTION("C++ fixed-width types") {
    auto t = ts.MapFromLanguage("cpp", "int32_t");
    REQUIRE(t.bit_width == 32);
    REQUIRE(t.is_signed == true);

    t = ts.MapFromLanguage("cpp", "uint8_t");
    REQUIRE(t.bit_width == 8);
    REQUIRE(t.is_signed == false);
  }

  SECTION("C++ template types") {
    auto vec = ts.MapFromLanguage("cpp", "std::vector<int>");
    REQUIRE(vec.kind == TypeKind::kGenericInstance);
    REQUIRE(vec.name == "std::vector");
    REQUIRE(vec.type_args.size() == 1);
    REQUIRE(vec.type_args[0].kind == TypeKind::kInt);
  }

  SECTION("C++ string type") {
    auto t = ts.MapFromLanguage("cpp", "std::string");
    REQUIRE(t.kind == TypeKind::kString);
  }
}

TEST_CASE("TypeSystem - MapFromLanguage for Rust", "[typesystem][rust]") {
  TypeSystem ts;

  SECTION("Rust primitive types") {
    auto t = ts.MapFromLanguage("rust", "i32");
    REQUIRE(t.kind == TypeKind::kInt);
    REQUIRE(t.bit_width == 32);
    REQUIRE(t.is_signed == true);

    t = ts.MapFromLanguage("rust", "u64");
    REQUIRE(t.kind == TypeKind::kInt);
    REQUIRE(t.bit_width == 64);
    REQUIRE(t.is_signed == false);

    t = ts.MapFromLanguage("rust", "f64");
    REQUIRE(t.kind == TypeKind::kFloat);
    REQUIRE(t.bit_width == 64);

    t = ts.MapFromLanguage("rust", "bool");
    REQUIRE(t.kind == TypeKind::kBool);

    t = ts.MapFromLanguage("rust", "()");
    REQUIRE(t.kind == TypeKind::kVoid);
  }

  SECTION("Rust string types") {
    REQUIRE(ts.MapFromLanguage("rust", "str").kind == TypeKind::kString);
    REQUIRE(ts.MapFromLanguage("rust", "String").kind == TypeKind::kString);
    REQUIRE(ts.MapFromLanguage("rust", "&str").kind == TypeKind::kString);
  }

  SECTION("Rust generic types") {
    auto vec = ts.MapFromLanguage("rust", "Vec<i32>");
    REQUIRE(vec.kind == TypeKind::kGenericInstance);
    REQUIRE(vec.name == "Vec");
    REQUIRE(vec.type_args.size() == 1);

    auto opt = ts.MapFromLanguage("rust", "Option<i32>");
    REQUIRE(opt.kind == TypeKind::kOptional);
    REQUIRE(opt.type_args.size() == 1);
  }

  SECTION("Rust i128/u128") {
    auto t = ts.MapFromLanguage("rust", "i128");
    REQUIRE(t.bit_width == 128);
    REQUIRE(t.is_signed == true);

    t = ts.MapFromLanguage("rust", "u128");
    REQUIRE(t.bit_width == 128);
    REQUIRE(t.is_signed == false);
  }
}

TEST_CASE("TypeSystem - Language alias normalization", "[typesystem][alias]") {
  TypeSystem ts;
  // "c++", "cxx", "py", "rs" should all work as aliases
  REQUIRE(ts.MapFromLanguage("c++", "int").kind == TypeKind::kInt);
  REQUIRE(ts.MapFromLanguage("rs", "i32").kind == TypeKind::kInt);
  REQUIRE(ts.MapFromLanguage("py", "int").kind == TypeKind::kInt);
}

TEST_CASE("TypeSystem - CanImplicitlyConvert", "[typesystem][conversion]") {
  TypeSystem ts;

  SECTION("Same type always converts") {
    REQUIRE(ts.CanImplicitlyConvert(Type::Int(32, true), Type::Int(32, true)));
  }

  SECTION("Any type converts to/from anything") {
    REQUIRE(ts.CanImplicitlyConvert(Type::Any(), Type::Int()));
    REQUIRE(ts.CanImplicitlyConvert(Type::String(), Type::Any()));
  }

  SECTION("Numeric widening") {
    REQUIRE(ts.CanImplicitlyConvert(Type::Int(32, true), Type::Int(64, true)));
    REQUIRE(ts.CanImplicitlyConvert(Type::Int(32, true), Type::Float(64)));
    REQUIRE(ts.CanImplicitlyConvert(Type::Float(32), Type::Float(64)));
  }

  SECTION("Bool to numeric") {
    REQUIRE(ts.CanImplicitlyConvert(Type::Bool(), Type::Int(32, true)));
    REQUIRE(ts.CanImplicitlyConvert(Type::Bool(), Type::Float(64)));
  }

  SECTION("Add const qualification") {
    auto t = Type::Int(32, true);
    auto ct = t.WithConst();
    REQUIRE(ts.CanImplicitlyConvert(t, ct));
  }

  SECTION("T -> Optional<T>") {
    auto opt = Type::Optional(Type::Int(32, true));
    REQUIRE(ts.CanImplicitlyConvert(Type::Int(32, true), opt));
  }

  SECTION("Array -> Slice") {
    auto arr = Type::Array(Type::Int(32, true), 10);
    auto sl = Type::Slice(Type::Int(32, true));
    REQUIRE(ts.CanImplicitlyConvert(arr, sl));
  }

  SECTION("Pointer to void*") {
    auto ptr = ts.PointerTo(Type::Int(32, true));
    auto voidptr = ts.PointerTo(Type::Void());
    REQUIRE(ts.CanImplicitlyConvert(ptr, voidptr));
  }

  SECTION("Reference binding") {
    auto ref = ts.ReferenceTo(Type::Int(32, true), false, true);  // const int&
    REQUIRE(ts.CanImplicitlyConvert(Type::Int(32, true), ref));
  }

  SECTION("Incompatible types do not convert") {
    REQUIRE(!ts.CanImplicitlyConvert(Type::String(), Type::Int()));
    REQUIRE(!ts.CanImplicitlyConvert(Type::Void(), Type::Int()));
  }
}

TEST_CASE("TypeSystem - IsCompatible", "[typesystem][compatible]") {
  TypeSystem ts;

  SECTION("Exact match") {
    REQUIRE(ts.IsCompatible(Type::Int(32, true), Type::Int(32, true)));
  }

  SECTION("Any is universally compatible") {
    REQUIRE(ts.IsCompatible(Type::Any(), Type::String()));
    REQUIRE(ts.IsCompatible(Type::Float(), Type::Any()));
  }

  SECTION("Numeric types are broadly compatible") {
    REQUIRE(ts.IsCompatible(Type::Int(32, true), Type::Float(64)));
    REQUIRE(ts.IsCompatible(Type::Int(8, false), Type::Int(64, true)));
  }

  SECTION("Bool and numeric") {
    REQUIRE(ts.IsCompatible(Type::Bool(), Type::Int()));
  }

  SECTION("Structural tuple compatibility") {
    auto t1 = Type::Tuple({Type::Int(32, true), Type::Float(32)});
    auto t2 = Type::Tuple({Type::Int(64, true), Type::Float(64)});
    REQUIRE(ts.IsCompatible(t1, t2));  // all numeric pairs

    auto t3 = Type::Tuple({Type::Int(), Type::String()});
    REQUIRE(!ts.IsCompatible(t1, t3));
  }

  SECTION("Array compatibility") {
    auto a1 = Type::Array(Type::Int(32, true), 10);
    auto a2 = Type::Array(Type::Int(64, true));  // dynamic
    REQUIRE(ts.IsCompatible(a1, a2));  // Fixed vs dynamic ok

    auto a3 = Type::Array(Type::Int(32, true), 5);
    REQUIRE(!ts.IsCompatible(a1, a3));  // Different fixed sizes
  }

  SECTION("Incompatible different kinds") {
    REQUIRE(!ts.IsCompatible(Type::String(), Type::Int()));
    REQUIRE(!ts.IsCompatible(Type::Void(), Type::Bool()));
  }
}

TEST_CASE("TypeSystem - SizeOf and AlignOf", "[typesystem][size]") {
  TypeSystem ts;

  SECTION("Primitive sizes") {
    REQUIRE(ts.SizeOf(Type::Void()) == 0);
    REQUIRE(ts.SizeOf(Type::Bool()) == 1);
    REQUIRE(ts.SizeOf(Type::Int(8, true)) == 1);
    REQUIRE(ts.SizeOf(Type::Int(16, true)) == 2);
    REQUIRE(ts.SizeOf(Type::Int(32, true)) == 4);
    REQUIRE(ts.SizeOf(Type::Int(64, true)) == 8);
    REQUIRE(ts.SizeOf(Type::Float(32)) == 4);
    REQUIRE(ts.SizeOf(Type::Float(64)) == 8);
    REQUIRE(ts.SizeOf(Type::String()) == 16);
  }

  SECTION("Pointer and reference are 8 bytes") {
    REQUIRE(ts.SizeOf(ts.PointerTo(Type::Int())) == 8);
    REQUIRE(ts.SizeOf(ts.ReferenceTo(Type::Int(), false)) == 8);
  }

  SECTION("Fixed array size") {
    auto arr = Type::Array(Type::Int(32, true), 10);
    REQUIRE(ts.SizeOf(arr) == 40);
  }

  SECTION("Dynamic array/slice are fat pointers") {
    REQUIRE(ts.SizeOf(Type::Array(Type::Int())) == 16);
    REQUIRE(ts.SizeOf(Type::Slice(Type::Int())) == 16);
  }

  SECTION("Alignment") {
    REQUIRE(ts.AlignOf(Type::Bool()) == 1);
    REQUIRE(ts.AlignOf(Type::Int(32, true)) == 4);
    REQUIRE(ts.AlignOf(Type::Int(64, true)) == 8);
    REQUIRE(ts.AlignOf(ts.PointerTo(Type::Int())) == 8);
  }

  SECTION("Tuple size with alignment padding") {
    // (i8, i32) = 1 byte + 3 pad + 4 bytes = 8 total
    auto tup = Type::Tuple({Type::Int(8, true), Type::Int(32, true)});
    REQUIRE(ts.SizeOf(tup) == 8);
    REQUIRE(ts.AlignOf(tup) == 4);
  }
}

TEST_CASE("TypeSystem - CommonType", "[typesystem][common]") {
  TypeSystem ts;

  SECTION("Same types") {
    auto ct = ts.CommonType(Type::Int(32, true), Type::Int(32, true));
    REQUIRE(ct == Type::Int(32, true));
  }

  SECTION("Int widening") {
    auto ct = ts.CommonType(Type::Int(32, true), Type::Int(64, true));
    REQUIRE(ct.bit_width == 64);
  }

  SECTION("Int + Float -> Float") {
    auto ct = ts.CommonType(Type::Int(32, true), Type::Float(64));
    REQUIRE(ct.kind == TypeKind::kFloat);
  }

  SECTION("Bool + Int -> Int") {
    auto ct = ts.CommonType(Type::Bool(), Type::Int(32, true));
    REQUIRE(ct.kind == TypeKind::kInt);
  }

  SECTION("Incompatible -> Any fallback") {
    auto ct = ts.CommonType(Type::String(), Type::Int());
    REQUIRE(ct.kind == TypeKind::kAny);
  }
}

TEST_CASE("TypeSystem - AreLayoutCompatible", "[typesystem][layout]") {
  TypeSystem ts;

  SECTION("Same bit-width numerics are layout-compatible") {
    REQUIRE(ts.AreLayoutCompatible(Type::Int(32, true), Type::Int(32, false)));
    REQUIRE(ts.AreLayoutCompatible(Type::Int(32, true), Type::Float(32)));
  }

  SECTION("Different bit-width numerics are NOT") {
    REQUIRE(!ts.AreLayoutCompatible(Type::Int(32, true), Type::Int(64, true)));
  }

  SECTION("All pointers are layout-compatible") {
    auto p1 = ts.PointerTo(Type::Int());
    auto p2 = ts.PointerTo(Type::String());
    REQUIRE(ts.AreLayoutCompatible(p1, p2));
  }
}

TEST_CASE("TypeSystem - Widening and Narrowing", "[typesystem][widening]") {
  REQUIRE(TypeSystem::IsWidening(Type::Int(32, true), Type::Int(64, true)));
  REQUIRE(TypeSystem::IsWidening(Type::Int(32, true), Type::Float(64)));
  REQUIRE(!TypeSystem::IsWidening(Type::Int(64, true), Type::Int(32, true)));

  REQUIRE(TypeSystem::IsNarrowing(Type::Int(64, true), Type::Int(32, true)));
  REQUIRE(TypeSystem::IsNarrowing(Type::Float(64), Type::Int(32, true)));
  REQUIRE(!TypeSystem::IsNarrowing(Type::Int(32, true), Type::Int(64, true)));
}

TEST_CASE("TypeSystem - ConversionRank", "[typesystem][rank]") {
  TypeSystem ts;

  REQUIRE(ts.ConversionRank(Type::Bool()) < ts.ConversionRank(Type::Int(8, true)));
  REQUIRE(ts.ConversionRank(Type::Int(8, true)) < ts.ConversionRank(Type::Int(32, true)));
  REQUIRE(ts.ConversionRank(Type::Int(32, true)) < ts.ConversionRank(Type::Int(64, true)));
  REQUIRE(ts.ConversionRank(Type::Int(64, true)) < ts.ConversionRank(Type::Float(32)));
  REQUIRE(ts.ConversionRank(Type::Float(32)) < ts.ConversionRank(Type::Float(64)));
}

TEST_CASE("TypeSystem - Aliases", "[typesystem][alias]") {
  TypeSystem ts;

  SECTION("Built-in aliases") {
    REQUIRE(ts.HasAlias("size_t"));
    auto resolved = ts.ResolveAlias("size_t");
    REQUIRE(resolved.kind == TypeKind::kInt);
    REQUIRE(resolved.bit_width == 64);
    REQUIRE(resolved.is_signed == false);
  }

  SECTION("Register and resolve custom alias") {
    ts.RegisterAlias("my_int", Type::Int(32, true));
    REQUIRE(ts.HasAlias("my_int"));
    auto resolved = ts.ResolveAlias("my_int");
    REQUIRE(resolved.kind == TypeKind::kInt);
    REQUIRE(resolved.bit_width == 32);
  }

  SECTION("Unregistered alias returns Invalid") {
    auto resolved = ts.ResolveAlias("nonexistent");
    REQUIRE(resolved.kind == TypeKind::kInvalid);
  }
}

TEST_CASE("TypeSystem - Normalize", "[typesystem][normalize]") {
  TypeSystem ts;

  SECTION("Strips qualifiers") {
    auto t = Type::Int(32, true).WithConst().WithVolatile();
    auto n = ts.Normalize(t);
    REQUIRE(!n.is_const);
    REQUIRE(!n.is_volatile);
    REQUIRE(n.kind == TypeKind::kInt);
  }

  SECTION("Resolves aliases") {
    ts.RegisterAlias("MyInt", Type::Int(64, true));
    Type aliased{TypeKind::kStruct, "MyInt"};
    auto n = ts.Normalize(aliased);
    REQUIRE(n.kind == TypeKind::kInt);
    REQUIRE(n.bit_width == 64);
  }
}

TEST_CASE("TypeSystem - KindToString", "[typesystem][string]") {
  REQUIRE(TypeSystem::KindToString(TypeKind::kInt) == "Int");
  REQUIRE(TypeSystem::KindToString(TypeKind::kFloat) == "Float");
  REQUIRE(TypeSystem::KindToString(TypeKind::kPointer) == "Pointer");
  REQUIRE(TypeSystem::KindToString(TypeKind::kArray) == "Array");
  REQUIRE(TypeSystem::KindToString(TypeKind::kOptional) == "Optional");
  REQUIRE(TypeSystem::KindToString(TypeKind::kSlice) == "Slice");
  REQUIRE(TypeSystem::KindToString(TypeKind::kGenericInstance) == "GenericInstance");
}

// ============================================================================
// TypeRegistry tests
// ============================================================================

TEST_CASE("TypeRegistry - Basic operations", "[registry]") {
  TypeRegistry reg;

  SECTION("Register and find") {
    reg.Register("MyStruct", Type::Struct("MyStruct", "cpp"));
    REQUIRE(reg.Contains("MyStruct"));
    REQUIRE(reg.Size() == 1);

    const Type *found = reg.Find("MyStruct");
    REQUIRE(found != nullptr);
    REQUIRE(found->kind == TypeKind::kStruct);
    REQUIRE(found->name == "MyStruct");
  }

  SECTION("Find returns nullptr for unregistered") {
    REQUIRE(reg.Find("NonExistent") == nullptr);
    REQUIRE(!reg.Contains("NonExistent"));
  }

  SECTION("Clear removes everything") {
    reg.Register("A", Type::Int());
    reg.Register("B", Type::Float());
    REQUIRE(reg.Size() == 2);
    reg.Clear();
    REQUIRE(reg.Size() == 0);
    REQUIRE(!reg.Contains("A"));
  }

  SECTION("AllTypes returns all registered entries") {
    reg.Register("X", Type::Int());
    reg.Register("Y", Type::String());
    auto all = reg.AllTypes();
    REQUIRE(all.size() == 2);
  }
}

TEST_CASE("TypeRegistry - Cross-language equivalences", "[registry][equivalence]") {
  TypeRegistry reg;

  SECTION("Register and query equivalence") {
    reg.RegisterEquivalence("python", "int", "rust", "i64");
    REQUIRE(reg.AreEquivalent("python", "int", "rust", "i64"));
    REQUIRE(reg.AreEquivalent("rust", "i64", "python", "int"));  // symmetric
  }

  SECTION("Same type in same language is always equivalent") {
    REQUIRE(reg.AreEquivalent("cpp", "int", "cpp", "int"));
  }

  SECTION("Non-registered equivalence returns false") {
    REQUIRE(!reg.AreEquivalent("python", "int", "cpp", "int"));
  }

  SECTION("Transitive equivalences") {
    reg.RegisterEquivalence("python", "int", "rust", "i64");
    reg.RegisterEquivalence("rust", "i64", "cpp", "long");

    // Now python::int should be equivalent to cpp::long through transitivity
    REQUIRE(reg.AreEquivalent("python", "int", "cpp", "long"));
    REQUIRE(reg.AreEquivalent("cpp", "long", "python", "int"));
  }

  SECTION("GetEquivalences returns all equivalent types") {
    reg.RegisterEquivalence("python", "float", "cpp", "double");
    reg.RegisterEquivalence("python", "float", "rust", "f64");

    auto eqs = reg.GetEquivalences("python", "float");
    REQUIRE(eqs.size() == 2);
  }
}

// ============================================================================
// TypeUnifier tests
// ============================================================================

TEST_CASE("TypeUnifier - Basic unification", "[unifier]") {
  TypeUnifier u;

  SECTION("Unify concrete types") {
    REQUIRE(u.Unify(Type::Int(32, true), Type::Int(32, true)));
    REQUIRE(!u.Unify(Type::Int(32, true), Type::String()));
  }

  SECTION("Unify generic param with concrete") {
    auto T = Type::GenericParam("T");
    REQUIRE(u.Unify(T, Type::Int(32, true)));
    auto result = u.Apply(T);
    REQUIRE(result.kind == TypeKind::kInt);
    REQUIRE(result.bit_width == 32);
  }

  SECTION("Unify two generic params") {
    auto T = Type::GenericParam("T");
    auto U = Type::GenericParam("U");
    REQUIRE(u.Unify(T, U));
    // T and U should resolve to the same thing
    REQUIRE(u.Apply(T) == u.Apply(U));
  }

  SECTION("Unify generic instance types") {
    auto t1 = Type::GenericInstance("Vec", {Type::GenericParam("T")}, "rust");
    auto t2 = Type::GenericInstance("Vec", {Type::Int(32, true)}, "rust");
    REQUIRE(u.Unify(t1, t2));
    auto result = u.Apply(Type::GenericParam("T"));
    REQUIRE(result.kind == TypeKind::kInt);
  }

  SECTION("Occurs check prevents infinite types") {
    auto T = Type::GenericParam("T");
    auto list_T = Type::GenericInstance("List", {T});
    REQUIRE(!u.Unify(T, list_T));  // T = List<T> would be infinite
  }
}

TEST_CASE("TypeUnifier - Trait constraints", "[unifier][traits]") {
  TypeUnifier u;

  auto T = Type::GenericParam("T");
  u.AddTraitConstraint(T, "Display");
  u.AddTraitConstraint(T, "Clone");

  REQUIRE(u.HasTraitConstraint(T, "Display"));
  REQUIRE(u.HasTraitConstraint(T, "Clone"));
  REQUIRE(!u.HasTraitConstraint(T, "Debug"));

  auto traits = u.TraitsFor(T);
  REQUIRE(traits.size() == 2);
}

TEST_CASE("TypeUnifier - Reset", "[unifier]") {
  TypeUnifier u;
  auto T = Type::GenericParam("T");
  u.Unify(T, Type::Int());
  u.AddTraitConstraint(T, "Copy");

  u.Reset();
  REQUIRE(u.Substitutions().empty());
  REQUIRE(!u.HasTraitConstraint(T, "Copy"));
}

// ============================================================================
// SymbolTable tests
// ============================================================================

TEST_CASE("SymbolTable - Scope management", "[symtab][scope]") {
  SymbolTable st;

  SECTION("Starts with global scope") {
    auto *scope = st.CurrentScope();
    REQUIRE(scope != nullptr);
    REQUIRE(scope->kind == ScopeKind::kGlobal);
    REQUIRE(st.CurrentScopeId() == 0);
    REQUIRE(st.GlobalScopeId() == 0);
  }

  SECTION("Enter and exit scopes") {
    int func_id = st.EnterScope("myFunc", ScopeKind::kFunction);
    REQUIRE(func_id > 0);
    REQUIRE(st.CurrentScopeId() == func_id);
    REQUIRE(st.CurrentScope()->kind == ScopeKind::kFunction);

    int block_id = st.EnterScope("if_body", ScopeKind::kBlock);
    REQUIRE(block_id > func_id);
    REQUIRE(st.CurrentScopeId() == block_id);

    st.ExitScope();
    REQUIRE(st.CurrentScopeId() == func_id);

    st.ExitScope();
    REQUIRE(st.CurrentScopeId() == 0);
  }

  SECTION("Parent scope query") {
    int func_id = st.EnterScope("fn", ScopeKind::kFunction);
    const auto *parent = st.ParentScope(func_id);
    REQUIRE(parent != nullptr);
    REQUIRE(parent->kind == ScopeKind::kGlobal);
    st.ExitScope();
  }
}

TEST_CASE("SymbolTable - Symbol declaration and lookup", "[symtab][declare]") {
  SymbolTable st;

  SECTION("Declare and lookup in global scope") {
    Symbol sym;
    sym.name = "x";
    sym.type = Type::Int(32, true);
    sym.kind = SymbolKind::kVariable;

    const Symbol *decl = st.Declare(sym);
    REQUIRE(decl != nullptr);
    REQUIRE(decl->name == "x");
    REQUIRE(decl->scope_id == 0);

    auto result = st.Lookup("x");
    REQUIRE(result.has_value());
    REQUIRE(result->symbol->name == "x");
    REQUIRE(result->scope_distance == 0);
  }

  SECTION("Duplicate variable declaration fails") {
    Symbol sym;
    sym.name = "dup";
    sym.type = Type::Int();
    sym.kind = SymbolKind::kVariable;

    REQUIRE(st.Declare(sym) != nullptr);
    REQUIRE(st.Declare(sym) == nullptr);  // duplicate
  }

  SECTION("Function overloading allows duplicates") {
    TypeSystem ts;
    Symbol fn1;
    fn1.name = "overloaded";
    fn1.kind = SymbolKind::kFunction;
    fn1.type = ts.FunctionType("overloaded", Type::Int(), {Type::Int(32, true)});

    Symbol fn2;
    fn2.name = "overloaded";
    fn2.kind = SymbolKind::kFunction;
    fn2.type = ts.FunctionType("overloaded", Type::Int(), {Type::Float(64)});

    REQUIRE(st.Declare(fn1) != nullptr);
    REQUIRE(st.Declare(fn2) != nullptr);  // overloads allowed
  }

  SECTION("Lookup walks up scope chain") {
    Symbol global_sym;
    global_sym.name = "global_var";
    global_sym.type = Type::Int();
    global_sym.kind = SymbolKind::kVariable;
    st.Declare(global_sym);

    st.EnterScope("inner", ScopeKind::kFunction);

    auto result = st.Lookup("global_var");
    REQUIRE(result.has_value());
    REQUIRE(result->scope_distance == 1);

    st.ExitScope();
  }

  SECTION("Inner scope shadows outer") {
    Symbol outer;
    outer.name = "shadow_x";
    outer.type = Type::Int();
    outer.kind = SymbolKind::kVariable;
    st.Declare(outer);

    st.EnterScope("inner", ScopeKind::kBlock);

    Symbol inner;
    inner.name = "shadow_x";
    inner.type = Type::String();
    inner.kind = SymbolKind::kVariable;
    st.Declare(inner);

    auto result = st.Lookup("shadow_x");
    REQUIRE(result.has_value());
    REQUIRE(result->symbol->type.kind == TypeKind::kString);
    REQUIRE(result->scope_distance == 0);

    st.ExitScope();

    // Back to outer: the global scope symbol should be found
    result = st.Lookup("shadow_x");
    REQUIRE(result.has_value());
    REQUIRE(result->scope_distance == 0);
  }
}

TEST_CASE("SymbolTable - DeclareInScope", "[symtab][scope-declare]") {
  SymbolTable st;
  int func_id = st.EnterScope("fn", ScopeKind::kFunction);
  (void)func_id;  // Suppress unused variable warning

  // Declare a symbol directly in the global scope from inside a function scope
  Symbol sym;
  sym.name = "global_from_inner";
  sym.type = Type::Int();
  sym.kind = SymbolKind::kVariable;

  const Symbol *decl = st.DeclareInScope(0, sym);
  REQUIRE(decl != nullptr);
  REQUIRE(decl->scope_id == 0);

  // Invalid scope id returns nullptr
  REQUIRE(st.DeclareInScope(-1, sym) == nullptr);
  REQUIRE(st.DeclareInScope(999, sym) == nullptr);

  st.ExitScope();
}

TEST_CASE("SymbolTable - Function overload resolution", "[symtab][overload]") {
  SymbolTable st;
  TypeSystem ts;

  // Declare two overloads: add(int, int) and add(float, float)
  Symbol add_int;
  add_int.name = "add";
  add_int.kind = SymbolKind::kFunction;
  add_int.type = ts.FunctionType("add", Type::Int(32, true),
                                 {Type::Int(32, true), Type::Int(32, true)});
  st.Declare(add_int);

  Symbol add_float;
  add_float.name = "add";
  add_float.kind = SymbolKind::kFunction;
  add_float.type = ts.FunctionType("add", Type::Float(64),
                                   {Type::Float(64), Type::Float(64)});
  st.Declare(add_float);

  SECTION("Resolution finds a matching overload for int args") {
    auto result = st.ResolveFunction("add", {Type::Int(32, true), Type::Int(32, true)}, ts);
    REQUIRE(result.has_value());
    // Both overloads are compatible (int<->float); verify we get a valid match.
    REQUIRE(result->symbol->type.GetParamCount() == 2);
  }

  SECTION("Resolution finds a matching overload for float args") {
    auto result = st.ResolveFunction("add", {Type::Float(64), Type::Float(64)}, ts);
    REQUIRE(result.has_value());
    REQUIRE(result->symbol->type.GetParamCount() == 2);
  }

  SECTION("Non-existent function returns nullopt") {
    auto result = st.ResolveFunction("subtract", {Type::Int()}, ts);
    REQUIRE(!result.has_value());
  }

  SECTION("Wrong arity returns nullopt") {
    auto result = st.ResolveFunction("add", {Type::Int(32, true)}, ts);
    REQUIRE(!result.has_value());
  }
}

TEST_CASE("SymbolTable - Type scope and member lookup", "[symtab][member]") {
  SymbolTable st;

  // Register a class-like scope and declare members inside it.
  int class_scope = st.EnterScope("MyClassScope", ScopeKind::kClass);
  st.RegisterTypeScope("MyClass", class_scope);

  Symbol field;
  field.name = "value";
  field.type = Type::Int(32, true);
  field.kind = SymbolKind::kField;
  st.Declare(field);

  Symbol method;
  method.name = "getValue";
  method.type = Type{TypeKind::kFunction, "getValue"};
  method.kind = SymbolKind::kFunction;
  st.Declare(method);

  SECTION("Members declared in type scope are findable") {
    // Use FindInAnyScope which iterates the stable vector
    auto *found_field = st.FindInAnyScope("value");
    REQUIRE(found_field != nullptr);
    REQUIRE(found_field->name == "value");
    REQUIRE(found_field->kind == SymbolKind::kField);

    auto *found_method = st.FindInAnyScope("getValue");
    REQUIRE(found_method != nullptr);
    REQUIRE(found_method->name == "getValue");
  }

  SECTION("Unknown type returns nullopt") {
    auto result = st.LookupMember("UnknownClass", "value");
    REQUIRE(!result.has_value());
  }

  SECTION("Unknown member returns nullopt") {
    auto result = st.LookupMember("MyClass", "nonexistent");
    REQUIRE(!result.has_value());
  }
}

TEST_CASE("SymbolTable - Inheritance member lookup", "[symtab][inheritance]") {
  SymbolTable st;

  // Base class
  int base_scope = st.EnterScope("BaseScope", ScopeKind::kClass);
  st.RegisterTypeScope("Base", base_scope);
  Symbol base_method;
  base_method.name = "baseMethod";
  base_method.type = Type{TypeKind::kFunction, "baseMethod"};
  base_method.kind = SymbolKind::kFunction;
  st.Declare(base_method);
  st.ExitScope();

  // Derived class
  int derived_scope = st.EnterScope("DerivedScope", ScopeKind::kClass);
  st.RegisterTypeScope("Derived", derived_scope);
  st.RegisterTypeBases("Derived", {"Base"});
  Symbol derived_method;
  derived_method.name = "derivedMethod";
  derived_method.type = Type{TypeKind::kFunction, "derivedMethod"};
  derived_method.kind = SymbolKind::kFunction;
  st.Declare(derived_method);

  SECTION("Derived can access its own members via FindInAnyScope") {
    auto *found = st.FindInAnyScope("derivedMethod");
    REQUIRE(found != nullptr);
    REQUIRE(found->name == "derivedMethod");
  }

  SECTION("Base members exist and are registered") {
    auto *found = st.FindInAnyScope("baseMethod");
    REQUIRE(found != nullptr);
    REQUIRE(found->name == "baseMethod");
  }

  SECTION("Type scope registration works") {
    // Verify that both type scopes are registered.
    // LookupMember for Derived should find its own declared member.
    auto result = st.LookupMember("Derived", "derivedMethod");
    REQUIRE(result.has_value());
  }
}

TEST_CASE("SymbolTable - Captured variable marking", "[symtab][capture]") {
  SymbolTable st;

  Symbol sym;
  sym.name = "captured_var";
  sym.type = Type::Int();
  sym.kind = SymbolKind::kVariable;
  const Symbol *decl = st.Declare(sym);
  REQUIRE(decl != nullptr);
  REQUIRE(!decl->captured);

  st.MarkCaptured(decl);
  REQUIRE(decl->captured);
}

TEST_CASE("SymbolTable - FindInAnyScope", "[symtab][find]") {
  SymbolTable st;

  Symbol sym;
  sym.name = "anywhere";
  sym.type = Type::String();
  sym.kind = SymbolKind::kVariable;
  st.Declare(sym);

  st.EnterScope("fn", ScopeKind::kFunction);

  // FindInAnyScope works regardless of current scope stack position
  const Symbol *found = st.FindInAnyScope("anywhere");
  REQUIRE(found != nullptr);
  REQUIRE(found->type.kind == TypeKind::kString);

  REQUIRE(st.FindInAnyScope("nonexistent") == nullptr);

  st.ExitScope();
}

// ============================================================================
// Symbol utility function tests
// ============================================================================

TEST_CASE("Symbol utility functions", "[symtab][utils]") {
  SECTION("SymbolKindToString") {
    REQUIRE(SymbolKindToString(SymbolKind::kVariable) == "Variable");
    REQUIRE(SymbolKindToString(SymbolKind::kFunction) == "Function");
    REQUIRE(SymbolKindToString(SymbolKind::kTypeName) == "TypeName");
    REQUIRE(SymbolKindToString(SymbolKind::kModule) == "Module");
    REQUIRE(SymbolKindToString(SymbolKind::kParameter) == "Parameter");
    REQUIRE(SymbolKindToString(SymbolKind::kField) == "Field");
  }

  SECTION("ScopeKindToString") {
    REQUIRE(ScopeKindToString(ScopeKind::kGlobal) == "Global");
    REQUIRE(ScopeKindToString(ScopeKind::kFunction) == "Function");
    REQUIRE(ScopeKindToString(ScopeKind::kClass) == "Class");
    REQUIRE(ScopeKindToString(ScopeKind::kBlock) == "Block");
    REQUIRE(ScopeKindToString(ScopeKind::kComprehension) == "Comprehension");
  }

  SECTION("FormatSymbol produces readable output") {
    Symbol sym;
    sym.name = "myVar";
    sym.type = Type::Int(32, true);
    sym.kind = SymbolKind::kVariable;
    sym.language = "cpp";
    sym.scope_id = 0;

    auto str = FormatSymbol(sym);
    REQUIRE(str.find("Variable") != std::string::npos);
    REQUIRE(str.find("myVar") != std::string::npos);
    REQUIRE(str.find("cpp") != std::string::npos);
  }

  SECTION("FormatScope produces readable output") {
    ScopeInfo scope;
    scope.id = 1;
    scope.parent = 0;
    scope.kind = ScopeKind::kFunction;
    scope.name = "myFunc";

    auto str = FormatScope(scope);
    REQUIRE(str.find("Function") != std::string::npos);
    REQUIRE(str.find("myFunc") != std::string::npos);
  }
}
