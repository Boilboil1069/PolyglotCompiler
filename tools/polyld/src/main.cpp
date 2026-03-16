// polyld standalone entry point
// Links against linker_lib for core Linker / PolyglotLinker functionality.

#include "tools/polyld/include/linker.h"
#include "tools/polyld/include/polyglot_linker.h"

#include <iostream>
#include <string>

int main(int argc, char **argv) {
    using namespace polyglot::linker;

    LinkerConfig config;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-o" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "-L" && i + 1 < argc) {
            config.library_paths.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-L") {
            config.library_paths.push_back(arg.substr(2));
        } else if (arg == "-l" && i + 1 < argc) {
            config.libraries.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-l") {
            config.libraries.push_back(arg.substr(2));
        } else if (arg == "-e" && i + 1 < argc) {
            config.entry_point = argv[++i];
        } else if (arg == "--entry" && i + 1 < argc) {
            config.entry_point = argv[++i];
        } else if (arg == "-T" && i + 1 < argc) {
            config.linker_script = argv[++i];
        } else if (arg == "-static") {
            config.static_link = true;
        } else if (arg == "-shared") {
            config.output_format = OutputFormat::kSharedLibrary;
            config.static_link = false;
        } else if (arg == "-r" || arg == "--relocatable") {
            config.output_format = OutputFormat::kRelocatable;
        } else if (arg == "--strip-all" || arg == "-s") {
            config.strip_all = true;
        } else if (arg == "--strip-debug" || arg == "-S") {
            config.strip_debug = true;
        } else if (arg == "--gc-sections") {
            config.gc_sections = true;
        } else if (arg == "--no-undefined") {
            config.no_undefined = true;
        } else if (arg == "--allow-multiple-definition") {
            config.allow_multiple_definition = true;
        } else if (arg == "-pie" || arg == "--pie") {
            config.pie = true;
        } else if (arg == "--build-id") {
            config.build_id = true;
        } else if (arg == "--icf") {
            config.icf = true;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--trace") {
            config.trace = true;
        } else if (arg == "-m" && i + 1 < argc) {
            std::string arch = argv[++i];
            if (arch == "elf_x86_64") {
                config.target_arch = TargetArch::kX86_64;
            } else if (arch == "aarch64linux" || arch == "aarch64elf") {
                config.target_arch = TargetArch::kAArch64;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: polyld [options] files...\n"
                      << "\nOptions:\n"
                      << "  -o <file>        Set output file name\n"
                      << "  -e <symbol>      Set entry point symbol\n"
                      << "  -L <dir>         Add library search path\n"
                      << "  -l <lib>         Link with library\n"
                      << "  -T <script>      Use linker script\n"
                      << "  -static          Create static executable\n"
                      << "  -shared          Create shared library\n"
                      << "  -r               Create relocatable output\n"
                      << "  -s, --strip-all  Strip all symbols\n"
                      << "  -S, --strip-debug Strip debug symbols\n"
                      << "  --gc-sections    Garbage collect unused sections\n"
                      << "  --no-undefined   Report undefined symbols as errors\n"
                      << "  --pie            Create position-independent executable\n"
                      << "  -v, --verbose    Verbose output\n"
                      << "  --trace          Trace file loading\n"
                      << "  -h, --help       Show this help\n";
            return 0;
        } else if (arg[0] != '-') {
            config.input_files.push_back(arg);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
        }
    }

    if (config.input_files.empty()) {
        std::cerr << "polyld: no input files\n"
                  << "Usage: polyld [options] files...\n"
                  << "Try 'polyld --help' for more information.\n";
        return 1;
    }

    // Create and run linker
    Linker linker(config);

    // Run cross-language link resolution before the main link so that
    // glue stubs and resolved symbols are available for relocation.
    PolyglotLinker poly_linker(config);
    if (!poly_linker.ResolveLinks()) {
        // When cross-language link entries are registered, resolution
        // failures are fatal — unresolved symbols must not be silently
        // ignored because the resulting binary would crash at runtime.
        std::cerr << "polyld: cross-language link failed\n";
        for (const auto &e : poly_linker.GetErrors()) {
            std::cerr << "  " << e << "\n";
        }
        if (poly_linker.HasLinkEntries()) {
            return 1;
        }
        // If there are no registered link entries (pure native linking),
        // allow the pipeline to continue — the errors are just warnings
        // from an empty cross-language phase.
        if (config.verbose) {
            std::cerr << "polyld: continuing (no cross-language link entries)\n";
        }
    }

    if (!linker.Link()) {
        std::cerr << "polyld: link failed\n";
        for (const auto &err : linker.GetErrors()) {
            std::cerr << "  " << err << "\n";
        }
        return 1;
    }

    if (config.verbose) {
        const auto &stats = linker.GetStats();
        std::cout << "Linked " << stats.objects_loaded << " object files\n";
        std::cout << "Processed " << stats.relocations_processed << " relocations\n";
        std::cout << "Output size: " << stats.total_output_size << " bytes\n";
    }

    return 0;
}
