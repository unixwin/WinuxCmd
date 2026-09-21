// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#ifndef WINUX_TEST_H
#define WINUX_TEST_H

/**
 * @brief Main include file for WinuxCmd test framework
 *
 * Convenience header that includes all essential test framework components.
 * Provides a single point of inclusion for test files.
 *
 * Components included:
 * - framework_pch.h: Precompiled header with common includes
 * - paths.h: Path utility functions for test environment
 * - pipeline.h: Command pipeline execution utilities
 * - process_win32.h: Low-level process execution functions
 * - temp_dir.h: Temporary directory management
 * - wctest.h: Core testing framework functionality
 */

#include "framework/framework_pch.h"  // Precompiled header
#include "framework/paths.h"          // Path utilities
#include "framework/pipeline.h"       // Pipeline execution
#include "framework/process_win32.h"  // Process management
#include "framework/temp_dir.h"       // Temporary directories
#include "framework/wctest.h"         // Core test framework

#endif  //! WINUX_TEST_H
