// Template Instantiation System Tests
// ====================================
// Comprehensive tests for the template instantiation system including:
// - Class template instantiation with AST traversal and substitution
// - Function template instantiation with parameter deduction
// - Partial and explicit specialization matching
// - Variadic templates (parameter packs)
// - Complex type pattern matching

#include <catch2/catch_test_macros.hpp>

#include "middle/include/ir/template_instantiator.h"

using namespace polyglot::ir;

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

// Create a simple typename template parameter
TemplateParameter MakeTypenameParam(const std::string& name, 
                                     const std::string& default_val = "") {
  TemplateParameter param;
  param.name = name;
  param.is_typename = true;
  param.default_value = default_val;
  return param;
}

// Create a non-type template parameter (e.g., int N)
TemplateParameter MakeNonTypeParam(const std::string& name, 
                                    const IRType& type,
                                    const std::string& default_val = "") {
  TemplateParameter param;
  param.name = name;
  param.is_typename = false;
  param.type = type;
  param.default_value = default_val;
  return param;
}

// Create a variadic typename parameter (typename... Args)
TemplateParameter MakeVariadicParam(const std::string& name) {
  TemplateParameter param;
  param.name = name;
  param.is_typename = true;
  param.is_parameter_pack = true;
  return param;
}

// Create a type argument from an IRType
TemplateArgument MakeTypeArg(const IRType& type) {
  return TemplateArgument::Type(type);
}

// Create a value argument
TemplateArgument MakeValueArg(const std::string& value) {
  return TemplateArgument::Value(value);
}

// Create a placeholder type for template parameters
IRType MakeParamType(const std::string& name) {
  IRType t;
  t.kind = IRTypeKind::kStruct;
  t.name = name;
  return t;
}

}  // anonymous namespace

// ============================================================================
// Test 1: Basic Class Template Registration and Instantiation
// ============================================================================

TEST_CASE("Template System - Class Template Registration", "[template]") {
  TemplateInstantiator instantiator;
  
  SECTION("Register and check class template") {
    // template<typename T>
    // class Container { ... };
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T")
    };
    
    int dummy_ast = 42;  // Placeholder AST
    instantiator.RegisterClassTemplate("Container", params, &dummy_ast);
    
    REQUIRE(instantiator.HasClassTemplate("Container"));
    REQUIRE_FALSE(instantiator.HasClassTemplate("NonExistent"));
    
    const auto* retrieved_params = instantiator.GetClassTemplateParams("Container");
    REQUIRE(retrieved_params != nullptr);
    REQUIRE(retrieved_params->size() == 1);
    REQUIRE((*retrieved_params)[0].name == "T");
  }
  
  SECTION("Instantiate class template with type argument") {
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Vector", params, &dummy_ast);
    
    // Instantiate Vector<int>
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32())
    };
    
    std::string name = instantiator.InstantiateClass("Vector", args);
    REQUIRE(name == "Vector<int>");
    
    // Instantiate Vector<double>
    args = { MakeTypeArg(IRType::F64()) };
    name = instantiator.InstantiateClass("Vector", args);
    REQUIRE(name == "Vector<double>");
  }
  
  SECTION("Class template with multiple parameters") {
    // template<typename K, typename V>
    // class Map { ... };
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("K"),
      MakeTypenameParam("V")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Map", params, &dummy_ast);
    
    // Instantiate Map<int, double>
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32()),
      MakeTypeArg(IRType::F64())
    };
    
    std::string name = instantiator.InstantiateClass("Map", args);
    REQUIRE(name == "Map<int, double>");
  }
}

// ============================================================================
// Test 2: Function Template Registration and Instantiation
// ============================================================================

TEST_CASE("Template System - Function Template Registration", "[template]") {
  TemplateInstantiator instantiator;
  
  SECTION("Register and instantiate function template") {
    // template<typename T>
    // T identity(T x);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T")
    };
    
    // Function parameter types for deduction
    std::vector<IRType> func_params = { MakeParamType("T") };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("identity", params, &dummy_ast, func_params);
    
    REQUIRE(instantiator.HasFunctionTemplate("identity"));
    
    // Instantiate identity<int>
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32())
    };
    
    std::string name = instantiator.InstantiateFunction("identity", args);
    REQUIRE(name == "identity<int>");
  }
  
  SECTION("Function template with non-type parameter") {
    // template<typename T, int N>
    // T process(T x);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeNonTypeParam("N", IRType::I32())
    };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("process", params, &dummy_ast);
    
    // Instantiate process<int, 5>
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32()),
      MakeValueArg("5")
    };
    
    std::string name = instantiator.InstantiateFunction("process", args);
    REQUIRE(name == "process<int, 5>");
  }
}

// ============================================================================
// Test 3: Template Argument Deduction
// ============================================================================

TEST_CASE("Template System - Argument Deduction", "[template][deduction]") {
  TemplateInstantiator instantiator;
  
  SECTION("Deduce single type parameter") {
    // template<typename T>
    // void func(T x);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T")
    };
    
    // Function takes one parameter of type T
    std::vector<IRType> func_params = { MakeParamType("T") };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("func", params, &dummy_ast, func_params);
    
    // Call with int argument
    std::vector<IRType> call_args = { IRType::I32() };
    
    auto result = instantiator.DeduceTemplateArguments("func", call_args);
    REQUIRE(result.success);
    REQUIRE(result.deduced_args.size() == 1);
    REQUIRE(result.deduced_args[0].is_type);
    REQUIRE(result.deduced_args[0].type == IRType::I32());
    
    // Check deduction map
    REQUIRE(result.deduction_map.count("T") > 0);
    REQUIRE(result.deduction_map.at("T").type == IRType::I32());
  }
  
  SECTION("Deduce multiple type parameters") {
    // template<typename T, typename U>
    // void func(T x, U y);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeTypenameParam("U")
    };
    
    std::vector<IRType> func_params = { 
      MakeParamType("T"), 
      MakeParamType("U") 
    };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("func2", params, &dummy_ast, func_params);
    
    // Call with int and double arguments
    std::vector<IRType> call_args = { IRType::I32(), IRType::F64() };
    
    auto result = instantiator.DeduceTemplateArguments("func2", call_args);
    REQUIRE(result.success);
    REQUIRE(result.deduced_args.size() == 2);
    REQUIRE(result.deduced_args[0].type == IRType::I32());
    REQUIRE(result.deduced_args[1].type == IRType::F64());
  }
  
  SECTION("Deduction from pointer type") {
    // template<typename T>
    // void func(T* ptr);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T")
    };
    
    // T* pattern
    IRType ptr_pattern = IRType::Pointer(MakeParamType("T"));
    std::vector<IRType> func_params = { ptr_pattern };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("func_ptr", params, &dummy_ast, func_params);
    
    // Call with int* argument
    std::vector<IRType> call_args = { IRType::Pointer(IRType::I32()) };
    
    auto result = instantiator.DeduceTemplateArguments("func_ptr", call_args);
    REQUIRE(result.success);
    REQUIRE(result.deduced_args.size() == 1);
    REQUIRE(result.deduced_args[0].type == IRType::I32());
  }
  
  SECTION("Deduction with default parameter") {
    // template<typename T, typename U = int>
    // void func(T x);
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeTypenameParam("U", "int")  // default = int
    };
    
    std::vector<IRType> func_params = { MakeParamType("T") };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("func_default", params, &dummy_ast, func_params);
    
    // Call with just double (U should use default)
    std::vector<IRType> call_args = { IRType::F64() };
    
    auto result = instantiator.DeduceTemplateArguments("func_default", call_args);
    REQUIRE(result.success);
    REQUIRE(result.deduced_args.size() == 2);
    REQUIRE(result.deduced_args[0].type == IRType::F64());
    // Second arg uses default
    REQUIRE(result.deduced_args[1].type.name == "int");
  }
}

// ============================================================================
// Test 4: Template Specialization
// ============================================================================

TEST_CASE("Template System - Specialization Matching", "[template][specialization]") {
  TemplateInstantiator instantiator;
  
  SECTION("Explicit specialization") {
    // Primary template
    // template<typename T> class Traits { ... };
    std::vector<TemplateParameter> params = { MakeTypenameParam("T") };
    int primary_ast = 1;
    instantiator.RegisterClassTemplate("Traits", params, &primary_ast);
    
    // Explicit specialization for int
    // template<> class Traits<int> { ... };
    std::vector<TemplateArgument> int_pattern = { MakeTypeArg(IRType::I32()) };
    int int_spec_ast = 2;
    instantiator.RegisterSpecialization("Traits", int_pattern, {}, &int_spec_ast, false);
    
    // Instantiate Traits<int> - should use specialization
    std::string name = instantiator.InstantiateClass("Traits", int_pattern);
    REQUIRE(name == "Traits<int>");
    
    // Find specialization
    const auto* spec = instantiator.FindBestSpecialization("Traits", int_pattern);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->specialized_ast == &int_spec_ast);
    
    // Instantiate Traits<double> - should use primary
    std::vector<TemplateArgument> double_args = { MakeTypeArg(IRType::F64()) };
    name = instantiator.InstantiateClass("Traits", double_args);
    REQUIRE(name == "Traits<double>");
    
    spec = instantiator.FindBestSpecialization("Traits", double_args);
    REQUIRE(spec == nullptr);  // No specialization for double
  }
  
  SECTION("Partial specialization") {
    // Primary template
    // template<typename T, typename U> class Pair { ... };
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeTypenameParam("U")
    };
    int primary_ast = 1;
    instantiator.RegisterClassTemplate("Pair", params, &primary_ast);
    
    // Partial specialization for T == U
    // template<typename T> class Pair<T, T> { ... };
    std::vector<TemplateParameter> spec_params = { MakeTypenameParam("T") };
    std::vector<TemplateArgument> same_pattern = {
      MakeTypeArg(MakeParamType("T")),
      MakeTypeArg(MakeParamType("T"))
    };
    int same_ast = 2;
    instantiator.RegisterSpecialization("Pair", same_pattern, spec_params, &same_ast, true);
    
    // Partial specialization for Pair<T, int>
    // template<typename T> class Pair<T, int> { ... };
    std::vector<TemplateArgument> int_pattern = {
      MakeTypeArg(MakeParamType("T")),
      MakeTypeArg(IRType::I32())
    };
    int int_ast = 3;
    instantiator.RegisterSpecialization("Pair", int_pattern, spec_params, &int_ast, true);
    
    // Test: Pair<int, int> should match both partial specs
    // Pair<T, int> is more specific than Pair<T, T>
    std::vector<TemplateArgument> int_int_args = {
      MakeTypeArg(IRType::I32()),
      MakeTypeArg(IRType::I32())
    };
    
    const auto* spec = instantiator.FindBestSpecialization("Pair", int_int_args);
    REQUIRE(spec != nullptr);
    // Should pick Pair<T, int> because it has a concrete type
    REQUIRE(spec->specialized_ast == &int_ast);
    
    // Test: Pair<double, double> should match Pair<T, T>
    std::vector<TemplateArgument> double_double_args = {
      MakeTypeArg(IRType::F64()),
      MakeTypeArg(IRType::F64())
    };
    
    spec = instantiator.FindBestSpecialization("Pair", double_double_args);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->specialized_ast == &same_ast);
  }
}

// ============================================================================
// Test 5: Type Substitution
// ============================================================================

TEST_CASE("Template System - Type Substitution", "[template][substitution]") {
  TemplateInstantiator instantiator;
  
  SECTION("Substitute simple type") {
    // Substitute T with int
    std::unordered_map<std::string, TemplateArgument> subs;
    subs["T"] = MakeTypeArg(IRType::I32());
    
    IRType param_type = MakeParamType("T");
    auto result = instantiator.SubstituteType(param_type, subs);
    
    REQUIRE(result.success);
    REQUIRE(result.result_type == IRType::I32());
  }
  
  SECTION("Substitute pointer type") {
    // Substitute T* where T = int, should get int*
    std::unordered_map<std::string, TemplateArgument> subs;
    subs["T"] = MakeTypeArg(IRType::I32());
    
    IRType ptr_type = IRType::Pointer(MakeParamType("T"));
    auto result = instantiator.SubstituteType(ptr_type, subs);
    
    REQUIRE(result.success);
    REQUIRE(result.result_type.kind == IRTypeKind::kPointer);
    REQUIRE(result.result_type.subtypes[0] == IRType::I32());
  }
  
  SECTION("Substitute nested types") {
    // Substitute T** where T = double
    std::unordered_map<std::string, TemplateArgument> subs;
    subs["T"] = MakeTypeArg(IRType::F64());
    
    IRType nested = IRType::Pointer(IRType::Pointer(MakeParamType("T")));
    auto result = instantiator.SubstituteType(nested, subs);
    
    REQUIRE(result.success);
    REQUIRE(result.result_type.kind == IRTypeKind::kPointer);
    REQUIRE(result.result_type.subtypes[0].kind == IRTypeKind::kPointer);
    REQUIRE(result.result_type.subtypes[0].subtypes[0] == IRType::F64());
  }
  
  SECTION("Substitute array type") {
    // Substitute T[10] where T = int
    std::unordered_map<std::string, TemplateArgument> subs;
    subs["T"] = MakeTypeArg(IRType::I32());
    
    IRType array_type = IRType::Array(MakeParamType("T"), 10);
    auto result = instantiator.SubstituteType(array_type, subs);
    
    REQUIRE(result.success);
    REQUIRE(result.result_type.kind == IRTypeKind::kArray);
    REQUIRE(result.result_type.count == 10);
    REQUIRE(result.result_type.subtypes[0] == IRType::I32());
  }
}

// ============================================================================
// Test 6: Complex Type Pattern Matching
// ============================================================================

TEST_CASE("Template System - Pattern Matching", "[template][pattern]") {
  TemplateInstantiator instantiator;
  
  SECTION("Match simple type pattern") {
    // Pattern: T, Argument: int
    std::vector<TemplateArgument> pattern = { MakeTypeArg(MakeParamType("T")) };
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    
    std::unordered_map<std::string, TemplateArgument> bindings;
    bool matches = instantiator.MatchesPattern(args, pattern, &bindings);
    
    REQUIRE(matches);
    REQUIRE(bindings.count("T") > 0);
    REQUIRE(bindings["T"].type == IRType::I32());
  }
  
  SECTION("Match exact type pattern") {
    // Pattern: int, Argument: int
    std::vector<TemplateArgument> pattern = { MakeTypeArg(IRType::I32()) };
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    
    bool matches = instantiator.MatchesPattern(args, pattern, nullptr);
    REQUIRE(matches);
    
    // Pattern: int, Argument: double - should not match
    args = { MakeTypeArg(IRType::F64()) };
    matches = instantiator.MatchesPattern(args, pattern, nullptr);
    REQUIRE_FALSE(matches);
  }
  
  SECTION("Match non-type value pattern") {
    // Pattern: 5, Argument: 5
    std::vector<TemplateArgument> pattern = { MakeValueArg("5") };
    std::vector<TemplateArgument> args = { MakeValueArg("5") };
    
    bool matches = instantiator.MatchesPattern(args, pattern, nullptr);
    REQUIRE(matches);
    
    // Pattern: 5, Argument: 10 - should not match
    args = { MakeValueArg("10") };
    matches = instantiator.MatchesPattern(args, pattern, nullptr);
    REQUIRE_FALSE(matches);
  }
  
  SECTION("Match mixed pattern") {
    // Pattern: (T, 5), Argument: (int, 5)
    std::vector<TemplateArgument> pattern = {
      MakeTypeArg(MakeParamType("T")),
      MakeValueArg("5")
    };
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32()),
      MakeValueArg("5")
    };
    
    std::unordered_map<std::string, TemplateArgument> bindings;
    bool matches = instantiator.MatchesPattern(args, pattern, &bindings);
    
    REQUIRE(matches);
    REQUIRE(bindings["T"].type == IRType::I32());
  }
}

// ============================================================================
// Test 7: Variadic Templates (Parameter Packs)
// ============================================================================

TEST_CASE("Template System - Variadic Templates", "[template][variadic]") {
  TemplateInstantiator instantiator;
  
  SECTION("Register variadic template") {
    // template<typename... Args>
    // class Tuple { ... };
    std::vector<TemplateParameter> params = {
      MakeVariadicParam("Args")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Tuple", params, &dummy_ast);
    
    REQUIRE(instantiator.HasClassTemplate("Tuple"));
    
    const auto* retrieved = instantiator.GetClassTemplateParams("Tuple");
    REQUIRE(retrieved != nullptr);
    REQUIRE((*retrieved)[0].is_parameter_pack);
  }
  
  SECTION("Instantiate variadic template") {
    std::vector<TemplateParameter> params = {
      MakeVariadicParam("Args")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Tuple", params, &dummy_ast);
    
    // Instantiate Tuple<int, double, float>
    std::vector<TemplateArgument> pack_contents = {
      MakeTypeArg(IRType::I32()),
      MakeTypeArg(IRType::F64()),
      MakeTypeArg(IRType::F32())
    };
    std::vector<TemplateArgument> args = {
      TemplateArgument::Pack(pack_contents)
    };
    
    std::string name = instantiator.InstantiateClass("Tuple", args);
    REQUIRE(name == "Tuple<int, double, float>");
  }
  
  SECTION("Mixed variadic and regular parameters") {
    // template<typename T, typename... Rest>
    // class List { ... };
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeVariadicParam("Rest")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("List", params, &dummy_ast);
    
    // Instantiate List<int, double, float>
    std::vector<TemplateArgument> rest_pack = {
      MakeTypeArg(IRType::F64()),
      MakeTypeArg(IRType::F32())
    };
    std::vector<TemplateArgument> args = {
      MakeTypeArg(IRType::I32()),
      TemplateArgument::Pack(rest_pack)
    };
    
    std::string name = instantiator.InstantiateClass("List", args);
    REQUIRE(name == "List<int, double, float>");
  }
}

// ============================================================================
// Test 8: Template Caching
// ============================================================================

TEST_CASE("Template System - Instantiation Caching", "[template][cache]") {
  TemplateInstantiator instantiator;
  
  SECTION("Cache hit for same arguments") {
    std::vector<TemplateParameter> params = { MakeTypenameParam("T") };
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Box", params, &dummy_ast);
    
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    
    // First instantiation
    std::string name1 = instantiator.InstantiateClass("Box", args);
    
    // Second instantiation - should be cached
    std::string name2 = instantiator.InstantiateClass("Box", args);
    
    REQUIRE(name1 == name2);
    REQUIRE(name1 == "Box<int>");
  }
  
  SECTION("Clear cache") {
    std::vector<TemplateParameter> params = { MakeTypenameParam("T") };
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Box", params, &dummy_ast);
    
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    instantiator.InstantiateClass("Box", args);
    
    // Clear cache
    instantiator.ClearCaches();
    
    // Should still work (will re-instantiate)
    std::string name = instantiator.InstantiateClass("Box", args);
    REQUIRE(name == "Box<int>");
  }
}

// ============================================================================
// Test 9: Error Handling
// ============================================================================

TEST_CASE("Template System - Error Handling", "[template][errors]") {
  TemplateInstantiator instantiator;
  
  SECTION("Instantiate non-existent template") {
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    
    std::string name = instantiator.InstantiateClass("NonExistent", args);
    REQUIRE(name.empty());
    
    name = instantiator.InstantiateFunction("NonExistent", args);
    REQUIRE(name.empty());
  }
  
  SECTION("Too few template arguments") {
    // template<typename T, typename U>
    std::vector<TemplateParameter> params = {
      MakeTypenameParam("T"),
      MakeTypenameParam("U")
    };
    
    int dummy_ast = 42;
    instantiator.RegisterClassTemplate("Pair", params, &dummy_ast);
    
    // Try with only one argument
    std::vector<TemplateArgument> args = { MakeTypeArg(IRType::I32()) };
    
    std::string name = instantiator.InstantiateClass("Pair", args);
    REQUIRE(name.empty());  // Should fail
  }
  
  SECTION("Deduction failure") {
    // template<typename T>
    // void func(T x);
    std::vector<TemplateParameter> params = { MakeTypenameParam("T") };
    std::vector<IRType> func_params = { MakeParamType("T") };
    
    int dummy_ast = 42;
    instantiator.RegisterFunctionTemplate("func", params, &dummy_ast, func_params);
    
    // Call with no arguments - cannot deduce T
    std::vector<IRType> call_args = {};
    
    auto result = instantiator.DeduceTemplateArguments("func", call_args);
    REQUIRE_FALSE(result.success);
  }
}

// ============================================================================
// Test 10: TypeToString Conversion
// ============================================================================

TEST_CASE("Template System - Type String Conversion", "[template][util]") {
  TemplateInstantiator instantiator;
  
  SECTION("Primitive types") {
    REQUIRE(instantiator.TypeToString(IRType::I32()) == "int");
    REQUIRE(instantiator.TypeToString(IRType::F64()) == "double");
    REQUIRE(instantiator.TypeToString(IRType::I1()) == "bool");
    REQUIRE(instantiator.TypeToString(IRType::Void()) == "void");
  }
  
  SECTION("Pointer types") {
    IRType int_ptr = IRType::Pointer(IRType::I32());
    REQUIRE(instantiator.TypeToString(int_ptr) == "int*");
    
    IRType double_ptr_ptr = IRType::Pointer(IRType::Pointer(IRType::F64()));
    REQUIRE(instantiator.TypeToString(double_ptr_ptr) == "double**");
  }
  
  SECTION("Array types") {
    IRType int_array = IRType::Array(IRType::I32(), 10);
    REQUIRE(instantiator.TypeToString(int_array) == "int[10]");
  }
  
  SECTION("Struct types") {
    IRType my_struct = IRType::Struct("MyClass", {});
    REQUIRE(instantiator.TypeToString(my_struct) == "MyClass");
  }
}
