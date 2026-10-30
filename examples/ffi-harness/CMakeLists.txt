# FFI host harness (prototype) — see README.md in this directory.
# Standalone: depends only on the ABI shape in src/ffi/winux_ffi.h, which
# harness.cpp mirrors locally so the harness builds even in a checkout
# where the DLL target is disabled.
option(BUILD_FFI_HARNESS_DEMO "Build the FFI harness test doubles (demo DLL + spawn twin)" OFF)

add_executable(ffi-harness harness.cpp)
set_target_properties(ffi-harness PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
if (WIN32)
    target_link_libraries(ffi-harness PRIVATE bcrypt)
endif ()
# CRT form (DESIGN D7): the root lists map USE_STATIC_CRT=ON to the static
# MSVC runtime for every in-tree target, this one included — the harness
# deliberately matches the DLL's form so no second CRT DLL is needed next
# to either binary.

if (BUILD_FFI_HARNESS_DEMO)
    add_subdirectory(demo)
endif ()
