# FFI Library - winuxcore.dll, FFI ABI v2 (see winux_ffi.h)
#
# Thin shell over winuxcmd-commands: ffi.cpp only validates the ABI
# context, borrows cwd/env/std-handles for the call, and forwards to
# CommandRegistry::dispatch. Command implementations are not modified.
add_library(winuxcmd_ffi SHARED
    ffi.cpp
    winuxcore.def
)

# Add PCH for FFI library
target_precompile_headers(winuxcmd_ffi PRIVATE ${CMAKE_SOURCE_DIR}/src/pch/pch.h)

# Set output name to winuxcore.dll
set_target_properties(winuxcmd_ffi PROPERTIES
    OUTPUT_NAME "winuxcore"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Export surface comes from winuxcore.def alone; no dllexport macro and no
# --export-all-symbols anywhere in this directory.
target_compile_definitions(winuxcmd_ffi PRIVATE
    _UNICODE
    UNICODE
    _CRT_DISABLE_PERFCRIT_LOCKS
    _HAS_RTTI=0
)

# Set C++ standard and options
set_target_properties(winuxcmd_ffi PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Compile options
target_compile_options(winuxcmd_ffi PRIVATE
    $<$<CONFIG:Release>:/O2 /Os>
    /GR-  # Disable RTTI (matches winuxcmd-commands)
)

# WHOLEARCHIVE: command self-registration units must not be discarded,
# exactly as for the winuxcmd executable target.
if (MSVC)
    target_link_options(winuxcmd_ffi PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        /WHOLEARCHIVE:winuxcmd-commands
    )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(winuxcmd_ffi PRIVATE
        -Wl,--whole-archive $<TARGET_FILE:winuxcmd-commands> -Wl,--no-whole-archive
    )
endif ()

# CRT form (DESIGN D7): static CRT. The root CMakeLists maps
# USE_STATIC_CRT=ON (all presets) to CMAKE_MSVC_RUNTIME_LIBRARY=
# MultiThreaded, which already covers this target; the explicit /MT below
# keeps the DLL on the same form even if configured standalone.
if (USE_STATIC_CRT AND MSVC)
    target_compile_options(winuxcmd_ffi PRIVATE /MT /Os /Oy /GS-)
endif ()

# MSVC module settings (the shell imports std/core/utils like the old one)
if (MSVC)
    target_compile_options(winuxcmd_ffi PRIVATE
        /std:c++latest
        /experimental:module
        /interface
    )
    target_include_directories(winuxcmd_ffi PRIVATE "${MODULES_DIR}")
endif ()

# Add include directories
target_include_directories(winuxcmd_ffi PRIVATE ${SRC_DIR} ${CMAKE_BINARY_DIR})

# Link Windows libraries and the commands static library
target_link_libraries(winuxcmd_ffi PRIVATE
    kernel32
    user32
    advapi32
    winuxcmd-commands
)

# ---- Tier table (generated at build time from the audited manifest) ----
set(WINUX_FFI_TIERS_OUT "${CMAKE_CURRENT_BINARY_DIR}/tiers.tsv")
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${WINUX_FFI_TIERS_OUT}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/generate_tiers.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tiers-manifest.tsv"
            "${CMAKE_SOURCE_DIR}/src/commands"
            "${PROJECT_VERSION}"
            "${WINUX_FFI_TIERS_OUT}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/generate_tiers.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tiers-manifest.tsv"
        COMMENT "Generating FFI tiers.tsv"
        VERBATIM
    )
    add_custom_target(winuxcmd_ffi_tiers DEPENDS "${WINUX_FFI_TIERS_OUT}")
    add_dependencies(winuxcmd_ffi winuxcmd_ffi_tiers)
else ()
    message(WARNING
        "Python3 not found: tiers.tsv falls back to the audited manifest "
        "verbatim (spawn-default rows for unclassified commands are then "
        "the host's responsibility).")
    add_custom_command(
        OUTPUT "${WINUX_FFI_TIERS_OUT}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tiers-manifest.tsv"
            "${WINUX_FFI_TIERS_OUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tiers-manifest.tsv"
    )
    add_custom_target(winuxcmd_ffi_tiers DEPENDS "${WINUX_FFI_TIERS_OUT}")
    add_dependencies(winuxcmd_ffi winuxcmd_ffi_tiers)
endif ()

# Stage the versioned side-by-side layout (DESIGN 4.1) in the build tree:
#   <build>/ffi-layout/usr/bin/ffi/v<version>/{winuxcore.dll,tiers.tsv}
# The pinned-hash manifest (winuxcore.sha256) is written by the packaging /
# harness tooling that consumes this layout, never by the DLL build itself.
set(WINUX_FFI_LAYOUT_DIR
    "${CMAKE_BINARY_DIR}/ffi-layout/usr/bin/ffi/v${PROJECT_VERSION}")
add_custom_command(TARGET winuxcmd_ffi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WINUX_FFI_LAYOUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:winuxcmd_ffi> "${WINUX_FFI_LAYOUT_DIR}/winuxcore.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINUX_FFI_TIERS_OUT}" "${WINUX_FFI_LAYOUT_DIR}/tiers.tsv"
    COMMENT "Staging FFI layout v${PROJECT_VERSION}"
)

# Install into the same versioned layout under usr/bin (CMAKE_INSTALL_BINDIR
# is forced to usr/bin by the root lists before this directory is added).
install(TARGETS winuxcmd_ffi
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/ffi/v${PROJECT_VERSION}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/ffi/v${PROJECT_VERSION}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_BINDIR}/ffi/v${PROJECT_VERSION}"
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/winux_ffi.h"
    "${WINUX_FFI_TIERS_OUT}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/ffi/v${PROJECT_VERSION}"
)
