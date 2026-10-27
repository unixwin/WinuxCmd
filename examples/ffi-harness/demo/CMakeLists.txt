# Test doubles for the FFI harness (see ../README.md). Built only when
# BUILD_FFI_HARNESS_DEMO is ON; never installed.
add_library(winuxcore_demo SHARED
    demo_dll.cpp
    demo_applets.cpp
    winuxcore_demo.def
)
set_target_properties(winuxcore_demo PROPERTIES
    OUTPUT_NAME "winuxcore"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(winuxcore_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(winuxcmd_demo
    demo_main.cpp
    demo_applets.cpp
)
set_target_properties(winuxcmd_demo PROPERTIES
    OUTPUT_NAME "winuxcmd-demo"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(winuxcmd_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
