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

auto constexpr PATHCHK_OPTIONS = std::array{
    OPTION("-p", "--portability", "check for all POSIX systems"),
    OPTION("-P", "--posix", "check for empty names and leading \"-\"")};

// ======================================================
// Helper functions
// ======================================================

namespace {
constexpr size_t kPosixPathMax = 256;
constexpr size_t kPosixNameMax = 14;
constexpr size_t kWindowsComponentMax = 255;

auto split_components(const std::string& path, bool windows_separators)
    -> std::vector<std::string> {
  std::vector<std::string> components;
  std::string current;
  for (char c : path) {
    bool separator = c == '/' || (windows_separators && c == '\\');
    if (separator) {
      components.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  components.push_back(current);
  return components;
}

bool has_empty_or_leading_dash_component(const std::string& path) {
  if (path.empty()) return true;

  auto components = split_components(path, true);
  for (size_t i = 0; i < components.size(); ++i) {
    const auto& component = components[i];
    if (component.empty()) {
      bool leading_root =
          i == 0 && !path.empty() && (path[0] == '/' || path[0] == '\\');
      if (!leading_root) return true;
      continue;
    }
    if (component.front() == '-') return true;
  }
  return false;
}

// Check if path is valid for Windows
auto windows_path_error(const std::string& path) -> std::optional<std::string> {
  if (path.empty()) return "empty file name";

  // Check for invalid characters
  const std::string invalid_chars = "<>:\"|?*";
  for (char c : path) {
    if (invalid_chars.find(c) != std::string::npos) {
      return "invalid Windows filename";
    }
  }

  const std::array<std::string_view, 22> reserved = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

  for (auto component : split_components(path, true)) {
    if (component.empty()) continue;
    if (component.size() > kWindowsComponentMax) return "file name too long";
    while (!component.empty() &&
           (component.back() == '.' || component.back() == ' ')) {
      component.pop_back();
    }
    size_t last_dot = component.find('.');
    if (last_dot != std::string::npos) component.resize(last_dot);

    std::transform(
        component.begin(), component.end(), component.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (auto name : reserved) {
      if (component == name) return "invalid Windows filename";
    }
  }

  return std::nullopt;
}

auto posix_portable_error(const std::string& path)
    -> std::optional<std::string> {
  if (path.empty()) return "empty file name";
  if (path.size() >= kPosixPathMax) return "path name too long";

  for (char c : path) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (!std::isalnum(uc) && c != '.' && c != '_' && c != '-' && c != '/') {
      return "contains nonportable character";
    }
  }

  for (const auto& component : split_components(path, false)) {
    if (component.empty()) continue;
    if (component.size() > kPosixNameMax) return "file name too long";
  }

  return std::nullopt;
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
  bool check_leading_dash = ctx.get<bool>("-P", false) ||
                            ctx.get<bool>("--posix", false) ||
                            ctx.get<bool>("--portability", false);
  if (!check_portability && !check_leading_dash &&
      std::getenv("POSIXLY_CORRECT") == nullptr) {
    check_leading_dash = true;
  }

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("pathchk: missing operand");
    safePrintLn("Try 'pathchk --help' for more information.");
    return 1;
  }

  int exit_code = 0;

  for (const auto& path : ctx.positionals) {
    std::string path_str(path);
    std::optional<std::string> error_msg;

    if (check_leading_dash && has_empty_or_leading_dash_component(path_str)) {
      error_msg = "empty file name or leading '-'";
    }

    if (!error_msg && check_portability) {
      error_msg = posix_portable_error(path_str);
    }

    if (!error_msg) {
      error_msg = windows_path_error(path_str);
    }

    if (error_msg) {
      safeErrorPrintLn("pathchk: '" + path_str + "' - " + *error_msg);
      exit_code = 1;
    }
  }

  return exit_code;
}
