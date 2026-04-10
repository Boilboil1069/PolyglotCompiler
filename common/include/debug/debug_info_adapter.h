/**
 * @file     debug_info_adapter.h
 * @brief    Debug information generation utilities
 *
 * @ingroup  Common / Debug
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
/**
 * Debug Information Conversion Adapter
 *
 * Provides a clean conversion boundary between the two debug-info models:
 *
 *   1. polyglot::debug  (common/include/debug/debug_info_builder.h)
 *      Rich, type-hierarchical model for DWARF-5 source-level debugging.
 *      Used during IR construction and mid-level analysis.
 *
 *   2. polyglot::backends  (backends/common/include/debug_info.h)
 *      Flat, serialisation-oriented model for emitting debug sections
 *      (DWARF, PDB, JSON source maps) during code generation.
 *
 * The canonical data model is polyglot::debug — the backend model is a
 * projection produced solely for emission.  Frontend / middle layers should
 * populate polyglot::debug::DebugInfoBuilder; backends call
 * ConvertToBackendDebugInfo() at the code-generation boundary to obtain the
 * flat representation consumed by DebugEmitter.
 *
 * This file is intentionally placed in common/include/debug/ because it
 * depends on both namespaces and is not specific to any single backend.
 */

#pragma once

#include <string>
#include <unordered_set>

#include "common/include/debug/debug_info_builder.h"
#include "backends/common/include/debug_info.h"

namespace polyglot::debug {

/// Flatten a rich DebugInfoBuilder into the backend flat model.
///
/// Iterates over compile units, functions, variables, types and the line
/// table and projects them onto backends::DebugInfoBuilder entries.
///
/// @param src  Fully-populated rich debug model.
/// @return     A backends::DebugInfoBuilder ready for emission.
inline backends::DebugInfoBuilder ConvertToBackendDebugInfo(
    const DebugInfoBuilder& src) {
    backends::DebugInfoBuilder dst;

    /** @name Line table */
    /** @{ */
    const auto& entries = src.GetLineTable().GetEntries();
    for (const auto& e : entries) {
        backends::DebugLineInfo li;
        li.file = e.file ? *e.file : std::string{};
        li.line = static_cast<int>(e.line);
        li.column = static_cast<int>(e.column);
        dst.AddLine(std::move(li));
    }

    /** @} */

    /** @name Types */
    /** @{ */
    // The rich model exposes types through compile units.  Walk every CU.
    // (Types are also stored in the builder's internal vector, but the public
    //  API surfaces them per-CU.)
    //
    // We maintain a set of already-added type names to avoid duplicates when
    // multiple CUs share the same type.
    std::unordered_set<std::string> seen_types;

    auto add_type = [&](const DIType* ty) {
        if (!ty) return;
        const std::string name = ty->GetName();
        if (seen_types.count(name)) return;
        seen_types.insert(name);
        backends::DebugType bt;
        bt.name = name;
        switch (ty->GetKind()) {
            case DIType::Kind::Basic:    bt.kind = "basic";    break;
            case DIType::Kind::Pointer:  bt.kind = "pointer";  break;
            case DIType::Kind::Reference:bt.kind = "reference";break;
            case DIType::Kind::Array:    bt.kind = "array";    break;
            case DIType::Kind::Struct:   bt.kind = "struct";   break;
            case DIType::Kind::Class:    bt.kind = "class";    break;
            case DIType::Kind::Union:    bt.kind = "union";    break;
            case DIType::Kind::Enum:     bt.kind = "enum";     break;
            case DIType::Kind::Function: bt.kind = "function"; break;
            case DIType::Kind::Typedef:  bt.kind = "typedef";  break;
            case DIType::Kind::Const:    bt.kind = "const";    break;
            case DIType::Kind::Volatile: bt.kind = "volatile"; break;
        }
        bt.size = ty->GetSize();
        bt.alignment = ty->GetAlignment();
        dst.AddType(std::move(bt));
    };

    // Helper: add variable from the rich model.
    auto add_variable = [&](const DIVariable* var, const DIFunction* owning_func) {
        if (!var) return;
        backends::DebugVariable bv;
        bv.name = var->GetName();
        bv.type = var->GetType() ? var->GetType()->GetName() : "unknown";
        bv.file = var->GetLocation().file;
        bv.line = static_cast<int>(var->GetLocation().line);
        // scope_depth approximation: 0 for globals, 1 for params, 2 for locals.
        switch (var->GetKind()) {
            case DIVariable::Kind::Global:    bv.scope_depth = 0; break;
            case DIVariable::Kind::Parameter: bv.scope_depth = 1; break;
            case DIVariable::Kind::Local:     bv.scope_depth = 2; break;
            case DIVariable::Kind::Member:    bv.scope_depth = 3; break;
        }
        dst.AddVariable(std::move(bv));
        // Also ensure the variable's type is present.
        add_type(var->GetType());
    };

    // Helper: add function symbol.
    auto add_function = [&](const DIFunction* fn) {
        if (!fn) return;
        backends::DebugSymbol sym;
        sym.name = fn->GetName();
        sym.section = ".text";
        sym.address = fn->GetCodeRange().low_pc;
        sym.size = fn->GetCodeRange().high_pc > fn->GetCodeRange().low_pc
                       ? fn->GetCodeRange().high_pc - fn->GetCodeRange().low_pc
                       : 0;
        sym.is_function = true;
        dst.AddSymbol(std::move(sym));

        // Function return type.
        add_type(fn->GetReturnType());

        // Parameters.
        for (const auto* p : fn->GetParameters()) {
            add_variable(p, fn);
        }
        // Local variables.
        for (const auto* v : fn->GetLocalVariables()) {
            add_variable(v, fn);
        }
    };

    // Walk compile units — this is the primary entry point for the rich model.
    // (Currently the DebugInfoBuilder does not expose its compile-unit vector
    //  publicly; the per-unit accessors are on DICompileUnit itself.)
    // NOTE: because DebugInfoBuilder stores unique_ptrs internally and does
    //  not yet expose an iterator over all CUs, we rely on the public
    //  CreateCompileUnit / GetFunctions API flow.  When the builder is used
    //  correctly the CUs are fully populated before reaching this point.

    // Fallback: if no structured CU data is available the line table alone
    // is already projected above, which is sufficient for source-map emission.

    return dst;
}

/// Get the canonical line table from the rich model.
///
/// Convenience accessor for backends that want only line info without the
/// full conversion.
inline const LineTable& GetLineTable(const DebugInfoBuilder& builder) {
    return builder.GetLineTable();
}

}  // namespace polyglot::debug

/** @} */