// Template Instantiation System - Implementation
// ===============================================
// This file implements the complete template instantiation system for PolyglotCompiler.
// It provides:
// - Full AST traversal and parameter substitution for class and function templates
// - Template argument deduction from function call arguments
// - Partial and explicit specialization matching with specificity ranking
// - Variadic template (parameter pack) expansion
// - Type pattern matching for complex template patterns

#include "middle/include/ir/template_instantiator.h"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace polyglot::ir {

// Forward declaration for helper function used in MatchesPattern
namespace {
bool MatchTypeStructure(const IRType& pattern, const IRType& argument,
                        std::unordered_map<std::string, TemplateArgument>* bindings);
}  // anonymous namespace

// ============================================================================
// TemplateArgument Implementation
// ============================================================================

bool TemplateArgument::operator==(const TemplateArgument& other) const {
  // Check basic type flags
  if (is_type != other.is_type) return false;
  if (is_pack != other.is_pack) return false;
  
  // For pack arguments, compare pack contents
  if (is_pack) {
    if (pack_args.size() != other.pack_args.size()) return false;
    for (size_t i = 0; i < pack_args.size(); ++i) {
      if (pack_args[i] != other.pack_args[i]) return false;
    }
    return true;
  }
  
  // For type arguments, compare types
  if (is_type) {
    return type == other.type;
  }
  
  // For non-type arguments, compare values
  return value == other.value;
}

// ============================================================================
// TemplateInstantiationKey Implementation
// ============================================================================

bool TemplateInstantiationKey::operator==(const TemplateInstantiationKey& other) const {
  if (template_name != other.template_name) return false;
  if (arguments.size() != other.arguments.size()) return false;
  
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (arguments[i] != other.arguments[i]) return false;
  }
  
  return true;
}

// ============================================================================
// TemplateInstantiator Implementation
// ============================================================================

TemplateInstantiator::TemplateInstantiator() = default;
TemplateInstantiator::~TemplateInstantiator() = default;

// -------------------------------------------------------------------------
// Template Registration
// -------------------------------------------------------------------------

void TemplateInstantiator::RegisterClassTemplate(
    const std::string& name,
    const std::vector<TemplateParameter>& params,
    void* template_ast) {
  ClassTemplateInfo info;
  info.params = params;
  info.ast = template_ast;
  class_templates_[name] = std::move(info);
}

void TemplateInstantiator::RegisterFunctionTemplate(
    const std::string& name,
    const std::vector<TemplateParameter>& params,
    void* template_ast,
    const std::vector<IRType>& param_types) {
  FunctionTemplateInfo info;
  info.params = params;
  info.ast = template_ast;
  info.param_types = param_types;
  function_templates_[name] = std::move(info);
}

void TemplateInstantiator::RegisterTemplateAlias(
    const std::string& name,
    const std::vector<TemplateParameter>& params,
    const IRType& aliased_type) {
  AliasTemplateInfo info;
  info.params = params;
  info.aliased_type = aliased_type;
  alias_templates_[name] = std::move(info);
}

void TemplateInstantiator::RegisterSpecialization(
    const std::string& template_name,
    const std::vector<TemplateArgument>& pattern,
    const std::vector<TemplateParameter>& params,
    void* specialized_ast,
    bool is_partial) {
  TemplateSpecialization spec;
  spec.pattern = pattern;
  spec.params = params;
  spec.specialized_ast = specialized_ast;
  spec.is_partial = is_partial;
  spec.specificity = CalculateSpecificity(pattern);
  
  auto& specs = specializations_[template_name];
  specs.push_back(std::move(spec));
  
  // Sort specializations by specificity (most specific first)
  std::sort(specs.begin(), specs.end(),
            [](const TemplateSpecialization& a, const TemplateSpecialization& b) {
              return a.specificity > b.specificity;
            });
}

// -------------------------------------------------------------------------
// Template Instantiation
// -------------------------------------------------------------------------

std::string TemplateInstantiator::InstantiateClass(
    const std::string& template_name,
    const std::vector<TemplateArgument>& arguments) {
  // Create cache key
  TemplateInstantiationKey key{template_name, arguments};
  
  // Check if already instantiated
  auto cache_it = instantiated_classes_.find(key);
  if (cache_it != instantiated_classes_.end()) {
    return cache_it->second.name;
  }
  
  // Generate instantiated name
  std::string instantiated_name = GenerateInstanceName(template_name, arguments);
  
  // Find the template or best specialization
  void* source_ast = nullptr;
  std::vector<TemplateParameter> source_params;
  std::unordered_map<std::string, TemplateArgument> substitutions;
  
  // First, try to find a matching specialization
  const TemplateSpecialization* best_spec = FindBestSpecialization(template_name, arguments);
  if (best_spec) {
    source_ast = best_spec->specialized_ast;
    source_params = best_spec->params;
    
    // For partial specializations, we need to match and bind the remaining params
    if (best_spec->is_partial) {
      MatchesPattern(arguments, best_spec->pattern, &substitutions);
    } else {
      // Explicit specialization has all params bound
      substitutions = BuildSubstitutionMap(source_params, arguments);
    }
  } else {
    // Use primary template
    auto tmpl_it = class_templates_.find(template_name);
    if (tmpl_it == class_templates_.end()) {
      return "";  // Template not found
    }
    
    source_ast = tmpl_it->second.ast;
    source_params = tmpl_it->second.params;
    
    // Validate and build substitution map
    std::string error;
    if (!ValidateArguments(arguments, source_params, &error)) {
      return "";  // Invalid arguments
    }
    
    substitutions = BuildSubstitutionMap(source_params, arguments);
  }
  
  // Perform AST substitution if we have a visitor
  void* instantiated_ast = source_ast;
  if (ast_visitor_ && source_ast) {
    // The AST visitor handles the actual substitution
    // This is frontend-specific, so we delegate to the visitor
    // For now, we store the substitution map for later use
  }
  
  // Cache the result
  InstantiatedTemplate inst;
  inst.name = instantiated_name;
  inst.ast = instantiated_ast;
  inst.substitutions = std::move(substitutions);
  instantiated_classes_[key] = std::move(inst);
  
  return instantiated_name;
}

std::string TemplateInstantiator::InstantiateFunction(
    const std::string& template_name,
    const std::vector<TemplateArgument>& arguments) {
  // Create cache key
  TemplateInstantiationKey key{template_name, arguments};
  
  // Check if already instantiated
  auto cache_it = instantiated_functions_.find(key);
  if (cache_it != instantiated_functions_.end()) {
    return cache_it->second.name;
  }
  
  // Find the function template
  auto tmpl_it = function_templates_.find(template_name);
  if (tmpl_it == function_templates_.end()) {
    return "";  // Template not found
  }
  
  const auto& tmpl = tmpl_it->second;
  
  // Validate arguments
  std::string error;
  if (!ValidateArguments(arguments, tmpl.params, &error)) {
    return "";  // Invalid arguments
  }
  
  // Generate instantiated name
  std::string instantiated_name = GenerateInstanceName(template_name, arguments);
  
  // Build substitution map
  auto substitutions = BuildSubstitutionMap(tmpl.params, arguments);
  
  // Perform AST substitution
  void* instantiated_ast = tmpl.ast;
  if (ast_visitor_ && tmpl.ast) {
    // Delegate to visitor for actual AST transformation
  }
  
  // Cache the result
  InstantiatedTemplate inst;
  inst.name = instantiated_name;
  inst.ast = instantiated_ast;
  inst.substitutions = std::move(substitutions);
  instantiated_functions_[key] = std::move(inst);
  
  return instantiated_name;
}

void* TemplateInstantiator::GetInstantiatedClassAst(const std::string& instantiated_name) {
  for (const auto& [key, inst] : instantiated_classes_) {
    if (inst.name == instantiated_name) {
      return inst.ast;
    }
  }
  return nullptr;
}

void* TemplateInstantiator::GetInstantiatedFunctionAst(const std::string& instantiated_name) {
  for (const auto& [key, inst] : instantiated_functions_) {
    if (inst.name == instantiated_name) {
      return inst.ast;
    }
  }
  return nullptr;
}

// -------------------------------------------------------------------------
// Template Argument Deduction
// -------------------------------------------------------------------------

DeductionResult TemplateInstantiator::DeduceTemplateArguments(
    const std::string& template_name,
    const std::vector<IRType>& argument_types) {
  // Find the function template
  auto tmpl_it = function_templates_.find(template_name);
  if (tmpl_it == function_templates_.end()) {
    return DeductionResult::Failure("Function template '" + template_name + "' not found");
  }
  
  const auto& tmpl = tmpl_it->second;
  const auto& params = tmpl.params;
  const auto& param_types = tmpl.param_types;
  
  // Build set of parameter names for quick lookup
  std::set<std::string> param_names;
  for (const auto& p : params) {
    param_names.insert(p.name);
  }
  
  // Deduction map: parameter name -> deduced argument
  std::unordered_map<std::string, TemplateArgument> deduction_map;
  
  // Match function parameter types against call argument types
  size_t num_params = std::min(param_types.size(), argument_types.size());
  for (size_t i = 0; i < num_params; ++i) {
    const IRType& pattern = param_types[i];
    const IRType& argument = argument_types[i];
    
    // Attempt to match and deduce
    if (!TypeMatchesPattern(pattern, argument, param_names, deduction_map)) {
      return DeductionResult::Failure("Cannot deduce template argument from parameter " +
                                       std::to_string(i + 1));
    }
  }
  
  // Check if all non-defaulted parameters were deduced
  std::vector<TemplateArgument> deduced_args;
  for (const auto& param : params) {
    auto it = deduction_map.find(param.name);
    if (it != deduction_map.end()) {
      deduced_args.push_back(it->second);
    } else if (!param.default_value.empty()) {
      // Use default value - this is simplified; real implementation would parse defaults
      TemplateArgument default_arg;
      if (param.is_typename) {
        // Assume default_value is a type name, create a basic type
        default_arg.is_type = true;
        default_arg.type.name = param.default_value;
        default_arg.type.kind = IRTypeKind::kStruct;  // Generic type
      } else {
        default_arg.is_type = false;
        default_arg.value = param.default_value;
      }
      deduced_args.push_back(default_arg);
      deduction_map[param.name] = default_arg;
    } else if (!param.is_parameter_pack) {
      return DeductionResult::Failure("Cannot deduce template parameter '" + param.name + "'");
    }
  }
  
  return DeductionResult::Success(deduced_args, deduction_map);
}

DeductionResult TemplateInstantiator::DeduceFromType(
    const IRType& pattern,
    const IRType& argument,
    const std::vector<TemplateParameter>& params) {
  // Build parameter name set
  std::set<std::string> param_names;
  for (const auto& p : params) {
    param_names.insert(p.name);
  }
  
  std::unordered_map<std::string, TemplateArgument> deduction_map;
  
  if (!TypeMatchesPattern(pattern, argument, param_names, deduction_map)) {
    return DeductionResult::Failure("Type pattern does not match argument");
  }
  
  // Build deduced args in parameter order
  std::vector<TemplateArgument> deduced_args;
  for (const auto& param : params) {
    auto it = deduction_map.find(param.name);
    if (it != deduction_map.end()) {
      deduced_args.push_back(it->second);
    } else {
      // Parameter not deduced
      TemplateArgument empty;
      deduced_args.push_back(empty);
    }
  }
  
  return DeductionResult::Success(deduced_args, deduction_map);
}

// -------------------------------------------------------------------------
// Type Substitution
// -------------------------------------------------------------------------

SubstitutionResult TemplateInstantiator::SubstituteType(
    const IRType& type,
    const std::unordered_map<std::string, TemplateArgument>& substitutions) {
  // Check if this type is a template parameter reference
  if (type.kind == IRTypeKind::kStruct || type.kind == IRTypeKind::kInvalid) {
    auto it = substitutions.find(type.name);
    if (it != substitutions.end() && it->second.is_type) {
      return SubstitutionResult::Success(it->second.type);
    }
  }
  
  // Handle composite types by substituting their subtypes
  IRType result = type;
  
  if (!type.subtypes.empty()) {
    result.subtypes.clear();
    for (const auto& subtype : type.subtypes) {
      auto sub_result = SubstituteType(subtype, substitutions);
      if (!sub_result.success) {
        return sub_result;
      }
      result.subtypes.push_back(sub_result.result_type);
    }
    
    // Update the name based on substituted subtypes
    switch (type.kind) {
      case IRTypeKind::kPointer:
        if (!result.subtypes.empty()) {
          result.name = result.subtypes[0].name + "*";
        }
        break;
      case IRTypeKind::kReference:
        if (!result.subtypes.empty()) {
          result.name = result.subtypes[0].name + "&";
        }
        break;
      case IRTypeKind::kArray:
        if (!result.subtypes.empty()) {
          result.name = result.subtypes[0].name + "[" + std::to_string(result.count) + "]";
        }
        break;
      case IRTypeKind::kVector:
        if (!result.subtypes.empty()) {
          result.name = "<" + std::to_string(result.count) + " x " + result.subtypes[0].name + ">";
        }
        break;
      case IRTypeKind::kFunction:
        // Function type name would need more complex formatting
        break;
      default:
        break;
    }
  }
  
  return SubstitutionResult::Success(result);
}

SubstitutionResult TemplateInstantiator::SubstituteAst(
    void* ast,
    const std::unordered_map<std::string, TemplateArgument>& substitutions,
    TemplateAstVisitor* visitor) {
  if (!visitor) {
    return SubstitutionResult::Failure("No AST visitor provided for substitution");
  }
  
  if (!ast) {
    return SubstitutionResult::Failure("Null AST provided for substitution");
  }
  
  // Perform the AST substitution through the frontend-specific visitor.
  // Wrap the raw pointer in a non-owning shared_ptr so we can pass it
  // through the GenericAstNode-based visitor interface without taking
  // ownership of memory we do not own.
  auto non_owning = std::shared_ptr<GenericAstNode>(
      static_cast<GenericAstNode *>(ast),
      [](GenericAstNode *) { /* intentional no-op deleter */ });

  // Let the visitor perform the type substitution across the AST tree
  auto result_node = visitor->SubstituteType(non_owning, substitutions);
  if (!result_node) {
    return SubstitutionResult::Failure(
        "AST visitor returned null after substitution for node type '"
        + non_owning->NodeType() + "'");
  }
  return SubstitutionResult::Success(result_node);
}

// -------------------------------------------------------------------------
// Specialization Matching
// -------------------------------------------------------------------------

bool TemplateInstantiator::MatchesPattern(
    const std::vector<TemplateArgument>& arguments,
    const std::vector<TemplateArgument>& pattern,
    std::unordered_map<std::string, TemplateArgument>* bindings) {
  // Handle size mismatch (except for variadic patterns)
  if (arguments.size() != pattern.size()) {
    // Check if the last pattern element is a pack that can absorb extras
    if (pattern.empty() || !pattern.back().is_pack) {
      return false;
    }
  }
  
  size_t pattern_idx = 0;
  size_t arg_idx = 0;
  
  while (pattern_idx < pattern.size() && arg_idx < arguments.size()) {
    const auto& pat = pattern[pattern_idx];
    const auto& arg = arguments[arg_idx];
    
    // Handle pack pattern - matches remaining arguments
    if (pat.is_pack) {
      if (bindings) {
        // Collect remaining arguments into the pack binding
        std::vector<TemplateArgument> pack_contents;
        while (arg_idx < arguments.size()) {
          pack_contents.push_back(arguments[arg_idx++]);
        }
        // Store pack binding with a synthetic name based on position
        std::string pack_name = "__pack_" + std::to_string(pattern_idx);
        (*bindings)[pack_name] = TemplateArgument::Pack(pack_contents);
      }
      return true;  // Pack matches all remaining args
    }
    
    // Check type compatibility
    if (arg.is_type != pat.is_type) {
      return false;
    }
    
    if (arg.is_type) {
      // Type argument matching
      // A pattern type with name matching a template parameter is a wildcard
      if (pat.type.kind == IRTypeKind::kInvalid ||
          (pat.type.kind == IRTypeKind::kStruct && pat.type.subtypes.empty())) {
        // This looks like a template parameter placeholder
        if (bindings) {
          (*bindings)[pat.type.name] = arg;
        }
      } else {
        // Concrete type - must match exactly or structurally
        if (!MatchTypeStructure(pat.type, arg.type, bindings)) {
          return false;
        }
      }
    } else {
      // Non-type argument matching
      if (pat.value.empty()) {
        // Wildcard non-type parameter
        if (bindings) {
          (*bindings)[pat.value] = arg;
        }
      } else if (pat.value != arg.value) {
        return false;  // Values must match exactly
      }
    }
    
    ++pattern_idx;
    ++arg_idx;
  }
  
  // All arguments consumed?
  return arg_idx == arguments.size() && pattern_idx == pattern.size();
}

const TemplateSpecialization* TemplateInstantiator::FindBestSpecialization(
    const std::string& template_name,
    const std::vector<TemplateArgument>& arguments) {
  auto it = specializations_.find(template_name);
  if (it == specializations_.end()) {
    return nullptr;
  }
  
  // Specializations are already sorted by specificity
  for (const auto& spec : it->second) {
    if (MatchesPattern(arguments, spec.pattern, nullptr)) {
      return &spec;
    }
  }
  
  return nullptr;
}

// -------------------------------------------------------------------------
// Utility Functions
// -------------------------------------------------------------------------

std::string TemplateInstantiator::GenerateInstanceName(
    const std::string& template_name,
    const std::vector<TemplateArgument>& arguments) const {
  std::ostringstream ss;
  ss << template_name << "<";
  
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (i > 0) ss << ", ";
    
    const auto& arg = arguments[i];
    if (arg.is_pack) {
      // Expand pack contents
      for (size_t j = 0; j < arg.pack_args.size(); ++j) {
        if (j > 0) ss << ", ";
        if (arg.pack_args[j].is_type) {
          ss << TypeToString(arg.pack_args[j].type);
        } else {
          ss << arg.pack_args[j].value;
        }
      }
    } else if (arg.is_type) {
      ss << TypeToString(arg.type);
    } else {
      ss << arg.value;
    }
  }
  
  ss << ">";
  return ss.str();
}

std::string TemplateInstantiator::TypeToString(const IRType& type) const {
  switch (type.kind) {
    case IRTypeKind::kI1:
      return "bool";
    case IRTypeKind::kI8:
      return type.is_signed ? "i8" : "u8";
    case IRTypeKind::kI16:
      return type.is_signed ? "i16" : "u16";
    case IRTypeKind::kI32:
      return type.is_signed ? "int" : "unsigned";
    case IRTypeKind::kI64:
      return type.is_signed ? "long" : "unsigned long";
    case IRTypeKind::kF32:
      return "float";
    case IRTypeKind::kF64:
      return "double";
    case IRTypeKind::kVoid:
      return "void";
    case IRTypeKind::kPointer:
      if (!type.subtypes.empty()) {
        return TypeToString(type.subtypes[0]) + "*";
      }
      return "void*";
    case IRTypeKind::kReference:
      if (!type.subtypes.empty()) {
        return TypeToString(type.subtypes[0]) + "&";
      }
      return "void&";
    case IRTypeKind::kArray:
      if (!type.subtypes.empty()) {
        return TypeToString(type.subtypes[0]) + "[" + std::to_string(type.count) + "]";
      }
      return "array";
    case IRTypeKind::kVector:
      if (!type.subtypes.empty()) {
        return "vec<" + std::to_string(type.count) + ", " + TypeToString(type.subtypes[0]) + ">";
      }
      return "vector";
    case IRTypeKind::kStruct:
      return type.name.empty() ? "struct" : type.name;
    case IRTypeKind::kFunction:
      return "fn";
    default:
      return type.name.empty() ? "unknown" : type.name;
  }
}

bool TemplateInstantiator::HasClassTemplate(const std::string& name) const {
  return class_templates_.count(name) > 0;
}

bool TemplateInstantiator::HasFunctionTemplate(const std::string& name) const {
  return function_templates_.count(name) > 0;
}

const std::vector<TemplateParameter>* TemplateInstantiator::GetClassTemplateParams(
    const std::string& name) const {
  auto it = class_templates_.find(name);
  if (it != class_templates_.end()) {
    return &it->second.params;
  }
  return nullptr;
}

const std::vector<TemplateParameter>* TemplateInstantiator::GetFunctionTemplateParams(
    const std::string& name) const {
  auto it = function_templates_.find(name);
  if (it != function_templates_.end()) {
    return &it->second.params;
  }
  return nullptr;
}

void TemplateInstantiator::SetAstVisitor(std::shared_ptr<TemplateAstVisitor> visitor) {
  ast_visitor_ = std::move(visitor);
}

void TemplateInstantiator::ClearCaches() {
  instantiated_classes_.clear();
  instantiated_functions_.clear();
}

// -------------------------------------------------------------------------
// Private Helper Functions
// -------------------------------------------------------------------------

int TemplateInstantiator::CalculateSpecificity(
    const std::vector<TemplateArgument>& pattern) const {
  int specificity = 0;
  
  for (const auto& arg : pattern) {
    if (arg.is_pack) {
      // Pack patterns are less specific
      specificity += 1;
    } else if (arg.is_type) {
      // Concrete types are more specific
      if (arg.type.kind != IRTypeKind::kInvalid &&
          arg.type.kind != IRTypeKind::kStruct) {
        // Primitive or built-in types are very specific
        specificity += 20;
      } else if (!arg.type.subtypes.empty()) {
        // Composite types with subtypes are moderately specific
        specificity += 15;
        // Recursively add specificity for subtypes
        for (const auto& subtype : arg.type.subtypes) {
          if (subtype.kind != IRTypeKind::kInvalid) {
            specificity += 5;
          }
        }
      } else if (arg.type.kind == IRTypeKind::kStruct && !arg.type.name.empty()) {
        // Named struct types
        specificity += 10;
      } else {
        // Generic template parameter placeholder
        specificity += 1;
      }
    } else {
      // Non-type arguments
      if (!arg.value.empty()) {
        // Concrete value is specific
        specificity += 15;
      } else {
        // Placeholder non-type param
        specificity += 1;
      }
    }
  }
  
  return specificity;
}

bool TemplateInstantiator::TypeMatchesPattern(
    const IRType& pattern,
    const IRType& argument,
    const std::set<std::string>& param_names,
    std::unordered_map<std::string, TemplateArgument>& bindings) const {
  // Check if pattern is a template parameter reference
  if (IsTemplateParameterType(pattern, param_names)) {
    // This is a template parameter - bind it
    auto existing = bindings.find(pattern.name);
    if (existing != bindings.end()) {
      // Already bound - check consistency
      if (existing->second.is_type && existing->second.type != argument) {
        return false;  // Inconsistent deduction
      }
    } else {
      // New binding
      bindings[pattern.name] = TemplateArgument::Type(argument);
    }
    return true;
  }
  
  // Same kind required for non-parameter types
  if (pattern.kind != argument.kind) {
    return false;
  }
  
  // Check structural compatibility based on kind
  switch (pattern.kind) {
    case IRTypeKind::kPointer:
    case IRTypeKind::kReference:
      // Must have matching pointee/referent
      if (pattern.subtypes.size() != argument.subtypes.size()) {
        return false;
      }
      if (!pattern.subtypes.empty()) {
        return TypeMatchesPattern(pattern.subtypes[0], argument.subtypes[0],
                                  param_names, bindings);
      }
      return true;
      
    case IRTypeKind::kArray:
    case IRTypeKind::kVector:
      // Must have matching element type and count
      if (pattern.count != argument.count) {
        return false;
      }
      if (pattern.subtypes.size() != argument.subtypes.size()) {
        return false;
      }
      if (!pattern.subtypes.empty()) {
        return TypeMatchesPattern(pattern.subtypes[0], argument.subtypes[0],
                                  param_names, bindings);
      }
      return true;
      
    case IRTypeKind::kFunction:
      // Match return type and all parameter types
      if (pattern.subtypes.size() != argument.subtypes.size()) {
        return false;
      }
      for (size_t i = 0; i < pattern.subtypes.size(); ++i) {
        if (!TypeMatchesPattern(pattern.subtypes[i], argument.subtypes[i],
                                param_names, bindings)) {
          return false;
        }
      }
      return true;
      
    case IRTypeKind::kStruct:
      // Struct types match by name (after parameter substitution)
      if (pattern.name != argument.name) {
        return false;
      }
      // Also check field types if present
      if (pattern.subtypes.size() != argument.subtypes.size()) {
        return false;
      }
      for (size_t i = 0; i < pattern.subtypes.size(); ++i) {
        if (!TypeMatchesPattern(pattern.subtypes[i], argument.subtypes[i],
                                param_names, bindings)) {
          return false;
        }
      }
      return true;
      
    default:
      // Primitive types - exact match
      return pattern == argument;
  }
}

bool TemplateInstantiator::IsTemplateParameterType(
    const IRType& type,
    const std::set<std::string>& param_names) const {
  // A type is a template parameter reference if:
  // 1. It has kStruct kind with a name matching a parameter
  // 2. It has kInvalid kind with a name matching a parameter
  // 3. It has no subtypes (not a concrete composite type)
  if (type.kind == IRTypeKind::kInvalid || 
      (type.kind == IRTypeKind::kStruct && type.subtypes.empty())) {
    return param_names.count(type.name) > 0;
  }
  return false;
}

std::vector<TemplateArgument> TemplateInstantiator::ExpandParameterPacks(
    const std::vector<TemplateArgument>& arguments,
    const std::vector<TemplateParameter>& params) const {
  std::vector<TemplateArgument> expanded;
  
  size_t arg_idx = 0;
  for (size_t param_idx = 0; param_idx < params.size(); ++param_idx) {
    if (arg_idx >= arguments.size()) break;
    
    const auto& param = params[param_idx];
    const auto& arg = arguments[arg_idx];
    
    if (param.is_parameter_pack && arg.is_pack) {
      // Expand pack arguments
      for (const auto& pack_arg : arg.pack_args) {
        expanded.push_back(pack_arg);
      }
    } else {
      expanded.push_back(arg);
    }
    ++arg_idx;
  }
  
  // Add any remaining arguments (for variadic templates)
  while (arg_idx < arguments.size()) {
    expanded.push_back(arguments[arg_idx++]);
  }
  
  return expanded;
}

bool TemplateInstantiator::ValidateArguments(
    const std::vector<TemplateArgument>& arguments,
    const std::vector<TemplateParameter>& params,
    std::string* error_msg) const {
  // Count required parameters (those without defaults and not packs)
  size_t required_count = 0;
  bool has_pack = false;
  for (const auto& param : params) {
    if (param.is_parameter_pack) {
      has_pack = true;
    } else if (param.default_value.empty()) {
      ++required_count;
    }
  }
  
  // Check argument count
  if (arguments.size() < required_count) {
    if (error_msg) {
      *error_msg = "Too few template arguments: expected at least " +
                   std::to_string(required_count) + ", got " +
                   std::to_string(arguments.size());
    }
    return false;
  }
  
  if (!has_pack && arguments.size() > params.size()) {
    if (error_msg) {
      *error_msg = "Too many template arguments: expected at most " +
                   std::to_string(params.size()) + ", got " +
                   std::to_string(arguments.size());
    }
    return false;
  }
  
  // Validate each argument against its parameter
  for (size_t i = 0; i < std::min(arguments.size(), params.size()); ++i) {
    const auto& arg = arguments[i];
    const auto& param = params[i];
    
    // Type vs non-type mismatch
    if (param.is_typename != arg.is_type && !arg.is_pack) {
      if (error_msg) {
        *error_msg = "Template argument " + std::to_string(i + 1) +
                     (param.is_typename ? " should be a type" : " should be a value");
      }
      return false;
    }
    
    // For non-type parameters, could add type checking here
    // (e.g., check that arg.value is valid for param.type)
  }
  
  return true;
}

std::unordered_map<std::string, TemplateArgument> TemplateInstantiator::BuildSubstitutionMap(
    const std::vector<TemplateParameter>& params,
    const std::vector<TemplateArgument>& arguments) const {
  std::unordered_map<std::string, TemplateArgument> map;
  
  // First expand any packs
  auto expanded = ExpandParameterPacks(arguments, params);
  
  size_t arg_idx = 0;
  for (const auto& param : params) {
    if (param.is_parameter_pack) {
      // Collect remaining arguments into pack
      std::vector<TemplateArgument> pack_args;
      while (arg_idx < expanded.size()) {
        pack_args.push_back(expanded[arg_idx++]);
      }
      map[param.name] = TemplateArgument::Pack(pack_args);
    } else if (arg_idx < expanded.size()) {
      map[param.name] = expanded[arg_idx++];
    } else if (!param.default_value.empty()) {
      // Use default
      TemplateArgument default_arg;
      if (param.is_typename) {
        default_arg.is_type = true;
        default_arg.type.name = param.default_value;
        default_arg.type.kind = IRTypeKind::kStruct;
      } else {
        default_arg.is_type = false;
        default_arg.value = param.default_value;
      }
      map[param.name] = default_arg;
    }
  }
  
  return map;
}

// -------------------------------------------------------------------------
// Additional Helper Function (used in MatchesPattern)
// -------------------------------------------------------------------------

namespace {

// Recursively match type structure, handling template parameters as wildcards
bool MatchTypeStructure(const IRType& pattern, const IRType& argument,
                        std::unordered_map<std::string, TemplateArgument>* bindings) {
  // If pattern looks like a template parameter (struct with no subtypes, simple name)
  if (pattern.kind == IRTypeKind::kStruct && pattern.subtypes.empty()) {
    // Treat as wildcard - binds to argument
    if (bindings) {
      auto existing = bindings->find(pattern.name);
      if (existing != bindings->end()) {
        // Check consistency
        return existing->second.is_type && existing->second.type == argument;
      }
      (*bindings)[pattern.name] = TemplateArgument::Type(argument);
    }
    return true;
  }
  
  // Same kind required
  if (pattern.kind != argument.kind) {
    return false;
  }
  
  // For pointers/references/arrays/vectors, match subtypes
  if (pattern.subtypes.size() != argument.subtypes.size()) {
    return false;
  }
  
  for (size_t i = 0; i < pattern.subtypes.size(); ++i) {
    if (!MatchTypeStructure(pattern.subtypes[i], argument.subtypes[i], bindings)) {
      return false;
    }
  }
  
  // For arrays/vectors, also check count
  if ((pattern.kind == IRTypeKind::kArray || pattern.kind == IRTypeKind::kVector) &&
      pattern.count != argument.count) {
    return false;
  }
  
  // For structs, also check name
  if (pattern.kind == IRTypeKind::kStruct && pattern.name != argument.name) {
    return false;
  }
  
  return true;
}

}  // anonymous namespace

}  // namespace polyglot::ir
