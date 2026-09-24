# WinuxCmd Project Development Guidelines

## 1. Core Positioning & Objectives

### 1.1 Project Core

Provide Windows-native, lightweight, AI-agnostic compatible Linux command set for Windows (PowerShell/CMD), focus on "Windows-only" initially with reserved cross-platform expansion space.

### 1.2 Quantitative Objectives

- Support hundreds high-frequency Linux commands (core parameters 1:1 compatible with Linux behavior)
- Combined main program winuxcmd.exe size ≤ 1MB
- Individual command executable (e.g., ls.exe) size ≤ 2KB per file
- Implemented with pure Windows API, no third-party library dependencies, developed in C++23 with zero extra performance overhead
- Compiler: Microsoft Visual Studio 2022+ (MSVC 19.34+, full support for C++23 stable features)

## 2. Development Language & Coding Standards

### 2.1 Language Standards

- Primary development language: C++23 (only use stable features supported by MSVC, disable experimental features)
- Build tool: CMake (primary build tool, adapted to MSVC compilation chain)
- Compiler: MSVC 19.34+ (built-in with VS 2022+, requires installation of "C++ 23 Standard Library Support" component)

### 2.1.1 C++23 Module System

- Use C++23 modules for all core functionality
- Separate interface (.cppm) and implementation (.cpp) files
- Use `export module` for interface files and `module` for implementation files
- Follow CMake's CXX_MODULES support for build configuration

### 2.2 Basic Coding Rules

#### 2.2.1 Naming Conventions

- Files: snake_case (e.g., path_resolver.cpp, ls_command.cppm)
- Functions: camelCase (e.g., parseCommandLine, getFileAttributes)
- Variables: camelCase (e.g., filePath, optionVerbose)
- Constants: kCamelCase (e.g., kMaxPathLength, kBufferSize)
- Namespaces: snake_case (e.g., filesystem_utils, console_output)

#### 2.2.2 Code Structure

##### Comments

- Functions: Doxygen-style comments (English only), example:

```cpp
/**
 * @brief List directory contents
 * @param args Command-line arguments
 * @return Exit code (0 on success, non-zero on error)
 */
int ls(std::span<std::string_view> args);
```

- Key logic: Single-line comments // to explain intent, avoid redundant comments.

##### Function Design Guidelines

- Use `std::span<std::string_view>` for command arguments
- Keep functions small and focused on a single task
- Return integer exit codes (0 = success, non-zero = error)
- Use output parameters sparingly, prefer return values
- Avoid global variables, use function parameters for state

##### Example Function-based Architecture

```cpp
// Command grouping by functionality
namespace filesystem_commands {
    int ls(std::span<std::string_view> args);
    int cp(std::span<std::string_view> args);
    int mv(std::span<std::string_view> args);
    int rm(std::span<std::string_view> args);
}

namespace text_commands {
    int cat(std::span<std::string_view> args);
    int grep(std::span<std::string_view> args);
    int sed(std::span<std::string_view> args);
}
```

##### Module Structure Example

```cpp
// Interface file: ls_command.cppm
export module ls_command;

import std;

export {
    int execute_ls(std::span<std::string_view> args) noexcept;
}

// Implementation file: ls_command.cpp
module ls_command;

int execute_ls(std::span<std::string_view> args) noexcept {
    // Implementation...
    return 0;
}
```

#### 2.2.3 Size & Performance Constraints (MSVC Exclusive)

##### Disable redundant C++ features

- RTTI off: /GR- (MSVC compilation parameter)
- Exceptions off: /EHs- /EHc- (disable C++ exception handling)

##### Prioritize Windows native APIs

- Directory traversal: FindFirstFileW/FindNextFileW instead of std::filesystem
- Console output: WriteConsoleA instead of std::cout
- Memory operations: LocalAlloc/LocalFree (optional) instead of new/delete to reduce CRT dependencies
- Memory management: Prioritize stack memory (e.g., char buf[256]), reduce heap memory allocation, avoid unnecessary memory copies
- Code reuse: Extract common logic (path resolution, parameter parsing) into static library lxcore.lib, prohibit duplicate code

##### MSVC extreme size optimization

- Compilation optimization: /O1 /Os (prioritize size over speed)
- Link optimization: /GL /Gy /LTCG /OPT:REF /OPT:ICF (Whole Program Optimization, eliminate redundant symbols, function-level linking)
- Debug info removal: /DEBUG:NONE, disable PDB files

## 3. Feature Development Standards

### 3.1 Command Implementation Rules

- Each command implements only Linux core parameters (e.g., ls supports -l/-a/-h/-r, disable rare parameters like --color=auto)
- Command output format: 1:1 aligned with Linux native output (e.g., ls -l output format for permissions, size, time is consistent with Ubuntu 22.04)
- Path compatibility: Automatically convert Linux paths (/c/Users) to Windows paths (C:\Users) without manual user adaptation
- Error handling: Error message format consistent with Linux (e.g., ls: cannot access 'test': No such file or directory), English output only
- CRT dependency: Use "CRT-free mode" as much as possible (/MT statically link CRT to avoid carrying vcruntime140.dll)

### 3.2 Dual-Mode Compilation Rules

- Individual command mode: Each command compiled as separate .exe (e.g., ls.exe), only link core library lxcore.lib and necessary Windows APIs
- Combined mode: Main program winuxcmd.exe built-in all commands, distributed via first parameter (e.g., winuxcmd ls -l)
- Common core logic shared between two modes, prohibit duplicate code for different modes
- Output format: All executables compiled as 64-bit (x64), 32-bit (x86) disabled to reduce compatibility redundancy

## 4. Build & Release Standards

### 4.1 CMake Build Rules (MSVC Adaptation)

Compilation must be done via CMake; CMakeLists.txt must force specify MSVC compilation chain, core configuration example:

```cmake
cmake_minimum_required(VERSION 3.30)

project(WinuxCmd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MODULES_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${MODULES_DIR}")

# Core module library
add_library(lxcore STATIC)
target_sources(lxcore
    PUBLIC
    FILE_SET CXX_MODULES
    TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
    src/core/path_resolver.cppm
    src/core/cmd_parser.cppm
    src/core/console_output.cppm
)
target_sources(lxcore PRIVATE
    src/core/path_resolver.cpp
    src/core/cmd_parser.cpp
    src/core/console_output.cpp
)

if(MSVC)
    target_compile_options(lxcore PRIVATE
        /O1 /Os              # Optimize for size
        /GR-                # Disable RTTI
        /EHs- /EHc-         # Disable exceptions
        /GL /Gy             # Whole Program Optimization, function-level linking
    )
    target_link_options(lxcore PRIVATE
        /LTCG /OPT:REF /OPT:ICF  # Link-time optimization
        /DEBUG:NONE              # Remove debug info
    )
endif()

# Individual command compilation (example)
set(COMMANDS ls grep sed)
foreach(CMD ${COMMANDS})
    add_executable(${CMD} src/cmd/${CMD}.cpp)
    target_link_libraries(${CMD} PRIVATE lxcore)
    
    if(MSVC)
        target_compile_options(${CMD} PRIVATE
            /O1 /Os
            /GR-
            /EHs- /EHc-
            /GL /Gy
        )
        target_link_options(${CMD} PRIVATE
            /LTCG /OPT:REF /OPT:ICF
            /DEBUG:NONE
        )
    endif()
    
    set_target_properties(${CMD} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/cmd"
    )
endforeach()

# Combined main program
add_executable(winuxcmd src/main.cpp)
target_link_libraries(winuxcmd PRIVATE lxcore)

if(MSVC)
    target_compile_options(winuxcmd PRIVATE
        /O1 /Os
        /GR-
        /EHs- /EHc-
        /GL /Gy
    )
    target_link_options(winuxcmd PRIVATE
        /LTCG /OPT:REF /OPT:ICF
        /DEBUG:NONE
    )
endif()

set_target_properties(winuxcmd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)
```

- Compilation output directory:
  - Individual commands: bin/cmd/
  - Combined main program: bin/

### 4.2 manager.py Build Tool Usage

The project includes a custom build tool `manager.py` (similar to Cargo) that simplifies the build and execution process. This tool is recommended for daily development.

#### 4.2.1 Features

- **Build Management**: Supports both Debug and Release configurations
- **Target Listing**: Automatically detects and lists all executable targets
- **Target Execution**: Directly run built executables with arguments
- **Environment Setup**: Automatically initializes MSVC environment variables
- **Encoding Support**: Handles various output encodings (UTF-8, GBK, Latin-1)

#### 4.2.2 Basic Usage

```bash
# Build project in Release mode
python scripts/manager.py build --release

# Build project in Debug mode
python scripts/manager.py build --debug

# List all executable targets
python scripts/manager.py list

# List targets in specific configuration
python scripts/manager.py list --release
python scripts/manager.py list --debug

# Run an executable (priority: positional argument > --target)
python scripts/manager.py run <target_name> -- <args>
python scripts/manager.py run --target <target_name> -- <args>
```

#### 4.2.3 Configuration

The tool's configuration is defined at the top of `scripts/manager.py`:

```python
CMAKE_PATH = r"C:\Program Files\CMake\bin\cmake.EXE"
PROJECT_ROOT = r"D:/codespace/WinuxCmd"

BUILD_DIR_DEBUG = os.path.join(PROJECT_ROOT, "cmake-build-debug")
BUILD_DIR_RELEASE = os.path.join(PROJECT_ROOT, "cmake-build-release")

VS_INSTALL_PATH = r"C:\Program Files\Microsoft Visual Studio\18\Community"
ARCH = "x64"
DEFAULT_BUILD_TYPE = "debug"
```

#### 4.2.4 Best Practices

- Always use `manager.py` for daily development tasks instead of raw CMake commands
- Use Release mode for performance testing and distribution
- Use Debug mode for development and debugging
- Run `list` command after build to verify successful compilation

#### 4.2.5 Alternative CMake Commands

If `manager.py` is not available, you can use the following CMake commands:

```bash
# Create build directory
mkdir build
cd build

# Configure CMake to use MSVC
cmake .. -G "Visual Studio 17 2022" -A x64

# Build all targets
cmake --build . --config Release -v
```

### 4.3 Release Standards

- Release package contains only: 64-bit executables + English help documentation (help.txt), no redundant files
- Version number: Semantic versioning (e.g., v1.0.0), `<major>.<minor>.<patch>`
- Release notes: English only, including new commands, fixed compatibility issues, size/performance data (e.g., winuxcmd.exe v1.0.0: 890KB)

## 5. Extension & Compatibility Rules

### 5.1 Cross-Platform Reservation

- Core logic layered design: Interface layer (pure abstract) + Windows implementation layer; subsequent Linux/macOS expansion only requires adding implementation layers
- Prohibit writing Windows-exclusive hardcoding (e.g., path separator \) in core logic, encapsulate via interfaces (e.g., get_path_separator())

### 5.2 Compatibility Constraints

- Support Windows 10/11 (64-bit), no 32-bit system compatibility required
- Command behavior must be compatible with Linux commands from common distributions (Ubuntu 22.04, CentOS 9), prohibit custom extended parameters
- Executables must pass "Windows Defender Compatibility Verification" to avoid false virus reporting

## 6. Repository & Collaboration Standards

### 6.1 Repository Structure

```
winuxcmd/
├── include/          # Public header files
├── src/
│   ├── core/         # Core static library code (path resolution, parameter parsing)
│   ├── cmd/          # Individual command implementations (ls.cpp, grep.cpp, etc.)
│   └── main.cpp      # Combined main program entry
├── xmake.lua         # XMake build script (MSVC adapted)
├── Project_Rules.md  # Project development guidelines
└── README.md         # Project documentation (English only)
```

### 6.2 Commit Standards

Commit format: `<type>: <message>` (English only), type includes:

- `feat`: Add new command/feature
- `fix`: Fix compatibility/size/performance issues
- `docs`: Update documentation
- `build`: Modify build scripts/compiler configurations

Example: `feat: add ls command with -l/-a/-h params (MSVC optimized)`