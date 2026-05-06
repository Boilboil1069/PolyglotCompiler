/**
 * @file     main.cpp
 * @brief    Polyglot linker implementation
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// polyld standalone entry point
// Links against linker_lib for core Linker / PolyglotLinker functionality.

#include <cctype>
#include <iostream>
#include <string>

#include "common/include/binary_container.h"
#include "common/include/target_triple.h"
#include "tools/common/include/effective_settings_loader.h"
#include "tools/polyld/include/linker.h"
#include "tools/polyld/include/polyglot_linker.h"

int main(int argc, char **argv) {
  if (auto rc = polyglot::tools::common::HandleSettingsCliFlags(argc, argv); rc.has_value()) {
    return *rc;
  }
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
      // BIN-7: `-T <triple>` carries an LLVM-style target triple from
      // polyc through to the linker.  When the value parses as a
      // recognised triple it is stored on `LinkerConfig::target_triple`
      // and the legacy linker_script slot is left untouched.  Otherwise
      // we preserve the historical meaning (linker script path) so old
      // build scripts using `-T script.ld` keep working.
      std::string val = argv[++i];
      auto parse = ::polyglot::common::ParseTargetTriple(val);
      if (parse.ok()) {
        config.target_triple = *parse.triple;
      } else {
        config.linker_script = val;
      }
    } else if (arg.rfind("--target=", 0) == 0) {
      // BIN-7: explicit triple; rejects unparseable specs early so the
      // rest of the pipeline never sees a half-resolved target.
      std::string spec = arg.substr(9);
      auto parse = ::polyglot::common::ParseTargetTriple(spec);
      if (!parse.ok()) {
        std::cerr << "polyld: --target=" << spec << ": polyld-err-E1100 "
                  << (parse.error ? parse.error->message : "invalid triple") << "\n";
        return 1;
      }
      config.target_triple = *parse.triple;
    } else if (arg.rfind("--container=", 0) == 0) {
      std::string c = arg.substr(12);
      if (c == "auto")        config.container = ::polyglot::common::BinaryContainer::kAuto;
      else if (c == "elf")    config.container = ::polyglot::common::BinaryContainer::kELF;
      else if (c == "pe")     config.container = ::polyglot::common::BinaryContainer::kPE;
      else if (c == "macho")  config.container = ::polyglot::common::BinaryContainer::kMachO;
      else if (c == "wasm")   config.container = ::polyglot::common::BinaryContainer::kWasm;
      else {
        std::cerr << "polyld: --container=" << c
                  << ": polyld-err-E1101 unknown container (auto|elf|pe|macho|wasm)\n";
        return 1;
      }
    } else if (arg.rfind("--subsystem=", 0) == 0) {
      config.subsystem = arg.substr(12);
    } else if (arg == "-static") {
      config.static_link = true;
    } else if (arg == "-shared") {
      config.output_format = OutputFormat::kSharedLibrary;
      config.static_link = false;
    } else if (arg == "-r" || arg == "--relocatable") {
      config.output_format = OutputFormat::kRelocatable;
    } else if (arg == "--pe" || arg == "--output-format=pe") {
      config.output_format = OutputFormat::kPEExecutable;
    } else if (arg == "--elf" || arg == "--output-format=elf") {
      config.output_format = OutputFormat::kExecutable;
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
    } else if (arg == "--ploy-desc" && i + 1 < argc) {
      config.ploy_descriptor_files.push_back(argv[++i]);
    } else if (arg == "--aux-dir" && i + 1 < argc) {
      config.aux_dir = argv[++i];
    } else if (arg == "--allow-adhoc-link") {
      config.allow_adhoc_link = true;
    } else if (arg == "--def" && i + 1 < argc) {
      // BIN-4: load exports from a .def file.
      config.def_files.push_back(argv[++i]);
    } else if (arg.rfind("/EXPORT:", 0) == 0) {
      // BIN-4: cl-style export descriptor on the command line.
      config.cli_export_specs.push_back(arg.substr(8));
    } else if (arg == "--export" && i + 1 < argc) {
      // GNU-style equivalent of /EXPORT: for cross-platform driver scripts.
      config.cli_export_specs.push_back(argv[++i]);
    } else if (arg == "--dll-name" && i + 1 < argc) {
      config.dll_name = argv[++i];
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
                << "  -T <triple>      Set target triple (BIN-7); falls back to script\n"
                << "                   path when the value does not parse as a triple\n"
                << "  --target=<t>     Set target triple, e.g. x86_64-pc-windows-msvc\n"
                << "  --container=<c>  Force binary container: auto|elf|pe|macho|wasm\n"
                << "  --subsystem=<s>  PE subsystem override (console|windows|...)\n"
                << "  -static          Create static executable\n"
                << "  -shared          Create shared library\n"
                << "  -r               Create relocatable output\n"
                << "  --pe             Emit Windows PE32+ executable (.exe)\n"
                << "  --elf            Emit ELF executable (Linux default)\n"
                << "  -s, --strip-all  Strip all symbols\n"
                << "  -S, --strip-debug Strip debug symbols\n"
                << "  --gc-sections    Garbage collect unused sections\n"
                << "  --no-undefined   Report undefined symbols as errors\n"
                << "  --pie            Create position-independent executable\n"
                << "  --ploy-desc <f>  Load cross-language descriptors from file\n"
                << "  --aux-dir <dir>  Auto-discover descriptors from aux directory\n"
                << "  --allow-adhoc-link  Allow ad-hoc cross-language stubs\n"
                << "  --def <file>     Load PE exports from a .def file\n"
                << "  /EXPORT:<spec>   Add one PE export (cl-style)\n"
                << "  --export <spec>  Add one PE export (GNU-style)\n"
                << "  --dll-name <name> Override DLL name in export directory\n"
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

  // Auto-select PE32+ output when the user asked for an .exe but did not
  // explicitly pick a format.  This makes `polyld foo.o -o foo.exe` produce
  // a Windows-runnable image without requiring an additional --pe flag.
  if (config.output_format == OutputFormat::kExecutable) {
    const std::string &ofile = config.output_file;
    if (ofile.size() >= 4) {
      std::string suffix = ofile.substr(ofile.size() - 4);
      for (auto &c : suffix)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (suffix == ".exe")
        config.output_format = OutputFormat::kPEExecutable;
    }
#ifdef _WIN32
    // Even without an .exe suffix, default to PE on Windows hosts so the
    // produced artefact is loadable by the host loader.  Users who really
    // want a foreign-format executable can pass --elf explicitly.
    if (config.output_format == OutputFormat::kExecutable)
      config.output_format = OutputFormat::kPEExecutable;
#endif
  }

  // Create and run linker
  Linker linker(config);

  // Load cross-language descriptors and run resolution
  PolyglotLinker poly_linker(config);

  // Load descriptors from explicit --ploy-desc files
  for (const auto &desc_file : config.ploy_descriptor_files) {
    if (!poly_linker.LoadDescriptorFile(desc_file)) {
      std::cerr << "polyld: failed to load descriptor file: " << desc_file << "\n";
      return 1;
    }
    if (config.verbose) {
      std::cerr << "polyld: loaded descriptors from " << desc_file << "\n";
    }
  }

  // Auto-discover descriptors from --aux-dir
  if (!config.aux_dir.empty()) {
    poly_linker.DiscoverDescriptors(config.aux_dir);
    if (config.verbose) {
      std::cerr << "polyld: discovered descriptors from " << config.aux_dir << "\n";
    }
  }

  // Run cross-language link resolution
  if (!poly_linker.ResolveLinks()) {
    std::cerr << "polyld: cross-language link failed\n";
    for (const auto &e : poly_linker.GetErrors()) {
      std::cerr << "  " << e << "\n";
    }
    if (poly_linker.HasLinkEntries()) {
      return 1;
    }
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
