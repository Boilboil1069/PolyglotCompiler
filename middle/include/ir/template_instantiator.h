/**
 * @file     template_instantiator.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

// Template Instantiation System
// =============================
// This module provides complete template instantiation support for the PolyglotCompiler.
// It handles:
// - Class template instantiation with full AST traversal and substitution
// - Function template instantiation with parameter deduction
// - Partial and explicit specializations with pattern matching
// - Variadic templates and parameter packs
// - SFINAE (Substitution Failure Is Not An Error) handling
// - Dependent name resolution
//
// The design follows C++ template semantics but is language-agnostic enough
// to support templates from multiple source languages.

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

// ============================================================================
// Template Parameter Definitions
// ============================================================================

// Represents a template parameter (typename T, class T, or non-type like int N)
/** @brief TemplateParameter data structure. */
struct TemplateParameter {
  std::string name;             // Parameter name (e.g., "T", "N")
  bool is_typename{true};       // true: typename/class, false: non-type parameter
  IRType type;                  // Type for non-type parameters (e.g., int for int N)
  std::string default_value;    // Default value or type name
  bool is_parameter_pack{false}; // true for variadic parameters (typename... Args)
  std::vector<std::string> constraints; // Concept constraints (requires clauses)
};

// Represents a template argument (actual type or value passed to instantiation)
/** @brief TemplateArgument data structure. */
struct TemplateArgument {
  bool is_type{true};           // true: type argument, false: non-type argument
  IRType type;                  // Type argument value
  std::string value;            // Value for non-type argument (as string representation)
  bool is_pack{false};          // true if this represents an expanded pack
  std::vector<TemplateArgument> pack_args; // Expanded pack arguments
  
  // Create a type argument
  static TemplateArgument Type(const IRType& t) {
    TemplateArgument arg;
    arg.is_type = true;
    arg.type = t;
    return arg;
  }
  
  // Create a non-type argument
  static TemplateArgument Value(const std::string& v) {
    TemplateArgument arg;
    arg.is_type = false;
    arg.value = v;
    return arg;
  }
  
  // Create a pack argument
  static TemplateArgument Pack(const std::vector<TemplateArgument>& args) {
    TemplateArgument arg;
    arg.is_pack = true;
    arg.pack_args = args;
    return arg;
  }
  
  bool operator==(const TemplateArgument& other) const;
  bool operator!=(const TemplateArgument& other) const { return !(*this == other); }
};

// ============================================================================
// Template Instantiation Key (for caching instantiated templates)
// ============================================================================

/** @brief TemplateInstantiationKey data structure. */
struct TemplateInstantiationKey {
  std::string template_name;
  std::vector<TemplateArgument> arguments;

  bool operator==(const TemplateInstantiationKey& other) const;
};

// ============================================================================
// Template Specialization
// ============================================================================

// Represents a template specialization (partial or explicit)
/** @brief TemplateSpecialization data structure. */
struct TemplateSpecialization {
  std::vector<TemplateArgument> pattern;   // The specialization pattern
  std::vector<TemplateParameter> params;   // Parameters for partial specialization
  void* specialized_ast{nullptr};          // Specialized AST (type-erased)
  int specificity{0};                      // Ranking for choosing best match
  bool is_partial{true};                   // true: partial, false: explicit
};

// ============================================================================
// AST Visitor Interface for Template Substitution
// ============================================================================

// Generic AST node interface for type-erased AST handling
/** @brief GenericAstNode data structure. */
struct GenericAstNode {
  virtual ~GenericAstNode() = default;
  virtual std::shared_ptr<GenericAstNode> Clone() const = 0;
  virtual std::string NodeType() const = 0;
};

// AST visitor interface for template parameter substitution
// Frontend-specific implementations should inherit from this
/** @brief TemplateAstVisitor class. */
class TemplateAstVisitor {
public:
  virtual ~TemplateAstVisitor() = default;
  
  // Substitute a type in an AST node, returns new node with substitution applied
  virtual std::shared_ptr<GenericAstNode> SubstituteType(
      std::shared_ptr<GenericAstNode> node,
      const std::unordered_map<std::string, TemplateArgument>& substitutions) = 0;
  
  // Check if a node contains a dependent type reference
  virtual bool HasDependentType(std::shared_ptr<GenericAstNode> node,
                                const std::set<std::string>& param_names) = 0;
  
  // Extract type references from an AST node (for deduction)
  virtual std::vector<std::pair<std::string, IRType>> ExtractTypeReferences(
      std::shared_ptr<GenericAstNode> node) = 0;
};

// ============================================================================
// Substitution Result
// ============================================================================

// Result of a template substitution operation
/** @brief SubstitutionResult data structure. */
struct SubstitutionResult {
  bool success{false};
  std::string error_message;
  std::shared_ptr<GenericAstNode> result_ast;
  IRType result_type;
  
  static SubstitutionResult Success(std::shared_ptr<GenericAstNode> ast) {
    SubstitutionResult r;
    r.success = true;
    r.result_ast = ast;
    return r;
  }
  
  static SubstitutionResult Success(const IRType& type) {
    SubstitutionResult r;
    r.success = true;
    r.result_type = type;
    return r;
  }
  
  static SubstitutionResult Failure(const std::string& msg) {
    SubstitutionResult r;
    r.success = false;
    r.error_message = msg;
    return r;
  }
};

// ============================================================================
// Deduction Result
// ============================================================================

// Result of template argument deduction
/** @brief DeductionResult data structure. */
struct DeductionResult {
  bool success{false};
  std::string error_message;
  std::vector<TemplateArgument> deduced_args;
  std::unordered_map<std::string, TemplateArgument> deduction_map;
  
  static DeductionResult Success(const std::vector<TemplateArgument>& args,
                                 const std::unordered_map<std::string, TemplateArgument>& map) {
    DeductionResult r;
    r.success = true;
    r.deduced_args = args;
    r.deduction_map = map;
    return r;
  }
  
  static DeductionResult Failure(const std::string& msg) {
    DeductionResult r;
    r.success = false;
    r.error_message = msg;
    return r;
  }
};

}  // namespace polyglot::ir

// ============================================================================
// Hash function for TemplateInstantiationKey
// ============================================================================

namespace std {
template <>
struct hash<polyglot::ir::TemplateInstantiationKey> {
  size_t operator()(const polyglot::ir::TemplateInstantiationKey& key) const {
    size_t h = hash<string>()(key.template_name);
    for (const auto& arg : key.arguments) {
      h ^= hash<bool>()(arg.is_type) << 1;
      if (arg.is_type) {
        h ^= hash<int>()(static_cast<int>(arg.type.kind)) << 2;
        h ^= hash<string>()(arg.type.name) << 3;
      } else {
        h ^= hash<string>()(arg.value) << 4;
      }
      if (arg.is_pack) {
        h ^= hash<size_t>()(arg.pack_args.size()) << 5;
      }
    }
    return h;
  }
};
}  // namespace std

namespace polyglot::ir {

// ============================================================================
// Main Template Instantiator Class
// ============================================================================

/** @brief TemplateInstantiator class. */
class TemplateInstantiator {
public:
  TemplateInstantiator();
  ~TemplateInstantiator();
  
  /** @name - */
  /** @{ */
  // Template Registration
  /** @} */

  /** @name - */
  /** @{ */
  
  // Register a class template
  void RegisterClassTemplate(const std::string& name,
                             const std::vector<TemplateParameter>& params,
                             void* template_ast);

  // Register a function template
  void RegisterFunctionTemplate(const std::string& name,
                                const std::vector<TemplateParameter>& params,
                                void* template_ast,
                                const std::vector<IRType>& param_types = {});

  // Register a template alias (using template)
  void RegisterTemplateAlias(const std::string& name,
                             const std::vector<TemplateParameter>& params,
                             const IRType& aliased_type);

  // Register a template specialization (partial or explicit)
  void RegisterSpecialization(const std::string& template_name,
                              const std::vector<TemplateArgument>& pattern,
                              const std::vector<TemplateParameter>& params,
                              void* specialized_ast,
                              bool is_partial = true);

  /** @} */

  /** @name - */
  /** @{ */
  // Template Instantiation
  /** @} */

  /** @name - */
  /** @{ */
  
  // Instantiate a class template with given arguments
  // Returns the mangled instantiation name (e.g., "vector<int>")
  std::string InstantiateClass(const std::string& template_name,
                               const std::vector<TemplateArgument>& arguments);

  // Instantiate a function template with given arguments
  // Returns the mangled instantiation name
  std::string InstantiateFunction(const std::string& template_name,
                                  const std::vector<TemplateArgument>& arguments);

  // Get the instantiated AST for a class template
  void* GetInstantiatedClassAst(const std::string& instantiated_name);
  
  // Get the instantiated AST for a function template
  void* GetInstantiatedFunctionAst(const std::string& instantiated_name);

  /** @} */

  /** @name - */
  /** @{ */
  // Template Argument Deduction
  /** @} */

  /** @name - */
  /** @{ */
  
  // Deduce template arguments from function call argument types
  DeductionResult DeduceTemplateArguments(const std::string& template_name,
                                          const std::vector<IRType>& argument_types);

  // Deduce template arguments from a single type pattern match
  DeductionResult DeduceFromType(const IRType& pattern,
                                 const IRType& argument,
                                 const std::vector<TemplateParameter>& params);

  /** @} */

  /** @name - */
  /** @{ */
  // Type Substitution
  /** @} */

  /** @name - */
  /** @{ */
  
  // Substitute template parameters in a type
  SubstitutionResult SubstituteType(
      const IRType& type,
      const std::unordered_map<std::string, TemplateArgument>& substitutions);

  // Perform full AST substitution (requires visitor)
  SubstitutionResult SubstituteAst(
      void* ast,
      const std::unordered_map<std::string, TemplateArgument>& substitutions,
      TemplateAstVisitor* visitor);

  /** @} */

  /** @name - */
  /** @{ */
  // Specialization Matching
  /** @} */

  /** @name - */
  /** @{ */
  
  // Check if arguments match a specialization pattern
  bool MatchesPattern(const std::vector<TemplateArgument>& arguments,
                      const std::vector<TemplateArgument>& pattern,
                      std::unordered_map<std::string, TemplateArgument>* bindings = nullptr);

  // Find the best matching specialization for given arguments
  const TemplateSpecialization* FindBestSpecialization(
      const std::string& template_name,
      const std::vector<TemplateArgument>& arguments);

  /** @} */

  /** @name - */
  /** @{ */
  // Utility Functions
  /** @} */

  /** @name - */
  /** @{ */
  
  // Generate a mangled name for an instantiation
  std::string GenerateInstanceName(const std::string& template_name,
                                   const std::vector<TemplateArgument>& arguments) const;

  // Convert an IRType to its string representation
  std::string TypeToString(const IRType& type) const;

  // Check if a template with the given name exists
  bool HasClassTemplate(const std::string& name) const;
  bool HasFunctionTemplate(const std::string& name) const;

  // Get template parameters for a registered template
  const std::vector<TemplateParameter>* GetClassTemplateParams(const std::string& name) const;
  const std::vector<TemplateParameter>* GetFunctionTemplateParams(const std::string& name) const;

  // Set the AST visitor for frontend-specific operations
  void SetAstVisitor(std::shared_ptr<TemplateAstVisitor> visitor);

  // Clear all caches (for testing or to free memory)
  void ClearCaches();

private:
  // Internal template information storage
  /** @brief ClassTemplateInfo data structure. */
  struct ClassTemplateInfo {
    std::vector<TemplateParameter> params;
    void* ast{nullptr};
  };
  
  /** @brief FunctionTemplateInfo data structure. */
  struct FunctionTemplateInfo {
    std::vector<TemplateParameter> params;
    void* ast{nullptr};
    std::vector<IRType> param_types;  // Function parameter types for deduction
  };
  
  /** @brief AliasTemplateInfo data structure. */
  struct AliasTemplateInfo {
    std::vector<TemplateParameter> params;
    IRType aliased_type;
  };
  
  /** @brief InstantiatedTemplate data structure. */
  struct InstantiatedTemplate {
    std::string name;
    void* ast{nullptr};
    std::unordered_map<std::string, TemplateArgument> substitutions;
  };
  
  // Template storage
  std::unordered_map<std::string, ClassTemplateInfo> class_templates_;
  std::unordered_map<std::string, FunctionTemplateInfo> function_templates_;
  std::unordered_map<std::string, AliasTemplateInfo> alias_templates_;
  std::unordered_map<std::string, std::vector<TemplateSpecialization>> specializations_;
  
  // Instantiation caches
  std::unordered_map<TemplateInstantiationKey, InstantiatedTemplate> instantiated_classes_;
  std::unordered_map<TemplateInstantiationKey, InstantiatedTemplate> instantiated_functions_;
  
  // AST visitor for substitution
  std::shared_ptr<TemplateAstVisitor> ast_visitor_;
  
  /** @} */

  /** @name - */
  /** @{ */
  // Internal Helper Functions
  /** @} */

  /** @name - */
  /** @{ */
  
  // Calculate the specificity score for a specialization pattern
  int CalculateSpecificity(const std::vector<TemplateArgument>& pattern) const;
  
  // Perform type matching for deduction
  bool TypeMatchesPattern(const IRType& pattern,
                          const IRType& argument,
                          const std::set<std::string>& param_names,
                          std::unordered_map<std::string, TemplateArgument>& bindings) const;
  
  // Check if a type is a template parameter reference
  bool IsTemplateParameterType(const IRType& type,
                               const std::set<std::string>& param_names) const;
  
  // Expand parameter packs in arguments
  std::vector<TemplateArgument> ExpandParameterPacks(
      const std::vector<TemplateArgument>& arguments,
      const std::vector<TemplateParameter>& params) const;
  
  // Validate arguments against parameters (check constraints, etc.)
  bool ValidateArguments(const std::vector<TemplateArgument>& arguments,
                         const std::vector<TemplateParameter>& params,
                         std::string* error_msg = nullptr) const;
  
  // Build substitution map from parameters and arguments
  std::unordered_map<std::string, TemplateArgument> BuildSubstitutionMap(
      const std::vector<TemplateParameter>& params,
      const std::vector<TemplateArgument>& arguments) const;
};

}  // namespace polyglot::ir

/** @} */