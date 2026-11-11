cmake_minimum_required(VERSION 3.30.0)
# CMake Module start from 3.30.0
# Ignore some warnings
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)
if (POLICY CMP0158)
    cmake_policy(SET CMP0158 OLD)
endif()
if (POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()
string(REPLACE "/EHc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# =============
# Runtime Library Configuration
# Option to choose between static (MT) and dynamic (MD) CRT
option(USE_STATIC_CRT "Use statically linked CRT (MT) instead of dynamically linked (MD)" ON)
# Build mode selection: DEV for local development, RELEASE for production
# Handle legacy boolean values and invalid settings
if(DEFINED BUILD_MODE)
    # Convert boolean values to proper strings
    if(BUILD_MODE STREQUAL "ON" OR BUILD_MODE STREQUAL "OFF" OR BUILD_MODE STREQUAL "TRUE" OR BUILD_MODE STREQUAL "FALSE")
        message(STATUS "Converting legacy BUILD_MODE '${BUILD_MODE}' to 'DEV'")
        unset(BUILD_MODE CACHE)
        set(BUILD_MODE "DEV" CACHE STRING "Build mode (DEV or RELEASE)" FORCE)
    endif()

    # Validate build mode
    if(NOT BUILD_MODE STREQUAL "DEV" AND
       NOT BUILD_MODE STREQUAL "RELEASE" AND
       NOT BUILD_MODE STREQUAL "DEBUG_RELEASE" AND
       NOT BUILD_MODE STREQUAL "RELEASE_EXAMPLES")
        message(WARNING "Invalid BUILD_MODE '${BUILD_MODE}', resetting to 'DEV'")
        unset(BUILD_MODE CACHE)
        set(BUILD_MODE "DEV" CACHE STRING "Build mode (DEV, RELEASE, DEBUG_RELEASE, or RELEASE_EXAMPLES)" FORCE)
    endif()
else()
    # Auto-detect build mode based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(BUILD_MODE "RELEASE" CACHE STRING "Build mode (DEV or RELEASE)" FORCE)
        message(STATUS "Auto-setting BUILD_MODE to RELEASE based on CMAKE_BUILD_TYPE=Release")
    else()
        set(BUILD_MODE "DEV" CACHE STRING "Build mode (DEV or RELEASE)" FORCE)
    endif()
endif()
set_property(CACHE BUILD_MODE PROPERTY STRINGS "DEV" "RELEASE" "DEBUG_RELEASE" "RELEASE_EXAMPLES")

# Enable PCRE2 for GNU grep -P / --perl-regexp. JIT stays off by default to
# keep the Windows-native bundle small and avoid executable-memory allocation.
option(WINUXCMD_ENABLE_PCRE2 "Enable PCRE2-backed grep -P support" ON)
option(WINUXCMD_PCRE2_JIT "Enable PCRE2 JIT support" OFF)

# WinuxCmd 2.0 command library (#1488 mechanical replacement). Commands in
# WINUXCMD_RUST2_COMMANDS have no C++ implementation any more: their
# src/commands/<name>.cpp is a registration whose handler calls the rust2
# staticlib (rust2/crates/winuxcmd-ffi, built by cargo below). The option
# defaults ON only when cargo is found, so a machine without Rust still
# configures and builds; it then ships without the commands listed here
# (replaced commands are not dual-tracked, see rust2/REPLACEMENT-RECIPE.md).
find_program(WINUXCMD_CARGO_EXECUTABLE cargo
        HINTS "$ENV{CARGO_HOME}/bin" "$ENV{USERPROFILE}/.cargo/bin")
if(WINUXCMD_CARGO_EXECUTABLE)
    set(WINUXCMD_ENABLE_RUST2_DEFAULT ON)
else()
    set(WINUXCMD_ENABLE_RUST2_DEFAULT OFF)
endif()
option(WINUXCMD_ENABLE_RUST2
       "Link the rust2 command library (replaced commands) into winuxcmd.exe; needs cargo"
       ${WINUXCMD_ENABLE_RUST2_DEFAULT})
if(WINUXCMD_ENABLE_RUST2 AND NOT WINUXCMD_CARGO_EXECUTABLE)
    message(FATAL_ERROR
            "WINUXCMD_ENABLE_RUST2=ON but cargo was not found. Install Rust "
            "(https://rustup.rs) or configure with -DWINUXCMD_ENABLE_RUST2=OFF.")
endif()
# Commands whose implementation is the rust2 library (one name per replaced
# applet; the recipe's per-command step appends here).
set(WINUXCMD_RUST2_COMMANDS "true;false;pwd;printenv;basename;dirname;readlink;realpath;mkdir;rmdir;test;test_bracket;sync;whoami;nproc;unlink;sleep;hostname;hostid;users;tsort;join;timeout;expand;unexpand;numfmt")

# Command-set edition shipped by this build (#1438, ruling of 2026-10-30):
#   default - every registered command stays runnable through the multicall
#             dispatcher, but the entry links for the shell-builtin shadow
#             names (see src/core/package_edition.h) are not materialized;
#   full    - every registered entry link is materialized (asset names carry
#             a "-full" suffix; this edition is not distributed via wpm).
set(WINUXCMD_PACKAGE_EDITION "default" CACHE STRING
    "Entry-link edition: default (shell-builtin shadow names stripped) or full (all entries)")
set_property(CACHE WINUXCMD_PACKAGE_EDITION PROPERTY STRINGS "default" "full")
if(NOT WINUXCMD_PACKAGE_EDITION STREQUAL "default" AND
   NOT WINUXCMD_PACKAGE_EDITION STREQUAL "full")
    message(FATAL_ERROR
            "Unsupported WINUXCMD_PACKAGE_EDITION='${WINUXCMD_PACKAGE_EDITION}'. "
            "Use default or full.")
endif()
if(WINUXCMD_PACKAGE_EDITION STREQUAL "full")
    set(WINUXCMD_PACKAGE_EDITION_SUFFIX "-full")
else()
    set(WINUXCMD_PACKAGE_EDITION_SUFFIX "")
endif()

set(WINUXCMD_CXX_DIALECT "23" CACHE STRING "C++ dialect canary: 23, 26, or latest")
set_property(CACHE WINUXCMD_CXX_DIALECT PROPERTY STRINGS "23" "26" "latest")
if (NOT WINUXCMD_CXX_DIALECT STREQUAL "23" AND
    NOT WINUXCMD_CXX_DIALECT STREQUAL "26" AND
    NOT WINUXCMD_CXX_DIALECT STREQUAL "latest")
    message(FATAL_ERROR "Unsupported WINUXCMD_CXX_DIALECT='${WINUXCMD_CXX_DIALECT}'. Use 23, 26, or latest.")
endif ()
set(WINUXCMD_CXX_STANDARD 23)
set(WINUXCMD_CXX_DIALECT_NOTE "pending compiler detection")

# Option to enable FFI library
# The FFI sources under src/ffi are not tracked in git; a checkout that carries
# them can build winuxcore.dll with -DBUILD_FFI=ON (all presets still do).
option(BUILD_FFI "Build the winuxcore FFI library (requires src/ffi sources)" OFF)

# Generate the map info(For the developers to collect binary information)
option(GENERATE_MAP_INFO "Generate_MAP_INFO" ON)
# Merge Every commands.cpp into one source file(It could decrease template volume costs)
option(ENABLE_UNITY_BUILD "Enable Unity Build" ON)
# MSVC Profile-Guided Optimization (see scripts/build-pgo.ps1):
# WINUXCMD_PGO_INSTRUMENT links with /GENPROFILE for the training build;
# WINUXCMD_PGO_USE_DIR points the final link at the collected .pgd/.pgc.
option(WINUXCMD_PGO_INSTRUMENT "Link with /GENPROFILE for PGO training" OFF)
option(WINUXCMD_PGO_USE "Link with /USEPROFILE; winuxcmd.pgd/.pgc must be copied into the build dir first" OFF)
set(WINUXCMD_PGO_FLAGS "")
if(WINUXCMD_PGO_INSTRUMENT)
    set(WINUXCMD_PGO_FLAGS "/GENPROFILE")
elseif(WINUXCMD_PGO_USE)
    set(WINUXCMD_PGO_FLAGS "/USEPROFILE")
endif()
# PGO requires whole-program optimization: objects compiled with /GL so the
# /LTCG link can instrument and optimize across translation units (LNK1264
# otherwise: "/GENPROFILE specified but required code generation failed").
if(NOT WINUXCMD_PGO_FLAGS STREQUAL "")
    add_compile_options($<$<CONFIG:Release>:/GL>)
endif()

# Configure options based on build mode
if(BUILD_MODE STREQUAL "DEV")
    # Development mode: optimize for fast incremental builds
    message(STATUS "Configuring for DEVELOPMENT mode")
    set(ENABLE_TESTS_DEFAULT ON)
    set(GENERATE_MAP_INFO_DEFAULT ON)
    set(ENABLE_UNITY_BUILD_DEFAULT OFF)  # Unity build slows down incremental compilation
elseif(BUILD_MODE STREQUAL "RELEASE")
    # Release mode: optimize for performance and size
    message(STATUS "Configuring for RELEASE mode")
    set(ENABLE_TESTS_DEFAULT OFF)
    set(GENERATE_MAP_INFO_DEFAULT OFF)
    set(ENABLE_UNITY_BUILD_DEFAULT ON)   # Unity build improves performance and reduces size
elseif(BUILD_MODE STREQUAL "DEBUG_RELEASE")
    # Debug-Release mode: Release optimizations with debug features
    message(STATUS "Configuring for DEBUG-RELEASE mode")
    set(ENABLE_TESTS_DEFAULT ON)
    set(GENERATE_MAP_INFO_DEFAULT ON)
    set(ENABLE_UNITY_BUILD_DEFAULT OFF)  # Disable Unity build for better debugging
elseif(BUILD_MODE STREQUAL "RELEASE_EXAMPLES")
    # Release mode with examples: optimize for performance and size, but build examples
    message(STATUS "Configuring for RELEASE_EXAMPLES mode")
    set(ENABLE_TESTS_DEFAULT OFF)
    set(GENERATE_MAP_INFO_DEFAULT OFF)
    set(ENABLE_UNITY_BUILD_DEFAULT ON)   # Unity build improves performance and reduces size
else()
    message(WARNING "Unknown BUILD_MODE '${BUILD_MODE}', using DEVELOPMENT settings")
    set(ENABLE_TESTS_DEFAULT ON)
    set(GENERATE_MAP_INFO_DEFAULT ON)
    set(ENABLE_UNITY_BUILD_DEFAULT ON)
endif()

# Apply default values based on build mode if not explicitly set
option(ENABLE_TESTS "Enable tests" ${ENABLE_TESTS_DEFAULT})
if(NOT DEFINED GENERATE_MAP_INFO_USER_VALUE)
    set(GENERATE_MAP_INFO ${GENERATE_MAP_INFO_DEFAULT} CACHE BOOL "Generate_MAP_INFO" FORCE)
endif()
if(NOT DEFINED ENABLE_UNITY_BUILD_USER_VALUE)
    set(ENABLE_UNITY_BUILD ${ENABLE_UNITY_BUILD_DEFAULT} CACHE BOOL "Enable Unity Build" FORCE)
endif()

if(BUILD_MODE STREQUAL "RELEASE" OR BUILD_MODE STREQUAL "RELEASE_EXAMPLES") 
endif()

if (USE_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
else ()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif ()

if (NOT CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)
endif ()

include(cmake/enable_import_std.cmake)
include(cmake/statistics_project.cmake)
include(cmake/ReadVersionFile.cmake)

# read from env
read_version_file()

project(WinuxCmd
        VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
        DESCRIPTION "Windows Compatible Linux Command Set"
        LANGUAGES CXX C
)

if(WINUXCMD_ENABLE_PCRE2)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_UNICODE ON CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_JIT ${WINUXCMD_PCRE2_JIT} CACHE BOOL "" FORCE)
    set(PCRE2_STATIC_PIC OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
            winuxcmd_pcre2
            URL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.gz
            URL_HASH SHA256=c08ae2388ef333e8403e670ad70c0a11f1eed021fd88308d7e02f596fcd9dc16
    )
    FetchContent_MakeAvailable(winuxcmd_pcre2)
    set(WINUXCMD_PCRE2_TARGET pcre2-8-static)

    # The release payload must ship the runtime only. PCRE2 registers
    # unconditional install() rules - headers, static import libs, CMake
    # package config, pkgconfig files, man pages, html/txt docs and the
    # pcre2-config script - which turned every standalone archive into a
    # full PCRE2 SDK tree (~7.8 MB of include/ lib/ share/ plus
    # usr/bin/pcre2-config that no consumer of the command package reads).
    # Registered after PCRE2's own rules, this pass strips that SDK tree
    # from every staged install tree - archive and installer alike - before
    # CPack packs it (unixwin/WinuxCmd#1438). Only PCRE2-populated paths are
    # removed, and the shared parent directories are pruned only when the
    # strip left them empty.
    install(CODE [[
        set(_sdk_prefix "${CMAKE_INSTALL_PREFIX}")
        file(GLOB _sdk_files
            "${_sdk_prefix}/include/pcre2*.h"
            "${_sdk_prefix}/lib/pcre2*.lib"
            "${_sdk_prefix}/lib/pkgconfig/libpcre2*.pc"
            "${_sdk_prefix}/bin/pcre2-config*"
            "${_sdk_prefix}/usr/bin/pcre2-config*")
        if(_sdk_files)
            file(REMOVE ${_sdk_files})
        endif()
        file(REMOVE_RECURSE
            "${_sdk_prefix}/lib/cmake/pcre2"
            "${_sdk_prefix}/share/doc/pcre2")
        file(GLOB _sdk_man_files
            "${_sdk_prefix}/share/man/man1/pcre2*"
            "${_sdk_prefix}/share/man/man3/pcre2*")
        if(_sdk_man_files)
            file(REMOVE ${_sdk_man_files})
        endif()
        # Deepest-first: a child directory must be judged (and removed)
        # before its parent, or the parent's glob still sees the child and
        # skips the prune. `bin` is pruned too because a fresh configure
        # lets PCRE2 drop pcre2-config there (GNUInstallDirs defaults the
        # bindir before the parent project pins usr/bin); `usr/bin` is
        # never pruned - the project's own payload lands there after this
        # strip.
        foreach(_sdk_dir lib/pkgconfig lib/cmake include
                share/man/man1 share/man/man3 share/man share/doc share
                lib bin)
            file(GLOB _sdk_children "${_sdk_prefix}/${_sdk_dir}/*")
            if(NOT _sdk_children)
                file(REMOVE_RECURSE "${_sdk_prefix}/${_sdk_dir}")
            endif()
        endforeach()
    ]])
endif()

set(WINUXCMD_IMPORT_STD_LEVELS "${CMAKE_CXX_COMPILER_IMPORT_STD}")
if (WINUXCMD_CXX_DIALECT STREQUAL "23")
    set(WINUXCMD_CXX_STANDARD 23)
    set(WINUXCMD_CXX_DIALECT_NOTE "CMake CXX23")
elseif (WINUXCMD_CXX_DIALECT STREQUAL "26")
    if (NOT DEFINED CMAKE_CXX_COMPILER_IMPORT_STD)
        set(CMAKE_CXX_COMPILER_IMPORT_STD "")
    endif()
    list(FIND CMAKE_CXX_COMPILER_IMPORT_STD 26 _WINUXCMD_IMPORT_STD_CXX26_INDEX)
    if (_WINUXCMD_IMPORT_STD_CXX26_INDEX GREATER_EQUAL 0)
        set(WINUXCMD_CXX_STANDARD 26)
        set(WINUXCMD_CXX_DIALECT_NOTE "native CMake CXX26 import std")
    else()
        set(WINUXCMD_CXX_STANDARD 23)
        set(WINUXCMD_CXX_DIALECT_NOTE "C++26 canary via compiler latest mode; import std levels=${WINUXCMD_IMPORT_STD_LEVELS}")
    endif()
elseif (WINUXCMD_CXX_DIALECT STREQUAL "latest")
    set(WINUXCMD_CXX_STANDARD 23)
    set(WINUXCMD_CXX_DIALECT_NOTE "compiler latest mode")
endif()

# Generate version header file
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
configure_file(
    ${CMAKE_SOURCE_DIR}/src/version/version.hpp.in
    ${CMAKE_BINARY_DIR}/version.hpp
    @ONLY
)
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/src/version/version.cppm"
    PROPERTIES OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/version.hpp"
)

set(CMAKE_CXX_STANDARD ${WINUXCMD_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd5050)    #we do not use rtti
    # Sources and narrow string literals use UTF-8 on every Windows code page.
    add_compile_options(/utf-8)
endif ()

# Directory for compiled module files
set(MODULES_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${MODULES_DIR}")

# Set source directory for header includes
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

if (WIN32)
    set(WINUXCMD_RESOURCE_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/winuxcmd.rc")
    # activeCodePage=UTF-8: pin the process ACP so narrow-char boundaries
    # (argv, narrow path ctors, path::string(), *A Win32 APIs) speak UTF-8
    # regardless of the system locale (niubash#88 class of bugs).
    set(WINUXCMD_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/winuxcmd.manifest")
endif ()

# ===================== Unity Build Implementation =====================
if(ENABLE_UNITY_BUILD)
    function(enable_unity_build TARGET_NAME)
        get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
        if(NOT TARGET_SOURCES)
            message(STATUS "Unity Build skipped for ${TARGET_NAME}: no sources found")
            return()
        endif()

        set(UNITY_SOURCE_FILES "")
        set(KEEP_SOURCE_FILES "")
        set(MODULE_FILES "")

        set(COMMANDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/commands")

        # The pattern below is built from a literal directory path, so escape
        # the regex metacharacters the path may contain ("C++Files", "Program
        # Files (x86)", "..."). Unescaped, "+" after a "+" can never match the
        # literal directory and every command source silently fails the
        # commands-dir test, skipping the unity build on those machines.
        set(COMMANDS_DIR_PATTERN "${COMMANDS_DIR}")
        string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" COMMANDS_DIR_PATTERN "${COMMANDS_DIR_PATTERN}")

        foreach(SOURCE_FILE ${TARGET_SOURCES})
            if(NOT IS_ABSOLUTE ${SOURCE_FILE})
                set(ABS_SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
            else()
                set(ABS_SOURCE_FILE ${SOURCE_FILE})
            endif()

            get_filename_component(SOURCE_EXT ${ABS_SOURCE_FILE} EXT)
            string(TOLOWER ${SOURCE_EXT} SOURCE_EXT_LOWER)

            if(SOURCE_EXT_LOWER MATCHES "\\.(cpp|cxx|cc)" AND ABS_SOURCE_FILE MATCHES "${COMMANDS_DIR_PATTERN}.*")
                if(EXISTS ${ABS_SOURCE_FILE})
                    list(APPEND UNITY_SOURCE_FILES ${ABS_SOURCE_FILE})
                endif()
            elseif(SOURCE_EXT_LOWER MATCHES "\\.(cppm)")
                list(APPEND MODULE_FILES ${SOURCE_FILE})
            else()
                list(APPEND KEEP_SOURCE_FILES ${SOURCE_FILE})
            endif()
        endforeach()

        list(LENGTH UNITY_SOURCE_FILES FILE_COUNT)
        if(FILE_COUNT LESS 2)
            message(STATUS "Unity Build skipped for ${TARGET_NAME}: too few command files (${FILE_COUNT})")
            return()
        endif()

        set(UNITY_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_commands_unity.cpp")
        file(WRITE ${UNITY_FILE} "// Auto-generated Unity Build file for ${TARGET_NAME} (only commands)\n")
        file(APPEND ${UNITY_FILE} "// Generated at: ${CMAKE_CURRENT_DATE}\n\n")

        foreach(SOURCE_FILE ${UNITY_SOURCE_FILES})
            string(REPLACE "\\" "\\\\" SOURCE_FILE_ESCAPED ${SOURCE_FILE})
            file(APPEND ${UNITY_FILE} "#include \"${SOURCE_FILE_ESCAPED}\"\n")
        endforeach()

        set_target_properties(${TARGET_NAME} PROPERTIES SOURCES "")

        if(KEEP_SOURCE_FILES)
            target_sources(${TARGET_NAME} PRIVATE ${KEEP_SOURCE_FILES})
        endif()

        if(MODULE_FILES)
            target_sources(${TARGET_NAME}
                    PUBLIC
                    FILE_SET CXX_MODULES
                    TYPE CXX_MODULES
                    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                    FILES ${MODULE_FILES}
            )
        endif()
        target_sources(${TARGET_NAME} PRIVATE ${UNITY_FILE})

        target_compile_definitions(${TARGET_NAME} PRIVATE UNITY_BUILD=1)

        message(STATUS "Unity Build enabled for ${TARGET_NAME}: merged ${FILE_COUNT} command files into ${UNITY_FILE}")
    endfunction()
else()
    function(enable_unity_build TARGET_NAME)
        message(STATUS "Unity Build disabled for ${TARGET_NAME}")
    endfunction()
endif()


# Main executable: winuxcmd.exe - includes all commands
add_executable(winuxcmd
        src/Main/main.cpp
        ${WINUXCMD_MANIFEST}
)

if (WIN32)
    target_sources(winuxcmd PRIVATE "${WINUXCMD_RESOURCE_RC}")
endif ()

# Copy winuxcmd.exe to usr/bin so tests can find it
if (WIN32)
    add_custom_command(TARGET winuxcmd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:winuxcmd> ${CMAKE_BINARY_DIR}/usr/bin/winuxcmd.exe
        COMMENT "Copying winuxcmd.exe to usr/bin..."
    )
endif ()

# Package shim: the forwarder behind `usr\bin\<command>.exe` for
# `layout: shim` packages. It runs `opt\<pkg>\<command>.exe` from the map wpm
# writes, instead of paying a whole WinuxCmd startup per invocation
# (unixwin/WinuxCmd#1207). Deliberately depends on nothing but the C runtime.
add_executable(winuxcmd-shim src/shims/winuxcmd_shim.cpp)
if (WIN32)
    target_compile_options(winuxcmd-shim PRIVATE /W4 /EHsc /MT /Os)
    target_link_options(winuxcmd-shim PRIVATE /SUBSYSTEM:CONSOLE)
    add_custom_command(TARGET winuxcmd-shim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:winuxcmd-shim> ${CMAKE_BINARY_DIR}/usr/bin/winuxcmd-shim.exe
        COMMENT "Copying winuxcmd-shim.exe to usr/bin..."
    )
endif ()

# PCH for winuxcmd
target_precompile_headers(winuxcmd PRIVATE ${CMAKE_SOURCE_DIR}/src/pch/pch.h)

# ====================================================
# Module Interface Library (shared by all targets)
# ====================================================
add_library(winuxcmd-modules INTERFACE)

# Configure common module compilation settings
if (MSVC)
    target_compile_options(winuxcmd-modules INTERFACE
            /std:c++latest
            /experimental:module
            /interface
    )
    target_include_directories(winuxcmd-modules INTERFACE "${MODULES_DIR}")
endif()

# ====================================================
# Commands Static Library (shared by exe and FFI)
# ====================================================
message(STATUS "=== Creating Commands Static Library ===")

add_library(winuxcmd-commands STATIC)

# Add PCH to commands library
target_precompile_headers(winuxcmd-commands PRIVATE ${CMAKE_SOURCE_DIR}/src/pch/pch.h)

# Add all command source files
file(GLOB COMMAND_SOURCES CONFIGURE_DEPENDS
        "src/commands/*.cpp"
)

set(WINUXCMD_BANNED_COMMANDS "link")
set(WINUXCMD_BANNED_COMMANDS_REASONS
        "Its hardlink-style name shadows the MSVC toolchain linker link.exe "
        "on PATH and breaks builds that invoke it. Removed once in 10b2364 "
        "and re-added by a bot PR (#1065); it must never come back.")

# Reserved command blacklist: these names must never exist as WinuxCmd
# commands. If a banned source appears (e.g. a generated PR re-adds it),
# configure fails immediately with the command name and the rejection
# reason instead of shipping a binary that shadows foreign tools on PATH.
foreach(_banned _reason IN ZIP_LISTS
        WINUXCMD_BANNED_COMMANDS
        WINUXCMD_BANNED_COMMANDS_REASONS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/${_banned}.cpp")
        message(FATAL_ERROR
                "Banned command '${_banned}': src/commands/${_banned}.cpp must "
                "not exist.\nReason: ${_reason}\n"
                "Remove the file (and its tests/docs/i18n entries) before "
                "configuring. See the 'Banned commands' section in AGENTS.md.")
    endif()
endforeach()

if(NOT WINUXCMD_ENABLE_RUST2)
    # Without the rust2 library the replaced commands have no implementation:
    # drop their registrations rather than keep a second C++ copy (#1488:
    # replaced commands are never dual-tracked). The bridge module goes too.
    foreach(_rust2_cmd IN LISTS WINUXCMD_RUST2_COMMANDS)
        list(FILTER COMMAND_SOURCES EXCLUDE REGEX "/src/commands/${_rust2_cmd}\\.cpp$")
    endforeach()
    message(STATUS "rust2 command library: OFF (not built: ${WINUXCMD_RUST2_COMMANDS})")
endif()

target_sources(winuxcmd-commands PRIVATE ${COMMAND_SOURCES})

# Link module interface library (for settings only)
target_link_libraries(winuxcmd-commands PRIVATE winuxcmd-modules)

# Add modules directly (required for static library)
file(GLOB_RECURSE CXX_MODULE_FILES CONFIGURE_DEPENDS
        "src/**/*.cppm"
)
if(NOT WINUXCMD_ENABLE_RUST2)
    list(FILTER CXX_MODULE_FILES EXCLUDE REGEX "/src/bridge/rust2_bridge\\.cppm$")
endif()

target_sources(winuxcmd-commands
        PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        ${CXX_MODULE_FILES}
)

enable_unity_build(winuxcmd-commands)

# Set C++ standard
set_target_properties(winuxcmd-commands PROPERTIES
        CXX_STANDARD ${WINUXCMD_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Compile options
target_compile_options(winuxcmd-commands PRIVATE
        $<$<CONFIG:Release>:/O2 /Os>
        /bigobj
        /GR-
)

target_compile_definitions(winuxcmd-commands PRIVATE
        _UNICODE
        UNICODE
        _CRT_DISABLE_PERFCRIT_LOCKS
        _HAS_RTTI=0
)

if(WINUXCMD_ENABLE_PCRE2)
    target_compile_definitions(winuxcmd-commands PRIVATE WINUXCMD_ENABLE_PCRE2)
    target_link_libraries(winuxcmd-commands PRIVATE ${WINUXCMD_PCRE2_TARGET})
endif()

if(WINUXCMD_PACKAGE_EDITION STREQUAL "full")
    # Entry-link edition switch read by src/core/package_edition.h (see the
    # single command-name list there); only the wpm link-management paths
    # consult it, so the winuxcmd-commands target is the sole definition
    # point (unixwin/WinuxCmd#1438).
    target_compile_definitions(winuxcmd-commands PRIVATE
            WINUXCMD_PACKAGE_EDITION_FULL=1)
endif()

# Link options
target_link_options(winuxcmd-commands PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF ${WINUXCMD_PGO_FLAGS}>
)

if (USE_STATIC_CRT AND MSVC)
    target_compile_options(winuxcmd-commands PRIVATE /MT /Os /Oy /GS-)
endif ()

if (MSVC)
    target_compile_options(winuxcmd-commands PRIVATE
            /std:c++latest
    )
endif ()

# Add include directories
target_include_directories(winuxcmd-commands PRIVATE ${SRC_DIR} ${CMAKE_BINARY_DIR})

message(STATUS "Commands library created with ${COMMAND_SOURCES_COUNT} files")

# ====================================================
# rust2 command library (staticlib, #1488)
# ====================================================
# Link shape: staticlib, not cdylib. The FFI post-mortem (#1487/niubash#727)
# traced the breakage to the DLL boundary - load-time CRT stdio binding in a
# separately linked module. A staticlib is folded into winuxcmd.exe by the
# same link.exe run: one image, one CRT, no load-time binding. The Rust side
# is built with +crt-static so its objects ask for libcmt (/MT), matching
# CMAKE_MSVC_RUNTIME_LIBRARY above; with USE_STATIC_CRT=OFF it keeps rustc's
# default msvcrt (/MD).
if(WINUXCMD_ENABLE_RUST2)
    set(WINUXCMD_RUST2_DIR "${CMAKE_SOURCE_DIR}/rust2")
    # Separate CARGO_TARGET_DIR per build tree: never touches rust2/target
    # (the gate/driver tree) and never shares artifacts across CMake trees.
    set(WINUXCMD_RUST2_TARGET_DIR "${CMAKE_BINARY_DIR}/rust2-target" CACHE PATH
        "CARGO_TARGET_DIR used for the rust2 staticlib build")
    # The staticlib must be built for the same machine the C++ link targets.
    # cargo without --target builds for the *host* triple: on an x64 host the
    # arm64 build (vcvars x64_arm64) silently got an x86_64 winuxcmd_ffi.lib
    # whose objects cannot resolve the bridge's ABI symbols for an ARM64
    # link (LNK2001 winuxcmd_abi_version / winuxcmd_run / winuxcmd_free_buf,
    # #1552). Resolve the target arch from the same signals the package arch
    # resolution below uses (explicit WINUXCMD_PACKAGE_ARCH, then the vcvars
    # target environment, then the processor), map it to a Rust target
    # triple, and always pass --target so the artifact lands in the
    # per-triple directory on both architectures. Prerequisite: the std
    # library for the triple must be installed
    # (`rustup target add aarch64-pc-windows-msvc` for arm64 builds).
    set(WINUXCMD_RUST2_TARGET_TRIPLE "" CACHE STRING
        "Rust target triple for the rust2 staticlib (empty = derive from the C++ target arch)")
    if(NOT WINUXCMD_RUST2_TARGET_TRIPLE)
        if(DEFINED WINUXCMD_PACKAGE_ARCH AND NOT WINUXCMD_PACKAGE_ARCH STREQUAL "")
            string(TOLOWER "${WINUXCMD_PACKAGE_ARCH}" _rust2_arch)
        elseif(DEFINED ENV{VSCMD_ARG_TGT_ARCH} AND NOT "$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "")
            string(TOLOWER "$ENV{VSCMD_ARG_TGT_ARCH}" _rust2_arch)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(_rust2_arch "arm64")
        else()
            set(_rust2_arch "x64")
        endif()
        if(_rust2_arch STREQUAL "arm64")
            set(WINUXCMD_RUST2_TARGET_TRIPLE "aarch64-pc-windows-msvc")
        else()
            set(WINUXCMD_RUST2_TARGET_TRIPLE "x86_64-pc-windows-msvc")
        endif()
    endif()
    set(WINUXCMD_RUST2_LIB "${WINUXCMD_RUST2_TARGET_DIR}/${WINUXCMD_RUST2_TARGET_TRIPLE}/release/winuxcmd_ffi.lib")
    set(WINUXCMD_RUST2_RUSTC_FLAGS)
    if(USE_STATIC_CRT)
        list(APPEND WINUXCMD_RUST2_RUSTC_FLAGS -C target-feature=+crt-static)
    endif()
    file(GLOB_RECURSE WINUXCMD_RUST2_SOURCES CONFIGURE_DEPENDS
            "${WINUXCMD_RUST2_DIR}/crates/*.rs"
            "${WINUXCMD_RUST2_DIR}/crates/*/Cargo.toml")
    # `cargo rustc --crate-type staticlib` builds only the staticlib artifact
    # of winuxcmd-ffi (the crate also declares cdylib for host experiments).
    # --locked: the workspace has no external crates; never resolve online.
    add_custom_command(
            OUTPUT "${WINUXCMD_RUST2_LIB}"
            COMMAND ${CMAKE_COMMAND} -E env
                    "CARGO_TARGET_DIR=${WINUXCMD_RUST2_TARGET_DIR}"
                    "${WINUXCMD_CARGO_EXECUTABLE}" rustc --release --locked
                    --manifest-path "${WINUXCMD_RUST2_DIR}/Cargo.toml"
                    -p winuxcmd-ffi --lib --crate-type staticlib
                    --target "${WINUXCMD_RUST2_TARGET_TRIPLE}"
                    -- ${WINUXCMD_RUST2_RUSTC_FLAGS}
            DEPENDS ${WINUXCMD_RUST2_SOURCES}
                    "${WINUXCMD_RUST2_DIR}/Cargo.toml"
                    "${WINUXCMD_RUST2_DIR}/Cargo.lock"
            WORKING_DIRECTORY "${WINUXCMD_RUST2_DIR}"
            COMMENT "cargo: building rust2 command library (winuxcmd_ffi.lib)"
            VERBATIM)
    add_custom_target(winuxcmd-rust2 DEPENDS "${WINUXCMD_RUST2_LIB}")

    add_library(winuxcmd-rust2-lib STATIC IMPORTED GLOBAL)
    # System libraries rustc reports for this staticlib
    # (`cargo rustc ... -- --print native-static-libs`; the CRT entry is the
    # /defaultlib directive embedded in the archive). Re-run that command and
    # update this list if a rust2 change makes the link fail with LNK2019.
    set_target_properties(winuxcmd-rust2-lib PROPERTIES
            IMPORTED_LOCATION "${WINUXCMD_RUST2_LIB}"
            INTERFACE_LINK_LIBRARIES "advapi32;kernel32;ntdll;userenv;ws2_32;dbghelp")
    add_dependencies(winuxcmd-commands winuxcmd-rust2)
    target_link_libraries(winuxcmd-commands PRIVATE winuxcmd-rust2-lib)
    message(STATUS "rust2 command library: ON (${WINUXCMD_RUST2_COMMANDS}) via ${WINUXCMD_CARGO_EXECUTABLE}")
endif()

# Link commands library to main executable
target_link_libraries(winuxcmd PRIVATE winuxcmd-commands winuxcmd-modules)
if(WINUXCMD_ENABLE_PCRE2)
    target_link_libraries(winuxcmd PRIVATE ${WINUXCMD_PCRE2_TARGET})
endif()

# Keep all command registration translation units from static library.
# Without WHOLEARCHIVE, MSVC may discard unreferenced objects and leave
# the command registry empty at runtime.
if(MSVC)
    target_link_options(winuxcmd PRIVATE "/WHOLEARCHIVE:winuxcmd-commands.lib")
endif()

# ====================================================
# Main executable modules and configuration
# ====================================================

# Link commands library which contains all modules and commands
target_link_libraries(winuxcmd PRIVATE winuxcmd-commands)

# Default optimization flags
target_compile_options(winuxcmd PRIVATE
        $<$<CONFIG:Release>:/O2 /Os>
        /GR-  # Keep RTTI disabled for size
)

target_compile_definitions(winuxcmd PRIVATE
        _UNICODE
        UNICODE
        _CRT_DISABLE_PERFCRIT_LOCKS
        _HAS_RTTI=0
)

target_link_options(winuxcmd PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF ${WINUXCMD_PGO_FLAGS}>
)

if (USE_STATIC_CRT AND MSVC)
    target_compile_options(winuxcmd PRIVATE /MT /Os /Oy /GS-)
endif ()

if(GENERATE_MAP_INFO)
    target_link_options(winuxcmd PRIVATE
            /MAP:${CMAKE_CURRENT_BINARY_DIR}/winuxcmd.map
            /MAPINFO:EXPORTS
    )
endif ()

enable_unity_build(winuxcmd)

# Add include directories for main executable
target_include_directories(winuxcmd PRIVATE ${SRC_DIR} ${CMAKE_BINARY_DIR})

# MSVC-specific configuration - common settings
if (MSVC)
    # Fix Windows SDK architecture errors

    # Enable linker optimizations
    set(CMAKE_LINKER_FLAGS_RELEASE "${CMAKE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} /LTCG")
endif ()

# Configuration for main executable
if (MSVC)
    target_compile_options(winuxcmd PRIVATE
            /std:c++latest        # Enable latest C++23 features including modules
            /GR-                  # Disable RTTI
    )

    target_compile_options(winuxcmd PRIVATE
            /experimental:module
            /interface
            "/translateInclude"
    )

    # Optimize for size in Release mode only
    target_compile_options(winuxcmd PRIVATE $<$<CONFIG:Release>:/O2 /Os>)

    # Module directory for compiled .ifc files
    target_include_directories(winuxcmd PRIVATE "${MODULES_DIR}")
endif ()

# Individual command executables - each only includes its own module

# Helper macro to create command executables
macro(create_command_executable CMD)
    add_executable(${CMD} src/Main/main.cpp ${WINUXCMD_MANIFEST})

    if (WIN32)
        target_sources(${CMD} PRIVATE "${WINUXCMD_RESOURCE_RC}")
    endif ()

    # Add only the necessary modules
    target_sources(${CMD}
            PUBLIC
            FILE_SET CXX_MODULES
            TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            src/core/dispatcher.cppm
            src/core/cmd_meta.cppm
            src/core/opt.cppm
            src/utils/utils.cppm
            src/utils/console.cppm
            src/utils/text_io.cppm
            src/utils/string.cppm
            src/utils/wildcard.cppm
            src/utils/path.cppm
            src/utils/native_path.cppm
            src/utils/utf8.cppm
            src/utils/win32.cppm
            src/utils/childproc.cppm
            src/utils/json.cppm
            src/utils/i18n.cppm
            src/utils/file_io.cppm
            src/utils/cppbar.cppm
            src/utils/encoding.cppm
            src/utils/regex.cppm
            src/utils/digest.cppm
            src/utils/pager.cppm
            src/container/container.cppm
            src/container/small_vector.cppm
    )

    target_sources(${CMD} PRIVATE src/commands/${CMD}.cpp)

    # MSVC-specific configuration for individual commands
    if (MSVC)
        target_compile_options(${CMD} PRIVATE
                /std:c++latest    # Enable latest C++23 features including modules
                /GR-              # Disable RTTI
                /experimental:module
                /interface
        )

        # Default optimization for size in Release mode
        target_compile_options(${CMD} PRIVATE
                $<$<CONFIG:Release>:/O2 /Os>
                /GR-  # Keep RTTI disabled for size
        )

        # Module directory for compiled .ifc files
        target_include_directories(${CMD} PRIVATE "${MODULES_DIR}")

        # Set source directory for header includes
        target_include_directories(${CMD} PRIVATE "${SRC_DIR}")

        # Add link options for release builds
        target_link_options(${CMD} PRIVATE $<$<CONFIG:Release>:
                /LTCG         # Link Time Code Generation
                /OPT:REF      # Eliminate unreferenced data
                /OPT:ICF      # Enable identical COMDAT folding
                >)

    endif ()
endmacro()


# Automatically collect commands from src/commands directory
file(GLOB COMMAND_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/*.cpp")
set(COMMANDS)
foreach (CMD_FILE ${COMMAND_FILES})
    # Extract command name from filename (without extension)
    get_filename_component(CMD_NAME "${CMD_FILE}" NAME_WE)
    list(APPEND COMMANDS ${CMD_NAME})
endforeach ()

# Note: Hardlinks are no longer created during build.
# Users can run `winuxcmd wpm links rebuild` in the bin directory to generate
# command links. scripts/create_links.ps1 remains as a compatibility wrapper.

add_subdirectory(examples)

if (ENABLE_TESTS)
    include(CTest)
    add_subdirectory(tests)

    # Ensure create-test-links depends on winuxcmd executable
    # This prevents the links script from running before winuxcmd.exe is built
    if(TARGET create-test-links)
        add_dependencies(create-test-links winuxcmd)
    endif()
endif ()

include(GNUInstallDirs)

# Install executables into the real Unix-style command directory.
set(CMAKE_INSTALL_BINDIR "usr/bin" CACHE PATH "Output directory for executables" FORCE)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/install" CACHE PATH "Installation prefix" FORCE)
endif ()

install(TARGETS winuxcmd
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

# The package shim travels with the payload: `wpm` links a `layout: shim`
# command to it when it is present, and without it in the archive the cheaper
# dispatch never reaches an installation (unixwin/WinuxCmd#1207).
install(TARGETS winuxcmd-shim
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux.ps1"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux.cmd"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux-activate.ps1"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winuxcmd_init.cmd"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winuxcmd_uninstall.cmd"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_links.ps1"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
)

install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/README-ZH.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "."
        COMPONENT Documentation
)

set(CPACK_PACKAGE_NAME "WinuxCmd")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Windows Compatible Linux Command Set")
set(CPACK_PACKAGE_VENDOR "WinuxCmd Project")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WinuxCmd")

set(CPACK_COMPONENTS_ALL Runtime Documentation)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime")
set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")
set(CPACK_COMPONENT_RUNTIME_GROUP "Core")
set(CPACK_COMPONENT_DOCUMENTATION_GROUP "Documentation")
set(CPACK_COMPONENT_GROUP_CORE_DISPLAY_NAME "Core Components")
set(CPACK_COMPONENT_GROUP_DOCUMENTATION_DISPLAY_NAME "Documentation")

if (WIN32)
    set(CPACK_GENERATOR "ZIP;TGZ;7Z")
    set(CPACK_ARCHIVE_SEVEN_ZIP_OPTIONS "-mx=9")
    set(CPACK_ARCHIVE_TAR_GZIP_OPTIONS "-9")
    if (DEFINED WINUXCMD_PACKAGE_ARCH AND NOT WINUXCMD_PACKAGE_ARCH STREQUAL "")
        string(TOLOWER "${WINUXCMD_PACKAGE_ARCH}" WINUXCMD_RESOLVED_PACKAGE_ARCH)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(WINUXCMD_RESOLVED_PACKAGE_ARCH "arm64")
    else ()
        set(WINUXCMD_RESOLVED_PACKAGE_ARCH "x64")
    endif ()

    if (WINUXCMD_RESOLVED_PACKAGE_ARCH STREQUAL "arm64")
        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win-arm64${WINUXCMD_PACKAGE_EDITION_SUFFIX}")
    else ()
        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win-x64${WINUXCMD_PACKAGE_EDITION_SUFFIX}")
    endif ()
endif ()

set(CPACK_IGNORE_FILES
        "${CMAKE_SOURCE_DIR}/CMakeLists.txt$"
        "${CMAKE_SOURCE_DIR}/.clang.*$"
        "${CMAKE_SOURCE_DIR}/.git.*$"
        "${CMAKE_SOURCE_DIR}/.vs/"
        "${CMAKE_SOURCE_DIR}/.idea/"
        "${CMAKE_SOURCE_DIR}/src/"
        "${CMAKE_SOURCE_DIR}/test/"
        "${CMAKE_SOURCE_DIR}/cmake-build-.*/"
        "${CMAKE_SOURCE_DIR}/out/"
        "${CMAKE_SOURCE_DIR}/TemplateLicense.lict$"
        "${CMAKE_SOURCE_DIR}/tests"
)

# ========== FFI Library for External Integration ==========
if(BUILD_FFI)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ffi/CMakeLists.txt")
        message(STATUS "FFI Library: ENABLED (winuxcore.dll)")
        add_subdirectory(src/ffi)
    else()
        message(WARNING
                "BUILD_FFI=ON but src/ffi is not present in this checkout "
                "(the FFI sources are not distributed with the repository). "
                "Continuing with the FFI library disabled; pass -DBUILD_FFI=OFF "
                "to silence this warning.")
        set(BUILD_FFI OFF CACHE BOOL "Build the winuxcore FFI library" FORCE)
    endif()
else()
    message(STATUS "FFI Library: DISABLED")
endif()

# Print final configuration summary
message(STATUS "=================== BUILD CONFIGURATION SUMMARY ===================")
message(STATUS "Build Mode: ${BUILD_MODE}")
message(STATUS "C++ Dialect: ${WINUXCMD_CXX_DIALECT} (${WINUXCMD_CXX_DIALECT_NOTE}, CXX_STANDARD=${WINUXCMD_CXX_STANDARD})")
message(STATUS "Tests Enabled: ${ENABLE_TESTS}")
message(STATUS "Map Info Generation: ${GENERATE_MAP_INFO}")
message(STATUS "Unity Build: ${ENABLE_UNITY_BUILD}")
message(STATUS "Static CRT: ${USE_STATIC_CRT}")
message(STATUS "PCRE2 grep -P: ${WINUXCMD_ENABLE_PCRE2} (JIT=${WINUXCMD_PCRE2_JIT})")
message(STATUS "Package edition: ${WINUXCMD_PACKAGE_EDITION} (entry links for shell-builtin shadow names: ${WINUXCMD_PACKAGE_EDITION_SUFFIX})")
message(STATUS "Note: Run `winuxcmd wpm links rebuild` in bin directory to generate command links")
message(STATUS "=================================================================")

# Print out supported the amount of supported commands
statistics_number_of_commands_supported()
message(STATUS "Final count result: ${COMMANDS_SRC_FILE_COUNT} .cpp files")

include(CPack)
