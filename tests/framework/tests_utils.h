// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#ifndef TESTS_UTILS_H
#define TESTS_UTILS_H
namespace wctest {
/**
 * @brief Console color enumeration for test output
 *
 * Defines standard colors used for different types of test output
 * to improve readability and visual feedback.
 */
enum class Color { Default, Red, Green, Yellow, Cyan };

/**
 * @brief Set console text color
 *
 * Platform-specific implementation for setting console colors.
 * Uses Windows Console API on Windows, ANSI escape codes elsewhere.
 *
 * @param c Desired color
 */
inline void set_color(Color c) {
#ifdef _WIN32
  static HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

  switch (c) {
    case Color::Red:
      attr = FOREGROUND_RED | FOREGROUND_INTENSITY;
      break;
    case Color::Green:
      attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
      break;
    case Color::Yellow:
      attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
      break;
    case Color::Cyan:
      attr = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
      break;
    default:
      break;
  }
  SetConsoleTextAttribute(h, attr);
#else
  switch (c) {
    case Color::Red:
      std::cout << "\033[31m";
      break;
    case Color::Green:
      std::cout << "\033[32m";
      break;
    case Color::Yellow:
      std::cout << "\033[33m";
      break;
    case Color::Cyan:
      std::cout << "\033[36m";
      break;
    default:
      std::cout << "\033[0m";
      break;
  }
#endif
}

/**
 * @brief Reset console color to default
 */
inline void reset_color() { set_color(Color::Default); }

// ============================
// Concepts: Printable Types
// ============================

/**
 * @brief Concept for types that can be streamed to std::ostream
 *
 * Used to constrain template parameters that need to support
 * output streaming operations.
 */
template <typename T>
concept Streamable = requires(std::ostream &os, T v) { os << v; };

// ============================
// Per-Test Temporary Directory Management
// ============================

/**
 * @brief Get reference to current test's temporary directory holder
 *
 * Uses thread-local storage to provide isolated temporary directories
 * for concurrent test execution.
 *
 * @return std::unique_ptr<TempDir>& Reference to temporary directory pointer
 */
inline std::unique_ptr<TempDir> &current_temp_holder() {
  static std::unique_ptr<TempDir> dir;
  return dir;
}

/**
 * @brief Get reference to current test's temporary directory
 *
 * @return const TempDir& Reference to current temporary directory
 */
inline const TempDir &current_temp() { return *current_temp_holder(); }

/**
 * @brief Get wide string path to current temporary directory
 *
 * @return std::wstring Path to temporary directory
 */
inline std::wstring current_temp_path() {
  return current_temp_holder()->wpath();
}

/**
 * @brief Create new temporary directory for current test
 *
 * @param test_name Name of current test (used for directory naming)
 */
inline void make_temp_dir(const char * /*test_name*/) {
  current_temp_holder() = std::make_unique<TempDir>();
}

/**
 * @brief Clean up current test's temporary directory
 */
inline void clear_temp_dir() { current_temp_holder().reset(); }
}  // namespace wctest

#endif  //! TESTS_UTILS_H
