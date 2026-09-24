// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#ifndef FRAMEWORK_PCH_H
#define FRAMEWORK_PCH_H

/**
 * @brief Precompiled header for the WinuxCmd test framework
 *
 * This header includes commonly used system headers and standard library
 * headers to improve compilation performance across the test framework.
 * It also defines Windows-specific macros to reduce header bloat.
 */

// Windows macro definitions to reduce header size and avoid conflicts
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#define NOMINMAX             // Prevent Windows.h from defining min/max macros
#define VC_EXTRALEAN         // Exclude rarely-used MFC stuff

// Windows system headers
#include <Windows.h>

// Standard library headers
#include <chrono>      // For Time utilities
#include <filesystem>  // For File system operations
#include <fstream>     // For File stream operations
#include <iostream>    // For Standard I/O streams
#include <map>         // For Map container
#include <memory>      // For Smart pointers
#include <optional>    // For Optional values
#include <sstream>     // For String streams
#include <stdexcept>   // For Standard exceptions
#include <string>      // For String utilities
#include <thread>      /// For Threading
#include <vector>      // For Dynamic arrays

#endif  // FRAMEWORK_PCH_H
