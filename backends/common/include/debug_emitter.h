#pragma once

#include <string>

#include "backends/common/include/debug_info.h"

namespace polyglot::backends {

// Options for controlling debug information emission
struct DebugEmitOptions {
    bool emit_dwarf{false};       // Emit DWARF debug info (ELF targets)
    bool emit_pdb{false};         // Emit PDB debug info (Windows targets)
    bool emit_source_map{true};   // Emit JSON source map
    bool include_line_info{true}; // Include line number information
    bool include_types{true};     // Include type information
    bool include_variables{true}; // Include variable information
    int dwarf_version{5};         // DWARF version to emit (4 or 5)
};

// Debug information emitter supporting multiple formats
//
// Supports:
// - DWARF 5 format for Linux/Unix/macOS (ELF/Mach-O object files)
// - PDB format for Windows targets
// - JSON source maps for custom tooling
//
// Usage:
//   DebugInfoBuilder builder;
//   // ... populate builder with debug info ...
//   DebugEmitter::EmitDWARF(builder, "output.o");
//   DebugEmitter::EmitPDB(builder, "output.pdb");
//   DebugEmitter::EmitSourceMap(builder, "output.map");
//
class DebugEmitter {
public:
    // Write a source map JSON sidecar to the given path.
    // Returns true on success, false on I/O error.
    static bool EmitSourceMap(const DebugInfoBuilder &info, const std::string &path);

    // Emit DWARF debug information as an ELF object file.
    // Generates .debug_info, .debug_line, .debug_str, .debug_abbrev,
    // .debug_frame, and .debug_aranges sections.
    // Returns true on success, false on error.
    static bool EmitDWARF(const DebugInfoBuilder &info, const std::string &path);

    // Emit PDB (Program Database) debug information for Windows targets.
    // Generates a Microsoft-compatible PDB file with type information,
    // symbol records, and source file mappings.
    // Returns true on success, false on error.
    static bool EmitPDB(const DebugInfoBuilder &info, const std::string &path);
};

}  // namespace polyglot::backends
