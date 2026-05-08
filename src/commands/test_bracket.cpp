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
 *  - File: test_bracket.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for [ command (alias for test).
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

auto constexpr BRACKET_OPTIONS =
    std::array{OPTION("-n", "", "string length is non-zero"),
               OPTION("-z", "", "string length is zero"),
               OPTION("-b", "", "file is block special"),
               OPTION("-c", "", "file is character special"),
               OPTION("-d", "", "file is a directory"),
               OPTION("-e", "", "file exists"),
               OPTION("-f", "", "file is a regular file"),
               OPTION("-g", "", "file has set-group-ID bit"),
               OPTION("-G", "", "file is owned by effective group ID"),
               OPTION("-h", "", "file is a symbolic link"),
               OPTION("-L", "", "file is a symbolic link"),
               OPTION("-k", "", "file has sticky bit"),
               OPTION("-p", "", "file is a named pipe"),
               OPTION("-r", "", "file is readable"),
               OPTION("-s", "", "file size is non-zero"),
               OPTION("-S", "", "file is a socket"),
               OPTION("-t", "", "file descriptor is a terminal"),
               OPTION("-u", "", "file has set-user-ID bit"),
               OPTION("-w", "", "file is writable"),
               OPTION("-x", "", "file is executable"),
               OPTION("-O", "", "file is owned by effective user ID"),
               OPTION("-eq", "", "integer equal"),
               OPTION("-ne", "", "integer not equal"),
               OPTION("-lt", "", "integer less than"),
               OPTION("-le", "", "integer less than or equal"),
               OPTION("-gt", "", "integer greater than"),
               OPTION("-ge", "", "integer greater than or equal"),
               OPTION("-a", "", "logical and"),
               OPTION("-and", "", "logical and"),
               OPTION("-o", "", "logical or"),
               OPTION("-or", "", "logical or"),
               OPTION("!", "", "logical not"),
               OPTION("=", "", "string equal"),
               OPTION("==", "", "string equal"),
               OPTION("!=", "", "string not equal"),
               OPTION("<", "", "string less than"),
               OPTION("<=", "", "string less than or equal"),
               OPTION(">", "", "string greater than"),
               OPTION(">=", "", "string greater than or equal")};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(bracket,
                 /* cmd_name */ "[",
                 /* cmd_synopsis */ "[ expression",
                 /* cmd_desc */
                 "Evaluate conditional expression (requires closing ]).",
                 /* examples */
                 "  [ -f /etc/passwd ]\n"
                 "  [ -n \"$var\" ]\n"
                 "  [ \"$a\" -eq \"$b\" ]\n"
                 "  [ -d /tmp ] && echo 'Directory exists'",
                 /* see_also */ "test, bash",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ BRACKET_OPTIONS) {
  // Find which operator is set
  std::string op;
  for (size_t i = 0; i < BRACKET_OPTIONS.size(); ++i) {
    if (ctx.get<bool>(BRACKET_OPTIONS[i].short_name, false) ||
        ctx.get<bool>(BRACKET_OPTIONS[i].long_name, false)) {
      op = std::string(BRACKET_OPTIONS[i].short_name);
      if (op.empty()) op = std::string(BRACKET_OPTIONS[i].long_name);
      break;
    }
  }

  // Check for closing bracket
  if (ctx.positionals.empty() || ctx.positionals.back() != "]") {
    safeErrorPrintLn("[: missing ']'");
    return 2;
  }

  // Remove the closing bracket - create a new vector with filtered arguments
  std::vector<std::string> args;
  args.reserve(ctx.positionals.size() - 1);
  for (size_t i = 0; i < ctx.positionals.size() - 1; ++i) {
    args.push_back(std::string(ctx.positionals[i]));
  }

  // Handle empty arguments
  if (args.empty() && op.empty()) {
    return 1;
  }

  // Handle single argument
  if (args.size() == 1 && op.empty()) {
    return args[0].empty() ? 1 : 0;
  }

  // Handle two arguments (op + arg)
  if (!op.empty() && args.size() == 1) {
    const std::string& arg = args[0];

    if (op == "-n") {
      return arg.empty() ? 1 : 0;
    }
    if (op == "-z") {
      return arg.empty() ? 0 : 1;
    }
    if (op == "-d") {
      std::wstring wpath = utf8_to_wstring(arg);
      DWORD attrs = GetFileAttributesW(wpath.c_str());
      return (attrs != INVALID_FILE_ATTRIBUTES &&
              (attrs & FILE_ATTRIBUTE_DIRECTORY))
                 ? 0
                 : 1;
    }
    if (op == "-f") {
      std::wstring wpath = utf8_to_wstring(arg);
      DWORD attrs = GetFileAttributesW(wpath.c_str());
      return (attrs != INVALID_FILE_ATTRIBUTES &&
              !(attrs & FILE_ATTRIBUTE_DIRECTORY))
                 ? 0
                 : 1;
    }
    if (op == "-e") {
      std::wstring wpath = utf8_to_wstring(arg);
      DWORD attrs = GetFileAttributesW(wpath.c_str());
      return (attrs != INVALID_FILE_ATTRIBUTES) ? 0 : 1;
    }

    // Default: test string is non-empty
    return arg.empty() ? 1 : 0;
  }

  // Handle three arguments (arg1 op arg2)
  if (op.empty() && args.size() == 3) {
    const std::string& a = args[0];
    const std::string& op_from_args = args[1];
    const std::string& b = args[2];

    if (op_from_args == "=" || op_from_args == "==") {
      return (a == b) ? 0 : 1;
    }
    if (op_from_args == "!=") {
      return (a != b) ? 0 : 1;
    }
    if (op_from_args == "-eq") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va == vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }
    if (op_from_args == "-ne") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va != vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }
    if (op_from_args == "-lt") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va < vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }
    if (op_from_args == "-le") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va <= vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }
    if (op_from_args == "-gt") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va > vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }
    if (op_from_args == "-ge") {
      try {
        long long va = std::stoll(a);
        long long vb = std::stoll(b);
        return (va >= vb) ? 0 : 1;
      } catch (...) {
        return 2;
      }
    }

    return 2;
  }

  return 2;
}
