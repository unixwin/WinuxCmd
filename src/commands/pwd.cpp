/*
 *  Copyright  2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: pwd.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @contributors:
///   - @contributor1 arookieofc <2128194521@qq.com>
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for pwd.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright  2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

/**
 * @brief PWD command options definition
 *
 * This array defines all the options supported by the pwd command.
 * Each option is described with its short form, long form, and description.
 *
 * @par Options:
 *
 * - @a -L, @a --logical: Use PWD from environment, even if it contains symlinks
 * [IMPLEMENTED]
 * - @a -P, @a --physical: Avoid all symlinks [IMPLEMENTED]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PWD_OPTIONS =
    std::array{OPTION("-L", "--logical",
                      "use PWD from environment, even if it contains symlinks"),
               OPTION("-P", "--physical", "avoid all symlinks")};

// ======================================================
// Pipeline components
// ======================================================
namespace pwd_pipeline {
namespace cp = core::pipeline;

// ----------------------------------------------
// 1. Get current working directory
// ----------------------------------------------
auto get_current_directory(const CommandContext<PWD_OPTIONS.size()>& ctx)
    -> cp::Result<std::string> {
  bool physical = ctx.get<bool>("--physical", false);
  physical |= ctx.get<bool>("-P", false);

  // Get current directory using Windows API
  DWORD bufferSize = GetCurrentDirectoryW(0, NULL);
  if (bufferSize == 0) {
    return std::unexpected("cannot get current directory");
  }

  std::wstring wCurrentDir(bufferSize, L'\0');
  DWORD result = GetCurrentDirectoryW(bufferSize, &wCurrentDir[0]);
  if (result == 0 || result >= bufferSize) {
    return std::unexpected("cannot get current directory");
  }

  // Remove null terminator
  wCurrentDir.resize(result);

  // Convert to UTF-8 using utility function
  std::string currentDir = wstring_to_utf8(wCurrentDir);

  return currentDir;
}

// ----------------------------------------------
// 2. Print current directory
// ----------------------------------------------
auto print_directory(const std::string& path) -> cp::Result<bool> {
  safePrint(path);
  safePrint("\n");
  return true;
}

// ----------------------------------------------
// 3. Main pipeline
// ----------------------------------------------
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  return get_current_directory(ctx).and_then(
      [](const std::string& path) { return print_directory(path); });
}

}  // namespace pwd_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    pwd,
    /* name */
    "pwd",

    /* synopsis */
    "print name of current/working directory",

    /* description */
    "Print the full filename of the current working directory.\n"
    "\n"
    "The -L option uses PWD from environment, even if it contains symlinks.\n"
    "The -P option avoids all symlinks.",

    /* examples */
    "  pwd                  Print the current working directory\n"
    "  pwd -L               Print logical current directory\n"
    "  pwd -P               Print physical current directory",

    /* see also */
    "cd(1), ls(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright  2026 WinuxCmd",

    /* options */
    PWD_OPTIONS) {
  using namespace pwd_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"pwd");
    return 1;
  }

  return *result ? 0 : 1;
}
