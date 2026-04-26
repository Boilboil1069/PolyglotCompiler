cmake_minimum_required(VERSION 3.20)
project(PolyglotCompiler VERSION 1.1.0 LANGUAGES C CXX)

# ============================================================================
# Central version header — generated from common/include/version.h.in
# All C++ sources should #include "common/include/version.h" instead of
# hard-coding version strings.
# ============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include/version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include/version.h"
    @ONLY
)
# Also emit a plain-text version file for packaging scripts.
# NOTE: named VERSION.txt (not VERSION) to avoid collision with the C++
# standard library <version> header when CMAKE_SOURCE_DIR is on -I.
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
    "${PROJECT_VERSION}\n${PROJECT_NAME}\n")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build shared libraries by default so common code is compiled once as
# DLLs / .so / .dylib instead of being duplicated in every executable.
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" ON)

if(BUILD_SHARED_LIBS)
    # On Windows (MSVC) automatically export all symbols from DLLs so we
    # do not need __declspec(dllexport/dllimport) macros on every function.
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

    # Ensure DLLs are placed next to executables so they can be found at
    # runtime without modifying PATH.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Set RPATH so executables find shared libraries in the same directory
    # or in ../lib on Linux/macOS without needing LD_LIBRARY_PATH.
    if(NOT WIN32)
        if(APPLE)
            # macOS uses @executable_path / @loader_path (not $ORIGIN).
            # - @executable_path/../Frameworks : Qt frameworks + custom dylibs
            #   inside the .app bundle (Contents/Frameworks/).
            # - @executable_path/../../..      : build-tree fallback — the
            #   binary lives at <build>/polyui.app/Contents/MacOS/polyui, and
            #   dylibs are produced directly in <build>/.
            set(CMAKE_MACOSX_RPATH TRUE)
            set(CMAKE_INSTALL_RPATH
                "@executable_path"
                "@executable_path/../Frameworks"
                "@executable_path/../lib"
                "@executable_path/../../.."
            )
            set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        else()
            set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
            set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        endif()
    endif()
endif()

# Platform-portable compile options
if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    # Suppress macOS linker warnings about duplicate libraries from transitive deps
    if(APPLE)
        add_link_options("LINKER:-no_warn_duplicate_libraries")
    endif()
endif()

# ============================================================================
# Quality gate options: sanitizers, coverage, clang-tidy
# ============================================================================

option(POLYGLOT_ENABLE_ASAN   "Enable AddressSanitizer (GCC/Clang)"   OFF)
option(POLYGLOT_ENABLE_UBSAN  "Enable UndefinedBehaviorSanitizer"     OFF)
option(POLYGLOT_ENABLE_COVERAGE "Enable code coverage (gcov/llvm-cov)" OFF)

# -- Sanitizers (not supported on MSVC) --------------------------------------
if(NOT MSVC)
    set(_SANITIZER_FLAGS "")
    if(POLYGLOT_ENABLE_ASAN)
        list(APPEND _SANITIZER_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer")
    endif()
    if(POLYGLOT_ENABLE_UBSAN)
        list(APPEND _SANITIZER_FLAGS "-fsanitize=undefined" "-fno-sanitize-recover=all")
    endif()
    if(_SANITIZER_FLAGS)
        add_compile_options(${_SANITIZER_FLAGS})
        add_link_options(${_SANITIZER_FLAGS})
    endif()
endif()

# -- Code coverage (GCC/Clang only) ------------------------------------------
if(POLYGLOT_ENABLE_COVERAGE AND NOT MSVC)
    # -fprofile-update=atomic prevents counter races (and lcov negative counts)
    # in multi-threaded code such as runtime/services/threading.
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic)
    add_link_options(--coverage)
endif()

# Required for runtime dynamic loading (dlopen/dlsym) and threading
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake)

# ============================================================================
# Modular subdirectory structure
#
# Order matters for inter-library dependencies:
#   common → middle → frontends → backends → runtime → tools → tests
# ============================================================================

# Enable CTest at the top level so that tests registered in subdirectories
# are discoverable by `ctest` from the build root.
enable_testing()

add_subdirectory(common)
add_subdirectory(middle)
add_subdirectory(frontends)
add_subdirectory(backends)
add_subdirectory(runtime)
add_subdirectory(tools)
add_subdirectory(tests)
