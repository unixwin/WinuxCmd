// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// framework/test_hooks.h
#pragma once

#include <vector>

namespace wctest {
/**
 * @brief Function signature for test hooks
 *
 * Hooks are functions that execute before or after each test.
 * They receive the test group and test name for context.
 */
using Hook = void (*)(const char* group, const char* test_name);

/**
 * @brief Get reference to before-test hooks vector
 *
 * @return std::vector<Hook>& Reference to before hooks collection
 */
std::vector<Hook>& before_hooks();

/**
 * @brief Get reference to after-test hooks vector
 *
 * @return std::vector<Hook>& Reference to after hooks collection
 */
std::vector<Hook>& after_hooks();

/**
 * @brief Add a hook to execute before each test
 *
 * @param h Hook function to add
 */
void add_before_each(Hook h);

/**
 * @brief Add a hook to execute after each test
 *
 * @param h Hook function to add
 */
void add_after_each(Hook h);

/**
 * @brief Install default test framework hooks
 *
 * Sets up built-in hooks for timing and temporary directory management.
 * Should be called once during framework initialization.
 */
void install_default_hooks();

/**
 * @brief Built-in hook implementations
 */
namespace hooks {
/**
 * @brief Hook to start timing before test execution
 */
void timer_before(const char* group, const char* test_name);

/**
 * @brief Hook to stop timing and report duration after test
 */
void timer_after(const char* group, const char* test_name);

/**
 * @brief Hook to create temporary directory before test
 */
void temp_dir_before(const char* group, const char* test_name);

/**
 * @brief Hook to clean up temporary directory after test
 */
void temp_dir_after(const char* group, const char* test_name);
}  // namespace hooks
}  // namespace wctest
