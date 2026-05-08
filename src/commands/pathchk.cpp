/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: pathchk.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for pathchk command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PATHCHK_OPTIONS =
    std::array{OPTION("-p", "--portability", "check for all POSIX systems"),
               OPTION("-P", "", "check for empty names and leading \"-\"")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Check if path is valid for Windows
bool is_valid_windows_path(const std::string& path) {
  if (path.empty()) return false;

  // Check for invalid characters
  const std::string invalid_chars = "<>:\"|?*";
  for (char c : path) {
    if (invalid_chars.find(c) != std::string::npos) {
      return false;
    }
  }

  // Check for reserved names
  std::string basename = path;
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    basename = path.substr(last_slash + 1);
  }

  // Remove extension
  size_t last_dot = basename.find_last_of('.');
  if (last_dot != std::string::npos) {
    basename = basename.substr(0, last_dot);
  }

  // Check for reserved names (case-insensitive)
  std::transform(basename.begin(), basename.end(), basename.begin(), ::toupper);
  const std::vector<std::string> reserved = {"CON", "PRN", "AUX", "NUL"};
  for (const auto& res : reserved) {
    if (basename == res ||
        (basename.length() == 4 && basename.substr(0, 3) == res &&
         basename[3] >= '1' && basename[3] <= '9')) {
      return false;
    }
  }

  return true;
}

// Check for POSIX portability
bool is_posix_portable(const std::string& path) {
  if (path.empty()) return false;

  // POSIX allows only: a-z, A-Z, 0-9, '.', '_', '-'
  for (char c : path) {
    if (!std::isalnum(c) && c != '.' && c != '_' && c != '-' && c != '/') {
      return false;
    }
  }

  // Path should not start with '-'
  if (path[0] == '-') return false;

  // Check length (POSIX limits to 255 bytes)
  if (path.length() > 255) return false;

  return true;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    pathchk,
    /* cmd_name */ "pathchk",
    /* cmd_synopsis */ "pathchk [OPTION] NAME...",
    /* cmd_desc */
    "Check whether file names are valid or portable.\n"
    "Check that each file name is valid for the current filesystem,\n"
    "and optionally check for POSIX portability.",
    /* examples */
    "  pathchk myfile.txt\n"
    "  pathchk -p /tmp/test\n"
    "  pathchk -P -p filename",
    /* see_also */ "ln, mkdir",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ PATHCHK_OPTIONS) {
  bool check_portability =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--portability", false);
  bool check_leading_dash = ctx.get<bool>("-P", false);

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("pathchk: missing operand");
    safePrintLn("Try 'pathchk --help' for more information.");
    return 1;
  }

  int exit_code = 0;

  for (const auto& path : ctx.positionals) {
    bool valid = true;
    std::string error_msg;
    std::string path_str(path);

    // Check for leading dash
    if (check_leading_dash && !path_str.empty() && path_str[0] == '-') {
      valid = false;
      error_msg = "leading '-'";
    }

    // Check portability
    if (valid && check_portability && !is_posix_portable(path_str)) {
      valid = false;
      error_msg = "not POSIX portable";
    }

    // Check Windows validity
    if (valid && !is_valid_windows_path(path_str)) {
      valid = false;
      error_msg = "invalid Windows filename";
    }

    if (!valid) {
      safeErrorPrintLn("pathchk: '" + path_str + "' - " + error_msg);
      exit_code = 1;
    }
  }

  return exit_code;
}
