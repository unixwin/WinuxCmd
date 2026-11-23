# enable_import_std.cmake
# ================= C++23 import std auto config =================

if(CMAKE_VERSION VERSION_LESS "3.30")
    message(STATUS "CMake < 3.30 — import std not supported")
    return()
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.5")
    # CMake 4.5 graduated `import std` out of the experimental model:
    # there is no UUID gate to satisfy, CMAKE_CXX_MODULE_STD no longer
    # has any effect, and CMAKE_CXX_COMPILER_IMPORT_STD is no longer
    # defined. `import std;` is handled automatically on every source
    # with module scanning enabled (CXX_SCAN_FOR_MODULES, on by default
    # for C++20 and later), using the stdlib module metadata CMake
    # discovers from the compiler. Nothing to configure here -- and
    # nothing that can half-configure: a toolchain without stdlib
    # modules fails inside CMake at generate time, not at link time
    # with LNK1181 (the #1855 failure shape this file used to allow).
    message(STATUS "CMake ${CMAKE_VERSION} — import std is native in CMake >= 4.5, no experimental gate")
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/import_std.cmake)

set(_ver "${CMAKE_MAJOR_VERSION}_${CMAKE_MINOR_VERSION}")

if(DEFINED _IMPORT_STD_UUID_${_ver})
    set(_import_std_uuid "${_IMPORT_STD_UUID_${_ver}}")
else()
    message(FATAL_ERROR "CMake ${CMAKE_VERSION} has no known import std UUID -- refusing to configure with import std half-configured (the build would only fail at link time with LNK1181 on std.compat.ixx.obj); pin a CMake with a known import std UUID or adapt cmake/import_std.cmake")
endif()

message(STATUS "Enabling C++23 import std (UUID=${_import_std_uuid}) for CMake ${CMAKE_VERSION}")

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "${_import_std_uuid}")
set(CMAKE_CXX_MODULE_STD ON)

message(STATUS "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD = ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
message(STATUS "CMAKE_CXX_MODULE_STD = ${CMAKE_CXX_MODULE_STD}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_GENERATOR = ${CMAKE_GENERATOR}")
# ================================================================