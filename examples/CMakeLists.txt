cmake_minimum_required(VERSION 3.30.0)

# Add subdirectories for different example types
add_subdirectory(container)

# FFI host harness prototype (only with the FFI library enabled)
if (BUILD_FFI)
    add_subdirectory(ffi-harness)
endif ()
