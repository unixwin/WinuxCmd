/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: wctest.h
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>

#include "temp_dir.h"
#include "tests_hooks.h"
#include "tests_utils.h"

#undef min
#undef max
#endif

template <typename T>
concept Streamable = requires(std::ostream &os, const T &t) {
  { os << t } -> std::same_as<std::ostream &>;
};

namespace wctest {
// ============================
// Test Registry System
// ============================

/**
 * @brief Structure representing a registered test case
 *
 * Contains metadata and function pointer for a single test case.
 */
struct TestCase {
  const char *group;  ///< Test group/category name
  const char *name;   ///< Individual test name

  void (*fn)();  ///< Pointer to test function
};

/**
 * @brief Get reference to global test registry
 *
 * Uses function-local static to ensure thread-safe initialization.
 * Stores all registered test cases for execution.
 *
 * @return std::vector<TestCase>& Reference to test registry
 */
inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> r;
  return r;
}

/**
 * @brief Split full test name into group and individual test name
 *
 * Parses test names in format "group.test_name" and separates
 * them into components. Uses thread-local storage for buffer safety.
 *
 * @param full Full test name (e.g., "cat.basic_test")
 * @param group Output: test group name
 * @param name Output: individual test name (nullptr if no dot)
 */
inline void split_test_name(const char *full, const char *&group,
                            const char *&name) {
  const char *dot = std::strchr(full, '.');
  if (!dot) {
    group = full;
    name = nullptr;
    return;
  }

  static thread_local char group_buf[128];
  size_t len = dot - full;
  if (len >= sizeof(group_buf)) len = sizeof(group_buf) - 1;

  std::memcpy(group_buf, full, len);
  group_buf[len] = 0;

  group = group_buf;
  name = dot + 1;
}

struct Registrar {
  Registrar(const char *group, const char *test_name, void (*fn)()) {
    registry().push_back({group, test_name, fn});
  }
};

// ============================
// Failed record
// ============================

inline int failures = 0;
inline int current_test_failed = 0;

inline void fail(const char *file, int line, const std::string &msg) {
  if (!current_test_failed) {
    set_color(Color::Red);
    std::cerr << "  FAILED\n";
    reset_color();
    current_test_failed = 1;
  }

  std::cerr << "    " << file << ":" << line << "\n";
  std::cerr << "    " << msg << "\n";
  ++failures;
}

/**
 * @brief Convert string to human-readable format
 *
 * Escapes control characters and non-printable bytes to make
 * string content visible in test output.
 *
 * @param s Input string
 * @return std::string Human-readable representation
 */
inline std::string to_visible(const std::string &s) {
  std::string out;
  for (unsigned char c : s) {
    switch (c) {
      case '\r':
        out += "\\r";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 32 || c > 126) {
          char buf[5];
          sprintf(buf, "\\x%02X", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

/**
 * @brief Convert string to hexadecimal representation
 *
 * Formats each byte as two-digit hexadecimal for detailed
 * binary content inspection.
 *
 * @param s Input string
 * @return std::string Hexadecimal representation with space separation
 */
inline std::string to_hex(const std::string &s) {
  std::ostringstream oss;
  for (unsigned char c : s) {
    oss << std::hex;
    oss.width(2);
    oss.fill('0');
    oss << (int)c << " ";
  }
  return oss.str();
}

/**
 * @brief Specialized EXPECT_EQ implementation for strings
 *
 * Provides detailed failure reporting for string comparisons
 * including both human-readable and hexadecimal views.
 *
 * @param a Left-hand side string
 * @param b Right-hand side string
 * @param file Source file (from macro)
 * @param line Line number (from macro)
 * @param exprA Expression text for left side
 * @param exprB Expression text for right side
 */
inline void expect_eq_impl(const std::string &a, const std::string &b,
                           const char *file, int line, const char *exprA,
                           const char *exprB) {
  if (a != b) {
    std::ostringstream oss;
    oss << "EXPECT_EQ(" << exprA << ", " << exprB << ") failed\n"
        << "      lhs (visible): [" << to_visible(a) << "]\n"
        << "      lhs (hex): " << to_hex(a) << "\n"
        << "      rhs (visible): [" << to_visible(b) << "]\n"
        << "      rhs (hex): " << to_hex(b);
    fail(file, line, oss.str());
  }
}

// ============================
// Assertion Macros
// ============================

/**
 * @brief Assert that expression evaluates to true
 *
 * Basic boolean assertion that reports failure with expression text.
 */
#define EXPECT_TRUE(x)                                                \
  do {                                                                \
    if (!(x)) {                                                       \
      wctest::fail(__FILE__, __LINE__, "EXPECT_TRUE(" #x ") failed"); \
    }                                                                 \
  } while (0)

/**
 * @brief Assert that expression evaluates to false
 *
 * Boolean assertion that checks expression is false.
 */
#define EXPECT_FALSE(x)                                                \
  do {                                                                 \
    if (x) {                                                           \
      wctest::fail(__FILE__, __LINE__, "EXPECT_FALSE(" #x ") failed"); \
    }                                                                  \
  } while (0)

/**
 * @brief Generic EXPECT_EQ implementation for streamable types
 *
 * Template function that works with any types supporting operator==
 * and ostream output. Provides standard failure reporting format.
 *
 * @tparam A Left-hand side type (must be Streamable)
 * @tparam B Right-hand side type (must be Streamable)
 * @param a Left-hand side value
 * @param b Right-hand side value
 * @param file Source file (from macro)
 * @param line Line number (from macro)
 * @param exprA Expression text for left side
 * @param exprB Expression text for right side
 */
template <Streamable A, Streamable B>
inline void expect_eq_impl(const A &a, const B &b, const char *file, int line,
                           const char *exprA, const char *exprB) {
  if (!(a == b)) {
    std::ostringstream oss;
    oss << "EXPECT_EQ(" << exprA << ", " << exprB << ") failed\n"
        << "      lhs: [" << a << "]\n"
        << "      rhs: [" << b << "]";
    fail(file, line, oss.str());
  }
}

/**
 * @brief Normalize line endings in text
 *
 * Converts Windows-style CRLF line endings to Unix-style LF
 * by removing standalone carriage returns and preserving
 * proper CRLF sequences as single newlines.
 *
 * @param s Input string with mixed line endings
 * @return std::string Normalized string with consistent line endings
 */
inline std::string normalize_newlines(std::string s) {
  std::string out;
  out.reserve(s.size());

  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r') {
      // Skip standalone \r when part of \r\n sequence
      if (i + 1 < s.size() && s[i + 1] == '\n') continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

/**
 * @brief Assert equality of two values
 *
 * Generic equality assertion that automatically selects the appropriate
 * implementation based on argument types.
 */
#define EXPECT_EQ(a, b) \
  wctest::expect_eq_impl((a), (b), __FILE__, __LINE__, #a, #b)

/**
 * @brief Assert that two values are not equal
 *
 * Generic inequality assertion that reports failure if values are equal.
 */
#define EXPECT_NE(a, b)                                                     \
  do {                                                                      \
    if ((a) == (b)) {                                                       \
      wctest::fail(__FILE__, __LINE__, "EXPECT_NE(" #a ", " #b ") failed"); \
    }                                                                       \
  } while (0)

/**
 * @brief Assert that first value is less than second
 *
 * Generic less-than assertion that reports failure if lhs >= rhs.
 */
#define EXPECT_LT(a, b)                            \
  do {                                             \
    if (!((a) < (b))) {                            \
      std::ostringstream oss;                      \
      oss << "EXPECT_LT(" #a ", " #b ") failed\n"  \
          << "      lhs: [" << (a) << "]\n"        \
          << "      rhs: [" << (b) << "]";         \
      wctest::fail(__FILE__, __LINE__, oss.str()); \
    }                                              \
  } while (0)

/**
 * @brief Assert that first value is greater than second
 *
 * Generic greater-than assertion that reports failure if lhs <= rhs.
 */
#define EXPECT_GT(a, b)                            \
  do {                                             \
    if (!((a) > (b))) {                            \
      std::ostringstream oss;                      \
      oss << "EXPECT_GT(" #a ", " #b ") failed\n"  \
          << "      lhs: [" << (a) << "]\n"        \
          << "      rhs: [" << (b) << "]";         \
      wctest::fail(__FILE__, __LINE__, oss.str()); \
    }                                              \
  } while (0)

/**
 * @brief Assert equality of text content with newline normalization
 *
 * Compares text strings after normalizing line endings to handle
 * cross-platform text file differences.
 */
#define EXPECT_EQ_TEXT(a, b)                                                \
  wctest::expect_eq_impl(wctest::normalize_newlines(a),                     \
                         wctest::normalize_newlines(b), __FILE__, __LINE__, \
                         #a, #b)

/**
 * @brief Assert equality of binary data
 *
 * Treats arguments as byte sequences and compares them directly,
 * useful for comparing raw data or when string interpretation
 * would be inappropriate.
 */
#define EXPECT_BYTES(a, b)                                                   \
  wctest::expect_eq_impl(std::string(a), std::string(b), __FILE__, __LINE__, \
                         #a, #b)

/**
 * @brief Assert process exit code
 *
 * Convenience macro for checking command execution results.
 */
#define EXPECT_EXIT_CODE(r, code) EXPECT_EQ((r).exit_code, code)

/**
 * @brief Print output with visible string formatting
 *
 * DEPRECATED: Use TEST_LOG instead. Prints labeled output
 * with control characters made visible.
 */
#define PRINT_OUTPUT(label, text) \
  std::cout << label << ": [" << wctest::to_visible(text) << "]" << std::endl

/**
 * @brief Log test information with visible string formatting
 *
 * Outputs labeled information during test execution with
 * control characters escaped for readability.
 */
#define TEST_LOG(label, text)                                      \
  do {                                                             \
    std::cout << label << ": [" << wctest::to_visible(text) << "]" \
              << std::endl;                                        \
  } while (0)

/**
 * @brief Log raw text without formatting
 *
 * Outputs text directly without any visible character conversion.
 */
#define TEST_LOG_RAW(label, text)                    \
  do {                                               \
    std::cout << label << ": " << text << std::endl; \
  } while (0)

/**
 * @brief Log data in hexadecimal format
 *
 * Outputs data as space-separated hexadecimal bytes for
 * detailed binary content inspection.
 */
#define TEST_LOG_HEX(label, text)                                        \
  do {                                                                   \
    std::cout << label << " hex: " << wctest::to_hex(text) << std::endl; \
  } while (0)

/**
 * @brief Log data with both visible and hexadecimal formats
 *
 * Provides comprehensive output showing both human-readable
 * and binary representations of the same data.
 */
#define TEST_LOG_FULL(label, text)                                         \
  do {                                                                     \
    std::cout << label << " visible: [" << wctest::to_visible(text) << "]" \
              << std::endl;                                                \
    std::cout << label << " hex: " << wctest::to_hex(text) << std::endl;   \
  } while (0)

#ifdef DEBUG
#define TEST_LOG_DEBUG(label, text) TEST_LOG(label, text)
#define TEST_LOG_HEX_DEBUG(label, text) TEST_LOG_HEX(label, text)
#else
#define TEST_LOG_DEBUG(label, text) ((void)0)
#define TEST_LOG_HEX_DEBUG(label, text) ((void)0)
#endif

#define TEST_LOG_ERROR(label, text)                                      \
  do {                                                                   \
    wctest::set_color(wctest::Color::Red);                               \
    std::cout << "ERROR: " << label << ": [" << wctest::to_visible(text) \
              << "]" << std::endl;                                       \
    wctest::reset_color();                                               \
  } while (0)

#define TEST_LOG_SUCCESS(label, text)                                      \
  do {                                                                     \
    wctest::set_color(wctest::Color::Green);                               \
    std::cout << "SUCCESS: " << label << ": [" << wctest::to_visible(text) \
              << "]" << std::endl;                                         \
    wctest::reset_color();                                                 \
  } while (0)

/**
 * @brief Log command execution information
 *
 * Displays the command being executed along with its arguments
 * for debugging and traceability purposes.
 */
#define TEST_LOG_CMD(cmd, args)        \
  do {                                 \
    std::cout << "Executing: " << cmd; \
    if (!args.empty()) {               \
      std::cout << " with args: ";     \
      for (const auto &arg : args) {   \
        std::wcout << arg << L" ";     \
      }                                \
    }                                  \
    std::cout << std::endl;            \
  } while (0)

#define TEST_LOG_CMD_LIST(cmd, ...)                           \
  do {                                                        \
    std::cout << "Executing: " << cmd;                        \
    std::initializer_list<std::wstring> args = {__VA_ARGS__}; \
    if (args.size() > 0) {                                    \
      std::cout << " with args: ";                            \
      for (const auto &arg : args) {                          \
        std::wcout << arg << L" ";                            \
      }                                                       \
    }                                                         \
    std::cout << std::endl;                                   \
  } while (0)

#define TEST_LOG_PIPELINE_STEP(index, cmd)                       \
  do {                                                           \
    std::cout << "  Step " << index << ": " << cmd << std::endl; \
  } while (0)

#define TEST_LOG_EXIT_CODE(result)                                  \
  do {                                                              \
    if (result.exit_code == 0) {                                    \
      wctest::set_color(wctest::Color::Green);                      \
      std::cout << "Exit code: 0 (SUCCESS)" << std::endl;           \
    } else {                                                        \
      wctest::set_color(wctest::Color::Red);                        \
      std::cout << "Exit code: " << result.exit_code << " (FAILED)" \
                << std::endl;                                       \
    }                                                               \
    wctest::reset_color();                                          \
  } while (0)

#define TEST_LOG_FILE_CONTENT(filename, content)                    \
  do {                                                              \
    std::cout << "File: " << filename << std::endl;                 \
    std::cout << "Content: [" << wctest::to_visible(content) << "]" \
              << std::endl;                                         \
  } while (0)

#define TEST_LOG_EXPECT_EQ(expected, actual)                            \
  do {                                                                  \
    std::cout << "  Expected: [" << wctest::to_visible(expected) << "]" \
              << std::endl;                                             \
    std::cout << "  Actual:   [" << wctest::to_visible(actual) << "]"   \
              << std::endl;                                             \
  } while (0)

/**
 * @brief Measure and log execution time for a scope
 *
 * Creates a timing scope that automatically measures elapsed time
 * and reports it when the scope exits. Useful for performance testing.
 */
#define TEST_TIME_SCOPE(label)                                         \
  auto test_scope_start_##__LINE__ = std::chrono::steady_clock::now(); \
  auto test_scope_end_##__LINE__ = [&]() {                             \
    auto end = std::chrono::steady_clock::now();                       \
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(   \
                  end - test_scope_start_##__LINE__)                   \
                  .count();                                            \
    std::cout << label << " took: " << ms << " ms" << std::endl;       \
  }

// ============================
// Test Definition Macro
// ============================

/**
 * @brief Define a test case
 *
 * Creates a test function along with registration and execution wrapper.
 * Automatically registers the test with the framework and sets up
 * before/after hooks execution.
 *
 * Usage: TEST(group_name, test_name) { // test code  }
 *
 * @param group Test group/category name
 * @param test_name Individual test name
 */
#define TEST(group, test_name)                                                 \
  static void test_fn_##group##_##test_name();                                 \
  /**                                                                          \
   * @brief Test runner function with hook integration                         \
   *                                                                           \
   * Executes before hooks, runs the test function, executes after hooks,      \
   * and reports pass/fail status with colored output.                         \
   */                                                                          \
  static void test_runner_##group##_##test_name() {                            \
    for (auto h : wctest::before_hooks()) h(#group, #test_name);               \
    wctest::current_test_failed = 0;                                           \
    test_fn_##group##_##test_name();                                           \
    for (auto h : wctest::after_hooks()) h(#group, #test_name);                \
    if (!wctest::current_test_failed) {                                        \
      wctest::set_color(wctest::Color::Green);                                 \
      std::cout << "  PASSED\n";                                               \
      wctest::reset_color();                                                   \
    }                                                                          \
  }                                                                            \
  /**                                                                          \
   * @brief Automatic test registration                                        \
   *                                                                           \
   * Registers the test with the global registry during static initialization. \
   */                                                                          \
  static wctest::Registrar reg_##group##_##test_name(                          \
      #group, #test_name, (void (*)()) & test_runner_##group##_##test_name);   \
  static void test_fn_##group##_##test_name()

// ============================
// Test Execution Functions
// ============================

/**
 * @brief Run all registered tests
 *
 * Executes every test in the registry and provides summary statistics.
 * Returns appropriate exit code for build system integration.
 *
 * @return int 0 if all tests pass, 1 if any tests fail
 */
inline int run_all() {
  int total = registry().size();
  int failed_before = failures;

  set_color(Color::Cyan);
  std::cout << "[==========] Running " << total << " tests\n";
  reset_color();

  for (auto &t : registry()) {
    set_color(Color::Cyan);
    std::cout << "[ RUN      ] " << t.group << "." << t.name << "\n";
    reset_color();
    t.fn();
  }

  int failed = failures - failed_before;

  std::cout << "\n";
  set_color(failed ? Color::Red : Color::Green);
  std::cout << "[==========] " << total << " tests, " << (total - failed)
            << " passed, " << failed << " failed\n";
  reset_color();

  return failed == 0 ? 0 : 1;
}

inline int run_single(const char *group, const char *name) {
  int failed_before = failures;

  set_color(Color::Cyan);
  std::cout << "[==========] Running single test: " << group << "." << name
            << "\n";
  reset_color();

  bool found = false;
  for (auto &t : registry()) {
    if (strcmp(t.group, group) == 0 && strcmp(t.name, name) == 0) {
      found = true;
      set_color(Color::Cyan);
      std::cout << "[ RUN      ] " << t.group << "." << t.name << "\n";
      reset_color();
      t.fn();
      break;
    }
  }

  if (!found) {
    set_color(Color::Red);
    std::cerr << "[ ERROR    ] Test case not found: " << group << "." << name
              << "\n";
    reset_color();
    return 1;
  }

  int failed = failures - failed_before;

  std::cout << "\n";
  set_color(failed ? Color::Red : Color::Green);
  std::cout << "[==========] 1 test, " << (1 - failed) << " passed, " << failed
            << " failed\n";
  reset_color();

  return failed == 0 ? 0 : 1;
}

inline int run_group(const char *group) {
  int total = 0;
  int failed_before = failures;

  set_color(Color::Cyan);
  std::cout << "[==========] Running all tests in group: " << group << "\n";
  reset_color();

  for (auto &t : registry()) {
    if (strcmp(t.group, group) == 0) {
      total++;
      set_color(Color::Cyan);
      std::cout << "[ RUN      ] " << t.group << "." << t.name << "\n";
      reset_color();
      t.fn();
    }
  }

  if (total == 0) {
    set_color(Color::Red);
    std::cerr << "[ ERROR    ] Test group not found: " << group << "\n";
    reset_color();
    return 1;
  }

  int failed = failures - failed_before;

  std::cout << "\n";
  set_color(failed ? Color::Red : Color::Green);
  std::cout << "[==========] " << total << " tests in group " << group << ", "
            << (total - failed) << " passed, " << failed << " failed\n";
  reset_color();

  return failed == 0 ? 0 : 1;
}

inline void list_tests() {
  std::string current_group;
  for (auto &t : registry()) {
    if (current_group != t.group) {
      current_group = t.group;
      std::cout << current_group << ".\n";
    }
    std::cout << "  " << t.name << "\n";
  }
}

inline bool pattern_matches(const char *pattern, const char *str) {
  if (pattern == nullptr || str == nullptr) {
    return false;
  }

  const char *pattern_ptr = pattern;
  const char *str_ptr = str;
  const char *pattern_backup = nullptr;
  const char *str_backup = nullptr;

  while (*str_ptr != '\0') {
    if (*pattern_ptr == '*') {
      while (*pattern_ptr == '*') {
        pattern_ptr++;
      }
      if (*pattern_ptr == '\0') {
        return true;
      }
      pattern_backup = pattern_ptr;
      str_backup = str_ptr;
    } else if (*pattern_ptr == '?') {
      if (*str_ptr == '\0') {
        return false;
      }
      pattern_ptr++;
      str_ptr++;
    } else if (*pattern_ptr == *str_ptr) {
      pattern_ptr++;
      str_ptr++;
    } else if (pattern_backup != nullptr) {
      pattern_ptr = pattern_backup;
      str_ptr = ++str_backup;
    } else {
      return false;
    }
  }
  while (*pattern_ptr == '*') {
    pattern_ptr++;
  }

  return (*pattern_ptr == '\0');
}

inline bool test_matches_filter(const TestCase &test, const char *filter) {
  if (filter == nullptr || filter[0] == '\0') {
    return true;
  }

  std::string full_name = std::string(test.group) + "." + test.name;

  return pattern_matches(filter, full_name.c_str());
}

inline int run_with_filter(const char *filter) {
  int total = 0;
  int failed_before = failures;

  set_color(Color::Cyan);
  std::cout << "[==========] Running tests with filter: " << filter << "\n";
  reset_color();

  for (auto &t : registry()) {
    if (test_matches_filter(t, filter)) {
      total++;
      set_color(Color::Cyan);
      std::cout << "[ RUN      ] " << t.group << "." << t.name << "\n";
      reset_color();
      t.fn();
    }
  }

  if (total == 0) {
    set_color(Color::Yellow);
    std::cout << "[  INFO    ] No tests matched filter: " << filter << "\n";
    reset_color();
  }

  int failed = failures - failed_before;

  std::cout << "\n";
  set_color(failed ? Color::Red : Color::Green);
  std::cout << "[==========] " << total << " tests with filter '" << filter
            << "', " << (total - failed) << " passed, " << failed
            << " failed\n";
  reset_color();

  return failed == 0 ? 0 : 1;
}

inline int run_with_posneg_filter(const char *filter) {
  if (filter == nullptr || filter[0] == '\0') {
    return run_all();
  }
  const char *minus_pos = strchr(filter, '-');

  if (minus_pos == nullptr || minus_pos == filter) {
    return run_with_filter(filter);
  }

  std::string positive(filter, minus_pos - filter);
  const char *negative = minus_pos + 1;

  std::vector<const TestCase *> matched_tests;
  for (auto &t : registry()) {
    if (test_matches_filter(t, positive.c_str())) {
      matched_tests.push_back(&t);
    }
  }

  if (matched_tests.empty()) {
    set_color(Color::Yellow);
    std::cout << "[  INFO    ] No tests matched positive filter: " << positive
              << "\n";
    reset_color();
    return 0;
  }

  int total = 0;
  int failed_before = failures;

  set_color(Color::Cyan);
  std::cout << "[==========] Running tests with filter: " << filter << "\n";
  reset_color();

  for (const TestCase *t : matched_tests) {
    if (!test_matches_filter(*t, negative)) {
      total++;
      set_color(Color::Cyan);
      std::cout << "[ RUN      ] " << t->group << "." << t->name << "\n";
      reset_color();
      t->fn();
    }
  }

  int failed = failures - failed_before;

  std::cout << "\n";
  set_color(failed ? Color::Red : Color::Green);
  std::cout << "[==========] " << total << " tests with filter '" << filter
            << "', " << (total - failed) << " passed, " << failed
            << " failed\n";
  reset_color();

  return failed == 0 ? 0 : 1;
}

inline int default_main(int argc = 0, char **argv = nullptr) {
  if (argc > 1) {
    if (strcmp(argv[1], "--list-tests") == 0 ||
        strcmp(argv[1], "--gtest_list_tests") == 0 ||
        strcmp(argv[1], "gtest_list_tests") == 0) {
      list_tests();
      return 0;
    } else if (strncmp(argv[1], "--gtest_filter=", 15) == 0) {
      const char *filter = argv[1] + 15;
      if (filter[0] == '\0') {
        std::cerr << "Error: --gtest_filter requires a filter pattern\n";
        return 1;
      }
      return run_with_posneg_filter(filter);
    } else if (strcmp(argv[1], "--run-test") == 0) {
      if (argc <= 2) {
        std::cerr
            << "Error: --run-test requires a target (group.name or group)\n";
        return 1;
      }
      const char *target = argv[2];
      const char *group, *name;
      split_test_name(target, group, name);

      if (name == nullptr) {
        return run_group(group);
      } else {
        return run_single(group, name);
      }
    } else if (strcmp(argv[1], "--help") == 0) {
      std::cerr << "Supported args:\n";
      std::cerr << "  --list-tests                List all test cases "
                   "(GoogleTest format)\n";
      std::cerr << "  --gtest_list_tests          Same as --list-tests\n";
      std::cerr << "  --gtest_filter=PATTERN      Run tests matching pattern "
                   "(supports * and ?)\n";
      std::cerr << "  --run-test <target>         Run single test (group.name) "
                   "or group (group)\n";
      std::cerr << "  --help                      Show this help\n";
      std::cerr << "\nFilter examples:\n";
      std::cerr << "  --gtest_filter=cat.*        Run all cat tests\n";
      std::cerr
          << "  --gtest_filter=*.basic_*    Run tests with 'basic_' in name\n";
      std::cerr << "  --gtest_filter=cat.*-cat.solo  Run all cat tests except "
                   "cat.solo\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << argv[1] << "\n";
      std::cerr << "Use --help for supported arguments\n";
      return 1;
    }
  }

  return run_all();
}
}  // namespace wctest
