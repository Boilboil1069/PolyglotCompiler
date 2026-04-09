#pragma once
// ============================================================================
// stage_packaging.h — Stage 6: aux file writing, object emission, linking
// ============================================================================

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

// Packaging result
struct PackagingResult {
    bool success{false};
    std::string output_path;             // final output (exe / obj)
    std::string obj_path;                // emitted object file path
    frontends::Diagnostics diagnostics;
};

// ────────────────────────────────────────────────────────────────────────────

/// Stage 6: emit aux files, write object, optionally invoke the system linker.
///
/// @param settings   Driver configuration (output paths, mode, obj_format, …)
/// @param backend    Result of RunBackendStage()
/// @param bridge     Result of RunBridgeStage() (provides descriptor_file path)
/// @param aux_dir    Aux output directory (may be empty)
/// @param stem       Source file stem used for naming aux files
PackagingResult RunPackagingStage(const DriverSettings     &settings,
                                  const BackendResult       &backend,
                                  const BridgeResult        &bridge,
                                  const std::string         &aux_dir,
                                  const std::string         &stem);

}  // namespace polyglot::tools
