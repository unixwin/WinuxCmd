cmake_minimum_required(VERSION 3.30)
project(winuxcmd-tests)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

file(GLOB WINUX_TEST_FRAMEWORK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cpp"
)

add_library(test_main STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/framework/test_main.cpp
)

target_precompile_headers(test_main PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/framework/framework_pch.h
)

list(FILTER WINUX_TEST_FRAMEWORK_SOURCES EXCLUDE REGEX ".*test_main\\.cpp$")

add_library(winux-test STATIC
        ${WINUX_TEST_FRAMEWORK_SOURCES}
)

target_precompile_headers(winux-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/framework/framework_pch.h
)

target_include_directories(winux-test
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(test_main
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Use absolute path for WINUXCMD_BIN_DIR to ensure tests work correctly
# regardless of the current working directory
file(REAL_PATH "${CMAKE_BINARY_DIR}" WINUXCMD_BIN_DIR_ABSOLUTE)
set(WINUXCMD_BIN_DIR "${WINUXCMD_BIN_DIR_ABSOLUTE}")
target_compile_definitions(winux-test
        PUBLIC
        WINUXCMD_BIN_DIR=L"${WINUXCMD_BIN_DIR}"
)

target_compile_definitions(test_main
        PUBLIC
        WINUXCMD_BIN_DIR=L"${WINUXCMD_BIN_DIR}"
)

file(GLOB_RECURSE ALL_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/unit/**/*.cpp"
)

list(FILTER ALL_TEST_SOURCES EXCLUDE REGEX ".*/framework/.*")

message(STATUS "=== WinuxCmd Test Configuration ===")
message(STATUS "Found test sources: ${ALL_TEST_SOURCES}")

if(NOT ALL_TEST_SOURCES)
    message(WARNING "No test sources found in tests/unit/ directory!")
else()
    add_executable(winuxcmd-tests
            ${ALL_TEST_SOURCES}
    )

    target_link_libraries(winuxcmd-tests PRIVATE winux-test test_main)
    
    target_include_directories(winuxcmd-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_precompile_headers(winuxcmd-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/framework/framework_pch.h)

    include(GoogleTest)
    
    # Only discover individual tests - TestMate will create hierarchy from these
    gtest_discover_tests(winuxcmd-tests
        EXTRA_ARGS ""
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TEST_PREFIX ""
        PROPERTIES LABELS "WinuxCmd"
    )
    
    # Custom targets for convenience (not CTest entries)
    add_custom_target(test-all
        COMMAND $<TARGET_FILE:winuxcmd-tests>
        DEPENDS winuxcmd-tests
        COMMENT "Running all WinuxCmd unit tests..."
    )
    
    add_custom_target(list-tests
        COMMAND $<TARGET_FILE:winuxcmd-tests> --list-tests
        DEPENDS winuxcmd-tests
        COMMENT "Listing all test cases..."
    )
    
    # Auto-detect groups for custom targets only
    set(TEST_GROUPS "")
    foreach(src ${ALL_TEST_SOURCES})
        get_filename_component(test_dir ${src} DIRECTORY)
        file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR}/unit ${test_dir})
        get_filename_component(group_name ${rel_path} NAME)
        
        if(NOT "${group_name}" STREQUAL "" AND NOT "${group_name}" IN_LIST TEST_GROUPS)
            list(APPEND TEST_GROUPS ${group_name})
            add_custom_target(test-${group_name}
                COMMAND $<TARGET_FILE:winuxcmd-tests> --gtest_filter=${group_name}.*
                DEPENDS winuxcmd-tests
                COMMENT "Running ${group_name} tests..."
            )
        endif()
    endforeach()
endif()

option(EXCLUDE_TESTS_FROM_DEFAULT_BUILD "Exclude test targets from default build" OFF)
if(EXCLUDE_TESTS_FROM_DEFAULT_BUILD)
    set_property(TARGET winuxcmd-tests PROPERTY EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    add_custom_target(build-tests
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target winuxcmd-tests
            COMMENT "Building test executable..."
    )
endif()

# Create command links for testing
find_program(POWERSHELL_EXE powershell REQUIRED)

# Use CMAKE_RUNTIME_OUTPUT_DIRECTORY directly instead of trying to get it from target
# This is more reliable as it's set before targets are processed
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# Get the path to winuxcmd.exe (it should be in the same directory)
set(WINUXCMD_EXE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/winuxcmd.exe")

# Custom target to create command links using PowerShell script
# Add DEPENDS on winuxcmd.exe to ensure it exists before running
add_custom_target(create-test-links
    COMMAND ${POWERSHELL_EXE} -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/create_links.ps1" -Force
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    DEPENDS ${WINUXCMD_EXE_PATH}
    COMMENT "Creating WinuxCmd command links in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}..."
    VERBATIM
)

# Make sure links are created before running tests
add_dependencies(winuxcmd-tests create-test-links)

add_test(
    NAME winux_activate_profile_cleanup
    COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux_activate_profile_cleanup_test.ps1"
)
set_tests_properties(winux_activate_profile_cleanup PROPERTIES LABELS "WinuxCmd;PowerShell")

add_test(
    NAME winux_command_discovery
    COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux_command_discovery_test.ps1"
)
set_tests_properties(winux_command_discovery PROPERTIES LABELS "WinuxCmd;PowerShell")

add_test(
    NAME winux_winget_discovery
    COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux_winget_discovery_test.ps1"
)
set_tests_properties(winux_winget_discovery PROPERTIES LABELS "WinuxCmd;PowerShell")

add_test(
    NAME winux_installer_root_discovery
    COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/winux_installer_root_discovery_test.ps1"
)
set_tests_properties(winux_installer_root_discovery PROPERTIES LABELS "WinuxCmd;PowerShell")

# Also add dependency to test-all target
if(TARGET test-all)
    add_dependencies(test-all create-test-links)
endif()

# Add dependency to individual test group targets
foreach(src ${ALL_TEST_SOURCES})
    get_filename_component(test_dir ${src} DIRECTORY)
    file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR}/unit ${test_dir})
    get_filename_component(group_name ${rel_path} NAME)
    
    if(NOT "${group_name}" STREQUAL "" AND NOT "${group_name}" IN_LIST TEST_GROUPS)
        if(TARGET test-${group_name})
            add_dependencies(test-${group_name} create-test-links)
        endif()
    endif()
endforeach()
