# Read version from VERSION file
# This module reads the VERSION file from the project root and sets version variables

function(read_version_file)
    if(EXISTS "${CMAKE_SOURCE_DIR}/PROJECT_VERSION")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/PROJECT_VERSION")
        file(READ "${CMAKE_SOURCE_DIR}/PROJECT_VERSION" VERSION_STRING)
        string(STRIP "${VERSION_STRING}" VERSION_STRING)
        
        # Parse version string (major.minor.patch)
        string(REPLACE "." ";" VERSION_LIST "${VERSION_STRING}")
        list(GET VERSION_LIST 0 VERSION_MAJOR)
        list(GET VERSION_LIST 1 VERSION_MINOR)
        list(GET VERSION_LIST 2 VERSION_PATCH)
        
        message(STATUS "Read version from PROJECT_VERSION file: ${VERSION_STRING}")
        
        # Set PARENT_SCOPE variables
        set(VERSION_STRING "${VERSION_STRING}" PARENT_SCOPE)
        set(VERSION_MAJOR "${VERSION_MAJOR}" PARENT_SCOPE)
        set(VERSION_MINOR "${VERSION_MINOR}" PARENT_SCOPE)
        set(VERSION_PATCH "${VERSION_PATCH}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "PROJECT_VERSION file not found in ${CMAKE_SOURCE_DIR}")
    endif()
endfunction()
