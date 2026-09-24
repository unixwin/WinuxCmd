# enable_import_std.cmake
# ================= C++23 import std auto config =================

if(CMAKE_VERSION VERSION_LESS "3.30")
    message(STATUS "CMake < 3.30 — import std not supported")
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/import_std.cmake)

set(_ver "${CMAKE_MAJOR_VERSION}_${CMAKE_MINOR_VERSION}")

if(DEFINED _IMPORT_STD_UUID_${_ver})
    set(_import_std_uuid "${_IMPORT_STD_UUID_${_ver}}")
else()
    message(WARNING "CMake ${CMAKE_VERSION} has no known import std UUID")
    return()
endif()

message(STATUS "Enabling C++23 import std (UUID=${_import_std_uuid}) for CMake ${CMAKE_VERSION}")

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "${_import_std_uuid}")
set(CMAKE_CXX_MODULE_STD ON)

message(STATUS "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD = ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
message(STATUS "CMAKE_CXX_MODULE_STD = ${CMAKE_CXX_MODULE_STD}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_GENERATOR = ${CMAKE_GENERATOR}")
# ================================================================