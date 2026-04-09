// ============================================================================
// stage_marshal.cpp — Stage 3 implementation
// ============================================================================

#include "tools/polyc/src/stage_marshal.h"

#include <iostream>

namespace polyglot::tools {

MarshalResult RunMarshalStage(const DriverSettings &settings,
                               const SemanticResult &semantic) {
    MarshalResult result;
    const bool V = settings.verbose;

    if (!semantic.success || settings.language != "ploy") {
        result.success = semantic.success;
        return result;
    }

    // Build a call plan for every LINK entry the sema collected.
    for (const auto &entry : semantic.link_entries) {
        MarshalCallPlan plan;
        plan.link_id = entry.target_symbol + "<-" + entry.source_symbol;
        plan.source_language = entry.source_language;
        plan.target_language = entry.target_language;
        plan.source_function = entry.source_symbol;
        plan.target_function = entry.target_symbol;

        // Derive parameter count from known signatures if available.
        auto it = semantic.signatures.find(entry.target_symbol);
        plan.param_count = (it != semantic.signatures.end())
                               ? it->second.param_types.size()
                               : 0;

        result.call_plans.push_back(std::move(plan));
    }

    if (V) {
        std::cerr << "[stage/marshal] " << result.call_plans.size()
                  << " call plan(s)\n";
    }

    result.success = true;
    return result;
}

}  // namespace polyglot::tools
