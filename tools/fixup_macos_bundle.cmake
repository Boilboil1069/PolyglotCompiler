# fixup_macos_bundle.cmake — Fix install_name paths in the macOS app bundle.
#
# Called as:
#   cmake -D BUNDLE_DIR=<path/to/polyui.app> -P fixup_macos_bundle.cmake
#
# Resolves "ERROR: Unexpected prefix @executable_path" warnings from
# macdeployqt by replacing @rpath references with direct
# @executable_path/../Frameworks/ paths in the copied project dylibs and
# stripping build-tree RPATHs that are meaningless inside the .app bundle.

if(NOT BUNDLE_DIR)
    message(FATAL_ERROR "BUNDLE_DIR must be set")
endif()

set(_fw_dir "${BUNDLE_DIR}/Contents/Frameworks")
set(_exe_dir "${BUNDLE_DIR}/Contents/MacOS")

if(NOT EXISTS "${_fw_dir}")
    return()
endif()

# ---------------------------------------------------------------------------
# Collect every project dylib that was copied into Frameworks/
# ---------------------------------------------------------------------------
file(GLOB _dylibs "${_fw_dir}/*.dylib")

set(_dylib_names "")
foreach(_d IN LISTS _dylibs)
    get_filename_component(_n "${_d}" NAME)
    list(APPEND _dylib_names "${_n}")
endforeach()

# ---------------------------------------------------------------------------
# RPATHs baked in by CMAKE_INSTALL_RPATH – only useful in the build tree
# ---------------------------------------------------------------------------
set(_stale_rpaths
    "@executable_path"
    "@executable_path/../Frameworks"
    "@executable_path/../lib"
    "@executable_path/../../.."
)

# ---------------------------------------------------------------------------
# Fix each project dylib in Contents/Frameworks/
# ---------------------------------------------------------------------------
foreach(_dylib IN LISTS _dylibs)
    get_filename_component(_name "${_dylib}" NAME)

    # 1. Set the dylib's own install-name id so it is found via
    #    @executable_path/../Frameworks/<name>
    execute_process(
        COMMAND install_name_tool -id
            "@executable_path/../Frameworks/${_name}" "${_dylib}"
        ERROR_QUIET OUTPUT_QUIET
    )

    # 2. Rewrite cross-references to sibling project dylibs:
    #    @rpath/<dep> → @executable_path/../Frameworks/<dep>
    foreach(_dep IN LISTS _dylib_names)
        if(NOT "${_dep}" STREQUAL "${_name}")
            execute_process(
                COMMAND install_name_tool -change
                    "@rpath/${_dep}"
                    "@executable_path/../Frameworks/${_dep}"
                    "${_dylib}"
                ERROR_QUIET OUTPUT_QUIET
            )
        endif()
    endforeach()

    # 3. Strip build-tree RPATHs (harmless if already absent)
    foreach(_rp IN LISTS _stale_rpaths)
        execute_process(
            COMMAND install_name_tool -delete_rpath "${_rp}" "${_dylib}"
            ERROR_QUIET OUTPUT_QUIET
        )
    endforeach()

    # 4. Add @loader_path so each dylib resolves siblings in the same dir
    execute_process(
        COMMAND install_name_tool -add_rpath "@loader_path" "${_dylib}"
        ERROR_QUIET OUTPUT_QUIET
    )
endforeach()

# ---------------------------------------------------------------------------
# Fix the main executable's references to project dylibs and strip
# build-tree RPATHs that macdeployqt cannot resolve.
# Keep only @executable_path/../Frameworks which is the standard bundle path.
# ---------------------------------------------------------------------------
set(_exe_stale_rpaths
    "@executable_path"
    "@executable_path/../lib"
    "@executable_path/../../.."
)

file(GLOB _exes "${_exe_dir}/*")
foreach(_exe IN LISTS _exes)
    if(IS_DIRECTORY "${_exe}")
        continue()
    endif()
    foreach(_dep IN LISTS _dylib_names)
        execute_process(
            COMMAND install_name_tool -change
                "@rpath/${_dep}"
                "@executable_path/../Frameworks/${_dep}"
                "${_exe}"
            ERROR_QUIET OUTPUT_QUIET
        )
    endforeach()
    foreach(_rp IN LISTS _exe_stale_rpaths)
        execute_process(
            COMMAND install_name_tool -delete_rpath "${_rp}" "${_exe}"
            ERROR_QUIET OUTPUT_QUIET
        )
    endforeach()
endforeach()

list(LENGTH _dylib_names _count)
message(STATUS "Bundle fixup: patched install names for ${_count} dylibs in ${_fw_dir}")
