# Count the number of .cpp files in the src/commands directory of the project
# Parameter: None
# Return: Stores the count result in COMMANDS_SRC_FILE_COUNT for external use
function(statistics_number_of_commands_supported)
    # Define the CORRECT source directory (src/commands) matching your project structure
    set(COMMAND_SRC_DIR "${CMAKE_SOURCE_DIR}/src/commands")
    
    # Debug: Print the resolved path to verify correctness (critical for troubleshooting)
    message(STATUS "=== Debug: Resolved command source directory ===")
    message(STATUS "Project root: ${CMAKE_PROJECT_SOURCE_DIR}")
    message(STATUS "Command src dir: ${COMMAND_SRC_DIR}")

    # Check if the directory exists
    if(NOT EXISTS ${COMMAND_SRC_DIR})
        message(WARNING "Directory ${COMMAND_SRC_DIR} does not exist, count result is 0")
        set(COMMANDS_SRC_FILE_COUNT 0 PARENT_SCOPE)
        return()
    endif()

    # Find all .cpp files in src/commands (add RECURSIVE for subdirectories if needed)
    file(GLOB CPP_FILES 
         ${COMMAND_SRC_DIR}/*.cpp
         # Uncomment the line below if you have subdirectories (e.g., src/commands/cat, src/commands/ls)
         # ${COMMAND_SRC_DIR}/**/*.cpp
    )

    # Get the number of .cpp files
    list(LENGTH CPP_FILES FILE_COUNT)

    # Pass the result to the parent scope (usable outside the function)
    set(COMMANDS_SRC_FILE_COUNT ${FILE_COUNT} PARENT_SCOPE)

    # Print the final count for verification
    message(STATUS "Number of .cpp files in src/commands: ${FILE_COUNT}")
endfunction()
