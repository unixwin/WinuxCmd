cmake_minimum_required(VERSION 3.30.0)

# ====================================================
# Container Module Library
# ====================================================
add_library(container_module)

target_sources(container_module
    PUBLIC
    FILE_SET CXX_MODULES
    TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_SOURCE_DIR}"
    FILES
    "${CMAKE_SOURCE_DIR}/src/container/constexpr_map.cppm"
    "${CMAKE_SOURCE_DIR}/src/container/container.cppm"
    "${CMAKE_SOURCE_DIR}/src/container/small_vector.cppm"
)

target_include_directories(container_module PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

set_target_properties(container_module PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# ====================================================
# Collect all example source files
# ====================================================
file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
)

# Create an executable for each example
foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    # Get filename without path and extension
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    
    # Create executable
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    
    # Link container module
    target_link_libraries(${EXAMPLE_NAME} PRIVATE
        container_module
    )
    
    # Include directories
    target_include_directories(${EXAMPLE_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )
    
    # MSVC specific options
    if(MSVC)
        target_compile_options(${EXAMPLE_NAME} PRIVATE
            /std:c++latest
            /experimental:module
        )
        target_include_directories(${EXAMPLE_NAME} PRIVATE
            "${CMAKE_BINARY_DIR}/modules"
        )
    endif()
    
    # Only build in DEV mode or RELEASE_EXAMPLES mode
    if(BUILD_MODE STREQUAL "DEV" OR BUILD_MODE STREQUAL "RELEASE_EXAMPLES")
        message(STATUS "Added example: ${EXAMPLE_NAME}")
    else()
        set_target_properties(${EXAMPLE_NAME} PROPERTIES
            EXCLUDE_FROM_ALL TRUE
        )
    endif()
endforeach()

# ====================================================
# Create a custom target that depends on all examples
# ====================================================
if(EXAMPLE_SOURCES)
    add_custom_target(examples
        DEPENDS ${EXAMPLE_NAMES}
        COMMENT "Building all examples"
    )
endif()